#include "core/model_targets.h"

#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_sidebar.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace vibestudio
{
namespace
{
// The limits below were read from each engine's own model loader and
// renderer; no engine code is copied. Each entry names its sources, which
// docs/MODEL_TARGETS.md and docs/CREDITS.md list with their licences.
struct TargetEntry
{
	const char *id;
	const char *format;
	const char *label;
	const char *description;
	bool isDefault;
	ModelTargetLimits limits;
	const char *sources;
};

ModelTargetLimits limits(int vertices, int renderVertices, int triangles, int frames, int skins, int skinWidth, int skinHeight,
						 qint64 skinPixels, int skinWidthMultiple)
{
	ModelTargetLimits result;
	result.vertices = vertices;
	result.renderVertices = renderVertices;
	result.triangles = triangles;
	result.frames = frames;
	result.skins = skins;
	result.skinWidth = skinWidth;
	result.skinHeight = skinHeight;
	result.skinPixels = skinPixels;
	result.skinWidthMultiple = skinWidthMultiple;
	return result;
}
// Per-surface vertices and triangles, and the detail levels loaded beside the
// model; 32 surfaces and 1,024 frames, as the format and the editable source.
ModelTargetLimits md3Limits(int vertices, int triangles, int detailLevels, int tagsPerFrame = 16)
{
	ModelTargetLimits result;
	result.vertices = vertices;
	result.triangles = triangles;
	result.surfaces = 32;
	result.frames = 1024;
	result.tagsPerFrame = tagsPerFrame;
	result.detailLevels = detailLevels;
	return result;
}
ModelTargetLimits quake3Limits() { return md3Limits(999, 1999, 2); }
ModelTargetLimits skeletalLimits(int joints, int vertices, int triangles, int influences = 0)
{
	ModelTargetLimits result;
	result.joints = joints;
	result.vertices = vertices;
	result.triangles = triangles;
	result.influences = influences;
	return result;
}
// Q2PRO's MD5 replacement models: per mesh, TESS_MAX_VERTICES vertices and
// triangles and MD5_MAX_WEIGHTS weights; an animation, MD5_MAX_FRAMES frames.
ModelTargetLimits q2proMd5Limits(bool animation)
{
	auto result = skeletalLimits(256, animation ? 0 : 6144, animation ? 0 : 6144);
	if (animation)
	{
		result.frames = 1024;
	}
	else
	{
		result.surfaces = 32;
		result.weights = 8192;
	}
	return result;
}

const TargetEntry kTargets[] = {
	{"quake", "mdl", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Quake (id Software)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "WinQuake, GLQuake and QuakeWorld as released. GLQuake's limits are the stricter ones, so "
												 "they apply: 1,024 vertices, 2,048 triangles, 256 poses and 32 skins of at most 640 by 480 "
												 "pixels, with a width that is a multiple of four for the software renderer."),
	 true, limits(1024, 0, 2048, 256, 32, 0, 480, 640 * 480, 4),
	 "https://github.com/id-Software/Quake (master, reviewed 2026-10-22): WinQuake/gl_model.h MAXALIASVERTS, MAXALIASTRIS, "
	 "MAXALIASFRAMES, MAX_SKINS; WinQuake/gl_model.c and model.c Mod_LoadAliasModel (MAX_LBM_HEIGHT, skin width multiple of 4); "
	 "WinQuake/gl_draw.c GL_LoadTexture (640*480 texture buffer)"},
	{"quakespasm", "mdl", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "QuakeSpasm"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "QuakeSpasm raises the vertex, triangle and pose limits to 2,400, 4,096 and 1,024. A skin "
												 "taller than 480 pixels only warns, and the skin width needs no multiple of four."),
	 false, limits(2400, 0, 4096, 1024, 32, 0, 0, 0, 0),
	 "https://github.com/sezero/quakespasm (master, reviewed 2026-10-22): Quake/gl_model.h MAXALIASVERTS, MAXALIASTRIS, "
	 "MAXALIASFRAMES, MAX_SKINS; Quake/gl_model.c Mod_LoadAliasModel and Mod_LoadAllSkins"},
	{"fte", "mdl", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "FTE QuakeWorld"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "FTE allocates MDL data as it loads, with no fixed count limits. It draws a copy of every "
												 "seam vertex, and builds with 16-bit indices hold 65,535 drawn vertices."),
	 false, limits(0, 65535, 0, 0, 0, 0, 0, 0, 0),
	 "https://github.com/fte-team/fteqw (master snapshot, reviewed 2026-10-22): engine/common/com_mesh.c Mod_LoadQ1Model "
	 "(numverts + onseams); engine/client/render.h index_t"},
	{"darkplaces", "mdl", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "DarkPlaces"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "DarkPlaces refuses vertex, triangle, pose and skin counts and skin sizes of 65,536 or more."),
	 false, limits(65535, 0, 65535, 65535, 65535, 65535, 65535, 0, 0),
	 "https://github.com/DarkPlacesEngine/DarkPlaces (master, reviewed 2026-10-22): model_alias.c Mod_IDP0_Load (BOUNDI checks)"},
	{"quake2", "md2", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Quake II (id Software)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "The released Quake II renderers; Yamagi Quake II keeps the same limits: 2,048 positions, "
												 "4,096 triangles, 512 frames and 32 skins of at most 640 by 480 pixels."),
	 true, limits(2048, 0, 4096, 512, 32, 640, 480, 0, 0),
	 "https://github.com/id-Software/Quake-2 (master, reviewed 2026-10-22): qcommon/qfiles.h MAX_VERTS, MAX_TRIANGLES, MAX_FRAMES, "
	 "MAX_MD2SKINS; ref_gl/gl_model.c and ref_soft/r_model.c Mod_LoadAliasModel; ref_gl/gl_image.c LoadPCX (width below 640) | "
	 "https://github.com/yquake2/yquake2 (master, reviewed 2026-10-22): src/common/header/files.h, src/client/refresh/files/models.c"},
	{"q2pro", "md2", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Q2PRO"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Q2PRO allows 1,024 frames and 6,144 triangles, and builds one drawn vertex for each "
												 "distinct position and texel pair, at most 6,144. Builds from before July 2024 hold 4,096 "
												 "of each."),
	 false, limits(2048, 6144, 6144, 1024, 32, 640, 480, 0, 0),
	 "https://github.com/q2pro/q2pro (preserved upstream, 601a8df, 2025-12-11, reviewed 2026-10-22): inc/format/md2.h MD2_MAX_*; "
	 "src/refresh/models.c MOD_ValidateMD2 and MOD_LoadMD2; src/refresh/gl.h TESS_MAX_VERTICES and TESS_MAX_INDICES (raised from 4096 "
	 "in 0dc71b8, 2024-07-21)"},
	{"quake3", "md3", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Quake III Arena (id Software)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "R_LoadMD3 accepts 1,000 vertices and 2,000 triangles per surface, but RB_CheckOverflow drops "
												 "the game when a surface reaches either, so 999 and 1,999 are the working limits. Two detail "
												 "levels, name_1 and name_2, load beside the model."),
	 true, quake3Limits(),
	 "https://github.com/id-Software/Quake-III-Arena (master, reviewed 2026-10-22): code/qcommon/qfiles.h SHADER_MAX_VERTEXES, "
	 "SHADER_MAX_INDEXES, MD3_MAX_LODS, MD3_MAX_*; code/renderer/tr_model.c R_LoadMD3 and R_RegisterModel; code/renderer/tr_surface.c "
	 "RB_CheckOverflow and RB_SurfaceMesh"},
	{"ioquake3", "md3", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "ioquake3 and Quake3e"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "R_LoadMD3 refuses a surface with 1,000 vertices or 2,000 triangles; the limits match the "
												 "working limits of the original game. Two detail levels load beside the model."),
	 false, quake3Limits(),
	 "https://github.com/ioquake/ioq3 (main, reviewed 2026-10-22): code/renderergl1/tr_model.c R_LoadMD3 and R_RegisterMD3 | "
	 "https://github.com/ec-/Quake3e (master, reviewed 2026-10-22): code/qcommon/qfiles.h, code/renderer/tr_model.c R_LoadMD3"},
	{"rtcw", "md3", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Return to Castle Wolfenstein (single player)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "The single-player renderer holds 4,000 vertices and 24,000 indexes per batch and drops "
												 "the game when one surface reaches either, so 3,999 vertices and 7,999 triangles are the "
												 "working limits. Two detail levels load beside the model. The game loads name.mdc before "
												 "name.md3 when both exist, so remove a stale MDC."),
	 false, md3Limits(3999, 7999, 2),
	 "https://github.com/id-Software/RTCW-SP (master, reviewed 2026-10-22): src/qcommon/qfiles.h SHADER_MAX_VERTEXES, "
	 "SHADER_MAX_INDEXES, MD3_MAX_LODS; src/renderer/tr_model.c R_RegisterModel, R_LoadMD3 and R_LoadMDC; src/renderer/tr_surface.c "
	 "RB_CheckOverflow"},
	{"rtcw-mp", "md3", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Return to Castle Wolfenstein (multiplayer)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "The multiplayer renderer keeps Quake III's 1,000-vertex batches, so 999 vertices and 1,999 "
												 "triangles per surface, and loads no detail levels (MD3_MAX_LODS is 1). It loads name.mdc "
												 "before name.md3 when both exist."),
	 false, md3Limits(999, 1999, 0),
	 "https://github.com/id-Software/RTCW-MP (master, reviewed 2026-10-22): src/qcommon/qfiles.h SHADER_MAX_VERTEXES, "
	 "SHADER_MAX_INDEXES, MD3_MAX_LODS; src/renderer/tr_model.c R_RegisterModel, R_LoadMD3 and R_LoadMDC; src/renderer/tr_surface.c "
	 "RB_CheckOverflow"},
	{"et", "md3", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Wolfenstein: Enemy Territory"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Enemy Territory holds 1,025 vertices and 6,150 indexes per batch and drops the game when "
												 "one surface reaches either, so 1,024 vertices and 2,049 triangles are the working limits. "
												 "Three detail levels, name_1 to name_3, load beside the model, and name.mdc loads before "
												 "name.md3 when both exist."),
	 false, md3Limits(1024, 2049, 3),
	 "https://github.com/id-Software/Enemy-Territory (master, reviewed 2026-10-22): src/qcommon/qfiles.h SHADER_MAX_VERTEXES, "
	 "SHADER_MAX_INDEXES, MD3_MAX_LODS; src/renderer/tr_init.c R_Init (tess.maxShaderVerts); src/renderer/tr_model.c R_RegisterModel, "
	 "R_LoadMD3 and R_LoadMDC; src/renderer/tr_surface.c RB_CheckOverflow"},
	{"q2pro", "md3", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Q2PRO"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Q2PRO loads MD3 models in place of Quake II's MD2: 6,144 vertices and triangles per "
												 "surface, 32 surfaces and 1,024 frames. It reads no attachment tags and loads no detail "
												 "levels."),
	 false, md3Limits(6144, 6144, 0, 0),
	 "https://github.com/q2pro/q2pro (preserved upstream, 601a8df, 2025-12-11, reviewed 2026-10-22): inc/format/md3.h MD3_MAX_*; "
	 "src/refresh/models.c MOD_ValidateMD3Mesh, MOD_ValidateMD3 and MOD_LoadMD3; src/refresh/gl.h TESS_MAX_VERTICES"},
	{"gzdoom", "md3", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "GZDoom (MODELDEF)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "GZDoom allocates MD3 data as it loads, with no vertex or triangle limit of its own. "
												 "MODELDEF's SurfaceSkin addresses surfaces 0 to 31, and MODELDEF maps sprite frames to "
												 "model frames; the studio does not write MODELDEF. No detail levels load."),
	 false, md3Limits(0, 0, 0, 0),
	 "https://github.com/ZDoom/gzdoom (master, c26ce2e, 2026-08-10, reviewed 2026-10-22): src/common/models/models_md3.cpp "
	 "FMD3Model::Load; src/common/models/model.h MD3_MAX_SURFACES; src/r_data/models.cpp ParseModelDefLump (SurfaceSkin)"},
	{"iqm", "iqm", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "IQM format (any engine)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "The format as the writer stores it: one-byte blend indexes, so at most 256 joints. Engines "
												 "can set lower limits; choose one of them when you know the game."),
	 true, skeletalLimits(256, 0, 0, 4), "https://github.com/lsalzman/iqm (master, reviewed 2026-10-22): iqm.txt, iqm.h (IQM_UBYTE blend indexes)"},
	{"ioquake3", "iqm", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "ioquake3 and Quake3e"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "R_LoadIQM refuses more than 128 joints, and a mesh with 1,000 vertices or 2,000 triangles."),
	 false, skeletalLimits(128, 999, 1999, 4),
	 "https://github.com/ioquake/ioq3 (main, reviewed 2026-10-22): code/renderercommon/iqm.h IQM_MAX_JOINTS; "
	 "code/renderergl1/tr_model_iqm.c R_LoadIQM | https://github.com/ec-/Quake3e (master, reviewed 2026-10-22): code/renderer/iqm.h, "
	 "code/renderer/tr_model_iqm.c R_LoadIQM"},
	{"fte", "iqm", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "FTE QuakeWorld"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "FTE loads at most 256 joints (MAX_BONES), and builds with 16-bit indices hold 65,535 "
												 "vertices per mesh."),
	 false, skeletalLimits(256, 65535, 0, 4),
	 "https://github.com/fte-team/fteqw (master snapshot, reviewed 2026-10-22): engine/gl/gl_model.h MAX_BONES; engine/common/com_mesh.c "
	 "IQM loader; engine/client/render.h index_t"},
	{"gzdoom", "iqm", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "GZDoom (MODELDEF)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "GZDoom reads one-byte blend indexes and weights, so 256 joints and four influences per "
												 "vertex, and sets no vertex or triangle limit of its own."),
	 false, skeletalLimits(256, 0, 0, 4),
	 "https://github.com/ZDoom/gzdoom (master, c26ce2e, 2026-08-10, reviewed 2026-10-22): src/common/models/models_iqm.cpp "
	 "IQMModel::Load, LoadBlendIndexes and LoadBlendWeights"},
	{"doom3", "md5mesh", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Doom 3 (id Software)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "The released Doom 3 loader allocates joints, meshes and weights as it reads them and sets "
												 "no count limits."),
	 true, ModelTargetLimits{},
	 "https://github.com/id-Software/DOOM-3 (GPL release, reviewed 2026-10-22): neo/renderer/Model_md5.cpp idRenderModelMD5::LoadModel"},
	{"q2pro", "md5mesh", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Q2PRO"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Q2PRO draws md5/name.md5mesh with md5/name.md5anim in place of an MD2 or MD3 of the same "
												 "name, when both files exist: at most 256 joints, 32 meshes, and 6,144 vertices, 6,144 "
												 "triangles and 8,192 weights per mesh. Skins come from the MD2 or MD3, under md5/."),
	 false, q2proMd5Limits(false),
	 "https://github.com/q2pro/q2pro (preserved upstream, 601a8df, 2025-12-11, reviewed 2026-10-22): src/refresh/gl.h MD5_MAX_* and "
	 "TESS_MAX_VERTICES; src/refresh/models.c MD5_ParseMesh, MOD_LoadMD5 and MD5_LoadSkins"},
	{"doom3", "md5anim", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Doom 3 (id Software)"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "The released Doom 3 loader allocates frames and joints as it reads them and sets no count "
												 "limits."),
	 true, ModelTargetLimits{}, "https://github.com/id-Software/DOOM-3 (GPL release, reviewed 2026-10-22): neo/game/anim/Anim.cpp idMD5Anim::LoadAnim"},
	{"q2pro", "md5anim", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Q2PRO"),
	 QT_TRANSLATE_NOOP("VibeStudioModelTargets", "Q2PRO reads 1 to 1,024 frames and the mesh's joints, at most 256. It warns when the "
												 "animation has fewer frames than the MD2 or MD3 it replaces."),
	 false, q2proMd5Limits(true),
	 "https://github.com/q2pro/q2pro (preserved upstream, 601a8df, 2025-12-11, reviewed 2026-10-22): src/refresh/gl.h MD5_MAX_FRAMES and "
	 "MD5_MAX_JOINTS; src/refresh/models.c MD5_ParseAnim"},
};

