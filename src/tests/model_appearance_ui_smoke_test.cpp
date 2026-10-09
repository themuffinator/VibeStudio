#include "app/application_shell.h"
#include "app/asset_views.h"
#include "app/model_editor_dialog.h"
#include "app/model_skin_source_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/model_fingerprint.h"
#include "core/studio_settings.h"
#include "tests/model_appearance_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QListWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRawFont>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0;
bool expect(bool value, const char* message) {
	++checks; if (!value) { std::cerr << "FAIL: " << message << '\n'; } return value;
}
bool waitFor(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 15000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
	return ready();
}
bool capture(QWidget& widget, const QString& name) {
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty()) { return true; }
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF()); image.fill(Qt::transparent);
	widget.render(&image); // Owned render target; no OS input or screen capture.
	return image.save(QDir(directory).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
class Expansion final : public QTranslator
{
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* text, const char*, int) const override {
		if (QByteArray(context) != "ApplicationShell" && QByteArray(context) != "vibestudio::ApplicationShell" &&
			QByteArray(context) != "VibeStudioModelSkinSource" && QByteArray(context) != "VibeStudioModelAppearance") { return {}; }
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool accessible(QWidget* widget) {
	auto* interface = widget ? QAccessible::queryAccessibleInterface(widget) : nullptr;
	return widget && interface && !interface->text(QAccessible::Name).isEmpty() &&
		!widget->accessibleDescription().isEmpty() && widget->focusPolicy() != Qt::NoFocus;
}
}
int main(int argc, char** argv)
{
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-appearance-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root)) { return 1; }
	QTemporaryDir temporary(QDir(root).filePath("model-appearance-ui-XXXXXX"));
	if (!temporary.isValid()) { return 1; }
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
	QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	ModelAppearanceFixture fixture;
	QString error;
	bool ok = expect(fixture.create(temporary.filePath("fixture"), &error), "create independent browser fixtures");
	if (!ok) { std::cerr << error.toStdString() << '\n'; return 1; }
	ok &= expect(QRawFont::fromFont(app.font()).supportsCharacter('M'), "offscreen font has real glyphs");
	for (int scenario = 0; scenario < 3; ++scenario) {
		Expansion expansion;
		if (scenario) { app.installTranslator(&expansion); }
		StudioSettings settings;
		auto preferences = settings.accessibilityPreferences();
		preferences.theme = scenario == 0 ? StudioTheme::Dark : scenario == 1 ? StudioTheme::HighContrastLight : StudioTheme::HighContrastDark;
		preferences.textScalePercent = scenario ? 200 : 100;
		preferences.reducedMotion = true;
		settings.setAccessibilityPreferences(preferences);
		applyStudioTheme(app, studioThemeTokens(preferences.theme, preferences.density, preferences.textScalePercent));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		{
			ApplicationShell shell;
			shell.resize(scenario ? 1800 : 1450, 1080); shell.show();
			shell.openPathFromCommandLine(fixture.path("assets"));
			auto* mode = shell.findChild<QAction*>("shell.mode.models");
			if (mode) { mode->trigger(); }
			auto* entries = shell.findChild<QListWidget*>("modelEntries");
			auto* viewport = shell.findChild<ModelViewport*>("modelViewport");
			auto* panel = shell.findChild<QScrollArea*>("modelAppearancePanel");
			auto* surface = shell.findChild<QComboBox*>("modelAppearanceSurface");
			auto* slot = shell.findChild<QComboBox*>("modelAppearanceSlot");
			auto* skin = shell.findChild<QComboBox*>("modelAppearanceMdlSkin");
			auto* member = shell.findChild<QComboBox*>("modelAppearanceMdlMember");
			auto* choose = shell.findChild<QPushButton*>("modelAppearanceChooseSkin");
			auto* reset = shell.findChild<QPushButton*>("modelAppearanceReset");
			auto* details = shell.findChild<QPlainTextEdit*>("modelAppearanceDetails");
			auto* image = panel ? panel->findChild<ImagePreviewView*>() : nullptr;
			QListWidgetItem *md3 = nullptr, *md2 = nullptr, *mdl = nullptr;
			if (entries) {
				for (int row = 0; row < entries->count(); ++row) {
					auto* item = entries->item(row);
					const auto path = item->data(Qt::UserRole).toString();
					if (path == "models/test.md3") { md3 = item; }
					if (path == "models/test.md2") { md2 = item; }
					if (path == "models/test.mdl") { mdl = item; }
				}
			}
			ok &= expect(mode && entries && viewport && panel && image && md3 && md2 && mdl && details && choose && reset &&
				surface && slot && skin && member, "real browser exposes appearance inspector for every native model family");
			if (!mode || !entries || !viewport || !panel || !image || !md3 || !md2 || !mdl || !details || !choose || !reset ||
				!surface || !slot || !skin || !member) { return 1; }
			const auto selectModel = [&](const QString& path) {
				// Package refreshes may replace list items while a modal editor services events.
				for (int row = 0; row < entries->count(); ++row) {
					if (entries->item(row)->data(Qt::UserRole).toString() == path) {
						entries->setCurrentRow(row); return true;
					}
				}
				return false;
			};
			for (auto* tabs : shell.findChildren<QTabWidget*>()) {
				if (tabs->indexOf(panel) >= 0) { tabs->setCurrentWidget(panel); }
			}
			selectModel("models/test.md3");
			ok &= expect(waitFor([&] { return viewport->mesh().sourcePath == "models/test.md3" && slot->isEnabled(); }), "MD3 model and primary materials finish loading");
			for (QWidget* control : QList<QWidget*>{surface, slot, skin, member, choose, reset, details}) {
				ok &= expect(accessible(control), "appearance controls expose names, descriptions and keyboard focus policies");
			}
			ok &= expect(image->image().pixelColor(0, 0) == QColor(Qt::red) && slot->count() == 2 && !skin->isVisible(),
				"external model starts with its primary surface texture");
			viewport->setFrame(1); viewport->zoomIn(); viewport->turnCamera(17, -4);
			const auto yaw = viewport->cameraYaw(), pitch = viewport->cameraPitch(), zoom = viewport->zoom();
			const auto original = modelStateFingerprint(viewport->mesh());
			slot->setCurrentIndex(1);
			ok &= expect(waitFor([&] { return slot->isEnabled() && image->image().pixelColor(0, 0) == QColor(Qt::blue); }), "material slot selection loads selected texture");
			ok &= expect(viewport->frame() == 1 && viewport->zoom() == zoom && viewport->cameraYaw() == yaw && viewport->cameraPitch() == pitch &&
				modelStateFingerprint(viewport->mesh()) == original, "appearance changes preserve frame, camera, authored bindings and geometry");
			ok &= expect(settleModelViewport(*viewport), "selected appearance renders asynchronously");
			panel->verticalScrollBar()->setValue(0);
			if (panel->horizontalScrollBar()->maximum() != 0) {
				std::cerr << "Appearance width " << panel->viewport()->width() << " / " << panel->widget()->width() << '\n';
				for (QWidget* control : QList<QWidget*>{surface, slot, choose, reset, details}) {
					std::cerr << control->objectName().toStdString() << " minimum " << control->minimumSizeHint().width() << '\n';
				}
			}
			ok &= expect(panel->horizontalScrollBar()->maximum() == 0, "appearance form fits the inspector width at expanded text scales");
			ok &= expect(surface->layoutDirection() == Qt::LeftToRight && slot->layoutDirection() == Qt::LeftToRight &&
				details->layoutDirection() == Qt::LeftToRight, "technical preview paths retain logical left-to-right order inside RTL forms");
			for (auto* label : panel->findChildren<QLabel*>()) {
				if (label->isVisible() && label->buddy()) {
					ok &= expect(label->height() >= label->heightForWidth(label->width()), "wrapped appearance labels retain every line at the available width");
				}
			}
			ok &= expect(capture(*panel, QStringLiteral("appearance-%1-slots").arg(scenario)), "retain owned material-slot controls render");
			surface->setCurrentIndex(1);
			ok &= expect(image->image().pixelColor(0, 0) == QColor(Qt::green) && slot->count() == 1,
				"surface selection shows its own material without resetting another surface's choice");
			surface->setCurrentIndex(0);
			ok &= expect(slot->currentIndex() == 1 && image->image().pixelColor(0, 0) == QColor(Qt::blue), "surface preview choice persists across surface navigation");
			bool picked = false, pickerCaptured = false;
			QTimer::singleShot(0, &shell, [&] {
				auto* picker = dynamic_cast<ModelSkinSourceDialog*>(shell.findChild<QDialog*>("meshSkinSourceDialog"));
				if (!picker) { return; }
				auto* table = picker->findChild<QTableView*>("meshSkinSourceEntries");
				auto* accept = picker->findChild<QPushButton*>("meshSkinSourceImport");
				QCoreApplication::processEvents();
				if (table && accept) {
					for (int row = 0; row < table->model()->rowCount(); ++row) {
						if (table->model()->index(row, 1).data().toString() == "models/test.skin") {
							table->selectionModel()->setCurrentIndex(table->model()->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
							picked = accept->isEnabled(); break;
						}
					}
					pickerCaptured = capture(*picker, QStringLiteral("appearance-%1-picker").arg(scenario));
				}
				if (picked) { accept->click(); } else { picker->reject(); }
			});
			choose->click();
			ok &= expect(picked && pickerCaptured && waitFor([&] { return choose->isEnabled() && image->image().pixelColor(0, 0) == QColor(Qt::cyan); }),
				"package picker applies exact native surface bindings through the worker");
			ok &= expect(!slot->isEnabled() && details->toPlainText().contains("SHA-256") && details->toPlainText().contains("models/test.skin"),
				"skin mode exposes input identity and disables competing slot mode");
			auto* edit = shell.findChild<QPushButton*>("openModelEditor");
			if (edit) { edit->click(); }
			auto* editor = dynamic_cast<ModelEditorDialog*>(shell.findChild<QDialog*>("modelEditorDialog"));
			ok &= expect(editor && modelStateFingerprint(editor->document().mesh()) == original,
				"opening the Mesh Editor retains original bindings instead of applying preview-only skin assignments");
			delete editor; // Owned synthetic document; no user settings or input devices.
			panel->verticalScrollBar()->setValue(0);
			ok &= expect(capture(*panel, QStringLiteral("appearance-%1-skin").arg(scenario)), "retain selected skin receipt render");
			reset->click();
			ok &= expect(waitFor([&] { return slot->isEnabled() && image->image().pixelColor(0, 0) == QColor(Qt::red); }) &&
				!details->toPlainText().contains("SHA-256"), "reset restores primary textures and clears stale input receipt");
			bool stalePicked = false;
			QTimer::singleShot(0, &shell, [&] {
				auto* picker = dynamic_cast<ModelSkinSourceDialog*>(shell.findChild<QDialog*>("meshSkinSourceDialog"));
				if (!picker) { return; }
				auto* table = picker->findChild<QTableView*>("meshSkinSourceEntries");
				auto* accept = picker->findChild<QPushButton*>("meshSkinSourceImport");
				if (table && accept) {
					for (int row = 0; row < table->model()->rowCount(); ++row) {
						if (table->model()->index(row, 1).data().toString() == "models/test.skin") {
							table->selectionModel()->setCurrentIndex(table->model()->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
							stalePicked = accept->isEnabled(); break;
						}
					}
				}
				selectModel("models/test.mdl");
				if (stalePicked) { accept->click(); } else { picker->reject(); }
			});
			choose->click();
			ok &= expect(stalePicked && waitFor([&] { return skin->isEnabled() && viewport->mesh().sourcePath == "models/test.mdl"; }) &&
				!details->toPlainText().contains("SHA-256"), "a model change while choosing a package skin rejects the stale picker result");
			selectModel("models/test.md2");
			ok &= expect(waitFor([&] { return viewport->mesh().sourcePath == "models/test.md2" && slot->isEnabled(); }), "MD2 slot metadata is available in the same inspector");
			slot->setCurrentIndex(1);
			ok &= expect(waitFor([&] { return slot->isEnabled() && image->image().pixelColor(0, 0) == QColor(Qt::blue); }), "MD2 alternate skin preview uses ordered native slots");
			selectModel("models/test.mdl");
			ok &= expect(waitFor([&] { return viewport->mesh().sourcePath == "models/test.mdl" && skin->isEnabled(); }) && skin->count() == 2 && member->count() == 2 && !slot->isVisible(),
				"MDL inspector exposes native skin slots and group members");
			ok &= expect(image->image().pixel(0, 0) == fixture.palette.colors[0], "browser uses actual project MDL palette");
			member->setCurrentIndex(1);
			ok &= expect(waitFor([&] { return member->isEnabled() && image->image().pixel(0, 0) == fixture.palette.colors[1]; }), "group member preview retains original palette indices");
			panel->verticalScrollBar()->setValue(0);
			ok &= expect(capture(*panel, QStringLiteral("appearance-%1-mdl").arg(scenario)), "retain embedded skin and member controls render");
			skin->setCurrentIndex(1);
			ok &= expect(waitFor([&] { return skin->isEnabled() && member->count() == 1 && image->image().pixel(0, 0) == fixture.palette.colors[16]; }) &&
				member->currentIndex() == 0, "changing native skin clamps member choice before submitting work");
			selectModel("models/test.md3");
			ok &= expect(waitFor([&] { return slot->isEnabled() && viewport->mesh().sourcePath == "models/test.md3"; }) && slot->currentIndex() == 0,
				"switching model clears appearance options from the previous source");
			slot->setCurrentIndex(1);
			auto* cancel = shell.findChild<QPushButton*>("cancelModelPreview");
			if (cancel) { cancel->click(); }
			ok &= expect(cancel && waitFor([&] { return reset->isEnabled(); }), "cancelled appearance eventually re-enables inspector controls");
			reset->click();
			ok &= expect(waitFor([&] { return slot->isEnabled() && image->image().pixelColor(0, 0) == QColor(Qt::red); }), "appearance can be retried after cancellation");
			ok &= expect(fixture.unchanged(), "browser preview choices never rewrite package or model sources");
			shell.close();
		}
		if (scenario) { app.removeTranslator(&expansion); }
	}
	std::cout << checks << " model appearance GUI checks\n";
	return ok ? 0 : 1;
}
