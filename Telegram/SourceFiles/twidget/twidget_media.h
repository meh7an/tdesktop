/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_media_types.h"
#include "twidget/twidget_api.h"

namespace Twidget {

struct Document;

struct WidgetData {
	QString envelope;
	InstanceHandle instance;
	QByteArray tree;
	std::shared_ptr<const Document> document;
};

[[nodiscard]] std::shared_ptr<const Document> ResolveWidget(
	not_null<WidgetData*> data,
	const Env &env);

[[nodiscard]] QString LogId(not_null<HistoryItem*> item);

class MediaWidget final : public Data::Media {
public:
	MediaWidget(
		not_null<HistoryItem*> parent,
		std::shared_ptr<WidgetData> data,
		std::unique_ptr<Data::Media> replaced);
	~MediaWidget();

	[[nodiscard]] const QString &envelope() const;
	[[nodiscard]] std::unique_ptr<Data::Media> takeReplaced();

	std::unique_ptr<Data::Media> clone(not_null<HistoryItem*> parent) override;

	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool allowsEdit() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	const std::shared_ptr<WidgetData> _data;
	std::unique_ptr<Data::Media> _replaced;

};

void RefreshItemMedia(not_null<HistoryItem*> item);

} // namespace Twidget
