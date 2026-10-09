#include "cli/release.h"

#include "core/game_asset_register.h"
#include "core/game_installation.h"
#include "core/project_manifest.h"
#include "core/release_notes.h"
#include "core/release_plan.h"
#include "core/release_publish.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>

#include <optional>

namespace vibestudio::cli {

namespace {

struct Arguments {
	QStringList positional;
	QHash<QString, QStringList> values;
	QSet<QString> flags;
	QString error;

	[[nodiscard]] QString value(const QString& key) const { return values.value(key).value(0); }
	[[nodiscard]] QStringList all(const QString& key) const { return values.value(key); }
	[[nodiscard]] bool has(const QString& key) const { return flags.contains(key) || values.contains(key); }
};

ReleaseCliResult failure(int code, const QString& message)
{
	ReleaseCliResult result;
	result.exitCode = code;
	result.error = message;
	return result;
}

Arguments parseArguments(const QStringList& arguments, const QSet<QString>& flags, const QSet<QString>& options, const QSet<QString>& repeatable)
{
	static const QSet<QString> globalOptions {QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	static const QSet<QString> globalFlags {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")};
	Arguments parsed;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString& argument = arguments[i];
		if (!argument.startsWith(QStringLiteral("--"))) {
			parsed.positional << argument;
			continue;
		}
		const qsizetype equal = argument.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? argument : argument.left(equal);
		if (globalFlags.contains(key) || flags.contains(key)) {
			if (equal >= 0) {
				parsed.error = QCoreApplication::translate("VibeStudioReleaseCli", "%1 takes no value.").arg(key);
				return parsed;
			}
			parsed.flags.insert(key);
			continue;
		}
		if (!options.contains(key) && !globalOptions.contains(key)) {
			parsed.error = QCoreApplication::translate("VibeStudioReleaseCli", "Unknown option %1.").arg(key);
			return parsed;
		}
		QString value;
		if (equal >= 0) {
			value = argument.mid(equal + 1);
		} else if (i + 1 < arguments.size()) {
			value = arguments[++i];
		}
		if (value.isEmpty()) {
			parsed.error = QCoreApplication::translate("VibeStudioReleaseCli", "%1 needs a value.").arg(key);
			return parsed;
		}
		if (parsed.values.contains(key) && !repeatable.contains(key)) {
			parsed.error = QCoreApplication::translate("VibeStudioReleaseCli", "%1 was given more than once.").arg(key);
			return parsed;
		}
		parsed.values[key] << value;
	}
	return parsed;
}

QJsonArray jsonStrings(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

bool findInstallation(const StudioSettings& settings, const QString& id, GameInstallationProfile* profile)
{
	for (const GameInstallationProfile& candidate : settings.gameInstallations()) {
		if (sameGameInstallationId(candidate.id, id)) {
			*profile = candidate;
			return true;
		}
	}
	return false;
}

struct ProjectContext {
	QString root;
	ProjectManifest manifest;
	bool hasManifest = false;
	GameInstallationProfile installation;
	bool hasInstallation = false;
	QString gameKey;
	ProjectReleaseSettings release;
	QStringList warnings;
};

// The project (or plain folder) a release command works on, its game, its
// installation and its effective release settings with command-line overrides.
bool loadProjectContext(const Arguments& args, const QString& projectArgument, ProjectContext* context, ReleaseCliResult* error)
{
	context->root = QDir::cleanPath(QFileInfo(projectArgument.isEmpty() ? QDir::currentPath() : projectArgument).absoluteFilePath());
	if (!QFileInfo(context->root).isDir()) {
		*error = failure(3, QCoreApplication::translate("VibeStudioReleaseCli", "Project folder not found: %1").arg(QDir::toNativeSeparators(context->root)));
		return false;
	}
	QString manifestError;
	context->hasManifest = loadProjectManifest(context->root, &context->manifest, &manifestError);
	if (!context->hasManifest && projectArgument.isEmpty()) {
		// Never scan an arbitrary working folder by accident: a folder without
		// a manifest must be named.
		*error = failure(3, QCoreApplication::translate("VibeStudioReleaseCli", "No project manifest in %1. Name the project folder, or create a manifest with: vibestudio --cli project init <folder>")
			.arg(QDir::toNativeSeparators(context->root)));
		return false;
	}
	if (!context->hasManifest) {
		context->manifest = defaultProjectManifest(context->root);
		context->warnings << QCoreApplication::translate("VibeStudioReleaseCli", "%1 has no project manifest; its folder defaults are used. Run: vibestudio --cli project init %2")
			.arg(QDir::toNativeSeparators(context->root), QDir::toNativeSeparators(context->root));
	}
	const StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
	const QString requested = args.value(QStringLiteral("--installation"));
	if (!requested.isEmpty()) {
		if (!findInstallation(settings, requested, &context->installation)) {
			*error = failure(3, QCoreApplication::translate("VibeStudioReleaseCli", "No saved installation has the id %1. List them with: vibestudio --cli install list").arg(requested));
			return false;
		}
		context->hasInstallation = true;
	} else {
		// The same choice the Package and Release window makes.
		context->hasInstallation = releaseInstallationFor(context->manifest, settings.gameInstallations(), settings.selectedGameInstallationId(), &context->installation);
	}
	context->gameKey = args.has(QStringLiteral("--game")) ? normalizedGameKey(args.value(QStringLiteral("--game")))
		: effectiveProjectGameKey(context->manifest, context->hasInstallation ? &context->installation : nullptr);
	context->release = effectiveProjectReleaseSettings(context->manifest, context->gameKey);
	if (args.has(QStringLiteral("--release-version"))) {
		context->release.version = args.value(QStringLiteral("--release-version"));
	}
	if (args.has(QStringLiteral("--title"))) {
		context->release.title = args.value(QStringLiteral("--title"));
	}
	if (args.has(QStringLiteral("--format"))) {
		context->release.packageFormat = args.value(QStringLiteral("--format")).toLower();
	}
	if (args.has(QStringLiteral("--package-name"))) {
		context->release.packageName = releaseSlug(args.value(QStringLiteral("--package-name")));
	}
	if (args.has(QStringLiteral("--include-sources"))) {
		context->release.includeSources = true;
	}
	if (!isValidReleaseVersion(context->release.version)) {
		*error = failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "%1 is not a usable version. Use letters, digits, '.', '-', '+' or '_', such as 1.2.0.").arg(context->release.version));
		return false;
	}
	return true;
}

struct PlannedRelease {
	ReleasePlan plan;
	ReleaseStockContext stock;
};

bool planFromArguments(const Arguments& args, const ProjectContext& context, PlannedRelease* planned, ReleaseCliResult* error)
{
	ReleaseRequest request;
	request.manifest = context.manifest;
	request.gameKey = context.gameKey;
	request.release = context.release;
	QStringList items;
	ReleaseScope scope = ReleaseScope::Project;
	int kinds = 0;
	for (const auto& [option, kind] : {std::pair {QStringLiteral("--map"), ReleaseScope::Maps}, std::pair {QStringLiteral("--model"), ReleaseScope::Models},
			 std::pair {QStringLiteral("--texture"), ReleaseScope::Textures}}) {
		if (args.values.contains(option)) {
			items += args.all(option);
			scope = kind;
			++kinds;
		}
	}
	if (kinds > 1) {
		*error = failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Choose one kind of item: --map, --model or --texture."));
		return false;
	}
	if (args.has(QStringLiteral("--scope"))) {
		bool ok = false;
		const ReleaseScope requested = releaseScopeFromId(args.value(QStringLiteral("--scope")), &ok);
		if (!ok || (kinds > 0 && requested != scope)) {
			*error = failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--scope must be project, maps, models or textures, and match the items given."));
			return false;
		}
		scope = requested;
	}
	if (scope != ReleaseScope::Project && items.isEmpty()) {
		*error = failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Name the items to release with --map, --model or --texture."));
		return false;
	}
	for (QString& item : items) {
		const QFileInfo info(item);
		item = info.isAbsolute() ? QDir::cleanPath(item) : QDir::cleanPath(QFileInfo::exists(QDir(context.root).filePath(item)) ? QDir(context.root).filePath(item) : info.absoluteFilePath());
	}
	request.scope = scope;
	request.items = items;
	if (!args.has(QStringLiteral("--no-stock"))) {
		planned->stock = prepareReleaseStock(context.hasInstallation ? &context.installation : nullptr, context.release, context.gameKey, args.value(QStringLiteral("--register")));
		request.stock = planned->stock.stock;
	}
	planned->plan = planRelease(request);
	planned->plan.stockDescription = planned->stock.description;
	for (const QString& warning : planned->stock.warnings + context.warnings) {
		if (!planned->plan.warnings.contains(warning)) {
			planned->plan.warnings.prepend(warning);
		}
	}
	return true;
}

QJsonObject contextJson(const ProjectContext& context, const PlannedRelease* planned)
{
	QJsonObject object {
		{QStringLiteral("project"), QDir::toNativeSeparators(context.root)},
		{QStringLiteral("manifest"), context.hasManifest},
		{QStringLiteral("game"), context.gameKey},
		{QStringLiteral("installation"), context.hasInstallation ? QJsonValue(context.installation.id) : QJsonValue(QJsonValue::Null)},
	};
	if (planned) {
		object.insert(QStringLiteral("stock"), QJsonObject {
			{QStringLiteral("available"), planned->stock.available},
			{QStringLiteral("description"), planned->stock.description},
			{QStringLiteral("warnings"), jsonStrings(planned->stock.warnings)},
			{QStringLiteral("register"), gameAssetRegisterStatusJson(planned->stock.status)},
		});
	}
	return object;
}

struct NotesBundle {
	ProjectChangelog changelog;
	QVector<ChangelogEntry> changes;
	QVector<ChangelogEntry> additions;
	ReleaseInventoryDiff diff;
	ReleaseNotesInput input;
	QString markdown;
	QString readme;
};

bool buildNotes(const ReleasePlan& plan, const ProjectContext& context, const QDate& date, NotesBundle* bundle, QString* error)
{
	const QString changelogPath = QDir::isAbsolutePath(context.release.changelogFile) ? context.release.changelogFile
		: QDir(context.root).absoluteFilePath(context.release.changelogFile);
	if (!loadProjectChangelog(changelogPath, context.release.title, &bundle->changelog, error)) {
		return false;
	}
	QVector<ReleaseRecordFile> inventory;
	if (!releaseInventory(plan, &inventory, error)) {
		return false;
	}
	if (const auto previous = latestReleaseRecord(context.root, plan.release.version)) {
		bundle->diff = diffReleaseInventories(*previous, inventory);
	}
	bundle->changes = bundle->changelog.unreleasedEntries();
	if (bundle->changes.isEmpty()) {
		bundle->changes = suggestedChangelogEntries(bundle->diff);
		bundle->additions = bundle->changes;
	}
	bundle->input = releaseNotesInput(plan, bundle->changes, bundle->diff, date);
	bundle->markdown = releaseNotesMarkdown(bundle->input);
	bundle->readme = releaseReadmeText(bundle->input);
	return true;
}

QJsonArray changesJson(const QVector<ChangelogEntry>& changes)
{
	QJsonArray array;
	for (const ChangelogEntry& entry : changes) {
		array.append(QJsonObject {{QStringLiteral("category"), entry.category}, {QStringLiteral("text"), entry.text}});
	}
	return array;
}

QJsonObject diffJson(const ReleaseInventoryDiff& diff)
{
	const auto paths = [](const QVector<ReleaseRecordFile>& files) {
		QJsonArray array;
		for (const ReleaseRecordFile& file : files) {
			array.append(file.path);
		}
		return array;
	};
	return {
		{QStringLiteral("hasPrevious"), diff.hasPrevious},
		{QStringLiteral("previousVersion"), diff.previousVersion},
		{QStringLiteral("added"), paths(diff.added)},
		{QStringLiteral("changed"), paths(diff.changed)},
		{QStringLiteral("removed"), paths(diff.removed)},
		{QStringLiteral("unchanged"), diff.unchanged},
		{QStringLiteral("summary"), jsonStrings(releaseDiffSummaryLines(diff))},
	};
}

bool parseDate(const Arguments& args, QDate* date, ReleaseCliResult* error)
{
	*date = QDate::currentDate();
	if (args.has(QStringLiteral("--date"))) {
		*date = QDate::fromString(args.value(QStringLiteral("--date")), Qt::ISODate);
		if (!date->isValid()) {
			*error = failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--date must be a date such as 2026-10-08."));
			return false;
		}
	}
	return true;
}

ReleaseCliResult runPlan(const Arguments& args, const QString& action)
{
	ProjectContext context;
	ReleaseCliResult result;
	if (!loadProjectContext(args, args.positional.value(2), &context, &result)) {
		return result;
	}
	PlannedRelease planned;
	if (!planFromArguments(args, context, &planned, &result)) {
		return result;
	}
	const ReleasePlan& plan = planned.plan;
	result.payload = contextJson(context, &planned);
	result.payload.insert(QStringLiteral("plan"), releasePlanJson(plan));
	result.lines << releasePlanText(plan);
	if (action == QStringLiteral("plan")) {
		result.exitCode = plan.canPublish() ? 0 : 4;
		return result;
	}

	QDate date;
	if (!parseDate(args, &date, &result)) {
		return result;
	}
	NotesBundle notes;
	QString error;
	if (plan.canPublish() && !buildNotes(plan, context, date, &notes, &error)) {
		return failure(1, error);
	}
	if (action == QStringLiteral("notes")) {
		if (!plan.canPublish()) {
			result.exitCode = 4;
			return result;
		}
		result.payload.insert(QStringLiteral("notes"), notes.markdown);
		result.payload.insert(QStringLiteral("readme"), notes.readme);
		result.payload.insert(QStringLiteral("changes"), changesJson(notes.changes));
		result.payload.insert(QStringLiteral("compared"), diffJson(notes.diff));
		result.lines = QStringList {args.has(QStringLiteral("--readme")) ? notes.readme : notes.markdown};
		return result;
	}

	// publish
	if (!plan.canPublish()) {
		result.exitCode = 4;
		result.error = QCoreApplication::translate("VibeStudioReleaseCli", "The release has %1 blocking problem(s); nothing was written.").arg(plan.blockingCount());
		return result;
	}
	const QString recordPath = releaseRecordPath(context.root, plan.release.version);
	if (!args.has(QStringLiteral("--overwrite")) && !recordPath.isEmpty() && QFileInfo::exists(recordPath)) {
		return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Version %1 has already been released. Pass --release-version %2 for the next release, or --overwrite to replace it.")
			.arg(plan.release.version, bumpReleaseVersion(plan.release.version, QStringLiteral("patch"))));
	}
	ReleasePublishRequest publish;
	publish.plan = plan;
	publish.projectRoot = context.root;
	publish.outputDirectory = args.has(QStringLiteral("--output")) ? QDir::cleanPath(QFileInfo(args.value(QStringLiteral("--output"))).absoluteFilePath())
		: defaultReleaseOutputDirectory(context.manifest, context.release);
	publish.readmeText = args.has(QStringLiteral("--no-readme")) ? QString() : notes.readme;
	publish.notesMarkdown = args.has(QStringLiteral("--no-notes")) ? QString() : notes.markdown;
	if (args.has(QStringLiteral("--notes-file"))) {
		QFile file(args.value(QStringLiteral("--notes-file")));
		if (!file.open(QIODevice::ReadOnly)) {
			return failure(3, QCoreApplication::translate("VibeStudioReleaseCli", "Unable to read %1.").arg(args.value(QStringLiteral("--notes-file"))));
		}
		publish.notesMarkdown = QString::fromUtf8(file.readAll());
	}
	publish.writeArchive = !args.has(QStringLiteral("--no-archive"));
	publish.writeRecord = !args.has(QStringLiteral("--no-record"));
	publish.updateChangelog = !args.has(QStringLiteral("--no-changelog"));
	publish.changelog = notes.changelog;
	publish.changelogAdditions = notes.additions;
	publish.releaseDate = date;
	publish.overwrite = args.has(QStringLiteral("--overwrite"));
	publish.dryRun = args.has(QStringLiteral("--dry-run"));
	if (args.has(QStringLiteral("--compression"))) {
		DeflateLevel level = DeflateLevel::Default;
		if (!deflateLevelFromId(args.value(QStringLiteral("--compression")), &level)) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--compression must be store, fast, default or best."));
		}
		publish.compression = level;
	}
	const ReleasePublishResult published = publishRelease(publish);
	result.payload.insert(QStringLiteral("publication"), releasePublishResultJson(published));
	result.lines << releasePublishResultText(published);
	if (!published.succeeded) {
		result.exitCode = published.packageCommitted ? 1 : 4;
		result.error = published.error;
	}
	return result;
}

