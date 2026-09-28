/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <optional>

struct TWidgetInstance;

namespace Twidget {

struct Env {
	int64 nowMs = 0;
	int32 tzOffsetMinutes = 0;
	bool dark = false;
	bool rtl = false;
};

struct Resolved {
	QByteArray tree;
	std::optional<int64> nextWakeupMs;
};

struct InstanceDeleter {
	void operator()(TWidgetInstance *value) const;
};

using InstanceHandle = std::unique_ptr<TWidgetInstance, InstanceDeleter>;

void CheckAbiVersion();
[[nodiscard]] bool Available();

[[nodiscard]] Env CurrentEnv(bool dark);

[[nodiscard]] InstanceHandle LoadInstance(const QByteArray &envelope);

[[nodiscard]] std::optional<Resolved> Resolve(
	not_null<TWidgetInstance*> instance,
	const Env &env);
[[nodiscard]] std::optional<QString> Summary(
	not_null<TWidgetInstance*> instance,
	const Env &env);

} // namespace Twidget
