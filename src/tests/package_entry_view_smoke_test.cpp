#include "app/package_entry_view.h"
#include "app/studio_theme.h"
#include "package_browser_test_fixture.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QIdentityProxyModel>
#include <QImage>
#include <QMimeData>
#include <QPointer>
#include <QThread>
#include <QTextLayout>
#include <QTranslator>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message) { if (!condition) { std::cerr << message << '\n'; } return condition; }
template<class Predicate> bool until(Predicate predicate) {
	QElapsedTimer elapsed; elapsed.start();
	while (!predicate() && elapsed.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return predicate();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		return QByteArray(context).contains("PackageEntryView") || QByteArray(context) == "VibeStudioPackageBrowser"
			? QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source)) : QString();
	}
};
class CountPresentation final : public QIdentityProxyModel {
public:
	mutable int reads = 0;
	QVariant data(const QModelIndex& index, int role) const override { ++reads; return QIdentityProxyModel::data(index, role); }
	void multiData(const QModelIndex& index, QModelRoleDataSpan roles) const override {
		reads += static_cast<int>(roles.size()); QIdentityProxyModel::multiData(index, roles);
	}
};
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication application(argc, argv);
#ifdef Q_OS_WIN
	application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto source = tests::browserFixture(50000); PackageArchive archive; QString error; bool ok = true;
	if (!expect(archive.loadSnapshot(source, &error), "admit virtual-list fixture")) { return 1; }
	for (int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { application.installTranslator(&translator); }
		applyStudioTheme(application, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		application.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		std::atomic_bool reached{false}, released{false}, workerOnly{true};
		std::mutex mutex; std::condition_variable gate;
		PackageEntryView view; view.resize(940, 620); view.setAccessibleName(QStringLiteral("Package entries"));
		view.setSelectionMode(QAbstractItemView::ExtendedSelection); view.show();
		int invalidatedSelections = 0;
		QObject::connect(&view, &PackageEntryView::entrySelectionChanged, &application, [&] {
			if (view.busy() && view.selectedEntryIndexes().isEmpty()) { ++invalidatedSelections; }
		});
		PackageReadControl control;
		control.progress = [&](const QString&, qint64 records, qint64) {
			if (QThread::currentThread() == application.thread()) { workerOnly = false; }
			if (records < 256 || reached.exchange(true)) { return; }
			std::unique_lock lock(mutex); gate.wait_for(lock, std::chrono::seconds(5), [&] { return released.load(); });
		};
		view.showEntries(archive, QStringLiteral("first"), QStringLiteral("bulk"), QString(), control);
		ok &= expect(until([&] { return reached.load() && view.statusText().contains(QStringLiteral("256")); }) && view.busy()
			&& !view.readyFor(QStringLiteral("first")) && view.selectedEntryIndexes().isEmpty() && workerOnly,
			"index work reports progress off the UI thread and cannot expose stale selection");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		const auto render = [&](const QString& name) {
			if (captures.isEmpty()) { return true; }
			QImage image(view.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); view.render(&image);
			return image.save(QDir(captures).filePath(QStringLiteral("package-virtual-%1-%2.png").arg(name).arg(scale)));
		};
		ok &= expect(render(QStringLiteral("pending")), "render listing progress");
		view.showEntries(archive, QStringLiteral("second"), QString(), QStringLiteral("THINGS"));
		view.selectEntry(QStringLiteral("THINGS"), 2);
		released = true; gate.notify_all();
		ok &= expect(until([&] { return !view.busy(); }) && view.readyFor(QStringLiteral("second")) && view.entryCount() == 2
			&& view.currentIndex().data(Qt::UserRole + 7).toLongLong() == 2 && source->payloadReads == 0,
			"a newer request supersedes the blocked result and preserves an exact duplicate selection");
		ok &= expect(view.currentIndex().data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("source entry 3"))
			&& view.focusPolicy() != Qt::NoFocus && !view.accessibleName().isEmpty(), "virtual rows expose occurrence labels and native list focus semantics");
		ok &= expect(render(QStringLiteral("duplicates")), "render duplicate rows");
		{
			int currentChanges = 0, selectionChanges = 0;
			const auto currentConnection = QObject::connect(&view, &PackageEntryView::currentEntryChanged, &view, [&] { ++currentChanges; });
			const auto selectionConnection = QObject::connect(&view, &PackageEntryView::entrySelectionChanged, &view, [&] { ++selectionChanges; });
			view.clearSelection(); view.selectAll();
			ok &= expect(view.selectedEntryIndexes().size() == 2 && currentChanges == 0 && selectionChanges > 0,
				"multi-selection changes leave the current preview occurrence intact");
			view.selectEntry(QStringLiteral("THINGS"), 1);
			ok &= expect(currentChanges == 1 && view.currentIndex().data(Qt::UserRole + 7).toLongLong() == 1,
				"changing between duplicate paths notifies the new preview occurrence");
			view.selectEntry(QStringLiteral("THINGS"), 2);
			QObject::disconnect(currentConnection); QObject::disconnect(selectionConnection);
		}
		QVector<qsizetype> copied;
		view.extractForDrag = [&](const auto& indexes) { copied = indexes; return QList<QUrl>{QUrl(QStringLiteral("file:///fixture-copy.txt"))}; };
		std::unique_ptr<QMimeData> mime(view.model()->mimeData({view.currentIndex()}));
		ok &= expect(mime && mime->hasUrls() && copied.size() == 1 && archive.entries().at(copied.first()).sourceOrdinal == 2,
			"copy drag preparation receives exact source indexes from the virtual model");
		bool indexedAgain = false; PackageReadControl reuse;
		reuse.progress = [&](const QString& phase, qint64, qint64) { if (phase.contains(QStringLiteral("Indexing"))) { indexedAgain = true; } };
		view.showEntries(archive, QStringLiteral("second"), QStringLiteral("bulk"), QString(), reuse);
		view.selectAllWhenReady();
		ok &= expect(until([&] { return !view.busy(); }) && view.entryCount() == 50000 && !indexedAgain
			&& view.selectedEntryIndexes().size() == 50000, "folder listing reuses the index and Select All covers every row");
		// Count actual Qt presentation requests: the full result remains exposed,
		// while layout/rendering asks only for a small visible subset.
		CountPresentation probe; probe.setSourceModel(view.model()); view.setModel(&probe); probe.reads = 0;
		view.clearSelection(); const auto last = probe.index(49999, 0);
		ok &= expect(until([&] {
			view.scrollTo(last);
			const auto rectangle = view.visualRect(last);
			return rectangle.isValid() && view.viewport()->rect().intersects(rectangle) && view.indexAt(rectangle.center()) == last;
		}), "batched layout makes the final row visible before its render is accepted");
		ok &= expect(render(QStringLiteral("large")) && probe.rowCount() == 50000 && probe.reads < 5000,
			"50,000 accessible rows render with bounded visible-row presentation work");
		std::cout << "Virtual list at " << scale << "%: " << probe.rowCount() << " rows, " << probe.reads << " presentation requests\n";
		if (scale == 200) {
			const auto text = last.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0);
			QTextLayout layout(text, view.font()); QTextOption options; options.setTextDirection(Qt::RightToLeft); layout.setTextOption(options);
			layout.beginLayout(); auto line = layout.createLine(); line.setLineWidth(view.viewport()->width()); layout.endLayout();
			// Interior caret positions avoid the ambiguous leading edge of a
			// bidirectional isolate while still checking actual visual ordering.
			const auto digits = line.cursorToX(text.indexOf(QStringLiteral("049999")) + 2);
			const auto extension = line.cursorToX(text.indexOf(QStringLiteral("txt")) + 1);
			if (digits >= extension) { std::cerr << "RTL filename positions: digits=" << digits << " extension=" << extension << '\n'; }
			ok &= expect(text.contains(QStringLiteral("049999.txt")) && digits < extension
				&& last.data(Qt::UserRole).toString() == QStringLiteral("bulk/049999.txt"), "numeric filenames retain their actual visual and logical order in RTL layouts");
		}
		view.setModel(probe.sourceModel());
		view.showEntries(archive, QStringLiteral("second"), QString(), QStringLiteral("ext=txt")); view.cancel();
		ok &= expect(!view.busy() && view.canRetry() && !view.readyFor(QStringLiteral("second")) && view.selectedEntryIndexes().isEmpty(),
			"Cancel exposes a retryable state without actionable old rows");
		ok &= expect(invalidatedSelections > 0, "model resets notify preview/action consumers that the old selection is unavailable");
		view.retry(); ok &= expect(until([&] { return !view.busy(); }) && view.entryCount() == 50000, "retry completes the same query");
		view.showEntries(archive, QStringLiteral("second"), QString(), QStringLiteral("unsupported"));
		ok &= expect(until([&] { return !view.busy(); }) && view.entryCount() == 1
			&& view.currentIndex().data(Qt::DisplayRole).toString().contains(QStringLiteral("Unreadable"))
			&& view.currentIndex().data(Qt::AccessibleDescriptionRole).toString().contains(QStringLiteral("Unsupported fixture method")),
			"unreadable entries expose text status and the source diagnostic");
		reached = false; released = false;
		view.showEntries(archive, QStringLiteral("closing"), QString(), QString(), control);
		ok &= expect(until([&] { return reached.load(); }), "start live index work before closing");
		QPointer<QThread> retiring;
		for (auto* thread : view.findChildren<QThread*>()) { if (thread->isRunning()) { retiring = thread; } }
		view.showMessage(QStringLiteral("Closed")); released = true; gate.notify_all();
		ok &= expect(until([&] { return !retiring || retiring->isFinished(); }), "closing cancels live worker preparation");
		application.processEvents();
		ok &= expect(!view.busy() && !view.canRetry() && view.selectedEntryIndexes().isEmpty()
			&& view.statusText() == QStringLiteral("Closed"), "a retired worker cannot resurrect a closed listing");
		if (scale == 200) { application.removeTranslator(&translator); }
	}
	return ok ? 0 : 1;
}
