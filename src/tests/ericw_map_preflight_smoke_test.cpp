#include "core/ericw_map_preflight.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool hasCode(const vibestudio::EricwMapPreflightResult& result, const QString& code)
{
	for (const vibestudio::EricwMapPreflightWarning& warning : result.warnings) {
		if (warning.code == code) {
			return true;
		}
	}
	return false;
}

bool hasIssueNumber(const vibestudio::EricwMapPreflightResult& result, int issueNumber)
{
	const QString suffix = QStringLiteral("#%1").arg(issueNumber);
	for (const vibestudio::EricwMapPreflightWarning& warning : result.warnings) {
		if (warning.upstreamIssue.endsWith(suffix)) {
			return true;
		}
	}
	return false;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QString longValue(1100, QLatin1Char('x'));
	const QString mapText = QStringLiteral(R"MAP(
{
"classname" "worldspawn"
"wad" "C:\Users\Mapper\quake\id1\gfx.wad;gfx/base.wad"
"WAD" "base.wad"
"message" "%1"
"Message" "case collision"
"note" "uses\backslash"
"_sunlight2" "1"
"_minlight" "0.05"
"_world_units_per_luxel" "16"
"_sunlight_mangle" "0 -90 0"
"_penumbra" "8"
"_bounce" "1"
"_soft" "1"
"_extra4" "1"
"_compile_if" "deathmatch"
"_qbsp_options" "hexen2 bsp2 forcegoodtree"
}
{
"classname" "misc_external_map"
"_external_map" "..\prefabs\room.map"
"_external_map_target" "merged_detail"
}
{
"classname" "misc_external_map"
"_external_map" "prefabs/grouped.map"
"_external_map_classname" "func_group"
"_external_map_target" "merged_detail"
"_phong" "1"
}
{
"classname" "light_spot"
"targetname" "switch_a"
"style" "32"
"spawnflags" "1"
"angle" "not-a-number"
"mangle" "0 -90 0"
"_surface" "*water"
"_project_texture" "textures/beam"
"_phong" "1"
"_sun" "1"
"_deviance" "4"
}
{
"classname" "light"
"targetname" "switch_a"
"spawnflags" "0"
}
{
"classname" "func_door_rotating"
"origin" "0 0 0"
"_phong" "1"
"_dirt" "1"
"_light" "200"
"_mirrorinside" "1"
"_switchableshadow" "1"
{
( 0.5 0 0 ) ( 64 0 0 ) ( 0 64 0 ) SKIP 0 0 0 1 1
( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) "" 0 0 0 1 1
}
}
{
"classname" "misc_model"
"model" "*4"
"_shadow" "1"
"_shadowself" "1"
"_phong" "1"
}
{
"classname" "monster_ogre"
"origin" "0 0 24"
}
{
"classname" "func_detail_illusionary"
}
{
"classname" "func_detail_null"
}
{
"classname" "func_region"
}
{
"classname" "func_region"
}
{
"classname" "func_areaportal"
}
)MAP").arg(longValue);

	vibestudio::EricwMapPreflightOptions options;
	options.mapPath = QStringLiteral("maps/start.v1.map");
	options.regionCompile = true;
	const vibestudio::EricwMapPreflightResult result = vibestudio::validateEricwMapPreflightText(mapText, options);
	if (!result.parseComplete) {
		return fail("Expected preflight parser to complete.");
	}
	if (result.entityCount != 13 || result.brushEntityCount != 1) {
		return fail("Expected entity and brush-entity counts.");
	}

	const QStringList expectedCodes = {
		QStringLiteral("entity-key-case-collision"),
		QStringLiteral("parser-sensitive-backslash"),
		QStringLiteral("wad-absolute-path"),
		QStringLiteral("wad-search-path-needed"),
		QStringLiteral("long-key-value"),
		QStringLiteral("escape-backslash"),
		QStringLiteral("invalid-numeric-key-value"),
		QStringLiteral("external-map-missing-classname"),
		QStringLiteral("external-map-path-portability"),
		QStringLiteral("external-map-func-group-risk"),
		QStringLiteral("external-map-lighting-key-risk"),
		QStringLiteral("external-map-entity-import-risk"),
		QStringLiteral("external-map-merge-target-risk"),
		QStringLiteral("light-style-conflict"),
		QStringLiteral("light-start-off-conflict"),
		QStringLiteral("q2-angle-mangle"),
		QStringLiteral("spotlight-phong-risk"),
		QStringLiteral("liquid-surface-light-risk"),
		QStringLiteral("surface-project-texture-risk"),
		QStringLiteral("brush-entity-origin-key"),
		QStringLiteral("bmodel-dirt-key-risk"),
		QStringLiteral("brush-as-light-marker"),
		QStringLiteral("switchable-shadow-skip-risk"),
		QStringLiteral("switchable-shadow-missing-shadow-key"),
		QStringLiteral("mirrorinside-shadow-risk"),
		QStringLiteral("sun-or-self-shadow-risk"),
		QStringLiteral("misc-model-shadow-expectation"),
		QStringLiteral("monster-outside-fill-origin-risk"),
		QStringLiteral("sky-light-penumbra-risk"),
		QStringLiteral("vertical-sunlight-penumbra-risk"),
		QStringLiteral("bounce-soft-extra-risk"),
		QStringLiteral("empty-brush-entity"),
		QStringLiteral("empty-texture-name"),
		QStringLiteral("conditional-compile-entity-key"),
		QStringLiteral("maphack-model-ordering-key"),
		QStringLiteral("hexen2-bsp2-forcegoodtree-risk"),
		QStringLiteral("func-detail-null-marker"),
		QStringLiteral("non-integer-brush-coordinate"),
		QStringLiteral("region-origin-brush-risk"),
		QStringLiteral("dotted-map-filename"),
		QStringLiteral("multiple-region-markers"),
		QStringLiteral("phong-bmodel-risk"),
		QStringLiteral("region-brush-entity-cull-risk"),
		QStringLiteral("region-areaportal-risk"),
		QStringLiteral("minlight-scale-ambiguity"),
		QStringLiteral("q2-sunlight2-lightingdir-risk"),
		QStringLiteral("world-units-per-luxel-override-risk"),
	};
	for (const QString& code : expectedCodes) {
		if (!hasCode(result, code)) {
			std::cerr << "Missing warning code: " << code.toStdString() << "\n";
			return EXIT_FAILURE;
		}
	}
	const QList<int> expectedIssueNumbers = {
		87,
		121,
		122,
		129,
		135,
		140,
		173,
		172,
		190,
		194,
		199,
		201,
		207,
		209,
		217,
		230,
		231,
		233,
		238,
		241,
		245,
		247,
		254,
		255,
		256,
		257,
		262,
		268,
		270,
		288,
		291,
		293,
		308,
		310,
		327,
		333,
		342,
		343,
		349,
		390,
		405,
		417,
		422,
		443,
		444,
		455,
		470,
		475,
		484,
		485,
	};
	for (const int issueNumber : expectedIssueNumbers) {
		if (!hasIssueNumber(result, issueNumber)) {
			std::cerr << "Missing upstream issue coverage: #" << issueNumber << "\n";
			return EXIT_FAILURE;
		}
	}

	QString denseMap = QStringLiteral(R"MAP(
{
"classname" "func_detail"
)MAP");
	for (int index = 0; index < 512; ++index) {
		denseMap += QStringLiteral(R"MAP(
{
( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) stone 0 0 0 1 1
}
)MAP");
	}
	denseMap += QStringLiteral(R"MAP(
}
)MAP");
	const vibestudio::EricwMapPreflightResult denseResult = vibestudio::validateEricwMapPreflightText(denseMap);
	if (!hasCode(denseResult, QStringLiteral("high-brush-count-clipnode-risk")) || !hasIssueNumber(denseResult, 136)) {
		return fail("Expected dense detail brush map to warn about clipnode and marksurface risk.");
	}

	const vibestudio::EricwMapPreflightResult cleanResult = vibestudio::validateEricwMapPreflightText(QStringLiteral(R"MAP(
{
"classname" "worldspawn"
"wad" "base.wad"
}
{
"classname" "light"
"targetname" "switch_b"
"spawnflags" "1"
}
{
"classname" "light"
"targetname" "switch_b"
"spawnflags" "1"
}
)MAP"));
	if (!cleanResult.parseComplete || !cleanResult.warnings.isEmpty()) {
		return fail("Expected conservative clean map to pass without warnings.");
	}

	if (vibestudio::ericwMapPreflightSeverityId(vibestudio::EricwMapPreflightSeverity::Warning) != QStringLiteral("warning")) {
		return fail("Expected severity id helper.");
	}

	return EXIT_SUCCESS;
}
