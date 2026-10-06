#include "tests/model_q3_animation_test_helpers.h"
#include "core/model_assembly_animation.h"
#include "core/model_assembly_recovery.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include <cmath>
#include <iostream>
#include <limits>

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
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("q3-animation-core-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QString error;
	auto config = tests::q3Config();
	const auto bytes = exportModelQ3Animation(config, &error);
	ModelQ3AnimationConfig parsed;
	ok &= expect(!bytes.isEmpty() && parseModelQ3Animation(bytes, &parsed, &error) && exportModelQ3Animation(parsed) == bytes,
				 "canonical native round trip");
	ok &= expect(modelQ3AnimationFirstFrame(config, 13) == 2 && modelQ3AnimationFirstFrame(config, 15) == 4 &&
					 modelQ3AnimationFirstFrame(config, 25) == 1,
				 "native lower offset applies to legs only; later torso gestures retain upper coordinates");
	for (int i = 0; i < modelQ3AnimationCount; ++i)
		ok &= expect(modelQ3AnimationSlot(modelQ3AnimationName(i)) == i, "stable native roster identifiers");
	ok &= expect(modelQ3AnimationName(-1).isEmpty() && modelQ3AnimationSlot("UNKNOWN") == -1, "unknown native names rejected");
	const int forward[]{4, 5, 6, 7, 8, 7, 8, 7, 8}, reverse[]{8, 7, 6, 5, 4, 5, 4, 5, 4};
	for (bool reversed : {false, true})
	{
		config.clips[15].reversed = reversed;
		for (int i = 0; i < 8; ++i)
		{
			ModelAnimationSample sample;
			ok &= expect(sampleModelQ3Animation(config, 15, (i + .5) / 10, true, &sample) &&
							 sample.frame == (reversed ? reverse[i] : forward[i]) &&
							 sample.nextFrame == (reversed ? reverse[i + 1] : forward[i + 1]) && std::abs(sample.fraction - .5) < 1e-10,
						 "intro then loop tail in playback order, including reversed boundary interpolation");
		}
	}
	config.clips[15].reversed = false;
	ModelAnimationSample sample;
	ok &= expect(sampleModelQ3Animation(config, 11, 1000000, true, &sample) && sample.frame == 5 && sample.nextFrame == 5 &&
					 sample.fraction == 0,
				 "nonlooping clip holds final frame under delayed updates");
	config.clips[15].framesPerSecond = 15;
	ok &= expect(modelQ3AnimationPeriod(config.clips[15]) == 66 && sampleModelQ3Animation(config, 15, .099, true, &sample) &&
					 sample.frame == 5 && sample.nextFrame == 6 && std::abs(sample.fraction - .5) < 1e-12,
				 "native FPS uses truncated millisecond period");
	ok &= expect(sampleModelQ3Animation(config, 15, .099, false, &sample) && sample.frame == 5 && sample.nextFrame == 5 &&
					 sample.fraction == 0,
				 "stored pose playback uses native timing");
	auto boundaryConfig = config;
	boundaryConfig.clips[3] = {1, 4, 3, 15, true};
	ok &= expect(sampleModelQ3Animation(boundaryConfig, 3, 2.046, true, &sample) && sample.frame == 3 && sample.nextFrame == 2 &&
					 sample.fraction == 0,
				 "exact native millisecond boundary does not fall into the previous pose through double roundoff");
	ok &= expect(sampleModelQ3Animation(boundaryConfig, 3, 2.045999, true, &sample) && sample.frame == 1 && sample.nextFrame == 3 &&
					 sample.fraction > .999 && sample.fraction < 1,
				 "nearby pre-boundary time retains continuous interpolation");
	for (double invalid : {-1.0, 1000001.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		sample = {7, 8, .25};
		ok &= expect(!sampleModelQ3Animation(config, 15, invalid, true, &sample) && sample.frame == 7 && sample.nextFrame == 8 &&
						 sample.fraction == .25,
					 "invalid sample is atomic");
	}
	const auto sentinel = exportModelQ3Animation(parsed);
	QList<QByteArray> malformed{QByteArray(),
								bytes + "0 1 0 10\n",
								QByteArray("unknown\n") + bytes,
								QByteArray("sex m\n") + bytes,
								bytes + QByteArray("\0", 1),
								QByteArray(modelQ3AnimationMaxBytes + 1, ' '),
								QByteArray("/* unterminated") + bytes};
	for (const auto &bad : malformed)
		ok &= expect(!parseModelQ3Animation(bad, &parsed, &error) && exportModelQ3Animation(parsed) == sentinel,
					 "malformed native parse leaves output intact");
	auto signedFirst = bytes;
	signedFirst.replace("1 5 2 10 // BOTH_DEATH1", "+1 5 2 10 // BOTH_DEATH1");
	ok &= expect(!parseModelQ3Animation(signedFirst, &parsed, &error), "native first row must begin with a digit");
	for (int variant = 0; variant < 8; ++variant)
	{
		auto bad = tests::q3Config();
		if (variant == 0)
			bad.clips[0].frameCount = 0;
		if (variant == 1)
			bad.clips[0].frameCount = 1025;
		if (variant == 2)
			bad.clips[0].loopFrames = 6;
		if (variant == 3)
			bad.clips[0].framesPerSecond = std::numeric_limits<float>::quiet_NaN();
		if (variant == 4)
			bad.clips[0].framesPerSecond = 1001;
		if (variant == 5)
			bad.clips[15].firstFrame = 0;
		if (variant == 6)
			bad.headOffset.x = std::numeric_limits<float>::infinity();
		if (variant == 7)
			bad.footsteps = "unsupported";
		ok &= expect(!validateModelQ3Animation(bad, &error) && exportModelQ3Animation(bad).isEmpty(),
					 "invalid authoring fails before native export");
	}
	QByteArray legacy = "/* legacy */\nsex n\n";
	for (int i = 0; i < 25; ++i)
		legacy += "0 -2 1 0 // slot\n";
	QStringList notes;
	ok &= expect(parseModelQ3Animation(legacy, &parsed, &error, &notes) && notes.size() == 26 && parsed.clips[6].reversed &&
					 !parsed.clips[25].reversed && parsed.clips[30].frameCount == 2 && parsed.clips[0].framesPerSecond == 1,
				 "legacy fallback and zero rate normalized with notes");
	ok &= expect(!parseModelQ3Animation(legacy + "0 2", &parsed, &error), "partial legacy extension row rejected");
	const auto modelBytes = QJsonDocument(editableModelJson(tests::q3Model())).toJson();
	ok &= expect(tests::q3Write(path("part.mesh.json"), modelBytes), "synthetic fixture stored");
	auto assembly = tests::q3Assembly(path("part.mesh.json"));
	ModelAssemblyContext context{temporary.path(), {}, {}};
	ModelAssemblyResolved resolved;
	ok &= expect(resolveModelAssembly(assembly, context, &resolved, &error), qPrintable(error));
	ModelAssemblyPose pose;
	ok &= expect(sampleModelAssembly(assembly, resolved, .45, &pose, &error), qPrintable(error));
	if (!ok)
		return 1;
	ok &= expect(pose.samples[1].frame == 8 && pose.samples[1].nextFrame == 7 && pose.samples[0].frame == 5 &&
					 std::abs(pose.partTransforms[0].origin.x - 7.5f) < .0001 &&
					 std::abs(pose.mesh.surfaces[1].frames[0].positions[0].z - 25) < .0001,
				 "native independent lower/upper samples drive tag attachment and composed geometry");
	ModelAssemblyAnimation bake;
	ok &= expect(bakeModelAssemblyAnimation(assembly, resolved, {0, 9, 10, "native"}, &bake, &error) && bake.mesh.frames.size() == 9,
				 "ordinary animation bake consumes native clips");
	for (int i = 0; i < 9; ++i)
		ok &= expect(std::abs(bake.mesh.surfaces[0].frames[i].positions[0].z - forward[i]) < .0001,
					 "bake agrees with explicit native pose sequence");
	ModelAssemblyDocument document;
	ok &= expect(document.setAssembly(assembly, temporary.path(), &error) && document.save(path("native.assembly.json"), false, &error),
				 "version 2 native source save");
	ModelAssemblyDocument reopened;
	ok &= expect(reopened.load(path("native.assembly.json"), &error) &&
					 modelAssemblyFingerprint(reopened.assembly()) == modelAssemblyFingerprint(assembly),
				 "version 2 source preserves native configuration and clip selections");
	auto renamed = document.assembly().parts[1];
	renamed.id = "legs";
	ok &= expect(document.setPart("lower", renamed, &error) && document.assembly().q3Animation->lowerPart == "legs" &&
					 document.assembly().parts[0].parent == "legs" && document.undo() &&
					 document.assembly().q3Animation->lowerPart == "lower",
				 "rename rebases animation binding and parent links in one undo step");
	ok &= expect(!document.removeBranch("lower", &error), "deleting bound part requires explicit native removal or rebinding");
	ok &= expect(document.setQ3Animation(std::nullopt, &error) && !document.assembly().q3Animation && document.undo() &&
					 document.assembly().q3Animation && document.redo() && document.undo(),
				 "native removal, undo and redo are one document transaction");
	const auto recoveryDirectory = path("recoveries");
	auto recovery = ModelAssemblyRecoverySession::acquire(recoveryDirectory, QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelAssemblyRecoverySnapshot snapshot{assembly, temporary.path(), "upper", .45, document.path(), document.sourceFingerprint()};
	ok &= expect(recovery && recovery->write(snapshot, &error), "native recovery checkpoint written");
	if (recovery)
	{
		const auto record = inspectModelAssemblyRecovery(recovery->path());
		ModelAssemblyDocument recovered;
		ModelAssemblyRecoverySnapshot restored;
		ok &= expect(record.isValid() && restoreModelAssemblyRecovery(record.path, record.sha256, &recovered, &restored, &error) &&
						 modelAssemblyFingerprint(recovered.assembly()) == modelAssemblyFingerprint(assembly) && restored.seconds == .45 &&
						 recovered.selectedPart() == "upper" && !recovered.save(document.path(), true, &error),
					 "recovery retains native data and selected pose while protecting original source");
		ok &= expect(recovery->retire(&error), "owned recovery retired");
	}
	auto invalid = assembly;
	invalid.q3Animation->config.clips[0].firstFrame = 100;
	const auto oldHash = resolved.recipeSha256;
	ok &= expect(!resolveModelAssembly(invalid, context, &resolved, &error) && resolved.recipeSha256 == oldHash,
				 "all slots validated against both models, including unselected ranges; failure atomic");
	ok &= expect(
		exportModelAssemblyQ3Animation(assembly, resolved, path("animation.cfg"), path("native.assembly.json"), false, true, &error) &&
			!QFileInfo::exists(path("animation.cfg")),
		"native export dry run leaves destination absent");
	ok &= expect(
		exportModelAssemblyQ3Animation(assembly, resolved, path("animation.cfg"), path("native.assembly.json"), false, false, &error) &&
			tests::q3Read(path("animation.cfg")) == bytes,
		"native file export identical to shared writer");
	ok &= expect(!exportModelAssemblyQ3Animation(assembly, resolved, path("animation.cfg"), {}, false, false, &error) &&
					 !exportModelAssemblyQ3Animation(assembly, resolved, path("animation.cfg"), path("animation.cfg"), true, false, &error),
				 "existing output and protected input denied");
	auto json = modelAssemblyJson(assembly);
	json.insert("version", 1);
	ModelAssembly unchanged = assembly;
	ok &= expect(!parseModelAssembly(QJsonDocument(json).toJson(), &unchanged, &error) && modelAssemblyFingerprint(unchanged) == oldHash,
				 "version 1 cannot silently discard native configuration");
	json = modelAssemblyJson(assembly);
	auto bindingJson = json.value("q3Animation").toObject();
	bindingJson.insert("unknown", true);
	json.insert("q3Animation", bindingJson);
	ok &= expect(!parseModelAssembly(QJsonDocument(json).toJson(), &unchanged, &error), "unknown native binding fields rejected");
	if (argc > 1)
	{
		const auto run = [&](const QStringList &args, int code, QJsonObject *payload = nullptr) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "model", "assembly"} + args +
															   QStringList{"--json", "--settings-file", path("settings.ini")});
			if (!process.waitForFinished(30000) || process.exitCode() != code)
			{
				std::cerr << process.readAllStandardError().constData();
				return false;
			}
			if (payload)
				*payload = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return true;
		};
		ok &= expect(run({path("native.assembly.json"), "--operation", "animation-set", "--config", path("animation.cfg"),
						  "--lower-animation", "LEGS_WALK", "--upper-animation", "TORSO_ATTACK", "--output", path("cli.assembly.json")},
						 0),
					 "CLI config import and preview clip selection");
		ok &= expect(run({path("cli.assembly.json"), "--operation", "animation-export", "--output", path("cli.cfg"), "--dry-run"}, 0) &&
						 !QFileInfo::exists(path("cli.cfg")),
					 "CLI dry run is read only");
		QJsonObject inspection;
		ok &= expect(
			run({path("cli.assembly.json"), "--time", "0.45"}, 0, &inspection) &&
				inspection.value("nativeAnimation").toObject().value("clips").toArray().size() == 31 &&
				inspection.value("nativeAnimation").toObject().value("clips").toArray()[15].toObject().value("modelFirstFrame").toInt() ==
					4,
			"CLI exposes structured native and adjusted frame diagnostics");
		ok &= expect(run({path("cli.assembly.json"), "--operation", "animation-export", "--output", path("cli.cfg")}, 0) &&
						 tests::q3Read(path("cli.cfg")) == bytes,
					 "CLI native export matches GUI/core contract");
		ok &= expect(run({path("cli.assembly.json"), "--operation", "animation-set", "--lower-animation", "TORSO_ATTACK", "--output",
						  path("bad.assembly.json")},
						 4) &&
						 !QFileInfo::exists(path("bad.assembly.json")),
					 "CLI mismatched native slot denied without output");
		ok &= expect(run({path("cli.assembly.json"), "--operation", "animation-clear", "--output", path("cleared.assembly.json")}, 0) &&
						 reopened.load(path("cleared.assembly.json"), &error) && !reopened.assembly().q3Animation,
					 "CLI native removal returns to ordinary assembly playback");
	}
	ok &= expect(tests::q3Read(path("part.mesh.json")) == modelBytes, "model input preserved byte for byte");
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (!evidence.isEmpty())
	{
		auto oracle = tests::q3Config();
		const float rates[]{.001f, 1, 10, 15, 29.97f, 60, 1000};
		QJsonArray slotRecords;
		for (int i = 0; i < modelQ3AnimationCount; ++i)
		{
			auto &clip = oracle.clips[i];
			clip.frameCount = 1 + i % 7;
			clip.loopFrames = i % (clip.frameCount + 1);
			clip.framesPerSecond = rates[i % 7];
			clip.reversed = i % 2;
		}
		for (int i = 0; i < modelQ3AnimationCount; ++i)
		{
			const auto &clip = oracle.clips[i];
			QJsonArray frames;
			for (int tick = 0; tick < 33; ++tick)
			{
				ok &= expect(sampleModelQ3Animation(oracle, i, tick * modelQ3AnimationPeriod(clip) / 1000.0, false, &sample),
							 "native oracle sample prepared");
				frames.append(sample.frame);
			}
			slotRecords.append(QJsonObject{{"first", modelQ3AnimationFirstFrame(oracle, i)},
										   {"count", clip.frameCount},
										   {"loop", clip.loopFrames},
										   {"period", modelQ3AnimationPeriod(clip)},
										   {"reverse", clip.reversed},
										   {"frames", frames}});
		}
		ok &= expect(tests::q3Write(QDir(evidence).filePath("animation.cfg"), exportModelQ3Animation(oracle)) &&
						 tests::q3Write(QDir(evidence).filePath("animation-expected.json"),
										QJsonDocument(QJsonObject{{"slots", slotRecords}}).toJson()),
					 "native oracle fixture exported");
	}
	QElapsedTimer timer;
	timer.start();
	for (int i = 0; i < 100000; ++i)
		if (!sampleModelQ3Animation(config, 15, i * .017, true, &sample))
			return 1;
	std::cout << checks << " checks; 100,000 native samples " << timer.elapsed() << " ms\n";
	return ok ? 0 : 1;
}
