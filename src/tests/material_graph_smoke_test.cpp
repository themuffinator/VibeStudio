// The material node graph: what it shows for each engine, and that every
// graph edit lands in the source text, keeps the rest of it, and parses
// again.

#include "core/material_classic.h"
#include "core/material_graph.h"
#include "core/material_script.h"

#include <QCoreApplication>

#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

MaterialGraphEditResult apply(const QString& text, MaterialTextKind kind, const QString& material, MaterialGraphEditKind editKind, const QString& node,
	const QString& property = {}, const QString& value = {}, const QString& nodeTemplate = {}, int step = 0)
{
	MaterialGraphEdit edit;
	edit.kind = editKind;
	edit.node = node;
	edit.property = property;
	edit.value = value;
	edit.nodeTemplate = nodeTemplate;
	edit.step = step;
	const MaterialGraphEditResult result = applyMaterialGraphEdit(text, kind, material, edit);
	if (!result.ok) {
		std::cerr << "  edit error: " << result.error.toStdString() << '\n';
	}
	return result;
}

const MaterialDefinition* parsed(const QString& text, MaterialEngine engine, MaterialScript* holder)
{
	*holder = parseMaterialScript(text, engine);
	return holder->materials.isEmpty() ? nullptr : &holder->materials.first();
}

