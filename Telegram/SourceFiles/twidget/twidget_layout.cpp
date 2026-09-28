/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_layout.h"

#include "base/algorithm.h"

#include <QtCore/QJsonArray>

#include <cmath>
#include <string>
#include <string_view>

namespace Twidget {
namespace {

constexpr auto kFramesFormat = 1;
constexpr auto kDividerMargin = 4.;
constexpr auto kFieldGap = 4.;
constexpr auto kFieldPadding = 10.;
constexpr auto kChartHeight = 0.6;
constexpr auto kSlack = 1e-6;
constexpr auto kEllipsis = char32_t(0x2026);

struct Metrics {
	float64 advance = 0.;
	float64 lineHeight = 0.;
};

struct Sides {
	float64 top = 0.;
	float64 left = 0.;
	float64 bottom = 0.;
	float64 right = 0.;
};

class Engine final {
public:
	Engine(bool rtl, not_null<const TextMeasurer*> measurer);

	[[nodiscard]] LaidNode layOut(const Node &node, float64 w) const;

private:
	[[nodiscard]] LaidNode column(const Node &node, float64 w) const;
	[[nodiscard]] LaidNode row(const Node &node, float64 w) const;
	[[nodiscard]] LaidNode scrollX(const Node &node, float64 w) const;
	[[nodiscard]] LaidNode canvas(const Node &node, float64 w) const;
	[[nodiscard]] LaidNode frame(const Node &frame, float64 scale) const;
	[[nodiscard]] LaidNode text(const Node &node, float64 w) const;
	[[nodiscard]] LaidNode textField(const Node &node, float64 w) const;
	[[nodiscard]] std::vector<LaidLine> lines(
		const QString &text,
		float64 width,
		TextStyle style,
		std::optional<int> maxLines,
		Align align) const;
	void sideBySide(
		std::vector<LaidNode> &laid,
		float64 w,
		float64 left,
		float64 right,
		float64 gap) const;
	[[nodiscard]] Sides padding(const Node &node) const;
	[[nodiscard]] float64 across(
		Align align,
		float64 space,
		float64 size) const;

