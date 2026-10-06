#include "app/level_texture_audit_panel.h"
#include "app/wrapping_action_button.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <new>

namespace vibestudio {

struct LevelTextureAuditPanel::Work {
	quint64 serial = 0;
	std::atomic_bool cancelled = false;
	std::atomic<qint64> bytes = 0, total = 0;
	MapTextureAudit result;
};

LevelTextureAuditPanel::LevelTextureAuditPanel(QWidget* parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("levelTextureAudit"));
	QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred); policy.setHeightForWidth(true); setSizePolicy(policy);
	setAccessibleName(QCoreApplication::translate("VibeStudioLevelTextureAudit", "Level texture check"));
	auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
	auto* row = new QHBoxLayout;
	m_status = new QLabel; m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);
	auto statusPolicy = m_status->sizePolicy(); statusPolicy.setHorizontalPolicy(QSizePolicy::Ignored);
	m_status->setSizePolicy(statusPolicy); m_status->setMinimumWidth(0);
	// Preserve QLabel's text-derived accessible name and change notifications.
	m_status->setObjectName(QStringLiteral("levelTextureAuditStatus"));
	layout->addWidget(m_status);
	m_cancel = new WrappingActionButton(QCoreApplication::translate("VibeStudioLevelTextureAudit", "Cancel"), 0); m_cancel->setObjectName(QStringLiteral("cancelLevelTextureAudit"));
	m_cancel->setAccessibleName(QCoreApplication::translate("VibeStudioLevelTextureAudit", "Cancel texture check")); m_cancel->setToolTip(m_cancel->accessibleName());
	m_retry = new WrappingActionButton(QCoreApplication::translate("VibeStudioLevelTextureAudit", "Retry"), 0); m_retry->setObjectName(QStringLiteral("retryLevelTextureAudit"));
	m_retry->setAccessibleName(QCoreApplication::translate("VibeStudioLevelTextureAudit", "Retry texture check")); m_retry->setToolTip(m_retry->accessibleName());
	row->addWidget(m_cancel); row->addWidget(m_retry); row->addStretch(1); layout->addLayout(row);
	m_progress = new QProgressBar; m_progress->setTextVisible(false); m_progress->setAccessibleName(QCoreApplication::translate("VibeStudioLevelTextureAudit", "Texture check progress"));
	layout->addWidget(m_progress);
	layout->addStretch(1);
	connect(m_cancel, &QPushButton::clicked, this, &LevelTextureAuditPanel::cancel);
	connect(m_retry, &QPushButton::clicked, this, &LevelTextureAuditPanel::restart);
	m_timer = new QTimer(this); m_timer->setInterval(75);
	connect(m_timer, &QTimer::timeout, this, [this] { refresh(); });
	refresh();
}

LevelTextureAuditPanel::~LevelTextureAuditPanel()
{
	changed = {}; cancel();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}

void LevelTextureAuditPanel::setSource(const QString& key, const LevelMapDocument& document,
	std::shared_ptr<const PackageArchiveReader> archive, bool reducedMotion)
{
	m_reducedMotion = reducedMotion;
	if (key == m_key) { refresh(); return; }
	++m_serial; if (m_work) { m_work->cancelled = true; }
	m_pending.reset(); m_request.reset(); m_result.reset(); m_timer->stop(); m_error.clear(); m_key = key;
	setVisible(document.format != LevelMapFormat::Unknown);
	if (document.format == LevelMapFormat::Unknown || !archive) {
		m_state = OperationState::Idle;
	} else if (!archive->isOpen()) {
		m_state = OperationState::Failed; m_error = archive->errorString();
		if (m_error.isEmpty()) { m_error = QCoreApplication::translate("VibeStudioLevelTextureAudit", "The planned package view is unavailable."); }
	} else {
		m_request = Request{document, std::move(archive)};
		restart(); return;
	}
	refresh(); if (changed) { changed(); }
}

