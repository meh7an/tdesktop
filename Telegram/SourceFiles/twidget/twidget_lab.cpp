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
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"

#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Twidget {

void LabBox(not_null<Ui::GenericBox*> box) {
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
		window->show(Box(LabBox));
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
