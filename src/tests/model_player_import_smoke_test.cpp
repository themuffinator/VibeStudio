#include "core/audio_export.h"
#include "core/model_player_import.h"
#include "tests/model_player_bundle_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
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
const ModelPlayerBundleFile *entry(const ModelPlayerBundle &bundle, const QString &path)
{
	for (const auto &file : bundle.files)
		if (file.path == path)
			return &file;
	return nullptr;
}
// An in-memory copy of a package with some entries changed, removed or added.
std::shared_ptr<SkinReader> copyPackage(const PackageArchiveReader &source, const std::function<bool(const QString &)> &keep = {})
{
	auto copy = std::make_shared<SkinReader>();
	const auto entries = source.entries();
	for (qsizetype i = 0; i < entries.size(); ++i)
	{
		QByteArray bytes;
		if (entries[i].kind == PackageEntryKind::File && (!keep || keep(entries[i].virtualPath)) && source.readEntryAt(i, &bytes, nullptr))
			copy->add(entries[i].virtualPath, bytes);
	}
	return copy;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("player-import-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const QString &name) { return temporary.filePath(name); };
	PlayerBundleFixture fixture;
	QString error;
	bool ok = expect(fixture.create(temporary.path(), &error), "create the original player fixture");
	// The package to open: three skins with icons, the team skins and two
	// custom sounds, one kept as it is and one converted.
	AudioClip stereo;
	stereo.channels = 2;
	stereo.sampleRate = 44100;
	for (int i = 0; i < 4410; ++i)
		stereo.samples << float(std::sin(i * .05) * .5) << float(std::sin(i * .07) * .25);
	AudioClip mono = stereo;
	mono.channels = 1;
	mono.sampleRate = 11025;
	mono.samples.resize(2205);
	AudioWavOptions eightBit;
	eightBit.format = AudioWavFormat::Pcm8;
	ok &= expect(q3Write(path("death.wav"), encodeAudioWav(mono, eightBit, &error)) && q3Write(path("jump.wav"), encodeAudioWav(stereo, &error)),
				 "write the sound sources");
	auto options = fixture.options;
	options.teamVariants = true;
	options.variants = {ModelPlayerSkinVariant{"red", {std::nullopt, ModelPlayerReference{"team_red_upper.skin"}, std::nullopt}, std::nullopt},
						ModelPlayerSkinVariant{"blue", {std::nullopt, std::nullopt, ModelPlayerReference{"team_blue_head.skin"}},
											   ModelPlayerReference{"textures/player/a.tga", ModelAssemblySource::Package, -1}}};
	options.sounds = {{"death1", ModelPlayerReference{"death.wav"}}, {"jump1", ModelPlayerReference{"jump.wav"}}};
	ModelPlayerBundle original;
	ok &= expect(prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, options, fixture.sourcePath, &original, &error) &&
					 writeModelPlayerBundle(original, path("player.pk3"), false, false).succeeded(),
				 "publish the original player package");
	if (!ok)
	{
		std::cerr << qPrintable(error) << '\n';
		return 1;
	}
	const auto packageBytes = q3Read(path("player.pk3"));
	auto archive = std::make_shared<PackageArchive>();
	ok &= expect(archive->load(path("player.pk3"), &error), "open the published package");

	// Listing reads entry metadata only.
	QVector<ModelQ3PlayerListing> players;
	ok &= expect(listModelQ3Players(*archive, &players, &error) && players.size() == 1, "one player is found");
	if (players.size() != 1)
		return 1;
	// A copy: `players` is reused for the edge cases below.
	const auto listed = players[0];
	ok &= expect(listed.name == "synthetic" && listed.directory == "models/players/synthetic/" && listed.canOpen() &&
					 listed.models[0].present() && listed.models[1].present() && listed.models[2].present() && listed.animation.present(),
				 "the player's three models and animation.cfg are found");
	ok &= expect(listed.completeSkins() == QStringList({"default", "blue", "red"}) &&
					 std::all_of(listed.skins.cbegin(), listed.skins.cend(), [](const auto &skin) { return skin.icon.present(); }),
				 "the default skin comes first, then the others by name, each with its icon");
	ok &= expect(listed.sounds.size() == 2 && listed.sounds[0].name == "death1" && listed.sounds[1].name == "jump1" && listed.detailLevels.isEmpty(),
				 "custom sounds are found in the game's order");
	QVector<ModelQ3PlayerListing> again;
	ok &= expect(listModelQ3Players(*archive, &again, &error) && QJsonDocument(modelQ3PlayerListingJson(again[0])).toJson() ==
																	 QJsonDocument(modelQ3PlayerListingJson(listed)).toJson(),
				 "listing is deterministic");

	// Opening builds a new assembly and Player Package settings.
	ModelQ3PlayerImport imported;
	ok &= expect(importModelQ3Player(*archive, "SYNTHETIC", {}, &imported, &error), "open the player as an assembly (names match case-insensitively)");
	if (!ok)
	{
		std::cerr << qPrintable(error) << '\n';
		return 1;
	}
	const auto &parts = imported.assembly.parts;
	ok &= expect(imported.skin == "default" && parts.size() == 3 && parts[0].id == "lower" && parts[0].parent.isEmpty() && parts[1].id == "upper" &&
					 parts[1].parent == "lower" && parts[1].tag == "tag_torso" && parts[2].id == "head" && parts[2].parent == "upper" &&
					 parts[2].tag == "tag_head",
				 "lower is the root, upper sits on tag_torso and head on tag_head");
	bool linked = true;
	for (int role = 0; role < 3; ++role)
	{
		const auto &part = parts[role];
		linked &= part.sourceKind == ModelAssemblySource::Package && part.source == "models/players/synthetic/" + part.id + ".md3" &&
				  part.translation.x == 0 && part.rotation.y == 0 && part.scale == 1 && part.skin &&
				  part.skin->sourceKind == ModelAssemblySource::Package &&
				  part.skin->source == "models/players/synthetic/" + part.id + "_default.skin" && part.skin->entryIndex == -1;
	}
	ok &= expect(linked, "each package part links its default .skin by unique path, with an identity transform");
	ok &= expect(imported.assembly.q3Animation && imported.assembly.q3Animation->lowerPart == "lower" &&
					 imported.assembly.q3Animation->upperPart == "upper" &&
					 modelQ3AnimationJson(imported.assembly.q3Animation->config) == modelQ3AnimationJson(fixture.assembly.q3Animation->config),
				 "native animation comes from the package's animation.cfg");
	const auto &publication = imported.publication;
	ok &= expect(publication.modelName == "synthetic" && publication.skinName == "default" && publication.headPart == "head" &&
					 publication.iconSource == "models/players/synthetic/icon_default.tga" && publication.iconKind == ModelAssemblySource::Package &&
					 publication.teamVariants && publication.variants.size() == 2 && publication.variants[0].name == "blue" &&
					 publication.variants[1].name == "red" && publication.variants[1].skins[1] &&
					 publication.variants[1].skins[1]->source == "models/players/synthetic/upper_red.skin" && publication.variants[0].icon &&
					 publication.variants[0].icon->source == "models/players/synthetic/icon_blue.tga" && publication.sounds.size() == 2 &&
					 publication.sounds[1].source.source == "sound/player/synthetic/jump1.wav",
				 "Player Package settings keep the other skins, their icons, the team skins and the sounds");
	ok &= expect(imported.notes.join(' ').contains("The package itself is not changed") && imported.notes.join(' ').contains("blue, red") &&
					 imported.notes.join(' ').contains("Team skins is on"),
				 "the notes say what was opened and carried over");

	// Round trip: reviewing the opened player again reproduces the package.
	const ModelAssemblyContext context{temporary.path(), archive, {}};
	ModelAssemblyResolved resolved;
	ModelPlayerBundle republished;
	ok &= expect(resolveModelAssembly(imported.assembly, context, &resolved, &error), "the opened assembly resolves from the package");
	ok &= expect(prepareModelPlayerBundle(imported.assembly, resolved, context, publication, {}, &republished, &error),
				 "the opened player reviews for publication again");
	if (!ok)
		std::cerr << qPrintable(error) << '\n';
	bool same = republished.files.size() == original.files.size();
	for (const auto &file : original.files)
	{
		if (file.role == "receipt")
			continue;
		const auto *copy = entry(republished, file.path);
		if (!copy || copy->sha256 != file.sha256)
		{
			std::cerr << "differs: " << qPrintable(file.path) << '\n';
			same = false;
		}
	}
	ok &= expect(same, "every model, skin, icon, sound, animation.cfg and dependency matches the original bytes");
	for (const auto &role : {QStringLiteral("lower"), QStringLiteral("upper"), QStringLiteral("head")})
	{
		const auto *before = entry(original, "models/players/synthetic/" + role + ".md3");
		const auto *after = entry(republished, "models/players/synthetic/" + role + ".md3");
		ok &= expect(before && after && before->bytes == after->bytes, "MD3 bytes survive the round trip with identity transforms");
	}
	// The listing orders skins by name after default; the original listed red first.
	auto originalSkins = original.skins, republishedSkins = republished.skins;
	std::sort(originalSkins.begin(), originalSkins.end());
	std::sort(republishedSkins.begin(), republishedSkins.end());
	ok &= expect(republished.skins == QStringList({"default", "blue", "red"}) && republishedSkins == originalSkins,
				 "the same skins are published, default first and the others by name");

	// Non-destructive: the package is never a destination.
	const auto refused = writeModelPlayerBundle(republished, path("player.pk3"), true, false);
	ok &= expect(!refused.succeeded() && q3Read(path("player.pk3")) == packageBytes, "republishing over the opened package is refused");
	ModelAssemblyDocument document;
	ok &= expect(document.setAssembly(imported.assembly, temporary.path(), &error) && document.isModified() && document.path().isEmpty() &&
					 !document.canUndo(),
				 "the import becomes a new, unsaved assembly source");
	ok &= expect(document.save(path("opened.assembly.json"), false, &error, {}, false, archive), "save the opened assembly");
	ModelAssembly reloaded;
	ok &= expect(parseModelAssembly(q3Read(path("opened.assembly.json")), &reloaded, &error) &&
					 modelAssemblyJson(reloaded) == modelAssemblyJson(imported.assembly),
				 "the saved assembly keeps its package parts, skins and animation");
	ok &= expect(q3Read(path("player.pk3")) == packageBytes, "the package bytes are unchanged");

	// Another skin can be chosen; incomplete skins, detail levels, missing
	// files and repeated paths are reported.
	ModelQ3PlayerImport red;
	ok &= expect(importModelQ3Player(*archive, "synthetic", "red", &red, &error) && red.assembly.parts[1].skin->source.endsWith("upper_red.skin") &&
					 red.publication.skinName == "red" && red.publication.variants.size() == 2 && red.publication.variants[1].name == "blue",
				 "the red skin can be opened, keeping default and blue as other skins");
	auto edited = copyPackage(*archive, [](const QString &item) { return !item.endsWith("icon_red.tga"); });
	edited->add("models/players/synthetic/lower_green.skin", "body,textures/player/b.tga\n");
	edited->add("models/players/synthetic/lower_1.md3", q3Read(path("player.pk3")).left(16));
	edited->add("models/players/synthetic/textures_only.tga", "x");
	edited->add("models/players/other/body.tga", "x");
	ModelQ3PlayerImport greenless;
	ok &= expect(listModelQ3Players(*edited, &players, &error) && players.size() == 1 && players[0].detailLevels.size() == 1 &&
					 players[0].completeSkins() == QStringList({"default", "blue", "red"}) && players[0].skins.size() == 4,
				 "an incomplete skin is listed but not complete, and texture-only folders are not players");
	ok &= expect(importModelQ3Player(*edited, "synthetic", {}, &greenless, &error) && greenless.notes.join(' ').contains("green") &&
					 greenless.notes.join(' ').contains("lower_1.md3") && greenless.notes.join(' ').contains("reuse the package icon when published: red") &&
					 !greenless.publication.variants[1].icon,
				 "notes name the incomplete skin, the detail levels and the skin without an icon");
	ok &= expect(!importModelQ3Player(*edited, "synthetic", "green", &greenless, &error) && error.contains("no complete green skin") &&
					 error.contains("default, blue, red"),
				 "an incomplete skin cannot be opened, and the complete ones are named");
	ok &= expect(!importModelQ3Player(*edited, "missing", {}, &greenless, &error) && error.contains("Players found: synthetic"),
				 "an unknown player is refused with the players found");
	for (const auto &missing : QStringList{"head.md3", "animation.cfg"})
	{
		const auto words = missing + " is missing";
		auto broken = copyPackage(*archive, [&](const QString &item) { return !item.endsWith(missing); });
		ModelQ3PlayerImport unchanged = imported;
		ok &= expect(listModelQ3Players(*broken, &players, &error) && !players[0].canOpen() && players[0].problems.join(' ').contains(words) &&
						 !importModelQ3Player(*broken, "synthetic", {}, &unchanged, &error) && error.contains(words) &&
						 modelAssemblyJson(unchanged.assembly) == modelAssemblyJson(imported.assembly),
					 qPrintable(QStringLiteral("a player without %1 is refused and the result is unchanged").arg(missing)));
	}
	auto noSkins = copyPackage(*archive, [](const QString &item) { return !item.endsWith("_default.skin") && !item.endsWith("_red.skin") &&
																		  !item.startsWith("models/players/synthetic/head_blue"); });
	ok &= expect(listModelQ3Players(*noSkins, &players, &error) && players[0].problems.join(' ').contains("No complete skin") &&
					 !importModelQ3Player(*noSkins, "synthetic", {}, &greenless, &error),
				 "a player without a complete skin is refused");
	// Repeated paths: package parts need one entry; a configuration can be
	// chosen by its entry index.
	auto repeated = copyPackage(*archive);
	QByteArray config;
	ok &= expect(archive->readEntryBytes("models/players/synthetic/animation.cfg", &config, &error), "read the original configuration");
	auto changed = config;
	changed.replace("sex f", "sex m");
	repeated->add("Models/Players/Synthetic/ANIMATION.CFG", changed);
	const auto last = repeated->metadata.size() - 1;
	ok &= expect(listModelQ3Players(*repeated, &players, &error) && players[0].problems.join(' ').contains("appears 2 times") &&
					 !importModelQ3Player(*repeated, "synthetic", {}, &greenless, &error),
				 "a repeated animation.cfg (matched case-insensitively) is ambiguous");
	QByteArray bytes;
	qsizetype selected = -1;
	ok &= expect(!readModelQ3AnimationEntry(*repeated, "models/players/synthetic/animation.cfg", -1, &bytes, &selected, &error) &&
					 error.contains("appears 2 times") && error.contains(QString::number(last)),
				 "reading a repeated configuration by path names its entries");
	ok &= expect(readModelQ3AnimationEntry(*repeated, "models/players/synthetic/animation.cfg", last, &bytes, &selected, &error) &&
					 selected == last && bytes == changed,
				 "an exact entry index resolves the ambiguity");
	ok &= expect(!readModelQ3AnimationEntry(*repeated, "models/players/synthetic/lower_default.skin", last, &bytes, &selected, &error) &&
					 error.contains("may have changed"),
				 "an entry index whose path does not match is refused");
	auto doubledModel = copyPackage(*archive);
	doubledModel->add("models/players/synthetic/UPPER.md3", "x");
	ok &= expect(listModelQ3Players(*doubledModel, &players, &error) && players[0].problems.join(' ').contains("upper.md3 appears 2 times"),
				 "a repeated model path cannot be a package part");
	auto oversized = copyPackage(*archive, [](const QString &item) { return !item.endsWith("animation.cfg"); });
	oversized->add("models/players/synthetic/animation.cfg", QByteArray(modelQ3AnimationMaxBytes + 1, ' '));
	ok &= expect(!importModelQ3Player(*oversized, "synthetic", {}, &greenless, &error) && error.contains("19,998"),
				 "an oversized configuration is refused");
	auto malformed = copyPackage(*archive, [](const QString &item) { return !item.endsWith("animation.cfg"); });
	malformed->add("models/players/synthetic/animation.cfg", "sex f\nnot a clip\n");
	ok &= expect(!importModelQ3Player(*malformed, "synthetic", {}, &greenless, &error) && error.contains("cannot be read"),
				 "a malformed configuration is refused");
	QVector<ModelQ3PlayerListing> untouched{listed};
	ok &= expect(!listModelQ3Players(*archive, &untouched, &error, {[] { return true; }, {}}) && untouched.size() == 1 &&
					 untouched[0].name == "synthetic",
				 "cancelled listing keeps the previous result");

	// CLI parity: the same services list, open and republish.
	const auto process = [&](const QStringList &args, int exitCode, QJsonObject *payload = nullptr) {
		QProcess p;
		p.setWorkingDirectory(temporary.path());
		p.start(QString::fromLocal8Bit(argv[1]),
				QStringList{"--cli", "model", "assembly"} + args + QStringList{"--settings-file", path("settings.ini"), "--json"});
		if (!p.waitForStarted(5000) || !p.waitForFinished(60000))
		{
			p.kill();
			p.waitForFinished();
			return false;
		}
		const auto output = p.readAllStandardOutput();
		if (payload)
			*payload = QJsonDocument::fromJson(output).object();
		if (p.exitCode() != exitCode)
			std::cerr << output.constData() << p.readAllStandardError().constData();
		return p.exitStatus() == QProcess::NormalExit && p.exitCode() == exitCode;
	};
	QJsonObject payload;
	ok &= expect(process({"--operation", "players", "--package", path("player.pk3")}, 0, &payload) &&
					 payload.value("players").toArray().size() == 1 &&
					 payload.value("players").toArray()[0].toObject() == modelQ3PlayerListingJson(listed),
				 "CLI lists the package's players with the shared listing");
	ok &= expect(process({"--new", "--from-player", "synthetic", "--package", path("player.pk3"), "--output", path("cli.assembly.json")}, 0, &payload) &&
					 payload.value("operation") == "open-player" && payload.value("written").toBool() &&
					 payload.value("inputs").toArray().size() == 3 &&
					 payload.value("player").toObject().value("publication").toObject().value("teamVariants").toBool(),
				 "CLI opens the player as a new assembly");
	ModelAssembly written;
	ok &= expect(parseModelAssembly(q3Read(path("cli.assembly.json")), &written, &error) && modelAssemblyJson(written) == modelAssemblyJson(imported.assembly),
				 "the CLI writes the same assembly as the shared service");
	QStringList arguments;
	for (const auto &value : payload.value("player").toObject().value("publication").toObject().value("arguments").toArray())
		arguments << value.toString();
	ok &= expect(arguments == modelPlayerBundleArguments(publication) && arguments.contains("--team-variants") &&
					 arguments.contains("blue:head=package:models/players/synthetic/head_blue.skin"),
				 "the CLI reports the Player Package settings as player-review options");
	ok &= expect(process(QStringList{path("cli.assembly.json"), "--operation", "player-review", "--package", path("player.pk3")} + arguments, 0, &payload),
				 "CLI reviews the opened player with the reported options");
	bool cliSame = payload.value("files").toArray().size() == republished.files.size();
	for (const auto &value : payload.value("files").toArray())
	{
		const auto file = value.toObject();
		const auto *core = entry(republished, file.value("path").toString());
		cliSame &= core && (file.value("role") == "receipt" || file.value("sha256").toString() == QString::fromLatin1(core->sha256.toHex()));
	}
	ok &= expect(cliSame, "CLI review matches the core round trip");
	ok &= expect(process({"--new", "--from-player", "synthetic", "--skin-name", "red", "--name", "Red Synthetic", "--package", path("player.pk3"),
						  "--output", path("red.assembly.json"), "--dry-run"},
						 0, &payload) &&
					 !QFileInfo::exists(path("red.assembly.json")) && payload.value("assembly").toObject().value("name") == "Red Synthetic" &&
					 payload.value("player").toObject().value("skin") == "red",
				 "CLI opens another skin and renames the assembly in a dry run");
	ok &= expect(process({"--new", "--from-player", "missing", "--package", path("player.pk3"), "--output", path("x.assembly.json")}, 4) &&
					 process({"--new", "--from-player", "synthetic", "--output", path("x.assembly.json")}, 2) &&
					 process({"--from-player", "synthetic", "--package", path("player.pk3"), "--output", path("x.assembly.json")}, 2) &&
					 process({"--new", "--from-player", "synthetic", "--operation", "add", "--package", path("player.pk3"), "--output",
							  path("x.assembly.json")},
							 2) &&
					 process({"--new", "--from-player", "synthetic", "--part", "x", "--package", path("player.pk3"), "--output", path("x.assembly.json")}, 2) &&
					 process({"--operation", "players"}, 2) && !QFileInfo::exists(path("x.assembly.json")),
				 "CLI refuses unknown players and incomplete or conflicting options");
	// A folder works as a package, and is protected as one.
	bool unpacked = true;
	for (const auto &file : original.files)
		unpacked &= QDir().mkpath(QFileInfo(path("unpacked/" + file.path)).absolutePath()) && q3Write(path("unpacked/" + file.path), file.bytes);
	ok &= expect(unpacked && process({"--new", "--from-player", "synthetic", "--package", path("unpacked"), "--output", path("folder.assembly.json")}, 0),
				 "CLI opens a player from a folder");
	ok &= expect(process({"--new", "--from-player", "synthetic", "--package", path("unpacked"), "--output", path("unpacked/inside.assembly.json")}, 2) &&
					 !QFileInfo::exists(path("unpacked/inside.assembly.json")),
				 "an assembly is never written inside its package folder");
	// Native animation from a package entry.
	ok &= expect(process({fixture.sourcePath, "--operation", "animation-set", "--package", path("player.pk3"), "--config-entry",
						  "models/players/synthetic/animation.cfg", "--output", path("entry.assembly.json")},
						 0, &payload) &&
					 payload.value("nativeAnimation").toObject().value("sex") == "f" &&
					 QJsonDocument(payload.value("notes").toArray()).toJson().contains("package entry"),
				 "animation-set reads animation.cfg from a package entry");
	const auto entries = archive->entries();
	const auto index = std::find_if(entries.cbegin(), entries.cend(), [](const auto &item) { return item.virtualPath.endsWith("animation.cfg"); }) -
					   entries.cbegin();
	ok &= expect(process({fixture.sourcePath, "--operation", "animation-set", "--package", path("player.pk3"), "--config-entry",
						  "models/players/synthetic/animation.cfg", "--config-entry-index", QString::number(index), "--output",
						  path("entry.assembly.json"), "--overwrite"},
						 0) &&
					 process({fixture.sourcePath, "--operation", "animation-set", "--package", path("player.pk3"), "--config-entry",
							  "models/players/synthetic/animation.cfg", "--config-entry-index", QString::number(index + 1), "--output",
							  path("bad.assembly.json")},
							 4) &&
					 process({fixture.sourcePath, "--operation", "animation-set", "--config-entry", "models/players/synthetic/animation.cfg",
							  "--output", path("bad.assembly.json")},
							 2) &&
					 process({fixture.sourcePath, "--operation", "animation-set", "--package", path("player.pk3"), "--config", path("x.cfg"),
							  "--config-entry", "models/players/synthetic/animation.cfg", "--output", path("bad.assembly.json")},
							 2) &&
					 process({fixture.sourcePath, "--operation", "animation-set", "--package", path("player.pk3"), "--config-entry-index", "0",
							  "--output", path("bad.assembly.json")},
							 2) &&
					 !QFileInfo::exists(path("bad.assembly.json")),
				 "animation-set takes an exact entry index and refuses wrong indexes and conflicting sources");
	ok &= expect(q3Read(path("player.pk3")) == packageBytes, "no operation changed the package");
	std::cout << checks << " player import core/CLI checks\n";
	return ok ? 0 : 1;
}