void checkQuake3Graph()
{
	const QString text = QStringLiteral(
		"textures/test/glow // the glow\n{\n\tqer_editorimage textures/test/glow.tga\n\tsurfaceparm nolightmap\n"
		"\t{\n\t\tmap textures/test/glow.tga\n\t\ttcMod scroll 0.5 0\n\t\ttcMod rotate 90\n\t}\n"
		"\t{\n\t\tmap $lightmap\n\t\tblendFunc filter\n\t}\n}\n");
	MaterialScript script;
	const MaterialDefinition* definition = parsed(text, MaterialEngine::Quake3, &script);
	if (!expect(definition != nullptr, "the Quake III shader parses")) {
		return;
	}
	const MaterialGraph graph = buildMaterialGraph(*definition);
	expect(graph.node(QStringLiteral("material")) && graph.node(QStringLiteral("stage/0")) && graph.node(QStringLiteral("stage/0/image"))
			&& graph.node(QStringLiteral("stage/0/tcmod/1")) && graph.node(QStringLiteral("stage/1/image")),
		"the Quake III graph has output, stages, images and tcMods");
	expect(graph.node(QStringLiteral("stage/1/image"))->kind == QStringLiteral("lightmap"), "$lightmap is a lightmap node");
	bool chained = false;
	for (const MaterialGraphLink& link : graph.links) {
		chained |= link.fromNode == QStringLiteral("stage/0/tcmod/0") && link.toNode == QStringLiteral("stage/0/tcmod/1");
	}
	expect(chained, "tcMods are chained in order");
	expect(graph.node(QStringLiteral("stage/0"))->property(QStringLiteral("blend"))->value == QStringLiteral("opaque"), "an unblended stage is opaque");

	const MaterialTextKind kind = MaterialTextKind::Quake3Shader;
	const QString name = QStringLiteral("textures/test/glow");
	MaterialGraphEditResult result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0"), QStringLiteral("blend"), QStringLiteral("add"));
	expect(result.ok && result.text.contains(QStringLiteral("blendFunc add")) && result.text.contains(QStringLiteral("// the glow")),
		"setting a stage's blend writes blendFunc and keeps comments");
	result = apply(result.text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0"), QStringLiteral("blend"), QStringLiteral("opaque"));
	expect(result.ok && !result.text.contains(QStringLiteral("blendFunc add")), "opaque removes blendFunc");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0/tcmod/0"), QStringLiteral("s"), QStringLiteral("0.25"));
	expect(result.ok && result.text.contains(QStringLiteral("tcMod scroll 0.25 0")), "a tcMod field rewrites its directive");
	result = apply(text, kind, name, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/0/tcmod/1"));
	expect(result.ok && !result.text.contains(QStringLiteral("rotate")), "removing a tcMod node removes its directive");
	result = apply(text, kind, name, MaterialGraphEditKind::MoveNode, QStringLiteral("stage/0/tcmod/1"), {}, {}, {}, -1);
	expect(result.ok && result.text.indexOf(QStringLiteral("rotate")) < result.text.indexOf(QStringLiteral("scroll")), "moving a tcMod reorders it");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("stage/1"), {}, {}, QStringLiteral("q3.rgbgen.wave"));
	expect(result.ok && result.text.contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 1")), "adding an rgbGen wave node to a stage");
	result = apply(result.text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/1/rgbgen"), QStringLiteral("frequency"), QStringLiteral("2"));
	expect(result.ok && result.text.contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 2")), "editing a wave field");
	result = apply(text, kind, name, MaterialGraphEditKind::MoveNode, QStringLiteral("stage/1"), {}, {}, {}, -1);
	MaterialScript moved;
	const MaterialDefinition* movedDefinition = parsed(result.text, MaterialEngine::Quake3, &moved);
	expect(result.ok && movedDefinition && movedDefinition->stages.first().imageKind == MaterialImageKind::Lightmap, "moving a stage up");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("material"), {}, {}, QStringLiteral("q3.deform.wave"));
	expect(result.ok && result.text.contains(QStringLiteral("deformVertexes wave 64 sin 0 2 0 0.5")), "adding a deform");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("material"), QStringLiteral("surfaceparms"),
		QStringLiteral("trans nonsolid"));
	expect(result.ok && !result.text.contains(QStringLiteral("nolightmap")) && result.text.contains(QStringLiteral("surfaceparm trans"))
			&& result.text.contains(QStringLiteral("surfaceparm nonsolid")),
		"surface parameters are diffed into directives");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("material"), QStringLiteral("cull"), QStringLiteral("none"));
	expect(result.ok && result.text.contains(QStringLiteral("cull none")), "cull none");
	result = apply(text, kind, name, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/0/image"));
	expect(!result.ok, "a stage's image cannot be removed on its own");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0/image"), QStringLiteral("clamp"), QStringLiteral("true"));
	expect(result.ok && result.text.contains(QStringLiteral("clampmap textures/test/glow.tga")), "clamp turns map into clampmap");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("material"), {}, {}, QStringLiteral("q3.stage.add"));
	MaterialScript added;
	const MaterialDefinition* addedDefinition = parsed(result.text, MaterialEngine::Quake3, &added);
	expect(result.ok && addedDefinition && addedDefinition->stages.size() == 3 && result.focusNode == QStringLiteral("stage/2"), "adding a stage");
}

void checkDoom3Graph()
{
	const QString text = QStringLiteral(
		"textures/test/panel\n{\n\tdiffusemap textures/test/panel\n\tbumpmap heightmap( textures/test/panel_h, 4 )\n"
		"\t{\n\t\tblend add\n\t\tmap textures/test/panel_add\n\t\trgb 0.5 + 0.5 * sinTable[ time ]\n\t\tscroll time * 0.1, 0\n\t}\n}\n");
	MaterialScript script;
	const MaterialDefinition* definition = parsed(text, MaterialEngine::Doom3, &script);
	if (!expect(definition != nullptr && definition->engineRejection.isEmpty(), "the Doom 3 material parses")) {
		return;
	}
	const MaterialGraph graph = buildMaterialGraph(*definition);
	expect(graph.node(QStringLiteral("stage/1/image")) && graph.node(QStringLiteral("stage/1/image"))->kind == QStringLiteral("program")
			&& graph.node(QStringLiteral("stage/1/image/0")),
		"image programs become node trees");
	expect(graph.node(QStringLiteral("stage/2/color/red/e")) && graph.node(QStringLiteral("stage/2/color/red/e/b/b/a")),
		"colour expressions become operator trees");
	expect(graph.node(QStringLiteral("stage/2/transform/0/a/e")), "transform arguments are expression nodes");
	const MaterialTextKind kind = MaterialTextKind::Doom3Material;
	const QString name = QStringLiteral("textures/test/panel");
	MaterialGraphEditResult result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/color/red/e/a"),
		QStringLiteral("value"), QStringLiteral("0.25"));
	expect(result.ok && result.text.contains(QStringLiteral("rgb 0.25 + 0.5 * sinTable[ time ]")), "editing a constant node rewrites the expression");
	result = apply(text, kind, name, MaterialGraphEditKind::WrapExpression, QStringLiteral("stage/2/color/red/e"), {}, QStringLiteral("multiply"));
	expect(result.ok && result.text.contains(QStringLiteral("( 0.5 + 0.5 * sinTable[ time ] ) * 1")), "wrapping keeps the grouping with parentheses");
	result = apply(text, kind, name, MaterialGraphEditKind::UnwrapExpression, QStringLiteral("stage/2/color/red/e"));
	expect(result.ok && result.text.contains(QStringLiteral("rgb 0.5\n")), "unwrapping keeps the first operand");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/color/red/e/b/b"), QStringLiteral("table"),
		QStringLiteral("cosTable"));
	expect(result.ok && result.text.contains(QStringLiteral("cosTable[ time ]")), "renaming a table lookup");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/color"), QStringLiteral("alpha"), QStringLiteral("0.5"));
	expect(result.ok && result.text.contains(QStringLiteral("color 0.5 + 0.5 * sinTable[ time ], 0.5 + 0.5 * sinTable[ time ], 0.5 + 0.5 * sinTable[ time ], 0.5")),
		"a different alpha turns rgb into color");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/transform/0"), QStringLiteral("b"), QStringLiteral("time * 0.2"));
	expect(result.ok && result.text.contains(QStringLiteral("scroll time * 0.1, time * 0.2")), "editing a transform argument");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/1/image"), QStringLiteral("scale"), QStringLiteral("8"));
	expect(result.ok && result.text.contains(QStringLiteral("bumpmap heightmap( textures/test/panel_h, 8 )")), "editing a shorthand's image program");
	result = apply(text, kind, name, MaterialGraphEditKind::WrapExpression, QStringLiteral("stage/0/image"), {}, QStringLiteral("makeintensity"));
	expect(result.ok && result.text.contains(QStringLiteral("diffusemap makeintensity( textures/test/panel )")), "wrapping an image in a program");
	result = apply(text, kind, name, MaterialGraphEditKind::ExpandShorthand, QStringLiteral("stage/0"));
	MaterialScript expanded;
	const MaterialDefinition* expandedDefinition = parsed(result.text, MaterialEngine::Doom3, &expanded);
	expect(result.ok && expandedDefinition && expandedDefinition->stages.size() == 3 && result.text.contains(QStringLiteral("blend diffusemap")),
		"expanding a shorthand makes a stage block");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("stage/2"), {}, {}, QStringLiteral("d3.condition"));
	expect(result.ok && result.text.contains(QStringLiteral("if parm7 == 0")), "adding a condition");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2"), QStringLiteral("blend"), QStringLiteral("blend"));
	expect(result.ok && result.text.contains(QStringLiteral("blend blend")), "changing a stage's blend");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("material"), QStringLiteral("translucent"), QStringLiteral("true"));
	expect(result.ok && result.text.contains(QStringLiteral("\ttranslucent\n")), "setting a material flag");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/color/red/e"), QStringLiteral("text"),
		QStringLiteral("time -"));
	expect(!result.ok, "a broken expression is refused before the text changes");
}

