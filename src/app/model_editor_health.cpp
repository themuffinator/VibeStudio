#include "app/model_editor_dialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>

#include <array>
#include <memory>

namespace vibestudio
{
namespace
{
QStringList findingNames()
{
	return {QCoreApplication::translate("VibeStudioModelEditor", "Duplicate faces"),
			QCoreApplication::translate("VibeStudioModelEditor", "Unused vertices"),
			QCoreApplication::translate("VibeStudioModelEditor", "Disconnected vertex fans"),
			QCoreApplication::translate("VibeStudioModelEditor", "Inconsistent winding"),
			QCoreApplication::translate("VibeStudioModelEditor", "Nonmanifold edges"),
			QCoreApplication::translate("VibeStudioModelEditor", "Boundary edges")};
}
QStringList findingDetails()
{
	return {QCoreApplication::translate("VibeStudioModelEditor",
										"Faces with the same three vertex indices, in any order. Repair keeps the lowest face index, "
										"including when a reversed copy was intentionally two-sided."),
			QCoreApplication::translate(
				"VibeStudioModelEditor",
				"Vertices used by no face. Repair removes their UVs and all animation samples; surviving attributes stay exact."),
			QCoreApplication::translate("VibeStudioModelEditor", "Face groups touching only at a vertex. Repair gives each disconnected "
																 "fan its own copy in every pose, preserving UVs, normals and seam marks."),
			QCoreApplication::translate("VibeStudioModelEditor",
										"Two faces traverse their shared edge in the same direction. Repair orients the whole surface from "
										"each component's lowest face. It preserves authored normals; use Recalculate Normals if needed. "
										"Remove duplicates and resolve nonmanifold edges first."),
			QCoreApplication::translate("VibeStudioModelEditor",
										"More than two faces share an indexed edge. Repair splits its endpoints into connected face fans, "
										"preserving existing two-face connections. It keeps every face, UV and animation sample, and can "
										"create open boundaries. Review the result before welding or exporting."),
			QCoreApplication::translate("VibeStudioModelEditor",
										"Edges belonging to one face. Open surfaces and indexed UV or normal seams commonly have "
										"boundaries. These are informational and are not closed automatically. To close a chosen hole, "
										"select an edge from it and use Geometry > Fill Boundary Loops.")};
}
QStringList repairNames()
{
	return {QCoreApplication::translate("VibeStudioModelEditor", "Remove Duplicate Faces"),
			QCoreApplication::translate("VibeStudioModelEditor", "Remove Unused Vertices"),
			QCoreApplication::translate("VibeStudioModelEditor", "Split Disconnected Fans"),
			QCoreApplication::translate("VibeStudioModelEditor", "Orient Faces"),
			QCoreApplication::translate("VibeStudioModelEditor", "Split Nonmanifold Edges")};
}
std::array<int, 6> counts(const ModelTopologyHealth &health)
{
	int duplicates = 0;
	for (const auto &group : health.duplicateFaces)
	{
		duplicates += int(group.size()) - 1;
	}
	return {duplicates,
			int(health.unusedVertices.size()),
			int(health.disconnectedFans.size()),
			int(health.windingEdges.size()),
			int(health.nonmanifoldEdges.size()),
			int(health.boundaryEdges.size())};
}
} // namespace

void ModelEditorDialog::addHealthControls(QFormLayout *form)
{
	m_healthInspect = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Inspect Surface"));
	m_healthInspect->setObjectName(QStringLiteral("inspectMeshTopology"));
	m_healthInspect->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
															"Inspect indexed topology on the active surface. Coincident seam copies "
															"remain separate. Inspection is cancellable and does not modify the source."));
	form->addRow(m_healthInspect);
	m_healthStatus = new QLabel;
	m_healthStatus->setObjectName(QStringLiteral("meshHealthStatus"));
	m_healthStatus->setTextFormat(Qt::PlainText);
	m_healthStatus->setWordWrap(true);
	m_healthStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_healthStatus->setMinimumWidth(0);
	m_healthStatus->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Surface topology health"));
	form->addRow(m_healthStatus);
	m_healthFinding = new QComboBox;
	m_healthFinding->setObjectName(QStringLiteral("meshHealthFinding"));
	m_healthFinding->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_healthFinding->setMinimumContentsLength(12);
	m_healthFinding->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Topology finding category"));
	m_healthFinding->setAccessibleDescription(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Choose a finding category to inspect its count, select the affected components, or repair the entire active surface."));
	m_healthFinding->addItems(findingNames());
	form->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Findings"), m_healthFinding);
	m_healthDetail = new QLabel;
	m_healthDetail->setObjectName(QStringLiteral("meshHealthDetail"));
	m_healthDetail->setTextFormat(Qt::PlainText);
	m_healthDetail->setWordWrap(true);
	m_healthDetail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_healthDetail->setMinimumWidth(0);
	form->addRow(m_healthDetail);
	m_healthSelect = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Select Findings"));
	m_healthSelect->setObjectName(QStringLiteral("selectMeshHealthFinding"));
	m_healthSelect->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Select the affected faces, vertices, or edges in the component table and both previews."));
	form->addRow(m_healthSelect);
	m_healthRepair = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Split Disconnected Fans"));
	m_healthRepair->setObjectName(QStringLiteral("repairMeshHealthFinding"));
	m_healthRepair->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Repair every finding of this category on the active surface in one undo step. The current component "
								 "selection does not limit the repair. All animation poses are preserved."));
	form->addRow(m_healthRepair);
	for (auto *button : {m_healthInspect, m_healthSelect, m_healthRepair})
	{
		button->setAccessibleName(button->text());
		button->setAccessibleDescription(button->toolTip());
	}
	connect(m_healthInspect, &QPushButton::clicked, this, [this] {
		QString error;
		if (!inspectTopology(&error))
		{
			m_status->setText(error);
		}
	});
	connect(m_healthFinding, &QComboBox::currentIndexChanged, this, [this] { refreshHealth(); });
	connect(m_healthSelect, &QPushButton::clicked, this, [this] { selectHealthFinding(); });
	connect(m_healthRepair, &QPushButton::clicked, this, [this] { repairHealthFinding(); });
	addIntersectionControls(form);
}

