#include "app/texture_png_export_dialog.h"

#include "core/texture_output.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <algorithm>
#include <memory>
#include <new>

namespace vibestudio {
namespace {
class TexturePngExportDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioTexturePngExport)
public:
	TexturePngExportDialog(QWidget* parent, const QImage& image, const QString& path, bool overwrite,
		std::function<void(const TexturePngExportResult&)> completed) : QDialog(parent), m_completed(std::move(completed))
	{
		setObjectName(QStringLiteral("texturePngExportDialog"));
		setWindowTitle(tr("Export Texture")); setAccessibleName(windowTitle());
		setWindowModality(Qt::WindowModal); setAttribute(Qt::WA_DeleteOnClose);
		resize(520, 180);
		auto* layout = new QVBoxLayout(this);
		m_status = new QLabel(tr("Encoding PNG…"));
		m_status->setObjectName(QStringLiteral("texturePngExportStatus"));
		m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true);
		m_status->setAccessibleName(tr("Texture export status")); layout->addWidget(m_status);
		auto* destination = new QLabel(QDir::toNativeSeparators(path));
		destination->setTextFormat(Qt::PlainText); destination->setWordWrap(true);
		destination->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
		destination->setAccessibleName(tr("Texture export destination")); layout->addWidget(destination);
		m_progress = new QProgressBar; m_progress->setRange(0, 0);
		m_progress->setAccessibleName(tr("Texture export progress")); layout->addWidget(m_progress);
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
		m_action = buttons->button(QDialogButtonBox::Cancel);
		m_action->setObjectName(QStringLiteral("cancelTexturePngExport"));
		m_action->setAccessibleName(tr("Cancel texture export"));
		m_action->setToolTip(tr("Stop encoding before the output is published."));
		connect(m_action, &QPushButton::clicked, this, [this] { if (m_busy) { cancel(); } else { close(); } });
		layout->addWidget(buttons);
		m_work->result.path = path; m_work->result.size = image.size();
		fitContents();
		// One event-loop turn makes the busy/cancel state visible before work starts.
		QTimer::singleShot(0, this, [this, image, path, overwrite] { prepare(image, path, overwrite); });
	}

	~TexturePngExportDialog() override
	{
		m_work->cancelled = true;
		if (m_worker) { m_worker->disconnect(this); m_worker->wait(); delete m_worker; }
	}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		if (m_busy) { cancel(); event->ignore(); } else { QDialog::closeEvent(event); }
	}
	void reject() override { if (m_busy) { cancel(); } else { QDialog::reject(); } }

private:
	struct Work {
		std::atomic_bool cancelled = false;
		TextureOutputTarget target;
		QByteArray bytes;
		TexturePngExportResult result;
	};
	std::shared_ptr<Work> m_work = std::make_shared<Work>();
	std::function<void(const TexturePngExportResult&)> m_completed;
	QThread* m_worker = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_action = nullptr;
	bool m_busy = true, m_publishing = false;

	void fitContents()
	{
		// Keep the chosen width: adjustSize() can narrow a word-wrapped label
		// after its height was measured, clipping the final status or controls.
		layout()->invalidate(); layout()->activate();
		const int width = std::max(this->width(), minimumSizeHint().width());
		const int height = std::max(layout()->totalHeightForWidth(width), minimumSizeHint().height());
		resize(width, height);
	}
	void cancel()
	{
		if (!m_busy || m_publishing) { return; }
		m_work->cancelled = true; m_action->setEnabled(false); m_status->setText(tr("Cancelling texture export…")); fitContents();
	}
	void retireWorker()
	{
		m_worker->wait(); m_worker->deleteLater(); m_worker = nullptr;
	}
	void prepare(const QImage& image, const QString& path, bool overwrite)
	{
		const auto work = m_work;
		m_worker = QThread::create([work, image, path, overwrite] {
			try {
				const auto stopped = [work] { return work->cancelled.load(); };
				if (inspectTextureOutputTarget(path, overwrite, &work->target, &work->result.error, stopped)) {
					work->bytes = encodeTextureOutputPng(image, &work->result.error, stopped);
				}
			} catch (const std::bad_alloc&) { work->result.error = tr("Not enough memory to encode this texture."); }
		});
		connect(m_worker, &QThread::finished, this, [this, work] {
			retireWorker();
			if (work->cancelled || work->bytes.isEmpty()) { finish(); return; }
			// The UI owns the transition. No cancellation can race a successful
			// commit and incorrectly report that the destination was untouched.
			m_publishing = true; m_action->setEnabled(false); m_status->setText(tr("Publishing PNG…"));
			m_worker = QThread::create([work] {
				try { work->result.succeeded = writeTextureOutput(work->target, work->bytes, false, &work->result.error); }
				catch (const std::bad_alloc&) { work->result.error = tr("Not enough memory to publish this texture."); }
			});
			connect(m_worker, &QThread::finished, this, [this] { retireWorker(); finish(); });
			m_worker->start();
		});
		m_worker->start();
	}
	void finish()
	{
		m_busy = false; m_work->bytes.clear();
		m_work->result.cancelled = !m_publishing && m_work->cancelled.load();
		m_progress->hide(); m_action->setEnabled(true); m_action->setText(tr("Close"));
		m_action->setObjectName(QStringLiteral("closeTexturePngExport"));
		m_action->setAccessibleName(tr("Close texture export")); m_action->setToolTip({});
		const auto result = m_work->result; const auto completed = m_completed;
		if (result.succeeded) { QDialog::accept(); }
		else if (result.cancelled) { QDialog::reject(); }
		else { m_status->setText(result.error); fitContents(); m_action->setFocus(); }
		if (completed) { completed(result); }
	}
};
} // namespace

QDialog* runTexturePngExportDialog(QWidget* parent, const QImage& image, const QString& path, bool overwrite,
	std::function<void(const TexturePngExportResult&)> completed)
{
	auto* dialog = new TexturePngExportDialog(parent, image, path, overwrite, std::move(completed));
	dialog->show(); return dialog;
}

} // namespace vibestudio