ReleaseCliResult runChangelog(const Arguments& args)
{
	ProjectContext context;
	ReleaseCliResult result;
	if (!loadProjectContext(args, args.positional.value(2), &context, &result)) {
		return result;
	}
	const QString path = QDir::isAbsolutePath(context.release.changelogFile) ? context.release.changelogFile : QDir(context.root).absoluteFilePath(context.release.changelogFile);
	ProjectChangelog changelog;
	QString error;
	if (!loadProjectChangelog(path, context.release.title, &changelog, &error)) {
		return failure(1, error);
	}
	bool added = false;
	if (args.has(QStringLiteral("--add"))) {
		const QString category = args.has(QStringLiteral("--category")) ? args.value(QStringLiteral("--category")) : QStringLiteral("Changed");
		if (!addChangelogEntry(&changelog, category, args.value(QStringLiteral("--add")), &error)) {
			return failure(2, error);
		}
		if (!args.has(QStringLiteral("--dry-run")) && !saveProjectChangelog(changelog, &error)) {
			return failure(1, error);
		}
		added = true;
	} else if (args.has(QStringLiteral("--category"))) {
		return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--category goes with --add."));
	}
	const QVector<ChangelogEntry> unreleased = changelog.unreleasedEntries();
	result.payload = {
		{QStringLiteral("changelog"), QDir::toNativeSeparators(path)},
		{QStringLiteral("exists"), QFileInfo::exists(path)},
		{QStringLiteral("added"), added},
		{QStringLiteral("dryRun"), args.has(QStringLiteral("--dry-run"))},
		{QStringLiteral("unreleased"), changesJson(unreleased)},
		{QStringLiteral("versions"), jsonStrings(changelog.versions())},
	};
	result.lines << QDir::toNativeSeparators(path);
	if (unreleased.isEmpty()) {
		result.lines << QCoreApplication::translate("VibeStudioReleaseCli", "No unreleased changes recorded yet.");
	}
	for (const ChangelogEntry& entry : unreleased) {
		result.lines << QStringLiteral("- [%1] %2").arg(changelogCategoryDisplayName(entry.category), entry.text);
	}
	if (!changelog.versions().isEmpty()) {
		result.lines << QCoreApplication::translate("VibeStudioReleaseCli", "Released: %1").arg(changelog.versions().join(QStringLiteral(", ")));
	}
	return result;
}

