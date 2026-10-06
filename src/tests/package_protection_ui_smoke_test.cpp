#include "app/package_operation_dialog.h"
#include "app/studio_theme.h"
#include "core/package_import_store.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTranslator>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}

class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "VibeStudioPackageDialog" && QByteArray(context) != "VibeStudioPackageStaging") { return {}; }
		return QStringLiteral("[%1 — expanded translation]").arg(QString::fromUtf8(source));
	}
};

struct Gate {
	std::atomic_bool reached {false}, onWorker {false}, expired {false};
	std::mutex mutex;
	std::condition_variable condition;
	bool released = false;
	void pause()
	{
		if (reached.exchange(true)) { return; }
		onWorker = QThread::currentThread() != qApp->thread();
		std::unique_lock lock(mutex);
		expired = !condition.wait_for(lock, std::chrono::seconds(5), [&] { return released; });
	}
	void release()
	{
		{ std::lock_guard lock(mutex); released = true; }
		condition.notify_all();
	}
};

bool inspect(QDialog* dialog, bool metadata, const QString& capture)
{
	auto* summary = dialog->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
	auto* progress = dialog->findChild<QProgressBar*>();
	auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
	bool ok = expect(summary && progress && cancel, "Operation exposes summary, progress and cancellation.");
	if (!ok) { return false; }
	if (dialog->layout()) { dialog->layout()->activate(); }
	ok &= expect(progress->height() >= progress->fontMetrics().height() + 4,
		"Progress text fits its current font, including after live scale changes.");
	ok &= expect(summary->wordWrap() && summary->height() >= summary->heightForWidth(summary->width()),
		"The summary fits at the selected scale and translation length.");
	ok &= expect(!dialog->accessibleName().isEmpty() && !progress->accessibleName().isEmpty()
		&& cancel->isEnabled()
		&& cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty(),
		"Progress and cancellation expose accessible names, state and keyboard focus policy.");
	ok &= expect(progress->accessibleDescription().startsWith(progress->format()), "Accessible progress starts with the visible phase.");
	if (metadata) {
		ok &= expect(summary->text().contains("Records checked") && progress->format().contains("Records checked")
			&& !progress->format().contains("MiB"), "Protection matching displays record units, including with an unknown total.");
	} else {
		const auto compact = dialog->locale().toString(64) + " / " + dialog->locale().toString(128) + " KiB";
		ok &= expect(!summary->text().contains("Records checked") && progress->format().contains(compact)
			&& progress->maximum() == 1000 && progress->value() == 500,
			"A half-read 128 KiB entry reports 64 / 128 KiB and half progress, without rounded false completion.");
		ok &= expect(progress->toolTip().contains(dialog->locale().toString(quint64(65536)))
			&& progress->toolTip().contains(dialog->locale().toString(quint64(131072)))
			&& progress->accessibleDescription().endsWith(progress->toolTip()) && !progress->toolTip().isEmpty(),
			"Exact localized byte counts remain in the tooltip and accessible description.");
	}
	const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (!captures.isEmpty()) {
		QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
		ok &= expect(dialog->size() == image.size(), "Rendering must preserve the captured dialog extent.");
		const QRect cancelBounds(cancel->mapTo(dialog, QPoint()), cancel->size());
		ok &= expect(image.rect().contains(cancelBounds), "The complete cancellation control must lie within the rendered image.");
		std::cout << capture.toStdString() << ": canvas " << image.width() << "x" << image.height() << ", dialog " << dialog->width() << "x" << dialog->height()
			<< ", cancel bottom " << cancelBounds.bottom() << std::endl;
		ok &= expect(image.save(QDir(captures).filePath(capture + ".png")), "Save widget-rendered protection progress evidence.");
	}
	return ok;
}

