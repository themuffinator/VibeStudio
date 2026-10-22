#include "core/level_mapgen.h"

#include "core/ai_gguf_runtime.h"
#include "core/ai_harness.h"

#include "qmg/map_writer.hpp"
#include "qmg/pipeline.hpp"
#include "qmg/prompt.hpp"
#include "qmg/report.hpp"
#include "qmg/semantic_planner.hpp"
#include "qmg/switches.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace vibestudio {

namespace {

QString fromStd(const std::string& text)
{
	return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

std::string toStd(const QString& text)
{
	const QByteArray bytes = text.toUtf8();
	return std::string(bytes.constData(), size_t(bytes.size()));
}

QString normalizedGame(const QString& game)
{
	const QString id = game.trimmed().toLower();
	if (id == QStringLiteral("q1") || id == QStringLiteral("quake1")) {
		return QStringLiteral("quake");
	}
	if (id == QStringLiteral("q2")) {
		return QStringLiteral("quake2");
	}
	if (id == QStringLiteral("q3") || id == QStringLiteral("quake3arena")) {
		return QStringLiteral("quake3");
	}
	return id;
}

QString normalizedMode(const QString& mode)
{
	const QString id = mode.trimmed().toLower();
	if (id == QStringLiteral("sp") || id == QStringLiteral("single_player")) {
		return QStringLiteral("single-player");
	}
	if (id == QStringLiteral("dm")) {
		return QStringLiteral("deathmatch");
	}
	if (id == QStringLiteral("capture-the-flag") || id == QStringLiteral("capture_the_flag")) {
		return QStringLiteral("ctf");
	}
	return id;
}

std::optional<qmg::Game> qmgGame(const QString& game)
{
	const QString id = normalizedGame(game);
	if (id == QStringLiteral("quake")) {
		return qmg::Game::quake1;
	}
	if (id == QStringLiteral("quake2")) {
		return qmg::Game::quake2;
	}
	if (id == QStringLiteral("quake3")) {
		return qmg::Game::quake3;
	}
	return std::nullopt;
}

QString studioGame(qmg::Game game)
{
	switch (game) {
	case qmg::Game::quake1:
		return QStringLiteral("quake");
	case qmg::Game::quake2:
		return QStringLiteral("quake2");
	case qmg::Game::quake3:
		return QStringLiteral("quake3");
	}
	return QString();
}

std::optional<qmg::GameMode> qmgMode(const QString& mode)
{
	const QString id = normalizedMode(mode);
	if (id == QStringLiteral("single-player")) {
		return qmg::GameMode::single_player;
	}
	if (id == QStringLiteral("deathmatch")) {
		return qmg::GameMode::deathmatch;
	}
	if (id == QStringLiteral("duel")) {
		return qmg::GameMode::duel;
	}
	if (id == QStringLiteral("ctf")) {
		return qmg::GameMode::capture_the_flag;
	}
	return std::nullopt;
}

QString studioMode(qmg::GameMode mode)
{
	switch (mode) {
	case qmg::GameMode::single_player:
		return QStringLiteral("single-player");
	case qmg::GameMode::deathmatch:
		return QStringLiteral("deathmatch");
	case qmg::GameMode::duel:
		return QStringLiteral("duel");
	case qmg::GameMode::capture_the_flag:
		return QStringLiteral("ctf");
	}
	return QString();
}

// The CLI's names for the dialects, as Quake-MapGen's own qmg and report spell them.
QString formatId(qmg::MapDialect dialect)
{
	switch (dialect) {
	case qmg::MapDialect::quake1_valve220:
		return QStringLiteral("q1-valve220");
	case qmg::MapDialect::quake2_valve220:
		return QStringLiteral("q2-valve220");
	case qmg::MapDialect::quake3_legacy:
		return QStringLiteral("q3-legacy");
	case qmg::MapDialect::quake3_brush_primitives:
		return QStringLiteral("q3-brush-primitives");
	}
	return QString();
}

std::optional<qmg::MapDialect> qmgDialect(const QString& format)
{
	if (format == QStringLiteral("q1-valve220")) {
		return qmg::MapDialect::quake1_valve220;
	}
	if (format == QStringLiteral("q2-valve220")) {
		return qmg::MapDialect::quake2_valve220;
	}
	if (format == QStringLiteral("q3-legacy")) {
		return qmg::MapDialect::quake3_legacy;
	}
	if (format == QStringLiteral("q3-brush-primitives")) {
		return qmg::MapDialect::quake3_brush_primitives;
	}
	return std::nullopt;
}

// The dialog's and CLI's verticality words, as Quake-MapGen's prompt reader would weigh them:
// "flat" 0.15, unspoken 0.5, "high verticality" 0.95.
std::optional<double> verticalityValue(const QString& verticality)
{
	if (verticality == QStringLiteral("flat")) {
		return 0.15;
	}
	if (verticality == QStringLiteral("low")) {
		return 0.35;
	}
	if (verticality == QStringLiteral("medium")) {
		return 0.5;
	}
	if (verticality == QStringLiteral("high")) {
		return 0.95;
	}
	return std::nullopt;
}

// As the prompt reader weighs "quiet" 0.25, unspoken 0.5, and "intense" 0.9.
std::optional<double> intensityValue(const QString& intensity)
{
	if (intensity == QStringLiteral("quiet")) {
		return 0.25;
	}
	if (intensity == QStringLiteral("balanced")) {
		return 0.5;
	}
	if (intensity == QStringLiteral("intense")) {
		return 0.9;
	}
	return std::nullopt;
}

// Quake-MapGen reads game and mode from the words; an empty description names them instead.
QString promptFor(const LevelGenerationSpec& spec)
{
	const QString prompt = spec.prompt.trimmed();
	if (!prompt.isEmpty()) {
		return prompt;
	}
	const QString game = normalizedGame(spec.game);
	const QString words = game == QStringLiteral("quake2") ? QStringLiteral("Quake 2") : game == QStringLiteral("quake3") ? QStringLiteral("Quake 3") : QStringLiteral("Quake 1");
	const QString mode = normalizedMode(spec.mode);
	const QString modeWords = mode == QStringLiteral("ctf") ? QStringLiteral("capture the flag") : mode.isEmpty() ? QString() : mode;
	return modeWords.isEmpty() ? words : QStringLiteral("%1 %2").arg(words, modeWords);
}

qmg::PromptOverrides overridesFor(const LevelGenerationSpec& spec)
{
	qmg::PromptOverrides overrides;
	overrides.game = qmgGame(spec.game);
	overrides.mode = qmgMode(spec.mode);
	if (spec.seed >= 0) {
		overrides.seed = quint64(spec.seed);
	}
	if (spec.rooms > 0) {
		overrides.room_count = spec.rooms;
	}
	if (spec.players > 0) {
		overrides.target_players = spec.players;
	}
	overrides.verticality = verticalityValue(spec.verticality);
	overrides.intensity = intensityValue(spec.intensity);
	return overrides;
}

qmg::StageSwitches switchesFor(const LevelGenerationSpec& spec)
{
	std::vector<std::string> assignments;
	for (const QString& assignment : spec.switches) {
		assignments.push_back(toStd(assignment.trimmed()));
	}
	return qmg::parse_stage_switch_assignments(assignments);
}

AiChatRequest chatRequestFor(const qmg::TextGenerationRequest& request, const AiTextConnection& connection)
{
	AiChatRequest chat;
	chat.connectorId = connection.connectorId;
	chat.model = connection.model;
	chat.endpoint = connection.endpoint;
	chat.system = fromStd(request.system_prompt);
	chat.messages = {{QStringLiteral("user"), fromStd(request.user_prompt)}};
	chat.maxOutputTokens = int(request.max_output_tokens);
	chat.responseSchema = QJsonDocument::fromJson(QByteArray::fromStdString(request.json_schema)).object();
	chat.responseSchemaName = QStringLiteral("qmg_semantic_plan");
	// The planner's own grammar, for runtimes that decode under one (the Local GGUF connector).
	chat.grammar = fromStd(request.grammar);
	return chat;
}

// The plan object alone: some providers wrap structured answers in a Markdown fence.
QString unfencedAnswer(const QString& answer)
{
	QString text = answer.trimmed();
	if (text.startsWith(QStringLiteral("```"))) {
		const qsizetype firstLine = text.indexOf(QLatin1Char('\n'));
		const qsizetype closing = text.lastIndexOf(QStringLiteral("```"));
		if (firstLine > 0 && closing > firstLine) {
			text = text.mid(firstLine + 1, closing - firstLine - 1).trimmed();
		}
	}
	return text;
}

// A studio AI connector as Quake-MapGen's planner model: named by connector and model, with the
// provider's own token counts. Runs on the calling thread's event loop until the answer arrives.
class ConnectorTextModel final : public qmg::TextModel {
public:
	explicit ConnectorTextModel(LevelMapgenModel* model)
		: m_model(model)
	{
	}

	[[nodiscard]] qmg::ModelProvenance provenance() const override
	{
		// A GGUF file run here is named by its bytes, as Quake-MapGen's own llama.cpp backend did.
		return local() ? qmg::ModelProvenance::local_weights : qmg::ModelProvenance::named_service;
	}

	[[nodiscard]] qmg::TextGenerationResult generate(const qmg::TextGenerationRequest& request) override
	{
		if (m_model->cancelled && m_model->cancelled->load()) {
			m_model->failure = AiChatFailure::Cancelled;
			throw qmg::TextModelUnavailable("planning was cancelled");
		}
		AiChatClient client;
		client.setTimeoutMsecs(m_model->timeoutMsecs);
		QEventLoop loop;
		std::optional<AiChatResponse> answer;
		QString error;
		++m_model->requests;
		if (!client.send(chatRequestFor(request, m_model->connection), m_model->apiKey,
				[&](const AiChatResponse& response) {
					answer = response;
					loop.quit();
				},
				&error)) {
			m_model->failure = AiChatFailure::NotConfigured;
			m_model->failureText = error;
			throw qmg::TextModelUnavailable(toStd(error));
		}
		QTimer poll;
		poll.setInterval(100);
		QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
			if (m_model->cancelled && m_model->cancelled->load() && client.busy()) {
				client.cancel();
			}
		});
		poll.start();
		if (!answer) {
			loop.exec();
		}
		poll.stop();
		const AiChatResponse& response = *answer;
		m_model->inputTokens += std::max(0, response.inputTokens);
		m_model->outputTokens += std::max(0, response.outputTokens);
		m_model->elapsedMsecs += response.elapsedMsecs;
		if (!response.ok) {
			m_model->failure = response.failure;
			m_model->failureText = response.errorMessage.isEmpty() ? aiChatFailureText(response.failure) : response.errorMessage;
			throw qmg::TextModelUnavailable(toStd(m_model->failureText));
		}

		qmg::TextGenerationResult result;
		result.text = toStd(unfencedAnswer(response.text));
		result.prompt_tokens = size_t(std::max(0, response.inputTokens));
		// A harness counts every turn it took and its reasoning, not the plan alone, so its
		// count is left unreported (0) rather than read against the plan's output limit.
		result.output_tokens = aiConnectorIsHarness(m_model->connection.connectorId) ? 0 : size_t(std::max(0, response.outputTokens));
		result.finish_reason = response.truncated ? qmg::FinishReason::token_limit : qmg::FinishReason::end_of_generation;
		if (local()) {
			result.identity.backend = "llama.cpp/gguf";
			result.identity.backend_version = toStd(aiGgufRuntimeVersion());
			result.identity.model_id = toStd(response.model);
			result.identity.model_sha256 = toStd(response.modelSha256);
			result.identity.tokenizer_sha256 = result.identity.model_sha256;
			result.identity.provenance = qmg::ModelProvenance::local_weights;
			result.inference.device = "cpu";
			result.inference.threads = response.threads;
			result.inference.context_limit = size_t(std::max(0, response.contextTokens));
			result.inference.sampling = toStd(response.sampling);
			return result;
		}
		// A tool that picks its own model (Codex with none set) is named by its default, the same
		// on every revision.
		const QString modelId = m_model->connection.model.isEmpty() ? QStringLiteral("%1-default").arg(m_model->connection.connectorId) : m_model->connection.model;
		result.identity.backend = toStd(QStringLiteral("vibestudio/%1").arg(m_model->connection.connectorId));
		result.identity.backend_version = toStd(modelId);
		result.identity.model_id = toStd(modelId);
		result.identity.provenance = qmg::ModelProvenance::named_service;
		result.inference.device = m_model->connection.local ? "local-service" : "hosted-service";
		result.inference.sampling = "provider-default";
		return result;
	}

private:
	[[nodiscard]] bool local() const
	{
		return aiTextTransport(m_model->connection.connectorId) == AiTextTransport::Gguf;
	}

