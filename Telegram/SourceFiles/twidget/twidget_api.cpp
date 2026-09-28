/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_api.h"

#include "base/debug_log.h"
#include "base/flat_map.h"
#include "base/options.h"
#include "base/unixtime.h"
#include "ui/style/style_core_direction.h"

#include <twidget.h>

#include <QtCore/QDateTime>
#include <QtCore/QFile>

namespace Twidget {
namespace {

constexpr auto kReadAttempts = 3;
constexpr auto kFixedNowMs = int64(1790415687123);
constexpr auto kFixedTzOffsetMinutes = 210;

base::options::toggle LiveWidgetsOption({
	.id = kOptionLiveWidgets,
	.name = "Live widgets",
	.description = "Show messages with a tgw widget block as native widgets.",
	.defaultValue = true,
	.restartRequired = true,
});

struct TemplateDeleter {
	void operator()(TWidgetTemplate *value) const;
};

using TemplateHandle = std::unique_ptr<TWidgetTemplate, TemplateDeleter>;

struct Output {
	QByteArray bytes;
	int32_t status = TWIDGET_OK;
};

auto AbiMatches = false;
auto Templates = base::flat_map<QString, TemplateHandle>();

void TemplateDeleter::operator()(TWidgetTemplate *value) const {
	twidget_template_drop(value);
}

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
[[nodiscard]] Output ReadOutput(Call &&call) {
	auto result = Output();
	for (auto attempt = 0; attempt != kReadAttempts; ++attempt) {
		auto length = size_t(0);
		result.status = call(
			(result.bytes.isEmpty()
				? nullptr
				: reinterpret_cast<uint8_t*>(result.bytes.data())),
			size_t(result.bytes.size()),
			&length);
		if (result.status == TWIDGET_OK) {
			result.bytes.resize(qsizetype(length));
			return result;
		} else if (result.status != TWIDGET_ERROR_BUFFER_TOO_SMALL) {
			result.bytes = QByteArray();
			return result;
		}
		result.bytes.resize(qsizetype(length));
	}
	result.bytes = QByteArray();
	return result;
}

[[nodiscard]] TemplateHandle MakeTemplate(const QByteArray &json) {
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

[[nodiscard]] InstanceHandle MakeInstance(
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

[[nodiscard]] const TWidgetTemplate *BundledTemplate(
		const QString &id,
		uint32_t version) {
	const auto name = u"%1@%2"_q.arg(id).arg(version);
	const auto i = Templates.find(name);
	if (i != Templates.end()) {
		return i->second.get();
	}
	auto file = QFile(u":/tgw/templates/%1.json"_q.arg(name));
	auto loaded = TemplateHandle();
	if (file.open(QIODevice::ReadOnly)) {
		loaded = MakeTemplate(file.readAll());
	} else {
		LOG(("Twidget Error: no bundled template %1.").arg(name));
	}
	if (loaded) {
		LOG(("Twidget Info: bundled template %1 loaded.").arg(name));
	}
	return Templates.emplace(name, std::move(loaded)).first->second.get();
}

} // namespace

const char kOptionLiveWidgets[] = "live-widgets";

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

bool Enabled() {
	return AbiMatches && LiveWidgetsOption.value();
}

Env CurrentEnv(bool dark) {
	return {
		.nowMs = ServerNowMs(),
		.tzOffsetMinutes = QDateTime::currentDateTime().offsetFromUtc() / 60,
		.dark = dark,
		.rtl = style::RightToLeft(),
	};
}

Env FixedEnv(bool dark, bool rtl) {
	return {
		.nowMs = kFixedNowMs,
		.tzOffsetMinutes = kFixedTzOffsetMinutes,
		.dark = dark,
		.rtl = rtl,
	};
}

InstanceHandle LoadInstance(const QByteArray &envelope) {
	auto version = uint32_t(0);
	const auto call = [&](uint8_t *out, size_t capacity, size_t *length) {
		return twidget_envelope_template_ref(
			Bytes(envelope),
			size_t(envelope.size()),
			out,
			capacity,
			length,
			&version);
	};
	const auto ref = ReadOutput(call);
	if (ref.status == TWIDGET_ERROR_NOT_FOUND) {
		return MakeInstance(envelope, nullptr);
	} else if (ref.status != TWIDGET_OK) {
		LogFailure(u"twidget_envelope_template_ref"_q, ref.status);
		return nullptr;
	}
	const auto bundled = BundledTemplate(
		QString::fromUtf8(ref.bytes),
		version);
	return bundled ? MakeInstance(envelope, bundled) : nullptr;
}

std::optional<Resolved> Resolve(
		not_null<TWidgetInstance*> instance,
		const Env &env) {
	const auto raw = Serialize(env);
	auto wakeup = int64_t(TWIDGET_NO_WAKEUP);
	const auto call = [&](uint8_t *out, size_t capacity, size_t *length) {
		return twidget_resolve(instance, &raw, out, capacity, length, &wakeup);
	};
	auto output = ReadOutput(call);
	if (output.status != TWIDGET_OK) {
		LogFailure(u"twidget_resolve"_q, output.status);
		return std::nullopt;
	}
	return Resolved{
		.tree = std::move(output.bytes),
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
	const auto output = ReadOutput(call);
	if (output.status != TWIDGET_OK) {
		LogFailure(u"twidget_summary"_q, output.status);
		return std::nullopt;
	}
	return QString::fromUtf8(output.bytes);
}

} // namespace Twidget
