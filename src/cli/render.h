#pragma once

#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {

struct RenderCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};

// `render <action>`: backends (start each 3D renderer and report what it
// found), test (draw a known frame on each and check every pixel) and set
// (save the renderer the studio and the CLI use), sharing
// core/render_device with the studio's views and Settings. `arguments`
// holds the whole command line after --cli.
RenderCliResult runRenderCommand(const QString& action, const QStringList& arguments);
// The actions runRenderCommand accepts, for routing.
QStringList renderCommandActions();

// Applies the saved renderer preference, then --renderer as an override for
// this run only. False, with `error`, for an unknown --renderer value.
bool applyRendererChoice(const QStringList& arguments, QString* error);

} // namespace vibestudio::cli