	LevelMapgenModel* m_model = nullptr;
};

QString roleOf(const qmg::Room& room)
{
	static const QSet<QString> roles = {QStringLiteral("start"), QStringLiteral("exit"), QStringLiteral("traversal"), QStringLiteral("combat"), QStringLiteral("resource"),
		QStringLiteral("setpiece"), QStringLiteral("spawn"), QStringLiteral("connector"), QStringLiteral("control"), QStringLiteral("vertical"), QStringLiteral("objective"),
		QStringLiteral("arena")};
	// The planned role is the last tag the generator adds; "start" and "exit" come first.
	for (auto tag = room.tags.rbegin(); tag != room.tags.rend(); ++tag) {
		const QString name = fromStd(*tag);
		if (roles.contains(name)) {
			return name;
		}
	}
	return room.tags.empty() ? QStringLiteral("room") : fromStd(room.tags.front());
}

QString placementKind(qmg::PlacementRole role)
{
	switch (role) {
	case qmg::PlacementRole::player_start:
		return QStringLiteral("player-start");
	case qmg::PlacementRole::deathmatch_spawn:
	case qmg::PlacementRole::team_initial_spawn:
	case qmg::PlacementRole::team_respawn:
	case qmg::PlacementRole::coop_start:
		return QStringLiteral("deathmatch-start");
	case qmg::PlacementRole::weapon:
		return QStringLiteral("weapon");
	case qmg::PlacementRole::health:
		return QStringLiteral("health");
	case qmg::PlacementRole::armor:
		return QStringLiteral("armor");
	case qmg::PlacementRole::monster:
		return QStringLiteral("monster");
	case qmg::PlacementRole::flag:
		return QStringLiteral("flag");
	case qmg::PlacementRole::light:
		return QStringLiteral("light");
	case qmg::PlacementRole::exit_trigger:
		return QStringLiteral("exit");
	case qmg::PlacementRole::intermission:
		return QStringLiteral("camera");
	case qmg::PlacementRole::teleporter_trigger:
		return QStringLiteral("teleporter");
	case qmg::PlacementRole::door:
	case qmg::PlacementRole::lift:
		return QStringLiteral("mover");
	case qmg::PlacementRole::exit_destination:
	case qmg::PlacementRole::door_trigger:
	case qmg::PlacementRole::door_destination:
	case qmg::PlacementRole::lift_trigger:
	case qmg::PlacementRole::lift_destination:
	case qmg::PlacementRole::teleporter_destination:
		return QString();
	}
	return QString();
}

