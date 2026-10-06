#include "cli/ai_generation.h"

#include "core/ai_audio_transport.h"
#include "core/ai_image_transport.h"
#include "core/ai_transport.h"
#include "core/idtech_image.h"
#include "core/level_ai_edit.h"
#include "core/level_generation.h"
#include "core/level_map.h"
#include "core/package_archive.h"
#include "core/sound_generation.h"
#include "core/studio_settings.h"
#include "core/texture_generation.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QUrl>

#include <optional>

namespace vibestudio::cli {

namespace {

const QSet<QString> kGlobalOptions = {
	QStringLiteral("--cli"),
	QStringLiteral("--json"),
	QStringLiteral("--quiet"),
	QStringLiteral("--verbose"),
	QStringLiteral("--no-color"),
	QStringLiteral("--settings-file"),
	QStringLiteral("--locale"),
	QStringLiteral("--catalog-root"),
};
const QSet<QString> kGlobalValues = {QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
const QSet<QString> kAiSwitches = {QStringLiteral("--yes"), QStringLiteral("--dry-run")};
const QSet<QString> kAiValues = {QStringLiteral("--provider"), QStringLiteral("--model"), QStringLiteral("--endpoint"), QStringLiteral("--timeout-ms")};

AiGenerationCliResult failure(int code, const QString& message)
{
	AiGenerationCliResult result;
	result.exitCode = code;
	result.error = message;
	return result;
}

// Options as `--name value` or `--name=value`; repeatable ones collect.
class Options {
public:
	bool parse(const QStringList& arguments, const QSet<QString>& switches, const QSet<QString>& values, QString* error)
	{
		for (qsizetype index = 1; index < arguments.size(); ++index) {
			const QString argument = arguments[index];
			if (!argument.startsWith(QStringLiteral("--"))) {
				m_positional << argument;
				continue;
			}
			const qsizetype equals = argument.indexOf(QLatin1Char('='));
			const QString key = equals > 0 ? argument.left(equals) : argument;
			if (switches.contains(key) || (kGlobalOptions.contains(key) && !kGlobalValues.contains(key))) {
				m_switches.insert(key);
				continue;
			}
			if (!values.contains(key) && !kGlobalValues.contains(key)) {
				*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "Unexpected option: %1").arg(argument);
				return false;
			}
			QString value;
			if (equals > 0) {
				value = argument.mid(equals + 1);
			} else if (index + 1 < arguments.size()) {
				value = arguments[++index];
			} else {
				*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "Missing value for %1.").arg(key);
				return false;
			}
			m_values[key] << value;
		}
		return true;
	}
	[[nodiscard]] bool has(const QString& key) const
	{
		return m_switches.contains(key) || m_values.contains(key);
	}
	[[nodiscard]] QString value(const QString& key, const QString& fallback = QString()) const
	{
		return m_values.contains(key) ? m_values.value(key).last() : fallback;
	}
	[[nodiscard]] QStringList values(const QString& key) const
	{
		return m_values.value(key);
	}
	[[nodiscard]] QStringList positional() const
	{
		return m_positional;
	}

private:
	QSet<QString> m_switches;
	QHash<QString, QStringList> m_values;
	QStringList m_positional;
};

bool readInteger(const Options& options, const QString& key, int low, int high, int* out, QString* error)
{
	if (!options.has(key)) {
		return true;
	}
	bool ok = false;
	const int value = options.value(key).toInt(&ok);
	if (!ok || value < low || value > high) {
		*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 takes a whole number from %2 to %3.").arg(key).arg(low).arg(high);
		return false;
	}
	*out = value;
	return true;
}

bool readSize(const QString& text, QSize* out)
{
	const QStringList parts = text.toLower().split(QLatin1Char('x'));
	bool okWidth = false;
	bool okHeight = false;
	const int width = parts.value(0).toInt(&okWidth);
	const int height = parts.size() > 1 ? parts.value(1).toInt(&okHeight) : width;
	if (parts.size() == 1) {
		okHeight = okWidth;
	}
	if (!okWidth || !okHeight || width < 8 || height < 8 || width > 4096 || height > 4096) {
		return false;
	}
	*out = QSize(width, height);
	return true;
}

bool writeFile(const QString& path, const QByteArray& bytes, bool overwrite, QString* error)
{
	if (QFileInfo::exists(path) && !overwrite) {
		*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 exists; pass --overwrite to replace it.").arg(QDir::toNativeSeparators(path));
		return false;
	}
	if (!QFileInfo(path).absolutePath().isEmpty()) {
		QDir().mkpath(QFileInfo(path).absolutePath());
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		return false;
	}
	return true;
}

QByteArray pngBytes(const QImage& image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}

AiAutomationPreferences preferencesFor(const Options& options, bool image)
{
	AiAutomationPreferences preferences = StudioSettings().aiAutomationPreferences();
	const QString provider = normalizedAiId(options.value(QStringLiteral("--provider")));
	QString connector = provider;
	if (connector.isEmpty()) {
		connector = image ? resolveAiImageConnection(preferences).connectorId : resolveAiTextConnection(preferences).connectorId;
	}
	const QString model = options.value(QStringLiteral("--model")).trimmed();
	const QString endpoint = options.value(QStringLiteral("--endpoint")).trimmed();
	if (!connector.isEmpty() && !model.isEmpty()) {
		(image ? preferences.connectorImageModels : preferences.connectorModels).insert(connector, model);
	}
	if (!connector.isEmpty() && !endpoint.isEmpty()) {
		(image ? preferences.connectorImageEndpoints : preferences.connectorEndpoints).insert(connector, endpoint);
	}
	return preferences;
}

int timeoutFor(const Options& options)
{
	bool ok = false;
	const int value = options.value(QStringLiteral("--timeout-ms")).toInt(&ok);
	return ok && value >= 1000 ? value : 5 * 60 * 1000;
}

AiChatResponse sendChat(const AiChatRequest& request, const QString& apiKey, int timeoutMsecs)
{
	AiChatClient client;
	client.setTimeoutMsecs(timeoutMsecs);
	QEventLoop loop;
	std::optional<AiChatResponse> answer;
	QString error;
	if (!client.send(request, apiKey, [&](const AiChatResponse& response) { answer = response; loop.quit(); }, &error)) {
		AiChatResponse failed;
		failed.failure = AiChatFailure::NotConfigured;
		failed.errorMessage = error;
		return failed;
	}
	if (!answer) {
		loop.exec();
	}
	return *answer;
}

AiImageResponse sendImage(const AiImageRequest& request, const QString& apiKey, int timeoutMsecs)
{
	AiImageClient client;
	client.setTimeoutMsecs(timeoutMsecs);
	QEventLoop loop;
	std::optional<AiImageResponse> answer;
	QString error;
	if (!client.send(request, apiKey, [&](const AiImageResponse& response) { answer = response; loop.quit(); }, &error)) {
		AiImageResponse failed;
		failed.failure = AiChatFailure::NotConfigured;
		failed.errorMessage = error;
		return failed;
	}
	if (!answer) {
		loop.exec();
	}
	return *answer;
}

AiSoundResponse sendSound(const AiSoundRequest& request, const QString& apiKey, int timeoutMsecs)
{
	AiSoundClient client;
	client.setTimeoutMsecs(timeoutMsecs);
	QEventLoop loop;
	std::optional<AiSoundResponse> answer;
	QString error;
	if (!client.send(request, apiKey, [&](const AiSoundResponse& response) { answer = response; loop.quit(); }, &error)) {
		AiSoundResponse failed;
		failed.failure = AiChatFailure::NotConfigured;
		failed.errorMessage = error;
		return failed;
	}
	if (!answer) {
		loop.exec();
	}
	return *answer;
}

// The settings, with --model and --endpoint applied to the sound connector.
AiAutomationPreferences soundPreferencesFor(const Options& options)
{
	AiAutomationPreferences preferences = StudioSettings().aiAutomationPreferences();
	QString connector = normalizedAiId(options.value(QStringLiteral("--provider")));
	if (connector.isEmpty()) {
		connector = resolveAiSoundConnection(preferences).connectorId;
	}
	const QString model = options.value(QStringLiteral("--model")).trimmed();
	const QString endpoint = options.value(QStringLiteral("--endpoint")).trimmed();
	if (!connector.isEmpty() && !model.isEmpty()) {
		preferences.connectorAudioModels.insert(connector, model);
	}
	if (!connector.isEmpty() && !endpoint.isEmpty()) {
		preferences.connectorAudioEndpoints.insert(connector, endpoint);
	}
	return preferences;
}

QJsonObject usageJson(const QString& connector, const QString& model, const QString& endpoint, int inputTokens, int outputTokens, qint64 elapsed)
{
	return QJsonObject {
		{QStringLiteral("connector"), connector},
		{QStringLiteral("model"), model},
		{QStringLiteral("host"), QUrl(endpoint).host()},
		{QStringLiteral("inputTokens"), inputTokens},
		{QStringLiteral("outputTokens"), outputTokens},
		{QStringLiteral("elapsedMs"), double(elapsed)},
	};
}

// What stops a send: a blocked connection, or an endpoint off this machine
// without --yes. Empty when it may go.
QString sendBlock(bool ready, const QString& blockText, bool local, const QString& endpoint, const Options& options)
{
	if (!ready) {
		return blockText;
	}
	if (!local && !options.has(QStringLiteral("--yes"))) {
		return QCoreApplication::translate("VibeStudioAiGenerationCli",
			"This sends the request to %1, off this machine. Review it with --dry-run, then pass --yes to send.").arg(QUrl(endpoint).host());
	}
	return {};
}

// Texture names a package or folder offers, as maps name them.
QStringList textureNamesIn(const QString& path, QString* error)
{
	QStringList names;
	PackageArchive archive;
	if (!archive.load(path, error)) {
		return names;
	}
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		if (entry.typeHint == QStringLiteral("wad-texture") || entry.typeHint == QStringLiteral("wad-flat")) {
			names << entry.virtualPath;
			continue;
		}
		const QString lower = entry.virtualPath.toLower();
		if (lower.startsWith(QStringLiteral("textures/"))) {
			const QFileInfo info(entry.virtualPath.mid(9));
			static const QStringList images = {QStringLiteral("wal"), QStringLiteral("tga"), QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg")};
			if (images.contains(info.suffix().toLower())) {
				const QString folder = info.path() == QStringLiteral(".") ? QString() : info.path() + QLatin1Char('/');
				names << folder + info.completeBaseName();
			}
		}
	}
	names.removeDuplicates();
	return names;
}

const QSet<QString> kMapSpecValues = {
	QStringLiteral("--prompt"),
	QStringLiteral("--game"),
	QStringLiteral("--mode"),
	QStringLiteral("--theme"),
	QStringLiteral("--rooms"),
	QStringLiteral("--size"),
	QStringLiteral("--verticality"),
	QStringLiteral("--players"),
	QStringLiteral("--monsters"),
	QStringLiteral("--liquid"),
	QStringLiteral("--title"),
	QStringLiteral("--seed"),
	QStringLiteral("--wad"),
	QStringLiteral("--texture"),
	QStringLiteral("--textures-from"),
	QStringLiteral("--planner"),
	QStringLiteral("--plan"),
	QStringLiteral("--revisions"),
};

bool levelSpecFrom(const Options& options, LevelGenerationSpec* spec, QString* error)
{
	spec->prompt = options.value(QStringLiteral("--prompt"));
	spec->game = options.value(QStringLiteral("--game"));
	spec->mode = options.value(QStringLiteral("--mode"));
	spec->theme = options.value(QStringLiteral("--theme"));
	spec->size = options.value(QStringLiteral("--size"));
	spec->verticality = options.value(QStringLiteral("--verticality"));
	spec->monsters = options.value(QStringLiteral("--monsters"));
	spec->liquid = options.value(QStringLiteral("--liquid"));
	spec->title = options.value(QStringLiteral("--title"));
	spec->wad = options.value(QStringLiteral("--wad"));
	const auto checkChoice = [&](const QString& key, const QString& value, const QStringList& allowed) {
		if (!value.isEmpty() && !allowed.contains(value)) {
			*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 takes one of: %2.").arg(key, allowed.join(QStringLiteral(", ")));
			return false;
		}
		return true;
	};
	if (!checkChoice(QStringLiteral("--game"), spec->game, levelGenerationGameIds()) || !checkChoice(QStringLiteral("--mode"), spec->mode, levelGenerationModeIds())
		|| !checkChoice(QStringLiteral("--theme"), spec->theme, levelGenerationThemeIds())
		|| !checkChoice(QStringLiteral("--size"), spec->size, {QStringLiteral("small"), QStringLiteral("medium"), QStringLiteral("large")})
		|| !checkChoice(QStringLiteral("--verticality"), spec->verticality, {QStringLiteral("flat"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")})
		|| !checkChoice(QStringLiteral("--monsters"), spec->monsters, {QStringLiteral("none"), QStringLiteral("light"), QStringLiteral("normal"), QStringLiteral("heavy")})
		|| !checkChoice(QStringLiteral("--liquid"), spec->liquid, {QStringLiteral("none"), QStringLiteral("water"), QStringLiteral("slime"), QStringLiteral("lava")})) {
		return false;
	}
	if (!readInteger(options, QStringLiteral("--rooms"), 2, 16, &spec->rooms, error) || !readInteger(options, QStringLiteral("--players"), 1, 32, &spec->players, error)) {
		return false;
	}
	if (options.has(QStringLiteral("--seed"))) {
		bool ok = false;
		spec->seed = options.value(QStringLiteral("--seed")).toLongLong(&ok);
		if (!ok || spec->seed < 0) {
			*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "--seed takes a whole number of 0 or more.");
			return false;
		}
	}
	for (const QString& pair : options.values(QStringLiteral("--texture"))) {
		const qsizetype equals = pair.indexOf(QLatin1Char('='));
		static const QStringList roles = {QStringLiteral("wall"), QStringLiteral("floor"), QStringLiteral("ceiling"), QStringLiteral("trim"), QStringLiteral("liquid")};
		if (equals <= 0 || !roles.contains(pair.left(equals)) || pair.mid(equals + 1).trimmed().isEmpty()) {
			*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "--texture takes role=name, with role one of: %1.").arg(roles.join(QStringLiteral(", ")));
			return false;
		}
		spec->textures.insert(pair.left(equals), pair.mid(equals + 1).trimmed());
	}
	for (const QString& path : options.values(QStringLiteral("--textures-from"))) {
		QString loadError;
		const QStringList names = textureNamesIn(path, &loadError);
		if (!loadError.isEmpty()) {
			*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read textures from %1: %2").arg(QDir::toNativeSeparators(path), loadError);
			return false;
		}
		spec->availableTextures += names;
	}
	return true;
}

struct PlanOutcome {
	bool ok = false;
	int exitCode = 0;
	QString error;
	LevelSemanticPlan plan;
	bool fromModel = false;
	bool dryRun = false;
	QStringList lines;
	QJsonObject ai;
	QStringList warnings;
};

// The plan a command builds from: a file, a text model's, or the rules'.
PlanOutcome planFor(const Options& options, const LevelGenerationSpec& spec)
{
	PlanOutcome outcome;
	const QString planner = options.value(QStringLiteral("--planner"), QStringLiteral("rules"));
	if (planner != QStringLiteral("rules") && planner != QStringLiteral("ai")) {
		outcome.exitCode = 2;
		outcome.error = QCoreApplication::translate("VibeStudioAiGenerationCli", "--planner takes rules or ai.");
		return outcome;
	}
	if (options.has(QStringLiteral("--plan"))) {
		QFile file(options.value(QStringLiteral("--plan")));
		if (!file.open(QIODevice::ReadOnly)) {
			outcome.exitCode = 3;
			outcome.error = QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read the plan %1.").arg(QDir::toNativeSeparators(file.fileName()));
			return outcome;
		}
		QStringList problems;
		if (!levelSemanticPlanFromAnswer(QString::fromUtf8(file.readAll()), spec, &outcome.plan, &problems)) {
			outcome.exitCode = 4;
			outcome.error = problems.join(QStringLiteral(" "));
			return outcome;
		}
		outcome.plan.planner = QStringLiteral("file:%1").arg(QFileInfo(file.fileName()).fileName());
		outcome.warnings += problems;
		outcome.ok = true;
		return outcome;
	}
	if (planner == QStringLiteral("rules")) {
		outcome.plan = rulesLevelPlan(spec);
		outcome.ok = true;
		return outcome;
	}

	// A text model's plan.
	const AiAutomationPreferences preferences = preferencesFor(options, false);
	const AiTextConnection connection = resolveAiTextConnection(preferences, options.value(QStringLiteral("--provider")));
	AiChatRequest request = levelPlanAiRequest(spec, connection.connectorId, connection.model, connection.endpoint);
	if (options.has(QStringLiteral("--dry-run"))) {
		AiHttpRequest http;
		QString error;
		outcome.dryRun = true;
		outcome.ok = true;
		outcome.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Dry run: nothing was sent. %1").arg(aiTextConnectionBlockText(connection));
		outcome.lines << (buildAiHttpRequest(request, QString(), &http, &error) ? describeAiHttpRequest(http) : error);
		return outcome;
	}
	const QString blocked = sendBlock(connection.ready(), aiTextConnectionBlockText(connection), connection.local, connection.endpoint, options);
	if (!blocked.isEmpty()) {
		outcome.exitCode = 5;
		outcome.error = blocked;
		return outcome;
	}
	int revisions = 1;
	QString revisionError;
	readInteger(options, QStringLiteral("--revisions"), 0, 3, &revisions, &revisionError);
	const QString key = aiTextConnectionApiKey(connection);
	int inputTokens = 0;
	int outputTokens = 0;
	qint64 elapsed = 0;
	QString previous;
	QStringList problems;
	for (int attempt = 0; attempt <= revisions; ++attempt) {
		if (attempt > 0) {
			request = levelPlanAiRequest(spec, connection.connectorId, connection.model, connection.endpoint, previous, problems);
		}
		const AiChatResponse response = sendChat(request, key, timeoutFor(options));
		inputTokens += std::max(0, response.inputTokens);
		outputTokens += std::max(0, response.outputTokens);
		elapsed += response.elapsedMsecs;
		if (!response.ok) {
			outcome.exitCode = 1;
			outcome.error = QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 %2").arg(aiChatFailureText(response.failure), response.errorMessage);
			return outcome;
		}
		previous = response.text;
		LevelSemanticPlan plan;
		const bool read = levelSemanticPlanFromAnswer(response.text, spec, &plan, &problems);
		if (read && problems.isEmpty()) {
			outcome.plan = plan;
			outcome.ok = true;
			break;
		}
		if (read && attempt == revisions) {
			// Good enough to repair: the rules make it buildable.
			outcome.plan = plan;
			outcome.ok = true;
			outcome.warnings += problems;
		}
	}
	outcome.ai = usageJson(connection.connectorId, connection.model, connection.endpoint, inputTokens, outputTokens, elapsed);
	if (!outcome.ok) {
		// The model's answers never held a plan; the rules make one instead.
		outcome.warnings << QCoreApplication::translate("VibeStudioAiGenerationCli", "The model's plan could not be used (%1); the rules planner made one instead.")
								 .arg(problems.join(QStringLiteral(" ")));
		outcome.plan = rulesLevelPlan(spec);
		outcome.ok = true;
		return outcome;
	}
	outcome.fromModel = true;
	outcome.plan.planner = QStringLiteral("ai:%1/%2").arg(connection.connectorId, connection.model);
	return outcome;
}

} // namespace

AiGenerationCliResult runMapPlan(const QStringList& arguments)
{
	Options options;
	QString error;
	QSet<QString> values = kMapSpecValues + kAiValues;
	values << QStringLiteral("--output");
	if (!options.parse(arguments, kAiSwitches + QSet<QString> {QStringLiteral("--overwrite")}, values, &error)) {
		return failure(2, error);
	}
	LevelGenerationSpec spec;
	if (!levelSpecFrom(options, &spec, &error)) {
		return failure(2, error);
	}
	if (spec.prompt.trimmed().isEmpty() && options.value(QStringLiteral("--planner")) == QStringLiteral("ai")) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "map plan --planner ai needs --prompt."));
	}
	spec = normalizedLevelGenerationSpec(spec);
	const PlanOutcome outcome = planFor(options, spec);
	if (!outcome.ok) {
		return failure(outcome.exitCode, outcome.error);
	}
	AiGenerationCliResult result;
	if (outcome.dryRun) {
		result.lines = outcome.lines;
		result.payload.insert(QStringLiteral("dryRun"), true);
		result.payload.insert(QStringLiteral("request"), outcome.lines.join(QLatin1Char('\n')));
		return result;
	}
	QJsonObject planJson = levelSemanticPlanJson(outcome.plan);
	planJson.insert(QStringLiteral("planner"), outcome.plan.planner);
	planJson.insert(QStringLiteral("repairs"), QJsonArray::fromStringList(outcome.plan.repairs));
	result.payload.insert(QStringLiteral("spec"), levelGenerationSpecJson(spec));
	result.payload.insert(QStringLiteral("plan"), planJson);
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(outcome.warnings));
	if (!outcome.ai.isEmpty()) {
		result.payload.insert(QStringLiteral("ai"), outcome.ai);
	}
	result.lines = levelSemanticPlanLines(outcome.plan) + outcome.warnings;
	if (options.has(QStringLiteral("--output"))) {
		const QString path = options.value(QStringLiteral("--output"));
		if (!writeFile(path, QJsonDocument(planJson).toJson(QJsonDocument::Indented), options.has(QStringLiteral("--overwrite")), &error)) {
			return failure(1, error);
		}
		result.payload.insert(QStringLiteral("output"), path);
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Plan written to %1.").arg(QDir::toNativeSeparators(path));
	}
	return result;
}

