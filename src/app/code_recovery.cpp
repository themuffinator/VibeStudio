#include "app/code_recovery.h"

#include "app/studio_runtime.h"
#include "core/studio_settings.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio {

struct CodeRecoveryWriter::Result { QString id, path, error; int revision = 0; };

CodeRecoveryWriter::CodeRecoveryWriter(QString directory, QObject* parent)
	: QObject(parent), m_directory(std::move(directory))
{
}

CodeRecoveryWriter::~CodeRecoveryWriter()
{
	finished = {};
	if (m_thread) { m_thread->wait(); delete m_thread; m_thread = nullptr; }
	// Normal, approved close retires tabs before destruction. Otherwise keep
	// queued checkpoints, including when the owner is destroyed by a harness.
	for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
		if (!m_retired.contains(it.key())) { writeTextRecovery(it->snapshot, m_directory, it.key()); }
	}
	removeRetired();
}

bool CodeRecoveryWriter::needsCheckpoint(const QString& id, int revision) const
{
	if (m_retired.contains(id)) { return false; }
	const auto queued = m_pending.constFind(id);
	if (queued != m_pending.cend()) { return queued->revision != revision; }
	if (m_thread && m_result && m_result->id == id) { return m_result->revision != revision; }
	return !m_saved.contains(id) || m_saved.value(id) != revision;
}

void CodeRecoveryWriter::checkpoint(const QString& id, int revision, TextRecoverySnapshot snapshot)
{
	if (!needsCheckpoint(id, revision)) { return; }
	m_pending.insert(id, Pending {revision, std::move(snapshot)});
	startNext();
}

void CodeRecoveryWriter::retire(const QString& id)
{
	m_pending.remove(id);
	m_saved.remove(id);
	m_retired.insert(id);
	if (m_thread && m_result && m_result->id == id) { m_thread->requestInterruption(); }
	removeRetired();
}

void CodeRecoveryWriter::removeRetired()
{
	for (auto it = m_retired.begin(); it != m_retired.end();) {
		if (m_thread && m_result && m_result->id == *it) { ++it; continue; }
		QString error;
		if (removeTextRecovery(m_directory, *it, &error)) { it = m_retired.erase(it); }
		else { if (finished) { finished(*it, {}, error); } ++it; }
	}
}

void CodeRecoveryWriter::startNext()
{
	if (m_thread || m_pending.isEmpty()) { return; }
	const auto it = m_pending.begin();
	auto result = std::make_shared<Result>();
	result->id = it.key();
	result->revision = it->revision;
	auto snapshot = std::move(it->snapshot);
	m_pending.erase(it);
	m_result = result;
	const QString directory = m_directory;
	m_thread = QThread::create([snapshot = std::move(snapshot), result, directory]() {
		result->path = writeTextRecovery(snapshot, directory, result->id, &result->error,
			[]() { return QThread::currentThread()->isInterruptionRequested(); });
	});
	connect(m_thread, &QThread::finished, this, [this, result]() {
		m_thread->deleteLater();
		m_thread = nullptr;
		const bool retired = m_retired.contains(result->id);
		if (!retired && !result->path.isEmpty()) { m_saved.insert(result->id, result->revision); }
		removeRetired();
		if (!retired && finished) { finished(result->id, result->path, result->error); }
		startNext();
	});
	m_thread->start();
}

