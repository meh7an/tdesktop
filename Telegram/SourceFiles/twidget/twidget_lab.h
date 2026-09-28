/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
class VerticalLayout;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace Twidget {

void LabBox(not_null<Ui::GenericBox*> box);

[[nodiscard]] QString AddLabButton(
	not_null<Window::Controller*> window,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> query);

} // namespace Twidget