AiGenerationCliResult runMapGenerate(const QStringList& arguments)
{
	Options options;
	QString error;
	QSet<QString> values = kMapSpecValues + kAiValues;
	values << QStringLiteral("--output") << QStringLiteral("--save-plan") << QStringLiteral("--preview") << QStringLiteral("--report");
	if (!options.parse(arguments, kAiSwitches + QSet<QString> {QStringLiteral("--overwrite")}, values, &error)) {
		return failure(2, error);
	}
	if (!options.has(QStringLiteral("--output")) && !options.has(QStringLiteral("--dry-run")) && !options.has(QStringLiteral("--preview"))
		&& !options.has(QStringLiteral("--report"))) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli",
			"map generate needs --output <map or folder> (or --preview/--report to look first). Describe the level with --prompt; --planner ai asks your text model for the plan."));
	}
	LevelGenerationSpec spec;
	if (!levelSpecFrom(options, &spec, &error)) {
		return failure(2, error);
	}
	spec = normalizedLevelGenerationSpec(spec);
	const PlanOutcome outcome = planFor(options, spec);
	if (!outcome.ok) {
		return failure(outcome.exitCode, outcome.error);
	}
	AiGenerationCliResult result;
	if (outcome.dryRun) {
		result.lines = outcome.lines;
		result.payload.insert(QStringLiteral("dryRun"), true);
		result.payload.insert(QStringLiteral("request"), outcome.lines.join(QLatin1Char('\n')));
		return result;
	}
	const LevelGenerationResult level = generateLevel(spec, &outcome.plan);
	if (!level.ok) {
		return failure(4, level.error);
	}
	QJsonObject report = levelGenerationReportJson(level);
	if (!outcome.ai.isEmpty()) {
		report.insert(QStringLiteral("ai"), outcome.ai);
	}
	QStringList warnings = outcome.warnings + level.warnings;
	result.lines = levelGenerationSummaryLines(level);
	const bool overwrite = options.has(QStringLiteral("--overwrite"));
	QStringList written;
	if (options.has(QStringLiteral("--output"))) {
		QString path = options.value(QStringLiteral("--output"));
		if (QFileInfo(path).isDir() || path.endsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('\\'))) {
			path = QDir(path).filePath(level.suggestedFileName);
		}
		const QString wanted = level.format == QStringLiteral("doom-wad") ? QStringLiteral("wad") : QStringLiteral("map");
		if (QFileInfo(path).suffix().compare(wanted, Qt::CaseInsensitive) != 0) {
			return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "A %1 level is written as .%2.").arg(spec.game, wanted));
		}
		if (!writeFile(path, level.mapBytes, overwrite, &error)) {
			return failure(1, error);
		}
		written << path;
		report.insert(QStringLiteral("output"), path);
	}
	if (options.has(QStringLiteral("--save-plan"))) {
		QJsonObject planJson = levelSemanticPlanJson(level.plan);
		planJson.insert(QStringLiteral("planner"), level.plan.planner);
		if (!writeFile(options.value(QStringLiteral("--save-plan")), QJsonDocument(planJson).toJson(QJsonDocument::Indented), overwrite, &error)) {
			return failure(1, error);
		}
		written << options.value(QStringLiteral("--save-plan"));
	}
	if (options.has(QStringLiteral("--preview"))) {
		if (!writeFile(options.value(QStringLiteral("--preview")), pngBytes(renderLevelLayoutPreview(level, QSize(1024, 1024))), overwrite, &error)) {
			return failure(1, error);
		}
		written << options.value(QStringLiteral("--preview"));
	}
	if (options.has(QStringLiteral("--report"))) {
		if (!writeFile(options.value(QStringLiteral("--report")), QJsonDocument(report).toJson(QJsonDocument::Indented), overwrite, &error)) {
			return failure(1, error);
		}
		written << options.value(QStringLiteral("--report"));
	}
	for (const QString& note : level.notes) {
		result.lines << QStringLiteral("- %1").arg(note);
	}
	for (const QString& warning : warnings) {
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Warning: %1").arg(warning);
	}
	for (const QString& path : written) {
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Wrote %1").arg(QDir::toNativeSeparators(path));
	}
	report.insert(QStringLiteral("written"), QJsonArray::fromStringList(written));
	report.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
	result.payload = report;
	return result;
}

