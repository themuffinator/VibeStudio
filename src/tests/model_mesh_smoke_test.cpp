#include "core/idtech_image.h"
#include "core/model_mesh.h"
#include "core/package_archive.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool nearly(float value, float expected, float epsilon = 1.0e-4f)
{
	return std::fabs(value - expected) <= epsilon;
}

void appendU8(QByteArray& bytes, int value)
{
	bytes.append(static_cast<char>(static_cast<quint8>(value)));
}

void appendI16(QByteArray& bytes, int value)
{
	const quint16 raw = static_cast<quint16>(static_cast<qint16>(value));
	bytes.append(static_cast<char>(raw & 0xFF));
	bytes.append(static_cast<char>((raw >> 8) & 0xFF));
}

void appendI32(QByteArray& bytes, qint32 value)
{
	const quint32 raw = static_cast<quint32>(value);
	for (int shift = 0; shift < 32; shift += 8) {
		bytes.append(static_cast<char>((raw >> shift) & 0xFF));
	}
}

void appendF32(QByteArray& bytes, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	appendI32(bytes, static_cast<qint32>(raw));
}

void appendFixed(QByteArray& bytes, const QByteArray& name, int length)
{
	QByteArray padded = name.left(length);
	padded.append(QByteArray(length - padded.size(), '\0'));
	bytes.append(padded);
}

void patchI32(QByteArray& bytes, int offset, qint32 value)
{
	const quint32 raw = static_cast<quint32>(value);
	for (int index = 0; index < 4; ++index) {
		bytes[offset + index] = static_cast<char>((raw >> (index * 8)) & 0xFF);
	}
}

// ---------------------------------------------------------------------------
// Quake MDL fixtures
// ---------------------------------------------------------------------------

constexpr float kMdlScaleX = 0.5f;
constexpr float kMdlScaleY = 0.25f;
constexpr float kMdlScaleZ = 2.0f;
constexpr float kMdlTranslateX = -10.0f;
constexpr float kMdlTranslateY = 5.0f;
constexpr float kMdlTranslateZ = 0.5f;
constexpr int kMdlSkinWidth = 32;
constexpr int kMdlSkinHeight = 16;

QByteArray buildMdl(bool backfaceSecondTriangle, int skinCount, const QByteArray& skinPixels, int skinWidth, int skinHeight)
{
	QByteArray bytes;
	bytes.append("IDPO", 4);
	appendI32(bytes, 6);
	appendF32(bytes, kMdlScaleX);
	appendF32(bytes, kMdlScaleY);
	appendF32(bytes, kMdlScaleZ);
	appendF32(bytes, kMdlTranslateX);
	appendF32(bytes, kMdlTranslateY);
	appendF32(bytes, kMdlTranslateZ);
	appendF32(bytes, 12.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 24.0f);
	appendI32(bytes, skinCount);
	appendI32(bytes, skinWidth);
	appendI32(bytes, skinHeight);
	appendI32(bytes, 4);
	appendI32(bytes, 2);
	appendI32(bytes, 1);
	appendI32(bytes, 0);
	appendI32(bytes, 0);
	appendF32(bytes, 1.0f);

	for (int index = 0; index < skinCount; ++index) {
		appendI32(bytes, 0);
		bytes.append(skinPixels);
	}

	// stvert_t: onseam, s, t. Vertex 2 carries ALIAS_ONSEAM (0x20).
	const int seam[4] = {0, 0, 0x20, 0};
	const int sCoord[4] = {0, 8, 16, 4};
	const int tCoord[4] = {0, 4, 8, 2};
	for (int index = 0; index < 4; ++index) {
		appendI32(bytes, seam[index]);
		appendI32(bytes, sCoord[index]);
		appendI32(bytes, tCoord[index]);
	}

	// dtriangle_t: facesfront, vertindex[3].
	appendI32(bytes, 1);
	appendI32(bytes, 0);
	appendI32(bytes, 1);
	appendI32(bytes, 2);
	appendI32(bytes, backfaceSecondTriangle ? 0 : 1);
	appendI32(bytes, 2);
	appendI32(bytes, 3);
	appendI32(bytes, 0);

	// One simple frame.
	appendI32(bytes, 0);
	appendU8(bytes, 0);
	appendU8(bytes, 0);
	appendU8(bytes, 0);
	appendU8(bytes, 0);
	appendU8(bytes, 255);
	appendU8(bytes, 255);
	appendU8(bytes, 255);
	appendU8(bytes, 0);
	appendFixed(bytes, QByteArrayLiteral("frame1"), 16);
	const int raw[4][3] = {{0, 0, 0}, {10, 20, 30}, {255, 255, 255}, {1, 2, 3}};
	for (int index = 0; index < 4; ++index) {
		appendU8(bytes, raw[index][0]);
		appendU8(bytes, raw[index][1]);
		appendU8(bytes, raw[index][2]);
		appendU8(bytes, index * 3);
	}
	return bytes;
}

QByteArray buildPlainMdl()
{
	return buildMdl(false, 0, QByteArray(), kMdlSkinWidth, kMdlSkinHeight);
}

// ---------------------------------------------------------------------------
// Quake II MD2 fixtures
// ---------------------------------------------------------------------------

constexpr int kMd2SkinWidth = 64;
constexpr int kMd2SkinHeight = 32;