ModelTargetProfile profileFrom(const TargetEntry &entry)
{
	ModelTargetProfile profile;
	profile.id = QString::fromLatin1(entry.id);
	profile.format = QString::fromLatin1(entry.format);
	profile.label = QCoreApplication::translate("VibeStudioModelTargets", entry.label);
	profile.description = QCoreApplication::translate("VibeStudioModelTargets", entry.description);
	profile.isDefault = entry.isDefault;
	profile.limits = entry.limits;
	profile.sources = QString::fromLatin1(entry.sources).split(QStringLiteral(" | "), Qt::SkipEmptyParts);
	return profile;
}

bool printableName(const QString &value, int limit, bool allowEmpty = false)
{
	return (allowEmpty || !value.isEmpty()) && value.size() < limit &&
		   std::all_of(value.cbegin(), value.cend(), [](QChar c) { return c.unicode() >= 32 && c.unicode() < 127; });
}

// The first few names, then how many more there are.
QString listed(const QStringList &names, int shown = 6)
{
	if (names.size() <= shown)
	{
		return names.join(QStringLiteral(", "));
	}
	const int more = int(names.size()) - shown;
	return QCoreApplication::translate("VibeStudioModelTargets", "%1 and %n more", nullptr, more).arg(names.mid(0, shown).join(QStringLiteral(", ")));
}

