/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_model.h"

#include <twidget.h>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Twidget {
namespace {

constexpr auto kMaxLines = 1'000'000.;

template <typename Type>
struct Named {
	QStringView name;
	Type value;
};

template <typename Type, size_t Size>
[[nodiscard]] std::optional<Type> Lookup(
		const std::array<Named<Type>, Size> &list,
		const QString &name) {
	for (const auto &entry : list) {
		if (entry.name == name) {
			return entry.value;
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<NodeType> ParseType(const QString &name) {
	static constexpr auto kTypes = std::array<Named<NodeType>, 13>{ {
		{ u"column", NodeType::Column },
		{ u"row", NodeType::Row },
		{ u"scroll_x", NodeType::ScrollX },
		{ u"canvas", NodeType::Canvas },
		{ u"frame", NodeType::Frame },
		{ u"text", NodeType::Text },
		{ u"text_field", NodeType::TextField },
		{ u"chart", NodeType::Chart },
		{ u"divider", NodeType::Divider },
		{ u"rect", NodeType::Rect },
		{ u"ellipse", NodeType::Ellipse },
		{ u"line", NodeType::Line },
		{ u"path", NodeType::Path },
	} };
	return Lookup(kTypes, name);
}

[[nodiscard]] Align ParseAlign(const QString &name) {
	static constexpr auto kAligns = std::array<Named<Align>, 3>{ {
		{ u"center", Align::Center },
		{ u"end", Align::End },
		{ u"stretch", Align::Stretch },
	} };
	return Lookup(kAligns, name).value_or(Align::Start);
}

[[nodiscard]] TextStyle ParseStyle(const QString &name) {
	static constexpr auto kStyles = std::array<Named<TextStyle>, 4>{ {
		{ u"caption", TextStyle::Caption },
		{ u"title", TextStyle::Title },
		{ u"display", TextStyle::Display },
		{ u"mono", TextStyle::Mono },
	} };
	return Lookup(kStyles, name).value_or(TextStyle::Body);
}

[[nodiscard]] LineCap ParseCap(const QString &name) {
	static constexpr auto kCaps = std::array<Named<LineCap>, 2>{ {
		{ u"round", LineCap::Round },
		{ u"square", LineCap::Square },
	} };
	return Lookup(kCaps, name).value_or(LineCap::Butt);
}

[[nodiscard]] LineJoin ParseJoin(const QString &name) {
	static constexpr auto kJoins = std::array<Named<LineJoin>, 2>{ {
		{ u"round", LineJoin::Round },
		{ u"bevel", LineJoin::Bevel },
	} };
	return Lookup(kJoins, name).value_or(LineJoin::Miter);
}

[[nodiscard]] std::optional<ColorToken> ParseToken(const QString &name) {
	static constexpr auto kTokens = std::array<
		Named<ColorToken>,
		kColorTokenCount>{ {
		{ u"text", ColorToken::Text },
		{ u"text_secondary", ColorToken::TextSecondary },
		{ u"accent", ColorToken::Accent },
		{ u"on_accent", ColorToken::OnAccent },
		{ u"surface", ColorToken::Surface },
		{ u"surface_alt", ColorToken::SurfaceAlt },
		{ u"divider", ColorToken::Divider },
		{ u"positive", ColorToken::Positive },
		{ u"negative", ColorToken::Negative },
		{ u"warning", ColorToken::Warning },
		{ u"palette_1", ColorToken::Palette1 },
		{ u"palette_2", ColorToken::Palette2 },
		{ u"palette_3", ColorToken::Palette3 },
		{ u"palette_4", ColorToken::Palette4 },
		{ u"palette_5", ColorToken::Palette5 },
		{ u"palette_6", ColorToken::Palette6 },
		{ u"palette_7", ColorToken::Palette7 },
	} };
	return Lookup(kTokens, name);
}

[[nodiscard]] bool IsPathCommand(QChar ch) {
	return (ch == u'M') || (ch == u'L') || (ch == u'C') || (ch == u'Z');
}

[[nodiscard]] int PathArguments(QChar command) {
	return (command == u'C')
		? 6
		: ((command == u'M') || (command == u'L'))
		? 2
		: 0;
}

[[nodiscard]] QPainterPath ParsePath(const QString &data) {
	auto result = QPainterPath();
	result.setFillRule(Qt::WindingFill);
	auto command = QChar();
	auto numbers = std::array<float64, 6>();
	auto count = 0;
	auto i = 0;
	while (i < data.size()) {
		const auto ch = data[i];
		if (ch == u' ' || ch == u',') {
			++i;
			continue;
		} else if (ch == u'Z') {
			result.closeSubpath();
			command = QChar();
			count = 0;
			++i;
			continue;
		} else if (IsPathCommand(ch)) {
			command = ch;
			count = 0;
			++i;
			continue;
		}
		auto till = i;
		while (till < data.size()
			&& data[till] != u' '
			&& data[till] != u','
			&& !IsPathCommand(data[till])) {
			++till;
		}
		auto ok = false;
		const auto value = QStringView(data).mid(i, till - i).toDouble(&ok);
		const auto needed = PathArguments(command);
		if (!ok || !needed) {
			return QPainterPath();
		}
		numbers[count++] = value;
		i = till;
		if (count < needed) {
			continue;
		}
		count = 0;
		if (command == u'M') {
			result.moveTo(numbers[0], numbers[1]);
			command = u'L';
		} else if (command == u'L') {
			result.lineTo(numbers[0], numbers[1]);
		} else {
			result.cubicTo(
				numbers[0],
				numbers[1],
				numbers[2],
				numbers[3],
				numbers[4],
				numbers[5]);
		}
	}
	return result;
}

[[nodiscard]] std::optional<float64> Number(
		const QJsonObject &object,
		const QString &name) {
	const auto value = object.value(name);
	return value.isDouble()
		? std::make_optional(value.toDouble())
		: std::nullopt;
}

[[nodiscard]] float64 NumberOr(
		const QJsonObject &object,
		const QString &name,
		float64 fallback) {
	return Number(object, name).value_or(fallback);
}

[[nodiscard]] QString String(const QJsonObject &object, const QString &name) {
	return object.value(name).toString();
}

[[nodiscard]] std::optional<Color> ParseColor(const QJsonValue &value) {
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto text = value.toString();
	if (text.startsWith(u'#')) {
		const auto fixed = QColor::fromString(text);
		return fixed.isValid()
			? std::make_optional(Color{
				.token = ColorToken::Fixed,
				.fixed = fixed,
			})
			: std::nullopt;
	}
	const auto at = text.indexOf(u'@');
	const auto token = ParseToken((at < 0) ? text : text.left(at));
	if (!token) {
		return std::nullopt;
	} else if (at < 0) {
		return Color{ .token = *token };
	}
	auto ok = false;
	const auto alpha = text.mid(at + 1).toDouble(&ok);
	return ok
		? std::make_optional(Color{ .token = *token, .alpha = alpha })
		: std::nullopt;
}

[[nodiscard]] Padding ParsePadding(const QJsonValue &value) {
	auto sides = std::array<float64, 4>{ { 0., 0., 0., 0. } };
	const auto list = value.toArray();
	for (auto i = 0; i != int(sides.size()) && i != list.size(); ++i) {
		if (list[i].isDouble()) {
			sides[i] = list[i].toDouble();
		}
	}
	return {
		.top = sides[0],
		.end = sides[1],
		.bottom = sides[2],
		.start = sides[3],
	};
}

[[nodiscard]] std::optional<Border> ParseBorder(const QJsonValue &value) {
	if (!value.isObject()) {
		return std::nullopt;
	}
	const auto object = value.toObject();
	return Border{
		.color = ParseColor(object.value(u"color"_q)).value_or(Color()),
		.width = NumberOr(object, u"width"_q, 0.),
	};
}

template <size_t Size>
[[nodiscard]] std::optional<std::array<float64, Size>> ParseNumbers(
		const QJsonValue &value) {
	const auto list = value.toArray();
	if (list.size() != qsizetype(Size)) {
		return std::nullopt;
	}
	auto result = std::array<float64, Size>();
	for (auto i = 0; i != int(Size); ++i) {
		if (!list[i].isDouble()) {
			return std::nullopt;
		}
		result[i] = list[i].toDouble();
	}
	return result;
}

[[nodiscard]] BoxData ParseBox(const QJsonObject &object) {
	const auto viewBox = ParseNumbers<2>(object.value(u"view_box"_q));
	return {
		.gap = NumberOr(object, u"gap"_q, 0.),
		.padding = ParsePadding(object.value(u"padding"_q)),
		.align = ParseAlign(String(object, u"align"_q)),
		.background = ParseColor(object.value(u"background"_q)),
		.radius = NumberOr(object, u"radius"_q, 0.),
		.border = ParseBorder(object.value(u"border"_q)),
		.viewBox = (viewBox
			? QSizeF((*viewBox)[0], (*viewBox)[1])
			: QSizeF(0., 0.)),
		.frame = QRectF(
			NumberOr(object, u"x"_q, 0.),
			NumberOr(object, u"y"_q, 0.),
			NumberOr(object, u"w"_q, 0.),
			NumberOr(object, u"h"_q, 0.)),
	};
}

[[nodiscard]] std::vector<Entity> ParseEntities(const QJsonValue &value) {
	auto result = std::vector<Entity>();
	for (const auto &entry : value.toArray()) {
		const auto object = entry.toObject();
		result.push_back({
			.type = String(object, u"type"_q),
			.offset = int(NumberOr(object, u"offset"_q, 0.)),
			.length = int(NumberOr(object, u"length"_q, 0.)),
			.url = String(object, u"url"_q),
		});
	}
	return result;
}

[[nodiscard]] TextData ParseText(const QJsonObject &object) {
	const auto maxLines = Number(object, u"max_lines"_q);
	return {
		.text = String(object, u"text"_q),
		.entities = ParseEntities(object.value(u"entities"_q)),
		.style = ParseStyle(String(object, u"style"_q)),
		.color = ParseColor(object.value(u"color"_q)).value_or(Color()),
		.align = ParseAlign(String(object, u"align"_q)),
		.maxLines = (maxLines
			? std::make_optional((*maxLines > 0.)
				? int(std::min(*maxLines, kMaxLines))
				: 0)
			: std::nullopt),
		.tabular = object.value(u"tabular"_q).toBool(),
	};
}

[[nodiscard]] FieldData ParseField(const QJsonObject &object) {
	return {
		.label = String(object, u"label"_q),
		.value = String(object, u"value"_q),
		.placeholder = String(object, u"placeholder"_q),
		.lines = NumberOr(object, u"lines"_q, 1.),
	};
}

[[nodiscard]] std::vector<Series> ParseSeries(const QJsonValue &value) {
	auto result = std::vector<Series>();
	for (const auto &entry : value.toArray()) {
		const auto object = entry.toObject();
		auto values = std::vector<std::optional<float64>>();
		for (const auto &number : object.value(u"values"_q).toArray()) {
			values.push_back(number.isDouble()
				? std::make_optional(number.toDouble())
				: std::nullopt);
		}
		result.push_back({
			.name = String(object, u"name"_q),
			.values = std::move(values),
			.color = ParseColor(object.value(u"color"_q)).value_or(Color()),
		});
	}
	return result;
}

[[nodiscard]] ChartData ParseChart(const QJsonObject &object) {
	auto labels = QStringList();
	for (const auto &label : object.value(u"labels"_q).toArray()) {
		labels.push_back(label.toString());
	}
	return {
		.kind = ((String(object, u"kind"_q) == u"bar"_q)
			? ChartKind::Bar
			: ChartKind::Line),
		.labels = std::move(labels),
		.series = ParseSeries(object.value(u"series"_q)),
		.yMin = NumberOr(object, u"y_min"_q, 0.),
		.yMax = NumberOr(object, u"y_max"_q, 1.),
		.height = Number(object, u"height"_q),
		.legend = object.value(u"legend"_q).toBool(),
	};
}

[[nodiscard]] ShapeData ParseShape(const QJsonObject &object) {
	return {
		.rect = QRectF(
			NumberOr(object, u"x"_q, 0.),
			NumberOr(object, u"y"_q, 0.),
			NumberOr(object, u"w"_q, 0.),
			NumberOr(object, u"h"_q, 0.)),
		.center = QPointF(
			NumberOr(object, u"cx"_q, 0.),
			NumberOr(object, u"cy"_q, 0.)),
		.from = QPointF(
			NumberOr(object, u"x1"_q, 0.),
			NumberOr(object, u"y1"_q, 0.)),
		.to = QPointF(
			NumberOr(object, u"x2"_q, 0.),
			NumberOr(object, u"y2"_q, 0.)),
		.rx = NumberOr(object, u"rx"_q, 0.),
		.ry = NumberOr(object, u"ry"_q, 0.),
		.path = ParsePath(String(object, u"d"_q)),
		.fill = ParseColor(object.value(u"fill"_q)),
		.stroke = ParseColor(object.value(u"stroke"_q)),
		.strokeWidth = NumberOr(object, u"stroke_width"_q, 1.),
		.opacity = NumberOr(object, u"opacity"_q, 1.),
		.dash = ParseNumbers<2>(object.value(u"dash"_q)),
		.cap = ParseCap(String(object, u"cap"_q)),
		.join = ParseJoin(String(object, u"join"_q)),
		.transform = ParseNumbers<6>(object.value(u"transform"_q)),
	};
}

[[nodiscard]] std::optional<Node> ParseNode(const QJsonObject &object) {
	const auto type = ParseType(String(object, u"type"_q));
	if (!type) {
		return std::nullopt;
	}
	auto result = Node{
		.type = *type,
		.key = String(object, u"key"_q),
		.a11y = String(object, u"a11y"_q),
		.width = Number(object, u"width"_q),
		.weight = NumberOr(object, u"weight"_q, 1.),
	};
	switch (*type) {
	case NodeType::Column:
	case NodeType::Row:
	case NodeType::ScrollX:
	case NodeType::Canvas:
	case NodeType::Frame:
		result.box = ParseBox(object);
		break;
	case NodeType::Text:
		result.text = ParseText(object);
		break;
	case NodeType::TextField:
		result.field = ParseField(object);
		break;
	case NodeType::Chart:
		result.chart = ParseChart(object);
		break;
	case NodeType::Divider:
		result.color = ParseColor(
			object.value(u"color"_q)).value_or(Color());
		break;
	case NodeType::Rect:
	case NodeType::Ellipse:
	case NodeType::Line:
	case NodeType::Path:
		result.shape = ParseShape(object);
		break;
	}
	const auto child = object.value(u"child"_q);
	if (child.isObject()) {
		auto parsed = ParseNode(child.toObject());
		if (!parsed) {
			return std::nullopt;
		}
		result.children.push_back(std::move(*parsed));
	}
	for (const auto &entry : object.value(u"children"_q).toArray()) {
		auto parsed = ParseNode(entry.toObject());
		if (!parsed) {
			return std::nullopt;
		}
		result.children.push_back(std::move(*parsed));
	}
	return result;
}

} // namespace

std::optional<Document> ParseResolved(const QByteArray &json) {
	auto error = QJsonParseError();
	const auto parsed = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !parsed.isObject()) {
		return std::nullopt;
	}
	const auto object = parsed.object();
	const auto format = Number(object, u"tgw_resolved"_q);
	if (!format || *format != TWIDGET_RESOLVED_FORMAT) {
		return std::nullopt;
	}
	auto result = Document{ .width = Number(object, u"width"_q) };
	const auto root = object.value(u"root"_q);
	if (root.isObject()) {
		result.root = ParseNode(root.toObject());
		if (!result.root) {
			return std::nullopt;
		}
	}
	return result;
}

} // namespace Twidget
