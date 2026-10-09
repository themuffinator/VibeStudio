#include "core/compiler_known_issues.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace vibestudio {

namespace {

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

bool isKeywordWordCharacter(QChar character)
{
	return character.isLetterOrNumber() || character == QLatin1Char('_');
}

// Keywords are matched as whole tokens, never as bare substrings: "leak" must not fire on the
// "-leaktest" argument preset and "Q2" must not fire on a path such as maps/q2dm1.bsp. Only ends
// that are themselves word characters are anchored, because many catalog keywords deliberately
// begin with '-', '_', '.' or '\' ("-notex", "_minlight", ".1.bsp", "\b") where a symmetric word
// boundary would anchor on the wrong side.
bool textContainsKeyword(const QString& text, const QString& keyword)
{
	if (keyword.isEmpty()) {
		return false;
	}
	const bool anchorStart = isKeywordWordCharacter(keyword.front());
	const bool anchorEnd = isKeywordWordCharacter(keyword.back());
	int index = text.indexOf(keyword, 0, Qt::CaseInsensitive);
	while (index >= 0) {
		const int end = index + keyword.size();
		const bool startFits = !anchorStart || index == 0 || !isKeywordWordCharacter(text.at(index - 1));
		const bool endFits = !anchorEnd || end >= text.size() || !isKeywordWordCharacter(text.at(end));
		if (startFits && endFits) {
			return true;
		}
		index = text.indexOf(keyword, index + 1, Qt::CaseInsensitive);
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
		QCoreApplication::translate("VibeStudioCompilerKnownIssues", summaryText),
		QCoreApplication::translate("VibeStudioCompilerKnownIssues", warningText),
		QCoreApplication::translate("VibeStudioCompilerKnownIssues", actionText),
	};
}

QStringList qbspTool()
{
	return {QStringLiteral("vibemap2-bsp")};
}

QStringList visTool()
{
	return {QStringLiteral("vibemap2-vis")};
}

QStringList lightTool()
{
	return {QStringLiteral("vibemap2-light")};
}

QStringList qbspProfile()
{
	return {QStringLiteral("vibemap2-bsp")};
}

QStringList visProfile()
{
	return {QStringLiteral("vibemap2-vis")};
}

QStringList lightProfile()
{
	return {QStringLiteral("vibemap2-light")};
}

QStringList allCompileTools()
{
	return {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-vis"), QStringLiteral("vibemap2-light")};
}

QStringList allCompileProfiles()
{
	return {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-vis"), QStringLiteral("vibemap2-light")};
}

QStringList bsputilTool()
{
	return {QStringLiteral("vibemap2-bsputil")};
}

QStringList bsputilProfile()
{
	return {QStringLiteral("vibemap2-bsputil")};
}

QStringList hubTool()
{
	return {QStringLiteral("vibemap2-hub")};
}

QStringList hubProfile()
{
	return {QStringLiteral("vibemap2-hub")};
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
		return QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Info");
	case CompilerKnownIssueSeverity::Warning:
		return QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Warning");
	case CompilerKnownIssueSeverity::Error:
		return QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Error");
	case CompilerKnownIssueSeverity::Critical:
		return QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Critical");
	}
	return QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Warning");
}

// Match keywords must discriminate. Two invariants keep the catalog from firing on healthy runs:
// a keyword may never be a tool's own program name, because VibeMap2 prints
// "---- <program_name> / VibeMap2 <version> ----" as the first line of every run
// (external/compilers/vibemap2/src/common/settings.cc, common_settings::set_parameters), and a
// keyword may never be a word that ordinary compiler output uses for its own status lines
// ("error", "warning", "failed"). An entry with nothing distinctive to match on carries an empty
// keyword list and stays a documentation-only note.
QVector<CompilerKnownIssueDescriptor> compilerKnownIssueDescriptors()
{
	return {
		knownIssue(QStringLiteral("483"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("light compilation parameters"), QStringLiteral("_light_cmdline"), QStringLiteral("_light_ver"), QStringLiteral("provenance")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Light compile parameters are not embedded in BSP/BSPX output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Compiled lighting may be hard to reproduce because VibeMap2 does not yet persist the full light command."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep VibeStudio command manifests beside generated BSPs and show them as the authoritative provenance record.")),
		knownIssue(QStringLiteral("399"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Error, true, lightTool(), lightProfile(), {QStringLiteral("LMSHIFT"), QStringLiteral("_lmscale"), QStringLiteral("lightmap scale"), QStringLiteral("BSPX")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "LMSHIFT metadata can be missing from 2.0 alpha builds."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Custom lightmap scale metadata may be absent from BSPX output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on custom _lmscale use, preserve the compiler version, and verify BSPX lightmap metadata after compile.")),
		knownIssue(QStringLiteral("167"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("save commandline"), QStringLiteral("tools version"), QStringLiteral("_light_cmdline"), QStringLiteral("_light_ver")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Older provenance request for saved command lines and tool versions."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Generated BSPs may not explain which VibeMap2 version or flags produced the lighting."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Record tool versions, arguments, inputs, and output hashes in the VibeStudio manifest.")),
		knownIssue(QStringLiteral("415"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("BSPX"), QStringLiteral("light-specific lump"), QStringLiteral("LIGHTINGDIR"), QStringLiteral("LMSHIFT")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Light-specific BSPX lump documentation is incomplete upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "BSPX inspectors may encounter light metadata whose compiler-side contract is not fully documented."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Present BSPX light metadata as inspectable evidence and avoid implying undocumented lumps are guaranteed across compiler versions.")),
		knownIssue(QStringLiteral("309"), QStringLiteral("D01"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("lightmap dimensions"), QStringLiteral("BSPX"), QStringLiteral("luxel"), QStringLiteral("_lmscale")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "BSPX output may not record compiler-calculated lightmap dimensions."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Engines and editor inspectors can disagree about lightmap dimensions when the metadata is absent."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep profile-required BSPX metadata checks separate from the compile and preserve the command manifest for triage.")),

		knownIssue(QStringLiteral("475"), QStringLiteral("D02"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("toggled light"), QStringLiteral("style"), QStringLiteral("targetname")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Toggled lights can overwrite explicit style values."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "A toggled light with a user-set style may not preserve the intended style assignment."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Preflight lights with both targetname and style, then ask the user to choose a generated or explicit style.")),
		knownIssue(QStringLiteral("173"), QStringLiteral("D02"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("style is set"), QStringLiteral("toggled light"), QStringLiteral("targetname")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Explicit style on toggled lights needs a clear warning."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Toggled light style conflicts can produce confusing runtime light behavior."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Surface an entity inspector warning before light compilation.")),
		knownIssue(QStringLiteral("122"), QStringLiteral("D02"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("START_OFF"), QStringLiteral("start off"), QStringLiteral("spawnflag"), QStringLiteral("targetname")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Grouped toggled lights can disagree about START_OFF."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Lights sharing a targetname may start in inconsistent on/off states."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Validate grouped lights and require a consistent start-off spawnflag policy.")),

		knownIssue(QStringLiteral("400"), QStringLiteral("D03"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-bouncedebug"), QStringLiteral("bouncedebug"), QStringLiteral("all black")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "-bouncedebug can produce all-black output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Indirect-light debug views may be misleading even when the normal light compile succeeds."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Treat bouncedebug output as diagnostic-only and cross-check with normal preview lighting.")),
		knownIssue(QStringLiteral("416"), QStringLiteral("D03"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-bouncedebug"), QStringLiteral("bouncedebug"), QStringLiteral("broken")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "-bouncedebug is tracked as broken in a related upstream report."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Bounce debug images may not reflect actual indirect lighting."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Group this with issue #400 in diagnostics and avoid blocking normal compiles on it.")),
		knownIssue(QStringLiteral("77"), QStringLiteral("D03"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-dirtdebug"), QStringLiteral("dirtdebug"), QStringLiteral("_dirt -1")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "-dirtdebug can draw ambient occlusion despite _dirt -1."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Dirt debug output may show AO for entities that disabled dirt."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Explain that debug visualization can disagree with intended entity dirt settings.")),

		knownIssue(QStringLiteral("293"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("absolute WAD"), QStringLiteral("wad path"), QStringLiteral("strip directories"), QStringLiteral("private path")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Absolute WAD paths can leak private directories into output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Entity lumps may expose local filesystem paths or exceed engine buffer expectations."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Sanitize exported WAD lists and keep project-relative package paths where possible.")),
		knownIssue(QStringLiteral("288"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("escape sequence"), QStringLiteral("backslash"), QStringLiteral("\\b"), QStringLiteral("entity key")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Backslash escape warnings can be unclear."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Entity keys containing backslashes may be parsed differently than the author expects."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Preflight suspicious backslashes and offer a map-safe escaped replacement.")),
		// Deliberately unmatchable: this is a user-experience meta-issue about how compiler output is
		// presented, and no compiler line can legitimately indicate it. Its former keywords ("error",
		// "warning", "failed") appear in the normal output of every successful VibeMap2 run, which
		// downgraded clean compiles to Warning and cited an unrelated upstream issue. It stays
		// high-value so the notes for these profiles keep listing it.
		knownIssue(QStringLiteral("287"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-vis"), QStringLiteral("vibemap2-light")}, {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-vis"), QStringLiteral("vibemap2-light")}, {},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Common compiler errors need richer user-facing guidance."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Raw compiler output may not explain the practical fix."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Layer known-issue explanations over parsed diagnostics and keep raw logs available.")),
		knownIssue(QStringLiteral("245"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("long key"), QStringLiteral("long value"), QStringLiteral("WAD list"), QStringLiteral("entity key")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Excessively long entity values can crash or confuse engines."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Large key values, especially WAD lists, may make a map fragile after compile."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Measure entity key/value lengths before compile and suggest package-relative cleanup.")),
		knownIssue(QStringLiteral("135"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("invalid numeric"), QStringLiteral("entity coordinates"), QStringLiteral("parse warning")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Numeric parse warnings may lack useful entity coordinates."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Invalid numeric entity values can be hard to locate from compiler output alone."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Attach parser warnings to editor entities and include map coordinates in diagnostics.")),
		knownIssue(QStringLiteral("87"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("escape sequence"), QStringLiteral("double quote"), QStringLiteral("WAD path")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "qbsp escape-sequence warnings need preflight handling."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Quotes and backslashes in map text can create misleading compiler warnings."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Run VibeStudio map-text validation before invoking qbsp.")),
		knownIssue(QStringLiteral("129"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("\\b"), QStringLiteral("backspace"), QStringLiteral("escape handling")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Backslash escape handling may happen later than users expect."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Map text that depends on escape interpretation can be reported inconsistently across pipeline stages."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Use VibeStudio parser warnings as the stable user-facing explanation before qbsp runs.")),
		knownIssue(QStringLiteral("121"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("entdict_t"), QStringLiteral("case-insensitive"), QStringLiteral("entity key"), QStringLiteral("classname")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Entity key case-sensitivity expectations are not fully settled upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Maps using keys that differ only by case may be interpreted differently by tools, engines, or editor services."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Normalize editor-authored keys where the game profile requires it and warn before exporting ambiguous entity dictionaries.")),
		knownIssue(QStringLiteral("105"), QStringLiteral("D04"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_sunlight_mangle"), QStringLiteral("mangle"), QStringLiteral("status output"), QStringLiteral("sunlight")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "_sunlight_mangle status output can be hard to parse."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Compiler logs may describe sunlight direction in a format that is awkward for users and diagnostics."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Display VibeStudio's parsed sun direction beside the raw log instead of relying on upstream wording alone.")),

		knownIssue(QStringLiteral("333"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map"), QStringLiteral("entities"), QStringLiteral("prefab")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "External maps do not import full entity content yet."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Prefab maps may lose entity behavior when compiled through current VibeMap2 support."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Flag prefab sources containing entities and document what will be merged.")),
		knownIssue(QStringLiteral("231"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("misc_external_map"), QStringLiteral("_phong"), QStringLiteral("lighting key")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "External-map lighting keys can be ignored."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Prefab lighting settings such as _phong may not carry into imported geometry."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on lighting keys attached to misc_external_map and suggest baking them into the prefab map.")),
		knownIssue(QStringLiteral("207"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("func_group"), QStringLiteral("external map"), QStringLiteral("corrupt")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "External maps made entirely of func_group brushes can corrupt output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Grouped prefab content may compile incorrectly when imported as an external map."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Validate prefab entity composition and require at least one safe import target path.")),
		knownIssue(QStringLiteral("199"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map"), QStringLiteral("relative path"), QStringLiteral("map-file-relative")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "External-map paths need map-file-relative handling."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Prefab references may break when the working directory differs from the source map location."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Resolve prefab paths through VibeStudio project services and pass a controlled working directory.")),
		knownIssue(QStringLiteral("194"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("_external_map_classname"), QStringLiteral("misc_external_map"), QStringLiteral("assert")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Missing external-map classname can crash the compile."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "A misc_external_map without _external_map_classname may hit an upstream assertion."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Require a classname during preflight, defaulting to func_detail only after user confirmation.")),
		knownIssue(QStringLiteral("193"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("0 360 0"), QStringLiteral("external map"), QStringLiteral("rotation"), QStringLiteral("shadow")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "External-map rotations can produce odd shadows."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Certain prefab rotations may light differently than expected."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Normalize prefab rotations and highlight transformed prefab lighting as a QA checkpoint.")),
		knownIssue(QStringLiteral("327"), QStringLiteral("D05"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("misc_external_map"), QStringLiteral("merge"), QStringLiteral("target entity"), QStringLiteral("prefab composition")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Multiple external-map pieces cannot be assumed to merge into one target entity."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Advanced prefab composition may produce separate or unexpected brush entities."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show composed prefab structure in the compile preview and keep grouped-target behavior documented as upstream work.")),

		knownIssue(QStringLiteral("444"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("region"), QStringLiteral("antiregion"), QStringLiteral("areaportal"), QStringLiteral("Quake 2")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Region brushes can prevent Quake 2 areaportals from working."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Partial Q2 compiles may produce misleading areaportal behavior."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show a region-compile warning when areaportals exist and recommend a full compile before shipping.")),
		knownIssue(QStringLiteral("422"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("region brush"), QStringLiteral("brush entities"), QStringLiteral("outside region")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Region builds may keep brush entities outside the requested region."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Partial compiles can include entity logic that the visible region does not explain."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "List out-of-region brush entities before starting a region compile.")),
		knownIssue(QStringLiteral("417"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("region+"), QStringLiteral("rotating door"), QStringLiteral("origin brush"), QStringLiteral("leak")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "region+ can leak with rotating doors that have origin brushes."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "A Q2 region+ compile may leak when rotating bmodels use origin brushes."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on origin-brush rotating doors in region+ profiles and suggest a full compile for validation.")),
		knownIssue(QStringLiteral("390"), QStringLiteral("D06"), CompilerKnownIssueSeverity::Info, true, qbspTool(), qbspProfile(), {QStringLiteral("multiple region brushes"), QStringLiteral("region brush"), QStringLiteral("partial compile")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Multiple region brushes are not supported as a union."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Editor selections using several regions may not match VibeMap2 region behavior."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Collapse region requests to one temporary region volume or explain the limitation before compile.")),

		knownIssue(QStringLiteral("484"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("LIGHTINGDIR"), QStringLiteral("_sunlight2"), QStringLiteral("Q2"), QStringLiteral("BSPX")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Q2 BSPX LIGHTINGDIR can be wrong with _sunlight2."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Directional lighting data may show artifacts when _sunlight2 is enabled."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn in Q2 lighting profiles and compare previews against non-directional output when needed.")),
		knownIssue(QStringLiteral("456"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("wrong shadows"), QStringLiteral("func_detail"), QStringLiteral("func_illusionary"), QStringLiteral("visblocker")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Structural meshes and func_* entities can cast inconsistent shadows."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Entity-based geometry may not light like equivalent structural brushes."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Flag mixed structural/detail shadow tests in map QA and keep versioned preview captures.")),
		knownIssue(QStringLiteral("441"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-vis")}, {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-vis")}, {QStringLiteral("func_illusionary_visblocker"), QStringLiteral("0.18.1"), QStringLiteral("visible faces")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "func_illusionary_visblocker behavior changed from older releases."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Maps relying on old visblocker behavior may render or vis differently."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Record compiler version and prompt for a regression check when visblockers are present.")),
		knownIssue(QStringLiteral("439"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("light bleed"), QStringLiteral("brush model"), QStringLiteral("2.0 alpha")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "2.0 alpha builds can show light bleed on brush model edges."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Brush models may receive edge lighting artifacts compared with older releases."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on affected version/profile combinations and preserve visual regression screenshots.")),
		knownIssue(QStringLiteral("405"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_phong"), QStringLiteral("phong_angle"), QStringLiteral("light bleed")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Phong settings can cause confusing smoothing and light bleed."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Phong-smoothed geometry may light incorrectly on hard cube-like edges."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on high-risk _phong and phong_angle combinations in the entity inspector.")),
		knownIssue(QStringLiteral("389"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Error, true, lightTool(), lightProfile(), {QStringLiteral("surface lights"), QStringLiteral("alpha 3"), QStringLiteral("Q2"), QStringLiteral("emit")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Q2 surface lights can stop emitting in alpha builds."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Surface-light textures may appear bright without lighting the room."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Track compiler version and run a small surface-light fixture for Q2 profiles.")),
		knownIssue(QStringLiteral("375"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("relighting"), QStringLiteral("old BSP"), QStringLiteral("too bright"), QStringLiteral("fullbright")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Relighting old Q2 BSPs can produce extreme brightness."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Existing BSP relight workflows may become fullbright, too bright, or too dark."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Require a preview pass and preserve the original BSP before relighting.")),
		knownIssue(QStringLiteral("364"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("corner artifacts"), QStringLiteral("grid point"), QStringLiteral("Q2 lightmaps")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Q2 lightmaps can show corner and grid-point artifacts."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Recent compiler ranges may introduce visible lightmap artifacts at face corners."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep a versioned lighting regression fixture for Q2 maps.")),
		knownIssue(QStringLiteral("221"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("sunlight"), QStringLiteral("shining through world"), QStringLiteral("sky geometry")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Sunlight can leak through world or sky geometry."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Sun lighting may appear in areas that should be occluded."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Flag sunlight-heavy maps for visual QA and keep leak/sky diagnostics visible.")),
		knownIssue(QStringLiteral("401"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("-tex_saturation_boost"), QStringLiteral("saturation"), QStringLiteral("brightness"), QStringLiteral("Kingpin")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Texture saturation boost can also change lighting brightness."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Relighting presets that adjust saturation may shift the overall brightness more than expected."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Treat saturation boost as a visual-regression checkpoint and keep before/after previews available.")),
		knownIssue(QStringLiteral("351"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("bouncescale"), QStringLiteral("_bouncescale"), QStringLiteral("per-light bounce"), QStringLiteral("bounce scale")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Per-light bounce scale has reported incorrect behavior."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Individual light bounce controls may not produce the expected indirect lighting contribution."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn when per-light bouncescale keys are present and compare against a conservative profile preview.")),
		knownIssue(QStringLiteral("349"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_dirt"), QStringLiteral("_dirtscale"), QStringLiteral("bmodels")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Dirt lighting keys can fail on brush models."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Brush entities may ignore dirt settings that work on worldspawn."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn when bmodels carry dirt keys and suggest checking baked lighting previews.")),
		knownIssue(QStringLiteral("291"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("deviance"), QStringLiteral("penumbra"), QStringLiteral("sky light entity"), QStringLiteral("_sunlight_penumbra")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Sky-light entity penumbra can behave differently from worldspawn sunlight."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Non-worldspawn sun setups may ignore or misapply deviance and penumbra values."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Prefer profile-visible sun settings and mark entity-based sky lighting for preview comparison.")),
		knownIssue(QStringLiteral("268"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_sun 1"), QStringLiteral("deviance"), QStringLiteral("penumbra"), QStringLiteral("sun entity")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Deviance can fail on light entities marked as sun sources."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Sunlight authored through `_sun 1` entities may not match penumbra expectations."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Flag `_sun 1` entities with deviance settings as lighting-regression candidates.")),
		knownIssue(QStringLiteral("262"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_mirrorinside"), QStringLiteral("mirrorinside"), QStringLiteral("shadow regression"), QStringLiteral("inside shadow")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "_mirrorinside has a tracked shadow regression."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Shadow output may differ from older compiler builds when mirrored-inside lighting is enabled."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep compiler version visible and ask for visual QA when `_mirrorinside` appears in a map.")),
		knownIssue(QStringLiteral("256"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_switchableshadow"), QStringLiteral("switchable shadow"), QStringLiteral("_shadow 1"), QStringLiteral("shadow casting")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Switchable shadows may not imply normal shadow casting."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Authors can expect switchable-shadow keys to cast shadows even when `_shadow` is not explicitly enabled."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on switchable-shadow entities without an explicit shadow policy in the inspector.")),
		knownIssue(QStringLiteral("255"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_switchableshadow"), QStringLiteral("skip"), QStringLiteral("bmodel"), QStringLiteral("switchable shadows")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Switchable shadows can interact badly with skip-textured bmodels."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Brush models using skip faces and switchable shadows may light incorrectly."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Preflight this entity combination and recommend a small preview compile before shipping.")),
		knownIssue(QStringLiteral("247"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("misc_model"), QStringLiteral("cast shadow"), QStringLiteral("model shadow"), QStringLiteral("prop shadow")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "misc_model shadow casting is a requested upstream feature."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Model props cannot be assumed to contribute baked shadows in current VibeMap2 compiles."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Expose model shadow expectations in asset diagnostics and suggest brush proxies when the target engine needs baked shadows.")),
		knownIssue(QStringLiteral("238"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("penumbra"), QStringLiteral("pitch"), QStringLiteral("yaw"), QStringLiteral("sunlight")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Sunlight penumbra can deviate differently in pitch and yaw."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Mostly vertical sunlight may produce softness that does not match the authored penumbra values."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show sun-vector previews and keep vertical sun setups in the lighting QA checklist.")),
		knownIssue(QStringLiteral("217"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_surface"), QStringLiteral("liquid"), QStringLiteral("surface light"), QStringLiteral("water")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Surface light settings can collide with liquid textures."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Liquid surface lights may be ignored, renamed, or matched differently than expected."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Validate `_surface` light names against liquid texture usage and show ambiguous matches before compile.")),
		knownIssue(QStringLiteral("214"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("-gate 4"), QStringLiteral("gate"), QStringLiteral("sunlight"), QStringLiteral("sun")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "High gate values can suppress sunlight."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Lighting profiles using `-gate 4` may accidentally remove expected sun contribution."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on high gate values in sunlight-heavy profiles and offer a lower-gate preview.")),
		knownIssue(QStringLiteral("203"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("disable bounce"), QStringLiteral("sunlight2"), QStringLiteral("sun bounce"), QStringLiteral("bounce")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Disabling bounce for sun sources is requested upstream behavior."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Profiles cannot assume sun or `_sunlight2` bounce can be disabled independently by the compiler."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Represent sun-bounce suppression as a profile limitation until a supported compiler option exists.")),
		knownIssue(QStringLiteral("190"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("bounce"), QStringLiteral("soft"), QStringLiteral("extra4"), QStringLiteral("messed up lighting")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Some bounce/soft/extra lighting combinations have reported visual regressions."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "High-quality lighting presets can still produce map-specific artifacts."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep high-quality preset output under visual regression review and preserve the exact compiler arguments.")),
		knownIssue(QStringLiteral("185"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("black seams"), QStringLiteral("shib5"), QStringLiteral("shib1"), QStringLiteral("seams")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Specific maps have reported black lighting seams."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Seam artifacts may persist even when the compile completes without errors."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Use seam reports as compatibility fixtures and keep visual inspection separate from process success.")),
		knownIssue(QStringLiteral("172"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("spotlight"), QStringLiteral("_phong 1"), QStringLiteral("models"), QStringLiteral("phong")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Spotlights can behave incorrectly on Phong-smoothed models."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Model or brush surfaces using `_phong 1` may receive invalid spotlighting."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn when spotlights target Phong-smoothed model geometry and suggest a controlled preview pass.")),
		knownIssue(QStringLiteral("145"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("H-shaped artifacts"), QStringLiteral("angled beam"), QStringLiteral("pedestals"), QStringLiteral("artifact")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Some angled-beam layouts have reported lighting artifacts."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Map-specific artifact reports can indicate compiler-version sensitivity rather than author error."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep artifact-prone maps as optional regression fixtures and cite the upstream issue in QA notes.")),
		knownIssue(QStringLiteral("140"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_dirtmode"), QStringLiteral("_dirtdepth"), QStringLiteral("_dirtgain"), QStringLiteral("func_group")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Dirt option support on non-worldspawn entities is incomplete upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Brush entities may not honor the same dirt controls that worldspawn uses."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Group this with bmodel dirt diagnostics and show which entity keys are compiler-supported for the active profile.")),
		knownIssue(QStringLiteral("134"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("hard lines"), QStringLiteral("dirtmap"), QStringLiteral("shadow regression"), QStringLiteral("hard shadow")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Hard dirt and shadow lines are tracked as a lighting regression."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Compiled lighting can show visible line artifacts despite valid map data."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Preserve compiler version data and include hard-line checks in visual regression passes.")),
		knownIssue(QStringLiteral("109"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("surface light"), QStringLiteral("submodel"), QStringLiteral("nudge"), QStringLiteral("bmodel")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Surface lights may need nudging on submodels."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Bmodel surface lights can sit exactly on geometry and fail to illuminate as expected."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on surface lights attached to submodels and suggest a small offset or preview check.")),
		knownIssue(QStringLiteral("91"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("contrasting textures"), QStringLiteral("bounce color"), QStringLiteral("subdivision"), QStringLiteral("bounce light")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Bounce color averaging can be too coarse on contrasting textures."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Large faces with varied texture colors may bounce an averaged color that does not match the visual detail."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Explain this as a lighting-quality limitation and favor authored fill lights when exact bounced color matters.")),
		knownIssue(QStringLiteral("377"), QStringLiteral("D07"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("autominlight"), QStringLiteral("bmodel"), QStringLiteral("minlight")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Global autominlight can override explicit bmodel minlight intent."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Brush-model minlight may not match author expectations when autominlight is enabled."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn when global autominlight and bmodel minlight are both present.")),

		knownIssue(QStringLiteral("451"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("textures folder"), QStringLiteral("loose texture"), QStringLiteral("custom engine")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Loose texture lookup expects a folder named textures."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Custom projects with nonstandard texture roots may fail to resolve assets."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Make texture roots explicit in project profiles and validate them before compile.")),
		knownIssue(QStringLiteral("455"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("func_detail_null"), QStringLiteral("compiler-only brush"), QStringLiteral("null detail"), QStringLiteral("shadow helper")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "func_detail_null is a requested compiler-only brush entity."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Advanced lighting or shadow helper brushes cannot be assumed to disappear safely at compile time."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Represent helper geometry as VibeStudio export metadata and show exactly what will reach qbsp.")),
		knownIssue(QStringLiteral("450"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("-notex"), QStringLiteral("non-WAD"), QStringLiteral("missing texture")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Non-WAD compiles can still report missing texture data."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Generated or external-texture BSP workflows may warn even when texture data is intentionally omitted."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Explain -notex/non-WAD profile behavior and separate expected warnings from real missing assets.")),
		knownIssue(QStringLiteral("346"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Info, true, {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-light")}, {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-light")}, {QStringLiteral("PCX"), QStringLiteral("PNG"), QStringLiteral("external textures"), QStringLiteral("palette")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "External texture format support is still expanding."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Some custom texture formats may not be accepted by VibeMap2."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Validate replacement textures through VibeStudio asset tools before starting a compile.")),
		knownIssue(QStringLiteral("201"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("-outputdir"), QStringLiteral("-waddir"), QStringLiteral("working directory")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Explicit output and WAD directories are a requested upstream feature."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Wrapper-controlled paths can be fragile when a compiler relies on its working directory."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Run compiles from an isolated, predictable working directory and record expected outputs.")),
		knownIssue(QStringLiteral("192"), QStringLiteral("D08"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("custom palette"), QStringLiteral("palette"), QStringLiteral("bounce lighting")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Light may assume the stock Quake palette in custom projects."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Bounce lighting can be wrong for total conversions with custom palettes."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Add a custom-palette profile warning and require visual QA for relit maps.")),

		knownIssue(QStringLiteral("463"), QString(), CompilerKnownIssueSeverity::Critical, true, {QStringLiteral("vibemap2-hub")}, {QStringLiteral("vibemap2-hub")}, {QStringLiteral("temporary directory"), QStringLiteral("overwrite")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Preview compiles should run in a temporary directory."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Preview compiles may write beside the source map and overwrite outputs."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Always launch preview-style compiles in an isolated VibeStudio work directory.")),
		knownIssue(QStringLiteral("435"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Error, true, {QStringLiteral("vibemap2-bsputil")}, {QStringLiteral("vibemap2-bsputil")}, {QStringLiteral("argument parsing"), QStringLiteral("dummy argument")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "bsputil argument parsing can reject valid operations."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Some bsputil wrapper commands may fail unless extra dummy arguments are supplied."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Smoke-test each bsputil operation before exposing it as a package or diagnostics action.")),
		knownIssue(QStringLiteral("482"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Warning, true, {QStringLiteral("vibemap2")}, {QStringLiteral("vibemap2")}, {QStringLiteral("Embree"), QStringLiteral("TBB"), QStringLiteral("Linux Mint"), QStringLiteral("build")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Embree/TBB build requirements can fail on older Linux distributions."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Local VibeMap2 builds may need newer compiler dependencies than the host provides."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Probe dependencies up front and show distro-specific build guidance.")),
		knownIssue(QStringLiteral("480"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Warning, false, hubTool(), hubProfile(), {QStringLiteral("Arch"), QStringLiteral("Nvidia"), QStringLiteral("Wayland")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "The hub (formerly lightpreview) can fail to launch on some Linux graphics stacks."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Qt/OpenGL context creation may fail before any compile work begins."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep the hub optional, expose launch diagnostics, and prefer VibeStudio previews where available.")),
		knownIssue(QStringLiteral("449"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("vibemap2")}, {QStringLiteral("vibemap2")}, {QStringLiteral("std::format"), QStringLiteral("fmt"), QStringLiteral("logging"), QStringLiteral("format")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Upstream logging format internals may change."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Forks or log parsers that depend on third-party formatting details could be fragile."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Parse stable diagnostic content rather than assuming a particular upstream logging library.")),
		knownIssue(QStringLiteral("289"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, true, {QStringLiteral("vibemap2-bsputil")}, {QStringLiteral("vibemap2-bsputil")}, {QStringLiteral("bspinfo"), QStringLiteral("diagnostics"), QStringLiteral("light stats"), QStringLiteral("entity extraction")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Richer bspinfo-style diagnostics are requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Current tools may not expose every statistic VibeStudio wants in a structured way."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep VibeStudio diagnostics pane tolerant of missing fields and preserve raw tool output.")),
		knownIssue(QStringLiteral("335"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Warning, false, {QStringLiteral("vibemap2")}, {QStringLiteral("vibemap2")}, {QStringLiteral("binary name"), QStringLiteral("light"), QStringLiteral("vis"), QStringLiteral("tool discovery")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Generic binary names can conflict with other packages."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Launching bare `light` or `vis` may resolve to the wrong executable on some systems."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Use VibeStudio tool descriptors, version probes, and explicit executable paths instead of bare command names alone.")),
		knownIssue(QStringLiteral("225"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, bsputilTool(), bsputilProfile(), {QStringLiteral("bspinfo"), QStringLiteral("log file"), QStringLiteral("write log"), QStringLiteral("diagnostics log")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "bspinfo does not provide every requested log-file workflow upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Helper-tool diagnostics may only be available through captured process output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Capture stdout and stderr in VibeStudio manifests and expose them as the durable diagnostic log.")),
		knownIssue(QStringLiteral("223"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("vibemap2")}, {QStringLiteral("vibemap2")}, {QStringLiteral("Darwin"), QStringLiteral("macOS"), QStringLiteral("package suffix"), QStringLiteral("release artifact")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "macOS release artifact naming is tracked upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Automated tool discovery may encounter package names that use older platform wording."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Match release artifacts by descriptor metadata and checksums instead of display suffix alone.")),
		knownIssue(QStringLiteral("215"), QStringLiteral("D09"), CompilerKnownIssueSeverity::Info, false, {QStringLiteral("vibemap2")}, {QStringLiteral("vibemap2")}, {QStringLiteral("Flatpak"), QStringLiteral("Flathub"), QStringLiteral("Linux package"), QStringLiteral("bundle")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Flatpak packaging is an upstream distribution request."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Linux users may not have a single supported package route for VibeMap2."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep VibeStudio setup able to use user-selected executables, submodule builds, or future package channels.")),

		knownIssue(QStringLiteral("464"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("func_viscluster"), QStringLiteral("VIS cluster"), QStringLiteral("optimization")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "func_viscluster-style VIS optimization is not available upstream yet."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Large open maps may still require expensive VIS runs without cluster hints."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Present this as future optimization work and keep current VIS estimates conservative.")),
		knownIssue(QStringLiteral("443"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("entity order"), QStringLiteral("map hack"), QStringLiteral("custom model"), QStringLiteral("*n model")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Stable entity order for map-hack custom models is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Map hacks that refer to numbered bmodels can break when compiler output ordering changes."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show entity and bmodel ordering as fragile compatibility data and avoid reordering source entities silently.")),
		knownIssue(QStringLiteral("413"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("translucency"), QStringLiteral("light transmission"), QStringLiteral("tinting"), QStringLiteral("thin material")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Translucency lighting support is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Thin or translucent material lighting cannot be assumed in current VibeMap2 profiles."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep translucent-material intent visible in the asset inspector and mark compiler transmission as unsupported unless detected.")),
		knownIssue(QStringLiteral("411"), QString(), CompilerKnownIssueSeverity::Info, false, bsputilTool(), bsputilProfile(), {QStringLiteral("decompiled output"), QStringLiteral("special texture"), QStringLiteral("no nodes"), QStringLiteral("round trip")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "A special decompile texture is requested for round-trip output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Decompiler helper textures may accidentally generate nodes, clipnodes, or faces if reused in source maps."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Mark decompiled maps as review-required and keep helper textures isolated from normal project assets.")),
		knownIssue(QStringLiteral("205"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("one-way VIS"), QStringLiteral("one-way leaf"), QStringLiteral("window")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "One-way VIS leaves are a requested feature."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Special window-like visibility behavior cannot be assumed in current compiles."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn profile authors that this requires engine/compiler support, not just editor metadata.")),
		knownIssue(QStringLiteral("131"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("visapprox"), QStringLiteral("occluding bars"), QStringLiteral("falloff light")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "visapprox can be misled by long falloff lights through occluders."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Approximate lighting may differ from final visibility-aware lighting in complex layouts."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Mark approximate lighting previews as provisional for maps with thin occluders.")),
		knownIssue(QStringLiteral("103"), QStringLiteral("D10"), CompilerKnownIssueSeverity::Info, false, visTool(), visProfile(), {QStringLiteral("hint brush"), QStringLiteral("liquids"), QStringLiteral("horizontal")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Hint brushes have reported VIS edge cases."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Liquid or horizontal hint layouts may not optimize visibility as expected."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep hint-brush troubleshooting guidance available from VIS diagnostics.")),

		knownIssue(QStringLiteral("485"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("world_units_per_luxel"), QStringLiteral("luxel"), QStringLiteral("_lmscale")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "world_units_per_luxel cannot be forcibly overridden yet."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Quality presets may not override every per-entity luxel density."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show the override as a planned capability and list entities that keep their own value.")),
		knownIssue(QStringLiteral("436"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("disable lightmaps"), QStringLiteral("fullbright"), QStringLiteral("skip lightmap"), QStringLiteral("lightmap allocation")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Disabling lightmaps per face or brush is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Fullbright or special-case surfaces may still consume normal lightmap handling."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Expose this as a compile-efficiency limitation and keep export-time alternatives separate from compiler behavior.")),
		knownIssue(QStringLiteral("425"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("lightgrid"), QStringLiteral("playable volume"), QStringLiteral("unreachable points"), QStringLiteral("calculation speed")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Lightgrid calculation can spend time on unreachable space."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Large Q2 or remaster maps may have longer lightgrid passes than users expect."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show lightgrid work as its own progress stage and plan future playable-volume hints as editor metadata.")),
		knownIssue(QStringLiteral("374"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, true, lightTool(), lightProfile(), {QStringLiteral("embedded lightmaps"), QStringLiteral("bake lightmaps"), QStringLiteral("diffuse textures"), QStringLiteral("export")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Embedded lightmap baking is a requested upstream feature."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Engines without standard lightmap support may need a separate export transform."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Treat baked diffuse-lightmap output as VibeStudio packaging work unless a compiler version provides native support.")),
		knownIssue(QStringLiteral("249"), QStringLiteral("D11"), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-lmscale"), QStringLiteral("-lmshift"), QStringLiteral("legacy lightmap scale"), QStringLiteral("BSPX")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Legacy lightmap-scale compatibility remains under upstream discussion."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Profiles that mix old `-lmscale` or `-lmshift` behavior with BSPX metadata can confuse compatibility expectations."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Prefer explicit profile wording around modern BSPX metadata and keep legacy flags visible in manifests.")),
		knownIssue(QStringLiteral("470"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("_minlight"), QStringLiteral("minlight scale"), QStringLiteral("minlight value")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "_minlight scale differs across output formats."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Minlight values can be misread when moving between Q1, Q2, and related profiles."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Apply profile-aware validation and offer value conversion hints.")),
		knownIssue(QStringLiteral("343"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Info, true, qbspTool(), qbspProfile(), {QStringLiteral("conditional compile"), QStringLiteral("entity set"), QStringLiteral("build profile")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Conditional entity compilation is not native upstream behavior."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Maps with alternate mod/entity sets need VibeStudio-side build-profile handling."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Route conditional entities through VibeStudio export transforms before qbsp.")),
		knownIssue(QStringLiteral("301"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("hull sizes"), QStringLiteral("total conversion"), QStringLiteral("custom hull")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Custom hull sizes are a requested total-conversion feature."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Nonstandard player hulls may not compile correctly with stock assumptions."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn in custom-engine profiles and keep hull settings explicit in project metadata.")),
		knownIssue(QStringLiteral("437"), QStringLiteral("D12"), CompilerKnownIssueSeverity::Info, false, allCompileTools(), allCompileProfiles(), {QStringLiteral("HLBSP"), QStringLiteral("GoldSrc"), QStringLiteral("Half-Life"), QStringLiteral("parity")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "GoldSrc HLBSP parity is incomplete upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Half-Life-style maps may rely on original compiler behaviors that VibeMap2 does not fully match."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep GoldSrc compatibility profile warnings explicit and avoid claiming original-compiler parity.")),

		knownIssue(QStringLiteral("350"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("leak"), QStringLiteral("face splitting"), QStringLiteral("excessive")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Leaking maps can trigger excessive face splitting."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Leak compiles may produce noisy or expensive geometry output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Prioritize leak repair diagnostics before trusting compile metrics.")),
		knownIssue(QStringLiteral("344"), QString(), CompilerKnownIssueSeverity::Critical, true, qbspTool(), qbspProfile(), {QStringLiteral("bad surface extents"), QStringLiteral("surface extents"), QStringLiteral("crash")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Bad surface extents can create BSPs that crash in-game."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Compiled geometry may be accepted by the tool but fail at runtime."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Run post-compile BSP validation and block packaging on critical surface extent errors.")),
		knownIssue(QStringLiteral("308"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("origin key"), QStringLiteral("brush entity"), QStringLiteral("leak")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Brush entities with origin keys can affect leak detection."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Entity origins may cause false or confusing leak results."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Validate brush-entity origin usage before qbsp and point leaks back to source entities.")),
		knownIssue(QStringLiteral("278"), QString(), CompilerKnownIssueSeverity::Critical, true, {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-light")}, {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-light")}, {QStringLiteral("corrupt BSP"), QStringLiteral("Hexen2"), QStringLiteral("misidentified")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Corrupt Q1 BSPs can be misidentified as Hexen2."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "A compile/light pipeline may produce a BSP with the wrong detected format."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Inspect BSP headers and lump sanity after compile before registering the artifact.")),
		knownIssue(QStringLiteral("270"), QString(), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("outside filling"), QStringLiteral("24 units"), QStringLiteral("monsters"), QStringLiteral("floor")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Floor-offset monsters may not block outside filling consistently."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Leak/outside-fill results can change around entities placed above the floor."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show entity placement warnings near outside-fill boundaries and verify the leak path visually.")),
		knownIssue(QStringLiteral("264"), QString(), CompilerKnownIssueSeverity::Critical, true, qbspTool(), qbspProfile(), {QStringLiteral("leafs with content 0"), QStringLiteral("Hexen2"), QStringLiteral("invisible")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Hexen2 BSP2 maps can contain invalid content-zero leafs."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Entities may become invisible in affected BSP output."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Run BSP leaf-content validation for Hexen2/BSP2 profiles.")),
		knownIssue(QStringLiteral("257"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("non-integer vertices"), QStringLiteral("missing faces"), QStringLiteral("grid")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Non-integer brush vertices can produce missing faces."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Off-grid geometry may compile with absent faces or wrong contents."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on unsnapped brush vertices and provide a safe snap/repair action.")),
		knownIssue(QStringLiteral("342"), QString(), CompilerKnownIssueSeverity::Warning, false, bsputilTool(), bsputilProfile(), {QStringLiteral("empty texture name"), QStringLiteral("decompile"), QStringLiteral("extract textures"), QStringLiteral("texture scale")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Empty texture names can break decompile and texture extraction workflows."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Round-tripped maps may receive incorrect texture mapping or scale when unnamed textures are present."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Validate BSP texture names before extraction and label decompile output as lossy when names are empty.")),
		knownIssue(QStringLiteral("310"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("angle"), QStringLiteral("mangle"), QStringLiteral("Q2 light"), QStringLiteral("sun direction")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Q2 light `angle` can interfere with `mangle`."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Directional lights may ignore the intended mangle when a nonzero angle is also present."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on Q2 light entities that define both keys and ask the user to choose one direction source.")),
		knownIssue(QStringLiteral("298"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("mapversion 220"), QStringLiteral("engine warning"), QStringLiteral("metadata"), QStringLiteral("strip mapversion")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Editor mapversion metadata may need export cleanup."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Some engines can warn about `mapversion 220` even when the compiler accepts it."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Offer export cleanup as a VibeStudio transform and keep the original source map unchanged.")),
		knownIssue(QStringLiteral("297"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_dirt_off_radius"), QStringLiteral("_dirt_on_radius"), QStringLiteral("dirt radius"), QStringLiteral("documentation")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Dirt radius keys need clearer upstream documentation."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Users may not know whether `_dirt_off_radius` and `_dirt_on_radius` are supported in the active compiler build."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show these keys through profile-aware help only when the selected tool version is known to support them.")),
		knownIssue(QStringLiteral("285"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("constant color"), QStringLiteral("translucency"), QStringLiteral("translucent color"), QStringLiteral("material")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Constant translucency color is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Material workflows cannot assume the compiler can force a fixed transmission color."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Represent constant-color translucency as a future material feature and keep current lighting previews conservative.")),
		knownIssue(QStringLiteral("281"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("-nosunlight"), QStringLiteral("disable sunlight"), QStringLiteral("sunlight override"), QStringLiteral("sun")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "A global no-sunlight option is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Quick compile profiles cannot rely on a native `-nosunlight` switch unless the selected compiler supports it."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Offer VibeStudio-side export/profile alternatives and clearly mark them as local transforms.")),
		knownIssue(QStringLiteral("277"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("alternate position"), QStringLiteral("moving door"), QStringLiteral("secret door"), QStringLiteral("alternate lightmaps")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Alternate-position lighting for moving models is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Animated bmodels may be baked only at their authored position."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show moving-model lighting as an advanced QA risk and preserve any alternate-lighting metadata separately.")),
		knownIssue(QStringLiteral("254"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("-forcegoodtree"), QStringLiteral("planenum"), QStringLiteral("Hexen2"), QStringLiteral("BSP2")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "`-forcegoodtree` can hit a planenum assertion for Hexen2 BSP2."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Risky tree-building flags may crash or fail the compile for some Hexen2 profiles."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn on `-forcegoodtree` in Hexen2/BSP2 profiles and suggest a standard qbsp pass first.")),
		knownIssue(QStringLiteral("246"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("brightness"), QStringLiteral("contrast"), QStringLiteral("final lightmap"), QStringLiteral("post adjustment")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Final lightmap brightness and contrast adjustment is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Compile profiles cannot assume the compiler can apply final post-lighting contrast controls."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep post-lightmap adjustment as an explicit export or preview operation unless native support appears.")),
		knownIssue(QStringLiteral("241"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("brush as light"), QStringLiteral("light volume"), QStringLiteral("skip brush"), QStringLiteral("lighting design")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Brush-volume lighting is a requested authoring feature."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Brushes used as lighting guides may still affect qbsp unless VibeStudio exports them safely."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Model brush-as-light workflows as editor metadata and show exactly what is stripped before qbsp.")),
		knownIssue(QStringLiteral("234"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("HSV"), QStringLiteral("hue"), QStringLiteral("saturation"), QStringLiteral("value")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Global HSV lightmap adjustment is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Color-grading style controls may not be available in the selected compiler."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Expose global HSV changes as preview/export processing only when the compiler lacks native support.")),
		knownIssue(QStringLiteral("233"), QString(), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("no valid brushes"), QStringLiteral("func_detail_illusionary"), QStringLiteral("valid brushes"), QStringLiteral("generated map")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "func_detail_illusionary can trigger no-valid-brush diagnostics."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Generated or model-style maps may produce confusing brush-validity failures."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Attach the diagnostic to the source entity and show which brushes are compiler-eligible.")),
		knownIssue(QStringLiteral("222"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("rangescale"), QStringLiteral("Q2"), QStringLiteral("range scale"), QStringLiteral("cleanup")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Q2 rangescale defaults are tracked for upstream cleanup."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Profiles should not depend on undocumented internal rangescale representation."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep Q2 light-range assumptions explicit in VibeStudio profile metadata.")),
		knownIssue(QStringLiteral("213"), QString(), CompilerKnownIssueSeverity::Warning, false, qbspTool(), qbspProfile(), {QStringLiteral("-convert valve"), QStringLiteral("valve postfix"), QStringLiteral("destination filename"), QStringLiteral("-valve")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Valve conversion can append an unexpected postfix."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Wrapper-provided destination filenames may still receive a `-valve` suffix from qbsp."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Predict and display the expected output path before launch, then register only the artifact VibeStudio requested.")),
		knownIssue(QStringLiteral("209"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("_project_texture"), QStringLiteral("_surface"), QStringLiteral("projected texture"), QStringLiteral("surface light")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Projected textures on surface lights are tracked as broken."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Advanced surface-light projection may not affect lighting as authored."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Warn when `_project_texture` is combined with `_surface` and preserve the setup for upstream regression reporting.")),
		knownIssue(QStringLiteral("200"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("random texture tiling"), QStringLiteral("texture randomization"), QStringLiteral("tiling"), QStringLiteral("variation")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Random texture tiling is a requested compiler feature."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Texture variation cannot be assumed at compile time for current VibeMap2 profiles."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep random tiling as an editor/export feature unless a selected compiler version advertises support.")),
		knownIssue(QStringLiteral("195"), QString(), CompilerKnownIssueSeverity::Warning, false, lightTool(), lightProfile(), {QStringLiteral("AD start.bsp"), QStringLiteral("Arcane Dimensions"), QStringLiteral("odd face"), QStringLiteral("visual compatibility")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "A specific Arcane Dimensions face has reported odd lighting or rendering."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Compatibility maps can reveal visual regressions that do not show up as compiler errors."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Treat this as optional compatibility-fixture coverage and keep generated outputs versioned.")),
		knownIssue(QStringLiteral("189"), QString(), CompilerKnownIssueSeverity::Info, false, qbspTool(), qbspProfile(), {QStringLiteral("_noskydeletion"), QStringLiteral("sky brushes"), QStringLiteral("skybox"), QStringLiteral("lightmap workflow")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Preserving brushes inside sky is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Sky-contained helper brushes may be deleted or compiled differently than an advanced skybox workflow expects."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Show sky-deletion assumptions in export previews and keep helper geometry marked as editor-owned.")),
		knownIssue(QStringLiteral("186"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("_falloff"), QStringLiteral("delay 1"), QStringLiteral("delay 2"), QStringLiteral("delay 5")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Falloff control is not available for every requested delay mode."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Light entities using unsupported delay/falloff combinations may not match author expectations."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Validate falloff keys against the active profile and explain unsupported combinations before compile.")),
		knownIssue(QStringLiteral("136"), QString(), CompilerKnownIssueSeverity::Warning, true, qbspTool(), qbspProfile(), {QStringLiteral("clipnodes"), QStringLiteral("marksurfaces"), QStringLiteral("txqbsp-xt"), QStringLiteral("compile metrics")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Clipnode and marksurface counts can differ from other compilers."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Maps may approach engine limits sooner with one compiler than another."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Track compile metrics in manifests and show comparisons as guidance rather than claiming compiler equivalence.")),
		knownIssue(QStringLiteral("106"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("specific lights"), QStringLiteral("sunlight bounce"), QStringLiteral("per-light bounce"), QStringLiteral("bounce control")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Per-light and sunlight bounce controls are requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Current profiles may not be able to target bounce behavior at the exact light-source level."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Expose per-source bounce as an advanced limitation and keep authored intent visible in the inspector.")),
		knownIssue(QStringLiteral("81"), QString(), CompilerKnownIssueSeverity::Info, false, lightTool(), lightProfile(), {QStringLiteral("flip projected textures"), QStringLiteral("projected texture"), QStringLiteral("texture projection"), QStringLiteral("flip")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Projected texture flipping is requested upstream."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Texture projection controls may lack a native flip operation in current light compiles."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Keep projection orientation editable in VibeStudio metadata and warn when the compiler cannot express it.")),
		knownIssue(QStringLiteral("230"), QString(), CompilerKnownIssueSeverity::Warning, true, lightTool(), lightProfile(), {QStringLiteral("double extension"), QStringLiteral("dotted filename"), QStringLiteral(".1.bsp")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Dotted BSP filenames can be handled incorrectly by light."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Output paths such as my-map.1.bsp may lose part of the intended name."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Use wrapper-controlled output paths and include dotted filenames in path smoke tests.")),
		knownIssue(QStringLiteral("114"), QString(), CompilerKnownIssueSeverity::Error, true, qbspTool(), qbspProfile(), {QStringLiteral("csg_fail"), QStringLiteral("leaf contents"), QStringLiteral("wrong contents"), QStringLiteral("content type")},
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Known CSG edge cases can assign the wrong leaf contents."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Problematic brush/content layouts may compile with incorrect contents classification."),
			QT_TRANSLATE_NOOP("VibeStudioCompilerKnownIssues", "Run geometry/content preflight fixtures and keep suspicious CSG failures visible in map-health diagnostics.")),
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
			if (textContainsKeyword(text, keyword)) {
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
	lines << QCoreApplication::translate("VibeStudioCompilerKnownIssues", "ericw-tools issue #%1").arg(issue.issueId);
	if (!issue.clusterId.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Cluster: %1").arg(issue.clusterId);
	}
	lines << QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Severity: %1").arg(compilerKnownIssueSeverityText(issue.severity));
	lines << QCoreApplication::translate("VibeStudioCompilerKnownIssues", "High value: %1").arg(issue.highValue ? QCoreApplication::translate("VibeStudioCompilerKnownIssues", "yes") : QCoreApplication::translate("VibeStudioCompilerKnownIssues", "no"));
	if (!issue.affectedToolIds.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Tools: %1").arg(issue.affectedToolIds.join(QStringLiteral(", ")));
	}
	if (!issue.affectedProfileIds.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerKnownIssues", "Profiles: %1").arg(issue.affectedProfileIds.join(QStringLiteral(", ")));
	}
	lines << issue.summaryText;
	lines << issue.warningText;
	lines << issue.actionText;
	lines << issue.upstreamUrl;
	return lines.join('\n');
}

QStringList vibemap2KnownIssuePlanWarnings(const QString& profileId, const QString& inputPath, const QStringList& arguments)
{
	QStringList warnings;
	if (!profileId.startsWith(QStringLiteral("vibemap2-"), Qt::CaseInsensitive)) {
		return warnings;
	}

	// Only genuinely matched, actionable issues belong here. The "N tracked issues" summary is
	// informational and is surfaced through the plan's known-issue notes instead, so that merely
	// having a catalog entry can never downgrade a clean run to Warning.
	QStringList appendedIssueIds;
	auto appendIssue = [&warnings, &appendedIssueIds](const QString& issueId) {
		CompilerKnownIssueDescriptor issue;
		if (compilerKnownIssueForId(issueId, &issue) && !appendedIssueIds.contains(issue.issueId)) {
			appendedIssueIds << issue.issueId;
			warnings << QCoreApplication::translate("VibeStudioCompilerKnownIssues", "ericw-tools #%1: %2 Action: %3").arg(issue.issueId, issue.warningText, issue.actionText);
		}
	};

	const QString context = QStringLiteral("%1\n%2").arg(inputPath, arguments.join(QLatin1Char('\n')));
	for (const CompilerKnownIssueMatch& match : matchCompilerKnownIssues(context, QString(), profileId)) {
		if (match.issue.highValue) {
			appendIssue(match.issue.issueId);
		}
	}

	if (profileId.compare(QStringLiteral("vibemap2-light"), Qt::CaseInsensitive) == 0 && QFileInfo(inputPath).completeBaseName().contains('.')) {
		appendIssue(QStringLiteral("230"));
	}
	if (profileId.compare(QStringLiteral("vibemap2-light"), Qt::CaseInsensitive) == 0
		&& (arguments.contains(QStringLiteral("-bspxhdr"), Qt::CaseInsensitive) || arguments.contains(QStringLiteral("-bspxlux"), Qt::CaseInsensitive) || arguments.contains(QStringLiteral("-wrnormals"), Qt::CaseInsensitive))) {
		appendIssue(QStringLiteral("484"));
	}
	if (profileId.compare(QStringLiteral("vibemap2-bsp"), Qt::CaseInsensitive) == 0 && arguments.contains(QStringLiteral("-notex"), Qt::CaseInsensitive)) {
		appendIssue(QStringLiteral("450"));
	}

	warnings.removeDuplicates();
	return warnings;
}

} // namespace vibestudio
