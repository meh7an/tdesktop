/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_paint.h"

#include "twidget/twidget_layout.h"
#include "twidget/twidget_text.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "ui/text/text.h"

namespace Twidget {
namespace {

constexpr auto kSurfaceShare = 0.05;
constexpr auto kSurfaceAltShare = 0.10;
constexpr auto kDividerShare = 0.12;
constexpr auto kPaletteCount = 7;
constexpr auto kMiterLimit = 4.;
constexpr auto kMinDash = 0.001;
constexpr auto kFieldBorder = 1.;
constexpr auto kPlotInset = 4.;
constexpr auto kLabelsHeight = 16.;
constexpr auto kLegendHeight = 18.;
constexpr auto kSwatch = 8.;
constexpr auto kSwatchRadius = 2.;
constexpr auto kSwatchGap = 4.;
constexpr auto kLegendGap = 12.;
constexpr auto kLineWidth = 2.;
constexpr auto kBarShare = 0.7;

struct Plot {
	float64 x = 0.;
	float64 top = 0.;
	float64 height = 0.;
	float64 slot = 0.;
	float64 low = 0.;
	float64 high = 0.;

	[[nodiscard]] float64 at(float64 value) const {
		const auto span = (high > low) ? (high - low) : 1.;
		return top + height * (1. - (value - low) / span);
	}
	[[nodiscard]] float64 center(int index) const {
		return x + (index + 0.5) * slot;
	}
};

[[nodiscard]] float64 AtLeastZero(float64 value) {
	return (value > 0.) ? value : 0.;
}

[[nodiscard]] style::align TextAlign(Align align) {
	switch (align) {
	case Align::Center: return style::al_center;
	case Align::End: return style::al_right;
	case Align::Start:
	case Align::Stretch: break;
	}
	return style::al_left;
}

[[nodiscard]] Qt::PenCapStyle PenCap(LineCap cap) {
	switch (cap) {
	case LineCap::Round: return Qt::RoundCap;
	case LineCap::Square: return Qt::SquareCap;
	case LineCap::Butt: break;
	}
	return Qt::FlatCap;
}

[[nodiscard]] Qt::PenJoinStyle PenJoin(LineJoin join) {
	switch (join) {
	case LineJoin::Round: return Qt::RoundJoin;
	case LineJoin::Bevel: return Qt::BevelJoin;
	case LineJoin::Miter: break;
	}
	return Qt::SvgMiterJoin;
}

[[nodiscard]] QPen ShapePen(const ShapeData &shape, QColor color) {
	auto result = QPen(
		color,
		shape.strokeWidth,
		Qt::SolidLine,
		PenCap(shape.cap),
		PenJoin(shape.join));
	result.setMiterLimit(kMiterLimit);
	if (const auto &dash = shape.dash) {
		const auto on = AtLeastZero((*dash)[0]);
		const auto off = AtLeastZero((*dash)[1]);
		if (on + off > 0.) {
			result.setDashPattern({
				std::max(on, kMinDash) / shape.strokeWidth,
				std::max(off, kMinDash) / shape.strokeWidth,
			});
		}
	}
	return result;
}

[[nodiscard]] QPainterPath ShapePath(const Node &node) {
	const auto &shape = node.shape;
	auto result = QPainterPath();
	result.setFillRule(Qt::WindingFill);
	switch (node.type) {
	case NodeType::Rect:
		if (shape.rect.width() > 0. && shape.rect.height() > 0.) {
			if (shape.rx > 0.) {
				result.addRoundedRect(shape.rect, shape.rx, shape.rx);
			} else {
				result.addRect(shape.rect);
			}
		}
		break;
	case NodeType::Ellipse:
		if (shape.rx > 0. && shape.ry > 0.) {
			result.addEllipse(shape.center, shape.rx, shape.ry);
		}
		break;
	case NodeType::Line:
		result.moveTo(shape.from);
		result.lineTo(shape.to);
		break;
	case NodeType::Path:
		result = shape.path;
		break;
	case NodeType::Column:
	case NodeType::Row:
	case NodeType::ScrollX:
	case NodeType::Canvas:
	case NodeType::Frame:
	case NodeType::Text:
	case NodeType::TextField:
	case NodeType::Chart:
	case NodeType::Divider: break;
	}
	return result;
}

class Painter final {
public:
	Painter(QPainter &p, const PaintContext &context);

