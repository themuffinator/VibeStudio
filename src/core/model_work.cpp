#include "core/model_work.h"

#include <QCoreApplication>

namespace vibestudio
{
bool modelWorkCheckpoint(const ModelWorkControl &control, ModelWorkPhase phase, qint64 completed, qint64 total, QString *error)
{
	const auto cancelled = [&]
	{
		if (!control.cancelled || !control.cancelled())
		{
			return false;
		}
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelDocument", "Model operation cancelled.");
		}
		return true;
	};
	if (cancelled())
	{
		return false;
	}
	if (control.progress)
	{
		control.progress(phase, completed, total);
	}
	return !cancelled();
}
} // namespace vibestudio