QByteArray buildMd2(const QList<QByteArray>& frameNames)
{
	const int frameCount = frameNames.size();
	const int positionCount = 3;
	const int stCount = 4;
	const int triangleCount = 2;
	const int frameSize = 40 + (positionCount * 4);
	const int skinOffset = 68;
	const int stOffset = skinOffset + 64;
	const int triangleOffset = stOffset + (stCount * 4);
	const int frameOffset = triangleOffset + (triangleCount * 12);
	const int endOffset = frameOffset + (frameCount * frameSize);

	QByteArray bytes;
	bytes.append("IDP2", 4);
	appendI32(bytes, 8);
	appendI32(bytes, kMd2SkinWidth);
	appendI32(bytes, kMd2SkinHeight);
	appendI32(bytes, frameSize);
	appendI32(bytes, 1);
	appendI32(bytes, positionCount);
	appendI32(bytes, stCount);
	appendI32(bytes, triangleCount);
	appendI32(bytes, 0);
	appendI32(bytes, frameCount);
	appendI32(bytes, skinOffset);
	appendI32(bytes, stOffset);
	appendI32(bytes, triangleOffset);
	appendI32(bytes, frameOffset);
	appendI32(bytes, endOffset);
	appendI32(bytes, endOffset);

	appendFixed(bytes, QByteArrayLiteral("models/test/skin.pcx"), 64);

	// dstvert_t: short s, short t.
	const int sCoord[4] = {0, 32, 32, 16};
	const int tCoord[4] = {0, 0, 16, 8};
	for (int index = 0; index < stCount; ++index) {
		appendI16(bytes, sCoord[index]);
		appendI16(bytes, tCoord[index]);
	}

	// dtriangle_t: index_xyz[3] then index_st[3]. Both triangles share the same
	// positions but the second uses a different st entry for its first corner.
	appendI16(bytes, 0);
	appendI16(bytes, 1);
	appendI16(bytes, 2);
	appendI16(bytes, 0);
	appendI16(bytes, 1);
	appendI16(bytes, 2);
	appendI16(bytes, 0);
	appendI16(bytes, 1);
	appendI16(bytes, 2);
	appendI16(bytes, 3);
	appendI16(bytes, 1);
	appendI16(bytes, 2);

	for (int frame = 0; frame < frameCount; ++frame) {
		appendF32(bytes, 1.0f);
		appendF32(bytes, 1.0f);
		appendF32(bytes, 1.0f);
		appendF32(bytes, 0.0f);
		appendF32(bytes, 0.0f);
		appendF32(bytes, 0.0f);
		appendFixed(bytes, frameNames.at(frame), 16);
		const int raw[3][3] = {{10, 20, 30}, {40, 50, 60}, {70, 80, 90}};
		for (int index = 0; index < positionCount; ++index) {
			appendU8(bytes, raw[index][0] + frame);
			appendU8(bytes, raw[index][1]);
			appendU8(bytes, raw[index][2]);
			appendU8(bytes, index);
		}
	}
	return bytes;
}

// ---------------------------------------------------------------------------
// Quake III MD3 fixtures
// ---------------------------------------------------------------------------

constexpr int kMd3SurfaceBytes = 236;

void appendMd3Surface(QByteArray& bytes, const QByteArray& name, const QByteArray& shader, int zOffset)
{
	const int shaderOffset = 108;
	const int triangleOffset = shaderOffset + 68;
	const int stOffset = triangleOffset + 12;
	const int vertexOffset = stOffset + (3 * 8);
	const int endOffset = vertexOffset + (3 * 8);

	bytes.append("IDP3", 4);
	appendFixed(bytes, name, 64);
	appendI32(bytes, 0);
	appendI32(bytes, 1);
	appendI32(bytes, 1);
	appendI32(bytes, 3);
	appendI32(bytes, 1);
	appendI32(bytes, triangleOffset);
	appendI32(bytes, shaderOffset);
	appendI32(bytes, stOffset);
	appendI32(bytes, vertexOffset);
	appendI32(bytes, endOffset);

	appendFixed(bytes, shader, 64);
	appendI32(bytes, 0);

	appendI32(bytes, 0);
	appendI32(bytes, 1);
	appendI32(bytes, 2);

	appendF32(bytes, 0.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 1.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 1.0f);
	appendF32(bytes, 1.0f);

	// int16 positions in 1/64 units, plus a packed lat/lng normal.
	const int raw[3][3] = {{64, 128, -64}, {-128, 0, 320}, {0, -256, 64}};
	for (int index = 0; index < 3; ++index) {
		appendI16(bytes, raw[index][0]);
		appendI16(bytes, raw[index][1]);
		appendI16(bytes, raw[index][2] + zOffset);
		appendI16(bytes, 0);
	}
}

