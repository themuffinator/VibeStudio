#include "core/package_import_store.h"
#include "app/package_import_dialog.h"
#include "app/package_operation_dialog.h"
#include "app/package_recovery_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTranslator>
#include <QUuid>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label)
{
	if (!value) { std::cerr << label << '\n'; } return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool wait(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	do { QApplication::processEvents(); if (ready()) { return true; } QThread::msleep(5); } while (timer.elapsed() < 10000);
	return false;
}
class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		return QByteArray(context) == "PackageImportDialog" ? QStringLiteral("[%1 — expanded label]").arg(QString::fromUtf8(source)) : QString();
	}
};
}
int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path());
	for (const char* name : {"TEMP", "TMP", "TMPDIR"}) { qputenv(name, root.path().toLocal8Bit()); }
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("settings.ini")));
	const QString source = root.filePath(QStringLiteral("source.txt")); bool ok = write(source, "owned content"); QString error;
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
	ok &= expect(plan.addFile(source, QStringLiteral("source.txt"), &error), "prepare a live working import");
	if (!ok) { return 1; }
	const QString directory = packageImportDirectory();
	const auto live = listPackageImports(directory).sessions.first();
	ExpandedTranslator translator;
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		if (scale == 200) { app.installTranslator(&translator); }
		PackageImportDialog dialog(directory); dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); }
		dialog.show();
		auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("packageImportSessions"));
		ok &= expect(wait([&]() { return !dialog.busy() && table->rowCount() == 1; }), "worker inventory populates the import manager");
		auto* bytes = dialog.findChild<QSpinBox*>(QStringLiteral("packageImportMaximumMiB"));
		auto* files = dialog.findChild<QSpinBox*>(QStringLiteral("packageImportMaximumFiles"));
		auto* discard = dialog.findChild<QPushButton*>(QStringLiteral("packageImportDiscard"));
		auto* unlock = dialog.findChild<QPushButton*>(QStringLiteral("packageImportUnlock"));
		auto* details = dialog.findChild<QPlainTextEdit*>(QStringLiteral("packageImportDetails"));
		auto* usage = dialog.findChild<QLabel*>(QStringLiteral("packageImportUsage"));
		auto* status = dialog.findChild<QLabel*>(QStringLiteral("packageImportStatus"));
		ok &= expect(bytes && files && discard && details && usage && status && !table->accessibleName().isEmpty()
			&& !discard->accessibleName().isEmpty() && discard->focusPolicy() != Qt::NoFocus && details->isReadOnly()
			&& !details->accessibleName().isEmpty() && usage->wordWrap() && status->wordWrap(), "storage controls expose accessible names, focus and wrapping");
		bytes->setValue(256); files->setValue(1);
		ok &= expect(StudioSettings().packageImportMaximumMiB() == 256 && StudioSettings().packageImportMaximumFiles() == 1, "storage controls persist bounded preferences");
		const auto rejected = runPackageStagingDialog(nullptr, plan, {{source, QStringLiteral("over-limit.txt")}});
		ok &= expect(rejected.accepted == 0 && !rejected.errors.isEmpty() && rejected.staging.operations().size() == 1, "file staging uses configured storage limits");
		bool confirmed = false;
		QTimer::singleShot(0, [&]() {
			if (auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				if (auto* button = question->button(QMessageBox::Discard)) { confirmed = true; button->click(); }
			}
		});
		discard->click();
		ok &= expect(confirmed && wait([&]() { return !dialog.busy() && status->text().contains(QStringLiteral("live document")); })
			&& QFileInfo::exists(live.path), "confirmed discard still refuses a live reader lease");
		ok &= expect(unlock && unlock->isEnabled() && !unlock->accessibleName().isEmpty() && unlock->focusPolicy() != Qt::NoFocus,
			"lock review is a named keyboard-focusable action");
		const QString rootLock = QDir(directory).filePath(QStringLiteral(".store.lock"));
		ok &= write(rootLock, {}); dialog.reload();
		ok &= expect(wait([&] { return !dialog.busy(); }), "inventory worker reviews an empty store lock");
		const auto reviewLock = [&](const QString& relative, bool accept, bool capture) {
			bool picked = false, answered = false;
			QTimer::singleShot(0, [&] {
				if (auto* picker = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
					picked = true; const QString label = QChar(0x2066) + relative + QChar(0x2069); picker->setTextValue(label);
					auto* field = picker->findChild<QComboBox*>();
					ok &= expect(field && field->layoutDirection() == Qt::LeftToRight && field->currentText() == label
						&& field->currentData(Qt::AccessibleTextRole).toString() == relative
						&& !field->accessibleName().isEmpty(), "technical lock paths preserve their leading punctuation and accessible value in RTL");
					ok &= expect(picker->layoutDirection() == dialog.layoutDirection()
						&& picker->width() <= picker->screen()->availableGeometry().width(), "lock selector respects RTL and fits the scaled screen");
					const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
					if (capture && !captures.isEmpty()) {
						QImage image(picker->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); picker->render(&image);
						ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-working-lock-picker-%1.png").arg(scale))), "save reviewed lock selector render");
					}
					QTimer::singleShot(0, [&] {
						if (auto* review = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
							answered = true;
							ok &= expect(review->defaultButton() == review->button(QMessageBox::Cancel)
								&& review->detailedText().contains(QStringLiteral("SHA-256")) && !review->accessibleName().isEmpty()
								&& review->layoutDirection() == dialog.layoutDirection(),
								"lock confirmation exposes review checksum with cancellation as default");
							const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
							if (capture && !captures.isEmpty()) {
								QImage image(review->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); review->render(&image);
								ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-working-lock-%1.png").arg(scale))), "save reviewed lock confirmation render");
							}
							review->button(accept ? QMessageBox::Yes : QMessageBox::Cancel)->click();
						}
					});
					picker->accept();
				}
			});
			unlock->click();
			ok &= expect(picked && answered && wait([&] { return !dialog.busy(); }), "lock review and confirmation finish with responsive worker completion");
		};
		reviewLock(QStringLiteral(".store.lock"), false, false);
		ok &= expect(QFileInfo::exists(rootLock), "cancelling lock confirmation preserves the reviewed file");
		reviewLock(QStringLiteral(".store.lock"), true, true);
		ok &= expect(!QFileInfo::exists(rootLock) && QFileInfo::exists(live.path)
			&& status->text().contains(QStringLiteral("lock released")), "confirmed empty-lock recovery preserves the live session");
		reviewLock(live.id + QStringLiteral(".working/.lease"), true, false);
		ok &= expect(QFileInfo::exists(QDir(live.path).filePath(QStringLiteral(".lease")))
			&& status->text().contains(QStringLiteral("in use")), "GUI lock release cannot break a live session lease");
		const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), path = QDir(directory).filePath(id + QStringLiteral(".working"));
		ok &= QDir().mkpath(QDir(path).filePath(QStringLiteral("objects")));
		ok &= write(QDir(path).filePath(QStringLiteral("working.json")), "{");
		dialog.reload();
		ok &= expect(wait([&]() { return !dialog.busy() && table->rowCount() == 2; }), "incomplete working sessions remain visible");
		for (int row = 0; row < table->rowCount(); ++row) {
			if (table->item(row, 0)->data(Qt::AccessibleTextRole).toString() == id) { table->setCurrentCell(row, 0); }
		}
		ok &= expect(discard->isEnabled() && details->toPlainText().contains(id), "reviewable incomplete sessions can be selected for cleanup");
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			auto* scroll = dialog.findChild<QScrollArea*>(); scroll->verticalScrollBar()->setValue(0); QApplication::processEvents();
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-working-%1-top.png").arg(scale))), "save working import overview render");
			auto* refresh = dialog.findChild<QPushButton*>(QStringLiteral("packageImportRefresh"));
			ok &= expect(wait([&] {
				scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
				const QRect visible(refresh->mapTo(scroll->viewport(), QPoint()), refresh->size());
				return scroll->viewport()->rect().contains(visible);
			}), "all working-import actions remain reachable at the bottom of the scaled layout");
			image.fill(Qt::transparent); dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-working-%1-actions.png").arg(scale))), "save working import actions render");
		}
		QTimer::singleShot(0, [&]() {
			if (auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				if (auto* button = question->button(QMessageBox::Discard)) { button->click(); }
			}
		});
		discard->click();
		ok &= expect(wait([&]() { return !dialog.busy() && table->rowCount() == 1; }) && !QFileInfo::exists(path)
			&& QFileInfo::exists(live.path), "reviewed cleanup removes only the abandoned session");
		dialog.reload(); dialog.reject();
		ok &= expect(wait([&]() { return !dialog.busy() && !dialog.isVisible(); }), "closing a busy import review cancels and joins its worker");
		app.removeTranslator(&translator);
	}
	{
		PackageRecoveryDialog recovery(root.filePath(QStringLiteral("recoveries"))); recovery.setAttribute(Qt::WA_DeleteOnClose, false);
		recovery.show(); auto* action = recovery.findChild<QPushButton*>(QStringLiteral("packageWorkingImports"));
		ok &= expect(action && !action->accessibleName().isEmpty(), "recovery manager connects to working import storage");
		if (action) {
			action->click();
			auto* working = recovery.findChild<QDialog*>(QStringLiteral("packageImportDialog"));
			ok &= expect(working != nullptr, "working import storage opens from the shared recovery manager");
			if (working) { working->close(); QApplication::processEvents(); }
		}
		recovery.reject(); wait([&]() { return !recovery.busy(); });
	}
	plan.clear(); QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup();
	ok &= expect(listPackageImports(directory).sessions.isEmpty(), "UI review leaves no owned working files after document close");
	std::cout << (ok ? "Package import UI smoke passed\n" : "Package import UI smoke failed\n"); return ok ? 0 : 1;
}
