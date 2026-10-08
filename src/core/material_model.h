#pragma once

// Materials across idTech 1-4: one model for Doom textures and flats, Quake
// and Quake II textures, Quake III shader scripts and Doom 3 material decls.
//
// The model is source-preserving. Script materials keep the exact text span
// of every block and directive, so edits patch the original text instead of
// regenerating it, and comments and formatting survive. Semantics are decoded
// from those directives into the typed fields the evaluator and the renderer
// read; the directives themselves stay the source of truth.
//
// Keyword knowledge comes from public documentation and the GPL engine
// releases, credited in docs/CREDITS.md ("Materials"):
// - Quake III Arena Shader Manual (https://www.qeradiant.com/manual/Q3AShader_Manual/)
//   and id Software's Quake III Arena source, code/renderer/tr_shader.c.
// - Doom 3 material documentation (https://www.iddevnet.com/doom3/materials.html,
//   https://modwiki.dhewm3.org/Material_(decl)) and id Software's Doom 3 GPL
//   source, neo/renderer/Material.cpp.
// - Doom texture lumps and animations: the Doom Wiki and id Software's
//   linuxdoom-1.10 source (p_spec.c, p_switch.c, r_data.c).
// - Quake and Quake II texture conventions: the Quake specifications and id
//   Software's Quake and Quake II GPL sources.
//
// No game data is embedded; everything is read from packages the user owns.

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

namespace vibestudio {

enum class MaterialEngine {
	Unknown,
	Doom,   // idTech 1: Doom, Heretic, Hexen, Strife and the Boom/ZDoom ports.
	Quake,  // idTech 2: Quake, plus Half-Life WAD3 naming conventions.
	Quake2, // idTech 2: Quake II.
	Quake3, // idTech 3: Quake III Arena and its derivatives.
	Doom3,  // idTech 4: Doom 3, Quake 4, Prey.
};

QString materialEngineId(MaterialEngine engine);
QString materialEngineDisplayName(MaterialEngine engine);
// "idTech 1" .. "idTech 4".
QString materialEngineGeneration(MaterialEngine engine);
// Accepts the ids above plus idtech1..idtech4, q1/q2/q3, d3, doom2, heretic,
// hexen, quake1, quake3arena, quake4, prey and the like.
MaterialEngine materialEngineFromId(const QString& id);
QVector<MaterialEngine> materialEngines();

// A span of script text. Offsets are UTF-16 positions in the owning script's
// text; lines are 1-based. Synthetic materials have no span (start < 0).
struct MaterialSourceSpan {
	int line = 0;
	int endLine = 0;
	int start = -1;
	int end = -1;

	[[nodiscard]] bool isValid() const { return start >= 0 && end >= start; }
	[[nodiscard]] int length() const { return isValid() ? end - start : 0; }
};

enum class MaterialDiagnosticSeverity {
	Info,
	Warning,
	Error,
};

QString materialDiagnosticSeverityId(MaterialDiagnosticSeverity severity);

struct MaterialDiagnostic {
	MaterialDiagnosticSeverity severity = MaterialDiagnosticSeverity::Warning;
	// Stable, untranslated id such as "unknown-keyword" or "missing-image".
	QString code;
	QString message;
	QString material;
	int stage = -1;
	int line = 0;
	int column = 0;
	int length = 0;
};

// One keyword and its arguments, exactly as written.
struct MaterialDirective {
	QString keyword;
	// Arguments as tokens. Quake III splits on whitespace like COM_ParseExt;
	// Doom 3 keeps idLexer tokens, so `scroll time * 0.1, 0` has six.
	QStringList arguments;
	// The argument text exactly as written, without the keyword.
	QString argumentText;
	// From the keyword's first character to the last argument's end.
	MaterialSourceSpan span;
	// False when the engine does not know the keyword: it is kept and
	// reported, and the preview ignores it like the engine would.
	bool recognised = true;
	// True when the keyword is known but deliberately not simulated (vertex
	// programs, compiler-only q3map_ keys, editor hints).
	bool previewIgnored = false;
};

enum class MaterialBlendFactor {
	Zero,
	One,
	SourceColor,
	OneMinusSourceColor,
	DestinationColor,
	OneMinusDestinationColor,
	SourceAlpha,
	OneMinusSourceAlpha,
	DestinationAlpha,
	OneMinusDestinationAlpha,
	SourceAlphaSaturate,
};

QString materialBlendFactorId(MaterialBlendFactor factor);   // "GL_ONE" ...
bool materialBlendFactorFromId(const QString& id, MaterialBlendFactor* factor);

struct MaterialBlend {
	MaterialBlendFactor source = MaterialBlendFactor::One;
	MaterialBlendFactor destination = MaterialBlendFactor::Zero;
	// The keyword's own words, e.g. "add" or "GL_ONE GL_ONE".
	QString written;
	bool explicitBlend = false;