bool coversXY(const qmg::GridBounds& bounds, double x, double y)
{
	return x >= bounds.min.x && x < bounds.max.x && y >= bounds.min.y && y < bounds.max.y;
}

std::optional<qmg::GridBounds> solidBounds(const qmg::LayoutSolid& solid)
{
	const qmg::BrushGeometryValidation validation = qmg::validate_brush_geometry(solid.geometry);
	if (!validation.ok()) {
		return std::nullopt;
	}
	return *validation.bounds;
}

QString liquidName(qmg::BrushContents contents)
{
	switch (contents) {
	case qmg::BrushContents::water:
		return QStringLiteral("water");
	case qmg::BrushContents::slime:
		return QStringLiteral("slime");
	case qmg::BrushContents::lava:
		return QStringLiteral("lava");
	case qmg::BrushContents::solid:
		break;
	}
	return QString();
}

// The layout as the studio's preview grid: open cells under rooms and passages (the highest floor
// where they stack, as seen from above), raised by platforms, stairs and ramps, with liquids.
void rasterize(const qmg::PipelineResult& pipeline, LevelGenerationResult* result)
{
	const qmg::Layout& level = pipeline.layout;
	struct FreeVolume {
		qmg::GridBounds bounds;
		QString owner;
	};
	std::vector<FreeVolume> free;
	for (const qmg::Room& room : level.rooms) {
		free.push_back({room.bounds, fromStd(room.name)});
	}
	for (const qmg::Connection& connection : level.connections) {
		for (const qmg::GridBounds& volume : connection.passage_volumes) {
			free.push_back({volume, QStringLiteral("c:%1").arg(connection.id.value)});
		}
	}
	LevelLayout& layout = result->layout;
	if (free.empty()) {
		return;
	}
	qint64 minX = std::numeric_limits<qint64>::max();
	qint64 minY = minX;
	qint64 maxX = std::numeric_limits<qint64>::min();
	qint64 maxY = maxX;
	for (const FreeVolume& volume : free) {
		minX = std::min<qint64>(minX, volume.bounds.min.x);
		minY = std::min<qint64>(minY, volume.bounds.min.y);
		maxX = std::max<qint64>(maxX, volume.bounds.max.x);
		maxY = std::max<qint64>(maxY, volume.bounds.max.y);
	}
	int cellSize = 32;
	while ((maxX - minX) / cellSize > 320 || (maxY - minY) / cellSize > 320) {
		cellSize *= 2;
	}
	const auto floorTo = [cellSize](qint64 value) { return qint64(std::floor(double(value) / cellSize)) * cellSize; };
	layout.cellSize = cellSize;
	layout.origin = QPoint(int(floorTo(minX) - cellSize), int(floorTo(minY) - cellSize));
	layout.width = int((maxX - layout.origin.x()) / cellSize) + 2;
	layout.height = int((maxY - layout.origin.y()) / cellSize) + 2;
	layout.cells.resize(qsizetype(layout.width) * layout.height);

	std::vector<std::pair<qmg::GridBounds, const qmg::LayoutSolid*>> raised;
	for (const qmg::LayoutSolid& solid : level.solids) {
		if (solid.contents != qmg::BrushContents::solid || solid.purpose == qmg::BrushPurpose::raised_platform || solid.purpose == qmg::BrushPurpose::navigation_stair
			|| solid.purpose == qmg::BrushPurpose::ramp || solid.purpose == qmg::BrushPurpose::lift) {
			if (const std::optional<qmg::GridBounds> bounds = solidBounds(solid)) {
				raised.emplace_back(*bounds, &solid);
			}
		}
	}
	for (int y = 0; y < layout.height; ++y) {
		for (int x = 0; x < layout.width; ++x) {
			const double cx = layout.origin.x() + (x + 0.5) * cellSize;
			const double cy = layout.origin.y() + (y + 0.5) * cellSize;
			const FreeVolume* top = nullptr;
			for (const FreeVolume& volume : free) {
				if (coversXY(volume.bounds, cx, cy) && (!top || volume.bounds.min.z > top->bounds.min.z)) {
					top = &volume;
				}
			}
			if (!top) {
				continue;
			}
			LevelCell& cell = layout.cell(x, y);
			cell.open = true;
			cell.floor = top->bounds.min.z;
			cell.ceiling = top->bounds.max.z;
			cell.owner = top->owner;
			for (const auto& [bounds, solid] : raised) {
				if (!coversXY(bounds, cx, cy) || bounds.max.z > cell.ceiling || bounds.min.z < cell.floor - 8) {
					continue;
				}
				const QString liquid = liquidName(solid->contents);
				if (!liquid.isEmpty()) {
					cell.liquid = liquid;
					cell.liquidTop = std::max(cell.liquidTop, int(bounds.max.z));
				} else {
					cell.floor = std::max(cell.floor, int(bounds.max.z));
				}
			}
		}
	}

	const auto cellRect = [&](const qmg::GridBounds& bounds) {
		const int left = int((bounds.min.x - layout.origin.x()) / cellSize);
		const int bottom = int((bounds.min.y - layout.origin.y()) / cellSize);
		const int right = int(std::ceil(double(bounds.max.x - layout.origin.x()) / cellSize)) - 1;
		const int topRow = int(std::ceil(double(bounds.max.y - layout.origin.y()) / cellSize)) - 1;
		return QRect(QPoint(left, bottom), QPoint(std::max(left, right), std::max(bottom, topRow)));
	};
	QHash<quint32, QString> roomNames;
	for (const qmg::Room& room : level.rooms) {
		LevelLayoutRoom placed;
		placed.id = fromStd(room.name);
		placed.name = placed.id;
		placed.role = roleOf(room);
		placed.rect = cellRect(room.bounds);
		placed.floor = room.bounds.min.z;
		placed.ceiling = room.bounds.max.z;
		placed.shape = QStringLiteral("box");
		placed.liquid = QStringLiteral("none");
		layout.rooms.push_back(placed);
		roomNames.insert(room.id.value, placed.id);
	}
	for (const qmg::Connection& connection : level.connections) {
		LevelLayoutCorridor corridor;
		corridor.from = roomNames.value(connection.from_room.value);
		corridor.to = roomNames.value(connection.to_room.value);
		corridor.width = std::max(1, int(connection.width / cellSize));
		for (const qmg::GridBounds& volume : connection.passage_volumes) {
			corridor.path << cellRect(volume).center();
		}
		layout.corridors.push_back(corridor);
	}

	// Entities, named by the classnames the map gave them.
	QHash<quint32, QString> classNames;
	if (pipeline.map_build) {
		QHash<qmg::MapNodeId, QString> entityClass;
		for (const qmg::MapEntity& entity : pipeline.map_build->document.entities()) {
			if (const std::optional<std::string_view> name = entity.first_value("classname")) {
				entityClass.insert(entity.id, QString::fromUtf8(name->data(), qsizetype(name->size())));
			}
		}
		for (const qmg::MapBuildManifestEntry& entry : pipeline.map_build->manifest) {
			if (entry.source.kind != qmg::MapBuildSourceKind::placement) {
				continue;
			}
			for (const qmg::MapNodeId node : entry.node_ids) {
				if (entityClass.contains(node)) {
					classNames.insert(entry.source.id, entityClass.value(node));
					break;
				}
			}
		}
	}
	for (const qmg::Placement& placement : level.placements) {
		const QString kind = placementKind(placement.role);
		if (kind.isEmpty()) {
			continue;
		}
		LevelPlacedEntity entity;
		entity.kind = kind;
		entity.className = classNames.value(placement.id.value);
		entity.item = entity.className;
		entity.x = placement.origin.x;
		entity.y = placement.origin.y;
		entity.z = placement.origin.z;
		entity.angle = placement.angle_degrees.value_or(0);
		for (const qmg::Room& room : level.rooms) {
			if (room.bounds.contains(placement.origin)) {
				entity.room = fromStd(room.name);
				break;
			}
		}
		layout.entities.push_back(entity);
	}
}

