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
namespace {

struct EntityName {
	QStringView name;
	EntityType type;
};

[[nodiscard]] EntityType ConvertType(const QString &name) {
	static constexpr auto kTypes = std::array<EntityName, 16>{ {
		{ u"bold", EntityType::Bold },
		{ u"italic", EntityType::Italic },
		{ u"underline", EntityType::Underline },
		{ u"strikethrough", EntityType::StrikeOut },
		{ u"spoiler", EntityType::Spoiler },
		{ u"code", EntityType::Code },
		{ u"pre", EntityType::Code },
		{ u"url", EntityType::Url },
		{ u"text_link", EntityType::CustomUrl },
		{ u"text_mention", EntityType::CustomUrl },
		{ u"email", EntityType::Email },
		{ u"phone_number", EntityType::Phone },
		{ u"mention", EntityType::Mention },
		{ u"hashtag", EntityType::Hashtag },
		{ u"cashtag", EntityType::Cashtag },
		{ u"bot_command", EntityType::BotCommand },
	} };
	for (const auto &entry : kTypes) {
		if (entry.name == name) {
			return entry.type;
		}
	}
	return EntityType::Invalid;
}

[[nodiscard]] TextWithEntities Convert(const TextData &text) {
	auto result = TextWithEntities{ .text = text.text };
	const auto size = int(text.text.size());
	for (const auto &entity : text.entities) {
		const auto type = ConvertType(entity.type);
		const auto from = std::clamp(entity.offset, 0, size);
		const auto till = std::clamp(entity.offset + entity.length, from, size);
		if (type != EntityType::Invalid && till > from) {
			result.entities.push_back(
				EntityInText(type, from, till - from, entity.url));
		}
	}
	return result;
}

} // namespace

RealTextMeasurer::RealTextMeasurer()
: _unit(style::ConvertScaleExact(1.))
, _mono(std::make_unique<style::TextStyle>(st::twidgetMonoStyle)) {
	_mono->font = _mono->font->monospace();
}

RealTextMeasurer::~RealTextMeasurer() = default;

float64 RealTextMeasurer::unit() const {
	return _unit;
}

int RealTextMeasurer::pixels(float64 width) const {
	return std::max(int(std::floor(width * _unit)), 1);
}

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

Ui::Text::String RealTextMeasurer::prepare(
		const TextData &text,
		float64 width,
		bool rtl) const {
	const auto options = TextParseOptions{
		TextParseMultiline,
		0,
		0,
		rtl ? Qt::RightToLeft : Qt::LeftToRight,
	};
	return Ui::Text::String(
		textStyle(text.style),
		Convert(text),
		options,
		pixels(width));
}

float64 RealTextMeasurer::lineHeight(TextStyle style) const {
	const auto &st = textStyle(style);
	return (st.lineHeight ? st.lineHeight : st.font->height) / _unit;
}

float64 RealTextMeasurer::hairline() const {
	return 1. / (_unit * style::DevicePixelRatio());
}

std::vector<MeasuredLine> RealTextMeasurer::lines(
		const TextData &text,
		float64 width) const {
	const auto string = prepare(text, width, false);
	auto widths = string.countLineWidths(pixels(width));
	if (text.maxLines && int(widths.size()) > *text.maxLines) {
		widths.resize(*text.maxLines);
	}
	auto result = std::vector<MeasuredLine>();
	result.reserve(widths.size());
	for (const auto lineWidth : widths) {
		result.push_back({ .width = lineWidth / _unit });
	}
	return result;
}

} // namespace Twidget