// A Mesh Editor sidebar page by its neutral (studio) title, for fixes.
QString page(const char *id) { return modelSidebarTabTitle(QString::fromLatin1(id), QStringLiteral("studio")); }

QString formatName(const QString &format)
{
	return format == QStringLiteral("md5mesh")	 ? QCoreApplication::translate("VibeStudioModelTargets", "MD5 mesh")
		   : format == QStringLiteral("md5anim") ? QCoreApplication::translate("VibeStudioModelTargets", "MD5 animation")
												 : format.toUpper();
}

class Assessor
{
  public:
	Assessor(const ModelMesh &mesh, const ModelTargetProfile &profile, ModelTargetReport *report, ModelWorkProgress &work,
			 const ModelWorkControl &control)
		: m_mesh(mesh), m_profile(profile), m_limits(profile.limits), m_report(report), m_work(work), m_control(control)
	{
	}
	ModelTargetIssue &issue(const QString &limit, const QString &element, int surface, qint64 value, qint64 maximum, const QString &message,
							const QString &fix)
	{
		m_report->issues.append({limit, element, surface, value, maximum, message, fix});
		return m_report->issues.last();
	}
	// Keeps the largest value per limit; the issue, if any, comes separately.
	void measure(const QString &limit, const QString &element, qint64 value, qint64 maximum, bool upperBound = false)
	{
		if (maximum <= 0)
		{
			return;
		}
		for (auto &existing : m_report->measures)
		{
			if (existing.limit == limit)
			{
				if (value > existing.value)
				{
					existing = {limit, element, value, maximum, upperBound};
				}
				return;
			}
		}
		m_report->measures.append({limit, element, value, maximum, upperBound});
	}
	// A whole-model count against its limit.
	void count(const QString &limit, qint64 value, qint64 maximum, const QString &message, const QString &fix)
	{
		measure(limit, {}, value, maximum);
		if (maximum > 0 && value > maximum)
		{
			issue(limit, {}, -1, value, maximum, message, fix);
		}
	}
	QString label() const { return m_profile.label; }
	const ModelTargetLimits &limits() const { return m_limits; }
	bool step() { return m_work.step(); }
	// Limits this depth leaves unchecked.
	void unchecked(const QStringList &limits)
	{
		for (const auto &limit : limits)
		{
			if (!m_report->unchecked.contains(limit))
			{
				m_report->unchecked << limit;
			}
		}
	}

	void skinSize(QSize size)
	{
		const auto fix = QCoreApplication::translate("VibeStudioModelTargets", "Resize the skin images to fit.");
		const int width = size.width(), height = size.height();
		if (m_limits.skinWidth > 0)
		{
			measure(QStringLiteral("skin-width"), {}, width, m_limits.skinWidth);
			if (width > m_limits.skinWidth)
			{
				issue(QStringLiteral("skin-width"), {}, -1, width, m_limits.skinWidth,
					  QCoreApplication::translate("VibeStudioModelTargets", "The skin is %1 pixels wide; %2 allows at most %3.")
						  .arg(width)
						  .arg(label())
						  .arg(m_limits.skinWidth),
					  fix);
			}
		}
		if (m_limits.skinHeight > 0)
		{
			measure(QStringLiteral("skin-height"), {}, height, m_limits.skinHeight);
			if (height > m_limits.skinHeight)
			{
				issue(QStringLiteral("skin-height"), {}, -1, height, m_limits.skinHeight,
					  QCoreApplication::translate("VibeStudioModelTargets", "The skin is %1 pixels tall; %2 allows at most %3.")
						  .arg(height)
						  .arg(label())
						  .arg(m_limits.skinHeight),
					  fix);
			}
		}
		const qint64 pixels = qint64(width) * height;
		if (m_limits.skinPixels > 0)
		{
			measure(QStringLiteral("skin-pixels"), {}, pixels, m_limits.skinPixels);
			if (pixels > m_limits.skinPixels)
			{
				issue(QStringLiteral("skin-pixels"), {}, -1, pixels, m_limits.skinPixels,
					  QCoreApplication::translate("VibeStudioModelTargets", "The skin has %1 pixels; %2 loads at most %3 per image.")
						  .arg(pixels)
						  .arg(label())
						  .arg(m_limits.skinPixels),
					  fix);
			}
		}
		if (m_limits.skinWidthMultiple > 1 && width % m_limits.skinWidthMultiple != 0)
		{
			issue(QStringLiteral("skin-width-multiple"), {}, -1, width, m_limits.skinWidthMultiple,
				  QCoreApplication::translate("VibeStudioModelTargets", "The skin is %1 pixels wide; %2 needs a width that is a multiple of %3.")
					  .arg(width)
					  .arg(label())
					  .arg(m_limits.skinWidthMultiple),
				  fix);
		}
	}

	void frameNames()
	{
		QStringList names;
		for (const auto &frame : m_mesh.frames)
		{
			if (!printableName(frame.name, 16, true))
			{
				names << frame.name;
			}
		}
		if (!names.isEmpty())
		{
			const int bad = int(names.size());
			issue(QStringLiteral("frame-names"), {}, -1, bad, 0,
				  QCoreApplication::translate("VibeStudioModelTargets", "Frame names that do not fit 15 printable ASCII characters (%n): %1.", nullptr, bad)
					  .arg(listed(names)),
				  QCoreApplication::translate("VibeStudioModelTargets", "Rename the frames with Rename Frame on the %1 page.").arg(page("animation")));
		}
	}

	void frames()
	{
		const int frames = int(m_mesh.frames.size());
		count(QStringLiteral("frames"), frames, m_limits.frames,
			  QCoreApplication::translate("VibeStudioModelTargets", "The model has %1 frames; %2 allows at most %3.")
				  .arg(frames)
				  .arg(label())
				  .arg(m_limits.frames),
			  QCoreApplication::translate("VibeStudioModelTargets", "Delete frames with Delete Frame on the %1 page, or split the animation into separate models.").arg(page("animation")));
	}

