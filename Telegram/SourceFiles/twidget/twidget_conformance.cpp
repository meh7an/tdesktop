/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_conformance.h"

#include "twidget/twidget_api.h"
#include "twidget/twidget_layout.h"
#include "twidget/twidget_model.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QLocale>

#include <cmath>

namespace Twidget {
namespace {

constexpr auto kTolerance = 1e-6;

[[nodiscard]] QByteArray ReadResource(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] QString Describe(const QJsonValue &value) {
	switch (value.type()) {
	case QJsonValue::Null: return u"null"_q;
	case QJsonValue::Bool: return value.toBool() ? u"true"_q : u"false"_q;
	case QJsonValue::Double: return QString::number(
		value.toDouble(),
		'g',
		QLocale::FloatingPointShortest);
	case QJsonValue::String: return u"\"%1\""_q.arg(value.toString());
	case QJsonValue::Array: return u"an array"_q;
	case QJsonValue::Object: return u"an object"_q;
	case QJsonValue::Undefined: break;
	}
	return u"nothing"_q;
}

[[nodiscard]] QString Difference(
		const QJsonValue &expected,
		const QJsonValue &actual,
		const QString &path) {
	if (expected.isDouble() && actual.isDouble()) {
		return (std::abs(expected.toDouble() - actual.toDouble())
				<= kTolerance)
			? QString()
			: u"%1: expected %2, got %3"_q.arg(
				path,
				Describe(expected),
				Describe(actual));
	} else if (expected.type() != actual.type()) {
		return u"%1: expected %2, got %3"_q.arg(
			path,
			Describe(expected),
			Describe(actual));
	} else if (expected.isObject()) {
		const auto wanted = expected.toObject();
		const auto given = actual.toObject();
		if (wanted.keys() != given.keys()) {
			return u"%1: expected members %2, got %3"_q.arg(
				path,
				wanted.keys().join(u','),
				given.keys().join(u','));
		}
		for (const auto &key : wanted.keys()) {
			const auto problem = Difference(
				wanted.value(key),
				given.value(key),
				path.isEmpty() ? key : (path + u'.' + key));
			if (!problem.isEmpty()) {
				return problem;
			}
		}
	} else if (expected.isArray()) {
		const auto wanted = expected.toArray();
		const auto given = actual.toArray();
		if (wanted.size() != given.size()) {
			return u"%1: expected %2 entries, got %3"_q.arg(
				path
			).arg(wanted.size()
			).arg(given.size());
		}
		for (auto i = 0; i != wanted.size(); ++i) {
			const auto problem = Difference(
				wanted[i],
				given[i],
				u"%1[%2]"_q.arg(path).arg(i));
			if (!problem.isEmpty()) {
				return problem;
			}
		}
	} else if (expected != actual) {
		return u"%1: expected %2, got %3"_q.arg(
			path,
			Describe(expected),
			Describe(actual));
	}
	return QString();
}

[[nodiscard]] QString Compare(const QString &name) {
	auto parts = name.chopped(5).split(u'.');
	auto ok = false;
	const auto width = (parts.size() > 1) ? parts[1].toDouble(&ok) : 0.;
	const auto rtl = (parts.size() == 3) && (parts[2] == u"rtl"_q);
	if (!ok || parts.size() > 3 || (parts.size() == 3 && !rtl)) {
		return u"unexpected golden name"_q;
	}
	const auto envelope = ReadResource(
		u":/tgw/fixtures/envelopes/%1.json"_q.arg(parts[0]));
	const auto golden = QJsonDocument::fromJson(
		ReadResource(u":/tgw/fixtures/expected/frames/%1"_q.arg(name)));
	if (envelope.isEmpty() || !golden.isObject()) {
		return u"fixture or golden missing"_q;
	}
	const auto instance = LoadInstance(envelope);
	if (!instance) {
		return u"twidget refused the envelope"_q;
	}
	const auto resolved = Resolve(instance.get(), FixedEnv(false, rtl));
	if (!resolved) {
		return u"twidget failed to resolve"_q;
	}
	const auto document = ParseResolved(resolved->tree);
	if (!document) {
		return u"the resolved tree did not parse"_q;
	}
	const auto layout = LayOut(*document, width, rtl, FakeTextMeasurer());
	return Difference(golden.object(), SerializeFrames(layout), QString());
}

} // namespace

std::vector<ConformanceResult> RunConformance() {
	const auto names = QDir(u":/tgw/fixtures/expected/frames"_q).entryList(
		QDir::Files,
		QDir::Name);
	auto result = std::vector<ConformanceResult>();
	result.reserve(names.size());
	for (const auto &name : names) {
		auto problem = Compare(name);
		result.push_back({
			.name = name,
			.passed = problem.isEmpty(),
			.problem = std::move(problem),
		});
	}
	return result;
}

} // namespace Twidget
