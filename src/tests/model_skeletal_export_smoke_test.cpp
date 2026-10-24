// Skeletal export fidelity: MD5 mesh and animation, IQM and ASE keep what
// their formats hold and report or refuse the rest. Covers joint scale and
// mirroring through MD5's rigid joints, md5anim clip selection and frame
// rates, IQM influence reduction and quantisation reports, independent frame
// edits, attachments, material slots and strict exports, the all-clips
// batch, the clips, frame origins and skeleton MDL, MD2, MD3 and OBJ cannot
// hold, and `model build` (when the vibestudio executable is passed).
#include "core/model_document.h"
#include "core/model_md5.h"
#include "core/model_iqm.h"
#include "core/model_skeletal_export.h"
#include "core/model_skeleton.h"
#include "tests/model_skeletal_export_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
bool near(double a, double b, double tolerance = 1e-4) { return std::abs(a - b) <= tolerance; }
bool near(const ModelVec3 &a, const ModelVec3 &b, double tolerance = 1e-4)
{
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}
bool mentions(const QStringList &notes, const QString &text)
{
	return std::any_of(notes.cbegin(), notes.cend(), [&](const QString &note) { return note.contains(text, Qt::CaseInsensitive); });
}
QByteArray sha256(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
ModelExportOptions clipOptions(int clip, int rate = 0, bool strict = false)
{
	ModelExportOptions options;
	options.clip = clip;
	options.md5FrameRate = rate;
	options.strict = strict;
	return options;
}
// The editor refuses frame edits on the clip frames of a skinned model and keeps the clip map
// through the edits it allows. A document saved before that, or edited by another tool, can still
// hold an added frame and no clip mapping, so build that state directly to check the export audit.
ModelMesh withUnmappedDuplicateFrame(ModelMesh mesh, int frame, QString *error)
{
	QVector<ModelSurfaceSkinning> skins;
	for (auto &surface : mesh.surfaces)
	{
		skins.append(surface.skinning);
		surface.skinning = {};
	}
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateFrame;
	duplicate.frame = frame;
	expect(applyModelEdit(&mesh, duplicate, nullptr, error), "duplicate a frame");
	for (int index = 0; index < mesh.surfaces.size(); ++index)
		mesh.surfaces[index].skinning = skins[index];
	QVector<int> map(mesh.frames.size(), -2);
	for (int index = 0; index < mesh.frames.size(); ++index)
	{
		if (mesh.frames[index].name == QLatin1String("bindpose"))
			map[index] = -1;
	}
	mesh.skeleton.bakedClipForFrame = map;
	return mesh;
}

// Doom 3's view of an md5mesh posed by an md5anim: the md5mesh's weights
// carried by the md5anim's rigid model-space joints.
bool md5Posed(const QByteArray &mesh, const QByteArray &anim, int frame, QVector<ModelFrameGeometry> *out)
{
	const auto decodedMesh = decodeModelMesh(QStringLiteral("rt/model.md5mesh"), mesh);
	const auto decodedAnim = decodeModelMesh(QStringLiteral("rt/clip.md5anim"), anim);
	if (!decodedMesh.error.isEmpty() || !decodedAnim.error.isEmpty() || decodedAnim.skeleton.clips.size() != 1 ||
		frame >= decodedAnim.skeleton.clips[0].frames.size())
	{
		return false;
	}
	out->clear();
	for (const auto &surface : decodedMesh.surfaces)
	{
		ModelFrameGeometry geometry;
		if (!skinModelSurface(surface, decodedAnim.skeleton.clips[0].frames[frame], &geometry))
		{
			return false;
		}
		out->append(geometry);
	}
	return true;
}
// The editor's pose of every surface in clip `clip` frame `frame`.
QVector<ModelFrameGeometry> editorPosed(const ModelMesh &mesh, int clip, int frame)
{
	QVector<ModelFrameGeometry> result;
	for (const auto &surface : mesh.surfaces)
	{
		ModelFrameGeometry geometry;
		if (surface.skinning.isEmpty())
		{
			geometry = surface.frames.first();
		}
		else
		{
			(void)skinModelSurface(surface, mesh.skeleton.clips[clip].frames[frame], &geometry);
		}
		result.append(geometry);
	}
	return result;
}
bool samePositions(const QVector<ModelFrameGeometry> &a, const QVector<ModelFrameGeometry> &b, double tolerance)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (int surface = 0; surface < a.size(); ++surface)
	{
		if (a[surface].positions.size() != b[surface].positions.size())
		{
			return false;
		}
		for (int vertex = 0; vertex < a[surface].positions.size(); ++vertex)
		{
			if (!near(a[surface].positions[vertex], b[surface].positions[vertex], tolerance))
			{
				return false;
			}
		}
	}
	return true;
}
// Every clip of `mesh` written as md5mesh plus md5anim reproduces the
// editor's posed vertices.
bool md5MatchesEditor(const ModelMesh &mesh, double tolerance, QString *error)
{
	const auto meshBytes = exportEditableModel(mesh, QStringLiteral("md5mesh"), ModelExportOptions{}, error);
	if (meshBytes.isEmpty())
	{
		return false;
	}
	for (int clip = 0; clip < mesh.skeleton.clips.size(); ++clip)
	{
		const auto animBytes = exportEditableModel(mesh, QStringLiteral("md5anim"), clipOptions(clip), error);
		if (animBytes.isEmpty())
		{
			return false;
		}
		for (int frame = 0; frame < mesh.skeleton.clips[clip].frames.size(); ++frame)
		{
			QVector<ModelFrameGeometry> doom;
			if (!md5Posed(meshBytes, animBytes, frame, &doom) || !samePositions(doom, editorPosed(mesh, clip, frame), tolerance))
			{
				*error = QStringLiteral("clip %1 frame %2 differs").arg(clip).arg(frame);
				return false;
			}
		}
	}
	return true;
}

// SHA-256 of each format of the rigid arm as the writers produced it before
// joint scale, frame rate and report handling were added (recorded on
// 2026-10-21 from the previous build's `model build`, md5anim clips chosen
// by its --frame). Rigid rigs must keep exporting byte for byte.
struct Golden
{
	const char *format;
	int clip;
	const char *hash;
};
const Golden kGolden[] = {
	{"md5mesh", -1, "8d1b73bb962f979ba8308f261ac6a2867cd62329ab444055f84e1e91d536a480"},
	{"md5anim", 0, "73e4f9a5bced1431e9fb736b232c0858833d6ab5a81586c92bb202715f70c3cf"},
	{"md5anim", 1, "6dc1de89af77b12d796ce02f38bb8ef367c962e28b5a3d5073872243e8ad366f"},
	{"iqm", -1, "b8f32b697f03cf6b32f3c6e616c4883cc5035d52bb3f98ed72d22aa73587943e"},
	{"ase", -1, "342ec9720af6bd5443a823b79fd5fdebb7fc60a2ad8056be9fe2bc2487a75d32"},
};

