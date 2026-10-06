#pragma once

#include "app/model_viewport.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTimer>
#include <algorithm>

namespace vibestudio::tests
{

// Wait for actual worker completion through the GUI event loop. Rendering the
// widget itself starts pending work even when its containing tab is hidden.
// This uses neither user input nor an operating-system capture surface.
inline bool settleModelViewport(ModelViewport &viewport, int timeoutMs = 20000)
{
	QElapsedTimer elapsed;
	elapsed.start();
	QImage probe(viewport.size(), QImage::Format_ARGB32_Premultiplied);
	while (elapsed.elapsed() < timeoutMs)
	{
		viewport.render(&probe);
		if (!viewport.isRendering())
		{
			return true;
		}
		QEventLoop events;
		QTimer deadline;
		deadline.setSingleShot(true);
		QObject::connect(&viewport, &ModelViewport::renderCompleted, &events, &QEventLoop::quit);
		QObject::connect(&deadline, &QTimer::timeout, &events, &QEventLoop::quit);
		deadline.start(std::max(1, timeoutMs - int(elapsed.elapsed())));
		events.exec();
	}
	return false;
}

} // namespace vibestudio::tests
