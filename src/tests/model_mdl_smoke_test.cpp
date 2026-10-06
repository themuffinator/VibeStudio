#include "core/model_mdl.h"
#include "core/model_recovery.h"
#include "tests/model_mdl_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
bool near(float value, double expected) { return std::abs(value - expected) < .000001; }
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-mdl-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto fixture = tests::groupedMdlFixture();
	auto palette = generatedIdTechPalette("quake");
	palette.colors[1] = palette.colors[0];
	palette.generated = false;
	const auto mesh = decodeModelMesh("progs/synthetic.mdl", fixture.bytes, &palette);
	bool ok = expect(mesh.isValid() && validateEditableModel(mesh).isEmpty(), "grouped MDL decodes into a complete editable value");
	if (!ok)
	{
		std::cerr << mesh.error.toStdString() << '\n' << validateEditableModel(mesh).join('\n').toStdString() << '\n';
		return EXIT_FAILURE;
	}
	ok &= expect(mesh.frames.size() == 3 && mesh.mdl.frameGroups.size() == 2 && mesh.mdl.frameGroups[0].firstFrame == 0 &&
					 mesh.mdl.frameGroups[0].intervals == QVector<float>{.1f, .3f} && mesh.mdl.frameGroups[1].firstFrame == 2 &&
					 mesh.mdl.frameGroups[1].intervals.isEmpty(),
				 "native groups and exact cumulative timing survive flattened geometry");
	ok &= expect(mesh.mdl.flags == 0x80000010u && mesh.mdl.syncType == 1 && mesh.mdl.size == 2.5f && mesh.mdl.eyePosition.x == 1.25f &&
					 mesh.mdl.eyePosition.y == 2.5f && mesh.mdl.eyePosition.z == 3.75f &&
					 mesh.mdl.palette == modelMdlPaletteBytes(palette) && !mesh.mdl.paletteGenerated,
				 "MDL header semantics and palette provenance survive");
	ok &= expect(mesh.embeddedSkins.size() == 2 && mesh.embeddedSkins[0].indexedFrames.size() == 2 &&
					 mesh.embeddedSkins[0].intervals == QVector<float>{.1f, .25f} && mesh.embeddedSkins[1].indexedFrames.size() == 1 &&
					 mesh.embeddedSkins[1].intervals.isEmpty(),
				 "all animated and single skin members survive");
	ok &= expect(mesh.embeddedSkins[0].indexedFrames[0][0] == 0 && mesh.embeddedSkins[0].indexedFrames[0][1] == 1 &&
					 mesh.embeddedSkins[0].image.pixel(0, 0) == mesh.embeddedSkins[0].image.pixel(1, 0),
				 "identical palette colours retain distinct source indices");
	QString error;
	ModelMesh parsed;
	const auto original = source(mesh);
	ok &= expect(importEditableModel("progs/synthetic.mdl", fixture.bytes, &parsed, &error, &palette) && source(parsed) == original,
				 "native MDL import retains all authoring data");
	ok &= expect(editableModelJson(mesh).value("version").toInt() == 4 && parseEditableModel(original, &parsed, &error) &&
					 source(parsed) == original,
				 "schema 4 preserves groups, every indexed image and header settings exactly");
	ModelDocument document;
	ok &= expect(document.setMesh(mesh, &error) && document.save(QDir(temporary.path()).filePath("source.mesh.json"), false, &error),
				 "MDL source uses ordinary validated document storage");
	ModelEdit edit;
	edit.kind = ModelEditKind::DuplicateFrame;
	edit.frame = 0;
	ok &=
		expect(document.edit(edit, &error) && document.mesh().frames.size() == 4 &&
				   document.mesh().mdl.frameGroups[0].intervals.size() == 3 && near(document.mesh().mdl.frameGroups[0].intervals[1], .2) &&
				   near(document.mesh().mdl.frameGroups[0].intervals[2], .4) && document.mesh().mdl.frameGroups[1].firstFrame == 3,
			   "duplicating a timed pose copies its duration and shifts following native ranges");
	ok &= expect(document.undo() && source(document.mesh()) == original && document.redo() && document.undo(),
				 "MDL metadata participates in complete undo and redo");
	edit.kind = ModelEditKind::DeleteFrame;
	ok &= expect(document.edit(edit, &error) && document.mesh().mdl.frameGroups[0].intervals.size() == 1 &&
					 near(document.mesh().mdl.frameGroups[0].intervals[0], .2) && document.mesh().mdl.frameGroups[1].firstFrame == 1 &&
					 document.undo(),
				 "deleting a timed pose removes its duration while retaining a group of one");
	edit.kind = ModelEditKind::InsertInbetweens;
	edit.inbetweenCount = 2;
	ok &= expect(
		document.edit(edit, &error) && document.mesh().frames.size() == 5 && document.mesh().mdl.frameGroups[0].intervals.size() == 4 &&
			near(document.mesh().mdl.frameGroups[0].intervals[0], .1 / 3) &&
			near(document.mesh().mdl.frameGroups[0].intervals[1], .2 / 3) && document.mesh().mdl.frameGroups[0].intervals[2] == .1f &&
			document.mesh().mdl.frameGroups[0].intervals[3] == .3f && document.mesh().mdl.frameGroups[1].firstFrame == 4 && document.undo(),
		"in-betweens divide the preceding hold time without moving the next pose's start or extending the cycle");
	edit.frame = 1;
	edit.inbetweenCount = 1;
	ok &= expect(document.edit(edit, &error) && document.mesh().mdl.frameGroups.size() == 3 &&
					 document.mesh().mdl.frameGroups[0].intervals == QVector<float>{.1f, .3f} &&
					 document.mesh().mdl.frameGroups[1].firstFrame == 2 && document.mesh().mdl.frameGroups[1].intervals.isEmpty() &&
					 document.mesh().mdl.frameGroups[2].firstFrame == 3 && document.undo(),
				 "insertion between native frames creates a single frame without retiming the existing group");
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = mesh;
	snapshot.frame = 1;
	const auto checkpoint = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot recovered;
	ok &= expect(!checkpoint.isEmpty() && inspectModelRecovery(checkpoint, &recovered).isValid() && source(recovered.mesh) == original,
				 "local recovery retains all MDL native data");
	for (const auto &key : {QStringLiteral("palette"), QStringLiteral("frameGroups"), QStringLiteral("flags")})
	{
		auto json = editableModelJson(mesh);
		auto settings = json.value("mdl").toObject();
		settings.remove(key);
		json["mdl"] = settings;
		ok &= expect(!parseEditableModel(QJsonDocument(json).toJson(), &parsed, &error) && source(parsed) == original,
					 "missing required native fields fail without altering the parse output");
	}
	for (int offset : {fixture.skinTimes, fixture.frameTimes})
	{
		for (float invalid : {0.f, -1.f, std::numeric_limits<float>::infinity(), .5f})
		{
			auto bytes = fixture.bytes;
			QByteArray value;
			tests::mdlNumber(value, invalid);
			bytes.replace(offset, 4, value);
			ok &= expect(!decodeModelMesh("bad.mdl", bytes, &palette).isValid(), "invalid or decreasing native group timing is rejected");
		}
	}
	auto bad = fixture.bytes;
	bad[fixture.normalIndex] = char(255);
	const auto invalidNormal = decodeModelMesh("bad.mdl", bad, &palette);
	ok &= expect(!invalidNormal.warnings.isEmpty() && !validateEditableModel(invalidNormal).isEmpty(),
				 "fallback preview normals cannot enter an editable source silently");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	ok &= expect(!parseEditableModel(original, &parsed, &error, cancelled) && source(parsed) == original &&
					 editableModelJson(mesh, &error, cancelled).isEmpty(),
				 "MDL source work preserves cancellation and atomic outputs");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
