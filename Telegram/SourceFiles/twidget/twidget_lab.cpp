/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_lab.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/debug_log.h"
#include "base/options.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_thread.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "twidget/twidget_api.h"
#include "twidget/twidget_conformance.h"
#include "twidget/twidget_layout.h"
#include "twidget/twidget_paint.h"
#include "twidget/twidget_text.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_twidget.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Twidget {
namespace {

constexpr auto kMinWidth = 240;
constexpr auto kMaxWidth = 480;
constexpr auto kDefaultWidth = 320;
constexpr auto kMessageLimit = 4096;

struct LabState {
	rpl::variable<bool> rtl = false;
	rpl::variable<bool> frozen = false;
	rpl::variable<bool> visible = false;
	rpl::variable<int> width = kDefaultWidth;
};

class Fixture final {
public:
	Fixture(
		QString name,
		QByteArray envelope,
		not_null<const Ui::ChatStyle*> st,
		not_null<LabState*> state);

	[[nodiscard]] const QString &name() const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] std::shared_ptr<const Document> document() const;
	[[nodiscard]] rpl::producer<> updates() const;
	[[nodiscard]] TextWithEntities message() const;

private:
	[[nodiscard]] Env env() const;
	void resolve();

	const QString _name;
	const QByteArray _envelope;
	const not_null<const Ui::ChatStyle*> _st;
	const not_null<LabState*> _state;
	const InstanceHandle _instance;
	QByteArray _tree;
	std::shared_ptr<const Document> _document;
	base::Timer _timer;
	rpl::event_stream<> _updates;
	rpl::lifetime _lifetime;

};

class Preview final : public Ui::RpWidget {
public:
	Preview(
		QWidget *parent,
		not_null<Fixture*> fixture,
		not_null<LabState*> state,
		not_null<const Ui::ChatStyle*> st,
		not_null<const RealTextMeasurer*> measurer,
		bool outgoing);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<Fixture*> _fixture;
	const not_null<LabState*> _state;
	const not_null<const Ui::ChatStyle*> _st;
	const not_null<const RealTextMeasurer*> _measurer;
	const bool _outgoing = false;
	std::shared_ptr<const Document> _document;
	Layout _layout;
	QRect _bubble;
	bool _rtl = false;

};

Fixture::Fixture(
	QString name,
	QByteArray envelope,
	not_null<const Ui::ChatStyle*> st,
	not_null<LabState*> state)
: _name(std::move(name))
, _envelope(std::move(envelope))
, _st(st)
, _state(state)
, _instance(LoadInstance(_envelope))
, _timer([=] { resolve(); }) {
	rpl::combine(
		_state->rtl.value(),
		_state->frozen.value()
	) | rpl::to_empty | rpl::on_next([=] {
		resolve();
	}, _lifetime);

	_state->visible.changes() | rpl::on_next([=](bool visible) {
		if (visible) {
			resolve();
		} else {
			_timer.cancel();
		}
	}, _lifetime);
}

const QString &Fixture::name() const {
	return _name;
}

bool Fixture::loaded() const {
	return (_instance != nullptr);
}

std::shared_ptr<const Document> Fixture::document() const {
	return _document;
}

rpl::producer<> Fixture::updates() const {
	return _updates.events();
}

TextWithEntities Fixture::message() const {
	const auto summary = _instance
		? Summary(_instance.get(), env()).value_or(QString())
		: QString();
	const auto fallback = summary.isEmpty() ? u"Live widget"_q : summary;
	const auto envelope = QString::fromUtf8(_envelope).trimmed();
	auto result = TextWithEntities{ .text = fallback + u'\n' + envelope };
	result.entities.push_back(EntityInText(
		EntityType::Pre,
		int(fallback.size()) + 1,
		int(envelope.size()),
		u"tgw"_q));
	return result;
}

Env Fixture::env() const {
	const auto dark = _st->dark();
	const auto rtl = _state->rtl.current();
	auto result = _state->frozen.current()
		? FixedEnv(dark, rtl)
		: CurrentEnv(dark);
	result.rtl = rtl;
	return result;
}

void Fixture::resolve() {
	_timer.cancel();
	if (!_instance) {
		return;
	}
	const auto env = this->env();
	const auto frozen = _state->frozen.current();
	const auto resolved = Resolve(_instance.get(), env);
	LOG(("Twidget Lab: resolved %1, rtl %2, frozen %3."
		).arg(_name
		).arg(env.rtl ? u"on"_q : u"off"_q
		).arg(frozen ? u"on"_q : u"off"_q));
	if (!resolved) {
		if (_document) {
			_tree = QByteArray();
			_document = nullptr;
			_updates.fire({});
		}
		return;
	} else if (resolved->tree != _tree) {
		auto document = ParseResolved(resolved->tree);
		_tree = resolved->tree;
		_document = document
			? std::make_shared<const Document>(std::move(*document))
			: nullptr;
		_updates.fire({});
	}
	if (resolved->nextWakeupMs && !frozen && _state->visible.current()) {
		const auto now = CurrentEnv(env.dark).nowMs;
		_timer.callOnce(std::max(*resolved->nextWakeupMs - now, int64()));
	}
}

