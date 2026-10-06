#include "app/level_object_model.h"
#include "app/studio_theme.h"
#include <QCoreApplication>
#include <algorithm>

namespace vibestudio {
namespace {
quint64 referenceKey(LevelMapSelectionRef ref)
{
	return (quint64(ref.kind) << 32) | quint32(ref.objectId);
}
QString singleLine(QString text)
{
	text.replace(QLatin1Char('\n'), QLatin1Char(' '));
	text.replace(QLatin1Char('\r'), QLatin1Char(' '));
	text.replace(QChar::LineSeparator, QLatin1Char(' '));
	text.replace(QChar::ParagraphSeparator, QLatin1Char(' '));
	return text;
}
} // namespace

QString LevelObjectRows::selector(int row) const
{
	return row >= 0 && row < rows.size() ? levelMapSelectionRefId(rows[row].reference) : QString();
}

QPair<QString, QString> LevelObjectRows::text(int row) const
{
	if (row < 0 || row >= rows.size()) { return {}; }
	const auto& entry = rows[row];
	const int id = entry.reference.objectId, at = entry.sourceIndex;
	// Retain the existing translation context when moving labels out of the shell.
	switch (entry.reference.kind) {
	case LevelMapSelectionKind::Entity: {
		const auto& entity = document.entities[at];
		QString detail = QCoreApplication::translate("vibestudio::ApplicationShell", "Origin %1 / %2 keys")
			.arg(entity.origin.valid ? QStringLiteral("%1,%2,%3").arg(entity.origin.x, 0, 'f', 0).arg(entity.origin.y, 0, 'f', 0)
				.arg(entity.origin.z, 0, 'f', 0) : QCoreApplication::translate("vibestudio::ApplicationShell", "unknown"))
			.arg(entity.properties.size());
		for (const auto& property : entity.properties) {
			if (property.value.isEmpty()) { continue; }
			if (property.key.compare(QStringLiteral("targetname"), Qt::CaseInsensitive) == 0) {
				detail += QCoreApplication::translate("vibestudio::ApplicationShell", " · named %1").arg(property.value);
			} else if (property.key.compare(QStringLiteral("target"), Qt::CaseInsensitive) == 0) {
				detail += QCoreApplication::translate("vibestudio::ApplicationShell", " · fires %1").arg(property.value);
			}
		}
		return {QCoreApplication::translate("vibestudio::ApplicationShell", "Entity %1: %2").arg(id).arg(entity.className), detail};
	}
	case LevelMapSelectionKind::QuakeBrush: {
		const auto& brush = document.brushes[at];
		return {QCoreApplication::translate("vibestudio::ApplicationShell", "Brush %1").arg(id),
			QCoreApplication::translate("vibestudio::ApplicationShell", "Entity %1 / %2 faces").arg(brush.entityId).arg(brush.faceCount)};
	}
	case LevelMapSelectionKind::QuakePatch: {
		const auto& patch = document.patches[at];
		return {QCoreApplication::translate("vibestudio::ApplicationShell", "Patch %1").arg(id),
			QCoreApplication::translate("vibestudio::ApplicationShell", "Entity %1 / %2 by %3 / %4")
				.arg(patch.entityId).arg(patch.width).arg(patch.height).arg(patch.textureName)};
	}
	case LevelMapSelectionKind::DoomVertex: {
		const auto& vertex = document.doomVertices[at];
		return {QCoreApplication::translate("vibestudio::ApplicationShell", "Vertex %1").arg(id),
			QCoreApplication::translate("vibestudio::ApplicationShell", "%1, %2").arg(vertex.x, 0, 'f', 0).arg(vertex.y, 0, 'f', 0)};
	}
	case LevelMapSelectionKind::DoomLinedef: {
		const auto& line = document.doomLinedefs[at];
		return {QCoreApplication::translate("vibestudio::ApplicationShell", "Linedef %1").arg(id),
			QCoreApplication::translate("vibestudio::ApplicationShell", "%1 > %2 / tag %3")
				.arg(line.startVertex).arg(line.endVertex).arg(line.tag)};
	}
	case LevelMapSelectionKind::DoomSector: {
		const auto& sector = document.doomSectors[at];
		return {QCoreApplication::translate("vibestudio::ApplicationShell", "Sector %1").arg(id),
			QCoreApplication::translate("vibestudio::ApplicationShell", "Floor %1 / ceiling %2 / tag %3")
				.arg(sector.floorHeight).arg(sector.ceilingHeight).arg(sector.tag)};
	}
	case LevelMapSelectionKind::DoomThing: {
		const auto& thing = document.doomThings[at];
		return {QCoreApplication::translate("vibestudio::ApplicationShell", "Thing %1").arg(id),
			QCoreApplication::translate("vibestudio::ApplicationShell", "Type %1 at %2, %3")
				.arg(thing.type).arg(thing.x, 0, 'f', 0).arg(thing.y, 0, 'f', 0)};
	}
	default: return {};
	}
}

QString LevelObjectRows::description(int row) const
{
	const auto [label, detail] = text(row);
	return row >= 0 && row < hidden.size() && hidden.testBit(row)
		? QCoreApplication::translate("vibestudio::ApplicationShell", "%1\n%2 · hidden").arg(label, detail)
		: QStringLiteral("%1\n%2").arg(label, detail);
}

LevelObjectModel::LevelObjectModel(QObject* parent) : QAbstractListModel(parent) {}

void LevelObjectModel::setDocument(const LevelMapDocument& document)
{
	beginResetModel();
	m_source = {};
	m_source.document = document;
	m_rowsById.clear();
	const auto append = [this](const auto& objects, LevelMapSelectionKind kind) {
		for (int i = 0; i < objects.size(); ++i) {
			const LevelMapSelectionRef ref{kind, objects[i].id};
			m_rowsById.insert(referenceKey(ref), static_cast<int>(m_source.rows.size()));
			m_source.rows.append({ref, i});
		}
	};
	if (document.format != LevelMapFormat::Unknown) {
		if (document.format != LevelMapFormat::DoomWad) { append(document.entities, LevelMapSelectionKind::Entity); }
		append(document.brushes, LevelMapSelectionKind::QuakeBrush);
		append(document.patches, LevelMapSelectionKind::QuakePatch);
		append(document.doomVertices, LevelMapSelectionKind::DoomVertex);
		append(document.doomLinedefs, LevelMapSelectionKind::DoomLinedef);
		append(document.doomSectors, LevelMapSelectionKind::DoomSector);
		append(document.doomThings, LevelMapSelectionKind::DoomThing);
	}
	m_source.hidden.resize(m_source.rows.size());
	endResetModel();
}

bool LevelObjectModel::refreshHidden(const std::function<bool(LevelMapSelectionRef)>& predicate)
{
	QBitArray hidden(objectCount());
	for (int i = 0; i < objectCount(); ++i) { hidden.setBit(i, predicate(m_source.rows[i].reference)); }
	if (hidden == m_source.hidden) { return false; }
	m_source.hidden = std::move(hidden);
	if (objectCount()) { Q_EMIT dataChanged(index(0), index(objectCount() - 1)); }
	return true;
}

int LevelObjectModel::objectCount() const { return static_cast<int>(m_source.rows.size()); }
int LevelObjectModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : std::max(1, objectCount()); }
LevelMapSelectionRef LevelObjectModel::reference(int row) const
{
	return row >= 0 && row < objectCount() ? m_source.rows[row].reference : LevelMapSelectionRef{};
}
int LevelObjectModel::rowForReference(LevelMapSelectionRef reference) const { return m_rowsById.value(referenceKey(reference), -1); }

