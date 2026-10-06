#include "app/game_launch_task_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/level_doom_nodes_test_helpers.h"
#include <QAccessible>
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace d = vibestudio::tests::doom;
namespace f = vibestudio::tests::doomNodes;
namespace {
bool ok = true;
void expect(bool value, const char* message, const QString& detail = {}) {
	if (!value) {
		ok = false;
		std::cerr << message << ": " << detail.toStdString() << '\n';
	}
}
class Expansion final : public QTranslator {
  public:
	QString translate(const char*, const char* text, const char*, int) const override {
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(source, QString(source.size() / 3, '~'));
	}
};
} // namespace
int main(int argc, char** argv) {
	// Semantic Qt calls and QWidget::render; no native input or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	auto lumps = d::lumps(f::fixture());
	lumps << d::Lump{"RESOURCE", QByteArray(48 * 1024 * 1024, 'x')};
	const auto path = temp.filePath("large.wad");
	expect(d::write(path, d::wad(lumps)), "large snapshot fixture");
	GameInstallationProfile installation;
	installation.gameKey = "doom";
	installation.engineFamily = GameEngineFamily::IdTech1;
	installation.executablePath = app.applicationFilePath();
	installation.rootPath = temp.path();
	GameLaunchRequest request;
	request.bspPath = path;
	request.mapName = "MAP01";
	for (int scale : {100, 200}) {
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme);
		settings.setTextScalePercent(scale);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		QWidget parent;
		parent.show();
		int beats = 0;
		qint64 longestGap = 0;
		QElapsedTimer elapsed;
		elapsed.start();
		QTimer heartbeat;
		heartbeat.setInterval(5);
		QObject::connect(&heartbeat, &QTimer::timeout, &parent, [&] {
			++beats;
			longestGap = std::max(longestGap, elapsed.restart());
		});
		heartbeat.start();
		bool checked = false;
		QTimer::singleShot(0, &parent, [&] {
			auto* dialog = parent.findChild<QDialog*>("gameLaunchTaskDialog");
			expect(dialog, "launch dialog visible");
			if (!dialog) {
				return;
			}
			auto* cancel = dialog->findChild<QPushButton*>("gameLaunchTaskCancel");
			auto* progress = dialog->findChild<QProgressBar*>("gameLaunchTaskProgress");
			auto* phase = dialog->findChild<QLabel*>("gameLaunchTaskPhase");
			expect(cancel && progress && phase, "named launch controls");
			if (!cancel || !progress || !phase) {
				return;
			}
			expect(!cancel->accessibleName().isEmpty() && !cancel->accessibleDescription().isEmpty() &&
					   cancel->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(cancel),
				   "accessible cancel control");
			cancel->setFocus(Qt::TabFocusReason);
			expect(dialog->focusWidget() == cancel, "keyboard focus reaches cancellation");
			expect(progress->maximum() == 1 && phase->wordWrap(), "reduced motion and translation wrapping");
			const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!captures.isEmpty()) {
				QDir().mkpath(captures);
				QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				dialog->render(&image);
				expect(image.save(QDir(captures).filePath(QStringLiteral("doom-node-launch-%1.png").arg(scale))), "render dialog");
			}
			checked = true;
		});
		const auto ready = GameLaunchTaskDialog::prepare(&parent, request, installation);
		heartbeat.stop();
		expect(checked && ready.runnable && ready.validatedArtifactHash.size() == 32, "worker returns validated snapshot",
			   ready.errors.join(';'));
		expect(beats > 2 && longestGap < 500, "GUI remains responsive during large WAD validation", QString::number(longestGap));
		QTimer::singleShot(0, &parent, [&] {
			auto* dialog = parent.findChild<QDialog*>("gameLaunchTaskDialog");
			expect(dialog, "cancellable dialog exists");
			if (dialog) {
				dialog->reject();
			}
		});
		const auto cancelled = GameLaunchTaskDialog::prepare(&parent, request, installation);
		expect(cancelled.cancelled && !cancelled.runnable && cancelled.state() == OperationState::Cancelled,
			   "closing validation waits for acknowledgement and cannot launch");
		d::write(path, "changed");
		const auto changed = GameLaunchTaskDialog::start(&parent, ready);
		expect(!changed.started && changed.pid == 0 && !changed.error.isEmpty(), "worker refuses changed WAD before process start");
		expect(d::write(path, d::wad(lumps)), "restore generated fixture");
		if (scale == 200) {
			app.removeTranslator(&expansion);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	return ok ? 0 : 1;
}