void LevelTextureAuditPanel::cancel()
{
	++m_serial; if (m_work) { m_work->cancelled = true; }
	m_pending.reset(); m_result.reset(); m_state = OperationState::Cancelled; m_timer->stop();
	refresh(); if (changed) { changed(); }
}

void LevelTextureAuditPanel::restart()
{
	if (!m_request) { return; }
	++m_serial; if (m_work) { m_work->cancelled = true; }
	m_result.reset(); m_error.clear(); m_pending = m_request; m_state = OperationState::Loading;
	refresh(); if (changed) { changed(); }
	QTimer::singleShot(0, this, [this] { startNext(); });
}

void LevelTextureAuditPanel::startNext()
{
	if (m_thread || !m_pending) { return; }
	auto request = std::move(*m_pending); m_pending.reset();
	auto work = std::make_shared<Work>(); m_work = work; work->serial = m_serial;
	m_thread = QThread::create([request = std::move(request), work] {
		try {
			PackageReadControl control;
			control.isCancelled = [work] { return work->cancelled.load(); };
			control.progress = [work](const QString&, qint64 bytes, qint64 total) { work->bytes = bytes; work->total = total; };
			work->result = auditLevelMapTextures(request.document, *request.archive, false, control);
		} catch (const std::bad_alloc&) {
			work->result = {}; work->result.complete = false;
			work->result.warnings << QCoreApplication::translate("VibeStudioLevelTextureAudit", "Not enough memory to check the map's textures.");
		}
	});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread->deleteLater(); m_thread = nullptr; m_timer->stop();
		if (m_work == work) { m_work.reset(); }
		if (work->serial == m_serial && !work->cancelled) {
			m_result = std::move(work->result);
			m_state = !m_result->complete ? OperationState::Failed : m_result->state();
			refresh(); if (changed) { changed(); }
		}
		startNext();
	});
	m_timer->start(); m_thread->start();
}

void LevelTextureAuditPanel::refresh()
{
	const bool running = m_state == OperationState::Loading;
	// Hide the departing action first: even transient simultaneous visibility
	// can enlarge the minimum width of a narrow dock or its top-level window.
	if (running) { m_retry->hide(); } else { m_cancel->hide(); }
	m_cancel->setVisible(running); m_retry->setVisible(bool(m_request) && !running);
	m_progress->setVisible(running);
	QString text;
	if (running) {
		text = QCoreApplication::translate("VibeStudioLevelTextureAudit", "Checking planned package textures…");
		const qint64 total = m_work && m_work->serial == m_serial ? m_work->total.load() : 0;
		const qint64 bytes = m_work && m_work->serial == m_serial ? m_work->bytes.load() : 0;
		m_progress->setRange(0, total > 0 ? 1000 : m_reducedMotion ? 1 : 0);
		m_progress->setValue(total > 0 ? int(qMin(1000.0, 1000.0 * double(bytes) / double(total))) : 0);
	} else if (m_state == OperationState::Cancelled) { text = QCoreApplication::translate("VibeStudioLevelTextureAudit", "Texture check cancelled."); }
	else if (m_state == OperationState::Failed) { text = QCoreApplication::translate("VibeStudioLevelTextureAudit", "Texture check incomplete."); }
	else if (!m_request) { text = QCoreApplication::translate("VibeStudioLevelTextureAudit", "Open a package to check the map's textures."); }
	else if (m_result && m_result->missingCount) { text = QCoreApplication::translate("VibeStudioLevelTextureAudit", "%n missing texture(s)", nullptr, m_result->missingCount); }
	else { text = QCoreApplication::translate("VibeStudioLevelTextureAudit", "Texture check complete."); }
	m_status->setText(text); m_status->setAccessibleDescription(m_error);
	m_status->setToolTip(m_error); setProperty("auditState", operationStateId(m_state));
}

} // namespace vibestudio
