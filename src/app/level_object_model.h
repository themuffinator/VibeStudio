#pragma once

#include "core/level_map.h"
#include <QAbstractListModel>
#include <QBitArray>
#include <functional>

namespace vibestudio {

// Immutable, implicitly shared source data. Formatting is independent of widgets
// so a filter can inspect it on a worker without touching the live model.
struct LevelObjectRows {
	struct Row { LevelMapSelectionRef reference; int sourceIndex = 0; };
	LevelMapDocument document;
	QVector<Row> rows;
	QBitArray hidden;
	[[nodiscard]] QString selector(int row) const;
	[[nodiscard]] QPair<QString, QString> text(int row) const;
	[[nodiscard]] QString description(int row) const;
};

class LevelObjectModel final : public QAbstractListModel {
public:
	explicit LevelObjectModel(QObject* parent = nullptr);
	void setDocument(const LevelMapDocument& document);
	bool refreshHidden(const std::function<bool(LevelMapSelectionRef)>& predicate);
	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
	[[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
	[[nodiscard]] int objectCount() const;
	[[nodiscard]] LevelMapSelectionRef reference(int row) const;
	[[nodiscard]] int rowForReference(LevelMapSelectionRef reference) const;
	[[nodiscard]] const LevelObjectRows& snapshot() const { return m_source; }

private:
	LevelObjectRows m_source;
	QHash<quint64, int> m_rowsById;
};

} // namespace vibestudio