ReleaseCliResult runHistory(const Arguments& args)
{
	ProjectContext context;
	ReleaseCliResult result;
	if (!loadProjectContext(args, args.positional.value(2), &context, &result)) {
		return result;
	}
	QStringList warnings;
	const QVector<ReleaseRecord> records = listReleaseRecords(context.root, &warnings);
	QJsonArray releases;
	for (const ReleaseRecord& record : records) {
		releases.append(QJsonObject {
			{QStringLiteral("version"), record.version},
			{QStringLiteral("title"), record.title},
			{QStringLiteral("publishedUtc"), record.publishedUtc.toString(Qt::ISODate)},
			{QStringLiteral("package"), record.packageFileName},
			{QStringLiteral("packageSha256"), record.packageSha256},
			{QStringLiteral("packageBytes"), double(record.packageBytes)},
			{QStringLiteral("files"), int(record.files.size())},
			{QStringLiteral("maps"), jsonStrings(record.maps)},
			{QStringLiteral("record"), QDir::toNativeSeparators(record.recordPath)},
		});
		result.lines << QStringLiteral("%1  %2  %3  %4").arg(record.version, record.publishedUtc.toLocalTime().toString(Qt::ISODate), record.packageFileName,
			QCoreApplication::translate("VibeStudioReleaseCli", "%1 file(s)").arg(record.files.size()));
	}
	if (records.isEmpty()) {
		result.lines << QCoreApplication::translate("VibeStudioReleaseCli", "No releases have been published from this project yet.");
	}
	result.lines += warnings;
	result.payload = {{QStringLiteral("project"), QDir::toNativeSeparators(context.root)}, {QStringLiteral("releases"), releases}, {QStringLiteral("warnings"), jsonStrings(warnings)}};
	return result;
}

