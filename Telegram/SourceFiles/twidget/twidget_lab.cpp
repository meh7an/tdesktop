/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_lab.h"

#include "base/options.h"
#include "lang/lang_keys.h"
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
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_twidget.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Twidget {
namespace {

constexpr auto kPreviewWidth = 320.;

class Preview final : public Ui::RpWidget {
public:
	Preview(
		QWidget *parent,
		std::shared_ptr<const Document> document,
		not_null<const Ui::ChatStyle*> st,
		not_null<const RealTextMeasurer*> measurer,
		bool outgoing);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const std::shared_ptr<const Document> _document;
	const not_null<const Ui::ChatStyle*> _st;
	const not_null<const RealTextMeasurer*> _measurer;
	const bool _outgoing = false;
	Layout _layout;
	QRect _bubble;

};

Preview::Preview(
	QWidget *parent,
	std::shared_ptr<const Document> document,
	not_null<const Ui::ChatStyle*> st,
	not_null<const RealTextMeasurer*> measurer,
	bool outgoing)
: RpWidget(parent)
, _document(std::move(document))
, _st(st)
, _measurer(measurer)
, _outgoing(outgoing) {
	_st->paletteChanged() | rpl::on_next([=] {
		update();
	}, lifetime());
}

int Preview::resizeGetHeight(int newWidth) {
	const auto &padding = st::msgPadding;
	const auto unit = _measurer->unit();
	const auto inner = newWidth - padding.left() - padding.right();
	_layout = LayOut(
		*_document,
		std::min(kPreviewWidth, std::max(inner, 0) / unit),
		false,
		*_measurer);
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
	});
}

[[nodiscard]] std::shared_ptr<const Document> LoadFixture(
		const QString &name,
		bool dark) {
	auto file = QFile(u":/tgw/fixtures/envelopes/%1.json"_q.arg(name));
	if (!file.open(QIODevice::ReadOnly)) {
		return nullptr;
	}
	const auto instance = LoadInstance(file.readAll());
	const auto resolved = instance
		? Resolve(instance.get(), FixedEnv(dark, false))
		: std::nullopt;
	auto document = resolved
		? ParseResolved(resolved->tree)
		: std::nullopt;
	return document
		? std::make_shared<const Document>(std::move(*document))
		: nullptr;
}

void FixturesBox(
		not_null<Ui::GenericBox*> box,
		not_null<const Ui::ChatStyle*> st) {
	box->setTitle(rpl::single(u"Live widgets fixtures"_q));
	box->setWidth(st::twidgetLabWidth);

	const auto measurer = box->lifetime().make_state<RealTextMeasurer>();
	const auto files = QDir(u":/tgw/fixtures/envelopes"_q).entryList(
		QDir::Files,
		QDir::Name);
	for (const auto &file : files) {
		const auto name = file.chopped(5);
		box->addRow(
			object_ptr<Ui::FlatLabel>(box, name, st::boxLabel),
			st::twidgetLabNamePadding);
		const auto document = LoadFixture(name, st->dark());
		if (!document) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				u"Failed to resolve."_q,
				st::boxLabel));
			continue;
		}
		for (const auto outgoing : { false, true }) {
			box->addRow(object_ptr<Preview>(
				box,
				document,
				st,
				measurer,
				outgoing));
		}
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
		const auto st = controller->chatStyle();
		box->addLeftButton(rpl::single(u"Fixtures"_q), [=] {
			window->show(Box(FixturesBox, st));
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
