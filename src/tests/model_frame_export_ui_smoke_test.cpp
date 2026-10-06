#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/studio_settings.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool waitFor(const std::function<bool()> &ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 15000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return ready();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-frame-export-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
	QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	const auto preferences = StudioSettings().accessibilityPreferences();
	applyStudioTheme(app, studioThemeTokens(preferences.theme, preferences.density, preferences.textScalePercent));
	ModelDesign design;
	design.parts << ModelDesignPart{};
	const auto smallMesh = buildModelDesignMesh(design);
	auto mesh = smallMesh;
	mesh.surfaces.clear();
	for (int i = 0; i < 16; ++i)
	{
		auto surface = smallMesh.surfaces.first();
		surface.name = QStringLiteral("surface%1").arg(i);
		surface.triangles.fill(surface.triangles.first(), 1900);
		mesh.surfaces << surface;
	}
	updateEditableModelMetadata(&mesh);
	QString error;
	const auto heavy = exportEditableModel(mesh, "md3", 0, &error);
	QDir().mkpath(path("package"));
	bool ok = expect(!heavy.isEmpty() && write(path("package/heavy.md3"), heavy) &&
						 write(path("package/small.md3"), exportEditableModel(smallMesh, "md3", 0, &error)),
					 "write independent browser fixtures");
	{
		ApplicationShell shell;
		shell.resize(1450, 960);
		shell.show();
		shell.openPathFromCommandLine(path("package"));
		if (auto *mode = shell.findChild<QAction *>("shell.mode.models"))
		{
			mode->trigger();
		}
		auto *entries = shell.findChild<QListWidget *>("modelEntries");
		auto *viewport = shell.findChild<ModelViewport *>("modelViewport");
		auto *button = shell.findChild<QPushButton *>("exportModelFrame");
		QListWidgetItem *big = nullptr, *small = nullptr;
		if (entries)
		{
			for (int i = 0; i < entries->count(); ++i)
			{
				const auto name = entries->item(i)->data(Qt::UserRole).toString();
				if (name == "heavy.md3")
				{
					big = entries->item(i);
				}
				if (name == "small.md3")
				{
					small = entries->item(i);
				}
			}
		}
		ok &= expect(entries && viewport && button && big && small && !button->accessibleName().isEmpty() &&
						 button->focusPolicy() != Qt::NoFocus,
					 "real Models browser exposes accessible export action");
		if (entries && viewport && button && big && small)
		{
			viewport->hide(); // Geometry export does not depend on a raster paint.
			entries->setCurrentItem(big);
			ok &= expect(waitFor([&] { return viewport->mesh().sourcePath == "heavy.md3" && button->isEnabled(); }),
						 "native preview finishes before exporting");
			ok &= expect(!shell.exportSelectedModelTo(path("package/heavy.md3"), true, &error) && read(path("package/heavy.md3")) == heavy,
						 "browser refuses input replacement");
			bool heartbeat = false, reentryRejected = false;
			QTimer::singleShot(0, &shell,
							   [&]
							   {
								   heartbeat = !button->isEnabled();
								   QString nested;
								   reentryRejected = !shell.exportSelectedModelTo(path("reentry.obj"), false, &nested) && !nested.isEmpty();
								   entries->setCurrentItem(small);
							   });
			ok &= expect(shell.exportSelectedModelTo(path("frame.obj"), false, &error) && heartbeat && reentryRejected,
						 "export services the event loop and prevents nested writes");
			const auto output = read(path("frame.obj"));
			ok &= expect(waitFor([&] { return viewport->mesh().sourcePath == "small.md3" && button->isEnabled(); }) &&
							 output.count("\nf ") == mesh.triangleCount && viewport->mesh().triangleCount == smallMesh.triangleCount &&
							 !QFileInfo::exists(path("reentry.obj")),
						 "selection change cannot replace the captured export mesh");
			ok &= expect(!shell.exportSelectedModelTo(path("frame.obj"), false, &error) && read(path("frame.obj")) == output,
						 "browser API also requires explicit overwrite");
			entries->setCurrentItem(big);
			ok &= expect(waitFor([&] { return viewport->mesh().sourcePath == "heavy.md3" && button->isEnabled(); }),
						 "reselected native model loads before cancellation tests");
			bool usedCancel = false;
			QTimer::singleShot(0, &shell,
							   [&]
							   {
								   auto *dialog = shell.findChild<QDialog *>("meshOperationDialog");
								   auto *cancel = dialog ? dialog->findChild<QPushButton *>("cancelMeshOperation") : nullptr;
								   auto *status = dialog ? dialog->findChild<QLabel *>("meshOperationStatus") : nullptr;
								   usedCancel =
									   cancel && status && !status->accessibleName().isEmpty() && cancel->focusPolicy() != Qt::NoFocus;
								   if (cancel)
								   {
									   cancel->click();
								   }
							   });
			ok &= expect(!shell.exportSelectedModelTo(path("frame.obj"), true, &error) && usedCancel && read(path("frame.obj")) == output,
						 "browser cancellation preserves a prior complete output");
			bool picked = false, actionCancelled = false, unexpectedErrorDialog = false, timedOut = false;
			QTimer driveAction, deadline;
			QObject::connect(&driveAction, &QTimer::timeout, &shell,
							 [&]
							 {
								 if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
								 {
									 picker->selectFile(path("button-cancelled.obj"));
									 picked = QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
								 }
								 else if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
								 {
									 unexpectedErrorDialog = true;
									 message->accept();
								 }
								 if (auto *progress = shell.findChild<QDialog *>("meshOperationDialog"))
								 {
									 if (auto *cancelButton = progress->findChild<QPushButton *>("cancelMeshOperation"))
									 {
										 actionCancelled = true;
										 cancelButton->click();
									 }
								 }
							 });
			deadline.setSingleShot(true);
			QObject::connect(&deadline, &QTimer::timeout, &shell,
							 [&]
							 {
								 timedOut = true;
								 if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
								 {
									 dialog->reject();
								 }
							 });
			driveAction.start(1);
			deadline.start(10000);
			button->click();
			driveAction.stop();
			deadline.stop();
			ok &=
				expect(picked && actionCancelled && !unexpectedErrorDialog && !timedOut && !QFileInfo::exists(path("button-cancelled.obj")),
					   "Export OBJ button uses the real picker and cancellation without an error dialog or output");
			bool deferred = false;
			QTimer::singleShot(0, &shell, [&] { deferred = !shell.close() && shell.isVisible(); });
			ok &=
				expect(!shell.exportSelectedModelTo(path("close.obj"), false, &error) && deferred && !QFileInfo::exists(path("close.obj")),
					   "studio close cancels an in-flight export before destruction");
			for (int i = 0; i < 4; ++i)
			{
				app.processEvents();
			}
			ok &= expect(!shell.isVisible(), "one close request resumes once export work has stopped");
		}
		shell.close();
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