QByteArray buildMd3()
{
	const int frameOffset = 108;
	const int tagOffset = frameOffset + 56;
	const int surfaceOffset = tagOffset + 112;
	const int endOffset = surfaceOffset + (2 * kMd3SurfaceBytes);

	QByteArray bytes;
	bytes.append("IDP3", 4);
	appendI32(bytes, 15);
	appendFixed(bytes, QByteArrayLiteral("models/test/pair"), 64);
	appendI32(bytes, 0);
	appendI32(bytes, 1);
	appendI32(bytes, 1);
	appendI32(bytes, 2);
	appendI32(bytes, 0);
	appendI32(bytes, frameOffset);
	appendI32(bytes, tagOffset);
	appendI32(bytes, surfaceOffset);
	appendI32(bytes, endOffset);

	// md3Frame_t
	appendF32(bytes, -16.0f);
	appendF32(bytes, -16.0f);
	appendF32(bytes, -16.0f);
	appendF32(bytes, 16.0f);
	appendF32(bytes, 16.0f);
	appendF32(bytes, 16.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 27.7f);
	appendFixed(bytes, QByteArrayLiteral("idle"), 16);

	// md3Tag_t
	appendFixed(bytes, QByteArrayLiteral("tag_head"), 64);
	appendF32(bytes, 1.0f);
	appendF32(bytes, 2.0f);
	appendF32(bytes, 3.0f);
	appendF32(bytes, 1.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 1.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 0.0f);
	appendF32(bytes, 1.0f);

	appendMd3Surface(bytes, QByteArrayLiteral("body"), QByteArrayLiteral("models/test/body"), 0);
	appendMd3Surface(bytes, QByteArrayLiteral("head"), QByteArrayLiteral("models/test/head"), 64);
	return bytes;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

bool runFormatDetectionSmoke()
{
	bool ok = true;
	ok &= expect(detectModelMeshFormat(QStringLiteral("a.mdl"), QByteArrayLiteral("IDPO....")) == ModelMeshFormat::QuakeMdl, "IDPO should detect as MDL.");
	ok &= expect(detectModelMeshFormat(QStringLiteral("a.md2"), QByteArrayLiteral("IDP2....")) == ModelMeshFormat::Quake2Md2, "IDP2 should detect as MD2.");
	ok &= expect(detectModelMeshFormat(QStringLiteral("a.md3"), QByteArrayLiteral("IDP3....")) == ModelMeshFormat::Quake3Md3, "IDP3 should detect as MD3.");
	ok &= expect(detectModelMeshFormat(QStringLiteral("a.mdc"), QByteArrayLiteral("IDPC....")) == ModelMeshFormat::Mdc, "IDPC should detect as MDC.");
	ok &= expect(detectModelMeshFormat(QStringLiteral("a.mdr"), QByteArrayLiteral("RDM5....")) == ModelMeshFormat::Mdr, "RDM5 should detect as MDR.");
	ok &= expect(detectModelMeshFormat(QStringLiteral("a.iqm"), QByteArray("INTERQUAKEMODEL\0", 16)) == ModelMeshFormat::Iqm, "The IQM magic should detect as IQM.");
	ok &= expect(detectModelMeshFormat(QStringLiteral("a.txt"), QByteArrayLiteral("nope")) == ModelMeshFormat::Unknown, "An unrelated file should not detect as a model.");
	ok &= expect(modelMeshFormatId(ModelMeshFormat::Quake3Md3) == QStringLiteral("md3"), "MD3 should report a stable format id.");
	return ok;
}

bool runMdlGeometrySmoke()
{
	bool ok = true;
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("progs/shape.mdl"), buildPlainMdl());
	ok &= expect(mesh.isValid() && mesh.error.isEmpty(), "A well formed MDL should decode.");
	ok &= expect(mesh.format == ModelMeshFormat::QuakeMdl && mesh.version == 6, "The MDL format and version should be reported.");
	ok &= expect(mesh.geometryAvailable, "MDL geometry should be available.");
	ok &= expect(mesh.surfaces.size() == 1, "An MDL decodes into a single surface.");
	if (mesh.surfaces.isEmpty()) {
		return false;
	}
	const ModelSurface& surface = mesh.surfaces.first();
	ok &= expect(surface.name == QStringLiteral("shape"), "The surface should be named after the model.");
	ok &= expect(surface.vertexCount == 4, "A front-facing MDL keeps one vertex per base vertex.");
	ok &= expect(surface.triangles.size() == 2, "Both MDL triangles should decode.");
	ok &= expect(surface.frames.size() == 1 && mesh.frames.size() == 1, "The single MDL frame should decode.");
	if (surface.frames.isEmpty()) {
		return false;
	}
	const ModelFrameGeometry& geometry = surface.frames.first();
	ok &= expect(geometry.positions.size() == 4, "The frame should hold one position per vertex.");
	// position = scale * raw + translate
	ok &= expect(nearly(geometry.positions.at(0).x, -10.0f) && nearly(geometry.positions.at(0).y, 5.0f) && nearly(geometry.positions.at(0).z, 0.5f), "Vertex 0 should decompress to the translation.");
	ok &= expect(nearly(geometry.positions.at(1).x, -5.0f) && nearly(geometry.positions.at(1).y, 10.0f) && nearly(geometry.positions.at(1).z, 60.5f), "Vertex 1 should decompress with scale and translation.");
	ok &= expect(nearly(geometry.positions.at(2).x, 117.5f) && nearly(geometry.positions.at(2).y, 68.75f) && nearly(geometry.positions.at(2).z, 510.5f), "Vertex 2 should decompress at the packed maximum.");
	ok &= expect(geometry.normals.size() == 4 && nearly(geometry.normals.at(0).z, 0.850651f), "Normal indices should resolve through the published table.");
	ok &= expect(nearly(surface.texCoords.at(1).u, 8.5f / 32.0f) && nearly(surface.texCoords.at(1).v, 4.5f / 16.0f), "Texture coordinates should be normalised by the skin size.");
	ok &= expect(mesh.frames.first().name == QStringLiteral("frame1"), "The frame name should decode.");
	ok &= expect(mesh.animations.size() == 1 && mesh.animations.first().name == QStringLiteral("frame"), "Frame names should group into an animation.");
	ok &= expect(mesh.vertexCount == 4 && mesh.triangleCount == 2, "Mesh totals should match the surface.");
	ok &= expect(nearly(mesh.mins.x, -10.0f) && nearly(mesh.maxs.z, 510.5f), "Bounds should cover every frame position.");
	ok &= expect(mesh.boundingRadius() > 500.0f, "The bounding radius should cover the farthest vertex.");
	const QStringList lines = modelMeshSummaryLines(mesh);
	ok &= expect(!lines.isEmpty() && lines.first().contains(QStringLiteral("Quake MDL")), "Summary lines should lead with the format.");
	ok &= expect(modelMeshSummaryText(mesh).contains(QStringLiteral("Frames: 1")), "Summary text should report the frame count.");
	return ok;
}

