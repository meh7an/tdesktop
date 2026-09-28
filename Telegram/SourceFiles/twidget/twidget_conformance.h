/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <vector>

namespace Twidget {

struct ConformanceResult {
	QString name;
	bool passed = false;
	QString problem;
};

[[nodiscard]] std::vector<ConformanceResult> RunConformance();

} // namespace Twidget
