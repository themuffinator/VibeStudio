#pragma once

// idTech model geometry.
//
// `asset_tools` reads model headers for metadata; this module decodes the
// geometry itself so a model can actually be drawn, exported, and validated
// against the skins it asks for.
//
// Format knowledge comes from public specifications and released id Software
// sources:
// - Quake MDL (IDPO 6): the Quake Specifications, chapter 5
//   (https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_5.htm) and
//   the released Quake source `modelgen.h`.
// - Quake II MD2 (IDP2 8): the released Quake II source `qfiles.h`, and the
//   normal-index table from `anorms.h`.
// - Quake III MD3 (IDP3 15): the released Quake III Arena source `md3.h`.
// - MDC, MDR and IQM headers: the Return to Castle Wolfenstein, Elite Force and
//   Inter-Quake Model public format documentation.
//
// No commercial model, skin, or animation data is embedded here. Vertex normals
// for MDL and MD2 are reconstructed from the published normal table, which is a
// mathematical constant of the format rather than game content.

#include "core/idtech_image.h"
#include "core/model_work.h"

#include <QImage>
#include <QPair>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

class PackageArchiveReader;

enum class ModelMeshFormat {
	Unknown,
	QuakeMdl,       // IDPO version 6
	Quake2Md2,      // IDP2 version 8
	Quake3Md3,      // IDP3 version 15
	Mdc,            // header only
	Mdr,            // header only
	Iqm,            // header only
	WavefrontObj,   // bounded polygonal interchange
};

struct ModelVec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct ModelTriangle {
	// Indices into the surface's vertex list for the current frame.
	// Counter-clockwise front faces: cross(b-a, c-a) points outward. Native
	// MDL/MD2/MD3 clockwise order is converted only at import/export boundaries.
	int a = 0;
	int b = 0;
	int c = 0;
};

struct ModelTexCoord {
	float u = 0.0f;
	float v = 0.0f;
};

// One animation frame's worth of positions for a single surface. Positions and
// normals are parallel arrays indexed the same way as `ModelSurface::texCoords`.
struct ModelFrameGeometry {
	QVector<ModelVec3> positions;
	QVector<ModelVec3> normals;
};

struct ModelSurface {
	int index = -1;
	QString name;
	int vertexCount = 0;
	QVector<ModelTriangle> triangles;
	QVector<ModelTexCoord> texCoords;
	// One entry per model frame, each holding `vertexCount` positions.
	QVector<ModelFrameGeometry> frames;
	QStringList skinPaths;
	QStringList warnings;
	// Authoring seam marks partition UV charts before their coordinates diverge.
	// Canonical indexed edge pairs; native game files retain the resulting UVs,
	// while these editable-source marks remain in .mesh.json.
	QSet<QPair<int, int>> uvSeams{};
};

struct ModelFrameInfo {
	int index = -1;
	QString name;
	ModelVec3 mins;
	ModelVec3 maxs;
	ModelVec3 origin;
	float radius = 0.0f;
};

// A named animation inferred from frame names, which is how MDL and MD2 encode
// animations: consecutive frames sharing a name stem with a trailing number.
struct ModelAnimation {
	QString name;
	int firstFrame = 0;
	int frameCount = 0;
	// Zero leaves playback timing unspecified. Positive rates persist in mesh v6.
	double framesPerSecond = 0;
};

