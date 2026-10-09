#pragma once

// For tests that check what the 3D renderer draws. Call once the application
// object exists (a QGuiApplication, so OpenGL can be prepared): it returns -1
// when a renderer starts, and otherwise the exit code to stop with: 77, which
// Meson reports as a skip, or 1 when VIBESTUDIO_RENDER_REQUIRE says this
// machine must have one (Linux CI, with Mesa's software drivers).

#include "core/render_device.h"

#include <QtGlobal>

#include <iostream>

namespace vibestudio::test_support {

inline int exitCodeWithoutRenderer(const char* test)
{
	prepareRenderBackends();
	RenderDeviceInfo failure;
	if (activeRenderDevice(&failure)) {
		return -1;
	}
	const std::string reason = failure.error.toStdString();
	if (qEnvironmentVariableIntValue("VIBESTUDIO_RENDER_REQUIRE") != 0) {
		std::cerr << test << ": no 3D renderer starts, and VIBESTUDIO_RENDER_REQUIRE is set. " << reason << '\n';
		return 1;
	}
	std::cout << test << ": skipped, no 3D renderer starts here. " << reason << '\n';
	return 77;
}

} // namespace vibestudio::test_support