ReleaseCliResult runCatalog(const Arguments& args)
{
	ProjectContext context;
	ReleaseCliResult result;
	if (!loadProjectContext(args, args.positional.value(2), &context, &result)) {
		return result;
	}
	const ReleaseCatalog catalog = releaseCatalog(context.manifest, context.gameKey);
	QJsonArray maps;
	for (const ReleaseMapInfo& map : catalog.maps) {
		maps.append(QJsonObject {
			{QStringLiteral("name"), map.name},
			{QStringLiteral("source"), QDir::toNativeSeparators(map.sourcePath)},
			{QStringLiteral("compiled"), QDir::toNativeSeparators(map.compiledPath)},
			{QStringLiteral("built"), map.built},
			{QStringLiteral("stale"), map.stale},
			{QStringLiteral("doomMaps"), jsonStrings(map.doomMaps)},
		});
		result.lines << QCoreApplication::translate("VibeStudioReleaseCli", "map      %1  %2").arg(map.name,
			!map.built ? QCoreApplication::translate("VibeStudioReleaseCli", "not built") : map.stale ? QCoreApplication::translate("VibeStudioReleaseCli", "out of date")
			: QCoreApplication::translate("VibeStudioReleaseCli", "built"));
	}
	for (const QString& model : catalog.models) {
		result.lines << QCoreApplication::translate("VibeStudioReleaseCli", "model    %1").arg(QDir::toNativeSeparators(model));
	}
	for (const QString& folder : catalog.textureFolders) {
		result.lines << QCoreApplication::translate("VibeStudioReleaseCli", "textures %1").arg(QDir::toNativeSeparators(folder));
	}
	result.lines += catalog.warnings;
	QJsonArray models;
	for (const QString& model : catalog.models) {
		models.append(QDir::toNativeSeparators(model));
	}
	QJsonArray folders;
	for (const QString& folder : catalog.textureFolders) {
		folders.append(QDir::toNativeSeparators(folder));
	}
	result.payload = contextJson(context, nullptr);
	result.payload.insert(QStringLiteral("maps"), maps);
	result.payload.insert(QStringLiteral("models"), models);
	result.payload.insert(QStringLiteral("textureFolders"), folders);
	result.payload.insert(QStringLiteral("warnings"), jsonStrings(catalog.warnings));
	return result;
}

} // namespace

