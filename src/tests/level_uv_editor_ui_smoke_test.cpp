// Offscreen checks of the UV Editor (NetRadiant Custom's TexTool) in the real
// shell, under the TrenchBroom profile at 100% and the NetRadiant Custom
// profile at 200% text with the high-visibility theme, an expanding
// translator and right-to-left layout. The command is registered with a
// palette entry, a menu, a Tools tab tile and no key (neither upstream binds
// TexTool); it waits for one picked face or one selected patch and refuses a
// hidden one. In the dialog the texture-space view is accessible, reads left
// to right in every layout, and its preview follows the edit: a drag of a
// face point moves the whole texture onto the snap grid and makes that point
// the anchor; a drag of another point turns the texture about the anchor
// (the nearest classic mapping, or Valve 220 when allowed); arrow keys,
// brackets, Ctrl+Z and Ctrl+Shift+Z nudge, turn, undo and redo; a point's
// cell moves it; Reset restores the start. Apply commits exactly what the
// shared service commits as one undo step, and Escape cancels without a
// change. A patch's control point moves on its own and applies the same way.

#include "app/application_shell.h"
#include "app/level_uv_editor_dialog.h"
#include "app/level_uv_view.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_brush.h"
#include "core/level_uv_edit.h"
#include "core/studio_semantics.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

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

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

