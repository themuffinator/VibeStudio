// Offscreen checks of the proportional fits and texture projections on the
// Levels page, under the TrenchBroom profile at 100% and at 200% text with
// the high-visibility theme, an expanding translator and right-to-left
// layout, and under NetRadiant Custom. Fit Texture Width and Height, their
// Width Only and Height Only forms, and Project Texture Axially, from 2D View
// and from Camera are registered, labelled, in the Surfaces menu, on the Tools
// tab, the Surfaces tab and in the palette, with no keys (as in NetRadiant
// Custom). Each makes the shared service's exact edit as one undo step on the
// selection: the 2D view's projection runs towards its viewer and the
// camera's back towards the camera, and the Surfaces tab says how many
// surfaces kept their mapping. Hidden objects are not targets. The Surface
// Alignment window's Keep proportions box reaches the request.

#include "app/application_shell.h"
#include "app/level_surface_dialog.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_theme.h"
#include "core/studio_semantics.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
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

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only; no system input.
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
	// A package with studio/grid at 128 by 64, which the fits need, and a box
	// of Valve 220 faces.
	QString error;
	LevelMapDocument materials;
	expect(tests::createMaterialFixture(temp.path(), &materials, &error), "material fixture", error);
	const QString assets = temp.filePath(QStringLiteral("assets"));
	const QString path = temp.filePath(QStringLiteral("projection.map"));
	if (!expect(tests::putMaterialFile(path, tests::surfaceFixture(QStringLiteral("valve220"))), "fixture")) {
		return 1;
	}
	const QHash<QString, QSize> sizes {{QStringLiteral("studio/grid"), QSize(128, 64)}};
	const QStringList added {QStringLiteral("map.surfaceFitWidth"), QStringLiteral("map.surfaceFitHeight"), QStringLiteral("map.surfaceProjectAxial"),
		QStringLiteral("map.surfaceProjectView"), QStringLiteral("map.surfaceProjectCamera")};
	const QStringList ids = added + QStringList {QStringLiteral("map.surfaceFitU"), QStringLiteral("map.surfaceFitV")};
	for (const auto& id : ids) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", id);
	}

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"trenchbroom", 100}, Pass {"trenchbroom", 200}, Pass {"netradiant-custom", 100}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Texture projection UI " << qPrintable(phase) << std::endl;
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
			// Read the map as Quake III, so the package's textures/ images resolve.
			for (auto* combo : shell.findChildren<QComboBox*>()) {
				if (combo->findData(QStringLiteral("idtech3")) >= 0 && combo->findData(QStringLiteral("idtech1")) >= 0) {
					combo->setCurrentIndex(combo->findData(QStringLiteral("idtech3")));
				}
			}
			shell.show();
			shell.openPathFromCommandLine(path);
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			if (!expect(until([&] { return shell.levelDocument().sourcePath == path && !shell.levelDocument().brushes.isEmpty(); }), "map ready")) {
				return 1;
			}
			// The package gives the fits their image size.
			shell.openPathFromCommandLine(assets);
			action(QStringLiteral("shell.mode.levels"))->trigger();
			if (!expect(until([&] { return shell.findChildren<MapViewport*>().size() >= 3; }), "plan views")) {
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
			auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
			auto* tools = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>(QStringLiteral("levelSurfaceTools")));
			if (!expect(top && front && camera && tools, "top and front views, camera and Surfaces tab")) {
				return 1;
			}
			const int brushId = shell.levelDocument().brushes.first().id;
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto settle = [&] { return until([&] { return !shell.levelSurfaceEditsPending(); }); };
			const auto select = [&](bool brush) {
				const QVector<LevelMapSelectionRef> refs = brush ? QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakeBrush, brushId}}
																 : QVector<LevelMapSelectionRef> {};
				top->setSelectionSet(refs);
				top->selectionSetChanged(refs);
			};
			// The view a command reads, as a click or focus would make it.
			const auto enter = [&](QWidget* view) {
				QFocusEvent entered(QEvent::FocusIn, Qt::OtherFocusReason);
				QApplication::sendEvent(view, &entered);
			};
			const double scale = levelSurfaceDefaultScale(shell.levelDocument());
			const auto reference = [&](const LevelSurfaceRequest& request) {
				auto expected = shell.levelDocument();
				QVector<LevelSurfaceFace> faces;
				for (int i = 0; i < expected.brushes.first().faces.size(); ++i) {
					faces.append({brushId, i});
				}
				LevelSurfaceEditPlan plan;
				QString problem;
				expect(prepareLevelSurfaceEdits(expected, faces, {}, {request}, sizes, &plan, &problem) && plan.faceCount() > 0
						&& commitLevelSurfaceEdit(&expected, plan, &problem),
					"reference edit", problem);
				return serializeLevelMap(expected).bytes;
			};
			const auto fit = [&](double u, double v, bool keep) {
				LevelSurfaceRequest request;
				request.x = u;
				request.y = v;
				request.keepProportions = keep;
				return reference(request);
			};
			const auto projected = [&](LevelSurfaceOperation operation, const LevelMapVec3& direction) {
				LevelSurfaceRequest request;
				request.operation = operation;
				request.x = request.y = scale;
				request.projectionDirection = direction;
				return reference(request);
			};
			// Runs `edit`, checks the map is then `wanted` with one more undo
			// step, and undoes it back to the exact bytes.
			const auto check = [&](const QByteArray& wanted, const std::function<void()>& edit, const QString& label) {
				const QByteArray start = bytes();
				const auto startDepth = shell.levelDocument().undoStack.size();
				edit();
				const bool settled = settle();
				expect(settled && bytes() == wanted && shell.levelDocument().undoStack.size() == startDepth + 1, "the shared service's edit, as one undo step",
					label + QLatin1Char(' ') + shell.statusBar()->currentMessage());
				action(QStringLiteral("map.undo"))->trigger();
				expect(settle() && bytes() == start && shell.levelDocument().undoStack.size() == startDepth, "undo restores the exact map", label);
			};

			// Registration: menus, the Tools tab, the Surfaces tab, labels,
			// accessibility and no keys.
			select(false);
			for (const auto& id : ids) {
				QAction* command = action(id);
				if (!expect(command != nullptr, "registered", id)) {
					return 1;
				}
				bool menu = false;
				for (auto* owner : command->associatedObjects()) {
					menu |= qobject_cast<QMenu*>(owner) != nullptr;
				}
				expect(!command->text().isEmpty() && !command->statusTip().isEmpty() && menu && !command->isEnabled(),
					"labelled, in a menu and disabled with nothing selected", id);
				expect(command->shortcuts().isEmpty(), "no default key, as in NetRadiant Custom", id);
				auto* button = tools->findChild<QToolButton*>(id);
				expect(button && !button->text().isEmpty() && button->accessibleName() == commandLabelWithoutMnemonic(command->text())
						&& button->focusPolicy() != Qt::NoFocus,
					"a Surfaces tab button named after its command", id);
				if (added.contains(id)) {
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
						"accessible Tools tile", id);
				}
			}
			expect(action(QStringLiteral("map.surfaceFitU"))->text().contains(QStringLiteral("Width Only"))
					&& action(QStringLiteral("map.surfaceFitV"))->text().contains(QStringLiteral("Height Only"))
					&& !action(QStringLiteral("map.surfaceFitWidth"))->text().contains(QStringLiteral("Only")),
				"Fit U and Fit V are the Only forms; Fit Width keeps proportions", action(QStringLiteral("map.surfaceFitU"))->text());

			// The commands on the selected brush.
			select(true);
			for (const auto& id : added) {
				expect(action(id)->isEnabled(), "enabled with a brush selected", id);
			}
			const QByteArray fitWidth = fit(1, 0, true), widthOnly = fit(1, 0, false);
			expect(fitWidth != widthOnly, "Fit Width differs from Width Only on this box");
			check(fitWidth, [&] { action(QStringLiteral("map.surfaceFitWidth"))->trigger(); }, QStringLiteral("Fit Texture Width"));
			check(fit(0, 1, true), [&] { action(QStringLiteral("map.surfaceFitHeight"))->trigger(); }, QStringLiteral("Fit Texture Height"));
			check(widthOnly, [&] { action(QStringLiteral("map.surfaceFitU"))->trigger(); }, QStringLiteral("Fit Texture Width Only"));
			check(fit(0, 1, false), [&] { action(QStringLiteral("map.surfaceFitV"))->trigger(); }, QStringLiteral("Fit Texture Height Only"));
			check(projected(LevelSurfaceOperation::ProjectAxial, {}), [&] { action(QStringLiteral("map.surfaceProjectAxial"))->trigger(); },
				QStringLiteral("Project Texture Axially"));
			// From the Top view, towards its viewer; the walls keep their mapping.
			enter(top);
			check(projected(LevelSurfaceOperation::ProjectDirection, {0, 0, 1, true}), [&] { action(QStringLiteral("map.surfaceProjectView"))->trigger(); },
				QStringLiteral("Project Texture from 2D View, Top"));
			// The Surfaces tab says how many kept their mapping (checked before the undo).
			action(QStringLiteral("map.surfaceProjectView"))->trigger();
			settle();
			auto* status = tools->findChild<QLabel*>(QStringLiteral("surfaceQuickStatus"));
			expect(status && status->text().contains(QLatin1Char('4')) && !status->accessibleDescription().isEmpty(), "the status counts the walls left alone",
				status ? status->text() : QString());
			action(QStringLiteral("map.undo"))->trigger();
			settle();
			// From the Front view, which looks along +Y.
			enter(front);
			check(projected(LevelSurfaceOperation::ProjectDirection, {0, -1, 0, true}), [&] { action(QStringLiteral("map.surfaceProjectView"))->trigger(); },
				QStringLiteral("Project Texture from 2D View, Front"));
			// From the camera, back towards it.
			const ModelVec3 forward = camera->viewForward();
			check(projected(LevelSurfaceOperation::ProjectDirection, {-forward.x, -forward.y, -forward.z, true}),
				[&] { action(QStringLiteral("map.surfaceProjectCamera"))->trigger(); }, QStringLiteral("Project Texture from Camera"));

			// Hidden objects are not targets.
			action(QStringLiteral("map.hideSelection"))->trigger();
			expect(until([&] { return !action(QStringLiteral("map.surfaceProjectAxial"))->isEnabled(); }), "a hidden brush is not a target");
			action(QStringLiteral("map.showAll"))->trigger();
			settle();
			shell.close();
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}

	phase = QStringLiteral("surface alignment window");
	{
		LevelMapDocument document;
		expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &document, &error), "load", error);
		QVector<LevelSurfaceFace> faces;
		for (int i = 0; i < document.brushes.first().faces.size(); ++i) {
			faces.append({document.brushes.first().id, i});
		}
		LevelSurfaceDialog dialog(document, faces);
		auto* keep = dialog.findChild<QCheckBox*>(QStringLiteral("surfaceKeepProportions"));
		auto* operation = dialog.findChild<QComboBox*>(QStringLiteral("surfaceOperation"));
		auto* x = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("surfaceX"));
		auto* y = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("surfaceY"));
		if (expect(keep && operation && x && y, "the window's controls")) {
			expect(!keep->accessibleName().isEmpty() && !keep->toolTip().isEmpty() && keep->focusPolicy() != Qt::NoFocus, "Keep proportions is labelled and takes focus");
			operation->setCurrentIndex(static_cast<int>(LevelSurfaceOperation::Fit));
			x->setValue(2);
			y->setValue(0);
			keep->setChecked(true);
			const auto request = dialog.request();
			expect(keep->isEnabled() && request.operation == LevelSurfaceOperation::Fit && request.keepProportions && request.x == 2 && request.y == 0,
				"a proportional fit request");
			operation->setCurrentIndex(static_cast<int>(LevelSurfaceOperation::Shift));
			expect(!keep->isEnabled() && !dialog.request().keepProportions, "only Fit keeps proportions");
		}
		// The preview runs on a worker; let it finish before the window goes.
		expect(until([&] { return dialog.isReady(); }), "the preview settles");
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Texture projection UI checks passed" : "Texture projection UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
