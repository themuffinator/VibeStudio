#pragma once

// Doom 3-family MD5 output (Doom 3, Quake 4, Prey, Enemy Territory: Quake
// Wars) and Doom 3 model declarations (`model` blocks in def/*.def).
//
// Format knowledge: the released Doom 3 GPL source (neo/renderer/Model_md5.cpp,
// neo/d3xp/anim/Anim.cpp, neo/framework/DeclManager.cpp) and the community
// MD5 documentation; see docs/CREDITS.md. Decoding lives in
// model_format_md5.cpp behind decodeModelMesh.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct ModelMd5ExportOptions {
	// The "commandline" header value; Doom 3 writes the exporter's options.
	QString commandLine;
	// md5anim frame rate when a clip has none.
	int defaultFrameRate = 24;
};

// md5mesh text for every surface. A model without a skeleton gets one
// "origin" joint at the model origin, with its first frame as the bind pose.
// Each surface's shader is its first skin path, else its name.
[[nodiscard]] QByteArray exportModelMd5Mesh(const ModelMesh& mesh, const ModelMd5ExportOptions& options = {}, QString* error = nullptr,
	const ModelWorkControl& control = {});
// md5anim text for skeleton.clips[clipIndex]. Needs a skeleton; every joint
// component is animated, so the file round-trips exactly.
[[nodiscard]] QByteArray exportModelMd5Anim(const ModelMesh& mesh, int clipIndex, const ModelMd5ExportOptions& options = {},
	QString* error = nullptr, const ModelWorkControl& control = {});

// One `model` declaration from a Doom 3 .def file.
struct Doom3ModelDecl {
	QString name;
	QString inherit;
	QString meshPath;
	QString skin;
	ModelVec3 offset;
	// (animation name, md5anim path) in declaration order.
	QVector<QPair<QString, QString>> anims;
	// 1-based line of the `model` keyword.
	int line = 0;
};

// Every `model` declaration in a .def file's text. Other declarations
// (entityDef, skin, ...) are skipped; frame commands inside anim blocks are
// ignored. Problems go to `warnings` with their line numbers.
[[nodiscard]] QVector<Doom3ModelDecl> parseDoom3ModelDecls(const QString& text, QStringList* warnings = nullptr);

} // namespace vibestudio