	void singleSurface()
	{
		const int surfaces = int(m_mesh.surfaces.size());
		if (surfaces != 1)
		{
			issue(QStringLiteral("single-surface"), {}, -1, surfaces, 1,
				  QCoreApplication::translate("VibeStudioModelTargets", "%1 holds one surface; the model has %2.")
					  .arg(formatName(m_profile.format))
					  .arg(surfaces),
				  QCoreApplication::translate("VibeStudioModelTargets", "Join the surfaces with Manage Surfaces… > Join surfaces on the %1 page, or export MD3.").arg(page("surface")));
		}
	}

	void noTags()
	{
		QSet<QString> names;
		for (const auto &tag : m_mesh.tags)
		{
			names.insert(tag.name);
		}
		if (!names.isEmpty())
		{
			const int tags = int(names.size());
			issue(QStringLiteral("tags"), {}, -1, tags, 0,
				  QCoreApplication::translate("VibeStudioModelTargets", "%1 has no attachment tags; the model has %n.", nullptr, tags)
					  .arg(formatName(m_profile.format)),
				  QCoreApplication::translate("VibeStudioModelTargets", "Delete the tags under Attachment tags on the %1 page, or export MD3 to keep them.").arg(page("animation")));
		}
	}

	void noEmbeddedSkins()
	{
		const int skins = int(m_mesh.embeddedSkins.size());
		if (skins > 0)
		{
			issue(QStringLiteral("embedded-skins"), {}, -1, skins, 0,
				  QCoreApplication::translate("VibeStudioModelTargets", "%1 names skins by path and cannot embed the model's %n skin image(s).",
											  nullptr, skins)
					  .arg(formatName(m_profile.format)),
				  QCoreApplication::translate("VibeStudioModelTargets",
											  "Export the skins as textures and assign their paths as materials."));
		}
	}

	// UVs outside the tile, for the formats that store texels.
	bool uvRange(const ModelSurface &surface)
	{
		QVector<int> vertices;
		for (int v = 0; v < surface.texCoords.size(); ++v)
		{
			if (!step())
			{
				return false;
			}
			const auto uv = surface.texCoords[v];
			if (uv.u < 0 || uv.u > 1 || uv.v < 0 || uv.v > 1)
			{
				vertices << v;
			}
		}
		const int outside = int(vertices.size());
		if (outside > 0)
		{
			issue(QStringLiteral("uv-range"), surface.name, 0, outside, 0,
				  QCoreApplication::translate("VibeStudioModelTargets", "UV vertices outside the 0–1 skin tile, which %1 stores as texels: %n.",
											  nullptr, outside)
					  .arg(formatName(m_profile.format)),
				  QCoreApplication::translate("VibeStudioModelTargets", "Move or scale the UVs into the tile."))
				.vertices = std::move(vertices);
		}
		return true;
	}

	// An upper bound over the limit is only measured: the exact count, which
	// a full assessment makes, may still fit.
	void storedVertices(const QString &element, int value, bool upperBound, const QString &message, const QString &fix)
	{
		measure(QStringLiteral("vertices"), element, value, m_limits.vertices, upperBound);
		if (!upperBound && m_limits.vertices > 0 && value > m_limits.vertices)
		{
			issue(QStringLiteral("vertices"), element, 0, value, m_limits.vertices, message, fix);
		}
	}
	void renderVertices(int value, bool upperBound, const QString &message)
	{
		measure(QStringLiteral("render-vertices"), {}, value, m_limits.renderVertices, upperBound);
		if (!upperBound && m_limits.renderVertices > 0 && value > m_limits.renderVertices)
		{
			issue(QStringLiteral("render-vertices"), {}, 0, value, m_limits.renderVertices, message,
				  QCoreApplication::translate("VibeStudioModelTargets", "Reduce the geometry with Mesh > Clean Up > Decimate, or weld UV seams with Weld by Distance."));
		}
	}

	// Per-surface vertex and triangle counts (MD3, IQM, MD5). Each issue names
	// the faces past the budget: those a split would move to new surfaces.
	bool surfaceCounts(int index)
	{
		const auto &surface = m_mesh.surfaces.at(index);
		const int vertices = surface.vertexCount, triangles = int(surface.triangles.size());
		const auto fix =
			QCoreApplication::translate("VibeStudioModelTargets", "Split the surface with Manage Surfaces… > Separate selected faces on the %1 page, or reduce it with Mesh > Clean Up > Decimate. Prepare for Target does either.")
						 .arg(page("surface"));
		measure(QStringLiteral("vertices"), surface.name, vertices, m_limits.vertices);
		const bool overVertices = m_limits.vertices > 0 && vertices > m_limits.vertices;
		const bool overTriangles = m_limits.triangles > 0 && triangles > m_limits.triangles;
		QVector<int> past;
		if (overVertices || overTriangles)
		{
			QVector<int> parts;
			int count = 0;
			// Only cancellation stops the check; a face too large for the budget
			// leaves the issue without its faces.
			if (!modelTargetSurfaceParts(surface, m_limits.vertices, m_limits.triangles, &parts, &count, nullptr, m_control) && !m_work.check())
			{
				return false;
			}
			for (int face = 0; face < parts.size(); ++face)
			{
				if (parts[face] > 0)
				{
					past << face;
				}
			}
		}
		if (overVertices)
		{
			issue(QStringLiteral("vertices"), surface.name, index, vertices, m_limits.vertices,
				  QCoreApplication::translate("VibeStudioModelTargets", "Surface \"%1\" has %2 vertices; %3 allows at most %4 per surface.")
					  .arg(surface.name)
					  .arg(vertices)
					  .arg(label())
					  .arg(m_limits.vertices),
				  fix)
				.faces = past;
		}
		measure(QStringLiteral("triangles"), surface.name, triangles, m_limits.triangles);
		if (overTriangles)
		{
			issue(QStringLiteral("triangles"), surface.name, index, triangles, m_limits.triangles,
				  QCoreApplication::translate("VibeStudioModelTargets", "Surface \"%1\" has %2 triangles; %3 allows at most %4 per surface.")
					  .arg(surface.name)
					  .arg(triangles)
					  .arg(label())
					  .arg(m_limits.triangles),
				  fix)
				.faces = past;
		}
		return true;
	}

	void modelTriangles()
	{
		const int triangles = m_mesh.surfaces.isEmpty() ? 0 : int(m_mesh.surfaces.first().triangles.size());
		count(QStringLiteral("triangles"), triangles, m_limits.triangles,
			  QCoreApplication::translate("VibeStudioModelTargets", "The model has %1 triangles; %2 allows at most %3.")
				  .arg(triangles)
				  .arg(label())
				  .arg(m_limits.triangles),
			  QCoreApplication::translate("VibeStudioModelTargets", "Reduce the model with Mesh > Clean Up > Decimate."));
	}

  private:
	const ModelMesh &m_mesh;
	const ModelTargetProfile &m_profile;
	const ModelTargetLimits &m_limits;
	ModelTargetReport *m_report;
	ModelWorkProgress &m_work;
	const ModelWorkControl &m_control;
};

