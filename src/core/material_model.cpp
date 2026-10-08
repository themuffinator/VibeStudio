#include "core/material_model.h"
#include "core/material_script.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

QJsonArray stringsJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QJsonObject spanJson(const MaterialSourceSpan& span)
{
	QJsonObject object;
	object.insert(QStringLiteral("line"), span.line);
	object.insert(QStringLiteral("endLine"), span.endLine);
	object.insert(QStringLiteral("start"), span.start);
	object.insert(QStringLiteral("end"), span.end);
	return object;
}

QJsonObject waveJson(const MaterialWave& wave)
{
	QJsonObject object;
	object.insert(QStringLiteral("function"), materialWaveFunctionId(wave.function));
	object.insert(QStringLiteral("base"), wave.base);
	object.insert(QStringLiteral("amplitude"), wave.amplitude);
	object.insert(QStringLiteral("phase"), wave.phase);
	object.insert(QStringLiteral("frequency"), wave.frequency);
	return object;
}

QJsonObject directiveJson(const MaterialDirective& directive)
{
	QJsonObject object;
	object.insert(QStringLiteral("keyword"), directive.keyword);
	object.insert(QStringLiteral("arguments"), directive.argumentText);
	object.insert(QStringLiteral("line"), directive.span.line);
	if (!directive.recognised) {
		object.insert(QStringLiteral("recognised"), false);
	}
	if (directive.previewIgnored) {
		object.insert(QStringLiteral("previewIgnored"), true);
	}
	return object;
}

QJsonObject colorGenJson(const MaterialColorGen& gen, bool alpha)
{
	QJsonObject object;
	object.insert(QStringLiteral("source"), materialColorSourceId(gen.source));
	object.insert(QStringLiteral("explicit"), gen.explicitlySet);
	if (gen.source == MaterialColorSource::Constant) {
		if (alpha) {
			object.insert(QStringLiteral("value"), gen.constantAlpha);
		} else {
			object.insert(QStringLiteral("value"), QJsonArray {gen.constant[0], gen.constant[1], gen.constant[2]});
		}
	}
	if (gen.source == MaterialColorSource::Wave) {
		object.insert(QStringLiteral("wave"), waveJson(gen.wave));
	}
	if (gen.source == MaterialColorSource::Portal) {
		object.insert(QStringLiteral("range"), gen.portalRange);
	}
	return object;
}

} // namespace

