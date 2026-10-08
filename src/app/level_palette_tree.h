#pragma once

#include "app/map_viewport.h"

#include <QMimeData>
#include <QTreeWidget>

namespace vibestudio {

// A Levels browser list whose rows drag out as what they place: entity
// classes, Doom thing types, models and sounds, each row carrying its payload
// ("entity:light", "thing:3004", "model:progs/...", "sound:sound/...") in
// Qt::UserRole. The map views and the camera take the drop.
class MapPaletteTree final : public QTreeWidget {
public:
	using QTreeWidget::QTreeWidget;

protected:
	// Only ever a copy: a move that the viewport accepted, with Shift held,
	// would have the view take the row out of the palette.
	void startDrag(Qt::DropActions supportedActions) override
	{
		QTreeWidget::startDrag(supportedActions & Qt::CopyAction);
	}

	[[nodiscard]] QStringList mimeTypes() const override
	{
		return {QString::fromLatin1(kMapPaletteMimeType)};
	}

	[[nodiscard]] QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
	{
		auto* data = new QMimeData;
		for (const QTreeWidgetItem* item : items) {
			const QString payload = item->data(0, Qt::UserRole).toString();
			if (!payload.isEmpty()) {
				data->setData(QString::fromLatin1(kMapPaletteMimeType), payload.toUtf8());
				break;
			}
		}
		return data;
	}
};

} // namespace vibestudio