void settle(int milliseconds = 50)
{
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
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

bool near(QPointF a, QPointF b, double tolerance = 1e-6)
{
	return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

QString text(QPointF p)
{
	return QStringLiteral("%1,%2").arg(p.x(), 0, 'g', 12).arg(p.y(), 0, 'g', 12);
}

QByteArray patchMap()
{
	return QByteArrayLiteral(
		"{\n\"classname\" \"worldspawn\"\n{\npatchDef2\n{\nstudio/grid\n( 3 3 0 0 0 )\n(\n( ( -128 -128 0 0 0 ) ( -128 -96 0 0 0.5 ) ( -128 -64 0 0 1 ) )\n"
		"( ( -96 -128 0 0.5 0 ) ( -96 -96 0 0.5 0.5 ) ( -96 -64 0 0.5 1 ) )\n( ( -64 -128 0 1 0 ) ( -64 -96 0 1 0.5 ) ( -64 -64 0 1 1 ) )\n)\n}\n}\n}\n");
}

} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and synthetic events only; no system input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QString quake2 = temp.filePath(QStringLiteral("quake2.map"));
	const QString curves = temp.filePath(QStringLiteral("curves.map"));
	expect(tests::putMaterialFile(quake2, tests::surfaceFixture(QStringLiteral("classic"))) && tests::putMaterialFile(curves, patchMap()), "fixtures");

	phase = QStringLiteral("registry");
	CommandPaletteEntry entry;
	expect(commandPaletteEntryForCommandId(QStringLiteral("map.uvEditor"), &entry) && entry.id == QStringLiteral("map-uv-editor") && !entry.label.isEmpty()
			&& !entry.summary.isEmpty(),
		"the UV Editor has a palette entry", entry.id);

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"trenchbroom", 100}, Pass {"netradiant-custom", 200}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "UV Editor UI " << qPrintable(phase) << std::endl;
		settings.setSelectedEditorProfileId(QString::fromLatin1(pass.profile));
		settings.setTextScalePercent(pass.scale);
		settings.setTheme(pass.scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, pass.scale));
		Expanded expanded;
		if (pass.scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		{
			ApplicationShell shell;
			shell.resize(pass.scale == 100 ? 1650 : 2400, pass.scale == 100 ? 1000 : 1500);
			QComboBox* engine = nullptr;
			for (auto* combo : shell.findChildren<QComboBox*>()) {
				if (combo->findData(QStringLiteral("idtech3")) >= 0 && combo->findData(QStringLiteral("idtech1")) >= 0) {
					engine = combo;
				}
			}
			const auto open = [&](const QString& path, const QString& family) {
				if (engine) {
					engine->setCurrentIndex(engine->findData(family));
				}
				shell.openPathFromCommandLine(path);
				return until([&] { return shell.levelDocument().sourcePath == path && (!shell.levelDocument().brushes.isEmpty() || !shell.levelDocument().patches.isEmpty()); });
			};
			shell.show();
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			if (!expect(open(quake2, QStringLiteral("idtech2")), "Quake II map ready")) {
				return 1;
			}
			action(QStringLiteral("shell.mode.levels"))->trigger();
			if (!expect(until([&] { return shell.findChildren<MapViewport*>().size() >= 3; }), "plan views")) {
				return 1;
			}
			MapViewport* top = nullptr;
			for (auto* pane : shell.findChildren<MapViewport*>()) {
				if (pane->projection() == MapViewportProjection::TopXY) {
					top = pane;
				}
			}
			QAction* editor = action(QStringLiteral("map.uvEditor"));
			if (!expect(top && editor, "the top view and the UV Editor command")) {
				return 1;
			}
			const int brushId = shell.levelDocument().brushes.first().id;
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };

			// Registration: a menu, a Tools tab tile, no key.
			bool inMenu = false;
			for (auto* owner : editor->associatedObjects()) {
				inMenu |= qobject_cast<QMenu*>(owner) != nullptr;
			}
			auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.uvEditor"));
			expect(!editor->text().isEmpty() && !editor->statusTip().isEmpty() && inMenu && tile && QAccessible::queryAccessibleInterface(tile)
					&& editor->shortcuts().isEmpty(),
				"the UV Editor is labelled, in a menu and on the Tools tab, with no key", editor->shortcut().toString());

			// Nothing targeted: disabled.
			top->setSelectionSet({});
			top->selectionSetChanged({});
			settle();
			expect(!editor->isEnabled(), "with nothing picked the UV Editor waits");

			// Pick the first face in face mode.
			const auto pickFace = [&](int index) {
				top->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, brushId}});
				top->selectionSetChanged(top->selectionSet());
				if (top->componentMode() != MapViewport::ComponentMode::Faces) {
					action(QStringLiteral("map.faceTool"))->trigger();
				}
				const bool picked = top->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush, brushId}, LevelBrushComponent::Face, {index}}});
				top->componentPicksChanged(top->pickedComponentCount());
				settle();
				return picked;
			};
			expect(pickFace(0) && editor->isEnabled(), "one picked face enables the UV Editor");

			// Modal dialogs are answered from the event loop.
			std::function<bool(QWidget*)> onModal;
			QTimer drive;
			drive.setInterval(30);
			QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
				QWidget* modal = QApplication::activeModalWidget();
				if (!modal || !onModal) {
					return;
				}
				auto handler = std::move(onModal);
				onModal = nullptr;
				if (!handler(modal)) {
					if (auto* dialog = qobject_cast<QDialog*>(modal)) {
						dialog->reject();
					}
				}
			});
			drive.start();
			const auto withDialog = [&](const std::function<bool(LevelUvEditorDialog*)>& handler) {
				bool handled = false;
				onModal = [&handled, handler](QWidget* modal) {
					auto* dialog = dynamic_cast<LevelUvEditorDialog*>(modal);
					return handled = dialog && handler(dialog);
				};
				QTimer watchdog;
				watchdog.setSingleShot(true);
				QObject::connect(&watchdog, &QTimer::timeout, [] {
					if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
						dialog->reject();
					}
				});
				watchdog.start(60000);
				editor->trigger();
				watchdog.stop();
				onModal = nullptr;
				return handled;
			};

			// A face: drags, keys, cells, Reset and Apply.
			const QByteArray original = bytes();
			const auto depth = shell.levelDocument().undoStack.size();
			QByteArray expected;
			const bool applied = withDialog([&](LevelUvEditorDialog* dialog) {
				if (!expect(until([&] { return dialog->isReady(); }), "the dialog reads the face")) {
					return false;
				}
				LevelUvView* view = dialog->view();
				const LevelUvLayout layout = view->layout();
				expect(!layout.isPatch() && layout.target.objectId == brushId && layout.target.faceIndex == 0 && layout.uv.size() >= 3, "the picked face is open");
				// Accessibility and direction.
				QAccessibleInterface* face = QAccessible::queryAccessibleInterface(view);
				expect(face && face->role() == QAccessible::Graphic && !view->accessibleName().isEmpty() && !view->accessibleDescription().isEmpty()
						&& view->focusPolicy() == Qt::StrongFocus && !view->toolTip().isEmpty(),
					"the view is an accessible keyboard control", view->accessibleDescription());
				expect(view->layoutDirection() == Qt::LeftToRight && dialog->layoutDirection() == (pass.scale == 200 ? Qt::RightToLeft : Qt::LeftToRight),
					"texture space reads left to right in every layout");
				expect(dialog->preview() && !dialog->preview()->accessibleName().isEmpty(), "the preview is named");
				for (const char* name : {"levelUvUndo", "levelUvRedo", "levelUvReset", "levelUvAnchor", "levelUvTurnAnticlockwise", "levelUvTurnClockwise",
						 "levelUvFrame", "levelUvZoomIn", "levelUvZoomOut"}) {
					auto* button = dialog->findChild<QPushButton*>(QString::fromLatin1(name));
					expect(button && !button->accessibleName().isEmpty() && !button->toolTip().isEmpty() && button->sizeHint().width() <= button->width() + 2,
						"each button is named and fits", QString::fromLatin1(name));
				}
				auto* status = dialog->findChild<QLabel*>(QStringLiteral("levelUvStatus"));
				auto* points = dialog->findChild<QTableWidget*>(QStringLiteral("levelUvPoints"));
				auto* allow = dialog->findChild<QCheckBox*>(QStringLiteral("levelUvAllowValve220"));
				auto* reset = dialog->findChild<QPushButton*>(QStringLiteral("levelUvReset"));
				expect(status && !status->text().isEmpty() && points && points->rowCount() == layout.uv.size() && allow && allow->isVisible() && reset,
					"status, point list and the Valve 220 choice for a classic face");
				expect(until([&] { return dialog->preview()->hasMesh(); }), "the preview shows the brush");
#ifdef VIBESTUDIO_HAVE_QTEST
				dialog->activateWindow();
				view->setFocus(Qt::OtherFocusReason);
				// Drag point 1: the whole texture follows onto the 1-texel grid,
				// and point 1 becomes the anchor.
				const QPointF startUv = view->result().uv[0];
				const QPoint from = view->texelToScreen(startUv).toPoint();
				const QPoint to = view->texelToScreen(startUv + QPointF(16, 8)).toPoint();
				QTest::mousePress(view, Qt::LeftButton, {}, from);
				QTest::mouseMove(view, (from + to) / 2);
				QTest::mouseMove(view, to);
				QTest::mouseRelease(view, Qt::LeftButton, {}, to);
				const QPointF moved = view->result().uv[0];
				expect(view->edit().transform.isTranslation() && !view->edit().transform.isIdentity() && moved.x() == std::round(moved.x())
						&& moved.y() == std::round(moved.y()) && near(moved, view->screenToTexel(to), 1.0) && view->anchor() == 0 && view->canUndo(),
					"a dragged point moves the texture onto the grid and becomes the anchor", text(moved));
				expect(near(view->result().uv[1] - view->result().uv[0], layout.uv[1] - layout.uv[0], 1e-6), "every point moves together");
				// Drag point 2: the texture turns about the anchor; this face's
				// unequal scales take the nearest classic mapping.
				const QPointF anchorUv = view->result().uv[0];
				const QPointF second = view->result().uv[1];
				const QPointF target = LevelUvTransform::rotation(anchorUv, 30).map(second);
				const QPoint turnFrom = view->texelToScreen(second).toPoint(), turnTo = view->texelToScreen(target).toPoint();
				QTest::mousePress(view, Qt::LeftButton, {}, turnFrom);
				QTest::mouseMove(view, (turnFrom + turnTo) / 2);
				QTest::mouseMove(view, turnTo);
				QTest::mouseRelease(view, Qt::LeftButton, {}, turnTo);
				expect(!view->edit().transform.isTranslation() && view->result().fit == LevelUvFit::Approximated && near(view->result().uv[0], anchorUv, 1e-6)
						&& !view->result().face.explicitTextureAxes && view->anchor() == 1,
					"a drag about the anchor turns the texture, keeping the anchor", text(view->result().uv[0]));
				allow->click();
				expect(view->result().fit == LevelUvFit::Converted && view->result().face.explicitTextureAxes, "allowed, the face converts to Valve 220");
				allow->click();
				expect(view->result().fit == LevelUvFit::Approximated, "and back");
				// Reset, then keys: a nudge, a turn, undo and redo.
				reset->click();
				expect(!view->changed() && view->result().uv == layout.uv, "Reset puts every point back");
				view->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(view, Qt::Key_Right);
				expect(near(view->result().uv[0], layout.uv[0] + QPointF(1, 0), 1e-9), "Right nudges one texel", text(view->result().uv[0]));
				QTest::keyClick(view, Qt::Key_Down, Qt::ShiftModifier);
				expect(near(view->result().uv[0], layout.uv[0] + QPointF(1, 8), 1e-9), "Shift+Down nudges eight", text(view->result().uv[0]));
				QTest::keyClick(view, Qt::Key_BracketRight);
				expect(!view->edit().transform.isTranslation(), "] turns");
				QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
				expect(near(view->result().uv[0], layout.uv[0] + QPointF(1, 8), 1e-9) && view->edit().transform.isTranslation(), "Ctrl+Z undoes the turn");
				QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
				expect(!view->edit().transform.isTranslation(), "Ctrl+Shift+Z redoes it");
				// A cell moves its point, and the whole texture with it.
				reset->click();
				points->item(0, 1)->setText(QLocale().toString(40));
				expect(view->result().uv[0].x() == 40 && std::abs(view->result().uv[0].y() - layout.uv[0].y()) < 1e-9, "a U cell moves point 1",
					text(view->result().uv[0]));
				// Right-click opens the view's menu.
				QTest::mouseClick(view, Qt::RightButton, {}, view->rect().center());
				expect(until([&] { return view->contextMenu()->isVisible(); }), "a right-click opens the menu");
				view->contextMenu()->hide();
#endif
				// Apply a known edit: the shared service's own result, as one step.
				reset->click();
				view->nudge({16, -8});
				// VIBESTUDIO_UV_EDITOR_GRAB=<folder> saves the dialog for a look.
				if (const QString grab = qEnvironmentVariable("VIBESTUDIO_UV_EDITOR_GRAB"); !grab.isEmpty()) {
					view->setAnchor(1);
					view->rotate(20);
					settle(200);
					dialog->grab().save(QDir(grab).filePath(QStringLiteral("uv-editor-%1-%2.png").arg(QLatin1String(pass.profile)).arg(pass.scale)));
					view->undo();
					view->setAnchor(-1);
				}
				LevelMapDocument reference = shell.levelDocument();
				LevelSurfaceEditPlan plan;
				QString problem;
				expect(prepareLevelUvEdit(reference, view->layout(), view->edit(), &plan, &problem) && commitLevelSurfaceEdit(&reference, plan, &problem),
					"the reference edit", problem);
				expected = serializeLevelMap(reference).bytes;
				auto* box = dialog->findChild<QDialogButtonBox*>();
				box->button(QDialogButtonBox::Ok)->click();
				return true;
			});
			expect(applied, "the face dialog ran");
			expect(bytes() == expected && shell.levelDocument().undoStack.size() == depth + 1, "Apply commits the service's edit as one undo step",
				shell.statusBar()->currentMessage());
			action(QStringLiteral("map.undo"))->trigger();
			expect(bytes() == original && shell.levelDocument().undoStack.size() == depth, "undo restores the exact map");

			// Escape cancels without a change.
			expect(pickFace(0) && editor->isEnabled(), "the face is picked again");
			const bool cancelled = withDialog([&](LevelUvEditorDialog* dialog) {
				if (!until([&] { return dialog->isReady(); })) {
					return false;
				}
				dialog->view()->nudge({4, 4});
#ifdef VIBESTUDIO_HAVE_QTEST
				dialog->activateWindow();
				dialog->view()->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(dialog->view(), Qt::Key_Escape);
				return until([&] { return !dialog->isVisible(); });
#else
				dialog->reject();
				return true;
#endif
			});
			expect(cancelled && bytes() == original && shell.levelDocument().undoStack.size() == depth, "Escape cancels without a change");
			const bool stalePackageRefused = withDialog([&](LevelUvEditorDialog* dialog) {
				if (!until([&] { return dialog->isReady(); })) {
					return false;
				}
				dialog->view()->nudge({4, 0});
				auto* reload = shell.findChild<QToolButton*>(QStringLiteral("levelPreviewReload"));
				if (!expect(reload != nullptr, "the package material reload control exists")) {
					return false;
				}
				// Simulate a package update completing while the modal editor is
				// open: source identity/reload matters even at the same revision.
				reload->click();
				dialog->accept();
				auto* status = dialog->findChild<QLabel*>(QStringLiteral("levelUvStatus"));
				expect(dialog->isVisible() && status && status->text().contains(QStringLiteral("map or package changed")),
					"a changed package prevents committing coordinates based on an old image");
				dialog->reject();
				return true;
			});
			expect(stalePackageRefused && bytes() == original && shell.levelDocument().undoStack.size() == depth,
				"a stale package leaves map bytes and history unchanged");

			// Two picked faces, or a hidden brush, are no target.
			top->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush, brushId}, LevelBrushComponent::Face, {0, 1}}});
			top->componentPicksChanged(top->pickedComponentCount());
			settle();
			expect(!editor->isEnabled(), "two picked faces are not one");
			action(QStringLiteral("map.faceTool"))->trigger();
			top->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, brushId}});
			top->selectionSetChanged(top->selectionSet());
			action(QStringLiteral("map.hideSelection"))->trigger();
			settle();
			expect(!editor->isEnabled(), "a hidden brush offers no face");
			action(QStringLiteral("map.showAll"))->trigger();
			settle();

			// A patch: one control point moves on its own.
			if (expect(open(curves, QStringLiteral("idtech3")), "patch map ready")) {
				const int patchId = shell.levelDocument().patches.first().id;
				top->setSelectionSet({{LevelMapSelectionKind::QuakePatch, patchId}});
				top->selectionSetChanged(top->selectionSet());
				settle();
				expect(editor->isEnabled(), "a selected patch enables the UV Editor");
				const QByteArray before = bytes();
				const auto patchDepth = shell.levelDocument().undoStack.size();
				QByteArray patchExpected;
				const bool patchApplied = withDialog([&](LevelUvEditorDialog* dialog) {
					if (!expect(until([&] { return dialog->isReady(); }), "the dialog reads the patch")) {
						return false;
					}
					LevelUvView* view = dialog->view();
					auto* override = dialog->findChild<QCheckBox*>(QStringLiteral("levelUvSizeOverride"));
					auto* allow = dialog->findChild<QCheckBox*>(QStringLiteral("levelUvAllowValve220"));
					expect(view->layout().isPatch() && view->layout().columns == 3 && view->layout().rows == 3 && override && override->isChecked()
							&& view->layout().imageSize == QSize(64, 64) && allow && !allow->isVisible(),
						"a patch without an image takes an explicit size the user can see");
					const QPointF start = view->result().uv[4];
#ifdef VIBESTUDIO_HAVE_QTEST
					dialog->activateWindow();
					view->setFocus(Qt::OtherFocusReason);
					const QPoint from = view->texelToScreen(start).toPoint();
					const QPoint to = view->texelToScreen(start + QPointF(8, 4)).toPoint();
					QTest::mousePress(view, Qt::LeftButton, {}, from);
					QTest::mouseMove(view, (from + to) / 2);
					QTest::mouseMove(view, to);
					QTest::mouseRelease(view, Qt::LeftButton, {}, to);
					expect(view->result().uv[4] != start && view->result().uv[0] == view->layout().uv[0] && view->selectedPoints() == QVector<int> {4},
						"a dragged control point moves on its own", text(view->result().uv[4]));
					expect(!override->isEnabled(), "the size waits while points have moved");
					view->resetEdit();
#endif
					view->setSelectedPoints({1, 4, 7});
					auto* points = dialog->findChild<QTableWidget*>(QStringLiteral("levelUvPoints"));
					expect(points && points->selectionModel()->selectedRows().size() == 3 && view->selectedPoints() == QVector<int>({1, 4, 7}),
						"the point table retains every selected control point");
					view->setSelectedPoints({4});
					view->nudge({8, 4});
					expect(view->result().uv[4] == start + QPointF(8, 4) && view->result().uv[3] == view->layout().uv[3], "a nudge moves the selected point");
					LevelMapDocument reference = shell.levelDocument();
					LevelSurfaceEditPlan plan;
					QString problem;
					expect(prepareLevelUvEdit(reference, view->layout(), view->edit(), &plan, &problem) && commitLevelSurfaceEdit(&reference, plan, &problem),
						"the patch reference edit", problem);
					patchExpected = serializeLevelMap(reference).bytes;
					dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
					return true;
				});
				expect(patchApplied && bytes() == patchExpected && shell.levelDocument().undoStack.size() == patchDepth + 1
						&& shell.levelDocument().patches.first().controlU.value(4) == 0.5 + 8.0 / 64,
					"the patch edit is one undo step", shell.statusBar()->currentMessage());
				action(QStringLiteral("map.undo"))->trigger();
				expect(bytes() == before, "undo restores the patch");
			}
			drive.stop();
			shell.close();
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "UV Editor UI checks passed" : "UV Editor UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
