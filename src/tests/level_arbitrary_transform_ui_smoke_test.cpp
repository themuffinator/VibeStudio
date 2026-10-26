// Rotate Selection… and Scale Selection… as NetRadiant Custom's modeless
// Arbitrary Rotation and Arbitrary Scale windows: Apply keeps them open on the
// refreshed selection, OK closes, component mode blocks them, profiles bind
// Shift+R and Ctrl+Shift+S only for NRC, and the controls stay accessible at
// 200% text, high contrast, expanded translations and right to left.
#include "app/application_shell.h"
#include "app/level_rotation_dialog.h"
#include "app/level_scale_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
bool ok = true;
QString phase;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(error) << std::endl;
	}
	return value;
}
bool until(const std::function<bool()>& ready, int milliseconds = 45000)
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
class Expanded final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		const QByteArray name(context);
		if (name != "LevelTransformDialog" && name != "LevelRotationDialog" && name != "LevelScaleDialog") {
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
QByteArray fixture()
{
	LevelMapDocument map;
	QString error;
	expect(loadLevelMapBytes({QStringLiteral("arbitrary.map"), {}, QStringLiteral("idtech2")},
			   "{\n\"classname\" \"worldspawn\"\n\"mapversion\" \"220\"\n}\n", &map, &error)
		&& addLevelMapBoxBrush(&map, {-32, -16, 0, true}, {32, 16, 48, true}, QStringLiteral("studio/checker"), nullptr, &error)
		&& addLevelMapBoxBrush(&map, {64, 0, 0, true}, {96, 32, 32, true}, QStringLiteral("studio/stone"), nullptr, &error),
		"fixture", error);
	return serializeLevelMap(map).bytes;
}
QPushButton* button(QDialog* dialog, const char* name) { return dialog->findChild<QPushButton*>(QLatin1String(name)); }
QString status(QDialog* dialog)
{
	auto* label = dialog->findChild<QLabel*>(QStringLiteral("transformStatus"));
	return label ? label->text() : QString();
}
QDialog* visibleDialog(ApplicationShell& shell, const char* name, int* count = nullptr)
{
	QDialog* found = nullptr;
	int visible = 0;
	for (auto* dialog : shell.findChildren<QDialog*>(QLatin1String(name), Qt::FindDirectChildrenOnly)) {
		if (dialog->isVisible()) {
			found = dialog;
			++visible;
		}
	}
	if (count) {
		*count = visible;
	}
	return found;
}
bool settled(LevelTransformDialog* dialog, const ApplicationShell& shell)
{
	return dialog->isReady() && dialog->sourceDocument().revision == shell.levelDocument().revision
		&& dialog->sourceDocument().selection == shell.levelDocument().selection;
}
// Accessible, keyboard-reachable and left to right in every layout.
void checkFields(QDialog* dialog, const QStringList& names)
{
	for (const auto& name : names) {
		auto* field = dialog->findChild<QDoubleSpinBox*>(name);
		if (!expect(field != nullptr, "value field", name)) {
			continue;
		}
		auto* accessible = QAccessible::queryAccessibleInterface(field);
		expect(accessible && accessible->role() == QAccessible::SpinBox && !field->accessibleName().isEmpty()
			&& !field->accessibleDescription().isEmpty() && field->focusPolicy() != Qt::NoFocus && field->layoutDirection() == Qt::LeftToRight,
			"numeric field accessibility, focus and direction", name);
	}
	expect(!button(dialog, "transformApply")->accessibleName().isEmpty() && !button(dialog, "transformOk")->accessibleName().isEmpty()
		&& !dialog->accessibleName().isEmpty(), "buttons and window are named");
	expect(dialog->findChild<QScrollArea*>()->horizontalScrollBar()->maximum() == 0, "translated controls fit without horizontal clipping");
}
} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and direct API calls; no OS input or capture.
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
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	const auto source = fixture();
	const QString path = temp.filePath(QStringLiteral("arbitrary.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "write fixture")) {
		return EXIT_FAILURE;
	}
	file.close();

	// Doom maps keep the fields their services cannot use off.
	{
		phase = QStringLiteral("Doom fields");
		LevelMapDocument doom;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("doom");
		QString error;
		expect(createLevelMap(create, &doom, &error), "Doom map", error);
		LevelRotationDialog rotation(doom);
		LevelScaleDialog scale(doom);
		expect(!rotation.findChild<QDoubleSpinBox*>(QStringLiteral("rotationAngleX"))->isEnabled()
			&& !rotation.findChild<QDoubleSpinBox*>(QStringLiteral("rotationAngleY"))->isEnabled()
			&& rotation.findChild<QDoubleSpinBox*>(QStringLiteral("rotationAngleZ"))->isEnabled(), "Doom rotation turns about Z only");
		expect(!scale.findChild<QDoubleSpinBox*>(QStringLiteral("scaleFactorZ"))->isEnabled() && scale.request().factors[2] == 1,
			"Doom scale keeps Z at 1");
	}

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Arbitrary transform UI " << profile << ' ' << scale << '%' << std::endl;
			const bool native = QLatin1String(profile) == QLatin1String("netradiant-custom");
			settings.setSelectedEditorProfileId(QLatin1String(profile));
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
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				if (!expect(view && until([&] { return shell.levelDocument().sourcePath == path; }), "map ready")) {
					return EXIT_FAILURE;
				}
				auto* rotate = action("map.rotatePrecisely");
				auto* scaleAction = action("map.scaleSelection");
				if (!expect(rotate && scaleAction, "registered commands")) {
					return EXIT_FAILURE;
				}
				// NRC's keys, and no Save As clash on Ctrl+Shift+S in any profile.
				expect(rotate->shortcuts() == (native ? QList<QKeySequence>{QKeySequence(QStringLiteral("Shift+R"))} : QList<QKeySequence>{}),
					"NRC-only Shift+R", rotate->shortcut().toString());
				expect(scaleAction->shortcuts() == (native ? QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+Shift+S"))} : QList<QKeySequence>{}),
					"NRC-only Ctrl+Shift+S", scaleAction->shortcut().toString());
				QStringList owners;
				for (auto* candidate : shell.findChildren<QAction*>()) {
					if (candidate->objectName().startsWith(QLatin1String("map.")) && candidate->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Shift+S")))) {
						owners << candidate->objectName();
					}
				}
				expect(owners.size() <= 1 && (!native || owners == QStringList{QStringLiteral("map.scaleSelection")}), "one Levels owner of Ctrl+Shift+S",
					owners.join(QLatin1Char(',')));
				expect(!rotate->isEnabled() && !scaleAction->isEnabled(), "commands need a selection");
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) {
					view->setSelectionSet(refs);
					view->selectionSetChanged(refs);
				};
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto baseline = bytes();

				// Rotate: Apply twice keeps the window, OK closes it.
				pick({brush(0), brush(1)});
				expect(rotate->isEnabled() && scaleAction->isEnabled(), "commands enabled with a selection");
				rotate->trigger();
				auto* rotationWindow = static_cast<LevelRotationDialog*>(visibleDialog(shell, "levelRotationDialog"));
				if (!expect(rotationWindow && !QApplication::activeModalWidget() && !rotationWindow->isModal(), "rotation window is modeless")) {
					return EXIT_FAILURE;
				}
				checkFields(rotationWindow, {QStringLiteral("rotationAngleX"), QStringLiteral("rotationAngleY"), QStringLiteral("rotationAngleZ")});
				auto request = rotationWindow->request();
				expect(request.euler && request.eulerDegrees[2] == 45 && request.eulerDegrees[0] == 0, "opens with 45° about the plan view's axis");
				request.eulerDegrees = {0, 0, 90};
				rotationWindow->setRequest(request);
				expect(until([&] { return settled(rotationWindow, shell) && rotationWindow->previewValid(); }), "rotation preview", status(rotationWindow));
				expect(rotationWindow->findChild<ModelViewport*>()->hasMesh() && bytes() == baseline, "preview leaves the map");
				// Profiles that undo selection changes add steps for the picks.
				const auto applied = shell.levelDocument().undoStack.size();
				button(rotationWindow, "transformApply")->click();
				expect(shell.levelDocument().undoStack.size() == applied + 1 && rotationWindow->isVisible() && bytes() != baseline,
					"Apply commits one step and stays open", shell.statusBar()->currentMessage() + QLatin1Char(' ') + status(rotationWindow));
				expect(until([&] { return settled(rotationWindow, shell) && rotationWindow->previewValid(); }), "preview follows the edited map",
					status(rotationWindow));
				const auto once = bytes();
				button(rotationWindow, "transformApply")->click();
				expect(shell.levelDocument().undoStack.size() == applied + 2 && bytes() != once, "second Apply turns the refreshed selection again",
					status(rotationWindow));
				expect(until([&] { return settled(rotationWindow, shell) && rotationWindow->previewValid(); }), "preview after second Apply");
				button(rotationWindow, "transformOk")->click();
				expect(shell.levelDocument().undoStack.size() == applied + 3 && !rotationWindow->isVisible(), "OK applies and closes");
				for (int i = 0; i < 3; ++i) {
					action("map.undo")->trigger();
				}
				expect(bytes() == baseline, "three undo steps restore the map");

				// One window at a time; it follows the selection.
				rotate->trigger();
				rotate->trigger();
				int open = 0;
				rotationWindow = static_cast<LevelRotationDialog*>(visibleDialog(shell, "levelRotationDialog", &open));
				expect(rotationWindow && open == 1, "the command raises the open window instead of a second one");
				expect(until([&] { return settled(rotationWindow, shell) && rotationWindow->previewValid(); }), "reopened preview");
				pick({});
				expect(until([&] { return settled(rotationWindow, shell); }) && !rotationWindow->previewValid()
					&& !button(rotationWindow, "transformApply")->isEnabled() && !button(rotationWindow, "transformOk")->isEnabled(),
					"an empty selection disables Apply", status(rotationWindow));
				pick({brush(0)});
				expect(until([&] { return settled(rotationWindow, shell) && rotationWindow->previewValid(); }), "a new selection rebuilds the preview");
				if (native) {
					// In component mode the window turns the picked components.
					action("map.vertexTool")->trigger();
					expect(until([&] { return status(rotationWindow).contains(QStringLiteral("component")); })
						&& !button(rotationWindow, "transformApply")->isEnabled() && !rotationWindow->apply(), "component mode without picks waits",
						status(rotationWindow));
					QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> handles;
					for (const auto& handle : view->componentHandles()) {
						if (handle.object.kind == LevelMapSelectionKind::QuakeBrush && handle.object.objectId == 0) {
							handles.append({handle.object, handle.position});
						}
					}
					expect(handles.size() == 8, "brush corners to pick");
					view->setPickedComponentPositions(handles);
					view->componentPicksChanged(static_cast<int>(handles.size()));
					request = rotationWindow->request();
					request.eulerDegrees = {0, 0, 90};
					request.pivot = {};
					rotationWindow->setRequest(request);
					expect(until([&] { return rotationWindow->isReady() && rotationWindow->previewValid()
						&& status(rotationWindow).contains(QStringLiteral("picked components")); }), "component preview", status(rotationWindow));
					const auto componentDepth = shell.levelDocument().undoStack.size();
					button(rotationWindow, "transformApply")->click();
					const auto& turned = shell.levelDocument().brushes[0];
					expect(shell.levelDocument().undoStack.size() == componentDepth + 1 && std::abs(turned.mins.x + 16) < 1e-6
						&& std::abs(turned.maxs.y - 32) < 1e-6 && rotationWindow->isVisible(), "Apply turns the picked corners and stays open",
						shell.statusBar()->currentMessage());
					action("map.undo")->trigger();
					expect(bytes() == baseline, "component undo");
					action("map.vertexTool")->trigger();
					if (view->componentMode() != MapViewport::ComponentMode::Off) {
						action("map.selectNone")->trigger();
					}
					pick({brush(0)});
					expect(until([&] { return settled(rotationWindow, shell) && rotationWindow->previewValid(); }), "leaving component mode unblocks",
						status(rotationWindow));
				}
				rotationWindow->reject();
				expect(!rotationWindow->isVisible() && bytes() == baseline, "Close changes nothing");

				// Scale: a negative factor mirrors; Apply keeps the window.
				pick({brush(1)});
				scaleAction->trigger();
				auto* scaleWindow = static_cast<LevelScaleDialog*>(visibleDialog(shell, "levelScaleDialog"));
				if (!expect(scaleWindow && !QApplication::activeModalWidget(), "scale window is modeless")) {
					return EXIT_FAILURE;
				}
				checkFields(scaleWindow, {QStringLiteral("scaleFactorX"), QStringLiteral("scaleFactorY"), QStringLiteral("scaleFactorZ")});
				auto factors = scaleWindow->request();
				factors.factors = {0, 1, 1};
				scaleWindow->setRequest(factors);
				expect(until([&] { return settled(scaleWindow, shell); }) && !scaleWindow->previewValid() && !button(scaleWindow, "transformOk")->isEnabled(),
					"a zero factor cannot apply", status(scaleWindow));
				factors.factors = {-1, 2, 1};
				factors.textures = {true, false};
				scaleWindow->setRequest(factors);
				expect(until([&] { return settled(scaleWindow, shell) && scaleWindow->previewValid(); }) && status(scaleWindow).contains(QStringLiteral("mirror")),
					"mirror preview", status(scaleWindow));
				const auto scaleDepth = shell.levelDocument().undoStack.size();
				button(scaleWindow, "transformApply")->click();
				const auto& scaled = shell.levelDocument().brushes[1];
				expect(scaleWindow->isVisible() && std::abs(scaled.mins.y + 16) < 1e-6 && std::abs(scaled.maxs.y - 48) < 1e-6
					&& std::abs(scaled.mins.x - 64) < 1e-6 && shell.levelDocument().undoStack.size() == scaleDepth + 1, "Apply scales and mirrors",
					QStringLiteral("%1 .. %2, %3 .. %4: ").arg(scaled.mins.x).arg(scaled.maxs.x).arg(scaled.mins.y).arg(scaled.maxs.y)
						+ shell.statusBar()->currentMessage() + QLatin1Char(' ') + status(scaleWindow));
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!captures.isEmpty() && until([&] { return settled(scaleWindow, shell); })) {
					QDir().mkpath(captures);
					QImage image(scaleWindow->size(), QImage::Format_ARGB32_Premultiplied);
					image.fill(Qt::transparent);
					scaleWindow->render(&image);
					expect(image.save(QDir(captures).filePath(QStringLiteral("arbitrary-scale-%1-%2.png").arg(QLatin1String(profile)).arg(scale))),
						"capture");
				}
				scaleWindow->reject();
				action("map.undo")->trigger();
				expect(bytes() == baseline, "scale undo");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	if (ok) {
		std::cout << "Arbitrary rotation and scale UI checks passed." << std::endl;
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
