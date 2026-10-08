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
// - The formats decoded in model_format_*.cpp (MDC, MDR, IQM, MD5, MDS,
//   MDM/MDX, Ghoul 2, Half-Life MDL, Hexen II, Heretic II FM, LightWave, ASE
//   and KVX) name their sources in their own files and in docs/CREDITS.md.
//
// No commercial model, skin, or animation data is embedded here. Vertex normals
// for MDL and MD2 are reconstructed from the published normal table, which is a
// mathematical constant of the format rather than game content.

#include "core/idtech_image.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QImage>
#include <QPair>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

class PackageArchiveReader;

enum class ModelMeshFormat {
	Unknown,
	QuakeMdl,       // IDPO version 6
	Quake2Md2,      // IDP2 version 8
	Quake3Md3,      // IDP3 version 15
	Mdc,            // IDPC version 2: RTCW / Wolfenstein: Enemy Territory compressed MD3
	Mdr,            // RDM5 version 2: skeletal, ioquake3 and Elite Force
	Iqm,            // INTERQUAKEMODEL version 2: skeletal, ioquake3 and later ports
	WavefrontObj,   // bounded polygonal interchange
	Md5Mesh,        // MD5Version 10 text mesh: Doom 3, Quake 4, Prey, ET: Quake Wars
	Md5Anim,        // MD5Version 10 text animation: a skeleton and one clip, no geometry
	Mds,            // MDSW version 4: RTCW skeletal
	Mdm,            // MDMW version 3: Enemy Territory skeletal mesh (bones in an MDX)
	Mdx,            // MDXW version 2: Enemy Territory bones and frames, no geometry
	Glm,            // 2LGM version 6: Ghoul 2 mesh (Jedi Outcast, Jedi Academy, SoF II)
	Gla,            // 2LGA version 6: Ghoul 2 animation, no geometry
	HalfLifeMdl,    // IDST version 10: GoldSrc studio model
	Hexen2Mdl,      // RAPO version 50: Hexen II mission-pack alias model
	LightWave,      // FORM LWO2 / LWOB: LightWave static models (Doom 3, Quake 4)
	Ase,            // *3DSMAX_ASCIIEXPORT: ASCII scene export (Doom 3, q3map2 misc_model)
	HereticFm,      // Heretic II flexible model (chunked "header" + "frames")
	Kvx,            // Build-engine voxel model, used by ZDoom-family Doom ports
};

struct ModelVec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

// ---------------------------------------------------------------------------
// Skeletons
//
// The skeletal formats of idTech3 and idTech4 games (MD5, MDS, MDM/MDX, MDR,
// IQM, Ghoul 2) and GoldSrc studio models all reduce to one representation: a
// joint hierarchy with a model-space bind pose, per-vertex joint influences,
// and clips of model-space joint matrices. Every vertex is the weighted sum of
// its influences, each an offset in its joint's bind space carried by that
// joint's matrix:
//
//     position = sum(weight * (M[joint] * offset))
//
// Formats that store bind-pose vertices with inverse bind matrices (IQM,
// Half-Life, Ghoul 2) are converted on import by expressing each vertex in
// its joint's bind space. Decoders also bake the bind pose and every clip into
// ordinary frames (see core/model_skeleton.h), so the viewport, mesh tools and
// vertex-animation exports (MD3, MD2, MDL) work on skeletal models unchanged,
// and skeletal exports (MD5) read the joints and influences kept here.
// ---------------------------------------------------------------------------

struct ModelQuat {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;
};

