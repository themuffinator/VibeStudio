#pragma once

// Parsing, printing and editing of material scripts: Quake III `.shader`
// files and Doom 3 `.mtr` decl files.
//
// Parsing follows each engine's own tokenizer so a script means here what it
// means in game:
// - Quake III splits on whitespace (COM_ParseExt): `rgbGen const ( 1 0 0 )`
//   needs its spaces, and `//` and `/* */` are comments. Arguments end at
//   the end of the line.
// - Doom 3 uses idLexer with path names allowed: punctuation is its own
//   token, newlines mean nothing, and expressions follow idMaterial's
//   precedence, where the right operand is parsed at the same priority, so
//   `a - b - c` is `a - (b - c)`.
//
// Editing never regenerates a script: each edit patches the text spans the
// parser recorded, keeping comments and layout, and the result is parsed
// again so the edit is validated by the same rules as typing.

#include "core/material_model.h"

namespace vibestudio {

// Quake III for .shader, Doom 3 for .mtr; Unknown otherwise.
MaterialEngine materialEngineForScriptPath(const QString& path);
bool isMaterialScriptPath(const QString& path);

MaterialScript parseMaterialScript(const QString& text, MaterialEngine engine, const QString& path = QString());
bool loadMaterialScript(const QString& path, MaterialScript* script, QString* error = nullptr,
	MaterialEngine engine = MaterialEngine::Unknown);

// Parses a Doom 3 expression on its own, for the graph editor and the CLI.
// Nodes are appended to `nodes`; returns the root or -1 with `error` set.
int parseMaterialExpression(const QString& text, QVector<MaterialExpressionNode>* nodes, QString* error = nullptr);
// Prints an expression with the parentheses Doom 3's grammar needs, keeping
// any the source had.
QString materialExpressionText(const QVector<MaterialExpressionNode>& nodes, int root);
// Image programs print back as `heightmap( textures/x_h.tga, 4 )`.
QString materialImageProgramText(const QVector<MaterialImageProgramNode>& nodes, int root);
// The images an image program reads, depth first.
QStringList materialImageProgramImages(const QVector<MaterialImageProgramNode>& nodes, int root);

// Keyword catalogues, for completion, highlighting and the graph editor's
// property choices. Lower case.
QStringList materialGlobalKeywords(MaterialEngine engine);
QStringList materialStageKeywords(MaterialEngine engine);
// One-line help for a keyword, translated; empty when unknown.
QString materialKeywordHelp(MaterialEngine engine, const QString& keyword, bool stage);

// --- Edits ---------------------------------------------------------------

enum class MaterialEditKind {
	// Replace the arguments of a directive, or add it when absent. Targets
	// `directive` when >= 0, else the first directive named `keyword`.
	SetDirective,
	// Insert a directive. `position` is the index among the block's
	// directives to insert before; -1 appends.
	AddDirective,
	// Remove the directive at `directive`, or the first one named `keyword`.
	RemoveDirective,
	// Insert a stage block. `text` is the block's body directives, one per
	// line, without braces; `position` is the stage index to insert before,
	// -1 to append.
	AddStage,
	RemoveStage,
	// Move stage `stage` so it ends up at index `position`.
	MoveStage,
	// Replace a whole definition's text (name through closing brace).
	ReplaceDefinition,
	// Append a new definition's text to the end of the script.
	AddDefinition,
	RemoveDefinition,
	RenameDefinition,
	// Move a directive within its block to index `position`.
	MoveDirective,
};

QString materialEditKindId(MaterialEditKind kind);
bool materialEditKindFromId(const QString& id, MaterialEditKind* kind);

struct MaterialEdit {
	MaterialEditKind kind = MaterialEditKind::SetDirective;
	QString material;
	// -1 addresses the material's global block.
	int stage = -1;
	int directive = -1;
	QString keyword;
	QString arguments;
	int position = -1;
	QString text;
};

struct MaterialEditResult {
	bool ok = false;
	QString error;
	// The whole script after the edit.
	QString text;
	// The script parsed again; valid only when ok.
	MaterialScript script;
	// Where the change landed, for the editor to scroll to.
	int changeStart = -1;
	int changeEnd = -1;
};

MaterialEditResult applyMaterialEdit(const MaterialScript& script, const MaterialEdit& edit);
MaterialEditResult applyMaterialEdits(const MaterialScript& script, const QVector<MaterialEdit>& edits);

// Reads a JSON edit list for the CLI: [{"op": "set", "stage": 1,
// "keyword": "blendFunc", "arguments": "add"}, ...].
bool parseMaterialEditsJson(const QByteArray& json, const QString& defaultMaterial, QVector<MaterialEdit>* edits, QString* error);

// --- Engine defaults -----------------------------------------------------

// Where a Quake III surface is drawn, which decides its default shader and
// what `$lightmap` means.
enum class MaterialSurfaceContext {
	World, // a lightmapped brush or patch
	Model, // lit by the light grid
	TwoD,  // HUD and menus
};

QString materialSurfaceContextId(MaterialSurfaceContext context);

// Quake III's shader for a name no script defines (R_FindShader): on world
// surfaces a lightmap pass filtered by the image, on models the image lit by
// the light grid, in 2D the image alone. `image` defaults to name + ".tga".
MaterialDefinition implicitQuake3Material(const QString& name, MaterialSurfaceContext context = MaterialSurfaceContext::World,
	const QString& image = QString());
// Doom 3's generated text for a material with no decl (SetDefaultText): one
// blended, colored, clamped stage of the image with the same name.
QString implicitDoom3MaterialText(const QString& name);
MaterialDefinition implicitDoom3Material(const QString& name);

// --- Templates -------------------------------------------------------------

struct MaterialTemplate {
	QString id;
	MaterialEngine engine = MaterialEngine::Unknown;
	QString displayName;
	QString description;
	// Uses %1 for the material name and %2 for the main image.
	QString body;
};

QVector<MaterialTemplate> materialTemplates(MaterialEngine engine = MaterialEngine::Unknown);
bool materialTemplateById(const QString& id, MaterialTemplate* out);
// The text of a new definition: `name { ... }` with the image filled in.
QString instantiateMaterialTemplate(const MaterialTemplate& materialTemplate, const QString& name, const QString& image);

} // namespace vibestudio