ReleaseCliResult runReleaseCommand(const QStringList& arguments)
{
	static const QSet<QString> flags {
		QStringLiteral("--no-stock"), QStringLiteral("--include-sources"), QStringLiteral("--no-readme"), QStringLiteral("--no-notes"),
		QStringLiteral("--no-archive"), QStringLiteral("--no-record"), QStringLiteral("--no-changelog"), QStringLiteral("--overwrite"),
		QStringLiteral("--dry-run"), QStringLiteral("--readme"),
	};
	static const QSet<QString> options {
		QStringLiteral("--scope"), QStringLiteral("--map"), QStringLiteral("--model"), QStringLiteral("--texture"), QStringLiteral("--format"),
		QStringLiteral("--installation"), QStringLiteral("--register"), QStringLiteral("--game"), QStringLiteral("--release-version"), QStringLiteral("--title"),
		QStringLiteral("--package-name"), QStringLiteral("--output"), QStringLiteral("--notes-file"), QStringLiteral("--compression"), QStringLiteral("--date"),
		QStringLiteral("--add"), QStringLiteral("--category"),
	};
	static const QSet<QString> repeatable {QStringLiteral("--map"), QStringLiteral("--model"), QStringLiteral("--texture")};
	const Arguments args = parseArguments(arguments, flags, options, repeatable);
	if (!args.error.isEmpty()) {
		return failure(2, args.error);
	}
	const QString action = args.positional.value(1).toLower();
	if (args.positional.size() > 3) {
		return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Expected at most one project folder."));
	}
	// Options that only one action reads are refused elsewhere.
	const QSet<QString> publishOnly {QStringLiteral("--output"), QStringLiteral("--notes-file"), QStringLiteral("--compression"), QStringLiteral("--no-readme"),
		QStringLiteral("--no-notes"), QStringLiteral("--no-archive"), QStringLiteral("--no-record"), QStringLiteral("--no-changelog"), QStringLiteral("--overwrite")};
	const QSet<QString> changelogOnly {QStringLiteral("--add"), QStringLiteral("--category")};
	for (const QString& key : publishOnly) {
		if (args.has(key) && action != QStringLiteral("publish")) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "%1 applies only to release publish.").arg(key));
		}
	}
	for (const QString& key : changelogOnly) {
		if (args.has(key) && action != QStringLiteral("changelog")) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "%1 applies only to release changelog.").arg(key));
		}
	}
	if (args.has(QStringLiteral("--readme")) && action != QStringLiteral("notes")) {
		return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--readme applies only to release notes."));
	}
	if (args.has(QStringLiteral("--dry-run")) && action != QStringLiteral("publish") && action != QStringLiteral("changelog")) {
		return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--dry-run applies to release publish and release changelog."));
	}
	if (action == QStringLiteral("plan") || action == QStringLiteral("publish") || action == QStringLiteral("notes")) {
		return runPlan(args, action);
	}
	if (action == QStringLiteral("changelog")) {
		return runChangelog(args);
	}
	if (action == QStringLiteral("history")) {
		return runHistory(args);
	}
	if (action == QStringLiteral("catalog")) {
		return runCatalog(args);
	}
	return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Expected release plan, publish, notes, changelog, history or catalog."));
}