AiGenerationCliResult runMapAiEdit(const QStringList& arguments)
{
	Options options;
	QString error;
	QSet<QString> values = kAiValues;
	values << QStringLiteral("--input") << QStringLiteral("--prompt") << QStringLiteral("--proposal") << QStringLiteral("--save-proposal") << QStringLiteral("--output")
		   << QStringLiteral("--only") << QStringLiteral("--select");
	if (!options.parse(arguments, kAiSwitches + QSet<QString> {QStringLiteral("--overwrite")}, values, &error)) {
		return failure(2, error);
	}
	QString input = options.value(QStringLiteral("--input"));
	if (input.isEmpty() && options.positional().size() >= 3) {
		input = options.positional()[2];
	}
	const QString instruction = options.value(QStringLiteral("--prompt")).trimmed();
	if (input.isEmpty() || (instruction.isEmpty() && !options.has(QStringLiteral("--proposal")))) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli",
			"map ai-edit needs a .map and --prompt \"<what to change>\", or --proposal <file> to review a saved proposal."));
	}
	LevelMapDocument document;
	LevelMapLoadRequest load;
	load.path = input;
	if (!loadLevelMap(load, &document, &error)) {
		return failure(3, QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read the map %1: %2").arg(QDir::toNativeSeparators(input), error));
	}
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return failure(4, QCoreApplication::translate("VibeStudioAiGenerationCli", "map ai-edit edits Quake-family .map files; %1 is not one.").arg(QDir::toNativeSeparators(input)));
	}
	// What the request is about, as the editor's selection says it.
	if (options.has(QStringLiteral("--select"))) {
		QVector<LevelMapSelectionRef> refs;
		for (const QString& selector : options.values(QStringLiteral("--select")).join(QLatin1Char(',')).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			const qsizetype colon = selector.indexOf(QLatin1Char(':'));
			bool ok = false;
			const LevelMapSelectionRef ref {levelMapSelectionKindFromId(selector.left(colon).trimmed()), selector.mid(colon + 1).trimmed().toInt(&ok)};
			if (colon <= 0 || !ok || !levelMapObjectExists(document, ref)) {
				return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--select takes selectors such as entity:3,brush:12; %1 is not in this map.").arg(selector.trimmed()));
			}
			refs << ref;
		}
		if (!setLevelMapSelection(&document, refs, &error)) {
			return failure(2, error);
		}
	}

	AiGenerationCliResult result;
	LevelAiEditProposal proposal;
	if (options.has(QStringLiteral("--proposal"))) {
		// A saved proposal, from --save-proposal or written by hand: no model.
		QFile file(options.value(QStringLiteral("--proposal")));
		if (!file.open(QIODevice::ReadOnly)) {
			return failure(3, QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read the proposal %1.").arg(QDir::toNativeSeparators(file.fileName())));
		}
		if (!levelAiEditProposalFromAnswer(QString::fromUtf8(file.readAll()), &proposal, &error)) {
			return failure(4, error);
		}
	} else {
		const AiAutomationPreferences preferences = preferencesFor(options, false);
		const AiTextConnection connection = resolveAiTextConnection(preferences, options.value(QStringLiteral("--provider")));
		const AiChatRequest request = levelAiEditRequest(document, instruction, connection.connectorId, connection.model, connection.endpoint, QString(), QDir::homePath());
		if (options.has(QStringLiteral("--dry-run"))) {
			AiHttpRequest http;
			result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Dry run: nothing was sent. %1").arg(aiTextConnectionBlockText(connection));
			result.lines << (buildAiHttpRequest(request, QString(), &http, &error) ? describeAiHttpRequest(http) : error);
			result.payload.insert(QStringLiteral("dryRun"), true);
			result.payload.insert(QStringLiteral("request"), result.lines.join(QLatin1Char('\n')));
			return result;
		}
		const QString blocked = sendBlock(connection.ready(), aiTextConnectionBlockText(connection), connection.local, connection.endpoint, options);
		if (!blocked.isEmpty()) {
			return failure(5, blocked);
		}
		const AiChatResponse response = sendChat(request, aiTextConnectionApiKey(connection), timeoutFor(options));
		if (!response.ok) {
			return failure(1, QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 %2").arg(aiChatFailureText(response.failure), response.errorMessage));
		}
		result.payload.insert(QStringLiteral("ai"),
			usageJson(connection.connectorId, connection.model, connection.endpoint, response.inputTokens, response.outputTokens, response.elapsedMsecs));
		if (!levelAiEditProposalFromAnswer(response.text, &proposal, &error)) {
			return failure(4, QCoreApplication::translate("VibeStudioAiGenerationCli", "The model's answer held no proposal: %1").arg(error));
		}
	}
	validateLevelAiEditProposal(document, &proposal);

	// --only picks actions by their number in the list; the rest are left out.
	QStringList warnings = proposal.problems;
	if (options.has(QStringLiteral("--only"))) {
		QSet<int> chosen;
		for (const QString& part : options.value(QStringLiteral("--only")).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			bool ok = false;
			const int number = part.trimmed().toInt(&ok);
			if (!ok || number < 1 || number > proposal.actions.size()) {
				return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--only takes action numbers from 1 to %1, such as 1,3.").arg(proposal.actions.size()));
			}
			chosen.insert(number - 1);
		}
		for (int index = 0; index < proposal.actions.size(); ++index) {
			LevelAiEditAction& action = proposal.actions[index];
			if (chosen.contains(index) && !action.valid()) {
				warnings << QCoreApplication::translate("VibeStudioAiGenerationCli", "Action %1 cannot run: %2").arg(index + 1).arg(action.problems.join(QStringLiteral(" ")));
			}
			action.enabled = chosen.contains(index) && action.valid();
		}
	}

	result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Proposal: %1")
						.arg(proposal.summary.isEmpty() ? QCoreApplication::translate("VibeStudioAiGenerationCli", "(no summary)") : proposal.summary);
	int runnable = 0;
	for (int index = 0; index < proposal.actions.size(); ++index) {
		const LevelAiEditAction& action = proposal.actions.at(index);
		const QString state = !action.valid() ? QCoreApplication::translate("VibeStudioAiGenerationCli", "blocked")
			: action.enabled					 ? QCoreApplication::translate("VibeStudioAiGenerationCli", "ready")
												 : QCoreApplication::translate("VibeStudioAiGenerationCli", "left out");
		runnable += action.enabled && action.valid() ? 1 : 0;
		result.lines << QStringLiteral("%1. [%2] %3").arg(index + 1).arg(state, action.description);
		if (!action.reason.isEmpty()) {
			result.lines << QStringLiteral("   %1").arg(action.reason);
		}
		for (const QString& problem : action.problems) {
			result.lines << QStringLiteral("   ! %1").arg(problem);
		}
	}
	if (proposal.actions.isEmpty()) {
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "No actions were proposed.");
	}
	for (const QString& warning : warnings) {
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Warning: %1").arg(warning);
	}

	const bool overwrite = options.has(QStringLiteral("--overwrite"));
	QStringList written;
	if (options.has(QStringLiteral("--save-proposal"))) {
		const QString path = options.value(QStringLiteral("--save-proposal"));
		if (!writeFile(path, QJsonDocument(levelAiEditProposalJson(proposal)).toJson(QJsonDocument::Indented), overwrite, &error)) {
			return failure(1, error);
		}
		written << path;
	}
	if (options.has(QStringLiteral("--output"))) {
		if (runnable == 0) {
			return failure(4, QCoreApplication::translate("VibeStudioAiGenerationCli", "None of the proposed actions can run, so nothing was written."));
		}
		const LevelAiEditApplyReport report = applyLevelAiEditProposal(&document, proposal);
		result.lines += report.lines;
		const LevelMapSerialized saved = serializeLevelMap(document);
		if (!saved.succeeded()) {
			return failure(1, saved.errors.join(QStringLiteral(" ")));
		}
		const QString path = options.value(QStringLiteral("--output"));
		if (!writeFile(path, saved.bytes, overwrite, &error)) {
			return failure(1, error);
		}
		written << path;
		result.payload.insert(QStringLiteral("applied"), report.applied);
		result.payload.insert(QStringLiteral("skipped"), report.skipped);
		result.payload.insert(QStringLiteral("applyLines"), QJsonArray::fromStringList(report.lines));
		result.payload.insert(QStringLiteral("output"), path);
	} else {
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Review only: the map was not changed. Pass --output <map> to write it with the ready actions.");
	}
	for (const QString& path : written) {
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Wrote %1").arg(QDir::toNativeSeparators(path));
	}
	result.payload.insert(QStringLiteral("map"), input);
	result.payload.insert(QStringLiteral("instruction"), instruction);
	result.payload.insert(QStringLiteral("proposal"), levelAiEditProposalJson(proposal));
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
	result.payload.insert(QStringLiteral("written"), QJsonArray::fromStringList(written));
	return result;
}

