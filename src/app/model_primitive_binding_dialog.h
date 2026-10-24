#pragma once

#include "app/live_status_label.h"
#include "core/model_mesh.h"
#include "core/model_vertex_origins.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace vibestudio
{
// Chooses how Add Primitive's new vertices follow the skeleton when the
// target surface is skinned. New geometry has no source vertex to take
// weights from, so the edit needs an explicit binding: rigidly to one joint
// (the selected joint by default), to the nearest bones, or not at all as a
// new unskinned surface. A bound primitive can also become a surface of its
// own. See core/model_vertex_origins.h.
class ModelPrimitiveBindingDialog final : public QDialog
{
  public:
	// `joint` preselects the rigid joint; `binding` the remembered choice (None
	// for an unskinned new surface); `newSurface` whether a bound primitive
	// gets its own surface.
	ModelPrimitiveBindingDialog(const ModelMesh &mesh, int surface, const QString &joint, ModelNewGeometryBinding binding,
		bool newSurface, QWidget *parent = nullptr);
	// None when the primitive is added as a new unskinned surface.
	[[nodiscard]] ModelNewGeometryBinding binding() const;
	// The rigid joint; empty unless binding() is Joint.
	[[nodiscard]] QString joint() const;
	// The new surface's name, skinned or not; empty when the primitive joins
	// the selected surface.
	[[nodiscard]] QString newSurface() const;

  private:
	void refreshState();
	QStringList m_surfaceNames;
	QRadioButton *m_rigid = nullptr, *m_nearest = nullptr, *m_separate = nullptr;
	QCheckBox *m_ownSurface = nullptr;
	QComboBox *m_joint = nullptr;
	QLineEdit *m_name = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr;
};
} // namespace vibestudio