QString chooseTextRecovery(QWidget* parent, const QString& directory)
{
	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("codeRecoveryDialog"));
	dialog.setWindowTitle(QCoreApplication::translate("CodeRecovery", "Recover Text Documents"));
	dialog.setAccessibleName(dialog.windowTitle());
	if (parent) { dialog.setLayoutDirection(parent->layoutDirection()); }
	dialog.resize(std::max(720, dialog.fontMetrics().averageCharWidth() * 70),
		std::max(480, dialog.fontMetrics().lineSpacing() * 24));
	auto* layout = new QVBoxLayout(&dialog);
	auto* automatic = new QCheckBox(QCoreApplication::translate("CodeRecovery", "Keep local recovery copies of unsaved Code edits"));
	automatic->setObjectName(QStringLiteral("codeRecoveryEnabled"));
	automatic->setAccessibleName(automatic->text());
	automatic->setToolTip(QCoreApplication::translate("CodeRecovery", "Checks for edited documents every five seconds. Copies stay on this device and may contain private text. Turning this off keeps existing copies."));
	automatic->setChecked(StudioSettings().codeRecoveryEnabled());
	QObject::connect(automatic, &QCheckBox::toggled, &dialog, [](bool enabled) {
		StudioSettings settings;
		settings.setCodeRecoveryEnabled(enabled);
		settings.sync();
	});
	layout->addWidget(automatic);
	auto* location = new QLabel(QDir::toNativeSeparators(directory));
	location->setWordWrap(true);
	location->setTextFormat(Qt::PlainText);
	location->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	location->setAccessibleName(QCoreApplication::translate("CodeRecovery", "Local recovery folder"));
	layout->addWidget(location);
	auto* list = new QListWidget;
	list->setObjectName(QStringLiteral("codeRecoveryList"));
	list->setAccessibleName(QCoreApplication::translate("CodeRecovery", "Text recovery copies"));
	list->setWordWrap(true);
	layout->addWidget(list, 1);
	auto* progress = new QProgressBar;
	progress->setRange(0, 0);
	progress->setAccessibleName(QCoreApplication::translate("CodeRecovery", "Reading recovery copies"));
	layout->addWidget(progress);
	auto* detail = new QPlainTextEdit;
	detail->setPlainText(QCoreApplication::translate("CodeRecovery", "Reading recovery copies…"));
	detail->setObjectName(QStringLiteral("codeRecoveryDetails"));
	detail->setReadOnly(true);
	detail->setMinimumHeight(dialog.fontMetrics().lineSpacing() * 4);
	detail->setMaximumHeight(dialog.fontMetrics().lineSpacing() * 7);
	detail->setAccessibleName(QCoreApplication::translate("CodeRecovery", "Recovery details"));
	layout->addWidget(detail);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
	auto* restore = buttons->button(QDialogButtonBox::Open);
	restore->setText(QCoreApplication::translate("CodeRecovery", "Restore as Draft"));
	restore->setObjectName(QStringLiteral("restoreCodeRecovery"));
	restore->setEnabled(false);
	auto* discard = buttons->addButton(QCoreApplication::translate("CodeRecovery", "Discard Copy"), QDialogButtonBox::ActionRole);
	discard->setObjectName(QStringLiteral("discardCodeRecovery"));
	discard->setEnabled(false);
	layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	auto result = std::make_shared<TextRecoveryScan>();
	auto* worker = QThread::create([result, directory]() {
		*result = listTextRecoveries(directory, []() { return QThread::currentThread()->isInterruptionRequested(); });
	});
	QObject::connect(worker, &QThread::finished, &dialog, [&]() {
		progress->hide();
		for (int index = 0; index < result->records.size(); ++index) {
			const auto& record = result->records.at(index);
			const QString title = record.title.isEmpty() ? QFileInfo(record.path).fileName() : record.title;
			auto* row = new QListWidgetItem(QStringLiteral("%1\n%2").arg(title,
				record.isValid() ? record.writtenUtc.toLocalTime().toString(Qt::ISODate) : record.error), list);
			row->setData(Qt::UserRole, index);
			row->setToolTip(record.path);
			row->setData(Qt::AccessibleTextRole, row->text());
		}
		detail->setPlainText(!result->error.isEmpty() ? result->error : result->limited
			? QCoreApplication::translate("CodeRecovery", "This scan reached its limit of 128 copies or 64 MiB. Review copies in the recovery folder for the remaining records.")
			: result->records.isEmpty() ? QCoreApplication::translate("CodeRecovery", "No local text recovery copies were found.") : QCoreApplication::translate("CodeRecovery", "Select a copy to review before restoring."));
	});
	QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [&]() {
		const auto* row = list->currentItem();
		if (!row) { restore->setEnabled(false); discard->setEnabled(false); return; }
		const auto& record = result->records.at(row->data(Qt::UserRole).toInt());
		const bool live = record.ownerProcessId > 0 && studioProcessIsRunning(record.ownerProcessId);
		restore->setEnabled(record.isValid());
		discard->setEnabled(!live);
		detail->setPlainText(record.isValid() ? QCoreApplication::translate("CodeRecovery", "%1 bytes · %2\nRestoring opens an unsaved draft and leaves the original file and this copy unchanged.%3")
			.arg(record.payloadBytes).arg(record.sourcePath.isEmpty() ? QCoreApplication::translate("CodeRecovery", "Untitled document") : QDir::toNativeSeparators(record.sourcePath),
				live ? QCoreApplication::translate("CodeRecovery", "\nThe owning studio is still running. Close that document there before discarding its recovery copy.") : QString()) : record.error);
	});
	QObject::connect(discard, &QPushButton::clicked, &dialog, [&]() {
		auto* row = list->currentItem();
		if (!row) { return; }
		const auto record = result->records.at(row->data(Qt::UserRole).toInt());
		if (QMessageBox::question(&dialog, QCoreApplication::translate("CodeRecovery", "Discard Recovery Copy"), QCoreApplication::translate("CodeRecovery", "Permanently discard the selected recovery copy? The original file is unchanged."),
			QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) { return; }
		QString error;
		if (!removeTextRecovery(directory, QFileInfo(record.path).completeBaseName(), &error)) { detail->setPlainText(error); return; }
		delete list->takeItem(list->row(row));
	});
	worker->start();
	const bool accepted = dialog.exec() == QDialog::Accepted;
	worker->requestInterruption();
	worker->wait();
	delete worker;
	const auto* selected = list->currentItem();
	return accepted && selected ? result->records.at(selected->data(Qt::UserRole).toInt()).path : QString();
}

} // namespace vibestudio
