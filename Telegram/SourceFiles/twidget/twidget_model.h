/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>
#include <QtCore/QStringList>
#include <QtGui/QColor>

#include <array>
#include <optional>
#include <vector>

namespace Twidget {

enum class NodeType : uchar {
	Column,
	Row,
	ScrollX,
	Canvas,
	Frame,
	Text,
	TextField,
	Chart,
	Divider,
	Rect,
	Ellipse,
	Line,
	Path,
};

enum class Align : uchar {
	Start,
	Center,
	End,
	Stretch,
};

enum class TextStyle : uchar {
	Body,
	Caption,
	Title,
	Display,
	Mono,
};

enum class ChartKind : uchar {
	Line,
	Bar,
};

enum class LineCap : uchar {
	Butt,
	Round,
	Square,
};

enum class LineJoin : uchar {
	Miter,
	Round,
	Bevel,
};

struct Color {
	QString token;
	QColor fixed;
	float64 alpha = 1.;
};

struct Padding {
	float64 top = 0.;
	float64 end = 0.;
	float64 bottom = 0.;
	float64 start = 0.;
};

struct Border {
	Color color;
	float64 width = 0.;
};

struct BoxData {
	float64 gap = 0.;
	Padding padding;
	Align align = Align::Start;
	std::optional<Color> background;
	float64 radius = 0.;
	std::optional<Border> border;
	QSizeF viewBox;
	QRectF frame;
};

struct Entity {
	QString type;
	int offset = 0;
	int length = 0;
	QString url;
};

struct TextData {
	QString text;
	std::vector<Entity> entities;
	TextStyle style = TextStyle::Body;
	Color color;
	Align align = Align::Start;
	std::optional<int> maxLines;
	bool tabular = false;
};

struct FieldData {
	QString label;
	QString value;
	QString placeholder;
	float64 lines = 1.;
};

struct Series {
	QString name;
	std::vector<std::optional<float64>> values;
	Color color;
};

struct ChartData {
	ChartKind kind = ChartKind::Line;
	QStringList labels;
	std::vector<Series> series;
	float64 yMin = 0.;
	float64 yMax = 1.;
	std::optional<float64> height;
	bool legend = false;
};

struct ShapeData {
	QRectF rect;
	QPointF center;
	QPointF from;
	QPointF to;
	float64 rx = 0.;
	float64 ry = 0.;
	QString path;
	std::optional<Color> fill;
	std::optional<Color> stroke;
	float64 strokeWidth = 1.;
	float64 opacity = 1.;
	std::optional<std::array<float64, 2>> dash;
	LineCap cap = LineCap::Butt;
	LineJoin join = LineJoin::Miter;
	std::optional<std::array<float64, 6>> transform;
};

struct Node {
	NodeType type = NodeType::Column;
	QString key;
	QString a11y;
	std::optional<float64> width;
	float64 weight = 1.;
	BoxData box;
	TextData text;
	FieldData field;
	ChartData chart;
	Color color;
	ShapeData shape;
	std::vector<Node> children;
};

struct Document {
	std::optional<float64> width;
	std::optional<Node> root;
};

[[nodiscard]] std::optional<Document> ParseResolved(const QByteArray &json);

} // namespace Twidget