bool ModelEditorDialog::inspectTopology(QString *error)
{
	const int surface = m_document.selection().surface;
	if (surface < 0 || surface >= m_document.mesh().surfaces.size())
	{
		return false;
	}
	auto health = std::make_shared<ModelTopologyHealth>();
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Inspect Surface Topology"),
			[surface, health](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
				return inspectModelTopology(candidate.mesh().surfaces[surface], health.get(), failure, control);
			},
			error))
	{
		return false;
	}
	m_health = std::move(*health);
	m_healthSurface = surface;
	m_healthRevision = m_document.revisionFingerprint();
	refreshSelection();
	return true;
}

void ModelEditorDialog::refreshHealth()
{
	if (!m_healthStatus)
	{
		return;
	}
	const bool current = m_healthSurface == m_document.selection().surface && m_healthRevision == m_document.revisionFingerprint() &&
						 !m_healthRevision.isEmpty();
	const auto total = counts(m_health);
	const auto names = findingNames();
	const QSignalBlocker block(m_healthFinding);
	for (int i = 0; i < names.size(); ++i)
	{
		m_healthFinding->setItemText(
			i, current ? QCoreApplication::translate("VibeStudioModelEditor", "%1 — %2").arg(names[i]).arg(total[i]) : names[i]);
	}
	const int category = m_healthFinding->currentIndex();
	const bool meshMode = m_selectionMode->currentIndex() < 3;
	m_healthInspect->setEnabled(meshMode);
	m_healthFinding->setEnabled(current && meshMode);
	m_healthSelect->setEnabled(current && meshMode && category >= 0 && total[category] > 0);
	const bool canRepair = category >= 0 && category < 5;
	m_healthRepair->setText(canRepair ? repairNames()[category] : QCoreApplication::translate("VibeStudioModelEditor", "Repair Surface"));
	m_healthRepair->setAccessibleName(m_healthRepair->text());
	m_healthRepair->setEnabled(current && meshMode && canRepair && total[category] > 0);
	m_healthDetail->setText(category >= 0 ? findingDetails()[category] : QString());
	m_healthDetail->setAccessibleName(m_healthDetail->text());
	m_healthStatus->setText(
		current ? QCoreApplication::translate("VibeStudioModelEditor",
											  "%1 edges · %2 face components\nRepair scope: entire active surface, all poses.")
					  .arg(m_health.edges.size())
					  .arg(m_health.faceComponents)
				: QCoreApplication::translate("VibeStudioModelEditor", "Inspect the active surface to update topology findings."));
	m_healthStatus->setAccessibleDescription(m_healthStatus->text());
	refreshIntersections();
}

void ModelEditorDialog::selectHealthFinding()
{
	if (m_working || !m_healthSelect->isEnabled())
	{
		return;
	}
	ModelSelection selection;
	selection.surface = m_healthSurface;
	switch (m_healthFinding->currentIndex())
	{
	case 0:
		for (const auto &group : m_health.duplicateFaces)
		{
			for (int face : group)
			{
				selection.faces.insert(face);
			}
		}
		break;
	case 1:
		selection.vertices = QSet<int>(m_health.unusedVertices.begin(), m_health.unusedVertices.end());
		break;
	case 2:
		for (const auto &fan : m_health.disconnectedFans)
		{
			selection.vertices.insert(fan.vertex);
		}
		break;
	case 3:
		selection.edges = QSet<ModelEdge>(m_health.windingEdges.begin(), m_health.windingEdges.end());
		break;
	case 4:
		selection.edges = QSet<ModelEdge>(m_health.nonmanifoldEdges.begin(), m_health.nonmanifoldEdges.end());
		break;
	case 5:
		selection.edges = QSet<ModelEdge>(m_health.boundaryEdges.begin(), m_health.boundaryEdges.end());
		break;
	default:
		return;
	}
	m_document.setSelection(selection);
	refresh();
}

void ModelEditorDialog::repairHealthFinding()
{
	if (m_working || !m_healthRepair->isEnabled())
	{
		return;
	}
	constexpr std::array operations{ModelEditKind::RemoveDuplicateFaces, ModelEditKind::RemoveUnusedVertices,
									ModelEditKind::SplitDisconnectedFans, ModelEditKind::OrientFaces, ModelEditKind::SplitNonmanifoldEdges};
	ModelEdit edit;
	edit.kind = operations[m_healthFinding->currentIndex()];
	edit.selection = m_document.selection();
	QString error;
	if (!applyEdit(edit, &error))
	{
		m_status->setText(error);
		return;
	}
	// The applied edit remains committed if the subsequent read-only scan is cancelled.
	if (!inspectTopology(&error))
	{
		m_status->setText(error);
	}
}
} // namespace vibestudio