namespace {

// The game's palette for a paletted texture: a file, a folder, a package, or
// the generated stand-in (with a warning) when none is named.
IdTechPaletteResolution paletteFor(const Options& options, const QString& game, QStringList* warnings, QString* error)
{
	TextureGameProfile profile;
	textureGameProfileForId(game, &profile);
	IdTechPaletteResolution resolution;
	if (profile.paletteId.isEmpty()) {
		return resolution;
	}
	const QString paletteId = options.value(QStringLiteral("--palette"), profile.paletteId);
	if (options.has(QStringLiteral("--palette-file"))) {
		QFile file(options.value(QStringLiteral("--palette-file")));
		if (!file.open(QIODevice::ReadOnly)) {
			*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read the palette %1.").arg(QDir::toNativeSeparators(file.fileName()));
			return resolution;
		}
		const QByteArray bytes = file.readAll();
		const bool pcx = file.fileName().endsWith(QStringLiteral(".pcx"), Qt::CaseInsensitive);
		if (!(pcx ? parsePcxPalette(bytes, paletteId, &resolution.palette, error) : parseIdTechPaletteBytes(bytes, paletteId, &resolution.palette, error))) {
			return resolution;
		}
		resolution.requestedPaletteId = paletteId;
		resolution.fromPackage = true;
		resolution.sourcePackagePath = file.fileName();
		return resolution;
	}
	if (options.has(QStringLiteral("--palette-root"))) {
		resolution = resolveIdTechPaletteFromDirectory(options.value(QStringLiteral("--palette-root")), paletteId);
	} else if (options.has(QStringLiteral("--package"))) {
		PackageArchive archive;
		if (!archive.load(options.value(QStringLiteral("--package")), error)) {
			return resolution;
		}
		resolution = resolveIdTechPalette(archive, paletteId);
	} else {
		resolution.palette = generatedIdTechPalette(paletteId);
		resolution.requestedPaletteId = paletteId;
	}
	if (resolution.palette.generated) {
		*warnings << QCoreApplication::translate("VibeStudioAiGenerationCli",
			"No %1 palette was given (--palette-file, --palette-root or --package), so a generated stand-in was used.").arg(profile.displayName);
	}
	return resolution;
}

bool textureSpecFrom(const Options& options, TextureGenerationSpec* spec, QString* error)
{
	spec->prompt = options.value(QStringLiteral("--prompt"));
	spec->game = options.value(QStringLiteral("--game"), spec->game);
	spec->surface = options.value(QStringLiteral("--surface"), spec->surface);
	spec->style = options.value(QStringLiteral("--style"));
	spec->name = options.value(QStringLiteral("--name"));
	spec->directory = options.value(QStringLiteral("--directory"));
	spec->companions = options.has(QStringLiteral("--companions"));
	spec->fullbrights = options.has(QStringLiteral("--fullbrights"));
	spec->seamless = !options.has(QStringLiteral("--no-seamless"));
	if (options.has(QStringLiteral("--dither"))) {
		const QString value = options.value(QStringLiteral("--dither")).toLower();
		spec->dither = value == QStringLiteral("on") || value == QStringLiteral("true") || value == QStringLiteral("1") || value == QStringLiteral("floyd-steinberg");
	}
	if (!textureGameProfileForId(spec->game)) {
		*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "--game takes one of: %1.").arg(textureGameProfileIds().join(QStringLiteral(", ")));
		return false;
	}
	TextureGameProfile profile;
	textureGameProfileForId(spec->game, &profile);
	spec->game = profile.id;
	if (!textureGenerationSurfaceIds().contains(spec->surface)) {
		*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "--surface takes one of: %1.").arg(textureGenerationSurfaceIds().join(QStringLiteral(", ")));
		return false;
	}
	if (options.has(QStringLiteral("--size")) && !readSize(options.value(QStringLiteral("--size")), &spec->size)) {
		*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "--size takes WIDTHxHEIGHT, such as 64x64.");
		return false;
	}
	int blend = spec->seamBlendPercent;
	if (!readInteger(options, QStringLiteral("--seam-blend"), 1, 45, &blend, error)) {
		return false;
	}
	spec->seamBlendPercent = blend;
	if (options.has(QStringLiteral("--normal-strength"))) {
		bool ok = false;
		spec->normalStrength = options.value(QStringLiteral("--normal-strength")).toDouble(&ok);
		if (!ok || spec->normalStrength <= 0.0 || spec->normalStrength > 20.0) {
			*error = QCoreApplication::translate("VibeStudioAiGenerationCli", "--normal-strength takes a number above 0, up to 20.");
			return false;
		}
	}
	return true;
}

