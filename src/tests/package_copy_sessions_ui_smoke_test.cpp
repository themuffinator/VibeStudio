#include "app/package_copy_sessions_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "package_copy_test_helpers.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QUuid>

using namespace vibestudio;
using namespace package_copy_test;
namespace {
class Expanded final : public QTranslator {
public:
	bool enabled = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		return enabled && QByteArray(context) == "PackageCopySessionsDialog" ? QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source)) : QString();
	}
};
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool settle(QApplication& app, PackageCopySessionsDialog& dialog)
{
	QElapsedTimer timer; timer.start();
	do { app.processEvents(); QThread::msleep(1); } while (dialog.busy() && timer.elapsed() < 15000);
	app.processEvents(); return !dialog.busy();
}
}
int main(int argc, char** argv)
{
	// Direct Qt properties/actions and QWidget renders; no native input or capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const auto cleanup = qScopeGuard([] { waitForPackageCopyCleanup(); });
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	Expanded translator; app.installTranslator(&translator); bool ok = true;
	for (const int scale : {100, 200}) {
		translator.enabled = scale == 200; StudioSettings().setReducedMotion(scale == 200);
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scale));
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		const QString store = temporary.filePath(QStringLiteral("copies-%1").arg(scale));
		Reader reader; reader.add(QStringLiteral("live.txt"), "active"); PackageCopyRequest request; request.storeDirectory = store; request.entryIndexes = {0};
		auto live = copyPackageEntries(reader, request); if (!expect(live.succeeded(), "prepare a live UI session")) { return 1; }
		const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), path = QDir(store).filePath(id + QStringLiteral(".copies"));
		const QString batch = QDir(path).filePath(QStringLiteral("package-copy-abc123")), payload = QDir(batch).filePath(QStringLiteral("saved.txt"));
		const auto record = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"kind", "vibestudio-package-copies"}, {"id", id},
			{"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}).toJson();
		if (!expect(QDir().mkpath(batch) && write(payload, "orphan") && write(QDir(path).filePath(QStringLiteral("session.json")), record), "prepare owned UI orphan fixture")) { return 1; }
		PackageCopySessionsDialog dialog(store); dialog.show(); ok &= expect(settle(app, dialog), "asynchronous copy inventory settles");
		auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("packageCopySessionsTable"));
		auto* details = dialog.findChild<QPlainTextEdit*>(QStringLiteral("packageCopySessionsDetails"));
		auto* discard = dialog.findChild<QPushButton*>(QStringLiteral("packageCopySessionsDiscard"));
		auto* cancel = dialog.findChild<QPushButton*>(QStringLiteral("packageCopySessionsCancel"));
		auto* close = dialog.findChild<QPushButton*>(QStringLiteral("packageCopySessionsClose"));
		auto* status = dialog.findChild<QLabel*>(QStringLiteral("packageCopySessionsStatus"));
		auto* progress = dialog.findChild<QProgressBar*>(QStringLiteral("packageCopySessionsProgress"));
		if (!expect(table && details && discard && cancel && close && status && progress && table->rowCount() == 2, "review controls and both sessions exist")) { return 1; }
		const auto select = [&](const QString& session) {
			for (int row = 0; row < table->rowCount(); ++row) {
				if (table->item(row, 0)->data(Qt::AccessibleTextRole).toString() == session) { table->setCurrentCell(row, 0); return true; }
			} return false;
		};
		ok &= expect(select(QFileInfo(live.session->path()).fileName().chopped(7)) && !discard->isEnabled(), "live session cannot be selected for discard");
		ok &= expect(select(id) && discard->isEnabled() && details->toPlainText().contains(id) && details->toPlainText().contains(QChar(0x2068)),
			"unused selection exposes a full, bidirectionally isolated identity and actionable discard");
		for (auto* widget : QList<QWidget*>{table, details, discard, cancel, close}) {
			ok &= expect(!widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus, "review controls retain native accessibility and keyboard focus");
		}
		for (auto* button : {cancel, close}) {
			ok &= expect(dialog.rect().contains(QRect(button->mapTo(&dialog, QPoint()), button->size())), "persistent Cancel and Close fit at both text scales");
		}
		if (scale == 200) {
			ok &= expect(dialog.fontMetrics().height() >= 24 && dialog.layoutDirection() == Qt::RightToLeft
				&& dialog.palette().color(QPalette::Window).lightness() > 128, "review uses actual 200 percent high-contrast RTL text");
		}
		if (auto* scroll = dialog.findChild<QScrollArea*>()) {
			ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, "long shared diagnostics wrap within the review viewport");
		}
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-copy-sessions-%1.png").arg(scale))), "render session review");
		}
		auto* sharedLimits = dialog.findChild<QPushButton*>(QStringLiteral("packageCopySharedLimits"));
		if (!expect(sharedLimits && sharedLimits->isEnabled(), "shared policy editor remains available during incomplete legacy accounting")) { return 1; }
		for (const bool conflict : {true, false}) {
			QTimer editor; editor.setInterval(1); bool edited = false;
			QObject::connect(&editor, &QTimer::timeout, [&] {
				auto* policy = dialog.findChild<QDialog*>(QStringLiteral("packageCopySharedLimitsDialog")); if (!policy) { return; }
				editor.stop(); edited = true;
				auto* bytes = policy->findChild<QSpinBox*>(QStringLiteral("packageCopySharedMiB"));
				auto* batches = policy->findChild<QSpinBox*>(QStringLiteral("packageCopySharedBatches"));
				auto* apply = policy->findChild<QPushButton*>(QStringLiteral("packageCopySharedApply"));
				if (!expect(bytes && batches && apply, "shared limit fields and Apply exist")) { policy->reject(); ok = false; return; }
				bytes->setValue(8); batches->setValue(1);
				for (auto* field : policy->findChildren<QSpinBox*>()) {
					ok &= expect(!field->accessibleName().isEmpty() && field->focusPolicy() != Qt::NoFocus, "shared quota fields expose native keyboard access");
				}
				for (auto* caption : policy->findChildren<QLabel*>()) {
					if (!caption->buddy()) { continue; }
					ok &= expect(caption->height() >= caption->heightForWidth(caption->width()), "wrapped shared limit labels have their full required height");
				}
				for (auto* button : policy->findChild<QDialogButtonBox*>()->buttons()) {
					ok &= expect(policy->rect().contains(QRect(button->mapTo(policy, QPoint()), button->size())), "shared quota Apply and Cancel remain visible at each scale");
				}
				if (conflict) {
					const auto quota = inspectPackageCopyQuota(store); QString error;
					ok &= expect(configurePackageCopyQuota(store, {16ull * 1024 * 1024, 16, 32, 4}, quota.policyFingerprint, false, &error), "change only the owned policy while its GUI review is open");
				} else if (!captures.isEmpty()) {
					QImage image(policy->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); policy->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-copy-shared-limits-%1.png").arg(scale))), "render shared quota editor");
				}
				apply->click();
			});
			editor.start(); sharedLimits->click(); ok &= expect(edited && settle(app, dialog), "shared policy worker settles");
			const auto current = inspectPackageCopyQuota(store);
			ok &= expect(current.limits.maximumBytes == (conflict ? 16ull : 8ull) * 1024 * 1024
				&& status->text().contains(conflict ? QStringLiteral("changed") : QStringLiteral("saved")) && read(live.paths.first()) == "active",
				"GUI rejects stale policies and saves fresh lower limits without removing active copies");
		}
		ok &= expect(select(id), "keep the retained session selected after policy refresh");
		if (auto* scroll = dialog.findChild<QScrollArea*>()) {
			for (auto* widget : QList<QWidget*>{table, details, discard}) {
				scroll->ensureWidgetVisible(widget); app.processEvents();
				ok &= expect(scroll->viewport()->rect().intersects(QRect(widget->mapTo(scroll->viewport(), QPoint()), widget->size())), "review details and discard remain reachable through scrolling");
			}
		}
		const auto confirm = [&](QMessageBox::StandardButton answer, bool change) {
			QTimer responder; responder.setInterval(1); bool answered = false;
			QObject::connect(&responder, &QTimer::timeout, [&] {
				auto* box = dialog.findChild<QMessageBox*>(QStringLiteral("packageCopySessionsConfirmation")); if (!box) { return; }
				ok &= expect(box->textFormat() == Qt::PlainText && box->defaultButton() == box->button(QMessageBox::Cancel)
					&& box->text().contains(id) && box->detailedText().contains(QDir::toNativeSeparators(path)), "discard confirmation identifies the exact session and defaults to Cancel");
				if (change) { ok &= expect(write(payload, "edits made after review"), "change owned payload while confirmation is open"); }
				answered = true; responder.stop(); box->button(answer)->click();
			});
			responder.start(); discard->click(); ok &= expect(answered && settle(app, dialog), "reviewed asynchronous discard completes");
		};
		confirm(QMessageBox::Discard, true);
		ok &= expect(QFileInfo::exists(payload) && status->text().contains(QStringLiteral("changed")), "confirmation-time changes refuse removal and refresh the inventory");
		ok &= expect(select(id), "find changed session after refresh"); confirm(QMessageBox::Cancel, false);
		ok &= expect(QFileInfo::exists(payload), "Cancel confirmation preserves temporary consumer edits");
		confirm(QMessageBox::Discard, false);
		ok &= expect(!QFileInfo::exists(path) && QFileInfo::exists(live.paths.first()) && table->rowCount() == 1, "reviewed discard removes only the unused session and refreshes usage");
		ok &= expect(dialog.findChild<QLabel*>(QStringLiteral("packageCopySharedUsage"))->text().contains(QStringLiteral("Shared initial reservations")), "after legacy cleanup the shared reservation totals become complete");
		dialog.reload(); ok &= expect(dialog.busy() && cancel->isEnabled(), "review has a persistent cancellation control before worker completion");
		if (scale == 200) { ok &= expect(progress->maximum() == 1, "reduced motion uses static indeterminate progress"); }
		cancel->click(); ok &= expect(settle(app, dialog) && status->text().contains(QStringLiteral("cancelled")), "cancelled scan reports incomplete review without deletion");
		dialog.reload(); close->click(); ok &= expect(settle(app, dialog) && !dialog.isVisible(), "close waits for worker cancellation before leaving the dialog");
		live.storage.reset(); live.session.reset(); waitForPackageCopyCleanup();
	}
	app.removeTranslator(&translator); return ok ? 0 : 1;
}
