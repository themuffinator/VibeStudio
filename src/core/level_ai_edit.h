#pragma once

// Map edits a text model proposes, applied only after review.
//
// The model sees a compact summary of the open map (objects by their `kind:id`
// selectors, with classnames, origins, keys, brush bounds and textures) and
// answers in a fixed vocabulary of actions against a JSON Schema (structured
// output where the provider has it). Every action is checked against the map
// before it is shown, and an accepted one runs through the editor's own
// undoable operations (addLevelMapEntity, setLevelMapEntitiesProperty,
// addLevelMapBoxBrush, applyLevelMapTexture, moveLevelMapSelection,
// deleteLevelMapObjects), as a person's edit would, never by rewriting text.
// Quake-family maps only: Doom geometry has other tools.

#include "core/ai_transport.h"
#include "core/level_map.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct LevelAiEditAction {
	// add-entity, set-key, remove-key, add-box, set-texture, move, or delete.
	QString kind;
	// The model's one-line reason.
	QString reason;
	// Objects acted on, as `kind:id` selectors from the summary.
	QStringList targets;
	QString classname;
	LevelMapVec3 origin;
	QVector<LevelMapProperty> keys;
	QString key;
	QString value;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	QString texture;
	LevelMapVec3 delta;

	// Set by validateLevelAiEditProposal: what the action does, in words,
	// and why it cannot run, if it cannot.
	QString description;
	QStringList problems;
	// The reviewer's choice, kept in a saved proposal; validation unchecks
	// an action with problems and never checks one again.
	bool enabled = true;

	[[nodiscard]] bool valid() const
	{
		return problems.isEmpty();
	}
};

struct LevelAiEditProposal {
	QString summary;
	QVector<LevelAiEditAction> actions;
	// What did not fit the schema.
	QStringList problems;
};

[[nodiscard]] QStringList levelAiEditActionKinds();
// The JSON Schema a proposal must match: strict (every object closed, every
// property required, unused fields empty).
[[nodiscard]] QJsonObject levelAiEditSchema();
// The open map as the model sees it, at most `maxObjects` entities and as
// many brushes; what is left out is counted. Paths under the project and home
// folders are shortened.
[[nodiscard]] QString levelAiEditMapContext(const LevelMapDocument& document, const QString& projectRoot = QString(), const QString& homeDirectory = QString(),
	int maxObjects = 300);
[[nodiscard]] QString levelAiEditSystemPrompt();
[[nodiscard]] AiChatRequest levelAiEditRequest(const LevelMapDocument& document, const QString& instruction, const QString& connectorId, const QString& model,
	const QString& endpoint, const QString& projectRoot = QString(), const QString& homeDirectory = QString());
// Reads a model's answer; false when it holds no proposal. Schema problems go
// to proposal->problems; a readable proposal is returned even with them.
bool levelAiEditProposalFromAnswer(const QString& answer, LevelAiEditProposal* proposal, QString* error = nullptr);
bool levelAiEditProposalFromJson(const QJsonObject& object, LevelAiEditProposal* proposal, QString* error = nullptr);
[[nodiscard]] QJsonObject levelAiEditProposalJson(const LevelAiEditProposal& proposal);
// Describes each action and checks it against the map: targets exist and are
// the right kind, numbers are finite and in range, names hold no quotes or
// line breaks. An action with problems is disabled; the rest keep their
// `enabled` choice.
void validateLevelAiEditProposal(const LevelMapDocument& document, LevelAiEditProposal* proposal);

struct LevelAiEditApplyReport {
	int applied = 0;
	int skipped = 0;
	// One line per action, in the order they ran.
	QStringList lines;
	QStringList errors;
	// Indexes into the proposal's actions: those that ran, and those that
	// were tried and failed. Unchecked and invalid actions are in neither.
	QVector<int> appliedActions;
	QVector<int> failedActions;
};

// Runs the enabled, valid actions, deletes last so other actions' targets keep
// their ids. Each runs as its own undo step; a failure is reported and the
// rest go on. The selection is left on the last objects acted on.
LevelAiEditApplyReport applyLevelAiEditProposal(LevelMapDocument* document, const LevelAiEditProposal& proposal);

} // namespace vibestudio
