#include "app/application_shell.h"
#include "app/level_udmf_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_doom_nodes.h"
#include "tests/level_udmf_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>
using namespace vibestudio;
namespace f = vibestudio::tests::udmf;
namespace d = vibestudio::tests::doom;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& detail = {}) {
	if (!value) {
		ok = false;
		std::cerr << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(10, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
}
void capture(QWidget& widget, const QString& name) {
	QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
	QApplication::processEvents();
	const auto path = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (path.isEmpty()) {
		return;
	}
	QDir().mkpath(path);
	QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	widget.render(&image);
	expect(image.save(QDir(path).filePath(name + ".png")), "widget render");
}
void stage(QDialog& dialog, const QString& object, const QString& key, const QString& literal) {
	dialog.findChild<QLineEdit*>("udmfObject")->setText(object);
	dialog.findChild<QPushButton*>("udmfLoad")->click();
	dialog.findChild<QLineEdit*>("udmfKey")->setText(key);
	dialog.findChild<QPlainTextEdit*>("udmfLiteral")->setPlainText(literal);
	dialog.findChild<QPushButton*>("udmfSet")->click();
}
class Expansion final : public QTranslator {
	QString translate(const char*, const char* text, const char*, int) const override {
		const auto s = QString::fromUtf8(text);
		return s + QString(s.size() / 3, '~');
	}
};
} // namespace
int main(int argc, char** argv) {
	// Semantic Qt operations and QWidget::render only; no native input/capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	QString error;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.setRestoreSession(false);
	const auto path = temp.filePath("udmf.wad"), assets = temp.filePath("assets.wad");
	expect(d::write(path, f::fixture()) && d::write(assets, d::wad(d::assets())), "UI fixtures");
	for (int scale : {100, 200}) {
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTextScalePercent(scale);
		settings.setTheme(theme);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		ApplicationShell shell;
		shell.resize(scale == 100 ? 1600 : 2400, scale == 100 ? 1000 : 1600);
		shell.show();
		shell.openPathFromCommandLine(assets);
		shell.openPathFromCommandLine(path);
		if (!expect(until([&] { return shell.levelDocument().doomUdmf != nullptr; }), "shell UDMF load")) {
			return 1;
		}
		auto* plan = shell.findChild<MapViewport*>("mapViewport");
		auto* action = shell.findChild<QAction*>("map.udmfProperties");
		if (!expect(plan && action && action->isEnabled(), "discoverable UDMF action")) {
			return 1;
		}
		plan->selectionSetChanged({{LevelMapSelectionKind::DoomVertex, 0}});
		const auto before = serializeLevelMap(shell.levelDocument()).bytes;
		QTimer::singleShot(0, &shell, [&] {
			auto* dialog = shell.findChild<QDialog*>("levelUdmfDialog");
			if (!expect(dialog, "property dialog opened")) {
				return;
			}
			dialog->resize(scale == 100 ? 800 : 1400, scale == 100 ? 650 : 1050);
			stage(*dialog, "vertex:0", "x", "32.75");
			for (auto* widget : {static_cast<QWidget*>(dialog->findChild<QLineEdit*>("udmfObject")),
								 static_cast<QWidget*>(dialog->findChild<QPlainTextEdit*>("udmfLiteral")),
								 static_cast<QWidget*>(dialog->findChild<QTableView*>("udmfProperties"))}) {
				expect(widget && QAccessible::queryAccessibleInterface(widget) && !widget->accessibleName().isEmpty() &&
						   widget->focusPolicy() != Qt::NoFocus,
					   "standard accessible focus controls");
			}
			capture(*dialog, QStringLiteral("udmf-properties-%1").arg(scale));
			for (auto* button : dialog->findChildren<QPushButton*>()) {
				expect(dialog->rect().contains(QRect(button->mapTo(dialog, QPoint()), button->size())),
					   "scaled dialog controls remain inside its bounds");
			}
			dialog->findChild<QPushButton*>("udmfApply")->click();
			QTimer::singleShot(15000, dialog, [dialog] {
				if (dialog->isVisible()) {
					expect(false, "edit worker failed to close dialog");
					dialog->reject();
				}
			});
		});
		action->trigger();
		expect(shell.levelDocument().doomVertices[0].x == 32.75 && shell.levelDocument().undoStack.size() == 1,
			   "GUI publishes one undo step");
		auto* toggle = shell.findChild<QAction*>("map.toggle3D");
		if (!toggle->isChecked()) {
			toggle->trigger();
		}
		auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
		expect(until([&] { return camera->hasMesh() && camera->hasSkin(); }) && tests::settleModelViewport(*camera),
			   "shared UDMF textured preview");
		camera->setCameraView({140, 64, 64}, 150, -10);
		expect(tests::settleModelViewport(*camera), "settled UDMF camera");
		capture(shell, QStringLiteral("udmf-preview-%1").arg(scale));
		shell.findChild<QAction*>("map.undo")->trigger();
		expect(serializeLevelMap(shell.levelDocument()).bytes == before, "GUI exact undo");
		shell.findChild<QAction*>("map.redo")->trigger();
		const auto saved = temp.filePath(QStringLiteral("saved-%1.wad").arg(scale));
		expect(shell.saveLevelDocument(saved, false, &error), "GUI WAD save", error);
		LevelMapDocument reopened;
		expect(loadLevelMap({saved, "MAP01", {}}, &reopened, &error) && reopened.doomVertices[0].x == 32.75 &&
				   inspectLevelDoomNodes(reopened).needsBuild(),
			   "GUI save reopens with rebuild state", error);
		LevelUdmfDialog cancelled(shell.levelDocument());
		bool applied = false;
		cancelled.setApplyHandler([&](const auto&, QString*) {
			applied = true;
			return true;
		});
		cancelled.show();
		stage(cancelled, "vertex:0", "x", "12.5");
		cancelled.findChild<QPushButton*>("udmfApply")->click();
		cancelled.reject();
		expect(until([&] { return !cancelled.isVisible(); }) && !applied, "cancel acknowledgement before publication");
		LevelUdmfDialog invalid(shell.levelDocument());
		invalid.show();
		stage(invalid, "linedef:0", "v1", "999");
		invalid.findChild<QPushButton*>("udmfApply")->click();
		expect(until([&] { return invalid.findChild<QPushButton*>("udmfApply")->isEnabled(); }) && invalid.isVisible() &&
				   invalid.findChild<QLabel*>("udmfStatus")->text().contains("missing"),
			   "inline invalid reference feedback");
		invalid.reject();
		shell.close();
		if (scale == 200) {
			app.removeTranslator(&expansion);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	return ok ? 0 : 1;
}