void checkRigidGolden(const QString &directory)
{
	QString error;
	const auto fixture = exportArmFixture();
	if (!expect(!fixture.skeleton.isEmpty(), "arm fixture builds"))
	{
		return;
	}
	ModelDocument document;
	const auto source = QDir(directory).filePath(QStringLiteral("arm.mesh.json"));
	ModelDocument reopened;
	if (!expect(document.setMesh(fixture, &error) && document.save(source, false, &error) && reopened.load(source, &error),
				"arm source saves and reopens"))
	{
		std::cerr << qPrintable(error) << '\n';
		return;
	}
	const auto dump = qEnvironmentVariable("VIBESTUDIO_SKELETAL_EXPORT_DUMP");
	if (!dump.isEmpty())
	{
		QDir().mkpath(dump);
		QFile::remove(QDir(dump).filePath(QStringLiteral("arm.mesh.json")));
		QFile::copy(source, QDir(dump).filePath(QStringLiteral("arm.mesh.json")));
	}
	for (const auto &golden : kGolden)
	{
		ModelExportOptions options;
		options.clip = golden.clip;
		const auto bytes = exportEditableModel(reopened.mesh(), QString::fromLatin1(golden.format), options, &error);
		const auto hash = sha256(bytes);
		if (!dump.isEmpty())
		{
			std::cout << golden.format << ' ' << golden.clip << ' ' << hash.constData() << '\n';
		}
		if (!expect(!bytes.isEmpty() && hash == golden.hash, "a rigid rig exports byte for byte as before"))
		{
			std::cerr << "  " << golden.format << ' ' << golden.clip << ' ' << hash.constData() << ' ' << qPrintable(error) << '\n';
		}
	}
	// The rigid arm's md5 pair reproduces the editor's animation.
	expect(md5MatchesEditor(reopened.mesh(), 1e-3, &error), "rigid md5mesh and md5anim reproduce the editor's poses");
}

void checkJointScale()
{
	QString error;
	// A bind scale through the bind-pose editor scales mid and its child.
	auto mesh = exportArmFixture();
	ModelEdit scale;
	scale.kind = ModelEditKind::TransformBindJoint;
	scale.skeletal.joint = QStringLiteral("mid");
	scale.skeletal.scale = {2, 2, 2};
	if (!expect(applyModelEdit(&mesh, scale, nullptr, &error), "bind scale applies through the document edit"))
	{
		std::cerr << qPrintable(error) << '\n';
		return;
	}
	expect(!modelJointIsRigid(mesh.skeleton.joints[1].bind) && modelJointIsRigid(mesh.skeleton.joints[0].bind),
		   "the scaled joint is not rigid; its parent is");
	ModelExportReport report;
	const auto md5mesh = exportEditableModel(mesh, QStringLiteral("md5mesh"), ModelExportOptions{}, &error, {}, &report);
	expect(!md5mesh.isEmpty() && mentions(report.notes, QStringLiteral("mid")) && mentions(report.notes, QStringLiteral("weight offsets")) &&
			   !mentions(report.omissions, QStringLiteral("weight offsets")),
		   "md5mesh moves bind scale into weight offsets and says which joints");
	const auto decoded = decodeModelMesh(QStringLiteral("rt/scaled.md5mesh"), md5mesh);
	bool rigid = decoded.error.isEmpty();
	for (const auto &joint : decoded.skeleton.joints)
	{
		rigid &= modelJointIsRigid(joint.bind, 1e-4);
	}
	expect(rigid, "the md5mesh joints are rigid");
	const int bind = modelBindPoseFrame(mesh);
	bool same = decoded.surfaces.size() == 1 && bind >= 0;
	for (int vertex = 0; same && vertex < mesh.surfaces[0].vertexCount; ++vertex)
	{
		same = near(decoded.surfaces[0].frames[0].positions[vertex], mesh.surfaces[0].frames[bind].positions[vertex], 1e-4);
	}
	expect(same, "the bind-pose mesh survives exactly");
	expect(md5MatchesEditor(mesh, 1e-3, &error), "every clip of the scaled rig plays as in the editor");

	// Non-uniform scale with a mirror, written directly into the bind and
	// every pose, also survives.
	auto mirrored = exportArmFixture();
	const ModelVec3 stretch{-1.0f, 1.5f, 1.0f};
	const auto restyle = [&](ModelJointMatrix *matrix)
	{
		*matrix = modelJointMatrix(modelJointRotation(*matrix), modelJointTranslation(*matrix), stretch);
	};
	const auto positions = mirrored.surfaces[0].frames[0].positions;
	// A mirrored joint would mirror its tag, which attachments refuse.
	mirrored.skeleton.tags.clear();
	restyle(&mirrored.skeleton.joints[2].bind);
	for (auto &clip : mirrored.skeleton.clips)
		for (auto &frame : clip.frames)
			restyle(&frame[2]);
	// Rebind each vertex to the influences the fixture gave it.
	QVector<int> perVertex;
	QVector<float> perWeight;
	for (int vertex = 0; vertex < mirrored.surfaces[0].vertexCount; ++vertex)
	{
		const int first = mirrored.surfaces[0].skinning.first[vertex], count = mirrored.surfaces[0].skinning.count[vertex];
		for (int slot = 0; slot < 2; ++slot)
		{
			perVertex << (slot < count ? mirrored.surfaces[0].skinning.influences[first + slot].joint : 0);
			perWeight << (slot < count ? mirrored.surfaces[0].skinning.influences[first + slot].weight : 0.0f);
		}
	}
	expect(modelSkinningFromBindPose(mirrored.skeleton, positions, mirrored.surfaces[0].frames[0].normals, perVertex, perWeight, 2,
									 &mirrored.surfaces[0].skinning, &error) &&
			   bakeModelSkeleton(&mirrored, {}, &error),
		   "mirrored rig rebinds");
	updateEditableModelMetadata(&mirrored);
	expect(md5MatchesEditor(mirrored, 1e-3, &error), "a mirrored, non-uniformly scaled joint plays as in the editor");

	// A pose that scales a joint differently from its bind is refused, naming
	// the joint, the clip and the frame.
	auto refused = exportArmFixture();
	auto &tip = refused.skeleton.clips[1].frames[2][2];
	tip = modelJointMatrix(modelJointRotation(tip), modelJointTranslation(tip), {1.5f, 1.5f, 1.5f});
	expect(bakeModelSkeleton(&refused, {}, &error), "the edited fixture bakes");
	updateEditableModelMetadata(&refused);
	error.clear();
	expect(exportEditableModel(refused, QStringLiteral("md5anim"), clipOptions(1), &error).isEmpty() && error.contains(QStringLiteral("\"tip\"")) &&
			   error.contains(QStringLiteral("frame 2")) && error.contains(QStringLiteral("\"wave\"")),
		   "a clip that scales a joint apart from its bind is refused with the joint, clip and frame");
	expect(!exportEditableModel(refused, QStringLiteral("md5anim"), clipOptions(0), &error).isEmpty(), "the other clip still exports");
	ModelExportReport iqmReport;
	expect(!exportEditableModel(refused, QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &iqmReport).isEmpty(),
		   "IQM keeps joint scale, so it still exports the scaled pose");
}