bool runMdlSeamSmoke()
{
	bool ok = true;
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("progs/seam.mdl"), buildMdl(true, 0, QByteArray(), kMdlSkinWidth, kMdlSkinHeight));
	ok &= expect(mesh.error.isEmpty(), "The on-seam MDL should decode.");
	if (mesh.surfaces.isEmpty()) {
		return false;
	}
	const ModelSurface& surface = mesh.surfaces.first();
	ok &= expect(surface.vertexCount == 5, "The on-seam back-facing vertex should be duplicated.");
	ok &= expect(surface.triangles.size() == 2, "Both triangles should survive the duplication.");
	if (surface.triangles.size() != 2 || surface.vertexCount != 5) {
		return false;
	}
	ok &= expect(surface.triangles.at(0).a == 0 && surface.triangles.at(0).b == 2 && surface.triangles.at(0).c == 1, "The front-skin triangle converts native winding and keeps the base vertices.");
	ok &= expect(surface.triangles.at(1).a == 4 && surface.triangles.at(1).b == 0 && surface.triangles.at(1).c == 3, "The back-skin triangle converts native winding and uses the duplicated vertex.");
	// s = 16, skinwidth / 2 = 16, plus the half-texel centre offset.
	ok &= expect(nearly(surface.texCoords.at(2).u, 16.5f / 32.0f), "The original seam vertex keeps its S coordinate.");
	ok &= expect(nearly(surface.texCoords.at(4).u, 32.5f / 32.0f), "The duplicated seam vertex should carry the shifted S coordinate.");
	ok &= expect(nearly(surface.texCoords.at(4).v, surface.texCoords.at(2).v), "The duplicated seam vertex keeps its T coordinate.");
	const ModelFrameGeometry& geometry = surface.frames.first();
	ok &= expect(geometry.positions.size() == 5, "The duplicated vertex needs a position too.");
	ok &= expect(nearly(geometry.positions.at(4).x, geometry.positions.at(2).x)
		&& nearly(geometry.positions.at(4).y, geometry.positions.at(2).y)
		&& nearly(geometry.positions.at(4).z, geometry.positions.at(2).z), "The duplicated vertex shares the original position.");
	return ok;
}

bool runMdlSkinSmoke()
{
	bool ok = true;
	const int width = 4;
	const int height = 2;
	QByteArray pixels;
	for (int index = 0; index < width * height; ++index) {
		appendU8(pixels, index * 7);
	}
	const IdTechPalette palette = generatedIdTechPalette(QStringLiteral("quake"));
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("progs/skinned.mdl"), buildMdl(false, 1, pixels, width, height), &palette);
	ok &= expect(mesh.error.isEmpty(), "An MDL with an embedded skin should decode.");
	ok &= expect(mesh.skinCount == 1 && mesh.embeddedSkins.size() == 1, "The embedded skin should be reported.");
	if (mesh.embeddedSkins.isEmpty()) {
		return false;
	}
	const QImage image = mesh.embeddedSkins.first().image;
	ok &= expect(image.width() == width && image.height() == height, "The embedded skin should keep the declared size.");
	if (image.isNull()) {
		return false;
	}
	bool pixelsMatch = true;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int index = ((y * width) + x) * 7;
			const QRgb expected = palette.colorAt(index);
			const QRgb actual = image.pixel(x, y);
			if (qRed(actual) != qRed(expected) || qGreen(actual) != qGreen(expected) || qBlue(actual) != qBlue(expected)) {
				pixelsMatch = false;
			}
		}
	}
	ok &= expect(pixelsMatch, "Embedded skin pixels should resolve through the supplied palette.");
	ok &= expect(mesh.embeddedSkins.first().groupFrameCount == 1, "A single skin is a group of one.");
	// The geometry must still decode past the skin block.
	ok &= expect(!mesh.surfaces.isEmpty() && mesh.surfaces.first().triangles.size() == 2, "Skins must not disturb the geometry offsets.");
	return ok;
}

bool runMd2GeometrySmoke()
{
	bool ok = true;
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/tris.md2"), buildMd2({QByteArrayLiteral("stand01")}));
	ok &= expect(mesh.error.isEmpty() && mesh.geometryAvailable, "A well formed MD2 should decode.");
	if (mesh.surfaces.isEmpty()) {
		return false;
	}
	const ModelSurface& surface = mesh.surfaces.first();
	// Triangle 0 uses st 0-2, triangle 1 reuses positions 0-2 with st 3 for its
	// first corner, so the combined list needs a fourth vertex.
	ok &= expect(surface.vertexCount == 4, "MD2 position/st pairs should build a combined vertex list.");
	ok &= expect(surface.texCoords.size() == 4, "Each combined vertex needs its own texture coordinate.");
	ok &= expect(surface.triangles.size() == 2, "Both MD2 triangles should decode.");
	if (surface.triangles.size() != 2 || surface.vertexCount != 4) {
		return false;
	}
	ok &= expect(surface.triangles.at(0).a == 0 && surface.triangles.at(0).b == 2 && surface.triangles.at(0).c == 1, "The first triangle converts native winding and uses the first three combined vertices.");
	ok &= expect(surface.triangles.at(1).a == 3 && surface.triangles.at(1).b == 2 && surface.triangles.at(1).c == 1, "The second triangle converts native winding and reuses the shared corners and new pair.");
	ok &= expect(nearly(surface.texCoords.at(0).u, 0.5f / kMd2SkinWidth) && nearly(surface.texCoords.at(1).u, 0.5f + 0.5f / kMd2SkinWidth), "MD2 st coordinates sample texel centres at the skin size.");
	ok &= expect(nearly(surface.texCoords.at(3).u, 0.25f + 0.5f / kMd2SkinWidth) && nearly(surface.texCoords.at(3).v, 0.25f + 0.5f / kMd2SkinHeight), "The duplicated position should carry the second st entry at its texel centre.");
	const ModelFrameGeometry& geometry = surface.frames.first();
	ok &= expect(geometry.positions.size() == 4, "The frame should hold one position per combined vertex.");
	ok &= expect(nearly(geometry.positions.at(0).x, 10.0f) && nearly(geometry.positions.at(0).y, 20.0f) && nearly(geometry.positions.at(0).z, 30.0f), "MD2 positions use the per-frame scale and translation.");
	ok &= expect(nearly(geometry.positions.at(3).x, geometry.positions.at(0).x)
		&& nearly(geometry.positions.at(3).z, geometry.positions.at(0).z), "The duplicated vertex shares its position with the original.");
	ok &= expect(mesh.skinPaths.size() == 1 && mesh.skinPaths.first() == QStringLiteral("models/test/skin.pcx"), "MD2 skin names should decode.");
	ok &= expect(surface.skinPaths == mesh.skinPaths, "Surface skins should mirror the model skins.");
	return ok;
}

