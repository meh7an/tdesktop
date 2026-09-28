/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget/twidget_media.h"

#include "base/debug_log.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "twidget/twidget_model.h"
#include "twidget/twidget_view.h"
#include "window/themes/window_theme.h"

namespace Twidget {
namespace {

[[nodiscard]] QString FindEnvelope(const TextWithEntities &text) {
	for (const auto &entity : text.entities) {
		if (entity.type() == EntityType::Pre && entity.data() == u"tgw"_q) {
			return text.text.mid(entity.offset(), entity.length());
		}
	}
	return QString();
}

} // namespace

std::shared_ptr<const Document> ResolveWidget(
		not_null<WidgetData*> data,
		const Env &env) {
	const auto resolved = Resolve(data->instance.get(), env);
	if (!resolved) {
		return nullptr;
	} else if (resolved->tree != data->tree || !data->document) {
		auto document = ParseResolved(resolved->tree);
		data->tree = resolved->tree;
		data->document = document
			? std::make_shared<const Document>(std::move(*document))
			: nullptr;
	}
	return data->document;
}

QString LogId(not_null<HistoryItem*> item) {
	return u"%1/%2"_q.arg(item->history()->peer->id.value).arg(item->id.bare);
}

MediaWidget::MediaWidget(
	not_null<HistoryItem*> parent,
	std::shared_ptr<WidgetData> data,
	std::unique_ptr<Data::Media> replaced)
: Media(parent)
, _data(std::move(data))
, _replaced(std::move(replaced)) {
}

MediaWidget::~MediaWidget() = default;

const QString &MediaWidget::envelope() const {
	return _data->envelope;
}

std::unique_ptr<Data::Media> MediaWidget::takeReplaced() {
	return std::move(_replaced);
}

std::unique_ptr<Data::Media> MediaWidget::clone(
		not_null<HistoryItem*> parent) {
	return _replaced ? _replaced->clone(parent) : nullptr;
}

TextWithEntities MediaWidget::notificationText() const {
	return parent()->originalText();
}

QString MediaWidget::pinnedTextSubstring() const {
	return QString();
}

TextForMimeData MediaWidget::clipboardText() const {
	return TextForMimeData();
}

bool MediaWidget::allowsEdit() const {
	return true;
}

bool MediaWidget::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaWidget::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaWidget::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<WidgetView>(message, _data);
}

void RefreshItemMedia(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto current = dynamic_cast<MediaWidget*>(media);
	const auto envelope = Enabled()
		? FindEnvelope(item->originalText())
		: QString();
	if (envelope.isEmpty()) {
		if (current) {
			item->overrideMedia(current->takeReplaced());
		}
		return;
	} else if (current && current->envelope() == envelope) {
		return;
	} else if (media && !current && !media->webpage()) {
		return;
	}
	auto data = std::make_shared<WidgetData>(WidgetData{
		.envelope = envelope,
		.instance = LoadInstance(envelope.toUtf8()),
	});
	const auto env = CurrentEnv(Window::Theme::IsNightMode());
	if (!data->instance || !ResolveWidget(data.get(), env)) {
		LOG(("Twidget Info: message %1 stays text, its envelope failed."
			).arg(LogId(item)));
		if (current) {
			item->overrideMedia(current->takeReplaced());
		}
		return;
	}
	auto replaced = current
		? current->takeReplaced()
		: media
		? media->clone(item)
		: nullptr;
	item->overrideMedia(std::make_unique<MediaWidget>(
		item,
		std::move(data),
		std::move(replaced)));
}

} // namespace Twidget
