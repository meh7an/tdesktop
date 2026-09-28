/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "twidget/twidget_layout.h"

namespace Twidget {

struct WidgetData;

class WidgetView final : public HistoryView::Media {
public:
	WidgetView(
		not_null<HistoryView::Element*> parent,
		std::shared_ptr<WidgetData> data);
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

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;
	[[nodiscard]] QMargins padding() const;
	[[nodiscard]] int infoHeight() const;
	[[nodiscard]] int heightFor(const Layout &layout) const;

	const std::shared_ptr<WidgetData> _data;
	std::shared_ptr<const Document> _document;
	Layout _layout;
	bool _rtl = false;

};

} // namespace Twidget
