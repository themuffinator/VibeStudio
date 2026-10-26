// Create Brush from Points… and Copy Vertex Coordinates in the shell
// (app/level_brush_points_actions.cpp, app/level_brush_points_dialog.cpp):
// registration in the palette, a menu and the Tools tab; enablement; copying
// the selected brushes', a brush entity's and picked faces' corners, leaving
// filtered brushes out and the map unchanged; and the dialog's accessible
// controls, refusals of flat and malformed points, Add Selected Corners, the
// worker-checked preview, one exact undo step in the current material, the
// remembered points and Cancel. NetRadiant Custom, TrenchBroom and
// VibeStudio profiles at 100%, and at 200% with high contrast, expanded
// translations and right-to-left layout. Owned offscreen widgets only.
#include "app/application_shell.h"
#include "app/level_brush_points_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/studio_semantics.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool until(const std::function<bool()>& ready, int milliseconds = 30000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < milliseconds) {
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

QString boxBrush(double x0, double y0, double z0, double x1, double y1, double z1)
{
	const auto p = [](double x, double y, double z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	const auto face = [&](const QString& a, const QString& b, const QString& c) { return QStringLiteral("%1 %2 %3 base/box 0 0 0 1 1").arg(a, b, c); };
	return QStringList {QStringLiteral("{"), face(p(x0 + 1, y0, z1), p(x0, y0, z1), p(x0, y0 + 1, z1)),
		face(p(x0, y0 + 1, z0), p(x0, y0, z0), p(x0 + 1, y0, z0)), face(p(x1, y0 + 1, z0), p(x1, y0, z0), p(x1, y0, z0 + 1)),
		face(p(x0, y0, z0 + 1), p(x0, y0, z0), p(x0, y0 + 1, z0)), face(p(x0, y1, z0 + 1), p(x0, y1, z0), p(x0 + 1, y1, z0)),
		face(p(x0 + 1, y0, z0), p(x0, y0, z0), p(x0, y0, z0 + 1)), QStringLiteral("}")}
		.join(QLatin1Char('\n'));
}

} // namespace