// The variants side by side, each tiled 2x2 so seams show.
QImage contactSheet(const QVector<GeneratedTexture>& textures)
{
	const int tile = 256;
	QImage sheet(tile * int(std::max<qsizetype>(1, textures.size())), tile, QImage::Format_ARGB32);
	sheet.fill(QColor(24, 26, 31));
	QPainter painter(&sheet);
	for (int index = 0; index < textures.size(); ++index) {
		const QImage preview = textures[index].preview.scaled(tile / 2, tile / 2, Qt::IgnoreAspectRatio, Qt::FastTransformation);
		for (int y = 0; y < 2; ++y) {
			for (int x = 0; x < 2; ++x) {
				painter.drawImage(index * tile + x * tile / 2, y * tile / 2, preview);
			}
		}
	}
	painter.end();
	return sheet;
}

} // namespace

AiGenerationCliResult runTextureGenerate(const QStringList& arguments)
{
	Options options;
	QString error;
	const QSet<QString> switches = kAiSwitches + QSet<QString> {
		QStringLiteral("--companions"),
		QStringLiteral("--fullbrights"),
		QStringLiteral("--no-seamless"),
		QStringLiteral("--overwrite"),
	};
	const QSet<QString> values = kAiValues + QSet<QString> {
		QStringLiteral("--prompt"),
		QStringLiteral("--game"),
		QStringLiteral("--surface"),
		QStringLiteral("--style"),
		QStringLiteral("--size"),
		QStringLiteral("--name"),
		QStringLiteral("--directory"),
		QStringLiteral("--dither"),
		QStringLiteral("--seam-blend"),
		QStringLiteral("--normal-strength"),
		QStringLiteral("--count"),
		QStringLiteral("--quality"),
		QStringLiteral("--from-image"),
		QStringLiteral("--source"),
		QStringLiteral("--folder"),
		QStringLiteral("--wad"),
		QStringLiteral("--palette"),
		QStringLiteral("--palette-file"),
		QStringLiteral("--palette-root"),
		QStringLiteral("--package"),
		QStringLiteral("--preview"),
	};
	if (!options.parse(arguments, switches, values, &error)) {
		return failure(2, error);
	}
	TextureGenerationSpec spec;
	if (!textureSpecFrom(options, &spec, &error)) {
		return failure(2, error);
	}
	if (spec.prompt.trimmed().isEmpty() && !options.has(QStringLiteral("--from-image"))) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli",
			"texture generate needs --prompt (sent to your image model) or --from-image <picture> (no AI)."));
	}
	int count = 1;
	if (!readInteger(options, QStringLiteral("--count"), 1, 8, &count, &error)) {
		return failure(2, error);
	}
	AiGenerationCliResult result;
	QStringList warnings;
	QVector<QImage> pictures;
	QJsonObject provenance;
	provenance.insert(QStringLiteral("prompt"), textureGenerationPrompt(spec));
	if (options.has(QStringLiteral("--from-image"))) {
		const QString path = options.value(QStringLiteral("--from-image"));
		QImage picture(path);
		if (picture.isNull()) {
			return failure(3, QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read the picture %1.").arg(QDir::toNativeSeparators(path)));
		}
		pictures << picture;
		count = 1;
		provenance.insert(QStringLiteral("source"), QFileInfo(path).fileName());
		provenance.insert(QStringLiteral("provider"), QStringLiteral("none"));
	} else {
		const AiAutomationPreferences preferences = preferencesFor(options, true);
		const AiImageConnection connection = resolveAiImageConnection(preferences, options.value(QStringLiteral("--provider")));
		AiImageRequest request;
		request.connectorId = connection.connectorId;
		request.model = connection.model;
		request.endpoint = connection.endpoint;
		request.prompt = textureGenerationPrompt(spec);
		request.negativePrompt = textureGenerationNegativePrompt(spec);
		request.size = textureGenerationRequestSize(spec);
		request.count = count;
		request.tileable = spec.seamless;
		request.quality = options.value(QStringLiteral("--quality"));
		if (options.has(QStringLiteral("--source"))) {
			QImage source(options.value(QStringLiteral("--source")));
			if (source.isNull()) {
				return failure(3, QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read the source picture %1.").arg(options.value(QStringLiteral("--source"))));
			}
			request.sourceImage = pngBytes(source);
		}
		if (options.has(QStringLiteral("--dry-run"))) {
			AiHttpRequest http;
			result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Dry run: nothing was sent. %1").arg(aiImageConnectionBlockText(connection));
			result.lines << (buildAiImageHttpRequest(request, QString(), &http, &error) ? describeAiHttpRequest(http) : error);
			result.payload.insert(QStringLiteral("dryRun"), true);
			result.payload.insert(QStringLiteral("request"), result.lines.join(QLatin1Char('\n')));
			return result;
		}
		const QString blocked = sendBlock(connection.ready(), aiImageConnectionBlockText(connection), connection.local, connection.endpoint, options);
		if (!blocked.isEmpty()) {
			return failure(5, blocked);
		}
		const AiImageResponse response = sendImage(request, aiImageConnectionApiKey(connection), timeoutFor(options));
		if (!response.ok && response.images.isEmpty()) {
			return failure(1, QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 %2").arg(aiChatFailureText(response.failure), response.errorMessage));
		}
		if (!response.ok) {
			warnings << response.errorMessage;
		}
		QJsonArray revised;
		for (const AiGeneratedImage& image : response.images) {
			QImage picture;
			picture.loadFromData(image.bytes);
			if (!picture.isNull()) {
				pictures << picture;
			}
			if (!image.revisedPrompt.isEmpty()) {
				revised.append(image.revisedPrompt);
			}
		}
		if (pictures.isEmpty()) {
			return failure(1, QCoreApplication::translate("VibeStudioAiGenerationCli", "The provider's pictures could not be read."));
		}
		provenance.insert(QStringLiteral("provider"), usageJson(connection.connectorId, response.model.isEmpty() ? connection.model : response.model, connection.endpoint,
														  response.inputTokens, response.outputTokens, response.elapsedMsecs));
		provenance.insert(QStringLiteral("negativePrompt"), request.negativePrompt);
		provenance.insert(QStringLiteral("revisedPrompts"), revised);
		if (!request.sourceImage.isEmpty()) {
			provenance.insert(QStringLiteral("source"), QFileInfo(options.value(QStringLiteral("--source"))).fileName());
		}
	}

	const IdTechPaletteResolution palette = paletteFor(options, spec.game, &warnings, &error);
	if (!error.isEmpty()) {
		return failure(3, error);
	}
	TextureGenerationOutput output;
	output.folder = options.value(QStringLiteral("--folder"), StudioSettings().currentProjectPath().isEmpty() ? QDir::currentPath() : StudioSettings().currentProjectPath());
	output.wadPath = options.value(QStringLiteral("--wad"));
	output.replaceExisting = options.has(QStringLiteral("--overwrite"));
	output.provenance = provenance;
	QVector<GeneratedTexture> textures;
	QJsonArray texturesJson;
	for (int index = 0; index < pictures.size(); ++index) {
		const TextureGenerationSpec variant = textureGenerationVariantSpec(spec, index, int(pictures.size()));
		const GeneratedTexture texture = processGeneratedTexture(pictures[index], variant, palette);
		if (!texture.ok) {
			return failure(4, texture.error);
		}
		const TextureGenerationWriteReport written = writeGeneratedTexture(texture, variant, output);
		if (!written.ok) {
			return failure(1, written.error);
		}
		textures << texture;
		QJsonObject entry = generatedTextureJson(texture);
		entry.insert(QStringLiteral("write"), textureGenerationWriteReportJson(written));
		texturesJson.append(entry);
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "%1: %2x%3, seam %4 (was %5).")
							.arg(written.mapTextureName)
							.arg(texture.image.width())
							.arg(texture.image.height())
							.arg(texture.seamScoreAfter, 0, 'f', 2)
							.arg(texture.seamScoreBefore, 0, 'f', 2);
		for (const QString& path : written.writtenPaths) {
			result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "  Wrote %1").arg(QDir::toNativeSeparators(path));
		}
		for (const QString& note : written.notes) {
			result.lines << QStringLiteral("  - %1").arg(note);
		}
		warnings += texture.warnings;
	}
	if (options.has(QStringLiteral("--preview"))) {
		if (!writeFile(options.value(QStringLiteral("--preview")), pngBytes(contactSheet(textures)), true, &error)) {
			return failure(1, error);
		}
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Preview: %1").arg(QDir::toNativeSeparators(options.value(QStringLiteral("--preview"))));
	}
	warnings.removeDuplicates();
	for (const QString& warning : warnings) {
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Warning: %1").arg(warning);
	}
	result.payload.insert(QStringLiteral("spec"), textureGenerationSpecJson(spec));
	result.payload.insert(QStringLiteral("textures"), texturesJson);
	result.payload.insert(QStringLiteral("provenance"), provenance);
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
	if (output.replaceExisting) {
		result.payload.insert(QStringLiteral("overwrite"), true);
	}
	return result;
}

