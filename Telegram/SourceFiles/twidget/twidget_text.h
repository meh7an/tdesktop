/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "twidget/twidget_layout.h"

namespace style {
struct TextStyle;
} // namespace style

namespace Twidget {

class RealTextMeasurer final : public TextMeasurer {
public:
	RealTextMeasurer();
	~RealTextMeasurer();

	[[nodiscard]] const style::TextStyle &textStyle(TextStyle style) const;

	[[nodiscard]] float64 lineHeight(TextStyle style) const override;
	[[nodiscard]] float64 hairline() const override;
	[[nodiscard]] std::vector<MeasuredLine> lines(
		const QString &text,
		TextStyle style,
		float64 width,
		std::optional<int> maxLines) const override;

private:
	float64 _unit = 1.;
	std::unique_ptr<style::TextStyle> _mono;

};

} // namespace Twidget
