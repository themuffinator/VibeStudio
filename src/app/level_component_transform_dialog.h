#pragma once
#include "core/level_brush.h"
#include <QDialog>
#include <array>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace vibestudio {
class LevelComponentTransformDialog final : public QDialog {
	Q_OBJECT
public:
	explicit LevelComponentTransformDialog(const LevelComponentTransformRequest& initial, const LevelMapVec3& centre, QWidget* parent = nullptr);
	LevelComponentTransformRequest request() const;
	void setApplyHandler(std::function<bool(const LevelComponentTransformRequest&, QString*)> handler);
	// What the transform includes beyond picked handles; hidden when empty.
	void setScopeSummary(const QString& text);
private:
	QLabel* m_scope = nullptr;
	QComboBox* m_operation = nullptr;
	QComboBox* m_rotationAxis = nullptr;
	QComboBox* m_shearAxis = nullptr;
	QComboBox* m_shearAlong = nullptr;
	QDoubleSpinBox* m_angle = nullptr;
	QDoubleSpinBox* m_factor = nullptr;
	QDoubleSpinBox* m_grid = nullptr;
	std::array<QDoubleSpinBox*,3> m_delta{}, m_scale{}, m_pivot{};
	QCheckBox* m_customPivot = nullptr;
	QCheckBox* m_collapse = nullptr;
	QCheckBox* m_uvLock = nullptr;
	QCheckBox* m_uvFit = nullptr;
	QCheckBox* m_valve = nullptr;
	QLabel* m_error = nullptr;
	std::function<bool(const LevelComponentTransformRequest&, QString*)> m_apply;
};
} // namespace vibestudio
