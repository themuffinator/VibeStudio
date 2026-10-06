#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_recovery.h"
#include "core/model_tags.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
bool near(ModelVec3 a, ModelVec3 b) { return std::abs(a.x - b.x) < 0.0001 && std::abs(a.y - b.y) < 0.0001 && std::abs(a.z - b.z) < 0.0001; }
QJsonObject json(const ModelMesh &mesh) { return editableModelJson(mesh); }
ModelMesh fixture()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	mesh.surfaces[0].frames << mesh.surfaces[0].frames[0];
	for (auto &point : mesh.surfaces[0].frames[1].positions)
	{
		point.z += 8;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	const auto original = fixture();
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "prepare animated tag source");
	document.setSelection({0, {}, {0}});
	ModelEdit edit;
	edit.kind = ModelEditKind::AddTag;
	edit.text = QStringLiteral("tag_weapon");
	edit.tagOrigin = {4, 5, 6};
	edit.rotation = {0, 0, 90};
	edit.selection = document.selection();
	ok &= expect(document.edit(edit, &error) && document.mesh().tags.size() == 2 && document.selection().tag == edit.text,
				 "creation spans every pose and selects the new attachment");
	const auto created = json(document.mesh());
	ok &= expect(created.value("surfaces") == json(original).value("surfaces") &&
					 near(modelTagPoint(document.mesh().tags[0], {2, 3, 4}), {1, 7, 10}),
				 "creation preserves geometry and uses MD3 basis vectors");
	ok &= expect(document.undo() && document.selection().faces == QSet<int>{0} && document.selection().tag.isEmpty() &&
					 json(document.mesh()) == json(original) && document.redo() && json(document.mesh()) == created,
				 "tag history restores prior component selection and complete poses");
	edit = {};
	edit.kind = ModelEditKind::SetTagOrigin;
	edit.selection = document.selection();
	edit.frame = 1;
	edit.tagOrigin = {4, 5, 10};
	ok &= expect(document.edit(edit, &error) && near(findModelTag(document.mesh(), "tag_weapon", 0)->origin, {4, 5, 6}) &&
					 near(findModelTag(document.mesh(), "tag_weapon", 1)->origin, {4, 5, 10}),
				 "absolute origin changes only the chosen pose");
	const auto beforeTransform = document.mesh();
	edit = {};
	edit.kind = ModelEditKind::TransformTag;
	edit.selection = document.selection();
	edit.translation = {1.26f, 0, 0};
	edit.translationGrid = 1;
	edit.rotation = {179, 0, 0};
	edit.rotationGrid = 15;
	edit.pivotMode = ModelTransformPivot::SelectionCentre;
	edit.pivotFrame = 1;
	ok &= expect(document.edit(edit, &error) && near(findModelTag(document.mesh(), "tag_weapon", 0)->origin, {5, 5, 14}) &&
					 near(findModelTag(document.mesh(), "tag_weapon", 1)->origin, {5, 5, 10}) &&
					 near(modelTagPoint(*findModelTag(document.mesh(), "tag_weapon", 1), {2, 0, 0}), {5, 3, 10}),
				 "all-pose movement/rotation use shared snapping and one fixed reference pivot");
	const auto transformed = document.mesh();
	for (const auto bad : {ModelEditKind::Transform, ModelEditKind::RecalculateNormals, ModelEditKind::TransformUv})
	{
		auto candidate = transformed;
		ModelEdit invalid;
		invalid.kind = bad;
		invalid.selection = document.selection();
		ok &= expect(!applyModelEdit(&candidate, invalid, nullptr, &error) && json(candidate) == json(transformed),
					 "tag selection cannot accidentally edit mesh or UV data");
	}
	for (int failure = 0; failure < 5; ++failure)
	{
		auto candidate = beforeTransform;
		auto invalid = edit;
		if (failure == 0)
		{
			invalid.scale = {2, 2, 2};
		}
		if (failure == 1)
		{
			invalid.pivotFrame = 99;
		}
		if (failure == 2)
		{
			invalid.selection.tag = "missing";
		}
		if (failure == 3)
		{
			invalid.selection.faces = {0};
		}
		if (failure == 4)
		{
			invalid.rotationGrid = 181;
		}
		ModelSelection unchanged{0, {}, {1}};
		ok &= expect(!applyModelEdit(&candidate, invalid, &unchanged, &error) && json(candidate) == json(beforeTransform) &&
						 unchanged.faces == QSet<int>{1} && !error.isEmpty(),
					 "invalid tag edits preserve both outputs atomically");
	}
	edit = {};
	edit.kind = ModelEditKind::CopyTagPose;
	edit.selection = document.selection();
	edit.sourceFrame = 1;
	ok &= expect(document.edit(edit, &error) && near(findModelTag(document.mesh(), "tag_weapon", 0)->origin, {5, 5, 10}),
				 "pose copying uses a retained source while updating every destination");
	edit = {};
	edit.kind = ModelEditKind::DuplicateTag;
	edit.selection = document.selection();
	edit.text = "tag_copy";
	ok &= expect(document.edit(edit, &error) && document.mesh().tagCount == 2 && document.selection().tag == "tag_copy",
				 "duplicate retains every source pose under a new identity");
	edit.kind = ModelEditKind::RenameTag;
	edit.selection = document.selection();
	edit.text = "tag_head";
	ok &= expect(document.edit(edit, &error) && findModelTag(document.mesh(), "tag_head", 1) &&
					 !findModelTag(document.mesh(), "tag_copy", 0) && document.selection().tag == "tag_head",
				 "rename spans poses and follows selection");
	const auto named = document.mesh();
	for (const auto &name : QStringList{"tag_weapon", "", " padded ", QString(64, 'x'), QString::fromUtf8("tag_é")})
	{
		auto invalid = edit;
		invalid.selection = document.selection();
		invalid.text = name;
		auto candidate = named;
		ok &= expect(!applyModelEdit(&candidate, invalid, nullptr, &error) && json(candidate) == json(named),
					 "names reject collisions and MD3-incompatible identities");
	}
	edit = {};
	edit.kind = ModelEditKind::DeleteTag;
	edit.selection = document.selection();
	ok &= expect(document.edit(edit, &error) && document.mesh().tagCount == 1 && document.selection().tag.isEmpty() && document.undo() &&
					 document.selection().tag == "tag_head",
				 "delete and undo retain the attachment selection contract");
	edit = {};
	edit.kind = ModelEditKind::DuplicateFrame;
	edit.selection = document.selection();
	edit.frame = 1;
	ok &= expect(document.edit(edit, &error) && document.mesh().frameCount == 3 && document.mesh().tags.size() == 6 &&
					 findModelTag(document.mesh(), "tag_head", 2),
				 "frame duplication retains all authored tags");
	edit.kind = ModelEditKind::DeleteFrame;
	ok &= expect(document.edit(edit, &error) && document.mesh().frameCount == 2 && validateEditableModel(document.mesh()).isEmpty(),
				 "frame deletion reindexes tag poses without changing their identities");
	auto reflected = beforeTransform;
	reflected.tags[0].axis[8] = -1;
	edit = {};
	edit.kind = ModelEditKind::TransformTag;
	edit.selection.tag = "tag_weapon";
	edit.frame = 0;
	edit.rotation = {0, 0, 90};
	ok &= expect(applyModelEdit(&reflected, edit, nullptr, &error) && reflected.tags[0].axis[8] == -1,
				 "rigid editing preserves imported basis handedness");
	edit.kind = ModelEditKind::ResetTagOrientation;
	edit.rotation = {};
	ok &= expect(applyModelEdit(&reflected, edit, nullptr, &error) &&
					 near(modelTagPoint(reflected.tags[0], {1, 2, 3}),
						  {reflected.tags[0].origin.x + 1, reflected.tags[0].origin.y + 2, reflected.tags[0].origin.z + 3}),
				 "orientation reset is an explicit pose edit");
	auto bounded = original;
	for (int index = 0; index < 16; ++index)
	{
		edit = {};
		edit.kind = ModelEditKind::AddTag;
		edit.text = QStringLiteral("tag_%1").arg(index);
		ok &= expect(applyModelEdit(&bounded, edit, nullptr, &error), "create tags through the supported per-frame limit");
	}
	const auto atLimit = json(bounded);
	edit.text = "tag_overflow";
	ok &= expect(!applyModelEdit(&bounded, edit, nullptr, &error) && json(bounded) == atLimit, "tag limit does not partially append poses");
	edit = {};
	edit.kind = ModelEditKind::AddTag;
	edit.text = "tag_cancelled";
	auto cancelled = original;
	int polls = 0;
	ok &= expect(!applyModelEdit(&cancelled, edit, nullptr, &error, {[&] { return ++polls > 5; }, {}}) && json(cancelled) == json(original),
				 "cancellation preserves the original tag table and geometry");
	const auto md3 = exportEditableModel(document.mesh(), "md3", 0, &error);
	ModelMesh roundTrip;
	bool retainsTags = !md3.isEmpty() && importEditableModel("authored.md3", md3, &roundTrip, &error) &&
					   roundTrip.tags.size() == document.mesh().tags.size();
	for (const auto &tag : document.mesh().tags)
	{
		const auto native = findModelTag(roundTrip, tag.name, tag.frameIndex);
		retainsTags &= native && near(native->origin, tag.origin);
		if (native)
		{
			for (int axis = 0; axis < 9; ++axis)
			{
				retainsTags &= native->axis[axis] == tag.axis[axis];
			}
		}
	}
	ok &= expect(retainsTags, "MD3 retains every authored name, origin, axis and pose");
	ok &= expect(exportEditableModel(document.mesh(), "md2", 0, &error).isEmpty() && !error.isEmpty(),
				 "MD2 refuses to discard authored attachments");
	ModelExportReport report;
	const auto obj = exportEditableModel(document.mesh(), "obj", 0, &error, {}, &report);
	ok &= expect(obj.startsWith("# Attachment tags are omitted") && !report.notes.isEmpty(),
				 "OBJ frame export explicitly reports its attachment loss");
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-tags-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = document.mesh();
	snapshot.selection = document.selection();
	snapshot.frame = 1;
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && restored.selection.tag == "tag_head" &&
					 json(restored.mesh) == json(document.mesh()),
				 "recovery retains active attachment and complete poses");
	ModelDocument recovered;
	ok &= expect(recovered.restoreDraft(restored.mesh, restored.selection, &error) && recovered.selection().tag == "tag_head" &&
					 recovered.isModified(),
				 "tag recovery restores an unsaved document with its attachment selected");
	restored.selection.tag = "missing";
	ok &= expect(!recovered.restoreDraft(restored.mesh, restored.selection, &error), "recovery refuses stale attachment identity");
	const auto path = QDir(temporary.path()).filePath("tagged.mesh.json");
	ok &= expect(document.save(path, false, &error) && recovered.load(path, &error) && json(recovered.mesh()) == json(document.mesh()),
				 "editable sources reopen without changing tags");
	if (argc > 1)
	{
		const auto cli = [&](QStringList args, int expected)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			args.prepend("--cli");
			args << "--settings-file" << QDir(temporary.path()).filePath("cli-settings.ini") << "--json";
			process.start(app.arguments()[1], args);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			if (!finished || process.exitCode() != expected)
			{
				std::cerr << bytes.constData() << process.readAllStandardError().constData();
			}
			ok &= expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "tag CLI exit status");
			return QJsonDocument::fromJson(bytes).object();
		};
		const auto inspection = cli({"model", "tags", path, "--frame", "1"}, 0);
		ok &= expect(inspection.value("tags").toArray().size() == 2, "CLI reports named poses with orientation data");
		const auto output = QDir(temporary.path()).filePath("changed.mesh.json");
		const QStringList command{
			"model",	 "edit",		path,		   "--operation", "transform-tag", "--tag",	 "tag_head",	 "--frame", "1",
			"--offset",	 "0.49,0.51,0", "--snap-grid", "1",			  "--rotate",	   "0,0,89", "--snap-angle", "15",		"--pivot-mode",
			"selection", "--output",	output};
		cli(command + QStringList{"--dry-run"}, 0);
		ok &= expect(!QFileInfo::exists(output), "tag dry-run does not publish a source");
		cli(command, 0);
		ok &= expect(recovered.load(output, &error) && near(findModelTag(recovered.mesh(), "tag_head", 1)->origin, {5, 6, 10}) &&
						 near(findModelTag(recovered.mesh(), "tag_head", 0)->origin, {5, 5, 10}),
					 "CLI uses the shared snapped frame-local tag transform");
		cli({"model", "edit", path, "--operation", "transform-tag", "--tag", "tag_head", "--scale", "2,2,2", "--output", output}, 2);
		cli({"model", "edit", path, "--operation", "rename-tag", "--tag", "tag_head", "--name", "tag_renamed", "--frame", "0", "--output",
			 output},
			2);
		cli({"model", "edit", path, "--operation", "copy-tag-pose", "--tag", "tag_head", "--source-frame", "99", "--output", output}, 2);
		const auto authored = QDir(temporary.path()).filePath("cli-authored.mesh.json");
		cli({"model", "edit", path, "--operation", "add-tag", "--name", "tag_added", "--tag-origin", "7,8,9", "--rotate", "0,90,0",
			 "--output", authored},
			0);
		ok &= expect(recovered.load(authored, &error) && recovered.mesh().tagCount == 3 &&
						 near(modelTagPoint(*findModelTag(recovered.mesh(), "tag_added", 1), {1, 0, 0}), {7, 8, 8}),
					 "CLI creates complete oriented tag poses");
		const auto editTag = [&](const QStringList &arguments)
		{ return cli(QStringList{"model", "edit", authored} + arguments + QStringList{"--output", authored, "--overwrite"}, 0); };
		editTag({"--operation", "duplicate-tag", "--tag", "tag_added", "--name", "tag_copy"});
		editTag({"--operation", "rename-tag", "--tag", "tag_copy", "--name", "tag_final"});
		editTag({"--operation", "set-tag-origin", "--tag", "tag_final", "--tag-origin", "10,20,30", "--frame", "1"});
		ok &= expect(recovered.load(authored, &error) && near(findModelTag(recovered.mesh(), "tag_final", 1)->origin, {10, 20, 30}) &&
						 near(findModelTag(recovered.mesh(), "tag_final", 0)->origin, {7, 8, 9}),
					 "CLI identity and origin operations preserve unselected poses");
		editTag({"--operation", "copy-tag-pose", "--tag", "tag_final", "--source-frame", "1"});
		editTag({"--operation", "reset-tag-orientation", "--tag", "tag_final"});
		ok &= expect(recovered.load(authored, &error) &&
						 near(modelTagPoint(*findModelTag(recovered.mesh(), "tag_final", 0), {1, 2, 3}), {11, 22, 33}),
					 "CLI copies the retained source pose and explicitly resets orientation");
		const auto filtered = cli({"model", "tags", authored, "--tag", "tag_final"}, 0).value("tags").toArray();
		ok &= expect(filtered.size() == 2 && filtered[0].toObject().value("axis").toArray().size() == 9,
					 "tag inspection filters identity across every pose");
		editTag({"--operation", "delete-tag", "--tag", "tag_final"});
		ok &= expect(recovered.load(authored, &error) && recovered.mesh().tagCount == 3 && !findModelTag(recovered.mesh(), "tag_final", 0),
					 "CLI deletion removes the complete identity");
		cli({"model", "tags", authored, "--tag", "missing"}, 2);
		cli({"model", "tags", authored, "--frame", "-1"}, 2);
		cli({"model", "edit", authored, "--operation", "transform-tag", "--tag", "tag_added", "--faces", "all", "--output", output}, 2);
		cli({"model", "edit", authored, "--operation", "set-tag-origin", "--tag", "tag_added", "--output", output}, 2);
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
