/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_text.h"

#include "ui/style/style_core_scale.h"
#include "ui/text/text.h"
#include "styles/style_chat.h"
#include "styles/style_twidget.h"

namespace Twidget {

RealTextMeasurer::RealTextMeasurer()
: _unit(style::ConvertScaleExact(1.))
, _mono(std::make_unique<style::TextStyle>(st::twidgetMonoStyle)) {
	_mono->font = _mono->font->monospace();
}

RealTextMeasurer::~RealTextMeasurer() = default;

const style::TextStyle &RealTextMeasurer::textStyle(TextStyle style) const {
	switch (style) {
	case TextStyle::Caption: return st::twidgetCaptionStyle;
	case TextStyle::Title: return st::twidgetTitleStyle;
	case TextStyle::Display: return st::twidgetDisplayStyle;
	case TextStyle::Mono: return *_mono;
	case TextStyle::Body: break;
	}
	return st::messageTextStyle;
}

float64 RealTextMeasurer::lineHeight(TextStyle style) const {
	const auto &st = textStyle(style);
	return (st.lineHeight ? st.lineHeight : st.font->height) / _unit;
}

float64 RealTextMeasurer::hairline() const {
	return 1. / (_unit * style::DevicePixelRatio());
}

std::vector<MeasuredLine> RealTextMeasurer::lines(
		const QString &text,
		TextStyle style,
		float64 width,
		std::optional<int> maxLines) const {
	const auto available = std::max(int(std::floor(width * _unit)), 1);
	const auto string = Ui::Text::String(
		textStyle(style),
		text,
		kPlainTextOptions,
		available);
	auto widths = string.countLineWidths(available);
	if (maxLines && int(widths.size()) > *maxLines) {
		widths.resize(*maxLines);
	}
	auto result = std::vector<MeasuredLine>();
	result.reserve(widths.size());
	for (const auto lineWidth : widths) {
		result.push_back({ .width = lineWidth / _unit });
	}
	return result;
}

} // namespace Twidget