void checkFrameRates()
{
	QString error;
	auto mesh = exportArmFixture();
	mesh.skeleton.clips[1].framesPerSecond = 12.5;
	error.clear();
	expect(exportEditableModel(mesh, QStringLiteral("md5anim"), clipOptions(1), &error).isEmpty() && error.contains(QStringLiteral("12.5")) &&
			   error.contains(QStringLiteral("whole number")),
		   "a fractional clip rate is refused without an explicit MD5 rate");
	ModelExportReport report;
	const auto chosen = exportEditableModel(mesh, QStringLiteral("md5anim"), clipOptions(1, 12), &error, {}, &report);
	expect(chosen.contains("\nframeRate 12\n") && report.frameRate == 12 && report.clip == 1 && report.clipName == QStringLiteral("wave") &&
			   report.clipFrames == 4 && mentions(report.notes, QStringLiteral("playback speed")),
		   "an explicit MD5 frame rate is written and its speed change reported");
	mesh.skeleton.clips[1].framesPerSecond = 0;
	const auto unspecified = exportEditableModel(mesh, QStringLiteral("md5anim"), clipOptions(1), &error, {}, &report);
	expect(unspecified.contains("\nframeRate 24\n") && mentions(report.notes, QStringLiteral("no frame rate")), "a clip without a rate notes the 24 used");
	mesh.skeleton.clips[1].framesPerSecond = 30;
	const auto whole = exportEditableModel(mesh, QStringLiteral("md5anim"), clipOptions(1), &error, {}, &report);
	// The note describes the file, so it reads the same for dry runs and refusals.
	expect(whole.contains("\nframeRate 30\n") && !mentions(report.notes, QStringLiteral("playback speed")) &&
			   report.notes.value(0).startsWith(QStringLiteral("Skeletal clip 1 \"wave\"")) && !mentions(report.notes, QStringLiteral("Wrote")),
		   "a whole rate is written as it is, naming the clip");
	ModelMd5ExportOptions direct;
	direct.frameRate = 200000;
	expect(exportModelMd5Anim(mesh, 1, direct, &error).isEmpty() && error.contains(QStringLiteral("100000")), "an out-of-range MD5 rate is refused");
}

void checkClipSelection()
{
	QString error;
	const auto mesh = exportArmFixture();
	expect(modelSkeletalClipIndex(mesh.skeleton, QStringLiteral("wave")) == 1 && modelSkeletalClipIndex(mesh.skeleton, QStringLiteral("WAVE")) == 1 &&
			   modelSkeletalClipIndex(mesh.skeleton, QStringLiteral("1")) == 1 && modelSkeletalClipIndex(mesh.skeleton, QStringLiteral("2")) == -1 &&
			   modelSkeletalClipIndex(mesh.skeleton, QStringLiteral("run")) == -1,
		   "clips resolve by index or name");
	expect(modelSkeletalClipForFrame(mesh, 0) == -1 && modelSkeletalClipForFrame(mesh, 1) == 0 && modelSkeletalClipForFrame(mesh, 4) == 1 &&
			   modelSkeletalClipForFrame(mesh, 99) == -1,
		   "frames map to the clip they are samples of");
	ModelSkeleton names;
	names.clips.resize(3);
	names.clips[0].name = QStringLiteral("run fast/1");
	names.clips[2].name = QStringLiteral("..");
	expect(modelMd5ClipFileName(names, 0) == QStringLiteral("run_fast_1.md5anim") && modelMd5ClipFileName(names, 1) == QStringLiteral("clip1.md5anim") &&
			   modelMd5ClipFileName(names, 2) == QStringLiteral("clip2.md5anim"),
		   "clip file names are safe");
	error.clear();
	expect(exportEditableModel(mesh, QStringLiteral("md5anim"), clipOptions(5), &error).isEmpty() && error.contains(QStringLiteral("no clip 5")),
		   "an out-of-range clip is refused instead of clamped");
	ModelExportReport report;
	ModelExportOptions byFrame;
	byFrame.frame = 4;
	expect(!exportEditableModel(mesh, QStringLiteral("md5anim"), byFrame, &error, {}, &report).isEmpty() && report.clip == 1,
		   "a frame selects the clip it belongs to");
	byFrame.frame = 0;
	error.clear();
	expect(exportEditableModel(mesh, QStringLiteral("md5anim"), byFrame, &error).isEmpty() && error.contains(QStringLiteral("idle")),
		   "the bind frame of a two-clip model asks for a clip");
	auto single = mesh;
	single.skeleton.clips.removeLast();
	expect(bakeModelSkeleton(&single, {}, &error), "the edited fixture bakes");
	updateEditableModelMetadata(&single);
	expect(!exportEditableModel(single, QStringLiteral("md5anim"), 0, &error, {}, &report).isEmpty() && report.clip == 0,
		   "the bind frame of a one-clip model writes that clip");
}

