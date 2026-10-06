#pragma once

#include "app/model_uv_view.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <algorithm>

namespace vibestudio::tests
{
inline bool settleModelUv(ModelUvView &view, int timeoutMs = 20000)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (view.isRendering() && elapsed.elapsed() < timeoutMs)
	{
		QEventLoop events;
		QTimer deadline;
		deadline.setSingleShot(true);
		QObject::connect(&view, &ModelUvView::renderCompleted, &events, &QEventLoop::quit);
		QObject::connect(&deadline, &QTimer::timeout, &events, &QEventLoop::quit);
		deadline.start(std::max(1, timeoutMs - int(elapsed.elapsed())));
		events.exec();
	}
	return !view.isRendering() && !view.pixmap().isNull();
}
} // namespace vibestudio::tests
