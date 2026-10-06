#include "core/model_recovery.h"
#include "core/package_staging.h"
#include "tests/model_uv_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <cmath>
#include <cstring>
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
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
int i32(const QByteArray &b, int offset) { return qFromLittleEndian<qint32>(b.constData() + offset); }
int i16(const QByteArray &b, int offset) { return qFromLittleEndian<qint16>(b.constData() + offset); }
float f32(const QByteArray &b, int offset)
{
	quint32 bits = qFromLittleEndian<quint32>(b.constData() + offset);
	float value;
	std::memcpy(&value, &bits, 4);
	return value;
}
ModelMesh fixture()
{
	auto mesh = tests::uvSquare();
	mesh.tags.clear();
	mesh.md2SkinSize = {320, 200};
	mesh.frames[0].name = QStringLiteral("idle01");
	mesh.frames[1].name = QStringLiteral("idle02");
	mesh.surfaces[0].skinPaths = {QStringLiteral("models/skin.pcx"), QStringLiteral("models/skin.pcx")};
	// An intentional UV split sharing exactly the same position and normal in
	// both poses. A later test changes only the second pose to prevent sharing.
	ModelEdit detach;
	detach.kind = ModelEditKind::DetachUv;
	detach.selection.faces = {0};
	applyModelEdit(&mesh, detach);
	mesh.surfaces[0].texCoords[mesh.surfaces[0].triangles[0].a] = {0.125f, 0.25f};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
bool compareCorners(const ModelMesh &before, const ModelMesh &after, double positionError, double uvError = 0)
{
	if (before.frames.size() != after.frames.size() || after.surfaces.size() != 1 ||
		before.surfaces[0].triangles.size() != after.surfaces[0].triangles.size())
	{
		return false;
	}
	const auto &a = before.surfaces[0], &b = after.surfaces[0];
	for (int f = 0; f < before.frames.size(); ++f)
	{
		if (before.frames[f].name != after.frames[f].name)
		{
			return false;
		}
		for (int t = 0; t < a.triangles.size(); ++t)
		{
			const auto ta = a.triangles[t], tb = b.triangles[t];
			const int av[]{ta.a, ta.b, ta.c}, bv[]{tb.a, tb.b, tb.c};
			for (int c = 0; c < 3; ++c)
			{
				const auto p = a.frames[f].positions[av[c]], q = b.frames[f].positions[bv[c]];
				const auto auv = a.texCoords[av[c]], buv = b.texCoords[bv[c]];
				if (std::abs(auv.u - buv.u) > uvError + 1e-6 || std::abs(auv.v - buv.v) > uvError + 1e-6)
				{
					return false;
				}
				if (std::hypot(double(p.x) - q.x, double(p.y) - q.y, double(p.z) - q.z) > positionError + 1e-5)
				{
					return false;
				}
			}
		}
	}
	return true;
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
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-md2-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	const auto mesh = fixture();
	ModelExportReport report;
	const auto bytes = exportEditableModel(mesh, QStringLiteral("md2"), 0, &error, {}, &report);
	ok &= expect(!bytes.isEmpty(), "export animated MD2 fixture");
	if (bytes.isEmpty())
	{
		std::cerr << error.toStdString();
		return EXIT_FAILURE;
	}
	ok &= expect(bytes == exportEditableModel(mesh, QStringLiteral("MD2"), 1, &error),
				 "all-frame export is deterministic and independent of preview frame");
	ok &= expect(bytes.startsWith("IDP2") && i32(bytes, 4) == 8 && i32(bytes, 8) == 320 && i32(bytes, 12) == 200 && i32(bytes, 20) == 2 &&
					 i32(bytes, 24) == 4 && i32(bytes, 28) == 6 && i32(bytes, 32) == 2 && i32(bytes, 40) == 2 &&
					 i32(bytes, 64) == bytes.size(),
				 "independent header reader checks counts, skin slots, seam recombination and file extent");
	const int stOffset = i32(bytes, 48), triOffset = i32(bytes, 52), glOffset = i32(bytes, 60);
	for (int triangle = 0; triangle < 2; ++triangle)
	{
		ok &= expect(i32(bytes, glOffset + triangle * 40) == 3, "GL strip command contains one face");
		for (int c = 0; c < 3; ++c)
		{
			const int st = i16(bytes, triOffset + triangle * 12 + 6 + c * 2), cmd = glOffset + triangle * 40 + 4 + c * 12;
			ok &= expect(i32(bytes, cmd + 8) == i16(bytes, triOffset + triangle * 12 + c * 2) &&
							 f32(bytes, cmd) == (i16(bytes, stOffset + st * 4) + 0.5f) / 320 &&
							 f32(bytes, cmd + 4) == (i16(bytes, stOffset + st * 4 + 2) + 0.5f) / 200,
						 "GL and software paths share triangle winding, position indices and texel centres");
		}
	}
	ok &= expect(i32(bytes, 36) == 21 && i32(bytes, bytes.size() - 4) == 0, "GL count includes terminating zero word");
	for (int f = 0; f < 2; ++f)
	{
		const int base = i32(bytes, 56) + f * i32(bytes, 16);
		ok &= expect(f32(bytes, base + 8) == 0 && f32(bytes, base + 20) == f * 8 && quint8(bytes[base + 43]) == 5,
					 "flat pose uses zero Z scale and published +Z normal index");
	}
	ModelMesh decoded;
	ok &= expect(importEditableModel(QStringLiteral("models/panel.md2"), bytes, &decoded, &error) &&
					 decoded.md2SkinSize == mesh.md2SkinSize && decoded.surfaces[0].skinPaths == mesh.surfaces[0].skinPaths &&
					 compareCorners(mesh, decoded, report.maxPositionError, report.maxUvError),
				 "editable reimport preserves all frames, corner geometry, UV splits, skin slots and dimensions");
	ok &= expect(report.storedVertices == 4 && report.maxUvError <= 0.002501 && report.maxNormalAngleDegrees < 1e-4 &&
					 report.notes.size() == 2,
				 "export reports measured loss and metadata limitations");
	ModelMesh stable;
	ok &= expect(importEditableModel(QStringLiteral("panel.md2"), exportEditableModel(decoded, "md2", 0, &error), &stable, &error) &&
					 compareCorners(decoded, stable, 1e-5),
				 "native decode and export stabilize without UV drift despite combined-index reordering");
	auto malformedNormal = bytes;
	malformedNormal[i32(bytes, 56) + 43] = char(255);
	ok &= expect(!importEditableModel(QStringLiteral("panel.md2"), malformedNormal, &stable, &error),
				 "invalid normal bytes are refused on editable import");
	for (int test = 0; test < 5; ++test)
	{
		auto malformed = bytes;
		if (test == 0)
		{
			qToLittleEndian(qint32(-1), malformed.data() + 36);
		}
		if (test == 1)
		{
			qToLittleEndian(qint32(0x7fffffff), malformed.data() + glOffset);
		}
		if (test == 2)
		{
			qToLittleEndian(qint32(2048), malformed.data() + glOffset + 12);
		}
		if (test == 3)
		{
			qToLittleEndian(quint32(0x3f800000), malformed.data() + glOffset + 4); // 1.0 is not a texel centre.
		}
		if (test == 4)
		{
			qToLittleEndian(qint32(1), malformed.data() + malformed.size() - 4);
		}
		const auto previousSource = source(stable);
		ModelDocument packageDocument;
		const auto preview = decodeModelMesh(QStringLiteral("panel.md2"), malformed);
		ok &= expect(preview.geometryAvailable && !packageDocument.setMesh(preview, &error),
					 "package-decoded models receive the same GL audit before authoring");
		ok &= expect(!importEditableModel(QStringLiteral("panel.md2"), malformed, &stable, &error) && source(stable) == previousSource,
					 "malformed or disagreeing GL commands cannot silently lose data during editable import");
	}
	for (int command : {4, -4})
	{
		auto compact = tests::uvSquare();
		compact.tags.clear();
		compact.surfaces[0].skinPaths.clear();
		if (command > 0)
		{
			compact.surfaces[0].triangles = {{0, 1, 3}, {3, 1, 2}};
		}
		auto compactBytes = exportEditableModel(compact, "md2", 0, &error);
		const int start = i32(compactBytes, 60);
		QByteArray stream(56, '\0');
		qToLittleEndian(qint32(command), stream.data());
		// Clockwise native strip: 0,3,1,2; clockwise fan: 0,3,2,1.
		// The fan traverses the second native triangle before the first.
		std::memcpy(stream.data() + 4, compactBytes.constData() + start + (command > 0 ? 4 : 44), 36);
		std::memcpy(stream.data() + 40, compactBytes.constData() + start + (command > 0 ? 56 : 28), 12);
		compactBytes.truncate(start);
		compactBytes += stream;
		qToLittleEndian(qint32(14), compactBytes.data() + 36);
		qToLittleEndian(qint32(compactBytes.size()), compactBytes.data() + 64);
		ok &= expect(importEditableModel(QStringLiteral("compact.md2"), compactBytes, &stable, &error) &&
						 compareCorners(compact, stable, .1, 1.0 / 256),
					 "multi-face GL strips and fans agree with software triangles");
	}
	auto different = mesh;
	different.surfaces[0].frames[1].positions[different.surfaces[0].triangles[0].a].z += 1;
	const auto changedPose = exportEditableModel(different, "md2", 0, &error);
	ok &= expect(!changedPose.isEmpty() && i32(changedPose, 24) == 5, "position sharing compares every pose");
	different = mesh;
	different.surfaces[0].frames[1].normals[different.surfaces[0].triangles[0].a] = {1, 0, 0};
	const auto changedNormal = exportEditableModel(different, "md2", 0, &error);
	ok &= expect(!changedNormal.isEmpty() && i32(changedNormal, 24) == 5, "hard normal split in a later pose is retained");

	for (int test = 0; test < 9; ++test)
	{
		auto invalid = mesh;
		switch (test)
		{
		case 0:
			invalid.surfaces << invalid.surfaces[0];
			invalid.surfaces[1].name = "extra";
			break;
		case 1:
			invalid.md2SkinSize = {641, 200};
			break;
		case 2:
			invalid.surfaces[0].skinPaths = {QStringLiteral("skin.png")};
			break;
		case 3:
			invalid.surfaces[0].skinPaths.fill(QStringLiteral("skin.pcx"), 33);
			break;
		case 4:
			invalid.surfaces[0].texCoords[0].u = -0.1f;
			break;
		case 5:
			invalid.frames[1].name = QString(16, QLatin1Char('x'));
			break;
		case 6:
			invalid.surfaces[0].triangles.fill(invalid.surfaces[0].triangles[0], 4097);
			break;
		case 7:
			invalid.tags = tests::uvSquare().tags;
			break;
		case 8:
			// Source triangle remains valid, but its short edge rounds to zero.
			invalid.surfaces[0].frames[1].positions[invalid.surfaces[0].triangles[0].b] = {0.001f, 0, 8};
			break;
		}
		const auto before = source(invalid);
		report.storedVertices = 999;
		ok &= expect(exportEditableModel(invalid, "md2", 0, &error, {}, &report).isEmpty() && !error.isEmpty() &&
						 report.storedVertices == 0 && source(invalid) == before,
					 "unsupported or quantization-damaged MD2 fails without mutation or partial report");
	}
	auto large = mesh;
	large.frames.fill(mesh.frames[0], 513);
	large.surfaces[0].frames.fill(mesh.surfaces[0].frames[0], 513);
	ok &= expect(exportEditableModel(large, "md2", 0, &error).isEmpty(), "MD2 refuses more than 512 poses");
	large = mesh;
	for (int i = large.surfaces[0].vertexCount; i < 2053; ++i)
	{
		large.surfaces[0].texCoords << ModelTexCoord{0.5f, 0.5f};
		for (auto &pose : large.surfaces[0].frames)
		{
			pose.positions << ModelVec3{float(i), 0, 0};
			pose.normals << ModelVec3{0, 0, 1};
		}
	}
	large.surfaces[0].vertexCount = large.surfaces[0].texCoords.size();
	ok &= expect(exportEditableModel(large, "md2", 0, &error).isEmpty() && error.contains("2048"),
				 "MD2 position budget is checked after exact sharing");
	// Original-renderer position/frame limits and the authoring slot cap meet
	// exactly here. Shared pose arrays avoid an artificial fixture-copy cost.
	auto maximum = mesh;
	ModelSurface grid;
	grid.name = QStringLiteral("maximum");
	ModelFrameGeometry pose;
	for (int y = 0; y < 32; ++y)
	{
		for (int x = 0; x < 64; ++x)
		{
			pose.positions << ModelVec3{float(x), float(y), 0};
			pose.normals << ModelVec3{0, 0, 1};
			grid.texCoords << ModelTexCoord{float(x) / 63, float(y) / 31};
			if (x < 63 && y < 31)
			{
				const int a = y * 64 + x;
				grid.triangles << ModelTriangle{a, a + 1, a + 65} << ModelTriangle{a, a + 65, a + 64};
			}
		}
	}
	grid.vertexCount = 2048;
	grid.frames.fill(pose, 512);
	maximum.surfaces = {grid};
	maximum.frames.fill(mesh.frames[0], 512);
	QElapsedTimer elapsed;
	elapsed.start();
	const auto maximumBytes = exportEditableModel(maximum, "md2", 0, &error, {}, &report);
	ok &= expect(!maximumBytes.isEmpty() && i32(maximumBytes, 24) == 2048 && i32(maximumBytes, 40) == 512 && report.storedVertices == 2048,
				 "full 2048-position by 512-pose model exports within the shared storage cap");
	std::cout << "MD2 maximum fixture: " << elapsed.elapsed() << " ms, " << maximumBytes.size() << " bytes, 1048576 frame vertices\n";
	bool cancel = false;
	ModelWorkControl control;
	control.cancelled = [&] { return cancel; };
	control.progress = [&](ModelWorkPhase phase, qint64, qint64)
	{
		if (phase == ModelWorkPhase::Serializing)
		{
			cancel = true;
		}
	};
	ok &= expect(exportEditableModel(mesh, "md2", 0, &error, control).isEmpty() && !error.isEmpty(), "cancelled export returns no bytes");

	ModelDocument document;
	ok &= expect(document.setMesh(mesh, &error), "set source fixture");
	ModelEdit size;
	size.kind = ModelEditKind::SetMd2SkinSize;
	size.md2SkinSize = {640, 480};
	ok &= expect(document.edit(size, &error) && document.mesh().md2SkinSize == QSize(640, 480) && document.undo() &&
					 document.mesh().md2SkinSize == mesh.md2SkinSize && document.redo(),
				 "skin dimensions are undoable document edits");
	size.md2SkinSize = {0, 100};
	const auto previous = document.revisionFingerprint();
	ok &= expect(!document.edit(size, &error) && document.revisionFingerprint() == previous, "invalid skin dimensions fail atomically");
	const auto json = editableModelJson(mesh);
	ok &=
		expect(json.value("version").toInt() == 3 && parseEditableModel(source(mesh), &decoded, &error) && source(mesh) == source(decoded),
			   "source version three preserves MD2 settings");
	for (int version : {1, 2})
	{
		auto legacy = json;
		legacy.insert("version", version);
		legacy.remove("md2SkinSize");
		ok &= expect(parseEditableModel(QJsonDocument(legacy).toJson(), &decoded, &error) && decoded.md2SkinSize == QSize(256, 256),
					 "legacy sources retain geometry with documented default skin settings");
	}
	for (const QJsonValue &bad :
		 QList<QJsonValue>{QJsonValue(), QJsonArray{320}, QJsonArray{0, 200}, QJsonArray{320.1, 200}, QJsonArray{320, "200"}})
	{
		auto malformed = json;
		malformed.insert("md2SkinSize", bad);
		const auto before = source(decoded);
		ok &= expect(!parseEditableModel(QJsonDocument(malformed).toJson(), &decoded, &error) && source(decoded) == before,
					 "malformed version-three settings preserve the existing document");
	}
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = mesh;
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && source(restored.mesh) == source(mesh),
				 "recovery retains skin dimensions, poses and repeated skin slots");

	const auto assets = QDir(temporary.path()).filePath(QStringLiteral("assets"));
	QDir().mkpath(assets);
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(archive.load(assets, &error) && staging.loadBaseArchive(archive, &error) &&
					 stageModelMesh(mesh, "models/panel.md2", &staging, nullptr, {}, false, &error),
				 "stage MD2 in shared package service");
	QByteArray stagedBytes;
	ok &= expect(PackageStagingArchive(staging).readEntryBytes("models/panel.md2", &stagedBytes, &error) && stagedBytes == bytes,
				 "package preview consumes the same animated export bytes");
	LevelMapDocument map;
	map.format = LevelMapFormat::Quake3Map;
	ok &= expect(!stageModelMesh(mesh, "models/another.md2", &staging, &map, {}, false, &error) &&
					 !PackageStagingArchive(staging).readEntryBytes("models/another.md2", &stagedBytes, &error),
				 "unsupported map placement leaves package unchanged");
	PackageWriteRequest request;
	request.destinationPath = QDir(temporary.path()).filePath("models.pak");
	request.format = PackageArchiveFormat::Pak;
	ok &= expect(staging.writeArchive(request).succeeded() && archive.load(request.destinationPath, &error) &&
					 archive.readEntryBytes("models/panel.md2", &stagedBytes, &error) && stagedBytes == bytes,
				 "PAK write and reopen preserve native output");

	if (app.arguments().size() > 1)
	{
		const auto input = QDir(temporary.path()).filePath("input.mesh.json"), output = QDir(temporary.path()).filePath("panel.md2");
		const auto sized = QDir(temporary.path()).filePath("sized.mesh.json");
		ok &= expect(document.setMesh(mesh, &error) && document.save(input, false, &error), "save CLI fixture");
		QJsonObject result;
		const auto cli = [&](QStringList arguments, int expected)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			arguments.prepend("--cli");
			arguments << "--json";
			process.start(app.arguments()[1], arguments);
			const bool done = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto outputBytes = process.readAllStandardOutput();
			const auto jsonOutput = QJsonDocument::fromJson(outputBytes);
			result = jsonOutput.object();
			if (!done || process.exitCode() != expected)
			{
				std::cerr << outputBytes.constData() << process.readAllStandardError().constData();
			}
			return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && jsonOutput.isObject();
		};
		ok &= expect(cli({"model", "build", input, "--output", output, "--dry-run"}, 0) && !QFileInfo::exists(output) &&
						 result.value("storedVertices").toInt() == 4 && result.value("exportNotes").toArray().size() == 2,
					 "CLI dry-run validates and reports target precision without writing");
		ok &= expect(cli({"model", "build", input, "--output", output}, 0) && cli({"model", "build", input, "--output", output}, 1),
					 "CLI writes native MD2 and protects existing output");
		ok &= expect(cli({"model", "import", output, "--output", sized}, 0) && document.load(sized, &error) &&
						 document.mesh().md2SkinSize == QSize(320, 200),
					 "CLI MD2 import retains skin dimensions");
		ok &= expect(
			cli({"model", "edit", input, "--operation", "md2-skin-size", "--skin-size", "640,480", "--output", sized, "--overwrite"}, 0) &&
				document.load(sized, &error) && document.mesh().md2SkinSize == QSize(640, 480),
			"CLI edits durable MD2 dimensions");
		for (const auto &bad : {QStringLiteral("0,200"), QStringLiteral("320.5,200"), QStringLiteral("320"), QStringLiteral("NaN,200")})
		{
			ok &= expect(
				cli({"model", "edit", input, "--operation", "md2-skin-size", "--skin-size", bad, "--output", sized, "--overwrite"}, 2),
				"CLI rejects malformed skin-size arguments");
		}
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