QVariant LevelObjectModel::data(const QModelIndex& item, int role) const
{
	if (!item.isValid() || item.model() != this || item.column() != 0 || item.row() < 0 || item.row() >= rowCount()) { return {}; }
	if (!objectCount()) {
		if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole) { return {}; }
		return m_source.document.format == LevelMapFormat::Unknown
			? QCoreApplication::translate("vibestudio::ApplicationShell", "No level map loaded.")
			: QCoreApplication::translate("vibestudio::ApplicationShell", "No objects parsed from the loaded map.");
	}
	const int row = item.row();
	if (role == Qt::UserRole) { return m_source.selector(row); }
	if (role == Qt::ForegroundRole) { return m_source.hidden.testBit(row) ? QVariant(currentStudioTheme().colors.textMuted) : QVariant(); }
	if (role == Qt::AccessibleTextRole || role == Qt::ToolTipRole) { return m_source.description(row); }
	if (role != Qt::DisplayRole && role != Qt::UserRole + 1 && role != Qt::UserRole + 2) { return {}; }
	const auto [label, detail] = m_source.text(row);
	if (role == Qt::UserRole + 1) { return label; }
	if (role == Qt::UserRole + 2) { return detail; }
	// Two elided lines keep layout independent of the number and length of rows.
	// Full author text remains in tooltips, accessibility and the query snapshot.
	return m_source.hidden.testBit(row)
		? QStringLiteral("%1\n%2").arg(singleLine(label), singleLine(QCoreApplication::translate("vibestudio::ApplicationShell", "%1 · hidden").arg(detail)))
		: QStringLiteral("%1\n%2").arg(singleLine(label), singleLine(detail));
}

Qt::ItemFlags LevelObjectModel::flags(const QModelIndex& item) const
{
	return item.isValid() && item.model() == this && item.row() >= 0 && item.row() < objectCount()
		? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}
} // namespace vibestudio
