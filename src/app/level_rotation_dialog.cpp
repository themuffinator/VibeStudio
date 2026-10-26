#include "app/level_rotation_dialog.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <algorithm>

namespace vibestudio
{
LevelRotationDialog::LevelRotationDialog(const LevelMapDocument& source, int axis, QWidget* parent) : LevelTransformDialog(source, parent)
{
	setObjectName(QStringLiteral("levelRotationDialog"));
	setWindowTitle(tr("Rotate Selection"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Turn the selection about X, then Y, then Z. Apply keeps this window open for another turn."));
	const QStringList labels{tr("About X"), tr("About Y"), tr("About Z")};
	const QStringList names{tr("Rotation about X in degrees"), tr("Rotation about Y in degrees"), tr("Rotation about Z in degrees")};
	const QString description = tr("Positive angles follow the right-hand rule. The selection turns about X first, then Y, then Z.");
	for (int i = 0; i < 3; ++i) {
		m_angles[i] = addValueField(QStringLiteral("rotationAngle%1").arg(QLatin1Char("XYZ"[i])), labels[i], names[i], description, -360000, 360000,
			i == std::clamp(axis, 0, 2) ? 45 : 0, 4, 15);
		m_angles[i]->setSuffix(tr("°"));
	}
	setTextureLockText(tr("Lock textures to faces"),
		tr("Keep the same texture coordinates on each rotated face. Patches always keep their control-point UV coordinates."));
	finishSetup();
}

LevelMapRotationRequest LevelRotationDialog::request() const
{
	LevelMapRotationRequest result;
	result.euler = true;
	for (int i = 0; i < 3; ++i) {
		result.eulerDegrees[i] = m_angles[i]->value();
	}
	// A single turn also reads as one, for callers that look at the axis.
	const auto steps = levelMapRotationSteps(result);
	if (steps.size() == 1) {
		result.axis = steps.first().first;
		result.degrees = steps.first().second;
	}
	result.pivot = pivot();
	result.textureLock = textureLock();
	result.allowValve220 = allowValve220();
	return result;
}

void LevelRotationDialog::setRequest(const LevelMapRotationRequest& value)
{
	setUpdating(true);
	for (int i = 0; i < 3; ++i) {
		m_angles[i]->setValue(value.euler ? value.eulerDegrees[i] : (i == value.axis ? value.degrees : 0.0));
	}
	setPivot(value.pivot);
	setTextureOptions(value.textureLock, value.allowValve220);
	setUpdating(false);
	schedule();
}

void LevelRotationDialog::setApplyHandler(std::function<bool(const LevelMapRotationRequest&, QString*)> handler)
{
	m_applyHandler = std::move(handler);
}

LevelPlacementRequest LevelRotationDialog::placementRequest() const
{
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::Rotate;
	work.rotation = request();
	return work;
}

bool LevelRotationDialog::commit(QString* error)
{
	return !m_applyHandler || m_applyHandler(request(), error);
}

QString LevelRotationDialog::readyText() const
{
	return tr("Ready. The rotation will be one undo step.");
}

QString LevelRotationDialog::requestProblem() const
{
	return levelMapRotationSteps(request()).isEmpty() ? tr("Whole turns change nothing. Enter an angle about X, Y or Z.") : QString();
}

void LevelRotationDialog::sourceChanged()
{
	// Doom maps turn in the top view only.
	const bool doom = sourceDocument().format == LevelMapFormat::DoomWad;
	for (int i = 0; i < 2; ++i) {
		if (doom) {
			m_angles[i]->setValue(0);
		}
		m_angles[i]->setEnabled(!doom);
	}
}
} // namespace vibestudio