QString lightingWord(qmg::LightingStyle style)
{
	switch (style) {
	case qmg::LightingStyle::low_key:
		return QStringLiteral("dim");
	case qmg::LightingStyle::balanced:
		return QStringLiteral("normal");
	case qmg::LightingStyle::high_contrast:
		return QStringLiteral("bright");
	}
	return QStringLiteral("normal");
}

QString sizeWord(const qmg::GridBounds& bounds)
{
	const double area = double(bounds.max.x - bounds.min.x) * double(bounds.max.y - bounds.min.y);
	return area < 512.0 * 512.0 ? QStringLiteral("small") : area > 832.0 * 832.0 ? QStringLiteral("large") : QStringLiteral("medium");
}

// The plan as the studio's plan structure: rooms in order with their roles, the passages, and what
// each room holds (by classname).
LevelSemanticPlan studioPlan(const qmg::PipelineResult& pipeline, const LevelGenerationResult& result)
{
	const qmg::Layout& level = pipeline.layout;
	LevelSemanticPlan plan;
	plan.title = result.spec.title;
	plan.theme = fromStd(level.spec.theme);
	plan.summary = QCoreApplication::translate("VibeStudioLevelMapgen", "%1 route, %2 symmetry, %3 lighting; verticality %4, intensity %5 (of 100).")
					   .arg(fromStd(qmg::to_string(pipeline.semantic_plan.route_style)), fromStd(qmg::to_string(pipeline.semantic_plan.symmetry)),
						   fromStd(qmg::to_string(pipeline.semantic_plan.lighting_style)))
					   .arg(pipeline.semantic_plan.verticality)
					   .arg(pipeline.semantic_plan.intensity);
	QHash<quint32, QString> roomNames;
	for (const qmg::Room& room : level.rooms) {
		LevelPlanRoom planned;
		planned.id = fromStd(room.name);
		planned.name = planned.id;
		planned.role = roleOf(room);
		planned.size = sizeWord(room.bounds);
		planned.level = std::clamp(int(room.bounds.min.z / 64), 0, 3);
		planned.lighting = lightingWord(level.lighting_style);
		plan.rooms.push_back(planned);
		roomNames.insert(room.id.value, planned.id);
	}
	for (const qmg::Connection& connection : level.connections) {
		LevelPlanConnection link;
		link.from = roomNames.value(connection.from_room.value);
		link.to = roomNames.value(connection.to_room.value);
		link.kind = QStringLiteral("passage");
		plan.connections.push_back(link);
	}
	QHash<QString, int> counts;
	QStringList order;
	for (const LevelPlacedEntity& entity : result.layout.entities) {
		if (entity.room.isEmpty() || entity.kind == QStringLiteral("light") || entity.kind == QStringLiteral("camera")) {
			continue;
		}
		const QString key = entity.room + QLatin1Char('\n') + entity.kind + QLatin1Char('\n') + entity.className;
		if (!counts.contains(key)) {
			order << key;
		}
		++counts[key];
	}
	for (const QString& key : order) {
		const QStringList parts = key.split(QLatin1Char('\n'));
		LevelPlanPlacement placement;
		placement.room = parts.value(0);
		placement.kind = parts.value(1);
		placement.item = parts.value(2);
		placement.count = counts.value(key);
		plan.placements.push_back(placement);
	}
	const QString backend = fromStd(pipeline.planner.used_backend);
	plan.planner = QStringLiteral("quake-mapgen:%1").arg(backend);
	return plan;
}