bool exercise(const QDir& root, const PackageStagingModel& plan, const QString& operation, int scale)
{
	const bool writer = operation == "export", copying = operation == "copy", continueReading = operation == "resume";
	bool ok = true, checked = false, payloadChecked = false, finished = false;
	const int continuedScale = scale == 100 ? 200 : 100;
	int previousFontHeight = 0;
	Gate matching, payload;
	const auto release = qScopeGuard([&] { matching.release(); payload.release(); });
	const auto label = QStringLiteral("protection-%1-%2").arg(operation).arg(scale);
	const auto destination = root.filePath(label + (writer ? ".pk3" : QString()));
	QDialog* ownedDialog = nullptr;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&] {
		auto* dialog = ownedDialog ? ownedDialog : qobject_cast<QDialog*>(QApplication::activeModalWidget());
		if (!dialog) { return; }
		auto* details = dialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"));
		if (matching.reached && !checked && details && details->toPlainText().contains("Checking package source protections")) {
			checked = true; ok &= inspect(dialog, true, label);
			if (!continueReading) { ok &= expect(!dialog->close(), "Close requests cancellation while matching is paused on the worker."); }
			else {
				previousFontHeight = dialog->findChild<QProgressBar*>()->fontMetrics().height();
				applyStudioTheme(*qApp, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark,
					UiDensity::Standard, continuedScale));
			}
			matching.release();
		} else if (continueReading && payload.reached && !payloadChecked && details && details->toPlainText().contains("payload.bin")) {
			payloadChecked = true;
			const auto* progress = dialog->findChild<QProgressBar*>();
			ok &= expect(progress && (continuedScale > scale ? progress->fontMetrics().height() > previousFontHeight
				: progress->fontMetrics().height() < previousFontHeight), "Live scaling reaches the active operation's progress font.");
			ok &= inspect(dialog, false, label + QStringLiteral("-payload-scale-%1").arg(continuedScale));
			payload.release();
		}
	});
	timer.start(5);
	PackageReadControl control;
	control.progress = [&](const QString& phase, qint64 done, qint64 total) {
		if (phase.contains("Checking package source protections")) { matching.pause(); }
		if (continueReading && phase == "payload.bin" && done > 0 && total > 0) { payload.pause(); }
	};
	if (writer) {
		PackageWriteRequest request; request.destinationPath = destination; request.writeManifest = true;
		request.byteProgress = [&](PackageWritePhase phase, const QString& text, quint64, quint64) {
			if (phase == PackageWritePhase::CheckIndex && text.contains("Checking package source protections")) { matching.pause(); }
		};
		PackageSaveResult result;
		QEventLoop loop;
		ownedDialog = showPackageSaveDialog(nullptr, plan, request, [&](const auto& value) { result = value; finished = true; loop.quit(); });
		QTimer timeout; timeout.setSingleShot(true); QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
		timeout.start(15000); if (!finished) { loop.exec(); }
		matching.release(); timer.stop();
		const auto* completedProgress = ownedDialog->findChild<QProgressBar*>();
		ok &= expect(completedProgress && !completedProgress->accessibleDescription().contains("Records checked"),
			"A finished operation does not retain its old metadata progress description.");
		delete ownedDialog; ownedDialog = nullptr;
		ok &= expect(finished && result.report.cancelled && !result.report.outputCommitted && !result.ready()
			&& !QFileInfo::exists(destination) && !QFileInfo::exists(destination + ".vibestudio-save.lock"),
			"Export cancellation during protection matching publishes nothing and finishes without blocking the UI.");
	} else if (copying) {
		const auto before = root.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
		PackageCopyRequest request; request.parentDirectory = root.absolutePath(); request.entryIndexes = {0}; request.control = control;
		const auto result = runPackageCopyDialog(nullptr, std::make_shared<PackageStagingArchive>(plan), request);
		ok &= expect(result.cancelled && !result.succeeded() && result.paths.isEmpty() && !result.storage
			&& root.entryList(QDir::AllEntries | QDir::NoDotAndDotDot) == before,
			"Copy cancellation during matching discards its temporary batch before any handoff.");
	} else {
		PackageExtractionRequest request; request.targetDirectory = destination; request.extractAll = true; request.control = control;
		const auto result = runPackageExtractionDialog(nullptr, std::make_shared<PackageStagingArchive>(plan), request);
		ok &= continueReading
			? expect(result.succeeded() && result.writtenCount == 1 && payloadChecked && payload.onWorker && !payload.expired
				&& QFileInfo(QDir(destination).filePath("payload.bin")).size() == 131072, "Extraction resumes byte progress and publishes the complete payload.")
			: expect(result.cancelled && result.errorCount == 0 && result.writtenCount == 0 && !QFileInfo::exists(destination),
				"Extraction cancellation during matching creates no output and is not reported as corruption.");
	}
	ok &= expect(checked && matching.onWorker && !matching.expired, "The event loop observes and cancels matching while its worker is gated.");
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	// Direct Qt calls and widget rendering only; no native input or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	const auto cleanup = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup(); });
	PackageStagingModel plan; QString error;
	bool ok = expect(plan.createEmpty(PackageArchiveFormat::Pk3, {}, &error)
		&& plan.addBytes(QByteArray(131072, 'p'), "payload.bin", &error), "Prepare synthetic package content.");
	for (int scale : {100, 200}) {
		ExpandedTranslator translator; if (scale == 200) { app.installTranslator(&translator); }
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		for (const auto& operation : {QStringLiteral("export"), QStringLiteral("extract"), QStringLiteral("copy"), QStringLiteral("resume")}) {
			ok &= exercise(QDir(temporary.path()), plan, operation, scale);
		}
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	return ok ? 0 : 1;
}
