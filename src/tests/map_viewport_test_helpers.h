#pragma once

#include "app/map_viewport.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTimer>
#include <algorithm>

namespace vibestudio::tests {

// Render the real widget and wait through the GUI event loop for its current
// background image. The supplied image retains its physical device scale. No
// user input is injected or dispatched and no OS capture surface is involved.
inline bool settleMapViewport(MapViewport& viewport, QImage* image, int timeoutMs = 30000)
{
	if (!image || image->isNull()) { return false; }
	QElapsedTimer elapsed; elapsed.start();
	while (elapsed.elapsed() < timeoutMs) {
		image->fill(Qt::transparent); viewport.render(image);
		if (!viewport.isRendering()) { return true; }
		QEventLoop events; QTimer deadline; deadline.setSingleShot(true);
		QObject::connect(&viewport, &MapViewport::renderCompleted, &events, &QEventLoop::quit);
		QObject::connect(&deadline, &QTimer::timeout, &events, &QEventLoop::quit);
		deadline.start(std::max(1,timeoutMs - int(elapsed.elapsed())));
		events.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return false;
}
} // namespace vibestudio::tests
