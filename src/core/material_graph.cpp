#include "core/material_graph.h"

#include "core/material_classic.h"
#include "core/material_script.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <functional>
#include <memory>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

constexpr double kColumn = 230.0;
constexpr double kRow = 96.0;

QString numberText(double value)
{
	if (std::abs(value - std::round(value)) < 1e-9 && std::abs(value) < 1e9) {
		return QString::number(static_cast<qint64>(std::llround(value)));
	}
	QString text = QString::number(value, 'f', 6);
	while (text.endsWith(QLatin1Char('0'))) {
		text.chop(1);
	}
	if (text.endsWith(QLatin1Char('.'))) {
		text.chop(1);
	}
	return text;
}

MaterialGraphProperty property(const QString& id, const QString& label, MaterialGraphPropertyType type, const QString& value,
	const QStringList& options = {}, const QString& help = {})
{
	MaterialGraphProperty result;
	result.id = id;
	result.label = label;
	result.type = type;
	result.value = value;
	result.options = options;
	result.help = help;
	return result;
}

MaterialGraphPort port(const QString& id, const QString& label, MaterialGraphPortType type)
{
	return {id, label, type};
}

QString boolText(bool value)
{
	return value ? QStringLiteral("true") : QStringLiteral("false");
}

bool isTrue(const QString& value)
{
	const QString key = value.trimmed().toLower();
	return key == QStringLiteral("true") || key == QStringLiteral("1") || key == QStringLiteral("yes") || key == QStringLiteral("on");
}

int findDirective(const QVector<MaterialDirective>& directives, const QStringList& keywords)
{
	for (int index = 0; index < directives.size(); ++index) {
		if (keywords.contains(directives.at(index).keyword.toLower())) {
			return index;
		}
	}
	return -1;
}

QVector<int> findDirectives(const QVector<MaterialDirective>& directives, const QStringList& keywords)
{
	QVector<int> found;
	for (int index = 0; index < directives.size(); ++index) {
		if (keywords.contains(directives.at(index).keyword.toLower())) {
			found.push_back(index);
		}
	}
	return found;
}

QString waveText(const MaterialWave& wave)
{
	return QStringLiteral("%1 %2 %3 %4 %5")
		.arg(materialWaveFunctionId(wave.function), numberText(wave.base), numberText(wave.amplitude), numberText(wave.phase),
			numberText(wave.frequency));
}

QStringList waveFunctions()
{
	return {QStringLiteral("sin"), QStringLiteral("triangle"), QStringLiteral("square"), QStringLiteral("sawtooth"),
		QStringLiteral("inversesawtooth"), QStringLiteral("noise")};
}

void addWaveProperties(MaterialGraphNode* node, const MaterialWave& wave)
{
	node->properties.push_back(property(QStringLiteral("function"), Text::tr("Function"), MaterialGraphPropertyType::Enum,
		materialWaveFunctionId(wave.function), waveFunctions()));
	node->properties.push_back(property(QStringLiteral("base"), Text::tr("Base"), MaterialGraphPropertyType::Number, numberText(wave.base)));
	node->properties.push_back(property(QStringLiteral("amplitude"), Text::tr("Amplitude"), MaterialGraphPropertyType::Number, numberText(wave.amplitude)));
	node->properties.push_back(property(QStringLiteral("phase"), Text::tr("Phase"), MaterialGraphPropertyType::Number, numberText(wave.phase)));
	node->properties.push_back(property(QStringLiteral("frequency"), Text::tr("Frequency"), MaterialGraphPropertyType::Number, numberText(wave.frequency)));
}

MaterialWave waveFrom(const MaterialGraphNode& node)
{
	MaterialWave wave;
	if (const MaterialGraphProperty* function = node.property(QStringLiteral("function"))) {
		materialWaveFunctionFromId(function->value, &wave.function);
	}
	const auto number = [&](const char* id) {
		const MaterialGraphProperty* p = node.property(QString::fromLatin1(id));
		return p ? p->value.toDouble() : 0.0;
	};
	wave.base = number("base");
	wave.amplitude = number("amplitude");
	wave.phase = number("phase");
	wave.frequency = number("frequency");
	return wave;
}

QStringList blendFactors()
{
	return {QStringLiteral("GL_ONE"), QStringLiteral("GL_ZERO"), QStringLiteral("GL_SRC_COLOR"), QStringLiteral("GL_ONE_MINUS_SRC_COLOR"),
		QStringLiteral("GL_DST_COLOR"), QStringLiteral("GL_ONE_MINUS_DST_COLOR"), QStringLiteral("GL_SRC_ALPHA"),
		QStringLiteral("GL_ONE_MINUS_SRC_ALPHA"), QStringLiteral("GL_DST_ALPHA"), QStringLiteral("GL_ONE_MINUS_DST_ALPHA"),
		QStringLiteral("GL_SRC_ALPHA_SATURATE")};
}

void link(MaterialGraph* graph, const QString& fromNode, const QString& fromPort, const QString& toNode, const QString& toPort)
{
	graph->links.push_back({fromNode, fromPort, toNode, toPort});
}

// ---------------------------------------------------------------------------
// Quake III graph
// ---------------------------------------------------------------------------

QString blendChoice(const MaterialBlend& blend)
{
	if (!blend.explicitBlend || blend.isOpaqueReplace()) {
		return QStringLiteral("opaque");
	}
	const QString written = blend.written.trimmed().toLower();
	if (written == QStringLiteral("add") || written == QStringLiteral("filter") || written == QStringLiteral("blend")) {
		return written;
	}
	return QStringLiteral("custom");
}

void buildQuake3Graph(const MaterialDefinition& definition, MaterialGraph* graph)
{
	int maxMods = 1;
	for (const MaterialStage& stage : definition.stages) {
		maxMods = std::max(maxMods, static_cast<int>(stage.tcMods.size()));
	}
	const double stageX = kColumn * (2 + maxMods);
	const double outputX = stageX + kColumn * 1.2;
	MaterialGraphNode output;
	output.id = QStringLiteral("material");
	output.kind = QStringLiteral("output");
	output.title = definition.name;
	output.subtitle = Text::tr("Quake III shader");
	output.line = definition.span.line;
	output.imageReference = definition.editorImage;
	output.properties.push_back(property(QStringLiteral("cull"), Text::tr("Cull"), MaterialGraphPropertyType::Enum, materialCullId(definition.cull),
		{QStringLiteral("front"), QStringLiteral("back"), QStringLiteral("none")}, materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("cull"), false)));
	const int sortDirective = findDirective(definition.directives, {QStringLiteral("sort")});
	output.properties.push_back(property(QStringLiteral("sort"), Text::tr("Sort"), MaterialGraphPropertyType::Text,
		sortDirective >= 0 ? definition.directives.at(sortDirective).argumentText : QString(), {},
		materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("sort"), false)));
	output.properties.push_back(property(QStringLiteral("surfaceparms"), Text::tr("Surface parameters"), MaterialGraphPropertyType::Text,
		definition.surfaceParms.join(QLatin1Char(' ')), {}, materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("surfaceparm"), false)));
	output.properties.push_back(property(QStringLiteral("editorImage"), Text::tr("Editor image"), MaterialGraphPropertyType::Image, definition.editorImage));
	output.properties.push_back(property(QStringLiteral("polygonOffset"), Text::tr("Polygon offset"), MaterialGraphPropertyType::Bool,
		boolText(definition.polygonOffset)));
	output.properties.push_back(property(QStringLiteral("nopicmip"), Text::tr("No picmip"), MaterialGraphPropertyType::Bool, boolText(definition.noPicMip)));
	output.properties.push_back(property(QStringLiteral("nomipmaps"), Text::tr("No mipmaps"), MaterialGraphPropertyType::Bool, boolText(definition.noMipMaps)));
	output.properties.push_back(property(QStringLiteral("portal"), Text::tr("Portal"), MaterialGraphPropertyType::Bool, boolText(definition.portal)));
	output.properties.push_back(property(QStringLiteral("tessSize"), Text::tr("Tessellation size"), MaterialGraphPropertyType::Number,
		definition.tessSize > 0 ? numberText(definition.tessSize) : QString()));
	output.inputs.push_back(port(QStringLiteral("geometry"), Text::tr("Geometry"), MaterialGraphPortType::Geometry));
	for (int index = 0; index < definition.stages.size(); ++index) {
		output.inputs.push_back(port(QStringLiteral("stage%1").arg(index), Text::tr("Stage %1").arg(index + 1), MaterialGraphPortType::Stage));
	}
	if (!definition.engineRejection.isEmpty()) {
		output.properties.push_back(property(QStringLiteral("rejected"), Text::tr("In game"), MaterialGraphPropertyType::ReadOnly, definition.engineRejection));
	}
	const double outputY = std::max(0.0, (definition.stages.size() - 1) * kRow * 3.2 / 2.0);
	output.position = QPointF(outputX, outputY);

	for (int stageIndex = 0; stageIndex < definition.stages.size(); ++stageIndex) {
		const MaterialStage& stage = definition.stages.at(stageIndex);
		const double laneY = stageIndex * kRow * 3.2;
		const QString stageId = QStringLiteral("stage/%1").arg(stageIndex);
		// The image.
		MaterialGraphNode image;
		image.id = stageId + QStringLiteral("/image");
		image.stage = stageIndex;
		image.line = stage.span.line;
		image.position = QPointF(0, laneY);
		image.outputs.push_back(port(QStringLiteral("image"), Text::tr("Image"), MaterialGraphPortType::Image));
		const int mapDirective = findDirective(stage.directives, {QStringLiteral("map"), QStringLiteral("clampmap"), QStringLiteral("animmap"),
			QStringLiteral("videomap")});
		image.directive = mapDirective;
		if (stage.imageKind == MaterialImageKind::Animation) {
			image.kind = QStringLiteral("animmap");
			image.title = Text::tr("Animated images");
			image.subtitle = Text::tr("%1 frames at %2 Hz").arg(stage.animationFrames.size()).arg(stage.animationFrequency);
			image.imageReference = stage.animationFrames.value(0);
			image.properties.push_back(property(QStringLiteral("frequency"), Text::tr("Frames a second"), MaterialGraphPropertyType::Number,
				numberText(stage.animationFrequency)));
			image.properties.push_back(property(QStringLiteral("frames"), Text::tr("Frames"), MaterialGraphPropertyType::Text,
				stage.animationFrames.join(QLatin1Char(' ')), {}, materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("animmap"), true)));
		} else if (stage.imageKind == MaterialImageKind::Video) {
			image.kind = QStringLiteral("video");
			image.title = Text::tr("Video");
			image.subtitle = stage.imagePath;
			image.properties.push_back(property(QStringLiteral("path"), Text::tr("Video"), MaterialGraphPropertyType::Text, stage.imagePath));
		} else {
			image.kind = stage.imageKind == MaterialImageKind::Lightmap ? QStringLiteral("lightmap") : QStringLiteral("image");
			image.title = stage.imageKind == MaterialImageKind::Lightmap ? Text::tr("Lightmap")
				: stage.imageKind == MaterialImageKind::White            ? Text::tr("White")
																		 : Text::tr("Image");
			image.subtitle = stage.imageKind == MaterialImageKind::File ? stage.imagePath : QString();
			image.imageReference = stage.imageKind == MaterialImageKind::File ? stage.imagePath : QString();
			const QString source = stage.imageKind == MaterialImageKind::Lightmap ? QStringLiteral("$lightmap")
				: stage.imageKind == MaterialImageKind::White                     ? QStringLiteral("$whiteimage")
																				  : QStringLiteral("image");
			image.properties.push_back(property(QStringLiteral("source"), Text::tr("Source"), MaterialGraphPropertyType::Enum, source,
				{QStringLiteral("image"), QStringLiteral("$lightmap"), QStringLiteral("$whiteimage")}));
			image.properties.push_back(property(QStringLiteral("path"), Text::tr("Image"), MaterialGraphPropertyType::Image, stage.imagePath, {},
				materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("map"), true)));
			image.properties.push_back(property(QStringLiteral("clamp"), Text::tr("Clamp"), MaterialGraphPropertyType::Bool, boolText(stage.clamp)));
		}
		image.inactive = stage.imageKind == MaterialImageKind::None;
		graph->nodes.push_back(image);

		// Coordinates: tcGen then the tcMods in order.
		MaterialGraphNode tcGen;
		tcGen.id = stageId + QStringLiteral("/tcgen");
		tcGen.kind = QStringLiteral("tcgen");
		tcGen.title = Text::tr("Coordinates");
		tcGen.subtitle = materialTexCoordSourceId(stage.tcGen.source);
		tcGen.stage = stageIndex;
		tcGen.directive = findDirective(stage.directives, {QStringLiteral("tcgen"), QStringLiteral("texgen")});
		tcGen.removable = tcGen.directive >= 0;
		tcGen.position = QPointF(kColumn, laneY);
		tcGen.outputs.push_back(port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates));
		tcGen.properties.push_back(property(QStringLiteral("source"), Text::tr("Source"), MaterialGraphPropertyType::Enum,
			materialTexCoordSourceId(stage.tcGen.source),
			{QStringLiteral("base"), QStringLiteral("lightmap"), QStringLiteral("environment"), QStringLiteral("vector")},
			materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("tcgen"), true)));
		if (stage.tcGen.source == MaterialTexCoordSource::Vector) {
			tcGen.properties.push_back(property(QStringLiteral("s"), Text::tr("S vector"), MaterialGraphPropertyType::Text,
				QStringLiteral("%1 %2 %3").arg(numberText(stage.tcGen.vectorS[0]), numberText(stage.tcGen.vectorS[1]), numberText(stage.tcGen.vectorS[2]))));
			tcGen.properties.push_back(property(QStringLiteral("t"), Text::tr("T vector"), MaterialGraphPropertyType::Text,
				QStringLiteral("%1 %2 %3").arg(numberText(stage.tcGen.vectorT[0]), numberText(stage.tcGen.vectorT[1]), numberText(stage.tcGen.vectorT[2]))));
		}
		graph->nodes.push_back(tcGen);
		QString coordsFrom = tcGen.id;
		for (int modIndex = 0; modIndex < stage.tcMods.size(); ++modIndex) {
			const MaterialTexMod& mod = stage.tcMods.at(modIndex);
			MaterialGraphNode node;
			node.id = stageId + QStringLiteral("/tcmod/%1").arg(modIndex);
			node.kind = QStringLiteral("tcmod");
			node.title = Text::tr("tcMod %1").arg(materialTexModKindId(mod.kind));
			node.stage = stageIndex;
			node.directive = mod.directive;
			node.line = mod.directive >= 0 && mod.directive < stage.directives.size() ? stage.directives.at(mod.directive).span.line : 0;
			node.subtitle = mod.directive >= 0 && mod.directive < stage.directives.size() ? stage.directives.at(mod.directive).argumentText : QString();
			node.removable = true;
			node.reorderable = true;
			node.position = QPointF(kColumn * (2 + modIndex), laneY);
			node.inputs.push_back(port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates));
			node.outputs.push_back(port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates));
			node.properties.push_back(property(QStringLiteral("type"), Text::tr("Type"), MaterialGraphPropertyType::Enum, materialTexModKindId(mod.kind),
				{QStringLiteral("scroll"), QStringLiteral("scale"), QStringLiteral("rotate"), QStringLiteral("stretch"), QStringLiteral("turb"),
					QStringLiteral("transform"), QStringLiteral("entityTranslate")},
				materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("tcmod"), true)));
			switch (mod.kind) {
			case MaterialTexModKind::Scroll:
			case MaterialTexModKind::Scale:
				node.properties.push_back(property(QStringLiteral("s"), Text::tr("S"), MaterialGraphPropertyType::Number, numberText(mod.values[0])));
				node.properties.push_back(property(QStringLiteral("t"), Text::tr("T"), MaterialGraphPropertyType::Number, numberText(mod.values[1])));
				break;
			case MaterialTexModKind::Rotate:
				node.properties.push_back(property(QStringLiteral("speed"), Text::tr("Degrees a second"), MaterialGraphPropertyType::Number,
					numberText(mod.values[0])));
				break;
			case MaterialTexModKind::Stretch:
				addWaveProperties(&node, mod.wave);
				break;
			case MaterialTexModKind::Turbulent:
				node.properties.push_back(property(QStringLiteral("base"), Text::tr("Base (ignored)"), MaterialGraphPropertyType::Number, numberText(mod.wave.base)));
				node.properties.push_back(property(QStringLiteral("amplitude"), Text::tr("Amplitude"), MaterialGraphPropertyType::Number,
					numberText(mod.wave.amplitude)));
				node.properties.push_back(property(QStringLiteral("phase"), Text::tr("Phase"), MaterialGraphPropertyType::Number, numberText(mod.wave.phase)));
				node.properties.push_back(property(QStringLiteral("frequency"), Text::tr("Frequency"), MaterialGraphPropertyType::Number,
					numberText(mod.wave.frequency)));
				break;
			case MaterialTexModKind::Transform: {
				const char* ids[6] = {"m00", "m01", "m10", "m11", "t0", "t1"};
				for (int value = 0; value < 6; ++value) {
					node.properties.push_back(property(QString::fromLatin1(ids[value]), QString::fromLatin1(ids[value]), MaterialGraphPropertyType::Number,
						numberText(mod.values[static_cast<size_t>(value)])));
				}
				break;
			}
			default:
				break;
			}
			graph->nodes.push_back(node);
			link(graph, coordsFrom, QStringLiteral("coords"), node.id, QStringLiteral("coords"));
			coordsFrom = node.id;
		}

		// Colour and alpha.
		const auto colorNode = [&](bool alpha) {
			const MaterialColorGen& gen = alpha ? stage.alphaGen : stage.rgbGen;
			MaterialGraphNode node;
			node.id = stageId + (alpha ? QStringLiteral("/alphagen") : QStringLiteral("/rgbgen"));
			node.kind = alpha ? QStringLiteral("alphagen") : QStringLiteral("rgbgen");
			node.title = alpha ? Text::tr("Alpha") : Text::tr("Colour");
			node.subtitle = materialColorSourceId(gen.source) + (gen.explicitlySet ? QString() : Text::tr(" (default)"));
			node.stage = stageIndex;
			node.directive = findDirective(stage.directives, {alpha ? QStringLiteral("alphagen") : QStringLiteral("rgbgen")});
			node.removable = node.directive >= 0;
			node.position = QPointF(kColumn * 2, laneY + kRow * (alpha ? 2.0 : 1.1));
			node.outputs.push_back(port(alpha ? QStringLiteral("alpha") : QStringLiteral("color"), alpha ? Text::tr("Alpha") : Text::tr("Colour"),
				alpha ? MaterialGraphPortType::Alpha : MaterialGraphPortType::Color));
			const QStringList sources = alpha
				? QStringList {QStringLiteral("identity"), QStringLiteral("const"), QStringLiteral("wave"), QStringLiteral("entity"),
					  QStringLiteral("oneMinusEntity"), QStringLiteral("vertex"), QStringLiteral("oneMinusVertex"), QStringLiteral("lightingSpecular"),
					  QStringLiteral("portal")}
				: QStringList {QStringLiteral("identity"), QStringLiteral("identityLighting"), QStringLiteral("const"), QStringLiteral("wave"),
					  QStringLiteral("entity"), QStringLiteral("oneMinusEntity"), QStringLiteral("vertex"), QStringLiteral("exactVertex"),
					  QStringLiteral("oneMinusVertex"), QStringLiteral("lightingDiffuse")};
			node.properties.push_back(property(QStringLiteral("source"), Text::tr("Source"), MaterialGraphPropertyType::Enum,
				materialColorSourceId(gen.source), sources,
				materialKeywordHelp(MaterialEngine::Quake3, alpha ? QStringLiteral("alphagen") : QStringLiteral("rgbgen"), true)));
			if (gen.source == MaterialColorSource::Wave) {
				addWaveProperties(&node, gen.wave);
			} else if (gen.source == MaterialColorSource::Constant) {
				node.properties.push_back(alpha ? property(QStringLiteral("value"), Text::tr("Alpha"), MaterialGraphPropertyType::Number,
													  numberText(gen.constantAlpha))
												: property(QStringLiteral("value"), Text::tr("Colour (r g b)"), MaterialGraphPropertyType::Text,
													  QStringLiteral("%1 %2 %3").arg(numberText(gen.constant[0]), numberText(gen.constant[1]),
														  numberText(gen.constant[2]))));
			} else if (gen.source == MaterialColorSource::Portal) {
				node.properties.push_back(property(QStringLiteral("value"), Text::tr("Range"), MaterialGraphPropertyType::Number, numberText(gen.portalRange)));
			}
			return node;
		};
		const MaterialGraphNode rgbGen = colorNode(false);
		const MaterialGraphNode alphaGen = colorNode(true);
		graph->nodes.push_back(rgbGen);
		graph->nodes.push_back(alphaGen);

		// The stage.
		MaterialGraphNode stageNode;
		stageNode.id = stageId;
		stageNode.kind = QStringLiteral("stage");
		stageNode.title = Text::tr("Stage %1").arg(stageIndex + 1);
		stageNode.subtitle = blendChoice(stage.blend);
		stageNode.stage = stageIndex;
		stageNode.line = stage.span.line;
		stageNode.removable = true;
		stageNode.reorderable = true;
		stageNode.inactive = stage.imageKind == MaterialImageKind::None;
		stageNode.imageReference = image.imageReference;
		stageNode.position = QPointF(stageX, laneY + kRow * 0.6);
		stageNode.inputs = {port(QStringLiteral("image"), Text::tr("Image"), MaterialGraphPortType::Image),
			port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates),
			port(QStringLiteral("color"), Text::tr("Colour"), MaterialGraphPortType::Color),
			port(QStringLiteral("alpha"), Text::tr("Alpha"), MaterialGraphPortType::Alpha)};
		stageNode.outputs.push_back(port(QStringLiteral("stage"), Text::tr("Stage"), MaterialGraphPortType::Stage));
		stageNode.properties.push_back(property(QStringLiteral("blend"), Text::tr("Blend"), MaterialGraphPropertyType::Enum, blendChoice(stage.blend),
			{QStringLiteral("opaque"), QStringLiteral("add"), QStringLiteral("filter"), QStringLiteral("blend"), QStringLiteral("custom")},
			materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("blendfunc"), true)));
		stageNode.properties.push_back(property(QStringLiteral("source"), Text::tr("Source factor"), MaterialGraphPropertyType::Enum,
			materialBlendFactorId(stage.blend.source), blendFactors()));
		stageNode.properties.push_back(property(QStringLiteral("destination"), Text::tr("Destination factor"), MaterialGraphPropertyType::Enum,
			materialBlendFactorId(stage.blend.destination), blendFactors()));
		const QString alphaFunc = stage.alphaTest == MaterialAlphaTest::None ? QStringLiteral("none") : materialAlphaTestId(stage.alphaTest);
		stageNode.properties.push_back(property(QStringLiteral("alphaFunc"), Text::tr("Alpha test"), MaterialGraphPropertyType::Enum, alphaFunc,
			{QStringLiteral("none"), QStringLiteral("GT0"), QStringLiteral("LT128"), QStringLiteral("GE128")},
			materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("alphafunc"), true)));
		stageNode.properties.push_back(property(QStringLiteral("depthFunc"), Text::tr("Depth test"), MaterialGraphPropertyType::Enum,
			stage.depthFunc == MaterialDepthFunc::Equal ? QStringLiteral("equal") : QStringLiteral("lequal"),
			{QStringLiteral("lequal"), QStringLiteral("equal")}, materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("depthfunc"), true)));
		stageNode.properties.push_back(property(QStringLiteral("depthWrite"), Text::tr("Write depth"), MaterialGraphPropertyType::Bool,
			boolText(stage.depthWriteExplicit), {}, materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("depthwrite"), true)));
		stageNode.properties.push_back(property(QStringLiteral("detail"), Text::tr("Detail"), MaterialGraphPropertyType::Bool, boolText(stage.detail)));
		graph->nodes.push_back(stageNode);
		link(graph, image.id, QStringLiteral("image"), stageId, QStringLiteral("image"));
		link(graph, coordsFrom, QStringLiteral("coords"), stageId, QStringLiteral("coords"));
		link(graph, rgbGen.id, QStringLiteral("color"), stageId, QStringLiteral("color"));
		link(graph, alphaGen.id, QStringLiteral("alpha"), stageId, QStringLiteral("alpha"));
		link(graph, stageId, QStringLiteral("stage"), output.id, QStringLiteral("stage%1").arg(stageIndex));
	}

	for (int deformIndex = 0; deformIndex < definition.deforms.size(); ++deformIndex) {
		const MaterialDeform& deform = definition.deforms.at(deformIndex);
		MaterialGraphNode node;
		node.id = QStringLiteral("deform/%1").arg(deformIndex);
		node.kind = QStringLiteral("deform");
		node.title = Text::tr("Deform %1").arg(materialDeformKindId(deform.kind));
		node.directive = deform.directive;
		node.removable = true;
		node.line = deform.directive >= 0 && deform.directive < definition.directives.size() ? definition.directives.at(deform.directive).span.line : 0;
		node.subtitle = deform.directive >= 0 && deform.directive < definition.directives.size() ? definition.directives.at(deform.directive).argumentText
																								 : QString();
		node.position = QPointF(stageX, -kRow * (1.4 + deformIndex));
		node.outputs.push_back(port(QStringLiteral("geometry"), Text::tr("Geometry"), MaterialGraphPortType::Geometry));
		node.properties.push_back(property(QStringLiteral("type"), Text::tr("Type"), MaterialGraphPropertyType::Enum, materialDeformKindId(deform.kind),
			{QStringLiteral("wave"), QStringLiteral("normal"), QStringLiteral("bulge"), QStringLiteral("move"), QStringLiteral("autosprite"),
				QStringLiteral("autosprite2"), QStringLiteral("projectionShadow")},
			materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("deformvertexes"), false)));
		switch (deform.kind) {
		case MaterialDeformKind::Wave:
			node.properties.push_back(property(QStringLiteral("spread"), Text::tr("Wavelength"), MaterialGraphPropertyType::Number,
				numberText(deform.spread > 0 ? 1.0 / deform.spread : 100.0)));
			addWaveProperties(&node, deform.wave);
			break;
		case MaterialDeformKind::Normal:
			node.properties.push_back(property(QStringLiteral("amplitude"), Text::tr("Amplitude"), MaterialGraphPropertyType::Number,
				numberText(deform.wave.amplitude)));
			node.properties.push_back(property(QStringLiteral("frequency"), Text::tr("Frequency"), MaterialGraphPropertyType::Number,
				numberText(deform.wave.frequency)));
			break;
		case MaterialDeformKind::Bulge:
			node.properties.push_back(property(QStringLiteral("width"), Text::tr("Width"), MaterialGraphPropertyType::Number, numberText(deform.values[0])));
			node.properties.push_back(property(QStringLiteral("height"), Text::tr("Height"), MaterialGraphPropertyType::Number, numberText(deform.values[1])));
			node.properties.push_back(property(QStringLiteral("speed"), Text::tr("Speed"), MaterialGraphPropertyType::Number, numberText(deform.values[2])));
			break;
		case MaterialDeformKind::Move:
			node.properties.push_back(property(QStringLiteral("vector"), Text::tr("Direction (x y z)"), MaterialGraphPropertyType::Text,
				QStringLiteral("%1 %2 %3").arg(numberText(deform.values[0]), numberText(deform.values[1]), numberText(deform.values[2]))));
			addWaveProperties(&node, deform.wave);
			break;
		default:
			break;
		}
		graph->nodes.push_back(node);
		link(graph, node.id, QStringLiteral("geometry"), output.id, QStringLiteral("geometry"));
	}
	if (definition.sky.present) {
		MaterialGraphNode node;
		node.id = QStringLiteral("sky");
		node.kind = QStringLiteral("sky");
		node.title = Text::tr("Sky");
		node.subtitle = definition.sky.farBox;
		node.directive = findDirective(definition.directives, {QStringLiteral("skyparms")});
		node.removable = true;
		node.position = QPointF(outputX, outputY - kRow * 2.0);
		node.outputs.push_back(port(QStringLiteral("sky"), Text::tr("Sky"), MaterialGraphPortType::Image));
		node.properties.push_back(property(QStringLiteral("farBox"), Text::tr("Far box"), MaterialGraphPropertyType::Text, definition.sky.farBox, {},
			materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("skyparms"), false)));
		node.properties.push_back(property(QStringLiteral("cloudHeight"), Text::tr("Cloud height"), MaterialGraphPropertyType::Number,
			numberText(definition.sky.cloudHeight)));
		node.properties.push_back(property(QStringLiteral("nearBox"), Text::tr("Near box"), MaterialGraphPropertyType::Text, definition.sky.nearBox));
		graph->nodes.push_back(node);
		output.inputs.push_back(port(QStringLiteral("sky"), Text::tr("Sky"), MaterialGraphPortType::Image));
		link(graph, node.id, QStringLiteral("sky"), output.id, QStringLiteral("sky"));
	}
	if (definition.fog.present) {
		MaterialGraphNode node;
		node.id = QStringLiteral("fog");
		node.kind = QStringLiteral("fog");
		node.title = Text::tr("Fog");
		node.directive = findDirective(definition.directives, {QStringLiteral("fogparms")});
		node.removable = true;
		node.position = QPointF(outputX, outputY + kRow * 2.0);
		node.outputs.push_back(port(QStringLiteral("fog"), Text::tr("Fog"), MaterialGraphPortType::Color));
		node.properties.push_back(property(QStringLiteral("color"), Text::tr("Colour (r g b)"), MaterialGraphPropertyType::Text,
			QStringLiteral("%1 %2 %3").arg(numberText(definition.fog.color[0]), numberText(definition.fog.color[1]), numberText(definition.fog.color[2])),
			{}, materialKeywordHelp(MaterialEngine::Quake3, QStringLiteral("fogparms"), false)));
		node.properties.push_back(property(QStringLiteral("distance"), Text::tr("Opaque at"), MaterialGraphPropertyType::Number,
			numberText(definition.fog.distanceToOpaque)));
		graph->nodes.push_back(node);
		output.inputs.push_back(port(QStringLiteral("fog"), Text::tr("Fog"), MaterialGraphPortType::Color));
		link(graph, node.id, QStringLiteral("fog"), output.id, QStringLiteral("fog"));
	}
	graph->nodes.push_back(output);
}

