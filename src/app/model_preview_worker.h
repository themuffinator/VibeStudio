#pragma once

#include "app/model_material_worker.h"
#include "core/model_appearance.h"
#include "core/package_preview.h"

namespace vibestudio
{
struct ModelPreviewResult
{
	QString key, path;
	ModelMesh mesh;
	LevelPreviewAssets assets;
	PackagePreview metadata;
	ModelAppearanceSnapshot appearance;
	QString error;
	bool cancelled = false;
};

// One active immutable request, one replaceable pending request. Cancellation
// and generation checks keep retired package snapshots out of the browser.
class ModelPreviewWorker final : public QObject
{
  public:
	explicit ModelPreviewWorker(QObject *parent = nullptr);
	~ModelPreviewWorker() override;
	void request(ModelMaterialSource source, QString path, QString key, ModelAppearance appearance = {});
	void reset();
	void cancel();
	bool busy() const;
	std::function<void(const ModelPreviewResult &)> completed;
	std::function<void()> settled;

  private:
	struct Request
	{
		ModelMaterialSource source;
		QString path, key;
		ModelAppearance appearance;
	};
	struct Work;
	std::optional<Request> m_pending;
	QThread *m_thread = nullptr;
	QTimer *m_timer = nullptr;
	quint64 m_serial = 0;
	QString m_key, m_path;
	void startNext();
};
} // namespace vibestudio