	void node(const LaidNode &laid);

private:
	[[nodiscard]] float64 px(float64 units) const;
	[[nodiscard]] QRectF rect(const LaidNode &laid) const;
	void container(const LaidNode &laid);
	void canvas(const LaidNode &laid);
	void canvasContent(const LaidNode &laid);
	void shape(const Node &node);
	void text(const LaidNode &laid);
	void textField(const LaidNode &laid);
	void divider(const LaidNode &laid);
	void chart(const LaidNode &laid);
	void chartLines(const Plot &plot, const std::vector<Series> &series);
	void chartBars(const Plot &plot, const std::vector<Series> &series);
	void chartLabels(const Plot &plot, const QStringList &labels);
	void chartLegend(const LaidNode &laid, const std::vector<Series> &series);
	void string(
		const TextData &text,
		QPointF position,
		float64 width,
		QColor color);
	void background(
		const std::optional<Color> &color,
		QRectF rect,
		float64 radius);
	void border(
		const std::optional<Border> &border,
		QRectF rect,
		float64 radius);
	void clip(QRectF rect, float64 radius);

	QPainter &_p;
	const PaintContext &_context;
	const float64 _unit = 1.;

};

Painter::Painter(QPainter &p, const PaintContext &context)
: _p(p)
, _context(context)
, _unit(context.measurer->unit()) {
}

void Painter::node(const LaidNode &laid) {
	switch (laid.node->type) {
	case NodeType::Column:
	case NodeType::Row:
	case NodeType::ScrollX:
	case NodeType::Frame: container(laid); return;
	case NodeType::Canvas: canvas(laid); return;
	case NodeType::Text: text(laid); return;
	case NodeType::TextField: textField(laid); return;
	case NodeType::Chart: chart(laid); return;
	case NodeType::Divider: divider(laid); return;
	case NodeType::Rect:
	case NodeType::Ellipse:
	case NodeType::Line:
	case NodeType::Path: return;
	}
}

float64 Painter::px(float64 units) const {
	return units * _unit;
}

QRectF Painter::rect(const LaidNode &laid) const {
	return QRectF(px(laid.x), px(laid.y), px(laid.w), px(laid.h));
}

void Painter::container(const LaidNode &laid) {
	const auto &box = laid.node->box;
	const auto rect = this->rect(laid);
	const auto radius = px(box.radius);
	background(box.background, rect, radius);
	border(box.border, rect, radius);
	_p.save();
	clip(rect, radius);
	for (const auto &child : laid.children) {
		node(child);
	}
	_p.restore();
}

void Painter::canvas(const LaidNode &laid) {
	const auto &box = laid.node->box;
	const auto rect = this->rect(laid);
	const auto radius = px(box.radius);
	if (radius <= 0.) {
		background(box.background, rect, 0.);
		_p.save();
		clip(rect, 0.);
		canvasContent(laid);
		_p.restore();
		return;
	}
	const auto outer = rect.toAlignedRect();
	if (outer.isEmpty()) {
		return;
	}
	const auto ratio = _p.device()->devicePixelRatioF();
	auto layer = QImage(
		outer.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	layer.setDevicePixelRatio(ratio);
	layer.fill(Qt::transparent);
	{
		auto q = QPainter(&layer);
		q.translate(-outer.topLeft());
		auto painter = Painter(q, _context);
		painter.background(box.background, rect, 0.);
		painter.canvasContent(laid);
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		auto hq = PainterHighQualityEnabler(q);
		q.setPen(Qt::NoPen);
		q.setBrush(Qt::white);
		q.drawRoundedRect(rect, radius, radius);
	}
	_p.drawImage(outer.topLeft(), layer);
}

void Painter::canvasContent(const LaidNode &laid) {
	const auto origin = rect(laid).topLeft();
	const auto scale = px(laid.scale);
	auto frames = laid.children.begin();
	for (const auto &child : laid.node->children) {
		if (child.type == NodeType::Frame) {
			if (frames != laid.children.end()) {
				container(*frames++);
			}
			continue;
		}
		_p.save();
		_p.translate(origin);
		_p.scale(scale, scale);
		shape(child);
		_p.restore();
	}
}

void Painter::shape(const Node &node) {
	const auto &shape = node.shape;
	const auto path = ShapePath(node);
	if (path.isEmpty()) {
		return;
	}
	const auto palette = _context.palette;
	const auto fill = (node.type == NodeType::Line)
		? std::nullopt
		: palette->resolve(shape.fill);
	const auto stroke = (shape.strokeWidth > 0.)
		? palette->resolve(shape.stroke)
		: std::nullopt;
	if (!fill && !stroke) {
		return;
	}
	_p.save();
	auto hq = PainterHighQualityEnabler(_p);
	if (const auto &matrix = shape.transform) {
		const auto &m = *matrix;
		_p.setTransform(QTransform(m[0], m[1], m[2], m[3], m[4], m[5]), true);
	}
	if (shape.opacity < 1.) {
		_p.setOpacity(_p.opacity() * AtLeastZero(shape.opacity));
	}
	if (fill) {
		_p.fillPath(path, *fill);
	}
	if (stroke) {
		_p.strokePath(path, ShapePen(shape, *stroke));
	}
	_p.restore();
}

void Painter::text(const LaidNode &laid) {
	const auto &data = laid.node->text;
	const auto color = _context.palette->resolve(data.color);
	if (color && (!data.maxLines || *data.maxLines > 0)) {
		string(data, QPointF(laid.x, laid.y), laid.w, *color);
	}
}

void Painter::textField(const LaidNode &laid) {
	const auto &field = laid.node->field;
	const auto palette = _context.palette;
	const auto secondary = palette->token(ColorToken::TextSecondary);
	string(FieldLabel(field), QPointF(laid.x, laid.y), laid.w, secondary);

	const auto top = laid.fieldTop;
	const auto line = px(kFieldBorder);
	const auto half = line / 2.;
	const auto box = QRectF(
		px(laid.x) + half,
		px(laid.y + top) + half,
		AtLeastZero(px(laid.w) - line),
		AtLeastZero(px(laid.h - top) - line));
	const auto radius = AtLeastZero(px(kFieldRadius) - half);
	{
		auto hq = PainterHighQualityEnabler(_p);
		_p.setPen(QPen(palette->token(ColorToken::Divider), line));
		_p.setBrush(Qt::NoBrush);
		_p.drawRoundedRect(box, radius, radius);
	}
	string(
		FieldValue(field),
		QPointF(laid.x + kFieldPadding, laid.y + top + kFieldPadding),
		AtLeastZero(laid.w - 2. * kFieldPadding),
		(field.value.isEmpty()
			? secondary
			: palette->token(ColorToken::Text)));
}

void Painter::divider(const LaidNode &laid) {
	if (const auto color = _context.palette->resolve(laid.node->color)) {
		_p.fillRect(
			QRectF(
				px(laid.x),
				px(laid.y + kDividerMargin),
				px(laid.w),
				px(_context.measurer->hairline())),
			*color);
	}
}

void Painter::chart(const LaidNode &laid) {
	const auto &chart = laid.node->chart;
	const auto legend = !chart.series.empty() && chart.legend;
	const auto legendHeight = legend ? kLegendHeight : 0.;
	const auto labelsHeight = chart.labels.isEmpty() ? 0. : kLabelsHeight;
	auto slots = std::max(int(chart.labels.size()), 1);
	for (const auto &series : chart.series) {
		slots = std::max(slots, int(series.values.size()));
	}
	const auto plot = Plot{
		.x = laid.x,
		.top = laid.y + kPlotInset,
		.height = AtLeastZero(
			laid.h - kPlotInset - labelsHeight - legendHeight),
		.slot = laid.w / slots,
		.low = chart.yMin,
		.high = chart.yMax,
	};
	_p.save();
	clip(rect(laid), 0.);
	_p.fillRect(
		QRectF(
			px(laid.x),
			px(plot.top + plot.height),
			px(laid.w),
			px(_context.measurer->hairline())),
		_context.palette->token(ColorToken::Divider));
	if (chart.kind == ChartKind::Bar) {
		chartBars(plot, chart.series);
	} else {
		chartLines(plot, chart.series);
	}
	chartLabels(plot, chart.labels);
	if (legend) {
		chartLegend(laid, chart.series);
	}
	_p.restore();
}

void Painter::chartLines(const Plot &plot, const std::vector<Series> &series) {
	auto hq = PainterHighQualityEnabler(_p);
	for (const auto &entry : series) {
		const auto color = _context.palette->resolve(entry.color);
		if (!color) {
			continue;
		}
		auto path = QPainterPath();
		auto drawing = false;
		for (auto i = 0; i != int(entry.values.size()); ++i) {
			const auto &value = entry.values[i];
			if (!value) {
				drawing = false;
				continue;
			}
			const auto point = QPointF(
				px(plot.center(i)),
				px(plot.at(*value)));
			if (drawing) {
				path.lineTo(point);
			} else {
				path.moveTo(point);
			}
			drawing = true;
		}
		_p.strokePath(
			path,
			QPen(
				*color,
				px(kLineWidth),
				Qt::SolidLine,
				Qt::RoundCap,
				Qt::RoundJoin));
	}
}

void Painter::chartBars(const Plot &plot, const std::vector<Series> &series) {
	auto hq = PainterHighQualityEnabler(_p);
	const auto group = plot.slot * kBarShare;
	const auto bar = group / std::max(int(series.size()), 1);
	const auto base = (plot.low > 0.)
		? plot.low
		: (plot.high < 0.)
		? plot.high
		: 0.;
	const auto zero = plot.at(base);
	for (auto position = 0; position != int(series.size()); ++position) {
		const auto &entry = series[position];
		const auto color = _context.palette->resolve(entry.color);
		if (!color) {
			continue;
		}
		for (auto i = 0; i != int(entry.values.size()); ++i) {
			const auto &value = entry.values[i];
			if (!value) {
				continue;
			}
			const auto top = plot.at(*value);
			const auto left = plot.center(i) - group / 2. + position * bar;
			const auto from = std::min(top, zero);
			const auto height = std::abs(zero - top);
			_p.fillRect(
				QRectF(px(left), px(from), px(bar), px(height)),
				*color);
		}
	}
}

void Painter::chartLabels(const Plot &plot, const QStringList &labels) {
	if (labels.isEmpty()) {
		return;
	}
	const auto &font = _context.measurer->textStyle(TextStyle::Caption).font;
	const auto baseline = px(plot.top + plot.height)
		+ (px(kLabelsHeight) - font->height) / 2.
		+ font->ascent;
	_p.setFont(font);
	_p.setPen(_context.palette->token(ColorToken::TextSecondary));
	for (auto i = 0; i != int(labels.size()); ++i) {
		const auto width = font->width(labels[i]);
		_p.drawText(
			QPointF(px(plot.center(i)) - width / 2., baseline),
			labels[i]);
	}
}

void Painter::chartLegend(
		const LaidNode &laid,
		const std::vector<Series> &series) {
	const auto &font = _context.measurer->textStyle(TextStyle::Caption).font;
	const auto top = laid.y + laid.h - kLegendHeight;
	const auto swatchTop = px(top + (kLegendHeight - kSwatch) / 2.);
	const auto baseline = px(top)
		+ (px(kLegendHeight) - font->height) / 2.
		+ font->ascent;
	const auto text = _context.palette->token(ColorToken::Text);
	auto hq = PainterHighQualityEnabler(_p);
	_p.setFont(font);
	auto along = 0.;
	for (const auto &entry : series) {
		const auto name = float64(font->width(entry.name));
		const auto swatch = _context.rtl
			? (px(laid.x + laid.w) - along - px(kSwatch))
			: (px(laid.x) + along);
		const auto left = _context.rtl
			? (swatch - px(kSwatchGap) - name)
			: (swatch + px(kSwatch + kSwatchGap));
		if (const auto color = _context.palette->resolve(entry.color)) {
			_p.setPen(Qt::NoPen);
			_p.setBrush(*color);
			_p.drawRoundedRect(
				QRectF(swatch, swatchTop, px(kSwatch), px(kSwatch)),
				px(kSwatchRadius),
				px(kSwatchRadius));
		}
		_p.setPen(text);
		_p.drawText(QPointF(left, baseline), entry.name);
		along += px(kSwatch + kSwatchGap + kLegendGap) + name;
	}
}

void Painter::string(
		const TextData &text,
		QPointF position,
		float64 width,
		QColor color) {
	if (text.text.isEmpty()) {
		return;
	}
	const auto &measurer = *_context.measurer;
	const auto prepared = measurer.prepare(text, width, _context.rtl);
	_p.setPen(color);
	prepared.draw(_p, {
		.position = QPoint(
			int(std::round(px(position.x()))),
			int(std::round(px(position.y())))),
		.availableWidth = measurer.pixels(width),
		.align = TextAlign(text.align),
		.palette = &_context.palette->textPalette(),
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = crl::now(),
		.pausedEmoji = true,
		.elisionLines = text.maxLines.value_or(0),
		.useFullWidth = true,
	});
}

void Painter::background(
		const std::optional<Color> &color,
		QRectF rect,
		float64 radius) {
	const auto fill = _context.palette->resolve(color);
	if (!fill || rect.isEmpty()) {
		return;
	} else if (radius <= 0.) {
		_p.fillRect(rect, *fill);
		return;
	}
	auto hq = PainterHighQualityEnabler(_p);
	_p.setPen(Qt::NoPen);
	_p.setBrush(*fill);
	_p.drawRoundedRect(rect, radius, radius);
}

void Painter::border(
		const std::optional<Border> &border,
		QRectF rect,
		float64 radius) {
	if (!border) {
		return;
	}
	const auto width = px(border->width);
	const auto color = _context.palette->resolve(border->color);
	if (!color || width <= 0.) {
		return;
	}
	const auto half = width / 2.;
	const auto inner = QRectF(
		rect.x() + half,
		rect.y() + half,
		AtLeastZero(rect.width() - width),
		AtLeastZero(rect.height() - width));
	const auto rounded = AtLeastZero(radius - half);
	auto hq = PainterHighQualityEnabler(_p);
	_p.setPen(QPen(*color, width));
	_p.setBrush(Qt::NoBrush);
	if (rounded > 0.) {
		_p.drawRoundedRect(inner, rounded, rounded);
	} else {
		_p.drawRect(inner);
	}
}

void Painter::clip(QRectF rect, float64 radius) {
	if (radius <= 0.) {
		_p.setClipRect(rect, Qt::IntersectClip);
		return;
	}
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	_p.setClipPath(path, Qt::IntersectClip);
}

} // namespace

Palette::Palette(not_null<const Ui::ChatStyle*> st, bool outgoing)
: _textPalette(&st->messageStyle(outgoing, false).textPalette) {
	const auto &message = st->messageStyle(outgoing, false);
	const auto bubble = message.msgBg->c;
	const auto text = message.historyTextFg->c;
	const auto set = [&](ColorToken token, QColor color) {
		_colors[int(token)] = color;
	};
	set(ColorToken::Text, text);
	set(ColorToken::TextSecondary, message.msgDateFg->c);
	set(ColorToken::Accent, message.msgServiceFg->c);
	set(ColorToken::OnAccent, bubble);
	set(ColorToken::Surface, anim::color(bubble, text, kSurfaceShare));
	set(ColorToken::SurfaceAlt, anim::color(bubble, text, kSurfaceAltShare));
	set(ColorToken::Divider, anim::color(bubble, text, kDividerShare));
	set(ColorToken::Positive, st->boxTextFgGood()->c);
	set(ColorToken::Negative, st->attentionButtonFg()->c);
	set(ColorToken::Warning, st->statisticsChartLineGolden()->c);
	for (auto i = 0; i != kPaletteCount; ++i) {
		set(
			ColorToken(int(ColorToken::Palette1) + i),
			st->coloredValues(false, uint8(i)).name);
	}
}

QColor Palette::token(ColorToken token) const {
	return (int(token) < kColorTokenCount) ? _colors[int(token)] : QColor();
}

std::optional<QColor> Palette::resolve(const Color &color) const {
	auto result = (color.token == ColorToken::Fixed)
		? color.fixed
		: token(color.token);
	if (!result.isValid()) {
		return std::nullopt;
	} else if (color.alpha) {
		result.setAlphaF(std::clamp(*color.alpha, 0., 1.));
	}
	return result;
}

std::optional<QColor> Palette::resolve(
		const std::optional<Color> &color) const {
	return color ? resolve(*color) : std::nullopt;
}

const style::TextPalette &Palette::textPalette() const {
	return *_textPalette;
}

void Paint(QPainter &p, const Layout &layout, const PaintContext &context) {
	if (!layout.root) {
		return;
	}
	const auto unit = context.measurer->unit();
	p.save();
	p.setClipRect(
		QRectF(0., 0., layout.width * unit, layout.height * unit),
		Qt::IntersectClip);
	Painter(p, context).node(*layout.root);
	p.restore();
}

} // namespace Twidget