AiGenerationCliResult runTextureDerive(const QStringList& arguments)
{
	Options options;
	QString error;
	const QSet<QString> values = {
		QStringLiteral("--input"),
		QStringLiteral("--game"),
		QStringLiteral("--output"),
		QStringLiteral("--normal-strength"),
		QStringLiteral("--glow-threshold"),
	};
	if (!options.parse(arguments, {QStringLiteral("--no-seamless"), QStringLiteral("--overwrite"), QStringLiteral("--dry-run")}, values, &error)) {
		return failure(2, error);
	}
	QString input = options.value(QStringLiteral("--input"));
	const QStringList positional = options.positional();
	if (input.isEmpty() && positional.size() >= 3) {
		input = positional[2];
	}
	if (input.isEmpty()) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "texture derive needs --input <texture picture>."));
	}
	const QImage image(input);
	if (image.isNull()) {
		return failure(3, QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read the picture %1.").arg(QDir::toNativeSeparators(input)));
	}
	TextureGameProfile profile;
	if (!textureGameProfileForId(options.value(QStringLiteral("--game"), QStringLiteral("quake")), &profile) || profile.normalSuffix.isEmpty()) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--game takes quake, quake2, quake3 or generic: games whose ports read companion maps."));
	}
	double strength = 2.5;
	double threshold = 0.82;
	if (options.has(QStringLiteral("--normal-strength"))) {
		strength = options.value(QStringLiteral("--normal-strength")).toDouble();
	}
	if (options.has(QStringLiteral("--glow-threshold"))) {
		threshold = options.value(QStringLiteral("--glow-threshold")).toDouble();
	}
	if (strength <= 0.0 || strength > 20.0 || threshold <= 0.0 || threshold > 1.0) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--normal-strength takes 0-20 and --glow-threshold 0-1."));
	}
	const bool wrap = !options.has(QStringLiteral("--no-seamless"));
	const QFileInfo info(input);
	const QString folder = options.value(QStringLiteral("--output"), info.absolutePath());
	AiGenerationCliResult result;
	QJsonArray maps;
	const QVector<QPair<QString, QImage>> outputs = {
		{profile.normalSuffix, deriveTextureNormalMap(image, strength, wrap)},
		{profile.glossSuffix, deriveTextureGlossMap(image)},
		{profile.glowSuffix, deriveTextureGlowMap(image, threshold)},
	};
	for (const auto& output : outputs) {
		if (output.second.isNull()) {
			result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Nothing glows; no %1 map.").arg(output.first);
			continue;
		}
		const QString path = QDir(folder).filePath(info.completeBaseName() + output.first + QStringLiteral(".tga"));
		TextureExportOptions targa;
		targa.format = TextureExportFormat::Targa;
		const TextureExportResult encoded = encodeTextureExport(output.second, targa);
		if (!encoded.succeeded) {
			return failure(1, encoded.error);
		}
		if (!options.has(QStringLiteral("--dry-run")) && !writeFile(path, encoded.bytes, options.has(QStringLiteral("--overwrite")), &error)) {
			return failure(1, error);
		}
		maps.append(QJsonObject {{QStringLiteral("suffix"), output.first}, {QStringLiteral("path"), path}});
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 %2").arg(options.has(QStringLiteral("--dry-run")) ? QStringLiteral("Would write") : QStringLiteral("Wrote"),
							QDir::toNativeSeparators(path));
	}
	result.payload.insert(QStringLiteral("input"), input);
	result.payload.insert(QStringLiteral("game"), profile.id);
	result.payload.insert(QStringLiteral("maps"), maps);
	result.payload.insert(QStringLiteral("dryRun"), options.has(QStringLiteral("--dry-run")));
	return result;
}