ReleaseCliResult runInstallRegisterCommand(const QStringList& arguments)
{
	static const QSet<QString> flags {QStringLiteral("--dry-run")};
	static const QSet<QString> options {QStringLiteral("--package"), QStringLiteral("--output"), QStringLiteral("--file"), QStringLiteral("--as")};
	static const QSet<QString> repeatable {QStringLiteral("--package")};
	const Arguments args = parseArguments(arguments, flags, options, repeatable);
	if (!args.error.isEmpty()) {
		return failure(2, args.error);
	}
	// positional: install register <action> <id> [paths...]
	const QString action = args.positional.value(2).toLower();
	const QString id = args.positional.value(3);
	if (action.isEmpty() || id.isEmpty()) {
		return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Expected install register build|info|check|export <installation-id>."));
	}
	const StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
	GameInstallationProfile installation;
	if (!findInstallation(settings, id, &installation)) {
		return failure(3, QCoreApplication::translate("VibeStudioReleaseCli", "No saved installation has the id %1. List them with: vibestudio --cli install list").arg(id));
	}
	const auto refuse = [&args](const QString& key, const QString& owner) -> std::optional<ReleaseCliResult> {
		if (args.has(key)) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "%1 applies only to install register %2.").arg(key, owner));
		}
		return std::nullopt;
	};
	ReleaseCliResult result;
	if (action == QStringLiteral("build")) {
		for (const QString& key : {QStringLiteral("--output"), QStringLiteral("--file"), QStringLiteral("--as")}) {
			if (auto refused = refuse(key, QStringLiteral("check or export"))) {
				return *refused;
			}
		}
		if (args.positional.size() > 4) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "install register build takes one installation id."));
		}
		GameAssetRegisterBuildRequest request;
		request.installation = installation;
		request.packagePaths = args.all(QStringLiteral("--package"));
		const GameAssetRegisterBuildResult built = buildGameAssetRegister(request);
		if (!built.succeeded) {
			return failure(built.cancelled ? 1 : 4, built.error);
		}
		const QString path = gameAssetRegisterPath(installation.id);
		QString error;
		if (!args.has(QStringLiteral("--dry-run")) && !saveGameAssetRegister(built.registerData, path, &error)) {
			return failure(1, error);
		}
		result.payload = {
			{QStringLiteral("installation"), installation.id},
			{QStringLiteral("register"), gameAssetRegisterSummaryJson(built.registerData)},
			{QStringLiteral("path"), QDir::toNativeSeparators(path)},
			{QStringLiteral("written"), !args.has(QStringLiteral("--dry-run"))},
			{QStringLiteral("dryRun"), args.has(QStringLiteral("--dry-run"))},
			{QStringLiteral("bytesRead"), double(built.bytesRead)},
			{QStringLiteral("warnings"), jsonStrings(built.warnings)},
		};
		result.lines << gameAssetRegisterSummaryText(built.registerData);
		result.lines << (args.has(QStringLiteral("--dry-run")) ? QCoreApplication::translate("VibeStudioReleaseCli", "Dry run: the index was not saved.")
															   : QCoreApplication::translate("VibeStudioReleaseCli", "Saved %1").arg(QDir::toNativeSeparators(path)));
		result.lines += built.warnings;
		return result;
	}
	if (args.has(QStringLiteral("--package"))) {
		return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--package applies only to install register build."));
	}
	if (args.has(QStringLiteral("--dry-run"))) {
		return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--dry-run applies only to install register build."));
	}
	GameAssetRegister loaded;
	const GameAssetRegisterStatus status = gameAssetRegisterStatus(installation, &loaded);
	if (action == QStringLiteral("info")) {
		if (args.positional.size() > 4 || args.has(QStringLiteral("--output")) || args.has(QStringLiteral("--file")) || args.has(QStringLiteral("--as"))) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "install register info takes only an installation id."));
		}
		result.payload = {{QStringLiteral("installation"), installation.id}, {QStringLiteral("status"), gameAssetRegisterStatusJson(status)}};
		if (status.loaded) {
			result.payload.insert(QStringLiteral("register"), gameAssetRegisterSummaryJson(loaded));
			result.lines << gameAssetRegisterSummaryText(loaded);
			result.lines << (status.fresh ? QCoreApplication::translate("VibeStudioReleaseCli", "Up to date.") : QCoreApplication::translate("VibeStudioReleaseCli", "Out of date:"));
			result.lines += status.staleReasons;
		} else {
			result.lines << (status.exists ? status.error : QCoreApplication::translate("VibeStudioReleaseCli", "Not indexed yet. Run: vibestudio --cli install register build %1").arg(installation.id));
		}
		// A missing or stale index is a finding, like a failed validation.
		result.exitCode = status.usable() ? 0 : 4;
		return result;
	}
	if (!status.loaded) {
		return failure(4, status.exists ? status.error : QCoreApplication::translate("VibeStudioReleaseCli", "Not indexed yet. Run: vibestudio --cli install register build %1").arg(installation.id));
	}
	if (action == QStringLiteral("check")) {
		if (args.has(QStringLiteral("--output"))) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--output applies only to install register export."));
		}
		QStringList paths = args.positional.mid(4);
		const bool compareFile = args.has(QStringLiteral("--file"));
		if (compareFile) {
			if (!args.has(QStringLiteral("--as")) || !paths.isEmpty()) {
				return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--file needs --as <game path> and no other paths."));
			}
			paths = {args.value(QStringLiteral("--as"))};
		} else if (args.has(QStringLiteral("--as"))) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "--as goes with --file."));
		}
		if (paths.isEmpty()) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Name at least one game path, such as textures/base_wall/basewall01.tga."));
		}
		QJsonArray checks;
		for (const QString& path : std::as_const(paths)) {
			const GameAssetRegisterFile* file = loaded.file(path);
			// A map can name a shader that has no file of its own.
			const GameAssetRegisterFile* shader = file ? nullptr : loaded.shaderScript(path);
			QJsonObject check {{QStringLiteral("path"), path}, {QStringLiteral("stock"), file != nullptr || shader != nullptr}};
			QString line = file ? QCoreApplication::translate("VibeStudioReleaseCli", "%1: in %2").arg(path, loaded.sourceLabel(file->source))
				: shader ? QCoreApplication::translate("VibeStudioReleaseCli", "%1: a shader declared by %2 in %3").arg(path, shader->path, loaded.sourceLabel(shader->source))
						 : QCoreApplication::translate("VibeStudioReleaseCli", "%1: not a stock file").arg(path);
			if (shader) {
				check.insert(QStringLiteral("shaderScript"), shader->path);
				check.insert(QStringLiteral("source"), loaded.sourceLabel(shader->source));
			}
			if (file) {
				check.insert(QStringLiteral("source"), loaded.sourceLabel(file->source));
				check.insert(QStringLiteral("bytes"), double(file->sizeBytes));
				check.insert(QStringLiteral("crc32"), QStringLiteral("%1").arg(file->crc32, 8, 16, QLatin1Char('0')));
			}
			if (compareFile) {
				quint32 crc = 0;
				QString error;
				const QString local = args.value(QStringLiteral("--file"));
				if (!fileCrc32(local, &crc, &error)) {
					return failure(3, error);
				}
				const GameAssetMatch match = loaded.match(path, quint64(QFileInfo(local).size()), crc);
				check.insert(QStringLiteral("match"), gameAssetMatchId(match));
				line += QStringLiteral(" (%1)").arg(match == GameAssetMatch::Identical ? QCoreApplication::translate("VibeStudioReleaseCli", "your copy is identical")
					: match == GameAssetMatch::Different ? QCoreApplication::translate("VibeStudioReleaseCli", "your copy differs and would replace it")
														 : QCoreApplication::translate("VibeStudioReleaseCli", "yours is new"));
			}
			checks.append(check);
			result.lines << line;
		}
		result.payload = {{QStringLiteral("installation"), installation.id}, {QStringLiteral("checks"), checks}, {QStringLiteral("fresh"), status.fresh}};
		return result;
	}
	if (action == QStringLiteral("export")) {
		if (args.has(QStringLiteral("--file")) || args.has(QStringLiteral("--as")) || args.positional.size() > 4) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "install register export takes an installation id and --output."));
		}
		const QString output = args.value(QStringLiteral("--output"));
		if (output.isEmpty()) {
			return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Name the file to write with --output."));
		}
		QString error;
		const QString path = QFileInfo(output).absoluteFilePath();
		if (!saveGameAssetRegister(loaded, path, &error)) {
			return failure(1, error);
		}
		result.payload = {{QStringLiteral("installation"), installation.id}, {QStringLiteral("output"), QDir::toNativeSeparators(path)}, {QStringLiteral("fresh"), status.fresh}};
		result.lines << QCoreApplication::translate("VibeStudioReleaseCli", "Wrote %1. Pass it to release commands with --register on machines without the game.").arg(QDir::toNativeSeparators(path));
		return result;
	}
	return failure(2, QCoreApplication::translate("VibeStudioReleaseCli", "Expected install register build|info|check|export <installation-id>."));
}

} // namespace vibestudio::cli