	bool _rtl = false;
	not_null<const TextMeasurer*> _measurer;

};

[[nodiscard]] Metrics MetricsOf(TextStyle style) {
	const auto [size, share] = [&]() -> std::pair<float64, float64> {
		switch (style) {
		case TextStyle::Caption: return { 12., 0.55 };
		case TextStyle::Title: return { 16., 0.55 };
		case TextStyle::Display: return { 32., 0.55 };
		case TextStyle::Mono: return { 13., 0.6 };
		case TextStyle::Body: break;
		}
		return { 14., 0.55 };
	}();
	return { .advance = size * share, .lineHeight = size * 1.25 };
}

[[nodiscard]] bool Fits(const Metrics &metrics, int count, float64 width) {
	return float64(count) * metrics.advance <= width + kSlack;
}

[[nodiscard]] int Fitting(const Metrics &metrics, int limit, float64 width) {
	auto count = 1;
	while (count < limit && Fits(metrics, count + 1, width)) {
		++count;
	}
	return count;
}

[[nodiscard]] std::vector<std::u32string_view> Split(
		std::u32string_view text,
		char32_t separator) {
	auto result = std::vector<std::u32string_view>();
	auto from = size_t(0);
	while (true) {
		const auto till = text.find(separator, from);
		if (till == std::u32string_view::npos) {
			result.push_back(text.substr(from));
			return result;
		}
		result.push_back(text.substr(from, till - from));
		from = till + 1;
	}
}

void BreakParagraph(
		std::u32string_view paragraph,
		float64 width,
		const Metrics &metrics,
		std::vector<std::u32string> &lines) {
	auto line = std::u32string();
	auto count = 0;
	auto started = false;
	for (const auto word : Split(paragraph, U' ')) {
		const auto joined = count + 1 + int(word.size());
		if (started && Fits(metrics, joined, width)) {
			line += U' ';
			line += word;
			count = joined;
			continue;
		}
		if (started) {
			lines.push_back(base::take(line));
		}
		auto rest = word;
		while (rest.size() > 1 && !Fits(metrics, int(rest.size()), width)) {
			const auto taken = Fitting(metrics, int(rest.size()) - 1, width);
			lines.emplace_back(rest.substr(0, taken));
			rest.remove_prefix(taken);
		}
		line += rest;
		count = int(rest.size());
		started = true;
	}
	lines.push_back(std::move(line));
}

[[nodiscard]] std::vector<std::u32string> FakeLines(
		std::u32string_view text,
		float64 width,
		const Metrics &metrics,
		std::optional<int> maxLines) {
	auto lines = std::vector<std::u32string>();
	if (text.empty()) {
		return lines;
	}
	for (const auto paragraph : Split(text, U'\n')) {
		BreakParagraph(paragraph, width, metrics, lines);
		if (maxLines && int(lines.size()) > *maxLines) {
			break;
		}
	}
	if (maxLines && int(lines.size()) > *maxLines) {
		lines.resize(*maxLines);
		if (!lines.empty()) {
			auto &last = lines.back();
			while (!last.empty()
				&& !Fits(metrics, int(last.size()) + 1, width)) {
				last.pop_back();
			}
			last.push_back(kEllipsis);
		}
	}
	return lines;
}

[[nodiscard]] float64 AtLeastZero(float64 value) {
	return (value > 0.) ? value : 0.;
}

[[nodiscard]] float64 Smaller(float64 a, float64 b) {
	return (a < b) ? a : b;
}

[[nodiscard]] float64 Larger(float64 a, float64 b) {
	return (a > b) ? a : b;
}

[[nodiscard]] std::optional<float64> OwnWidth(const Node &node) {
	return node.width
		? std::make_optional(AtLeastZero(*node.width))
		: std::nullopt;
}

[[nodiscard]] float64 Capped(const Node &node, float64 width) {
	const auto own = OwnWidth(node);
	return own ? Smaller(*own, width) : width;
}

[[nodiscard]] float64 Weight(const Node &node) {
	return AtLeastZero(node.weight);
}

[[nodiscard]] float64 Down(Align align, float64 space, float64 size) {
	switch (align) {
	case Align::Center: return (space - size) / 2.;
	case Align::End: return space - size;
	case Align::Start:
	case Align::Stretch: break;
	}
	return 0.;
}

[[nodiscard]] float64 Tallest(const std::vector<LaidNode> &laid) {
	auto result = 0.;
	for (const auto &child : laid) {
		result = Larger(result, child.h);
	}
	return result;
}

[[nodiscard]] LaidNode Leaf(const Node &node, float64 w, float64 h) {
	return { .node = &node, .w = w, .h = h };
}

void Place(LaidNode &laid, float64 x, float64 y) {
	laid.x += x;
	laid.y += y;
	if (laid.lines) {
		for (auto &line : *laid.lines) {
			line.x += laid.x;
			line.y += laid.y;
		}
	}
	for (auto &child : laid.children) {
		Place(child, laid.x, laid.y);
	}
}

void AppendFrames(const LaidNode &laid, QJsonArray &nodes) {
	auto object = QJsonObject{
		{ u"key"_q, laid.node->key },
		{ u"x"_q, laid.x },
		{ u"y"_q, laid.y },
		{ u"w"_q, laid.w },
		{ u"h"_q, laid.h },
	};
	if (laid.lines) {
		auto lines = QJsonArray();
		for (const auto &line : *laid.lines) {
			lines.push_back(QJsonObject{
				{ u"text"_q, line.text },
				{ u"x"_q, line.x },
				{ u"y"_q, line.y },
			});
		}
		object.insert(u"lines"_q, lines);
	}
	nodes.push_back(object);
	for (const auto &child : laid.children) {
		AppendFrames(child, nodes);
	}
}

Engine::Engine(bool rtl, not_null<const TextMeasurer*> measurer)
: _rtl(rtl)
, _measurer(measurer) {
}

LaidNode Engine::layOut(const Node &node, float64 w) const {
	switch (node.type) {
	case NodeType::Column: return column(node, w);
	case NodeType::Row: return row(node, w);
	case NodeType::ScrollX: return scrollX(node, w);
	case NodeType::Canvas: return canvas(node, w);
	case NodeType::Text: return text(node, w);
	case NodeType::TextField: return textField(node, w);
	case NodeType::Chart: return Leaf(
		node,
		w,
		(node.chart.height
			? AtLeastZero(*node.chart.height)
			: (w * kChartHeight)));
	case NodeType::Divider: return Leaf(
		node,
		w,
		2. * kDividerMargin + _measurer->hairline());
	case NodeType::Frame:
	case NodeType::Rect:
	case NodeType::Ellipse:
	case NodeType::Line:
	case NodeType::Path: break;
	}
	return Leaf(node, w, 0.);
}

LaidNode Engine::column(const Node &node, float64 w) const {
	const auto sides = padding(node);
	const auto inner = AtLeastZero(w - sides.left - sides.right);
	auto result = Leaf(node, w, 0.);
	auto y = sides.top;
	for (const auto &child : node.children) {
		if (!result.children.empty()) {
			y += node.box.gap;
		}
		auto laid = layOut(child, Capped(child, inner));
		laid.x = sides.left + across(node.box.align, inner, laid.w);
		laid.y = y;
		y += laid.h;
		result.children.push_back(std::move(laid));
	}
	result.h = y + sides.bottom;
	return result;
}

LaidNode Engine::row(const Node &node, float64 w) const {
	const auto sides = padding(node);
	const auto gap = node.box.gap;
	const auto count = int(node.children.size());
	const auto gaps = gap * std::max(count - 1, 0);
	auto fixed = 0.;
	auto weights = 0.;
	for (const auto &child : node.children) {
		if (const auto own = OwnWidth(child)) {
			fixed += *own;
		} else {
			weights += Weight(child);
		}
	}
	const auto rest = AtLeastZero(
		w - sides.left - sides.right - gaps - fixed);
	auto laid = std::vector<LaidNode>();
	laid.reserve(count);
	for (const auto &child : node.children) {
		const auto own = OwnWidth(child);
		const auto width = own
			? *own
			: ((weights > 0.) ? (rest * Weight(child) / weights) : 0.);
		laid.push_back(layOut(child, width));
	}
	const auto inner = Tallest(laid);
	const auto align = node.box.align;
	sideBySide(laid, w, sides.left, sides.right, gap);
	for (auto &child : laid) {
		child.y = sides.top + Down(align, inner, child.h);
		if (align == Align::Stretch) {
			child.h = inner;
		}
	}
	auto result = Leaf(node, w, sides.top + inner + sides.bottom);
	result.children = std::move(laid);
	return result;
}

LaidNode Engine::scrollX(const Node &node, float64 w) const {
	const auto sides = padding(node);
	auto laid = std::vector<LaidNode>();
	laid.reserve(node.children.size());
	for (const auto &child : node.children) {
		laid.push_back(layOut(child, OwnWidth(child).value_or(0.)));
	}
	const auto inner = Tallest(laid);
	sideBySide(laid, w, sides.left, sides.right, node.box.gap);
	for (auto &child : laid) {
		child.y = sides.top;
	}
	auto result = Leaf(node, w, sides.top + inner + sides.bottom);
	result.children = std::move(laid);
	return result;
}

LaidNode Engine::canvas(const Node &node, float64 w) const {
	const auto viewWidth = node.box.viewBox.width();
	const auto scale = (viewWidth > 0.) ? (w / viewWidth) : 0.;
	auto result = Leaf(node, w, node.box.viewBox.height() * scale);
	result.scale = scale;
	for (const auto &child : node.children) {
		if (child.type == NodeType::Frame) {
			result.children.push_back(frame(child, scale));
		}
	}
	return result;
}

LaidNode Engine::frame(const Node &frame, float64 scale) const {
	const auto &box = frame.box;
	const auto w = AtLeastZero(box.frame.width() * scale);
	const auto h = AtLeastZero(box.frame.height() * scale);
	const auto sides = padding(frame);
	const auto innerWidth = AtLeastZero(w - sides.left - sides.right);
	const auto innerHeight = AtLeastZero(h - sides.top - sides.bottom);
	auto result = Leaf(frame, w, h);
	result.x = box.frame.x() * scale;
	result.y = box.frame.y() * scale;
	if (!frame.children.empty()) {
		const auto &child = frame.children.front();
		const auto stretch = (box.align == Align::Stretch);
		auto laid = layOut(
			child,
			stretch ? innerWidth : Capped(child, innerWidth));
		if (stretch) {
			laid.h = innerHeight;
		}
		laid.x = sides.left + across(box.align, innerWidth, laid.w);
		laid.y = sides.top + Down(box.align, innerHeight, laid.h);
		result.children.push_back(std::move(laid));
	}
	return result;
}

LaidNode Engine::text(const Node &node, float64 w) const {
	const auto &data = node.text;
	auto lines = this->lines(
		data.text,
		w,
		data.style,
		data.maxLines,
		data.align);
	auto result = Leaf(
		node,
		w,
		float64(lines.size()) * _measurer->lineHeight(data.style));
	result.lines = std::move(lines);
	return result;
}

LaidNode Engine::textField(const Node &node, float64 w) const {
	const auto &data = node.field;
	auto lines = this->lines(
		data.label,
		w,
		TextStyle::Caption,
		std::nullopt,
		Align::Start);
	const auto labels = int(lines.size());
	const auto top = lines.empty()
		? 0.
		: (float64(labels) * _measurer->lineHeight(TextStyle::Caption)
			+ kFieldGap);
	const auto &shown = data.value.isEmpty()
		? data.placeholder
		: data.value;
	const auto values = this->lines(
		shown,
		AtLeastZero(w - 2. * kFieldPadding),
		TextStyle::Body,
		std::nullopt,
		Align::Start);
	const auto rows = Larger(
		float64(values.size()),
		std::floor(AtLeastZero(data.lines)));
	for (const auto &line : values) {
		lines.push_back({
			.text = line.text,
			.x = kFieldPadding + line.x,
			.y = top + kFieldPadding + line.y,
		});
	}
	auto result = Leaf(
		node,
		w,
		(top
			+ rows * _measurer->lineHeight(TextStyle::Body)
			+ 2. * kFieldPadding));
	result.lines = std::move(lines);
	result.fieldTop = top;
	result.fieldLabels = labels;
	return result;
}

std::vector<LaidLine> Engine::lines(
		const QString &text,
		float64 width,
		TextStyle style,
		std::optional<int> maxLines,
		Align align) const {
	const auto lineHeight = _measurer->lineHeight(style);
	auto result = std::vector<LaidLine>();
	for (auto &line : _measurer->lines(text, style, width, maxLines)) {
		const auto index = float64(result.size());
		result.push_back({
			.text = std::move(line.text),
			.x = across(align, width, line.width),
			.y = index * lineHeight,
		});
	}
	return result;
}

void Engine::sideBySide(
		std::vector<LaidNode> &laid,
		float64 w,
		float64 left,
		float64 right,
		float64 gap) const {
	auto along = 0.;
	for (auto &child : laid) {
		if (&child != &laid.front()) {
			along += gap;
		}
		child.x = _rtl ? (w - right - along - child.w) : (left + along);
		along += child.w;
	}
}

Sides Engine::padding(const Node &node) const {
	const auto &sides = node.box.padding;
	return _rtl
		? Sides{ sides.top, sides.end, sides.bottom, sides.start }
		: Sides{ sides.top, sides.start, sides.bottom, sides.end };
}

float64 Engine::across(Align align, float64 space, float64 size) const {
	const auto start = _rtl ? (space - size) : 0.;
	const auto end = _rtl ? 0. : (space - size);
	switch (align) {
	case Align::Center: return (space - size) / 2.;
	case Align::End: return end;
	case Align::Start:
	case Align::Stretch: break;
	}
	return start;
}

} // namespace

float64 FakeTextMeasurer::lineHeight(TextStyle style) const {
	return MetricsOf(style).lineHeight;
}

float64 FakeTextMeasurer::hairline() const {
	return 1.;
}

std::vector<MeasuredLine> FakeTextMeasurer::lines(
		const QString &text,
		TextStyle style,
		float64 width,
		std::optional<int> maxLines) const {
	const auto metrics = MetricsOf(style);
	const auto source = text.toStdU32String();
	auto result = std::vector<MeasuredLine>();
	for (const auto &line : FakeLines(source, width, metrics, maxLines)) {
		result.push_back({
			.text = QString::fromStdU32String(line),
			.width = float64(line.size()) * metrics.advance,
		});
	}
	return result;
}

Layout LayOut(
		const Document &document,
		float64 available,
		bool rtl,
		const TextMeasurer &measurer) {
	const auto space = AtLeastZero(available);
	const auto width = document.width
		? Smaller(AtLeastZero(*document.width), space)
		: space;
	auto result = Layout{ .width = width };
	if (document.root) {
		auto root = Engine(rtl, &measurer).layOut(*document.root, width);
		result.height = root.h;
		Place(root, 0., 0.);
		result.root = std::move(root);
	}
	return result;
}

QJsonObject SerializeFrames(const Layout &layout) {
	auto nodes = QJsonArray();
	if (layout.root) {
		AppendFrames(*layout.root, nodes);
	}
	return QJsonObject{
		{ u"tgw_frames"_q, kFramesFormat },
		{ u"width"_q, layout.width },
		{ u"height"_q, layout.height },
		{ u"nodes"_q, nodes },
	};
}

} // namespace Twidget
