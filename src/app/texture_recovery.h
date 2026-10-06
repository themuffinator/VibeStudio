#pragma once

#include "core/texture_recovery.h"

#include <QObject>
#include <QSet>
#include <atomic>
#include <memory>
#include <optional>

class QThread;

namespace vibestudio {

// A single writer and a coalescing queue. Retired in-flight documents are
// cancelled and removed after the worker exits, preventing late resurrection.
class TextureRecoveryWriter final : public QObject {
public:
	explicit TextureRecoveryWriter(QString directory, QObject* parent = nullptr);
	~TextureRecoveryWriter() override;
	bool checkpoint(const QString& id, TextureRecoverySnapshot snapshot);
	void retire(const QString& id);
	[[nodiscard]] bool busy() const;
	[[nodiscard]] int progress() const;
	std::function<void(const QString& id, const QString& path, const QString& error)> finished;

private:
	struct Pending { QString id; QByteArray key; TextureRecoverySnapshot snapshot; };
	struct Result {
		QString id, path, error;
		QByteArray key;
		std::atomic_bool cancelled{false};
		std::atomic_int progress{0};
	};
	void startNext();
	void removeRetired();
	QString m_directory, m_savedId;
	QByteArray m_savedKey;
	std::optional<Pending> m_pending;
	QSet<QString> m_retired;
	QThread* m_thread = nullptr;
	std::shared_ptr<Result> m_result;
};

} // namespace vibestudio