AiGenerationCliResult runAiImage(const QStringList& arguments)
{
	Options options;
	QString error;
	const QSet<QString> switches = kAiSwitches + QSet<QString> {QStringLiteral("--transparent"), QStringLiteral("--tileable"), QStringLiteral("--overwrite")};
	const QSet<QString> values = kAiValues + QSet<QString> {
		QStringLiteral("--prompt"),
		QStringLiteral("--negative"),
		QStringLiteral("--size"),
		QStringLiteral("--count"),
		QStringLiteral("--quality"),
		QStringLiteral("--seed"),
		QStringLiteral("--steps"),
		QStringLiteral("--source"),
		QStringLiteral("--strength"),
		QStringLiteral("--output"),
	};
	if (!options.parse(arguments, switches, values, &error)) {
		return failure(2, error);
	}
	if (options.value(QStringLiteral("--prompt")).trimmed().isEmpty()) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "ai image needs --prompt, and --output for where the pictures go."));
	}
	const AiAutomationPreferences preferences = preferencesFor(options, true);
	const AiImageConnection connection = resolveAiImageConnection(preferences, options.value(QStringLiteral("--provider")));
	AiImageRequest request;
	request.connectorId = connection.connectorId;
	request.model = connection.model;
	request.endpoint = connection.endpoint;
	request.prompt = options.value(QStringLiteral("--prompt"));
	request.negativePrompt = options.value(QStringLiteral("--negative"));
	request.quality = options.value(QStringLiteral("--quality"));
	request.transparentBackground = options.has(QStringLiteral("--transparent"));
	request.tileable = options.has(QStringLiteral("--tileable"));
	if (options.has(QStringLiteral("--size")) && !readSize(options.value(QStringLiteral("--size")), &request.size)) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--size takes WIDTHxHEIGHT, such as 1024x1024."));
	}
	int count = 1;
	int steps = 0;
	if (!readInteger(options, QStringLiteral("--count"), 1, 8, &count, &error) || !readInteger(options, QStringLiteral("--steps"), 1, 150, &steps, &error)) {
		return failure(2, error);
	}
	request.count = count;
	request.steps = steps;
	if (options.has(QStringLiteral("--seed"))) {
		request.seed = options.value(QStringLiteral("--seed")).toLongLong();
	}
	if (options.has(QStringLiteral("--strength"))) {
		request.sourceStrength = options.value(QStringLiteral("--strength")).toDouble();
	}
	if (options.has(QStringLiteral("--source"))) {
		const QImage source(options.value(QStringLiteral("--source")));
		if (source.isNull()) {
			return failure(3, QCoreApplication::translate("VibeStudioAiGenerationCli", "Could not read the source picture %1.").arg(options.value(QStringLiteral("--source"))));
		}
		request.sourceImage = pngBytes(source);
	}
	AiGenerationCliResult result;
	if (options.has(QStringLiteral("--dry-run"))) {
		AiHttpRequest http;
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Dry run: nothing was sent. %1").arg(aiImageConnectionBlockText(connection));
		result.lines << (buildAiImageHttpRequest(request, QString(), &http, &error) ? describeAiHttpRequest(http) : error);
		result.payload.insert(QStringLiteral("dryRun"), true);
		result.payload.insert(QStringLiteral("connection"), QJsonObject {
			{QStringLiteral("connector"), connection.connectorId},
			{QStringLiteral("api"), aiImageApiId(connection.api)},
			{QStringLiteral("state"), aiImageConnectionBlockId(connection.block)},
		});
		result.payload.insert(QStringLiteral("request"), result.lines.join(QLatin1Char('\n')));
		return result;
	}
	if (!options.has(QStringLiteral("--output"))) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "ai image needs --output <picture or folder>."));
	}
	const QString blocked = sendBlock(connection.ready(), aiImageConnectionBlockText(connection), connection.local, connection.endpoint, options);
	if (!blocked.isEmpty()) {
		return failure(5, blocked);
	}
	const AiImageResponse response = sendImage(request, aiImageConnectionApiKey(connection), timeoutFor(options));
	if (response.images.isEmpty()) {
		return failure(1, QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 %2").arg(aiChatFailureText(response.failure), response.errorMessage));
	}
	const QString output = options.value(QStringLiteral("--output"));
	const bool folder = QFileInfo(output).isDir() || output.endsWith(QLatin1Char('/')) || output.endsWith(QLatin1Char('\\')) || response.images.size() > 1;
	QJsonArray written;
	for (int index = 0; index < response.images.size(); ++index) {
		const AiGeneratedImage& image = response.images[index];
		QString path;
		QByteArray bytes = image.bytes;
		if (folder) {
			const QString suffix = image.mimeType == QStringLiteral("image/jpeg") ? QStringLiteral("jpg") : image.mimeType == QStringLiteral("image/webp") ? QStringLiteral("webp") : QStringLiteral("png");
			const QString base = QFileInfo(output).isDir() || output.endsWith(QLatin1Char('/')) || output.endsWith(QLatin1Char('\\')) ? QDir(output).filePath(QStringLiteral("image"))
																																	  : QFileInfo(output).absolutePath() + QLatin1Char('/') + QFileInfo(output).completeBaseName();
			path = QStringLiteral("%1_%2.%3").arg(base).arg(index + 1).arg(suffix);
		} else {
			path = output;
			// A .png asked for gets PNG bytes whatever the provider sent.
			if (output.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) && image.mimeType != QStringLiteral("image/png")) {
				QImage decoded;
				decoded.loadFromData(image.bytes);
				bytes = pngBytes(decoded);
			}
		}
		if (!writeFile(path, bytes, options.has(QStringLiteral("--overwrite")), &error)) {
			return failure(1, error);
		}
		written.append(path);
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Wrote %1").arg(QDir::toNativeSeparators(path));
		if (!image.revisedPrompt.isEmpty()) {
			result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "  The provider drew: %1").arg(image.revisedPrompt);
		}
	}
	if (!response.ok) {
		result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Warning: %1").arg(response.errorMessage);
	}
	result.payload.insert(QStringLiteral("written"), written);
	result.payload.insert(QStringLiteral("usage"), usageJson(connection.connectorId, response.model.isEmpty() ? connection.model : response.model, connection.endpoint,
													   response.inputTokens, response.outputTokens, response.elapsedMsecs));
	return result;
}

