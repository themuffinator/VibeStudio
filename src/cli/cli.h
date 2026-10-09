#pragma once

#include <QStringList>

namespace vibestudio::cli {

int run(const QStringList& args);

// Whether the command draws in 3D (material render, render backends, render
// test). main() gives such commands a GUI application without a window,
// because Qt offers OpenGL only to one.
bool commandUsesRenderer(const QStringList& args);

} // namespace vibestudio::cli
