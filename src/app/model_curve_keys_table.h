#pragma once
#include "core/model_curve_key_tools.h"
#include <QAbstractTableModel>
#include <QItemSelection>
namespace vibestudio
{
struct ModelCurveKeyRows
{
	struct Segment
	{
		int group = 0, channel = 0, first = 0, count = 0;
	};
	QVector<ModelPoseCurves> groups;
	QVector<Segment> segments;
	int count = 0;
};
class ModelCurveKeysTable final : public QAbstractTableModel
{
  public:
	explicit ModelCurveKeysTable(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
	void setGroups(const QVector<ModelPoseCurves> &groups);
	void filter(const QString &joint, int channel);
	ModelCurveKeyRows snapshot() const
	{
		return m_rows;
	}
	QItemSelection sampleRange(int first, int last) const;
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : m_rows.count;
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 4;
	}
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  private:
	ModelCurveKeyRows m_rows;
};
// Value snapshot conversion runs on the review worker, without touching widgets.
bool modelCurveKeysFromRows(const ModelCurveKeyRows &rows, QVector<QPair<int, int>> ranges, QVector<ModelCurveKeySelection> *selection,
							QString *error, const ModelWorkControl &control = {});
class ModelCurveKeyReportTable final : public QAbstractTableModel
{
  public:
	explicit ModelCurveKeyReportTable(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
	void setReport(const ModelCurveKeyReport &report)
	{
		beginResetModel();
		m_report = report;
		endResetModel();
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : int(m_report.channels.size());
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 6;
	}
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  private:
	ModelCurveKeyReport m_report;
};
} // namespace vibestudio
