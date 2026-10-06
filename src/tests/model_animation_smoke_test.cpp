#include "core/model_animation.h"
#include "core/model_recovery.h"
#include "core/model_tags.h"
#include "tests/model_animation_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

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
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool near(float a, float b, float tolerance = .00001f) { return std::abs(a - b) <= tolerance; }
bool same(ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool samePose(const ModelMesh &a, int af, const ModelMesh &b, int bf)
{
	if (!same(a.frames[af].origin, b.frames[bf].origin) || a.surfaces.size() != b.surfaces.size())
	{
		return false;
	}
	for (int s = 0; s < a.surfaces.size(); ++s)
	{
		const auto &first = a.surfaces[s].frames[af], &second = b.surfaces[s].frames[bf];
		if (first.positions.size() != second.positions.size() || first.normals.size() != second.normals.size())
		{
			return false;
		}
		for (int v = 0; v < first.positions.size(); ++v)
		{
			if (!same(first.positions[v], second.positions[v]) || !same(first.normals[v], second.normals[v]))
			{
				return false;
			}
		}
	}
	for (const auto &tag : a.tags)
	{
		if (tag.frameIndex != af)
		{
			continue;
		}
		const auto other = findModelTag(b, tag.name, bf);
		if (!other || !same(tag.origin, other->origin) || !std::equal(std::begin(tag.axis), std::end(tag.axis), std::begin(other->axis)))
		{
			return false;
		}
	}
	return true;
}
bool sameTopology(const ModelMesh &a, const ModelMesh &b)
{
	auto left = editableModelJson(a).value("surfaces").toArray(), right = editableModelJson(b).value("surfaces").toArray();
	if (left.size() != right.size())
	{
		return false;
	}
	for (int i = 0; i < left.size(); ++i)
	{
		auto first = left[i].toObject(), second = right[i].toObject();
		first.remove("frames");
		second.remove("frames");
		if (first != second)
		{
			return false;
		}
	}
	return true;
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-animation-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	const auto original = tests::animationFixture();
	ModelDocument document;
	if (!expect(document.setMesh(original, &error), "open multisurface animated fixture"))
	{
		std::cerr << error.toStdString();
		return EXIT_FAILURE;
	}
	ModelSelection selection{1, {1}, {0}, {{0, 1}}};
	document.setSelection(selection);
	ModelEdit insert;
	insert.kind = ModelEditKind::InsertInbetweens;
	insert.frame = 0;
	insert.inbetweenCount = 3;
	insert.text = "transition";
	insert.selection = selection;
	if (!expect(document.edit(insert, &error), "insert poses across every surface and tag"))
	{
		std::cerr << error.toStdString();
		return EXIT_FAILURE;
	}
	const auto generated = document.mesh();
	ok &= expect(generated.frames.size() == 6 && generated.tags.size() == 12 && validateEditableModel(generated).isEmpty(),
				 "generated source validates");
	for (int old = 0; old < 3; ++old)
	{
		const int shifted = old == 0 ? 0 : old + 3;
		ok &= expect(samePose(original, old, generated, shifted) && original.frames[old].name == generated.frames[shifted].name,
					 "original positions, normals, origins, tags and frame names stay exact");
	}
	ok &= expect(sameTopology(original, generated) && document.selection().surface == 1 &&
					 document.selection().vertices == selection.vertices && document.selection().edges == selection.edges &&
					 document.selection().faces == selection.faces,
				 "whole-pose edit retains topology and mixed selection");
	for (int n = 1; n <= 3; ++n)
	{
		const float t = float(n) / 4;
		ok &= expect(generated.frames[n].name == QString("transition_%1").arg(n, 3, 10, QLatin1Char('0')) &&
						 near(generated.frames[n].origin.x, 4 * t),
					 "generated naming and frame origins");
		for (int s = 0; s < 2; ++s)
		{
			for (int v = 0; v < 4; ++v)
			{
				const auto p = generated.surfaces[s].frames[n].positions[v], normal = generated.surfaces[s].frames[n].normals[v];
				const float norm = std::sqrt(t * t + (1 - t) * (1 - t));
				ok &= expect(near(p.z, 8 * t) && near(p.x, original.surfaces[s].frames[0].positions[v].x) && near(normal.x, 0) &&
								 near(normal.y, t / norm) && near(normal.z, (1 - t) / norm),
							 "linear positions and normalized endpoint-normal blend");
			}
		}
		for (const auto &name : {QString("tag_mount"), QString("tag_reflected")})
		{
			const auto tag = findModelTag(generated, name, n);
			const float angle = t * float(std::acos(-1.) / 2);
			ok &= expect(tag && near(tag->origin.z, 3 + 8 * t) && near(tag->axis[0], std::cos(angle)) &&
							 near(tag->axis[1], std::sin(angle)) && near(tag->axis[3], -std::sin(angle)) &&
							 near(tag->axis[4], std::cos(angle)) && tag->axis[8] == (name == "tag_reflected" ? -1.f : 1.f),
						 "rigid attachment slerp uses local basis and preserves shared reflection");
		}
	}
	const auto clips = generated.animations;
	ok &= expect(clips[0].firstFrame == 0 && clips[0].frameCount == 5 && clips[1].firstFrame == 0 && clips[1].frameCount == 1 &&
					 clips[2].firstFrame == 4 && clips[2].frameCount == 1 && clips[3].firstFrame == 5 && clips[3].frameCount == 1 &&
					 clips[4].firstFrame == 4 && clips[4].frameCount == 2,
				 "only clips containing both endpoints grow; later clips shift");
	ok &= expect(document.undo() && bytes(document.mesh()) == bytes(original) && !document.isModified() && document.redo() &&
					 bytes(document.mesh()) == bytes(generated),
				 "in-between edit is one exact undo and redo step");
	ModelMesh parsed;
	ok &= expect(parseEditableModel(bytes(generated), &parsed, &error) && bytes(parsed) == bytes(generated),
				 "source round trip retains clips and poses");
	ModelRecoverySnapshot snapshot{generated, selection, 2, "Animation draft", {}, {}};
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), "b8ad1212-6a51-4a20-8b68-1ba0e7590001", &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && bytes(restored.mesh) == bytes(generated) &&
					 restored.frame == 2 && restored.selection.surface == 1,
				 "recovery retains authored clips, generated poses and selected frame");
	ModelExportReport report;
	const auto md3 = exportEditableModel(generated, "md3", 0, &error, {}, &report);
	ok &= expect(!md3.isEmpty() && report.notes.join(' ').contains("animation clip") &&
					 importEditableModel("generated.md3", md3, &parsed, &error) && parsed.frameCount == 6 && parsed.surfaceCount == 2 &&
					 parsed.tags.size() == 12,
				 "MD3 preserves every generated pose/tag and reports custom clip omission");
	if (parsed.frameCount == 6 && parsed.surfaces.size() == 2)
	{
		for (int f = 0; f < 6; ++f)
		{
			ok &= expect(near(parsed.surfaces[1].frames[f].positions[0].z, generated.surfaces[1].frames[f].positions[0].z, 1.f / 64) &&
							 near(findModelTag(parsed, "tag_mount", f)->axis[1], findModelTag(generated, "tag_mount", f)->axis[1]),
						 "native frame and tag data survives reload");
		}
	}
	ok &= expect(!exportEditableModel(generated, "obj", 2, &error, {}, &report).isEmpty() &&
					 report.notes.join(' ').contains("selected frame"),
				 "OBJ reports single-pose clip omission");

	auto untagged = generated;
	untagged.tags.clear();
	untagged.surfaces.resize(1);
	untagged.surfaces[0].skinPaths = {QStringLiteral("models/test/body.pcx")};
	updateEditableModelMetadata(&untagged);
	const auto md2 = exportEditableModel(untagged, "md2", 0, &error, {}, &report);
	ok &= expect(!md2.isEmpty() && report.notes.join(' ').contains("custom animation ranges") &&
					 importEditableModel("generated.md2", md2, &parsed, &error) && parsed.frameCount == 6,
				 "untagged generated poses export through MD2 with clip-omission diagnostics");

	ModelEdit copy;
	copy.kind = ModelEditKind::CopyFramePose;
	copy.frame = 1;
	copy.sourceFrame = 5;
	copy.selection = {0, {}, {}, {}, "tag_mount"};
	ok &= expect(document.edit(copy, &error) && samePose(generated, 5, document.mesh(), 1) &&
					 document.mesh().frames[1].name == "transition_001" &&
					 editableModelJson(document.mesh())["animations"] == editableModelJson(generated)["animations"] &&
					 sameTopology(generated, document.mesh()) && document.selection().tag == "tag_mount",
				 "copy replaces full destination pose while retaining name, clips, topology and selected tag");
	for (int f : {0, 2, 3, 4, 5})
	{
		ok &= expect(samePose(generated, f, document.mesh(), f), "pose copy leaves every other pose exact");
	}
	ok &= expect(document.undo() && bytes(document.mesh()) == bytes(generated), "pose copy undo restores original destination");

	ModelEdit metadata;
	metadata.kind = ModelEditKind::AddAnimation;
	metadata.text = "authored";
	metadata.rangeFirst = 1;
	metadata.rangeLast = 3;
	ok &= expect(document.edit(metadata, &error) && document.mesh().animations.size() == 6, "create overlapping named range");
	metadata.animationIndex = 5;
	metadata.kind = ModelEditKind::RenameAnimation;
	metadata.text = QString::fromUtf8("Départ");
	ok &= expect(document.edit(metadata, &error) && document.mesh().animations[5].name == metadata.text,
				 "clip names support Unicode in editable source");
	metadata.kind = ModelEditKind::SetAnimationRange;
	metadata.rangeFirst = 2;
	metadata.rangeLast = 2;
	ok &= expect(document.edit(metadata, &error) && document.mesh().animations[5].firstFrame == 2 &&
					 document.mesh().animations[5].frameCount == 1,
				 "inclusive clip range supports one pose");
	metadata.kind = ModelEditKind::DeleteAnimation;
	ok &= expect(document.edit(metadata, &error) && bytes(document.mesh()) == bytes(generated),
				 "deleting a clip leaves every pose and other clip unchanged");

	const auto reject = [&](const ModelMesh &source, ModelEdit operation, const char *message)
	{
		ModelDocument trial;
		if (!trial.setMesh(source, &error))
		{
			std::cerr << "Bad refusal fixture: " << error.toStdString() << '\n';
			return false;
		}
		trial.setSelection(selection);
		const auto fingerprint = trial.revisionFingerprint();
		return expect(!trial.edit(operation, &error) && !error.isEmpty() && trial.revisionFingerprint() == fingerprint &&
						  !trial.canUndo() && !trial.isModified() && trial.selection().vertices == selection.vertices &&
						  trial.selection().edges == selection.edges,
					  message);
	};
	for (const auto &name : {QString(), QString(" padded "), QString(129, 'x'), QString("line\nfeed"), QString("span")})
	{
		metadata.kind = ModelEditKind::AddAnimation;
		metadata.text = name;
		metadata.rangeFirst = 0;
		metadata.rangeLast = 1;
		ok &= reject(original, metadata, "invalid or duplicate clip name cannot change history or source");
	}
	metadata.text = "new_clip";
	metadata.rangeFirst = 2;
	metadata.rangeLast = 1;
	ok &= reject(original, metadata, "reversed clip bounds are rejected");
	metadata.rangeFirst = 0;
	metadata.rangeLast = 3;
	ok &= reject(original, metadata, "clip cannot run beyond available frames");
	metadata.kind = ModelEditKind::DeleteAnimation;
	metadata.animationIndex = 5;
	ok &= reject(original, metadata, "clip deletion rejects a missing index");
	metadata.animationIndex = 0;
	metadata.frame = 0;
	ok &= reject(original, metadata, "clip metadata rejects frame-local scope");
	copy.frame = 1;
	copy.sourceFrame = 1;
	ok &= reject(original, copy, "self-copy is rejected");
	copy.sourceFrame = 3;
	ok &= reject(original, copy, "missing copy source is rejected");
	for (int count : {0, -1, 1022, 1023})
	{
		auto invalid = insert;
		invalid.inbetweenCount = count;
		ok &= reject(original, invalid, "invalid insertion count or total frame limit is atomic");
	}
	auto invalid = insert;
	invalid.frame = 2;
	ok &= reject(original, invalid, "last pose has no next endpoint");
	invalid = insert;
	invalid.text = QString(124, 'x');
	ok &= reject(original, invalid, "generated names enforce prefix length");
	auto opposite = original;
	opposite.surfaces[1].frames[1].normals.fill({0, 0, -1}, 4);
	ok &= reject(opposite, insert, "cancelling normals reject whole edit after other surfaces were generated");
	auto reflected = original;
	reflected.tags[2].axis[8] = -1;
	ok &= reject(reflected, insert, "different endpoint handedness cannot interpolate a rigid attachment");
	auto wrappedRotation = original;
	for (auto &tag : wrappedRotation.tags)
	{
		const double angle = (tag.frameIndex == 0 ? 170. : -170.) * std::acos(-1.) / 180.;
		tag.axis[0] = tag.axis[4] = float(std::cos(angle));
		tag.axis[1] = float(std::sin(angle));
		tag.axis[3] = -tag.axis[1];
	}
	ok &= expect(applyModelEdit(&wrappedRotation, insert, nullptr, &error), "interpolate rotations spanning the angle wrap");
	const auto halfway = findModelTag(wrappedRotation, "tag_mount", 2);
	ok &= expect(halfway && near(halfway->axis[0], -1) && near(halfway->axis[1], 0),
				 "orientation crosses 180 degrees along the shortest arc");
	auto collapsed = original;
	for (auto &p : collapsed.surfaces[1].frames[1].positions)
	{
		p.x = 40 - p.x;
		p.y = -p.y;
	}
	updateEditableModelMetadata(&collapsed);
	ok &= reject(collapsed, insert, "collapsed generated triangle rejects all poses atomically");
	auto duplicates = original;
	duplicates.animations[1].name = duplicates.animations[0].name;
	metadata = {};
	metadata.kind = ModelEditKind::RenameAnimation;
	metadata.animationIndex = 1;
	metadata.text = "distinct";
	ok &= expect(applyModelEdit(&duplicates, metadata, nullptr, &error) && duplicates.animations[0].name == "span" &&
					 duplicates.animations[1].name == "distinct",
				 "imported duplicate names can be addressed by clip index");
	auto bounded = original;
	bounded.animations.clear();
	for (int i = 0; i < 1024; ++i)
	{
		bounded.animations << ModelAnimation{QString("clip_%1").arg(i), 0, 1};
	}
	metadata.kind = ModelEditKind::AddAnimation;
	metadata.text = "overflow";
	ok &= reject(bounded, metadata, "clip count limit rejects allocation without changing source");
	bounded.animations << ModelAnimation{"excess", 0, 1};
	ok &= expect(!validateEditableModel(bounded).isEmpty(), "source validator enforces clip count bound");
	auto oversizedSource = editableModelJson(original);
	QJsonArray excessiveClips;
	for (const auto &clip : bounded.animations)
	{
		excessiveClips << QJsonObject{{"name", clip.name}, {"first", 0}, {"count", 1}};
	}
	oversizedSource["animations"] = excessiveClips;
	parsed = original;
	ok &= expect(!parseEditableModel(QJsonDocument(oversizedSource).toJson(), &parsed, &error) && bytes(parsed) == bytes(original) &&
					 error.contains("1024 animation clips"),
				 "source parser rejects excess clips before building geometry and preserves output");
	auto maximumFrames = original;
	invalid = insert;
	invalid.inbetweenCount = 1021;
	ok &= expect(applyModelEdit(&maximumFrames, invalid, nullptr, &error) && maximumFrames.frameCount == 1024 &&
					 samePose(original, 1, maximumFrames, 1022) && samePose(original, 2, maximumFrames, 1023) &&
					 maximumFrames.frames[1021].name == "transition_1021",
				 "maximum allowed frame count succeeds and retains original endpoints");
	// A maximum-vertex surface permits only sixteen poses under the frame-storage cap.
	auto large = original;
	auto grid = tests::uvGrid(255);
	ModelFrameGeometry gridFrame;
	for (int y = 0; y <= 255; ++y)
	{
		for (int x = 0; x <= 255; ++x)
		{
			gridFrame.positions << ModelVec3{float(x), float(y), 0};
		}
	}
	gridFrame.normals.fill({0, 0, 1}, grid.vertexCount);
	grid.frames = {gridFrame, gridFrame, gridFrame};
	large.surfaces = {grid};
	updateEditableModelMetadata(&large);
	invalid = insert;
	invalid.inbetweenCount = 14;
	invalid.selection = {};
	// This fixture has a single surface, so use direct atomic application with an output sentinel.
	const auto beforeLarge = bytes(large);
	ModelSelection unchanged{0, {3}, {}, {}};
	ok &= expect(!applyModelEdit(&large, invalid, &unchanged, &error) && bytes(large) == beforeLarge && unchanged.vertices == QSet<int>{3},
				 "frame-vertex storage budget is enforced before generated allocations");
	bool cancelledDuringEditing = false;
	ModelWorkControl control;
	control.progress = [&](ModelWorkPhase phase, qint64 completed, qint64)
	{
		if (phase == ModelWorkPhase::Editing && completed >= 256)
		{
			cancelledDuringEditing = true;
		}
	};
	control.cancelled = [&] { return cancelledDuringEditing; };
	invalid = insert;
	invalid.inbetweenCount = 100;
	const auto beforeCancel = document.revisionFingerprint();
	const auto history = document.historyBytes();
	ok &= expect(!document.edit(invalid, &error, control) && cancelledDuringEditing && document.revisionFingerprint() == beforeCancel &&
					 document.historyBytes() == history,
				 "mid-generation cancellation preserves document and history");

	if (app.arguments().size() > 1)
	{
		const auto inputPath = QDir(temporary.path()).filePath("source.mesh.json");
		ModelDocument cliExpected;
		ok &= expect(cliExpected.setMesh(original, &error) && cliExpected.save(inputPath, false, &error), "save CLI fixture");
		const auto cli = [&](QStringList args, int expected, QJsonObject *json = nullptr)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			args.prepend("--cli");
			args << "--json" << "--settings-file" << QDir(temporary.path()).filePath("settings.ini");
			process.start(app.arguments()[1], args);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto output = process.readAllStandardOutput();
			const auto result = QJsonDocument::fromJson(output);
			if (!finished || process.exitCode() != expected)
			{
				std::cerr << output.constData() << process.readAllStandardError().constData();
			}
			if (json)
			{
				*json = result.object();
			}
			return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && result.isObject();
		};
		QJsonObject listed;
		ok &= expect(cli({"model", "animations", inputPath}, 0, &listed) && listed["frameCount"].toInt() == 3 &&
						 listed["clips"].toArray().size() == 5 && listed["clips"].toArray()[0].toObject()["lastFrame"].toInt() == 1,
					 "CLI lists stable clip indices and inclusive bounds");
		ok &= expect(cli({"model", "animations", inputPath, "--clip", "2"}, 0, &listed) && listed["clips"].toArray().size() == 1 &&
						 listed["clips"].toArray()[0].toObject()["index"].toInt() == 2 &&
						 cli({"model", "animations", inputPath, "--clip", "5"}, 2),
					 "CLI filters and validates clip index");
		const auto dryOutput = QDir(temporary.path()).filePath("dry.mesh.json");
		ok &= expect(cli({"model", "edit", inputPath, "--operation", "insert-inbetweens", "--frame", "0", "--insert-count", "3", "--output",
						  dryOutput, "--dry-run"},
						 0) &&
						 !QFileInfo::exists(dryOutput),
					 "CLI in-between dry-run produces no source file");
		QString current = inputPath;
		for (int step = 0; step < 6; ++step)
		{
			ModelEdit operation;
			QStringList flags;
			if (step == 0)
			{
				operation.kind = ModelEditKind::AddAnimation;
				operation.text = "new_clip";
				operation.rangeLast = 1;
				flags = {"add-clip", "--name", "new_clip", "--first-frame", "0", "--last-frame", "1"};
			}
			if (step == 1)
			{
				operation.kind = ModelEditKind::RenameAnimation;
				operation.animationIndex = 5;
				operation.text = "renamed";
				flags = {"rename-clip", "--clip", "5", "--name", "renamed"};
			}
			if (step == 2)
			{
				operation.kind = ModelEditKind::SetAnimationRange;
				operation.animationIndex = 5;
				operation.rangeLast = 2;
				flags = {"set-clip-range", "--clip", "5", "--first-frame", "0", "--last-frame", "2"};
			}
			if (step == 3)
			{
				operation = insert;
				operation.selection = {};
				flags = {"insert-inbetweens", "--frame", "0", "--insert-count", "3", "--name", "transition"};
			}
			if (step == 4)
			{
				operation.kind = ModelEditKind::CopyFramePose;
				operation.frame = 1;
				operation.sourceFrame = 5;
				flags = {"copy-frame-pose", "--frame", "1", "--source-frame", "5"};
			}
			if (step == 5)
			{
				operation.kind = ModelEditKind::DeleteAnimation;
				operation.animationIndex = 5;
				flags = {"delete-clip", "--clip", "5"};
			}
			const auto output = QDir(temporary.path()).filePath(QString("step-%1.mesh.json").arg(step));
			QStringList args{"model", "edit", current, "--operation"};
			args << flags << "--output" << output;
			ok &= expect(cli(args, 0) && cliExpected.edit(operation, &error), "CLI executes animation edit through shared service");
			ModelDocument loaded;
			ok &= expect(loaded.load(output, &error) && bytes(loaded.mesh()) == bytes(cliExpected.mesh()),
						 "CLI animation output equals shared document result exactly");
			current = output;
		}
		ok &= expect(cli({"model", "edit", inputPath, "--operation", "delete-clip", "--clip", "0", "--output", current}, 1),
					 "CLI keeps overwrite protection");
		ok &= expect(cli({"model", "edit", inputPath, "--operation", "add-clip", "--name", "span", "--first-frame", "0", "--last-frame",
						  "1", "--output", dryOutput},
						 4) &&
						 cli({"model", "edit", inputPath, "--operation", "copy-frame-pose", "--frame", "0", "--source-frame", "0",
							  "--output", dryOutput},
							 4) &&
						 !QFileInfo::exists(dryOutput),
					 "CLI validation failures do not write output");
		for (const QStringList &flags : QList<QStringList>{{"insert-inbetweens", "--frame", "0", "--vertices", "0"},
														   {"add-clip", "--name", "new"},
														   {"delete-clip"},
														   {"copy-frame-pose", "--frame", "0"},
														   {"insert-inbetweens", "--frame", "0", "--insert-count", "0"}})
		{
			QStringList args{"model", "edit", inputPath, "--operation"};
			args << flags << "--output" << dryOutput;
			ok &= expect(cli(args, 2) && !QFileInfo::exists(dryOutput), "CLI refuses ambiguous scope or missing animation arguments");
		}
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
