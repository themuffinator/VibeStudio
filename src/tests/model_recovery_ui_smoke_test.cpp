#include "app/model_editor_dialog.h"
#include "app/model_recovery_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
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
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelRecovery")
		{
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
bool capture(QDialog &dialog, int scale)
{
	auto *restore = dialog.findChild<QPushButton *>(QStringLiteral("restoreModelRecovery"));
	bool ok = restore && restore->width() >= restore->fontMetrics().horizontalAdvance(restore->text()) + 12 &&
			  restore->focusPolicy() != Qt::NoFocus;
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (!directory.isEmpty())
	{
		QDir().mkpath(directory);
		QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		dialog.render(&image);
		ok &= image.save(QDir(directory).filePath(QStringLiteral("mesh-recovery-%1.png").arg(scale)));
	}
	return ok;
}
bool settle(ModelEditorDialog &editor)
{
	QEventLoop loop;
	QTimer deadline, check;
	deadline.setSingleShot(true);
	QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(&check, &QTimer::timeout, &loop,
					 [&]
					 {
						 if (!editor.recoveryBusy())
						 {
							 loop.quit();
						 }
					 });
	deadline.start(20000);
	check.start(5);
	if (editor.recoveryBusy())
	{
		loop.exec();
	}
	return !editor.recoveryBusy();
}
QDialog *chooser()
{
	for (auto *widget : QApplication::topLevelWidgets())
	{
		if (widget->objectName() == QStringLiteral("modelRecoveryDialog"))
		{
			return qobject_cast<QDialog *>(widget);
		}
	}
	return nullptr;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

int main(int argc, char **argv)
{
	// Direct widget APIs exercise UI state without injecting user input or taking OS screenshots.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-recovery-ui-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	bool ok = true;
	QString error;
	const auto directory = modelRecoveryDirectory();
	ModelEdit edit;
	edit.selection.faces = {0};
	edit.translation = {1, 0, 0};
	{
		ModelEditorDialog editor;
		ok &= expect(editor.applyEdit(edit, &error), "edit model before checkpoint");
		editor.checkpointRecovery();
		const auto path = editor.recoveryPath();
		ok &= expect(settle(editor) && inspectModelRecovery(path).isValid(), "editor checkpoints unsaved geometry in the background");
		auto *enabled = editor.findChild<QCheckBox *>(QStringLiteral("meshRecoveryEnabled"));
		ok &= expect(enabled && !enabled->accessibleName().isEmpty(), "recovery preference is accessible");
		enabled->setChecked(false);
		ok &= expect(!StudioSettings().modelRecoveryEnabled() && QFileInfo::exists(path),
					 "disabling persists the preference and keeps committed copies");
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		ok &= expect(settle(editor) && !editor.document().isModified() && !QFileInfo::exists(path),
					 "undo to clean state retires the old checkpoint");
		enabled->setChecked(true);
		ModelEdit duplicate;
		duplicate.kind = ModelEditKind::DuplicateFrame;
		duplicate.frame = 0;
		duplicate.text = QStringLiteral("pose02");
		ok &= expect(editor.applyEdit(duplicate, &error), "prepare a second frame for recovery");
		editor.findChild<QComboBox *>(QStringLiteral("meshFrame"))->setCurrentIndex(1);
		ok &= expect(editor.applyEdit(edit, &error), "prepare interrupted editor");
	}
	auto scan = listModelRecoveries(directory);
	ok &= expect(scan.records.size() == 1 && scan.records[0].isValid(), "unexpected editor destruction preserves the last unsaved edit");
	if (scan.records.size() != 1)
	{
		return EXIT_FAILURE;
	}
	const auto path = scan.records[0].path;
	const auto bytes = read(path);
	bool inspected = false, triggered = false;
	QTimer driver, deadline;
	deadline.setSingleShot(true);
	QObject::connect(&deadline, &QTimer::timeout, &app,
					 []
					 {
						 if (auto *dialog = chooser())
						 {
							 dialog->reject();
						 }
					 });
	QObject::connect(&driver, &QTimer::timeout, &app,
					 [&]
					 {
						 auto *dialog = chooser();
						 if (!dialog || triggered)
						 {
							 return;
						 }
						 auto *list = dialog->findChild<QListWidget *>(QStringLiteral("modelRecoveryList"));
						 if (!list || list->count() == 0 || !list->isEnabled())
						 {
							 return;
						 }
						 list->setCurrentRow(0);
						 auto *restore = dialog->findChild<QPushButton *>(QStringLiteral("restoreModelRecovery"));
						 auto *discard = dialog->findChild<QPushButton *>(QStringLiteral("discardModelRecovery"));
						 inspected =
							 restore && restore->isEnabled() && discard && !discard->isEnabled() && !list->accessibleName().isEmpty();
						 triggered = true;
						 ok &= expect(capture(*dialog, 100), "recovery controls fit at normal scale");
						 restore->click();
					 });
	driver.start(5);
	deadline.start(20000);
	auto restored = chooseModelRecovery(nullptr, directory);
	driver.stop();
	deadline.stop();
	ok &= expect(inspected && restored && restored->document.isModified() && restored->document.path().isEmpty() &&
					 restored->document.selection().faces == edit.selection.faces && read(path) == bytes,
				 "chooser verifies the payload, preserves selection, and restores a draft without modifying its copy");
	{
		ModelEditorDialog editor;
		triggered = false;
		driver.start(5);
		deadline.start(20000);
		editor.findChild<QAction *>(QStringLiteral("recoverMesh"))->trigger();
		driver.stop();
		deadline.stop();
		ok &= expect(editor.document().isModified() && editor.document().path().isEmpty() &&
						 editor.document().selection().faces == edit.selection.faces && editor.document().mesh().frameCount == 2 &&
						 editor.findChild<QComboBox *>(QStringLiteral("meshFrame"))->currentIndex() == 1 && !editor.document().canUndo(),
					 "editor recovery action publishes the draft with its saved frame and selection");
		const auto draftCopy = editor.recoveryPath();
		ok &=
			expect(settle(editor) && inspectModelRecovery(draftCopy).isValid(), "restored editor draft receives an independent checkpoint");
		ok &= expect(editor.setMesh(editor.document().mesh(), &error) && settle(editor) && !QFileInfo::exists(draftCopy) &&
						 read(path) == bytes,
					 "replacing the restored draft retires its checkpoint and keeps the original recovery available");
	}
	// A damaged payload is listed by its header but must not be accepted by Restore.
	Expansion expansion;
	app.installTranslator(&expansion);
	app.setLayoutDirection(Qt::RightToLeft);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 200));
	auto damaged = bytes;
	damaged[damaged.size() - 1] = char(damaged.back() ^ 1);
	QFile file(path);
	ok &= expect(file.open(QIODevice::WriteOnly) && file.write(damaged) == damaged.size(), "prepare damaged recovery");
	file.close();
	triggered = false;
	bool rejectedDamage = false;
	QObject::disconnect(&driver, nullptr, &app, nullptr);
	QObject::connect(&driver, &QTimer::timeout, &app,
					 [&]
					 {
						 auto *dialog = chooser();
						 if (!dialog)
						 {
							 return;
						 }
						 auto *list = dialog->findChild<QListWidget *>(QStringLiteral("modelRecoveryList"));
						 if (!list || list->count() == 0 || !list->isEnabled())
						 {
							 return;
						 }
						 if (!triggered)
						 {
							 list->setCurrentRow(0);
							 triggered = true;
							 dialog->findChild<QPushButton *>(QStringLiteral("restoreModelRecovery"))->click();
							 return;
						 }
						 const auto message = dialog->findChild<QPlainTextEdit *>(QStringLiteral("modelRecoveryDetails"))->toPlainText();
						 if (message.contains(QStringLiteral("checksum")))
						 {
							 rejectedDamage = true;
							 ok &= expect(capture(*dialog, 200),
										  "recovery controls fit at 200 percent high contrast with RTL and expanded labels");
							 dialog->reject();
						 }
					 });
	driver.start(5);
	deadline.start(20000);
	restored = chooseModelRecovery(nullptr, directory);
	driver.stop();
	deadline.stop();
	ok &= expect(rejectedDamage && !restored && read(path) == damaged, "damaged restore stays in the chooser and preserves the evidence");
	ModelDesign design;
	design.parts << ModelDesignPart{};
	ModelRecoverySnapshot other;
	other.mesh = buildModelDesignMesh(design);
	other.title = QStringLiteral("Another copy");
	const auto otherPath = writeModelRecovery(other, directory, QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	const auto otherBytes = read(otherPath);
	ok &= expect(!otherPath.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(otherBytes) == otherBytes.size(),
				 "prepare a header naming another recovery copy");
	file.close();
	triggered = false;
	bool safelyDiscarded = false;
	QObject::disconnect(&driver, nullptr, &app, nullptr);
	QObject::connect(&driver, &QTimer::timeout, &app,
					 [&]
					 {
						 auto *dialog = chooser();
						 if (!dialog)
						 {
							 return;
						 }
						 auto *list = dialog->findChild<QListWidget *>(QStringLiteral("modelRecoveryList"));
						 if (!list || !list->isEnabled() || list->count() == 0)
						 {
							 return;
						 }
						 if (triggered)
						 {
							 if (!QFileInfo::exists(path))
							 {
								 safelyDiscarded = read(otherPath) == otherBytes;
								 dialog->reject();
							 }
							 return;
						 }
						 for (int i = 0; i < list->count(); ++i)
						 {
							 if (list->item(i)->toolTip() != path)
							 {
								 continue;
							 }
							 list->setCurrentRow(i);
							 triggered = true;
							 QTimer::singleShot(0, dialog,
												[dialog]
												{
													if (auto *question = dialog->findChild<QMessageBox *>())
													{
														question->button(QMessageBox::Discard)->click();
													}
												});
							 dialog->findChild<QPushButton *>(QStringLiteral("discardModelRecovery"))->click();
							 return;
						 }
					 });
	driver.start(5);
	deadline.start(20000);
	restored = chooseModelRecovery(nullptr, directory);
	driver.stop();
	deadline.stop();
	ok &= expect(safelyDiscarded && !restored, "discard targets the chosen filename even when its damaged header names another copy");
	QTimer::singleShot(0, &app,
					   []
					   {
						   if (auto *dialog = chooser())
						   {
							   dialog->reject();
						   }
					   });
	ok &= expect(!chooseModelRecovery(nullptr, directory), "cancelling an active catalog scan returns no draft");
	StudioSettings::setOverrideFilePath({});
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
