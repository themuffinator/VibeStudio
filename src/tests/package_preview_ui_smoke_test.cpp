#include "package_entry_test_helpers.h"
#include "package_summary_test_fixture.h"
#include "app/ui_primitives.h"
#include "app/application_shell.h"
#include "app/studio_theme.h"
#include "core/package_draft.h"
#include "core/package_import_store.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLocale>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QSplitter>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QTranslator>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 10000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return ready();
}
class ExpandedTranslator final : public QTranslator {
public:
	bool expanded = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		const QByteArray text(source);
		return expanded && QByteArray(context).endsWith("ApplicationShell")
			&& (text == "Cancel Preview" || text == "Retry Preview" || text == "Loading package preview…" || text == "Preview cancelled.")
			? QStringLiteral("[%1 — expanded label]").arg(QString::fromUtf8(source)) : QString();
	}
};
}

int main(int argc, char** argv)
{
	// Direct Qt widgets only; no OS input injection, desktop capture or game.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const auto cleanup = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup(); });
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
	plan.addBytes("FIRST planned content", QStringLiteral("first.txt"));
	plan.addBytes("SECOND planned content", QStringLiteral("second.txt"));
	plan.addBytes(QByteArray(1024 * 1024, 'x'), QStringLiteral("large.txt"));
	// Header-only fixtures; metadata does not establish that codec packets decode.
	plan.addBytes(QByteArray::fromHex("4f67675300020000000000000000110000000000000000000000011e01766f72626973000000000244ac0000000000000000000000000000b8014f676753000444ac0000000000001100000001000000000000000100"), QStringLiteral("estimate.ogg"));
	plan.addBytes(QByteArray::fromHex("4f67675300020000000000000000110000000000000000000000011e01766f72626973000000000244ac0000000000000000000000000000b8014f676753000400000000000000801100000001000000000000000100"), QStringLiteral("range.ogg"));
	const QString fixture = temporary.filePath(QStringLiteral("fixture.vibepackage")); QString error;
	if (!expect(PackageDraft::save(fixture, &plan, false, &error), "save owned planned fixture")) { return 1; }
	const quint64 hugeSize = std::numeric_limits<quint64>::max();
	const QString hugeFixture = temporary.filePath(QStringLiteral("huge-declarations.zip"));
	if (!tests::summaryZip(hugeFixture, {{"huge.txt", hugeSize}, {"huge.png", hugeSize}})) { return 1; }
	ExpandedTranslator translator; app.installTranslator(&translator); bool ok = true;
	for (const int scale : {100, 200}) {
		translator.expanded = scale == 200;
		StudioSettings preferences;
		auto accessibility = preferences.accessibilityPreferences();
		accessibility.theme = scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark;
		accessibility.textScalePercent = scale; accessibility.reducedMotion = scale == 200;
		preferences.setAccessibilityPreferences(accessibility); preferences.sync();
		ApplicationShell shell; shell.resize(1700, 1050); shell.show(); shell.openPathFromCommandLine(fixture); app.processEvents();
		applyStudioTheme(app, studioThemeTokens(accessibility.theme, accessibility.density, scale));
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); shell.setLayoutDirection(app.layoutDirection());
		app.processEvents();
		auto entries = tests::PackageRows(shell.findChild<PackageEntryView*>(QStringLiteral("packageEntries")));
		auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("packageFilter"));
		auto* text = shell.findChild<QPlainTextEdit*>(QStringLiteral("packageTextPreview"));
		auto* action = shell.findChild<QPushButton*>(QStringLiteral("packagePreviewCancel"));
		auto* status = shell.findChild<QLabel*>(QStringLiteral("packagePreviewStatus"));
		auto* progress = shell.findChild<QProgressBar*>(QStringLiteral("packagePreviewProgress"));
		auto* inspector = shell.findChild<QWidget*>(QStringLiteral("packagePreviewInspector"));
		if (!expect(entries && filter && text && action && status && progress && inspector, "preview controls exist in the real shell")) { return 1; }
		auto* drawer = dynamic_cast<DetailDrawer*>(inspector->findChild<QWidget*>(QStringLiteral("detailDrawer")));
		if (!drawer) {
			for (auto* candidate : inspector->findChildren<QFrame*>()) {
				if (auto* found = dynamic_cast<DetailDrawer*>(candidate)) { drawer = found; break; }
			}
		}
		if (!expect(drawer, "package preview exposes its shared detail drawer")) { return 1; }
		const auto select = [&](const QString& path) {
			filter->clear(); entries->setCurrentItem(nullptr);
			for (int row = 0; row < entries->count(); ++row) {
				if (entries->item(row)->data(Qt::UserRole).toString() == path) { entries->setCurrentItem(entries->item(row)); return true; }
			}
			return false;
		};
		const auto render = [&](const QString& state) {
			// Settle parent layouts after revealing the action. Layout-only delivery
			// preserves the pending worker state while sizing expanded Cancel text.
			if (state != QStringLiteral("loading")) { app.processEvents(); }
			inspector->ensurePolished();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
			inspector->layout()->activate();
			if (!action->isHidden()) {
				ok &= expect(inspector->rect().contains(QRect(action->mapTo(inspector, QPoint()), action->size()))
					&& action->width() >= action->sizeHint().width(), "visible expanded preview action fits after pending layout");
			}
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (captures.isEmpty()) { return true; }
			QImage image(inspector->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); inspector->render(&image);
			return image.save(QDir(captures).filePath(QStringLiteral("package-preview-%1-%2.png").arg(state).arg(scale)));
		};
		ok &= expect(select(QStringLiteral("large.txt")) && !action->isHidden() && !progress->isHidden() && text->toPlainText().isEmpty(), "selection immediately exposes loading and removes stale text");
		ok &= expect(!status->accessibleName().isEmpty() && !progress->accessibleName().isEmpty()
			&& !action->accessibleName().isEmpty() && action->focusPolicy() != Qt::NoFocus, "status, progress and cancellation are accessible");
		if (scale == 200) {
			ok &= expect(progress->maximum() > 0 && status->fontMetrics().height() >= 24
				&& inspector->palette().color(QPalette::Window).lightness() > 128 && inspector->layoutDirection() == Qt::RightToLeft,
				"200% fixture actually uses large high-contrast RTL text and static reduced-motion progress");
		}
		ok &= expect(render(QStringLiteral("loading")), "render loading state");
		action->click();
		ok &= expect(status->text().contains(QStringLiteral("cancelled")) && !action->isHidden() && progress->isHidden()
			&& text->toPlainText().isEmpty(), "Cancel immediately exposes a retryable state without old bytes");
		app.processEvents();
		ok &= expect(status->text().contains(QStringLiteral("cancelled")), "cancelled queued work cannot publish later");
		ok &= expect(render(QStringLiteral("cancelled")), "render cancellation at each scale and direction");
		ok &= expect(inspector->rect().contains(QRect(action->mapTo(inspector, QPoint()), action->size()))
			&& action->width() >= action->sizeHint().width(), "expanded cancel/retry control fits the inspector");
		QElapsedTimer retryTime; retryTime.start(); action->click();
		ok &= expect(until([&] { return action->isHidden() && text->toPlainText().size() > 0; }) && text->toPlainText().size() <= 65536,
			"Retry uses the current selection and respects the sampling bound");
		std::cout << "Preview retry at " << scale << "%: " << retryTime.elapsed() << " ms\n";
		const auto* details = inspector->findChild<QTextEdit*>(QStringLiteral("detailContent"));
		ok &= expect(details && details->toPlainText().size() < 8192 && details->toPlainText().contains(QStringLiteral("excerpt"))
			&& text->toPlainText().size() == 65536, "Details bounds the wrapped excerpt while Preview retains all sampled text");
		ok &= expect(render(QStringLiteral("excerpt")), "render long-line excerpt");
		ok &= expect(select(QStringLiteral("first.txt")) && select(QStringLiteral("second.txt")), "rapidly switch planned selections");
		ok &= expect(until([&] { return text->toPlainText() == QStringLiteral("SECOND planned content"); }), "latest selected planned content reaches the inspector");
		ok &= expect(render(QStringLiteral("ready")), "render completed preview");
		ok &= expect(select(QStringLiteral("first.txt")), "start a preview while reading another detail section");
		drawer->showSection(QStringLiteral("summary"));
		ok &= expect(until([&] { return text->toPlainText() == QStringLiteral("FIRST planned content"); })
			&& drawer->currentSectionId() == QStringLiteral("summary") && drawer->currentSectionText().contains(QStringLiteral("first.txt")),
			"asynchronous preview completion preserves the reader's chosen metadata section");
		QTextCursor cursor(text->document()); cursor.setPosition(0); cursor.setPosition(5, QTextCursor::KeepAnchor); text->setTextCursor(cursor);
		int textChanges = 0;
		const auto textConnection = QObject::connect(text->document(), &QTextDocument::contentsChanged, &shell, [&] { ++textChanges; });
		entries->selectAll(); app.processEvents();
		ok &= expect(entries->selectedItems().size() > 1 && textChanges == 0 && text->textCursor().selectedText() == QStringLiteral("FIRST")
			&& drawer->currentSectionId() == QStringLiteral("summary"),
			"multi-selection updates actions without replacing the current preview or its text selection");
		entries->clearSelection();
		ok &= expect(textChanges == 0 && text->textCursor().selectedText() == QStringLiteral("FIRST"),
			"clearing multi-selection while retaining the current row preserves its preview");
		QObject::disconnect(textConnection);
		ok &= expect(select(QStringLiteral("first.txt")), "start a new selection before clearing it");
		entries->setCurrentItem(nullptr); app.processEvents();
		ok &= expect(text->toPlainText().isEmpty() && action->isHidden() && status->isHidden(), "clearing selection cancels and clears the inspector");
		if (auto* splitter = shell.findChild<QSplitter*>(QStringLiteral("packagesWorkbench"))) { splitter->setSizes({180, 360, 1120}); }

		for (const QString& path : {QStringLiteral("estimate.ogg"), QStringLiteral("range.ogg")}) {
			ok &= expect(select(path) && until([&] { return action->isHidden(); }), "planned Ogg metadata reaches the GUI preview worker");
			drawer->showSection(QStringLiteral("asset-details"));
			const QString expected = path == QStringLiteral("range.ogg") ? QStringLiteral("supported range") : QStringLiteral("Duration estimate");
			ok &= expect(details && details->toPlainText().contains(expected) && details->toPlainText().contains(QStringLiteral("playable length")),
				"GUI shows range refusal or header timing provenance at both scales");
			ok &= expect(render(path == QStringLiteral("range.ogg") ? QStringLiteral("ogg-range") : QStringLiteral("ogg-estimate")), "render Ogg timing diagnostics");
		}
		drawer->showSection(QStringLiteral("preview"));
		shell.openPathFromCommandLine(hugeFixture);
		ok &= expect(until([&] { return entries->count() == 2 && !static_cast<PackageEntryView*>(entries)->busy(); }), "load unsigned-size metadata without reading oversized payloads");
		if (auto* splitter = shell.findChild<QSplitter*>(QStringLiteral("packagesWorkbench"))) { splitter->setSizes({180, 360, 1120}); }
		app.processEvents();
		const auto exactSize = QLocale().toString(hugeSize);
		const auto hasExactSize = [&] {
			for (const auto& section : drawer->sections()) {
				if (section.content.contains(QStringLiteral("Bytes sampled:"))) {
					return section.content.contains(exactSize) && section.content.contains(QStringLiteral("0 B"));
				}
			}
			return false;
		};
		for (const QString& path : {QStringLiteral("huge.txt"), QStringLiteral("huge.png")}) {
			ok &= expect(select(path) && hasExactSize(), "loading metadata and texture previews retain the exact unsigned total");
			action->click();
			ok &= expect(status->text().contains(QStringLiteral("cancelled")) && hasExactSize() && text->toPlainText().isEmpty(),
				"cancelled metadata and texture previews retain known totals without publishing bytes");
			action->click();
			ok &= expect(until([&] { return action->isHidden(); }) && hasExactSize() && text->toPlainText().isEmpty(),
				"failed oversized previews retain exact totals and publish no unverified bytes");
			ok &= expect(render(path.endsWith(QStringLiteral("png")) ? QStringLiteral("huge-image") : QStringLiteral("huge-text")),
				"render exact oversized preview diagnostics at both scales");
		}
		ok &= expect(select(QStringLiteral("huge.txt")), "start a request before shell destruction");
		// The destructor must retire the preview snapshot without a late callback.
	}
	app.removeTranslator(&translator);
	return ok ? 0 : 1;
}