bool runMd2AnimationSmoke()
{
	bool ok = true;
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/anim.md2"),
		buildMd2({QByteArrayLiteral("stand01"), QByteArrayLiteral("stand02"), QByteArrayLiteral("run01")}));
	ok &= expect(mesh.error.isEmpty(), "A multi-frame MD2 should decode.");
	ok &= expect(mesh.frameCount == 3 && mesh.frames.size() == 3, "Every MD2 frame should decode.");
	ok &= expect(mesh.animations.size() == 2, "Frame names should group into two animations.");
	if (mesh.animations.size() != 2) {
		return false;
	}
	ok &= expect(mesh.animations.at(0).name == QStringLiteral("stand") && mesh.animations.at(0).firstFrame == 0 && mesh.animations.at(0).frameCount == 2, "The first animation should cover both stand frames.");
	ok &= expect(mesh.animations.at(1).name == QStringLiteral("run") && mesh.animations.at(1).firstFrame == 2 && mesh.animations.at(1).frameCount == 1, "The second animation should start at the run frame.");
	ok &= expect(!mesh.surfaces.isEmpty() && mesh.surfaces.first().frames.size() == 3, "Each frame needs its own geometry.");
	const ModelSurface& surface = mesh.surfaces.first();
	ok &= expect(nearly(surface.frames.at(2).positions.at(0).x, 12.0f), "Later frames should decode their own positions.");
	return ok;
}

bool runMd3GeometrySmoke()
{
	bool ok = true;
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/pair.md3"), buildMd3());
	ok &= expect(mesh.error.isEmpty() && mesh.geometryAvailable, "A well formed MD3 should decode.");
	ok &= expect(mesh.surfaces.size() == 2, "The MD3 surface chain should be walked to the end.");
	if (mesh.surfaces.size() != 2) {
		return false;
	}
	ok &= expect(mesh.surfaces.at(0).name == QStringLiteral("body") && mesh.surfaces.at(1).name == QStringLiteral("head"), "Surface names should decode.");
	ok &= expect(mesh.surfaces.at(0).index == 0 && mesh.surfaces.at(1).index == 1, "Surfaces should be indexed in chain order.");
	const ModelFrameGeometry& geometry = mesh.surfaces.at(0).frames.first();
	ok &= expect(geometry.positions.size() == 3, "Each MD3 surface frame holds one position per vertex.");
	// 64 / 64 = 1, 128 / 64 = 2, -64 / 64 = -1
	ok &= expect(nearly(geometry.positions.at(0).x, 1.0f) && nearly(geometry.positions.at(0).y, 2.0f) && nearly(geometry.positions.at(0).z, -1.0f), "MD3 positions decode at 1/64 scale.");
	ok &= expect(nearly(geometry.positions.at(1).z, 5.0f), "MD3 positions decode at 1/64 scale for every vertex.");
	ok &= expect(nearly(mesh.surfaces.at(1).frames.first().positions.at(0).z, 0.0f), "The second surface should decode its own vertex block.");
	ok &= expect(nearly(geometry.normals.at(0).z, 1.0f), "A zero lat/lng normal points along +Z.");
	ok &= expect(mesh.tags.size() == 1 && mesh.tagCount == 1, "The tag should decode.");
	ok &= expect(mesh.tags.first().name == QStringLiteral("tag_head") && mesh.tags.first().frameIndex == 0, "Tag names and frames should decode.");
	ok &= expect(nearly(mesh.tags.first().origin.x, 1.0f) && nearly(mesh.tags.first().origin.z, 3.0f), "Tag origins should decode.");
	ok &= expect(nearly(mesh.tags.first().axis[0], 1.0f) && nearly(mesh.tags.first().axis[4], 1.0f) && nearly(mesh.tags.first().axis[8], 1.0f), "The tag axis should decode row-major.");
	ok &= expect(mesh.frames.size() == 1 && mesh.frames.first().name == QStringLiteral("idle"), "MD3 frame names should decode.");
	ok &= expect(nearly(mesh.frames.first().radius, 27.7f, 1.0e-3f), "MD3 frame radius should decode.");
	ok &= expect(mesh.skinPaths.size() == 2 && mesh.skinPaths.first() == QStringLiteral("models/test/body"), "Surface shaders should be collected as skin paths.");
	ok &= expect(mesh.vertexCount == 6 && mesh.triangleCount == 2, "Mesh totals should sum both surfaces.");
	ok &= expect(mesh.warnings.isEmpty(), "A clean MD3 should not warn.");
	return ok;
}

bool runTruncatedHeaderSmoke()
{
	bool ok = true;
	const QList<QByteArray> magics = {QByteArrayLiteral("IDPO"), QByteArrayLiteral("IDP2"), QByteArrayLiteral("IDP3"),
		QByteArrayLiteral("IDPC"), QByteArrayLiteral("RDM5")};
	for (const QByteArray& magic : magics) {
		QByteArray bytes = magic;
		appendI32(bytes, 6);
		const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/broken.bin"), bytes);
		ok &= expect(!mesh.error.isEmpty(), "A truncated header should report an error.");
		ok &= expect(!mesh.geometryAvailable && mesh.surfaces.isEmpty(), "A truncated header should not claim geometry.");
	}
	QByteArray iqm = QByteArray("INTERQUAKEMODEL\0", 16);
	appendI32(iqm, 2);
	const ModelMesh iqmMesh = decodeModelMesh(QStringLiteral("models/broken.iqm"), iqm);
	ok &= expect(!iqmMesh.error.isEmpty(), "A truncated IQM header should report an error.");

	const ModelMesh empty = decodeModelMesh(QStringLiteral("models/empty.mdl"), QByteArray());
	ok &= expect(!empty.error.isEmpty(), "An empty file should report an error.");

	// Truncated bodies behind a complete header.
	QByteArray mdl = buildPlainMdl();
	const ModelMesh shortMdl = decodeModelMesh(QStringLiteral("progs/short.mdl"), mdl.left(mdl.size() - 8));
	ok &= expect(!shortMdl.error.isEmpty(), "A truncated MDL frame block should report an error.");
	QByteArray md2 = buildMd2({QByteArrayLiteral("stand01")});
	const ModelMesh shortMd2 = decodeModelMesh(QStringLiteral("models/short.md2"), md2.left(md2.size() - 8));
	ok &= expect(!shortMd2.error.isEmpty(), "A truncated MD2 frame block should report an error.");
	QByteArray md3 = buildMd3();
	const ModelMesh shortMd3 = decodeModelMesh(QStringLiteral("models/short.md3"), md3.left(md3.size() - 8));
	ok &= expect(shortMd3.surfaces.size() == 1 && !shortMd3.warnings.isEmpty(), "A truncated MD3 tail should stop the surface walk with a warning.");
	return ok;
}