	[[nodiscard]] bool isOpaqueReplace() const
	{
		return source == MaterialBlendFactor::One && destination == MaterialBlendFactor::Zero;
	}
	[[nodiscard]] bool writesNothing() const
	{
		return source == MaterialBlendFactor::Zero && destination == MaterialBlendFactor::One;
	}
};

enum class MaterialWaveFunction {
	Sin,
	Triangle,
	Square,
	Sawtooth,
	InverseSawtooth,
	Noise,
};

QString materialWaveFunctionId(MaterialWaveFunction function);
bool materialWaveFunctionFromId(const QString& id, MaterialWaveFunction* function);

struct MaterialWave {
	MaterialWaveFunction function = MaterialWaveFunction::Sin;
	double base = 0.0;
	double amplitude = 0.0;
	double phase = 0.0;
	double frequency = 0.0;
};

// Quake III rgbGen/alphaGen sources. Doom 3 colours use expressions instead.
enum class MaterialColorSource {
	Identity,
	IdentityLighting,
	Constant,
	Wave,
	Entity,
	OneMinusEntity,
	Vertex,
	ExactVertex,
	OneMinusVertex,
	LightingDiffuse,
	LightingSpecular,
	Portal,
	Skip,
};

QString materialColorSourceId(MaterialColorSource source);

struct MaterialColorGen {
	MaterialColorSource source = MaterialColorSource::Identity;
	std::array<double, 3> constant {1.0, 1.0, 1.0};
	double constantAlpha = 1.0;
	MaterialWave wave;
	double portalRange = 256.0;
	bool explicitlySet = false;
};

enum class MaterialTexCoordSource {
	Base,
	Lightmap,
	Environment,
	Vector,
	// Doom 3 texgen.
	Normal,
	Reflect,
	Skybox,
	WobbleSky,
	Screen,
	Screen2,
	GlassWarp,
};

QString materialTexCoordSourceId(MaterialTexCoordSource source);

struct MaterialTexCoordGen {
	MaterialTexCoordSource source = MaterialTexCoordSource::Base;
	// tcGen vector ( sx sy sz ) ( tx ty tz ).
	std::array<double, 3> vectorS {1.0, 0.0, 0.0};
	std::array<double, 3> vectorT {0.0, 1.0, 0.0};
	// Doom 3 wobbleSky expressions, -1 when unused.
	std::array<int, 3> expressions {-1, -1, -1};
	bool explicitlySet = false;
};

enum class MaterialTexModKind {
	// Quake III tcMod.
	Scroll,
	Scale,
	Rotate,
	Stretch,
	Transform,
	Turbulent,
	EntityTranslate,
	// Doom 3 stage transforms (expression arguments).
	Translate, // also "scroll"
	ScaleExpression,
	CenterScale,
	Shear,
	RotateExpression,
};

QString materialTexModKindId(MaterialTexModKind kind);

struct MaterialTexMod {
	MaterialTexModKind kind = MaterialTexModKind::Scroll;
	// Quake III numbers: scroll s t, scale s t, rotate degPerSec, transform
	// m00 m01 m10 m11 t0 t1.
	std::array<double, 6> values {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	// stretch and turb.
	MaterialWave wave;
	// Doom 3 expression pool indices: two for translate/scale/centerScale/
	// shear, one for rotate. -1 when unused.
	std::array<int, 2> expressions {-1, -1};
	// The directive this came from, in the stage's directive list.
	int directive = -1;
};

enum class MaterialImageKind {
	None,
	File,
	Lightmap,     // $lightmap
	White,        // $whiteimage, _white
	Animation,    // Quake III animMap
	Video,        // videoMap
	CubeMap,      // cubeMap / cameraCubeMap
	RenderTarget, // remoteRenderMap, mirrorRenderMap, xrayRenderMap, _currentRender
	Builtin,      // Doom 3 _black, _flat, _quadratic, ...
	Program,      // Doom 3 image program
	SoundMap,
};

QString materialImageKindId(MaterialImageKind kind);

// Doom 3 image programs: heightmap(), addnormals(), scale() and friends.
// A leaf names an image; a call names its function with child nodes and
// trailing numbers.
struct MaterialImageProgramNode {
	QString function;      // empty for a plain image reference
	QString path;          // the image for a leaf
	QVector<int> children; // node indices
	QVector<double> numbers;
	MaterialSourceSpan span;
};

enum class MaterialStageRole {
	Regular,
	Diffuse,
	Bump,
	Specular,
};

QString materialStageRoleId(MaterialStageRole role);

enum class MaterialAlphaTest {
	None,
	Greater0,
	Less128,
	GreaterEqual128,
	Expression, // Doom 3 alphaTest <expr>
};

QString materialAlphaTestId(MaterialAlphaTest test);

enum class MaterialDepthFunc {
	LessEqual,
	Equal,
	Always,
};

enum class MaterialVertexColor {
	None,
	Vertex,
	InverseVertex,
};

struct MaterialStage {
	int index = 0;
	// From the stage's `{` to its `}` inclusive.
	MaterialSourceSpan span;
	QVector<MaterialDirective> directives;
	MaterialStageRole role = MaterialStageRole::Regular;