bool assessMdl(const ModelMesh &mesh, Assessor &a, ModelTargetDepth depth, QString *error, const ModelWorkControl &control)
{
	if (!mesh.mdl.enabled || mesh.embeddedSkins.isEmpty())
	{
		a.issue(QStringLiteral("mdl-skin"), {}, -1, 0, 0, QCoreApplication::translate("VibeStudioModelTargets", "MDL needs an indexed skin and its palette."),
				QCoreApplication::translate("VibeStudioModelTargets", "Add an indexed skin with Add Skin… on the %1 page.").arg(page("mdl")));
	}
	a.singleSurface();
	a.noTags();
	a.frames();
	const int skins = int(mesh.embeddedSkins.size());
	a.count(QStringLiteral("skins"), skins, a.limits().skins,
			QCoreApplication::translate("VibeStudioModelTargets", "The model has %1 skin images; %2 allows at most %3.")
				.arg(skins)
				.arg(a.label())
				.arg(a.limits().skins),
			QCoreApplication::translate("VibeStudioModelTargets", "Remove skins with Remove Skin on the %1 page.").arg(page("mdl")));
	if (mesh.mdl.enabled)
	{
		a.skinSize(mesh.mdl.skinSize);
	}
	a.frameNames();
	if (mesh.surfaces.size() != 1)
	{
		return true;
	}
	const auto &surface = mesh.surfaces.first();
	a.modelTriangles();
	const bool uvsInside = std::all_of(surface.texCoords.cbegin(), surface.texCoords.cend(),
								 [](const ModelTexCoord &uv) { return uv.u >= 0 && uv.u <= 1 && uv.v >= 0 && uv.v <= 1; });
	if (!a.uvRange(surface))
	{
		return false;
	}
	const auto vertexMessage = [&](int value)
	{
		return QCoreApplication::translate("VibeStudioModelTargets",
										   "The MDL stores %1 vertices after sharing seams and exact duplicates; %2 allows at most %3.")
			.arg(value)
			.arg(a.label())
			.arg(a.limits().vertices);
	};
	const auto vertexFix =
		QCoreApplication::translate("VibeStudioModelTargets", "Reduce the geometry with Mesh > Clean Up > Decimate, or weld UV seams with Weld by Distance so more vertices are shared.");
	const auto renderMessage = [&](int value)
	{
		return QCoreApplication::translate("VibeStudioModelTargets",
										   "%1 draws %2 vertices for this MDL, counting a copy of every seam vertex; it holds at most %3.")
			.arg(a.label())
			.arg(value)
			.arg(a.limits().renderVertices);
	};
	if (depth == ModelTargetDepth::Counts || !uvsInside || !mesh.mdl.enabled)
	{
		// Sharing can only lower the stored count, so the source count bounds it.
		a.storedVertices(surface.name, surface.vertexCount, true, vertexMessage(surface.vertexCount), vertexFix);
		a.renderVertices(surface.vertexCount * 2, true, renderMessage(surface.vertexCount * 2));
		return true;
	}
	int stored = 0, render = 0;
	if (!modelMdlVertexCounts(mesh, &stored, &render, error, control))
	{
		return false;
	}
	a.storedVertices(surface.name, stored, false, vertexMessage(stored), vertexFix);
	a.renderVertices(render, false, renderMessage(render));
	return true;
}

bool assessMd2(const ModelMesh &mesh, Assessor &a, ModelTargetDepth depth, QString *error, const ModelWorkControl &control)
{
	a.singleSurface();
	a.noTags();
	a.noEmbeddedSkins();
	a.frames();
	a.skinSize(mesh.md2SkinSize);
	a.frameNames();
	if (mesh.surfaces.size() != 1)
	{
		return true;
	}
	const auto &surface = mesh.surfaces.first();
	a.modelTriangles();
	const int slotCount = int(surface.skinPaths.size());
	a.count(QStringLiteral("skins"), slotCount, a.limits().skins,
			QCoreApplication::translate("VibeStudioModelTargets", "The surface has %1 skin slots; %2 allows at most %3.")
				.arg(slotCount)
				.arg(a.label())
				.arg(a.limits().skins),
			QCoreApplication::translate("VibeStudioModelTargets", "Remove material slots with Manage Material Slots… on the %1 page.").arg(page("surface")));
	QStringList badPaths;
	for (const auto &path : surface.skinPaths)
	{
		if (!printableName(path, 64) || !path.endsWith(QStringLiteral(".pcx")))
		{
			badPaths << path;
		}
	}
	if (!badPaths.isEmpty())
	{
		const int bad = int(badPaths.size());
		a.issue(QStringLiteral("skin-paths"), surface.name, 0, bad, 0,
				QCoreApplication::translate("VibeStudioModelTargets",
											"Skin slots that are not package-relative PCX paths of at most 63 printable ASCII characters (%n): %1.",
											nullptr, bad)
					.arg(listed(badPaths)),
				QCoreApplication::translate("VibeStudioModelTargets", "Assign PCX materials to the slots."));
	}
	if (surface.vertexCount > 32767)
	{
		a.issue(QStringLiteral("texel-vertices"), surface.name, 0, surface.vertexCount, 32767,
				QCoreApplication::translate("VibeStudioModelTargets", "The surface has %1 UV vertices; MD2 indexes at most 32,767.")
					.arg(surface.vertexCount),
				QCoreApplication::translate("VibeStudioModelTargets", "Reduce the model with Mesh > Clean Up > Decimate."));
	}
	const bool uvsInside = std::all_of(surface.texCoords.cbegin(), surface.texCoords.cend(),
									   [](const ModelTexCoord &uv) { return uv.u >= 0 && uv.u <= 1 && uv.v >= 0 && uv.v <= 1; });
	if (!a.uvRange(surface))
	{
		return false;
	}
	const auto positionMessage = [&](int value)
	{
		return QCoreApplication::translate("VibeStudioModelTargets",
										   "The MD2 stores %1 positions after sharing exact duplicates across every frame; %2 allows at most %3.")
			.arg(value)
			.arg(a.label())
			.arg(a.limits().vertices);
	};
	const auto positionFix = QCoreApplication::translate(
		"VibeStudioModelTargets", "Reduce the geometry with Mesh > Clean Up > Decimate, or remove hard-normal splits so more positions are shared.");
	const auto renderMessage = [&](int value)
	{
		return QCoreApplication::translate("VibeStudioModelTargets",
										   "%1 builds %2 drawn vertices, one for each distinct position and texel pair; it holds at most %3.")
			.arg(a.label())
			.arg(value)
			.arg(a.limits().renderVertices);
	};
	if (depth == ModelTargetDepth::Counts)
	{
		a.storedVertices(surface.name, surface.vertexCount, true, positionMessage(surface.vertexCount), positionFix);
		a.renderVertices(surface.vertexCount, true, renderMessage(surface.vertexCount));
		return true;
	}
	QVector<int> positionIndex, representative;
	if (!modelMd2PositionIndex(mesh, &positionIndex, &representative, error, control))
	{
		return false;
	}
	const int positions = int(representative.size());
	a.storedVertices(surface.name, positions, false, positionMessage(positions), positionFix);
	if (a.limits().renderVertices > 0 && uvsInside)
	{
		// Q2PRO's remap: one drawn vertex per distinct position and texel.
		const int width = mesh.md2SkinSize.width(), height = mesh.md2SkinSize.height();
		QSet<qint64> drawn;
		for (const auto &triangle : surface.triangles)
		{
			if (!a.step())
			{
				return false;
			}
			for (int v : {triangle.a, triangle.b, triangle.c})
			{
				const auto uv = surface.texCoords[v];
				const qint64 s = std::clamp(int(std::lround(double(uv.u) * width - 0.5)), 0, width - 1);
				const qint64 t = std::clamp(int(std::lround(double(uv.v) * height - 0.5)), 0, height - 1);
				drawn.insert((qint64(positionIndex[v]) << 32) | (s << 16) | t);
			}
		}
		const int render = int(drawn.size());
		a.renderVertices(render, false, renderMessage(render));
	}
	return true;
}