bool runHostileOffsetSmoke()
{
	bool ok = true;
	const int surfaceOffset = 108 + 56 + 112;

	QByteArray backwards = buildMd3();
	patchI32(backwards, surfaceOffset + 104, -16);
	const ModelMesh backwardsMesh = decodeModelMesh(QStringLiteral("models/hostile.md3"), backwards);
	ok &= expect(backwardsMesh.surfaces.isEmpty(), "A backwards surface end offset must stop the walk.");
	ok &= expect(!backwardsMesh.warnings.isEmpty(), "A backwards surface end offset should warn.");
	ok &= expect(!backwardsMesh.geometryAvailable, "A model with no readable surface has no geometry.");
	ok &= expect(backwardsMesh.frameCount == 1 && backwardsMesh.tagCount == 1, "Frames and tags still decode when the surface chain breaks.");

	QByteArray past = buildMd3();
	patchI32(past, surfaceOffset + 104, 1 << 28);
	const ModelMesh pastMesh = decodeModelMesh(QStringLiteral("models/hostile2.md3"), past);
	ok &= expect(pastMesh.surfaces.isEmpty() && !pastMesh.warnings.isEmpty(), "A surface end offset past the buffer must stop the walk.");

	QByteArray innerBlock = buildMd3();
	// Point the first surface's vertex block past its own record.
	patchI32(innerBlock, surfaceOffset + 100, 1 << 20);
	const ModelMesh innerMesh = decodeModelMesh(QStringLiteral("models/hostile3.md3"), innerBlock);
	ok &= expect(innerMesh.surfaces.size() == 1, "A surface with an out-of-record block is skipped, not fatal.");
	ok &= expect(!innerMesh.warnings.isEmpty(), "A surface with an out-of-record block should warn.");

	QByteArray badTriangles = buildMd3();
	patchI32(badTriangles, surfaceOffset + 108 + 68 + 4, 9999);
	const ModelMesh badTriangleMesh = decodeModelMesh(QStringLiteral("models/hostile4.md3"), badTriangles);
	ok &= expect(badTriangleMesh.surfaces.size() == 2, "Out-of-range triangle indices should not stop the walk.");
	ok &= expect(badTriangleMesh.surfaces.at(0).triangles.isEmpty(), "Out-of-range triangles should be dropped.");

	// MD2 offsets that point outside the file.
	QByteArray md2 = buildMd2({QByteArrayLiteral("stand01")});
	patchI32(md2, 52, 1 << 24);
	const ModelMesh md2Mesh = decodeModelMesh(QStringLiteral("models/hostile.md2"), md2);
	ok &= expect(!md2Mesh.error.isEmpty(), "An MD2 block outside the file should report an error.");

	// MD2 triangle indices past the vertex list.
	QByteArray md2Indices = buildMd2({QByteArrayLiteral("stand01")});
	md2Indices[148] = static_cast<char>(0x20);
	md2Indices[149] = static_cast<char>(0x4E);
	const ModelMesh md2IndexMesh = decodeModelMesh(QStringLiteral("models/hostile2.md2"), md2Indices);
	ok &= expect(md2IndexMesh.error.isEmpty(), "An out-of-range MD2 index should not fail the whole model.");
	if (md2IndexMesh.surfaces.isEmpty()) {
		return expect(false, "The surviving MD2 triangle should still decode.");
	}
	ok &= expect(md2IndexMesh.surfaces.first().triangles.size() == 1, "The MD2 triangle with an out-of-range index should be dropped.");
	ok &= expect(!md2IndexMesh.surfaces.first().warnings.isEmpty(), "Dropping a triangle should warn.");
	return ok;
}

bool runOverflowSmoke()
{
	bool ok = true;
	// numverts * numframes would need gigabytes; the header must be rejected
	// before anything is allocated.
	QByteArray mdl = buildPlainMdl();
	patchI32(mdl, 60, 500000);
	patchI32(mdl, 68, 100000);
	const ModelMesh hugeMdl = decodeModelMesh(QStringLiteral("progs/huge.mdl"), mdl);
	ok &= expect(!hugeMdl.error.isEmpty() && hugeMdl.surfaces.isEmpty(), "An MDL with overflowing counts should be rejected.");

	QByteArray mdlSkin = buildPlainMdl();
	patchI32(mdlSkin, 48, 1);
	patchI32(mdlSkin, 52, 100000);
	patchI32(mdlSkin, 56, 100000);
	const ModelMesh hugeSkin = decodeModelMesh(QStringLiteral("progs/hugeskin.mdl"), mdlSkin);
	ok &= expect(!hugeSkin.error.isEmpty(), "An MDL with an absurd skin size should be rejected.");

	QByteArray mdlNegative = buildPlainMdl();
	patchI32(mdlNegative, 64, -5);
	const ModelMesh negativeMdl = decodeModelMesh(QStringLiteral("progs/negative.mdl"), mdlNegative);
	ok &= expect(!negativeMdl.error.isEmpty(), "A negative MDL triangle count should be rejected.");

	QByteArray md2 = buildMd2({QByteArrayLiteral("stand01")});
	patchI32(md2, 24, 1000000);
	patchI32(md2, 40, 1000);
	const ModelMesh hugeMd2 = decodeModelMesh(QStringLiteral("models/huge.md2"), md2);
	ok &= expect(!hugeMd2.error.isEmpty() && hugeMd2.surfaces.isEmpty(), "An MD2 with overflowing counts should be rejected.");

	QByteArray md3 = buildMd3();
	patchI32(md3, 76, 8000);
	patchI32(md3, 80, 8000);
	const ModelMesh hugeMd3 = decodeModelMesh(QStringLiteral("models/huge.md3"), md3);
	ok &= expect(!hugeMd3.error.isEmpty() && hugeMd3.surfaces.isEmpty(), "An MD3 with overflowing tag counts should be rejected.");

	QByteArray md3Verts = buildMd3();
	const int surfaceOffset = 108 + 56 + 112;
	patchI32(md3Verts, surfaceOffset + 80, 1 << 24);
	const ModelMesh hugeSurface = decodeModelMesh(QStringLiteral("models/hugesurface.md3"), md3Verts);
	ok &= expect(hugeSurface.surfaces.size() <= 1, "An MD3 surface with an absurd vertex count should be skipped.");
	ok &= expect(!hugeSurface.warnings.isEmpty(), "An MD3 surface with an absurd vertex count should warn.");
	return ok;
}

