#pragma once

// Compiled artifact inspection for idTech BSP families and the auxiliary files
// the compilers emit next to them.
//
// Format knowledge comes from public specifications and released id Software
// sources:
// - Quake BSP29: the Quake Specifications (Olivier Montanuy,
//   https://www.gamers.org/dEngine/quake/spec/quake-spec34/) and the released
//   Quake tools sources. BSP2/2PSB extensions follow the ericw-tools
//   documentation (https://ericwa.github.io/ericw-tools/).
// - Quake II IBSP38: the released Quake II source `qfiles.h`.
// - Quake III IBSP46: the released Quake III Arena source `qfiles.h` and the
//   q3map2 sources imported under external/compilers.
// - Portal (.prt) and point (.pts/.lin) files: ericw-tools and q3map2 output
//   documentation.
//
// Inspection is strictly read-only and bounds-checked; it never trusts lump
// offsets or counts taken from the file.

#include "core/operation_state.h"

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class BspFamily {
	Unknown,
	Quake,      // BSP29 / BSP2 / 2PSB
	Quake2,     // IBSP 38
	Quake3,     // IBSP 46 / RBSP 1
};

struct BspLumpInfo {
	int index = -1;
	QString name;
	quint32 offset = 0;
	quint32 length = 0;
	int entrySize = 0;
	int entryCount = 0;
	bool withinFile = true;
	bool aligned = true;
};

struct BspEntityKeyValue {
	QString key;
	QString value;
};

struct BspEntitySummary {
	int index = -1;
	QString className;
	QVector<BspEntityKeyValue> properties;
};

struct BspTextureSummary {
	QString name;
	int width = 0;
	int height = 0;
	int referenceCount = 0;
	bool embedded = false;
	quint32 surfaceFlags = 0;
	// Quake III shaders carry content flags; Quake II texinfo does not, and keeps
	// a light/surface value in the same position instead.
	quint32 contentFlags = 0;
	qint32 surfaceValue = 0;
};

struct BspInspection {
	QString sourcePath;
	BspFamily family = BspFamily::Unknown;
	QString familyId;
	QString magic;
	int version = 0;
	qint64 fileSizeBytes = 0;
	bool valid = false;
	QVector<BspLumpInfo> lumps;
	QVector<BspEntitySummary> entities;
	QVector<BspTextureSummary> textures;
	int modelCount = 0;
	int faceCount = 0;
	int vertexCount = 0;
	int leafCount = 0;
	int nodeCount = 0;
	int planeCount = 0;
	int brushCount = 0;
	int lightmapCount = 0;
	int entityCount = 0;
	double mins[3] = {0.0, 0.0, 0.0};
	double maxs[3] = {0.0, 0.0, 0.0};
	bool hasVisData = false;
	bool hasLightData = false;
	QString worldspawnMessage;
	QStringList detailLines;
	QStringList warnings;
	QStringList errors;
	QString error;

	[[nodiscard]] OperationState state() const;
};

// A leak line emitted by qbsp/q3map2 as a .pts/.lin file.
struct LeakPointFile {
	QString sourcePath;
	bool valid = false;
	QVector<double> pointsXyz;    // flattened triples
	int pointCount = 0;
	double mins[3] = {0.0, 0.0, 0.0};
	double maxs[3] = {0.0, 0.0, 0.0};
	QString error;
};

struct PortalFileSummary {
	QString sourcePath;
	bool valid = false;
	QString magic;
	int portalCount = 0;
	int leafCount = 0;
	int clusterCount = 0;
	qint64 fileSizeBytes = 0;
	QStringList warnings;
	QString error;
};

// Everything the studio can say about one compiled map, gathered in a single
// read-only pass over the output folder.
struct CompiledMapArtifacts {
	QString bspPath;
	BspInspection bsp;
	bool hasLeakFile = false;
	LeakPointFile leak;
	bool hasPortalFile = false;
	PortalFileSummary portals;
	QStringList relatedPaths;
	QStringList warnings;
};

QString bspFamilyId(BspFamily family);
QString bspFamilyDisplayName(BspFamily family);

BspInspection inspectBspFile(const QString& path);
BspInspection inspectBspBytes(const QString& sourcePath, const QByteArray& bytes);
LeakPointFile loadLeakPointFile(const QString& path);
PortalFileSummary inspectPortalFile(const QString& path);
CompiledMapArtifacts inspectCompiledMapArtifacts(const QString& bspPath);

QStringList bspInspectionLines(const BspInspection& inspection);
QString bspInspectionText(const BspInspection& inspection);
QJsonObject bspInspectionJson(const BspInspection& inspection);
QJsonObject compiledMapArtifactsJson(const CompiledMapArtifacts& artifacts);
QString compiledMapArtifactsText(const CompiledMapArtifacts& artifacts);

} // namespace vibestudio