// ---------------------------------------------------------------------------
// Doom 3 graph
// ---------------------------------------------------------------------------

QStringList expressionOperators()
{
	return {QStringLiteral("constant"), QStringLiteral("time"), QStringLiteral("parm"), QStringLiteral("global"), QStringLiteral("sound"),
		QStringLiteral("table"), QStringLiteral("add"), QStringLiteral("subtract"), QStringLiteral("multiply"), QStringLiteral("divide"),
		QStringLiteral("modulo"), QStringLiteral("greater"), QStringLiteral("greaterEqual"), QStringLiteral("less"), QStringLiteral("lessEqual"),
		QStringLiteral("equal"), QStringLiteral("notEqual"), QStringLiteral("and"), QStringLiteral("or")};
}

QString expressionTitle(const MaterialExpressionNode& node)
{
	switch (node.op) {
	case MaterialExpressionOp::Constant:
		return numberText(node.value);
	case MaterialExpressionOp::Time:
		return Text::tr("Time");
	case MaterialExpressionOp::Parm:
		return QStringLiteral("parm%1").arg(node.index);
	case MaterialExpressionOp::Global:
		return QStringLiteral("global%1").arg(node.index);
	case MaterialExpressionOp::Sound:
		return Text::tr("Sound");
	case MaterialExpressionOp::FragmentPrograms:
		return QStringLiteral("fragmentPrograms");
	case MaterialExpressionOp::Table:
		return QStringLiteral("%1[ ]").arg(node.name);
	default:
		break;
	}
	return materialExpressionOpToken(node.op);
}

// Adds an expression tree right to left from its consumer; returns the
// root node's id.
QString addExpressionNodes(MaterialGraph* graph, const MaterialDefinition& definition, int expression, const QString& id, const QPointF& anchor,
	int depth = 0)
{
	if (expression < 0 || expression >= definition.expressions.size() || depth > 24) {
		return QString();
	}
	const MaterialExpressionNode& source = definition.expressions.at(expression);
	MaterialGraphNode node;
	node.id = id;
	node.kind = QStringLiteral("expression");
	node.title = expressionTitle(source);
	node.subtitle = materialExpressionOpId(source.op);
	node.removable = depth > 0;
	node.line = source.span.line;
	node.position = anchor;
	node.outputs.push_back(port(QStringLiteral("value"), Text::tr("Value"), MaterialGraphPortType::Scalar));
	node.properties.push_back(property(QStringLiteral("op"), Text::tr("Operation"), MaterialGraphPropertyType::Enum, materialExpressionOpId(source.op),
		expressionOperators()));
	switch (source.op) {
	case MaterialExpressionOp::Constant:
		node.properties.push_back(property(QStringLiteral("value"), Text::tr("Value"), MaterialGraphPropertyType::Number, numberText(source.value)));
		break;
	case MaterialExpressionOp::Parm:
	case MaterialExpressionOp::Global:
		node.properties.push_back(property(QStringLiteral("index"), Text::tr("Number"), MaterialGraphPropertyType::Integer, QString::number(source.index)));
		break;
	case MaterialExpressionOp::Table:
		node.properties.push_back(property(QStringLiteral("table"), Text::tr("Table"), MaterialGraphPropertyType::Text, source.name));
		break;
	default:
		break;
	}
	node.properties.push_back(property(QStringLiteral("text"), Text::tr("As text"), MaterialGraphPropertyType::Expression,
		materialExpressionText(definition.expressions, expression)));
	const double childX = anchor.x() - kColumn * 0.9;
	if (source.op == MaterialExpressionOp::Table) {
		node.inputs.push_back(port(QStringLiteral("a"), Text::tr("Index"), MaterialGraphPortType::Scalar));
		graph->nodes.push_back(node);
		const QString child = addExpressionNodes(graph, definition, source.a, id + QStringLiteral("/a"), QPointF(childX, anchor.y()), depth + 1);
		if (!child.isEmpty()) {
			link(graph, child, QStringLiteral("value"), id, QStringLiteral("a"));
		}
		return id;
	}
	if (source.a >= 0 && source.b >= 0) {
		node.inputs.push_back(port(QStringLiteral("a"), Text::tr("A"), MaterialGraphPortType::Scalar));
		node.inputs.push_back(port(QStringLiteral("b"), Text::tr("B"), MaterialGraphPortType::Scalar));
		graph->nodes.push_back(node);
		const double spread = kRow * 0.75 / std::max(1, depth + 1) * 2.0;
		const QString a = addExpressionNodes(graph, definition, source.a, id + QStringLiteral("/a"), QPointF(childX, anchor.y() - spread / 2), depth + 1);
		const QString b = addExpressionNodes(graph, definition, source.b, id + QStringLiteral("/b"), QPointF(childX, anchor.y() + spread / 2), depth + 1);
		if (!a.isEmpty()) {
			link(graph, a, QStringLiteral("value"), id, QStringLiteral("a"));
		}
		if (!b.isEmpty()) {
			link(graph, b, QStringLiteral("value"), id, QStringLiteral("b"));
		}
		return id;
	}
	graph->nodes.push_back(node);
	return id;
}

QString addProgramNodes(MaterialGraph* graph, const MaterialDefinition& definition, int program, const QString& id, const QPointF& anchor, int stage,
	int directive, int depth = 0)
{
	if (program < 0 || program >= definition.imagePrograms.size() || depth > 16) {
		return QString();
	}
	const MaterialImageProgramNode& source = definition.imagePrograms.at(program);
	MaterialGraphNode node;
	node.id = id;
	node.kind = source.function.isEmpty() ? QStringLiteral("image") : QStringLiteral("program");
	node.stage = stage;
	node.directive = directive;
	node.line = source.span.line;
	node.position = anchor;
	node.removable = depth > 0;
	node.outputs.push_back(port(QStringLiteral("image"), Text::tr("Image"), MaterialGraphPortType::Image));
	if (source.function.isEmpty()) {
		node.title = Text::tr("Image");
		node.subtitle = source.path;
		node.imageReference = source.path;
		node.properties.push_back(property(QStringLiteral("path"), Text::tr("Image"), MaterialGraphPropertyType::Image, source.path, {},
			materialKeywordHelp(MaterialEngine::Doom3, QStringLiteral("map"), true)));
		graph->nodes.push_back(node);
		return id;
	}
	node.title = source.function + QStringLiteral("()");
	node.imageReference = materialImageProgramText(definition.imagePrograms, program);
	node.properties.push_back(property(QStringLiteral("function"), Text::tr("Function"), MaterialGraphPropertyType::Enum, source.function,
		{QStringLiteral("heightmap"), QStringLiteral("addnormals"), QStringLiteral("smoothnormals"), QStringLiteral("add"), QStringLiteral("scale"),
			QStringLiteral("invertalpha"), QStringLiteral("invertcolor"), QStringLiteral("makeintensity"), QStringLiteral("makealpha")}));
	if (source.function == QStringLiteral("heightmap")) {
		node.properties.push_back(property(QStringLiteral("scale"), Text::tr("Bump scale"), MaterialGraphPropertyType::Number,
			numberText(source.numbers.value(0, 1.0))));
	} else if (source.function == QStringLiteral("scale")) {
		const QStringList channels {QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("b"), QStringLiteral("a")};
		for (int channel = 0; channel < 4; ++channel) {
			node.properties.push_back(property(channels.at(channel), channels.at(channel), MaterialGraphPropertyType::Number,
				numberText(source.numbers.value(channel, 1.0))));
		}
	}
	for (int child = 0; child < source.children.size(); ++child) {
		node.inputs.push_back(port(QStringLiteral("in%1").arg(child), Text::tr("Image %1").arg(child + 1), MaterialGraphPortType::Image));
	}
	graph->nodes.push_back(node);
	for (int child = 0; child < source.children.size(); ++child) {
		const QString childId = addProgramNodes(graph, definition, source.children.at(child), id + QStringLiteral("/%1").arg(child),
			QPointF(anchor.x() - kColumn * 0.95, anchor.y() + (child - (source.children.size() - 1) / 2.0) * kRow), stage, directive, depth + 1);
		if (!childId.isEmpty()) {
			link(graph, childId, QStringLiteral("image"), id, QStringLiteral("in%1").arg(child));
		}
	}
	return id;
}

QString d3BlendChoice(const MaterialStage& stage)
{
	switch (stage.role) {
	case MaterialStageRole::Diffuse:
		return QStringLiteral("diffusemap");
	case MaterialStageRole::Bump:
		return QStringLiteral("bumpmap");
	case MaterialStageRole::Specular:
		return QStringLiteral("specularmap");
	case MaterialStageRole::Regular:
		break;
	}
	if (!stage.blend.explicitBlend) {
		return QStringLiteral("replace");
	}
	const QString written = stage.blend.written.trimmed().toLower();
	static const QStringList named {QStringLiteral("blend"), QStringLiteral("add"), QStringLiteral("filter"), QStringLiteral("modulate"), QStringLiteral("none")};
	return named.contains(written) ? written : QStringLiteral("custom");
}

QString wrapChoice(const MaterialStage& stage)
{
	if (stage.alphaZeroClamp) {
		return QStringLiteral("alphazeroclamp");
	}
	if (stage.zeroClamp) {
		return QStringLiteral("zeroclamp");
	}
	return stage.clamp ? QStringLiteral("clamp") : QStringLiteral("repeat");
}

