// The CSG Tool section of the Levels Tools tab in the real shell (NetRadiant
// Custom's CSG Tool window): CSG Tool opens and focuses it; Use Grid copies
// the grid into the thickness; every option persists; the buttons are the
// commands, enabled with a brush selection; Diagonal, Wrap with Keep Inner
// Brush and caulk, Extrude on the picked face only, a shaft facing the 2D
// view, and Shrink and Expand each make one exact undo step. Runs for the
// NetRadiant Custom and TrenchBroom profiles at 100% and at 200% with high
// contrast, an expanding translation and right-to-left layout. Qt events go
// to the widgets directly; no native input or screen capture is used.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/level_hollow.h"
#include "core/studio_semantics.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <array>
#include <functional>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << message << ": " << qPrintable(detail) << '\n';
		ok = false;
	}
	return condition;
}
void drain(int ms = 20)
{
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
}
bool until(const std::function<bool()>& ready, int timeoutMs = 45000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < timeoutMs) {
		drain();
	}
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
// An axis-aligned Quake III brush, faces in the order -X, +X, -Y, +Y, -Z,
// +Z, each wound out of the box.
QString box(std::array<double, 3> lo, std::array<double, 3> hi, const QString& material)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[axis] = positive ? hi[axis] : lo[axis];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[first] += 1;
			b[second] += 1;
			lines << QStringLiteral("%1 %2 %3 %4 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b), material);
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}
int faceCount(const LevelMapDocument& document, const QString& texture)
{
	int count = 0;
	for (const LevelMapBrush& brush : document.brushes) {
		for (const LevelMapBrushFace& face : brush.faces) {
			count += face.textureName == texture;
		}
	}
	return count;
}
const LevelMapBrush* brushWithId(const LevelMapDocument& document, int id)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}
} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	// Brush 0 is the one the tool works on; brush 1 stays as it is. The patch
	// makes the map Quake III's, whose caulk is common/caulk.
	const QString patch = QStringLiteral(
		"{\npatchDef2\n{\nstudio/patch\n( 3 3 0 0 0 )\n(\n( ( -328 -128 0 0 0 ) ( -328 -96 0 0 0.5 ) ( -328 -64 0 0 1 ) )\n"
		"( ( -296 -128 0 0.5 0 ) ( -296 -96 0 0.5 0.5 ) ( -296 -64 0 0.5 1 ) )\n( ( -264 -128 0 1 0 ) ( -264 -96 0 1 0.5 ) ( -264 -64 0 1 1 ) )\n)\n}\n}");
	const QString text = QStringLiteral("{\n\"classname\" \"worldspawn\"\n") + box({-64, -64, -64}, {64, 64, 64}, QStringLiteral("studio/wall"))
		+ QLatin1Char('\n') + box({256, 0, 0}, {320, 64, 64}, QStringLiteral("studio/other")) + QLatin1Char('\n') + patch + QStringLiteral("\n}\n");
	const QString mapPath = temp.filePath(QStringLiteral("csg.map"));
	{
		QFile file(mapPath);
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size(), "fixture written")) {
			return 1;
		}
	}
	const QStringList commandIds {QStringLiteral("map.csgShrink"), QStringLiteral("map.csgExpand"), QStringLiteral("map.csgHollowDiagonal"),
		QStringLiteral("map.csgHollowWrap"), QStringLiteral("map.csgHollowExtrude"), QStringLiteral("map.csgHollowPull")};
	phase = QStringLiteral("registry");
	for (const char* id : {"map.csg-tool", "map.csg-shrink", "map.csg-expand", "map.csg-hollow-diagonal", "map.csg-hollow-wrap", "map.csg-hollow-extrude",
			 "map.csg-hollow-pull"}) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(),
			"each CSG Tool command has a palette entry", QString::fromLatin1(id));
	}
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (const int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "CSG Tool UI " << qPrintable(phase) << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setLevelRoomPreferences({});
			settings.setLevelCsgToolPreferences({});
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
				shell.openPathFromCommandLine(mapPath);
				const auto command = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				command(QStringLiteral("shell.mode.levels"))->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				if (!expect(view && until([&] { return shell.levelDocument().sourcePath == mapPath && shell.levelDocument().brushes.size() == 2; }),
						"map ready")
					|| !expect(shell.levelDocument().format == LevelMapFormat::Quake3Map, "a Quake III map")) {
					return 1;
				}
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const QByteArray original = bytes();
				auto* panel = shell.findChild<QWidget*>(QStringLiteral("levelCsgToolPanel"));
				auto* thickness = shell.findChild<QDoubleSpinBox*>(QStringLiteral("levelCsgThickness"));
				auto* useGrid = shell.findChild<QToolButton*>(QStringLiteral("levelCsgUseGrid"));
				auto* faces = shell.findChild<QComboBox*>(QStringLiteral("levelCsgFacePolicy"));
				auto* caulk = shell.findChild<QCheckBox*>(QStringLiteral("levelCsgCaulk"));
				auto* keepInner = shell.findChild<QCheckBox*>(QStringLiteral("levelCsgKeepInner"));
				auto* summary = shell.findChild<QLabel*>(QStringLiteral("levelCsgSummary"));
				auto* section = shell.findChild<SidebarSection*>(QStringLiteral("tools.csg"));
				if (!section) {
					for (auto* candidate : shell.findChildren<SidebarSection*>()) {
						if (candidate->sectionId() == QLatin1String("tools.csg")) {
							section = candidate;
						}
					}
				}
				if (!expect(panel && thickness && useGrid && faces && caulk && keepInner && summary && section, "the CSG Tool section and its controls")) {
					return 1;
				}
				QHash<QString, QPushButton*> rows;
				for (const QString& id : commandIds) {
					rows.insert(id, shell.findChild<QPushButton*>(QStringLiteral("levelCsgRow-") + id));
					expect(rows.value(id) && command(id) && !rows.value(id)->accessibleName().isEmpty() && rows.value(id)->focusPolicy() != Qt::NoFocus
							&& !command(id)->statusTip().isEmpty(),
						"each command has an accessible keyboard row", id);
				}
				// The defaults, and accessible controls.
				expect(std::abs(thickness->value() - 16.0) < 1e-9 && faces->currentData().toString() == QLatin1String("exclude-picked") && caulk->isChecked()
						&& !keepInner->isChecked(),
					"the section starts with NetRadiant Custom's defaults");
				for (QWidget* control : std::initializer_list<QWidget*> {thickness, useGrid, faces, caulk, keepInner}) {
					expect(!control->accessibleName().isEmpty() && control->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(control),
						"the controls are accessible keyboard stops", control->objectName());
				}
				expect(faces->count() == 4 && !faces->itemData(3, Qt::ToolTipRole).toString().isEmpty(), "four face policies, each explained");
				expect(scale != 100 || summary->text().contains(QStringLiteral("common/caulk")), "the summary names the game's caulk", summary->text());
				expect(summary->accessibleDescription() == summary->text(), "the summary is read out");

				// With nothing selected the commands wait.
				view->setSelectionSet({});
				view->selectionSetChanged(view->selectionSet());
				drain();
				expect(!command(QStringLiteral("map.csgHollowDiagonal"))->isEnabled() && !rows.value(QStringLiteral("map.csgShrink"))->isEnabled(),
					"the commands are disabled without a brush");

				// CSG Tool opens the section and gives the thickness the keyboard.
				shell.activateWindow();
				command(QStringLiteral("map.csgTool"))->trigger();
				drain();
				expect(section->isExpanded() && section->isVisibleTo(&shell), "CSG Tool opens its section");
				expect(!QApplication::focusWidget() || QApplication::focusWidget() == thickness || thickness->isAncestorOf(QApplication::focusWidget()),
					"CSG Tool focuses the thickness", QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : QString());

				// Use Grid copies the grid; every option is saved.
				for (int step = 0; step < 3 && (step == 0 || std::abs(view->gridSize() - 16.0) < 1e-9); ++step) {
					command(QStringLiteral("map.gridLarger"))->trigger();
					drain();
				}
				const double grid = view->gridSize();
				useGrid->click();
				settings.sync();
				expect(std::abs(grid - 16.0) > 1e-9 && std::abs(thickness->value() - grid) < 1e-9 && std::abs(thickness->singleStep() - grid) < 1e-9
						&& std::abs(settings.levelCsgToolPreferences().thickness - grid) < 1e-9,
					"Use Grid copies the grid size and saves it", QStringLiteral("%1 for grid %2").arg(thickness->value()).arg(grid));
				thickness->setValue(16);
				caulk->setChecked(false);
				settings.sync();
				expect(std::abs(settings.levelCsgToolPreferences().thickness - 16.0) < 1e-9 && !settings.levelCsgToolPreferences().caulk,
					"the thickness and caulk are saved");
				expect(summary->text() != QString() && (scale != 100 || !summary->text().contains(QStringLiteral("common/caulk"))),
					"the summary follows caulk", summary->text());

				const auto selectRoom = [&] {
					view->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 0}});
					view->selectionSetChanged(view->selectionSet());
					drain();
				};
				const auto runRow = [&](const QString& id) {
					QPushButton* row = rows.value(id);
					if (!expect(row && row->isEnabled(), "the row is enabled with a brush selected", id)) {
						return false;
					}
					row->click();
					drain();
					return true;
				};

				// Diagonal joints, no caulk: six walls in one step.
				selectRoom();
				auto depth = shell.levelDocument().undoStack.size();
				if (runRow(QStringLiteral("map.csgHollowDiagonal"))) {
					expect(shell.levelDocument().brushes.size() == 7 && shell.levelDocument().undoStack.size() == depth + 1,
						"six diagonal walls in one step", QString::number(shell.levelDocument().brushes.size()));
					expect(faceCount(shell.levelDocument(), QStringLiteral("common/caulk")) == 0, "without caulk the walls keep their material");
					expect(scale != 100 || shell.statusBar()->currentMessage().contains(QStringLiteral("Hollowed")), "the status reports the hollow",
						shell.statusBar()->currentMessage());
					const QByteArray made = bytes();
					command(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == original, "undo restores the brush exactly");
					command(QStringLiteral("map.redo"))->trigger();
					expect(bytes() == made, "redo rebuilds the same walls");
					command(QStringLiteral("map.undo"))->trigger();
				}

				// Wrap keeping the inner brush, caulked between.
				caulk->setChecked(true);
				keepInner->setChecked(true);
				settings.sync();
				expect(settings.levelCsgToolPreferences().keepInner && settings.levelCsgToolPreferences().caulk, "keep inner and caulk are saved");
				selectRoom();
				depth = shell.levelDocument().undoStack.size();
				if (runRow(QStringLiteral("map.csgHollowWrap"))) {
					const LevelMapBrush* inner = brushWithId(shell.levelDocument(), 0);
					expect(shell.levelDocument().brushes.size() == 8 && shell.levelDocument().undoStack.size() == depth + 1,
						"six wrapped walls and the inner brush in one step", QString::number(shell.levelDocument().brushes.size()));
					expect(inner && std::abs(inner->maxs.x - 48) < 0.01 && faceCount(shell.levelDocument(), QStringLiteral("common/caulk")) >= 12,
						"the inner brush keeps its number, with caulk between it and the walls");
					command(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == original, "undo restores the brush again");
				}

				// Extrude on the picked face only, the inner brush removed.
				keepInner->setChecked(false);
				caulk->setChecked(false);
				faces->setCurrentIndex(faces->findData(QStringLiteral("only-picked")));
				settings.sync();
				expect(settings.levelCsgToolPreferences().facePolicy == LevelCsgFacePolicy::OnlyPicked, "the face policy is saved");
				selectRoom();
				if (view->componentMode() != MapViewport::ComponentMode::Faces) {
					command(QStringLiteral("map.faceTool"))->trigger();
				}
				const bool picked = view->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {5}}});
				view->componentPicksChanged(view->pickedComponentCount());
				drain();
				if (expect(picked, "the top face is picked") && runRow(QStringLiteral("map.csgHollowExtrude"))) {
					const LevelMapDocument& document = shell.levelDocument();
					expect(document.brushes.size() == 2, "one slab on the picked face replaces the brush", QString::number(document.brushes.size()));
					double lowest = 1e9;
					for (const LevelMapBrush& brush : document.brushes) {
						if (brush.faces.first().textureName == QLatin1String("studio/wall")) {
							lowest = std::min(lowest, brush.mins.z);
						}
					}
					expect(std::abs(lowest - 64) < 0.01, "the slab sits on top of where the brush was", QString::number(lowest));
					command(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == original, "undo restores the brush after the slab");
				}
				if (view->componentMode() == MapViewport::ComponentMode::Faces) {
					command(QStringLiteral("map.faceTool"))->trigger();
				}

				// A shaft: the faces facing the active 2D view are left open.
				faces->setCurrentIndex(faces->findData(QStringLiteral("exclude-view")));
				selectRoom();
				const MapViewportProjection projection = view->projection();
				if (runRow(QStringLiteral("map.csgHollowDiagonal"))) {
					expect(shell.levelDocument().brushes.size() == 5, "four diagonal walls round the view's axis",
						QString::number(shell.levelDocument().brushes.size()) + QLatin1Char(' ') + QString::number(static_cast<int>(projection)));
					command(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == original, "undo restores the brush after the shaft");
				}

				// Shrink and Expand by the thickness, faces picked: none.
				faces->setCurrentIndex(faces->findData(QStringLiteral("exclude-picked")));
				thickness->setValue(8);
				selectRoom();
				depth = shell.levelDocument().undoStack.size();
				if (runRow(QStringLiteral("map.csgShrink"))) {
					const LevelMapBrush* shrunk = brushWithId(shell.levelDocument(), 0);
					expect(shrunk && std::abs(shrunk->maxs.x - 56) < 0.01 && std::abs(shrunk->mins.z + 56) < 0.01
							&& shell.levelDocument().undoStack.size() == depth + 1,
						"Shrink moves every face 8 units in, in one step");
					command(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == original, "undo restores the brush after Shrink");
				}
				selectRoom();
				command(QStringLiteral("map.csgExpand"))->trigger();
				drain();
				const LevelMapBrush* grown = brushWithId(shell.levelDocument(), 0);
				expect(grown && std::abs(grown->maxs.x - 72) < 0.01, "Expand, run as a command, moves every face 8 units out");
				command(QStringLiteral("map.undo"))->trigger();
				expect(bytes() == original, "undo restores the brush after Expand");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	if (!ok) {
		return 1;
	}
	std::cout << "CSG Tool UI: section, options, hollows, keep inner, face policies, shrink and expand, undo and redo passed for the NetRadiant "
				 "Custom and TrenchBroom profiles."
			  << std::endl;
	return 0;
}