QString severityWord(const QString& severity)
{
	return severity == QStringLiteral("error") ? QCoreApplication::translate("VibeStudioLevelMapgen", "error") : QCoreApplication::translate("VibeStudioLevelMapgen", "warning");
}

// "3 errors, 2 warnings" over a findings array, with the codes that occur, most frequent first.
QString findingsText(const QJsonArray& findings)
{
	if (findings.isEmpty()) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "no findings");
	}
	int errors = 0;
	int warnings = 0;
	QHash<QString, int> codes;
	for (const QJsonValue& value : findings) {
		const QJsonObject finding = value.toObject();
		(finding.value(QStringLiteral("severity")).toString() == QStringLiteral("error") ? errors : warnings) += 1;
		codes[finding.value(QStringLiteral("code")).toString()] += 1;
	}
	QStringList names = codes.keys();
	std::sort(names.begin(), names.end(), [&](const QString& a, const QString& b) { return codes.value(a) != codes.value(b) ? codes.value(a) > codes.value(b) : a < b; });
	QStringList parts;
	for (const QString& name : names) {
		parts << QStringLiteral("%1 x%2").arg(name).arg(codes.value(name));
	}
	return QCoreApplication::translate("VibeStudioLevelMapgen", "errors %1, warnings %2 (%3)").arg(errors).arg(warnings).arg(parts.join(QStringLiteral(", ")));
}

QString agentName(const QString& agent)
{
	if (agent == QStringLiteral("player")) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "player");
	}
	if (agent == QStringLiteral("monster_small")) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "small monsters");
	}
	if (agent == QStringLiteral("monster_large")) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "large monsters");
	}
	return agent;
}

