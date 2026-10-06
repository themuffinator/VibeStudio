#pragma once

#include "core/model_document.h"
#include "core/model_transform_axes.h"
#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <cmath>

namespace vibestudio::cli
{
inline bool parseModelTransformAxesOptions(const QHash<QString, QString> &values, int frameCount, ModelEdit *edit, QString *error)
{
	const auto fail = [&](const QString &message)
	{
		if (error) *error = message;
		return false;
	};
	if (!values.contains("--transform-space") && !values.contains("--axis-rotation") && !values.contains("--axes-frame"))
		return true;
	if (edit->kind != ModelEditKind::Transform && edit->kind != ModelEditKind::TransformTag &&
		edit->kind != ModelEditKind::TransformCollisionBox && edit->kind != ModelEditKind::Extrude && edit->kind != ModelEditKind::DuplicateFaces)
		return fail(QCoreApplication::translate("ModelTransformAxesCli", "Transform axes require a mesh, tag or collision transform, face extrusion or duplication."));
	const auto mode = values.value("--transform-space", "world").toLower();
	const int index = QStringList{"world", "selection", "custom"}.indexOf(mode);
	if (index < 0)
		return fail(QCoreApplication::translate("ModelTransformAxesCli", "Use --transform-space world|selection|custom."));
	edit->transformSpace = ModelTransformSpace(index);
	if (values.contains("--axis-rotation"))
	{
		const auto parts = values.value("--axis-rotation").split(',');
		if (edit->transformSpace != ModelTransformSpace::Custom || parts.size() != 3)
			return fail(QCoreApplication::translate("ModelTransformAxesCli", "--axis-rotation requires custom axes and three finite XYZ angles within ±1,000,000 degrees."));
		float angles[3]{};
		for (int i = 0; i < 3; ++i)
		{
			bool valid = false;
			const double angle = parts[i].toDouble(&valid);
			if (!valid || !std::isfinite(angle) || std::abs(angle) > 1000000)
				return fail(QCoreApplication::translate("ModelTransformAxesCli", "--axis-rotation requires custom axes and three finite XYZ angles within ±1,000,000 degrees."));
			angles[i] = float(angle);
		}
		edit->axisRotation = {angles[0], angles[1], angles[2]};
	}
	if (values.contains("--axes-frame"))
	{
		static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
		bool valid = false;
		const auto text = values.value("--axes-frame");
		edit->axesFrame = text.toInt(&valid);
		if (edit->transformSpace != ModelTransformSpace::Selection || !valid || !digits.match(text).hasMatch() ||
			edit->axesFrame < 0 || edit->axesFrame >= frameCount)
			return fail(QCoreApplication::translate("ModelTransformAxesCli", "--axes-frame requires selection axes and an existing zero-based reference pose."));
	}
	return validModelTransformAxesOptions(*edit, error);
}
} // namespace vibestudio::cli
