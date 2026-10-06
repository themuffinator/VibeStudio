#include "app/model_assembly_dialog.h"
#include "app/model_assembly_recovery_dialog.h"
#include "app/model_assembly_recovery_writer.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_assembly_test_helpers.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QUuid>
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
bool wait(const std::function<bool()> &done)
{
	QEventLoop loop;
	QTimer poll, deadline;
	deadline.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop,
					 [&]
					 {
						 if (done())
						 {
							 loop.quit();
						 }
					 });
	QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5);
	deadline.start(15000);
	if (!done())
	{
		loop.exec();
	}
	return done();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		return QByteArray(context) == "ModelAssemblyRecovery" ? QStringLiteral("[ %1 / %1 ]").arg(QString::fromUtf8(source)) : QString();
	}
};
bool capture(QDialog &dialog, const QString &name)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty())
	{
		return true;
	}
	if (!QDir().mkpath(directory))
	{
		return false;
	}
	QImage image(dialog.size() * dialog.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(dialog.devicePixelRatioF());
	image.fill(Qt::transparent);
	dialog.render(&image);
	return image.save(QDir(directory).filePath(name + ".png"));
}
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("assembly-recovery-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	StudioSettings().setModelRecoveryEnabled(true);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelAssemblyRecoverySnapshot snapshot;
	snapshot.assembly = tests::assemblyRecipe(path("part.mesh.json"));
	snapshot.directory = temporary.path();
	snapshot.selectedPart = "child";
	snapshot.seconds = .5;
	bool ok = expect(write(path("part.mesh.json"), QJsonDocument(editableModelJson(tests::assemblyModel())).toJson()), "model fixture");
	const auto directory = modelAssemblyRecoveryDirectory();
	QString error, savedPath;
	{
		ModelAssemblyRecoveryWriter writer(directory);
		ok &= expect(writer.checkpoint(snapshot), "background checkpoint accepted");
		for (int i = 0; i < 25; ++i)
		{
			snapshot.seconds = i + .25;
			writer.checkpoint(snapshot);
		}
		int ticks = 0;
		QTimer pulse;
		QObject::connect(&pulse, &QTimer::timeout, &app, [&] { ++ticks; });
		pulse.start(1);
		ok &= expect(wait([&] { return !writer.busy(); }) && ticks > 0, "coalesced writer services GUI events");
		savedPath = writer.path();
		const auto record = inspectModelAssemblyRecovery(savedPath);
		ok &= expect(record.isValid() && record.seconds == 24.25 && !writer.checkpoint(snapshot),
					 "latest pending state wins and unchanged snapshots deduplicate");
		ok &=
			expect(!discardModelAssemblyRecovery(directory, record.id, record.sha256, false, &error), "writer owns a live recovery lease");
		bool retired = false;
		writer.retired = [&] { retired = true; };
		snapshot.seconds = 50;
		writer.checkpoint(snapshot);
		writer.retire();
		ok &= expect(wait([&] { return retired; }) && !QFileInfo::exists(savedPath),
					 "retirement cancels pending work and cannot resurrect a saved/discarded copy");
	}
	{
		auto writer = std::make_unique<ModelAssemblyRecoveryWriter>(directory);
		savedPath = writer->path();
		snapshot.seconds = 1;
		writer->checkpoint(snapshot);
		snapshot.seconds = 2;
		writer->checkpoint(snapshot);
	}
	auto record = inspectModelAssemblyRecovery(savedPath);
	ok &= expect(record.isValid() && record.seconds == 2 && !record.sessionFilePresent,
				 "unexpected destruction flushes latest pending snapshot and releases live lease");
	ok &= expect(discardModelAssemblyRecovery(directory, record.id, record.sha256, false, &error), "reviewed writer fixture removal");
	{
		ModelAssemblyDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.show();
		ok &= expect(editor.setAssembly(snapshot.assembly, snapshot.directory, &error) &&
						 editor.saveSource(path("source.assembly.json"), false, &error),
					 "saved GUI document fixture");
		const auto original = read(path("source.assembly.json"));
		auto part = editor.document().assembly().parts[1];
		part.scale = 1.5;
		ok &= expect(editor.applyPart("child", part, &error) && editor.setTime(.5, &error), "dirty GUI assembly edits and manual time");
		editor.selectPart("child");
		editor.checkpointRecovery();
		ok &= expect(wait([&] { return !editor.recoveryBusy(); }), "editor checkpoint settles");
		savedPath = editor.recoveryPath();
		record = inspectModelAssemblyRecovery(savedPath, &snapshot);
		ok &= expect(record.isValid() && snapshot.selectedPart == "child" && snapshot.seconds == .5 &&
						 snapshot.sourcePath == path("source.assembly.json") &&
						 snapshot.sourceSha256 == editor.document().sourceFingerprint(),
					 "GUI checkpoint stores selected part, time and original fingerprint");
		auto *preference = editor.findChild<QCheckBox *>("assemblyRecoveryEnabled");
		auto *status = editor.findChild<QLabel *>("assemblyRecoveryStatus");
		ok &= expect(preference && preference->isChecked() && status && !status->text().isEmpty() &&
						 QAccessible::queryAccessibleInterface(preference)->role() == QAccessible::CheckBox,
					 "recovery preference and textual status are accessible");
		ok &= expect(capture(editor, "assembly-recovery-editor"), "widget-owned editor recovery status capture");
		ok &= expect(editor.restoreRecovery(savedPath, record.sha256, &error) && editor.document().isModified() &&
						 editor.document().path().isEmpty() && editor.document().selectedPart() == "child" && editor.timeSeconds() == .5 &&
						 editor.previewReady(),
					 "live own-copy restoration retains editor context and samples linked models");
		ok &= expect(wait([&] { return !editor.recoveryBusy(); }) && inspectModelAssemblyRecovery(savedPath).sha256 == record.sha256,
					 "restoring own active copy preserves reviewed copy and checkpoints draft under another identity");
		const auto draftCopy = editor.recoveryPath();
		ok &= expect(!editor.saveSource(path("source.assembly.json"), true, &error) && read(path("source.assembly.json")) == original,
					 "GUI recovered draft cannot overwrite original");
		ok &= expect(editor.saveSource(path("new.assembly.json"), false, &error) && wait([&] { return !QFileInfo::exists(draftCopy); }) &&
						 QFileInfo::exists(savedPath),
					 "saving retires only current draft recovery");
		part.scale = 2;
		ok &= expect(editor.applyPart("child", part, &error) && wait([&] { return !editor.recoveryBusy(); }), "second dirty session");
		const auto undoCopy = editor.recoveryPath();
		ok &= expect(editor.undo(&error) && !editor.document().isModified() && wait([&] { return !QFileInfo::exists(undoCopy); }),
					 "undo back to saved source retires recovery");
		ok &= expect(editor.redo(&error) && editor.document().isModified() && wait([&] { return !editor.recoveryBusy(); }),
					 "redo creates a new dirty checkpoint");
		const auto secondCopy = editor.recoveryPath();
		preference->setChecked(false);
		ok &= expect(!StudioSettings().modelRecoveryEnabled() && wait([&] { return !editor.recoveryBusy(); }) &&
						 QFileInfo::exists(secondCopy),
					 "shared preference pauses checkpoints and retains current local copy");
		preference->setChecked(true);
		ok &= expect(wait([&] { return !editor.recoveryBusy(); }) && QFileInfo::exists(editor.recoveryPath()),
					 "re-enabling recovery checkpoints existing dirty document");
		QTimer answer;
		QMessageBox::StandardButton response = QMessageBox::Cancel;
		QObject::connect(&answer, &QTimer::timeout, &app,
						 [&]
						 {
							 if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
							 {
								 box->button(response)->click();
							 }
						 });
		answer.start(5);
		ok &= expect(!editor.close() && editor.document().isModified() && QFileInfo::exists(editor.recoveryPath()),
					 "cancelled close preserves dirty assembly and recovery");
		const auto closeCopy = editor.recoveryPath();
		response = QMessageBox::Discard;
		ok &= expect(editor.close() && wait([&] { return !QFileInfo::exists(closeCopy); }),
					 "approved discard closes and retires current copy");
		answer.stop();
	}
	// Missing inputs do not prevent restoration and repair of the linked source.
	snapshot.assembly.parts[0].source = path("unavailable.mesh.json");
	snapshot.seconds = 3.25;
	auto external = ModelAssemblyRecoverySession::acquire(directory, QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ok &= expect(external && external->write(snapshot, &error), "unavailable input recovery fixture");
	if (!external)
	{
		return EXIT_FAILURE;
	}
	const auto missing = inspectModelAssemblyRecovery(external->path());
	external.reset();
	{
		ModelAssemblyDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		ok &= expect(editor.restoreRecovery(missing.path, missing.sha256, &error) && !editor.previewReady() && !error.isEmpty() &&
						 editor.timeSeconds() == 3.25 && editor.document().assembly().parts.size() == 2 && editor.document().isModified(),
					 "missing input recipe restores as repairable dirty draft without a stale preview");
		ok &= expect(editor.saveSource(path("missing-restored.assembly.json"), false, &error), "missing input draft can be saved");
		editor.close();
	}
	const auto invalidPath = modelAssemblyRecoveryPath(directory, QUuid::createUuid().toString(QUuid::WithoutBraces));
	ok &= expect(write(invalidPath, "invalid record"), "invalid chooser entry fixture");
	Expansion expansion;
	for (int variant = 0; variant < 3; ++variant)
	{
		applyStudioTheme(app, studioThemeTokens(variant == 0   ? StudioTheme::Dark
												: variant == 1 ? StudioTheme::HighContrastDark
															   : StudioTheme::HighContrastLight,
												variant == 0 ? UiDensity::Standard : UiDensity::Comfortable, variant == 0 ? 100 : 200));
		if (variant == 2)
		{
			app.installTranslator(&expansion);
		}
		QTimer choose, deadline;
		deadline.setSingleShot(true);
		bool inspected = false;
		QObject::connect(&choose, &QTimer::timeout, &app,
						 [&]
						 {
							 for (auto *widget : QApplication::topLevelWidgets())
							 {
								 if (widget->objectName() != "assemblyRecoveryDialog" || inspected)
								 {
									 continue;
								 }
								 auto *dialog = qobject_cast<QDialog *>(widget);
								 auto *list = dialog->findChild<QListWidget *>("assemblyRecoveryList");
								 auto *restore = dialog->findChild<QPushButton *>("assemblyRecoveryRestore");
								 if (!list || !list->isEnabled() || list->count() < 3)
								 {
									 continue;
								 }
								 inspected = true;
								 if (variant == 2)
								 {
									 dialog->setLayoutDirection(Qt::RightToLeft);
									 dialog->resize(1460, 1000);
								 }
								 list->setCurrentRow(list->count() - 1);
								 ok &= expect(!restore->isEnabled(), "invalid recovery visible but not restorable");
								 list->setCurrentRow(0);
								 ok &= expect(restore->isEnabled() && restore->focusPolicy() != Qt::NoFocus &&
												  QAccessible::queryAccessibleInterface(list)->role() == QAccessible::List,
											  "standard list and restoration button expose accessible state and focus");
								 QApplication::processEvents();
								 ok &= expect(list->horizontalScrollBar()->maximum() == 0 &&
												  list->visualItemRect(list->item(list->count() - 1)).height() > 0,
											  "wrapped recovery rows fit expanded RTL without horizontal clipping");
								 ok &= expect(dialog->minimumSizeHint().width() <= dialog->width() &&
												  restore->width() >= restore->fontMetrics().horizontalAdvance(restore->text()) + 12,
											  "recovery chooser accommodates scaled and expanded text");
								 ok &= expect(capture(*dialog, QStringLiteral("assembly-recovery-chooser-%1").arg(variant)),
											  "widget-owned recovery chooser capture");
								 restore->click();
							 }
						 });
		QObject::connect(&deadline, &QTimer::timeout, &app,
						 []
						 {
							 for (auto *widget : QApplication::topLevelWidgets())
							 {
								 if (widget->objectName() == "assemblyRecoveryDialog")
								 {
									 widget->close();
								 }
							 }
						 });
		choose.start(10);
		deadline.start(15000);
		const auto chosen = chooseModelAssemblyRecovery(nullptr, directory);
		choose.stop();
		deadline.stop();
		ok &= expect(inspected && chosen && chosen->document.path().isEmpty() && chosen->document.isModified() &&
						 QFileInfo::exists(chosen->record.path),
					 "chooser returns verified unsaved draft and retains recovery copy");
		if (variant == 2)
		{
			app.removeTranslator(&expansion);
		}
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
