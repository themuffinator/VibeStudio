#include "core/model_document.h"
#include "core/model_mdl.h"
#include "tests/model_mdl_test_helpers.h"

#include <QCoreApplication>
#include <QJsonDocument>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool cornersMatch(const ModelMesh &a, const ModelMesh &b, const ModelExportReport &report)
{
	if (!b.isValid() || b.frames.size() != a.frames.size() || b.surfaces.size() != 1)
	{
		return false;
	}
	const auto &left = a.surfaces[0], &right = b.surfaces[0];
	if (left.triangles.size() != right.triangles.size())
	{
		return false;
	}
	for (int f = 0; f < a.frames.size(); ++f)
	{
		for (int t = 0; t < left.triangles.size(); ++t)
		{
			const auto x = left.triangles[t], y = right.triangles[t];
			const int before[]{x.a, x.b, x.c}, after[]{y.a, y.b, y.c};
			for (int c = 0; c < 3; ++c)
			{
				const auto p = left.frames[f].positions[before[c]], q = right.frames[f].positions[after[c]];
				const double dx = double(p.x) - q.x, dy = double(p.y) - q.y, dz = double(p.z) - q.z;
				const auto uv = left.texCoords[before[c]], st = right.texCoords[after[c]];
				if (std::sqrt(dx * dx + dy * dy + dz * dz) > report.maxPositionError + .00001 ||
					std::abs(double(uv.u) - st.u) > report.maxUvError + .000001 ||
					std::abs(double(uv.v) - st.v) > report.maxUvError + .000001)
				{
					return false;
				}
			}
		}
	}
	return true;
}
ModelMesh seamedGrid(ModelMesh base)
{
	base.frames.resize(1);
	base.animations.clear();
	base.mdl.frameGroups = {{0, {}}};
	auto &surface = base.surfaces[0];
	surface.triangles.clear();
	surface.texCoords.clear();
	surface.frames = {ModelFrameGeometry{}};
	constexpr int nx = 23, ny = 24, half = nx * ny;
	for (int side = 0; side < 2; ++side)
	{
		for (int y = 0; y < ny; ++y)
		{
			for (int x = 0; x < nx; ++x)
			{
				surface.frames[0].positions << ModelVec3{float(x * 8), float(y * 8), 0};
				surface.frames[0].normals << modelAliasNormal(5);
				surface.texCoords << ModelTexCoord{(side * 4 + .5f) / 8, ((y % 4) + .5f) / 4};
				if (x + 1 < nx && y + 1 < ny)
				{
					const int a = side * half + y * nx + x;
					surface.triangles << ModelTriangle{a, a + 1, a + nx} << ModelTriangle{a + 1, a + nx + 1, a + nx};
				}
			}
		}
	}
	surface.vertexCount = surface.texCoords.size();
	updateEditableModelMetadata(&base);
	return base;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto fixture = tests::groupedMdlFixture();
	auto palette = generatedIdTechPalette("quake");
	palette.colors[1] = palette.colors[0];
	palette.generated = false;
	const auto mesh = decodeModelMesh("fixture.mdl", fixture.bytes, &palette);
	QString error;
	ModelExportReport report;
	const auto original = source(mesh);
	const auto bytes = exportEditableModel(mesh, "MDL", 0, &error, {}, &report);
	const auto loaded = decodeModelMesh("roundtrip.mdl", bytes, &palette);
	bool ok = expect(!bytes.isEmpty() && error.isEmpty() && cornersMatch(mesh, loaded, report),
					 "MDL dispatcher exports all poses with bounded corner error");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	ok &= expect(source(mesh) == original && exportModelMdl(mesh) == bytes, "export is deterministic and leaves the source intact");
	ok &= expect(bytes.left(4) == "IDPO" && qFromLittleEndian<qint32>(bytes.data() + 4) == 6 &&
					 qFromLittleEndian<qint32>(bytes.data() + 60) == report.storedVertices &&
					 qFromLittleEndian<qint32>(bytes.data() + 68) == 2 && qFromLittleEndian<quint32>(bytes.data() + 76) == 0x80000010u &&
					 bytes.mid(84, 116) == fixture.bytes.mid(84, 116),
				 "independent header and skin byte audit preserves unsigned flags and every native skin record");
	ok &= expect(modelMdlSettingsJson(mesh.mdl) == modelMdlSettingsJson(loaded.mdl) &&
					 modelMdlSkinJson(mesh.embeddedSkins[0]) == modelMdlSkinJson(loaded.embeddedSkins[0]) &&
					 loaded.embeddedSkins[1].indexedFrames == mesh.embeddedSkins[1].indexedFrames,
				 "reload retains native groups, exact timing, palette indices and header semantics");
	const auto grid = seamedGrid(mesh);
	const auto gridBytes = exportModelMdl(grid, &error, {}, &report);
	ok &= expect(!gridBytes.isEmpty() && grid.surfaces[0].vertexCount == 1104 && report.storedVertices == 552 &&
					 cornersMatch(grid, decodeModelMesh("grid.mdl", gridBytes, &palette), report),
				 "compatible seams fit 1104 editable UV corners into 552 native vertices without changing face UVs");
	auto conflict = grid;
	conflict.surfaces[0].triangles[0].a += 552;
	const auto conflictBytes = exportModelMdl(conflict, &error, {}, &report);
	ok &= expect(!conflictBytes.isEmpty() && report.storedVertices > 552 &&
					 cornersMatch(conflict, decodeModelMesh("conflict.mdl", conflictBytes, &palette), report),
				 "mixed seam halves in one triangle split storage instead of altering texture coordinates");
	auto reject = [&](ModelMesh invalid, const char *message)
	{
		report.storedVertices = 99;
		report.notes = {"stale"};
		const auto result = exportModelMdl(invalid, &error, {}, &report);
		ok &= expect(result.isEmpty() && !error.isEmpty() && report.storedVertices == 0 && report.notes.isEmpty(), message);
	};
	auto invalid = mesh;
	invalid.surfaces[0].texCoords[0].u = -.01f;
	reject(invalid, "out-of-tile UVs fail with no partial report");
	invalid = mesh;
	invalid.frames[0].name = QString(16, 'a');
	reject(invalid, "overlong native frame names cannot be truncated");
	invalid = mesh;
	invalid.embeddedSkins.clear();
	reject(invalid, "skinless sources cannot be exported as playable original Quake MDL files");
	invalid = mesh;
	invalid.surfaces[0].frames[0].positions = {{0, 0, 0}, {.1f, 0, 0}, {0, .1f, 0}, {1000, 1000, 0}};
	invalid.surfaces[0].triangles = {{0, 1, 2}, {1, 3, 2}};
	updateEditableModelMetadata(&invalid);
	reject(invalid, "global byte quantization cannot silently collapse a valid source triangle");
	invalid = mesh;
	invalid.surfaces[0].vertexCount = 6;
	invalid.surfaces[0].texCoords = QVector<ModelTexCoord>(6, ModelTexCoord{.5f, .5f});
	invalid.surfaces[0].triangles = {{0, 1, 2}, {3, 4, 5}};
	for (auto &pose : invalid.surfaces[0].frames)
	{
		pose.positions = {{1.49f, 1.49f, 0}, {1.51f, 2.49f, 0}, {2.49f, 2.51f, 0}, {0, 0, 0}, {255, 0, 0}, {0, 255, 0}};
		pose.normals = QVector<ModelVec3>(6, modelAliasNormal(5));
	}
	updateEditableModelMetadata(&invalid);
	ok &= expect(validateEditableModel(invalid).isEmpty(), "winding-reversal fixture is valid before quantization");
	reject(invalid, "byte rounding cannot reverse a triangle even when all three packed corners remain distinct");
	invalid = grid;
	for (int v = 552; v < invalid.surfaces[0].vertexCount; ++v)
	{
		invalid.surfaces[0].frames[0].positions[v].z += 1;
	}
	updateEditableModelMetadata(&invalid);
	reject(invalid, "distinct all-pose vertices cannot be merged to evade the original renderer limit");
	bool serializing = false;
	ModelWorkControl cancelled;
	cancelled.progress = [&](ModelWorkPhase phase, qint64, qint64) { serializing |= phase == ModelWorkPhase::Serializing; };
	cancelled.cancelled = [&] { return serializing; };
	ok &= expect(exportModelMdl(grid, &error, cancelled, &report).isEmpty() && !error.isEmpty() && report.storedVertices == 0,
				 "cancellation during export leaves no file payload or misleading report");
	const auto obj = exportEditableModel(mesh, "obj", 0, &error, {}, &report);
	ok &= expect(!obj.isEmpty() && obj.contains("MDL indexed skins") && report.notes.join('\n').contains("indexed skins"),
				 "OBJ explicitly reports omitted native skin and group data");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