void buildDoom3Graph(const MaterialDefinition& definition, MaterialGraph* graph)
{
	const double stageX = kColumn * 4.6;
	const double outputX = stageX + kColumn * 1.4;
	MaterialGraphNode output;
	output.id = QStringLiteral("material");
	output.kind = QStringLiteral("output");
	output.title = definition.name;
	output.subtitle = definition.isLight() ? Text::tr("Doom 3 light") : Text::tr("Doom 3 material");
	output.line = definition.span.line;
	output.imageReference = definition.editorImage;
	const auto flag = [&](const char* id, const QString& label) {
		output.properties.push_back(property(QString::fromLatin1(id), label, MaterialGraphPropertyType::Bool,
			boolText(definition.hasFlag(QString::fromLatin1(id).toLower()))));
	};
	flag("translucent", Text::tr("Translucent"));
	flag("twoSided", Text::tr("Two-sided"));
	flag("noShadows", Text::tr("No shadows"));
	flag("noSelfShadow", Text::tr("No self-shadow"));
	flag("forceOpaque", Text::tr("Force opaque"));
	output.properties.push_back(property(QStringLiteral("decal"), Text::tr("Decal (DECAL_MACRO)"), MaterialGraphPropertyType::Bool,
		boolText(findDirective(definition.directives, {QStringLiteral("decal_macro")}) >= 0)));
	output.properties.push_back(property(QStringLiteral("sort"), Text::tr("Sort"), MaterialGraphPropertyType::Text,
		findDirective(definition.directives, {QStringLiteral("sort")}) >= 0 ? definition.sort : QString(), {},
		materialKeywordHelp(MaterialEngine::Doom3, QStringLiteral("sort"), false)));
	output.properties.push_back(property(QStringLiteral("surfaceType"), Text::tr("Surface type"), MaterialGraphPropertyType::Enum,
		definition.surfaceType.isEmpty() ? QStringLiteral("none") : definition.surfaceType,
		{QStringLiteral("none"), QStringLiteral("metal"), QStringLiteral("stone"), QStringLiteral("flesh"), QStringLiteral("wood"),
			QStringLiteral("cardboard"), QStringLiteral("liquid"), QStringLiteral("glass"), QStringLiteral("plastic"), QStringLiteral("ricochet")}));
	output.properties.push_back(property(QStringLiteral("editorImage"), Text::tr("Editor image"), MaterialGraphPropertyType::Image, definition.editorImage));
	output.properties.push_back(property(QStringLiteral("description"), Text::tr("Description"), MaterialGraphPropertyType::Text, definition.description));
	if (definition.isLight() || !definition.lightFalloffImage.isEmpty()) {
		output.properties.push_back(property(QStringLiteral("lightFalloffImage"), Text::tr("Falloff image"), MaterialGraphPropertyType::Text,
			definition.lightFalloffImage, {}, materialKeywordHelp(MaterialEngine::Doom3, QStringLiteral("lightfalloffimage"), false)));
		output.properties.push_back(property(QStringLiteral("lightKind"), Text::tr("Light kind"), MaterialGraphPropertyType::Enum,
			definition.fogLight ? QStringLiteral("fogLight")
				: definition.blendLight ? QStringLiteral("blendLight")
				: definition.ambientLight ? QStringLiteral("ambientLight")
										: QStringLiteral("normal"),
			{QStringLiteral("normal"), QStringLiteral("ambientLight"), QStringLiteral("blendLight"), QStringLiteral("fogLight")}));
	}
	if (!definition.engineRejection.isEmpty()) {
		output.properties.push_back(property(QStringLiteral("rejected"), Text::tr("In game"), MaterialGraphPropertyType::ReadOnly, definition.engineRejection));
	}
	output.inputs.push_back(port(QStringLiteral("geometry"), Text::tr("Geometry"), MaterialGraphPortType::Geometry));
	for (int index = 0; index < definition.stages.size(); ++index) {
		output.inputs.push_back(port(QStringLiteral("stage%1").arg(index), Text::tr("Stage %1").arg(index + 1), MaterialGraphPortType::Stage));
	}
	output.position = QPointF(outputX, std::max(0.0, (definition.stages.size() - 1) * kRow * 3.6 / 2.0));

	for (int stageIndex = 0; stageIndex < definition.stages.size(); ++stageIndex) {
		const MaterialStage& stage = definition.stages.at(stageIndex);
		const double laneY = stageIndex * kRow * 3.6;
		const QString stageId = QStringLiteral("stage/%1").arg(stageIndex);
		const bool shorthand = stage.shorthandDirective >= 0;
		const int imageDirective = shorthand ? stage.shorthandDirective
											 : findDirective(stage.directives, {QStringLiteral("map"), QStringLiteral("cubemap"), QStringLiteral("cameracubemap"),
												   QStringLiteral("videomap")});
		MaterialGraphNode stageNode;
		stageNode.id = stageId;
		stageNode.kind = QStringLiteral("stage");
		stageNode.title = shorthand ? QStringLiteral("%1").arg(definition.directives.value(stage.shorthandDirective).keyword)
									: Text::tr("Stage %1").arg(stageIndex + 1);
		stageNode.subtitle = materialStageRoleId(stage.role);
		stageNode.stage = stageIndex;
		stageNode.line = stage.span.line;
		stageNode.removable = true;
		stageNode.reorderable = !shorthand;
		stageNode.position = QPointF(stageX, laneY + kRow * 0.8);
		stageNode.inputs = {port(QStringLiteral("image"), Text::tr("Image"), MaterialGraphPortType::Image),
			port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates),
			port(QStringLiteral("color"), Text::tr("Colour"), MaterialGraphPortType::Color),
			port(QStringLiteral("condition"), Text::tr("Condition"), MaterialGraphPortType::Scalar),
			port(QStringLiteral("alphaTest"), Text::tr("Alpha test"), MaterialGraphPortType::Scalar)};
		stageNode.outputs.push_back(port(QStringLiteral("stage"), Text::tr("Stage"), MaterialGraphPortType::Stage));
		stageNode.inactive = stage.imageKind == MaterialImageKind::None;
		if (shorthand) {
			stageNode.properties.push_back(property(QStringLiteral("shorthand"), Text::tr("Shorthand"), MaterialGraphPropertyType::ReadOnly,
				Text::tr("Expand it into a stage block to change more than the image.")));
		} else {
			stageNode.properties.push_back(property(QStringLiteral("blend"), Text::tr("Blend"), MaterialGraphPropertyType::Enum, d3BlendChoice(stage),
				{QStringLiteral("replace"), QStringLiteral("blend"), QStringLiteral("add"), QStringLiteral("filter"), QStringLiteral("modulate"),
					QStringLiteral("none"), QStringLiteral("diffusemap"), QStringLiteral("bumpmap"), QStringLiteral("specularmap"), QStringLiteral("custom")},
				materialKeywordHelp(MaterialEngine::Doom3, QStringLiteral("blend"), true)));
			stageNode.properties.push_back(property(QStringLiteral("source"), Text::tr("Source factor"), MaterialGraphPropertyType::Enum,
				materialBlendFactorId(stage.blend.source), blendFactors()));
			stageNode.properties.push_back(property(QStringLiteral("destination"), Text::tr("Destination factor"), MaterialGraphPropertyType::Enum,
				materialBlendFactorId(stage.blend.destination), blendFactors()));
			stageNode.properties.push_back(property(QStringLiteral("wrap"), Text::tr("Edges"), MaterialGraphPropertyType::Enum, wrapChoice(stage),
				{QStringLiteral("repeat"), QStringLiteral("clamp"), QStringLiteral("zeroclamp"), QStringLiteral("alphazeroclamp")}));
			stageNode.properties.push_back(property(QStringLiteral("filter"), Text::tr("Filter"), MaterialGraphPropertyType::Enum,
				findDirective(stage.directives, {QStringLiteral("nearest")}) >= 0 ? QStringLiteral("nearest")
					: findDirective(stage.directives, {QStringLiteral("linear")}) >= 0 ? QStringLiteral("linear")
																					   : QStringLiteral("default"),
				{QStringLiteral("default"), QStringLiteral("nearest"), QStringLiteral("linear")}));
			stageNode.properties.push_back(property(QStringLiteral("vertexColor"), Text::tr("Vertex colour"), MaterialGraphPropertyType::Enum,
				stage.vertexColor == MaterialVertexColor::Vertex ? QStringLiteral("vertexColor")
					: stage.vertexColor == MaterialVertexColor::InverseVertex ? QStringLiteral("inverseVertexColor")
																			  : QStringLiteral("none"),
				{QStringLiteral("none"), QStringLiteral("vertexColor"), QStringLiteral("inverseVertexColor")}));
			stageNode.properties.push_back(property(QStringLiteral("ignoreAlphaTest"), Text::tr("Ignore alpha test"), MaterialGraphPropertyType::Bool,
				boolText(stage.ignoreAlphaTest)));
			stageNode.properties.push_back(property(QStringLiteral("maskColor"), Text::tr("Mask colour"), MaterialGraphPropertyType::Bool,
				boolText(stage.maskRed && stage.maskGreen && stage.maskBlue)));
			stageNode.properties.push_back(property(QStringLiteral("maskAlpha"), Text::tr("Mask alpha"), MaterialGraphPropertyType::Bool, boolText(stage.maskAlpha)));
			stageNode.properties.push_back(property(QStringLiteral("maskDepth"), Text::tr("Mask depth"), MaterialGraphPropertyType::Bool, boolText(stage.maskDepth)));
		}
		graph->nodes.push_back(stageNode);
		link(graph, stageId, QStringLiteral("stage"), output.id, QStringLiteral("stage%1").arg(stageIndex));

		// Image (program tree), cube map or video.
		QString imageId;
		if (stage.imageProgram >= 0 && stage.imageKind != MaterialImageKind::CubeMap) {
			imageId = addProgramNodes(graph, definition, stage.imageProgram, stageId + QStringLiteral("/image"), QPointF(stageX - kColumn * 2.2, laneY),
				stageIndex, imageDirective);
		} else if (stage.imageKind != MaterialImageKind::None) {
			MaterialGraphNode node;
			node.id = stageId + QStringLiteral("/image");
			node.kind = stage.imageKind == MaterialImageKind::CubeMap ? QStringLiteral("cube")
				: stage.imageKind == MaterialImageKind::Video         ? QStringLiteral("video")
																	  : QStringLiteral("image");
			node.title = stage.imageKind == MaterialImageKind::CubeMap ? (stage.cameraCubeMap ? Text::tr("Camera cube map") : Text::tr("Cube map"))
				: stage.imageKind == MaterialImageKind::Video          ? Text::tr("Video")
																	   : Text::tr("Image");
			node.subtitle = stage.imagePath;
			node.stage = stageIndex;
			node.directive = imageDirective;
			node.position = QPointF(stageX - kColumn * 2.2, laneY);
			node.outputs.push_back(port(QStringLiteral("image"), Text::tr("Image"), MaterialGraphPortType::Image));
			node.properties.push_back(property(QStringLiteral("path"), Text::tr("Image"), MaterialGraphPropertyType::Text, stage.imagePath));
			graph->nodes.push_back(node);
			imageId = node.id;
		}
		if (!imageId.isEmpty()) {
			link(graph, imageId, QStringLiteral("image"), stageId, QStringLiteral("image"));
		}
		if (shorthand) {
			continue;
		}

		// Coordinates: texgen, then transforms in order.
		MaterialGraphNode texgen;
		texgen.id = stageId + QStringLiteral("/texgen");
		texgen.kind = QStringLiteral("texgen");
		texgen.title = Text::tr("Coordinates");
		texgen.subtitle = materialTexCoordSourceId(stage.tcGen.source);
		texgen.stage = stageIndex;
		texgen.directive = findDirective(stage.directives, {QStringLiteral("texgen"), QStringLiteral("screen"), QStringLiteral("screen2"), QStringLiteral("glasswarp")});
		texgen.removable = texgen.directive >= 0;
		texgen.position = QPointF(stageX - kColumn * 3.2, laneY + kRow * 1.2);
		texgen.outputs.push_back(port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates));
		texgen.properties.push_back(property(QStringLiteral("source"), Text::tr("Source"), MaterialGraphPropertyType::Enum,
			materialTexCoordSourceId(stage.tcGen.source),
			{QStringLiteral("base"), QStringLiteral("normal"), QStringLiteral("reflect"), QStringLiteral("skybox"), QStringLiteral("wobbleSky"),
				QStringLiteral("screen"), QStringLiteral("screen2"), QStringLiteral("glassWarp")},
			materialKeywordHelp(MaterialEngine::Doom3, QStringLiteral("texgen"), true)));
		graph->nodes.push_back(texgen);
		QString coordsFrom = texgen.id;
		for (int modIndex = 0; modIndex < stage.tcMods.size(); ++modIndex) {
			const MaterialTexMod& mod = stage.tcMods.at(modIndex);
			MaterialGraphNode node;
			node.id = stageId + QStringLiteral("/transform/%1").arg(modIndex);
			node.kind = QStringLiteral("transform");
			node.title = materialTexModKindId(mod.kind);
			node.stage = stageIndex;
			node.directive = mod.directive;
			node.removable = true;
			node.reorderable = true;
			node.line = mod.directive >= 0 && mod.directive < stage.directives.size() ? stage.directives.at(mod.directive).span.line : 0;
			node.subtitle = mod.directive >= 0 && mod.directive < stage.directives.size() ? stage.directives.at(mod.directive).argumentText : QString();
			node.position = QPointF(stageX - kColumn * 2.2, laneY + kRow * (1.2 + 0.95 * (modIndex + 1)));
			node.inputs.push_back(port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates));
			node.inputs.push_back(port(QStringLiteral("a"), mod.kind == MaterialTexModKind::RotateExpression ? Text::tr("Turns") : Text::tr("S"),
				MaterialGraphPortType::Scalar));
			if (mod.kind != MaterialTexModKind::RotateExpression) {
				node.inputs.push_back(port(QStringLiteral("b"), Text::tr("T"), MaterialGraphPortType::Scalar));
			}
			node.outputs.push_back(port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates));
			const QString keyword = mod.directive >= 0 && mod.directive < stage.directives.size() ? stage.directives.at(mod.directive).keyword.toLower()
																									 : materialTexModKindId(mod.kind).toLower();
			node.properties.push_back(property(QStringLiteral("type"), Text::tr("Type"), MaterialGraphPropertyType::Enum, keyword,
				{QStringLiteral("scroll"), QStringLiteral("translate"), QStringLiteral("scale"), QStringLiteral("centerscale"), QStringLiteral("shear"),
					QStringLiteral("rotate")},
				materialKeywordHelp(MaterialEngine::Doom3, keyword, true)));
			node.properties.push_back(property(QStringLiteral("a"), mod.kind == MaterialTexModKind::RotateExpression ? Text::tr("Turns") : Text::tr("S"),
				MaterialGraphPropertyType::Expression, materialExpressionText(definition.expressions, mod.expressions[0])));
			if (mod.kind != MaterialTexModKind::RotateExpression) {
				node.properties.push_back(property(QStringLiteral("b"), Text::tr("T"), MaterialGraphPropertyType::Expression,
					materialExpressionText(definition.expressions, mod.expressions[1])));
			}
			graph->nodes.push_back(node);
			link(graph, coordsFrom, QStringLiteral("coords"), node.id, QStringLiteral("coords"));
			coordsFrom = node.id;
			const QString a = addExpressionNodes(graph, definition, mod.expressions[0], node.id + QStringLiteral("/a/e"),
				QPointF(node.position.x() - kColumn, node.position.y() - kRow * 0.25));
			if (!a.isEmpty()) {
				link(graph, a, QStringLiteral("value"), node.id, QStringLiteral("a"));
			}
			if (mod.kind != MaterialTexModKind::RotateExpression) {
				const QString b = addExpressionNodes(graph, definition, mod.expressions[1], node.id + QStringLiteral("/b/e"),
					QPointF(node.position.x() - kColumn, node.position.y() + kRow * 0.25));
				if (!b.isEmpty()) {
					link(graph, b, QStringLiteral("value"), node.id, QStringLiteral("b"));
				}
			}
		}
		link(graph, coordsFrom, QStringLiteral("coords"), stageId, QStringLiteral("coords"));

		// Colour channels.
		MaterialGraphNode color;
		color.id = stageId + QStringLiteral("/color");
		color.kind = QStringLiteral("color");
		color.title = Text::tr("Colour");
		color.stage = stageIndex;
		color.removable = std::any_of(stage.colorExpressions.cbegin(), stage.colorExpressions.cend(), [](int e) { return e >= 0; });
		color.position = QPointF(stageX - kColumn * 1.1, laneY + kRow * 1.6);
		color.outputs.push_back(port(QStringLiteral("color"), Text::tr("Colour"), MaterialGraphPortType::Color));
		const QStringList channels {QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("alpha")};
		const QStringList labels {Text::tr("Red"), Text::tr("Green"), Text::tr("Blue"), Text::tr("Alpha")};
		for (int channel = 0; channel < 4; ++channel) {
			color.inputs.push_back(port(channels.at(channel), labels.at(channel), MaterialGraphPortType::Scalar));
			const int expression = stage.colorExpressions[static_cast<size_t>(channel)];
			color.properties.push_back(property(channels.at(channel), labels.at(channel), MaterialGraphPropertyType::Expression,
				expression < 0 ? QStringLiteral("1") : materialExpressionText(definition.expressions, expression), {},
				materialKeywordHelp(MaterialEngine::Doom3, QStringLiteral("color"), true)));
		}
		graph->nodes.push_back(color);
		link(graph, color.id, QStringLiteral("color"), stageId, QStringLiteral("color"));
		// Shared expressions (rgb, rgba, colored) appear once.
		QHash<int, QString> shown;
		for (int channel = 0; channel < 4; ++channel) {
			const int expression = stage.colorExpressions[static_cast<size_t>(channel)];
			if (expression < 0) {
				continue;
			}
			if (shown.contains(expression)) {
				link(graph, shown.value(expression), QStringLiteral("value"), color.id, channels.at(channel));
				continue;
			}
			const QString id = addExpressionNodes(graph, definition, expression, color.id + QLatin1Char('/') + channels.at(channel) + QStringLiteral("/e"),
				QPointF(color.position.x() - kColumn, color.position.y() + (channel - 1.5) * kRow * 0.55));
			if (!id.isEmpty()) {
				shown.insert(expression, id);
				link(graph, id, QStringLiteral("value"), color.id, channels.at(channel));
			}
		}
		const auto scalarNode = [&](const char* kind, const QString& title, const char* keyword, int expression, double rowOffset) {
			if (expression < 0) {
				return;
			}
			MaterialGraphNode node;
			node.id = stageId + QLatin1Char('/') + QString::fromLatin1(kind);
			node.kind = QString::fromLatin1(kind);
			node.title = title;
			node.stage = stageIndex;
			node.directive = findDirective(stage.directives, {QString::fromLatin1(keyword)});
			node.removable = true;
			node.position = QPointF(stageX - kColumn * 1.1, laneY + kRow * rowOffset);
			node.inputs.push_back(port(QStringLiteral("value"), title, MaterialGraphPortType::Scalar));
			node.outputs.push_back(port(QStringLiteral("value"), title, MaterialGraphPortType::Scalar));
			node.properties.push_back(property(QStringLiteral("value"), title, MaterialGraphPropertyType::Expression,
				materialExpressionText(definition.expressions, expression), {},
				materialKeywordHelp(MaterialEngine::Doom3, QString::fromLatin1(keyword), true)));
			graph->nodes.push_back(node);
			link(graph, node.id, QStringLiteral("value"), stageId, QString::fromLatin1(kind) == QStringLiteral("condition") ? QStringLiteral("condition")
																														 : QStringLiteral("alphaTest"));
			const QString root = addExpressionNodes(graph, definition, expression, node.id + QStringLiteral("/value/e"),
				QPointF(node.position.x() - kColumn, node.position.y()));
			if (!root.isEmpty()) {
				link(graph, root, QStringLiteral("value"), node.id, QStringLiteral("value"));
			}
		};
		scalarNode("condition", Text::tr("Condition"), "if", stage.conditionExpression, -0.4);
		scalarNode("alphatest", Text::tr("Alpha test"), "alphatest", stage.alphaTestExpression, 2.8);
	}

	for (int deformIndex = 0; deformIndex < definition.deforms.size(); ++deformIndex) {
		const MaterialDeform& deform = definition.deforms.at(deformIndex);
		MaterialGraphNode node;
		node.id = QStringLiteral("deform/%1").arg(deformIndex);
		node.kind = QStringLiteral("deform");
		node.title = Text::tr("Deform %1").arg(materialDeformKindId(deform.kind));
		node.directive = deform.directive;
		node.removable = true;
		node.subtitle = deform.directive >= 0 && deform.directive < definition.directives.size() ? definition.directives.at(deform.directive).argumentText
																								 : QString();
		node.position = QPointF(stageX, -kRow * (1.4 + deformIndex));
		node.outputs.push_back(port(QStringLiteral("geometry"), Text::tr("Geometry"), MaterialGraphPortType::Geometry));
		node.properties.push_back(property(QStringLiteral("arguments"), Text::tr("Arguments"), MaterialGraphPropertyType::Text, node.subtitle, {},
			materialKeywordHelp(MaterialEngine::Doom3, QStringLiteral("deform"), false)));
		graph->nodes.push_back(node);
		link(graph, node.id, QStringLiteral("geometry"), output.id, QStringLiteral("geometry"));
	}
	graph->nodes.push_back(output);
}