bool assessMd3(const ModelMesh &mesh, Assessor &a, ModelTargetDepth depth)
{
	a.noEmbeddedSkins();
	a.frames();
	a.frameNames();
	const int surfaces = int(mesh.surfaces.size());
	a.count(QStringLiteral("surfaces"), surfaces, a.limits().surfaces,
			QCoreApplication::translate("VibeStudioModelTargets", "The model has %1 surfaces; %2 allows at most %3.")
				.arg(surfaces)
				.arg(a.label())
				.arg(a.limits().surfaces),
			QCoreApplication::translate("VibeStudioModelTargets", "Join surfaces that share a material with Manage Surfaces… > Join surfaces on the %1 page.").arg(page("surface")));
	// Every frame holds the same tags (validateEditableModel), so frame 0 counts.
	int tags = 0;
	QStringList longTags;
	for (const auto &tag : mesh.tags)
	{
		if (tag.frameIndex == 0)
		{
			++tags;
			if (!printableName(tag.name, 64))
			{
				longTags << tag.name;
			}
		}
	}
	a.count(QStringLiteral("tags-per-frame"), tags, a.limits().tagsPerFrame,
			QCoreApplication::translate("VibeStudioModelTargets", "Each frame has %1 attachment tags; %2 allows at most %3.")
				.arg(tags)
				.arg(a.label())
				.arg(a.limits().tagsPerFrame),
			QCoreApplication::translate("VibeStudioModelTargets", "Delete attachment tags under Attachment tags on the %1 page.").arg(page("animation")));
	if (!longTags.isEmpty())
	{
		const int bad = int(longTags.size());
		a.issue(QStringLiteral("tag-names"), {}, -1, bad, 0,
				QCoreApplication::translate("VibeStudioModelTargets", "Tag names that do not fit 63 printable ASCII characters (%n): %1.", nullptr, bad)
					.arg(listed(longTags)),
				QCoreApplication::translate("VibeStudioModelTargets", "Rename the tags with Rename Tag on the %1 page.").arg(page("animation")));
	}
	QHash<QString, QString> engineNames;
	for (int index = 0; index < mesh.surfaces.size(); ++index)
	{
		const auto &surface = mesh.surfaces.at(index);
		if (!a.step())
		{
			return false;
		}
		// R_LoadMD3 lowercases names and strips a final underscore-plus-character.
		auto engineName = surface.name.toLower();
		if (engineName.size() > 2 && engineName[engineName.size() - 2] == QLatin1Char('_'))
		{
			engineName.chop(2);
		}
		const auto clash = engineNames.constFind(engineName);
		if (clash != engineNames.cend())
		{
			a.issue(QStringLiteral("surface-names"), surface.name, index, 0, 0,
					QCoreApplication::translate("VibeStudioModelTargets",
												"Surfaces \"%1\" and \"%2\" become the same name after Quake III's case and suffix normalisation.")
						.arg(clash.value(), surface.name),
					QCoreApplication::translate("VibeStudioModelTargets", "Rename one of the surfaces on the %1 page.").arg(page("surface")));
		}
		engineNames.insert(engineName, surface.name);
		if (!printableName(surface.name, 64))
		{
			a.issue(QStringLiteral("surface-names"), surface.name, index, surface.name.size(), 63,
					QCoreApplication::translate("VibeStudioModelTargets", "Surface name \"%1\" does not fit 63 printable ASCII characters.")
						.arg(surface.name),
					QCoreApplication::translate("VibeStudioModelTargets", "Rename the surface on the %1 page.").arg(page("surface")));
		}
		QStringList longPaths;
		for (const auto &material : surface.skinPaths)
		{
			if (!printableName(material, 64))
			{
				longPaths << material;
			}
		}
		if (!longPaths.isEmpty())
		{
			const int bad = int(longPaths.size());
			a.issue(QStringLiteral("material-paths"), surface.name, index, bad, 0,
					QCoreApplication::translate("VibeStudioModelTargets",
												"Material paths on surface \"%1\" that do not fit 63 printable ASCII characters (%n): %2.", nullptr, bad)
						.arg(surface.name, listed(longPaths)),
					QCoreApplication::translate("VibeStudioModelTargets", "Assign shorter material paths."));
		}
		if (!a.surfaceCounts(index))
		{
			return false;
		}
		if (depth == ModelTargetDepth::Counts)
		{
			a.unchecked({QStringLiteral("coordinate-range"), QStringLiteral("precision")});
			continue;
		}
		// MD3 stores positions in 1/64 units in 16 bits. Each vertex or face
		// counts once, in whichever frame it is outside or collapses.
		QVector<bool> outsideVertex(surface.vertexCount, false), collapsedFace(surface.triangles.size(), false);
		for (const auto &frame : surface.frames)
		{
			QVector<std::array<double, 3>> quantised(frame.positions.size());
			for (int v = 0; v < frame.positions.size(); ++v)
			{
				if (!a.step())
				{
					return false;
				}
				const auto p = frame.positions[v];
				quantised[v] = {std::round(p.x * 64.0), std::round(p.y * 64.0), std::round(p.z * 64.0)};
				if (std::any_of(quantised[v].cbegin(), quantised[v].cend(), [](double q) { return q < -32768 || q > 32767; }))
				{
					outsideVertex[v] = true;
				}
			}
			for (int f = 0; f < surface.triangles.size(); ++f)
			{
				if (!a.step())
				{
					return false;
				}
				const auto &t = surface.triangles[f];
				const auto &pa = quantised[t.a], &pb = quantised[t.b], &pc = quantised[t.c];
				const double ux = pb[0] - pa[0], uy = pb[1] - pa[1], uz = pb[2] - pa[2], vx = pc[0] - pa[0], vy = pc[1] - pa[1], vz = pc[2] - pa[2];
				if (uy * vz == uz * vy && uz * vx == ux * vz && ux * vy == uy * vx)
				{
					collapsedFace[f] = true;
				}
			}
		}
		const auto indexes = [](const QVector<bool> &flags)
		{
			QVector<int> result;
			for (int i = 0; i < flags.size(); ++i)
			{
				if (flags[i])
				{
					result << i;
				}
			}
			return result;
		};
		auto outside = indexes(outsideVertex);
		auto collapsed = indexes(collapsedFace);
		if (!outside.isEmpty())
		{
			const int count = int(outside.size());
			a.issue(QStringLiteral("coordinate-range"), surface.name, index, count, 0,
					QCoreApplication::translate("VibeStudioModelTargets",
												"Vertices of surface \"%1\" outside MD3's range of -512 to 511.984375 units in some frame: %n.",
												nullptr, count)
						.arg(surface.name),
					QCoreApplication::translate("VibeStudioModelTargets", "Resize or recentre the model; Prepare for Target moves it into range."))
				.vertices = std::move(outside);
		}
		if (!collapsed.isEmpty())
		{
			const int count = int(collapsed.size());
			a.issue(QStringLiteral("precision"), surface.name, index, count, 0,
					QCoreApplication::translate("VibeStudioModelTargets",
												"Triangles of surface \"%1\" that collapse at MD3's 1/64-unit precision in some frame: %n.", nullptr,
												count)
						.arg(surface.name),
					QCoreApplication::translate("VibeStudioModelTargets", "Enlarge small features, or scale the model up."))
				.faces = std::move(collapsed);
		}
	}
	return true;
}