bool runHeaderOnlySmoke()
{
	bool ok = true;
	QByteArray mdc;
	mdc.append("IDPC", 4);
	appendI32(mdc, 2);
	appendFixed(mdc, QByteArrayLiteral("models/test/thing.mdc"), 64);
	appendI32(mdc, 0);
	appendI32(mdc, 12);
	appendI32(mdc, 3);
	appendI32(mdc, 2);
	appendI32(mdc, 1);
	for (int index = 0; index < 5; ++index) {
		appendI32(mdc, 0);
	}
	// These formats decode fully now; a lone header that declares surfaces it
	// does not hold must fail with a named error and never invent geometry.
	const ModelMesh mdcMesh = decodeModelMesh(QStringLiteral("models/thing.mdc"), mdc);
	ok &= expect(!mdcMesh.error.isEmpty() && !mdcMesh.geometryAvailable && mdcMesh.format == ModelMeshFormat::Mdc,
		"A lone MDC header is refused with an error.");
	ok &= expect(exportModelFrameObj(mdcMesh, 0).isEmpty(), "A refused model exports no OBJ.");

	QByteArray mdr;
	mdr.append("RDM5", 4);
	appendI32(mdr, 2);
	appendFixed(mdr, QByteArrayLiteral("models/test/thing.mdr"), 64);
	appendI32(mdr, 7);
	appendI32(mdr, 32);
	appendI32(mdr, 0);
	appendI32(mdr, 3);
	appendI32(mdr, 0);
	appendI32(mdr, 4);
	appendI32(mdr, 0);
	appendI32(mdr, 0);
	const ModelMesh mdrMesh = decodeModelMesh(QStringLiteral("models/thing.mdr"), mdr);
	ok &= expect(!mdrMesh.error.isEmpty() && !mdrMesh.geometryAvailable && mdrMesh.format == ModelMeshFormat::Mdr,
		"A lone MDR header is refused with an error.");

	QByteArray iqm = QByteArray("INTERQUAKEMODEL\0", 16);
	appendI32(iqm, 2);
	appendI32(iqm, 124);
	for (int index = 0; index < 25; ++index) {
		appendI32(iqm, 0);
	}
	patchI32(iqm, 36, 2);
	patchI32(iqm, 48, 48);
	patchI32(iqm, 56, 24);
	patchI32(iqm, 92, 6);
	const ModelMesh iqmMesh = decodeModelMesh(QStringLiteral("models/thing.iqm"), iqm);
	ok &= expect(!iqmMesh.error.isEmpty() && !iqmMesh.geometryAvailable && iqmMesh.format == ModelMeshFormat::Iqm,
		"A lone IQM header is refused with an error.");
	const QStringList lines = modelMeshSummaryLines(iqmMesh);
	ok &= expect(!lines.isEmpty() && lines.first().contains(QStringLiteral("could not be decoded")), "Refused models say so first.");
	return ok;
}

