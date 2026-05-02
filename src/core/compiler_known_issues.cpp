#include "core/compiler_known_issues.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace vibestudio {

namespace {

QString issueText(const char* source)
{
	return QCoreApplication::translate("VibeStudioCompilerKnownIssues", source);
}

QString normalizedId(QString value)
{
	value = value.trimmed().toLower().replace('_', '-');
	if (value.startsWith('#')) {
		value.remove(0, 1);
	}
	return value;
}

QString upstreamIssueUrl(const QString& issueId)
{
	return QStringLiteral("https://github.com/ericwa/ericw-tools/issues/%1").arg(normalizedId(issueId));
}

bool containsNormalizedId(const QStringList& ids, const QString& id)
{
	const QString requested = normalizedId(id);
	for (const QString& candidate : ids) {
		if (normalizedId(candidate) == requested) {
			return true;
		}
	}
	return false;
}

bool matchesScope(const CompilerKnownIssueDescriptor& issue, const QString& toolId, const QString& profileId)
{
	if (toolId.trimmed().isEmpty() && profileId.trimmed().isEmpty()) {
		return true;
	}
	if (!toolId.trimmed().isEmpty() && containsNormalizedId(issue.affectedToolIds, toolId)) {
		return true;
	}
	if (!profileId.trimmed().isEmpty() && containsNormalizedId(issue.affectedProfileIds, profileId)) {
		return true;
	}
	return false;
}

CompilerKnownIssueDescriptor knownIssue(
	const QString& issueId,
	const QString& clusterId,
	CompilerKnownIssueSeverity severity,
	bool highValue,
	const QStringList& affectedToolIds,
	const QStringList& affectedProfileIds,
	const QStringList& matchKeywords,
	const char* summaryText,
	const char* warningText,
	const char* actionText)
{
	return {
		normalizedId(issueId),
		clusterId,
		severity,
		highValue,
		affectedToolIds,
		affectedProfileIds,
		matchKeywords,
		upstreamIssueUrl(issueId),
		issueText(summaryText),
		issueText(warningText),
		issueText(actionText),
	};
}

QStringList qbspTool()
{
	return {QStringLiteral("ericw-qbsp")};
}

QStringList visTool()
{
	return {QStringLiteral("ericw-vis")};
}

QStringList lightTool()
{
	return {QStringLiteral("ericw-light")};
}

QStringList qbspProfile()
{
	return {QStringLiteral("ericw-qbsp")};
}

QStringList visProfile()
{
	return {QStringLiteral("ericw-vis")};
}

QStringList lightProfile()
{
	return {QStringLiteral("ericw-light")};
}

QStringList allCompileTools()
{
	return {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-vis"), QStringLiteral("ericw-light")};
}

QStringList allCompileProfiles()
{
	return {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-vis"), QStringLiteral("ericw-light")};
}

QStringList bsputilTool()
{
	return {QStringLiteral("ericw-bsputil")};
}

QStringList bsputilProfile()
{
	return {QStringLiteral("ericw-bsputil")};
}

QStringList lightpreviewTool()
{
	return {QStringLiteral("ericw-lightpreview")};
}

QStringList lightpreviewProfile()
{
	return {QStringLiteral("ericw-lightpreview")};
}

} // namespace

QString compilerKnownIssueSeverityId(CompilerKnownIssueSeverity severity)
{
	switch (severity) {
	case CompilerKnownIssueSeverity::Info:
		return QStringLiteral("info");
	case CompilerKnownIssueSeverity::Warning:
		return QStringLiteral("warning");
	case CompilerKnownIssueSeverity::Error:
		return QStringLiteral("error");
	case CompilerKnownIssueSeverity::Critical:
		return QStringLiteral("critical");
	}
	return QStringLiteral("warning");
}

QString compilerKnownIssueSeverityText(CompilerKnownIssueSeverity severity)
{
	switch (severity) {
	case CompilerKnownIssueSeverity::Info:
		return issueText("Info");
	case CompilerKnownIssueSeverity::Warning:
		return issueText("Warning");
	case CompilerKnownIssueSeverity::Error:
		return issueText("Error");
	case CompilerKnownIssueSeverity::Critical:
		return issueText("Critical");
	}
	return issueText("Warning");
}

QVector<CompilerKnownIssueDescriptor> compilerKnownIssueDescriptors()
{
	return {
		knownIssue(QStringLiteral("483"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("light compilation parameters"), QStringLiteral("_light_cmdline"), QStringLiteral("_light_ver"), QStringLiteral("provenance")},
			"Light compile parameters are not embedded in BSP/BSPX output.",
			"Compiled lighting may be hard to reproduce because ericw-tools does not yet persist the full light command.",
			"Keep VibeStudio command manifests beside generated BSPs and show them as the authoritative provenance record."),
		knownIssue(QStringLiteral("399"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Error, true, lightTool(), lightProfile(), {QStringLiteral("LMSHIFT"), QStringLiteral("_lmscale"), QStringLiteral("lightmap scale"), QStringLiteral("BSPX")},
			"LMSHIFT metadata can be missing from 2.0 alpha builds.",
			"Custom lightmap scale metadata may be absent from BSPX output.",
			"Warn on custom _lmscale use, preserve the compiler version, and verify BSPX lightmap metadata after compile."),
		knownIssue(QStringLiteral("167"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("save commandline"), QStringLiteral("tools version"), QStringLiteral("_light_cmdline"), QStringLiteral("_light_ver")},
			"Older provenance request for saved command lines and tool versions.",
			"Generated BSPs may not explain which ericw-tools version or flags produced the lighting.",
			"Record tool versions, arguments, inputs, and output hashes in the VibeStudio manifest."),
		knownIssue(QStringLiteral("415"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("BSPX"), QStringLiteral("light-specific lump"), QStringLiteral("LIGHTINGDIR"), QStringLiteral("LMSHIFT")},
			"Light-specific BSPX lump documentation is incomplete upstream.",
			"BSPX inspectors may encounter light metadata whose compiler-side contract is not fully documented.",
			"Present BSPX light metadata as inspectable evidence and avoid implying undocumented lumps are guaranteed across compiler versions."),
		knownIssue(QStringLiteral("309"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("lightmap dimensions"), QStringLiteral("BSPX"), QStringLiteral("luxel"), QStringLiteral("_lmscale")},
			"BSPX output may not record compiler-calculated lightmap dimensions.",
			"Engines and editor inspectors can disagree about lightmap dimensions when the metadata is absent.",
			"Keep profile-required BSPX metadata checks separate from the compile and preserve the command manifest for triage."),

		knownIssue(QStringLiteral("475"), QStringLiteral("D02"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("toggled light"), QStringLiteral("style"), QStringLiteral("targetname")},
			"Toggled lights can overwrite explicit style values.",
			"A toggled light with a user-set style may not preserve the intended style assignment.",
			"Preflight lights with both targetname and style, then ask the user to choose a generated or explicit style."),
		knownIssue(QStringLiteral("173"), QStringLiteral("D02"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("style is set"), QStringLiteral("toggled light"), QStringLiteral("targetname")},
			"Explicit style on toggled lights needs a clear warning.",
			"Toggled light style conflicts can produce confusing runtime light behavior.",
			"Surface an entity inspector warning before light compilation."),
		knownIssue(QStringLiteral("122"), QStringLiteral("D02"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("START_OFF"), QStringLiteral("start off"), QStringLiteral("spawnflag"), QStringLiteral("targetname")},
			"Grouped toggled lights can disagree about START_OFF.",
			"Lights sharing a targetname may start in inconsistent on/off states.",
			"Validate grouped lights and require a consistent start-off spawnflag policy."),

		knownIssue(QStringLiteral("400"), QStringLiteral("D03"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-bouncedebug"), QStringLiteral("bouncedebug"), QStringLiteral("all black")},
			"-bouncedebug can produce all-black output.",
			"Indirect-light debug views may be misleading even when the normal light compile succeeds.",
			"Treat bouncedebug output as diagnostic-only and cross-check with normal preview lighting."),
		knownIssue(QStringLiteral("416"), QStringLiteral("D03"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-bouncedebug"), QStringLiteral("bouncedebug"), QStringLiteral("broken")},
			"-bouncedebug is tracked as broken in a related upstream report.",
			"Bounce debug images may not reflect actual indirect lighting.",
			"Group this with issue #400 in diagnostics and avoid blocking normal compiles on it."),
		knownIssue(QStringLiteral("77"), QStringLiteral("D03"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-dirtdebug"), QStringLiteral("dirtdebug"), QStringLiteral("_dirt -1")},
			"-dirtdebug can draw ambient occlusion despite _dirt -1.",
			"Dirt debug output may show AO for entities that disabled dirt.",
			"Explain that debug visualization can disagree with intended entity dirt settings."),

		knownIssue(QStringLiteral("293"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("absolute WAD"), QStringLiteral("wad path"), QStringLiteral("strip directories"), QStringLiteral("private path")},
			"Absolute WAD paths can leak private directories into output.",
			"Entity lumps may expose local filesystem paths or exceed engine buffer expectations.",
			"Sanitize exported WAD lists and keep project-relative package paths where possible."),
		knownIssue(QStringLiteral("288"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("escape sequence"), QStringLiteral("backslash"), QStringLiteral("\\b"), QStringLiteral("entity key")},
			"Backslash escape warnings can be unclear.",
			"Entity keys containing backslashes may be parsed differently than the author expects.",
			"Preflight suspicious backslashes and offer a map-safe escaped replacement."),
		knownIssue(QStringLiteral("287"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-vis"), QStringLiteral("ericw-light")}, {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-vis"), QStringLiteral("ericw-light")}, {QStringLiteral("error"), QStringLiteral("warning"), QStringLiteral("common issue"), QStringLiteral("failed")},
			"Common compiler errors need richer user-facing guidance.",
			"Raw compiler output may not explain the practical fix.",
			"Layer known-issue explanations over parsed diagnostics and keep raw logs available."),
		knownIssue(QStringLiteral("245"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("long key"), QStringLiteral("long value"), QStringLiteral("WAD list"), QStringLiteral("entity key")},
			"Excessively long entity values can crash or confuse engines.",
			"Large key values, especially WAD lists, may make a map fragile after compile.",
			"Measure entity key/value lengths before compile and suggest package-relative cleanup."),
		knownIssue(QStringLiteral("135"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("invalid numeric"), QStringLiteral("entity coordinates"), QStringLiteral("parse warning")},
			"Numeric parse warnings may lack useful entity coordinates.",
			"Invalid numeric entity values can be hard to locate from compiler output alone.",
			"Attach parser warnings to editor entities and include map coordinates in diagnostics."),
		knownIssue(QStringLiteral("87"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("escape sequence"), QStringLiteral("double quote"), QStringLiteral("WAD path"), QStringLiteral("qbsp")},
			"qbsp escape-sequence warnings need preflight handling.",
			"Quotes and backslashes in map text can create misleading compiler warnings.",
			"Run VibeStudio map-text validation before invoking qbsp."),
		knownIssue(QStringLiteral("129"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("\\b"), QStringLiteral("backspace"), QStringLiteral("escape handling"), QStringLiteral("qbsp")},
			"Backslash escape handling may happen later than users expect.",
			"Map text that depends on escape interpretation can be reported inconsistently across pipeline stages.",
			"Use VibeStudio parser warnings as the stable user-facing explanation before qbsp runs."),
		knownIssue(QStringLiteral("121"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("entdict_t"), QStringLiteral("case-insensitive"), QStringLiteral("entity key"), QStringLiteral("classname")},
			"Entity key case-sensitivity expectations are not fully settled upstream.",
			"Maps using keys that differ only by case may be interpreted differently by tools, engines, or editor services.",
			"Normalize editor-authored keys where the game profile requires it and warn before exporting ambiguous entity dictionaries."),
		knownIssue(QStringLiteral("105"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_sunlight_mangle"), QStringLiteral("mangle"), QStringLiteral("status output"), QStringLiteral("sunlight")},
			"_sunlight_mangle status output can be hard to parse.",
			"Compiler logs may describe sunlight direction in a format that is awkward for users and diagnostics.",
			"Display VibeStudio's parsed sun direction beside the raw log instead of relying on upstream wording alone."),

		knownIssue(QStringLiteral("333"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map"), QStringLiteral("entities"), QStringLiteral("prefab")},
			"External maps do not import full entity content yet.",
			"Prefab maps may lose entity behavior when compiled through current ericw-tools support.",
			"Flag prefab sources containing entities and document what will be merged."),
		knownIssue(QStringLiteral("231"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("misc_external_map"), QStringLiteral("_phong"), QStringLiteral("lighting key")},
			"External-map lighting keys can be ignored.",
			"Prefab lighting settings such as _phong may not carry into imported geometry.",
			"Warn on lighting keys attached to misc_external_map and suggest baking them into the prefab map."),
		knownIssue(QStringLiteral("207"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("func_group"), QStringLiteral("external map"), QStringLiteral("corrupt")},
			"External maps made entirely of func_group brushes can corrupt output.",
			"Grouped prefab content may compile incorrectly when imported as an external map.",
			"Validate prefab entity composition and require at least one safe import target path."),
		knownIssue(QStringLiteral("199"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map"), QStringLiteral("relative path"), QStringLiteral("map-file-relative")},
			"External-map paths need map-file-relative handling.",
			"Prefab references may break when the working directory differs from the source map location.",
			"Resolve prefab paths through VibeStudio project services and pass a controlled working directory."),
		knownIssue(QStringLiteral("194"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map_classname"), QStringLiteral("misc_external_map"), QStringLiteral("assert")},
			"Missing external-map classname can crash the compile.",
			"A misc_external_map without _external_map_classname may hit an upstream assertion.",
			"Require a classname during preflight, defaulting to func_detail only after user confirmation."),
		knownIssue(QStringLiteral("193"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("0 360 0"), QStringLiteral("external map"), QStringLiteral("rotation"), QStringLiteral("shadow")},
			"External-map rotations can produce odd shadows.",
			"Certain prefab rotations may light differently than expected.",
			"Normalize prefab rotations and highlight transformed prefab lighting as a QA checkpoint."),
		knownIssue(QStringLiteral("327"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("misc_external_map"), QStringLiteral("merge"), QStringLiteral("target entity"), QStringLiteral("prefab composition")},
			"Multiple external-map pieces cannot be assumed to merge into one target entity.",
			"Advanced prefab composition may produce separate or unexpected brush entities.",
			"Show composed prefab structure in the compile preview and keep grouped-target behavior documented as upstream work."),

		knownIssue(QStringLiteral("444"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("region"), QStringLiteral("antiregion"), QStringLiteral("areaportal"), QStringLiteral("Quake 2")},
			"Region brushes can prevent Quake 2 areaportals from working.",
			"Partial Q2 compiles may produce misleading areaportal behavior.",
			"Show a region-compile warning when areaportals exist and recommend a full compile before shipping."),
		knownIssue(QStringLiteral("422"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("region brush"), QStringLiteral("brush entities"), QStringLiteral("outside region")},
			"Region builds may keep brush entities outside the requested region.",
			"Partial compiles can include entity logic that the visible region does not explain.",
			"List out-of-region brush entities before starting a region compile."),
		knownIssue(QStringLiteral("417"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("region+"), QStringLiteral("rotating door"), QStringLiteral("origin brush"), QStringLiteral("leak")},
			"region+ can leak with rotating doors that have origin brushes.",
			"A Q2 region+ compile may leak when rotating bmodels use origin brushes.",
			"Warn on origin-brush rotating doors in region+ profiles and suggest a full compile for validation."),
		knownIssue(QStringLiteral("390"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Info, true, qbspTool(), qbspProfile(), {QStringLiteral("multiple region brushes"), QStringLiteral("region brush"), QStringLiteral("partial compile")},
			"Multiple region brushes are not supported as a union.",
			"Editor selections using several regions may not match ericw-tools region behavior.",
			"Collapse region requests to one temporary region volume or explain the limitation before compile."),

		knownIssue(QStringLiteral("484"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("LIGHTINGDIR"), QStringLiteral("_sunlight2"), QStringLiteral("Q2"), QStringLiteral("BSPX")},
			"Q2 BSPX LIGHTINGDIR can be wrong with _sunlight2.",
			"Directional lighting data may show artifacts when _sunlight2 is enabled.",
			"Warn in Q2 lighting profiles and compare previews against non-directional output when needed."),
		knownIssue(QStringLiteral("456"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("wrong shadows"), QStringLiteral("func_detail"), QStringLiteral("func_illusionary"), QStringLiteral("visblocker")},
			"Structural meshes and func_* entities can cast inconsistent shadows.",
			"Entity-based geometry may not light like equivalent structural brushes.",
			"Flag mixed structural/detail shadow tests in map QA and keep versioned preview captures."),
		knownIssue(QStringLiteral("441"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-vis")}, {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-vis")}, {QStringLiteral("func_illusionary_visblocker"), QStringLiteral("0.18.1"), QStringLiteral("visible faces")},
			"func_illusionary_visblocker behavior changed from older releases.",
			"Maps relying on old visblocker behavior may render or vis differently.",
			"Record compiler version and prompt for a regression check when visblockers are present."),
		knownIssue(QStringLiteral("439"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("light bleed"), QStringLiteral("brush model"), QStringLiteral("2.0 alpha")},
			"2.0 alpha builds can show light bleed on brush model edges.",
			"Brush models may receive edge lighting artifacts compared with older releases.",
			"Warn on affected version/profile combinations and preserve visual regression screenshots."),
		knownIssue(QStringLiteral("405"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_phong"), QStringLiteral("phong_angle"), QStringLiteral("light bleed")},
			"Phong settings can cause confusing smoothing and light bleed.",
			"Phong-smoothed geometry may light incorrectly on hard cube-like edges.",
			"Warn on high-risk _phong and phong_angle combinations in the entity inspector."),
		knownIssue(QStringLiteral("389"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Error, true, lightTool(), lightProfile(), {QStringLiteral("surface lights"), QStringLiteral("alpha 3"), QStringLiteral("Q2"), QStringLiteral("emit")},
			"Q2 surface lights can stop emitting in alpha builds.",
			"Surface-light textures may appear bright without lighting the room.",
			"Track compiler version and run a small surface-light fixture for Q2 profiles."),
		knownIssue(QStringLiteral("375"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("relighting"), QStringLiteral("old BSP"), QStringLiteral("too bright"), QStringLiteral("fullbright")},
			"Relighting old Q2 BSPs can produce extreme brightness.",
			"Existing BSP relight workflows may become fullbright, too bright, or too dark.",
			"Require a preview pass and preserve the original BSP before relighting."),
		knownIssue(QStringLiteral("364"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("corner artifacts"), QStringLiteral("grid point"), QStringLiteral("Q2 lightmaps")},
			"Q2 lightmaps can show corner and grid-point artifacts.",
			"Recent compiler ranges may introduce visible lightmap artifacts at face corners.",
			"Keep a versioned lighting regression fixture for Q2 maps."),
		knownIssue(QStringLiteral("221"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("sunlight"), QStringLiteral("shining through world"), QStringLiteral("sky geometry")},
			"Sunlight can leak through world or sky geometry.",
			"Sun lighting may appear in areas that should be occluded.",
			"Flag sunlight-heavy maps for visual QA and keep leak/sky diagnostics visible."),
		knownIssue(QStringLiteral("401"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("-tex_saturation_boost"), QStringLiteral("saturation"), QStringLiteral("brightness"), QStringLiteral("Kingpin")},
			"Texture saturation boost can also change lighting brightness.",
			"Relighting presets that adjust saturation may shift the overall brightness more than expected.",
			"Treat saturation boost as a visual-regression checkpoint and keep before/after previews available."),
		knownIssue(QStringLiteral("351"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("bouncescale"), QStringLiteral("_bouncescale"), QStringLiteral("per-light bounce"), QStringLiteral("bounce scale")},
			"Per-light bounce scale has reported incorrect behavior.",
			"Individual light bounce controls may not produce the expected indirect lighting contribution.",
			"Warn when per-light bouncescale keys are present and compare against a conservative profile preview."),
		knownIssue(QStringLiteral("349"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_dirt"), QStringLiteral("_dirtscale"), QStringLiteral("bmodels")},
			"Dirt lighting keys can fail on brush models.",
			"Brush entities may ignore dirt settings that work on worldspawn.",
			"Warn when bmodels carry dirt keys and suggest checking baked lighting previews."),
		knownIssue(QStringLiteral("291"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("deviance"), QStringLiteral("penumbra"), QStringLiteral("sky light entity"), QStringLiteral("_sunlight_penumbra")},
			"Sky-light entity penumbra can behave differently from worldspawn sunlight.",
			"Non-worldspawn sun setups may ignore or misapply deviance and penumbra values.",
			"Prefer profile-visible sun settings and mark entity-based sky lighting for preview comparison."),
		knownIssue(QStringLiteral("268"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_sun 1"), QStringLiteral("deviance"), QStringLiteral("penumbra"), QStringLiteral("sun entity")},
			"Deviance can fail on light entities marked as sun sources.",
			"Sunlight authored through `_sun 1` entities may not match penumbra expectations.",
			"Flag `_sun 1` entities with deviance settings as lighting-regression candidates."),
		knownIssue(QStringLiteral("262"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_mirrorinside"), QStringLiteral("mirrorinside"), QStringLiteral("shadow regression"), QStringLiteral("inside shadow")},
			"_mirrorinside has a tracked shadow regression.",
			"Shadow output may differ from older compiler builds when mirrored-inside lighting is enabled.",
			"Keep compiler version visible and ask for visual QA when `_mirrorinside` appears in a map."),
		knownIssue(QStringLiteral("256"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_switchableshadow"), QStringLiteral("switchable shadow"), QStringLiteral("_shadow 1"), QStringLiteral("shadow casting")},
			"Switchable shadows may not imply normal shadow casting.",
			"Authors can expect switchable-shadow keys to cast shadows even when `_shadow` is not explicitly enabled.",
			"Warn on switchable-shadow entities without an explicit shadow policy in the inspector."),
		knownIssue(QStringLiteral("255"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_switchableshadow"), QStringLiteral("skip"), QStringLiteral("bmodel"), QStringLiteral("switchable shadows")},
			"Switchable shadows can interact badly with skip-textured bmodels.",
			"Brush models using skip faces and switchable shadows may light incorrectly.",
			"Preflight this entity combination and recommend a small preview compile before shipping."),
		knownIssue(QStringLiteral("247"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("misc_model"), QStringLiteral("cast shadow"), QStringLiteral("model shadow"), QStringLiteral("prop shadow")},
			"misc_model shadow casting is a requested upstream feature.",
			"Model props cannot be assumed to contribute baked shadows in current ericw-tools compiles.",
			"Expose model shadow expectations in asset diagnostics and suggest brush proxies when the target engine needs baked shadows."),
		knownIssue(QStringLiteral("238"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("penumbra"), QStringLiteral("pitch"), QStringLiteral("yaw"), QStringLiteral("sunlight")},
			"Sunlight penumbra can deviate differently in pitch and yaw.",
			"Mostly vertical sunlight may produce softness that does not match the authored penumbra values.",
			"Show sun-vector previews and keep vertical sun setups in the lighting QA checklist."),
		knownIssue(QStringLiteral("217"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_surface"), QStringLiteral("liquid"), QStringLiteral("surface light"), QStringLiteral("water")},
			"Surface light settings can collide with liquid textures.",
			"Liquid surface lights may be ignored, renamed, or matched differently than expected.",
			"Validate `_surface` light names against liquid texture usage and show ambiguous matches before compile."),
		knownIssue(QStringLiteral("214"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("-gate 4"), QStringLiteral("gate"), QStringLiteral("sunlight"), QStringLiteral("sun")},
			"High gate values can suppress sunlight.",
			"Lighting profiles using `-gate 4` may accidentally remove expected sun contribution.",
			"Warn on high gate values in sunlight-heavy profiles and offer a lower-gate preview."),
		knownIssue(QStringLiteral("203"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("disable bounce"), QStringLiteral("sunlight2"), QStringLiteral("sun bounce"), QStringLiteral("bounce")},
			"Disabling bounce for sun sources is requested upstream behavior.",
			"Profiles cannot assume sun or `_sunlight2` bounce can be disabled independently by the compiler.",
			"Represent sun-bounce suppression as a profile limitation until a supported compiler option exists."),
		knownIssue(QStringLiteral("190"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("bounce"), QStringLiteral("soft"), QStringLiteral("extra4"), QStringLiteral("messed up lighting")},
			"Some bounce/soft/extra lighting combinations have reported visual regressions.",
			"High-quality lighting presets can still produce map-specific artifacts.",
			"Keep high-quality preset output under visual regression review and preserve the exact compiler arguments."),
		knownIssue(QStringLiteral("185"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("black seams"), QStringLiteral("shib5"), QStringLiteral("shib1"), QStringLiteral("seams")},
			"Specific maps have reported black lighting seams.",
			"Seam artifacts may persist even when the compile completes without errors.",
			"Use seam reports as compatibility fixtures and keep visual inspection separate from process success."),
		knownIssue(QStringLiteral("172"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("spotlight"), QStringLiteral("_phong 1"), QStringLiteral("models"), QStringLiteral("phong")},
			"Spotlights can behave incorrectly on Phong-smoothed models.",
			"Model or brush surfaces using `_phong 1` may receive invalid spotlighting.",
			"Warn when spotlights target Phong-smoothed model geometry and suggest a controlled preview pass."),
		knownIssue(QStringLiteral("145"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("H-shaped artifacts"), QStringLiteral("angled beam"), QStringLiteral("pedestals"), QStringLiteral("artifact")},
			"Some angled-beam layouts have reported lighting artifacts.",
			"Map-specific artifact reports can indicate compiler-version sensitivity rather than author error.",
			"Keep artifact-prone maps as optional regression fixtures and cite the upstream issue in QA notes."),
		knownIssue(QStringLiteral("140"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_dirtmode"), QStringLiteral("_dirtdepth"), QStringLiteral("_dirtgain"), QStringLiteral("func_group")},
			"Dirt option support on non-worldspawn entities is incomplete upstream.",
			"Brush entities may not honor the same dirt controls that worldspawn uses.",
			"Group this with bmodel dirt diagnostics and show which entity keys are compiler-supported for the active profile."),
		knownIssue(QStringLiteral("134"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("hard lines"), QStringLiteral("dirtmap"), QStringLiteral("shadow regression"), QStringLiteral("hard shadow")},
			"Hard dirt and shadow lines are tracked as a lighting regression.",
			"Compiled lighting can show visible line artifacts despite valid map data.",
			"Preserve compiler version data and include hard-line checks in visual regression passes."),
		knownIssue(QStringLiteral("109"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("surface light"), QStringLiteral("submodel"), QStringLiteral("nudge"), QStringLiteral("bmodel")},
			"Surface lights may need nudging on submodels.",
			"Bmodel surface lights can sit exactly on geometry and fail to illuminate as expected.",
			"Warn on surface lights attached to submodels and suggest a small offset or preview check."),
		knownIssue(QStringLiteral("91"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("contrasting textures"), QStringLiteral("bounce color"), QStringLiteral("subdivision"), QStringLiteral("bounce light")},
			"Bounce color averaging can be too coarse on contrasting textures.",
			"Large faces with varied texture colors may bounce an averaged color that does not match the visual detail.",
			"Explain this as a lighting-quality limitation and favor authored fill lights when exact bounced color matters."),
		knownIssue(QStringLiteral("377"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("autominlight"), QStringLiteral("bmodel"), QStringLiteral("minlight")},
			"Global autominlight can override explicit bmodel minlight intent.",
			"Brush-model minlight may not match author expectations when autominlight is enabled.",
			"Warn when global autominlight and bmodel minlight are both present."),

		knownIssue(QStringLiteral("451"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("textures folder"), QStringLiteral("loose texture"), QStringLiteral("custom engine")},
			"Loose texture lookup expects a folder named textures.",
			"Custom projects with nonstandard texture roots may fail to resolve assets.",
			"Make texture roots explicit in project profiles and validate them before compile."),
		knownIssue(QStringLiteral("455"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("func_detail_null"), QStringLiteral("compiler-only brush"), QStringLiteral("null detail"), QStringLiteral("shadow helper")},
			"func_detail_null is a requested compiler-only brush entity.",
			"Advanced lighting or shadow helper brushes cannot be assumed to disappear safely at compile time.",
			"Represent helper geometry as VibeStudio export metadata and show exactly what will reach qbsp."),
		knownIssue(QStringLiteral("450"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("-notex"), QStringLiteral("non-WAD"), QStringLiteral("missing texture")},
			"Non-WAD compiles can still report missing texture data.",
			"Generated or external-texture BSP workflows may warn even when texture data is intentionally omitted.",
			"Explain -notex/non-WAD profile behavior and separate expected warnings from real missing assets."),
		knownIssue(QStringLiteral("346"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Info, true, {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-light")}, {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-light")}, {QStringLiteral("PCX"), QStringLiteral("PNG"), QStringLiteral("external textures"), QStringLiteral("palette")},
			"External texture format support is still expanding.",
			"Some custom texture formats may not be accepted by ericw-tools.",
			"Validate replacement textures through VibeStudio asset tools before starting a compile."),
		knownIssue(QStringLiteral("201"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("-outputdir"), QStringLiteral("-waddir"), QStringLiteral("working directory")},
			"Explicit output and WAD directories are a requested upstream feature.",
			"Wrapper-controlled paths can be fragile when a compiler relies on its working directory.",
			"Run compiles from an isolated, predictable working directory and record expected outputs."),
		knownIssue(QStringLiteral("192"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("custom palette"), QStringLiteral("palette"), QStringLiteral("bounce lighting")},
			"Light may assume the stock Quake palette in custom projects.",
			"Bounce lighting can be wrong for total conversions with custom palettes.",
			"Add a custom-palette profile warning and require visual QA for relit maps."),

		knownIssue(QStringLiteral("463"), QString(), CompilerKnownIssueSeverity::Critical, true, {QStringLiteral("ericw-lightpreview")}, {QStringLiteral("ericw-lightpreview")}, {QStringLiteral("lightpreview"), QStringLiteral("temporary directory"), QStringLiteral("overwrite")},
			"lightpreview should run in a temporary directory.",
			"Preview compiles may write beside the source map and overwrite outputs.",
			"Always launch preview-style compiles in an isolated VibeStudio work directory."),
		knownIssue(QStringLiteral("435"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Error, true, {QStringLiteral("ericw-bsputil")}, {QStringLiteral("ericw-bsputil")}, {QStringLiteral("bsputil"), QStringLiteral("argument parsing"), QStringLiteral("dummy argument")},
			"bsputil argument parsing can reject valid operations.",
			"Some bsputil wrapper commands may fail unless extra dummy arguments are supplied.",
			"Smoke-test each bsputil operation before exposing it as a package or diagnostics action."),
		knownIssue(QStringLiteral("482"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Warning, true, {QStringLiteral("ericw-tools")}, {QStringLiteral("ericw-tools")}, {QStringLiteral("Embree"), QStringLiteral("TBB"), QStringLiteral("Linux Mint"), QStringLiteral("build")},
			"Embree/TBB build requirements can fail on older Linux distributions.",
			"Local ericw-tools builds may need newer compiler dependencies than the host provides.",
			"Probe dependencies up front and show distro-specific build guidance."),
		knownIssue(QStringLiteral("480"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Warning, false, lightpreviewTool(), lightpreviewProfile(), {QStringLiteral("lightpreview"), QStringLiteral("Arch"), QStringLiteral("Nvidia"), QStringLiteral("Wayland")},
			"lightpreview can fail to launch on some Linux graphics stacks.",
			"Qt/OpenGL context creation may fail before any compile work begins.",
			"Keep lightpreview optional, expose launch diagnostics, and prefer VibeStudio previews where available."),
		knownIssue(QStringLiteral("449"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("ericw-tools")}, {QStringLiteral("ericw-tools")}, {QStringLiteral("std::format"), QStringLiteral("fmt"), QStringLiteral("logging"), QStringLiteral("format")},
			"Upstream logging format internals may change.",
			"Forks or log parsers that depend on third-party formatting details could be fragile.",
			"Parse stable diagnostic content rather than assuming a particular upstream logging library."),
		knownIssue(QStringLiteral("289"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, true, {QStringLiteral("ericw-bsputil")}, {QStringLiteral("ericw-bsputil")}, {QStringLiteral("bspinfo"), QStringLiteral("diagnostics"), QStringLiteral("light stats"), QStringLiteral("entity extraction")},
			"Richer bspinfo-style diagnostics are requested upstream.",
			"Current tools may not expose every statistic VibeStudio wants in a structured way.",
			"Keep VibeStudio diagnostics pane tolerant of missing fields and preserve raw tool output."),
		knownIssue(QStringLiteral("335"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Warning, false, {QStringLiteral("ericw-tools")}, {QStringLiteral("ericw-tools")}, {QStringLiteral("binary name"), QStringLiteral("light"), QStringLiteral("vis"), QStringLiteral("tool discovery")},
			"Generic binary names can conflict with other packages.",
			"Launching bare `light` or `vis` may resolve to the wrong executable on some systems.",
			"Use VibeStudio tool descriptors, version probes, and explicit executable paths instead of bare command names alone."),
		knownIssue(QStringLiteral("225"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, bsputilTool(), bsputilProfile(), {QStringLiteral("bspinfo"), QStringLiteral("log file"), QStringLiteral("write log"), QStringLiteral("diagnostics log")},
			"bspinfo does not provide every requested log-file workflow upstream.",
			"Helper-tool diagnostics may only be available through captured process output.",
			"Capture stdout and stderr in VibeStudio manifests and expose them as the durable diagnostic log."),
		knownIssue(QStringLiteral("223"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("ericw-tools")}, {QStringLiteral("ericw-tools")}, {QStringLiteral("Darwin"), QStringLiteral("macOS"), QStringLiteral("package suffix"), QStringLiteral("release artifact")},
			"macOS release artifact naming is tracked upstream.",
			"Automated tool discovery may encounter package names that use older platform wording.",
			"Match release artifacts by descriptor metadata and checksums instead of display suffix alone."),
		knownIssue(QStringLiteral("215"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("ericw-tools")}, {QStringLiteral("ericw-tools")}, {QStringLiteral("Flatpak"), QStringLiteral("Flathub"), QStringLiteral("Linux package"), QStringLiteral("bundle")},
			"Flatpak packaging is an upstream distribution request.",
			"Linux users may not have a single supported package route for ericw-tools.",
			"Keep VibeStudio setup able to use user-selected executables, submodule builds, or future package channels."),

		knownIssue(QStringLiteral("464"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("func_viscluster"), QStringLiteral("VIS cluster"), QStringLiteral("optimization")},
			"func_viscluster-style VIS optimization is not available upstream yet.",
			"Large open maps may still require expensive VIS runs without cluster hints.",
			"Present this as future optimization work and keep current VIS estimates conservative."),
		knownIssue(QStringLiteral("443"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("entity order"), QStringLiteral("map hack"), QStringLiteral("custom model"), QStringLiteral("*n model")},
			"Stable entity order for map-hack custom models is requested upstream.",
			"Map hacks that refer to numbered bmodels can break when compiler output ordering changes.",
			"Show entity and bmodel ordering as fragile compatibility data and avoid reordering source entities silently."),
		knownIssue(QStringLiteral("413"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("translucency"), QStringLiteral("light transmission"), QStringLiteral("tinting"), QStringLiteral("thin material")},
			"Translucency lighting support is requested upstream.",
			"Thin or translucent material lighting cannot be assumed in current ericw-tools profiles.",
			"Keep translucent-material intent visible in the asset inspector and mark compiler transmission as unsupported unless detected."),
		knownIssue(QStringLiteral("411"), QString(), CompilerKnownIssueSeverity::Info, false, bsputilTool(), bsputilProfile(), {QStringLiteral("decompiled output"), QStringLiteral("special texture"), QStringLiteral("no nodes"), QStringLiteral("round trip")},
			"A special decompile texture is requested for round-trip output.",
			"Decompiler helper textures may accidentally generate nodes, clipnodes, or faces if reused in source maps.",
			"Mark decompiled maps as review-required and keep helper textures isolated from normal project assets."),
		knownIssue(QStringLiteral("205"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("one-way VIS"), QStringLiteral("one-way leaf"), QStringLiteral("window")},
			"One-way VIS leaves are a requested feature.",
			"Special window-like visibility behavior cannot be assumed in current compiles.",
			"Warn profile authors that this requires engine/compiler support, not just editor metadata."),
		knownIssue(QStringLiteral("131"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("visapprox"), QStringLiteral("occluding bars"), QStringLiteral("falloff light")},
			"visapprox can be misled by long falloff lights through occluders.",
			"Approximate lighting may differ from final visibility-aware lighting in complex layouts.",
			"Mark approximate lighting previews as provisional for maps with thin occluders."),
		knownIssue(QStringLiteral("103"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("hint brush"), QStringLiteral("liquids"), QStringLiteral("horizontal")},
			"Hint brushes have reported VIS edge cases.",
			"Liquid or horizontal hint layouts may not optimize visibility as expected.",
			"Keep hint-brush troubleshooting guidance available from VIS diagnostics."),

		knownIssue(QStringLiteral("485"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("world_units_per_luxel"), QStringLiteral("luxel"), QStringLiteral("_lmscale")},
			"world_units_per_luxel cannot be forcibly overridden yet.",
			"Quality presets may not override every per-entity luxel density.",
			"Show the override as a planned capability and list entities that keep their own value."),
		knownIssue(QStringLiteral("436"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("disable lightmaps"), QStringLiteral("fullbright"), QStringLiteral("skip lightmap"), QStringLiteral("lightmap allocation")},
			"Disabling lightmaps per face or brush is requested upstream.",
			"Fullbright or special-case surfaces may still consume normal lightmap handling.",
			"Expose this as a compile-efficiency limitation and keep export-time alternatives separate from compiler behavior."),
		knownIssue(QStringLiteral("425"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("lightgrid"), QStringLiteral("playable volume"), QStringLiteral("unreachable points"), QStringLiteral("calculation speed")},
			"Lightgrid calculation can spend time on unreachable space.",
			"Large Q2 or remaster maps may have longer lightgrid passes than users expect.",
			"Show lightgrid work as its own progress stage and plan future playable-volume hints as editor metadata."),
		knownIssue(QStringLiteral("374"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("embedded lightmaps"), QStringLiteral("bake lightmaps"), QStringLiteral("diffuse textures"), QStringLiteral("export")},
			"Embedded lightmap baking is a requested upstream feature.",
			"Engines without standard lightmap support may need a separate export transform.",
			"Treat baked diffuse-lightmap output as VibeStudio packaging work unless a compiler version provides native support."),
		knownIssue(QStringLiteral("249"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-lmscale"), QStringLiteral("-lmshift"), QStringLiteral("legacy lightmap scale"), QStringLiteral("BSPX")},
			"Legacy lightmap-scale compatibility remains under upstream discussion.",
			"Profiles that mix old `-lmscale` or `-lmshift` behavior with BSPX metadata can confuse compatibility expectations.",
			"Prefer explicit profile wording around modern BSPX metadata and keep legacy flags visible in manifests."),
		knownIssue(QStringLiteral("470"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_minlight"), QStringLiteral("float"), QStringLiteral("integer"), QStringLiteral("Q1"), QStringLiteral("Q2")},
			"_minlight scale differs across output formats.",
			"Minlight values can be misread when moving between Q1, Q2, and related profiles.",
			"Apply profile-aware validation and offer value conversion hints."),
		knownIssue(QStringLiteral("343"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Info, true, qbspTool(), qbspProfile(), {QStringLiteral("conditional compile"), QStringLiteral("entity set"), QStringLiteral("build profile")},
			"Conditional entity compilation is not native upstream behavior.",
			"Maps with alternate mod/entity sets need VibeStudio-side build-profile handling.",
			"Route conditional entities through VibeStudio export transforms before qbsp."),
		knownIssue(QStringLiteral("301"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("hull sizes"), QStringLiteral("total conversion"), QStringLiteral("custom hull")},
			"Custom hull sizes are a requested total-conversion feature.",
			"Nonstandard player hulls may not compile correctly with stock assumptions.",
			"Warn in custom-engine profiles and keep hull settings explicit in project metadata."),
		knownIssue(QStringLiteral("437"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Info, false, allCompileTools(), allCompileProfiles(), {QStringLiteral("HLBSP"), QStringLiteral("GoldSrc"), QStringLiteral("Half-Life"), QStringLiteral("parity")},
			"GoldSrc HLBSP parity is incomplete upstream.",
			"Half-Life-style maps may rely on original compiler behaviors that ericw-tools does not fully match.",
			"Keep GoldSrc compatibility profile warnings explicit and avoid claiming original-compiler parity."),

		knownIssue(QStringLiteral("350"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("leak"), QStringLiteral("face splitting"), QStringLiteral("excessive")},
			"Leaking maps can trigger excessive face splitting.",
			"Leak compiles may produce noisy or expensive geometry output.",
			"Prioritize leak repair diagnostics before trusting compile metrics."),
		knownIssue(QStringLiteral("344"), QString(), CompilerKnownIssueSeverity::Critical, true, qbspTool(), qbspProfile(), {QStringLiteral("bad surface extents"), QStringLiteral("surface extents"), QStringLiteral("crash")},
			"Bad surface extents can create BSPs that crash in-game.",
			"Compiled geometry may be accepted by the tool but fail at runtime.",
			"Run post-compile BSP validation and block packaging on critical surface extent errors."),
		knownIssue(QStringLiteral("308"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("origin key"), QStringLiteral("brush entity"), QStringLiteral("leak")},
			"Brush entities with origin keys can affect leak detection.",
			"Entity origins may cause false or confusing leak results.",
			"Validate brush-entity origin usage before qbsp and point leaks back to source entities."),
		knownIssue(QStringLiteral("278"), QString(), CompilerKnownIssueSeverity::Critical, true, {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-light")}, {QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-light")}, {QStringLiteral("corrupt BSP"), QStringLiteral("Hexen2"), QStringLiteral("misidentified")},
			"Corrupt Q1 BSPs can be misidentified as Hexen2.",
			"A compile/light pipeline may produce a BSP with the wrong detected format.",
			"Inspect BSP headers and lump sanity after compile before registering the artifact."),
		knownIssue(QStringLiteral("270"), QString(), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("outside filling"), QStringLiteral("24 units"), QStringLiteral("monsters"), QStringLiteral("floor")},
			"Floor-offset monsters may not block outside filling consistently.",
			"Leak/outside-fill results can change around entities placed above the floor.",
			"Show entity placement warnings near outside-fill boundaries and verify the leak path visually."),
		knownIssue(QStringLiteral("264"), QString(), CompilerKnownIssueSeverity::Critical, true, qbspTool(), qbspProfile(), {QStringLiteral("leafs with content 0"), QStringLiteral("Hexen2"), QStringLiteral("invisible")},
			"Hexen2 BSP2 maps can contain invalid content-zero leafs.",
			"Entities may become invisible in affected BSP output.",
			"Run BSP leaf-content validation for Hexen2/BSP2 profiles."),
		knownIssue(QStringLiteral("257"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("non-integer vertices"), QStringLiteral("missing faces"), QStringLiteral("grid")},
			"Non-integer brush vertices can produce missing faces.",
			"Off-grid geometry may compile with absent faces or wrong contents.",
			"Warn on unsnapped brush vertices and provide a safe snap/repair action."),
		knownIssue(QStringLiteral("342"), QString(), CompilerKnownIssueSeverity::Warning, false, bsputilTool(), bsputilProfile(), {QStringLiteral("empty texture name"), QStringLiteral("decompile"), QStringLiteral("extract textures"), QStringLiteral("texture scale")},
			"Empty texture names can break decompile and texture extraction workflows.",
			"Round-tripped maps may receive incorrect texture mapping or scale when unnamed textures are present.",
			"Validate BSP texture names before extraction and label decompile output as lossy when names are empty."),
		knownIssue(QStringLiteral("310"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("angle"), QStringLiteral("mangle"), QStringLiteral("Q2 light"), QStringLiteral("sun direction")},
			"Q2 light `angle` can interfere with `mangle`.",
			"Directional lights may ignore the intended mangle when a nonzero angle is also present.",
			"Warn on Q2 light entities that define both keys and ask the user to choose one direction source."),
		knownIssue(QStringLiteral("298"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("mapversion 220"), QStringLiteral("engine warning"), QStringLiteral("metadata"), QStringLiteral("strip mapversion")},
			"Editor mapversion metadata may need export cleanup.",
			"Some engines can warn about `mapversion 220` even when the compiler accepts it.",
			"Offer export cleanup as a VibeStudio transform and keep the original source map unchanged."),
		knownIssue(QStringLiteral("297"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_dirt_off_radius"), QStringLiteral("_dirt_on_radius"), QStringLiteral("dirt radius"), QStringLiteral("documentation")},
			"Dirt radius keys need clearer upstream documentation.",
			"Users may not know whether `_dirt_off_radius` and `_dirt_on_radius` are supported in the active compiler build.",
			"Show these keys through profile-aware help only when the selected tool version is known to support them."),
		knownIssue(QStringLiteral("285"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("constant color"), QStringLiteral("translucency"), QStringLiteral("translucent color"), QStringLiteral("material")},
			"Constant translucency color is requested upstream.",
			"Material workflows cannot assume the compiler can force a fixed transmission color.",
			"Represent constant-color translucency as a future material feature and keep current lighting previews conservative."),
		knownIssue(QStringLiteral("281"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-nosunlight"), QStringLiteral("disable sunlight"), QStringLiteral("sunlight override"), QStringLiteral("sun")},
			"A global no-sunlight option is requested upstream.",
			"Quick compile profiles cannot rely on a native `-nosunlight` switch unless the selected compiler supports it.",
			"Offer VibeStudio-side export/profile alternatives and clearly mark them as local transforms."),
		knownIssue(QStringLiteral("277"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("alternate position"), QStringLiteral("moving door"), QStringLiteral("secret door"), QStringLiteral("alternate lightmaps")},
			"Alternate-position lighting for moving models is requested upstream.",
			"Animated bmodels may be baked only at their authored position.",
			"Show moving-model lighting as an advanced QA risk and preserve any alternate-lighting metadata separately."),
		knownIssue(QStringLiteral("254"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("-forcegoodtree"), QStringLiteral("planenum"), QStringLiteral("Hexen2"), QStringLiteral("BSP2")},
			"`-forcegoodtree` can hit a planenum assertion for Hexen2 BSP2.",
			"Risky tree-building flags may crash or fail the compile for some Hexen2 profiles.",
			"Warn on `-forcegoodtree` in Hexen2/BSP2 profiles and suggest a standard qbsp pass first."),
		knownIssue(QStringLiteral("246"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("brightness"), QStringLiteral("contrast"), QStringLiteral("final lightmap"), QStringLiteral("post adjustment")},
			"Final lightmap brightness and contrast adjustment is requested upstream.",
			"Compile profiles cannot assume the compiler can apply final post-lighting contrast controls.",
			"Keep post-lightmap adjustment as an explicit export or preview operation unless native support appears."),
		knownIssue(QStringLiteral("241"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("brush as light"), QStringLiteral("light volume"), QStringLiteral("skip brush"), QStringLiteral("lighting design")},
			"Brush-volume lighting is a requested authoring feature.",
			"Brushes used as lighting guides may still affect qbsp unless VibeStudio exports them safely.",
			"Model brush-as-light workflows as editor metadata and show exactly what is stripped before qbsp."),
		knownIssue(QStringLiteral("234"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("HSV"), QStringLiteral("hue"), QStringLiteral("saturation"), QStringLiteral("value")},
			"Global HSV lightmap adjustment is requested upstream.",
			"Color-grading style controls may not be available in the selected compiler.",
			"Expose global HSV changes as preview/export processing only when the compiler lacks native support."),
		knownIssue(QStringLiteral("233"), QString(), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("no valid brushes"), QStringLiteral("func_detail_illusionary"), QStringLiteral("valid brushes"), QStringLiteral("generated map")},
			"func_detail_illusionary can trigger no-valid-brush diagnostics.",
			"Generated or model-style maps may produce confusing brush-validity failures.",
			"Attach the diagnostic to the source entity and show which brushes are compiler-eligible."),
		knownIssue(QStringLiteral("222"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("rangescale"), QStringLiteral("Q2"), QStringLiteral("range scale"), QStringLiteral("cleanup")},
			"Q2 rangescale defaults are tracked for upstream cleanup.",
			"Profiles should not depend on undocumented internal rangescale representation.",
			"Keep Q2 light-range assumptions explicit in VibeStudio profile metadata."),
		knownIssue(QStringLiteral("213"), QString(), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("-convert valve"), QStringLiteral("valve postfix"), QStringLiteral("destination filename"), QStringLiteral("-valve")},
			"Valve conversion can append an unexpected postfix.",
			"Wrapper-provided destination filenames may still receive a `-valve` suffix from qbsp.",
			"Predict and display the expected output path before launch, then register only the artifact VibeStudio requested."),
		knownIssue(QStringLiteral("209"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_project_texture"), QStringLiteral("_surface"), QStringLiteral("projected texture"), QStringLiteral("surface light")},
			"Projected textures on surface lights are tracked as broken.",
			"Advanced surface-light projection may not affect lighting as authored.",
			"Warn when `_project_texture` is combined with `_surface` and preserve the setup for upstream regression reporting."),
		knownIssue(QStringLiteral("200"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("random texture tiling"), QStringLiteral("texture randomization"), QStringLiteral("tiling"), QStringLiteral("variation")},
			"Random texture tiling is a requested compiler feature.",
			"Texture variation cannot be assumed at compile time for current ericw-tools profiles.",
			"Keep random tiling as an editor/export feature unless a selected compiler version advertises support."),
		knownIssue(QStringLiteral("195"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("AD start.bsp"), QStringLiteral("Arcane Dimensions"), QStringLiteral("odd face"), QStringLiteral("visual compatibility")},
			"A specific Arcane Dimensions face has reported odd lighting or rendering.",
			"Compatibility maps can reveal visual regressions that do not show up as compiler errors.",
			"Treat this as optional compatibility-fixture coverage and keep generated outputs versioned."),
		knownIssue(QStringLiteral("189"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("_noskydeletion"), QStringLiteral("sky brushes"), QStringLiteral("skybox"), QStringLiteral("lightmap workflow")},
			"Preserving brushes inside sky is requested upstream.",
			"Sky-contained helper brushes may be deleted or compiled differently than an advanced skybox workflow expects.",
			"Show sky-deletion assumptions in export previews and keep helper geometry marked as editor-owned."),
		knownIssue(QStringLiteral("186"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_falloff"), QStringLiteral("delay 1"), QStringLiteral("delay 2"), QStringLiteral("delay 5")},
			"Falloff control is not available for every requested delay mode.",
			"Light entities using unsupported delay/falloff combinations may not match author expectations.",
			"Validate falloff keys against the active profile and explain unsupported combinations before compile."),
		knownIssue(QStringLiteral("136"), QString(), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("clipnodes"), QStringLiteral("marksurfaces"), QStringLiteral("txqbsp-xt"), QStringLiteral("compile metrics")},
			"Clipnode and marksurface counts can differ from other compilers.",
			"Maps may approach engine limits sooner with one compiler than another.",
			"Track compile metrics in manifests and show comparisons as guidance rather than claiming compiler equivalence."),
		knownIssue(QStringLiteral("106"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("specific lights"), QStringLiteral("sunlight bounce"), QStringLiteral("per-light bounce"), QStringLiteral("bounce control")},
			"Per-light and sunlight bounce controls are requested upstream.",
			"Current profiles may not be able to target bounce behavior at the exact light-source level.",
			"Expose per-source bounce as an advanced limitation and keep authored intent visible in the inspector."),
		knownIssue(QStringLiteral("81"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("flip projected textures"), QStringLiteral("projected texture"), QStringLiteral("texture projection"), QStringLiteral("flip")},
			"Projected texture flipping is requested upstream.",
			"Texture projection controls may lack a native flip operation in current light compiles.",
			"Keep projection orientation editable in VibeStudio metadata and warn when the compiler cannot express it."),
		knownIssue(QStringLiteral("230"), QString(), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("double extension"), QStringLiteral("dotted filename"), QStringLiteral(".1.bsp")},
			"Dotted BSP filenames can be handled incorrectly by light.",
			"Output paths such as my-map.1.bsp may lose part of the intended name.",
			"Use wrapper-controlled output paths and include dotted filenames in path smoke tests."),
		knownIssue(QStringLiteral("114"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("csg_fail"), QStringLiteral("leaf contents"), QStringLiteral("wrong contents"), QStringLiteral("content type")},
			"Known CSG edge cases can assign the wrong leaf contents.",
			"Problematic brush/content layouts may compile with incorrect contents classification.",
			"Run geometry/content preflight fixtures and keep suspicious CSG failures visible in map-health diagnostics."),
	};
}

QStringList compilerKnownIssueIds()
{
	QStringList ids;
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		ids.push_back(issue.issueId);
	}
	return ids;
}

bool compilerKnownIssueForId(const QString& issueId, CompilerKnownIssueDescriptor* out)
{
	const QString requested = normalizedId(issueId);
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (normalizedId(issue.issueId) == requested) {
			if (out) {
				*out = issue;
			}
			return true;
		}
	}
	return false;
}

QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForCluster(const QString& clusterId)
{
	QVector<CompilerKnownIssueDescriptor> issues;
	const QString requested = normalizedId(clusterId);
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (!requested.isEmpty() && normalizedId(issue.clusterId) == requested) {
			issues.push_back(issue);
		}
	}
	return issues;
}

QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForTool(const QString& toolId)
{
	QVector<CompilerKnownIssueDescriptor> issues;
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (containsNormalizedId(issue.affectedToolIds, toolId)) {
			issues.push_back(issue);
		}
	}
	return issues;
}

QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForProfile(const QString& profileId)
{
	QVector<CompilerKnownIssueDescriptor> issues;
	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (containsNormalizedId(issue.affectedProfileIds, profileId)) {
			issues.push_back(issue);
		}
	}
	return issues;
}

QVector<CompilerKnownIssueMatch> matchCompilerKnownIssues(const QString& text, const QString& toolId, const QString& profileId)
{
	QVector<CompilerKnownIssueMatch> matches;
	if (text.trimmed().isEmpty()) {
		return matches;
	}

	for (const CompilerKnownIssueDescriptor& issue : compilerKnownIssueDescriptors()) {
		if (!matchesScope(issue, toolId, profileId)) {
			continue;
		}
		for (const QString& keyword : issue.matchKeywords) {
			if (!keyword.isEmpty() && text.contains(keyword, Qt::CaseInsensitive)) {
				matches.push_back({issue, keyword});
				break;
			}
		}
	}
	return matches;
}

QString compilerKnownIssueText(const CompilerKnownIssueDescriptor& issue)
{
	QStringList lines;
	lines << issueText("ericw-tools issue #%1").arg(issue.issueId);
	if (!issue.clusterId.isEmpty()) {
		lines << issueText("Cluster: %1").arg(issue.clusterId);
	}
	lines << issueText("Severity: %1").arg(compilerKnownIssueSeverityText(issue.severity));
	lines << issueText("High value: %1").arg(issue.highValue ? issueText("yes") : issueText("no"));
	if (!issue.affectedToolIds.isEmpty()) {
		lines << issueText("Tools: %1").arg(issue.affectedToolIds.join(QStringLiteral(", ")));
	}
	if (!issue.affectedProfileIds.isEmpty()) {
		lines << issueText("Profiles: %1").arg(issue.affectedProfileIds.join(QStringLiteral(", ")));
	}
	lines << issue.summaryText;
	lines << issue.warningText;
	lines << issue.actionText;
	lines << issue.upstreamUrl;
	return lines.join('\n');
}

QStringList ericwKnownIssuePlanWarnings(const QString& profileId, const QString& inputPath, const QStringList& arguments)
{
	QStringList warnings;
	if (!profileId.startsWith(QStringLiteral("ericw-"), Qt::CaseInsensitive)) {
		return warnings;
	}

	const QVector<CompilerKnownIssueDescriptor> profileIssues = compilerKnownIssuesForProfile(profileId);
	int highValueCount = 0;
	for (const CompilerKnownIssueDescriptor& issue : profileIssues) {
		if (issue.highValue) {
			++highValueCount;
		}
	}
	warnings << issueText("ericw-tools known-issue checks active: %1 high-value upstream issues are tracked for this profile.").arg(highValueCount);

	QStringList appendedIssueIds;
	auto appendIssue = [&warnings, &appendedIssueIds](const QString& issueId) {
		CompilerKnownIssueDescriptor issue;
		if (compilerKnownIssueForId(issueId, &issue) && !appendedIssueIds.contains(issue.issueId)) {
			appendedIssueIds << issue.issueId;
			warnings << issueText("ericw-tools #%1: %2 Action: %3").arg(issue.issueId, issue.warningText, issue.actionText);
		}
	};

	const QString context = QStringLiteral("%1\n%2").arg(inputPath, arguments.join(QLatin1Char('\n')));
	for (const CompilerKnownIssueMatch& match : matchCompilerKnownIssues(context, QString(), profileId)) {
		if (match.issue.highValue) {
			appendIssue(match.issue.issueId);
		}
	}

	if (profileId.compare(QStringLiteral("ericw-light"), Qt::CaseInsensitive) == 0 && QFileInfo(inputPath).completeBaseName().contains('.')) {
		appendIssue(QStringLiteral("230"));
	}
	if (profileId.compare(QStringLiteral("ericw-light"), Qt::CaseInsensitive) == 0
		&& (arguments.contains(QStringLiteral("-bspxhdr"), Qt::CaseInsensitive) || arguments.contains(QStringLiteral("-bspxlux"), Qt::CaseInsensitive) || arguments.contains(QStringLiteral("-wrnormals"), Qt::CaseInsensitive))) {
		appendIssue(QStringLiteral("484"));
	}
	if (profileId.compare(QStringLiteral("ericw-qbsp"), Qt::CaseInsensitive) == 0 && arguments.contains(QStringLiteral("-notex"), Qt::CaseInsensitive)) {
		appendIssue(QStringLiteral("450"));
	}

	warnings.removeDuplicates();
	return warnings;
}

} // namespace vibestudio
