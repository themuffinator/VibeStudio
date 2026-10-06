#pragma once

#include "app/level_object_model.h"
#include <QListView>
#include <memory>

class QThread;
class QTimer;

namespace vibestudio {

class LevelObjectList final : public QListView {
	Q_OBJECT
public:
	explicit LevelObjectList(QWidget* parent = nullptr);
	~LevelObjectList() override;
	void setDocument(const LevelMapDocument& document);
	void refreshHidden(const std::function<bool(LevelMapSelectionRef)>& predicate);
	void setFilterText(const QString& text);
	[[nodiscard]] bool isFiltering() const { return m_filtering; }
	[[nodiscard]] int matchingCount() const { return m_matchingCount; }
	[[nodiscard]] const QSet<QString>& knownQueryKeys() const { return m_knownKeys; }
	[[nodiscard]] LevelObjectModel* objectModel() const { return m_objects; }
	[[nodiscard]] int rowForSelector(const QString& selector) const;
	[[nodiscard]] QVector<LevelMapSelectionRef> selectedReferences() const;
	// Synchronization helpers suppress selectionEdited; callers own the document
	// update. selectRows accepts ascending, unique row indices and batches ranges.
	void setSelectedReferences(const QVector<LevelMapSelectionRef>& selected, LevelMapSelectionRef primary);
	void selectRows(const QVector<int>& rows, int primary);

Q_SIGNALS:
	void selectionEdited();
	void filterStateChanged();
	void filterFinished();

protected:
	void changeEvent(QEvent* event) override;

private:
	struct Work;
	void requestFilter();
	void startFilter();
	void applyFilter(const QBitArray& hidden);
	void setFiltering(bool filtering);
	LevelObjectModel* m_objects = nullptr;
	QTimer* m_debounce = nullptr;
	QThread* m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	QString m_filter;
	quint64 m_generation = 0;
	quint64 m_sourceGeneration = 0;
	bool m_filtering = false;
	int m_matchingCount = 0;
	QSet<QString> m_knownKeys;
	QHash<QString, LevelMapObjectProperties> m_properties;
	bool m_propertiesReady = false;
};

} // namespace vibestudio