	MaterialImageKind imageKind = MaterialImageKind::None;
	// map, clampmap, Doom 3 map with a plain image, videoMap file, cubeMap base.
	QString imagePath;
	QStringList animationFrames;
	double animationFrequency = 0.0;
	int imageProgram = -1;
	bool clamp = false;
	bool zeroClamp = false;
	bool alphaZeroClamp = false;
	bool nearest = false;
	bool videoLoop = false;
	bool cameraCubeMap = false;

	MaterialBlend blend;
	MaterialColorGen rgbGen;
	MaterialColorGen alphaGen;
	// Doom 3 colour registers: red, green, blue, alpha. -1 means 1.0.
	std::array<int, 4> colorExpressions {-1, -1, -1, -1};
	MaterialVertexColor vertexColor = MaterialVertexColor::None;

	MaterialTexCoordGen tcGen;
	QVector<MaterialTexMod> tcMods;

	MaterialAlphaTest alphaTest = MaterialAlphaTest::None;
	int alphaTestExpression = -1;
	// Doom 3 `if <expr>`, -1 when the stage is unconditional.
	int conditionExpression = -1;

	MaterialDepthFunc depthFunc = MaterialDepthFunc::LessEqual;
	bool depthWrite = true;
	bool depthWriteExplicit = false;
	bool detail = false;