AiGenerationCliResult runAudioGenerate(const QStringList& arguments)
{
	Options options;
	QString error;
	const QSet<QString> values = kAiValues + QSet<QString> {
		QStringLiteral("--prompt"),
		QStringLiteral("--game"),
		QStringLiteral("--kind"),
		QStringLiteral("--name"),
		QStringLiteral("--folder"),
		QStringLiteral("--duration"),
		QStringLiteral("--loop"),
		QStringLiteral("--influence"),
		QStringLiteral("--seed"),
		QStringLiteral("--variants"),
		QStringLiteral("--source"),
		QStringLiteral("--output"),
		QStringLiteral("--wad"),
		QStringLiteral("--preview"),
	};
	if (!options.parse(arguments, kAiSwitches + QSet<QString> {QStringLiteral("--overwrite")}, values, &error)) {
		return failure(2, error);
	}
	SoundGenerationSpec spec;
	spec.prompt = options.value(QStringLiteral("--prompt")).trimmed();
	spec.kind = options.value(QStringLiteral("--kind")).trimmed().toLower();
	if (spec.prompt.isEmpty() && spec.kind.isEmpty()) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli",
			"asset audio-generate needs --prompt \"<the sound>\" or --kind <%1>.").arg(soundGenerationKindIds().join(QLatin1Char('|'))));
	}
	if (!spec.kind.isEmpty() && !soundGenerationKindIds().contains(spec.kind)) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--kind takes %1.").arg(soundGenerationKindIds().join(QStringLiteral(", "))));
	}
	SoundGameProfile profile;
	if (!soundGameProfileForId(options.value(QStringLiteral("--game"), QStringLiteral("quake")), &profile)) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--game takes %1.").arg(soundGameProfileIds().join(QStringLiteral(", "))));
	}
	spec.game = profile.id;
	spec.name = options.value(QStringLiteral("--name"));
	if (options.has(QStringLiteral("--folder"))) {
		spec.folder = options.value(QStringLiteral("--folder"));
	}
	bool ok = true;
	if (options.has(QStringLiteral("--duration"))) {
		spec.durationSeconds = options.value(QStringLiteral("--duration")).toDouble(&ok);
		if (!ok || spec.durationSeconds < 0.1 || spec.durationSeconds > 30.0) {
			return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--duration takes seconds from 0.1 to 30."));
		}
	}
	if (options.has(QStringLiteral("--influence"))) {
		spec.promptInfluence = options.value(QStringLiteral("--influence")).toDouble(&ok);
		if (!ok || spec.promptInfluence < 0.0 || spec.promptInfluence > 1.0) {
			return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--influence takes a number from 0 to 1."));
		}
	}
	if (options.has(QStringLiteral("--seed"))) {
		spec.seed = options.value(QStringLiteral("--seed")).toLongLong(&ok);
		if (!ok || spec.seed < 0 || spec.seed > 0x7fffffff) {
			return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--seed takes a whole number from 0 to 2147483647."));
		}
	}
	if (!readInteger(options, QStringLiteral("--variants"), 1, 4, &spec.variants, &error)) {
		return failure(2, error);
	}
	spec = normalizedSoundGenerationSpec(spec);
	// Ambience and alarms loop unless told not to; the rest do not unless told to.
	spec.loop = soundGenerationKindLoops(spec.kind);
	if (options.has(QStringLiteral("--loop"))) {
		const QString loop = options.value(QStringLiteral("--loop")).trimmed().toLower();
		if (loop == QStringLiteral("on") || loop == QStringLiteral("true") || loop == QStringLiteral("yes")) {
			spec.loop = true;
		} else if (loop == QStringLiteral("off") || loop == QStringLiteral("false") || loop == QStringLiteral("no")) {
			spec.loop = false;
		} else {
			return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--loop takes on or off."));
		}
	}
	const QString source = options.value(QStringLiteral("--source"), QStringLiteral("synth")).trimmed().toLower();
	if (source != QStringLiteral("synth") && source != QStringLiteral("ai")) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--source takes synth (no AI) or ai."));
	}
	if (source == QStringLiteral("ai") && spec.prompt.isEmpty()) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli", "--source ai needs --prompt: the model works from the description."));
	}
	if (!options.has(QStringLiteral("--output")) && !options.has(QStringLiteral("--preview")) && !options.has(QStringLiteral("--dry-run"))) {
		return failure(2, QCoreApplication::translate("VibeStudioAiGenerationCli",
			"asset audio-generate needs --output <project folder> (or --preview <wav> to listen first)."));
	}

	AiGenerationCliResult result;
	QJsonObject provenance;
	AiSoundConnection connection;
	if (source == QStringLiteral("ai")) {
		connection = resolveAiSoundConnection(soundPreferencesFor(options), options.value(QStringLiteral("--provider")));
		if (options.has(QStringLiteral("--dry-run"))) {
			AiSoundRequest request;
			request.connectorId = connection.connectorId;
			request.model = connection.model;
			request.endpoint = connection.endpoint;
			request.prompt = soundGenerationPrompt(soundGenerationVariantSpec(spec, 0, spec.variants));
			request.durationSeconds = spec.durationSeconds > 0.0 ? std::max(0.5, spec.durationSeconds) : 0.0;
			request.promptInfluence = spec.promptInfluence;
			request.loop = spec.loop;
			AiHttpRequest http;
			result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Dry run: nothing was sent. %1").arg(aiSoundConnectionBlockText(connection));
			result.lines << (buildAiSoundHttpRequest(request, QString(), &http, &error) ? describeAiHttpRequest(http) : error);
			if (spec.variants > 1) {
				result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 such requests would be sent, one per variant.").arg(spec.variants);
			}
			result.payload.insert(QStringLiteral("dryRun"), true);
			result.payload.insert(QStringLiteral("request"), result.lines.join(QLatin1Char('\n')));
			return result;
		}
		const QString blocked = sendBlock(connection.ready(), aiSoundConnectionBlockText(connection), connection.local, connection.endpoint, options);
		if (!blocked.isEmpty()) {
			return failure(5, blocked);
		}
		provenance.insert(QStringLiteral("connector"), connection.connectorId);
		provenance.insert(QStringLiteral("model"), connection.model);
		provenance.insert(QStringLiteral("host"), QUrl(connection.endpoint).host());
	} else {
		provenance.insert(QStringLiteral("connector"), QStringLiteral("synth"));
		if (options.has(QStringLiteral("--dry-run"))) {
			result.lines << QCoreApplication::translate("VibeStudioAiGenerationCli", "Dry run: the synthesizer runs on this machine; nothing is sent, and nothing is written.");
		}
	}

	SoundGenerationOutput output;
	output.folder = options.value(QStringLiteral("--output"));
	output.wadPath = options.value(QStringLiteral("--wad"));
	output.replaceExisting = options.has(QStringLiteral("--overwrite"));
	output.dryRun = options.has(QStringLiteral("--dry-run"));
	QJsonArray sounds;
	QStringList written;
	for (int index = 0; index < spec.variants; ++index) {
		const SoundGenerationSpec variant = soundGenerationVariantSpec(spec, index, spec.variants);
		GeneratedSound sound;
		QJsonObject record = provenance;
		if (source == QStringLiteral("ai")) {
			AiSoundRequest request;
			request.connectorId = connection.connectorId;
			request.model = connection.model;
			request.endpoint = connection.endpoint;
			request.prompt = soundGenerationPrompt(variant);
			request.durationSeconds = variant.durationSeconds > 0.0 ? std::max(0.5, variant.durationSeconds) : 0.0;
			request.promptInfluence = variant.promptInfluence;
			request.loop = variant.loop;
			const AiSoundResponse response = sendSound(request, aiSoundConnectionApiKey(connection), timeoutFor(options));
			if (!response.ok) {
				return failure(1, QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 %2").arg(aiChatFailureText(response.failure), response.errorMessage));
			}
			record.insert(QStringLiteral("promptSent"), request.prompt);
			record.insert(QStringLiteral("elapsedMs"), double(response.elapsedMsecs));
			if (!response.cost.isEmpty()) {
				record.insert(QStringLiteral("cost"), response.cost);
			}
			sound = processGeneratedSoundBytes(response.audio, response.mimeType, variant, QStringLiteral("ai:%1/%2").arg(connection.connectorId, connection.model));
		} else {
			sound = processGeneratedSound(synthesizeSound(variant), variant, QStringLiteral("synth"));
		}
		if (!sound.ok) {
			return failure(4, sound.error);
		}
		result.lines << generatedSoundSummary(sound);
		for (const QString& note : sound.notes) {
			result.lines << QStringLiteral("- %1").arg(note);
		}
		QJsonObject entry = generatedSoundJson(sound);
		if (options.has(QStringLiteral("--preview"))) {
			// The working sound at its own rate, to listen to before writing.
			QString path = options.value(QStringLiteral("--preview"));
			if (spec.variants > 1) {
				const QFileInfo info(path);
				path = QDir(info.absolutePath()).filePath(QStringLiteral("%1_%2.wav").arg(info.completeBaseName()).arg(index + 1));
			}
			if (!output.dryRun && !writeFile(path, encodeAudioWav(sound.clip), output.replaceExisting, &error)) {
				return failure(1, error);
			}
			written << path;
			entry.insert(QStringLiteral("preview"), path);
		}
		if (options.has(QStringLiteral("--output"))) {
			output.provenance = record;
			const SoundGenerationWriteReport report = writeGeneratedSound(sound, variant, output);
			if (!report.ok) {
				return failure(report.alreadyExists ? 4 : 1, report.alreadyExists
						? QCoreApplication::translate("VibeStudioAiGenerationCli", "%1 Pass --overwrite to replace it.").arg(report.error)
						: report.error);
			}
			for (const QString& note : report.notes) {
				result.lines << QStringLiteral("- %1").arg(note);
			}
			for (const QString& path : report.writtenPaths) {
				// Variants of a Doom sound share one PWAD.
				if (!written.contains(path)) {
					written << path;
				}
			}
			entry.insert(QStringLiteral("write"), soundGenerationWriteReportJson(report));
		}
		sounds.append(entry);
	}
	for (const QString& path : written) {
		result.lines << (output.dryRun ? QCoreApplication::translate("VibeStudioAiGenerationCli", "Would write %1").arg(QDir::toNativeSeparators(path))
									   : QCoreApplication::translate("VibeStudioAiGenerationCli", "Wrote %1").arg(QDir::toNativeSeparators(path)));
	}
	result.payload.insert(QStringLiteral("spec"), soundGenerationSpecJson(spec));
	result.payload.insert(QStringLiteral("source"), source);
	result.payload.insert(QStringLiteral("sounds"), sounds);
	result.payload.insert(QStringLiteral("written"), QJsonArray::fromStringList(written));
	if (output.dryRun) {
		result.payload.insert(QStringLiteral("dryRun"), true);
	}
	return result;
}

} // namespace vibestudio::cli