Preview::Preview(
	QWidget *parent,
	not_null<Fixture*> fixture,
	not_null<LabState*> state,
	not_null<const Ui::ChatStyle*> st,
	not_null<const RealTextMeasurer*> measurer,
	bool outgoing)
: RpWidget(parent)
, _fixture(fixture)
, _state(state)
, _st(st)
, _measurer(measurer)
, _outgoing(outgoing) {
	rpl::merge(
		_fixture->updates(),
		_state->rtl.changes() | rpl::to_empty,
		_state->width.changes() | rpl::to_empty
	) | rpl::on_next([=] {
		resizeToWidth(width());
		update();
	}, lifetime());

	_st->paletteChanged() | rpl::on_next([=] {
		update();
	}, lifetime());
}

int Preview::resizeGetHeight(int newWidth) {
	auto document = _fixture->document();
	if (!document) {
		_layout = Layout();
		_document = nullptr;
		_bubble = QRect();
		return 0;
	}
	const auto &padding = st::msgPadding;
	const auto unit = _measurer->unit();
	const auto inner = newWidth - padding.left() - padding.right();
	_rtl = _state->rtl.current();
	_layout = LayOut(
		*document,
		std::min(float64(_state->width.current()), std::max(inner, 0) / unit),
		_rtl,
		*_measurer);
	_document = std::move(document);
	const auto width = int(std::ceil(_layout.width * unit))
		+ padding.left()
		+ padding.right();
	const auto height = int(std::ceil(_layout.height * unit))
		+ padding.top()
		+ padding.bottom();
	_bubble = QRect(_outgoing ? (newWidth - width) : 0, 0, width, height);
	return height + st::msgShadow + st::twidgetLabBubbleSkip;
}

void Preview::paintEvent(QPaintEvent *e) {
	if (_bubble.isEmpty()) {
		return;
	}
	auto p = QPainter(this);
	const auto &message = _st->messageStyle(_outgoing, false);
	const auto radius = Ui::BubbleRadiusLarge();
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(message.msgShadow);
		p.drawRoundedRect(
			_bubble.translated(0, st::msgShadow),
			radius,
			radius);
		p.setBrush(message.msgBg);
		p.drawRoundedRect(_bubble, radius, radius);
	}
	p.translate(_bubble.topLeft()
		+ QPoint(st::msgPadding.left(), st::msgPadding.top()));
	const auto palette = Palette(_st, _outgoing);
	Paint(p, _layout, {
		.measurer = _measurer,
		.palette = &palette,
		.rtl = _rtl,
	});
}

void AddControls(
		not_null<Ui::VerticalLayout*> container,
		not_null<LabState*> state) {
	const auto addCheckbox = [&](
			const QString &text,
			rpl::variable<bool> LabState::*field) {
		const auto checkbox = container->add(
			object_ptr<Ui::Checkbox>(
				container,
				text,
				(state->*field).current(),
				st::defaultBoxCheckbox),
			st::twidgetLabControlPadding);
		checkbox->checkedChanges(
		) | rpl::on_next([=](bool checked) {
			state->*field = checked;
		}, checkbox->lifetime());
	};
	addCheckbox(u"Right to left"_q, &LabState::rtl);
	addCheckbox(u"Frozen time"_q, &LabState::frozen);

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			state->width.value() | rpl::map([](int width) {
				return u"Width: %1"_q.arg(width);
			}),
			st::boxLabel),
		st::twidgetLabControlPadding);
	const auto slider = container->add(
		object_ptr<Ui::MediaSliderWheelless>(container, st::settingsScale),
		st::twidgetLabSliderPadding);
	slider->resize(slider->width(), st::settingsScale.seekSize.height());
	slider->setPseudoDiscrete(
		kMaxWidth - kMinWidth + 1,
		[](int index) { return kMinWidth + index; },
		state->width.current(),
		[=](int width) { state->width = width; });
}

[[nodiscard]] bool RefuseLong(
		not_null<Window::Controller*> window,
		const QString &name,
		const TextWithEntities &message) {
	const auto size = int(message.text.size());
	if (size <= kMessageLimit) {
		return false;
	}
	LOG(("Twidget Lab: refused to send %1, %2 UTF-16 code units."
		).arg(name
		).arg(size));
	window->showToast(u"Too long to send: %1 of %2 UTF-16 code units."_q
		.arg(size)
		.arg(kMessageLimit));
	return true;
}

