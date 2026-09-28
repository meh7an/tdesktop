/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "twidget/twidget_model.h"

#include <QtCore/QJsonObject>

namespace Twidget {

struct MeasuredLine {
	QString text;
	float64 width = 0.;
};

class TextMeasurer {
public:
	virtual ~TextMeasurer() = default;

	[[nodiscard]] virtual float64 lineHeight(TextStyle style) const = 0;
	[[nodiscard]] virtual float64 hairline() const = 0;
	[[nodiscard]] virtual std::vector<MeasuredLine> lines(
		const QString &text,
		TextStyle style,
		float64 width,
		std::optional<int> maxLines) const = 0;

};

class FakeTextMeasurer final : public TextMeasurer {
public:
	[[nodiscard]] float64 lineHeight(TextStyle style) const override;
	[[nodiscard]] float64 hairline() const override;
	[[nodiscard]] std::vector<MeasuredLine> lines(
		const QString &text,
		TextStyle style,
		float64 width,
		std::optional<int> maxLines) const override;

};

struct LaidLine {
	QString text;
	float64 x = 0.;
	float64 y = 0.;
};

struct LaidNode {
	not_null<const Node*> node;
	float64 x = 0.;
	float64 y = 0.;
	float64 w = 0.;
	float64 h = 0.;
	std::optional<std::vector<LaidLine>> lines;
	float64 fieldTop = 0.;
	int fieldLabels = 0;
	float64 scale = 0.;
	std::vector<LaidNode> children;
};

struct Layout {
	float64 width = 0.;
	float64 height = 0.;
	std::optional<LaidNode> root;
};

[[nodiscard]] Layout LayOut(
	const Document &document,
	float64 available,
	bool rtl,
	const TextMeasurer &measurer);

[[nodiscard]] QJsonObject SerializeFrames(const Layout &layout);

} // namespace Twidget
