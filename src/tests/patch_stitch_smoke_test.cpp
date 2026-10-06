#include "tests/patch_stitch_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *text, const QString &error = {})
{
	if (!value) {
		std::cerr << text << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-8; }
bool same(const LevelMapVec3 &a, const LevelMapVec3 &b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	const auto base = tests::stitchPatch(LevelPatchBoundary::LastColumn, 0);
	for (bool columns : {false, true}) {
		for (int spans = 1; spans <= 15; ++spans) {
			auto refined = base;
			ok &= expect(refineLevelPatch(&refined, columns, spans, &error), "exact refinement", error);
			for (int x = 0; x <= 10; ++x) {
				for (int y = 0; y <= 10; ++y) {
					const auto a = tests::samplePatch(base, x / 10.0, y / 10.0), b = tests::samplePatch(refined, x / 10.0, y / 10.0);
					for (int k = 0; k < 5; ++k) {
						ok &= expect(near(a[k], b[k]), "refinement preserves independently sampled geometry and UV");
					}
				}
			}
		}
	}
	for (int a = 0; a < 4; ++a) {
		for (int b = 0; b < 4; ++b) {
			for (bool reverse : {false, true}) {
				LevelPatchStitchRequest request;
				request.first = static_cast<LevelPatchBoundary>(a);
				request.second = static_cast<LevelPatchBoundary>(b);
				request.matchTangents = true;
				request.uv = LevelPatchStitchUv::First;
				auto first = tests::stitchPatch(request.first, 0), second = tests::stitchPatch(request.second, 1, true, reverse);
				refineLevelPatch(&first, a < 2, 2);
				refineLevelPatch(&second, b < 2, 3);
				LevelPatchStitchResult result;
				ok &= expect(prepareLevelPatchStitch(first, second, request, &result, &error), "all boundary axes/directions", error);
				if (result.boundaryPoints != 13) {
					ok &= expect(false, "LCM refinement produces 13 controls");
					continue;
				}
				ok &= expect(result.reversed == reverse && near(result.maxGap, 4) && near(result.maxMovement, 2),
							 "direction and displacement report");
				const auto ae = levelPatchBoundaryIndices(result.first, request.first),
						   be = levelPatchBoundaryIndices(result.second, request.second);
				const auto ah = levelPatchBoundaryIndices(result.first, request.first, true),
						   bh = levelPatchBoundaryIndices(result.second, request.second, true);
				for (int i = 0; i < 13; ++i) {
					const int j = reverse ? 12 - i : i;
					const auto p = result.first.controlPoints[ae[i]], q = result.second.controlPoints[be[j]];
					const auto x = result.first.controlPoints[ah[i]], y = result.second.controlPoints[bh[j]];
					ok &= expect(same(p, q) && near(p.x, 2) && near(x.x + y.x, 2 * p.x) && near(x.y + y.y, 2 * p.y) &&
									 near(x.z + y.z, 2 * p.z),
								 "exact edge and opposite equal derivatives");
					ok &= expect(near(result.first.controlU[ae[i]], result.second.controlU[be[j]]) &&
									 near(result.first.controlV[ae[i]], result.second.controlV[be[j]]),
								 "UV edge copies selected source");
				}
			}
		}
	}
	LevelMapDocument map;
	ok &= expect(tests::createStitchMap(&map, &error), "create map", error);
	const QByteArray source = serializeLevelMap(map).bytes;
	const auto selection = map.selection;
	const auto revision = map.revision;
	LevelPatchStitchRequest request;
	request.matchTangents = true;
	LevelPatchStitchResult result;
	ok &= expect(stitchLevelMapPatches(&map, 0, 1, request, &result, &error), "atomic document stitch", error);
	const QByteArray stitched = serializeLevelMap(map).bytes;
	ok &= expect(map.revision == revision + 1 && map.selection == selection && map.patches[0].height == 13 && map.patches[1].height == 13,
				 "single edit preserves selection and owners");
	ok &=
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source && map.selection == selection, "exact undo", error);
	ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == stitched, "exact redo", error);
	LevelMapDocument loaded;
	ok &= expect(loadLevelMapBytes({QStringLiteral("seam.map"), {}, {}}, stitched, &loaded, &error) && loaded.patches.size() == 2 &&
					 loaded.patches[0].height == 13,
				 "saved seam reloads", error);
	for (int i = 0; i < 13; ++i) {
		ok &=
			expect(same(loaded.patches[0].controlPoints[i * 3 + 2], loaded.patches[1].controlPoints[i * 3]), "seam persists without gaps");
	}
	const auto current = serializeLevelMap(map).bytes;
	QByteArray sharedLines = source;
	sharedLines.replace("}\n}\n", "}}\n");
	LevelMapDocument shared;
	ok &=
		expect(loadLevelMapBytes({QStringLiteral("shared.map"), {}, {}}, sharedLines, &shared, &error), "parse shared source lines", error);
	const auto sharedBefore = serializeLevelMap(shared).bytes;
	ok &= expect(!stitchLevelMapPatches(&shared, 0, 1, request, nullptr, &error) && serializeLevelMap(shared).bytes == sharedBefore,
				 "source-line ownership rejects the whole stitch");
	const auto beforeRevision = map.revision;
	auto replacement = map.patches[0];
	replacement.controlPoints[0].z += 4;
	ok &= expect(!replaceLevelMapPatches(&map, {{0, replacement}, {999, replacement}}, &error) && serializeLevelMap(map).bytes == current &&
					 map.revision == beforeRevision,
				 "invalid second replacement cannot partially mutate");
	auto first = base, second = tests::stitchPatch(LevelPatchBoundary::FirstColumn, 1, true);
	const auto baseline = levelPatchDefinition(first);
	LevelPatchStitchResult untouched;
	untouched.boundaryPoints = 123;
	request.maxDistance = 3;
	ok &= expect(!prepareLevelPatchStitch(first, second, request, &untouched, &error) && untouched.boundaryPoints == 123 &&
					 levelPatchDefinition(first) == baseline,
				 "gap rejection leaves all inputs/results intact");
	request.maxDistance = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!prepareLevelPatchStitch(first, second, request, &untouched, &error), "nonfinite gap rejected");
	request.maxDistance = 8;
	ok &= expect(!prepareLevelPatchStitch(first, second, request, &untouched, &error, [] { return true; }),
				 "cancelled prepare has no changes");
	second.entityId = 2;
	ok &= expect(!prepareLevelPatchStitch(first, second, request, &untouched, &error), "different owners rejected");
	second.entityId = 0;
	second.id = 0;
	ok &= expect(!prepareLevelPatchStitch(first, second, request, &untouched, &error), "self stitch rejected");
	second.id = 1;
	refineLevelPatch(&first, false, 8);
	refineLevelPatch(&second, false, 9);
	ok &= expect(!prepareLevelPatchStitch(first, second, request, &untouched, &error), "over-limit common refinement rejected");
	first = base;
	second = tests::stitchPatch(LevelPatchBoundary::FirstColumn, 1, true);
	second.fixedSubdivisions = true;
	second.subdivisionsX = 2;
	second.subdivisionsY = 4;
	ok &= expect(prepareLevelPatchStitch(first, second, request, &result, &error) && !result.warnings.isEmpty() &&
					 result.second.fixedSubdivisions,
				 "mixed tessellation remains explicit");
	for (auto target : {LevelPatchStitchTarget::First, LevelPatchStitchTarget::Second, LevelPatchStitchTarget::Average}) {
		request.target = target;
		request.uv = LevelPatchStitchUv::Preserve;
		ok &= expect(prepareLevelPatchStitch(first, second, request, &result, &error), "position target", error);
		ok &= expect(near(result.first.controlPoints[2].x, target == LevelPatchStitchTarget::First	  ? 0
														   : target == LevelPatchStitchTarget::Second ? 4
																									  : 2) &&
						 result.first.controlU == first.controlU && result.second.controlU == second.controlU,
					 "positions chosen independently of retained UVs");
	}
	for (auto uv : {LevelPatchStitchUv::First, LevelPatchStitchUv::Second, LevelPatchStitchUv::Average}) {
		request.uv = uv;
		ok &= expect(prepareLevelPatchStitch(first, second, request, &result, &error), "each UV policy", error);
		for (int row = 0; row < 3; ++row) {
			const int a = row * 3 + 2, b = row * 3;
			const double expected = uv == LevelPatchStitchUv::First	   ? first.controlU[a]
									: uv == LevelPatchStitchUv::Second ? second.controlU[b]
																	   : (first.controlU[a] + second.controlU[b]) * 0.5;
			ok &= expect(near(result.first.controlU[a], expected) && near(result.second.controlU[b], expected) &&
							 near(result.first.controlU[a - 1] - result.first.controlU[a], first.controlU[a - 1] - first.controlU[a]) &&
							 near(result.second.controlU[b + 1] - result.second.controlU[b], second.controlU[b + 1] - second.controlU[b]),
						 "UV choices preserve each inner derivative");
		}
	}
	request.order = LevelPatchStitchOrder::Forward;
	ok &= expect(prepareLevelPatchStitch(first, second, request, &result, &error) && !result.reversed, "explicit forward pairing", error);
	request.order = LevelPatchStitchOrder::Reversed;
	ok &= expect(!prepareLevelPatchStitch(first, second, request, &result, &error), "wrong explicit direction respects gap limit");
	request.order = LevelPatchStitchOrder::Automatic;
	auto collapsed = second;
	for (int row = 0; row < 3; ++row) {
		collapsed.controlPoints[row * 3 + 1].x = collapsed.controlPoints[row * 3].x - 64;
	}
	ok &= expect(!prepareLevelPatchStitch(first, collapsed, request, &result, &error), "collapsed tangent rejected atomically");
	// Full source wrappers, comments, header extensions and unrelated entities.
	QByteArray commented = source;
	commented.replace("patchDef2", "// seam source\npatchDef2");
	commented.replace("\"classname\" \"worldspawn\"", "\"classname\" \"worldspawn\"\n\"custom\" \"retain me\"");
	ok &= expect(loadLevelMapBytes({QStringLiteral("source.map"), {}, {}}, commented, &map, &error) &&
					 stitchLevelMapPatches(&map, 0, 1, request, nullptr, &error),
				 "loaded source stitch", error);
	const auto kept = serializeLevelMap(map).bytes;
	ok &= expect(kept.count("// seam source") == 2 && kept.contains("\"custom\" \"retain me\""),
				 "source comments and unrelated properties preserved");
	if (argc > 1) {
		const auto input = QDir(temp.path()).filePath(QStringLiteral("input.map")),
				   output = QDir(temp.path()).filePath(QStringLiteral("output.map"));
		QFile file(input);
		ok &= file.open(QIODevice::WriteOnly) && file.write(source) == source.size();
		file.close();
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto run = [&](QStringList args, int expected, QJsonObject *json = nullptr, bool inputFirst = true) {
			QProcess process;
			process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
											  QDir(temp.path()).filePath(QStringLiteral("settings.ini")), QStringLiteral("map"),
											  QStringLiteral("stitch-patches"), QStringLiteral("--json")} +
									  (inputFirst ? QStringList{input} : QStringList{}) + args +
									  (inputFirst ? QStringList{} : QStringList{input}));
			const bool finished = process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			if (json) {
				*json = QJsonDocument::fromJson(bytes).object();
			}
			return expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI exit",
						  QString::fromUtf8(bytes + process.readAllStandardError()));
		};
		const QStringList args{QStringLiteral("--first"),		 QStringLiteral("0:last-column"), QStringLiteral("--second"),
							   QStringLiteral("1:first-column"), QStringLiteral("--output"),	  output};
		QJsonObject report;
		ok &= run(args + QStringList{QStringLiteral("--dry-run")}, 0, &report);
		ok &= run(args + QStringList{QStringLiteral("--dry-run"), QStringLiteral("--match-tangents")}, 0, nullptr, false);
		ok &= expect(!QFile::exists(output) &&
						 report.value(QStringLiteral("stitch")).toObject().value(QStringLiteral("boundaryPoints")).toInt() == 13,
					 "CLI dry run reports refinement without writing");
		ok &= run(args + QStringList{QStringLiteral("--match-tangents"), QStringLiteral("--uv"), QStringLiteral("first")}, 0);
		const auto saved = read(output);
		ok &= expect(!saved.isEmpty(), "CLI output written");
		ok &= run(args, 4);
		ok &= expect(read(output) == saved && read(input) == source, "existing output and source protected");
		ok &= run(args + QStringList{QStringLiteral("--max-gap"), QStringLiteral("1")}, 4);
		ok &= run(args + QStringList{QStringLiteral("--direction"), QStringLiteral("bad")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--max-gap")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--first"), QStringLiteral("2:last-row")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--first")}, 2);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