void SendToChat(
		not_null<Window::SessionController*> controller,
		not_null<Fixture*> fixture) {
	const auto window = &controller->window();
	const auto name = fixture->name();
	const auto message = fixture->message();
	if (RefuseLong(window, name, message)) {
		return;
	}
	const auto weak = base::make_weak(window);
	Window::ShowChooseRecipientBox(controller, [=](
			not_null<Data::Thread*> thread) {
		auto send = Api::MessageToSend(Api::SendAction(thread));
		send.textWithTags = TextWithTags{
			message.text,
			TextUtilities::ConvertEntitiesToTextTags(message.entities),
		};
		send.webPage.removed = true;
		send.action.clearDraft = false;
		thread->session().api().sendMessage(std::move(send));
		LOG(("Twidget Lab: sent %1, %2 UTF-16 code units."
			).arg(name
			).arg(message.text.size()));
		if (const auto strong = weak.get()) {
			strong->showToast(u"Sent %1."_q.arg(name));
		}
		return true;
	}, rpl::single(u"Send %1 to"_q.arg(name)));
}

void FixturesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(rpl::single(u"Live widgets fixtures"_q));
	box->setWidth(st::twidgetLabWidth);

	const auto st = controller->chatStyle();
	const auto state = box->lifetime().make_state<LabState>();
	const auto measurer = box->lifetime().make_state<RealTextMeasurer>();
	state->visible = box->shownValue();
	AddControls(
		box->setPinnedToTopContent(object_ptr<Ui::VerticalLayout>(box)),
		state);

	const auto files = QDir(u":/tgw/fixtures/envelopes"_q).entryList(
		QDir::Files,
		QDir::Name);
	for (const auto &file : files) {
		const auto name = file.chopped(5);
		auto source = QFile(u":/tgw/fixtures/envelopes/"_q + file);
		const auto fixture = box->lifetime().make_state<Fixture>(
			name,
			(source.open(QIODevice::ReadOnly)
				? source.readAll()
				: QByteArray()),
			st,
			state);
		box->addRow(
			object_ptr<Ui::FlatLabel>(box, name, st::boxLabel),
			st::twidgetLabNamePadding);
		if (!fixture->loaded()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				u"Failed to load."_q,
				st::boxLabel));
			continue;
		}
		for (const auto outgoing : { false, true }) {
			box->addRow(object_ptr<Preview>(
				box,
				fixture,
				state,
				st,
				measurer,
				outgoing));
		}
		const auto send = box->addRow(
			object_ptr<Ui::LinkButton>(box, u"Send to chat"_q),
			st::twidgetLabSendPadding,
			style::al_right);
		send->setClickedCallback([=] {
			SendToChat(controller, fixture);
		});
	}
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

} // namespace

void LabBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::Controller*> window) {
	box->setTitle(rpl::single(u"Live widgets lab"_q));
	box->setWidth(st::boxWideWidth);

	const auto results = RunConformance();
	const auto passed = ranges::count_if(
		results,
		&ConformanceResult::passed);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		u"Frame goldens: %1 of %2 pass."_q.arg(passed).arg(results.size()),
		st::boxLabel));
	for (const auto &result : results) {
		auto text = (result.passed ? u"Passed: "_q : u"Failed: "_q)
			+ result.name;
		if (!result.problem.isEmpty()) {
			text += u"\n"_q + result.problem;
		}
		box->addRow(object_ptr<Ui::FlatLabel>(box, text, st::boxLabel));
	}
	if (const auto controller = window->sessionController()) {
		box->addLeftButton(rpl::single(u"Fixtures"_q), [=] {
			window->show(Box(FixturesBox, controller));
		});
	}
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

QString AddLabButton(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> query) {
	const auto name = u"Live widgets lab"_q;
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)),
		style::margins(),
		style::al_justify);
	const auto inner = wrap->entity();
	const auto button = inner->add(object_ptr<Ui::SettingsButton>(
		inner,
		rpl::single(name),
		st::settingsButtonNoIcon));
	button->setClickedCallback([=] {
		window->show(Box(LabBox, window));
	});

	const auto option = &base::options::lookup<bool>(kOptionLiveWidgets);
	auto enabled = rpl::single(
		rpl::empty
	) | rpl::then(
		option->changes()
	) | rpl::map([=] {
		return option->value();
	});
	const auto terms = Settings::SearchWords(name);
	rpl::combine(
		std::move(enabled),
		std::move(query)
	) | rpl::on_next([=](bool enabled, const QString &text) {
		const auto matches = Settings::MatchesWords(
			terms,
			Settings::SearchWords(text));
		wrap->toggle(enabled && matches, anim::type::instant);
	}, wrap->lifetime());

	return name;
}

} // namespace Twidget
