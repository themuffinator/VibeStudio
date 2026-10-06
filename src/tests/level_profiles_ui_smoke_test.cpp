#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/level_gestures.h"
#include "tests/level_material_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QProcess>
#include <QShortcut>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <iostream>
#include <cmath>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* label, const QString& detail = {})
{
	if (!condition) { std::cerr << label << ": " << detail.toStdString() << '\n'; }
	return condition;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop; QTimer::singleShot(15, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
QString canonical(QString text) { return text.toLower().remove('-').remove('_'); }
QAction* command(ApplicationShell& shell, const QString& id)
{
	for (auto* action : shell.findChildren<QAction*>()) {
		if (canonical(action->objectName()) == canonical(id)) { return action; }
	}
	return nullptr;
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
}

int main(int argc, char** argv)
{
	// Direct Qt services and widget rendering only: no input injection,
	// desktop capture, game launches or real settings/installation writes.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	// The offscreen plugin otherwise chooses the first font in the directory,
	// which is not the normal Windows UI font and hides text-width problems.
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setReducedMotion(true); settings.setRestoreSession(false); settings.sync();
	LevelMapDocument fixture;
	QString error;
	bool ok = expect(tests::createMaterialFixture(temp.path(), &fixture, &error), "fixture", error);
	const auto mapPath = temp.filePath(QStringLiteral("profile.map"));
	ok &= expect(tests::putMaterialFile(mapPath, serializeLevelMap(fixture).bytes), "fixture persisted");
	const auto profiles = editorProfileDescriptors();
	// Lifecycle checks call semantic view methods, never input handlers. The
	// offscreen backend deliberately never reads or warps a desktop pointer.
	{
		MapViewport plan;
		plan.setControls(familiarLevelControls("sledge").plan); plan.show();
		plan.setTemporaryPan(true, {100, 100});
		ok &= expect(plan.isTemporarilyPanning() && plan.accessibleDescription().contains("Temporary pan"), "held pan starts with accessible state");
		plan.setTemporaryPan(false);
		ok &= expect(!plan.isTemporarilyPanning(), "held pan releases");
		plan.activateWindow(); app.processEvents(); plan.setFocus();
		ok &= expect(plan.hasFocus(), "held-pan focus test starts focused");
		plan.setTemporaryPan(true); plan.clearFocus();
		ok &= expect(!plan.isTemporarilyPanning(), "focus loss clears held pan");
		plan.setTemporaryPan(true); plan.setEnabled(false);
		ok &= expect(!plan.isTemporarilyPanning(), "disabled view clears held pan");
		plan.setEnabled(true); plan.setTemporaryPan(true); plan.hide();
		ok &= expect(!plan.isTemporarilyPanning(), "hidden view clears held pan");
		plan.show(); plan.setTemporaryPan(true); plan.setControls(gtkRadiantLevelControls().plan);
		ok &= expect(!plan.isTemporarilyPanning(), "profile change clears held pan");
		ModelViewport camera;
		camera.setCameraControls(familiarLevelControls("sledge").camera); camera.show();
		camera.setTemporaryLooking(true);
		ok &= expect(camera.isLooking() && camera.isTemporarilyLooking() && camera.accessibleDescription().contains("Temporary mouse look"), "hold activates mouse look with accessible state");
		camera.setTemporaryLooking(true); camera.setTemporaryLooking(false);
		ok &= expect(!camera.isLooking() && !camera.isTemporarilyLooking(), "repeat hold preserves the original idle state");
		camera.setLooking(true); camera.setTemporaryLooking(true); camera.setTemporaryLooking(false);
		ok &= expect(camera.isLooking() && !camera.isTemporarilyLooking(), "hold release restores an existing toggle");
		camera.setTemporaryLooking(true); camera.setLooking(false); camera.setTemporaryLooking(false);
		ok &= expect(!camera.isLooking(), "cancellation cannot reactivate look on later key release");
		camera.activateWindow(); app.processEvents(); camera.setFocus();
		ok &= expect(camera.hasFocus(), "held-look focus test starts focused");
		camera.setTemporaryLooking(true); camera.clearFocus();
		ok &= expect(!camera.isLooking() && !camera.isTemporarilyLooking(), "focus loss clears held look");
		camera.setTemporaryLooking(true); camera.setEnabled(false);
		ok &= expect(!camera.isLooking() && !camera.isTemporarilyLooking(), "disabled camera clears held look");
		camera.setEnabled(true); camera.setTemporaryLooking(true); camera.hide();
		ok &= expect(!camera.isLooking() && !camera.isTemporarilyLooking(), "hidden camera clears held look");
		camera.show(); camera.setTemporaryLooking(true); camera.setCameraControls(gtkRadiantLevelControls().camera);
		ok &= expect(!camera.isLooking() && !camera.isTemporarilyLooking(), "profile change clears held look");
	}
	if (argc > 1) {
		const auto binary = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
		auto cli = [&](const QStringList& args, int expected) {
			QProcess process; process.setWorkingDirectory(temp.path());
			process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), temp.filePath(QStringLiteral("cli.ini")), QStringLiteral("editor")} + args + QStringList{QStringLiteral("--json")});
			const bool finished = process.waitForFinished(30000);
			const auto output = process.readAllStandardOutput();
			ok &= expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "profile CLI", QString::fromUtf8(output + process.readAllStandardError()));
			return QJsonDocument::fromJson(output).object();
		};
		const auto selected = cli({QStringLiteral("select"), QStringLiteral("Hammer++")}, 0).value(QStringLiteral("profile")).toObject();
		ok &= expect(selected.value(QStringLiteral("id")) == QStringLiteral("hammer") && !selected.value(QStringLiteral("adaptations")).toArray().isEmpty(), "CLI alias canonicalization and differences");
		cli({QStringLiteral("select"), QStringLiteral("unknown-editor")}, 2);
		ok &= expect(cli({QStringLiteral("current")}, 0).value(QStringLiteral("profile")).toObject().value(QStringLiteral("id")) == QStringLiteral("hammer"), "invalid profile preserves settings");
		ok &= expect(cli({"select", "sledge-editor"}, 0).value("profile").toObject().value("id") == "sledge"
			&& cli({"select", "xonotic-netradiant"}, 0).value("profile").toObject().value("id") == "netradiant", "new profile aliases persist independently through actual CLI");
		ok &= expect(cli({"select", "quake-iii-radiant"}, 0).value("profile").toObject().value("id") == "q3radiant", "classic Radiant alias selects its own profile");
		for (const auto& profile : profiles) {
			const auto controls = cli({QStringLiteral("controls"), profile.id}, 0).value(QStringLiteral("controls")).toObject();
			ok &= expect(controls.value(QStringLiteral("layout")) == levelViewLayoutId(profile.controls.layout) && controls.value(QStringLiteral("problems")).toArray().isEmpty(), "CLI controls match registry", profile.id);
		}
	}
	for (const int scale : {100, 200}) {
		settings.setSelectedEditorProfileId(QStringLiteral("vibestudio-default"));
		settings.setLevelViewLayoutPreference(QStringLiteral("profile"));
		settings.setTextScalePercent(scale); settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) { app.installTranslator(&expansion); }
		{
			ApplicationShell shell;
			if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
			shell.resize(scale == 100 ? 1700 : 2600, scale == 100 ? 1100 : 1700); shell.show();
			shell.openPathFromCommandLine(mapPath);
			ok &= expect(until([&] { return shell.levelDocument().brushes.size() == fixture.brushes.size(); }), "map opens");
			command(shell, QStringLiteral("shell.mode.levels"))->trigger();
			auto* combo = shell.findChild<QComboBox*>(QStringLiteral("editorProfileCombo"));
			auto* plan = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
			auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
			auto* side = shell.findChild<MapViewport*>(QStringLiteral("mapViewport2"));
			auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
			if (!expect(combo && plan && front && side && camera, "profile widgets available")) { return EXIT_FAILURE; }
			command(shell, QStringLiteral("map.selectAll"))->trigger();
			const auto bytes = serializeLevelMap(shell.levelDocument()).bytes;
			const auto selection = shell.levelDocument().selection;
			ok &= expect(!selection.isEmpty(), "profile switches preserve a populated selection");
			QElapsedTimer switching; switching.start();
			for (const auto& profile : profiles) {
				const auto index = combo->findData(profile.id);
				ok &= expect(index >= 0, "profile exposed in Settings", profile.id);
				combo->setCurrentIndex(index);
				auto live = profile.controls; live.camera = camera->cameraControls(); live.plan = plan->controls();
				ok &= expect(levelGestureValues(live) == levelGestureValues(profile.controls), "viewport adopts all navigation preferences", profile.id);
				ok &= expect(StudioSettings().selectedEditorProfileId() == profile.id, "profile persisted", profile.id);
				const bool four = profile.controls.layout == LevelViewLayout::FourViews;
				ok &= expect(front->isVisible() == four && side->isVisible() == four, "familiar layout adopted", profile.id);
				ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().selection == selection, "switch preserves map and selection", profile.id);
				for (const auto& binding : profile.bindings) {
					if (!binding.implemented) { continue; }
					const auto* action = command(shell, binding.commandId);
					ok &= expect(action, "binding has a live command", profile.id + QLatin1Char(':') + binding.commandId);
					if (!action) { continue; }
					for (const auto& key : editorProfileBindingKeys(binding)) {
						ok &= expect(action->shortcuts().contains(QKeySequence::fromString(key, QKeySequence::PortableText)), "key actually installed", profile.id + QLatin1Char(':') + binding.commandId + QLatin1Char(':') + key);
					}
					if (binding.clearsKeys) { ok &= expect(action->shortcuts().isEmpty(), "reserved key released", binding.commandId); }
					if (binding.commandId == QLatin1String("map.cycleView") || binding.commandId == QLatin1String("map.nextProjection")
						|| binding.commandId == QLatin1String("map.toggle3D") || binding.commandId == QLatin1String("map.maximizeView")
						|| binding.commandId == QLatin1String("map.equalizeViews")) {
						for (auto* owner : action->associatedObjects()) {
							auto* widget = qobject_cast<QWidget*>(owner);
							ok &= expect(!widget || widget == plan || !widget->isAncestorOf(plan), "view keys leave page controls outside their shortcut scope", profile.id);
						}
					}
					if (binding.commandId.startsWith(QLatin1String("map."))) {
						for (auto* local : plan->findChildren<QShortcut*>()) {
							if (action->shortcuts().contains(local->key())) { ok &= expect(!local->isEnabled(), "local cycling yields to profile action", profile.id); }
						}
					}
				}
			}
			std::cout << profiles.size() << " live profile switches at " << scale << "%: " << switching.elapsed() << " ms\n";
			combo->setCurrentIndex(combo->findData(QStringLiteral("hammer")));
			command(shell, QStringLiteral("map.layout.single-2d"))->trigger();
			combo->setCurrentIndex(combo->findData(QStringLiteral("godot")));
			ok &= expect(!camera->isVisible() && plan->isVisible(), "layout override survives profile switch");
			command(shell, QStringLiteral("map.layout.profile"))->trigger();
			ok &= expect(camera->isVisible() && !plan->isVisible(), "follow-profile restores scene layout");
			combo->setCurrentIndex(combo->findData(QStringLiteral("q3radiant")));
			ok &= expect(until([&] { return camera->isVisible() && camera->isEnabled() && camera->hasMesh(); }), "classic camera is ready");
			camera->setCameraView({0, 0, 64}, 0, 60);
			const auto classicStep = planCameraStep(plan->controls(), Qt::Key_Up, Qt::NoModifier, plan->gridSize());
			plan->cameraDriveRequested(classicStep.x(), classicStep.y());
			ok &= expect(std::abs(camera->cameraPosition().x - 32) < 0.001 && std::abs(camera->cameraPosition().z - 64) < 0.001,
				"classic plan camera handoff preserves fixed distance and elevation");
			ok &= expect(camera->beginPointerDrive(QPointF(camera->width() / 2.0, camera->height() / 2.0)), "classic shell camera can steer");
			camera->advancePointerDrive(0.05); camera->endPointerDrive();
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().selection == selection,
				"classic navigation preserves the shared authoring document and selection");
			// Preference application restores the configured locale's direction;
			// set the test direction after the profile loop, then check the dialog.
			if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
			command(shell, QStringLiteral("levelControlsReference"))->trigger();
			auto* dialog = shell.findChild<QDialog*>(QStringLiteral("levelControlsDialog"));
			auto* tree = shell.findChild<QTreeWidget*>(QStringLiteral("levelControlsTree"));
			auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("levelControlsFilter"));
			ok &= expect(dialog && tree && filter && QAccessible::queryAccessibleInterface(tree), "accessible control reference");
			if (dialog && tree && filter) {
				if (scale == 200) { dialog->setLayoutDirection(Qt::RightToLeft); }
				ok &= expect(tree->layoutDirection() == (scale == 200 ? Qt::RightToLeft : Qt::LeftToRight), "reference layout direction");
				filter->setText(QStringLiteral("Z-checker"));
				ok &= expect(!tree->topLevelItem(0)->isHidden(), "adaptations searchable");
				filter->clear(); dialog->resize(scale == 100 ? 760 : 1200, scale == 100 ? 720 : 1100); app.processEvents(QEventLoop::ExcludeUserInputEvents);
				const auto noteRect = tree->visualItemRect(tree->topLevelItem(0)->child(0));
				ok &= expect(noteRect.height() > tree->fontMetrics().height() * 2, "long adaptations wrap to readable rows");
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!captures.isEmpty()) {
					QDir().mkpath(captures);
					QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("profile-controls-%1.png").arg(scale))), "reference rendered");
				}
				dialog->close();
			}
			shell.close();
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