// The report's analysis as lines: the quality verdict first, one line per analysis, then each
// finding that carries a message.
QStringList analysisLines(const QJsonObject& report)
{
	QStringList lines;
	const QJsonObject analysis = report.value(QStringLiteral("analysis")).toObject();
	if (analysis.isEmpty()) {
		return lines;
	}
	const QJsonObject quality = analysis.value(QStringLiteral("quality")).toObject();
	const QJsonObject score = quality.value(QStringLiteral("score")).toObject();
	if (quality.value(QStringLiteral("available")).toBool()) {
		const int errors = score.value(QStringLiteral("structural_errors")).toInt() + score.value(QStringLiteral("leak_errors")).toInt()
			+ score.value(QStringLiteral("navigation_errors")).toInt() + score.value(QStringLiteral("visibility_errors")).toInt()
			+ score.value(QStringLiteral("lighting_errors")).toInt() + score.value(QStringLiteral("timing_errors")).toInt();
		const int warnings = score.value(QStringLiteral("warnings")).toInt();
		lines << QCoreApplication::translate("VibeStudioLevelMapgen",
			"Analysis (advisory): error findings %1 (navigation %2, visibility %3, lighting %4, timing %5), warnings %6.")
					 .arg(errors)
					 .arg(score.value(QStringLiteral("navigation_errors")).toInt())
					 .arg(score.value(QStringLiteral("visibility_errors")).toInt())
					 .arg(score.value(QStringLiteral("lighting_errors")).toInt())
					 .arg(score.value(QStringLiteral("timing_errors")).toInt())
					 .arg(warnings);
	}
	QVector<QPair<QString, QJsonArray>> detailed;
	const QJsonObject navigation = analysis.value(QStringLiteral("navigation")).toObject();
	for (const QString& agent : {QStringLiteral("player"), QStringLiteral("monster_small"), QStringLiteral("monster_large")}) {
		const QJsonObject result = navigation.value(agent).toObject();
		if (!result.value(QStringLiteral("available")).toBool()) {
			continue;
		}
		const QJsonArray findings = result.value(QStringLiteral("findings")).toArray();
		lines << QCoreApplication::translate("VibeStudioLevelMapgen", "Navigation for %1: %2.").arg(agentName(agent), findingsText(findings));
		detailed.append({agentName(agent), findings});
	}
	const QJsonObject visibility = analysis.value(QStringLiteral("visibility")).toObject();
	if (visibility.value(QStringLiteral("available")).toBool()) {
		const QJsonObject summary = visibility.value(QStringLiteral("summary")).toObject();
		const QJsonArray findings = visibility.value(QStringLiteral("findings")).toArray();
		lines << QCoreApplication::translate("VibeStudioLevelMapgen", "Visibility: %1 of %2 spawns safe, chokepoint links %3; %4.")
					 .arg(summary.value(QStringLiteral("safe_spawns")).toInt())
					 .arg(summary.value(QStringLiteral("evaluated_spawns")).toInt())
					 .arg(summary.value(QStringLiteral("chokepoint_connections")).toInt())
					 .arg(findingsText(findings));
		detailed.append({QCoreApplication::translate("VibeStudioLevelMapgen", "visibility"), findings});
	}
	const QJsonObject lighting = analysis.value(QStringLiteral("lighting")).toObject();
	if (lighting.value(QStringLiteral("available")).toBool()) {
		const QJsonObject summary = lighting.value(QStringLiteral("summary")).toObject();
		const QJsonArray findings = lighting.value(QStringLiteral("findings")).toArray();
		lines << QCoreApplication::translate("VibeStudioLevelMapgen", "Lighting: lights %1, %2 of %3 sample points readable; %4.")
					 .arg(summary.value(QStringLiteral("lights")).toInt())
					 .arg(summary.value(QStringLiteral("readable_samples")).toInt())
					 .arg(summary.value(QStringLiteral("samples")).toInt())
					 .arg(findingsText(findings));
		detailed.append({QCoreApplication::translate("VibeStudioLevelMapgen", "lighting"), findings});
	}
	if (quality.value(QStringLiteral("available")).toBool()) {
		const QJsonObject summary = quality.value(QStringLiteral("summary")).toObject();
		const QJsonArray findings = quality.value(QStringLiteral("findings")).toArray();
		lines << QCoreApplication::translate("VibeStudioLevelMapgen", "Travel: %1 of %2 timed routes reachable; %3.")
					 .arg(summary.value(QStringLiteral("reachable_timing_pairs")).toInt())
					 .arg(summary.value(QStringLiteral("timing_pairs")).toInt())
					 .arg(findingsText(findings));
		detailed.append({QCoreApplication::translate("VibeStudioLevelMapgen", "travel"), findings});
	}
	// The findings themselves, at most 40 so a long list stays readable; the report has them all.
	int shown = 0;
	int hidden = 0;
	for (const auto& [area, findings] : detailed) {
		for (const QJsonValue& value : findings) {
			const QJsonObject finding = value.toObject();
			if (shown >= 40) {
				++hidden;
				continue;
			}
			const QString message = finding.value(QStringLiteral("message")).toString();
			lines << QCoreApplication::translate("VibeStudioLevelMapgen", "- %1 %2 (%3)%4")
						 .arg(severityWord(finding.value(QStringLiteral("severity")).toString()), finding.value(QStringLiteral("code")).toString(), area,
							 message.isEmpty() ? QString() : QStringLiteral(": %1").arg(message));
			++shown;
		}
	}
	if (hidden > 0) {
		lines << QCoreApplication::translate("VibeStudioLevelMapgen", "%n more finding(s) are in the report.", nullptr, hidden);
	}
	return lines;
}

QString formatFor(qmg::Game game)
{
	switch (game) {
	case qmg::Game::quake1:
		return QStringLiteral("quake-map");
	case qmg::Game::quake2:
		return QStringLiteral("quake2-map");
	case qmg::Game::quake3:
		return QStringLiteral("quake3-map");
	}
	return QString();
}

// Replaces worldspawn's message with a title the user chose.
void retitle(qmg::MapDocument* document, const QString& title)
{
	std::vector<qmg::MapEntity>& entities = document->edit_entities();
	if (entities.empty()) {
		return;
	}
	for (qmg::MapEntityProperty& property : entities.front().properties) {
		if (property.key == "message") {
			property.value = toStd(title);
			return;
		}
	}
}

} // namespace

QString levelMapgenGeneratorId()
{
	return QStringLiteral("quake-mapgen");
}

QStringList levelMapgenGameIds()
{
	return {QStringLiteral("quake"), QStringLiteral("quake2"), QStringLiteral("quake3")};
}

QStringList levelMapgenModeIds()
{
	return {QStringLiteral("single-player"), QStringLiteral("deathmatch"), QStringLiteral("duel"), QStringLiteral("ctf")};
}

QStringList levelMapgenFormatIds(const QString& game)
{
	const QString id = normalizedGame(game);
	if (id == QStringLiteral("quake")) {
		return {QStringLiteral("q1-valve220")};
	}
	if (id == QStringLiteral("quake2")) {
		return {QStringLiteral("q2-valve220")};
	}
	if (id == QStringLiteral("quake3")) {
		return {QStringLiteral("q3-legacy"), QStringLiteral("q3-brush-primitives")};
	}
	return {QStringLiteral("q1-valve220"), QStringLiteral("q2-valve220"), QStringLiteral("q3-legacy"), QStringLiteral("q3-brush-primitives")};
}

