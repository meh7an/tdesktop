/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_view.h"

#include "base/debug_log.h"
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

[[nodiscard]] const RealTextMeasurer &Measurer() {
	static const auto result = RealTextMeasurer();
	return result;
}

} // namespace

WidgetView::WidgetView(
	not_null<HistoryView::Element*> parent,
	std::shared_ptr<WidgetData> data)
: Media(parent)
, _data(std::move(data))
, _rtl(style::RightToLeft()) {
	_document = ResolveWidget(
		_data.get(),
		CurrentEnv(Window::Theme::IsNightMode()));
	if (!isDisplayed()) {
		LOG(("Twidget Info: message %1 shows as text, %2."
			).arg(LogId(parent->data())
			).arg(_document
				? u"it resolved to nothing"_q
				: u"it failed to resolve"_q));
	}
}

WidgetView::~WidgetView() = default;

void WidgetView::draw(
		Painter &p,
		const HistoryView::PaintContext &context) const {
	if (!_layout.root) {
		return;
	}
	const auto padding = this->padding();
	const auto palette = Palette(context.st, _parent->hasOutLayout());
	p.save();
	p.translate(padding.left(), padding.top());
	Paint(p, _layout, {
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

QSize WidgetView::countOptimalSize() {
	if (!isDisplayed()) {
		return {};
	}
	const auto padding = this->padding();
	const auto skip = padding.left() + padding.right();
	const auto unit = Measurer().unit();
	auto maxWidth = st::msgMaxWidth;
	if (const auto width = _document->width) {
		accumulate_min(
			maxWidth,
			int(std::ceil(std::max(*width, 0.) * unit)) + skip);
	}
	const auto layout = LayOut(
		*_document,
		(maxWidth - skip) / unit,
		_rtl,
		Measurer());
	return { maxWidth, heightFor(layout) };
}

QSize WidgetView::countCurrentSize(int newWidth) {
	if (!isDisplayed()) {
		return { newWidth, 0 };
	}
	accumulate_min(newWidth, maxWidth());
	const auto padding = this->padding();
	const auto inner = newWidth - padding.left() - padding.right();
	_layout = LayOut(
		*_document,
		std::max(inner, 0) / Measurer().unit(),
		_rtl,
		Measurer());
	return { newWidth, heightFor(_layout) };
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

int WidgetView::heightFor(const Layout &layout) const {
	const auto padding = this->padding();
	return int(std::ceil(layout.height * Measurer().unit()))
		+ padding.top()
		+ padding.bottom()
		+ infoHeight();
}

} // namespace Twidget
