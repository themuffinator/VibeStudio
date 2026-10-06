#include "core/model_assembly.h"
#include "core/model_tags.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <algorithm>
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
		std::cerr << "FAIL: " << message << std::endl;
	}
	return value;
}
bool close(ModelVec3 a, ModelVec3 b) { return std::hypot(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z) < .0001; }
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
ModelMesh model(const QString &tagName, bool animatedTag)
{
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	ModelSurface surface;
	surface.name = QStringLiteral("triangle");
	surface.vertexCount = 3;
	surface.triangles = {{0, 1, 2}};
	surface.texCoords = {{0, 0}, {1, 0}, {0, 1}};
	surface.uvSeams.insert({0, 1});
	surface.skinPaths = {QStringLiteral("textures/assembly/check")};
	for (int frame = 0; frame < 2; ++frame)
	{
		ModelFrameGeometry geometry;
		geometry.positions = {{0, 0, float(frame)}, {1, 0, float(frame)}, {0, 1, float(frame)}};
		geometry.normals.fill({0, 0, 1}, 3);
		surface.frames.append(geometry);
		ModelFrameInfo info;
		info.name = QStringLiteral("pose%1").arg(frame);
		mesh.frames.append(info);
		ModelTag tag;
		tag.name = tagName;
		tag.frameIndex = frame;
		tag.origin = animatedTag ? ModelVec3{float(10 + frame * 10), 0, 0} : ModelVec3{0, 5, 0};
		if (animatedTag && frame == 1)
		{
			const float rotation[]{0, 1, 0, -1, 0, 0, 0, 0, 1};
			std::copy(std::begin(rotation), std::end(rotation), std::begin(tag.axis));
		}
		mesh.tags.append(tag);
	}
	mesh.surfaces.append(surface);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("assembly-core-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	QString error;
	bool ok = true;
	auto rootMesh = model(QStringLiteral("tag_upper"), true), childMesh = model(QStringLiteral("tag_tip"), false);
	const auto rootBytes = QJsonDocument(editableModelJson(rootMesh)).toJson(QJsonDocument::Compact);
	const auto childBytes = QJsonDocument(editableModelJson(childMesh)).toJson(QJsonDocument::Compact);
	ok &=
		expect(write(path("root.mesh.json"), rootBytes) && write(path("child.mesh.json"), childBytes), "original synthetic model sources");
	ModelAssemblyPart rootPart;
	rootPart.id = "root";
	rootPart.source = "root.mesh.json";
	rootPart.translation = {1, 2, 3};
	rootPart.rotation = {0, 0, 90};
	rootPart.scale = 2;
	rootPart.framesPerSecond = 1;
	ModelAssemblyPart child;
	child.id = "child";
	child.source = "child.mesh.json";
	child.parent = "root";
	child.tag = "tag_upper";
	child.translation = {1, 0, 0};
	child.scale = .5;
	child.lastFrame = 0;
	child.framesPerSecond = 0;
	ModelAssemblyPart tip = child;
	tip.id = "tip";
	tip.parent = "child";
	tip.tag = "tag_tip";
	tip.translation = {};
	tip.scale = 1;
	ModelAssembly assembly;
	assembly.name = "Three-part rig";
	assembly.parts = {tip, child, rootPart};
	QVector<int> order;
	ok &= expect(validateModelAssembly(assembly, &error, &order) && order == QVector<int>{2, 1, 0},
				 "parents precede children regardless of storage order");
	const auto bytes = QJsonDocument(modelAssemblyJson(assembly)).toJson(QJsonDocument::Compact);
	ModelAssembly parsed;
	ok &= expect(parseModelAssembly(bytes, &parsed, &error) && modelAssemblyFingerprint(parsed) == modelAssemblyFingerprint(assembly),
				 "all assembly settings round trip");
	for (const auto &key : {"version", "parts", "name"})
	{
		auto json = modelAssemblyJson(assembly);
		json.insert(key, QJsonValue::Null);
		const auto before = modelAssemblyFingerprint(parsed);
		ok &= expect(!parseModelAssembly(QJsonDocument(json).toJson(), &parsed, &error) && modelAssemblyFingerprint(parsed) == before,
					 "malformed parse is atomic");
	}
	auto json = modelAssemblyJson(assembly);
	json.insert("unexpected", true);
	ok &= expect(!parseModelAssembly(QJsonDocument(json).toJson(), &parsed, &error),
				 "unknown source fields are rejected instead of discarded");
	ModelAssemblyContext context;
	context.directory = temporary.path();
	ModelAssemblyResolved resolved;
	ok &= expect(resolveModelAssembly(assembly, context, &resolved, &error), qPrintable(error));
	ModelAssemblyPose pose;
	ok &= expect(sampleModelAssembly(assembly, resolved, .5, &pose, &error), qPrintable(error));
	if (!ok)
	{
		return EXIT_FAILURE;
	}
	const double diagonal = std::sqrt(2.0);
	const ModelVec3 childOrigin{float(1 - diagonal), float(32 + diagonal), 3};
	const ModelVec3 tipOrigin{float(childOrigin.x - 5 / std::sqrt(2.0)), float(childOrigin.y - 5 / std::sqrt(2.0)), 3};
	ok &= expect(close(pose.partTransforms[1].origin, childOrigin) && close(pose.partTransforms[0].origin, tipOrigin),
				 "animated parent tags compose with nested local rotations, offsets and inherited uniform scale");
	ok &= expect(pose.surfaceParts == QVector<int>{2, 1, 0} && pose.mesh.surfaceCount == 3 && pose.mesh.vertexCount == 9 &&
					 pose.mesh.frameCount == 1,
				 "one-pose composition preserves part-to-surface identity");
	ok &= expect(close(pose.mesh.surfaces[1].frames[0].positions[0], childOrigin) && pose.samples[1].frame == 0 &&
					 pose.samples[1].fraction == 0,
				 "child animation is independent of its animated parent");
	ok &= expect(pose.mesh.surfaces[1].texCoords.size() == 3 && pose.mesh.surfaces[1].uvSeams.contains({0, 1}) &&
					 pose.mesh.surfaces[1].skinPaths == childMesh.surfaces[0].skinPaths && !pose.notes.isEmpty(),
				 "UV seams and material paths survive composition with explicit bake notes");
	ok &= expect(validateEditableModel(pose.mesh).isEmpty() && pose.mesh.tags.isEmpty(),
				 "baked geometry is editable while tag identities remain in the recipe inputs");
	ModelAssembly invalid = assembly;
	invalid.parts[1].parent = "tip";
	ok &= expect(!validateModelAssembly(invalid, &error), "cycles are rejected");
	invalid = assembly;
	invalid.parts[0].id = "ROOT";
	ok &= expect(!validateModelAssembly(invalid, &error), "IDs cannot collide on case-insensitive filesystems");
	invalid = assembly;
	invalid.parts[0].scale = 0;
	ok &= expect(!validateModelAssembly(invalid, &error), "zero scale is rejected");
	invalid = assembly;
	invalid.parts[0].lastFrame = 100;
	ok &= expect(!resolveModelAssembly(invalid, context, &resolved, &error), "animation range checks use the actual model");
	invalid = assembly;
	invalid.parts[0].tag = "missing";
	ok &= expect(!resolveModelAssembly(invalid, context, &resolved, &error), "missing parent tags cannot silently detach a child");
	ok &= expect(resolved.recipeSha256 == modelAssemblyFingerprint(assembly), "failed resolution preserves the previous snapshot");
	invalid = assembly;
	invalid.parts[0].translation.x = 5;
	ok &= expect(!sampleModelAssembly(invalid, resolved, 0, &pose, &error), "stale recipes cannot reuse a resolved graph");
	ok &= expect(!sampleModelAssembly(assembly, resolved, -1, &pose, &error), "negative playback time is rejected");
	const auto beforePose = QJsonDocument(editableModelJson(pose.mesh)).toJson();
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	ok &= expect(!sampleModelAssembly(assembly, resolved, 0, &pose, &error, cancelled) &&
					 QJsonDocument(editableModelJson(pose.mesh)).toJson() == beforePose,
				 "cancelled pose sampling is atomic");
	ok &= expect(!resolveModelAssembly(assembly, context, &resolved, &error, cancelled) &&
					 resolved.recipeSha256 == modelAssemblyFingerprint(assembly),
				 "cancelled resolution preserves the current snapshot");
	ModelAssemblyDocument document;
	ok &= expect(document.setAssembly(assembly, temporary.path(), &error) && document.save(path("rig.assembly.json"), false, &error),
				 "save guarded assembly with relative file references");
	const auto stored = read(path("rig.assembly.json"));
	ok &= expect(!stored.contains(temporary.path().toUtf8()) && !document.isModified(),
				 "saved references remain relocatable and the document becomes clean");
	ModelAssemblyDocument reopened;
	ok &= expect(reopened.load(path("rig.assembly.json"), &error) &&
					 modelAssemblyFingerprint(reopened.assembly()) == modelAssemblyFingerprint(document.assembly()),
				 "load resolves relative files without changing source identity");
	auto renamed = reopened.assembly().parts[1];
	renamed.id = "upper";
	ok &= expect(reopened.setPart("child", renamed, &error) && reopened.assembly().parts[0].parent == "upper" &&
					 reopened.selectedPart() == "upper",
				 "renaming a part preserves descendant links and selection");
	ok &= expect(reopened.removeBranch("upper", &error) && reopened.assembly().parts.size() == 1 && reopened.selectedPart() == "root",
				 "branch removal includes descendants and selects the parent");
	ok &= expect(reopened.undo() && reopened.selectedPart() == "upper" && reopened.assembly().parts.size() == 3 && reopened.undo() &&
					 !reopened.isModified(),
				 "undo restores branch, links and selection through the saved state");
	ok &= expect(reopened.redo() && reopened.selectedPart() == "upper", "redo restores the renamed selection");
	ok &= expect(write(path("rig.assembly.json"), stored + ' ') && !document.save(path("rig.assembly.json"), true, &error) &&
					 read(path("rig.assembly.json")) == stored + ' ',
				 "even explicit overwrite cannot replace an externally changed assembly source");
	ok &= expect(document.save(path("copy.assembly.json"), false, &error), "Save As preserves the edited source binding separately");
	ModelExportReport report;
	ok &= expect(
		exportModelAssemblyPose(assembly, resolved, .5, path("baked.obj"), path("copy.assembly.json"), false, true, &error, {}, &report) &&
			!QFile::exists(path("baked.obj")) && !report.notes.isEmpty(),
		"dry-run prepares a bounded bake and creates no output");
	ok &= expect(exportModelAssemblyPose(assembly, resolved, .5, path("baked.obj"), path("copy.assembly.json"), false, false, &error),
				 qPrintable(error));
	const auto baked = read(path("baked.obj"));
	ok &= expect(!baked.isEmpty() && !exportModelAssemblyPose(assembly, resolved, .5, path("baked.obj"), {}, false, false, &error) &&
					 read(path("baked.obj")) == baked,
				 "existing exports require explicit overwrite");
	ok &= expect(!exportModelAssemblyPose(assembly, resolved, 0, path("root.mesh.json"), {}, true, false, &error) &&
					 read(path("root.mesh.json")) == rootBytes,
				 "bakes protect every model input");
	ok &=
		expect(!exportModelAssemblyPose(assembly, resolved, 0, path("copy.assembly.json"), path("copy.assembly.json"), true, false, &error),
			   "bakes protect the assembly source");
	ok &= expect(!exportModelAssemblyPose(assembly, resolved, 0, path("cancel.obj"), {}, false, false, &error, cancelled) &&
					 !QFile::exists(path("cancel.obj")),
				 "cancelled bake publishes nothing");
	PackageStagingModel staging;
	ok &=
		expect(staging.createEmpty(PackageArchiveFormat::Pak, {}, &error) && staging.addBytes(rootBytes, "models/root.mesh.json", &error) &&
				   staging.addBytes(childBytes, "models/child.mesh.json", &error),
			   "original staged package fixture");
	ModelAssembly packaged = assembly;
	for (auto &part : packaged.parts)
	{
		part.sourceKind = ModelAssemblySource::Package;
		part.source = "models/" + part.source;
	}
	context.archive = std::make_shared<PackageArchive>(packagePlannedArchive(staging));
	ModelAssemblyResolved packageResolved;
	ok &= expect(resolveModelAssembly(packaged, context, &packageResolved, &error) &&
					 sampleModelAssembly(packaged, packageResolved, .5, &pose, &error) && close(pose.partTransforms[1].origin, childOrigin),
				 "staged package inputs use the same attachment and pose services");
	ok &= expect(staging.deleteEntry("models/child.mesh.json", &error) && resolveModelAssembly(packaged, context, &packageResolved, &error),
				 "immutable package snapshot survives later staged deletion");
	context.archive = std::make_shared<PackageArchive>(packagePlannedArchive(staging));
	ok &= expect(!resolveModelAssembly(packaged, context, &packageResolved, &error),
				 "reloading the changed package reports its missing child");
	assembly.parts[2].rotation = {};
	assembly.parts[2].translation = {};
	assembly.parts[2].scale = 1;
	assembly.parts[2].interpolate = false;
	rootMesh.tags[0].axis[0] = -1;
	ok &=
		expect(write(path("root.mesh.json"), QJsonDocument(editableModelJson(rootMesh)).toJson()) &&
				   resolveModelAssembly(assembly, context, &resolved, &error) && sampleModelAssembly(assembly, resolved, .5, &pose, &error),
			   "stored reflected tags remain usable even when the next pose has another handedness");
	ok &= expect(pose.mesh.surfaces[1].triangles[0].b == 2 && pose.mesh.surfaces[1].triangles[0].c == 1,
				 "reflected attachment composition preserves front-face winding");
	assembly.parts[2].interpolate = true;
	ok &= expect(resolveModelAssembly(assembly, context, &resolved, &error) && !sampleModelAssembly(assembly, resolved, .5, &pose, &error),
				 "ambiguous interpolation cannot silently flip a child");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
