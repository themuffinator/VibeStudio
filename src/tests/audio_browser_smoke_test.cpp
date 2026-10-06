#include "app/application_shell.h"
#include "app/asset_views.h"
#include "app/audio_browser_worker.h"
#include "app/audio_editor_dialog.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"
#include "core/audio_export.h"
#include "tests/fake_audio_playback.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QtEndian>

#include <atomic>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool waitUntil(const std::function<bool()>& done, int milliseconds = 15000)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!done() && elapsed.elapsed() < milliseconds) {
		QEventLoop loop; QTimer::singleShot(5, &loop, &QEventLoop::quit); loop.exec();
	}
	return done();
}
bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray wav(float value, int frames = 44100)
{
	AudioClip clip; clip.sampleRate = 44100; clip.channels = 1; clip.samples.fill(value, frames);
	AudioWavOptions options; options.format = AudioWavFormat::Float32;
	return encodeAudioWav(clip, options);
}
class Reader final : public PackageArchiveReader {
public:
	QVector<PackageEntry> metadata;
	QVector<QByteArray> payloads;
	mutable std::atomic_int reads = 0, cancelledReads = 0;
	mutable std::atomic_bool entered = false, hold = false;
	bool badChecksum = false;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Wad; }
	QString sourcePath() const override { return {}; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return metadata; }
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { return false; }
	bool readEntryAt(qsizetype index, QByteArray* out, QString*, qint64 cap) const override
	{
		++reads; *out = cap < 0 ? payloads.at(index) : payloads.at(index).first(std::min<qsizetype>(cap, payloads.at(index).size())); return true;
	}
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
	                   const std::function<bool()>& cancelled) const override
	{
		++reads; entered = true;
		while (hold) {
			if (cancelled && cancelled()) { ++cancelledReads; return false; }
			QThread::msleep(1);
		}
		const auto& bytes = payloads.at(index);
		for (qsizetype offset = 0; offset < bytes.size(); offset += 65536) {
			if (cancelled && cancelled()) { ++cancelledReads; return false; }
			if (!sink(QByteArrayView(bytes).sliced(offset, std::min<qsizetype>(65536, bytes.size() - offset)))) { return false; }
		}
		if (badChecksum) { *error = QStringLiteral("fixture checksum failure"); return false; }
		return true;
	}
	void add(const QString& name, const QByteArray& bytes)
	{
		PackageEntry entry; entry.virtualPath = name; entry.sizeBytes = bytes.size();
		entry.sourceOrdinal = metadata.size(); metadata << entry; payloads << bytes;
	}
};
bool mediaLifecycle()
{
	auto backend = std::make_unique<FakeAudioPlaybackBackend>(); auto* fake = backend.get();
	AudioPlayback transport(nullptr, std::move(backend));
	bool ok = expect(transport.startMedia("native-compressed", QStringLiteral("https://host/clip.mp3"), 0, 1200) &&
	                 fake->fileName == QStringLiteral("audition.mp3") && fake->lastSeek == 1200 &&
	                 transport.positionMilliseconds() == 1200, "browser starts unknown-duration media at requested ms with a safe hint");
	const auto session = fake->currentSession;
	emit fake->ready(session, true);
	ok &= expect(fake->seeks == 1 && fake->plays == 1, "repeated ready cannot repeat the initial seek");
	emit fake->durationChanged(session, 30000);
	ok &= expect(transport.durationMilliseconds() == 30000, "backend duration replaces unknown header duration");
	transport.pause();
	ok &= expect(transport.seekToMilliseconds(2200) && fake->lastSeek == 2200 &&
	             transport.state() == AudioPlayback::State::Paused, "browser seek preserves pause");
	emit fake->durationChanged(session - 1, 3);
	ok &= expect(transport.durationMilliseconds() == 30000, "old duration callbacks cannot change current media");
	ok &= expect(transport.startMedia("bytes", QStringLiteral("clip.flac"), std::numeric_limits<qint64>::max()),
	             "native media duration does not inherit the editable frame limit");
	emit fake->positionChanged(fake->currentSession, std::numeric_limits<qint64>::max() - 1);
	ok &= expect(transport.positionMilliseconds() == std::numeric_limits<qint64>::max() - 1, "large media time avoids multiplication overflow");
	transport.stop(); fake->seekable = false;
	ok &= expect(!transport.startMedia("bytes", QStringLiteral("clip.ogg"), 5000, 10) &&
	             !transport.errorString().isEmpty(), "unseekable media explains a nonzero-start failure");
	return ok;
}
bool workers()
{
	bool ok = true;
	auto reader = std::make_shared<Reader>();
	reader->add(QStringLiteral("same.wav"), wav(0.12500001f)); reader->add(QStringLiteral("same.wav"), wav(-0.5f));
	AudioBrowserWorker worker;
	QVector<AudioBrowserResult> results;
	worker.completed = [&](const AudioBrowserResult& result) { results << result; };
	const auto request = [&](AudioBrowserRequest::Kind kind, qsizetype index, quint64 revision) {
		worker.request({kind, reader, revision, index, reader->metadata.at(index).virtualPath});
	};
	request(AudioBrowserRequest::Kind::Preview, 1, 1);
	ok &= expect(waitUntil([&] { return !worker.busy(); }) && results.size() == 1 && results.last().preview.audioPeaks.valid &&
	             results.last().entryIndex == 1 && results.last().preview.audioPeaks.peaks.first() == -0.5f,
	             "preview uses the selected repeated occurrence");
	results.clear();
	request(AudioBrowserRequest::Kind::Audition, 0, 2);
	ok &= expect(waitUntil([&] { return !worker.busy(); }) && results.size() == 1 &&
	             results.last().source.bytes == reader->payloads[0], "worker preserves original float WAV bits");
	results.clear(); reader->hold = true; reader->entered = false;
	request(AudioBrowserRequest::Kind::Audition, 0, 3);
	ok &= expect(waitUntil([&] { return reader->entered.load(); }), "controlled slow read starts");
	request(AudioBrowserRequest::Kind::Preview, 0, 4);
	request(AudioBrowserRequest::Kind::Audition, 1, 5);
	reader->hold = false;
	ok &= expect(waitUntil([&] { return !worker.busy(); }) && results.size() == 1 && results.last().revision == 5 &&
	             results.last().source.bytes == reader->payloads[1], "only the latest pending request publishes");
	results.clear(); reader->hold = true; reader->entered = false;
	request(AudioBrowserRequest::Kind::Audition, 0, 6);
	ok &= expect(waitUntil([&] { return reader->entered.load(); }), "cancellation fixture starts");
	const int cancellationsBefore = reader->cancelledReads;
	worker.cancel();
	ok &= expect(!worker.busy() && waitUntil([&] { return reader->cancelledReads.load() > cancellationsBefore; }) && results.isEmpty(),
	             "Stop invalidates work immediately and interrupts a streaming read");
	reader->hold = false; reader->badChecksum = true;
	request(AudioBrowserRequest::Kind::Audition, 0, 7);
	ok &= expect(waitUntil([&] { return !worker.busy(); }) && results.size() == 1 && !results.last().error.isEmpty() &&
	             results.last().source.bytes.isEmpty(), "checksum failure discards even fully received media");
	reader->badChecksum = false; results.clear();
	const int reads = reader->reads;
	reader->metadata[0].sizeBytes = AudioInputByteLimit + 1;
	request(AudioBrowserRequest::Kind::Audition, 0, 8);
	ok &= expect(waitUntil([&] { return !worker.busy(); }) && results.size() == 1 && !results.last().error.isEmpty() &&
	             reader->reads == reads, "oversized audition is rejected before any read");
	results.clear();
	request(AudioBrowserRequest::Kind::Preview, 0, 9);
	ok &= expect(waitUntil([&] { return !worker.busy(); }) && results.size() == 1 && results.last().preview.truncated &&
	             !results.last().preview.audioPeaks.valid, "large preview samples bounded header metadata without showing a partial waveform");
	reader->metadata[0].sizeBytes = 4; results.clear();
	request(AudioBrowserRequest::Kind::Audition, 0, 10);
	ok &= expect(waitUntil([&] { return !worker.busy(); }) && results.size() == 1 &&
	             !results.last().error.isEmpty() && results.last().source.bytes.isEmpty(), "reader overrun is refused");
	// Exercise the actual preview cap while the GUI timer continues ticking.
	results.clear(); reader = std::make_shared<Reader>();
	reader->add(QStringLiteral("large.wav"), wav(0.25f, 16 * 1024 * 1024 - 256));
	int ticks = 0; QTimer heartbeat; heartbeat.setInterval(5);
	QObject::connect(&heartbeat, &QTimer::timeout, &worker, [&] { ++ticks; });
	QElapsedTimer elapsed; elapsed.start(); heartbeat.start();
	request(AudioBrowserRequest::Kind::Preview, 0, 11);
	ok &= expect(waitUntil([&] { return !worker.busy(); }, 30000) && results.size() == 1 &&
	             results.last().preview.audioPeaks.valid && ticks > 0, "64 MiB preview keeps the GUI event loop responsive");
	heartbeat.stop();
	std::cout << "Preview benchmark: " << elapsed.elapsed() << " ms, " << ticks << " GUI timer ticks, "
	          << reader->payloads[0].size() << " source bytes\n";
	return ok;
}
QByteArray duplicateWad()
{
	QByteArray bytes("PWAD", 4); bytes.resize(12);
	QVector<QByteArray> sounds;
	for (const char value : {char(64), char(192)}) {
		QByteArray sound(108, value); qToLittleEndian<quint16>(3, sound.data());
		qToLittleEndian<quint16>(11025, sound.data() + 2); qToLittleEndian<quint32>(100, sound.data() + 4);
		sounds << sound; bytes += sound;
	}
	const int directory = bytes.size();
	for (int i = 0; i < sounds.size(); ++i) {
		QByteArray entry(16, '\0'); qToLittleEndian<quint32>(12 + i * 108, entry.data());
		qToLittleEndian<quint32>(108, entry.data() + 4); entry.replace(8, 6, "DSSAME"); bytes += entry;
	}
	qToLittleEndian<quint32>(2, bytes.data() + 4); qToLittleEndian<quint32>(directory, bytes.data() + 8); return bytes;
}
class ExpandedTranslator final : public QTranslator {
public:
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const QString value = QString::fromUtf8(source);
		if (value.contains(QStringLiteral("Sound")) || value.contains(QStringLiteral("audio")) ||
		    value == QStringLiteral("&Position") || value == QStringLiteral("Volume") || value == QStringLiteral("Playing")) {
			return value + QStringLiteral(" · ") + value;
		}
		return {};
	}
};
bool shellWorkflow(QApplication& app, const QString& root, int scale)
{
	bool ok = true;
	const QString folder = QDir(root).filePath(QStringLiteral("sounds")); QDir().mkpath(folder);
	const QByteArray first = wav(0.12500001f), second = wav(-0.5f);
	ok &= writeFile(QDir(folder).filePath(QStringLiteral("first.wav")), first);
	ok &= writeFile(QDir(folder).filePath(QStringLiteral("second.wav")), second);
	auto backend = std::make_unique<FakeAudioPlaybackBackend>(); auto* fake = backend.get();
	auto preferences = StudioSettings().accessibilityPreferences();
	preferences.textScalePercent = scale;
	preferences.theme = scale == 100 ? StudioTheme::HighContrastDark : StudioTheme::HighContrastLight;
	StudioSettings().setAccessibilityPreferences(preferences);
	ExpandedTranslator expanded;
	if (scale == 200) { app.installTranslator(&expanded); }
	ApplicationShell shell(nullptr, std::move(backend));
	if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
	shell.resize(1500, 920); shell.show();
	shell.openPathFromCommandLine(folder);
	shell.findChild<QAction*>(QStringLiteral("shell.mode.audio"))->trigger();
	auto* list = shell.findChild<QListWidget*>(QStringLiteral("audioEntries"));
	auto* play = shell.findChild<QToolButton*>(QStringLiteral("audioPlay"))->defaultAction();
	auto* stop = shell.findChild<QToolButton*>(QStringLiteral("audioStop"))->defaultAction();
	auto* transport = shell.findChild<AudioPlayback*>(QStringLiteral("audioBrowserPlayback"));
	auto* waveform = shell.findChild<WaveformView*>();
	ok &= expect(waitUntil([&] { return list->count() == 2 && play->isEnabled(); }), "asynchronous browser preview becomes playable");
	if (!play->isEnabled()) { return false; }
	auto* previewState = dynamic_cast<LoadingPane*>(shell.findChild<QFrame*>(QStringLiteral("audioBrowserState")));
	list->setCurrentRow(1);
	ok &= expect(previewState && previewState->state() == OperationState::Loading, "new selection exposes preview loading");
	list->clearSelection();
	ok &= expect(previewState && previewState->state() == OperationState::Idle && !play->isEnabled(),
	             "clearing selection cancels preview and leaves an idle state even when the current row remains");
	shell.findChild<QPushButton*>(QStringLiteral("openAudioEditor"))->click();
	ok &= expect(!shell.findChild<AudioEditorDialog*>(), "Edit Sound requires an actual selected row");
	auto* exportButton = shell.findChild<QPushButton*>(QStringLiteral("audioBrowserExportWav"));
	auto* exportCommand = shell.findChild<QAction*>(QStringLiteral("audio.export"));
	ok &= expect(exportButton && exportCommand && !exportButton->isEnabled() && !exportCommand->isEnabled(),
	             "browser export button and shared command require an actual selected row");
	// Exercise the defensive service guard directly, without bypassing disabled
	// UI controls through keyboard or mouse events.
	if (exportCommand) { emit exportCommand->triggered(false); }
	ok &= expect(!shell.findChild<QAction*>(QStringLiteral("audio.export"))->isEnabled() &&
	             !shell.findChild<QPushButton*>(QStringLiteral("audioBrowserExportWav"))->isEnabled(),
	             "browser export and its command are disabled without an actual selected row");
	list->setCurrentRow(-1);
	ok &= expect(previewState && previewState->state() == OperationState::Idle && !play->isEnabled(),
	             "removing the current row keeps the empty selection idle");
	list->setCurrentRow(0);
	ok &= expect(waitUntil([&] { return play->isEnabled(); }), "preview resumes after selecting a sound again");
	ok &= expect(exportButton && exportCommand && exportButton->isEnabled() && exportCommand->isEnabled(),
	             "valid selection re-enables browser export and its shared command");
	list->setCurrentRow(0); play->trigger();
	ok &= expect(stop->isEnabled() && !play->isEnabled(), "preparation exposes Stop before the worker starts");
	stop->trigger();
	QApplication::processEvents();
	ok &= expect(fake->opens == 0, "Stop suppresses pending autoplay");
	play->trigger();
	ok &= expect(waitUntil([&] { return transport->state() == AudioPlayback::State::Playing; }) && fake->bytes == first,
	             "browser plays original staged float WAV through shared transport");
	if (!transport->active()) {
		std::cerr << "Browser failed to start: " << shell.statusBar()->currentMessage().toStdString()
		          << "; row=" << list->currentRow() << "; index=" << list->currentItem()->data(Qt::UserRole + 6).toLongLong()
		          << "; opens=" << fake->opens << '\n';
		return false;
	}
	auto* seek = shell.findChild<QSlider*>(QStringLiteral("audioBrowserSeek"));
	seek->setValue(250);
	ok &= expect(fake->lastSeek == 250 && seek->value() == 250 && seek->focusPolicy() != Qt::NoFocus &&
	             !seek->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(seek),
	             "browser exposes a named standard seek control independent of waveform availability");
	const auto captureRoot = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (!captureRoot.isEmpty()) {
		QDir().mkpath(captureRoot); QApplication::processEvents();
		QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
		ok &= image.save(QDir(captureRoot).filePath(QStringLiteral("audio-browser-%1.png").arg(scale)));
	}
	if (scale == 200) { stop->trigger(); shell.close(); app.removeTranslator(&expanded); return ok; }
	const auto firstSession = fake->currentSession;
	play->trigger(); emit waveform->seekRequested(500);
	ok &= expect(transport->state() == AudioPlayback::State::Paused && fake->lastSeek == 500,
	             "browser pause and seek preserve the session");
	play->trigger();
	list->setCurrentRow(1);
	ok &= expect(!transport->active() && !play->isEnabled(), "new selection stops old playback while preview loads");
	ok &= expect(waitUntil([&] { return play->isEnabled(); }), "second preview settles");
	play->trigger();
	ok &= expect(waitUntil([&] { return transport->state() == AudioPlayback::State::Playing; }) && fake->bytes == second,
	             "second sound starts");
	emit fake->failed(firstSession, QStringLiteral("late failure"));
	QApplication::processEvents();
	ok &= expect(transport->state() == AudioPlayback::State::Playing && fake->bytes == second,
	             "late old-source error cannot stop a newer sound");
	emit fake->failed(fake->currentSession, QStringLiteral("device disappeared"));
	ok &= expect(transport->state() == AudioPlayback::State::Error && shell.statusBar()->currentMessage().contains(QStringLiteral("device disappeared")),
	             "current device error is visible and retryable");
	play->trigger();
	ok &= expect(waitUntil([&] { return transport->state() == AudioPlayback::State::Playing; }), "browser retries a failed device");
	stop->trigger(); play->trigger();
	shell.findChild<QPushButton*>(QStringLiteral("openAudioEditor"))->click();
	auto* editor = shell.findChild<AudioEditorDialog*>();
	ok &= expect(editor && waitUntil([&] { return !editor->isBusy(); }) && !transport->active(),
	             "opening the editor cancels pending browser playback");
	if (editor) {
		play->trigger();
		ok &= expect(waitUntil([&] { return transport->state() == AudioPlayback::State::Playing; }), "browser can audition while the editor exists");
		editor->beforePlayback();
		ok &= expect(!transport->active(), "editor audition cancels the browser transport");
		AudioClip staged; staged.channels = 1; staged.sampleRate = 44100; staged.samples.fill(0.25f, 44100);
		const auto stagedBytes = encodeAudioWav(staged);
		QString stageError;
		play->trigger();
		ok &= expect(waitUntil([&] { return transport->active(); }) &&
		             editor->handoff(stagedBytes, QStringLiteral("second.wav"), true, &stageError) && !transport->active(),
		             "same-path package replacement invalidates active audition");
		ok &= expect(waitUntil([&] { return play->isEnabled(); }), "replacement preview completes");
		play->trigger();
		ok &= expect(waitUntil([&] { return transport->state() == AudioPlayback::State::Playing; }) && fake->bytes == stagedBytes,
		             "cached browser snapshot advances to the replacement revision");
		shell.findChild<QAction*>(QStringLiteral("package.unstageLast"))->trigger();
		ok &= expect(waitUntil([&] { return play->isEnabled(); }), "undo replacement refreshes the browser snapshot");
		play->trigger();
		ok &= expect(waitUntil([&] { return transport->state() == AudioPlayback::State::Playing; }) && fake->bytes == second,
		             "package undo restores original audition bytes");
		stop->trigger();
		editor->hide();
	}
	const QString wadPath = QDir(root).filePath(QStringLiteral("repeated.wad"));
	ok &= writeFile(wadPath, duplicateWad());
	shell.openPathFromCommandLine(wadPath);
	ok &= expect(waitUntil([&] { return list->count() == 2 && list->item(0)->data(Qt::UserRole).toString() == QStringLiteral("DSSAME"); }),
	             "repeated WAD sound rows remain selectable");
	if (list->count() == 2) {
		ok &= expect(list->item(0)->text() != list->item(1)->text(), "repeated sounds have distinct visible entry labels");
		list->setCurrentRow(1);
		ok &= expect(waitUntil([&] { return play->isEnabled(); }), "second WAD occurrence previews");
		play->trigger();
		ok &= expect(waitUntil([&] { return transport->state() == AudioPlayback::State::Playing; }) &&
		             decodeAudioClip(QStringLiteral("selected.wav"), fake->bytes).clip.samples.first() == 0.5f,
		             "audition selects the second repeated WAD occurrence");
		shell.findChild<QPushButton*>(QStringLiteral("openAudioEditor"))->click();
		ok &= expect(editor && waitUntil([&] { return !editor->isBusy(); }) && editor->clip().samples.first() == 0.5f,
		             "Edit Sound opens the same repeated occurrence");
		if (editor) { editor->hide(); }
	}
	PackageArchive archive; QString error; ok &= archive.load(wadPath, &error);
	const QString exported = QDir(root).filePath(QStringLiteral("occurrence.wav"));
	const auto report = exportPackageAudioToWavAt(archive, 1, exported);
	QFile output(exported); ok &= expect(output.open(QIODevice::ReadOnly), "open exported occurrence");
	ok &= expect(report.succeeded() && decodeAudioClip(QStringLiteral("export.wav"), output.readAll()).clip.samples.first() == 0.5f,
	             "shared export service preserves exact occurrence");
	ok &= expect(!exportPackageAudioToWav(archive, QStringLiteral("DSSAME"), QDir(root).filePath(QStringLiteral("ambiguous.wav"))).succeeded(),
	             "path-only export refuses ambiguous occurrences");
	ok &= expect(!exportPackageAudioToWavAt(archive, 1, wadPath).succeeded(), "occurrence export still protects the source");
	PackageArchive folderArchive;
	ok &= folderArchive.load(folder, &error);
	ok &= expect(!exportPackageAudioToWav(folderArchive, QStringLiteral("first.wav"),
	             QDir(folder).filePath(QStringLiteral("second.wav")), false, true).succeeded(),
	             "WAV export cannot overwrite another existing folder input");
	shell.close();
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	// Direct Qt API calls and fake media only; no input injection or device output.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath()); QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-browser-XXXXXX")));
	if (!temporary.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	qputenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT", temporary.filePath(QStringLiteral("recovery")).toUtf8());
	StudioSettings().setAudioRecoveryEnabled(false);
	const bool media = mediaLifecycle(), worker = workers(), ui = shellWorkflow(app, temporary.path(), 100);
	const bool layout = shellWorkflow(app, temporary.path(), 200);
	std::cout << "Audio browser checks " << (media && worker && ui && layout ? "passed" : "failed") << '\n';
	return media && worker && ui && layout ? EXIT_SUCCESS : EXIT_FAILURE;
}
