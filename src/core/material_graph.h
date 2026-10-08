#pragma once

// Node graphs for materials, and the edits a graph makes.
//
// A graph is built from a parsed definition: image and program nodes feed
// coordinate and colour nodes, which feed stage nodes, which feed the
// material's output node in draw order. Doom 3 expressions become trees of
// operator and term nodes. Classic textures show their frames, composition,
// switches and flags.
//
// The graph is a view of the source text, never a second copy of it. Every
// graph edit is translated into text edits (core/material_script.h for
// Quake III and Doom 3, SWANTBLS, ANIMDEFS or .wal_json for the classic
// engines), the text is parsed again, and the graph is rebuilt from it.
// Node ids are stable paths ("stage/1/tcmod/0", "stage/2/color/red/a") so a
// view can keep positions across rebuilds.

#include "core/material_model.h"

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// The text a material's edits change.
enum class MaterialTextKind {
	None,
	Quake3Shader,
	Doom3Material,
	DoomSwantbls,
	DoomAnimdefs,
	Quake2WalJson,
};

QString materialTextKindId(MaterialTextKind kind);
bool materialTextKindFromId(const QString& id, MaterialTextKind* kind);
// The natural text form for a definition's engine.
MaterialTextKind defaultMaterialTextKind(const MaterialDefinition& definition);

enum class MaterialGraphPortType {
	Image,
	Coordinates,
	Color,
	Alpha,
	Scalar,
	Stage,
	Geometry,
	Frame,
};

QString materialGraphPortTypeId(MaterialGraphPortType type);

struct MaterialGraphPort {
	QString id;
	QString label;
	MaterialGraphPortType type = MaterialGraphPortType::Scalar;
};

enum class MaterialGraphPropertyType {
	Text,
	Number,
	Integer,
	Bool,
	Enum,
	Expression,
	Image,
	ReadOnly,
};

QString materialGraphPropertyTypeId(MaterialGraphPropertyType type);

struct MaterialGraphProperty {
	QString id;
	QString label;
	MaterialGraphPropertyType type = MaterialGraphPropertyType::Text;
	QString value;
	QStringList options;
	QString help;
};

struct MaterialGraphNode {
	QString id;
	// "output", "stage", "image", "animmap", "lightmap", "video", "cube",
	// "program", "tcgen", "tcmod", "transform", "rgbgen", "alphagen",
	// "color", "condition", "alphatest", "texgen", "expression", "deform",
	// "sky", "fog", "light", "frame", "animation", "patch", "switch",
	// "flags", "warp", "info".
	QString kind;
	QString title;
	QString subtitle;
	QVector<MaterialGraphPort> inputs;
	QVector<MaterialGraphPort> outputs;
	QVector<MaterialGraphProperty> properties;
	int stage = -1;
	int directive = -1;
	int line = 0;
	// An image the view may draw in the node.
	QString imageReference;
	QPointF position;
	bool removable = false;
	// The node can move up or down in draw order (stages, tcMods).
	bool reorderable = false;
	// Shown dimmed: present in the text but not drawn (a failed condition,
	// a stage without an image, an ignored keyword).
	bool inactive = false;

	[[nodiscard]] const MaterialGraphProperty* property(const QString& id) const;
};

struct MaterialGraphLink {
	QString fromNode;
	QString fromPort;
	QString toNode;
	QString toPort;
};

struct MaterialGraph {
	MaterialEngine engine = MaterialEngine::Unknown;
	MaterialTextKind textKind = MaterialTextKind::None;
	QString material;
	QVector<MaterialGraphNode> nodes;
	QVector<MaterialGraphLink> links;

	[[nodiscard]] int indexOf(const QString& nodeId) const;
	[[nodiscard]] const MaterialGraphNode* node(const QString& nodeId) const;
};

MaterialGraph buildMaterialGraph(const MaterialDefinition& definition, MaterialTextKind textKind = MaterialTextKind::None);

// A node the palette can add, and where it goes.
struct MaterialGraphNodeTemplate {
	QString id;
	QString category;
	QString title;
	QString description;
	// Needs a selected stage (tcMod, rgbGen...) rather than the material.
	bool needsStage = false;
};

QVector<MaterialGraphNodeTemplate> materialGraphNodeTemplates(MaterialEngine engine, MaterialTextKind textKind = MaterialTextKind::None);

enum class MaterialGraphEditKind {
	SetProperty,
	AddNode,
	RemoveNode,
	// Move a stage or tcMod one step earlier (-1) or later (+1).
	MoveNode,
	// Doom 3 expressions: put an operator above a node (value: the
	// operator id, e.g. "multiply") with the node as its first operand.
	WrapExpression,
	// Doom 3 expressions: replace a node by its first operand.
	UnwrapExpression,
	// Doom 3: turn a diffusemap/bumpmap/specularmap shorthand into a block.
	ExpandShorthand,
};

QString materialGraphEditKindId(MaterialGraphEditKind kind);
bool materialGraphEditKindFromId(const QString& id, MaterialGraphEditKind* kind);

struct MaterialGraphEdit {
	MaterialGraphEditKind kind = MaterialGraphEditKind::SetProperty;
	QString node;
	QString property;
	QString value;
	// AddNode: the template id; the stage it goes into (node "stage/N").
	QString nodeTemplate;
	int step = 0;
	// Classic edits: the material's frame names, so an animated range is
	// found from any of its members.
	QStringList related;
};

struct MaterialGraphEditResult {
	bool ok = false;
	QString error;
	QString text;
	// The node to select after the edit, when it moved or is new.
	QString focusNode;
};

// Applies one graph edit to the material's source text and returns the new
// text. The text is checked by parsing it again.
MaterialGraphEditResult applyMaterialGraphEdit(const QString& text, MaterialTextKind kind, const QString& material,
	const MaterialGraphEdit& edit, const QString& sourcePath = QString());

QJsonObject materialGraphJson(const MaterialGraph& graph);
// Reads graph edits for the CLI: [{"edit": "set", "node": "stage/1",
// "property": "blend", "value": "add"}, ...].
bool parseMaterialGraphEditsJson(const QByteArray& json, QVector<MaterialGraphEdit>* edits, QString* error);

} // namespace vibestudio
