#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"

#include <QAbstractListModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <memory>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelEditor)
};
QString contactText(ModelTriangleContact kind)
{
	return kind == ModelTriangleContact::CoplanarOverlap ? Text::tr("Coplanar overlap") : Text::tr("Crossing");
}
} // namespace

// Values are formatted on demand; a large report creates no per-finding widgets.
class ModelIntersectionList final : public QAbstractListModel
{
  public:
	explicit ModelIntersectionList(QObject *parent) : QAbstractListModel(parent)
	{
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : int(m_findings.size());
	}
	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() < 0 || index.row() >= m_findings.size())
			return {};
		if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole)
			return {};
		const auto &f = m_findings[index.row()];
		return Text::tr("Pose %1 · %2:%3 / %4:%5 · %6")
			.arg(f.frame)
			.arg(f.firstSurface)
			.arg(f.firstFace)
			.arg(f.secondSurface)
			.arg(f.secondFace)
			.arg(contactText(f.kind));
	}
	void setFindings(const QVector<ModelIntersectionFinding> &findings)
	{
		beginResetModel();
		m_findings = findings;
		endResetModel();
	}

  private:
	QVector<ModelIntersectionFinding> m_findings;
};

void ModelEditorDialog::addIntersectionControls(QFormLayout *form)
{
	auto *heading = new QLabel(Text::tr("Geometry intersections"));
	heading->setWordWrap(true);
	auto font = heading->font();
	font.setBold(true);
	heading->setFont(font);
	form->addRow(heading);
	m_intersectionScope = new QComboBox;
	m_intersectionScope->setObjectName(QStringLiteral("meshIntersectionScope"));
	m_intersectionScope->addItems({Text::tr("All poses"), Text::tr("Current pose")});
	m_intersectionScope->setAccessibleName(Text::tr("Intersection scan poses"));
	m_intersectionScope->setAccessibleDescription(
		Text::tr("Scan all surfaces, including contacts between surfaces, in all stored poses or the current pose."));
	form->addRow(Text::tr("Scan poses"), m_intersectionScope);
	m_intersectionInspect = new QPushButton(Text::tr("Inspect Intersections"));
	m_intersectionInspect->setObjectName(QStringLiteral("inspectMeshIntersections"));
	m_intersectionInspect->setToolTip(
		Text::tr("Find face crossings and coplanar area overlaps throughout the mesh. Shared boundary edges and isolated point contacts "
				 "are allowed. Inspection is cancellable and changes no geometry."));
	form->addRow(m_intersectionInspect);
	m_intersectionStatus = new QLabel;
	m_intersectionStatus->setObjectName(QStringLiteral("meshIntersectionStatus"));
	m_intersectionStatus->setAccessibleName(Text::tr("Geometry intersection scan status"));
	form->addRow(m_intersectionStatus);
	m_intersectionFinding = new QComboBox;
	m_intersectionFinding->setObjectName(QStringLiteral("meshIntersectionFinding"));
	m_intersectionFinding->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_intersectionFinding->setMinimumContentsLength(12);
	m_intersectionFinding->setAccessibleName(Text::tr("Intersecting face pair"));
	m_intersectionFinding->setAccessibleDescription(Text::tr(
		"Each result identifies a stored pose and two surface:face indices. Select either face to inspect and edit it in the mesh."));
	m_intersectionList = new ModelIntersectionList(m_intersectionFinding);
	m_intersectionFinding->setModel(m_intersectionList);
	form->addRow(Text::tr("Face pair"), m_intersectionFinding);
	m_intersectionDetail = new QLabel;
	m_intersectionDetail->setObjectName(QStringLiteral("meshIntersectionDetail"));
	form->addRow(m_intersectionDetail);
	for (auto *label : {m_intersectionStatus, m_intersectionDetail})
	{
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		label->setMinimumWidth(0);
		label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	}
	m_intersectionFirst = new QPushButton(Text::tr("Show First Face"));
	m_intersectionFirst->setObjectName(QStringLiteral("showMeshIntersectionFirst"));
	m_intersectionSecond = new QPushButton(Text::tr("Show Second Face"));
	m_intersectionSecond->setObjectName(QStringLiteral("showMeshIntersectionSecond"));
	for (auto *button : {m_intersectionFirst, m_intersectionSecond})
	{
		button->setToolTip(Text::tr(
			"Pause playback, show the finding's pose and select this exact face on its surface. Geometry and history remain unchanged."));
		form->addRow(button);
	}
	for (auto *button : {m_intersectionInspect, m_intersectionFirst, m_intersectionSecond})
	{
		button->setAccessibleName(button->text());
		button->setAccessibleDescription(button->toolTip());
	}
	connect(m_intersectionScope, &QComboBox::currentIndexChanged, this, [this] { refreshIntersections(); });
	connect(m_intersectionFinding, &QComboBox::currentIndexChanged, this, [this] { refreshIntersections(); });
	connect(m_intersectionInspect, &QPushButton::clicked, this, [this] {
		QString error;
		if (!inspectIntersections(&error))
			m_status->setText(error);
	});
	connect(m_intersectionFirst, &QPushButton::clicked, this, [this] { selectIntersectionFace(false); });
	connect(m_intersectionSecond, &QPushButton::clicked, this, [this] { selectIntersectionFace(true); });
}