QString materialEngineId(MaterialEngine engine)
{
	switch (engine) {
	case MaterialEngine::Doom:
		return QStringLiteral("doom");
	case MaterialEngine::Quake:
		return QStringLiteral("quake");
	case MaterialEngine::Quake2:
		return QStringLiteral("quake2");
	case MaterialEngine::Quake3:
		return QStringLiteral("quake3");
	case MaterialEngine::Doom3:
		return QStringLiteral("doom3");
	case MaterialEngine::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString materialEngineDisplayName(MaterialEngine engine)
{
	switch (engine) {
	case MaterialEngine::Doom:
		return QCoreApplication::translate("VibeStudioMaterials", "Doom");
	case MaterialEngine::Quake:
		return QCoreApplication::translate("VibeStudioMaterials", "Quake");
	case MaterialEngine::Quake2:
		return QCoreApplication::translate("VibeStudioMaterials", "Quake II");
	case MaterialEngine::Quake3:
		return QCoreApplication::translate("VibeStudioMaterials", "Quake III");
	case MaterialEngine::Doom3:
		return QCoreApplication::translate("VibeStudioMaterials", "Doom 3");
	case MaterialEngine::Unknown:
		break;
	}
	return QCoreApplication::translate("VibeStudioMaterials", "Unknown engine");
}

QString materialEngineGeneration(MaterialEngine engine)
{
	switch (engine) {
	case MaterialEngine::Doom:
		return QStringLiteral("idTech 1");
	case MaterialEngine::Quake:
	case MaterialEngine::Quake2:
		return QStringLiteral("idTech 2");
	case MaterialEngine::Quake3:
		return QStringLiteral("idTech 3");
	case MaterialEngine::Doom3:
		return QStringLiteral("idTech 4");
	case MaterialEngine::Unknown:
		break;
	}
	return QString();
}

MaterialEngine materialEngineFromId(const QString& id)
{
	const QString key = id.trimmed().toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('-')).remove(QLatin1Char('_'));
	static const QHash<QString, MaterialEngine> aliases {
		{QStringLiteral("doom"), MaterialEngine::Doom},
		{QStringLiteral("doom1"), MaterialEngine::Doom},
		{QStringLiteral("doom2"), MaterialEngine::Doom},
		{QStringLiteral("idtech1"), MaterialEngine::Doom},
		{QStringLiteral("heretic"), MaterialEngine::Doom},
		{QStringLiteral("hexen"), MaterialEngine::Doom},
		{QStringLiteral("strife"), MaterialEngine::Doom},
		{QStringLiteral("boom"), MaterialEngine::Doom},
		{QStringLiteral("zdoom"), MaterialEngine::Doom},
		{QStringLiteral("quake"), MaterialEngine::Quake},
		{QStringLiteral("quake1"), MaterialEngine::Quake},
		{QStringLiteral("q1"), MaterialEngine::Quake},
		{QStringLiteral("idtech2"), MaterialEngine::Quake},
		{QStringLiteral("halflife"), MaterialEngine::Quake},
		{QStringLiteral("goldsrc"), MaterialEngine::Quake},
		{QStringLiteral("quake2"), MaterialEngine::Quake2},
		{QStringLiteral("q2"), MaterialEngine::Quake2},
		{QStringLiteral("quakeii"), MaterialEngine::Quake2},
		{QStringLiteral("quake3"), MaterialEngine::Quake3},
		{QStringLiteral("q3"), MaterialEngine::Quake3},
		{QStringLiteral("quakeiii"), MaterialEngine::Quake3},
		{QStringLiteral("quake3arena"), MaterialEngine::Quake3},
		{QStringLiteral("q3a"), MaterialEngine::Quake3},
		{QStringLiteral("idtech3"), MaterialEngine::Quake3},
		{QStringLiteral("rtcw"), MaterialEngine::Quake3},
		{QStringLiteral("et"), MaterialEngine::Quake3},
		{QStringLiteral("jk2"), MaterialEngine::Quake3},
		{QStringLiteral("jka"), MaterialEngine::Quake3},
		{QStringLiteral("doom3"), MaterialEngine::Doom3},
		{QStringLiteral("d3"), MaterialEngine::Doom3},
		{QStringLiteral("idtech4"), MaterialEngine::Doom3},
		{QStringLiteral("quake4"), MaterialEngine::Doom3},
		{QStringLiteral("q4"), MaterialEngine::Doom3},
		{QStringLiteral("prey"), MaterialEngine::Doom3},
	};
	return aliases.value(key, MaterialEngine::Unknown);
}

QVector<MaterialEngine> materialEngines()
{
	return {MaterialEngine::Doom, MaterialEngine::Quake, MaterialEngine::Quake2, MaterialEngine::Quake3, MaterialEngine::Doom3};
}

QString materialDiagnosticSeverityId(MaterialDiagnosticSeverity severity)
{
	switch (severity) {
	case MaterialDiagnosticSeverity::Info:
		return QStringLiteral("info");
	case MaterialDiagnosticSeverity::Warning:
		return QStringLiteral("warning");
	case MaterialDiagnosticSeverity::Error:
		return QStringLiteral("error");
	}
	return QStringLiteral("warning");
}

QString materialBlendFactorId(MaterialBlendFactor factor)
{
	switch (factor) {
	case MaterialBlendFactor::Zero:
		return QStringLiteral("GL_ZERO");
	case MaterialBlendFactor::One:
		return QStringLiteral("GL_ONE");
	case MaterialBlendFactor::SourceColor:
		return QStringLiteral("GL_SRC_COLOR");
	case MaterialBlendFactor::OneMinusSourceColor:
		return QStringLiteral("GL_ONE_MINUS_SRC_COLOR");
	case MaterialBlendFactor::DestinationColor:
		return QStringLiteral("GL_DST_COLOR");
	case MaterialBlendFactor::OneMinusDestinationColor:
		return QStringLiteral("GL_ONE_MINUS_DST_COLOR");
	case MaterialBlendFactor::SourceAlpha:
		return QStringLiteral("GL_SRC_ALPHA");
	case MaterialBlendFactor::OneMinusSourceAlpha:
		return QStringLiteral("GL_ONE_MINUS_SRC_ALPHA");
	case MaterialBlendFactor::DestinationAlpha:
		return QStringLiteral("GL_DST_ALPHA");
	case MaterialBlendFactor::OneMinusDestinationAlpha:
		return QStringLiteral("GL_ONE_MINUS_DST_ALPHA");
	case MaterialBlendFactor::SourceAlphaSaturate:
		return QStringLiteral("GL_SRC_ALPHA_SATURATE");
	}
	return QStringLiteral("GL_ONE");
}

bool materialBlendFactorFromId(const QString& id, MaterialBlendFactor* factor)
{
	static const QHash<QString, MaterialBlendFactor> factors {
		{QStringLiteral("GL_ZERO"), MaterialBlendFactor::Zero},
		{QStringLiteral("GL_ONE"), MaterialBlendFactor::One},
		{QStringLiteral("GL_SRC_COLOR"), MaterialBlendFactor::SourceColor},
		{QStringLiteral("GL_ONE_MINUS_SRC_COLOR"), MaterialBlendFactor::OneMinusSourceColor},
		{QStringLiteral("GL_DST_COLOR"), MaterialBlendFactor::DestinationColor},
		{QStringLiteral("GL_ONE_MINUS_DST_COLOR"), MaterialBlendFactor::OneMinusDestinationColor},
		{QStringLiteral("GL_SRC_ALPHA"), MaterialBlendFactor::SourceAlpha},
		{QStringLiteral("GL_ONE_MINUS_SRC_ALPHA"), MaterialBlendFactor::OneMinusSourceAlpha},
		{QStringLiteral("GL_DST_ALPHA"), MaterialBlendFactor::DestinationAlpha},
		{QStringLiteral("GL_ONE_MINUS_DST_ALPHA"), MaterialBlendFactor::OneMinusDestinationAlpha},
		{QStringLiteral("GL_SRC_ALPHA_SATURATE"), MaterialBlendFactor::SourceAlphaSaturate},
	};
	const auto found = factors.constFind(id.trimmed().toUpper());
	if (found == factors.constEnd()) {
		return false;
	}
	if (factor) {
		*factor = *found;
	}
	return true;
}

QString materialWaveFunctionId(MaterialWaveFunction function)
{
	switch (function) {
	case MaterialWaveFunction::Sin:
		return QStringLiteral("sin");
	case MaterialWaveFunction::Triangle:
		return QStringLiteral("triangle");
	case MaterialWaveFunction::Square:
		return QStringLiteral("square");
	case MaterialWaveFunction::Sawtooth:
		return QStringLiteral("sawtooth");
	case MaterialWaveFunction::InverseSawtooth:
		return QStringLiteral("inversesawtooth");
	case MaterialWaveFunction::Noise:
		return QStringLiteral("noise");
	}
	return QStringLiteral("sin");
}

bool materialWaveFunctionFromId(const QString& id, MaterialWaveFunction* function)
{
	const QString key = id.trimmed().toLower();
	MaterialWaveFunction value = MaterialWaveFunction::Sin;
	if (key == QStringLiteral("sin")) {
		value = MaterialWaveFunction::Sin;
	} else if (key == QStringLiteral("triangle")) {
		value = MaterialWaveFunction::Triangle;
	} else if (key == QStringLiteral("square")) {
		value = MaterialWaveFunction::Square;
	} else if (key == QStringLiteral("sawtooth")) {
		value = MaterialWaveFunction::Sawtooth;
	} else if (key == QStringLiteral("inversesawtooth")) {
		value = MaterialWaveFunction::InverseSawtooth;
	} else if (key == QStringLiteral("noise")) {
		value = MaterialWaveFunction::Noise;
	} else {
		return false;
	}
	if (function) {
		*function = value;
	}
	return true;
}

QString materialColorSourceId(MaterialColorSource source)
{
	switch (source) {
	case MaterialColorSource::Identity:
		return QStringLiteral("identity");
	case MaterialColorSource::IdentityLighting:
		return QStringLiteral("identityLighting");
	case MaterialColorSource::Constant:
		return QStringLiteral("const");
	case MaterialColorSource::Wave:
		return QStringLiteral("wave");
	case MaterialColorSource::Entity:
		return QStringLiteral("entity");
	case MaterialColorSource::OneMinusEntity:
		return QStringLiteral("oneMinusEntity");
	case MaterialColorSource::Vertex:
		return QStringLiteral("vertex");
	case MaterialColorSource::ExactVertex:
		return QStringLiteral("exactVertex");
	case MaterialColorSource::OneMinusVertex:
		return QStringLiteral("oneMinusVertex");
	case MaterialColorSource::LightingDiffuse:
		return QStringLiteral("lightingDiffuse");
	case MaterialColorSource::LightingSpecular:
		return QStringLiteral("lightingSpecular");
	case MaterialColorSource::Portal:
		return QStringLiteral("portal");
	case MaterialColorSource::Skip:
		return QStringLiteral("skip");
	}
	return QStringLiteral("identity");
}

QString materialTexCoordSourceId(MaterialTexCoordSource source)
{
	switch (source) {
	case MaterialTexCoordSource::Base:
		return QStringLiteral("base");
	case MaterialTexCoordSource::Lightmap:
		return QStringLiteral("lightmap");
	case MaterialTexCoordSource::Environment:
		return QStringLiteral("environment");
	case MaterialTexCoordSource::Vector:
		return QStringLiteral("vector");
	case MaterialTexCoordSource::Normal:
		return QStringLiteral("normal");
	case MaterialTexCoordSource::Reflect:
		return QStringLiteral("reflect");
	case MaterialTexCoordSource::Skybox:
		return QStringLiteral("skybox");
	case MaterialTexCoordSource::WobbleSky:
		return QStringLiteral("wobbleSky");
	case MaterialTexCoordSource::Screen:
		return QStringLiteral("screen");
	case MaterialTexCoordSource::Screen2:
		return QStringLiteral("screen2");
	case MaterialTexCoordSource::GlassWarp:
		return QStringLiteral("glassWarp");
	}
	return QStringLiteral("base");
}

QString materialTexModKindId(MaterialTexModKind kind)
{
	switch (kind) {
	case MaterialTexModKind::Scroll:
		return QStringLiteral("scroll");
	case MaterialTexModKind::Scale:
		return QStringLiteral("scale");
	case MaterialTexModKind::Rotate:
		return QStringLiteral("rotate");
	case MaterialTexModKind::Stretch:
		return QStringLiteral("stretch");
	case MaterialTexModKind::Transform:
		return QStringLiteral("transform");
	case MaterialTexModKind::Turbulent:
		return QStringLiteral("turb");
	case MaterialTexModKind::EntityTranslate:
		return QStringLiteral("entityTranslate");
	case MaterialTexModKind::Translate:
		return QStringLiteral("translate");
	case MaterialTexModKind::ScaleExpression:
		return QStringLiteral("scale");
	case MaterialTexModKind::CenterScale:
		return QStringLiteral("centerScale");
	case MaterialTexModKind::Shear:
		return QStringLiteral("shear");
	case MaterialTexModKind::RotateExpression:
		return QStringLiteral("rotate");
	}
	return QStringLiteral("scroll");
}

QString materialImageKindId(MaterialImageKind kind)
{
	switch (kind) {
	case MaterialImageKind::None:
		return QStringLiteral("none");
	case MaterialImageKind::File:
		return QStringLiteral("file");
	case MaterialImageKind::Lightmap:
		return QStringLiteral("lightmap");
	case MaterialImageKind::White:
		return QStringLiteral("white");
	case MaterialImageKind::Animation:
		return QStringLiteral("animation");
	case MaterialImageKind::Video:
		return QStringLiteral("video");
	case MaterialImageKind::CubeMap:
		return QStringLiteral("cubemap");
	case MaterialImageKind::RenderTarget:
		return QStringLiteral("render-target");
	case MaterialImageKind::Builtin:
		return QStringLiteral("builtin");
	case MaterialImageKind::Program:
		return QStringLiteral("program");
	case MaterialImageKind::SoundMap:
		return QStringLiteral("soundmap");
	}
	return QStringLiteral("none");
}

QString materialStageRoleId(MaterialStageRole role)
{
	switch (role) {
	case MaterialStageRole::Regular:
		return QStringLiteral("regular");
	case MaterialStageRole::Diffuse:
		return QStringLiteral("diffuse");
	case MaterialStageRole::Bump:
		return QStringLiteral("bump");
	case MaterialStageRole::Specular:
		return QStringLiteral("specular");
	}
	return QStringLiteral("regular");
}

QString materialAlphaTestId(MaterialAlphaTest test)
{
	switch (test) {
	case MaterialAlphaTest::None:
		return QStringLiteral("none");
	case MaterialAlphaTest::Greater0:
		return QStringLiteral("GT0");
	case MaterialAlphaTest::Less128:
		return QStringLiteral("LT128");
	case MaterialAlphaTest::GreaterEqual128:
		return QStringLiteral("GE128");
	case MaterialAlphaTest::Expression:
		return QStringLiteral("expression");
	}
	return QStringLiteral("none");
}

QString materialCullId(MaterialCull cull)
{
	switch (cull) {
	case MaterialCull::Front:
		return QStringLiteral("front");
	case MaterialCull::Back:
		return QStringLiteral("back");
	case MaterialCull::None:
		return QStringLiteral("none");
	}
	return QStringLiteral("front");
}

QString materialDeformKindId(MaterialDeformKind kind)
{
	switch (kind) {
	case MaterialDeformKind::Wave:
		return QStringLiteral("wave");
	case MaterialDeformKind::Normal:
		return QStringLiteral("normal");
	case MaterialDeformKind::Bulge:
		return QStringLiteral("bulge");
	case MaterialDeformKind::Move:
		return QStringLiteral("move");
	case MaterialDeformKind::Autosprite:
		return QStringLiteral("autosprite");
	case MaterialDeformKind::Autosprite2:
		return QStringLiteral("autosprite2");
	case MaterialDeformKind::ProjectionShadow:
		return QStringLiteral("projectionShadow");
	case MaterialDeformKind::Text:
		return QStringLiteral("text");
	case MaterialDeformKind::Sprite:
		return QStringLiteral("sprite");
	case MaterialDeformKind::Tube:
		return QStringLiteral("tube");
	case MaterialDeformKind::Flare:
		return QStringLiteral("flare");
	case MaterialDeformKind::Expand:
		return QStringLiteral("expand");
	case MaterialDeformKind::MoveExpression:
		return QStringLiteral("move");
	case MaterialDeformKind::Turbulent:
		return QStringLiteral("turbulent");
	case MaterialDeformKind::EyeBall:
		return QStringLiteral("eyeBall");
	case MaterialDeformKind::Particle:
		return QStringLiteral("particle");
	case MaterialDeformKind::Particle2:
		return QStringLiteral("particle2");
	}
	return QStringLiteral("wave");
}

QString materialWarpStyleId(MaterialWarpStyle style)
{
	switch (style) {
	case MaterialWarpStyle::None:
		return QStringLiteral("none");
	case MaterialWarpStyle::QuakeTurbulent:
		return QStringLiteral("quake-turbulent");
	case MaterialWarpStyle::QuakeSky:
		return QStringLiteral("quake-sky");
	case MaterialWarpStyle::DoomWarp:
		return QStringLiteral("warp");
	case MaterialWarpStyle::DoomWarp2:
		return QStringLiteral("warp2");
	}
	return QStringLiteral("none");
}

QString materialExpressionOpId(MaterialExpressionOp op)
{
	switch (op) {
	case MaterialExpressionOp::Constant:
		return QStringLiteral("constant");
	case MaterialExpressionOp::Time:
		return QStringLiteral("time");
	case MaterialExpressionOp::Parm:
		return QStringLiteral("parm");
	case MaterialExpressionOp::Global:
		return QStringLiteral("global");
	case MaterialExpressionOp::Sound:
		return QStringLiteral("sound");
	case MaterialExpressionOp::FragmentPrograms:
		return QStringLiteral("fragmentPrograms");
	case MaterialExpressionOp::Table:
		return QStringLiteral("table");
	case MaterialExpressionOp::Negate:
		return QStringLiteral("negate");
	case MaterialExpressionOp::Add:
		return QStringLiteral("add");
	case MaterialExpressionOp::Subtract:
		return QStringLiteral("subtract");
	case MaterialExpressionOp::Multiply:
		return QStringLiteral("multiply");
	case MaterialExpressionOp::Divide:
		return QStringLiteral("divide");
	case MaterialExpressionOp::Modulo:
		return QStringLiteral("modulo");
	case MaterialExpressionOp::Greater:
		return QStringLiteral("greater");
	case MaterialExpressionOp::GreaterEqual:
		return QStringLiteral("greaterEqual");
	case MaterialExpressionOp::Less:
		return QStringLiteral("less");
	case MaterialExpressionOp::LessEqual:
		return QStringLiteral("lessEqual");
	case MaterialExpressionOp::Equal:
		return QStringLiteral("equal");
	case MaterialExpressionOp::NotEqual:
		return QStringLiteral("notEqual");
	case MaterialExpressionOp::And:
		return QStringLiteral("and");
	case MaterialExpressionOp::Or:
		return QStringLiteral("or");
	}
	return QStringLiteral("constant");
}

QString materialExpressionOpToken(MaterialExpressionOp op)
{
	switch (op) {
	case MaterialExpressionOp::Negate:
	case MaterialExpressionOp::Subtract:
		return QStringLiteral("-");
	case MaterialExpressionOp::Add:
		return QStringLiteral("+");
	case MaterialExpressionOp::Multiply:
		return QStringLiteral("*");
	case MaterialExpressionOp::Divide:
		return QStringLiteral("/");
	case MaterialExpressionOp::Modulo:
		return QStringLiteral("%");
	case MaterialExpressionOp::Greater:
		return QStringLiteral(">");
	case MaterialExpressionOp::GreaterEqual:
		return QStringLiteral(">=");
	case MaterialExpressionOp::Less:
		return QStringLiteral("<");
	case MaterialExpressionOp::LessEqual:
		return QStringLiteral("<=");
	case MaterialExpressionOp::Equal:
		return QStringLiteral("==");
	case MaterialExpressionOp::NotEqual:
		return QStringLiteral("!=");
	case MaterialExpressionOp::And:
		return QStringLiteral("&&");
	case MaterialExpressionOp::Or:
		return QStringLiteral("||");
	default:
		break;
	}
	return QString();
}

int materialExpressionOpPriority(MaterialExpressionOp op)
{
	switch (op) {
	case MaterialExpressionOp::Multiply:
	case MaterialExpressionOp::Divide:
	case MaterialExpressionOp::Modulo:
		return 1;
	case MaterialExpressionOp::Add:
	case MaterialExpressionOp::Subtract:
		return 2;
	case MaterialExpressionOp::Greater:
	case MaterialExpressionOp::GreaterEqual:
	case MaterialExpressionOp::Less:
	case MaterialExpressionOp::LessEqual:
	case MaterialExpressionOp::Equal:
	case MaterialExpressionOp::NotEqual:
		return 3;
	case MaterialExpressionOp::And:
	case MaterialExpressionOp::Or:
		return 4;
	default:
		break;
	}
	return 0;
}

QVector<MaterialFlagDescriptor> quake2SurfaceFlagDescriptors()
{
	// Values from Quake II's qfiles.h / q_shared.h (SURF_*).
	return {
		{kQuake2SurfLight, QStringLiteral("light"), Text::tr("Light"), Text::tr("Emits light into the level; the value sets its intensity.")},
		{kQuake2SurfSlick, QStringLiteral("slick"), Text::tr("Slick"), Text::tr("Reduced friction for players.")},
		{kQuake2SurfSky, QStringLiteral("sky"), Text::tr("Sky"), Text::tr("Drawn as the level's sky box.")},
		{kQuake2SurfWarp, QStringLiteral("warp"), Text::tr("Warp"), Text::tr("Turbulent liquid warp, drawn without a lightmap.")},
		{kQuake2SurfTrans33, QStringLiteral("trans33"), Text::tr("Translucent 33%"), Text::tr("Blended at 33% opacity without a lightmap.")},
		{kQuake2SurfTrans66, QStringLiteral("trans66"), Text::tr("Translucent 66%"), Text::tr("Blended at 66% opacity without a lightmap.")},
		{kQuake2SurfFlowing, QStringLiteral("flowing"), Text::tr("Flowing"), Text::tr("Scrolls along the texture's horizontal axis.")},
		{kQuake2SurfNoDraw, QStringLiteral("nodraw"), Text::tr("No draw"), Text::tr("Never drawn; used for clip and trigger brushes.")},
		{0x100, QStringLiteral("hint"), Text::tr("Hint"), Text::tr("Compiler hint for the visibility split.")},
		{0x200, QStringLiteral("skip"), Text::tr("Skip"), Text::tr("Ignored by the compiler's face list.")},
		// The 2023 re-release's additions (quake2-rerelease-dll README).
		{1u << 25, QStringLiteral("alphatest"), Text::tr("Alpha test"), Text::tr("Re-release: transparent pixels are cut out.")},
		{1u << 28, QStringLiteral("n64_uv"), Text::tr("N64 UV"), Text::tr("Re-release: halves the texture size, as on the N64.")},
		{1u << 29, QStringLiteral("n64_scroll_x"), Text::tr("N64 scroll X"), Text::tr("Re-release: scrolls horizontally.")},
		{1u << 30, QStringLiteral("n64_scroll_y"), Text::tr("N64 scroll Y"), Text::tr("Re-release: scrolls vertically.")},
		{1u << 31, QStringLiteral("n64_scroll_flip"), Text::tr("N64 scroll flip"), Text::tr("Re-release: reverses the scroll direction.")},
	};
}

QVector<MaterialFlagDescriptor> quake2ContentFlagDescriptors()
{
	// Values from Quake II's q_shared.h (CONTENTS_*).
	return {
		{0x1, QStringLiteral("solid"), Text::tr("Solid"), Text::tr("Blocks movement and sight.")},
		{0x2, QStringLiteral("window"), Text::tr("Window"), Text::tr("Translucent but solid.")},
		{0x4, QStringLiteral("aux"), Text::tr("Auxiliary"), QString()},
		{0x8, QStringLiteral("lava"), Text::tr("Lava"), Text::tr("Hurts and burns.")},
		{0x10, QStringLiteral("slime"), Text::tr("Slime"), Text::tr("Hurts on contact.")},
		{0x20, QStringLiteral("water"), Text::tr("Water"), Text::tr("Swimmable liquid.")},
		{0x40, QStringLiteral("mist"), Text::tr("Mist"), Text::tr("Non-solid volume.")},
		{0x8000, QStringLiteral("areaportal"), Text::tr("Area portal"), QString()},
		{0x10000, QStringLiteral("playerclip"), Text::tr("Player clip"), QString()},
		{0x20000, QStringLiteral("monsterclip"), Text::tr("Monster clip"), QString()},
		{0x40000, QStringLiteral("current_0"), Text::tr("Current 0°"), QString()},
		{0x80000, QStringLiteral("current_90"), Text::tr("Current 90°"), QString()},
		{0x100000, QStringLiteral("current_180"), Text::tr("Current 180°"), QString()},
		{0x200000, QStringLiteral("current_270"), Text::tr("Current 270°"), QString()},
		{0x400000, QStringLiteral("current_up"), Text::tr("Current up"), QString()},
		{0x800000, QStringLiteral("current_down"), Text::tr("Current down"), QString()},
		{0x1000000, QStringLiteral("origin"), Text::tr("Origin"), Text::tr("Marks a brush model's rotation origin.")},
		{0x2000000, QStringLiteral("monster"), Text::tr("Monster"), QString()},
		{0x4000000, QStringLiteral("deadmonster"), Text::tr("Dead monster"), QString()},
		{0x8000000, QStringLiteral("detail"), Text::tr("Detail"), Text::tr("Left out of the visibility split.")},
		{0x10000000, QStringLiteral("translucent"), Text::tr("Translucent"), Text::tr("Does not block sight.")},
		{0x20000000, QStringLiteral("ladder"), Text::tr("Ladder"), Text::tr("Climbable.")},
	};
}

QStringList quake2FlagIds(quint32 flags, const QVector<MaterialFlagDescriptor>& descriptors)
{
	QStringList ids;
	quint32 known = 0;
	for (const MaterialFlagDescriptor& descriptor : descriptors) {
		known |= descriptor.bit;
		if ((flags & descriptor.bit) != 0) {
			ids << descriptor.id;
		}
	}
	const quint32 unknown = flags & ~known;
	if (unknown != 0) {
		ids << QStringLiteral("0x%1").arg(unknown, 0, 16);
	}
	return ids;
}

bool MaterialDefinition::hasFlag(const QString& flag) const
{
	return flags.contains(flag.toLower());
}

bool MaterialDefinition::isLight() const
{
	if (engine != MaterialEngine::Doom3) {
		return false;
	}
	const QString key = materialLookupKey(name);
	return key.startsWith(QStringLiteral("lights/")) || key.startsWith(QStringLiteral("fogs/"))
		|| !lightFalloffImage.isEmpty() || fogLight || blendLight || ambientLight;
}

bool MaterialDefinition::isSky() const
{
	if (sky.present) {
		return true;
	}
	if (surfaceParms.contains(QStringLiteral("sky"))) {
		return true;
	}
	if (engine == MaterialEngine::Quake && classic.warp == MaterialWarpStyle::QuakeSky) {
		return true;
	}
	if (engine == MaterialEngine::Quake2 && (classic.surfaceFlags & kQuake2SurfSky) != 0) {
		return true;
	}
	if (engine == MaterialEngine::Doom && materialLookupKey(name).startsWith(QStringLiteral("f_sky"))) {
		return true;
	}
	if (engine == MaterialEngine::Doom3) {
		for (const MaterialStage& stage : stages) {
			if (stage.tcGen.source == MaterialTexCoordSource::Skybox || stage.tcGen.source == MaterialTexCoordSource::WobbleSky) {
				return true;
			}
		}
	}
	return false;
}

bool MaterialDefinition::isFog() const
{
	return fog.present || fogLight || surfaceParms.contains(QStringLiteral("fog"));
}

bool MaterialDefinition::usesLightmap() const
{
	for (const MaterialStage& stage : stages) {
		if (stage.imageKind == MaterialImageKind::Lightmap || stage.tcGen.source == MaterialTexCoordSource::Lightmap) {
			return true;
		}
	}
	return false;
}

bool MaterialDefinition::isAnimated() const
{
	if (!classic.frames.isEmpty() || classic.warp != MaterialWarpStyle::None) {
		return true;
	}
	if (engine == MaterialEngine::Quake2 && (classic.surfaceFlags & (kQuake2SurfWarp | kQuake2SurfFlowing)) != 0) {
		return true;
	}
	for (const MaterialDeform& deform : deforms) {
		if (deform.kind != MaterialDeformKind::Autosprite && deform.kind != MaterialDeformKind::Autosprite2
			&& deform.kind != MaterialDeformKind::ProjectionShadow) {
			return true;
		}
	}
	for (const MaterialExpressionNode& node : expressions) {
		if (node.op == MaterialExpressionOp::Time || node.op == MaterialExpressionOp::Sound) {
			return true;
		}
	}
	for (const MaterialStage& stage : stages) {
		if (stage.imageKind == MaterialImageKind::Animation || stage.imageKind == MaterialImageKind::Video) {
			return true;
		}
		if (stage.rgbGen.source == MaterialColorSource::Wave || stage.alphaGen.source == MaterialColorSource::Wave) {
			return true;
		}
		for (const MaterialTexMod& mod : stage.tcMods) {
			switch (mod.kind) {
			case MaterialTexModKind::Scroll:
			case MaterialTexModKind::Rotate:
			case MaterialTexModKind::Stretch:
			case MaterialTexModKind::Turbulent:
				return true;
			default:
				break;
			}
		}
	}
	return false;
}

QStringList MaterialDefinition::imageReferences() const
{
	QStringList images;
	QSet<QString> seen;
	const auto add = [&](const QString& path) {
		const QString trimmed = path.trimmed();
		if (trimmed.isEmpty() || trimmed == QStringLiteral("-")) {
			return;
		}
		const QString key = materialLookupKey(trimmed);
		if (seen.contains(key)) {
			return;
		}
		seen.insert(key);
		images << trimmed;
	};
	for (const MaterialStage& stage : stages) {
		if (stage.imageKind == MaterialImageKind::File || stage.imageKind == MaterialImageKind::CubeMap
			|| stage.imageKind == MaterialImageKind::Video) {
			add(stage.imagePath);
		}
		for (const QString& frame : stage.animationFrames) {
			add(frame);
		}
		if (stage.imageProgram >= 0) {
			QVector<int> pending {stage.imageProgram};
			while (!pending.isEmpty()) {
				const int index = pending.takeFirst();
				if (index < 0 || index >= imagePrograms.size()) {
					continue;
				}
				const MaterialImageProgramNode& node = imagePrograms.at(index);
				if (node.function.isEmpty()) {
					add(node.path);
				}
				for (int child : node.children) {
					pending.push_back(child);
				}
			}
		}
	}
	if (sky.present) {
		for (const QString& box : {sky.farBox, sky.nearBox}) {
			if (!box.isEmpty() && box != QStringLiteral("-")) {
				for (const QString& suffix : {QStringLiteral("_rt"), QStringLiteral("_bk"), QStringLiteral("_lf"),
						 QStringLiteral("_ft"), QStringLiteral("_up"), QStringLiteral("_dn")}) {
					add(box + suffix);
				}
			}
		}
	}
	add(lightFalloffImage);
	add(editorImage);
	for (const MaterialFrame& frame : classic.frames) {
		add(frame.name);
	}
	return images;
}

int MaterialDefinition::errorCount() const
{
	return static_cast<int>(std::count_if(diagnostics.cbegin(), diagnostics.cend(), [](const MaterialDiagnostic& diagnostic) {
		return diagnostic.severity == MaterialDiagnosticSeverity::Error;
	}));
}

int MaterialDefinition::warningCount() const
{
	return static_cast<int>(std::count_if(diagnostics.cbegin(), diagnostics.cend(), [](const MaterialDiagnostic& diagnostic) {
		return diagnostic.severity == MaterialDiagnosticSeverity::Warning;
	}));
}

int MaterialScript::indexOf(const QString& materialName) const
{
	const QString key = materialLookupKey(materialName);
	for (int index = 0; index < materials.size(); ++index) {
		if (materialLookupKey(materials.at(index).name) == key) {
			return index;
		}
	}
	return -1;
}

const MaterialDefinition* MaterialScript::find(const QString& materialName) const
{
	const int index = indexOf(materialName);
	return index < 0 ? nullptr : &materials.at(index);
}

const MaterialTable* MaterialScript::findTable(const QString& tableName) const
{
	const QString key = tableName.trimmed().toLower();
	for (const MaterialTable& table : tables) {
		if (table.name.toLower() == key) {
			return &table;
		}
	}
	return nullptr;
}

QVector<MaterialDiagnostic> MaterialScript::allDiagnostics() const
{
	QVector<MaterialDiagnostic> all = diagnostics;
	for (const MaterialDefinition& definition : materials) {
		all += definition.diagnostics;
	}
	return all;
}

QString materialLookupKey(const QString& name)
{
	QString key = name.trimmed();
	key.replace(QLatin1Char('\\'), QLatin1Char('/'));
	while (key.startsWith(QLatin1Char('/'))) {
		key.remove(0, 1);
	}
	return key.toCaseFolded();
}

QJsonObject materialDiagnosticJson(const MaterialDiagnostic& diagnostic)
{
	QJsonObject object;
	object.insert(QStringLiteral("severity"), materialDiagnosticSeverityId(diagnostic.severity));
	object.insert(QStringLiteral("code"), diagnostic.code);
	object.insert(QStringLiteral("message"), diagnostic.message);
	if (!diagnostic.material.isEmpty()) {
		object.insert(QStringLiteral("material"), diagnostic.material);
	}
	if (diagnostic.stage >= 0) {
		object.insert(QStringLiteral("stage"), diagnostic.stage);
	}
	object.insert(QStringLiteral("line"), diagnostic.line);
	object.insert(QStringLiteral("column"), diagnostic.column);
	return object;
}

QJsonObject materialDefinitionJson(const MaterialDefinition& definition, bool includeDirectives)
{
	QJsonObject object;
	object.insert(QStringLiteral("name"), definition.name);
	object.insert(QStringLiteral("engine"), materialEngineId(definition.engine));
	object.insert(QStringLiteral("generation"), materialEngineGeneration(definition.engine));
	object.insert(QStringLiteral("kind"), definition.kind);
	if (!definition.sourcePath.isEmpty()) {
		object.insert(QStringLiteral("sourcePath"), definition.sourcePath);
	}
	if (!definition.sourceLayer.isEmpty()) {
		object.insert(QStringLiteral("sourceLayer"), definition.sourceLayer);
	}
	if (definition.span.isValid()) {
		object.insert(QStringLiteral("span"), spanJson(definition.span));
	}
	object.insert(QStringLiteral("animated"), definition.isAnimated());
	object.insert(QStringLiteral("sky"), definition.isSky());
	object.insert(QStringLiteral("fog"), definition.isFog());
	object.insert(QStringLiteral("light"), definition.isLight());
	object.insert(QStringLiteral("usesLightmap"), definition.usesLightmap());
	object.insert(QStringLiteral("cull"), materialCullId(definition.cull));
	if (!definition.sort.isEmpty()) {
		object.insert(QStringLiteral("sort"), definition.sort);
	}
	if (!definition.surfaceParms.isEmpty()) {
		object.insert(QStringLiteral("surfaceParms"), stringsJson(definition.surfaceParms));
	}
	if (!definition.flags.isEmpty()) {
		object.insert(QStringLiteral("flags"), stringsJson(definition.flags));
	}
	if (!definition.editorImage.isEmpty()) {
		object.insert(QStringLiteral("editorImage"), definition.editorImage);
	}
	if (!definition.description.isEmpty()) {
		object.insert(QStringLiteral("description"), definition.description);
	}
	if (!definition.lightFalloffImage.isEmpty()) {
		object.insert(QStringLiteral("lightFalloffImage"), definition.lightFalloffImage);
	}
	if (definition.sky.present) {
		QJsonObject sky;
		sky.insert(QStringLiteral("farBox"), definition.sky.farBox);
		sky.insert(QStringLiteral("cloudHeight"), definition.sky.cloudHeight);
		sky.insert(QStringLiteral("nearBox"), definition.sky.nearBox);
		object.insert(QStringLiteral("skyParms"), sky);
	}
	if (definition.fog.present) {
		QJsonObject fog;
		fog.insert(QStringLiteral("color"), QJsonArray {definition.fog.color[0], definition.fog.color[1], definition.fog.color[2]});
		fog.insert(QStringLiteral("distanceToOpaque"), definition.fog.distanceToOpaque);
		object.insert(QStringLiteral("fogParms"), fog);
	}
	QJsonArray deforms;
	for (const MaterialDeform& deform : definition.deforms) {
		QJsonObject item;
		item.insert(QStringLiteral("kind"), materialDeformKindId(deform.kind));
		if (deform.kind == MaterialDeformKind::Wave || deform.kind == MaterialDeformKind::Move || deform.kind == MaterialDeformKind::Normal) {
			item.insert(QStringLiteral("wave"), waveJson(deform.wave));
		}
		if (deform.kind == MaterialDeformKind::Wave) {
			item.insert(QStringLiteral("spread"), deform.spread);
		}
		if (!deform.name.isEmpty()) {
			item.insert(QStringLiteral("name"), deform.name);
		}
		deforms.append(item);
	}
	if (!deforms.isEmpty()) {
		object.insert(QStringLiteral("deforms"), deforms);
	}
	QJsonArray stages;
	for (const MaterialStage& stage : definition.stages) {
		QJsonObject item;
		item.insert(QStringLiteral("index"), stage.index);
		item.insert(QStringLiteral("line"), stage.span.line);
		item.insert(QStringLiteral("role"), materialStageRoleId(stage.role));
		item.insert(QStringLiteral("image"), materialImageKindId(stage.imageKind));
		if (!stage.imagePath.isEmpty()) {
			item.insert(QStringLiteral("imagePath"), stage.imagePath);
		}
		if (!stage.animationFrames.isEmpty()) {
			item.insert(QStringLiteral("animationFrames"), stringsJson(stage.animationFrames));
			item.insert(QStringLiteral("animationFrequency"), stage.animationFrequency);
		}
		if (stage.imageProgram >= 0) {
			item.insert(QStringLiteral("imageProgram"), materialImageProgramText(definition.imagePrograms, stage.imageProgram));
		}
		item.insert(QStringLiteral("blend"), QJsonArray {materialBlendFactorId(stage.blend.source), materialBlendFactorId(stage.blend.destination)});
		if (definition.engine == MaterialEngine::Doom3) {
			QJsonArray colors;
			for (int expression : stage.colorExpressions) {
				colors.append(expression < 0 ? QStringLiteral("1") : materialExpressionText(definition.expressions, expression));
			}
			item.insert(QStringLiteral("color"), colors);
			if (stage.conditionExpression >= 0) {
				item.insert(QStringLiteral("condition"), materialExpressionText(definition.expressions, stage.conditionExpression));
			}
		} else {
			item.insert(QStringLiteral("rgbGen"), colorGenJson(stage.rgbGen, false));
			item.insert(QStringLiteral("alphaGen"), colorGenJson(stage.alphaGen, true));
		}
		item.insert(QStringLiteral("tcGen"), materialTexCoordSourceId(stage.tcGen.source));
		QJsonArray mods;
		for (const MaterialTexMod& mod : stage.tcMods) {
			QJsonObject modObject;
			modObject.insert(QStringLiteral("kind"), materialTexModKindId(mod.kind));
			if (mod.directive >= 0 && mod.directive < stage.directives.size()) {
				modObject.insert(QStringLiteral("arguments"), stage.directives.at(mod.directive).argumentText);
			}
			mods.append(modObject);
		}
		if (!mods.isEmpty()) {
			item.insert(QStringLiteral("tcMods"), mods);
		}
		if (stage.alphaTest != MaterialAlphaTest::None) {
			item.insert(QStringLiteral("alphaTest"), materialAlphaTestId(stage.alphaTest));
		}
		item.insert(QStringLiteral("depthWrite"), stage.depthWrite);
		if (stage.depthFunc == MaterialDepthFunc::Equal) {
			item.insert(QStringLiteral("depthFunc"), QStringLiteral("equal"));
		}
		if (stage.detail) {
			item.insert(QStringLiteral("detail"), true);
		}
		if (includeDirectives) {
			QJsonArray directives;
			for (const MaterialDirective& directive : stage.directives) {
				directives.append(directiveJson(directive));
			}
			item.insert(QStringLiteral("directives"), directives);
		}
		stages.append(item);
	}
	object.insert(QStringLiteral("stages"), stages);
	if (!definition.classic.kind.isEmpty()) {
		QJsonObject classic;
		classic.insert(QStringLiteral("kind"), definition.classic.kind);
		if (definition.classic.size.isValid()) {
			classic.insert(QStringLiteral("width"), definition.classic.size.width());
			classic.insert(QStringLiteral("height"), definition.classic.size.height());
		}
		if (!definition.classic.frames.isEmpty()) {
			QJsonArray frames;
			for (const MaterialFrame& frame : definition.classic.frames) {
				QJsonObject frameObject;
				frameObject.insert(QStringLiteral("name"), frame.name);
				frameObject.insert(QStringLiteral("duration"), frame.duration);
				if (frame.maximumDuration > frame.duration) {
					frameObject.insert(QStringLiteral("maximumDuration"), frame.maximumDuration);
				}
				frames.append(frameObject);
			}
			classic.insert(QStringLiteral("frames"), frames);
			classic.insert(QStringLiteral("animationSource"), definition.classic.animationSource);
		}
		if (!definition.classic.alternateFrames.isEmpty()) {
			QJsonArray frames;
			for (const MaterialFrame& frame : definition.classic.alternateFrames) {
				frames.append(frame.name);
			}
			classic.insert(QStringLiteral("alternateFrames"), frames);
		}
		if (!definition.classic.switchPartner.isEmpty()) {
			classic.insert(QStringLiteral("switchPartner"), definition.classic.switchPartner);
		}
		if (!definition.classic.patches.isEmpty()) {
			QJsonArray patches;
			for (const MaterialPatchPlacement& patch : definition.classic.patches) {
				patches.append(QJsonObject {{QStringLiteral("patch"), patch.patch}, {QStringLiteral("x"), patch.x}, {QStringLiteral("y"), patch.y}});
			}
			classic.insert(QStringLiteral("patches"), patches);
		}
		if (definition.classic.warp != MaterialWarpStyle::None) {
			classic.insert(QStringLiteral("warp"), materialWarpStyleId(definition.classic.warp));
		}
		if (definition.engine == MaterialEngine::Quake2) {
			classic.insert(QStringLiteral("surfaceFlags"), static_cast<qint64>(definition.classic.surfaceFlags));
			classic.insert(QStringLiteral("surfaceFlagIds"), stringsJson(quake2FlagIds(definition.classic.surfaceFlags, quake2SurfaceFlagDescriptors())));
			classic.insert(QStringLiteral("contentFlags"), static_cast<qint64>(definition.classic.contentFlags));
			classic.insert(QStringLiteral("contentFlagIds"), stringsJson(quake2FlagIds(definition.classic.contentFlags, quake2ContentFlagDescriptors())));
			classic.insert(QStringLiteral("value"), definition.classic.surfaceValue);
			if (!definition.classic.nextFrame.isEmpty()) {
				classic.insert(QStringLiteral("nextFrame"), definition.classic.nextFrame);
			}
		}
		if (definition.classic.masked) {
			classic.insert(QStringLiteral("masked"), true);
		}
		if (definition.classic.fullbrights) {
			classic.insert(QStringLiteral("fullbrights"), true);
		}
		if (!definition.classic.companions.isEmpty()) {
			classic.insert(QStringLiteral("companions"), stringsJson(definition.classic.companions));
		}
		object.insert(QStringLiteral("classic"), classic);
	}
	if (includeDirectives) {
		QJsonArray directives;
		for (const MaterialDirective& directive : definition.directives) {
			directives.append(directiveJson(directive));
		}
		object.insert(QStringLiteral("directives"), directives);
	}
	object.insert(QStringLiteral("images"), stringsJson(definition.imageReferences()));
	QJsonArray diagnostics;
	for (const MaterialDiagnostic& diagnostic : definition.diagnostics) {
		diagnostics.append(materialDiagnosticJson(diagnostic));
	}
	object.insert(QStringLiteral("diagnostics"), diagnostics);
	return object;
}

QJsonObject materialScriptJson(const MaterialScript& script, bool includeDirectives)
{
	QJsonObject object;
	object.insert(QStringLiteral("path"), script.path);
	object.insert(QStringLiteral("engine"), materialEngineId(script.engine));
	QJsonArray materials;
	for (const MaterialDefinition& definition : script.materials) {
		materials.append(materialDefinitionJson(definition, includeDirectives));
	}
	object.insert(QStringLiteral("materials"), materials);
	QJsonArray tables;
	for (const MaterialTable& table : script.tables) {
		QJsonObject item;
		item.insert(QStringLiteral("name"), table.name);
		item.insert(QStringLiteral("snap"), table.snap);
		item.insert(QStringLiteral("clamp"), table.clamp);
		item.insert(QStringLiteral("values"), table.values.size());
		item.insert(QStringLiteral("line"), table.span.line);
		tables.append(item);
	}
	if (!tables.isEmpty()) {
		object.insert(QStringLiteral("tables"), tables);
	}
	QJsonArray diagnostics;
	for (const MaterialDiagnostic& diagnostic : script.diagnostics) {
		diagnostics.append(materialDiagnosticJson(diagnostic));
	}
	object.insert(QStringLiteral("diagnostics"), diagnostics);
	return object;
}

QStringList materialDefinitionSummaryLines(const MaterialDefinition& definition)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioMaterials", "Material: %1").arg(definition.name);
	lines << QCoreApplication::translate("VibeStudioMaterials", "Engine: %1 (%2)")
				 .arg(materialEngineDisplayName(definition.engine), materialEngineGeneration(definition.engine));
	if (!definition.sourcePath.isEmpty() && definition.span.line > 0) {
		lines << QCoreApplication::translate("VibeStudioMaterials", "Source: %1, line %2").arg(definition.sourcePath).arg(definition.span.line);
	} else if (!definition.sourcePath.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioMaterials", "Source: %1").arg(definition.sourcePath);
	}
	QStringList traits;
	if (definition.isAnimated()) {
		traits << QCoreApplication::translate("VibeStudioMaterials", "animated");
	}
	if (definition.isSky()) {
		traits << QCoreApplication::translate("VibeStudioMaterials", "sky");
	}
	if (definition.isFog()) {
		traits << QCoreApplication::translate("VibeStudioMaterials", "fog");
	}
	if (definition.isLight()) {
		traits << QCoreApplication::translate("VibeStudioMaterials", "light");
	}
	if (definition.usesLightmap()) {
		traits << QCoreApplication::translate("VibeStudioMaterials", "lightmapped");
	}
	if (definition.cull == MaterialCull::None) {
		traits << QCoreApplication::translate("VibeStudioMaterials", "two-sided");
	}
	if (!traits.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioMaterials", "Traits: %1").arg(traits.join(QStringLiteral(", ")));
	}
	if (!definition.surfaceParms.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioMaterials", "Surface parameters: %1").arg(definition.surfaceParms.join(QStringLiteral(", ")));
	}
	const int stageCount = static_cast<int>(definition.stages.size());
	if (stageCount > 0) {
		lines << QCoreApplication::translate("VibeStudioMaterials", "%n stage(s)", nullptr, stageCount);
	}
	for (const MaterialStage& stage : definition.stages) {
		QString image = stage.imagePath;
		if (stage.imageKind == MaterialImageKind::Lightmap) {
			image = QStringLiteral("$lightmap");
		} else if (stage.imageKind == MaterialImageKind::Animation) {
			image = QCoreApplication::translate("VibeStudioMaterials", "animMap %1 Hz: %2")
						.arg(stage.animationFrequency)
						.arg(stage.animationFrames.join(QLatin1Char(' ')));
		} else if (stage.imageKind == MaterialImageKind::Program) {
			image = materialImageProgramText(definition.imagePrograms, stage.imageProgram);
		}
		lines << QCoreApplication::translate("VibeStudioMaterials", "  Stage %1 (%2): %3, blend %4 %5")
					 .arg(stage.index + 1)
					 .arg(materialStageRoleId(stage.role), image.isEmpty() ? QStringLiteral("-") : image,
						 materialBlendFactorId(stage.blend.source), materialBlendFactorId(stage.blend.destination));
	}
	if (!definition.classic.frames.isEmpty()) {
		QStringList names;
		for (const MaterialFrame& frame : definition.classic.frames) {
			names << frame.name;
		}
		lines << QCoreApplication::translate("VibeStudioMaterials", "Animation (%1): %2").arg(definition.classic.animationSource, names.join(QStringLiteral(" > ")));
	}
	if (!definition.classic.switchPartner.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioMaterials", "Switch partner: %1").arg(definition.classic.switchPartner);
	}
	const int patchCount = static_cast<int>(definition.classic.patches.size());
	if (patchCount > 0) {
		lines << QCoreApplication::translate("VibeStudioMaterials", "Composed from %n patch(es)", nullptr, patchCount);
	}
	if (definition.engine == MaterialEngine::Quake2) {
		const QStringList surface = quake2FlagIds(definition.classic.surfaceFlags, quake2SurfaceFlagDescriptors());
		lines << QCoreApplication::translate("VibeStudioMaterials", "Surface flags: %1").arg(surface.isEmpty() ? QStringLiteral("-") : surface.join(QStringLiteral(", ")));
		lines << QCoreApplication::translate("VibeStudioMaterials", "Value: %1").arg(definition.classic.surfaceValue);
	}
	const int errors = definition.errorCount();
	const int warnings = definition.warningCount();
	if (errors > 0 || warnings > 0) {
		lines << QCoreApplication::translate("VibeStudioMaterials", "Problems: %1 error(s), %2 warning(s)").arg(errors).arg(warnings);
	}
	return lines;
}

} // namespace vibestudio