// ---------------------------------------------------------------------------
// Classic graphs
// ---------------------------------------------------------------------------

void buildClassicGraph(const MaterialDefinition& definition, MaterialTextKind textKind, MaterialGraph* graph)
{
	MaterialGraphNode output;
	output.id = QStringLiteral("material");
	output.kind = QStringLiteral("output");
	output.title = definition.name;
	output.subtitle = materialEngineDisplayName(definition.engine);
	output.imageReference = definition.name;
	output.position = QPointF(kColumn * 3.2, 0);
	output.inputs.push_back(port(QStringLiteral("frames"), Text::tr("Frames"), MaterialGraphPortType::Frame));
	if (definition.classic.size.isValid()) {
		output.properties.push_back(property(QStringLiteral("size"), Text::tr("Size"), MaterialGraphPropertyType::ReadOnly,
			QStringLiteral("%1 x %2").arg(definition.classic.size.width()).arg(definition.classic.size.height())));
	}
	// Frames, left to right.
	const QVector<MaterialFrame>& frames = definition.classic.frames;
	QString previous;
	for (int index = 0; index < frames.size(); ++index) {
		MaterialGraphNode node;
		node.id = QStringLiteral("frame/%1").arg(index);
		node.kind = QStringLiteral("frame");
		node.title = frames.at(index).name;
		node.subtitle = Text::tr("%1 s").arg(QString::number(frames.at(index).duration, 'f', 3));
		node.imageReference = frames.at(index).name;
		node.position = QPointF(0, (index - (frames.size() - 1) / 2.0) * kRow);
		node.outputs.push_back(port(QStringLiteral("frame"), Text::tr("Frame"), MaterialGraphPortType::Frame));
		graph->nodes.push_back(node);
		previous = node.id;
	}
	if (!frames.isEmpty()) {
		MaterialGraphNode animation;
		animation.id = QStringLiteral("animation");
		animation.kind = QStringLiteral("animation");
		animation.title = Text::tr("Animation");
		animation.subtitle = definition.classic.animationSource;
		animation.position = QPointF(kColumn * 1.6, 0);
		animation.outputs.push_back(port(QStringLiteral("frames"), Text::tr("Frames"), MaterialGraphPortType::Frame));
		for (int index = 0; index < frames.size(); ++index) {
			animation.inputs.push_back(port(QStringLiteral("frame%1").arg(index), frames.at(index).name, MaterialGraphPortType::Frame));
			link(graph, QStringLiteral("frame/%1").arg(index), QStringLiteral("frame"), animation.id, QStringLiteral("frame%1").arg(index));
		}
		const bool doom = definition.engine == MaterialEngine::Doom;
		const int tics = doom ? static_cast<int>(std::lround(frames.first().duration * kDoomTicsPerSecond)) : 0;
		if (doom && (textKind == MaterialTextKind::DoomSwantbls || textKind == MaterialTextKind::DoomAnimdefs)) {
			animation.properties.push_back(property(QStringLiteral("tics"), Text::tr("Tics a frame"), MaterialGraphPropertyType::Integer, QString::number(tics), {},
				Text::tr("Doom runs at 35 tics a second; 8 tics is the vanilla speed.")));
		} else {
			animation.properties.push_back(property(QStringLiteral("timing"), Text::tr("Timing"), MaterialGraphPropertyType::ReadOnly,
				Text::tr("%1 s a frame, set by the engine").arg(QString::number(frames.first().duration, 'f', 3))));
		}
		animation.properties.push_back(property(QStringLiteral("first"), Text::tr("First"), MaterialGraphPropertyType::ReadOnly, frames.first().name));
		animation.properties.push_back(property(QStringLiteral("last"), Text::tr("Last"), MaterialGraphPropertyType::ReadOnly, frames.last().name));
		graph->nodes.push_back(animation);
		link(graph, animation.id, QStringLiteral("frames"), output.id, QStringLiteral("frames"));
	}
	if (definition.engine == MaterialEngine::Doom) {
		for (int index = 0; index < definition.classic.patches.size(); ++index) {
			const MaterialPatchPlacement& patch = definition.classic.patches.at(index);
			MaterialGraphNode node;
			node.id = QStringLiteral("patch/%1").arg(index);
			node.kind = QStringLiteral("patch");
			node.title = patch.patch;
			node.subtitle = QStringLiteral("%1, %2").arg(patch.x).arg(patch.y);
			node.imageReference = patch.patch;
			node.position = QPointF(kColumn * 1.6, kRow * (2.0 + index));
			node.outputs.push_back(port(QStringLiteral("image"), Text::tr("Patch"), MaterialGraphPortType::Image));
			node.properties.push_back(property(QStringLiteral("offset"), Text::tr("Offset"), MaterialGraphPropertyType::ReadOnly, node.subtitle));
			graph->nodes.push_back(node);
			output.inputs.push_back(port(QStringLiteral("patch%1").arg(index), patch.patch, MaterialGraphPortType::Image));
			link(graph, node.id, QStringLiteral("image"), output.id, QStringLiteral("patch%1").arg(index));
		}
		MaterialGraphNode node;
		node.id = QStringLiteral("switch");
		node.kind = QStringLiteral("switch");
		node.title = Text::tr("Switch");
		node.subtitle = definition.classic.switchPartner.isEmpty() ? Text::tr("none") : definition.classic.switchPartner;
		node.position = QPointF(kColumn * 3.2, -kRow * 1.8);
		node.outputs.push_back(port(QStringLiteral("frame"), Text::tr("Pressed"), MaterialGraphPortType::Frame));
		node.properties.push_back(property(QStringLiteral("partner"), Text::tr("Pressed texture"),
			textKind == MaterialTextKind::DoomSwantbls ? MaterialGraphPropertyType::Text : MaterialGraphPropertyType::ReadOnly,
			definition.classic.switchPartner));
		graph->nodes.push_back(node);
		if (definition.classic.warp != MaterialWarpStyle::None) {
			MaterialGraphNode warp;
			warp.id = QStringLiteral("warp");
			warp.kind = QStringLiteral("warp");
			warp.title = materialWarpStyleId(definition.classic.warp);
			warp.position = QPointF(kColumn * 1.6, -kRow * 1.8);
			warp.outputs.push_back(port(QStringLiteral("coords"), Text::tr("Coordinates"), MaterialGraphPortType::Coordinates));
			warp.properties.push_back(property(QStringLiteral("speed"), Text::tr("Speed"),
				textKind == MaterialTextKind::DoomAnimdefs ? MaterialGraphPropertyType::Number : MaterialGraphPropertyType::ReadOnly,
				numberText(definition.classic.warpSpeed)));
			graph->nodes.push_back(warp);
			link(graph, warp.id, QStringLiteral("coords"), output.id, QStringLiteral("frames"));
		}
	}
	if (definition.engine == MaterialEngine::Quake) {
		const QuakeTextureName parsed = parseQuakeTextureName(definition.name);
		MaterialGraphNode info;
		info.id = QStringLiteral("info");
		info.kind = QStringLiteral("info");
		info.title = Text::tr("Name rules");
		info.position = QPointF(kColumn * 1.6, kRow * 2.0);
		info.properties.push_back(property(QStringLiteral("liquid"), Text::tr("Liquid (*)"), MaterialGraphPropertyType::ReadOnly, boolText(parsed.liquid)));
		info.properties.push_back(property(QStringLiteral("sky"), Text::tr("Sky (sky...)"), MaterialGraphPropertyType::ReadOnly, boolText(parsed.sky)));
		info.properties.push_back(property(QStringLiteral("masked"), Text::tr("Masked ({)"), MaterialGraphPropertyType::ReadOnly, boolText(parsed.masked)));
		info.properties.push_back(property(QStringLiteral("animated"), Text::tr("Animated (+N)"), MaterialGraphPropertyType::ReadOnly,
			boolText(parsed.frame >= 0)));
		info.properties.push_back(property(QStringLiteral("note"), Text::tr("How to change"), MaterialGraphPropertyType::ReadOnly,
			Text::tr("Quake reads these from the texture name; rename the texture in its WAD to change them.")));
		graph->nodes.push_back(info);
	}
	if (definition.engine == MaterialEngine::Quake2) {
		MaterialGraphNode flags;
		flags.id = QStringLiteral("flags");
		flags.kind = QStringLiteral("flags");
		flags.title = Text::tr("Surface");
		flags.position = QPointF(kColumn * 1.6, kRow * 2.0);
		flags.outputs.push_back(port(QStringLiteral("flags"), Text::tr("Flags"), MaterialGraphPortType::Geometry));
		const bool editable = textKind == MaterialTextKind::Quake2WalJson;
		for (const MaterialFlagDescriptor& descriptor : quake2SurfaceFlagDescriptors()) {
			MaterialGraphProperty flag = property(QStringLiteral("flag:") + descriptor.id, descriptor.displayName,
				editable ? MaterialGraphPropertyType::Bool : MaterialGraphPropertyType::ReadOnly, boolText((definition.classic.surfaceFlags & descriptor.bit) != 0));
			flag.help = descriptor.description;
			flags.properties.push_back(flag);
		}
		flags.properties.push_back(property(QStringLiteral("value"), Text::tr("Value (light)"), editable ? MaterialGraphPropertyType::Integer
																									  : MaterialGraphPropertyType::ReadOnly,
			QString::number(definition.classic.surfaceValue)));
		flags.properties.push_back(property(QStringLiteral("next"), Text::tr("Next frame"), editable ? MaterialGraphPropertyType::Text
																							   : MaterialGraphPropertyType::ReadOnly,
			definition.classic.nextFrame));
		QStringList contents;
		for (const MaterialFlagDescriptor& descriptor : quake2ContentFlagDescriptors()) {
			if ((definition.classic.contentFlags & descriptor.bit) != 0) {
				contents << descriptor.id;
			}
		}
		flags.properties.push_back(property(QStringLiteral("contents"), Text::tr("Contents"), editable ? MaterialGraphPropertyType::Text
																								 : MaterialGraphPropertyType::ReadOnly,
			contents.join(QLatin1Char(' '))));
		graph->nodes.push_back(flags);
		output.inputs.push_back(port(QStringLiteral("flags"), Text::tr("Flags"), MaterialGraphPortType::Geometry));
		link(graph, flags.id, QStringLiteral("flags"), output.id, QStringLiteral("flags"));
	}
	graph->nodes.push_back(output);
}

} // namespace

const MaterialGraphProperty* MaterialGraphNode::property(const QString& propertyId) const
{
	for (const MaterialGraphProperty& item : properties) {
		if (item.id == propertyId) {
			return &item;
		}
	}
	return nullptr;
}

int MaterialGraph::indexOf(const QString& nodeId) const
{
	for (int index = 0; index < nodes.size(); ++index) {
		if (nodes.at(index).id == nodeId) {
			return index;
		}
	}
	return -1;
}

const MaterialGraphNode* MaterialGraph::node(const QString& nodeId) const
{
	const int index = indexOf(nodeId);
	return index < 0 ? nullptr : &nodes.at(index);
}

QString materialTextKindId(MaterialTextKind kind)
{
	switch (kind) {
	case MaterialTextKind::None:
		return QStringLiteral("none");
	case MaterialTextKind::Quake3Shader:
		return QStringLiteral("quake3-shader");
	case MaterialTextKind::Doom3Material:
		return QStringLiteral("doom3-material");
	case MaterialTextKind::DoomSwantbls:
		return QStringLiteral("swantbls");
	case MaterialTextKind::DoomAnimdefs:
		return QStringLiteral("animdefs");
	case MaterialTextKind::Quake2WalJson:
		return QStringLiteral("wal-json");
	}
	return QStringLiteral("none");
}

bool materialTextKindFromId(const QString& id, MaterialTextKind* kind)
{
	for (MaterialTextKind candidate : {MaterialTextKind::None, MaterialTextKind::Quake3Shader, MaterialTextKind::Doom3Material,
			 MaterialTextKind::DoomSwantbls, MaterialTextKind::DoomAnimdefs, MaterialTextKind::Quake2WalJson}) {
		if (materialTextKindId(candidate) == id.trimmed().toLower()) {
			*kind = candidate;
			return true;
		}
	}
	return false;
}

MaterialTextKind defaultMaterialTextKind(const MaterialDefinition& definition)
{
	switch (definition.engine) {
	case MaterialEngine::Quake3:
		return MaterialTextKind::Quake3Shader;
	case MaterialEngine::Doom3:
		return MaterialTextKind::Doom3Material;
	case MaterialEngine::Doom:
		return definition.classic.animationSource == QStringLiteral("animdefs") ? MaterialTextKind::DoomAnimdefs : MaterialTextKind::DoomSwantbls;
	case MaterialEngine::Quake2:
		return MaterialTextKind::Quake2WalJson;
	default:
		break;
	}
	return MaterialTextKind::None;
}

QString materialGraphPortTypeId(MaterialGraphPortType type)
{
	switch (type) {
	case MaterialGraphPortType::Image:
		return QStringLiteral("image");
	case MaterialGraphPortType::Coordinates:
		return QStringLiteral("coordinates");
	case MaterialGraphPortType::Color:
		return QStringLiteral("color");
	case MaterialGraphPortType::Alpha:
		return QStringLiteral("alpha");
	case MaterialGraphPortType::Scalar:
		return QStringLiteral("scalar");
	case MaterialGraphPortType::Stage:
		return QStringLiteral("stage");
	case MaterialGraphPortType::Geometry:
		return QStringLiteral("geometry");
	case MaterialGraphPortType::Frame:
		return QStringLiteral("frame");
	}
	return QStringLiteral("scalar");
}

QString materialGraphPropertyTypeId(MaterialGraphPropertyType type)
{
	switch (type) {
	case MaterialGraphPropertyType::Text:
		return QStringLiteral("text");
	case MaterialGraphPropertyType::Number:
		return QStringLiteral("number");
	case MaterialGraphPropertyType::Integer:
		return QStringLiteral("integer");
	case MaterialGraphPropertyType::Bool:
		return QStringLiteral("bool");
	case MaterialGraphPropertyType::Enum:
		return QStringLiteral("enum");
	case MaterialGraphPropertyType::Expression:
		return QStringLiteral("expression");
	case MaterialGraphPropertyType::Image:
		return QStringLiteral("image");
	case MaterialGraphPropertyType::ReadOnly:
		return QStringLiteral("read-only");
	}
	return QStringLiteral("text");
}

MaterialGraph buildMaterialGraph(const MaterialDefinition& definition, MaterialTextKind textKind)
{
	MaterialGraph graph;
	graph.engine = definition.engine;
	graph.textKind = textKind == MaterialTextKind::None ? defaultMaterialTextKind(definition) : textKind;
	graph.material = definition.name;
	switch (definition.engine) {
	case MaterialEngine::Quake3:
		buildQuake3Graph(definition, &graph);
		break;
	case MaterialEngine::Doom3:
		buildDoom3Graph(definition, &graph);
		break;
	case MaterialEngine::Doom:
	case MaterialEngine::Quake:
	case MaterialEngine::Quake2:
		buildClassicGraph(definition, graph.textKind, &graph);
		break;
	case MaterialEngine::Unknown:
		break;
	}
	return graph;
}

QVector<MaterialGraphNodeTemplate> materialGraphNodeTemplates(MaterialEngine engine, MaterialTextKind textKind)
{
	QVector<MaterialGraphNodeTemplate> templates;
	const auto add = [&](const char* id, const QString& category, const QString& title, const QString& description, bool stage) {
		templates.push_back({QString::fromLatin1(id), category, title, description, stage});
	};
	const QString stages = Text::tr("Stages");
	const QString coordinates = Text::tr("Coordinates");
	const QString color = Text::tr("Colour");
	const QString geometry = Text::tr("Geometry");
	const QString surface = Text::tr("Surface");
	const QString math = Text::tr("Expressions");
	if (engine == MaterialEngine::Quake3) {
		add("q3.stage.image", stages, Text::tr("Image stage"), Text::tr("A new stage drawing an image."), false);
		add("q3.stage.lightmap", stages, Text::tr("Lightmap stage"), Text::tr("map $lightmap with rgbGen identity."), false);
		add("q3.stage.add", stages, Text::tr("Additive stage"), Text::tr("An image added over what is drawn (blendFunc add)."), false);
		add("q3.stage.filter", stages, Text::tr("Filter stage"), Text::tr("An image multiplied into what is drawn (blendFunc filter)."), false);
		add("q3.tcmod.scroll", coordinates, Text::tr("Scroll"), Text::tr("tcMod scroll: move the image steadily."), true);
		add("q3.tcmod.scale", coordinates, Text::tr("Scale"), Text::tr("tcMod scale: repeat the image more or less often."), true);
		add("q3.tcmod.rotate", coordinates, Text::tr("Rotate"), Text::tr("tcMod rotate: spin about the image centre."), true);
		add("q3.tcmod.turb", coordinates, Text::tr("Turbulence"), Text::tr("tcMod turb: a liquid wobble."), true);
		add("q3.tcmod.stretch", coordinates, Text::tr("Stretch"), Text::tr("tcMod stretch: pulse the image's size with a wave."), true);
		add("q3.tcgen.environment", coordinates, Text::tr("Environment map"), Text::tr("tcGen environment: a fake reflection."), true);
		add("q3.rgbgen.wave", color, Text::tr("Colour wave"), Text::tr("rgbGen wave: brightness follows a waveform."), true);
		add("q3.rgbgen.const", color, Text::tr("Constant colour"), Text::tr("rgbGen const: tint the stage."), true);
		add("q3.alphagen.const", color, Text::tr("Constant alpha"), Text::tr("alphaGen const: fixed transparency."), true);
		add("q3.alphafunc", color, Text::tr("Alpha test"), Text::tr("alphaFunc GE128: cut out transparent pixels."), true);
		add("q3.deform.wave", geometry, Text::tr("Wave deform"), Text::tr("deformVertexes wave: vertices ripple along their normals."), false);
		add("q3.deform.bulge", geometry, Text::tr("Bulge deform"), Text::tr("deformVertexes bulge: a moving swell along the texture."), false);
		add("q3.deform.move", geometry, Text::tr("Move deform"), Text::tr("deformVertexes move: the surface bobs."), false);
		add("q3.deform.autosprite", geometry, Text::tr("Autosprite"), Text::tr("deformVertexes autosprite: always faces the viewer."), false);
		add("q3.surfaceparm", surface, Text::tr("Surface parameter"), Text::tr("surfaceparm nolightmap (edit it after adding)."), false);
		add("q3.sky", surface, Text::tr("Sky"), Text::tr("skyParms with a far box."), false);
		add("q3.fog", surface, Text::tr("Fog"), Text::tr("fogParms with a colour and distance."), false);
	} else if (engine == MaterialEngine::Doom3) {
		add("d3.stage.image", stages, Text::tr("Blended image stage"), Text::tr("An unlit image blended over the surface."), false);
		add("d3.stage.add", stages, Text::tr("Glow stage"), Text::tr("An unlit image added over the surface."), false);
		add("d3.stage.diffuse", stages, Text::tr("Diffuse stage"), Text::tr("The colour the lights illuminate."), false);
		add("d3.stage.bump", stages, Text::tr("Bump stage"), Text::tr("A normal map for the lights."), false);
		add("d3.stage.specular", stages, Text::tr("Specular stage"), Text::tr("How shiny the surface is to the lights."), false);
		add("d3.transform.scroll", coordinates, Text::tr("Scroll"), Text::tr("scroll time * 0.1, 0"), true);
		add("d3.transform.scale", coordinates, Text::tr("Scale"), Text::tr("scale 2, 2"), true);
		add("d3.transform.centerscale", coordinates, Text::tr("Centre scale"), Text::tr("centerScale 1, 1"), true);
		add("d3.transform.rotate", coordinates, Text::tr("Rotate"), Text::tr("rotate time * 0.1"), true);
		add("d3.transform.shear", coordinates, Text::tr("Shear"), Text::tr("shear 0.1, 0"), true);
		add("d3.texgen.reflect", coordinates, Text::tr("Reflection"), Text::tr("texGen reflect with a cube map."), true);
		add("d3.color.rgb", color, Text::tr("Brightness"), Text::tr("rgb: one expression for red, green and blue."), true);
		add("d3.color.pulse", color, Text::tr("Pulse"), Text::tr("rgb 0.5 + 0.5 * sinTable[ time ]"), true);
		add("d3.condition", color, Text::tr("Condition"), Text::tr("if: draw only while an expression is not zero."), true);
		add("d3.alphatest", color, Text::tr("Alpha test"), Text::tr("alphaTest 0.5: cut out transparent pixels."), true);
		add("d3.deform.turbulent", geometry, Text::tr("Turbulent deform"), Text::tr("deform turbulent sinTable 0.0175 time * 0.15 10"), false);
		add("d3.deform.expand", geometry, Text::tr("Expand deform"), Text::tr("deform expand: push the surface along its normals."), false);
		add("d3.deform.sprite", geometry, Text::tr("Sprite deform"), Text::tr("deform sprite: always faces the viewer."), false);
		add("d3.translucent", surface, Text::tr("Translucent"), Text::tr("Blended, unlit and drawn after opaque surfaces."), false);
		add("d3.table", math, Text::tr("Table"), Text::tr("A new table decl for expressions to read."), false);
	} else if (engine == MaterialEngine::Doom && textKind == MaterialTextKind::DoomSwantbls) {
		add("doom.range", stages, Text::tr("Animated range"), Text::tr("Animate this texture through the next names in its namespace."), false);
		add("doom.switch", stages, Text::tr("Switch"), Text::tr("Make this texture a switch with a pressed partner."), false);
	} else if (engine == MaterialEngine::Doom && textKind == MaterialTextKind::DoomAnimdefs) {
		add("doom.animdefs.range", stages, Text::tr("Animated range"), Text::tr("An ANIMDEFS range entry for this texture."), false);
		add("doom.animdefs.warp", stages, Text::tr("Warp"), Text::tr("An ANIMDEFS warp entry (ZDoom ports)."), false);
		add("doom.animdefs.warp2", stages, Text::tr("Swirl"), Text::tr("An ANIMDEFS warp2 entry (ZDoom ports)."), false);
	}
	return templates;
}