QStringList levelMapgenIntensityIds()
{
	return {QStringLiteral("quiet"), QStringLiteral("balanced"), QStringLiteral("intense")};
}

QStringList levelMapgenSwitchNames()
{
	QStringList names;
	for (const qmg::StageSwitchDefinition& definition : qmg::stage_switch_registry()) {
		names << QString::fromUtf8(definition.name.data(), qsizetype(definition.name.size()));
	}
	return names;
}

QStringList levelMapgenRankerIds()
{
	return qmg::ggml_ranker_available() ? QStringList {QStringLiteral("scalar"), QStringLiteral("ggml")} : QStringList {QStringLiteral("scalar")};
}

QString levelMapgenVersion()
{
	return QStringLiteral(QMG_VERSION);
}

QString levelMapgenSpecProblem(const LevelGenerationSpec& spec)
{
	if (!spec.game.trimmed().isEmpty() && !qmgGame(spec.game)) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "Quake-MapGen builds Quake, Quake II, and Quake III Arena levels, not %1; use the studio generator for it.")
			.arg(spec.game);
	}
	if (!spec.mode.trimmed().isEmpty() && !qmgMode(spec.mode)) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "Quake-MapGen's modes are %1, not %2.").arg(levelMapgenModeIds().join(QStringLiteral(", ")), spec.mode);
	}
	if (!spec.format.isEmpty()) {
		if (!qmgDialect(spec.format)) {
			return QCoreApplication::translate("VibeStudioLevelMapgen", "The map formats are %1, not %2.").arg(levelMapgenFormatIds().join(QStringLiteral(", ")), spec.format);
		}
		if (qmgGame(spec.game) && !levelMapgenFormatIds(spec.game).contains(spec.format)) {
			return QCoreApplication::translate("VibeStudioLevelMapgen", "%1 is not a %2 map format; it takes %3.")
				.arg(spec.format, normalizedGame(spec.game), levelMapgenFormatIds(spec.game).join(QStringLiteral(", ")));
		}
	}
	if (!spec.intensity.isEmpty() && !intensityValue(spec.intensity)) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "Intensity is one of %1, not %2.").arg(levelMapgenIntensityIds().join(QStringLiteral(", ")), spec.intensity);
	}
	if (!spec.ranker.isEmpty() && !levelMapgenRankerIds().contains(spec.ranker)) {
		return spec.ranker == QStringLiteral("ggml")
			? QCoreApplication::translate("VibeStudioLevelMapgen", "This build has no ggml to rank with; configure it with -Dggml=enabled, or use the scalar ranker, which gives the same scores.")
			: QCoreApplication::translate("VibeStudioLevelMapgen", "The rankers are %1, not %2.").arg(levelMapgenRankerIds().join(QStringLiteral(", ")), spec.ranker);
	}
	if (spec.candidates < 1 || spec.candidates > 64) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "Quake-MapGen ranks 1 to 64 candidate layouts.");
	}
	if (spec.repairIterations < 0 || spec.repairIterations > 64) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "Quake-MapGen runs 0 to 64 repair passes.");
	}
	try {
		static_cast<void>(switchesFor(spec));
	} catch (const std::exception& error) {
		return QCoreApplication::translate("VibeStudioLevelMapgen", "Stage switches: %1. Known switches: %2.").arg(fromStd(error.what()), levelMapgenSwitchNames().join(QStringLiteral(", ")));
	}
	return QString();
}

bool levelMapgenPlanAiRequest(const LevelGenerationSpec& spec, const AiTextConnection& connection, AiChatRequest* request, QString* error)
{
	const QString problem = levelMapgenSpecProblem(spec);
	if (!problem.isEmpty()) {
		if (error) {
			*error = problem;
		}
		return false;
	}
	try {
		qmg::PromptSpec parsed = qmg::PromptParser {}.parse(toStd(promptFor(spec)), overridesFor(spec));
		parsed.switches = switchesFor(spec);
		qmg::PlannerOptions options;
		options.max_output_tokens = 4096;
		const qmg::TextGenerationRequest first = qmg::semantic_plan_initial_request(parsed, options);
		if (request) {
			*request = chatRequestFor(first, connection);
		}
		return true;
	} catch (const std::exception& failure) {
		if (error) {
			*error = fromStd(failure.what());
		}
		return false;
	}
}

