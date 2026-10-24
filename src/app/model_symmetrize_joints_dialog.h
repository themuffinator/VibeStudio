#pragma once

#include "app/live_status_label.h"
#include "core/model_mesh.h"

#include <QDialog>
#include <QMap>
#include <QString>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace vibestudio
{
// Confirms the joint mirror map Symmetrize applies to a skinned surface. One
// row lists each off-centre joint that weights the surface with its mirror
// joint; Detect Pairs fills them from left/right naming
// (detectModelMirrorJointPairs), keeping only pairs on opposite sides of the
// mirror plane, and Keep joint names binds the mirrored half to the same
// joints instead. Each mirror cell's accessible name carries its value. The
// map is validated again by the edit itself.
class ModelSymmetrizeJointsDialog final : public QDialog
{
  public:
	// `joints` is the remembered map; only its pairs that mirror across the
	// plane on `axis` are filled in. `tolerance` is how far a joint may sit from
	// the plane and still keep its name, as for the edit.
	ModelSymmetrizeJointsDialog(const ModelMesh &mesh, int surface, int axis, const QMap<QString, QString> &joints, bool keepNames,
		double tolerance = 0.001, QWidget *parent = nullptr);
	[[nodiscard]] QMap<QString, QString> mirrorJoints() const;
	[[nodiscard]] bool keepJointNames() const;
	void detectPairs();

  private:
	void refreshStatus();
	// Only the skeleton, for the joint pair helpers.
	ModelMesh m_rig;
	int m_axis = 0;
	double m_tolerance = 0.001;
	QTableWidget *m_table = nullptr;
	QCheckBox *m_keep = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_detect = nullptr;
};
} // namespace vibestudio
