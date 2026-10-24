// World-axis Flip X/Y/Z and Rotate X/Y/Z in the shell, the profile's quick-turn
// pivot for them and for Rotate 90° Right and Left in every projection, vertical flips
// of Quake brushes, component mode, Repeat, undo, menus, Tools tiles and the
// palette, across the NRC, TrenchBroom and VibeStudio profiles at 100% and at
// 200% with high contrast, expanded translations and right-to-left layout.
#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_quick_transform.h"
#include "core/map_geometry.h"
#include "core/studio_semantics.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QMenu>
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
bool onGrid(double value, double grid) { return std::abs(value / grid - std::round(value / grid)) < 1e-9; }
bool brushOnGrid(const LevelMapDocument& map, int id, double grid)
{
	for (const LevelMapBrush& brush : map.brushes) {
		if (brush.id != id) { continue; }
		const MapBrushGeometry mesh = solveBrushGeometry(brush.faces);
		if (!mesh.solved) { return false; }
		for (const auto& face : mesh.faces) {
			for (const LevelMapVec3& p : face.points) {
				if (!onGrid(p.x, grid) || !onGrid(p.y, grid) || !onGrid(p.z, grid)) { return false; }
			}
		}
		return true;
	}
	return false;
}
} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets, semantic signals and actions only: no native
	// input, operating-system capture or game launch.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference("four-views");
	// Brush 0's footprint is odd across X and Z turns, brush 1's across Y turns.
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	if (!expect(createLevelMap(create, &created, &error)
			&& addLevelMapBoxBrush(&created, {0, 0, 0, true}, {16, 32, 16, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&created, {64, 0, 0, true}, {96, 16, 16, true}, "studio/checker", nullptr, &error), "fixture", error)) {
		return 1;
	}
	const auto path = temp.filePath("axis.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();
	const QStringList ids {QStringLiteral("map.flipX"), QStringLiteral("map.flipY"), QStringLiteral("map.flipZ"),
		QStringLiteral("map.rotateX"), QStringLiteral("map.rotateY"), QStringLiteral("map.rotateZ")};
	for (const auto& id : ids) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", id);
	}
	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		const bool rounds = QLatin1String(profile) != QLatin1String("vibestudio-default");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Axis transforms UI " << phase.toStdString() << std::endl;
			settings.setSelectedEditorProfileId(profile);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
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
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				action(QStringLiteral("map.layout.four-views"))->trigger();
				if (!expect(until([&] { return shell.levelDocument().brushes.size() == 2 && shell.findChildren<MapViewport*>().size() >= 3; }), "map ready")) {
					return 1;
				}
				QHash<int, MapViewport*> panes;
				for (auto* pane : shell.findChildren<MapViewport*>()) {
					if (!panes.contains(static_cast<int>(pane->projection()))) { panes.insert(static_cast<int>(pane->projection()), pane); }
				}
				auto* grid = shell.findChild<QComboBox*>("levelMapGrid");
				auto* snap = shell.findChild<QCheckBox*>("levelMapSnap");
				auto* undo = action(QStringLiteral("map.undo"));
				if (!expect(panes.size() == 3 && grid && snap && undo, "panes, grid and snap")) { return 1; }
				grid->setCurrentIndex(grid->findData(16));
				snap->setChecked(true);
				const auto pick = [&](MapViewport* pane, int id) {
					const QVector<LevelMapSelectionRef> refs {{LevelMapSelectionKind::QuakeBrush, id}};
					pane->setSelectionSet(refs);
					pane->selectionSetChanged(refs);
				};
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto baseline = bytes();

				// Registration: Edit > Transform, the Transform toolbar menu and
				// Tools tiles; no default keys, as in NRC; disabled without a
				// selection; accessible and keyboard reachable.
				panes.value(0)->setSelectionSet({});
				panes.value(0)->selectionSetChanged({});
				for (const auto& id : ids) {
					auto* command = action(id);
					if (!expect(command != nullptr, "registered", id)) { return 1; }
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
					bool menu = false;
					for (auto* owner : command->associatedObjects()) { menu |= qobject_cast<QMenu*>(owner) != nullptr; }
					expect(!command->isEnabled() && command->shortcuts().isEmpty() && !command->text().isEmpty() && !command->statusTip().isEmpty() && menu,
						"disabled, unbound and in a menu without a selection", id + command->shortcut().toString());
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && !row->toolTip().isEmpty()
							&& QAccessible::queryAccessibleInterface(row),
						"accessible Tools tile", id);
				}

				// Each world-axis command, from a pane that does not show its axis
				// as horizontal or vertical: the shared core result, one undo step.
				const QVector<int> brushFor {0, 0, 0, 0, 1, 0};
				for (int index = 0; index < ids.size(); ++index) {
					LevelAxisTransform transform;
					levelAxisTransformForCommand(ids.at(index), &transform);
					const int brush = brushFor.at(index);
					// The side view for X commands, the top view for Y and Z ones.
					pick(panes.value(transform.axis == 0 ? 2 : 0), brush);
					expect(action(ids.at(index))->isEnabled(), "enabled with a selection", ids.at(index));
					auto expected = shell.levelDocument();
					LevelMapVec3 pivot;
					if (rounds && !transform.mirror) { levelMapQuickTurnPivot(expected, 16, &pivot); }
					if (transform.mirror) {
						expect(flipLevelMapSelection(&expected, transform.axis, {settings.levelTextureLock(), settings.levelAllowValve220()}, &error), "reference flip", error);
					} else {
						LevelMapRotationRequest rotation;
						rotation.axis = transform.axis;
						rotation.degrees = 90.0 * transform.quarterTurns;
						rotation.pivot = pivot;
						rotation.textureLock = settings.levelTextureLock();
						rotation.allowValve220 = settings.levelAllowValve220();
						expect(rotateLevelMapSelection(&expected, rotation, &error), "reference turn", error);
					}
					const auto depth = shell.levelDocument().undoStack.size();
					action(ids.at(index))->trigger();
					expect(bytes() == serializeLevelMap(expected).bytes && shell.levelDocument().undoStack.size() == depth + 1,
						"same edit as the shared service, one undo step", ids.at(index) + QLatin1Char(' ') + shell.statusBar()->currentMessage());
					if (!transform.mirror) {
						expect(brushOnGrid(shell.levelDocument(), brush, 16) == rounds, "odd footprint stays on the grid only when the profile rounds the pivot",
							ids.at(index));
						const auto& recorded = shell.levelDocument().repeat.actions;
						expect(!recorded.isEmpty() && recorded.last().rotationAxis == transform.axis && action(QStringLiteral("map.repeat"))->isEnabled(),
							"turn feeds Repeat", ids.at(index));
					}
					undo->trigger();
					expect(bytes() == baseline, "undo restores the source", ids.at(index));
				}

				// Rotate 90° Right and Left in each projection use the same pivot
				// policy: Right is -90 about Z from the top, +90 about Y from the
				// front and -90 about X from the side; Left is the opposite.
				for (int projection = 0; projection < 3; ++projection) {
					for (const bool right : {true, false}) {
						const int axis = projection == 0 ? 2 : projection == 1 ? 1 : 0;
						const int brush = axis == 1 ? 1 : 0;
						pick(panes.value(projection), brush);
						auto expected = shell.levelDocument();
						LevelMapRotationRequest rotation;
						rotation.axis = axis;
						rotation.degrees = (projection == 1) == right ? 90 : -90;
						if (rounds) { levelMapQuickTurnPivot(expected, 16, &rotation.pivot); }
						rotation.textureLock = settings.levelTextureLock();
						rotation.allowValve220 = settings.levelAllowValve220();
						expect(rotateLevelMapSelection(&expected, rotation, &error), "reference quick turn", error);
						action(right ? QStringLiteral("map.rotateRight") : QStringLiteral("map.rotateLeft"))->trigger();
						expect(bytes() == serializeLevelMap(expected).bytes && brushOnGrid(shell.levelDocument(), brush, 16) == rounds,
							"Rotate 90° Right and Left follow the projection and the pivot policy",
							QStringLiteral("%1 %2 ").arg(projection).arg(right) + shell.statusBar()->currentMessage());
						undo->trigger();
					}
				}
				// Flip Vertical of a Quake brush in the front view mirrors along Z.
				pick(panes.value(1), 0);
				auto vertical = shell.levelDocument();
				expect(flipLevelMapSelection(&vertical, 2, {settings.levelTextureLock(), settings.levelAllowValve220()}, &error), "reference vertical flip", error);
				action(QStringLiteral("map.flipVertical"))->trigger();
				expect(bytes() == serializeLevelMap(vertical).bytes, "front-view Flip Vertical mirrors a Quake brush along Z", shell.statusBar()->currentMessage());
				undo->trigger();
				expect(bytes() == baseline, "quick transform undo restores the source");

				if (scale == 100) {
					// Snap off turns about the exact centre under every profile.
					snap->setChecked(false);
					pick(panes.value(0), 0);
					action(QStringLiteral("map.rotateZ"))->trigger();
					expect(!brushOnGrid(shell.levelDocument(), 0, 16), "without Snap the pivot is the exact centre");
					undo->trigger();
					snap->setChecked(true);
					// Component mode turns the picked corners about their own
					// bounds, rounded under the same policy.
					pick(panes.value(0), 0);
					action(QStringLiteral("map.vertexTool"))->trigger();
					auto* view = panes.value(0);
					const QVector<LevelMapComponentSet> corners {{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Vertex, {0, 1, 2, 3, 4, 5, 6, 7}}};
					if (expect(view->setPickedComponents(corners), "pick every corner")) {
						view->componentPicksChanged(8);
						const auto depth = shell.levelDocument().undoStack.size();
						action(QStringLiteral("map.rotateZ"))->trigger();
						expect(shell.levelDocument().undoStack.size() == depth + 1 && brushOnGrid(shell.levelDocument(), 0, 16) == rounds,
							"component Rotate Z uses the profile's pivot", shell.statusBar()->currentMessage());
						undo->trigger();
					}
					action(QStringLiteral("map.selectNone"))->trigger();
					action(QStringLiteral("map.selectNone"))->trigger();
					expect(bytes() == baseline, "component turn undo restores the source");
				}
				if (scale == 200) {
					const auto capture = qEnvironmentVariable("VIBESTUDIO_AXIS_TRANSFORM_CAPTURE");
					if (!capture.isEmpty() && QLatin1String(profile) == QLatin1String("netradiant-custom")) {
						QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied);
						shell.render(&image);
						expect(image.save(capture), "owned Qt capture");
					}
				}
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	return ok ? 0 : 1;
}
