#include "core/package_import_store.h"
#include "app/package_draft_storage_dialog.h"
#include "app/package_operation_dialog.h"
#include "app/package_recovery_dialog.h"
#include "app/studio_theme.h"
#include "core/package_draft.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTranslator>

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
		return QByteArray(context) == "PackageDraftStorageDialog" ? QStringLiteral("[%1 — expanded label]").arg(QString::fromUtf8(source)) : QString();
	}
};
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const QDir root(temporary.path()); bool ok = true; QString error;
	for (const char* name : {"TEMP", "TMP", "TMPDIR"}) { qputenv(name, root.path().toLocal8Bit()); }
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("settings.ini"))); ExpandedTranslator translator;
	QTranslator english;
	if (english.load(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../i18n/vibestudio_en.qm")))) { app.installTranslator(&english); }
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		if (scale == 200) { app.installTranslator(&translator); }
		PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
		ok &= plan.addBytes("retained", QStringLiteral("one.txt"), &error);
		ok &= plan.addBytes("redo", QStringLiteral("two.txt"), &error); ok &= plan.undo();
		const QString draft = root.filePath(QStringLiteral("saved-%1.vibepackage").arg(scale));
		ok &= expect(PackageDraft::save(draft, &plan, false, &error), "prepare UI draft fixture");
		if (!ok) { std::cerr << error.toStdString(); return 1; }
		const QByteArray unused("unused");
		const QString orphan = QDir(draft).filePath(QStringLiteral("objects/") + QString::fromLatin1(QCryptographicHash::hash(unused, QCryptographicHash::Sha256).toHex()));
		ok &= write(orphan, unused);
		PackageDraftStorageDialog dialog(draft); dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); } dialog.show();
		auto* compact = dialog.findChild<QPushButton*>(QStringLiteral("packageDraftStorageCompact"));
		auto* bytes = dialog.findChild<QSpinBox*>(QStringLiteral("packageDraftMaximumMiB"));
		auto* files = dialog.findChild<QSpinBox*>(QStringLiteral("packageDraftMaximumFiles"));
		auto* details = dialog.findChild<QPlainTextEdit*>(QStringLiteral("packageDraftStorageDetails"));
		auto* usage = dialog.findChild<QLabel*>(QStringLiteral("packageDraftStorageUsage"));
		auto* status = dialog.findChild<QLabel*>(QStringLiteral("packageDraftStorageStatus"));
		ok &= expect(compact && bytes && files && details && usage && status, "storage controls exist"); if (!ok) { return 1; }
		ok &= expect(wait([&]() { return !dialog.busy() && compact->isEnabled(); }), "worker verifies storage before enabling maintenance");
		ok &= expect(!compact->accessibleName().isEmpty() && compact->focusPolicy() != Qt::NoFocus
			&& !files->accessibleName().isEmpty() && details->isReadOnly() && !details->accessibleName().isEmpty()
			&& usage->wordWrap() && status->wordWrap(), "storage controls expose names, focus and wrapping");
		bytes->setValue(256); files->setValue(1);
		ok &= expect(StudioSettings().packageDraftMaximumMiB() == 256 && StudioSettings().packageDraftMaximumFiles() == 1, "quota controls persist preferences");
		{
			const QString rejected = root.filePath(QStringLiteral("over-limit-%1.vibepackage").arg(scale));
			const auto result = runPackageDraftSaveDialog(nullptr, plan, rejected, false);
			ok &= expect(!result.ready() && !result.error.isEmpty() && !QFileInfo::exists(rejected), "GUI draft save enforces configured limits without creating an output");
		}
		files->setValue(200000);
		bool confirmed = false;
		const auto confirm = [&]() {
			QTimer::singleShot(0, [&]() {
				if (auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
					if (auto* button = question->button(QMessageBox::Discard)) { confirmed = true; button->click(); }
				}
			});
		};
		confirm(); compact->click();
		ok &= expect(confirmed && wait([&]() { return !dialog.busy() && status->text().contains(QStringLiteral("in use")); })
			&& QFileInfo::exists(orphan), "confirmed maintenance refuses a live draft reader");
		auto* close = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close);
		ok &= expect(close->isVisible() && dialog.rect().contains(QRect(close->mapTo(&dialog, QPoint()), close->size()))
			&& close->focusPolicy() != Qt::NoFocus, "Close remains visible and focusable at each text scale");
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			ok &= QDir().mkpath(captures); auto* scroll = dialog.findChild<QScrollArea*>(); scroll->verticalScrollBar()->setValue(0); QApplication::processEvents();
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-draft-storage-%1-top.png").arg(scale))), "save storage overview render");
			auto* refresh = dialog.findChild<QPushButton*>(QStringLiteral("packageDraftStorageRefresh"));
			ok &= expect(wait([&]() {
				scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
				return scroll->viewport()->rect().contains(QRect(refresh->mapTo(scroll->viewport(), QPoint()), refresh->size()))
					&& scroll->viewport()->rect().contains(QRect(compact->mapTo(scroll->viewport(), QPoint()), compact->size()));
			}), "scaled storage actions remain reachable at the end of the scroll area");
			QApplication::processEvents();
			image.fill(Qt::transparent); dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-draft-storage-%1-actions.png").arg(scale))), "save storage action render");
		}
		plan.clear(); confirmed = false; confirm(); compact->click();
		ok &= expect(confirmed && wait([&]() { return !dialog.busy() && !QFileInfo::exists(orphan); }), "closed draft permits reviewed storage reclamation");
		PackageStagingModel loaded; QByteArray redo;
		ok &= expect(PackageDraft::load(draft, &loaded, &error) && loaded.redo()
			&& PackageStagingArchive(loaded).readEntryBytes(QStringLiteral("two.txt"), &redo, &error) && redo == "redo", "GUI compaction preserves undo and redo content");
		loaded.clear(); dialog.reload(); dialog.reject();
		ok &= expect(wait([&]() { return !dialog.busy() && !dialog.isVisible(); }), "closing a busy review cancels and joins its worker");
		app.removeTranslator(&translator);
	}
	{
		PackageRecoveryDialog recovery(root.filePath(QStringLiteral("recoveries"))); recovery.setAttribute(Qt::WA_DeleteOnClose, false); recovery.show();
		auto* button = recovery.findChild<QPushButton*>(QStringLiteral("packageSavedDraftStorage"));
		ok &= expect(button && !button->accessibleName().isEmpty(), "recovery manager links saved-draft storage");
		if (button) {
			button->click(); auto* storage = recovery.findChild<QDialog*>(QStringLiteral("packageDraftStorageDialog"));
			ok &= expect(storage != nullptr, "saved draft storage opens from shared recovery manager");
			if (storage) { storage->close(); QApplication::processEvents(); }
		}
		recovery.reject(); wait([&]() { return !recovery.busy(); });
	}
	QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup();
	std::cout << (ok ? "Package draft storage UI smoke passed\n" : "Package draft storage UI smoke failed\n"); return ok ? 0 : 1;
}
