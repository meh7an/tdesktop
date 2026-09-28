/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "twidget/twidget_model.h"

namespace style {
struct TextPalette;
} // namespace style

namespace Ui {
class ChatStyle;
} // namespace Ui

namespace Twidget {

struct Layout;
class RealTextMeasurer;

class Palette final {
public:
	Palette(not_null<const Ui::ChatStyle*> st, bool outgoing);

	[[nodiscard]] QColor token(ColorToken token) const;
	[[nodiscard]] std::optional<QColor> resolve(const Color &color) const;
	[[nodiscard]] std::optional<QColor> resolve(
		const std::optional<Color> &color) const;
	[[nodiscard]] const style::TextPalette &textPalette() const;

private:
	std::array<QColor, kColorTokenCount> _colors;
	not_null<const style::TextPalette*> _textPalette;

};

struct PaintContext {
	not_null<const RealTextMeasurer*> measurer;
	not_null<const Palette*> palette;
	bool rtl = false;
};

void Paint(QPainter &p, const Layout &layout, const PaintContext &context);

} // namespace Twidget
