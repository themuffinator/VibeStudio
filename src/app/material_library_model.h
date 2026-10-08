#pragma once

// The Materials page's library list: one row per material in the open
// package or script, with swatches rendered off the GUI thread (animated
// ones keep moving while they are on screen) and the studio's filter
// language over each material's properties.

#include "core/material_images.h"
#include "core/material_library.h"
#include "core/studio_query.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QSortFilterProxyModel>

#include <atomic>
#include <functional>
#include <memory>

class QThreadPool;
class QTimer;

namespace vibestudio {

// What the list needs for a library, prepared with the scan: the filter
// properties of every entry.
struct MaterialLibraryRows {
	std::shared_ptr<const MaterialLibrary> library;
	QVector<StudioQueryProperties> properties;
};

MaterialLibraryRows prepareMaterialLibraryRows(std::shared_ptr<const MaterialLibrary> library);

class MaterialLibraryModel final : public QAbstractListModel {
	Q_OBJECT

public:
	enum Role {
		EntryRole = Qt::UserRole + 1,
		NameRole,
		EngineRole,
		KindRole,
		AnimatedRole,
		ShadowedRole,
	};

	explicit MaterialLibraryModel(QObject* parent = nullptr);
	~MaterialLibraryModel() override;

	// `cache` backs images.cache and stays alive while swatches draw.
	void setRows(MaterialLibraryRows rows, const MaterialImageSource& images, std::shared_ptr<MaterialImageCache> cache);
	void clear();
	[[nodiscard]] std::shared_ptr<const MaterialLibrary> library() const { return m_rows.library; }
	[[nodiscard]] const StudioQueryProperties& properties(int row) const;
	[[nodiscard]] QSet<QString> propertyKeys() const;
	[[nodiscard]] int rowForName(const QString& name, MaterialEngine engine = MaterialEngine::Unknown) const;
	void setThumbnailSide(int side);
	[[nodiscard]] int thumbnailSide() const { return m_side; }
	// The size of each item in the grid, so names use the whole cell; an
	// invalid size leaves sizing to the view (the list).
	void setItemSize(const QSize& size);
	void setAnimateThumbnails(bool animate);
	[[nodiscard]] bool animateThumbnails() const { return m_animate; }
	// The rows on screen, where animated swatches play.
	void setVisibleRows(const QVector<int>& rows);
	[[nodiscard]] int pendingThumbnails() const;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;

private:
	void requestThumbnail(int row, double time) const;
	void deliverThumbnail(quint64 generation, int row, const QImage& image);
	void advanceAnimation();
	[[nodiscard]] QString rowDescription(int row) const;

	MaterialLibraryRows m_rows;
	MaterialImageSource m_images;
	std::shared_ptr<MaterialImageCache> m_cache;
	int m_side = 72;
	QSize m_itemSize;
	bool m_animate = false;
	double m_time = 0.0;
	quint64 m_generation = 0;
	mutable QHash<int, QPixmap> m_thumbnails;
	mutable QSet<int> m_requested;
	mutable QPixmap m_placeholder;
	QVector<int> m_visible;
	QThreadPool* m_pool = nullptr;
	QTimer* m_animation = nullptr;
	std::shared_ptr<std::atomic_int> m_pending;
};

class MaterialLibraryFilter final : public QSortFilterProxyModel {
public:
	explicit MaterialLibraryFilter(QObject* parent = nullptr);

	void setQuery(const QString& text);
	[[nodiscard]] QString queryText() const { return m_text; }
	void setEngine(MaterialEngine engine);
	[[nodiscard]] MaterialEngine engine() const { return m_engine; }
	void setShowShadowed(bool show);
	// Keys the query tests that no material has: a likely typo.
	[[nodiscard]] QStringList unknownKeys() const;

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
	void refilter(const std::function<void()>& change);

	QString m_text;
	StudioQuery m_query;
	MaterialEngine m_engine = MaterialEngine::Unknown;
	bool m_showShadowed = true;
};

} // namespace vibestudio
