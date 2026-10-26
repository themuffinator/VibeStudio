// TrenchBroom's debug Create Brush… and Print Vertices as ordinary Levels
// commands. Create Brush from Points… builds the convex brush enclosing four
// or more typed points in the current material, previewed, as one undo step;
// Copy Vertex Coordinates puts the picked faces', edges' and vertices'
// corners, or else the selected brushes', on the clipboard and in Activity at
// full precision, in a form Create Brush from Points reads back
// (core/level_brush_points.h).
// Workflow reference: TB MapWindow::debugCreateBrush and
// MapWindow::debugPrintVertices at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_brush_points_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_actions.h"
#include "core/level_hollow.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>

namespace vibestudio {

namespace {
// The last typed points survive closing the dialog, up to this much text.
constexpr qsizetype kRememberedPointsText = 64 * 1024;
} // namespace

void ApplicationShell::registerLevelBrushPointsCommands()
{
	StudioCommandRegistration create;
	create.commandId = QStringLiteral("map.createBrushFromPoints");
	create.group = StudioCommandGroup::Edit;
	create.label = tr("Create Brush from Points…");
	create.statusTip = tr("Type four or more points and add the convex brush that encloses them, in the current material, as one undo step.");
	create.iconName = QStringLiteral("cube");
	create.menuSection = tr("Brush Shapes");
	create.handler = [this]() {
		setMode(StudioMode::Levels);
		createLevelBrushFromPointsFromUi();
	};
	m_commands->registerCommand(create);

	StudioCommandRegistration copy;
	copy.commandId = QStringLiteral("map.copyVertexCoordinates");
	copy.group = StudioCommandGroup::Edit;
	copy.label = tr("Copy Vertex Coordinates");
	copy.statusTip = tr("Copy the corners of the picked faces, edges and vertices, or else of the selected brushes, at full precision.");
	copy.iconName = QStringLiteral("copy");
	copy.menuSection = tr("Brush Shapes");
	copy.handler = [this]() { copyLevelVertexCoordinatesFromUi(); };
	m_commands->registerCommand(copy);
}

void ApplicationShell::refreshLevelBrushPointsCommands()
{
	if (!m_commands) {
		return;
	}
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	m_commands->setEnabled(QStringLiteral("map.createBrushFromPoints"), quake);
	m_commands->setEnabled(QStringLiteral("map.copyVertexCoordinates"), quake && (!m_levelMapDocument.selection.isEmpty() || levelComponentsActive()));
}

QString ApplicationShell::levelSelectionCornerText(int* listings, int* points, QString* error) const
{
	if (listings) {
		*listings = 0;
	}
	if (points) {
		*points = 0;
	}
	if (error) {
		error->clear();
	}
	if (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		return {};
	}
	// Picked components first, as TB lists selected faces before brushes;
	// independent face picks join them (app/level_component_transform_actions.cpp).
	QString reason;
	const QVector<LevelMapComponentSet> sets = levelComponentTransformSets(&reason);
	QVector<int> brushes;
	if (sets.isEmpty()) {
		for (const int id : levelCsgReachedBrushIds(m_levelMapDocument)) {
			if (!m_levelMapViewport || !m_levelMapViewport->isHidden(LevelMapSelectionKind::QuakeBrush, id)) {
				brushes << id;
			}
		}
	}
	QVector<LevelVertexListing> made;
	if ((sets.isEmpty() && brushes.isEmpty()) || !levelMapVertexListings(m_levelMapDocument, brushes, sets, &made, &reason)) {
		if (error) {
			*error = reason;
		}
		return {};
	}
	if (listings) {
		*listings = static_cast<int>(made.size());
	}
	if (points) {
		*points = levelVertexListingPointCount(made);
	}
	return levelVertexListingText(made);
}

void ApplicationShell::copyLevelVertexCoordinatesFromUi()
{
	int listings = 0;
	int points = 0;
	QString error;
	const QString text = levelSelectionCornerText(&listings, &points, &error);
	if (text.isEmpty()) {
		statusBar()->showMessage(error.isEmpty() ? tr("Select brushes, or pick faces, edges or vertices, to copy their corners.") : error, 6000);
		return;
	}
	QGuiApplication::clipboard()->setText(text);
	const QString summary = tr("%n corner(s) copied.", nullptr, points);
	// Activity keeps the full text, as TrenchBroom logs it to its console.
	recordActivity(tr("Vertex coordinates copied"), text, QStringLiteral("level-map"), OperationState::Completed, summary);
	statusBar()->showMessage(summary + QLatin1Char(' ') + tr("Paste them into Create Brush from Points, a script or a report."), 6000);
}

void ApplicationShell::createLevelBrushFromPointsFromUi()
{
	if ((m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a Quake-family .map to create brushes from points."));
		return;
	}
	auto archive = m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	const QString remembered = QString::fromUtf8(m_settings.shellLayoutState(QStringLiteral("levelBrushPoints")));
	LevelBrushPointsDialog dialog(m_levelMapDocument, remembered, levelBrushMaterial(), {}, archive, activePaletteId(), this);
	dialog.setSelectionPointsProvider([this] { return levelSelectionCornerText(); },
		!m_levelMapDocument.selection.isEmpty() || levelComponentsActive());
	const auto publish = levelPlacementCommitter();
	dialog.setApplyHandler([this, &dialog, publish](const LevelMapDocument& candidate, QString* error) {
		if (!publish(candidate, error)) {
			return false;
		}
		const LevelBrushPointsReport& report = dialog.report();
		rememberLevelMaterial(dialog.material());
		recordActivity(tr("Level map brush created from points"), QStringLiteral("brush:%1").arg(report.brushId), QStringLiteral("level-map"),
			OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Created brush:%1. Corners: %2. Faces: %3. Undo removes it.").arg(report.brushId).arg(report.corners).arg(report.faces),
			6000);
		return true;
	});
	dialog.exec();
	const QString typed = dialog.pointsText();
	if (typed.size() <= kRememberedPointsText) {
		m_settings.setShellLayoutState(QStringLiteral("levelBrushPoints"), typed.toUtf8());
	}
}
} // namespace vibestudio
