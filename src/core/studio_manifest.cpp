#include "core/studio_manifest.h"

#include "vibestudio_config.h"

namespace vibestudio {

QVector<StudioModule> plannedModules()
{
	return {
		{
			"workspace",
			"Workspace",
			"Production",
			"scaffolded",
			"Project hub for installations, open packages, tasks, asset graph, and build output.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"level-editor",
			"Level Editor",
			"World Building",
			"planned",
			"Brush, sector, entity, region, lighting, and compiler orchestration workspace.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"asset-editors",
			"Asset Editors",
			"Content",
			"planned",
			"Texture, sprite, model, audio, and cinematic editors sharing one asset database.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"package-manager",
			"Package Manager",
			"Packaging",
			"planned",
			"PakFu-derived archive management for WAD, PAK, PK3, folders, and nested packages.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"script-ide",
			"Script And Code IDE",
			"Code",
			"planned",
			"Script, shader, config, QuakeC, QVM, and helper-code editing with diagnostics.",
			{"idTech1", "idTech2", "idTech3"},
		},
		{
			"shader-graph",
			"idTech3 Shader Graph",
			"Materials",
			"planned",
			"Graphical editor for Quake III shader scripts with text round-tripping.",
			{"idTech3"},
		},
	};
}

QVector<CompilerIntegration> compilerIntegrations()
{
	return {
		{
			"ericw-tools",
			"ericw-tools",
			"idTech2 / Quake BSP family",
			"qbsp, vis, light, bspinfo, and bsputil toolchain.",
			"external/compilers/ericw-tools",
			"https://github.com/ericwa/ericw-tools",
			"f80b1e216a415581aea7475cb52b16b8c4859084",
			"GPL-2.0-or-later, with GPL-3.0+ builds when Embree is enabled",
		},
		{
			"q3map2-nrc",
			"NetRadiant Custom q3map2",
			"idTech3 / Quake III BSP family",
			"q3map2 compiler source imported from NetRadiant Custom under tools/quake3/q3map2.",
			"external/compilers/q3map2-nrc",
			"https://github.com/Garux/netradiant-custom",
			"68ecbed64b7be78741878c730279b5471d978c7c",
			"Mixed GPL/LGPL/BSD; q3map2 is GPL per upstream LICENSE",
		},
		{
			"zdbsp",
			"ZDBSP",
			"idTech1 / Doom WAD node building",
			"Fast Doom-family node builder with vanilla, Hexen, GL, and UDMF-aware modes.",
			"external/compilers/zdbsp",
			"https://github.com/rheit/zdbsp",
			"bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659",
			"GPL-2.0-or-later",
		},
		{
			"zokumbsp",
			"ZokumBSP",
			"idTech1 / Doom blockmap and node building",
			"Advanced Doom-family node, blockmap, and reject builder for vanilla-conscious maps.",
			"external/compilers/zokumbsp",
			"https://github.com/zokum-no/zokumbsp",
			"22af6defeb84ce836e0b184d6be5e80f127d9451",
			"GPL-2.0",
		},
	};
}

QString versionString()
{
	return QString::fromUtf8(VIBESTUDIO_VERSION);
}

} // namespace vibestudio
