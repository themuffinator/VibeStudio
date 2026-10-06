#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_fingerprint.h"
#include "core/model_mdl.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_scale_test_helpers.h"

#include <QCoreApplication>
#include <QJsonDocument>

#include <cstdlib>
#include <functional>
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
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts.append(part);
	auto mesh = buildModelDesignMesh(design);
	mesh.sourcePath = QStringLiteral("tests/revision.mesh.json");
	mesh.frames.append(mesh.frames.first());
	mesh.frames[1].name = QStringLiteral("raised");
	mesh.surfaces[0].frames.append(mesh.surfaces[0].frames.first());
	for (auto &p : mesh.surfaces[0].frames[1].positions)
	{
		p.z += 1;
	}
	mesh.animations = {{QStringLiteral("idle"), 0, 2}};
	for (int frame = 0; frame < 2; ++frame)
	{
		ModelTag tag;
		tag.name = QStringLiteral("tag_test");
		tag.frameIndex = frame;
		mesh.tags.append(tag);
	}
	ModelEmbeddedSkin skin;
	skin.name = QStringLiteral("skin");
	skin.image = QImage(2, 2, QImage::Format_ARGB32);
	skin.image.fill(qRgba(40, 80, 160, 128));
	mesh.embeddedSkins.append(skin);
	updateEditableModelMetadata(&mesh);
	bool ok = expect(validateEditableModel(mesh).isEmpty(), "fingerprint fixture is a valid editable mesh");
	if (!ok)
	{
		return EXIT_FAILURE;
	}
	const auto original = source(mesh), fingerprint = modelStateFingerprint(mesh);
	ok &= expect(fingerprint.size() == 32 && modelStateFingerprint(mesh) == fingerprint, "state identity is stable and complete");
	const std::pair<const char *, std::function<void(ModelMesh &)>> changes[]{
		{"source path", [](auto &m) { m.sourcePath += QStringLiteral(".copy"); }},
		{"surface name", [](auto &m) { m.surfaces[0].name += QStringLiteral(" copy"); }},
		{"material paths", [](auto &m) { m.surfaces[0].skinPaths.append(QStringLiteral("textures/test.tga")); }},
		{"triangle winding", [](auto &m) { std::swap(m.surfaces[0].triangles[0].b, m.surfaces[0].triangles[0].c); }},
		{"UV coordinate", [](auto &m) { m.surfaces[0].texCoords[0].u += .125f; }},
		{"UV seam",
		 [](auto &m)
		 {
			 const auto t = m.surfaces[0].triangles[0];
			 m.surfaces[0].uvSeams.insert(modelEdge(t.a, t.b));
		 }},
		{"later pose position", [](auto &m) { m.surfaces[0].frames[1].positions[0].x += .25f; }},
		{"later pose normal", [](auto &m) { m.surfaces[0].frames[1].normals[0].x += .25f; }},
		{"frame name", [](auto &m) { m.frames[1].name += QStringLiteral(" copy"); }},
		{"frame origin", [](auto &m) { m.frames[1].origin.z += 1; }},
		{"animation name", [](auto &m) { m.animations[0].name += QStringLiteral(" copy"); }},
		{"animation range", [](auto &m) { m.animations[0].frameCount = 1; }},
		{"attachment origin", [](auto &m) { m.tags[1].origin.x += 1; }},
		{"attachment orientation",
		 [](auto &m)
		 {
			 m.tags[1].axis[0] = -1;
			 m.tags[1].axis[4] = -1;
		 }},
		{"attachment name",
		 [](auto &m)
		 {
			 for (auto &t : m.tags)
			 {
				 t.name += QStringLiteral("_copy");
			 }
		 }},
		{"embedded skin name", [](auto &m) { m.embeddedSkins[0].name += QStringLiteral(" copy"); }},
		{"embedded skin pixel", [](auto &m) { m.embeddedSkins[0].image.setPixel(0, 0, qRgba(40, 80, 161, 128)); }},
		{"MD2 skin dimensions", [](auto &m) { m.md2SkinSize = {512, 256}; }}};
	for (const auto &[name, change] : changes)
	{
		auto edited = mesh;
		change(edited);
		updateEditableModelMetadata(&edited);
		ok &= expect(validateEditableModel(edited).isEmpty() && source(edited) != original && modelStateFingerprint(edited) != fingerprint,
					 name);
	}
	auto reordered = mesh;
	const auto edges = modelSurfaceEdges(mesh.surfaces[0]);
	for (auto edge : edges)
	{
		mesh.surfaces[0].uvSeams.insert(edge);
	}
	for (auto at = edges.crbegin(); at != edges.crend(); ++at)
	{
		reordered.surfaces[0].uvSeams.insert(*at);
	}
	ok &= expect(source(mesh) == source(reordered) && modelStateFingerprint(mesh) == modelStateFingerprint(reordered),
				 "seam insertion order cannot change content identity");
	reordered = mesh;
	reordered.surfaces[0].frames[0].positions[0].z = -0.0f;
	ok &= expect(source(mesh) == source(reordered) && modelStateFingerprint(mesh) == modelStateFingerprint(reordered),
				 "signed zero follows serialized numeric equivalence");
	ModelMesh reopened;
	QString error;
	ok &= expect(parseEditableModel(source(mesh), &reopened, &error) && modelStateFingerprint(mesh) == modelStateFingerprint(reopened),
				 "source round trip retains content identity including skins and tags");
	auto palette = generatedIdTechPalette(QStringLiteral("quake"));
	palette.colors[1] = palette.colors[0];
	const auto mdl = decodeModelMesh(QStringLiteral("tests/revision.mdl"), tests::groupedMdlFixture().bytes, &palette);
	ok &= expect(validateEditableModel(mdl).isEmpty(), "native MDL fingerprint fixture is valid");
	const auto mdlSource = source(mdl), mdlFingerprint = modelStateFingerprint(mdl);
	const std::pair<const char *, std::function<void(ModelMesh &)>> nativeChanges[]{
		{"MDL flags", [](auto &m) { m.mdl.flags ^= 32; }},
		{"MDL eye", [](auto &m) { m.mdl.eyePosition.x += 1; }},
		{"MDL sync", [](auto &m) { m.mdl.syncType = 0; }},
		{"MDL size", [](auto &m) { m.mdl.size += 1; }},
		{"MDL palette", [](auto &m) { m.mdl.palette[0] = char(quint8(m.mdl.palette[0]) ^ 1); }},
		{"MDL pixel indices", [](auto &m) { m.embeddedSkins[0].indexedFrames[0][0] = 1; }},
		{"MDL skin timing", [](auto &m) { m.embeddedSkins[0].intervals[0] *= .9f; }},
		{"MDL pose timing", [](auto &m) { m.mdl.frameGroups[0].intervals[0] *= .9f; }}};
	for (const auto &[name, change] : nativeChanges)
	{
		auto edited = mdl;
		change(edited);
		ok &= expect(validateEditableModel(edited).isEmpty() && source(edited) != mdlSource &&
						 modelStateFingerprint(edited) != mdlFingerprint,
					 name);
	}
	auto previewOnly = mdl;
	previewOnly.embeddedSkins[0].image.fill(Qt::red);
	ok &= expect(source(previewOnly) == mdlSource && modelStateFingerprint(previewOnly) == mdlFingerprint,
				 "derived MDL preview colours are outside the indexed source identity");
	const auto large = tests::maximumEditableGrid();
	bool cancelled = false;
	ModelWorkControl control;
	control.cancelled = [&] { return cancelled; };
	control.progress = [&](ModelWorkPhase phase, qint64 done, qint64)
	{
		if (phase == ModelWorkPhase::Serializing && done >= 8192)
		{
			cancelled = true;
		}
	};
	ok &= expect(modelStateFingerprint(large, &error, control).isEmpty() && cancelled && !error.isEmpty(),
				 "streamed geometry hashing acknowledges cancellation without returning a partial identity");
	std::cout << "Fingerprint field changes checked: " << std::size(changes) + std::size(nativeChanges) << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
