/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "history/view/media/history_view_media.h"
#include "twidget/twidget_layout.h"

namespace Twidget {

struct WidgetData;

class WidgetView final : public HistoryView::Media {
public:
	WidgetView(
		not_null<HistoryView::Element*> parent,
		std::shared_ptr<WidgetData> data,
		HistoryView::Element *replacing);
	~WidgetView();

	void draw(
		Painter &p,
		const HistoryView::PaintContext &context) const override;
	HistoryView::TextState textState(
		QPoint point,
		HistoryView::StateRequest request) const override;

	bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	bool dragItemByHandler(const ClickHandlerPtr &p) const override;

	bool isDisplayed() const override;
	bool hideMessageText() const override;
	bool hasTextForCopy() const override;
	bool needsBubble() const override;
	bool customInfoLayout() const override;
	bool overrideEditedDate() const override;
	HistoryMessageEdited *displayedEditBadge() const override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

private:
	using CachedLayout = std::pair<int, std::shared_ptr<const Layout>>;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;
	[[nodiscard]] QMargins padding() const;
	[[nodiscard]] int infoHeight() const;
	[[nodiscard]] int innerWidth(int width) const;
	[[nodiscard]] int heightFor(const Layout &layout) const;
	[[nodiscard]] std::shared_ptr<const Layout> layoutFor(int inner);
	void startTicking() const;
	void scheduleWakeUp() const;
	void wakeUp();

	const std::shared_ptr<WidgetData> _data;
	QByteArray _tree;
	std::shared_ptr<const Document> _document;
	std::vector<CachedLayout> _layouts;
	std::shared_ptr<const Layout> _layout;
	mutable base::Timer _timer;
	mutable int _wakeUps = 0;
	mutable bool _ticking = false;
	bool _rtl = false;

};

} // namespace Twidget