bool assessSkeletal(const ModelMesh &mesh, Assessor &a, bool animation)
{
	const int joints = int(mesh.skeleton.joints.size());
	a.count(QStringLiteral("joints"), joints, a.limits().joints,
			QCoreApplication::translate("VibeStudioModelTargets", "The skeleton has %1 joints; %2 allows at most %3.")
				.arg(joints)
				.arg(a.label())
				.arg(a.limits().joints),
			QCoreApplication::translate("VibeStudioModelTargets", "Rig the model with fewer joints, or choose a delivery target that loads more."));
	if (animation)
	{
		// Any clip can be written as an md5anim, so each is checked.
		for (const auto &clip : mesh.skeleton.clips)
		{
			const int frames = int(clip.frames.size());
			a.measure(QStringLiteral("frames"), clip.name, frames, a.limits().frames);
			if (a.limits().frames > 0 && frames > a.limits().frames)
			{
				a.issue(QStringLiteral("frames"), clip.name, -1, frames, a.limits().frames,
						QCoreApplication::translate("VibeStudioModelTargets", "Skeletal clip \"%1\" has %2 frames; %3 allows at most %4.")
							.arg(clip.name)
							.arg(frames)
							.arg(a.label())
							.arg(a.limits().frames),
						QCoreApplication::translate("VibeStudioModelTargets", "Shorten the clip, or split it into several clips, on the %1 page.")
							.arg(page("animation")));
			}
		}
		return true;
	}
	// Surfaces without vertices (a bare skeleton) write no mesh.
	int meshes = 0;
	for (const auto &surface : mesh.surfaces)
	{
		meshes += surface.vertexCount > 0;
	}
	a.count(QStringLiteral("surfaces"), meshes, a.limits().surfaces,
			QCoreApplication::translate("VibeStudioModelTargets", "The model has %1 meshes; %2 allows at most %3.")
				.arg(meshes)
				.arg(a.label())
				.arg(a.limits().surfaces),
			QCoreApplication::translate("VibeStudioModelTargets", "Join surfaces that share a material with Manage Surfaces… > Join surfaces on the %1 page.").arg(page("surface")));
	for (int index = 0; index < mesh.surfaces.size(); ++index)
	{
		const auto &surface = mesh.surfaces.at(index);
		if (surface.vertexCount == 0)
		{
			continue;
		}
		if (!a.surfaceCounts(index))
		{
			return false;
		}
		// Influences: the most on one vertex, and the weights an MD5 mesh stores
		// (a vertex without influences is bound to the first joint by one).
		int most = 0, weights = surface.skinning.isEmpty() ? surface.vertexCount : 0;
		QVector<int> crowded;
		for (int v = 0; v < surface.skinning.count.size(); ++v)
		{
			const int influences = surface.skinning.count[v];
			most = std::max(most, influences);
			weights += std::max(1, influences);
			if (a.limits().influences > 0 && influences > a.limits().influences)
			{
				crowded << v;
			}
		}
		a.measure(QStringLiteral("influences"), surface.name, most, a.limits().influences);
		a.measure(QStringLiteral("weights"), surface.name, weights, a.limits().weights);
		if (a.limits().weights > 0 && weights > a.limits().weights)
		{
			a.issue(QStringLiteral("weights"), surface.name, index, weights, a.limits().weights,
					QCoreApplication::translate("VibeStudioModelTargets", "Surface \"%1\" stores %2 joint weights; %3 allows at most %4 per mesh.")
						.arg(surface.name)
						.arg(weights)
						.arg(a.label())
						.arg(a.limits().weights),
					QCoreApplication::translate("VibeStudioModelTargets", "Reduce the influences per vertex with Skin Weights and Binding… > Reduce and normalise weights on the %1 page, or split the surface.")
						.arg(page("skeleton")))
				.vertices = crowded;
		}
	}
	return true;
}
} // namespace

QVector<ModelTargetProfile> modelTargetProfiles()
{
	QVector<ModelTargetProfile> result;
	for (const auto &format : modelExportFormatIds())
	{
		result += modelTargetProfiles(format);
	}
	return result;
}

QVector<ModelTargetProfile> modelTargetProfiles(const QString &format)
{
	QVector<ModelTargetProfile> result;
	const auto id = format.toLower();
	for (const auto &entry : kTargets)
	{
		if (id == QLatin1String(entry.format))
		{
			result.append(profileFrom(entry));
		}
	}
	return result;
}

bool findModelTargetProfile(const QString &format, const QString &id, ModelTargetProfile *profile)
{
	const auto wanted = id.trimmed().toLower();
	for (const auto &candidate : modelTargetProfiles(format))
	{
		if (wanted.isEmpty() ? candidate.isDefault : candidate.id == wanted)
		{
			if (profile)
			{
				*profile = candidate;
			}
			return true;
		}
	}
	return false;
}

QString modelTargetChoices(const QString &format)
{
	QStringList choices;
	for (const auto &profile : modelTargetProfiles(format))
	{
		choices << QStringLiteral("%1 (%2)").arg(profile.id, profile.label);
	}
	return choices.join(QStringLiteral(", "));
}

bool modelTargetSurfaceParts(const ModelSurface &surface, int vertices, int triangles, QVector<int> *partOfFace, int *parts, QString *error,
							 const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	const int faces = int(surface.triangles.size());
	QVector<int> part(faces, -1);
	int count = 0;
	const auto finish = [&](bool ok)
	{
		if (partOfFace)
		{
			*partOfFace = part;
		}
		if (parts)
		{
			*parts = count;
		}
		return ok;
	};
	// Faces around each vertex, for growing parts across shared vertices.
	QVector<int> first(surface.vertexCount + 1, 0), around;
	for (const auto &t : surface.triangles)
	{
		for (int v : {t.a, t.b, t.c})
		{
			++first[v + 1];
		}
	}
	int unused = 0;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		unused += first[v + 1] == 0;
		first[v + 1] += first[v];
	}
	around.resize(first.last());
	{
		auto fill = first;
		for (int f = 0; f < faces; ++f)
		{
			if (!work.step())
			{
				return finish(false);
			}
			const auto &t = surface.triangles[f];
			for (int v : {t.a, t.b, t.c})
			{
				around[fill[v]++] = f;
			}
		}
	}
	const int noLimit = std::numeric_limits<int>::max();
	// `scanned` marks the vertices whose faces a part has queued: a vertex
	// that many faces share (a fan's centre, a non-manifold spine) is scanned
	// once per part, not once per face that reaches it, which would make the
	// header's live check quadratic. A second scan would only find faces
	// already queued or assigned, so the parts are the same.
	QVector<int> stamp(surface.vertexCount, -1), scanned(surface.vertexCount, -1), queued(faces, -1);
	int assigned = 0, cursor = 0;
	while (assigned < faces)
	{
		const int current = count++;
		const int vertexBudget = vertices > 0 ? vertices - (current == 0 ? unused : 0) : noLimit;
		const int triangleBudget = triangles > 0 ? triangles : noLimit;
		int partVertices = 0, partTriangles = 0;
		QVector<int> queue;
		bool open = true;
		while (open)
		{
			while (cursor < faces && part[cursor] >= 0)
			{
				++cursor;
			}
			if (cursor >= faces)
			{
				break;
			}
			// A new seed (the lowest unassigned face) joins the part only if it fits.
			queue = {cursor};
			queued[cursor] = current;
			bool seeded = false;
			for (int head = 0; head < queue.size(); ++head)
			{
				if (!work.step())
				{
					return finish(false);
				}
				const int f = queue[head];
				if (part[f] >= 0)
				{
					continue;
				}
				const auto &t = surface.triangles[f];
				const int corners[]{t.a, t.b, t.c};
				int added = 0;
				for (int i = 0; i < 3; ++i)
				{
					// A repeated corner counts once.
					const bool repeated = (i > 0 && corners[0] == corners[i]) || (i > 1 && corners[1] == corners[i]);
					added += !repeated && stamp[corners[i]] != current;
				}
				if (partVertices + added > vertexBudget || partTriangles + 1 > triangleBudget)
				{
					if (head == 0)
					{
						open = false;
					}
					continue;
				}
				seeded = true;
				part[f] = current;
				++assigned;
				++partTriangles;
				partVertices += added;
				for (int v : {t.a, t.b, t.c})
				{
					stamp[v] = current;
				}
				for (int v : {t.a, t.b, t.c})
				{
					if (scanned[v] == current)
					{
						continue;
					}
					scanned[v] = current;
					for (int i = first[v]; i < first[v + 1]; ++i)
					{
						const int g = around[i];
						if (part[g] < 0 && queued[g] != current)
						{
							queued[g] = current;
							queue << g;
						}
					}
				}
			}
			if (!seeded)
			{
				open = false;
			}
		}
		if (partTriangles == 0)
		{
			if (current == 0 && unused > 0 && vertices > 0)
			{
				// Only the unused vertices fit the first part; the faces follow.
				continue;
			}
			if (error)
			{
				*error = QCoreApplication::translate("VibeStudioModelTargets", "A face of surface \"%1\" does not fit a part of %2 vertices.")
							 .arg(surface.name)
							 .arg(vertices);
			}
			return finish(false);
		}
	}
	if (count == 0)
	{
		count = 1;
	}
	return finish(work.check());
}