ModelMesh influenceFixture(const QVector<float> &weights)
{
	// One joint per weight along +X, all influencing vertex 0 of a strip.
	ModelMesh mesh = exportArmFixture();
	auto &skeleton = mesh.skeleton;
	skeleton.tags.clear();
	skeleton.joints.clear();
	QVector<ModelJointMatrix> local;
	for (int joint = 0; joint < weights.size(); ++joint)
	{
		skeleton.joints.append({QStringLiteral("j%1").arg(joint), joint - 1, {}});
		local.append(modelJointMatrix({}, {joint == 0 ? 0.0f : 4.0f, 0, 0}));
	}
	const auto bind = exportPose(skeleton, local);
	for (int joint = 0; joint < bind.size(); ++joint)
		skeleton.joints[joint].bind = bind[joint];
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("sway");
	clip.framesPerSecond = 20;
	clip.frames = {bind, exportPose(skeleton, [&] {
					   auto moved = local;
					   moved[1] = modelJointMatrix(exportAboutZ(15), {4, 0, 0});
					   return moved;
				   }())};
	skeleton.clips = {clip};
	auto &surface = mesh.surfaces[0];
	QVector<int> joints;
	QVector<float> perVertex;
	for (int vertex = 0; vertex < surface.vertexCount; ++vertex)
		for (int slot = 0; slot < weights.size(); ++slot)
		{
			joints << slot;
			perVertex << (vertex == 0 ? weights[slot] : (slot == 0 ? 1.0f : 0.0f));
		}
	QString error;
	if (!modelSkinningFromBindPose(skeleton, surface.frames[0].positions, surface.frames[0].normals, joints, perVertex, int(weights.size()),
								   &surface.skinning, &error) ||
		!bakeModelSkeleton(&mesh, {}, &error))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// The arm scaled by `factor` about the origin, without its tag: joint
// translations and bind positions grow, rotations stay, and each vertex is
// bound again to the influences the fixture gave it.
ModelMesh scaledArm(float factor)
{
	auto mesh = exportArmFixture();
	mesh.skeleton.tags.clear();
	mesh.tags.clear();
	const auto grow = [factor](ModelJointMatrix *matrix)
	{
		const auto t = modelJointTranslation(*matrix);
		*matrix = modelJointMatrix(modelJointRotation(*matrix), {t.x * factor, t.y * factor, t.z * factor});
	};
	for (auto &joint : mesh.skeleton.joints)
		grow(&joint.bind);
	for (auto &clip : mesh.skeleton.clips)
		for (auto &frame : clip.frames)
			for (auto &matrix : frame)
				grow(&matrix);
	auto &surface = mesh.surfaces[0];
	auto positions = surface.frames[modelBindPoseFrame(mesh)].positions;
	for (auto &p : positions)
		p = {p.x * factor, p.y * factor, p.z * factor};
	QVector<int> joints;
	QVector<float> weights;
	for (int vertex = 0; vertex < surface.vertexCount; ++vertex)
	{
		const int first = surface.skinning.first[vertex], count = surface.skinning.count[vertex];
		for (int slot = 0; slot < 2; ++slot)
		{
			joints << (slot < count ? surface.skinning.influences[first + slot].joint : 0);
			weights << (slot < count ? surface.skinning.influences[first + slot].weight : 0.0f);
		}
	}
	QString error;
	if (!modelSkinningFromBindPose(mesh.skeleton, positions, surface.frames[0].normals, joints, weights, 2, &surface.skinning, &error) ||
		!bakeModelSkeleton(&mesh, {}, &error))
		return {};
	mesh.tags.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// The arm as a frame-animated strip with no skeleton, its frames named
// stand1-stand4 and run1-run4 and its clips the ones those names give, so
// MDL, MD2 and MD3 readers rebuild them; with a PCX skin for MD2.
ModelMesh namedFrames()
{
	auto mesh = exportArmFixture();
	mesh.skeleton = {};
	for (auto &surface : mesh.surfaces)
		surface.skinning = {};
	mesh.tags.clear();
	for (int frame = 0; frame < mesh.frames.size(); ++frame)
	{
		mesh.frames[frame].name = frame < 4 ? QStringLiteral("stand%1").arg(frame + 1) : QStringLiteral("run%1").arg(frame - 3);
		mesh.frames[frame].origin = {};
	}
	mesh.animations = modelAnimationsFromFrameNames(mesh.frames);
	mesh.surfaces[0].skinPaths = {QStringLiteral("models/test/arm.pcx")};
	mesh.md2SkinSize = {64, 64};
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// MDL, MD2, MD3 and OBJ have no clip table: clips the frame names rebuild are
// kept, and only clips, origins or a skeleton they cannot hold are omissions.
void checkVertexFormatClips()
{
	QString error;
	ModelExportOptions strict;
	strict.strict = true;
	ModelExportReport report;
	const auto named = namedFrames();
	expect(named.frames.size() == 8 && named.animations.size() == 2 && named.animations[1].name == QStringLiteral("run") &&
			   named.animations[1].frameCount == 4,
		   "the strip's frame names give stand and run");
	for (const auto &format : {QStringLiteral("md2"), QStringLiteral("md3")})
	{
		const auto bytes = exportEditableModel(named, format, strict, &error, {}, &report);
		const QByteArray label = QStringLiteral("clips the frame names rebuild pass a strict %1 export").arg(format).toUtf8();
		if (!expect(!bytes.isEmpty() && report.omissions.isEmpty(), label.constData()))
			std::cerr << "  " << qPrintable(error) << '\n';
		ModelMesh back;
		expect(importEditableModel(QStringLiteral("rt/named.") + format, bytes, &back, &error) && back.animations.size() == 2 &&
				   back.animations[1].name == QStringLiteral("run") && back.animations[1].frameCount == 4,
			   "and a reader rebuilds them");
		const auto again = exportEditableModel(back, format, strict, &error, {}, &report);
		const QByteArray reimport = QStringLiteral("the re-imported %1 exports strictly again").arg(format).toUtf8();
		if (!expect(!again.isEmpty() && report.omissions.isEmpty(), reimport.constData()))
			std::cerr << "  " << qPrintable(error) << '\n';
	}
	// Renamed and regrouped clips do not come back from the frame names.
	auto renamed = named;
	renamed.animations = {{QStringLiteral("idle"), 0, 4, 0}, {QStringLiteral("jump"), 4, 2, 0}, {QStringLiteral("land"), 6, 2, 0}};
	for (const auto &format : {QStringLiteral("md2"), QStringLiteral("md3")})
	{
		error.clear();
		const QByteArray label = QStringLiteral("%1 reports renamed clips and a strict export refuses them").arg(format).toUtf8();
		expect(exportEditableModel(renamed, format, strict, &error).isEmpty() && error.contains(QStringLiteral("jump")) &&
				   !exportEditableModel(renamed, format, ModelExportOptions{}, &error, {}, &report).isEmpty() &&
				   mentions(report.omissions, QStringLiteral("idle, jump, land")),
			   label.constData());
	}
	auto timed = named;
	timed.animations[1].framesPerSecond = 10;
	error.clear();
	expect(exportEditableModel(timed, QStringLiteral("md3"), strict, &error).isEmpty() && error.contains(QStringLiteral("run")),
		   "a clip rate the format cannot hold is an omission");
	// Frame origins: MD2 readers place each at the centre of the frame's bounds.
	auto placed = named;
	placed.frames[2].origin = {100, 0, 0};
	expect(!exportEditableModel(placed, QStringLiteral("md2"), ModelExportOptions{}, &error, {}, &report).isEmpty() &&
			   mentions(report.omissions, QStringLiteral("frame origins")) &&
			   !exportEditableModel(placed, QStringLiteral("md3"), strict, &error).isEmpty(),
		   "an authored origin is an MD2 omission and MD3 keeps it");
	auto centred = named;
	for (int frame = 0; frame < centred.frames.size(); ++frame)
	{
		const auto &info = centred.frames[frame];
		centred.frames[frame].origin = {(info.mins.x + info.maxs.x) * 0.5f, (info.mins.y + info.maxs.y) * 0.5f, (info.mins.z + info.maxs.z) * 0.5f};
	}
	expect(!exportEditableModel(centred, QStringLiteral("md2"), strict, &error).isEmpty(), "origins at the bounds' centre come back, so MD2 keeps them");
	// A one-frame prop: OBJ holds its only pose, and MD3's round trip is clean.
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	const auto prop = buildModelDesignMesh(design);
	ModelMesh propBack;
	const auto propMd3 = exportEditableModel(prop, QStringLiteral("md3"), ModelExportOptions{}, &error);
	expect(importEditableModel(QStringLiteral("rt/prop.md3"), propMd3, &propBack, &error) && !propBack.animations.isEmpty() &&
			   !exportEditableModel(propBack, QStringLiteral("md3"), strict, &error).isEmpty() &&
			   !exportEditableModel(propBack, QStringLiteral("obj"), strict, &error).isEmpty(),
		   "an imported one-frame prop exports strictly to MD3 and OBJ");
	error.clear();
	expect(exportEditableModel(named, QStringLiteral("obj"), strict, &error).isEmpty() && error.contains(QStringLiteral("selected frame")),
		   "OBJ still leaves out the other frames");
	// A skeleton in a vertex format.
	expect(!exportEditableModel(exportArmFixture(), QStringLiteral("md3"), ModelExportOptions{}, &error, {}, &report).isEmpty() &&
			   mentions(report.omissions, QStringLiteral("skeleton stays in the editable source")),
		   "MD3 reports the skeleton it does not store");
}

void checkIqmReports()
{
	QString error;
	// Six influences on one vertex: the strongest four are kept.
	const auto six = influenceFixture({0.3f, 0.25f, 0.2f, 0.1f, 0.1f, 0.05f});
	ModelExportReport report;
	const auto iqm = exportEditableModel(six, QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &report);
	expect(!iqm.isEmpty() && report.reducedInfluenceVertices == 1 && near(report.maxDroppedWeight, 0.15, 1e-6) &&
			   mentions(report.omissions, QStringLiteral("more than four joint influences")),
		   "IQM counts the vertex that lost influences and the weight they carried");
	const auto decoded = decodeModelMesh(QStringLiteral("rt/six.iqm"), iqm);
	expect(decoded.error.isEmpty() && decoded.surfaces[0].skinning.count[0] == 4, "the IQM vertex keeps four influences");
	ModelExportOptions strict;
	strict.strict = true;
	error.clear();
	expect(exportEditableModel(six, QStringLiteral("iqm"), strict, &error, {}, &report).isEmpty() && error.contains(QStringLiteral("Strict export refused")) &&
			   !report.omissions.isEmpty(),
		   "a strict IQM export refuses the reduction and still reports it");

	// Weights 0.5, 0.3 and 0.2 store as 127, 77 and 51 out of 255; the root
	// slides x through 0, 1 and 0.5, which 16-bit channels round.
	auto quantised = influenceFixture({0.5f, 0.3f, 0.2f});
	auto &frames = quantised.skeleton.clips[0].frames;
	const auto bind = modelSkeletonBindPose(quantised.skeleton);
	frames.clear();
	for (const float x : {0.0f, 1.0f, 0.5f})
	{
		auto pose = bind;
		const auto moved = modelJointMatrix({}, {x, 0, 0});
		for (auto &matrix : pose)
			matrix = modelJointMultiply(moved, matrix);
		frames.append(pose);
	}
	expect(bakeModelSkeleton(&quantised, {}, &error), "the edited fixture bakes");
	updateEditableModelMetadata(&quantised);
	expect(!exportEditableModel(quantised, QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &report).isEmpty(), "quantised IQM exports");
	const float scale = float(1.0 / 65535.0);
	double expected = 0;
	for (const float x : {0.0f, 1.0f, 0.5f})
	{
		float restored = 0.0f;
		restored += float(std::clamp<long>(std::lround(double(x) / double(scale)), 0, 65535)) * scale;
		expected = std::max(expected, std::abs(double(restored) - x));
	}
	expect(near(report.maxWeightError, 0.5 / 255.0, 1e-6) && near(report.maxJointTranslationError, expected, 1e-9) &&
			   near(report.maxPositionError, expected, 1e-6) &&
			   near(report.maxJointRotationDegrees, 0, 1e-6) && near(report.maxJointScaleError, 0, 1e-9) && report.reducedInfluenceVertices == 0 &&
			   mentions(report.notes, QStringLiteral("1/255")) && mentions(report.notes, QStringLiteral("16-bit")),
		   "IQM weight and pose channel errors match the analytic quantisation");

	// MD5-style influences whose offsets disagree keep the bind position but
	// deform apart; IQM stores one bind position per vertex, so it reports the
	// difference as an omission.
	auto disagreeing = exportArmFixture();
	auto &shared = disagreeing.surfaces[0].skinning;
	const int vertex = 5; // (8, 8): root 0.25, mid 0.75
	if (expect(shared.count[vertex] == 2, "the shared vertex has two influences"))
	{
		shared.influences[shared.first[vertex]].offset.y -= 6;
		shared.influences[shared.first[vertex] + 1].offset.y += 2;
	}
	expect(bakeModelSkeleton(&disagreeing, {}, &error), "the disagreeing rig bakes");
	updateEditableModelMetadata(&disagreeing);
	ModelExportReport disagreed;
	expect(!exportEditableModel(disagreeing, QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &disagreed).isEmpty() &&
			   disagreed.maxPositionError > 0.5 && mentions(disagreed.omissions, QStringLiteral("1/64")),
		   "IQM measures how far disagreeing influence offsets move a vertex and reports it");
	expect(disagreed.maxLayoutError > 0.5, "the difference is the layout's own, before any rounding");
	ModelExportReport rigid;
	expect(!exportEditableModel(exportArmFixture(), QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &rigid).isEmpty() &&
			   rigid.maxPositionError < 1.0 / 64.0 && !mentions(rigid.omissions, QStringLiteral("1/64")) &&
			   mentions(rigid.notes, QStringLiteral("move skinned vertices")),
		   "the arm's IQM stays within MD3's precision and says how far it moves");
	// The same consistent rig at Doom 3 scale (24-unit segments): 8-bit weights
	// move the vertex root and mid share past 1/64 unit, but that is the
	// format's rounding, not data left out, so a strict export writes it.
	const auto large = scaledArm(3.0f);
	ModelExportReport scaled;
	ModelExportOptions strictIqm;
	strictIqm.strict = true;
	error.clear();
	const auto largeIqm = exportEditableModel(large, QStringLiteral("iqm"), strictIqm, &error, {}, &scaled);
	if (!expect(!largeIqm.isEmpty() && scaled.maxPositionError > 1.0 / 64.0 && scaled.maxLayoutError < 1e-3 && scaled.omissions.isEmpty() &&
					mentions(scaled.notes, QStringLiteral("move skinned vertices")),
				"a consistent rig at game scale is not refused for IQM's own rounding"))
		std::cerr << "  " << scaled.maxPositionError << ' ' << scaled.maxLayoutError << ' ' << qPrintable(scaled.omissions.join(QLatin1Char('|'))) << ' '
				  << qPrintable(error) << '\n';

	// A surface without influences stays in its bind position, as in the editor.
	auto mixed = exportArmFixture();
	auto still = exportStrip(QStringLiteral("shield"), 2);
	for (auto &position : still.frames[0].positions)
		position.z += 4;
	still.frames = QVector<ModelFrameGeometry>(mixed.frames.size(), still.frames[0]);
	mixed.surfaces.append(still);
	updateEditableModelMetadata(&mixed);
	const auto mixedIqm = exportEditableModel(mixed, QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &report);
	const auto mixedBack = decodeModelMesh(QStringLiteral("rt/mixed.iqm"), mixedIqm);
	bool kept = mixedBack.error.isEmpty() && mixedBack.surfaces.size() == 2 && mixedBack.surfaces[1].skinning.isEmpty();
	for (int frame = 0; kept && frame < mixedBack.surfaces[1].frames.size(); ++frame)
		for (int vertex = 0; kept && vertex < still.vertexCount; ++vertex)
			kept = near(mixedBack.surfaces[1].frames[frame].positions[vertex], still.frames[0].positions[vertex], 1e-6);
	expect(kept && mentions(report.notes, QStringLiteral("shield")), "an IQM surface without influences decodes still and is noted");
	// MD5 binds it to the root, which "wave" moves, so the game differs.
	ModelExportReport anim;
	expect(!exportEditableModel(mixed, QStringLiteral("md5anim"), clipOptions(1), &error, {}, &anim).isEmpty() &&
			   mentions(anim.omissions, QStringLiteral("shield")) && mentions(anim.omissions, QStringLiteral("\"root\"")),
		   "an md5anim that moves the root reports the unskinned surface following it");
	expect(!exportEditableModel(mixed, QStringLiteral("md5anim"), clipOptions(0), &error, {}, &anim).isEmpty() && anim.omissions.isEmpty(),
		   "a clip that keeps the root still has nothing to report");
}

void checkSourceOmissions()
{
	QString error;
	// An independent edit in a "wave" frame.
	auto edited = exportArmFixture();
	const int waveFrame = edited.animations[1].firstFrame + 2;
	edited.surfaces[0].frames[waveFrame].positions[7].z += 3;
	ModelExportReport report;
	expect(!exportEditableModel(edited, QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &report).isEmpty() &&
			   mentions(report.omissions, QStringLiteral("differ from the poses")) && mentions(report.omissions, QStringLiteral("wave")),
		   "IQM reports a frame edited apart from the skeleton");
	expect(!exportEditableModel(edited, QStringLiteral("md5anim"), clipOptions(1), &error, {}, &report).isEmpty() &&
			   mentions(report.omissions, QStringLiteral("differ from the poses")),
		   "the md5anim of that clip reports it");
	expect(!exportEditableModel(edited, QStringLiteral("md5anim"), clipOptions(0), &error, {}, &report).isEmpty() &&
			   !mentions(report.omissions, QStringLiteral("differ from the poses")),
		   "another clip's md5anim does not");
	error.clear();
	expect(exportEditableModel(edited, QStringLiteral("iqm"), ModelExportOptions{0, -1, 0, true}, &error).isEmpty() &&
			   error.contains(QStringLiteral("differ from the poses")),
		   "strict refuses the edited frame");
	ModelSkeletalFrameAudit audit;
	expect(auditModelSkeletalFrames(edited, &audit, &error) && audit.editedFrames == QVector<int>{waveFrame} && audit.editedVertices == 1 &&
			   audit.unmappedFrames.isEmpty(),
		   "the audit finds exactly the edited frame");

	// A duplicated frame is no clip sample.
	const auto duplicated = withUnmappedDuplicateFrame(exportArmFixture(), 2, &error);
	expect(!exportEditableModel(duplicated, QStringLiteral("iqm"), ModelExportOptions{}, &error, {}, &report).isEmpty() &&
			   mentions(report.omissions, QStringLiteral("not samples of a skeletal clip")),
		   "IQM reports frames that are not clip samples");
	// Adding a frame leaves no frame mapped to a clip, so every md5anim says it
	// cannot check its clip's frames, and a strict one is refused.
	auto edited2 = duplicated;
	edited2.surfaces[0].frames[6].positions[7].z += 3;
	ModelExportOptions strictWave;
	strictWave.clip = 1;
	strictWave.strict = true;
	error.clear();
	expect(exportEditableModel(edited2, QStringLiteral("md5anim"), strictWave, &error, {}, &report).isEmpty() &&
			   error.contains(QStringLiteral("not samples of a skeletal clip")) && error.contains(QStringLiteral("No baked frame is a sample of clip \"wave\"")) &&
			   !report.omissions.isEmpty(),
		   "a strict md5anim of a model whose frames were added is refused");
	expect(!exportEditableModel(duplicated, QStringLiteral("md5anim"), clipOptions(0), &error, {}, &report).isEmpty() &&
			   mentions(report.omissions, QStringLiteral("no longer match a skeletal clip")) && mentions(report.omissions, QStringLiteral("idle")),
		   "the md5anim of the clip whose frame animation grew reports it by name");
	ModelExportOptions strictAll;
	strictAll.strict = true;
	QVector<ModelMd5ClipFile> refused;
	error.clear();
	expect(!prepareModelMd5Clips(edited2, QStringLiteral("anims"), strictAll, &refused, &error) && refused.size() == 2 && refused[0].bytes.isEmpty() &&
			   refused[1].bytes.isEmpty() && !refused[0].report.omissions.isEmpty() && !refused[1].report.omissions.isEmpty() &&
			   error.contains(QStringLiteral("idle")) && error.contains(QStringLiteral("wave")),
		   "a strict all-clips batch prepares every clip and refuses, keeping each clip's omissions");
	const auto shared = modelMd5ClipsSharedNotes(refused);
	expect(mentions(shared, QStringLiteral("not samples of a skeletal clip")) && !mentions(shared, QStringLiteral("\"wave\"")),
		   "the frames no clip owns are a note every clip shares, listed once");

	// Tags, material slots, collision and the skeleton in ASE.
	auto extras = exportArmFixture();
	extras.surfaces[0].skinPaths = {QStringLiteral("models/test/arm"), QStringLiteral("models/test/arm_red")};
	ModelCollisionBox box;
	box.name = QStringLiteral("hull");
	extras.collisionBoxes = {box};
	updateEditableModelMetadata(&extras);
	for (const auto &format : {QStringLiteral("md5mesh"), QStringLiteral("iqm"), QStringLiteral("ase")})
	{
		const auto bytes = exportEditableModel(extras, format, ModelExportOptions{}, &error, {}, &report);
		const QByteArray label = QStringLiteral("%1 reports tags, alternate slots and collision").arg(format).toUtf8();
		expect(!bytes.isEmpty() && mentions(report.omissions, QStringLiteral("tag_hand")) && mentions(report.omissions, QStringLiteral("alternate material slots")) &&
				   mentions(report.omissions, QStringLiteral("Collision boxes")),
			   label.constData());
	}
	expect(mentions(report.omissions, QStringLiteral("no joints or weights")) && mentions(report.omissions, QStringLiteral("one static frame")),
		   "ASE reports the skeleton and the other frames");

	// A frame-animated model as MD5: one pose.
	auto frames = exportArmFixture();
	frames.skeleton = {};
	for (auto &surface : frames.surfaces)
		surface.skinning = {};
	updateEditableModelMetadata(&frames);
	expect(!exportEditableModel(frames, QStringLiteral("md5mesh"), ModelExportOptions{}, &error, {}, &report).isEmpty() &&
			   mentions(report.omissions, QStringLiteral("later frame")),
		   "an md5mesh of a frame-animated model reports the frames it leaves out");

	// Normals Doom 3 rebuilds differently are measured.
	auto tilted = exportArmFixture();
	for (auto &frame : tilted.surfaces[0].frames)
		for (auto &normal : frame.normals)
			normal = {0.6f, 0.0f, 0.8f};
	expect(!exportEditableModel(tilted, QStringLiteral("md5mesh"), ModelExportOptions{}, &error, {}, &report).isEmpty() &&
			   report.maxNormalAngleDegrees > 30 && mentions(report.notes, QStringLiteral("no vertex normals")),
		   "md5mesh measures how far Doom 3's rebuilt normals differ");
}

void checkRoundTrips()
{
	QString error;
	auto mesh = exportArmFixture();
	// Off-grid texture coordinates exercise the text formats' precision.
	for (auto &uv : mesh.surfaces[0].texCoords)
		uv = {uv.u * 0.3f + 0.0123f, uv.v * 0.7f + 0.0456f};
	updateEditableModelMetadata(&mesh);
	const int bind = modelBindPoseFrame(mesh);
	for (const auto &format : {QStringLiteral("md5mesh"), QStringLiteral("iqm"), QStringLiteral("ase")})
	{
		ModelExportOptions options;
		options.frame = bind;
		const auto bytes = exportEditableModel(mesh, format, options, &error);
		const auto decoded = decodeModelMesh(QStringLiteral("rt/arm.") + format, bytes);
		const auto &a = mesh.surfaces[0];
		bool same = !bytes.isEmpty() && decoded.error.isEmpty() && decoded.surfaces.size() == 1;
		const auto &b = decoded.surfaces.value(0);
		same = same && b.name == a.name && b.vertexCount == a.vertexCount && b.triangles.size() == a.triangles.size() &&
			   b.skinPaths.value(0).contains(a.skinPaths.first());
		if (!same)
			std::cerr << "  " << qPrintable(format) << " surface: " << qPrintable(b.name) << ' ' << b.vertexCount << ' '
					  << b.triangles.size() << ' ' << qPrintable(b.skinPaths.value(0)) << ' ' << qPrintable(decoded.error) << '\n';
		// Corner by corner: the same position and texture coordinate in the
		// same winding (ASE renumbers corners as it reads them).
		for (int t = 0; same && t < a.triangles.size(); ++t)
		{
			const int ca[3]{a.triangles[t].a, a.triangles[t].b, a.triangles[t].c};
			const int cb[3]{b.triangles[t].a, b.triangles[t].b, b.triangles[t].c};
			for (int corner = 0; same && corner < 3; ++corner)
			{
				same = near(a.texCoords[ca[corner]].u, b.texCoords[cb[corner]].u, 1e-7) && near(a.texCoords[ca[corner]].v, b.texCoords[cb[corner]].v, 1e-7) &&
					   near(a.frames[bind].positions[ca[corner]], b.frames[0].positions[cb[corner]], 1e-5);
			}
			if (!same)
				std::cerr << "  " << qPrintable(format) << " triangle " << t << " differs\n";
		}
		const QByteArray label = QStringLiteral("%1 keeps the surface, its triangles, UVs, bind positions and first material").arg(format).toUtf8();
		expect(same, label.constData());
		if (format == QStringLiteral("iqm") && same)
		{
			// Joints, clips, rates and loops come back; poses within 16-bit steps.
			bool clips = decoded.skeleton.joints.size() == 3 && decoded.skeleton.clips.size() == 2;
			for (int joint = 0; clips && joint < 3; ++joint)
				clips = decoded.skeleton.joints[joint].name == mesh.skeleton.joints[joint].name &&
						decoded.skeleton.joints[joint].parent == mesh.skeleton.joints[joint].parent;
			for (int clip = 0; clips && clip < 2; ++clip)
			{
				const auto &source = mesh.skeleton.clips[clip];
				const auto &back = decoded.skeleton.clips[clip];
				clips = back.name == source.name && back.frames.size() == source.frames.size() && near(back.framesPerSecond, source.framesPerSecond, 1e-6) &&
						back.loops == source.loops;
				if (!clips)
					std::cerr << "  iqm clip " << clip << ": " << qPrintable(back.name) << ' ' << back.frames.size() << ' ' << back.framesPerSecond
							  << ' ' << back.loops << '\n';
				for (int frame = 0; clips && frame < source.frames.size(); ++frame)
				{
					ModelFrameGeometry posed;
					// 8-bit weights move a vertex shared by joints a few thousandths.
					clips = skinModelSurface(decoded.surfaces[0], back.frames[frame], &posed) &&
							samePositions({posed}, editorPosed(mesh, clip, frame), 1e-2);
					if (!clips)
						std::cerr << "  iqm clip " << clip << " frame " << frame << " differs\n";
				}
			}
			expect(clips, "IQM keeps joints, clips, rates and loops, and poses within its channel precision");
		}
	}
}

void checkClipBatch(const QString &directory)
{
	QString error;
	const auto mesh = exportArmFixture();
	const auto folder = QDir(directory).filePath(QStringLiteral("anims"));
	QVector<ModelMd5ClipFile> files;
	expect(prepareModelMd5Clips(mesh, folder, ModelExportOptions{}, &files, &error) && files.size() == 2 &&
			   files[1].path.endsWith(QStringLiteral("anims/wave.md5anim")) && files[1].report.clip == 1,
		   "every clip is prepared under its own name");
	expect(writeModelMd5Clips(files, false, true, {}, &error) && !QFileInfo::exists(files[0].path), "a dry run writes nothing");
	expect(writeModelMd5Clips(files, false, false, {}, &error) && QFileInfo::exists(files[0].path) && QFileInfo::exists(files[1].path),
		   "the batch writes every clip");
	for (const auto &file : std::as_const(files))
	{
		QFile read(file.path);
		const auto decoded = read.open(QIODevice::ReadOnly) ? decodeModelMesh(file.path, read.readAll()) : ModelMesh{};
		expect(decoded.error.isEmpty() && decoded.skeleton.clips.size() == 1 &&
				   decoded.skeleton.clips[0].frames.size() == mesh.skeleton.clips[file.clip].frames.size(),
			   "each written md5anim decodes with its clip's frames");
	}
	QFile first(files[0].path);
	expect(first.open(QIODevice::WriteOnly | QIODevice::Truncate) && first.write("keep") == 4, "replace one clip file by hand");
	first.close();
	QFile::remove(files[1].path);
	error.clear();
	expect(!writeModelMd5Clips(files, false, false, {}, &error) && error.contains(QStringLiteral("already exists")) && !QFileInfo::exists(files[1].path),
		   "an existing file refuses the whole batch before anything is written");
	auto clash = mesh;
	clash.skeleton.clips[1].name = QStringLiteral("IDLE");
	expect(!prepareModelMd5Clips(clash, folder, ModelExportOptions{}, &files, &error) && error.contains(QStringLiteral("idle"), Qt::CaseInsensitive),
		   "clips that share a file name are refused");
}

void checkCli(const QString &vibestudio, const QString &directory)
{
	QString error;
	ModelDocument document;
	const auto arm = QDir(directory).filePath(QStringLiteral("cli-arm.mesh.json"));
	const auto six = QDir(directory).filePath(QStringLiteral("cli-six.mesh.json"));
	auto fractional = exportArmFixture();
	fractional.skeleton.clips[1].framesPerSecond = 12.5;
	const auto rates = QDir(directory).filePath(QStringLiteral("cli-rates.mesh.json"));
	expect(document.setMesh(exportArmFixture(), &error) && document.save(arm, false, &error) &&
			   document.setMesh(influenceFixture({0.3f, 0.25f, 0.2f, 0.1f, 0.1f, 0.05f}), &error) && document.save(six, false, &error) &&
			   document.setMesh(fractional, &error) && document.save(rates, false, &error),
		   "save CLI sources");
	QJsonObject result;
	const auto cli = [&](QStringList arguments, int expected)
	{
		QProcess process;
		process.setWorkingDirectory(directory);
		arguments = QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), QDir(directory).filePath(QStringLiteral("settings.ini"))} +
					arguments + QStringList{QStringLiteral("--json")};
		process.start(vibestudio, arguments);
		const bool done = process.waitForStarted(10000) && process.waitForFinished(60000);
		const auto output = process.readAllStandardOutput();
		result = QJsonDocument::fromJson(output).object();
		if (!done || process.exitCode() != expected)
		{
			std::cerr << output.constData() << process.readAllStandardError().constData();
		}
		return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected;
	};
	const auto out = [&](const QString &name) { return QDir(directory).filePath(name); };
	expect(cli({"model", "build", arm, "--output", out("a.md5anim"), "--clip", "5"}, 2) && !QFileInfo::exists(out("a.md5anim")),
		   "CLI refuses a clip the model does not have");
	expect(cli({"model", "build", arm, "--output", out("a.md5anim")}, 2) && result.value("message").toString().contains("--clip"),
		   "CLI asks which clip of a two-clip model to write");
	expect(cli({"model", "build", arm, "--output", out("wave.md5anim"), "--clip", "wave"}, 0) &&
			   result.value("clip").toObject().value("index").toInt() == 1 && result.value("clip").toObject().value("frames").toInt() == 4 &&
			   result.value("clip").toObject().value("frameRate").toInt() == 30 && QFileInfo::exists(out("wave.md5anim")),
		   "CLI writes a clip chosen by name and reports it");
	expect(cli({"model", "build", arm, "--output", out("frame.md5anim"), "--frame", "5"}, 0) &&
			   result.value("clip").toObject().value("name").toString() == "wave" &&
			   result.value("exportNotes").toArray().first().toString().contains("--frame 5"),
		   "CLI --frame selects the clip that frame belongs to, as the editor does");
	expect(cli({"model", "build", arm, "--output", out("x.iqm"), "--clip", "0"}, 2), "CLI keeps --clip to md5anim output");
	expect(cli({"model", "build", rates, "--output", out("r.md5anim"), "--clip", "1"}, 4) && result.value("message").toString().contains("12.5"),
		   "CLI refuses a fractional clip rate");
	expect(cli({"model", "build", rates, "--output", out("r.md5anim"), "--clip", "1", "--md5-frame-rate", "12"}, 0) &&
			   result.value("clip").toObject().value("frameRate").toInt() == 12,
		   "CLI writes an explicit MD5 frame rate");
	expect(cli({"model", "build", rates, "--output", out("r2.md5anim"), "--clip", "1", "--md5-frame-rate", "0"}, 2), "CLI rejects a zero MD5 rate");
	const auto anims = out("cli-anims");
	expect(cli({"model", "build", arm, "--all-clips", "--output-dir", anims, "--dry-run"}, 0) && result.value("clips").toArray().size() == 2 &&
			   !QFileInfo::exists(QDir(anims).filePath("idle.md5anim")),
		   "CLI dry-runs every clip without writing");
	expect(cli({"model", "build", arm, "--all-clips", "--output-dir", anims}, 0) && QFileInfo::exists(QDir(anims).filePath("idle.md5anim")) &&
			   QFileInfo::exists(QDir(anims).filePath("wave.md5anim")) &&
			   result.value("clips").toArray()[1].toObject().value("frames").toInt() == 4,
		   "CLI writes every clip as its own md5anim");
	expect(cli({"model", "build", arm, "--all-clips", "--output-dir", anims}, 1), "CLI refuses to replace clip files without --overwrite");
	expect(cli({"model", "build", arm, "--all-clips", "--output-dir", anims, "--overwrite"}, 0), "CLI replaces clip files with --overwrite");
	expect(cli({"model", "build", arm, "--all-clips", "--output", out("x.md5anim")}, 2), "CLI keeps --all-clips to --output-dir");
	expect(cli({"model", "build", six, "--output", out("six.iqm")}, 0) && result.value("influencesReduced").toInt() == 1 &&
			   !result.value("exportOmissions").toArray().isEmpty() && result.value("strict").toBool() == false,
		   "CLI JSON reports the IQM influence reduction as an omission");
	expect(cli({"model", "build", six, "--output", out("strict.iqm"), "--strict"}, 4) && !QFileInfo::exists(out("strict.iqm")) &&
			   !result.value("exportOmissions").toArray().isEmpty(),
		   "CLI --strict refuses an export that leaves data out, writing nothing");
	expect(cli({"model", "build", arm, "--output", out("arm.md5mesh"), "--strict"}, 4) && result.value("message").toString().contains("tag_hand"),
		   "CLI --strict names the tags an md5mesh cannot hold");
	// Options before the source are read as options, not as the source path.
	expect(cli({"model", "build", "--all-clips", arm, "--output-dir", out("before-anims"), "--dry-run"}, 0) && result.value("clips").toArray().size() == 2,
		   "CLI reads --all-clips before the source");
	expect(cli({"model", "build", "--md5-frame-rate", "12", rates, "--output", out("before.md5anim"), "--clip", "1", "--dry-run"}, 0) &&
			   result.value("clip").toObject().value("frameRate").toInt() == 12,
		   "CLI reads --md5-frame-rate before the source");
	// A dry run describes each file rather than saying it was written.
	bool described = cli({"model", "build", arm, "--all-clips", "--output-dir", out("dry-anims"), "--dry-run"}, 0);
	for (const auto &clip : result.value("clips").toArray())
		described &= clip.toObject().value("exportNotes").toArray().first().toString().startsWith("Skeletal clip");
	expect(described, "CLI dry-run notes describe the clips");
	// Frames added with frame edits: a strict md5anim and a strict batch refuse,
	// and the batch reports every clip's omissions.
	const auto duplicated = withUnmappedDuplicateFrame(exportArmFixture(), 6, &error);
	const auto dup = QDir(directory).filePath(QStringLiteral("cli-dup.mesh.json"));
	expect(document.setMesh(duplicated, &error) && document.save(dup, false, &error), "save a source with a duplicated frame");
	expect(cli({"model", "build", dup, "--output", out("w.md5anim"), "--clip", "wave", "--strict"}, 4) && !QFileInfo::exists(out("w.md5anim")) &&
			   !result.value("exportOmissions").toArray().isEmpty(),
		   "CLI --strict refuses an md5anim whose clip frames cannot be checked");
	expect(cli({"model", "build", dup, "--all-clips", "--output-dir", out("dup-anims"), "--strict", "--dry-run"}, 4) &&
			   result.value("clips").toArray().size() == 2 && !result.value("clips").toArray()[0].toObject().value("exportOmissions").toArray().isEmpty() &&
			   result.value("clips").toArray()[1].toObject().value("refused").toBool() && !result.value("exportOmissions").toArray().isEmpty() &&
			   !result.value("written").toBool() && !result.value("sharedNotes").toArray().isEmpty(),
		   "CLI --all-clips --strict refuses with every clip's omissions");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	checkRigidGolden(temporary.path());
	checkJointScale();
	checkFrameRates();
	checkClipSelection();
	checkVertexFormatClips();
	checkIqmReports();
	checkSourceOmissions();
	checkRoundTrips();
	checkClipBatch(temporary.path());
	if (app.arguments().size() > 1)
	{
		checkCli(app.arguments()[1], temporary.path());
	}
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "model skeletal export checks passed\n";
	return EXIT_SUCCESS;
}
