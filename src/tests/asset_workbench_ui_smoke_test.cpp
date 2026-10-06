#include "package_entry_test_helpers.h"
#include "app/application_shell.h"
#include "app/asset_views.h"
#include "app/studio_actions.h"
#include "app/studio_layout.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"
#include "app/model_viewport.h"
#include "core/audio_export.h"
#include "core/workspace_document.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QTreeWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QtEndian>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const QString& message)
{
	if (!value) { std::cerr << message.toStdString() << '\n'; }
	return value;
}
bool waitUntil(const std::function<bool()>& done)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!done() && elapsed.elapsed() < 20000) {
		QEventLoop loop; QTimer::singleShot(5, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return done();
}
bool put(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray duplicateWad()
{
	QByteArray bytes("PWAD", 4); bytes.resize(12);
	for (const char value : {char(64), char(192)}) {
		QByteArray sound(108, value); qToLittleEndian<quint16>(3, sound.data());
		qToLittleEndian<quint16>(11025, sound.data() + 2); qToLittleEndian<quint32>(100, sound.data() + 4); bytes += sound;
	}
	const auto directory = bytes.size();
	for (int i = 0; i < 2; ++i) {
		QByteArray entry(16, '\0'); qToLittleEndian<quint32>(12 + i * 108, entry.data());
		qToLittleEndian<quint32>(108, entry.data() + 4); entry.replace(8, 6, "DSSAME"); bytes += entry;
	}
	qToLittleEndian<quint32>(2, bytes.data() + 4); qToLittleEndian<quint32>(directory, bytes.data() + 8); return bytes;
}
class Expansion final : public QTranslator {
public:
	QString translate(const char*, const char* source, const char*, int) const override
	{
		return QStringLiteral("[%1 · %1]").arg(QString::fromUtf8(source));
	}
};
bool checkSurface(ApplicationShell& shell, const QString& mode, const QString& capture, bool empty)
{
	auto* action = shell.findChild<QAction*>(QStringLiteral("shell.mode.") + mode);
	if (!expect(action, QStringLiteral("Missing module command: ") + mode)) { return false; }
	action->trigger(); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	shell.resize(1440, 900); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	auto* page = shell.findChild<QStackedWidget*>(QStringLiteral("modeStack"))->currentWidget();
	if (mode == QStringLiteral("models") && !empty) {
		auto* viewport = shell.findChild<ModelViewport*>(QStringLiteral("modelViewport"));
		waitUntil([&] { return !viewport->isRendering(); });
	}
	auto* header = dynamic_cast<PageHeader*>(page->findChild<QWidget*>(QStringLiteral("pageHeader")));
	bool ok = expect(header && !header->titleLabel()->text().isEmpty(), mode + QStringLiteral(" has a common named header"));
	ok &= expect(shell.width() <= 1440, mode + QStringLiteral(" fits the requested window width: %1").arg(shell.width()));
	if (shell.width() > 1440 && mode == QStringLiteral("workspace")) {
		for (auto* widget : shell.findChildren<QWidget*>()) {
			if (widget->minimumWidth() > 1000 || widget->minimumSizeHint().width() > 1000) {
				std::cerr << widget->metaObject()->className() << ' ' << widget->objectName().toStdString()
					<< " minimum " << widget->minimumWidth() << " hint " << widget->minimumSizeHint().width() << '\n';
			}
		}
	}
	if (empty) {
		for (auto* label : page->findChildren<QLabel*>(QStringLiteral("emptyBody"))) {
			if (label->isVisible()) { ok &= expect(label->height() >= label->heightForWidth(label->width()), mode + QStringLiteral(" keeps all wrapped empty-state text readable")); }
		}
	}
	if (header) {
		for (auto* button : header->findChildren<QAbstractButton*>()) {
			if (!button->isVisible()) { continue; }
			ok &= expect(header->rect().contains(QRect(button->mapTo(header, QPoint()), button->size())), mode + QStringLiteral(" header action fits: ") + button->objectName());
			ok &= expect(!button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(button),
				mode + QStringLiteral(" header action has keyboard focus and accessible identity: ") + button->objectName());
		}
	}
	if (empty && (mode == QStringLiteral("levels") || mode == QStringLiteral("textures") || mode == QStringLiteral("packages") ||
		mode == QStringLiteral("code") || mode == QStringLiteral("shaders"))) {
		for (auto* toolbar : page->findChildren<QToolBar*>(QStringLiteral("pageToolBar"))) {
			ok &= expect(!toolbar->isVisible(), mode + QStringLiteral(" hides content tools before content is opened"));
		}
	}
	if (!empty) {
		if (auto* toolbar = page->findChild<QToolBar*>(QStringLiteral("pageToolBar"), Qt::FindDirectChildrenOnly)) {
			ok &= expect(toolbar->isVisible(), mode + QStringLiteral(" restores content tools after opening a document"));
		}
	}
	if (empty && mode == QStringLiteral("build")) {
		auto* details = dynamic_cast<DetailDrawer*>(shell.findChild<QFrame*>(QStringLiteral("buildPipelineDetails")));
		ok &= expect(details && details->currentSectionText().contains("No build has run") &&
			!details->currentSectionText().contains("State: failed"), "Build details explain missing input without reporting a failed run");
	}
	for (auto* grid : page->findChildren<QTreeWidget*>(QStringLiteral("propertyGrid"))) {
		if (!grid->isVisible()) { continue; }
		for (int column = 0; column < grid->columnCount(); ++column) {
			ok &= expect(grid->header()->sectionSize(column) >= grid->header()->sectionSizeHint(column),
				mode + QStringLiteral(" property header fits its translated label: %1").arg(column));
		}
	}
	if (!capture.isEmpty()) {
		QDir().mkpath(capture);
		QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
		ok &= expect(image.save(QDir(capture).filePath(mode + QStringLiteral(".png"))), QStringLiteral("Save Qt-rendered evidence"));
	}
	return ok;
}
bool workflow(QApplication& app, const QString& root, int scale)
{
	StudioSettings::setOverrideFilePath(QDir(root).filePath(QStringLiteral("settings-%1.ini").arg(scale)));
	StudioSettings settings; settings.setRestoreSession(false); settings.setReducedMotion(true);
	settings.setAudioRecoveryEnabled(false); settings.setTextureRecoveryEnabled(false); settings.setModelRecoveryEnabled(false);
	settings.setPackageRecoveryEnabled(false); settings.setLevelRecoveryEnabled(false); settings.setCodeRecoveryEnabled(false);
	auto preferences = settings.accessibilityPreferences(); preferences.textScalePercent = scale;
	preferences.theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight;
	settings.setAccessibilityPreferences(preferences); settings.sync();
	Expansion expansion; if (scale == 200) { app.installTranslator(&expansion); }
	bool ok = true;
	{
		ApplicationShell shell; if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
		shell.resize(1440, 900); shell.show();
		const auto captureRoot = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		const auto captures = [&](const QString& phase) { return captureRoot.isEmpty() ? QString() : QDir(captureRoot).filePath(QStringLiteral("%1-%2").arg(scale).arg(phase)); };
		const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
		for (const auto* id : {"texture.editor", "texture.edit", "texture.export", "texture.reveal", "model.editor", "model.design", "model.assembly",
			"model.export", "model.reveal", "audio.open", "audio.edit", "audio.session", "audio.export", "audio.reveal"}) {
			ok &= expect(action(id) && !action(id)->text().isEmpty() && !action(id)->statusTip().isEmpty(), QStringLiteral("Registered action: ") + id);
			bool inMenu = false;
			for (const auto* menu : shell.findChildren<QMenu*>()) { inMenu |= menu->actions().contains(action(id)); }
			ok &= expect(inMenu && commandPaletteEntryForCommandId(QString::fromLatin1(id)), QStringLiteral("Menu and CLI discovery: ") + id);
		}
		for (const auto* id : {"texture.edit", "texture.export", "texture.reveal", "audio.edit", "audio.export", "audio.reveal", "model.reveal"}) {
			ok &= expect(!action(id)->isEnabled(), QStringLiteral("No selection disables ") + id);
		}
		for (const auto* mode : {"workspace", "levels", "models", "textures", "audio", "packages", "code", "shaders", "build", "settings"}) {
			ok &= checkSurface(shell, QString::fromLatin1(mode), captures("empty"), true);
		}
		for (const auto* name : {"startTextures", "startModels", "startAudio"}) {
			auto* button = shell.findChild<QPushButton*>(QString::fromLatin1(name));
			ok &= expect(button && button->isEnabled() && !button->property("commandId").toString().isEmpty(), QStringLiteral("Empty page has direct authoring: ") + name);
		}
		const auto project = QDir(root).filePath("project");
		WorkspaceDocument document; document.projectPath = project; document.packagePath = project;
		document.activeModule = QStringLiteral("textures"); document.assetSelections = {{"textures", "wall.png"}};
		const auto workspace = QDir(root).filePath(QStringLiteral("ui-%1.vibeworkspace").arg(scale)); QString error;
		ok &= expect(writeWorkspaceDocument(workspace, document, {}, nullptr, &error) && shell.openWorkspaceFrom(workspace, &error), error);
		auto* textures = shell.findChild<QListWidget*>("textureEntries");
		auto* models = shell.findChild<QListWidget*>("modelEntries");
		auto* audio = shell.findChild<QListWidget*>("audioEntries");
		const auto select = [&](QListWidget* list, const QString& path) {
			for (int row = 0; row < list->count(); ++row) { if (list->item(row)->data(Qt::UserRole).toString() == path) { list->setCurrentRow(row); return true; } }
			return false;
		};
		ok &= expect(waitUntil([&] { return action("texture.edit")->isEnabled(); }), "Selected image finishes decoding");
		ok &= expect(select(textures, "broken.png") && !action("texture.edit")->isEnabled() && !action("texture.export")->isEnabled(), "Switching assets immediately invalidates stale texture actions");
		ok &= expect(select(textures, "wall.png") && waitUntil([&] { return action("texture.export")->isEnabled(); }), "Valid texture becomes exportable again");
		auto* textureFilter = shell.findChild<QLineEdit*>("textureFilter"); textureFilter->setText("no-matching-texture");
		ok &= expect(!action("texture.edit")->isEnabled() && !action("texture.export")->isEnabled() && !action("texture.reveal")->isEnabled(), "Filtered-out selection cannot dispatch asset actions");
		textureFilter->clear();
		textures->clearSelection();
		ok &= expect(!action("texture.edit")->isEnabled() && !action("texture.reveal")->isEnabled(), "Deselected current image cannot be edited or revealed");
		select(textures, "wall.png");
		for (const auto pair : {qMakePair("textures", "texture.reveal"), qMakePair("models", "model.reveal"), qMakePair("audio", "audio.reveal")}) {
			action((QByteArray("shell.mode.") + pair.first).constData())->trigger();
			auto* list = QByteArray(pair.first) == "textures" ? textures : QByteArray(pair.first) == "models" ? models : audio;
			list->setCurrentRow(0); const auto path = list->currentItem()->data(Qt::UserRole).toString();
			ok &= expect(waitUntil([&] { return action(pair.second)->isEnabled(); }), "Source reveal is available");
			action(pair.second)->trigger();
			auto entries = tests::PackageRows(shell.findChild<PackageEntryView*>("packageEntries"));
			ok &= expect(shell.findChild<QStackedWidget*>("modeStack")->currentIndex() == int(StudioMode::Packages) && entries->currentItem() &&
				entries->currentItem()->data(Qt::UserRole).toString() == path && StudioSettings().currentProjectPath() == project, "Asset handoff preserves package identity and project context");
			action("shell.navigateBack")->trigger();
			ok &= expect(list->isVisible() && list->currentItem() && list->currentItem()->data(Qt::UserRole).toString() == path,
				"Back returns to the originating asset and selection");
		}
		select(textures, "wall.png"); select(audio, "sound.wav"); select(models, "triangle.obj");
		ok &= expect(waitUntil([&] { return action("texture.edit")->isEnabled() && action("audio.edit")->isEnabled() && action("model.export")->isEnabled(); }), "All three asset kinds share usable selection states");
		for (auto* button : shell.findChildren<QAbstractButton*>()) {
			const auto id = button->property("commandId").toString();
			if (!id.startsWith("texture.") && !id.startsWith("model.") && !id.startsWith("audio.")) { continue; }
			if (auto* target = shell.findChild<QAction*>(id)) { ok &= expect(button->isEnabled() == target->isEnabled(), QStringLiteral("Button follows command state: ") + id); }
		}
		shell.openPathFromCommandLine(QDir(project).filePath("sample.map"));
		ok &= expect(waitUntil([&] { return shell.levelDocument().format != LevelMapFormat::Unknown; }), "Level uses the shared project context");
		shell.openPathFromCommandLine(QDir(project).filePath("code.qc"));
		shell.openPathFromCommandLine(QDir(project).filePath("sample.shader"));
		for (const auto* mode : {"workspace", "levels", "textures", "models", "audio", "packages", "code", "shaders", "build"}) {
			ok &= checkSurface(shell, mode, captures("loaded"), false);
		}
		auto* buildSections = shell.findChild<QTabWidget*>(QStringLiteral("buildSections"));
		for (int tab = 1; tab <= 2; ++tab) {
			buildSections->setCurrentIndex(tab); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
			shell.resize(1440, 900); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
			ok &= expect(shell.width() <= 1440, QStringLiteral("Build tab %1 fits the requested width").arg(tab));
			if (!captureRoot.isEmpty()) {
				QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
				ok &= expect(image.save(QDir(captures("loaded")).filePath(QStringLiteral("build-%1.png").arg(tab))), "Save build tab evidence");
			}
		}
		buildSections->setCurrentIndex(0);
		action("shell.commandPalette")->trigger();
		auto* filter = shell.findChild<QLineEdit*>("commandPaletteFilter"); filter->setText("Show Selected Asset in Package");
		auto* results = shell.findChild<QListWidget*>("commandPaletteList");
		bool audioResult = false;
		for (int row = 0; row < results->count(); ++row) {
			const auto* item = results->item(row);
			if (item->data(Qt::UserRole + 4).toString() == QStringLiteral("audio.reveal")) {
				audioResult = item->data(Qt::UserRole + 1).toString().contains("Audio", Qt::CaseInsensitive);
			}
		}
		ok &= expect(results->count() == 3 && audioResult, "Palette identifies otherwise identical commands by module");
		shell.findChild<QDialog*>("commandPalette")->hide();
		// Compressed streams have header previews rather than PCM waveforms.
		const auto fixture = qEnvironmentVariable("VIBESTUDIO_AUDIO_FIXTURES");
		if (!fixture.isEmpty()) {
			shell.openPathFromCommandLine(fixture); action("shell.mode.audio")->trigger();
			ok &= expect(select(audio, "mp3-vbr.mp3") && waitUntil([&] { return action("audio.edit")->isEnabled() && action("audio.export")->isEnabled(); }), "Compressed audio remains editable without a waveform");
		}
		const auto wad = QDir(root).filePath("repeated.wad"); ok &= put(wad, duplicateWad());
		shell.openPathFromCommandLine(wad); action("shell.mode.audio")->trigger();
		ok &= expect(audio->count() == 2, "Duplicate WAD fixture opens");
		if (audio->count() == 2) {
			audio->setCurrentRow(1); action("audio.reveal")->trigger();
			auto entries = tests::PackageRows(shell.findChild<PackageEntryView*>("packageEntries"));
			ok &= expect(entries->currentItem() && entries->currentItem()->data(Qt::UserRole + 7).toLongLong() == 1, "Package handoff preserves the exact repeated sound occurrence");
		}
	}
	if (scale == 200) { app.removeTranslator(&expansion); }
	return ok;
}
}
int main(int argc, char** argv)
{
	// Direct widget/service calls and Qt rendering only: no OS input, capture or audio playback.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto temporaryRoot = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	if (!QDir().mkpath(temporaryRoot)) { return 1; }
	QTemporaryDir temp(QDir(temporaryRoot).filePath("asset-workbench-XXXXXX")); if (!temp.isValid()) { return 1; }
	const auto project = temp.filePath("project"); QDir().mkpath(project);
	QImage image(32, 32, QImage::Format_ARGB32); image.fill(qRgb(64, 128, 192));
	bool ok = image.save(QDir(project).filePath("wall.png"));
	ok &= put(QDir(project).filePath("broken.png"), "invalid-image");
	ok &= put(QDir(project).filePath("triangle.obj"), "v 0 0 0\nv 16 0 0\nv 0 16 0\nf 1 2 3\n");
	ok &= put(QDir(project).filePath("code.qc"), "void() main = {};\n");
	ok &= put(QDir(project).filePath("sample.map"), "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 64\"\n}\n");
	ok &= put(QDir(project).filePath("sample.shader"), "textures/test\n{\n{\nmap wall.png\n}\n}\n");
	AudioClip clip; clip.sampleRate = 11025; clip.channels = 1; clip.samples.fill(0.25f, 11025);
	ok &= put(QDir(project).filePath("sound.wav"), encodeAudioWav(clip));
	ok &= workflow(app, temp.path(), 100); ok &= workflow(app, temp.path(), 200);
	std::cout << "Asset workbench cohesion " << (ok ? "passed" : "failed") << '\n'; return ok ? 0 : 1;
}