bool runObjExportSmoke()
{
	bool ok = true;
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/pair.md3"), buildMd3());
	if (mesh.surfaces.size() != 2) {
		return expect(false, "The OBJ export test needs the MD3 fixture.");
	}
	const QString obj = exportModelFrameObj(mesh, 0, QStringLiteral("test_material"));
	ok &= expect(!obj.isEmpty(), "A frame with geometry should export an OBJ.");
	const QStringList lines = obj.split(QLatin1Char('\n'));
	int positionLines = 0;
	int texCoordLines = 0;
	int normalLines = 0;
	int faceLines = 0;
	int groupLines = 0;
	int materialLines = 0;
	int minimumIndex = 1000000;
	int maximumIndex = 0;
	bool wellFormedFaces = true;
	for (const QString& line : lines) {
		if (line.startsWith(QStringLiteral("v "))) {
			++positionLines;
		} else if (line.startsWith(QStringLiteral("vt "))) {
			++texCoordLines;
		} else if (line.startsWith(QStringLiteral("vn "))) {
			++normalLines;
		} else if (line.startsWith(QStringLiteral("g "))) {
			++groupLines;
		} else if (line.startsWith(QStringLiteral("usemtl "))) {
			++materialLines;
		} else if (line.startsWith(QStringLiteral("f "))) {
			++faceLines;
			const QStringList corners = line.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
			if (corners.size() != 3) {
				wellFormedFaces = false;
				continue;
			}
			for (const QString& corner : corners) {
				const QStringList parts = corner.split(QLatin1Char('/'));
				if (parts.size() != 3 || parts.at(0) != parts.at(1) || parts.at(1) != parts.at(2)) {
					wellFormedFaces = false;
					continue;
				}
				bool parsed = false;
				const int index = parts.at(0).toInt(&parsed);
				if (!parsed) {
					wellFormedFaces = false;
					continue;
				}
				minimumIndex = std::min(minimumIndex, index);
				maximumIndex = std::max(maximumIndex, index);
			}
		}
	}
	ok &= expect(positionLines == mesh.vertexCount, "The OBJ should hold one v record per vertex.");
	ok &= expect(texCoordLines == mesh.vertexCount, "The OBJ should hold one vt record per vertex.");
	ok &= expect(normalLines == mesh.vertexCount, "The OBJ should hold one vn record per vertex.");
	ok &= expect(faceLines == mesh.triangleCount, "The OBJ should hold one f record per triangle.");
	ok &= expect(groupLines == 2, "Each surface should become its own group.");
	ok &= expect(materialLines == 2, "The material name should be emitted for each group.");
	ok &= expect(wellFormedFaces, "Face records should be v/vt/vn triples.");
	ok &= expect(minimumIndex >= 1, "OBJ indices are 1-based.");
	ok &= expect(maximumIndex <= mesh.vertexCount, "OBJ indices must stay inside the exported vertex list.");
	ok &= expect(obj.contains(QStringLiteral("g body")) && obj.contains(QStringLiteral("g head")), "Group names should come from the surfaces.");
	ok &= expect(!obj.contains(QLatin1Char(',')), "Exported numbers must never use a locale decimal comma.");
	ok &= expect(exportModelFrameObj(mesh, 0) == exportModelFrameObj(mesh, 0), "The OBJ export should be deterministic.");
	ok &= expect(exportModelFrameObj(mesh, 5).isEmpty(), "An out-of-range frame exports nothing.");
	ok &= expect(exportModelFrameObj(mesh, -1).isEmpty(), "A negative frame index exports nothing.");
	ok &= expect(!exportModelFrameObj(mesh, 0).contains(QStringLiteral("usemtl")), "No material means no usemtl record.");
	return ok;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	const QFileInfo info(path);
	if (!QDir().mkpath(info.absolutePath())) {
		return false;
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	const bool written = file.write(bytes) == bytes.size();
	file.close();
	return written;
}

bool runArchiveSmoke(const QString& root)
{
	bool ok = true;
	PackageArchive archive;
	QString error;
	if (!archive.load(root, &error)) {
		return expect(false, "The folder package fixture should open.");
	}

	const ModelMesh md2 = decodeModelMeshFromArchive(archive, QStringLiteral("models/test/tris.md2"), QStringLiteral("quake2"));
	ok &= expect(md2.error.isEmpty() && md2.geometryAvailable, "An MD2 should decode straight out of the archive.");
	ok &= expect(md2.surfaces.size() == 1 && md2.surfaces.first().vertexCount == 4, "Archive decoding should match in-memory decoding.");

	QString resolvedPath;
	const QImage skin = resolveModelSkin(archive, md2, QStringLiteral("quake2"), &resolvedPath);
	// The MD2 asks for skin.pcx; only skin.png exists, so the extension probe
	// has to find it.
	ok &= expect(!skin.isNull(), "The external skin should resolve through the extension probe.");
	ok &= expect(resolvedPath == QStringLiteral("models/test/skin.png"), "The resolved skin path should be reported.");
	ok &= expect(skin.width() == 2 && skin.height() == 2, "The resolved skin should decode at its real size.");

	const ModelMesh mdl = decodeModelMeshFromArchive(archive, QStringLiteral("progs/skinned.mdl"));
	ok &= expect(mdl.error.isEmpty() && mdl.embeddedSkins.size() == 1, "An MDL should decode its embedded skin from the archive.");
	QString embeddedPath = QStringLiteral("unchanged");
	const QImage embedded = resolveModelSkin(archive, mdl, QString(), &embeddedPath);
	ok &= expect(!embedded.isNull() && embedded.width() == 4, "The embedded skin should be the fallback when nothing external resolves.");
	ok &= expect(embeddedPath.isEmpty(), "An embedded fallback resolves no archive path.");

	const ModelMesh missing = decodeModelMeshFromArchive(archive, QStringLiteral("models/test/nothing.md2"));
	ok &= expect(!missing.error.isEmpty(), "A missing package entry should report an error.");
	return ok;
}

} // namespace

int main()
{
	bool ok = true;
	ok &= runFormatDetectionSmoke();
	ok &= runMdlGeometrySmoke();
	ok &= runMdlSeamSmoke();
	ok &= runMdlSkinSmoke();
	ok &= runMd2GeometrySmoke();
	ok &= runMd2AnimationSmoke();
	ok &= runMd3GeometrySmoke();
	ok &= runTruncatedHeaderSmoke();
	ok &= runHostileOffsetSmoke();
	ok &= runOverflowSmoke();
	ok &= runHeaderOnlySmoke();
	ok &= runObjExportSmoke();

	QTemporaryDir temporaryDir;
	if (!temporaryDir.isValid()) {
		std::cerr << "Unable to create a temporary directory.\n";
		return EXIT_FAILURE;
	}
	const QString root = QDir(temporaryDir.path()).absoluteFilePath(QStringLiteral("package"));
	QByteArray skinPixels;
	for (int index = 0; index < 8; ++index) {
		appendU8(skinPixels, index * 7);
	}
	QImage skinImage(2, 2, QImage::Format_ARGB32);
	skinImage.fill(qRgb(200, 40, 40));
	QByteArray pngBytes;
	{
		QBuffer buffer(&pngBytes);
		if (!buffer.open(QIODevice::WriteOnly) || !skinImage.save(&buffer, "PNG")) {
			std::cerr << "Unable to encode the skin fixture.\n";
			return EXIT_FAILURE;
		}
	}
	if (!writeFile(QDir(root).absoluteFilePath(QStringLiteral("models/test/tris.md2")), buildMd2({QByteArrayLiteral("stand01")}))
		|| !writeFile(QDir(root).absoluteFilePath(QStringLiteral("models/test/skin.png")), pngBytes)
		|| !writeFile(QDir(root).absoluteFilePath(QStringLiteral("progs/skinned.mdl")), buildMdl(false, 1, skinPixels, 4, 2))) {
		std::cerr << "Unable to write the package fixture.\n";
		return EXIT_FAILURE;
	}
	ok &= runArchiveSmoke(root);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
