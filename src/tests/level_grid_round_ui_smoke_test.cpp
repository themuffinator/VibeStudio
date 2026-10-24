// Round Selection to Grid and the component UV lock commands in the shell:
// profile keys (NRC Ctrl+G, TrenchBroom U), the Tools tab controls, object and
// component rounding with exact undo, cancellation, the fitted-lock toggle and
// the Transform Components option, at 100% and at 200% with high contrast,
// expanded translations and right-to-left layout.
#include "app/application_shell.h"
#include "app/level_component_transform_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_patch.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, '~'));
	}
};
bool onGrid(double value, double grid)
{
	return std::abs(value / grid - std::round(value / grid)) < 1e-9;
}
bool brushOnGrid(const LevelMapBrush& brush, double grid)
{
	LevelBrushTopology topology;
	return levelBrushTopology(brush, &topology)
		&& std::all_of(topology.vertices.cbegin(), topology.vertices.cend(), [grid](const auto& v) { return onGrid(v.x, grid) && onGrid(v.y, grid) && onGrid(v.z, grid); });
}
} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference("four-views");
	expect(settings.levelComponentUvFit(), "fitted UV lock is on by default");
	settings.setLevelComponentUvFit(false);
	expect(!StudioSettings().levelComponentUvFit(), "fitted UV lock setting persists");
	settings.setLevelComponentUvFit(true);

	// An original fixture: an off-grid brush, a point entity and a patch.
	QString error;
	LevelMapDocument fixture;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	int brushId = -1, entityId = -1;
	LevelPatchCreateRequest patchRequest;
	// Large enough that every profile's default grid (up to 64) keeps it solid.
	patchRequest.center = {128.3, 0.4, 64.2, true};
	patchRequest.size = {128.5, 128.3, 8, true};
	LevelMapPatch patch;
	const auto path = temp.filePath("round.map");
	if (!expect(createLevelMap(create, &fixture, &error)
				&& addLevelMapBoxBrush(&fixture, {0.3, -0.2, 0.4, true}, {127.6, 128.3, 64.3, true}, QStringLiteral("studio/round"), &brushId, &error)
				&& addLevelMapEntity(&fixture, QStringLiteral("info_player_start"), {10.4, 20.6, 30.2, true}, {}, &entityId, &error)
				&& createLevelPatch(patchRequest, &patch, &error) && addLevelMapPatch(&fixture, patch, nullptr, &error)
				&& saveLevelMapAs(fixture, path).succeeded(),
			"fixture", error)) {
		return 1;
	}

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Round to grid UI " << phase.toStdString() << std::endl;
			settings.setSelectedEditorProfileId(profile);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setLevelComponentUvLock(false);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(1600, 1000);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger();
				auto* view = shell.findChild<MapViewport*>("mapViewport");
				if (!expect(view && until([&] { return shell.levelDocument().sourcePath == path; }), "map ready")) {
					return 1;
				}
				auto* round = action("map.roundToGrid");
				auto* snap = action("map.snapToGrid");
				auto* lock = action("map.componentUvLock");
				auto* fit = action("map.componentUvFit");
				if (!expect(round && snap && lock && fit, "round, snap and UV lock commands registered")) {
					return 1;
				}
				// Profile keys: NRC's Ctrl+G rounds; TrenchBroom's U toggles UV lock.
				const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
				const bool tb = QLatin1String(profile) == QLatin1String("trenchbroom");
				expect(round->shortcuts() == (nrc ? QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+G"))} : QList<QKeySequence>{}), "Ctrl+G rounds only in NRC",
					round->shortcut().toString());
				if (nrc) {
					expect(snap->shortcuts().isEmpty(), "NRC Snap Selection to Grid gives Ctrl+G up", snap->shortcut().toString());
				}
				expect(lock->shortcuts() == (tb ? QList<QKeySequence>{QKeySequence(QStringLiteral("U"))} : QList<QKeySequence>{}), "U toggles UV lock only in TrenchBroom",
					lock->shortcut().toString());
				EditorProfileDescriptor descriptor;
				if (tb && expect(editorProfileForId(QStringLiteral("trenchbroom"), &descriptor), "TrenchBroom profile")) {
					expect(!descriptor.controls.camera.flyKeys.all().contains(QStringLiteral("U")) && levelEditorControlProblems(descriptor.controls).isEmpty(),
						"U does not collide with the camera fly keys", levelEditorControlProblems(descriptor.controls).join(';'));
				}
				// Tools tab controls are keyboard reachable and named.
				shell.showLevelSidebarTab(QStringLiteral("tools"));
				for (const char* id : {"levelToolsRow-map.roundToGrid", "levelToolsRow-map.componentUvFit", "levelToolsRow-map.componentUvLock"}) {
					auto* button = shell.findChild<QPushButton*>(QLatin1String(id));
					expect(button && !button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(button),
						"accessible Tools control", QLatin1String(id));
				}
				expect(!round->isEnabled(), "round needs a selection");
				expect(fit->isCheckable() && fit->isChecked() && !lock->isChecked(), "UV lock and fit check marks follow the settings");
				fit->trigger();
				expect(!settings.levelComponentUvFit() && !fit->isChecked(), "fit toggle turns the fallback off");
				fit->trigger();
				expect(settings.levelComponentUvFit() && fit->isChecked(), "fit toggle turns the fallback back on");

				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) {
					view->setSelectionSet(refs);
					view->selectionSetChanged(refs);
					view->setFocus(Qt::OtherFocusReason);
				};
				const auto baseline = bytes();
				const int patchId = shell.levelDocument().patches.isEmpty() ? -1 : shell.levelDocument().patches.first().id;
				pick({{LevelMapSelectionKind::QuakeBrush, brushId}, {LevelMapSelectionKind::Entity, entityId}, {LevelMapSelectionKind::QuakePatch, patchId}});
				// TrenchBroom records selection changes in the history too.
				int history = static_cast<int>(shell.levelDocument().undoStack.size());
				expect(round->isEnabled(), "round enabled with a selection");
				if (nrc && scale == 100) {
					bool cancelled = false;
					QTimer::singleShot(0, &shell, [&] {
						if (auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog")) {
							cancelled = true;
							dialog->reject();
						}
					});
					round->trigger();
					expect(!cancelled || (bytes() == baseline && static_cast<int>(shell.levelDocument().undoStack.size()) == history), "cancelled rounding is atomic");
				}
				if (bytes() == baseline) {
					round->trigger();
				}
				const auto& document = shell.levelDocument();
				const auto brush = std::find_if(document.brushes.cbegin(), document.brushes.cend(), [&](const auto& b) { return b.id == brushId; });
				const auto entity = std::find_if(document.entities.cbegin(), document.entities.cend(), [&](const auto& e) { return e.id == entityId; });
				const double grid = view->gridSize();
				expect(brush != document.brushes.cend() && brushOnGrid(*brush, grid), "brush vertices round onto the view grid", QString::number(grid));
				expect(entity != document.entities.cend() && onGrid(entity->origin.x, grid) && onGrid(entity->origin.y, grid) && onGrid(entity->origin.z, grid),
					"entity origin rounds");
				expect(!document.patches.isEmpty() && std::all_of(document.patches.first().controlPoints.cbegin(), document.patches.first().controlPoints.cend(),
														  [&](const auto& p) { return onGrid(p.x, grid) && onGrid(p.y, grid) && onGrid(p.z, grid); }),
					"patch control points round");
				expect(static_cast<int>(document.undoStack.size()) == history + 1, "one undo step", shell.statusBar()->currentMessage());
				expect(shell.statusBar()->currentMessage().contains(QStringLiteral("3")), "status names the rounded objects", shell.statusBar()->currentMessage());
				action("map.undo")->trigger();
				expect(bytes() == baseline, "undo restores every rounded point");

				// Component mode rounds only the picks.
				pick({{LevelMapSelectionKind::QuakeBrush, brushId}});
				action("map.vertexTool")->trigger();
				expect(view->componentMode() == MapViewport::ComponentMode::Vertices, "vertex mode");
				const QVector<LevelMapComponentSet> one{{{LevelMapSelectionKind::QuakeBrush, brushId}, LevelBrushComponent::Vertex, {0}}};
				expect(view->setPickedComponents(one), "pick one vertex");
				view->componentPicksChanged(1);
				history = static_cast<int>(shell.levelDocument().undoStack.size());
				LevelBrushTopology before;
				levelBrushTopology(*std::find_if(shell.levelDocument().brushes.cbegin(), shell.levelDocument().brushes.cend(), [&](const auto& b) { return b.id == brushId; }),
					&before);
				expect(round->isEnabled(), "round enabled for picked components");
				round->trigger();
				LevelBrushTopology after;
				levelBrushTopology(*std::find_if(shell.levelDocument().brushes.cbegin(), shell.levelDocument().brushes.cend(), [&](const auto& b) { return b.id == brushId; }),
					&after);
				int kept = 0;
				for (const auto& v : before.vertices) {
					kept += std::any_of(after.vertices.cbegin(), after.vertices.cend(), [&](const auto& q) { return std::abs(q.x - v.x) < 1e-3 && std::abs(q.y - v.y) < 1e-3 && std::abs(q.z - v.z) < 1e-3; }) ? 1 : 0;
				}
				expect(kept == 7 && static_cast<int>(shell.levelDocument().undoStack.size()) == history + 1, "only the picked vertex rounds", shell.statusBar()->currentMessage());
				action("map.undo")->trigger();
				expect(bytes() == baseline, "component rounding undo");
				action("map.selectNone")->trigger();
				action("map.selectNone")->trigger();

				// The Transform Components option follows the lock.
				LevelComponentTransformRequest initial;
				initial.fitUvLock = true;
				LevelComponentTransformDialog dialog(initial, {0, 0, 0, true}, &shell);
				auto* uvLock = dialog.findChild<QCheckBox*>("componentUvLock");
				auto* uvFit = dialog.findChild<QCheckBox*>("componentUvFit");
				if (expect(uvLock && uvFit, "dialog UV lock controls")) {
					expect(!uvFit->isEnabled() && !dialog.request().fitUvLock, "fit waits for the lock");
					uvLock->setChecked(true);
					expect(uvFit->isEnabled() && uvFit->isChecked() && dialog.request().textureLock && dialog.request().fitUvLock, "fit follows the lock");
					uvFit->setChecked(false);
					expect(!dialog.request().fitUvLock, "fit can be refused per transform");
					auto* accessible = QAccessible::queryAccessibleInterface(uvFit);
					expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !uvFit->accessibleDescription().isEmpty(), "accessible fit option");
				}
				expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("saved-%1-%2.map").arg(QLatin1String(profile)).arg(scale)), true, &error), "save tested shell", error);
				expect(shell.close(), "clean shell close");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	return ok ? 0 : 1;
}