LevelGenerationResult generateMapgenLevel(const LevelGenerationSpec& input, LevelMapgenModel* model)
{
	LevelGenerationResult result;
	result.generator = levelMapgenGeneratorId();
	result.spec = input;
	result.spec.generator = levelMapgenGeneratorId();
	if (const QString problem = levelMapgenSpecProblem(input); !problem.isEmpty()) {
		result.error = problem;
		return result;
	}
	std::optional<ConnectorTextModel> connector;
	if (model) {
		model->maxOutputTokens = std::clamp(model->maxOutputTokens, 64, 4096);
		model->maxRevisions = std::clamp(model->maxRevisions, 0, 8);
		connector.emplace(model);
	}
	qmg::PipelineOptions options;
	options.candidate_count = size_t(input.candidates);
	options.max_repair_iterations = input.repairIterations;
	options.dialect = input.format.isEmpty() ? std::nullopt : qmgDialect(input.format);
	options.text_model = connector ? &*connector : nullptr;
	if (model) {
		options.planner_max_output_tokens = size_t(model->maxOutputTokens);
		options.planner_max_revisions = size_t(model->maxRevisions);
	}
	options.rules_fallback = true;
	options.ranker = input.ranker == QStringLiteral("ggml") ? qmg::RankerBackend::ggml : qmg::RankerBackend::scalar;

	qmg::PipelineResult pipeline;
	try {
		options.switches = switchesFor(input);
		pipeline = qmg::GenerationPipeline {}.run(toStd(promptFor(input)), overridesFor(input), options);
	} catch (const qmg::TextModelUnavailable& error) {
		const bool cancelled = model && model->failure == AiChatFailure::Cancelled;
		result.error = cancelled ? QCoreApplication::translate("VibeStudioLevelMapgen", "Cancelled. Nothing was built.")
								 : QCoreApplication::translate("VibeStudioLevelMapgen", "The text model did not plan the level: %1").arg(fromStd(error.what()));
		return result;
	} catch (const std::exception& error) {
		result.error = fromStd(error.what());
		return result;
	}

	const qmg::PromptSpec& parsed = pipeline.layout.spec;
	result.spec.prompt = input.prompt;
	result.spec.game = studioGame(parsed.game);
	result.spec.mode = studioMode(parsed.mode);
	result.spec.theme = fromStd(parsed.theme);
	result.spec.rooms = parsed.room_count;
	result.spec.players = parsed.target_players;
	result.spec.seed = parsed.seed > quint64(std::numeric_limits<qint64>::max()) ? -1 : qint64(parsed.seed);
	result.spec.title = input.title.trimmed().isEmpty() ? fromStd(parsed.title) : input.title.trimmed();
	result.spec.format = formatId(pipeline.dialect);
	result.dialect = result.spec.format;
	result.format = formatFor(parsed.game);
	result.suggestedFileName = QStringLiteral("qmg_%1_%2_%3.map").arg(result.spec.game, result.spec.mode == QStringLiteral("single-player") ? QStringLiteral("sp") : result.spec.mode,
		QString::number(parsed.seed));

	if (pipeline.map_build && !input.title.trimmed().isEmpty()) {
		retitle(&pipeline.map_build->document, input.title.trimmed());
	}
	try {
		result.generatorReport = QByteArray::fromStdString(qmg::render_report(pipeline));
		if (pipeline.validation.ok() && pipeline.map_build) {
			result.mapBytes = QByteArray::fromStdString(qmg::MapWriter {}.render(pipeline.map_build->document));
		}
	} catch (const std::exception& error) {
		result.error = fromStd(error.what());
		return result;
	}
	const QJsonObject report = QJsonDocument::fromJson(result.generatorReport).object();
	result.analysis = analysisLines(report);

	rasterize(pipeline, &result);
	result.plan = studioPlan(pipeline, result);

	LevelGenerationStatistics& stats = result.statistics;
	stats.rooms = int(pipeline.layout.rooms.size());
	stats.corridors = int(pipeline.layout.connections.size());
	for (const LevelCell& cell : result.layout.cells) {
		stats.openCells += cell.open ? 1 : 0;
	}
	if (pipeline.map_build) {
		stats.brushes = int(pipeline.map_build->stats.brush_count);
		stats.entities = int(pipeline.map_build->stats.entity_count);
	}
	for (const qmg::Placement& placement : pipeline.layout.placements) {
		stats.lights += placement.role == qmg::PlacementRole::light ? 1 : 0;
		stats.monsters += placement.role == qmg::PlacementRole::monster ? 1 : 0;
		stats.items += (placement.role == qmg::PlacementRole::weapon || placement.role == qmg::PlacementRole::health || placement.role == qmg::PlacementRole::armor) ? 1 : 0;
	}
	// Rooms the start room's passages lead to.
	if (!pipeline.layout.rooms.empty()) {
		std::set<quint32> reached = {pipeline.layout.rooms.front().id.value};
		for (bool grew = true; grew;) {
			grew = false;
			for (const qmg::Connection& connection : pipeline.layout.connections) {
				const bool from = reached.count(connection.from_room.value) > 0;
				const bool to = reached.count(connection.to_room.value) > 0;
				if (from != to) {
					reached.insert(from ? connection.to_room.value : connection.from_room.value);
					grew = true;
				}
			}
		}
		stats.reachableRooms = int(reached.size());
	}
	stats.extentUnits = QSize(result.layout.width * result.layout.cellSize, result.layout.height * result.layout.cellSize);

	result.notes << QCoreApplication::translate("VibeStudioLevelMapgen", "Quake-MapGen %1 ranked %n candidate layout(s) and kept the best (score %2), written as %3.", nullptr,
		input.candidates)
						.arg(levelMapgenVersion())
						.arg(pipeline.layout.rank_score, 0, 'f', 2)
						.arg(result.dialect);
	if (!pipeline.planner_fallback.empty()) {
		result.warnings << QCoreApplication::translate("VibeStudioLevelMapgen", "The text model's plan could not be used (%1); the rules planned the level instead.")
							   .arg(fromStd(pipeline.planner_fallback));
	} else if (pipeline.planner.attempts > 0) {
		result.notes << QCoreApplication::translate("VibeStudioLevelMapgen", "The text model planned it in %n attempt(s).", nullptr, int(pipeline.planner.attempts));
	}
	for (const qmg::RepairPass& pass : pipeline.repairs) {
		result.notes << QCoreApplication::translate("VibeStudioLevelMapgen", "Repair pass %1 addressed %n issue(s).", nullptr, int(pass.before.size())).arg(pass.iteration);
	}
	for (const qmg::Issue& issue : pipeline.validation.issues) {
		const QString line = QStringLiteral("%1: %2").arg(fromStd(issue.code), fromStd(issue.message));
		(issue.severity == qmg::Severity::error ? result.warnings : result.notes) << line;
	}
	if (!pipeline.validation.ok() || !pipeline.map_build) {
		result.error = QCoreApplication::translate("VibeStudioLevelMapgen", "The layout failed validation after %n repair pass(es), so no map was written; the report says why.", nullptr,
			int(pipeline.repairs.size()));
		return result;
	}
	result.ok = true;
	return result;
}

QJsonObject levelMapgenUsageJson(const LevelMapgenModel& model)
{
	return QJsonObject {
		{QStringLiteral("connector"), model.connection.connectorId},
		{QStringLiteral("model"), model.connection.model},
		{QStringLiteral("host"), QUrl(model.connection.endpoint).host()},
		{QStringLiteral("requests"), model.requests},
		{QStringLiteral("inputTokens"), model.inputTokens},
		{QStringLiteral("outputTokens"), model.outputTokens},
		{QStringLiteral("elapsedMs"), double(model.elapsedMsecs)},
	};
}

} // namespace vibestudio