bool modelMd2PositionIndex(const ModelMesh &mesh, QVector<int> *positionIndex, QVector<int> *representative, QString *error,
						   const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	positionIndex->clear();
	representative->clear();
	if (mesh.surfaces.isEmpty())
	{
		return work.check();
	}
	// Recombine only exact all-pose position/normal duplicates, so UV seams
	// share MD2 position indexes.
	const auto &surface = mesh.surfaces.first();
	QHash<QByteArray, int> positions;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		if (!work.step())
		{
			return false;
		}
		QByteArray key(mesh.frames.size() * 24, '\0');
		for (int f = 0; f < mesh.frames.size(); ++f)
		{
			const auto p = surface.frames[f].positions[v], n = surface.frames[f].normals[v];
			const float values[]{p.x, p.y, p.z, n.x, n.y, n.z};
			std::memcpy(key.data() + f * 24, values, sizeof(values));
		}
		const auto found = positions.constFind(key);
		if (found == positions.cend())
		{
			positionIndex->append(int(representative->size()));
			positions.insert(key, int(representative->size()));
			representative->append(v);
		}
		else
		{
			positionIndex->append(found.value());
		}
	}
	return work.check();
}

bool assessModelTarget(const ModelMesh &mesh, const ModelTargetProfile &profile, ModelTargetReport *report, QString *error,
					   const ModelWorkControl &control, ModelTargetDepth depth)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	ModelTargetReport result;
	result.format = profile.format;
	result.target = profile.id;
	result.label = profile.label;
	Assessor assessor(mesh, profile, &result, work, control);
	bool ok = work.check();
	if (ok && profile.format == QStringLiteral("mdl"))
	{
		ok = assessMdl(mesh, assessor, depth, error, control);
	}
	else if (ok && profile.format == QStringLiteral("md2"))
	{
		ok = assessMd2(mesh, assessor, depth, error, control);
	}
	else if (ok && profile.format == QStringLiteral("md3"))
	{
		ok = assessMd3(mesh, assessor, depth);
	}
	else if (ok && (profile.format == QStringLiteral("iqm") || profile.format == QStringLiteral("md5mesh") ||
					profile.format == QStringLiteral("md5anim")))
	{
		ok = assessSkeletal(mesh, assessor, profile.format == QStringLiteral("md5anim"));
	}
	if (!ok || !work.check())
	{
		return false;
	}
	if (report)
	{
		*report = std::move(result);
	}
	return true;
}

QStringList modelTargetIssueLines(const ModelTargetReport &report)
{
	QStringList lines;
	for (const auto &issue : report.issues)
	{
		lines << (issue.fix.isEmpty() ? issue.message : QCoreApplication::translate("VibeStudioModelTargets", "%1 %2").arg(issue.message, issue.fix));
	}
	return lines;
}

QString modelTargetLimitName(const QString &limit)
{
	static const struct
	{
		const char *id;
		const char *name;
	} names[] = {
		{"surfaces", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "surfaces")},
		{"vertices", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "vertices")},
		{"render-vertices", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "drawn vertices")},
		{"triangles", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "triangles")},
		{"frames", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "frames")},
		{"skins", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "skins")},
		{"skin-width", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "skin width")},
		{"skin-height", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "skin height")},
		{"skin-pixels", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "skin pixels")},
		{"joints", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "joints")},
		{"influences", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "influences per vertex")},
		{"weights", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "joint weights")},
		{"tags-per-frame", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "tags per frame")},
		{"coordinate-range", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "coordinate range")},
		{"precision", QT_TRANSLATE_NOOP("VibeStudioModelTargets", "1/64-unit precision")},
	};
	for (const auto &entry : names)
	{
		if (limit == QLatin1String(entry.id))
		{
			return QCoreApplication::translate("VibeStudioModelTargets", entry.name);
		}
	}
	return limit;
}

QStringList modelTargetMeasureLines(const ModelTargetReport &report)
{
	QStringList lines;
	for (const auto &measure : report.measures)
	{
		auto line = QCoreApplication::translate("VibeStudioModelTargets", "%1 %2 of %3")
						.arg(modelTargetLimitName(measure.limit))
						.arg(measure.value)
						.arg(measure.maximum);
		if (!measure.element.isEmpty())
		{
			line = QCoreApplication::translate("VibeStudioModelTargets", "%1 (%2)").arg(line, measure.element);
		}
		if (measure.upperBound)
		{
			line = QCoreApplication::translate("VibeStudioModelTargets", "%1, at most, before the writer shares vertices").arg(line);
		}
		lines << line;
	}
	return lines;
}

QJsonObject modelTargetProfileJson(const ModelTargetProfile &profile)
{
	const auto &l = profile.limits;
	QJsonObject limits;
	const auto put = [&limits](const char *key, qint64 value)
	{
		if (value > 0)
		{
			limits.insert(QLatin1String(key), value);
		}
	};
	put("surfaces", l.surfaces);
	put("vertices", l.vertices);
	put("renderVertices", l.renderVertices);
	put("triangles", l.triangles);
	put("frames", l.frames);
	put("skins", l.skins);
	put("skinWidth", l.skinWidth);
	put("skinHeight", l.skinHeight);
	put("skinPixels", l.skinPixels);
	put("skinWidthMultiple", l.skinWidthMultiple);
	put("joints", l.joints);
	put("influences", l.influences);
	put("weights", l.weights);
	put("tagsPerFrame", l.tagsPerFrame);
	if (l.detailLevels >= 0)
	{
		limits.insert(QStringLiteral("detailLevels"), l.detailLevels);
	}
	return {{QStringLiteral("id"), profile.id},
			{QStringLiteral("format"), profile.format},
			{QStringLiteral("label"), profile.label},
			{QStringLiteral("description"), profile.description},
			{QStringLiteral("default"), profile.isDefault},
			{QStringLiteral("limits"), limits},
			{QStringLiteral("sources"), QJsonArray::fromStringList(profile.sources)}};
}

QJsonObject modelTargetIssueJson(const ModelTargetIssue &issue)
{
	QJsonObject object{{QStringLiteral("limit"), issue.limit},
					   {QStringLiteral("value"), issue.value},
					   {QStringLiteral("message"), issue.message},
					   {QStringLiteral("fix"), issue.fix}};
	if (!issue.element.isEmpty())
	{
		object.insert(QStringLiteral("element"), issue.element);
	}
	if (issue.surface >= 0)
	{
		object.insert(QStringLiteral("surface"), issue.surface);
	}
	if (issue.maximum > 0)
	{
		object.insert(QStringLiteral("maximum"), issue.maximum);
	}
	const auto indexes = [](const QVector<int> &values)
	{
		QJsonArray array;
		for (int value : values)
		{
			array.append(value);
		}
		return array;
	};
	if (!issue.faces.isEmpty())
	{
		object.insert(QStringLiteral("faces"), indexes(issue.faces));
	}
	if (!issue.vertices.isEmpty())
	{
		object.insert(QStringLiteral("vertices"), indexes(issue.vertices));
	}
	return object;
}

QJsonObject modelTargetReportJson(const ModelTargetReport &report)
{
	QJsonArray issues, measures;
	for (const auto &issue : report.issues)
	{
		issues.append(modelTargetIssueJson(issue));
	}
	for (const auto &measure : report.measures)
	{
		QJsonObject object{{QStringLiteral("limit"), measure.limit}, {QStringLiteral("value"), measure.value}, {QStringLiteral("maximum"), measure.maximum}};
		if (!measure.element.isEmpty())
		{
			object.insert(QStringLiteral("element"), measure.element);
		}
		if (measure.upperBound)
		{
			object.insert(QStringLiteral("upperBound"), true);
		}
		measures.append(object);
	}
	QJsonObject result{{QStringLiteral("format"), report.format},	{QStringLiteral("target"), report.target}, {QStringLiteral("label"), report.label},
					   {QStringLiteral("ready"), report.ready()}, {QStringLiteral("issues"), issues},		  {QStringLiteral("measures"), measures}};
	if (!report.unchecked.isEmpty())
	{
		result.insert(QStringLiteral("unchecked"), QJsonArray::fromStringList(report.unchecked));
	}
	return result;
}
} // namespace vibestudio
