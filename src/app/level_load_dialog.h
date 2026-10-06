#pragma once

#include "core/level_map.h"
#include "core/map_geometry_cache.h"
#include <QDialog>
#include <memory>

namespace vibestudio {
struct LevelMapOpenResult {
	LevelMapDocument document;
	std::shared_ptr<MapBrushGeometryCache> geometry;
	QString error;
	bool succeeded = false;
	bool cancelled = false;
};

// Window-modal authoring operation. Parsing and geometry run on a worker while
// the GUI event loop remains live. Closing requests cancellation and waits for
// acknowledgement; the caller decides whether to adopt the returned document.
class LevelMapLoadDialog final : public QDialog {
	Q_OBJECT
public:
	static LevelMapOpenResult openMap(QWidget* parent, const LevelMapLoadRequest& request);
	void reject() override;
protected:
	void closeEvent(QCloseEvent* event) override;
private:
	explicit LevelMapLoadDialog(QWidget* parent);
	std::function<void()> m_cancel;
};
} // namespace vibestudio
