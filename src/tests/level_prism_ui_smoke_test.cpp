// Make Prism…, Make Cone…, Make Sphere… and Make 3- to 9-Sided Prism in the
// shell (app/level_prism_actions.cpp): registration in the palette, the Edit
// menu and the Tools tab; disabled with no brush; the side-count prompt
// (accepted and cancelled); each selected brush, a brush entity's included,
// rebuilt in place along the active plan view's depth axis in one exact undo
// step; NetRadiant Custom's Ctrl+3 to Ctrl+9 claimed by the views while a
// brush is selected and left to the mode keys otherwise and in the other
// profiles; and the Shapes tab's Fit choice and raised side limits.
// NetRadiant Custom, TrenchBroom and VibeStudio profiles at 100%, and at 200%
// with high contrast, expanded translations and right-to-left layout. Owned
// offscreen widgets and synthetic Qt Test key events only.
#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/map_geometry.h"
#include "core/studio_semantics.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

#include <algorithm>
#include <cmath>
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

const LevelMapBrush* brushById(const LevelMapDocument& document, int id)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}

// True when the brush has a face with this outward normal.
bool facing(const LevelMapDocument& document, int id, double nx, double ny, double nz)
{
	const LevelMapBrush* brush = brushById(document, id);
	if (!brush) {
		return false;
	}
	for (const MapFacePolygon& face : solveBrushGeometry(brush->faces).faces) {
		if (face.isValid() && std::abs(face.plane.normalX - nx) < 1e-4 && std::abs(face.plane.normalY - ny) < 1e-4 && std::abs(face.plane.normalZ - nz) < 1e-4) {
			return true;
		}
	}
	return false;
}

