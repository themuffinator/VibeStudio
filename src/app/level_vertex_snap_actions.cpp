#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QStatusBar>

namespace vibestudio {
bool ApplicationShell::snapLevelVertex(const LevelMapSelectionRef& target,int vertex,QString* error)
{
	if (!m_levelMapViewport || !m_levelMapViewport->canSnapPickedVertex()) {
		if (error) { *error = tr("Pick one brush vertex before snapping. Coincident vertices may be picked together."); } return false;
	}
	LevelComponentTransformRequest request;
	if (!levelMapVertexSnapDelta(m_levelMapDocument,m_levelMapViewport->pickedComponents(),target,vertex,&request.delta,error)) { return false; }
	if (request.delta.x == 0 && request.delta.y == 0 && request.delta.z == 0) { statusBar()->showMessage(tr("The picked vertex is already at the target."),4000); return true; }
	for (auto* view : std::as_const(m_levelPlanViews)) { view->finishComponentMove(false); }
	if (m_levelMap3D) { m_levelMap3D->finishComponentMove(false); }
	request.allowCollapse = true; applyLevelComponentUvSettings(&request);
	return applyLevelComponentTransform(request,error);
}
void ApplicationShell::snapLevelVertexFromUi()
{
	if (!m_levelMapViewport || !m_levelMapViewport->canSnapPickedVertex()) { return; }
	const auto load = m_levelMapLoadSerial,revision = m_levelMapDocument.revision; const auto token = levelComponentSelectionToken();
	const auto handles = m_levelMapViewport->componentHandles();
	QDialog dialog(this); dialog.setObjectName(QStringLiteral("levelVertexSnapDialog")); dialog.setWindowTitle(tr("Snap Picked Vertex")); dialog.setAccessibleName(dialog.windowTitle());
	dialog.setLayoutDirection(layoutDirection());
	auto* form = new QFormLayout(&dialog); form->setRowWrapPolicy(QFormLayout::WrapLongRows); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	auto* target = new QComboBox; target->setObjectName(QStringLiteral("vertexSnapTarget")); target->setAccessibleName(tr("Target vertex"));
	target->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); target->setMinimumContentsLength(24);
	for (int i = 0; i < handles.size(); ++i) {
		const auto& h = handles[i]; if (h.object.kind != LevelMapSelectionKind::QuakeBrush) { continue; }
		target->addItem(tr("Brush %1 · Vertex %2 · X %3, Y %4, Z %5").arg(h.object.objectId).arg(h.component).arg(h.position.x,0,'g',8).arg(h.position.y,0,'g',8).arg(h.position.z,0,'g',8),i);
		target->setItemData(target->count()-1,target->itemText(target->count()-1),Qt::ToolTipRole);
	}
	const auto describeTarget = [target] { target->setToolTip(target->currentText()); target->setAccessibleDescription(target->currentText()); };
	connect(target,&QComboBox::currentIndexChanged,&dialog,describeTarget); describeTarget();
	form->addRow(tr("Target vertex"),target);
	auto* errorLabel = new QLabel; errorLabel->setObjectName(QStringLiteral("vertexSnapError")); errorLabel->setWordWrap(true); errorLabel->setAccessibleName(tr("Snap result")); form->addRow(errorLabel);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); form->addRow(buttons);
	connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
	connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
		QString error;
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || token != levelComponentSelectionToken()) {
			error = tr("The map or picked vertex changed. Reopen Snap Picked Vertex from the current selection.");
		} else {
			const int index = target->currentData().toInt();
			if (index >= 0 && index < handles.size() && snapLevelVertex(handles[index].object,handles[index].component,&error)) { dialog.accept(); return; }
		}
		errorLabel->setText(error); errorLabel->setAccessibleDescription(error);
	});
	dialog.resize(570,180); dialog.exec();
}
} // namespace vibestudio