QString materialGraphEditKindId(MaterialGraphEditKind kind)
{
	switch (kind) {
	case MaterialGraphEditKind::SetProperty:
		return QStringLiteral("set");
	case MaterialGraphEditKind::AddNode:
		return QStringLiteral("add");
	case MaterialGraphEditKind::RemoveNode:
		return QStringLiteral("remove");
	case MaterialGraphEditKind::MoveNode:
		return QStringLiteral("move");
	case MaterialGraphEditKind::WrapExpression:
		return QStringLiteral("wrap");
	case MaterialGraphEditKind::UnwrapExpression:
		return QStringLiteral("unwrap");
	case MaterialGraphEditKind::ExpandShorthand:
		return QStringLiteral("expand");
	}
	return QStringLiteral("set");
}

bool materialGraphEditKindFromId(const QString& id, MaterialGraphEditKind* kind)
{
	for (MaterialGraphEditKind candidate : {MaterialGraphEditKind::SetProperty, MaterialGraphEditKind::AddNode, MaterialGraphEditKind::RemoveNode,
			 MaterialGraphEditKind::MoveNode, MaterialGraphEditKind::WrapExpression, MaterialGraphEditKind::UnwrapExpression,
			 MaterialGraphEditKind::ExpandShorthand}) {
		if (materialGraphEditKindId(candidate) == id.trimmed().toLower()) {
			*kind = candidate;
			return true;
		}
	}
	return false;
}

