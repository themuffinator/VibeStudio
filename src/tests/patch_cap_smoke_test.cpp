#include "tests/patch_cap_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *label, const QString &error = {})
{
	if (!value) {
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-8; }
QByteArray read(const QString &path)
{
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
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
	for (bool open : {false, true}) {
		for (int edge = 0; edge < 4; ++edge) {
			for (bool invert : {false, true}) {
				for (auto uv : {LevelPatchCapUv::Planar, LevelPatchCapUv::Boundary}) {
					const auto p = tests::capSource(edge >= 2, open);
					const auto original = levelPatchDefinition(p);
					LevelPatchCapRequest request;
					request.boundaries = {static_cast<LevelPatchBoundary>(edge)};
					request.invert = invert;
					request.uv = uv;
					LevelPatchCapResult result;
					if (!expect(prepareLevelPatchCaps(p, request, &result, &error), "closed and open caps on every boundary", error)) {
						ok = false;
						continue;
					}
					const auto &cap = result.caps.first();
					const auto center = result.centers.first();
					ok &= expect(levelPatchDefinition(p) == original && result.closed.first() != open && cap.height == 3,
								 "source retained and correct closure type");
					for (int i = 0; i <= 40; ++i) {
						const double u = i / 40.0, t = result.reversed.first() ? 1 - u : u;
						const auto boundary = tests::samplePatch(p, edge < 2 ? t : edge - 2, edge < 2 ? edge : t);
						for (int j = 0; j <= 10; ++j) {
							const double radial = j / 10.0;
							const auto point = tests::samplePatch(cap, u, radial);
							ok &= expect(near(point[0], center.x + (boundary[0] - center.x) * radial) &&
											 near(point[1], center.y + (boundary[1] - center.y) * radial) &&
											 near(point[2], center.z + (boundary[2] - center.z) * radial),
										 "independent evaluator verifies entire radial surface and exact seam");
							if (uv == LevelPatchCapUv::Boundary && j == 10) {
								ok &= expect(near(point[3], boundary[3]) && near(point[4], boundary[4]),
											 "outer curve retains full UV function");
							}
						}
					}
					const auto a = tests::samplePatch(cap, 0.1, 0.7), b = tests::samplePatch(cap, 0.1001, 0.7),
							   c = tests::samplePatch(cap, 0.1, 0.7001);
					const double normalZ = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
					const bool positive = ((edge == 1 || edge == 2) != invert) != open;
					ok &= expect((normalZ > 0) == positive, "caps face consistently with source orientation");
				}
			}
		}
	}
	auto source = tests::capSource();
	{
		auto rotated = source;
		const double a = 0.7, b = 0.4;
		for (auto &p : rotated.controlPoints) {
			const double x = p.x, y = p.y * std::cos(a) - p.z * std::sin(a), z = p.y * std::sin(a) + p.z * std::cos(a);
			p = {x * std::cos(b) - y * std::sin(b), x * std::sin(b) + y * std::cos(b), z, true};
		}
		LevelPatchCapResult tilted;
		ok &= expect(prepareLevelPatchCaps(rotated, {}, &tilted, &error), "caps on arbitrarily oriented planes", error);
		if (!tilted.caps.isEmpty()) {
			const auto p = tests::samplePatch(tilted.caps.first(), 0.1, 0.7), q = tests::samplePatch(tilted.caps.first(), 0.1001, 0.7),
					   r = tests::samplePatch(tilted.caps.first(), 0.1, 0.7001);
			const double nx = (q[1] - p[1]) * (r[2] - p[2]) - (q[2] - p[2]) * (r[1] - p[1]);
			const double ny = (q[2] - p[2]) * (r[0] - p[0]) - (q[0] - p[0]) * (r[2] - p[2]);
			const double nz = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
			ok &=
				expect(nx * std::sin(b) * std::sin(a) - ny * std::cos(b) * std::sin(a) + nz * std::cos(a) < 0, "rotated cap faces outward");
		}
		auto maximum = source;
		maximum.width = 31;
		maximum.controlPoints.clear();
		maximum.controlU.clear();
		maximum.controlV.clear();
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 31; ++column) {
				const double theta = column * 2 * std::acos(-1.0) / 30;
				maximum.controlPoints << LevelMapVec3{64 * std::cos(theta), 64 * std::sin(theta), (row - 1) * 64.0, true};
				maximum.controlU << column / 30.0;
				maximum.controlV << row * 0.5;
			}
		}
		ok &= expect(prepareLevelPatchCaps(maximum, {}, &tilted, &error) && tilted.caps.first().width == 31,
					 "maximum supported boundary grid", error);
	}
	LevelPatchCapRequest request;
	LevelPatchCapResult result;
	for (int spans : {4, 8, 12}) {
		auto refined = source;
		ok &= refineLevelPatch(&refined, true, spans, &error);
		ok &= expect(prepareLevelPatchCaps(refined, request, &result, &error) && result.caps.first().width == spans * 2 + 1,
					 "caps follow exact refined boundary grids", error);
	}
	request.boundaries = {LevelPatchBoundary::FirstRow};
	request.customCenter = true;
	request.center = {8, 4, -64, true};
	ok &= expect(prepareLevelPatchCaps(source, request, &result, &error), "off-center star-shaped cap", error);
	request.center = {1000, 0, -64, true};
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error), "outside center refused");
	request.center = {0, 0, 0, true};
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error), "off-plane center refused");
	request = {};
	source.fixedSubdivisions = true;
	source.subdivisionsX = 7;
	source.subdivisionsY = 3;
	source.headerTail = {1, 2, 3};
	ok &= expect(prepareLevelPatchCaps(source, request, &result, &error) && result.caps.first().subdivisionsX == 7 &&
					 result.caps.first().subdivisionsY == 1 && result.caps.first().headerTail == source.headerTail,
				 "fixed seam tessellation and header extensions retained", error);
	source = tests::capSource();
	const auto stable = levelPatchCapReportJson(result);
	auto invalid = source;
	invalid.controlPoints[1].z += 1;
	ok &= expect(!prepareLevelPatchCaps(invalid, request, &result, &error) && levelPatchCapReportJson(result) == stable,
				 "nonplanar input leaves result intact");
	invalid = source;
	std::swap(invalid.controlPoints[1], invalid.controlPoints[5]);
	ok &= expect(!prepareLevelPatchCaps(invalid, request, &result, &error), "folded control directions refused");
	invalid = source;
	for (int i = 0; i < 9; ++i) {
		invalid.controlPoints[i] = invalid.controlPoints[0];
	}
	ok &= expect(!prepareLevelPatchCaps(invalid, request, &result, &error), "collapsed end refused");
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error, [] { return true; }), "cancelled draft refused");
	request.unitsPerTile = std::numeric_limits<double>::infinity();
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error), "nonfinite UV scale refused");
	request = {};
	request.boundaries = {LevelPatchBoundary::FirstRow, LevelPatchBoundary::FirstRow};
	ok &= expect(!prepareLevelPatchCaps(source, request, &result, &error), "duplicate boundary refused");
	request = {};
	LevelMapDocument map;
	ok &= expect(tests::createCapMap(&map, &error), "create document", error);
	const auto input = serializeLevelMap(map).bytes;
	const auto depth = map.undoStack.size();
	const auto revision = map.revision;
	ok &= expect(capLevelMapPatch(&map, 0, request, &result, &error) && map.patches.size() == 3 && map.undoStack.size() == depth + 1 &&
					 map.revision == revision + 1,
				 "both caps commit in one revision and undo", error);
	const auto after = serializeLevelMap(map).bytes;
	ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == input && redoLevelMapEdit(&map, &error) &&
					 serializeLevelMap(map).bytes == after,
				 "exact source undo and redo", error);
	LevelMapDocument loaded;
	ok &= expect(loadLevelMapBytes({QStringLiteral("caps.map"), {}, {}}, after, &loaded, &error) && loaded.patches.size() == 3,
				 "serialized cap round trip", error);
	// All cap surfaces inherit the source entity, including non-world owners.
	QByteArray entity = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_static\"\n\"custom\" \"retain\"\n// original curve\n";
	entity += levelPatchDefinition(source).join('\n').toUtf8() + "\n}\n";
	ok &= expect(loadLevelMapBytes({QStringLiteral("entity.map"), {}, {}}, entity, &map, &error) &&
					 capLevelMapPatch(&map, 0, request, &result, &error),
				 "loaded entity caps", error);
	ok &= expect(map.patches.size() == 3 && map.patches[1].entityId == 1 && map.patches[2].entityId == 1 &&
					 serializeLevelMap(map).bytes.contains("// original curve") &&
					 serializeLevelMap(map).bytes.contains("\"custom\" \"retain\""),
				 "owner and unrelated source text retained");
	const auto beforeBad = serializeLevelMap(map).bytes;
	const auto badDepth = map.undoStack.size();
	invalid = source;
	invalid.controlV.clear();
	QVector<int> ids{999};
	ok &= expect(!addLevelMapPatches(&map, {source, invalid}, 1, &ids, &error) && ids == QVector<int>{999} &&
					 serializeLevelMap(map).bytes == beforeBad && map.undoStack.size() == badDepth,
				 "invalid second insertion changes nothing");
	QByteArray shared = entity;
	shared.chop(4);
	shared += "}}\n";
	ok &= expect(loadLevelMapBytes({QStringLiteral("shared.map"), {}, {}}, shared, &map, &error) &&
					 !capLevelMapPatch(&map, 0, request, &result, &error),
				 "shared owner closing line refused", error);
	if (argc > 1) {
		const auto in = QDir(temp.path()).filePath(QStringLiteral("source.map")),
				   out = QDir(temp.path()).filePath(QStringLiteral("capped.map"));
		QFile f(in);
		ok &= f.open(QIODevice::WriteOnly) && f.write(input) == input.size();
		f.close();
		const auto run = [&](const QStringList &args, int code, QJsonObject *json = nullptr) {
			QProcess p;
			p.start(QString::fromLocal8Bit(argv[1]),
					QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
								QDir(temp.path()).filePath(QStringLiteral("settings.ini")), QStringLiteral("map"),
								QStringLiteral("cap-patch"), QStringLiteral("--json")} +
						args + QStringList{in});
			const bool done = p.waitForFinished(30000);
			const auto bytes = p.readAllStandardOutput();
			if (json) {
				*json = QJsonDocument::fromJson(bytes).object();
			}
			return expect(done && p.exitStatus() == QProcess::NormalExit && p.exitCode() == code, "CLI cap result",
						  QString::fromUtf8(bytes + p.readAllStandardError()));
		};
		const QStringList args{
			QStringLiteral("--patch"),	  QStringLiteral("0"),		  QStringLiteral("--boundary"), QStringLiteral("first-row"),
			QStringLiteral("--boundary"), QStringLiteral("last-row"), QStringLiteral("--output"),	out};
		QJsonObject json;
		ok &= run(args + QStringList{QStringLiteral("--dry-run")}, 0, &json);
		ok &=
			expect(!QFile::exists(out) && json.value(QStringLiteral("cap")).toObject().value(QStringLiteral("caps")).toArray().size() == 2,
				   "dry run reports both caps without writes");
		ok &= run(args + QStringList{QStringLiteral("--invert"), QStringLiteral("--dry-run")}, 0);
		ok &= run(args, 0);
		const auto saved = read(out);
		ok &= run(args, 4);
		ok &= expect(read(in) == input && read(out) == saved, "source and existing output protected");
		ok &= run(args + QStringList{QStringLiteral("--uv"), QStringLiteral("bad")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--boundary")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--patch"), QStringLiteral("1")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--center"), QStringLiteral("nan,0,0")}, 2);
		ok &= run(args + QStringList{QStringLiteral("--boundary"), QStringLiteral("first-row")}, 4);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
