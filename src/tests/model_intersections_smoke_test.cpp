#include "core/model_intersections.h"
#include "core/model_fingerprint.h"
#include "tests/model_intersections_test_helpers.h"
#include "tests/model_scale_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
using Triangle = std::array<ModelVec3, 3>;
bool unchanged(const ModelIntersectionReport &report)
{
	return report.framesScanned == -17 && report.findings.size() == 1 && report.findings[0].firstFace == 4321;
}
ModelIntersectionReport sentinel()
{
	ModelIntersectionReport report;
	report.framesScanned = -17;
	report.findings.append({0, 0, 4321, 0, 7654, ModelTriangleContact::Crossing});
	return report;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().size() < 2)
		return 1;
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("intersections-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	bool ok = true;
	QString error;
	const auto mesh = tests::intersectionFixture();
	const auto fingerprint = modelStateFingerprint(mesh);
	ok &= expect(!fingerprint.isEmpty() && validateEditableModel(mesh).isEmpty(), "original animated fixture is valid");
	ModelIntersectionReport report;
	ok &= expect(inspectModelIntersections(mesh, {}, &report, &error) && report.framesScanned == 3 && report.facePoses == 9 &&
					 report.findings == QVector<ModelIntersectionFinding>{{1, 0, 0, 1, 0, ModelTriangleContact::Crossing},
																		  {2, 0, 0, 1, 0, ModelTriangleContact::CoplanarOverlap}},
				 "all-pose cross-surface report has exact sorted pairs and kinds");
	const auto expected = report.findings;
	ok &= expect(modelStateFingerprint(mesh) == fingerprint, "inspection preserves every geometry and metadata field");
	for (int surface = 0; surface < 3; ++surface)
	{
		ModelIntersectionOptions options;
		options.surface = surface;
		ok &= expect(inspectModelIntersections(mesh, options, &report, &error) &&
						 report.findings == (surface == 2 ? QVector<ModelIntersectionFinding>{} : expected),
					 "either side's surface filter retains its cross-surface pairs without duplicates");
	}
	for (int frame = 0; frame < 3; ++frame)
	{
		ModelIntersectionOptions options;
		options.frame = frame;
		ok &= expect(inspectModelIntersections(mesh, options, &report, &error) && report.framesScanned == 1 && report.facePoses == 3 &&
						 report.findings ==
							 (frame == 0 ? QVector<ModelIntersectionFinding>{} : QVector<ModelIntersectionFinding>{expected[frame - 1]}),
					 "single-pose scope reports the correct native frame index");
	}
	const auto self = tests::intersectionFixture(true);
	ok &= expect(inspectModelIntersections(self, {}, &report, &error) && report.findings.size() == 2 &&
					 report.findings[0] == ModelIntersectionFinding{1, 0, 0, 0, 1, ModelTriangleContact::Crossing} &&
					 report.findings[1] == ModelIntersectionFinding{2, 0, 0, 0, 1, ModelTriangleContact::CoplanarOverlap},
				 "self intersections use the same exact face identity and animation scan");
	const Triangle base{{{0, 0, 0}, {4, 0, 0}, {0, 4, 0}}};
	struct ContactCase
	{
		Triangle other;
		ModelTriangleContact expected;
		const char *message;
	};
	const QVector<ContactCase> cases{
		{base, ModelTriangleContact::CoplanarOverlap, "identical face geometry overlaps even with seam copies"},
		{{{{.5f, .5f, 0}, {1, .5f, 0}, {.5f, 1, 0}}}, ModelTriangleContact::CoplanarOverlap, "contained coplanar face overlaps"},
		{{{{4, 0, 0}, {4, 4, 0}, {0, 4, 0}}}, ModelTriangleContact::None, "common coplanar edge is legal"},
		{{{{0, 0, 0}, {4, 0, 0}, {0, 0, 3}}}, ModelTriangleContact::None, "common noncoplanar edge is legal"},
		{{{{1, 1, 0}, {1, 1, 1}, {2, 1, 1}}}, ModelTriangleContact::None, "isolated point on face interior is allowed"},
		{{{{1, .5f, 0}, {1, 2, 0}, {1, 1, 2}}},
		 ModelTriangleContact::Crossing,
		 "positive boundary interval entering another face is found"},
		{{{{1, -1, -1}, {1, -1, 1}, {1, 3, 0}}}, ModelTriangleContact::Crossing, "interior transverse crossing is found"},
		{{{{4, 0, -1}, {4, 0, 1}, {4, 3, 0}}}, ModelTriangleContact::None, "single-point transverse contact is allowed"},
		{{{{0, 0, .001f}, {4, 0, .001f}, {0, 4, .001f}}}, ModelTriangleContact::None, "separate parallel faces do not intersect"},
		{{{{4.1f, 0, 0}, {5, 0, 0}, {4.1f, 1, 0}}}, ModelTriangleContact::None, "coplanar separated faces do not intersect"}};
	for (const auto &test : cases)
	{
		bool exact = true;
		for (int reversed = 0; reversed < 2; ++reversed)
			for (int a = 0; a < 3; ++a)
				for (int b = 0; b < 3; ++b)
				{
					Triangle first{base[a], base[(a + 1) % 3], base[(a + 2) % 3]};
					Triangle second{test.other[b], test.other[(b + 1) % 3], test.other[(b + 2) % 3]};
					if (reversed)
						std::swap(second[1], second[2]);
					exact &= modelTriangleContact(first, second) == test.expected && modelTriangleContact(second, first) == test.expected;
				}
		ok &= expect(exact, test.message);
	}
	std::mt19937 generator(81237);
	std::uniform_real_distribution<float> dimensions(.1f, 15), locations(-.3f, 1.3f);
	for (int sample = 0; sample < 48; ++sample)
	{
		const float width = dimensions(generator), height = dimensions(generator), fraction = locations(generator), x = fraction * width;
		Triangle a{{{0, 0, 0}, {width, 0, 0}, {0, height, 0}}};
		Triangle b{{{x, -1, -2}, {x, -1, 2}, {x, height + 1, 0}}};
		const auto kind = fraction > 0 && fraction < 1 ? ModelTriangleContact::Crossing : ModelTriangleContact::None;
		bool exact = true;
		for (const float scale : {.001f, 1.f, 1000.f})
		{
			const auto transform = [scale](Triangle t) {
				for (auto &p : t)
					p = {p.z * scale + 500, p.x * scale - 300, p.y * scale + 700};
				return t;
			};
			exact &= modelTriangleContact(transform(a), transform(b)) == kind;
		}
		ok &= expect(exact, "analytic plane crossing agrees under axis rotation, scale and translation");
	}
	for (int limit = 0; limit < 3; ++limit)
	{
		ModelIntersectionOptions options;
		if (limit == 0)
			options.pairLimit = 1;
		if (limit == 1)
			options.nodeLimit = 1;
		if (limit == 2)
			options.findingLimit = 1;
		report = sentinel();
		ok &= expect(!inspectModelIntersections(mesh, options, &report, &error) && unchanged(report) && !error.isEmpty(),
					 "work/result limit refuses atomically after partial internal work");
	}
	for (int frame : {-2, 3})
	{
		ModelIntersectionOptions options;
		options.frame = frame;
		report = sentinel();
		ok &= expect(!inspectModelIntersections(mesh, options, &report, &error) && unchanged(report),
					 "invalid pose leaves caller's report intact");
	}
	for (int surface : {-2, 3})
	{
		ModelIntersectionOptions options;
		options.surface = surface;
		report = sentinel();
		ok &= expect(!inspectModelIntersections(mesh, options, &report, &error) && unchanged(report),
					 "invalid surface leaves caller's report intact");
	}
	bool cancelled = false;
	ModelWorkControl cancelAfterFinding;
	cancelAfterFinding.cancelled = [&] { return cancelled; };
	cancelAfterFinding.progress = [&](ModelWorkPhase, qint64 complete, qint64 total) {
		if (total == 3 && complete == 2)
			cancelled = true;
	};
	report = sentinel();
	ok &= expect(!inspectModelIntersections(mesh, {}, &report, &error, cancelAfterFinding) && cancelled && unchanged(report),
				 "cancellation after a positive pose never publishes a partial report");
	ModelWorkControl immediate;
	immediate.cancelled = [] { return true; };
	report = sentinel();
	ok &=
		expect(!inspectModelIntersections(mesh, {}, &report, &error, immediate) && unchanged(report), "pre-cancelled inspection is atomic");
	auto invalid = mesh;
	invalid.surfaces[0].frames[2].positions[0].x = std::numeric_limits<float>::quiet_NaN();
	report = sentinel();
	ok &= expect(!inspectModelIntersections(invalid, {}, &report, &error) && unchanged(report),
				 "invalid stored pose cannot produce a clean report");
	auto excessive = mesh;
	excessive.surfaces.resize(1);
	excessive.surfaces[0].triangles.fill({0, 1, 2}, modelDocumentMaxTriangles);
	excessive.frames.resize(33);
	report = sentinel();
	ok &= expect(!inspectModelIntersections(excessive, {}, &report, &error) && error.contains("face poses") && unchanged(report),
				 "face-pose workload is bounded before expensive scanning");
	const auto maximum = tests::maximumEditableGrid();
	ok &= expect(inspectModelIntersections(tests::intersectionCrowd(), {}, &report, &error) && report.findings.size() == 65341 &&
					 report.findings.first() == ModelIntersectionFinding{0, 0, 0, 0, 1, ModelTriangleContact::CoplanarOverlap} &&
					 report.findings.last() == ModelIntersectionFinding{0, 0, 360, 0, 361, ModelTriangleContact::CoplanarOverlap},
				 "dense positive report remains complete, canonical and within the finding ceiling");
	report = sentinel();
	ok &= expect(!inspectModelIntersections(tests::intersectionCrowd(363), {}, &report, &error) && unchanged(report),
				 "one larger duplicate crowd exceeds the real result ceiling atomically");
	QElapsedTimer timer;
	timer.start();
	ok &= expect(inspectModelIntersections(maximum, {}, &report, &error) && report.findings.isEmpty() && report.framesScanned == 16 &&
					 report.facePoses == 2080800 && report.candidatePairs < qint64(maximum.triangleCount) * 32 * 16,
				 "maximum animated grid is scanned completely with bounded broad-phase work");
	std::cout << "maximum animated grid intersections: " << timer.elapsed() << " ms, " << report.candidatePairs << " candidate pairs, "
			  << report.nodeChecks << " node visits\n";
	ModelDocument input;
	const auto inputPath = QDir(temporary.path()).filePath("fixture.mesh.json");
	ok &= expect(input.setMesh(mesh, &error) && input.save(inputPath, false, &error), "write CLI fixture through document services");
	QFile original(inputPath);
	ok &= expect(original.open(QIODevice::ReadOnly), "read original CLI fixture bytes");
	const auto originalBytes = original.readAll();
	original.close();
	const auto cli = [&](QStringList arguments, int expectedCode, QJsonObject *json = nullptr, bool asJson = true) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		arguments.prepend("--cli");
		if (asJson)
			arguments.append("--json");
		arguments << "--settings-file" << QDir(temporary.path()).filePath("settings.ini");
		process.start(app.arguments()[1], arguments);
		const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
		const auto output = process.readAllStandardOutput();
		if (!finished || process.exitCode() != expectedCode)
			std::cerr << output.constData() << process.readAllStandardError().constData();
		const auto document = QJsonDocument::fromJson(output);
		if (json)
			*json = document.object();
		return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedCode &&
			   (asJson ? document.isObject() : output.contains("face-pair findings"));
	};
	const QStringList command{"model", "intersections", inputPath};
	QJsonObject json;
	ok &= expect(
		cli(command, 0, &json) && json["complete"].toBool() && json["frame"].toInt() == -1 && json["surface"].toInt() == -1 &&
			json["findings"].toArray() ==
				QJsonArray{
					QJsonObject{
						{"frame", 1}, {"firstSurface", 0}, {"firstFace", 0}, {"secondSurface", 1}, {"secondFace", 0}, {"kind", "crossing"}},
					QJsonObject{{"frame", 2},
								{"firstSurface", 0},
								{"firstFace", 0},
								{"secondSurface", 1},
								{"secondFace", 0},
								{"kind", "coplanar-overlap"}}},
		"CLI JSON provides exact complete cross-surface findings");
	ok &= expect(cli({"model", "intersections", "--input=" + inputPath, "--frame=1", "--surface=1"}, 0, &json) &&
					 json["findings"].toArray().size() == 1 && json["findings"].toArray()[0].toObject()["kind"] == "crossing",
				 "inline CLI selectors match the shared service on the second surface");
	ok &= expect(cli(command + QStringList{"--frame", "0", "--surface", "all"}, 0, &json) && json["findings"].toArray().isEmpty(),
				 "CLI explicitly reports a complete clear pose");
	ok &= expect(cli(command, 0, nullptr, false), "CLI text report describes findings");
	for (const QStringList &bad :
		 {QStringList{"--frame", "3"}, QStringList{"--surface=-2"}, QStringList{"--frame=01"}, QStringList{"--frame"},
		  QStringList{"--frame=0", "--frame", "1"}, QStringList{"--output", inputPath}, QStringList{"--dry-run"},
		  QStringList{"--overwrite"}, QStringList{"--unknown"}, QStringList{"--input", inputPath}, QStringList{"extra"}})
		ok &= expect(cli(command + bad, 2), "CLI rejects invalid, repeated, unrelated and ambiguous arguments");
	ok &= expect(cli({"model", "intersections", QDir(temporary.path()).filePath("missing.mesh.json")}, 1),
				 "missing source is an I/O failure");
	ok &= expect(original.open(QIODevice::ReadOnly), "reopen CLI fixture after inspection");
	ok &= expect(original.readAll() == originalBytes && modelStateFingerprint(mesh) == fingerprint,
				 "every CLI and core route retains source identity");
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " intersection core/CLI checks\n";
	return ok ? 0 : 1;
}