namespace {

// ---------------------------------------------------------------------------
// Edits: shared helpers
// ---------------------------------------------------------------------------

MaterialGraphEditResult fail(const QString& error)
{
	MaterialGraphEditResult result;
	result.error = error;
	return result;
}

MaterialEdit setEdit(const QString& material, int stage, int directive, const QString& keyword, const QString& arguments)
{
	MaterialEdit edit;
	edit.kind = MaterialEditKind::SetDirective;
	edit.material = material;
	edit.stage = stage;
	edit.directive = directive;
	edit.keyword = keyword;
	edit.arguments = arguments;
	return edit;
}

MaterialEdit addEdit(const QString& material, int stage, const QString& keyword, const QString& arguments, int position = -1)
{
	MaterialEdit edit;
	edit.kind = MaterialEditKind::AddDirective;
	edit.material = material;
	edit.stage = stage;
	edit.keyword = keyword;
	edit.arguments = arguments;
	edit.position = position;
	return edit;
}

MaterialEdit removeEdit(const QString& material, int stage, int directive)
{
	MaterialEdit edit;
	edit.kind = MaterialEditKind::RemoveDirective;
	edit.material = material;
	edit.stage = stage;
	edit.directive = directive;
	return edit;
}

// Removes every directive named in `keywords` from a block, last first so
// the earlier indexes stay valid.
void removeAll(QVector<MaterialEdit>* edits, const QString& material, int stage, const QVector<MaterialDirective>& directives, const QStringList& keywords)
{
	QVector<int> found = findDirectives(directives, keywords);
	std::sort(found.begin(), found.end(), std::greater<int>());
	for (int index : found) {
		edits->push_back(removeEdit(material, stage, index));
	}
}

// Sets a directive by keyword, adding it when missing.
void setOrAdd(QVector<MaterialEdit>* edits, const QString& material, int stage, const QVector<MaterialDirective>& directives, const QString& keyword,
	const QString& arguments)
{
	const int index = findDirective(directives, {keyword.toLower()});
	if (index >= 0) {
		edits->push_back(setEdit(material, stage, index, directives.at(index).keyword, arguments));
	} else {
		edits->push_back(addEdit(material, stage, keyword, arguments));
	}
}

void setBool(QVector<MaterialEdit>* edits, const QString& material, int stage, const QVector<MaterialDirective>& directives, const QString& keyword,
	bool on)
{
	const int index = findDirective(directives, {keyword.toLower()});
	if (on && index < 0) {
		edits->push_back(addEdit(material, stage, keyword, QString()));
	} else if (!on && index >= 0) {
		removeAll(edits, material, stage, directives, {keyword.toLower()});
	}
}

int stageOf(const QString& nodeId)
{
	if (!nodeId.startsWith(QStringLiteral("stage/"))) {
		return -1;
	}
	return nodeId.section(QLatin1Char('/'), 1, 1).toInt();
}

MaterialGraphNode withProperty(MaterialGraphNode node, const QString& propertyId, const QString& value)
{
	for (MaterialGraphProperty& item : node.properties) {
		if (item.id == propertyId) {
			item.value = value;
			return node;
		}
	}
	node.properties.push_back(property(propertyId, propertyId, MaterialGraphPropertyType::Text, value));
	return node;
}

QString propertyValue(const MaterialGraphNode& node, const QString& id, const QString& fallback = QString())
{
	const MaterialGraphProperty* item = node.property(id);
	return item ? item->value : fallback;
}

// ---------------------------------------------------------------------------
// Doom 3 expression trees
// ---------------------------------------------------------------------------

struct ExprTree {
	MaterialExpressionOp op = MaterialExpressionOp::Constant;
	double value = 0.0;
	int index = 0;
	QString name;
	bool parenthesised = false;
	std::unique_ptr<ExprTree> a;
	std::unique_ptr<ExprTree> b;
};

std::unique_ptr<ExprTree> treeFrom(const QVector<MaterialExpressionNode>& nodes, int root, int depth = 0)
{
	if (root < 0 || root >= nodes.size() || depth > 64) {
		auto one = std::make_unique<ExprTree>();
		one->value = 1.0;
		return one;
	}
	const MaterialExpressionNode& node = nodes.at(root);
	auto tree = std::make_unique<ExprTree>();
	tree->op = node.op;
	tree->value = node.value;
	tree->index = node.index;
	tree->name = node.name;
	tree->parenthesised = node.parenthesised;
	if (node.a >= 0) {
		tree->a = treeFrom(nodes, node.a, depth + 1);
	}
	if (node.b >= 0) {
		tree->b = treeFrom(nodes, node.b, depth + 1);
	}
	return tree;
}

int poolFrom(const ExprTree& tree, QVector<MaterialExpressionNode>* pool)
{
	MaterialExpressionNode node;
	node.op = tree.op;
	node.value = tree.value;
	node.index = tree.index;
	node.name = tree.name;
	node.parenthesised = tree.parenthesised;
	if (tree.a) {
		node.a = poolFrom(*tree.a, pool);
	}
	if (tree.b) {
		node.b = poolFrom(*tree.b, pool);
	}
	pool->push_back(node);
	return static_cast<int>(pool->size()) - 1;
}

QString treeText(const ExprTree& tree)
{
	QVector<MaterialExpressionNode> pool;
	const int root = poolFrom(tree, &pool);
	return materialExpressionText(pool, root);
}

std::unique_ptr<ExprTree> parseTree(const QString& text, QString* error)
{
	QVector<MaterialExpressionNode> pool;
	const int root = parseMaterialExpression(text, &pool, error);
	if (root < 0) {
		return {};
	}
	return treeFrom(pool, root);
}

std::unique_ptr<ExprTree>* navigateOwner(std::unique_ptr<ExprTree>* root, const QStringList& path)
{
	std::unique_ptr<ExprTree>* slot = root;
	for (const QString& step : path) {
		if (!*slot) {
			return nullptr;
		}
		slot = step == QStringLiteral("a") ? &(*slot)->a : step == QStringLiteral("b") ? &(*slot)->b : nullptr;
		if (!slot) {
			return nullptr;
		}
	}
	return slot;
}

bool isBinary(MaterialExpressionOp op)
{
	return materialExpressionOpPriority(op) > 0;
}

bool expressionOpFromId(const QString& id, MaterialExpressionOp* op)
{
	static const QList<MaterialExpressionOp> all {MaterialExpressionOp::Constant, MaterialExpressionOp::Time, MaterialExpressionOp::Parm,
		MaterialExpressionOp::Global, MaterialExpressionOp::Sound, MaterialExpressionOp::Table, MaterialExpressionOp::Add, MaterialExpressionOp::Subtract,
		MaterialExpressionOp::Multiply, MaterialExpressionOp::Divide, MaterialExpressionOp::Modulo, MaterialExpressionOp::Greater,
		MaterialExpressionOp::GreaterEqual, MaterialExpressionOp::Less, MaterialExpressionOp::LessEqual, MaterialExpressionOp::Equal,
		MaterialExpressionOp::NotEqual, MaterialExpressionOp::And, MaterialExpressionOp::Or};
	for (MaterialExpressionOp candidate : all) {
		if (materialExpressionOpId(candidate) == id) {
			*op = candidate;
			return true;
		}
	}
	return false;
}

// Where an expression node's tree lives: the owner node, its slot and the
// path below the root.
struct ExpressionAddress {
	QString owner;    // e.g. "stage/2/transform/0", "stage/2/color", "stage/2/condition"
	QString slot;     // "a", "b", "red".."alpha", "value"
	QStringList path; // below the root
};

bool expressionAddress(const QString& nodeId, ExpressionAddress* address)
{
	const int marker = nodeId.indexOf(QStringLiteral("/e"));
	if (marker < 0) {
		return false;
	}
	const QString before = nodeId.left(marker);
	const int slash = before.lastIndexOf(QLatin1Char('/'));
	if (slash < 0) {
		return false;
	}
	address->owner = before.left(slash);
	address->slot = before.mid(slash + 1);
	const QString rest = nodeId.mid(marker + 2);
	address->path = rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
	return nodeId.mid(marker, 2) == QStringLiteral("/e") && (rest.isEmpty() || rest.startsWith(QLatin1Char('/')));
}

// The text of the expression slot an owner node holds now.
QString slotText(const MaterialDefinition& definition, const QString& owner, const QString& slot, int* rootOut = nullptr)
{
	const int stageIndex = stageOf(owner);
	if (stageIndex < 0 || stageIndex >= definition.stages.size()) {
		return QString();
	}
	const MaterialStage& stage = definition.stages.at(stageIndex);
	int root = -1;
	if (owner.contains(QStringLiteral("/transform/"))) {
		const int mod = owner.section(QLatin1Char('/'), 3, 3).toInt();
		if (mod >= 0 && mod < stage.tcMods.size()) {
			root = stage.tcMods.at(mod).expressions[slot == QStringLiteral("b") ? 1 : 0];
		}
	} else if (owner.endsWith(QStringLiteral("/color"))) {
		const QStringList channels {QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("alpha")};
		const int channel = static_cast<int>(channels.indexOf(slot));
		if (channel >= 0) {
			root = stage.colorExpressions[static_cast<size_t>(channel)];
		}
	} else if (owner.endsWith(QStringLiteral("/condition"))) {
		root = stage.conditionExpression;
	} else if (owner.endsWith(QStringLiteral("/alphatest"))) {
		root = stage.alphaTestExpression;
	}
	if (rootOut) {
		*rootOut = root;
	}
	return root < 0 ? QStringLiteral("1") : materialExpressionText(definition.expressions, root);
}

// Writes a new expression into an owner's slot as text edits.
bool writeSlot(const MaterialDefinition& definition, const QString& owner, const QString& slot, const QString& expression, QVector<MaterialEdit>* edits,
	QString* error)
{
	QVector<MaterialExpressionNode> check;
	if (!expression.trimmed().isEmpty() && parseMaterialExpression(expression, &check, error) < 0) {
		return false;
	}
	const int stageIndex = stageOf(owner);
	if (stageIndex < 0 || stageIndex >= definition.stages.size()) {
		*error = Text::tr("That stage no longer exists.");
		return false;
	}
	const MaterialStage& stage = definition.stages.at(stageIndex);
	if (owner.contains(QStringLiteral("/transform/"))) {
		const int mod = owner.section(QLatin1Char('/'), 3, 3).toInt();
		if (mod < 0 || mod >= stage.tcMods.size()) {
			*error = Text::tr("That transform no longer exists.");
			return false;
		}
		const MaterialTexMod& texMod = stage.tcMods.at(mod);
		const QString a = slot == QStringLiteral("a") ? expression : materialExpressionText(definition.expressions, texMod.expressions[0]);
		const QString b = slot == QStringLiteral("b") ? expression : materialExpressionText(definition.expressions, texMod.expressions[1]);
		const QString keyword = stage.directives.value(texMod.directive).keyword;
		edits->push_back(setEdit(definition.name, stageIndex, texMod.directive, keyword,
			texMod.kind == MaterialTexModKind::RotateExpression ? a : QStringLiteral("%1, %2").arg(a, b)));
		return true;
	}
	if (owner.endsWith(QStringLiteral("/color"))) {
		const QStringList channels {QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("alpha")};
		QStringList values;
		for (int channel = 0; channel < 4; ++channel) {
			const int root = stage.colorExpressions[static_cast<size_t>(channel)];
			values << (root < 0 ? QStringLiteral("1") : materialExpressionText(definition.expressions, root));
		}
		const int changed = static_cast<int>(channels.indexOf(slot));
		const QString replacement = expression.trimmed().isEmpty() ? QStringLiteral("1") : expression.trimmed();
		if (changed >= 0) {
			// rgb, rgba and colored share one expression between channels;
			// an edit through one channel changes every channel sharing it.
			const int shared = stage.colorExpressions[static_cast<size_t>(changed)];
			for (int channel = 0; channel < 4; ++channel) {
				if (channel == changed || (shared >= 0 && stage.colorExpressions[static_cast<size_t>(channel)] == shared)) {
					values[channel] = replacement;
				}
			}
		} else if (slot == QStringLiteral("rgb")) {
			values[0] = values[1] = values[2] = replacement;
		}
		removeAll(edits, definition.name, stageIndex, stage.directives,
			{QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("alpha"), QStringLiteral("rgb"),
				QStringLiteral("rgba"), QStringLiteral("color"), QStringLiteral("colored")});
		// One canonical directive for the four channels.
		if (values == QStringList {QStringLiteral("parm0"), QStringLiteral("parm1"), QStringLiteral("parm2"), QStringLiteral("parm3")}) {
			edits->push_back(addEdit(definition.name, stageIndex, QStringLiteral("colored"), QString()));
		} else if (values.at(0) == values.at(1) && values.at(1) == values.at(2) && values.at(2) == values.at(3)) {
			if (values.at(0) != QStringLiteral("1")) {
				edits->push_back(addEdit(definition.name, stageIndex, QStringLiteral("rgba"), values.at(0)));
			}
		} else if (values.at(0) == values.at(1) && values.at(1) == values.at(2) && values.at(3) == QStringLiteral("1")) {
			edits->push_back(addEdit(definition.name, stageIndex, QStringLiteral("rgb"), values.at(0)));
		} else {
			edits->push_back(addEdit(definition.name, stageIndex, QStringLiteral("color"), values.join(QStringLiteral(", "))));
		}
		return true;
	}
	const QString keyword = owner.endsWith(QStringLiteral("/condition")) ? QStringLiteral("if") : QStringLiteral("alphaTest");
	if (expression.trimmed().isEmpty()) {
		removeAll(edits, definition.name, stageIndex, stage.directives, {keyword.toLower()});
	} else {
		setOrAdd(edits, definition.name, stageIndex, stage.directives, keyword, expression.trimmed());
	}
	return true;
}

bool expressionEdit(const MaterialDefinition& definition, const MaterialGraphEdit& edit, QVector<MaterialEdit>* edits, QString* error)
{
	ExpressionAddress address;
	if (!expressionAddress(edit.node, &address)) {
		*error = Text::tr("%1 is not an expression node.").arg(edit.node);
		return false;
	}
	std::unique_ptr<ExprTree> root = parseTree(slotText(definition, address.owner, address.slot), error);
	if (!root) {
		return false;
	}
	std::unique_ptr<ExprTree>* slot = navigateOwner(&root, address.path);
	if (!slot || !*slot) {
		*error = Text::tr("That expression node no longer exists.");
		return false;
	}
	ExprTree* node = slot->get();
	switch (edit.kind) {
	case MaterialGraphEditKind::SetProperty: {
		if (edit.property == QStringLiteral("text")) {
			std::unique_ptr<ExprTree> replacement = parseTree(edit.value, error);
			if (!replacement) {
				return false;
			}
			*slot = std::move(replacement);
		} else if (edit.property == QStringLiteral("value")) {
			bool ok = false;
			const double value = edit.value.toDouble(&ok);
			if (!ok) {
				*error = Text::tr("'%1' is not a number.").arg(edit.value);
				return false;
			}
			node->op = MaterialExpressionOp::Constant;
			node->value = value;
			node->a.reset();
			node->b.reset();
		} else if (edit.property == QStringLiteral("index")) {
			bool ok = false;
			const int value = edit.value.toInt(&ok);
			const int limit = node->op == MaterialExpressionOp::Global ? 7 : 11;
			if (!ok || value < 0 || value > limit) {
				*error = Text::tr("The register number must be 0 to %1.").arg(limit);
				return false;
			}
			node->index = value;
		} else if (edit.property == QStringLiteral("table")) {
			const QString name = edit.value.trimmed();
			if (name.isEmpty() || name.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")))) {
				*error = Text::tr("A table name holds letters, digits and underscores.");
				return false;
			}
			node->name = name;
		} else if (edit.property == QStringLiteral("op")) {
			MaterialExpressionOp op = MaterialExpressionOp::Constant;
			if (!expressionOpFromId(edit.value, &op)) {
				*error = Text::tr("Unknown operation '%1'.").arg(edit.value);
				return false;
			}
			const bool wasBinary = isBinary(node->op);
			const bool binary = isBinary(op);
			if (binary && !wasBinary) {
				// The old term becomes the first operand.
				auto operand = std::make_unique<ExprTree>();
				operand->op = node->op;
				operand->value = node->value;
				operand->index = node->index;
				operand->name = node->name;
				operand->a = std::move(node->a);
				node->a = std::move(operand);
				node->b = std::make_unique<ExprTree>();
				node->b->value = op == MaterialExpressionOp::Multiply || op == MaterialExpressionOp::Divide ? 1.0 : 0.0;
			} else if (!binary && wasBinary) {
				node->a.reset();
				node->b.reset();
			}
			if (op == MaterialExpressionOp::Table) {
				if (!node->a) {
					node->a = std::make_unique<ExprTree>();
					node->a->op = MaterialExpressionOp::Time;
				}
				if (node->name.isEmpty()) {
					node->name = QStringLiteral("sinTable");
				}
			} else if (!binary) {
				node->a.reset();
			}
			node->op = op;
		} else {
			*error = Text::tr("Unknown expression property '%1'.").arg(edit.property);
			return false;
		}
		break;
	}
	case MaterialGraphEditKind::WrapExpression: {
		MaterialExpressionOp op = MaterialExpressionOp::Multiply;
		if (!expressionOpFromId(edit.value.isEmpty() ? QStringLiteral("multiply") : edit.value, &op)) {
			*error = Text::tr("Unknown operation '%1'.").arg(edit.value);
			return false;
		}
		auto wrapper = std::make_unique<ExprTree>();
		wrapper->op = op;
		wrapper->a = std::move(*slot);
		if (op == MaterialExpressionOp::Table) {
			wrapper->name = QStringLiteral("sinTable");
		} else if (isBinary(op)) {
			wrapper->b = std::make_unique<ExprTree>();
			wrapper->b->value = op == MaterialExpressionOp::Multiply || op == MaterialExpressionOp::Divide ? 1.0 : 0.0;
		} else {
			*error = Text::tr("Only an operator or a table lookup can wrap a node.");
			return false;
		}
		*slot = std::move(wrapper);
		break;
	}
	case MaterialGraphEditKind::UnwrapExpression:
	case MaterialGraphEditKind::RemoveNode: {
		if (node->a) {
			std::unique_ptr<ExprTree> first = std::move(node->a);
			*slot = std::move(first);
		} else if (address.path.isEmpty()) {
			*error = Text::tr("Remove the input's owner instead: an input needs an expression.");
			return false;
		} else {
			auto zero = std::make_unique<ExprTree>();
			zero->value = 0.0;
			*slot = std::move(zero);
		}
		break;
	}
	default:
		*error = Text::tr("Expression nodes cannot do that.");
		return false;
	}
	return writeSlot(definition, address.owner, address.slot, treeText(*root), edits, error);
}

// ---------------------------------------------------------------------------
// Doom 3 image programs
// ---------------------------------------------------------------------------

struct ProgramTree {
	QString function;
	QString path;
	QVector<double> numbers;
	std::vector<std::unique_ptr<ProgramTree>> children;
};

std::unique_ptr<ProgramTree> programFrom(const QVector<MaterialImageProgramNode>& nodes, int index, int depth = 0)
{
	auto tree = std::make_unique<ProgramTree>();
	if (index < 0 || index >= nodes.size() || depth > 32) {
		tree->path = QStringLiteral("_default");
		return tree;
	}
	const MaterialImageProgramNode& node = nodes.at(index);
	tree->function = node.function;
	tree->path = node.path;
	tree->numbers = node.numbers;
	for (int child : node.children) {
		tree->children.push_back(programFrom(nodes, child, depth + 1));
	}
	return tree;
}

QString programText(const ProgramTree& tree)
{
	if (tree.function.isEmpty()) {
		return tree.path;
	}
	QStringList arguments;
	for (const auto& child : tree.children) {
		arguments << programText(*child);
	}
	for (double number : tree.numbers) {
		arguments << numberText(number);
	}
	return QStringLiteral("%1( %2 )").arg(tree.function, arguments.join(QStringLiteral(", ")));
}

int programChildren(const QString& function)
{
	return function == QStringLiteral("addnormals") || function == QStringLiteral("add") ? 2 : 1;
}

int programNumbers(const QString& function)
{
	return function == QStringLiteral("heightmap") ? 1 : function == QStringLiteral("scale") ? 4 : 0;
}

bool programEdit(const MaterialDefinition& definition, const MaterialGraph& graph, const MaterialGraphEdit& edit, QVector<MaterialEdit>* edits,
	QString* error)
{
	const MaterialGraphNode* node = graph.node(edit.node);
	const int stageIndex = node ? node->stage : -1;
	if (!node || stageIndex < 0 || stageIndex >= definition.stages.size()) {
		*error = Text::tr("That image no longer exists.");
		return false;
	}
	const MaterialStage& stage = definition.stages.at(stageIndex);
	std::unique_ptr<ProgramTree> root = programFrom(definition.imagePrograms, stage.imageProgram);
	const QString base = QStringLiteral("stage/%1/image").arg(stageIndex);
	const QStringList path = edit.node.mid(base.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
	std::unique_ptr<ProgramTree>* slot = &root;
	for (const QString& step : path) {
		const int child = step.toInt();
		if (!*slot || child < 0 || child >= static_cast<int>((*slot)->children.size())) {
			*error = Text::tr("That image no longer exists.");
			return false;
		}
		slot = &(*slot)->children[static_cast<size_t>(child)];
	}
	ProgramTree* target = slot->get();
	if (edit.kind == MaterialGraphEditKind::SetProperty) {
		if (edit.property == QStringLiteral("path")) {
			if (edit.value.trimmed().isEmpty() || edit.value.contains(QRegularExpression(QStringLiteral("\\s")))) {
				*error = Text::tr("An image path cannot be empty or contain spaces.");
				return false;
			}
			target->function.clear();
			target->children.clear();
			target->numbers.clear();
			target->path = edit.value.trimmed();
		} else if (edit.property == QStringLiteral("function")) {
			const QString function = edit.value.trimmed().toLower();
			static const QStringList functions {QStringLiteral("heightmap"), QStringLiteral("addnormals"), QStringLiteral("smoothnormals"),
				QStringLiteral("add"), QStringLiteral("scale"), QStringLiteral("invertalpha"), QStringLiteral("invertcolor"),
				QStringLiteral("makeintensity"), QStringLiteral("makealpha")};
			if (!functions.contains(function)) {
				*error = Text::tr("Unknown image function '%1'.").arg(edit.value);
				return false;
			}
			target->function = function;
			while (static_cast<int>(target->children.size()) < programChildren(function)) {
				auto child = std::make_unique<ProgramTree>();
				child->path = QStringLiteral("_flat");
				target->children.push_back(std::move(child));
			}
			while (static_cast<int>(target->children.size()) > programChildren(function)) {
				target->children.pop_back();
			}
			target->numbers.resize(programNumbers(function));
			for (double& number : target->numbers) {
				if (number == 0.0) {
					number = function == QStringLiteral("heightmap") ? 4.0 : 1.0;
				}
			}
		} else {
			const QStringList numbers = target->function == QStringLiteral("scale")
				? QStringList {QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("b"), QStringLiteral("a")}
				: QStringList {QStringLiteral("scale")};
			const int at = static_cast<int>(numbers.indexOf(edit.property));
			bool ok = false;
			const double value = edit.value.toDouble(&ok);
			if (at < 0 || at >= target->numbers.size() || !ok) {
				*error = Text::tr("Unknown or invalid image property '%1'.").arg(edit.property);
				return false;
			}
			target->numbers[at] = value;
		}
	} else if (edit.kind == MaterialGraphEditKind::WrapExpression) {
		const QString function = edit.value.trimmed().toLower();
		auto wrapper = std::make_unique<ProgramTree>();
		wrapper->function = function.isEmpty() ? QStringLiteral("makeintensity") : function;
		wrapper->children.push_back(std::move(*slot));
		while (static_cast<int>(wrapper->children.size()) < programChildren(wrapper->function)) {
			auto child = std::make_unique<ProgramTree>();
			child->path = QStringLiteral("_flat");
			wrapper->children.push_back(std::move(child));
		}
		wrapper->numbers.resize(programNumbers(wrapper->function));
		for (double& number : wrapper->numbers) {
			number = wrapper->function == QStringLiteral("heightmap") ? 4.0 : 1.0;
		}
		*slot = std::move(wrapper);
	} else if (edit.kind == MaterialGraphEditKind::RemoveNode || edit.kind == MaterialGraphEditKind::UnwrapExpression) {
		if (target->children.empty()) {
			*error = Text::tr("A plain image cannot be unwrapped; change its path instead.");
			return false;
		}
		std::unique_ptr<ProgramTree> first = std::move(target->children.front());
		*slot = std::move(first);
	}
	const QString text = programText(*root);
	if (stage.shorthandDirective >= 0) {
		const MaterialDirective& directive = definition.directives.at(stage.shorthandDirective);
		edits->push_back(setEdit(definition.name, -1, stage.shorthandDirective, directive.keyword, text));
	} else {
		const int directive = node->directive;
		if (directive < 0) {
			*error = Text::tr("The stage has no map directive to change.");
			return false;
		}
		edits->push_back(setEdit(definition.name, stageIndex, directive, stage.directives.at(directive).keyword, text));
	}
	return true;
}

// ---------------------------------------------------------------------------
// Property edits per engine
// ---------------------------------------------------------------------------

bool quake3PropertyEdit(const MaterialDefinition& definition, const MaterialGraph& graph, const MaterialGraphEdit& edit, QVector<MaterialEdit>* edits,
	QString* error)
{
	const MaterialGraphNode* found = graph.node(edit.node);
	if (!found) {
		*error = Text::tr("Node %1 no longer exists.").arg(edit.node);
		return false;
	}
	const MaterialGraphNode node = withProperty(*found, edit.property, edit.value);
	const QString& material = definition.name;
	const QString value = edit.value.trimmed();
	if (node.kind == QStringLiteral("output")) {
		const QVector<MaterialDirective>& directives = definition.directives;
		if (edit.property == QStringLiteral("cull")) {
			if (value == QStringLiteral("front")) {
				removeAll(edits, material, -1, directives, {QStringLiteral("cull")});
			} else {
				setOrAdd(edits, material, -1, directives, QStringLiteral("cull"), value);
			}
		} else if (edit.property == QStringLiteral("sort")) {
			if (value.isEmpty()) {
				removeAll(edits, material, -1, directives, {QStringLiteral("sort")});
			} else {
				setOrAdd(edits, material, -1, directives, QStringLiteral("sort"), value);
			}
		} else if (edit.property == QStringLiteral("surfaceparms")) {
			const QStringList wanted = value.toLower().split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
			QVector<int> remove;
			for (int index : findDirectives(directives, {QStringLiteral("surfaceparm")})) {
				if (!wanted.contains(directives.at(index).argumentText.trimmed().toLower())) {
					remove.push_back(index);
				}
			}
			std::sort(remove.begin(), remove.end(), std::greater<int>());
			for (int index : remove) {
				edits->push_back(removeEdit(material, -1, index));
			}
			for (const QString& parm : wanted) {
				if (!definition.surfaceParms.contains(parm)) {
					edits->push_back(addEdit(material, -1, QStringLiteral("surfaceparm"), parm));
				}
			}
		} else if (edit.property == QStringLiteral("editorImage")) {
			if (value.isEmpty()) {
				removeAll(edits, material, -1, directives, {QStringLiteral("qer_editorimage")});
			} else {
				setOrAdd(edits, material, -1, directives, QStringLiteral("qer_editorimage"), value);
			}
		} else if (edit.property == QStringLiteral("polygonOffset") || edit.property == QStringLiteral("nopicmip")
			|| edit.property == QStringLiteral("nomipmaps") || edit.property == QStringLiteral("portal")) {
			setBool(edits, material, -1, directives, edit.property, isTrue(value));
		} else if (edit.property == QStringLiteral("tessSize")) {
			if (value.isEmpty() || value.toDouble() <= 0) {
				removeAll(edits, material, -1, directives, {QStringLiteral("tesssize")});
			} else {
				setOrAdd(edits, material, -1, directives, QStringLiteral("tessSize"), value);
			}
		} else {
			*error = Text::tr("Unknown shader property '%1'.").arg(edit.property);
			return false;
		}
		return true;
	}
	if (node.kind == QStringLiteral("sky")) {
		setOrAdd(edits, material, -1, definition.directives, QStringLiteral("skyParms"),
			QStringLiteral("%1 %2 %3").arg(propertyValue(node, QStringLiteral("farBox"), QStringLiteral("-")),
				propertyValue(node, QStringLiteral("cloudHeight"), QStringLiteral("512")), propertyValue(node, QStringLiteral("nearBox"), QStringLiteral("-"))));
		return true;
	}
	if (node.kind == QStringLiteral("fog")) {
		setOrAdd(edits, material, -1, definition.directives, QStringLiteral("fogParms"),
			QStringLiteral("( %1 ) %2").arg(propertyValue(node, QStringLiteral("color"), QStringLiteral("0.5 0.5 0.5")),
				propertyValue(node, QStringLiteral("distance"), QStringLiteral("512"))));
		return true;
	}
	if (node.kind == QStringLiteral("deform")) {
		const QString type = propertyValue(node, QStringLiteral("type"), QStringLiteral("wave"));
		QString arguments = type;
		if (type == QStringLiteral("wave")) {
			arguments += QLatin1Char(' ') + propertyValue(node, QStringLiteral("spread"), QStringLiteral("100")) + QLatin1Char(' ') + waveText(waveFrom(node));
		} else if (type == QStringLiteral("normal")) {
			arguments += QStringLiteral(" %1 %2").arg(propertyValue(node, QStringLiteral("amplitude"), QStringLiteral("0.1")),
				propertyValue(node, QStringLiteral("frequency"), QStringLiteral("1")));
		} else if (type == QStringLiteral("bulge")) {
			arguments += QStringLiteral(" %1 %2 %3").arg(propertyValue(node, QStringLiteral("width"), QStringLiteral("3")),
				propertyValue(node, QStringLiteral("height"), QStringLiteral("3")), propertyValue(node, QStringLiteral("speed"), QStringLiteral("1")));
		} else if (type == QStringLiteral("move")) {
			arguments += QLatin1Char(' ') + propertyValue(node, QStringLiteral("vector"), QStringLiteral("0 0 1")) + QLatin1Char(' ') + waveText(waveFrom(node));
		}
		edits->push_back(setEdit(material, -1, node.directive, QStringLiteral("deformVertexes"), arguments));
		return true;
	}
	const int stageIndex = node.stage;
	if (stageIndex < 0 || stageIndex >= definition.stages.size()) {
		*error = Text::tr("That stage no longer exists.");
		return false;
	}
	const MaterialStage& stage = definition.stages.at(stageIndex);
	const QVector<MaterialDirective>& directives = stage.directives;
	if (node.kind == QStringLiteral("stage")) {
		if (edit.property == QStringLiteral("blend") || edit.property == QStringLiteral("source") || edit.property == QStringLiteral("destination")) {
			const QString blend = propertyValue(node, QStringLiteral("blend"));
			if (edit.property == QStringLiteral("blend") && blend == QStringLiteral("opaque")) {
				removeAll(edits, material, stageIndex, directives, {QStringLiteral("blendfunc")});
			} else if (edit.property == QStringLiteral("blend") && blend != QStringLiteral("custom")) {
				setOrAdd(edits, material, stageIndex, directives, QStringLiteral("blendFunc"), blend);
			} else {
				setOrAdd(edits, material, stageIndex, directives, QStringLiteral("blendFunc"),
					QStringLiteral("%1 %2").arg(propertyValue(node, QStringLiteral("source")), propertyValue(node, QStringLiteral("destination"))));
			}
		} else if (edit.property == QStringLiteral("alphaFunc")) {
			if (value == QStringLiteral("none")) {
				removeAll(edits, material, stageIndex, directives, {QStringLiteral("alphafunc")});
			} else {
				setOrAdd(edits, material, stageIndex, directives, QStringLiteral("alphaFunc"), value);
			}
		} else if (edit.property == QStringLiteral("depthFunc")) {
			if (value == QStringLiteral("lequal")) {
				removeAll(edits, material, stageIndex, directives, {QStringLiteral("depthfunc")});
			} else {
				setOrAdd(edits, material, stageIndex, directives, QStringLiteral("depthFunc"), value);
			}
		} else if (edit.property == QStringLiteral("depthWrite") || edit.property == QStringLiteral("detail")) {
			setBool(edits, material, stageIndex, directives, edit.property, isTrue(value));
		} else {
			*error = Text::tr("Unknown stage property '%1'.").arg(edit.property);
			return false;
		}
		return true;
	}
	if (node.kind == QStringLiteral("image") || node.kind == QStringLiteral("lightmap")) {
		const QString source = propertyValue(node, QStringLiteral("source"), QStringLiteral("image"));
		const QString path = source == QStringLiteral("image") ? propertyValue(node, QStringLiteral("path")) : source;
		if (path.isEmpty() || path.contains(QRegularExpression(QStringLiteral("\\s")))) {
			*error = Text::tr("An image path cannot be empty or contain spaces.");
			return false;
		}
		const bool clamp = source == QStringLiteral("image") && isTrue(propertyValue(node, QStringLiteral("clamp")));
		const QString keyword = clamp ? QStringLiteral("clampmap") : QStringLiteral("map");
		if (node.directive >= 0) {
			edits->push_back(setEdit(material, stageIndex, node.directive, keyword, path));
		} else {
			edits->push_back(addEdit(material, stageIndex, keyword, path, 0));
		}
		return true;
	}
	if (node.kind == QStringLiteral("animmap")) {
		const QString frames = propertyValue(node, QStringLiteral("frames")).simplified();
		if (frames.isEmpty()) {
			*error = Text::tr("animMap needs at least one frame.");
			return false;
		}
		edits->push_back(setEdit(material, stageIndex, node.directive, QStringLiteral("animMap"),
			propertyValue(node, QStringLiteral("frequency"), QStringLiteral("1")) + QLatin1Char(' ') + frames));
		return true;
	}
	if (node.kind == QStringLiteral("video")) {
		edits->push_back(setEdit(material, stageIndex, node.directive, QStringLiteral("videoMap"), value));
		return true;
	}
	if (node.kind == QStringLiteral("tcgen")) {
		const QString source = propertyValue(node, QStringLiteral("source"));
		if (source == QStringLiteral("base")) {
			removeAll(edits, material, stageIndex, directives, {QStringLiteral("tcgen"), QStringLiteral("texgen")});
			return true;
		}
		QString arguments = source;
		if (source == QStringLiteral("vector")) {
			arguments += QStringLiteral(" ( %1 ) ( %2 )").arg(propertyValue(node, QStringLiteral("s"), QStringLiteral("1 0 0")),
				propertyValue(node, QStringLiteral("t"), QStringLiteral("0 1 0")));
		}
		if (node.directive >= 0) {
			edits->push_back(setEdit(material, stageIndex, node.directive, directives.at(node.directive).keyword, arguments));
		} else {
			edits->push_back(addEdit(material, stageIndex, QStringLiteral("tcGen"), arguments));
		}
		return true;
	}
	if (node.kind == QStringLiteral("tcmod")) {
		const QString type = propertyValue(node, QStringLiteral("type"));
		QString arguments = type;
		if (type == QStringLiteral("scroll") || type == QStringLiteral("scale")) {
			arguments += QStringLiteral(" %1 %2").arg(propertyValue(node, QStringLiteral("s"), type == QStringLiteral("scale") ? QStringLiteral("1") : QStringLiteral("0")),
				propertyValue(node, QStringLiteral("t"), type == QStringLiteral("scale") ? QStringLiteral("1") : QStringLiteral("0")));
		} else if (type == QStringLiteral("rotate")) {
			arguments += QLatin1Char(' ') + propertyValue(node, QStringLiteral("speed"), QStringLiteral("30"));
		} else if (type == QStringLiteral("stretch")) {
			arguments += QLatin1Char(' ') + waveText(waveFrom(node));
		} else if (type == QStringLiteral("turb")) {
			arguments += QStringLiteral(" %1 %2 %3 %4").arg(propertyValue(node, QStringLiteral("base"), QStringLiteral("0")),
				propertyValue(node, QStringLiteral("amplitude"), QStringLiteral("0.1")), propertyValue(node, QStringLiteral("phase"), QStringLiteral("0")),
				propertyValue(node, QStringLiteral("frequency"), QStringLiteral("0.5")));
		} else if (type == QStringLiteral("transform")) {
			for (const char* id : {"m00", "m01", "m10", "m11", "t0", "t1"}) {
				const bool diagonal = QByteArray(id) == "m00" || QByteArray(id) == "m11";
				arguments += QLatin1Char(' ') + propertyValue(node, QString::fromLatin1(id), diagonal ? QStringLiteral("1") : QStringLiteral("0"));
			}
		}
		edits->push_back(setEdit(material, stageIndex, node.directive, QStringLiteral("tcMod"), arguments));
		return true;
	}
	if (node.kind == QStringLiteral("rgbgen") || node.kind == QStringLiteral("alphagen")) {
		const bool alpha = node.kind == QStringLiteral("alphagen");
		const QString source = propertyValue(node, QStringLiteral("source"));
		QString arguments = source;
		if (source == QStringLiteral("wave")) {
			MaterialWave wave = waveFrom(node);
			if (wave.amplitude == 0.0 && wave.frequency == 0.0 && edit.property == QStringLiteral("source")) {
				wave.base = 0.5;
				wave.amplitude = 0.5;
				wave.frequency = 1.0;
			}
			arguments += QLatin1Char(' ') + waveText(wave);
		} else if (source == QStringLiteral("const")) {
			const QString constant = propertyValue(node, QStringLiteral("value"));
			const bool fresh = constant.isEmpty() || (edit.property == QStringLiteral("source") && !constant.contains(QLatin1Char(' ')));
			arguments += alpha ? QLatin1Char(' ') + (constant.isEmpty() ? QStringLiteral("1") : constant)
							   : QStringLiteral(" ( %1 )").arg(fresh ? QStringLiteral("1 1 1") : constant);
		} else if (source == QStringLiteral("portal")) {
			arguments += QLatin1Char(' ') + propertyValue(node, QStringLiteral("value"), QStringLiteral("256"));
		}
		const QString keyword = alpha ? QStringLiteral("alphaGen") : QStringLiteral("rgbGen");
		if (node.directive >= 0) {
			edits->push_back(setEdit(material, stageIndex, node.directive, directives.at(node.directive).keyword, arguments));
		} else {
			edits->push_back(addEdit(material, stageIndex, keyword, arguments));
		}
		return true;
	}
	*error = Text::tr("%1 nodes have no editable properties.").arg(node.kind);
	return false;
}

bool doom3PropertyEdit(const MaterialDefinition& definition, const MaterialGraph& graph, const MaterialGraphEdit& edit, QVector<MaterialEdit>* edits,
	QString* error)
{
	if (edit.node.contains(QStringLiteral("/e"))) {
		return expressionEdit(definition, edit, edits, error);
	}
	const MaterialGraphNode* found = graph.node(edit.node);
	if (!found) {
		*error = Text::tr("Node %1 no longer exists.").arg(edit.node);
		return false;
	}
	const MaterialGraphNode node = withProperty(*found, edit.property, edit.value);
	const QString& material = definition.name;
	const QString value = edit.value.trimmed();
	if (node.kind == QStringLiteral("image") || node.kind == QStringLiteral("program")) {
		if (node.stage >= 0 && node.stage < definition.stages.size() && definition.stages.at(node.stage).imageProgram >= 0) {
			return programEdit(definition, graph, edit, edits, error);
		}
	}
	if (node.kind == QStringLiteral("output")) {
		const QVector<MaterialDirective>& directives = definition.directives;
		static const QStringList flags {QStringLiteral("translucent"), QStringLiteral("twosided"), QStringLiteral("noshadows"),
			QStringLiteral("noselfshadow"), QStringLiteral("forceopaque")};
		if (flags.contains(edit.property.toLower())) {
			setBool(edits, material, -1, directives, edit.property, isTrue(value));
		} else if (edit.property == QStringLiteral("decal")) {
			setBool(edits, material, -1, directives, QStringLiteral("DECAL_MACRO"), isTrue(value));
		} else if (edit.property == QStringLiteral("sort")) {
			if (value.isEmpty()) {
				removeAll(edits, material, -1, directives, {QStringLiteral("sort")});
			} else {
				setOrAdd(edits, material, -1, directives, QStringLiteral("sort"), value);
			}
		} else if (edit.property == QStringLiteral("surfaceType")) {
			static const QStringList types {QStringLiteral("metal"), QStringLiteral("stone"), QStringLiteral("flesh"), QStringLiteral("wood"),
				QStringLiteral("cardboard"), QStringLiteral("liquid"), QStringLiteral("glass"), QStringLiteral("plastic"), QStringLiteral("ricochet")};
			removeAll(edits, material, -1, directives, types);
			if (types.contains(value)) {
				edits->push_back(addEdit(material, -1, value, QString()));
			}
		} else if (edit.property == QStringLiteral("editorImage")) {
			if (value.isEmpty()) {
				removeAll(edits, material, -1, directives, {QStringLiteral("qer_editorimage")});
			} else {
				setOrAdd(edits, material, -1, directives, QStringLiteral("qer_editorimage"), value);
			}
		} else if (edit.property == QStringLiteral("description")) {
			if (value.isEmpty()) {
				removeAll(edits, material, -1, directives, {QStringLiteral("description")});
			} else {
				QString quoted = value;
				quoted.remove(QLatin1Char('"'));
				setOrAdd(edits, material, -1, directives, QStringLiteral("description"), QStringLiteral("\"%1\"").arg(quoted));
			}
		} else if (edit.property == QStringLiteral("lightFalloffImage")) {
			if (value.isEmpty()) {
				removeAll(edits, material, -1, directives, {QStringLiteral("lightfalloffimage")});
			} else {
				setOrAdd(edits, material, -1, directives, QStringLiteral("lightFalloffImage"), value);
			}
		} else if (edit.property == QStringLiteral("lightKind")) {
			removeAll(edits, material, -1, directives, {QStringLiteral("foglight"), QStringLiteral("blendlight"), QStringLiteral("ambientlight")});
			if (value != QStringLiteral("normal")) {
				edits->push_back(addEdit(material, -1, value, QString()));
			}
		} else {
			*error = Text::tr("Unknown material property '%1'.").arg(edit.property);
			return false;
		}
		return true;
	}
	if (node.kind == QStringLiteral("deform")) {
		edits->push_back(setEdit(material, -1, node.directive, QStringLiteral("deform"), value));
		return true;
	}
	const int stageIndex = node.stage;
	if (stageIndex < 0 || stageIndex >= definition.stages.size()) {
		*error = Text::tr("That stage no longer exists.");
		return false;
	}
	const MaterialStage& stage = definition.stages.at(stageIndex);
	const QVector<MaterialDirective>& directives = stage.directives;
	if (node.kind == QStringLiteral("stage")) {
		if (stage.shorthandDirective >= 0) {
			*error = Text::tr("Expand the shorthand into a stage block first.");
			return false;
		}
		if (edit.property == QStringLiteral("blend") || edit.property == QStringLiteral("source") || edit.property == QStringLiteral("destination")) {
			const QString blend = propertyValue(node, QStringLiteral("blend"));
			if (edit.property == QStringLiteral("blend") && blend == QStringLiteral("replace")) {
				removeAll(edits, material, stageIndex, directives, {QStringLiteral("blend")});
			} else if (edit.property == QStringLiteral("blend") && blend != QStringLiteral("custom")) {
				setOrAdd(edits, material, stageIndex, directives, QStringLiteral("blend"), blend);
			} else {
				setOrAdd(edits, material, stageIndex, directives, QStringLiteral("blend"),
					QStringLiteral("%1, %2").arg(propertyValue(node, QStringLiteral("source")), propertyValue(node, QStringLiteral("destination"))));
			}
		} else if (edit.property == QStringLiteral("wrap")) {
			removeAll(edits, material, stageIndex, directives,
				{QStringLiteral("clamp"), QStringLiteral("zeroclamp"), QStringLiteral("alphazeroclamp"), QStringLiteral("noclamp")});
			if (value != QStringLiteral("repeat")) {
				edits->push_back(addEdit(material, stageIndex, value, QString()));
			}
		} else if (edit.property == QStringLiteral("filter")) {
			removeAll(edits, material, stageIndex, directives, {QStringLiteral("nearest"), QStringLiteral("linear")});
			if (value != QStringLiteral("default")) {
				edits->push_back(addEdit(material, stageIndex, value, QString()));
			}
		} else if (edit.property == QStringLiteral("vertexColor")) {
			removeAll(edits, material, stageIndex, directives, {QStringLiteral("vertexcolor"), QStringLiteral("inversevertexcolor")});
			if (value != QStringLiteral("none")) {
				edits->push_back(addEdit(material, stageIndex, value, QString()));
			}
		} else if (edit.property == QStringLiteral("ignoreAlphaTest") || edit.property == QStringLiteral("maskAlpha")
			|| edit.property == QStringLiteral("maskDepth") || edit.property == QStringLiteral("maskColor")) {
			setBool(edits, material, stageIndex, directives, edit.property, isTrue(value));
		} else {
			*error = Text::tr("Unknown stage property '%1'.").arg(edit.property);
			return false;
		}
		return true;
	}
	if (node.kind == QStringLiteral("image") || node.kind == QStringLiteral("cube") || node.kind == QStringLiteral("video")) {
		if (value.isEmpty() || value.contains(QRegularExpression(QStringLiteral("\\s")))) {
			*error = Text::tr("An image path cannot be empty or contain spaces.");
			return false;
		}
		if (node.directive < 0) {
			edits->push_back(addEdit(material, stageIndex, QStringLiteral("map"), value, 0));
		} else {
			edits->push_back(setEdit(material, stageIndex, node.directive, directives.at(node.directive).keyword, value));
		}
		return true;
	}
	if (node.kind == QStringLiteral("texgen")) {
		removeAll(edits, material, stageIndex, directives, {QStringLiteral("texgen"), QStringLiteral("screen"), QStringLiteral("screen2"), QStringLiteral("glasswarp")});
		if (value == QStringLiteral("screen") || value == QStringLiteral("screen2") || value == QStringLiteral("glassWarp")) {
			edits->push_back(addEdit(material, stageIndex, value, QString()));
		} else if (value == QStringLiteral("wobbleSky")) {
			edits->push_back(addEdit(material, stageIndex, QStringLiteral("texGen"), QStringLiteral("wobbleSky 0 0 0")));
		} else if (value != QStringLiteral("base")) {
			edits->push_back(addEdit(material, stageIndex, QStringLiteral("texGen"), value));
		}
		return true;
	}
	if (node.kind == QStringLiteral("transform")) {
		const QString type = propertyValue(node, QStringLiteral("type"));
		QString a = propertyValue(node, QStringLiteral("a"), QStringLiteral("0"));
		QString b = propertyValue(node, QStringLiteral("b"), QStringLiteral("0"));
		QVector<MaterialExpressionNode> check;
		if (parseMaterialExpression(a, &check, error) < 0 || (type != QStringLiteral("rotate") && parseMaterialExpression(b, &check, error) < 0)) {
			return false;
		}
		edits->push_back(setEdit(material, stageIndex, node.directive, type, type == QStringLiteral("rotate") ? a : QStringLiteral("%1, %2").arg(a, b)));
		return true;
	}
	if (node.kind == QStringLiteral("color")) {
		return writeSlot(definition, node.id, edit.property, value, edits, error);
	}
	if (node.kind == QStringLiteral("condition") || node.kind == QStringLiteral("alphatest")) {
		return writeSlot(definition, node.id, QStringLiteral("value"), value, edits, error);
	}
	*error = Text::tr("%1 nodes have no editable properties.").arg(node.kind);
	return false;
}

// ---------------------------------------------------------------------------
// Adding and removing
// ---------------------------------------------------------------------------

QString defaultImage(const MaterialDefinition& definition)
{
	if (!definition.editorImage.isEmpty()) {
		return definition.editorImage;
	}
	for (const MaterialStage& stage : definition.stages) {
		if (stage.imageKind == MaterialImageKind::File && !stage.imagePath.isEmpty()) {
			return stage.imagePath;
		}
	}
	return definition.engine == MaterialEngine::Doom3 ? QStringLiteral("_white") : QStringLiteral("$whiteimage");
}

bool addNodeEdits(const MaterialDefinition& definition, const MaterialGraphEdit& edit, QVector<MaterialEdit>* edits, QString* focus, QString* error)
{
	const QString& material = definition.name;
	const QString id = edit.nodeTemplate;
	const int stageIndex = stageOf(edit.node);
	const auto needStage = [&]() {
		if (stageIndex < 0 || stageIndex >= definition.stages.size()) {
			*error = Text::tr("Choose a stage first.");
			return false;
		}
		return true;
	};
	const auto addStage = [&](const QString& body) {
		MaterialEdit add;
		add.kind = MaterialEditKind::AddStage;
		add.material = material;
		add.text = body;
		edits->push_back(add);
		*focus = QStringLiteral("stage/%1").arg(definition.stages.size());
		return true;
	};
	const QString image = defaultImage(definition);
	if (id == QStringLiteral("q3.stage.image")) {
		return addStage(QStringLiteral("map %1\nrgbGen identity").arg(image));
	}
	if (id == QStringLiteral("q3.stage.lightmap")) {
		return addStage(QStringLiteral("map $lightmap\nrgbGen identity"));
	}
	if (id == QStringLiteral("q3.stage.add")) {
		return addStage(QStringLiteral("map %1\nblendFunc add").arg(image));
	}
	if (id == QStringLiteral("q3.stage.filter")) {
		return addStage(QStringLiteral("map %1\nblendFunc filter\nrgbGen identity").arg(image));
	}
	const auto stageDirective = [&](const QString& keyword, const QString& arguments, bool replace) {
		if (!needStage()) {
			return false;
		}
		const QVector<MaterialDirective>& directives = definition.stages.at(stageIndex).directives;
		if (replace) {
			setOrAdd(edits, material, stageIndex, directives, keyword, arguments);
		} else {
			edits->push_back(addEdit(material, stageIndex, keyword, arguments));
		}
		*focus = QStringLiteral("stage/%1").arg(stageIndex);
		return true;
	};
	if (id == QStringLiteral("q3.tcmod.scroll")) {
		return stageDirective(QStringLiteral("tcMod"), QStringLiteral("scroll 0.1 0"), false);
	}
	if (id == QStringLiteral("q3.tcmod.scale")) {
		return stageDirective(QStringLiteral("tcMod"), QStringLiteral("scale 2 2"), false);
	}
	if (id == QStringLiteral("q3.tcmod.rotate")) {
		return stageDirective(QStringLiteral("tcMod"), QStringLiteral("rotate 30"), false);
	}
	if (id == QStringLiteral("q3.tcmod.turb")) {
		return stageDirective(QStringLiteral("tcMod"), QStringLiteral("turb 0 0.1 0 0.5"), false);
	}
	if (id == QStringLiteral("q3.tcmod.stretch")) {
		return stageDirective(QStringLiteral("tcMod"), QStringLiteral("stretch sin 1 0.1 0 0.5"), false);
	}
	if (id == QStringLiteral("q3.tcgen.environment")) {
		return stageDirective(QStringLiteral("tcGen"), QStringLiteral("environment"), true);
	}
	if (id == QStringLiteral("q3.rgbgen.wave")) {
		return stageDirective(QStringLiteral("rgbGen"), QStringLiteral("wave sin 0.5 0.5 0 1"), true);
	}
	if (id == QStringLiteral("q3.rgbgen.const")) {
		return stageDirective(QStringLiteral("rgbGen"), QStringLiteral("const ( 1 0.5 0.25 )"), true);
	}
	if (id == QStringLiteral("q3.alphagen.const")) {
		return stageDirective(QStringLiteral("alphaGen"), QStringLiteral("const 0.5"), true);
	}
	if (id == QStringLiteral("q3.alphafunc")) {
		return stageDirective(QStringLiteral("alphaFunc"), QStringLiteral("GE128"), true);
	}
	const auto global = [&](const QString& keyword, const QString& arguments, bool replace) {
		if (replace) {
			setOrAdd(edits, material, -1, definition.directives, keyword, arguments);
		} else {
			edits->push_back(addEdit(material, -1, keyword, arguments));
		}
		*focus = QStringLiteral("material");
		return true;
	};
	if (id == QStringLiteral("q3.deform.wave")) {
		return global(QStringLiteral("deformVertexes"), QStringLiteral("wave 64 sin 0 2 0 0.5"), false);
	}
	if (id == QStringLiteral("q3.deform.bulge")) {
		return global(QStringLiteral("deformVertexes"), QStringLiteral("bulge 3 3 1"), false);
	}
	if (id == QStringLiteral("q3.deform.move")) {
		return global(QStringLiteral("deformVertexes"), QStringLiteral("move 0 0 2 sin 0 1 0 0.5"), false);
	}
	if (id == QStringLiteral("q3.deform.autosprite")) {
		return global(QStringLiteral("deformVertexes"), QStringLiteral("autosprite"), false);
	}
	if (id == QStringLiteral("q3.surfaceparm")) {
		return global(QStringLiteral("surfaceparm"), QStringLiteral("nolightmap"), false);
	}
	if (id == QStringLiteral("q3.sky")) {
		return global(QStringLiteral("skyParms"), QStringLiteral("env/sky 512 -"), true);
	}
	if (id == QStringLiteral("q3.fog")) {
		global(QStringLiteral("surfaceparm"), QStringLiteral("fog"), false);
		return global(QStringLiteral("fogParms"), QStringLiteral("( 0.5 0.5 0.5 ) 512"), true);
	}
	// Doom 3.
	const QString d3Image = image == QStringLiteral("$whiteimage") ? QStringLiteral("_white") : image;
	if (id == QStringLiteral("d3.stage.image")) {
		return addStage(QStringLiteral("blend blend\nmap %1").arg(d3Image));
	}
	if (id == QStringLiteral("d3.stage.add")) {
		return addStage(QStringLiteral("blend add\nmap %1").arg(d3Image));
	}
	if (id == QStringLiteral("d3.stage.diffuse")) {
		return addStage(QStringLiteral("blend diffusemap\nmap %1").arg(d3Image));
	}
	if (id == QStringLiteral("d3.stage.bump")) {
		return addStage(QStringLiteral("blend bumpmap\nmap _flat"));
	}
	if (id == QStringLiteral("d3.stage.specular")) {
		return addStage(QStringLiteral("blend specularmap\nmap _white"));
	}
	if (id == QStringLiteral("d3.transform.scroll")) {
		return stageDirective(QStringLiteral("scroll"), QStringLiteral("time * 0.1, 0"), false);
	}
	if (id == QStringLiteral("d3.transform.scale")) {
		return stageDirective(QStringLiteral("scale"), QStringLiteral("2, 2"), false);
	}
	if (id == QStringLiteral("d3.transform.centerscale")) {
		return stageDirective(QStringLiteral("centerScale"), QStringLiteral("1, 1"), false);
	}
	if (id == QStringLiteral("d3.transform.rotate")) {
		return stageDirective(QStringLiteral("rotate"), QStringLiteral("time * 0.1"), false);
	}
	if (id == QStringLiteral("d3.transform.shear")) {
		return stageDirective(QStringLiteral("shear"), QStringLiteral("0.1, 0"), false);
	}
	if (id == QStringLiteral("d3.texgen.reflect")) {
		return stageDirective(QStringLiteral("texGen"), QStringLiteral("reflect"), true);
	}
	if (id == QStringLiteral("d3.color.rgb") || id == QStringLiteral("d3.color.pulse")) {
		if (!needStage()) {
			return false;
		}
		*focus = QStringLiteral("stage/%1/color").arg(stageIndex);
		return writeSlot(definition, QStringLiteral("stage/%1/color").arg(stageIndex), QStringLiteral("rgb"),
			id == QStringLiteral("d3.color.rgb") ? QStringLiteral("0.5") : QStringLiteral("0.5 + 0.5 * sinTable[ time ]"), edits, error);
	}
	if (id == QStringLiteral("d3.condition")) {
		return stageDirective(QStringLiteral("if"), QStringLiteral("parm7 == 0"), true);
	}
	if (id == QStringLiteral("d3.alphatest")) {
		return stageDirective(QStringLiteral("alphaTest"), QStringLiteral("0.5"), true);
	}
	if (id == QStringLiteral("d3.deform.turbulent")) {
		return global(QStringLiteral("deform"), QStringLiteral("turbulent sinTable 0.0175 time * 0.15 10"), true);
	}
	if (id == QStringLiteral("d3.deform.expand")) {
		return global(QStringLiteral("deform"), QStringLiteral("expand 0.5"), true);
	}
	if (id == QStringLiteral("d3.deform.sprite")) {
		return global(QStringLiteral("deform"), QStringLiteral("sprite"), true);
	}
	if (id == QStringLiteral("d3.translucent")) {
		return global(QStringLiteral("translucent"), QString(), true);
	}
	if (id == QStringLiteral("d3.table")) {
		MaterialEdit add;
		add.kind = MaterialEditKind::AddDefinition;
		add.text = QStringLiteral("table newTable { { 0, 1, 0.5, 1 } }");
		edits->push_back(add);
		return true;
	}
	*error = Text::tr("Unknown node template '%1'.").arg(id);
	return false;
}

bool removeNodeEdits(const MaterialDefinition& definition, const MaterialGraph& graph, const MaterialGraphEdit& edit, QVector<MaterialEdit>* edits,
	QString* error)
{
	const MaterialGraphNode* node = graph.node(edit.node);
	if (!node) {
		*error = Text::tr("Node %1 no longer exists.").arg(edit.node);
		return false;
	}
	const QString& material = definition.name;
	if (node->kind == QStringLiteral("expression")) {
		return expressionEdit(definition, edit, edits, error);
	}
	if (node->kind == QStringLiteral("program") || (node->kind == QStringLiteral("image") && edit.node.count(QLatin1Char('/')) > 2)) {
		return programEdit(definition, graph, edit, edits, error);
	}
	if (!node->removable) {
		*error = node->kind == QStringLiteral("image") || node->kind == QStringLiteral("animmap")
			? Text::tr("A stage needs an image; remove the stage instead.")
			: Text::tr("%1 cannot be removed.").arg(node->title);
		return false;
	}
	if (node->kind == QStringLiteral("stage")) {
		MaterialEdit remove;
		remove.kind = MaterialEditKind::RemoveStage;
		remove.material = material;
		remove.stage = node->stage;
		edits->push_back(remove);
		return true;
	}
	if (node->kind == QStringLiteral("deform") || node->kind == QStringLiteral("sky") || node->kind == QStringLiteral("fog")) {
		if (node->directive < 0) {
			*error = Text::tr("%1 has no directive to remove.").arg(node->title);
			return false;
		}
		edits->push_back(removeEdit(material, -1, node->directive));
		return true;
	}
	if (node->stage < 0 || node->stage >= definition.stages.size()) {
		*error = Text::tr("That stage no longer exists.");
		return false;
	}
	const MaterialStage& stage = definition.stages.at(node->stage);
	if (node->kind == QStringLiteral("color")) {
		removeAll(edits, material, node->stage, stage.directives,
			{QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("alpha"), QStringLiteral("rgb"),
				QStringLiteral("rgba"), QStringLiteral("color"), QStringLiteral("colored")});
		return true;
	}
	if (node->kind == QStringLiteral("texgen")) {
		removeAll(edits, material, node->stage, stage.directives, {QStringLiteral("texgen"), QStringLiteral("tcgen"), QStringLiteral("screen"),
			QStringLiteral("screen2"), QStringLiteral("glasswarp")});
		return true;
	}
	if (node->directive < 0) {
		*error = Text::tr("%1 has no directive to remove.").arg(node->title);
		return false;
	}
	edits->push_back(removeEdit(material, node->stage, node->directive));
	return true;
}

bool moveNodeEdits(const MaterialDefinition& definition, const MaterialGraph& graph, const MaterialGraphEdit& edit, QVector<MaterialEdit>* edits,
	QString* focus, QString* error)
{
	const MaterialGraphNode* node = graph.node(edit.node);
	if (!node || !node->reorderable) {
		*error = Text::tr("That node cannot move.");
		return false;
	}
	const int step = edit.step < 0 ? -1 : 1;
	if (node->kind == QStringLiteral("stage")) {
		const int target = node->stage + step;
		if (target < 0 || target >= definition.stages.size()) {
			*error = Text::tr("The stage is already at that end.");
			return false;
		}
		MaterialEdit move;
		move.kind = MaterialEditKind::MoveStage;
		move.material = definition.name;
		move.stage = node->stage;
		move.position = target;
		edits->push_back(move);
		*focus = QStringLiteral("stage/%1").arg(target);
		return true;
	}
	// tcMods and transforms move past their neighbour.
	const MaterialStage& stage = definition.stages.at(node->stage);
	const int modIndex = edit.node.section(QLatin1Char('/'), 3, 3).toInt();
	const int targetMod = modIndex + step;
	if (targetMod < 0 || targetMod >= stage.tcMods.size()) {
		*error = Text::tr("It is already at that end.");
		return false;
	}
	MaterialEdit move;
	move.kind = MaterialEditKind::MoveDirective;
	move.material = definition.name;
	move.stage = node->stage;
	move.directive = stage.tcMods.at(modIndex).directive;
	move.position = stage.tcMods.at(targetMod).directive;
	edits->push_back(move);
	*focus = edit.node.section(QLatin1Char('/'), 0, 2) + QStringLiteral("/%1").arg(targetMod);
	return true;
}

// ---------------------------------------------------------------------------
// Classic text edits
// ---------------------------------------------------------------------------

MaterialGraphEditResult swantblsEdit(const QString& text, const QString& material, const MaterialGraphEdit& edit)
{
	QVector<DoomAnimationRange> ranges;
	QVector<DoomSwitchPair> switches;
	QVector<MaterialDiagnostic> diagnostics;
	parseSwantblsText(text, &ranges, &switches, &diagnostics);
	const QString key = material.trimmed().toUpper();
	MaterialGraphEditResult result;
	if (edit.kind == MaterialGraphEditKind::SetProperty && edit.node == QStringLiteral("animation") && edit.property == QStringLiteral("tics")) {
		bool ok = false;
		const int tics = edit.value.toInt(&ok);
		if (!ok || tics <= 0 || tics > 35 * 60) {
			return fail(Text::tr("Tics must be a whole number from 1 to 2100."));
		}
		// A range is found from any member: both its ends are among the
		// material's frames.
		bool changed = false;
		for (DoomAnimationRange& range : ranges) {
			const bool ends = range.first.toUpper() == key || range.last.toUpper() == key;
			const bool member = edit.related.contains(range.first, Qt::CaseInsensitive) && edit.related.contains(range.last, Qt::CaseInsensitive);
			if (ends || member) {
				range.tics = tics;
				changed = true;
			}
		}
		if (!changed) {
			return fail(Text::tr("No animated range in this table holds %1; add one first.").arg(material));
		}
	} else if (edit.kind == MaterialGraphEditKind::SetProperty && edit.node == QStringLiteral("switch") && edit.property == QStringLiteral("partner")) {
		const QString partner = edit.value.trimmed().toUpper();
		QVector<DoomSwitchPair> kept;
		int episode = 1;
		for (const DoomSwitchPair& pair : switches) {
			if (pair.off.toUpper() != key && pair.on.toUpper() != key) {
				kept.push_back(pair);
			} else {
				episode = pair.episode;
			}
		}
		if (!partner.isEmpty()) {
			if (partner.size() > 8) {
				return fail(Text::tr("Doom lump names have at most 8 characters."));
			}
			DoomSwitchPair pair;
			pair.off = key;
			pair.on = partner;
			pair.episode = episode;
			kept.push_back(pair);
		}
		switches = kept;
	} else if (edit.kind == MaterialGraphEditKind::AddNode && edit.nodeTemplate == QStringLiteral("doom.range")) {
		// The value says what the range animates: "flat" or "texture".
		DoomAnimationRange range;
		range.first = key;
		range.last = key;
		range.texture = edit.value.compare(QStringLiteral("flat"), Qt::CaseInsensitive) != 0;
		range.tics = 8;
		range.source = QStringLiteral("boom-animated");
		ranges.push_back(range);
		result.focusNode = QStringLiteral("animation");
	} else if (edit.kind == MaterialGraphEditKind::AddNode && edit.nodeTemplate == QStringLiteral("doom.switch")) {
		DoomSwitchPair pair;
		pair.off = key;
		pair.on = key.startsWith(QStringLiteral("SW1")) ? QStringLiteral("SW2") + key.mid(3) : key;
		pair.episode = 1;
		switches.push_back(pair);
		result.focusNode = QStringLiteral("switch");
	} else {
		return fail(Text::tr("That edit does not apply to a Boom animation table."));
	}
	result.ok = true;
	result.text = swantblsText(ranges, switches);
	return result;
}

MaterialGraphEditResult animdefsEdit(const QString& text, const QString& material, const MaterialGraphEdit& edit)
{
	QVector<MaterialDiagnostic> diagnostics;
	const QVector<DoomAnimdefsEntry> entries = parseAnimdefs(text, &diagnostics);
	const QString key = material.trimmed().toUpper();
	MaterialGraphEditResult result;
	if (edit.kind == MaterialGraphEditKind::AddNode) {
		DoomAnimdefsEntry entry;
		entry.base = key;
		entry.texture = true;
		if (edit.nodeTemplate == QStringLiteral("doom.animdefs.range")) {
			entry.kind = QStringLiteral("texture");
			entry.range = true;
			entry.rangeLast = key;
			entry.rangeTics = 8;
		} else if (edit.nodeTemplate == QStringLiteral("doom.animdefs.warp") || edit.nodeTemplate == QStringLiteral("doom.animdefs.warp2")) {
			entry.kind = edit.nodeTemplate.endsWith(QLatin1Char('2')) ? QStringLiteral("warp2") : QStringLiteral("warp");
		} else {
			return fail(Text::tr("Unknown node template '%1'.").arg(edit.nodeTemplate));
		}
		result.ok = true;
		result.text = text + (text.endsWith(QLatin1Char('\n')) || text.isEmpty() ? QString() : QStringLiteral("\n")) + animdefsEntryText(entry);
		result.focusNode = entry.kind.startsWith(QStringLiteral("warp")) ? QStringLiteral("warp") : QStringLiteral("animation");
		return result;
	}
	for (const DoomAnimdefsEntry& entry : entries) {
		if (entry.base.toUpper() != key || !entry.span.isValid()) {
			continue;
		}
		DoomAnimdefsEntry changed = entry;
		if (edit.node == QStringLiteral("animation") && edit.property == QStringLiteral("tics")) {
			bool ok = false;
			const int tics = edit.value.toInt(&ok);
			if (!ok || tics <= 0) {
				return fail(Text::tr("Tics must be a whole number above 0."));
			}
			changed.rangeTics = tics;
			for (DoomAnimdefsFrame& frame : changed.frames) {
				frame.tics = tics;
			}
		} else if (edit.node == QStringLiteral("warp") && edit.property == QStringLiteral("speed")) {
			bool ok = false;
			const double speed = edit.value.toDouble(&ok);
			if (!ok || speed <= 0) {
				return fail(Text::tr("The warp speed must be above 0."));
			}
			changed.warpSpeed = speed;
		} else {
			continue;
		}
		QString updated = text;
		QString replacement = animdefsEntryText(changed);
		if (replacement.endsWith(QLatin1Char('\n'))) {
			replacement.chop(1);
		}
		updated.replace(entry.span.start, entry.span.end - entry.span.start, replacement);
		result.ok = true;
		result.text = updated;
		return result;
	}
	return fail(Text::tr("ANIMDEFS has no entry for %1 that this edit changes.").arg(material));
}

MaterialGraphEditResult walJsonEdit(const QString& text, const MaterialGraphEdit& edit)
{
	Quake2WalInfo info;
	QString error;
	if (!text.trimmed().isEmpty() && !parseQuake2WalInfoText(text, &info, &error)) {
		return fail(error);
	}
	if (edit.kind != MaterialGraphEditKind::SetProperty || edit.node != QStringLiteral("flags")) {
		return fail(Text::tr("That edit does not apply to WAL metadata."));
	}
	if (edit.property.startsWith(QStringLiteral("flag:"))) {
		const QString id = edit.property.mid(5);
		for (const MaterialFlagDescriptor& descriptor : quake2SurfaceFlagDescriptors()) {
			if (descriptor.id == id) {
				info.surfaceFlags = isTrue(edit.value) ? (info.surfaceFlags | descriptor.bit) : (info.surfaceFlags & ~descriptor.bit);
			}
		}
	} else if (edit.property == QStringLiteral("value")) {
		bool ok = false;
		const int value = edit.value.toInt(&ok);
		if (!ok) {
			return fail(Text::tr("The value must be a whole number."));
		}
		info.value = value;
	} else if (edit.property == QStringLiteral("next")) {
		info.nextFrame = edit.value.trimmed();
	} else if (edit.property == QStringLiteral("contents")) {
		quint32 contents = 0;
		for (const QString& name : edit.value.toLower().split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
			bool found = false;
			for (const MaterialFlagDescriptor& descriptor : quake2ContentFlagDescriptors()) {
				if (descriptor.id == name) {
					contents |= descriptor.bit;
					found = true;
				}
			}
			if (!found) {
				return fail(Text::tr("Unknown contents '%1'.").arg(name));
			}
		}
		info.contentFlags = contents;
	} else {
		return fail(Text::tr("Unknown WAL property '%1'.").arg(edit.property));
	}
	MaterialGraphEditResult result;
	result.ok = true;
	result.text = quake2WalInfoText(info);
	return result;
}

} // namespace

MaterialGraphEditResult applyMaterialGraphEdit(const QString& text, MaterialTextKind kind, const QString& material, const MaterialGraphEdit& edit,
	const QString& sourcePath)
{
	switch (kind) {
	case MaterialTextKind::DoomSwantbls:
		return swantblsEdit(text, material, edit);
	case MaterialTextKind::DoomAnimdefs:
		return animdefsEdit(text, material, edit);
	case MaterialTextKind::Quake2WalJson:
		return walJsonEdit(text, edit);
	case MaterialTextKind::None:
		return fail(Text::tr("This material has no text to edit."));
	case MaterialTextKind::Quake3Shader:
	case MaterialTextKind::Doom3Material:
		break;
	}
	const MaterialEngine engine = kind == MaterialTextKind::Doom3Material ? MaterialEngine::Doom3 : MaterialEngine::Quake3;
	const MaterialScript script = parseMaterialScript(text, engine, sourcePath);
	const MaterialDefinition* definition = script.find(material);
	if (!definition && !(edit.kind == MaterialGraphEditKind::AddNode && edit.nodeTemplate == QStringLiteral("d3.table"))) {
		return fail(Text::tr("No material named '%1' in this text.").arg(material));
	}
	const MaterialDefinition empty;
	const MaterialDefinition& subject = definition ? *definition : empty;
	const MaterialGraph graph = buildMaterialGraph(subject, kind);
	QVector<MaterialEdit> edits;
	QString error;
	QString focus = edit.node;
	bool ok = false;
	switch (edit.kind) {
	case MaterialGraphEditKind::SetProperty:
		ok = engine == MaterialEngine::Doom3 ? doom3PropertyEdit(subject, graph, edit, &edits, &error)
											 : quake3PropertyEdit(subject, graph, edit, &edits, &error);
		break;
	case MaterialGraphEditKind::AddNode:
		ok = addNodeEdits(subject, edit, &edits, &focus, &error);
		break;
	case MaterialGraphEditKind::RemoveNode:
		ok = removeNodeEdits(subject, graph, edit, &edits, &error);
		focus = QStringLiteral("material");
		break;
	case MaterialGraphEditKind::MoveNode:
		ok = moveNodeEdits(subject, graph, edit, &edits, &focus, &error);
		break;
	case MaterialGraphEditKind::WrapExpression:
	case MaterialGraphEditKind::UnwrapExpression:
		if (edit.node.contains(QStringLiteral("/e"))) {
			ok = expressionEdit(subject, edit, &edits, &error);
		} else {
			ok = programEdit(subject, graph, edit, &edits, &error);
		}
		break;
	case MaterialGraphEditKind::ExpandShorthand: {
		const int stageIndex = stageOf(edit.node);
		if (stageIndex < 0 || stageIndex >= subject.stages.size() || subject.stages.at(stageIndex).shorthandDirective < 0) {
			error = Text::tr("Only diffusemap, bumpmap and specularmap shorthands expand.");
			break;
		}
		const MaterialStage& stage = subject.stages.at(stageIndex);
		const MaterialDirective& directive = subject.directives.at(stage.shorthandDirective);
		edits.push_back(removeEdit(subject.name, -1, stage.shorthandDirective));
		MaterialEdit add;
		add.kind = MaterialEditKind::AddStage;
		add.material = subject.name;
		add.text = QStringLiteral("blend %1\nmap %2").arg(directive.keyword.toLower(), directive.argumentText);
		edits.push_back(add);
		ok = true;
		break;
	}
	}
	if (!ok) {
		return fail(error);
	}
	const MaterialEditResult applied = applyMaterialEdits(script, edits);
	if (!applied.ok) {
		return fail(applied.error);
	}
	MaterialGraphEditResult result;
	result.ok = true;
	result.text = applied.text;
	result.focusNode = focus;
	return result;
}

QJsonObject materialGraphJson(const MaterialGraph& graph)
{
	QJsonObject object;
	object.insert(QStringLiteral("material"), graph.material);
	object.insert(QStringLiteral("engine"), materialEngineId(graph.engine));
	object.insert(QStringLiteral("text"), materialTextKindId(graph.textKind));
	QJsonArray nodes;
	for (const MaterialGraphNode& node : graph.nodes) {
		QJsonObject item;
		item.insert(QStringLiteral("id"), node.id);
		item.insert(QStringLiteral("kind"), node.kind);
		item.insert(QStringLiteral("title"), node.title);
		if (!node.subtitle.isEmpty()) {
			item.insert(QStringLiteral("subtitle"), node.subtitle);
		}
		item.insert(QStringLiteral("x"), node.position.x());
		item.insert(QStringLiteral("y"), node.position.y());
		if (node.line > 0) {
			item.insert(QStringLiteral("line"), node.line);
		}
		if (node.removable) {
			item.insert(QStringLiteral("removable"), true);
		}
		if (node.reorderable) {
			item.insert(QStringLiteral("reorderable"), true);
		}
		if (node.inactive) {
			item.insert(QStringLiteral("inactive"), true);
		}
		QJsonArray inputs;
		for (const MaterialGraphPort& item : node.inputs) {
			inputs.append(QJsonObject {{QStringLiteral("id"), item.id}, {QStringLiteral("type"), materialGraphPortTypeId(item.type)}});
		}
		QJsonArray outputs;
		for (const MaterialGraphPort& item : node.outputs) {
			outputs.append(QJsonObject {{QStringLiteral("id"), item.id}, {QStringLiteral("type"), materialGraphPortTypeId(item.type)}});
		}
		item.insert(QStringLiteral("inputs"), inputs);
		item.insert(QStringLiteral("outputs"), outputs);
		QJsonArray properties;
		for (const MaterialGraphProperty& property : node.properties) {
			QJsonObject entry;
			entry.insert(QStringLiteral("id"), property.id);
			entry.insert(QStringLiteral("type"), materialGraphPropertyTypeId(property.type));
			entry.insert(QStringLiteral("value"), property.value);
			if (!property.options.isEmpty()) {
				entry.insert(QStringLiteral("options"), QJsonArray::fromStringList(property.options));
			}
			properties.append(entry);
		}
		item.insert(QStringLiteral("properties"), properties);
		nodes.append(item);
	}
	object.insert(QStringLiteral("nodes"), nodes);
	QJsonArray links;
	for (const MaterialGraphLink& item : graph.links) {
		links.append(QJsonObject {{QStringLiteral("from"), item.fromNode + QLatin1Char('.') + item.fromPort},
			{QStringLiteral("to"), item.toNode + QLatin1Char('.') + item.toPort}});
	}
	object.insert(QStringLiteral("links"), links);
	return object;
}

bool parseMaterialGraphEditsJson(const QByteArray& json, QVector<MaterialGraphEdit>* edits, QString* error)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
		*error = Text::tr("Graph edits must be a JSON array: %1").arg(parseError.errorString());
		return false;
	}
	const QJsonArray array = document.array();
	if (array.size() > 256) {
		*error = Text::tr("At most 256 graph edits are accepted at once.");
		return false;
	}
	static const QSet<QString> known {QStringLiteral("edit"), QStringLiteral("node"), QStringLiteral("property"), QStringLiteral("value"),
		QStringLiteral("template"), QStringLiteral("step")};
	for (int index = 0; index < array.size(); ++index) {
		if (!array.at(index).isObject()) {
			*error = Text::tr("Graph edit %1 is not an object.").arg(index + 1);
			return false;
		}
		const QJsonObject object = array.at(index).toObject();
		for (const QString& key : object.keys()) {
			if (!known.contains(key)) {
				*error = Text::tr("Graph edit %1 has an unknown field '%2'.").arg(index + 1).arg(key);
				return false;
			}
		}
		MaterialGraphEdit edit;
		if (!materialGraphEditKindFromId(object.value(QStringLiteral("edit")).toString(QStringLiteral("set")), &edit.kind)) {
			*error = Text::tr("Graph edit %1 has an unknown kind '%2'.").arg(index + 1).arg(object.value(QStringLiteral("edit")).toString());
			return false;
		}
		edit.node = object.value(QStringLiteral("node")).toString();
		edit.property = object.value(QStringLiteral("property")).toString();
		const QJsonValue value = object.value(QStringLiteral("value"));
		edit.value = value.isBool() ? boolText(value.toBool()) : value.isDouble() ? numberText(value.toDouble()) : value.toString();
		edit.nodeTemplate = object.value(QStringLiteral("template")).toString();
		edit.step = object.value(QStringLiteral("step")).toInt(1);
		edits->push_back(edit);
	}
	return true;
}

} // namespace vibestudio
