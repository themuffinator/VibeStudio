#include "app/application_shell.h"
#include "app/studio_theme.h"
#include "app/texture_editor_dialog.h"
#include "core/studio_settings.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool until(const std::function<bool()>& condition, int milliseconds = 15000)
{
	// This fixture invokes close outside QApplication::exec(). Qt 6.4 leaves
	// those deferred deletes pending when a later nested event loop is entered.
	const auto check = [&]() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); return condition(); };
	QEventLoop loop; QTimer poll, timeout; timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (check()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5); timeout.start(milliseconds); if (!check()) { loop.exec(); } return check();
}
bool settled(const QPointer<TextureEditorDialog>& editor) { return until([&]() { return editor && !editor->isBusy() && !editor->recoveryBusy(); }); }
bool answer(QMessageBox::StandardButton choice, const std::function<void()>& action)
{
	// Activate the dialog's public button API; no injected keyboard/mouse events.
	bool answered = false; QTimer poll, timeout; timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, [&]() {
		for (auto* widget : QApplication::topLevelWidgets()) {
			auto* box = qobject_cast<QMessageBox*>(widget);
			if (box && box->isVisible() && box->button(choice)) { answered = true; poll.stop(); box->button(choice)->click(); return; }
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, [&]() {
		for (auto* widget : QApplication::topLevelWidgets()) { if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) { box->done(QMessageBox::Cancel); } }
	});
	poll.start(1); timeout.start(5000); action(); return answered;
}
bool paint(TextureEditorDialog* editor, const QString& color)
{
	editor->applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("stroke")}, {QStringLiteral("points"), QJsonArray{QJsonArray{0, 0}}}, {QStringLiteral("color"), color}}});
	return settled(editor);
}
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QElapsedTimer timer; timer.start();
	const auto milestone = [&](const char* label) { std::cerr << label << ": " << timer.elapsed() << " ms\n"; };
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	const QDir root(temporary.path()); StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("profile.ini")));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool ok = true; QString error;
	TextureDocument fixture; fixture.create({8, 4}, Qt::red); fixture.addLayer(QStringLiteral("Detail"));
	TextureProjectSaveRequest initial; initial.path = root.filePath(QStringLiteral("source.vtexture"));
	if (!writeTextureProject(fixture, initial).succeeded) { return EXIT_FAILURE; }
	TextureProjectSaveRequest alternate; alternate.path = root.filePath(QStringLiteral("alternate.vtexture"));
	TextureDocument other; other.create({3, 7}, Qt::cyan); if (!writeTextureProject(other, alternate).succeeded) { return EXIT_FAILURE; }
	QPointer<TextureEditorDialog> editor = new TextureEditorDialog; editor->show(); editor->openFromPath(initial.path);
	ok &= expect(settled(editor) && paint(editor, QStringLiteral("blue")), "open and edit lifecycle fixture");
	editor->checkpointRecovery(); const auto checkpoint = editor->recoveryPath();
	ok &= expect(settled(editor) && QFileInfo::exists(checkpoint), "unsaved project gets an independent recovery checkpoint");
	const auto edited = encodeTextureProject(editor->document()); const auto original = read(initial.path);
	editor->findChild<QSpinBox*>(QStringLiteral("textureWidth"))->setValue(12);
	ok &= expect(answer(QMessageBox::Cancel, [&]() { editor->newTexture(); }) && encodeTextureProject(editor->document()) == edited && QFileInfo::exists(checkpoint), "cancel New retains edits and recovery");
	ok &= expect(answer(QMessageBox::Cancel, [&]() { editor->openFromPath(alternate.path); }) && encodeTextureProject(editor->document()) == edited, "cancel Open retains the current project");
	int resumed = 0;
	ok &= expect(answer(QMessageBox::Cancel, [&]() { editor->requestClose([&]() { ++resumed; }); }) && editor->isVisible() && resumed == 0, "cancel Close neither closes nor resumes its caller");
	ok &= expect(answer(QMessageBox::Discard, [&]() { editor->openFromPath(root.filePath(QStringLiteral("missing.vtexture"))); }) && settled(editor) && encodeTextureProject(editor->document()) == edited && QFileInfo::exists(checkpoint), "failed Open after Discard still retains the old document and checkpoint");
	put(initial.path, "external change");
	ok &= expect(answer(QMessageBox::Save, [&]() { editor->requestClose([&]() { ++resumed; }); }) && settled(editor) && editor->isVisible() && editor->hasUnsavedChanges() && resumed == 0 && read(initial.path) == "external change", "failed save-on-close keeps the editor open and never resumes the caller");
	put(initial.path, original);
	ok &= expect(answer(QMessageBox::Save, [&]() { editor->requestClose([&]() { ++resumed; }); }) && until([&]() { return editor.isNull(); }) && resumed == 1 && !QFileInfo::exists(checkpoint), "successful deferred Save closes the editor, retires recovery and resumes exactly once");
	TextureDocument loaded; ok &= expect(readTextureProject(initial.path, &loaded) && encodeTextureProject(loaded) == edited, "save-on-close persists the actual edited layers");
	editor = new TextureEditorDialog; editor->show(); editor->openFromPath(initial.path); ok &= settled(editor); ok &= paint(editor, QStringLiteral("green"));
	editor->checkpointRecovery(); const auto newCheckpoint = editor->recoveryPath(); ok &= settled(editor);
	editor->findChild<QSpinBox*>(QStringLiteral("textureWidth"))->setValue(12); editor->findChild<QSpinBox*>(QStringLiteral("textureHeight"))->setValue(5);
	ok &= expect(answer(QMessageBox::Save, [&]() { editor->newTexture(); }) && settled(editor) && editor->document().size() == QSize(12, 5) && editor->hasUnsavedChanges() && !QFileInfo::exists(newCheckpoint), "Save then New resumes with the requested dimensions and retires the previous recovery");
	ok &= expect(readTextureProject(initial.path, &loaded) && loaded.activeLayer()->pixels.pixelColor(0, 0) == QColor(QStringLiteral("green")), "Save before New writes the outgoing document");
	editor->checkpointRecovery(); const auto discarded = editor->recoveryPath(); ok &= settled(editor);
	editor->findChild<QSpinBox*>(QStringLiteral("textureWidth"))->setValue(6); editor->findChild<QSpinBox*>(QStringLiteral("textureHeight"))->setValue(7);
	ok &= expect(answer(QMessageBox::Discard, [&]() { editor->newTexture(); }) && editor->document().size() == QSize(6, 7) && !QFileInfo::exists(discarded), "Discard then New replaces the document and retires only its checkpoint");
	const auto outgoing = root.filePath(QStringLiteral("outgoing.vtexture")); editor->saveProjectToPath(outgoing, false); ok &= settled(editor); ok &= paint(editor, QStringLiteral("yellow"));
	ok &= expect(answer(QMessageBox::Save, [&]() { editor->openFromPath(alternate.path); }) && settled(editor) && !editor->hasUnsavedChanges() && editor->document().size() == other.size(), "Save then Open resumes asynchronously with the requested project");
	ok &= expect(readTextureProject(outgoing, &loaded) && loaded.image().pixelColor(0, 0) == QColor(Qt::yellow), "Save before Open persists outgoing pixels");
	ok &= paint(editor, QStringLiteral("magenta")); editor->checkpointRecovery(); const auto openDiscarded = editor->recoveryPath(); ok &= settled(editor);
	ok &= expect(answer(QMessageBox::Discard, [&]() { editor->openFromPath(initial.path); }) && settled(editor) && !editor->hasUnsavedChanges() && !QFileInfo::exists(openDiscarded), "Discard then Open retires recovery only after successful load");
	ok &= paint(editor, QStringLiteral("white")); editor->checkpointRecovery(); const auto closeDiscarded = editor->recoveryPath(); ok &= settled(editor);
	resumed = 0;
	ok &= expect(answer(QMessageBox::Discard, [&]() { editor->requestClose([&]() { ++resumed; }); }) && until([&]() { return editor.isNull(); }) && resumed == 0 && !QFileInfo::exists(closeDiscarded), "synchronous Discard close needs no deferred continuation and removes its checkpoint");

	// Real timer coverage, separate profile storage, unexpected owner loss,
	// restoration, metadata-only revisions, and first-save protection.
	milestone("Save/Discard/Cancel transitions complete");
	editor = new TextureEditorDialog; editor->show(); editor->openFromPath(initial.path); ok &= settled(editor);
	editor->findChild<QSpinBox*>(QStringLiteral("textureRecoveryInterval"))->setValue(5);
	ok &= paint(editor, QStringLiteral("#80442211"));
	const auto automatic = editor->recoveryPath();
	ok &= expect(until([&]() { return QFileInfo::exists(automatic) && !editor->recoveryBusy(); }, 10000), "the actual autosave timer checkpoints unsaved content without blocking editing");
	const auto automaticBytes = read(automatic);
	editor->findChild<QCheckBox*>(QStringLiteral("textureRecoveryEnabled"))->setChecked(false);
	editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("textures/metadata-only.png")); editor->checkpointRecovery();
	ok &= expect(!editor->recoveryBusy() && read(automatic) == automaticBytes, "disabling recovery retains existing checkpoints and schedules no new writes");
	editor->findChild<QCheckBox*>(QStringLiteral("textureRecoveryEnabled"))->setChecked(true); ok &= settled(editor);
	QJsonObject recoveryMetadata;
	ok &= expect(restoreTextureRecovery(automatic, &loaded, &recoveryMetadata) && recoveryMetadata.value(QStringLiteral("packageTexturePath")).toString() == QStringLiteral("textures/metadata-only.png"), "metadata-only changes produce a new checkpoint on re-enable");
	editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("textures/last-edit.png"));
	delete editor.data();
	ok &= expect(restoreTextureRecovery(automatic, &loaded, &recoveryMetadata) && recoveryMetadata.value(QStringLiteral("packageTexturePath")).toString() == QStringLiteral("textures/last-edit.png"), "unexpected destruction retains the final unsaved metadata snapshot");
	const auto retained = read(automatic), sourceBeforeRecovery = read(initial.path);
	editor = new TextureEditorDialog; editor->show(); editor->restoreRecovery(automatic);
	ok &= expect(settled(editor) && editor->hasUnsavedChanges() && editor->document().layers().size() == 2 && read(automatic) == retained, "recovery opens an unsaved layered draft while retaining the chosen checkpoint");
	editor->saveProjectToPath(initial.path, true);
	ok &= expect(!editor->isBusy() && editor->hasUnsavedChanges() && read(initial.path) == sourceBeforeRecovery, "the first save of a recovered draft cannot replace an existing project");
	editor->saveToPath(root.filePath(QStringLiteral("draft.png")), false); ok &= settled(editor);
	ok &= expect(editor->hasUnsavedChanges(), "exporting a recovered draft cannot dismiss the native save requirement");
	const auto restoredProject = root.filePath(QStringLiteral("recovered-copy.vtexture")); const auto draftCheckpoint = editor->recoveryPath();
	editor->saveProjectToPath(restoredProject, false); ok &= settled(editor);
	ok &= expect(!editor->hasUnsavedChanges() && !QFileInfo::exists(draftCheckpoint) && read(automatic) == retained && read(initial.path) == sourceBeforeRecovery, "saving the recovered draft retires its new checkpoint and preserves original evidence");
	editor->refreshRecoveries(); ok &= settled(editor);
	auto* list = editor->findChild<QListWidget*>(QStringLiteral("textureRecoveryList")); int selected = -1;
	for (int i = 0; i < list->count(); ++i) { if (QFileInfo(list->item(i)->data(Qt::UserRole).toString()).absoluteFilePath() == QFileInfo(automatic).absoluteFilePath()) { selected = i; break; } }
	ok &= expect(selected >= 0, "the inspector lists retained crash checkpoints");
	if (selected >= 0) {
		list->setCurrentRow(selected); auto* restoreButton = editor->findChild<QPushButton*>(QStringLiteral("restoreTextureRecovery"));
		ok &= expect(restoreButton->isEnabled(), "a verified checkpoint can be selected for restoration");
		const auto info = inspectTextureRecovery(automatic); TextureRecoverySnapshot replacement{loaded, recoveryMetadata, {}, QStringLiteral("Changed checkpoint")};
		writeTextureRecovery(replacement, QFileInfo(automatic).absolutePath(), info.id);
		const auto beforeStaleRestore = encodeTextureProject(editor->document()); restoreButton->click(); ok &= settled(editor);
		ok &= expect(!editor->hasUnsavedChanges() && encodeTextureProject(editor->document()) == beforeStaleRestore && editor->findChild<QLabel*>(QStringLiteral("textureEditorStatus"))->text().contains(QStringLiteral("changed")), "stale recovery-list selections are rechecked before replacing the document");
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastLight, UiDensity::Standard, 200));
	editor->resize(1500, 1000);
	auto* properties = editor->findChild<QTabWidget*>(QStringLiteral("textureProperties"));
	for (int i = 0; i < properties->count(); ++i) {
		properties->setCurrentIndex(i); app.processEvents();
		auto* scroll = qobject_cast<QScrollArea*>(properties->currentWidget());
		const bool fits = scroll && until([&]() { return scroll->horizontalScrollBar()->maximum() == 0; }, 2000);
		if (!fits) { std::cerr << "Overflow in " << properties->tabText(i).toStdString() << '\n'; }
		ok &= expect(fits, "inspector controls fit after changing to 200 percent high-contrast light");
	}
	properties->setCurrentIndex(6); app.processEvents();
	const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (!captures.isEmpty()) {
		QImage image(editor->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); editor->render(&image);
		ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("texture-recovery-200-light.png"))), "render recovery controls at 200 percent high-contrast light without OS capture");
	}
	milestone("Recovery and high-contrast layout complete");
	editor->close(); ok &= until([&]() { return editor.isNull(); });
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	// Restoring this same editor's earlier checkpoint after Save must retain the
	// chosen file while saving the newer work to its original project.
	editor = new TextureEditorDialog; editor->show(); editor->openFromPath(initial.path); ok &= settled(editor);
	ok &= paint(editor, QStringLiteral("blue")); editor->checkpointRecovery(); const auto ownCheckpoint = editor->recoveryPath(); ok &= settled(editor);
	const auto ownBytes = read(ownCheckpoint); ok &= paint(editor, QStringLiteral("red"));
	ok &= expect(answer(QMessageBox::Save, [&]() { editor->restoreRecovery(ownCheckpoint); }) && settled(editor) && editor->hasUnsavedChanges() &&
		editor->document().activeLayer()->pixels.pixelColor(0, 0) == QColor(Qt::blue) && read(ownCheckpoint) == ownBytes,
		"Save before restoring the current editor's checkpoint preserves that checkpoint and restores its earlier pixels");
	ok &= expect(readTextureProject(initial.path, &loaded) && loaded.activeLayer()->pixels.pixelColor(0, 0) == QColor(Qt::red), "self-recovery Save preserves the newer outgoing project before restoring the earlier draft");
	ok &= expect(answer(QMessageBox::Discard, [&]() { editor->close(); }) && until([&]() { return editor.isNull(); }) && read(ownCheckpoint) == ownBytes, "discarding a restored draft retains the explicitly selected original checkpoint");
	// Test the actual parent-window continuation, not just an isolated callback.
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("shell.ini")));
	milestone("Constructing shell");
	auto* shell = new ApplicationShell; milestone("Shell constructed"); shell->show(); shell->openPathFromCommandLine(initial.path);
	editor = shell->findChild<TextureEditorDialog*>(QStringLiteral("textureEditorDialog"));
	ok &= expect(editor && settled(editor) && paint(editor, QStringLiteral("#123456")), "shell opens the close-continuation fixture");
	if (editor) {
		ok &= expect(answer(QMessageBox::Save, [&]() { shell->close(); }) && until([&]() { return !shell->isVisible() && editor.isNull(); }), "saving a texture during main-window close automatically resumes and completes that close");
	}
	delete shell; StudioSettings::setOverrideFilePath({});
	milestone("Shell close continuation complete");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