int main(int argc, char** argv)
{
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
	// Worldspawn brushes 0 (a 64 cube) and 1, and brush 2 in a func_door.
	const QString source = QStringList {QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\""), boxBrush(0, 0, 0, 64, 64, 64),
		boxBrush(128, 0, 0, 192, 32, 64), QStringLiteral("}"), QStringLiteral("{"), QStringLiteral("\"classname\" \"func_door\""),
		boxBrush(0, 128, 0, 64, 192, 64), QStringLiteral("}")}
							   .join(QLatin1Char('\n'));
	const QString path = temp.filePath("points.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(source.toUtf8()) <= 0) {
		return 1;
	}
	file.close();
	const QStringList ids {QStringLiteral("map.createBrushFromPoints"), QStringLiteral("map.copyVertexCoordinates")};
	const QString wedge = QStringLiteral("256,0,0 384,0,0 256,128,0\n(256 0 64) (384 0 64) (256 128 64)\n300 10 10");
	{
		LevelMapDocument empty;
		LevelBrushPointsDialog lazy(empty, {}, QStringLiteral("studio/points"), {});
		int calls = 0;
		lazy.setSelectionPointsProvider([&] { ++calls; return wedge; }, true);
		expect(calls == 0 && lazy.pointsText().isEmpty(), "selected corners are not solved when opening the dialog");
		lazy.findChild<QPushButton*>(QStringLiteral("brushPointsAddSelection"))->click();
		expect(calls == 1 && lazy.pointsText() == wedge, "selected corners are resolved on request");
		lazy.reject();
	}

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Brush points UI " << qPrintable(phase) << std::endl;
			settings.setSelectedEditorProfileId(profile);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			// Each pass starts without remembered points.
			settings.setShellLayoutState(QStringLiteral("levelBrushPoints"), {});
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
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				if (!expect(view && until([&] { return shell.levelDocument().brushes.size() == 3; }), "map ready")) {
					return 1;
				}
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) {
					view->setSelectionSet(refs);
					view->selectionSetChanged(refs);
				};
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto clipboard = [] { return QApplication::clipboard()->text(); };
				pick({});
				const auto baseline = bytes();

				// Registration: palette, a menu and the Tools tab.
				for (const QString& id : ids) {
					auto* command = action(id);
					if (!expect(command != nullptr, "registered", id)) {
						return 1;
					}
					bool menu = false;
					for (auto* owner : command->associatedObjects()) {
						menu |= qobject_cast<QMenu*>(owner) != nullptr;
					}
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
					CommandPaletteEntry entry;
					expect(!command->text().isEmpty() && !command->statusTip().isEmpty() && menu && commandPaletteEntryForCommandId(id, &entry)
							&& !entry.label.isEmpty() && !entry.summary.isEmpty(),
						"labelled, in the palette and a menu", id);
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
						"accessible Tools tile", id);
				}
				auto* create = action(QStringLiteral("map.createBrushFromPoints"));
				auto* copy = action(QStringLiteral("map.copyVertexCoordinates"));
				expect(create->isEnabled() && !copy->isEnabled(), "create is ready; copy waits for a selection");

				// Copy Vertex Coordinates: a brush, a brush entity, picked faces.
				pick({{LevelMapSelectionKind::QuakeBrush, 0}});
				expect(copy->isEnabled(), "copy enabled with a brush");
				QApplication::clipboard()->clear();
				copy->trigger();
				expect(clipboard() == QStringLiteral("// brush:0\n(0 0 0) (0 0 64) (0 64 0) (0 64 64) (64 0 0) (64 0 64) (64 64 0) (64 64 64)\n")
						&& bytes() == baseline && !shell.statusBar()->currentMessage().isEmpty(),
					"a brush's corners copied, map unchanged", clipboard());
				const int door = shell.levelDocument().brushes.at(2).entityId;
				pick({{LevelMapSelectionKind::Entity, door}});
				copy->trigger();
				expect(clipboard().startsWith(QStringLiteral("// brush:2\n(0 128 0)")), "a brush entity's brush", clipboard());
				view->setFilteredObjects({{LevelMapSelectionKind::QuakeBrush, 0}});
				pick({{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}});
				copy->trigger();
				expect(clipboard().startsWith(QStringLiteral("// brush:1\n")) && !clipboard().contains(QStringLiteral("brush:0")), "filtered brushes are left out",
					clipboard());
				view->setFilteredObjects({});
				pick({{LevelMapSelectionKind::QuakeBrush, 0}});
				action(QStringLiteral("map.faceTool"))->trigger();
				const QVector<LevelMapComponentSet> faces {{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {0, 1}}};
				expect(view->setPickedComponents(faces), "pick two faces");
				view->componentPicksChanged(2);
				copy->trigger();
				expect(clipboard().startsWith(QStringLiteral("// brush:0 face:0 base/box\n(0 0 64)")) && clipboard().contains(QStringLiteral("// brush:0 face:1 base/box\n"))
						&& clipboard().split(QStringLiteral("//")).size() == 3,
					"picked faces' corners", clipboard());
				action(QStringLiteral("map.selectNone"))->trigger();
				action(QStringLiteral("map.selectNone"))->trigger();
				expect(bytes() == baseline && view->componentMode() == MapViewport::ComponentMode::Off, "copying changes nothing");

				// Create Brush from Points…
				auto* paint = shell.findChild<QComboBox*>(QStringLiteral("levelPaintMaterial"));
				if (expect(paint != nullptr, "current material picker")) {
					paint->setEditText(QStringLiteral("studio/points"));
				}
				pick({{LevelMapSelectionKind::QuakeBrush, 1}});
				const auto depth = shell.levelDocument().undoStack.size();
				const auto run = [&](const std::function<void(LevelBrushPointsDialog*)>& body) {
					bool seen = false;
					QTimer::singleShot(0, &shell, [&] {
						auto* dialog = static_cast<LevelBrushPointsDialog*>(shell.findChild<QDialog*>(QStringLiteral("levelBrushPointsDialog")));
						if (!dialog || !until([&] { return dialog->isVisible(); }, 5000)) {
							if (dialog) {
								dialog->reject();
							}
							return;
						}
						seen = true;
						body(dialog);
						if (dialog->isVisible()) {
							dialog->reject();
						}
					});
					create->trigger();
					return seen;
				};
				const auto settle = [](LevelBrushPointsDialog* dialog) { return until([&] { return dialog->isReady(); }); };
				expect(run([&](LevelBrushPointsDialog* dialog) {
					auto* text = dialog->findChild<QPlainTextEdit*>(QStringLiteral("brushPointsText"));
					auto* material = dialog->findChild<QComboBox*>(QStringLiteral("brushPointsMaterial"));
					auto* status = dialog->findChild<QLabel*>(QStringLiteral("brushPointsStatus"));
					auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("brushPointsApply"));
					auto* corners = dialog->findChild<QPushButton*>(QStringLiteral("brushPointsAddSelection"));
					if (!expect(text && material && status && apply && corners, "dialog controls")) {
						return;
					}
					expect(!dialog->accessibleName().isEmpty() && !text->accessibleName().isEmpty() && !text->accessibleDescription().isEmpty()
							&& !material->accessibleName().isEmpty() && !status->accessibleName().isEmpty() && !apply->accessibleName().isEmpty()
							&& text->focusPolicy() != Qt::NoFocus && text->tabChangesFocus() && text->layoutDirection() == Qt::LeftToRight,
						"accessible controls");
					expect(material->currentText() == QStringLiteral("studio/points") && corners->isEnabled() && text->toPlainText().isEmpty(),
						"current material, selection corners on offer, empty start");
					expect(settle(dialog) && !dialog->isValid() && !apply->isEnabled() && !status->text().isEmpty(), "nothing typed yet");
					dialog->setPointsText(QStringLiteral("0,0,0 64,0,0 0,64,0 64,64,0"));
					expect(settle(dialog) && !dialog->isValid() && !apply->isEnabled() && status->text() == status->accessibleDescription()
							&& !status->text().isEmpty(),
						"flat points refused", status->text());
					dialog->setPointsText(QStringLiteral("0,0,0 64,0"));
					expect(settle(dialog) && !dialog->isValid() && !apply->isEnabled(), "malformed points refused", status->text());
					dialog->setPointsText({});
					corners->click();
					expect(text->toPlainText().startsWith(QStringLiteral("// brush:1\n")) && settle(dialog) && dialog->isValid() && dialog->report().corners == 8,
						"Add Selected Corners", status->text());
					dialog->setPointsText(wedge);
					expect(settle(dialog) && dialog->isValid() && apply->isEnabled() && dialog->report().corners == 6 && dialog->report().faces == 5
							&& dialog->report().unusedPoints == 1,
						"the wedge is checked", status->text());
					apply->click();
				}),
					"the dialog opens");
				const auto& document = shell.levelDocument();
				expect(document.brushes.size() == 4 && document.undoStack.size() == depth + 1 && document.selection.size() == 1
						&& document.selection.first().objectId == document.brushes.last().id && document.brushes.last().faceCount == 5
						&& std::all_of(document.brushes.last().faces.cbegin(), document.brushes.last().faces.cend(),
							[](const LevelMapBrushFace& face) { return face.textureName == QStringLiteral("studio/points"); }),
					"one step adds the selected wedge in the current material", shell.statusBar()->currentMessage());
				action(QStringLiteral("map.undo"))->trigger();
				expect(bytes() == baseline, "undo restores the exact map");
				// The typed points come back; Cancel leaves the map alone.
				expect(run([&](LevelBrushPointsDialog* dialog) {
					expect(dialog->pointsText() == wedge && settle(dialog) && dialog->isValid(), "the last points are remembered", dialog->pointsText());
					dialog->reject();
				}),
					"the dialog reopens");
				expect(bytes() == baseline && shell.levelDocument().undoStack.size() == depth, "Cancel changes nothing");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	std::cout << (ok ? "level brush points UI smoke passed" : "level brush points UI smoke FAILED") << std::endl;
	return ok ? 0 : 1;
}