	bool maskRed = false;
	bool maskGreen = false;
	bool maskBlue = false;
	bool maskAlpha = false;
	bool maskDepth = false;
	bool ignoreAlphaTest = false;
	double privatePolygonOffset = 0.0;
	QString vertexProgram;
	QString fragmentProgram;
	// Doom 3 `diffusemap x` and friends make a stage from one global
	// directive: its index in the material's directives, else -1.
	int shorthandDirective = -1;
};

enum class MaterialCull {
	Front, // the default: only front faces are drawn
	Back,  // only back faces are drawn
	None,  // two-sided
};

QString materialCullId(MaterialCull cull);

enum class MaterialDeformKind {
	// Quake III deformVertexes.
	Wave,
	Normal,
	Bulge,
	Move,
	Autosprite,
	Autosprite2,
	ProjectionShadow,
	Text,
	// Doom 3 deform.
	Sprite,
	Tube,
	Flare,
	Expand,
	MoveExpression,
	Turbulent,
	EyeBall,
	Particle,
	Particle2,
};

QString materialDeformKindId(MaterialDeformKind kind);

struct MaterialDeform {
	MaterialDeformKind kind = MaterialDeformKind::Wave;
	MaterialWave wave;
	// Quake III wave: 1/div. Bulge: width, height, speed. Move: x, y, z.
	double spread = 0.0;
	std::array<double, 3> values {0.0, 0.0, 0.0};
	// Doom 3: turbulent table name, particle decl.
	QString name;
	// Doom 3 expression arguments: flare size, expand/move amount,
	// turbulent range, time offset and domain.
	std::array<int, 3> expressions {-1, -1, -1};
	int directive = -1;
};

struct MaterialSkyParms {
	bool present = false;
	QString farBox;  // "-" or empty for none
	double cloudHeight = 128.0;
	QString nearBox;
};

struct MaterialFogParms {
	bool present = false;
	std::array<double, 3> color {0.0, 0.0, 0.0};
	double distanceToOpaque = 512.0;
};

// Doom 3 expressions are kept as a node pool per material; stage and global
// fields refer to node indices.
enum class MaterialExpressionOp {
	Constant,
	Time,
	Parm,
	Global,
	Sound,
	FragmentPrograms,
	Table,
	Negate,
	Add,
	Subtract,
	Multiply,
	Divide,
	Modulo,
	Greater,
	GreaterEqual,
	Less,
	LessEqual,
	Equal,
	NotEqual,
	And,
	Or,
};

QString materialExpressionOpId(MaterialExpressionOp op);
// The operator's token: "+", "&&", ... Empty for terms.
QString materialExpressionOpToken(MaterialExpressionOp op);
// Doom 3 priority: 1 for * / %, 2 for + -, 3 for comparisons, 4 for && ||.
int materialExpressionOpPriority(MaterialExpressionOp op);

struct MaterialExpressionNode {
	MaterialExpressionOp op = MaterialExpressionOp::Constant;
	double value = 0.0;
	// Parm and Global register number.
	int index = 0;
	// Table name.
	QString name;
	// Operands: a for unary operators and table lookups, a and b for binary.
	int a = -1;
	int b = -1;
	// True when the source put this node in parentheses.
	bool parenthesised = false;
	MaterialSourceSpan span;
};

// Doom 3 `table` decls.
struct MaterialTable {
	QString name;
	bool snap = false;
	bool clamp = false;
	QVector<double> values;
	QString sourcePath;
	MaterialSourceSpan span;
	// True for the procedural stand-ins used when the game's tables.mtr is
	// not mounted.
	bool generated = false;
};

// What idTech 1 and 2 know about a texture: animations, switches, flags and
// composition. These engines have no material scripts; this is what the
// engine derives from names and lumps.
struct MaterialFrame {
	QString name;
	// Seconds this frame stays up.
	double duration = 0.0;
	// Doom ANIMDEFS rand: duration is the minimum, this the maximum.
	double maximumDuration = 0.0;
};

struct MaterialPatchPlacement {
	QString patch;
	int x = 0;
	int y = 0;
};

enum class MaterialWarpStyle {
	None,
	QuakeTurbulent,  // Quake and Quake II liquids
	QuakeSky,        // Quake two-layer scrolling sky
	DoomWarp,        // ZDoom ANIMDEFS warp
	DoomWarp2,       // ZDoom ANIMDEFS warp2
};

QString materialWarpStyleId(MaterialWarpStyle style);

struct MaterialClassicInfo {
	// Doom: "wall" or "flat"; Quake: "miptex"; Quake II: "wal".
	QString kind;
	QSize size;
	// Animation frames in play order, starting with this material's first
	// frame. Empty when the texture does not animate.
	QVector<MaterialFrame> frames;
	// Which rule supplied the animation: "doom-vanilla", "boom-animated",
	// "animdefs", "quake-name", "quake2-wal".
	QString animationSource;
	bool oscillate = false;
	// Quake alternate (+a..+j) sequence, used while an entity's frame is set.
	QVector<MaterialFrame> alternateFrames;
	// Doom switch partner (SW1 <-> SW2).
	QString switchPartner;
	QString switchSource;
	// Doom wall composition from TEXTURE1/TEXTURE2.
	QVector<MaterialPatchPlacement> patches;
	QString definitionLump;
	MaterialWarpStyle warp = MaterialWarpStyle::None;
	double warpSpeed = 1.0;
	// Quake II WAL header.
	quint32 surfaceFlags = 0;
	quint32 contentFlags = 0;
	qint32 surfaceValue = 0;
	QString nextFrame;
	// Quake { textures and Quake II/WAD3 index 255 holes.
	bool masked = false;
	// Quake: fullbright palette range applies (indices 224-255).
	bool fullbrights = false;
	// Quake source-port companion images (_glow, _luma, _norm, _gloss).
	QStringList companions;
};

// Quake II surface and content flags (qfiles.h / q_shared.h).
inline constexpr quint32 kQuake2SurfLight = 0x1;
inline constexpr quint32 kQuake2SurfSlick = 0x2;
inline constexpr quint32 kQuake2SurfSky = 0x4;
inline constexpr quint32 kQuake2SurfWarp = 0x8;
inline constexpr quint32 kQuake2SurfTrans33 = 0x10;
inline constexpr quint32 kQuake2SurfTrans66 = 0x20;
inline constexpr quint32 kQuake2SurfFlowing = 0x40;
inline constexpr quint32 kQuake2SurfNoDraw = 0x80;

struct MaterialFlagDescriptor {
	quint32 bit = 0;
	QString id;
	QString displayName;
	QString description;
};

QVector<MaterialFlagDescriptor> quake2SurfaceFlagDescriptors();
QVector<MaterialFlagDescriptor> quake2ContentFlagDescriptors();
QStringList quake2FlagIds(quint32 flags, const QVector<MaterialFlagDescriptor>& descriptors);

struct MaterialDefinition {
	QString name;
	MaterialEngine engine = MaterialEngine::Unknown;
	// "shader", "material", "implicit", "doom-wall", "doom-flat",
	// "quake-miptex", "quake2-wal", "guide".
	QString kind;
	QString sourcePath;
	QString sourceLayer;
	// The whole definition, from the name to the closing brace.
	MaterialSourceSpan span;
	MaterialSourceSpan nameSpan;
	// The body's braces.
	MaterialSourceSpan bodySpan;

