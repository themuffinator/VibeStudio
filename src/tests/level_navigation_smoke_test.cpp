#include "core/level_navigation.h"
#include "core/level_document.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* label, const QString& detail = {})
{
	if (!condition) { std::cerr << label << ": " << detail.toStdString() << '\n'; }
	return condition;
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray encode(const LevelViewBookmarks& views) { return QJsonDocument(levelBookmarksJson(views)).toJson(); }
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	LevelViewBookmark a {QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("Courtyard 北"), {}};
	a.view.layout = LevelViewLayout::FourViews;
	a.view.activePlan = 2;
	a.view.cameraVisible = true;
	a.view.camera.perspective = true;
	a.view.camera.position = {1024, -512, 192};
	a.view.camera.yaw = 32;
	a.view.camera.pitch = -15;
	a.view.camera.fieldOfView = 75;
	a.view.camera.orbitTarget = {32, -64, 128};
	a.view.plans[1].center = QPointF(256, -64);
	a.view.plans[1].zoom = 3.5;
	a.view.links = {true, false, true};
	LevelViewBookmarks original {a}, decoded;
	QString error;
	bool ok = expect(parseLevelBookmarks(encode(original), &decoded, &error) && encode(decoded) == encode(original), "full state round trip", error);
	for (const auto layout : {LevelViewLayout::CameraAbovePlans, LevelViewLayout::CameraBesidePlans}) {
		auto cameraWorkspace = original;
		cameraWorkspace[0].view.layout = layout;
		ok &= expect(parseLevelBookmarks(encode(cameraWorkspace), &decoded, &error)
			&& encode(decoded) == encode(cameraWorkspace), "camera above plans bookmark round trip", error);
		cameraWorkspace[0].view.plans[1].projection = 0;
		ok &= expect(!validateLevelBookmarks(cameraWorkspace, &error), "camera above plans requires three distinct ordered planes");
	}
	{
		auto plans = std::array<PlanViewState, 3> {{{0, {10, 20}, 2}, {1, {-40, 30}, 5}, {2, {70, -80}, 0.5}}};
		ok &= expect(linkLevelPlanNavigation(&plans, 0, {true, false, false}, &error) && plans[1].center == QPointF(10, 30)
			&& plans[2].center == QPointF(20, 30) && plans[1].zoom == 5 && plans[2].zoom == 0.5, "top links shared axes, preserving depth and independent zoom", error);
		plans[1].center = {100, 200}; plans[1].zoom = 4;
		ok &= expect(linkLevelPlanNavigation(&plans, 1, {true, true, false}, &error) && plans[0].center == QPointF(100, 20)
			&& plans[2].center == QPointF(20, 200) && plans[0].zoom == 4 && plans[2].zoom == 4, "front drives X/Z and shared scale");
		plans[2].center = {90, 75}; plans[2].zoom = 0.75;
		ok &= expect(linkLevelPlanNavigation(&plans, 2, {false, true, false}) && plans[0].center == QPointF(100, 20)
			&& plans[1].center == QPointF(100, 200) && plans[0].zoom == 0.75, "zoom-only linking preserves independent centres");
		ok &= expect(centerLevelPlanNavigation(&plans, {-10, 20, -30}) && plans[0].center == QPointF(-10, 20)
			&& plans[1].center == QPointF(-10, -30) && plans[2].center == QPointF(20, -30) && plans[2].zoom == 0.75, "camera centring uses XY, XZ and YZ without changing scale");
		plans[1].projection = 0; plans[1].center = {15, 35};
		ok &= expect(linkLevelPlanNavigation(&plans, 1, {true, false, false}) && plans[0].center == QPointF(15, 35)
			&& plans[2].center == QPointF(35, -30), "repeated projections preserve the remaining world coordinate");
		const auto before = plans;
		ok &= expect(!linkLevelPlanNavigation(&plans, 3, {true, true, true}) && plans[0].center == before[0].center, "invalid source is atomic");
		ok &= expect(!centerLevelPlanNavigation(&plans, {0, std::numeric_limits<double>::quiet_NaN(), 0})
			&& plans[2].center == before[2].center, "invalid follow position is atomic");
		plans[2].zoom = 0;
		ok &= expect(!linkLevelPlanNavigation(&plans, 0, {true, true, false}) && plans[2].zoom == 0
			&& plans[1].center == before[1].center, "invalid target pane is not partially linked");
	}
	{
		StudioSettings settings;
		ok &= expect(settings.levelViewLinks() == LevelViewLinks{}, "link defaults preserve independent navigation");
		ok &= expect(settings.setLevelViewLinks({true, true, false}), "save navigation preferences"); settings.sync();
		ok &= expect(StudioSettings().levelViewLinks() == LevelViewLinks{true, true, false}, "navigation preference reload");
	}
	{
		auto legacy = levelBookmarksJson(original); legacy.insert("version", 1);
		auto list = legacy.value("bookmarks").toArray(); auto item = list[0].toObject(); auto view = item.value("view").toObject();
		view.remove("links"); item.insert("view", view); list[0] = item; legacy.insert("bookmarks", list);
		ok &= expect(parseLevelBookmarks(QJsonDocument(legacy).toJson(), &decoded, &error) && decoded[0].view.links == LevelViewLinks{}
			&& decoded[0].view.camera.position == a.view.camera.position, "version-1 views retain positions with independent navigation", error);
	}
	auto invalid = [&](const QJsonObject& object, const char* name) {
		const auto before = encode(decoded);
		ok &= expect(!parseLevelBookmarks(QJsonDocument(object).toJson(), &decoded, &error) && encode(decoded) == before, name);
	};
	auto envelope = levelBookmarksJson(original);
	envelope.insert("version", 3);
	invalid(envelope, "future version rejected atomically");
	envelope = levelBookmarksJson(original);
	envelope.insert("extra", true);
	invalid(envelope, "unknown envelope rejected");
	envelope = levelBookmarksJson(original);
	{
		auto list = envelope.value("bookmarks").toArray(); auto item = list[0].toObject(); auto view = item.value("view").toObject();
		auto links = view.value("links").toObject(); links.insert("zoom", "on"); view.insert("links", links); item.insert("view", view);
		list[0] = item; envelope.insert("bookmarks", list); invalid(envelope, "version-2 links require booleans");
	}
	auto altered = original;
	altered[0].view.plans[0].projection = 2;
	invalid(levelBookmarksJson(altered), "four-view plane order checked");
	altered = original;
	altered[0].view.camera.fieldOfView = 151;
	invalid(levelBookmarksJson(altered), "camera bounds checked");
	altered[0].view.camera.fieldOfView = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!validateLevelBookmarks(altered), "nonfinite camera rejected");
	altered = original;
	altered[0].view.activePlan = 3;
	invalid(levelBookmarksJson(altered), "invalid active pane");
	altered = original;
	altered[0].view.plans[2].zoom = 0;
	invalid(levelBookmarksJson(altered), "zero zoom");
	altered = original;
	altered[0].view.plans[2].center.setX(1e30);
	invalid(levelBookmarksJson(altered), "unbounded plan position");
	altered = original;
	altered[0].name = QStringLiteral("line\nbreak");
	invalid(levelBookmarksJson(altered), "control character name");
	altered = original;
	altered.append(a);
	invalid(levelBookmarksJson(altered), "duplicate id");
	altered[1].id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	altered[1].name = a.name.toUpper();
	invalid(levelBookmarksJson(altered), "casefold name collision");
	altered.clear();
	for (int i = 0; i < LevelBookmarkLimit + 1; ++i) {
		altered.append({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::number(i), {}});
	}
	invalid(levelBookmarksJson(altered), "collection limit");
	ok &= expect(!parseLevelBookmarks(QByteArray(LevelBookmarkBytesLimit + 1, ' '), &decoded, &error), "byte limit");
	const auto path = temp.filePath(QStringLiteral("nested/views.vviews"));
	QByteArray revision;
	ok &= expect(readLevelBookmarks(path, &decoded, &revision, &error) && decoded.isEmpty() && revision.isEmpty(), "missing is empty");
	ok &= expect(writeLevelBookmarks(path, original, {}, &revision, &error), "atomic first save", error);
	const auto firstRevision = revision;
	ok &= expect(readLevelBookmarks(path, &decoded, &revision, &error) && encode(decoded) == encode(original), "durable reload");
	altered = original;
	altered[0].name = QStringLiteral("Changed");
	ok &= expect(writeLevelBookmarks(path, altered, revision, &revision, &error), "matching revision save", error);
	const auto changedBytes = read(path);
	ok &= expect(!writeLevelBookmarks(path, original, firstRevision, nullptr, &error) && read(path) == changedBytes, "stale write preserves external changes");
	{
		QLockFile lock(path + QStringLiteral(".lock"));
		ok &= expect(lock.tryLock(0), "test owns write lock");
		ok &= expect(!writeLevelBookmarks(path, original, revision, nullptr, &error) && read(path) == changedBytes, "busy writer refused");
	}
	ok &= expect(put(path, "not views"), "corrupt fixture");
	ok &= expect(!writeLevelBookmarks(path, original, revision, nullptr, &error) && read(path) == "not views", "corrupt store preserved");
	const auto mapPath = temp.filePath(QStringLiteral("arena.map"));
	LevelMapDocument map;
	LevelMapCreateRequest create;
	ok &= expect(createLevelMap(create, &map, &error) && put(mapPath, serializeLevelMap(map).bytes), "map fixture", error);
	const auto mapBytes = read(mapPath);
	const auto store = levelBookmarkStorePath(mapPath);
	ok &= expect(!store.isEmpty() && levelBookmarkStorePath({}).isEmpty(), "saved versus unsaved identity");
	ok &= expect(store != levelBookmarkStorePath(mapPath, QStringLiteral("MAP01")) &&
		levelBookmarkStorePath(mapPath, QStringLiteral("MAP01")) != levelBookmarkStorePath(mapPath, QStringLiteral("MAP02")), "WAD map identities isolated");
	ok &= expect(levelBookmarkStorePath(mapPath, QStringLiteral("map01")) == levelBookmarkStorePath(mapPath, QStringLiteral("MAP01")), "WAD names case insensitive");
	const auto input = temp.filePath(QStringLiteral("input.vviews"));
	ok &= expect(put(input, encode(original)), "portable input");
	if (argc > 1) {
		const auto binary = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
		auto viewLinksCli = [&](const QStringList& options, int code) {
			QProcess process; process.setWorkingDirectory(temp.path());
			process.start(binary, QStringList {"--cli", "--settings-file", StudioSettings::overrideFilePath(), "--json", "editor", "view-links"} + options);
			if (!process.waitForFinished(30000)) { process.kill(); process.waitForFinished(); }
			ok &= expect(process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "view-links CLI status", QString::fromUtf8(process.readAllStandardError()));
			const auto output = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			ok &= expect(!output.isEmpty(), "view-links CLI JSON"); return output;
		};
		ok &= expect(viewLinksCli({}, 0).value("links").toObject().value("centers").toBool(), "CLI reads shared preference");
		viewLinksCli({"--centers", "off", "--follow-camera=on"}, 0);
		const auto linksJson = viewLinksCli({}, 0).value("links").toObject();
		ok &= expect(!linksJson.value("centers").toBool() && linksJson.value("zoom").toBool() && linksJson.value("followCamera").toBool(), "partial CLI update retains omitted preferences");
		const auto settingsBytes = read(StudioSettings::overrideFilePath());
		for (const auto& invalidOptions : {QStringList {"--zoom", "yes"}, QStringList {"--zoom", "on", "--zoom", "off"},
			QStringList {"--unknown", "on"}, QStringList {"extra"}, QStringList {"--centers"}, QStringList {"--json=on"}}) { viewLinksCli(invalidOptions, 2); }
		ok &= expect(read(StudioSettings::overrideFilePath()) == settingsBytes, "malformed navigation CLI is write-free");
		auto cli = [&](QStringList arguments, int code) {
			QProcess process;
			process.setProgram(binary);
			process.setWorkingDirectory(temp.path());
			process.setArguments(QStringList{"--cli", "--settings-file", StudioSettings::overrideFilePath(), "--json", "editor", "bookmarks"} + arguments);
			process.start();
			if (!process.waitForFinished(30000)) { process.kill(); process.waitForFinished(); }
			const auto output = process.readAllStandardOutput();
			ok &= expect(process.exitStatus() == QProcess::NormalExit && process.exitCode() == code && QJsonDocument::fromJson(output).isObject(),
				"CLI status and JSON", QString::fromUtf8(output) + QString::fromUtf8(process.readAllStandardError()));
			return QJsonDocument::fromJson(output).object();
		};
		cli({"list", mapPath}, 0);
		ok &= expect(!QFileInfo::exists(store), "list is write free");
		cli({"import", mapPath, "--input", input}, 0);
		const auto result = cli({"list", mapPath}, 0);
		ok &= expect(result.value("views").toObject() == levelBookmarksJson(original), "CLI returns complete navigation state");
		cli({"import", mapPath, "--input", input}, 2);
		cli({"import", mapPath, "--input", input, "--replace"}, 0);
		const auto exported = temp.filePath(QStringLiteral("export.vviews"));
		cli({"export", mapPath, "--output", exported}, 0);
		ok &= expect(read(exported) == encode(original), "portable export exact");
		cli({"export", mapPath, "--output", exported}, 2);
		cli({"export", mapPath, "--output", exported, "--overwrite"}, 0);
		cli({"export", mapPath, "--output", mapPath, "--overwrite"}, 4);
		cli({"rename", mapPath, "--id", a.id, "--name", "North gate"}, 0);
		cli({"remove", mapPath, "--id", "missing"}, 3);
		cli({"list", mapPath, "--name", "unused"}, 2);
		cli({"list", mapPath, "--json"}, 2);
		cli({"list", mapPath, "--map-name", "MAP01"}, 2);
		cli({"list", mapPath, "extra"}, 2);
		cli({"import", mapPath, "--input"}, 2);
		cli({"remove", mapPath, "--id", a.id}, 0);
		ok &= expect(read(mapPath) == mapBytes, "CLI operations leave map bytes untouched");
		create.game = QStringLiteral("doom");
		create.mapName = QStringLiteral("MAP02");
		const auto wadPath = temp.filePath(QStringLiteral("test.wad"));
		ok &= expect(createLevelMap(create, &map, &error) && put(wadPath, serializeLevelMap(map).bytes), "WAD fixture", error);
		const auto wadBytes = read(wadPath);
		cli({"list", wadPath}, 2);
		cli({"import", wadPath, "--map-name", "MAP02", "--input", input}, 0);
		ok &= expect(cli({"list", wadPath, "--map-name", "MAP02"}, 0).value("views").toObject() == levelBookmarksJson(original), "CLI WAD bookmark identity");
		cli({"list", wadPath, "--map-name", "MAP99"}, 4);
		ok &= expect(read(wadPath) == wadBytes, "WAD bytes unchanged");
		const auto isolatedStore = store;
		StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("newer.ini")));
		{
			QSettings newer(StudioSettings::overrideFilePath(), QSettings::IniFormat);
			newer.setValue(QStringLiteral("app/settingsSchemaVersion"), 999);
			newer.sync();
		}
		const auto newerBytes = read(StudioSettings::overrideFilePath());
		viewLinksCli({"--centers", "on"}, 1);
		viewLinksCli({}, 0);
		cli({"import", mapPath, "--input", input}, 1);
		cli({"list", mapPath}, 0);
		ok &= expect(!QFileInfo::exists(levelBookmarkStorePath(mapPath)) && read(StudioSettings::overrideFilePath()) == newerBytes &&
			isolatedStore != levelBookmarkStorePath(mapPath), "read-only settings and override identity preserved");
	}
	std::cout << (ok ? "Level navigation and bookmarks passed.\n" : "Level navigation failed.\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
