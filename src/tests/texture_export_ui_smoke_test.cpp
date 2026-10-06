#include "app/texture_editor_dialog.h"
#include "app/texture_export_panel.h"
#include "core/package_staging.h"
#include "app/studio_theme.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message) { if (!condition) { std::cerr << message << '\n'; } return condition; }
bool settled(TextureEditorDialog* editor)
{
	QEventLoop loop; QTimer poll, timeout; timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (!editor->isBusy()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5); timeout.start(15000); if (editor->isBusy()) { loop.exec(); } return !editor->isBusy();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!QByteArray(context).startsWith("VibeStudioTexture")) { return {}; }
		return QStringLiteral("[%1 expanded text]").arg(QString::fromUtf8(source));
	}
};
}
int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	QDir root(temporary.path()); bool ok = true; QString error;
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("export-profile.ini"))); StudioSettings settings; settings.setTextureRecoveryEnabled(false); settings.sync();
	IdTechPaletteResolution palette; palette.palette.id = QStringLiteral("quake"); palette.palette.displayName = QStringLiteral("Synthetic palette"); palette.palette.sourceDescription = QStringLiteral("Test fixture");
	for (int i = 0; i < 256; ++i) { palette.palette.colors << qRgb(i, (i * 3) % 256, 255 - i); }
	QImage image(64, 64, QImage::Format_Indexed8); image.setColorTable(palette.palette.colors);
	for (int y = 0; y < 64; ++y) { for (int x = 0; x < 64; ++x) { image.setPixel(x, y, (x * 3 + y) % 224); } }
	int pass = 0;
	for (auto theme : {StudioTheme::Dark, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight}) {
		ExpandedTranslator expanded; const bool enlarged = pass > 0; if (enlarged) { app.installTranslator(&expanded); }
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, enlarged ? 200 : 100));
		auto* editor = new TextureEditorDialog; if (enlarged) { editor->setLayoutDirection(Qt::RightToLeft); editor->resize(1550, 1100); }
		ok &= expect(editor->setImage(image, QStringLiteral("wall.png"), &error), "open indexed fixture"); editor->setPaletteResolution(palette, false); editor->show(); app.processEvents();
		auto* sections = editor->findChild<QComboBox*>(QStringLiteral("texturePropertySection")); sections->setCurrentIndex(7);
		auto* panel = static_cast<TextureExportPanel*>(editor->findChild<QWidget*>(QStringLiteral("textureExportPanel")));
		auto* profile = editor->findChild<QComboBox*>(QStringLiteral("textureExportProfile"));
		auto* mip = editor->findChild<QComboBox*>(QStringLiteral("textureExportMipLevel"));
		auto* report = editor->findChild<QLabel*>(QStringLiteral("textureExportReport"));
		TextureExportOptions options; options.format = TextureExportFormat::Quake2Wal; options.name = QStringLiteral("custom/wall"); options.animationNext = QStringLiteral("custom/wall2"); options.surfaceFlags = 0xf0000001u; options.contentFlags = 32; options.surfaceValue = -17;
		editor->setExportOptions(options); app.processEvents(); const auto revision = editor->document().revision();
		ok &= expect(editor->hasUnsavedChanges() && editor->document().revision() == revision, "export setting changes dirty metadata without pixel history");
		for (const auto& descriptor : textureExportProfiles()) {
			profile->setCurrentIndex(profile->findData(descriptor.id)); editor->findChild<QLineEdit*>(QStringLiteral("textureExportName"))->setText(QStringLiteral("wall")); editor->previewExport();
			ok &= expect(settled(editor) && !panel->previewImage().isNull() && editor->document().revision() == revision, "each profile previews through the worker without modifying pixels");
			ok &= expect(mip->count() == (descriptor.mipmapped ? 4 : 1), "native preview exposes every encoded mip");
		}
		editor->setExportOptions(options); editor->previewExport(); ok &= expect(settled(editor), "WAL preview settles"); mip->setCurrentIndex(3);
		ok &= expect(panel->previewImage().size() == QSize(8, 8) && report->text().contains(QStringLiteral("Test fixture")), "mip selection and palette provenance remain inspectable");
		const auto project = root.filePath(QStringLiteral("project-%1.vtexture").arg(pass)); editor->saveProjectToPath(project, false);
		ok &= expect(settled(editor) && !editor->hasUnsavedChanges(), "project save records native options as clean metadata");
		TextureDocument saved; QJsonObject metadata; ok &= expect(readTextureProject(project, &saved, nullptr, &metadata, &error) && metadata.value(QStringLiteral("export")).toObject() == textureExportOptionsJson(options), "project file persists every export option");
		editor->openFromPath(project); TextureExportOptions restored;
		ok &= expect(settled(editor) && editor->exportOptions(&restored, &error) && textureExportOptionsJson(restored) == textureExportOptionsJson(options) && !editor->hasUnsavedChanges(), "reopen restores exact options without dirtying the project");
		const auto output = root.filePath(QStringLiteral("output-%1.wal").arg(pass)); bool completed = false;
		editor->saveExportToPath(output, false, [&]() { completed = true; });
		ok &= expect(settled(editor) && completed && QFileInfo::exists(output) && !editor->hasUnsavedChanges(), "two-phase native export reports publication completion and preserves project state");
		const auto bytes = read(output); const auto decoded = decodeIdTechImage(output, bytes, palette.palette);
		ok &= expect(decoded.decoded && decoded.surfaceFlags == options.surfaceFlags && decoded.surfaceValue == -17 && decoded.animationNextName == options.animationNext, "GUI writes native metadata into real output bytes");
		editor->saveExportToPath(output, false); ok &= expect(settled(editor) && read(output) == bytes, "GUI export refuses implicit overwrite");
		editor->openFromPath(output); ok &= expect(settled(editor) && editor->exportOptions(&restored) && textureExportOptionsJson(restored) == textureExportOptionsJson(options), "native image import retains WAL metadata for subsequent export");
		ok &= expect(editor->editDecodedImage(decoded, QStringLiteral("textures/source.wal"), palette) && editor->exportOptions(&restored)
			&& restored.surfaceFlags == options.surfaceFlags && editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->text() == QStringLiteral("textures/source.wal")
			&& !editor->hasUnsavedChanges(), "browser handoff retains native metadata and its original package path without dirtying the imported document");
		if (pass == 0) {
			PackageArchive archive; PackageStagingModel staging; archive.load(root.path()); staging.loadBaseArchive(archive);
			TextureEditorContext current{root.path(), {}, true, true, {}, QStringLiteral("revision-1"), {}};
			editor->context = [&]() { return current; };
			editor->handoff = [&](const TextureExportResult& result, const TextureExportOptions& settings, const QString& path, bool replace, bool, QString* problem) { return stageTextureExport(result, settings, path, &staging, replace, problem); };
			editor->refreshContext();
			ok &= expect(!editor->findChild<QAction*>(QStringLiteral("applyTexture"))->isEnabled(), "native WAL cannot be applied as a Quake III image");
			editor->stage(); current.packageTargetKey = QStringLiteral("revision-2");
			ok &= expect(settled(editor) && staging.operations().isEmpty(), "same-path package revision changes cancel an encoded handoff");
			editor->stage(); ok &= expect(settled(editor) && staging.operations().size() == 1, "GUI native staging uses the selected export profile");
			QByteArray staged; PackageStagingArchive(staging).readEntryBytes(QStringLiteral("textures/source.wal"), &staged, &error);
			ok &= expect(decodeIdTechImage(QStringLiteral("textures/source.wal"), staged, palette.palette).surfaceFlags == options.surfaceFlags, "staged native bytes retain WAL metadata");
			editor->previewExport(); ok &= expect(settled(editor) && !panel->previewImage().isNull(), "native staging preview available");
			current.packageTargetKey = QStringLiteral("revision-3"); editor->refreshContext();
			ok &= expect(panel->previewImage().isNull(), "staged package changes invalidate the encoded preview");
			editor->context = {}; editor->handoff = {}; editor->refreshContext();
		}
		editor->applyOperations({QJsonObject{{"op", "fill"}, {"x", 0}, {"y", 0}, {"color", "red"}}}); ok &= expect(settled(editor) && panel->previewImage().isNull(), "pixel edits invalidate encoded previews");
		editor->saveExportToPath(output, true); ok &= expect(settled(editor) && editor->hasUnsavedChanges(), "image export never clears unsaved project content");
		editor->previewExport(); ok &= expect(settled(editor) && !panel->previewImage().isNull(), "edited pixels revalidate"); editor->setPaletteResolution(palette);
		ok &= expect(panel->previewImage().isNull(), "palette changes invalidate prior encoded pixels");
		const auto cancelled = root.filePath(QStringLiteral("cancelled-%1.wal").arg(pass)); editor->saveExportToPath(cancelled, false); editor->cancelPending();
		ok &= expect(settled(editor) && !QFileInfo::exists(cancelled), "cancellation before publication creates no output");
		editor->findChild<QLineEdit*>(QStringLiteral("textureExportFlags"))->setText(QStringLiteral("0xffffffff"));
		ok &= expect(editor->exportOptions(&restored) && restored.surfaceFlags == 0xffffffffu, "hexadecimal flags retain every bit");
		editor->findChild<QLineEdit*>(QStringLiteral("textureExportFlags"))->setText(QStringLiteral("4294967296"));
		ok &= expect(!editor->exportOptions(&restored, &error), "overflowing flags do not silently truncate"); editor->setExportOptions(options);
		for (auto selected : {TextureExportFormat::Quake2Wal, TextureExportFormat::DoomPatch}) {
			options.format = selected; editor->setExportOptions(options); editor->previewExport(); ok &= expect(settled(editor), "native metadata controls remain valid"); app.processEvents();
			auto* scroll = qobject_cast<QScrollArea*>(editor->findChild<QTabWidget*>(QStringLiteral("textureProperties"))->widget(7));
			ok &= expect(scroll && scroll->horizontalScrollBar()->maximum() == 0, "export inspector fits scaled and expanded RTL layouts without horizontal clipping");
			for (auto* field : panel->findChildren<QWidget*>()) {
				if (!field->isVisible() || !(qobject_cast<QComboBox*>(field) || qobject_cast<QLineEdit*>(field) || qobject_cast<QCheckBox*>(field) || qobject_cast<QPushButton*>(field) || qobject_cast<QSpinBox*>(field)) || field->objectName() == QStringLiteral("qt_spinbox_lineedit")) { continue; }
				auto* accessible = QAccessible::queryAccessibleInterface(field);
				ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && accessible->state().focusable, "export controls expose accessible names and keyboard focus");
			}
			if (scroll && !qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_ROOT")) {
				for (bool bottom : {false, true}) {
					scroll->verticalScrollBar()->setValue(bottom ? scroll->verticalScrollBar()->maximum() : 0); app.processEvents();
					QImage capture(editor->size(), QImage::Format_ARGB32_Premultiplied); capture.fill(Qt::transparent); editor->render(&capture);
					ok &= expect(capture.save(QDir(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT")).filePath(QStringLiteral("texture-export-%1-%2-%3.png").arg(pass).arg(textureExportFormatId(selected), bottom ? QStringLiteral("bottom") : QStringLiteral("top")))), "render export controls directly from Qt widgets");
				}
			}
		}
		delete editor; if (enlarged) { app.removeTranslator(&expanded); } ++pass;
	}
	{
		TextureEditorDialog editor;
		QImage large(2048, 2048, QImage::Format_ARGB32); large.fill(qRgba(60, 90, 130, 180));
		ok &= editor.setImage(large, QStringLiteral("large.png"), &error);
		QJsonArray operations;
		for (int i = 0; i < 7; ++i) {
			operations.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-duplicate")}});
			operations.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-properties")}, {QStringLiteral("blend"), QStringLiteral("multiply")}, {QStringLiteral("opacity"), 85}});
		}
		QTimer heartbeat; heartbeat.setInterval(5); QElapsedTimer clock; clock.start();
		int ticks = 0; qint64 previous = 0, longestGap = 0;
		QObject::connect(&heartbeat, &QTimer::timeout, &editor, [&]() { const auto now = clock.elapsed(); longestGap = std::max(longestGap, now - previous); previous = now; ++ticks; });
		heartbeat.start(); editor.applyOperations(operations);
		ok &= expect(editor.isBusy() && settled(&editor) && editor.document().layers().size() == 8 && ticks > 1,
			"maximum layer workload keeps the UI event loop active while compositing on a worker");
		const auto revision = editor.document().revision();
		editor.undo(); ok &= expect(editor.isBusy() && settled(&editor) && editor.document().revision() != revision, "large undo prepares its composite asynchronously");
		editor.redo(); ok &= expect(editor.isBusy() && settled(&editor) && editor.document().revision() == revision, "large redo prepares its composite asynchronously");
		editor.undo(); editor.cancelPending();
		ok &= expect(settled(&editor) && editor.document().revision() == revision, "cancelling large undo retains the current document and history");
		heartbeat.stop();
		std::cout << "maximum_layer_ui_heartbeat_ticks=" << ticks << " maximum_gap_ms=" << longestGap << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
