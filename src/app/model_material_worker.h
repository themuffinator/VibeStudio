#pragma once

#include "core/level_materials.h"

#include <QObject>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio
{
struct ModelMaterialSource
{
	std::shared_ptr<const PackageArchiveReader> archive;
	// Changes whenever the package snapshot or its staged plan changes.
	QString revision;
	QString paletteId;
};

struct ModelMaterialResult
{
	LevelPreviewAssets assets;
	QString error;
};

// One value-only worker and one replaceable pending request. Geometry edits
// reuse an unchanged material request; source/material changes retire old work.
class ModelMaterialWorker final : public QObject
{
  public:
	explicit ModelMaterialWorker(QObject *parent = nullptr);
	~ModelMaterialWorker() override;
	void request(const ModelMesh &mesh, ModelMaterialSource source, const QHash<int, int> &selectedSlots = {});
	void reset();
	// Keep the request identity so ordinary view refresh does not undo Cancel.
	// Reload uses reset() before requesting the same materials again.
	void cancel();
	[[nodiscard]] bool busy() const;
	std::function<void()> started;
	std::function<void(int completed, int total)> progress;
	std::function<void(const ModelMaterialResult &)> completed;

  private:
	struct Work;
	struct Request
	{
		ModelMesh mesh;
		ModelMaterialSource source;
	};
	QByteArray m_key;
	quint64 m_serial = 0;
	std::optional<Request> m_pending;
	std::shared_ptr<Work> m_work;
	QThread *m_thread = nullptr;
	QTimer *m_progressTimer = nullptr;
	QTimer *m_startTimer = nullptr;
	void stop(bool forgetKey);
	void startNext();
};
} // namespace vibestudio