bool ModelEditorDialog::inspectIntersections(QString *error)
{
	if (m_working)
		return false;
	m_preview->pause();
	ModelIntersectionOptions options;
	options.frame = m_intersectionScope->currentIndex() == 0 ? -1 : m_preview->frame();
	auto report = std::make_shared<ModelIntersectionReport>();
	if (!performWork(
			Text::tr("Inspect Geometry Intersections"),
			[options, report](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
				return inspectModelIntersections(candidate.mesh(), options, report.get(), failure, control);
			},
			error))
		return false;
	m_intersections = std::move(*report);
	m_intersectionFrame = options.frame;
	m_intersectionRevision = m_document.revisionFingerprint();
	{
		const QSignalBlocker block(m_intersectionFinding);
		m_intersectionList->setFindings(m_intersections.findings);
		m_intersectionFinding->setCurrentIndex(m_intersections.findings.isEmpty() ? -1 : 0);
	}
	refreshIntersections();
	return true;
}

void ModelEditorDialog::refreshIntersections()
{
	if (!m_intersectionStatus)
		return;
	const int requestedFrame = m_intersectionScope->currentIndex() == 0 ? -1 : m_preview->frame();
	const bool current = !m_intersectionRevision.isEmpty() && m_intersectionRevision == m_document.revisionFingerprint() &&
						 m_intersectionFrame == requestedFrame;
	const int row = m_intersectionFinding->currentIndex();
	const bool selected = current && row >= 0 && row < m_intersections.findings.size();
	m_intersectionFinding->setEnabled(current && !m_intersections.findings.isEmpty());
	m_intersectionFirst->setEnabled(selected);
	m_intersectionSecond->setEnabled(selected);
	m_intersectionStatus->setText(current
									  ? Text::tr("Face pairs: %1 · Poses scanned: %2\nAll surfaces, including contacts between surfaces.")
											.arg(m_intersections.findings.size())
											.arg(m_intersections.framesScanned)
									  : Text::tr("Inspect the mesh to update geometry intersections for the chosen poses."));
	m_intersectionStatus->setAccessibleDescription(m_intersectionStatus->text());
	if (selected)
	{
		const auto &finding = m_intersections.findings[row];
		m_intersectionDetail->setText(Text::tr("Pose %1 · %2\nFirst: surface %3 (%4), face %5\nSecond: surface %6 (%7), face %8")
										  .arg(finding.frame)
										  .arg(contactText(finding.kind))
										  .arg(finding.firstSurface)
										  .arg(m_document.mesh().surfaces[finding.firstSurface].name)
										  .arg(finding.firstFace)
										  .arg(finding.secondSurface)
										  .arg(m_document.mesh().surfaces[finding.secondSurface].name)
										  .arg(finding.secondFace));
	}
	else
		m_intersectionDetail->clear();
	m_intersectionDetail->setAccessibleName(m_intersectionDetail->text());
}

void ModelEditorDialog::selectIntersectionFace(bool second)
{
	refreshIntersections();
	if (m_working || !m_intersectionFirst->isEnabled())
		return;
	const auto finding = m_intersections.findings[m_intersectionFinding->currentIndex()];
	ModelSelection selection;
	selection.surface = second ? finding.secondSurface : finding.firstSurface;
	selection.faces.insert(second ? finding.secondFace : finding.firstFace);
	m_refreshing = true;
	m_preview->pause();
	m_document.setSelection(selection);
	m_preview->setFrame(finding.frame);
	m_frame->setCurrentIndex(finding.frame);
	m_refreshing = false;
	refresh();
}
} // namespace vibestudio
