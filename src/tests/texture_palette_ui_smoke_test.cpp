#include "app/texture_editor_dialog.h"
#include "app/texture_preview_worker.h"
#include "core/package_archive.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer clock; clock.start();
	while (!ready() && clock.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return ready();
}
class PaletteReader final : public PackageArchiveReader {
public:
	mutable std::atomic_bool reading = false, release = false, onGui = false;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Pak; }
	QString sourcePath() const override { return QStringLiteral("synthetic-palettes"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override {
		PackageEntry doom, quake; doom.virtualPath = QStringLiteral("PLAYPAL"); doom.sizeBytes = 768;
		quake = doom; quake.virtualPath = QStringLiteral("gfx/palette.lmp"); return {doom, quake};
	}
	bool readEntryBytes(const QString&, QByteArray* bytes, QString*, qint64) const override {
		onGui = QThread::currentThread() == QCoreApplication::instance()->thread(); reading = true;
		QElapsedTimer clock; clock.start();
		while (!release && clock.elapsed() < 5000) { QThread::msleep(1); }
		*bytes = QByteArray(768, '\0'); (*bytes)[0] = char(230); (*bytes)[2] = char(95); return true;
	}
};
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv); bool ok = true;
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("profile.ini")));
	StudioSettings settings; settings.setTextureRecoveryEnabled(false); settings.sync();
	TextureEditorDialog editor;
	QImage pixels(32, 32, QImage::Format_ARGB32); pixels.fill(QColor(30, 60, 90, 120));
	ok &= expect(editor.setImage(pixels, QStringLiteral("authored.png")), "create the palette refresh document");
	IdTechPaletteResolution initial; initial.palette = generatedIdTechPalette(QStringLiteral("quake")); editor.setPaletteResolution(initial, false);
	const auto revision = editor.document().revision();
	auto reader = std::make_shared<PaletteReader>();
	TexturePreviewSource source; source.archive = reader; source.revision = QStringLiteral("staged-a"); source.paletteId = QStringLiteral("quake");
	bool capturedOnGui = true;
	editor.paletteSource = [&] { capturedOnGui &= QThread::currentThread() == app.thread(); return source; };
	auto* choice = editor.findChild<QComboBox*>(QStringLiteral("textureEditorPalette"));
	int ticks = 0; QTimer heartbeat; heartbeat.setInterval(5);
	QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; }); heartbeat.start();
	choice->setCurrentIndex(choice->findData(QStringLiteral("doom")));
	ok &= expect(until([&] { return reader->reading && ticks >= 5; }) && editor.isBusy(), "palette loading keeps the event loop responsive");
	reader->release = true;
	ok &= expect(until([&] { return !editor.isBusy(); }) && capturedOnGui && !reader->onGui && editor.paletteResolution().fromPackage &&
		editor.paletteResolution().palette.colors.first() == qRgb(230, 0, 95) && editor.document().image() == pixels &&
		editor.document().revision() == revision && editor.hasUnsavedChanges(), "resolution runs on the worker and changes only palette metadata");
	editor.saveProjectToPath(temporary.filePath(QStringLiteral("saved.vtexture")), false);
	ok &= expect(until([&] { return !editor.isBusy(); }) && !editor.hasUnsavedChanges(), "save the resolved palette as project metadata");
	for (bool cancel : {true, false}) {
		reader->reading = false; reader->release = false;
		choice->setCurrentIndex(choice->findData(QStringLiteral("quake")));
		ok &= expect(until([&] { return reader->reading.load(); }), "start another explicit palette source read");
		if (cancel) { editor.cancelPending(); }
		else { source.revision = QStringLiteral("staged-b"); }
		reader->release = true;
		ok &= expect(until([&] { return !editor.isBusy(); }) && choice->currentData().toString() == QStringLiteral("doom") &&
			editor.paletteResolution().palette.id == QStringLiteral("doom") && editor.document().image() == pixels && !editor.hasUnsavedChanges(),
			"cancelled and stale source results retain the saved palette, choice, pixels and clean state");
	}
	editor.refreshPaletteSource(QStringLiteral("quake"));
	ok &= expect(until([&] { return !editor.isBusy(); }) && editor.paletteResolution().palette.id == QStringLiteral("quake") &&
		editor.paletteResolution().fromPackage && editor.hasUnsavedChanges(), "explicit retry resolves the current source revision");
	std::cout << "Palette worker heartbeat ticks: " << ticks << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