int faces(const LevelMapDocument& document, int id)
{
	const LevelMapBrush* brush = brushById(document, id);
	return brush ? brush->faceCount : -1;
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
	// Worldspawn brushes 0 (a 64 cube) and 1 (64 by 32 by 64), and brush 2 in
	// a func_door.
	const QString source = QStringList {QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\""), boxBrush(0, 0, 0, 64, 64, 64),
		boxBrush(128, 0, 0, 192, 32, 64), QStringLiteral("}"), QStringLiteral("{"), QStringLiteral("\"classname\" \"func_door\""),
		boxBrush(0, 128, 0, 64, 192, 64), QStringLiteral("}")}
							   .join(QLatin1Char('\n'));
	const QString path = temp.filePath("prism.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(source.toUtf8()) <= 0) {
		return 1;
	}
	file.close();
	QStringList ids {QStringLiteral("map.makePrism"), QStringLiteral("map.makeCone"), QStringLiteral("map.makeSphere")};
	for (int sides = 3; sides <= 9; ++sides) {
		ids << QStringLiteral("map.make%1SidedPrism").arg(sides);
	}

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Prism UI " << qPrintable(phase) << std::endl;
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
				if (!expect(until([&] { return shell.levelDocument().brushes.size() == 3 && shell.findChildren<MapViewport*>().size() >= 3; }), "map ready")) {
					return 1;
				}
				QHash<int, MapViewport*> panes;
				for (auto* pane : shell.findChildren<MapViewport*>()) {
					if (!panes.contains(static_cast<int>(pane->projection()))) {
						panes.insert(static_cast<int>(pane->projection()), pane);
					}
				}
				auto* top = panes.value(static_cast<int>(MapViewportProjection::TopXY));
				auto* front = panes.value(static_cast<int>(MapViewportProjection::FrontXZ));
				auto* undo = action(QStringLiteral("map.undo"));
				if (!expect(top && front && undo, "panes and undo")) {
					return 1;
				}
				const auto pick = [&](MapViewport* pane, const QVector<LevelMapSelectionRef>& refs) {
					pane->setSelectionSet(refs);
					pane->selectionSetChanged(refs);
				};
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				auto* stack = shell.findChild<QStackedWidget*>(QStringLiteral("modeStack"));
				if (!expect(stack != nullptr, "mode stack")) {
					return 1;
				}
				const auto mode = [&] { return static_cast<StudioMode>(stack->currentIndex()); };
				const auto focus = [&](QWidget* view) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return view->hasFocus(); }), "view takes focus", view->objectName());
				};
				const auto press = [&](QWidget* view, Qt::Key key, Qt::KeyboardModifiers modifiers) {
					focus(view);
					QTest::keyClick(view, key, modifiers);
				};
				// Answers the side-count prompt the next command opens, or cancels it.
				const auto answer = [&](QAction* command, int sides) {
					bool seen = false;
					QTimer::singleShot(0, &shell, [&] {
						auto* prompt = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
						if (!prompt) {
							prompt = shell.findChild<QInputDialog*>(QStringLiteral("levelPrismSidesDialog"));
						}
						if (!prompt) {
							return;
						}
						seen = prompt->objectName() == QStringLiteral("levelPrismSidesDialog") && !prompt->accessibleName().isEmpty()
							&& !prompt->labelText().isEmpty() && prompt->intMinimum() == 3;
						if (sides > 0) {
							prompt->setIntValue(sides);
							prompt->accept();
						} else {
							prompt->reject();
						}
					});
					command->trigger();
					return seen;
				};
				const auto baseline = bytes();

				// Registration: palette, menu, Tools tab; disabled with no brush.
				pick(top, {});
				for (const QString& id : std::as_const(ids)) {
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
					expect(!command->isEnabled() && !command->text().isEmpty() && !command->statusTip().isEmpty() && menu
							&& commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(),
						"disabled without a brush, labelled, in the palette and a menu", id);
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
						"accessible Tools tile", id);
				}
				// NetRadiant Custom's Ctrl+3 to Ctrl+9; the mode keys stay.
				const auto keys = [&](const QString& id) { return action(id)->shortcuts(); };
				for (int sides = 3; sides <= 9; ++sides) {
					const QString id = QStringLiteral("map.make%1SidedPrism").arg(sides);
					const QKeySequence wanted(QStringLiteral("Ctrl+%1").arg(sides));
					expect(nrc ? keys(id) == QList<QKeySequence> {wanted} : keys(id).isEmpty(), "sided prism keys follow the profile",
						id + QLatin1Char(' ') + QKeySequence::listToString(keys(id)));
				}
				expect(keys(QStringLiteral("shell.mode.models")).contains(QKeySequence(QStringLiteral("Ctrl+3")))
						&& keys(QStringLiteral("shell.mode.build")).contains(QKeySequence(QStringLiteral("Ctrl+9"))),
					"the mode keys keep Ctrl+3 and Ctrl+9");

				// With nothing selected, Ctrl+3 in a view still opens Models.
				press(top, Qt::Key_3, Qt::ControlModifier);
				expect(until([&] { return mode() == StudioMode::Models; }, 5000) && bytes() == baseline, "Ctrl+3 switches modes with no brush");
				action(QStringLiteral("shell.mode.levels"))->trigger();
				expect(until([&] { return mode() == StudioMode::Levels && top->isVisible(); }), "back to Levels");

				// A selected brush: Radiant's Ctrl+6 rebuilds it in the view; the
				// other profiles keep the mode key.
				pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
				const auto depth = shell.levelDocument().undoStack.size();
				press(top, Qt::Key_6, Qt::ControlModifier);
				if (nrc) {
					expect(until([&] { return faces(shell.levelDocument(), 0) == 8; }, 5000) && mode() == StudioMode::Levels
							&& shell.levelDocument().undoStack.size() == depth + 1 && facing(shell.levelDocument(), 0, 1, 0, 0)
							&& facing(shell.levelDocument(), 0, 0, 0, 1) && shell.levelDocument().selection.size() == 1,
						"Ctrl+6 makes a six-sided prism along Z in one step", shell.statusBar()->currentMessage());
					const auto rebuilt = bytes();
					QKeyEvent repeat(QEvent::KeyPress, Qt::Key_6, Qt::ControlModifier, {}, true);
					QApplication::sendEvent(top, &repeat);
					expect(bytes() == rebuilt && shell.levelDocument().undoStack.size() == depth + 1,
						"holding the prism key does not rebuild or add undo steps");
					undo->trigger();
					expect(bytes() == baseline, "undo restores the exact map");
				} else {
					expect(until([&] { return mode() == StudioMode::Packages; }, 5000) && bytes() == baseline,
						"Ctrl+6 still opens Packages in this profile");
					action(QStringLiteral("shell.mode.levels"))->trigger();
					expect(until([&] { return mode() == StudioMode::Levels && top->isVisible(); }), "back to Levels");
					pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
				}

				// Make Prism… asks for the sides and follows the active plan view.
				auto* makePrism = action(QStringLiteral("map.makePrism"));
				expect(makePrism->isEnabled() && answer(makePrism, 5) && faces(shell.levelDocument(), 0) == 7 && facing(shell.levelDocument(), 0, 0, 0, -1)
						&& facing(shell.levelDocument(), 0, 1, 0, 0),
					"Make Prism… with five sides along the top view's Z", shell.statusBar()->currentMessage());
				undo->trigger();
				expect(bytes() == baseline, "undo the five-sided prism");
				focus(front);
				pick(front, {{LevelMapSelectionKind::QuakeBrush, 0}});
				expect(answer(makePrism, 5) && faces(shell.levelDocument(), 0) == 7 && facing(shell.levelDocument(), 0, 0, 1, 0)
						&& facing(shell.levelDocument(), 0, 0, -1, 0) && facing(shell.levelDocument(), 0, 0, 0, 1),
					"Make Prism… from the front view runs along Y with a face up Z", shell.statusBar()->currentMessage());
				undo->trigger();
				expect(bytes() == baseline, "undo the front-view prism");
				// Cancelling the prompt changes nothing.
				const auto steps = shell.levelDocument().undoStack.size();
				expect(answer(action(QStringLiteral("map.makeCone")), 0) && bytes() == baseline && shell.levelDocument().undoStack.size() == steps,
					"a cancelled Make Cone… leaves the map alone");
				// Make Sphere… uses its count for sides and bands.
				focus(top);
				pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
				expect(answer(action(QStringLiteral("map.makeSphere")), 6) && faces(shell.levelDocument(), 0) == 36,
					"Make Sphere… with six sides and bands", shell.statusBar()->currentMessage());
				undo->trigger();
				expect(bytes() == baseline, "undo the sphere");
				// A brush entity's brushes are rebuilt and stay its own.
				const int door = brushById(shell.levelDocument(), 2)->entityId;
				pick(top, {{LevelMapSelectionKind::Entity, door}, {LevelMapSelectionKind::QuakeBrush, 1}});
				action(QStringLiteral("map.make4SidedPrism"))->trigger();
				expect(faces(shell.levelDocument(), 2) == 6 && brushById(shell.levelDocument(), 2)->entityId == door && faces(shell.levelDocument(), 1) == 6
						&& std::abs(brushById(shell.levelDocument(), 1)->mins.y + 16) < 0.01 && shell.levelDocument().brushes.size() == 3,
					"Make 4-Sided Prism keeps the door's brush and squares the oblong one", shell.statusBar()->currentMessage());
				undo->trigger();
				expect(bytes() == baseline, "undo the square prisms");

				// The Shapes tab: Fit, and side limits as far as 128 faces allow.
				expect(shell.showLevelSidebarTab(QStringLiteral("shapes")), "Shapes tab");
				auto* fit = shell.findChild<QComboBox*>(QStringLiteral("levelShapeFit"));
				auto* sides = shell.findChild<QSpinBox*>(QStringLiteral("levelShapeSides"));
				auto* cylinder = shell.findChild<QToolButton*>(QStringLiteral("levelShape-cylinder"));
				auto* cone = shell.findChild<QToolButton*>(QStringLiteral("levelShape-cone"));
				auto* box = shell.findChild<QToolButton*>(QStringLiteral("levelShape-box"));
				auto* replace = shell.findChild<QAbstractButton*>(QStringLiteral("levelShapeReplace"));
				if (expect(fit && sides && cylinder && cone && box && replace, "Shapes controls")) {
					expect(!fit->accessibleName().isEmpty() && !fit->toolTip().isEmpty() && fit->count() == 2 && fit->focusPolicy() != Qt::NoFocus,
						"accessible Fit choice");
					cone->click();
					expect(sides->maximum() == 127 && fit->isVisibleTo(&shell), "cones take 127 sides and a fit");
					box->click();
					expect(!fit->isVisibleTo(&shell), "a box has no fit");
					cylinder->click();
					expect(sides->maximum() == 126, "cylinders take 126 sides", QString::number(sides->maximum()));
					sides->setValue(4);
					fit->setCurrentIndex(fit->findData(QStringLiteral("circle")));
					pick(top, {{LevelMapSelectionKind::QuakeBrush, 1}});
					replace->click();
					const int made = shell.levelDocument().selection.isEmpty() ? -1 : shell.levelDocument().selection.first().objectId;
					expect(faces(shell.levelDocument(), made) == 6 && facing(shell.levelDocument(), made, 1, 0, 0)
							&& std::abs(brushById(shell.levelDocument(), made)->mins.y + 16) < 0.01,
						"Replace Selected Brushes with a circle-fit square", shell.statusBar()->currentMessage());
					undo->trigger();
					expect(bytes() == baseline, "undo the replacement");
					fit->setCurrentIndex(fit->findData(QStringLiteral("box")));
					sides->setValue(8);
				}
				expect(bytes() == baseline, "every edit undone exactly");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	std::cout << (ok ? "level prism UI smoke passed" : "level prism UI smoke FAILED") << std::endl;
	return ok ? 0 : 1;
}