	QVector<MaterialDirective> directives;
	QVector<MaterialStage> stages;
	QVector<MaterialExpressionNode> expressions;
	QVector<MaterialImageProgramNode> imagePrograms;

	MaterialCull cull = MaterialCull::Front;
	QString sort;
	double sortValue = 0.0;
	QStringList surfaceParms;
	QString editorImage;
	double editorTransparency = 0.0;
	QString description;
	QVector<MaterialDeform> deforms;
	MaterialSkyParms sky;
	MaterialFogParms fog;
	bool polygonOffset = false;
	double polygonOffsetValue = 0.0;
	bool portal = false;
	bool noPicMip = false;
	bool noMipMaps = false;
	double tessSize = 0.0;
	// Recognised boolean keywords such as "noshadows" or "translucent",
	// lower case.
	QStringList flags;
	// Doom 3 lights.
	QString lightFalloffImage;
	bool fogLight = false;
	bool blendLight = false;
	bool ambientLight = false;
	bool translucent = false;
	// Doom 3 coverage: "opaque", "perforated" or "translucent".
	QString coverage;
	QString guiSurface;
	QString decalInfo;
	QString surfaceType;

	MaterialClassicInfo classic;
	QVector<MaterialDiagnostic> diagnostics;
	// Why the engine would throw this definition away, empty when it would
	// not. Quake III drops a shader whose stage has an unknown keyword or a
	// missing image and draws its default shader instead; Doom 3 marks a
	// material with an unknown keyword as defaulted. The preview follows.
	QString engineRejection;
	// A Quake 4 guide instance: the template's name and its arguments.
	QString guideTemplate;
	QStringList guideArguments;

	[[nodiscard]] bool hasFlag(const QString& flag) const;
	// A Doom 3 light shader: its name starts with lights/ or fogs/, or it
	// declares a falloff image or a light kind.
	[[nodiscard]] bool isLight() const;
	[[nodiscard]] bool isSky() const;
	[[nodiscard]] bool isFog() const;
	[[nodiscard]] bool usesLightmap() const;
	// True when any stage, deform or classic rule changes over time.
	[[nodiscard]] bool isAnimated() const;
	// Every image path the material reads, in stage order, without
	// duplicates, including editor images and animation frames.
	[[nodiscard]] QStringList imageReferences() const;
	[[nodiscard]] int errorCount() const;
	[[nodiscard]] int warningCount() const;
};

struct MaterialScript {
	QString path;
	MaterialEngine engine = MaterialEngine::Unknown;
	QString text;
	QVector<MaterialDefinition> materials;
	QVector<MaterialTable> tables;
	// Script-level problems; per-material ones live on each definition.
	QVector<MaterialDiagnostic> diagnostics;

	[[nodiscard]] int indexOf(const QString& materialName) const;
	[[nodiscard]] const MaterialDefinition* find(const QString& materialName) const;
	[[nodiscard]] const MaterialTable* findTable(const QString& tableName) const;
	// Every diagnostic, script-level first, then each material's.
	[[nodiscard]] QVector<MaterialDiagnostic> allDiagnostics() const;
};

// Case-folded, slash-normalised lookup key for material and image names.
QString materialLookupKey(const QString& name);

QJsonObject materialDiagnosticJson(const MaterialDiagnostic& diagnostic);
QJsonObject materialDefinitionJson(const MaterialDefinition& definition, bool includeDirectives = true);
QJsonObject materialScriptJson(const MaterialScript& script, bool includeDirectives = true);
// A readable multi-line summary for the CLI and the detail drawer.
QStringList materialDefinitionSummaryLines(const MaterialDefinition& definition);

} // namespace vibestudio
