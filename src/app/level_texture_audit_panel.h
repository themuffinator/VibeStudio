#pragma once

#include "core/map_assets.h"
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QProgressBar;
class QPushButton;
class QThread;
class QTimer;

namespace vibestudio {

// One active immutable map/package snapshot and one replaceable pending request.
// Results, progress and cancellation stay bound to the requested context.
class LevelTextureAuditPanel final : public QWidget {
public:
	explicit LevelTextureAuditPanel(QWidget* parent = nullptr);
	~LevelTextureAuditPanel() override;
	void setSource(const QString& key, const LevelMapDocument& document,
		std::shared_ptr<const PackageArchiveReader> archive, bool reducedMotion);
	void cancel();
	OperationState state() const { return m_state; }
	const std::optional<MapTextureAudit>& result() const { return m_result; }
	QString error() const { return m_error; }
	std::function<void()> changed;

private:
	struct Request {
		LevelMapDocument document;
		std::shared_ptr<const PackageArchiveReader> archive;
	};
	struct Work;
	void restart();
	void startNext();
	void refresh();
	QString m_key, m_error;
	quint64 m_serial = 0;
	OperationState m_state = OperationState::Idle;
	bool m_reducedMotion = false;
	std::optional<Request> m_request, m_pending;
	std::optional<MapTextureAudit> m_result;
	std::shared_ptr<Work> m_work;
	QThread* m_thread = nullptr;
	QTimer* m_timer = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_cancel = nullptr;
	QPushButton* m_retry = nullptr;
};

} // namespace vibestudio