// An affine transform, row-major 3x4: rotation and scale in the left 3x3,
// translation in the last column. Points are column vectors: p' = M * p.
struct ModelJointMatrix {
	float m[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

struct ModelJoint {
	QString name;
	// An earlier joint, or -1 for a root.
	int parent = -1;
	// The bind pose in model space.
	ModelJointMatrix bind;
	// Format-specific bits kept for round trips (MD5 anim component flags,
	// MDS/MDM bone flags, Ghoul 2 bone flags).
	quint32 flags = 0;
};

struct ModelJointInfluence {
	int joint = 0;
	float weight = 0.0f;
	// The vertex position in the joint's bind space.
	ModelVec3 offset;
	// The vertex normal in the joint's bind space; zero when the format stores
	// none, in which case baked normals are rebuilt from the faces.
	ModelVec3 normalOffset;
};

// Parallel to a surface's vertices: vertex v's influences are
// influences[first[v]] .. influences[first[v] + count[v] - 1]. Empty for a
// surface that does not follow the skeleton.
struct ModelSurfaceSkinning {
	QVector<int> first;
	QVector<int> count;
	QVector<ModelJointInfluence> influences;
	[[nodiscard]] bool isEmpty() const { return first.isEmpty(); }
};

// An attachment point that follows a joint: MDS/MDM/MDR/Ghoul 2 tags and
// Doom 3 joints used as attachments. Baked into ModelMesh::tags per frame.
struct ModelSkeletalTag {
	QString name;
	int joint = 0;
	// The tag's transform relative to its joint.
	ModelJointMatrix offset;
};

struct ModelSkeletalClip {
	QString name;
	// The file the clip came from when it is not the mesh file (an md5anim,
	// an MDX or a GLA); empty when it is.
	QString sourcePath;
	// Zero leaves playback timing unspecified.
	double framesPerSecond = 0.0;
	bool loops = true;
	// One entry per frame, each holding one model-space matrix per joint.
	QVector<QVector<ModelJointMatrix>> frames;
	// Optional per-frame bounds from the file, parallel to `frames`.
	QVector<ModelVec3> frameMins;
	QVector<ModelVec3> frameMaxs;
};

struct ModelSkeleton {
	QVector<ModelJoint> joints;
	QVector<ModelSkeletalClip> clips;
	QVector<ModelSkeletalTag> tags;
	// The format family the skeleton came from: "md5", "mds", "mdm", "mdr",
	// "iqm", "ghoul2" or "studio".
	QString sourceFormat;
	// Which baked frame range belongs to which clip; -1 for the bind pose.
	// Filled by bakeModelSkeleton (core/model_skeleton.h).
	QVector<int> bakedClipForFrame;
	[[nodiscard]] bool isEmpty() const { return joints.isEmpty(); }
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
	// Joint influences for skeletal models, parallel to the vertices.
	ModelSurfaceSkinning skinning{};
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
	// Skeletal models keep their joints, clips and joint-following tags here;
	// the frames above hold the baked bind pose and clips.
	ModelSkeleton skeleton{};
	// Companion files the decode read besides the model itself (md5anim, MDX,
	// GLA, Half-Life texture or sequence files), in the order read.
	QStringList companionPaths{};

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

// Files a model refers to besides itself: the animations beside an MD5 mesh,
// the MDX an MDM's bones live in, the GLA a Ghoul 2 mesh names, a Half-Life
// model's texture and sequence files. Paths use '/'. A path the model names
// relative to the game folder ("models/players/_humanoid/_humanoid.gla") is
// tried as given; the file-system source also tries it against every folder
// above the model. Either function may be empty, which disables that lookup.
struct ModelCompanionSource {
	// Reads one file; false when it does not exist or cannot be read.
	std::function<bool(const QString& path, QByteArray* bytes, QString* error)> read;
	// The files directly inside `directory` whose lower-case suffix is one of
	// `suffixes` (without the dot), as full paths in the source's terms, sorted.
	std::function<QStringList(const QString& directory, const QStringList& suffixes)> list;
	// What one decode may read besides the model itself.
	int maxFiles = 128;
	qint64 maxBytes = 256LL * 1024LL * 1024LL;
};

// Companions from inside a package, by virtual path.
[[nodiscard]] ModelCompanionSource modelCompanionsFromArchive(const PackageArchiveReader& archive, const ModelWorkControl& control = {});
// Companions on disk around a loose model file.
[[nodiscard]] ModelCompanionSource modelCompanionsFromFileSystem(const QString& modelPath);

// `palette` colours MDL's embedded indexed skins. Pass a resolved package
// palette; a null pointer falls back to the generated Quake ramp.
ModelMesh decodeModelMesh(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette* palette = nullptr,
	const ModelWorkControl& control = {});
// The same, reading companion files (animations, bones, textures) through
// `companions`. Formats that need none ignore it.
ModelMesh decodeModelMesh(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette* palette,
	const ModelWorkControl& control, const ModelCompanionSource& companions);
// Decodes a loose file on disk with its companions.
ModelMesh decodeModelMeshFile(const QString& path, const IdTechPalette* palette = nullptr, const ModelWorkControl& control = {});
ModelMesh decodeModelMeshFromArchive(const PackageArchiveReader& archive, const QString& virtualPath, const QString& paletteId = QString(),
	const ModelWorkControl& control = {});

// True when the format stores joints (whether or not this file holds
// geometry), so a skeleton panel and skeletal exports apply.
[[nodiscard]] bool modelMeshFormatIsSkeletal(ModelMeshFormat format);
// True when the file holds only animation (md5anim, MDX, GLA): it decodes to
// a skeleton and clips with no surfaces.
[[nodiscard]] bool modelMeshFormatIsAnimationOnly(ModelMeshFormat format);
// The lower-case file suffixes (without the dot) the studio reads as models.
[[nodiscard]] QStringList modelMeshFileSuffixes();

// The embedded skin a surface shows: the one named by its first skin path
// (Half-Life textures, KVX palettes), else the model's first. Null when the
// model embeds none.
[[nodiscard]] const ModelEmbeddedSkin* modelEmbeddedSkinForSurface(const ModelMesh& mesh, int surface);

// What the studio knows about one format, for `model formats`, the format
// pickers and the docs.
struct ModelFormatCapability {
	ModelMeshFormat format = ModelMeshFormat::Unknown;
	QString id;
	QString name;
	QStringList suffixes;
	// "idtech1" (through Doom source ports), "idtech2", "idtech3", "idtech4",
	// "goldsrc" or "interchange".
	QStringList engines;
	// Untranslated game and port names.
	QStringList games;
	bool skeletal = false;
	bool animationOnly = false;
	// What the decoder reads besides the file, as suffixes or patterns.
	QStringList companions;
	// The studio writes this format too (see modelExportFormatIds).
	bool writes = false;
	// Translated: what is kept and what is not.
	QString notes;
};
[[nodiscard]] QVector<ModelFormatCapability> modelFormatCapabilities();

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
