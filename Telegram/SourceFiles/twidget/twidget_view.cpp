/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_view.h"

#include "base/debug_log.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "twidget/twidget_media.h"
#include "twidget/twidget_paint.h"
#include "twidget/twidget_text.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/style/style_core_direction.h"
#include "window/themes/window_theme.h"
#include "styles/style_chat.h"

namespace Twidget {
namespace {

constexpr auto kLayoutCacheSize = 8;

[[nodiscard]] const RealTextMeasurer &Measurer() {
	static const auto result = RealTextMeasurer();
	return result;
}

[[nodiscard]] Env LiveEnv() {
	return CurrentEnv(Window::Theme::IsNightMode());
}

} // namespace

WidgetView::WidgetView(
	not_null<HistoryView::Element*> parent,
	std::shared_ptr<WidgetData> data,
	HistoryView::Element *replacing)
: Media(parent)
, _data(std::move(data))
, _timer([=] { wakeUp(); })
, _rtl(style::RightToLeft()) {
	const auto previous = replacing
		? dynamic_cast<WidgetView*>(replacing->media())
		: nullptr;
	if (previous) {
		_tree = previous->_tree;
		_document = previous->_document;
		_layouts = previous->_layouts;
	}
	const auto due = _data->nextWakeupMs
		&& (*_data->nextWakeupMs <= LiveEnv().nowMs);
	const auto document = (!_data->document || due)
		? ResolveWidget(_data.get(), LiveEnv())
		: _data->document;
	if (!document) {
		_document = nullptr;
		_layouts.clear();
	} else if (_data->tree != _tree) {
		_tree = _data->tree;
		_document = document;
		_layouts.clear();
	}
	if (!isDisplayed()) {
		LOG(("Twidget Info: message %1 shows as text, %2."
			).arg(LogId(parent->data())
			).arg(document
				? u"it resolved to nothing"_q
				: u"it failed to resolve"_q));
	}
}

WidgetView::~WidgetView() = default;

void WidgetView::draw(
		Painter &p,
		const HistoryView::PaintContext &context) const {
	if (!_layout || !_layout->root) {
		return;
	}
	startTicking();
	const auto padding = this->padding();
	const auto palette = Palette(context.st, _parent->hasOutLayout());
	p.save();
	p.translate(padding.left(), padding.top());
	Paint(p, *_layout, {
		.measurer = &Measurer(),
		.palette = &palette,
		.rtl = _rtl,
	});
	p.restore();
}

HistoryView::TextState WidgetView::textState(
		QPoint point,
		HistoryView::StateRequest request) const {
	return HistoryView::TextState(_parent);
}

bool WidgetView::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	return true;
}

bool WidgetView::dragItemByHandler(const ClickHandlerPtr &p) const {
	return false;
}

bool WidgetView::isDisplayed() const {
	return _document && _document->root;
}

bool WidgetView::hideMessageText() const {
	return isDisplayed();
}

bool WidgetView::hasTextForCopy() const {
	return true;
}

bool WidgetView::needsBubble() const {
	return true;
}

bool WidgetView::customInfoLayout() const {
	return false;
}

bool WidgetView::overrideEditedDate() const {
	return isDisplayed();
}

HistoryMessageEdited *WidgetView::displayedEditBadge() const {
	return nullptr;
}

bool WidgetView::hasHeavyPart() const {
	return _ticking;
}

void WidgetView::unloadHeavyPart() {
	if (!_ticking) {
		return;
	}
	_ticking = false;
	_timer.cancel();
	LOG(("Twidget Info: message %1 stopped ticking after %2 wake-ups."
		).arg(LogId(_parent->data())
		).arg(_wakeUps));
	_wakeUps = 0;
}

QSize WidgetView::countOptimalSize() {
	if (!isDisplayed()) {
		return {};
	}
	const auto padding = this->padding();
	const auto skip = padding.left() + padding.right();
	auto maxWidth = st::msgMaxWidth;
	if (const auto width = _document->width) {
		const auto preferred = std::max(*width, 0.) * Measurer().unit();
		accumulate_min(maxWidth, int(std::ceil(preferred)) + skip);
	}
	return { maxWidth, heightFor(*layoutFor(maxWidth - skip)) };
}

QSize WidgetView::countCurrentSize(int newWidth) {
	if (!isDisplayed()) {
		return { newWidth, 0 };
	}
	accumulate_min(newWidth, maxWidth());
	_layout = layoutFor(innerWidth(newWidth));
	return { newWidth, heightFor(*_layout) };
}

QMargins WidgetView::padding() const {
	const auto &padding = st::msgPadding;
	return {
		padding.left(),
		isBubbleTop() ? padding.top() : st::mediaInBubbleSkip,
		padding.right(),
		isBubbleBottom() ? padding.bottom() : st::mediaInBubbleSkip,
	};
}

int WidgetView::infoHeight() const {
	return isBubbleBottom() ? _parent->skipBlockHeight() : 0;
}

int WidgetView::innerWidth(int width) const {
	const auto padding = this->padding();
	return std::max(width - padding.left() - padding.right(), 0);
}

int WidgetView::heightFor(const Layout &layout) const {
	const auto padding = this->padding();
	return int(std::ceil(layout.height * Measurer().unit()))
		+ padding.top()
		+ padding.bottom()
		+ infoHeight();
}

std::shared_ptr<const Layout> WidgetView::layoutFor(int inner) {
	const auto i = ranges::find(_layouts, inner, &CachedLayout::first);
	if (i != end(_layouts)) {
		return i->second;
	}
	auto result = std::make_shared<const Layout>(LayOut(
		*_document,
		inner / Measurer().unit(),
		_rtl,
		Measurer()));
	if (int(_layouts.size()) >= kLayoutCacheSize) {
		_layouts.erase(begin(_layouts));
	}
	_layouts.emplace_back(inner, result);
	return result;
}

void WidgetView::startTicking() const {
	if (_ticking || !_data->nextWakeupMs) {
		return;
	}
	_ticking = true;
	history()->owner().registerHeavyViewPart(_parent);
	LOG(("Twidget Info: message %1 ticks while on screen."
		).arg(LogId(_parent->data())));
	scheduleWakeUp();
}

void WidgetView::scheduleWakeUp() const {
	if (const auto wakeUp = _data->nextWakeupMs) {
		const auto delay = *wakeUp - LiveEnv().nowMs;
		_timer.callOnce(std::max(delay, int64()));
	}
}

void WidgetView::wakeUp() {
	++_wakeUps;
	const auto document = ResolveWidget(_data.get(), LiveEnv());
	if (document && _data->tree != _tree) {
		const auto wasHeight = height();
		_tree = _data->tree;
		_document = document;
		_layouts.clear();
		_layout = layoutFor(innerWidth(width()));
		if (heightFor(*_layout) != wasHeight) {
			history()->owner().requestViewResize(_parent);
		} else {
			repaint();
		}
	}
	if (_ticking) {
		scheduleWakeUp();
	}
}

} // namespace Twidget
