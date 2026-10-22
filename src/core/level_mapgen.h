#pragma once

// Quake-MapGen in the studio: the generator in src/mapgen (imported from
// https://github.com/themuffinator/Quake-MapGen, MIT; see src/mapgen/VIBESTUDIO.md and
// docs/CREDITS.md) as a second level generator beside core/level_generation's cell generator.
//
// A prompt and the spec's overrides become Quake-MapGen's request; its rules planner, or a text
// model through the studio's own AI connectors, makes the coordinate-free plan; it lays out,
// validates, repairs, and lowers the level into one of four map dialects; then it analyses
// navigation, visibility, lighting, and quality before anything is written. The result arrives as
// an ordinary LevelGenerationResult: map text that opens in Levels, a preview layout, the plan as a
// person reads it, the analysis, and Quake-MapGen's own schema v4 report.
//
// Generation blocks, and with a text model it waits for each answer on a local event loop, so the
// studio runs it on a worker thread; the CLI runs it inline.

#include "core/ai_transport.h"
#include "core/level_generation.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace vibestudio {

[[nodiscard]] QString levelMapgenGeneratorId();
// quake, quake2, and quake3.
[[nodiscard]] QStringList levelMapgenGameIds();
// single-player, deathmatch, duel, and ctf.
[[nodiscard]] QStringList levelMapgenModeIds();
// The map dialects a game writes (all four for an empty game); the first is its default.
[[nodiscard]] QStringList levelMapgenFormatIds(const QString& game = QString());
// quiet, balanced, and intense.
[[nodiscard]] QStringList levelMapgenIntensityIds();
// The registered stage switches, by name.
[[nodiscard]] QStringList levelMapgenSwitchNames();
// scalar, and ggml in builds that link it.
[[nodiscard]] QStringList levelMapgenRankerIds();
// The generator's version, as its report records it.
[[nodiscard]] QString levelMapgenVersion();

// What stops Quake-MapGen building this spec (a game it lacks, a mode, a format, a switch); empty
// when nothing does.
[[nodiscard]] QString levelMapgenSpecProblem(const LevelGenerationSpec& spec);

// A text model that plans for Quake-MapGen: the connection and key the studio resolved, how long to
// wait, and a flag another thread sets to stop waiting. What the model used is filled in.
struct LevelMapgenModel {
	AiTextConnection connection;
	QString apiKey;
	int timeoutMsecs = 5 * 60 * 1000;
	// Planner output limit per answer, 64 to 4096 tokens.
	int maxOutputTokens = 4096;
	// Corrections the planner may ask for after an invalid plan, 0 to 8.
	int maxRevisions = 2;
	std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);

	int requests = 0;
	int inputTokens = 0;
	int outputTokens = 0;
	qint64 elapsedMsecs = 0;
	// The last failure the connector reported, if any.
	AiChatFailure failure = AiChatFailure::None;
	QString failureText;
};

// The first request the planner sends the model, as the studio sends it, so it can be previewed and
// confirmed before anything leaves the machine. Later requests repeat it with a rejected answer and
// its problems.
bool levelMapgenPlanAiRequest(const LevelGenerationSpec& spec, const AiTextConnection& connection, AiChatRequest* request, QString* error = nullptr);

// Plans (by the rules, or by `model`), lays out, validates, builds, and analyses the level.
// Failure is ok == false with the reason; a failed validation still returns its report.
[[nodiscard]] LevelGenerationResult generateMapgenLevel(const LevelGenerationSpec& spec, LevelMapgenModel* model = nullptr);

// The usage `model` recorded, for reports: connector, model, host, tokens, and time.
[[nodiscard]] QJsonObject levelMapgenUsageJson(const LevelMapgenModel& model);

} // namespace vibestudio
