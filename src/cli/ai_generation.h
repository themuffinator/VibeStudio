#pragma once

// Generative commands: `map generate`, `map plan`, `map ai-edit`, `texture
// generate`, `texture derive`, `asset audio-generate`, and `ai image`. They
// follow `ai ask`'s rules: AI-free mode and cloud opt-in come from settings, a
// request leaves the machine only with --yes, and --dry-run shows the request
// (without its key) and sends nothing. Every command also has a path that
// needs no AI: the rules planner for maps, a saved --proposal for map edits,
// --from-image and `texture derive` for textures, the synthesizer for sounds.

#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {

struct AiGenerationCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};

AiGenerationCliResult runMapGenerate(const QStringList& arguments);
AiGenerationCliResult runMapPlan(const QStringList& arguments);
AiGenerationCliResult runMapAiEdit(const QStringList& arguments);
AiGenerationCliResult runTextureGenerate(const QStringList& arguments);
// `asset audio-generate`: the synthesizer by default, --source ai for the
// sound model.
AiGenerationCliResult runAudioGenerate(const QStringList& arguments);
AiGenerationCliResult runTextureDerive(const QStringList& arguments);
AiGenerationCliResult runAiImage(const QStringList& arguments);

} // namespace vibestudio::cli
