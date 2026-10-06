#include "app/application_shell.h"
#include "app/audio_editor_dialog.h"
#include "app/audio_recovery.h"
#include "app/audio_recovery_dialog.h"
#include "app/studio_layout.h"
#include "app/studio_theme.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QGroupBox>
#include <QImage>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QUuid>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
bool waitFor(const std::function<bool()>& ready)
{
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
		if (ready()) {
			loop.quit();
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(15000);
	if (!ready()) {
		loop.exec();
	}
	return ready();
}
QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool capture(QWidget* widget, const QString& name)
{
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (root.isEmpty()) {
		return true;
	}
	QDir().mkpath(root);
	QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	widget->render(&image);
	return expect(image.save(QDir(root).filePath(name + QStringLiteral(".png"))),
	              "save direct widget render");
}
class Expanded final : public QTranslator {
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!QByteArray(context).endsWith("ApplicationShell")) {
			return {};
		}
		const QString text = QString::fromUtf8(source);
		const QStringList messages{QStringLiteral("Audio Recovery"), QStringLiteral("Keep recovery copies"),
		                           QStringLiteral("Offer copies at startup"), QStringLiteral("Review Copies"),
		                           QStringLiteral("Folder")};
		return messages.contains(text) ? QStringLiteral("[%1 extended]").arg(text) : QString();
	}
};
bool checkPreferences(ApplicationShell& shell, int scale)
{
	shell.findChild<QAction*>(QStringLiteral("shell.mode.settings"))->trigger();
	auto* group = shell.findChild<QGroupBox*>(QStringLiteral("audioRecoveryPreferences"));
	auto* keep = shell.findChild<QCheckBox*>(QStringLiteral("setupAudioRecoveryEnabled"));
	auto* notify = shell.findChild<QCheckBox*>(QStringLiteral("setupAudioRecoveryNotify"));
	auto* review = shell.findChild<QPushButton*>(QStringLiteral("setupReviewAudioRecovery"));
	if (!group || !keep || !notify || !review) {
		return expect(false, "setup exposes audio recovery controls");
	}
	for (QWidget* parent = group->parentWidget(); parent; parent = parent->parentWidget()) {
		if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
			scroll->ensureWidgetVisible(group);
			break;
		}
	}
	QApplication::processEvents();
	bool ok = true;
	for (auto* control :
	     {static_cast<QWidget*>(keep), static_cast<QWidget*>(notify), static_cast<QWidget*>(review)}) {
		ok &= expect(!control->accessibleName().isEmpty() && control->focusPolicy() != Qt::NoFocus &&
		                 group->rect().contains(QRect(control->mapTo(group, QPoint()), control->size())),
		             "setup controls have accessible names, tab focus and contained geometry");
	}
	ok &= expect(keep->nextInFocusChain() == notify,
	             "checkpoint and notification controls follow in tab order");
	ok &= capture(group, QStringLiteral("audio-recovery-setup-%1").arg(scale));
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	// Direct widget methods and QWidget rendering only. No input injection or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	app.setQuitOnLastWindowClosed(false);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-startup-ui-XXXXXX")));
	if (!temporary.isValid()) {
		return 2;
	}
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	StudioSettings().setAudioRecoveryEnabled(false);
	const QString directory = temporary.filePath(QStringLiteral("recovery"));
	qputenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT", directory.toUtf8());
	QString error;
	AudioProject project{{1, 22050, {0.25f, -0.5f}},
	                     0,
	                     2,
	                     QStringLiteral("Startup fixture"),
	                     temporary.filePath(QStringLiteral("source.wav")),
	                     {}};
	bool ok = expect(write(project.sourcePath, QByteArray("protected source")), "create source sentinel");
	const QString original =
	    writeAudioRecovery(project, directory, QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	const QByteArray recoveryBytes = read(original);
	const QString broken = audioRecoveryPath(directory, QUuid::createUuid().toString(QUuid::WithoutBraces));
	ok &= expect(!original.isEmpty() && write(broken, QByteArray("broken")),
	             "create valid and corrupt retained copies");
	{
		AudioRecoveryDiscovery discovery;
		int finished = 0;
		discovery.finished = [&](const AudioRecoveryInventory& result) {
			++finished;
			ok &= expect(result.records.size() == 2 && result.records[0].sha256.isEmpty(),
			             "worker discovery stays metadata-only");
		};
		discovery.start(directory);
		discovery.cancel();
		ok &= expect(waitFor([&] { return !discovery.busy(); }) && finished == 0,
		             "cancelled startup results are not published");
		discovery.start(directory);
		ok &= expect(waitFor([&] { return !discovery.busy(); }) && finished == 1,
		             "worker can complete after cancellation");
	}
	{
		AudioRecoveryDiscovery discovery;
		discovery.start(directory); // Destruction joins safely without publishing late callbacks.
	}
	auto preferences = StudioSettings().accessibilityPreferences();
	preferences.theme = StudioTheme::HighContrastDark;
	preferences.textScalePercent = 100;
	StudioSettings().setAccessibilityPreferences(preferences);
	applyStudioTheme(app, studioThemeTokens(preferences.theme, preferences.density, preferences.textScalePercent));
	{
		ApplicationShell shell;
		shell.show();
		app.processEvents();
		ok &= expect(!shell.findChild<QObject*>(QStringLiteral("audioRecoveryDiscovery")),
		             "construction and self-tests do not initiate startup discovery");
		auto* notice = shell.findChild<NoticeBar*>(QStringLiteral("noticeBar"));
		notice->showNotice(QStringLiteral("failed"), QStringLiteral("Earlier notice"),
		                   QStringLiteral("Keep this visible until dismissed."));
		QWidget* focusBefore = app.focusWidget();
		shell.beginSession(false);
		auto* discovery = static_cast<AudioRecoveryDiscovery*>(
		    shell.findChild<QObject*>(QStringLiteral("audioRecoveryDiscovery")));
		ok &= expect(discovery && waitFor([&] { return !discovery->busy(); }),
		             "session startup runs background discovery");
		ok &= expect(notice->title() == QStringLiteral("Earlier notice") &&
		                 app.focusWidget() == focusBefore && !shell.findChild<AudioEditorDialog*>(),
		             "discovery preserves the preceding notice, focus and unopened documents");
		notice->dismiss();
		ok &= expect(waitFor([&] { return !notice->isHidden(); }) &&
		                 notice->title().contains(QStringLiteral("audio")),
		             "audio recovery is offered after the preceding notice is dismissed");
		ok &= capture(notice, QStringLiteral("audio-recovery-startup-100"));
		auto* review = notice->findChild<QPushButton*>(QStringLiteral("noticeReviewAudio"));
		if (!review) {
			return 1;
		}
		review->click();
		QPointer<AudioRecoveryDialog> manager = shell.findChild<AudioRecoveryDialog*>();
		ok &= expect(manager && waitFor([&] {
			             return manager && !manager->busy() &&
			                    manager->findChild<QTableWidget*>(QStringLiteral("audioRecoveryRecords"))
			                            ->rowCount() == 2;
		             }),
		             "startup review opens the verifying manager");
		auto* table = manager->findChild<QTableWidget*>(QStringLiteral("audioRecoveryRecords"));
		ok &= expect(table->rowCount() == 2, "both valid and corrupt copies remain reviewable");
		int validRow = -1;
		for (int row = 0; row < table->rowCount(); ++row) {
			if (table->item(row, 0)->text() == project.sourceName) {
				validRow = row;
			}
		}
		if (validRow < 0) {
			return 1;
		}
		table->setCurrentCell(validRow, 0);
		manager->findChild<QPushButton*>(QStringLiteral("audioRecoveryRestore"))->click();
		auto* editor = shell.findChild<AudioEditorDialog*>();
		ok &= expect(editor && waitFor([&] { return !editor->isBusy(); }) && editor->hasChanges() &&
		                 editor->projectPath().isEmpty() && editor->clip().samples == project.clip.samples,
		             "startup restore opens exact samples as an unsaved draft");
		ok &= expect(read(original) == recoveryBytes &&
		                 read(project.sourcePath) == QByteArray("protected source") &&
		                 read(broken) == QByteArray("broken"),
		             "discovery and restoration preserve the source and retained copies");
		ok &= checkPreferences(shell, 100);
		auto* keep = shell.findChild<QCheckBox*>(QStringLiteral("setupAudioRecoveryEnabled"));
		auto* editorKeep = editor->findChild<QCheckBox*>(QStringLiteral("audioRecoveryEnabled"));
		keep->setChecked(true);
		ok &= expect(editorKeep->isChecked() && waitFor([&] { return !editor->recoveryBusy(); }),
		             "setup preference updates an open editor and checkpoints its draft");
		editorKeep->setChecked(false);
		ok &= expect(!keep->isChecked() && !StudioSettings().audioRecoveryEnabled() &&
		                 QFileInfo::exists(editor->recoveryPath()),
		             "editor preference updates setup while disabling retains the checkpoint");
		ok &= expect(editor->saveProjectTo(temporary.filePath(QStringLiteral("recovered.vsaudio"))) &&
		                 waitFor([&] { return !editor->isBusy() && !editor->recoveryBusy(); }),
		             "save recovered work before clean close");
		editor->close();
		shell.beginSession(false);
		app.processEvents();
		ok &= expect(notice->isHidden(), "repeated session initialization does not repeat a dismissed offer");
	}
	StudioSettings().setAudioRecoveryNotifyAtStartup(false);
	preferences.theme = StudioTheme::HighContrastLight;
	preferences.textScalePercent = 200;
	StudioSettings().setAccessibilityPreferences(preferences);
	Expanded expanded;
	app.installTranslator(&expanded);
	app.setLayoutDirection(Qt::RightToLeft);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastLight, UiDensity::Standard, 200));
	{
		ApplicationShell shell;
		shell.setLayoutDirection(Qt::RightToLeft);
		shell.show();
		app.processEvents();
		shell.beginSession(false);
		ok &= expect(!shell.findChild<QObject*>(QStringLiteral("audioRecoveryDiscovery")),
		             "disabled startup offers do not scan the directory");
		ok &= checkPreferences(shell, 200);
		auto* command = shell.findChild<QAction*>(QStringLiteral("audio.recover"));
		ok &= expect(command && command->isEnabled(),
		             "File and command palette recovery remains available without notifications");
		if (command) {
			command->trigger();
		}
		QPointer<AudioRecoveryDialog> manager = shell.findChild<AudioRecoveryDialog*>();
		ok &= expect(manager && waitFor([&] {
			             return manager && !manager->busy() &&
			                    manager->findChild<QTableWidget*>(QStringLiteral("audioRecoveryRecords"))
			                            ->rowCount() >= 2;
		             }),
		             "manual recovery remains available when checkpoints and notifications are off");
		if (manager) {
			manager->close();
		}
		app.processEvents();
	}
	app.removeTranslator(&expanded);
	ok &=
	    expect(read(original) == recoveryBytes && read(project.sourcePath) == QByteArray("protected source"),
	           "all startup and setup paths preserve retained source data");
	return ok ? 0 : 1;
}
