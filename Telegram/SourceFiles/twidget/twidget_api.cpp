/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_api.h"

#include "base/debug_log.h"
#include "base/unixtime.h"
#include "ui/style/style_core_direction.h"

#include <twidget.h>

#include <QtCore/QDateTime>

namespace Twidget {
namespace {

constexpr auto kReadAttempts = 3;

auto AbiMatches = false;

[[nodiscard]] QString StatusName(int32_t status) {
	switch (status) {
	case TWIDGET_OK: return u"TWIDGET_OK"_q;
	case TWIDGET_ERROR_INVALID_ARGUMENT:
		return u"TWIDGET_ERROR_INVALID_ARGUMENT"_q;
	case TWIDGET_ERROR_BUFFER_TOO_SMALL:
		return u"TWIDGET_ERROR_BUFFER_TOO_SMALL"_q;
	case TWIDGET_ERROR_JSON: return u"TWIDGET_ERROR_JSON"_q;
	case TWIDGET_ERROR_SCHEMA: return u"TWIDGET_ERROR_SCHEMA"_q;
	case TWIDGET_ERROR_BUDGET: return u"TWIDGET_ERROR_BUDGET"_q;
	case TWIDGET_ERROR_UNSUPPORTED_VERSION:
		return u"TWIDGET_ERROR_UNSUPPORTED_VERSION"_q;
	case TWIDGET_ERROR_TEMPLATE_REQUIRED:
		return u"TWIDGET_ERROR_TEMPLATE_REQUIRED"_q;
	case TWIDGET_ERROR_TEMPLATE_MISMATCH:
		return u"TWIDGET_ERROR_TEMPLATE_MISMATCH"_q;
	case TWIDGET_ERROR_NOT_FOUND: return u"TWIDGET_ERROR_NOT_FOUND"_q;
	case TWIDGET_ERROR_INTERNAL: return u"TWIDGET_ERROR_INTERNAL"_q;
	}
	return QString::number(status);
}

[[nodiscard]] QString LastError() {
	const auto length = twidget_last_error(nullptr, 0);
	auto result = QByteArray(qsizetype(length), Qt::Uninitialized);
	twidget_last_error(reinterpret_cast<uint8_t*>(result.data()), length);
	return QString::fromUtf8(result);
}

void LogFailure(const QString &function, int32_t status) {
	LOG(("Twidget Error: %1 failed with %2: %3").arg(
		function,
		StatusName(status),
		LastError()));
}

[[nodiscard]] const uint8_t *Bytes(const QByteArray &data) {
	return reinterpret_cast<const uint8_t*>(data.constData());
}

// base::unixtime keeps the server time as the local time(nullptr) plus a
// whole-second shift. Reading the local millisecond clock on both sides of
// base::unixtime::now() within one second recovers that shift exactly, so
// the result keeps the local clock's milliseconds on the server's seconds.
[[nodiscard]] int64 ServerNowMs() {
	while (true) {
		const auto before = QDateTime::currentMSecsSinceEpoch();
		const auto server = int64(base::unixtime::now());
		const auto after = QDateTime::currentMSecsSinceEpoch();
		if (before / 1000 == after / 1000) {
			return before + (server - before / 1000) * 1000;
		}
	}
}

[[nodiscard]] TWidgetEnv Serialize(const Env &env) {
	return {
		.struct_size = uint32_t(sizeof(TWidgetEnv)),
		.now_ms = env.nowMs,
		.tz_offset_minutes = env.tzOffsetMinutes,
		.dark = env.dark ? 1U : 0U,
		.rtl = env.rtl ? 1U : 0U,
	};
}

template <typename Call>
[[nodiscard]] std::optional<QByteArray> ReadOutput(
		const QString &function,
		Call &&call) {
	auto result = QByteArray();
	for (auto attempt = 0; attempt != kReadAttempts; ++attempt) {
		auto length = size_t(0);
		const auto status = call(
			(result.isEmpty()
				? nullptr
				: reinterpret_cast<uint8_t*>(result.data())),
			size_t(result.size()),
			&length);
		if (status == TWIDGET_OK) {
			result.resize(qsizetype(length));
			return result;
		} else if (status != TWIDGET_ERROR_BUFFER_TOO_SMALL) {
			LogFailure(function, status);
			return std::nullopt;
		}
		result.resize(qsizetype(length));
	}
	LogFailure(function, TWIDGET_ERROR_BUFFER_TOO_SMALL);
	return std::nullopt;
}

} // namespace

void TemplateDeleter::operator()(TWidgetTemplate *value) const {
	twidget_template_drop(value);
}

void InstanceDeleter::operator()(TWidgetInstance *value) const {
	twidget_instance_drop(value);
}

void CheckAbiVersion() {
	const auto version = twidget_abi_version();
	AbiMatches = (version == TWIDGET_ABI_VERSION);
	if (AbiMatches) {
		LOG(("Twidget Info: ABI version %1.").arg(version));
	} else {
		LOG(("Twidget Error: ABI version %1, expected %2, widgets are off."
			).arg(version
			).arg(TWIDGET_ABI_VERSION));
	}
}

bool Available() {
	return AbiMatches;
}

Env CurrentEnv(bool dark) {
	return {
		.nowMs = ServerNowMs(),
		.tzOffsetMinutes = QDateTime::currentDateTime().offsetFromUtc() / 60,
		.dark = dark,
		.rtl = style::RightToLeft(),
	};
}

TemplateHandle MakeTemplate(const QByteArray &json) {
	auto status = int32_t(TWIDGET_OK);
	auto result = TemplateHandle(twidget_template_new(
		Bytes(json),
		size_t(json.size()),
		&status));
	if (!result) {
		LogFailure(u"twidget_template_new"_q, status);
	}
	return result;
}

InstanceHandle MakeInstance(
		const QByteArray &envelope,
		const TWidgetTemplate *bundled) {
	auto status = int32_t(TWIDGET_OK);
	auto result = InstanceHandle(twidget_instance_new(
		Bytes(envelope),
		size_t(envelope.size()),
		bundled,
		&status));
	if (!result) {
		LogFailure(u"twidget_instance_new"_q, status);
	}
	return result;
}

std::optional<Resolved> Resolve(
		not_null<TWidgetInstance*> instance,
		const Env &env) {
	const auto raw = Serialize(env);
	auto wakeup = int64_t(TWIDGET_NO_WAKEUP);
	const auto call = [&](uint8_t *out, size_t capacity, size_t *length) {
		return twidget_resolve(instance, &raw, out, capacity, length, &wakeup);
	};
	auto tree = ReadOutput(u"twidget_resolve"_q, call);
	if (!tree) {
		return std::nullopt;
	}
	return Resolved{
		.tree = std::move(*tree),
		.nextWakeupMs = ((wakeup == TWIDGET_NO_WAKEUP)
			? std::nullopt
			: std::make_optional(int64(wakeup))),
	};
}

std::optional<QString> Summary(
		not_null<TWidgetInstance*> instance,
		const Env &env) {
	const auto raw = Serialize(env);
	const auto call = [&](uint8_t *out, size_t capacity, size_t *length) {
		return twidget_summary(instance, &raw, out, capacity, length);
	};
	const auto text = ReadOutput(u"twidget_summary"_q, call);
	return text
		? std::make_optional(QString::fromUtf8(*text))
		: std::nullopt;
}

} // namespace Twidget