void checkClassicGraph()
{
	QVector<DoomAnimationRange> ranges = doomVanillaAnimations();
	const QString swantbls = swantblsText(ranges, doomVanillaSwitches());
	MaterialGraphEdit edit;
	edit.kind = MaterialGraphEditKind::SetProperty;
	edit.node = QStringLiteral("animation");
	edit.property = QStringLiteral("tics");
	edit.value = QStringLiteral("4");
	edit.related = {QStringLiteral("NUKAGE2"), QStringLiteral("NUKAGE3"), QStringLiteral("NUKAGE1")};
	MaterialGraphEditResult result = applyMaterialGraphEdit(swantbls, MaterialTextKind::DoomSwantbls, QStringLiteral("NUKAGE2"), edit);
	QVector<DoomAnimationRange> parsedRanges;
	QVector<DoomSwitchPair> parsedSwitches;
	QVector<MaterialDiagnostic> diagnostics;
	expect(result.ok && parseSwantblsText(result.text, &parsedRanges, &parsedSwitches, &diagnostics) && parsedRanges.first().tics == 4,
		"a range's tics change from any member");
	edit.node = QStringLiteral("switch");
	edit.property = QStringLiteral("partner");
	edit.value = QStringLiteral("SW2TEST");
	result = applyMaterialGraphEdit(swantbls, MaterialTextKind::DoomSwantbls, QStringLiteral("SW1TEST"), edit);
	expect(result.ok && result.text.contains(QStringLiteral("SW1TEST")) && result.text.contains(QStringLiteral("SW2TEST")), "adding a switch pair");
	const QString animdefs = QStringLiteral("warp flat WATER1\n");
	edit.node = QStringLiteral("warp");
	edit.property = QStringLiteral("speed");
	edit.value = QStringLiteral("2");
	result = applyMaterialGraphEdit(animdefs, MaterialTextKind::DoomAnimdefs, QStringLiteral("WATER1"), edit);
	expect(result.ok && result.text.contains(QStringLiteral("warp flat WATER1 2")), "a warp's speed changes in ANIMDEFS");
	edit.node = QStringLiteral("flags");
	edit.property = QStringLiteral("flag:warp");
	edit.value = QStringLiteral("true");
	result = applyMaterialGraphEdit(QStringLiteral("{\"flags\": 1, \"value\": 300}"), MaterialTextKind::Quake2WalJson, QStringLiteral("e1u1/water"), edit);
	Quake2WalInfo info;
	QString error;
	expect(result.ok && parseQuake2WalInfoText(result.text, &info, &error) && info.surfaceFlags == (kQuake2SurfLight | kQuake2SurfWarp) && info.value == 300,
		"a Quake II flag toggles in .wal_json");
	MaterialDefinition water = quakeMaterialDefinition(QStringLiteral("+0slime"), {QStringLiteral("+1slime")});
	const MaterialGraph graph = buildMaterialGraph(water);
	expect(graph.node(QStringLiteral("frame/1")) && graph.node(QStringLiteral("animation")) && graph.node(QStringLiteral("info")),
		"Quake frames, animation and name rules show as nodes");
	expect(!materialGraphNodeTemplates(MaterialEngine::Quake3).isEmpty() && !materialGraphNodeTemplates(MaterialEngine::Doom3).isEmpty(),
		"the palettes list node templates");
	QVector<MaterialGraphEdit> edits;
	expect(parseMaterialGraphEditsJson(QByteArray(R"([{"edit":"set","node":"stage/0","property":"blend","value":"add"}])"), &edits, &error)
			&& edits.size() == 1,
		"graph edits read from JSON");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkQuake3Graph();
	checkDoom3Graph();
	checkClassicGraph();
	if (failures > 0) {
		std::cerr << failures << " graph check(s) failed\n";
		return 1;
	}
	std::cout << "material graph smoke passed\n";
	return 0;
}