struct ModelTag {
	QString name;
	int frameIndex = 0;
	ModelVec3 origin;
	// Row-major 3x3 orientation.
	float axis[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};

struct ModelCollisionPose {
	ModelVec3 centre;
	ModelVec3 size{16, 16, 16};
	ModelVec3 rotation;
};

// Source-only collision boxes. Rotation is in model axes,
// X then Y then Z, matching the shared transform service. Game mesh formats do
// not carry these volumes; explicit map handoff turns them into clip brushes.
struct ModelCollisionBox {
	QString name;
	ModelVec3 centre;
	ModelVec3 size{16, 16, 16};
	ModelVec3 rotation;
	// Empty for a static box; otherwise exactly one pose per mesh frame.
	// The scalar fields mirror frame zero for compatibility. Use
	// setModelCollisionFrames to replace a track and maintain that invariant.
	QVector<ModelCollisionPose> framePoses{};
};

// A skin stored inside the model itself. MDL carries indexed pixels that need a
// palette; MD2 and MD3 reference external files instead.
struct ModelEmbeddedSkin {
	int index = -1;
	QString name;
	QImage image;
	int groupFrameCount = 0;
	// MDL pixels retain their original palette indices, including fullbright and
	// player-colour ranges. image is a derived preview of the first member only.
	QVector<QByteArray> indexedFrames{};
	// Empty for a single skin; otherwise strictly increasing cumulative seconds.
	QVector<float> intervals{};
};

struct ModelMdlFrameGroup {
	int firstFrame = 0;
	// Empty denotes one native single frame. A nonempty array denotes a native
	// group, including a group of one, and gives each pose's cumulative end time.
	QVector<float> intervals{};
	[[nodiscard]] int frameCount() const { return intervals.isEmpty() ? 1 : int(intervals.size()); }
};

struct ModelMdlSettings {
	bool enabled = false;
	QSize skinSize{256, 256};
	// The file carries indices, not colours. These RGB triplets are retained for
	// source/preview fidelity; exporting MDL does not embed or replace a palette.
	QByteArray palette{};
	bool paletteGenerated = false;
	ModelVec3 eyePosition{};
	quint32 flags = 0;
	int syncType = 0;
	float size = 0;
	QVector<ModelMdlFrameGroup> frameGroups{};
};

struct ModelMesh {
	QString sourcePath;
	ModelMeshFormat format = ModelMeshFormat::Unknown;
	QString formatId;
	QString formatName;
	int version = 0;
	bool geometryAvailable = false;
	QVector<ModelSurface> surfaces;
	QVector<ModelFrameInfo> frames;
	QVector<ModelAnimation> animations;
	QVector<ModelTag> tags;
	QVector<ModelEmbeddedSkin> embeddedSkins;
	QStringList skinPaths;
	int frameCount = 0;
	int surfaceCount = 0;
	int vertexCount = 0;
	int triangleCount = 0;
	int tagCount = 0;
	int skinCount = 0;
	ModelVec3 mins;
	ModelVec3 maxs;
	QStringList detailLines;
	QStringList warnings;
	QString error;
	// MD2 ST coordinates are measured in pixels of this skin. Retained on import
	// and in editable sources; other formats and older sources start at 256x256.
	QSize md2SkinSize{256, 256};
	ModelMdlSettings mdl{};
	QVector<ModelCollisionBox> collisionBoxes{};

	[[nodiscard]] bool isValid() const;
	// Bounds across every frame, used to frame the model in a viewport.
	[[nodiscard]] float boundingRadius() const;
};

// Nearest direction in the shared 162-entry MDL/MD2 normal table.
int modelAliasNormalIndex(const ModelVec3& normal);
ModelVec3 modelAliasNormal(int index);
// Consistency audit for the two MD2 render streams. Decode retains failures as
// surface warnings, so every editable adoption route refuses lossy data.
bool validateModelMd2Commands(const QByteArray& bytes, QString* error = nullptr, const ModelWorkControl& control = {});

QString modelMeshFormatId(ModelMeshFormat format);
QString modelMeshFormatDisplayName(ModelMeshFormat format);
ModelMeshFormat detectModelMeshFormat(const QString& virtualPath, const QByteArray& bytes);

// `palette` colours MDL's embedded indexed skins. Pass a resolved package
// palette; a null pointer falls back to the generated Quake ramp.
ModelMesh decodeModelMesh(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette* palette = nullptr,
	const ModelWorkControl& control = {});
ModelMesh decodeModelMeshFromArchive(const PackageArchiveReader& archive, const QString& virtualPath, const QString& paletteId = QString(),
	const ModelWorkControl& control = {});

// Resolves each `skinPaths` entry against the archive, trying the idTech image
// extensions, and returns the first that decodes. Empty when none resolve.
QStringList modelSkinCandidatePaths(const QString& skinPath);
QImage resolveModelSkin(const PackageArchiveReader& archive, const ModelMesh& mesh, const QString& paletteId = QString(), QString* resolvedPathOut = nullptr);

QStringList modelMeshSummaryLines(const ModelMesh& mesh);
QString modelMeshSummaryText(const ModelMesh& mesh);

// Wavefront OBJ export of one frame, so a model can leave the studio for an
// external modeller. Returns an empty string when the frame has no geometry.
QString exportModelFrameObj(const ModelMesh& mesh, int frameIndex, const QString& materialName = QString(),
	const ModelWorkControl& control = {}, QString* error = nullptr);

} // namespace vibestudio
