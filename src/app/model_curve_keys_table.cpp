#include "app/model_curve_keys_table.h"
#include "app/locale_number.h"
#include <QCoreApplication>
#include <algorithm>
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelCurveKeysDialog)
};
} // namespace
void ModelCurveKeysTable::setGroups(const QVector<ModelPoseCurves> &groups)
{
	m_rows.groups = groups;
	filter({}, -1);
}
void ModelCurveKeysTable::filter(const QString &joint, int channel)
{
	beginResetModel();
	m_rows.segments.clear();
	m_rows.count = 0;
	for (int g = 0; g < m_rows.groups.size(); ++g)
	{
		if (!joint.isEmpty() && m_rows.groups[g].joint != joint)
			continue;
		for (int c = 0; c < 9; ++c)
			if (channel < 0 || c == channel)
			{
				const int count = int(m_rows.groups[g].channels[c].size());
				if (count)
					m_rows.segments.append({g, c, m_rows.count, count});
				m_rows.count += count;
			}
	}
	endResetModel();
}
QVariant ModelCurveKeysTable::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.count ||
		(role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole))
		return {};
	auto it = std::upper_bound(m_rows.segments.cbegin(), m_rows.segments.cend(), index.row(),
							   [](int row, const auto &s) { return row < s.first; });
	--it;
	const auto &group = m_rows.groups[it->group];
	const auto &key = group.channels[it->channel][index.row() - it->first];
	switch (index.column())
	{
	case 0:
		return group.joint;
	case 1:
		return modelCurveChannelId(it->channel);
	case 2:
		return key.frame;
	case 3:
		return localeNumber(key.value, 12);
	default:
		return {};
	}
}
QVariant ModelCurveKeysTable::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole)
		return {};
	if (orientation == Qt::Vertical)
		return section + 1;
	return QStringList{Text::tr("Joint"), Text::tr("Channel"), Text::tr("Sample"), Text::tr("Value")}.value(section);
}
QItemSelection ModelCurveKeysTable::sampleRange(int first, int last) const
{
	QItemSelection selection;
	for (const auto &s : m_rows.segments)
	{
		const auto &keys = m_rows.groups[s.group].channels[s.channel];
		const auto a = std::lower_bound(keys.cbegin(), keys.cend(), first, [](const auto &k, int f) { return k.frame < f; });
		const auto b = std::upper_bound(keys.cbegin(), keys.cend(), last, [](int f, const auto &k) { return f < k.frame; });
		if (a < b)
			selection.select(index(s.first + int(a - keys.cbegin()), 0), index(s.first + int(b - keys.cbegin()) - 1, 3));
	}
	return selection;
}
bool modelCurveKeysFromRows(const ModelCurveKeyRows &rows, QVector<QPair<int, int>> ranges, QVector<ModelCurveKeySelection> *selection,
							QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!selection || !work.check())
		return false;
	std::sort(ranges.begin(), ranges.end());
	QVector<QPair<int, int>> merged;
	for (const auto &range : ranges)
	{
		if (range.first < 0 || range.second < range.first || range.second >= rows.count)
		{
			if (error)
				*error = Text::tr("Key selection no longer matches the displayed rows.");
			return false;
		}
		if (!merged.isEmpty() && range.first <= merged.last().second + 1)
			merged.last().second = std::max(merged.last().second, range.second);
		else
			merged.append(range);
	}
	QVector<ModelCurveKeySelection> result;
	int range = 0;
	for (const auto &s : rows.segments)
	{
		if (!work.check())
			return false;
		while (range < merged.size() && merged[range].second < s.first)
			++range;
		ModelCurveKeySelection target{rows.groups[s.group].joint, s.channel, {}};
		for (int i = range; i < merged.size() && merged[i].first < s.first + s.count; ++i)
			for (int row = std::max(s.first, merged[i].first); row <= std::min(s.first + s.count - 1, merged[i].second); ++row)
			{
				if (!work.step())
					return false;
				target.samples.append(rows.groups[s.group].channels[s.channel][row - s.first].frame);
			}
		if (!target.samples.isEmpty())
			result.append(target);
	}
	if (result.isEmpty())
	{
		if (error)
			*error = Text::tr("Select at least one existing key.");
		return false;
	}
	if (!work.check())
		return false;
	*selection = std::move(result);
	return true;
}
QVariant ModelCurveKeyReportTable::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_report.channels.size() ||
		(role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole))
		return {};
	const auto &c = m_report.channels[index.row()];
	switch (index.column())
	{
	case 0:
		return c.joint;
	case 1:
		return modelCurveChannelId(c.channel);
	case 2:
		return c.selected;
	case 3:
		return c.before;
	case 4:
		return c.after;
	case 5:
		return localeNumber(c.maximumChange, 12);
	default:
		return {};
	}
}
QVariant ModelCurveKeyReportTable::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole)
		return {};
	if (orientation == Qt::Vertical)
		return section + 1;
	return QStringList{Text::tr("Joint"),		Text::tr("Channel"),	Text::tr("Selected keys"),
					   Text::tr("Keys before"), Text::tr("Keys after"), Text::tr("Largest value change")}
		.value(section);
}
} // namespace vibestudio
