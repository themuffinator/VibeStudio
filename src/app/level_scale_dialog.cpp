#include "app/level_scale_dialog.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <cmath>

namespace vibestudio
{
LevelScaleDialog::LevelScaleDialog(const LevelMapDocument& source, QWidget* parent) : LevelTransformDialog(source, parent)
{
	setObjectName(QStringLiteral("levelScaleDialog"));
	setWindowTitle(tr("Scale Selection"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Multiply the selection's size along X, Y and Z about the pivot. Apply keeps this window open for another step."));
	const QStringList labels{tr("X factor"), tr("Y factor"), tr("Z factor")};
	const QStringList names{tr("Scale factor along X"), tr("Scale factor along Y"), tr("Scale factor along Z")};
	const QString description = tr("1 keeps the size, 2 doubles it, 0.5 halves it. A negative factor mirrors the selection along this axis.");
	for (int i = 0; i < 3; ++i) {
		m_factors[i] = addValueField(QStringLiteral("scaleFactor%1").arg(QLatin1Char("XYZ"[i])), labels[i], names[i], description, -32768, 32768, 1,
			6, 0.25);
	}
	setTextureLockText(tr("Stretch textures with the selection"),
		tr("Texture Scale Lock: keep each brush face's texture fixed to its corners as it grows, shrinks or mirrors. Otherwise the "
		   "written texture parameters stay. Patches always keep their control-point UV coordinates."));
	finishSetup();
}

LevelMapScaleRequest LevelScaleDialog::request() const
{
	LevelMapScaleRequest result;
	for (int i = 0; i < 3; ++i) {
		result.factors[i] = m_factors[i]->value();
	}
	result.pivot = pivot();
	result.textures = {textureLock(), allowValve220()};
	return result;
}

void LevelScaleDialog::setRequest(const LevelMapScaleRequest& value)
{
	setUpdating(true);
	for (int i = 0; i < 3; ++i) {
		m_factors[i]->setValue(value.factors[i]);
	}
	setPivot(value.pivot);
	setTextureOptions(value.textures.enabled, value.textures.allowValve220);
	setUpdating(false);
	schedule();
}

void LevelScaleDialog::setApplyHandler(std::function<bool(const LevelMapScaleRequest&, QString*)> handler)
{
	m_applyHandler = std::move(handler);
}

LevelPlacementRequest LevelScaleDialog::placementRequest() const
{
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::Scale;
	work.scale = request();
	return work;
}

bool LevelScaleDialog::commit(QString* error)
{
	return !m_applyHandler || m_applyHandler(request(), error);
}

QString LevelScaleDialog::readyText() const
{
	const auto factors = request().factors;
	const int mirrored = int(factors[0] < 0) + int(factors[1] < 0) + int(factors[2] < 0);
	if (mirrored > 0) {
		return tr("Ready. The scale will be one undo step. Negative factors mirror the selection; brush faces and patches keep facing outward.");
	}
	return tr("Ready. The scale will be one undo step.");
}

QString LevelScaleDialog::requestProblem() const
{
	const auto factors = request().factors;
	for (double factor : factors) {
		if (std::abs(factor) < 1e-6) {
			return tr("A factor of 0 would flatten the selection. Use a negative factor to mirror it.");
		}
	}
	return factors == std::array<double, 3>{1, 1, 1} ? tr("Factors of 1 keep the size. Enter a factor other than 1.") : QString();
}

void LevelScaleDialog::sourceChanged()
{
	// Doom maps scale in the top view only; sector heights stay.
	const bool doom = sourceDocument().format == LevelMapFormat::DoomWad;
	if (doom) {
		m_factors[2]->setValue(1);
	}
	m_factors[2]->setEnabled(!doom);
}
} // namespace vibestudio
