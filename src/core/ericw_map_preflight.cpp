#include "core/ericw_map_preflight.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace vibestudio {

namespace {

struct MapKeyValue {
	QString key;
	QString value;
	int line = 0;
};

struct MapEntity {
	QVector<MapKeyValue> values;
	QString rawText;
	int startLine = 0;
	int brushCount = 0;
	bool hasBrush = false;
	bool hasOriginBrush = false;
	bool hasSkipTexture = false;
	bool hasEmptyTextureName = false;
	bool hasNonIntegerBrushCoordinate = false;
};

struct LightGroupState {
	bool sawStartOff = false;
	bool sawStartOn = false;
	int firstLine = 0;
};

QString preflightText(const char* source)
{
	return QCoreApplication::translate("VibeStudioEricwMapPreflight", source);
}

QString issueId(int number)
{
	return QStringLiteral("ericw-tools #%1").arg(number);
}

QString cleanClassName(const MapEntity& entity)
{
	for (const MapKeyValue& item : entity.values) {
		if (item.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
			return item.value.trimmed();
		}
	}
	return {};
}

QString firstValue(const MapEntity& entity, const QString& key)
{
	for (const MapKeyValue& item : entity.values) {
		if (item.key.compare(key, Qt::CaseInsensitive) == 0) {
			return item.value;
		}
	}
	return {};
}

bool hasKey(const MapEntity& entity, const QString& key)
{
	for (const MapKeyValue& item : entity.values) {
		if (item.key.compare(key, Qt::CaseInsensitive) == 0) {
			return true;
		}
	}
	return false;
}

bool hasAnyKey(const MapEntity& entity, const QStringList& keys)
{
	for (const QString& key : keys) {
		if (hasKey(entity, key)) {
			return true;
		}
	}
	return false;
}

QVector<MapKeyValue> valuesForKey(const MapEntity& entity, const QString& key)
{
	QVector<MapKeyValue> values;
	for (const MapKeyValue& item : entity.values) {
		if (item.key.compare(key, Qt::CaseInsensitive) == 0) {
			values.push_back(item);
		}
	}
	return values;
}

bool keyValueEquals(const MapEntity& entity, const QString& key, const QString& expected)
{
	for (const MapKeyValue& item : entity.values) {
		if (item.key.compare(key, Qt::CaseInsensitive) == 0 && item.value.trimmed().compare(expected, Qt::CaseInsensitive) == 0) {
			return true;
		}
	}
	return false;
}

int integerValue(const QString& value)
{
	bool ok = false;
	const int parsed = value.trimmed().toInt(&ok);
	return ok ? parsed : 0;
}

bool startOffFlagSet(const QString& spawnflags)
{
	return (integerValue(spawnflags) & 1) != 0;
}

bool isAbsoluteOrPrivatePath(const QString& value)
{
	const QString trimmed = value.trimmed();
	static const QRegularExpression windowsDrive(QStringLiteral(R"(^[A-Za-z]:[\\/])"));
	static const QRegularExpression windowsHome(QStringLiteral(R"([A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/])"), QRegularExpression::CaseInsensitiveOption);
	return trimmed.startsWith('/') || trimmed.startsWith('~') || windowsDrive.match(trimmed).hasMatch() || windowsHome.match(trimmed).hasMatch();
}

bool hasPathDirectoryComponent(const QString& value)
{
	const QString trimmed = value.trimmed();
	return trimmed.contains('/') || trimmed.contains('\\');
}

bool valueHasSuspiciousBackslash(const QString& value)
{
	if (!value.contains('\\')) {
		return false;
	}
	static const QRegularExpression suspicious(QStringLiteral(R"(\\(?:[A-Za-z"]|$))"));
	return suspicious.match(value).hasMatch();
}

bool valueHasParserSensitiveBackslash(const QString& value)
{
	static const QRegularExpression parserSensitive(QStringLiteral(R"(\\[bfnrtv0])"), QRegularExpression::CaseInsensitiveOption);
	return parserSensitive.match(value).hasMatch();
}

bool lineHasNonIntegerBrushCoordinate(const QString& line)
{
	static const QRegularExpression pointPattern(QStringLiteral(R"(\(\s*([-+]?\d+(?:\.\d+)?)\s+([-+]?\d+(?:\.\d+)?)\s+([-+]?\d+(?:\.\d+)?)\s*\))"));
	QRegularExpressionMatchIterator matches = pointPattern.globalMatch(line);
	while (matches.hasNext()) {
		const QRegularExpressionMatch match = matches.next();
		if (match.captured(1).contains('.') || match.captured(2).contains('.') || match.captured(3).contains('.')) {
			return true;
		}
	}
	return false;
}

bool lineHasOriginBrushTexture(const QString& line)
{
	static const QRegularExpression originTexture(QStringLiteral(R"((^|[\s/\\])(?:common[/\\])?origin($|\s))"), QRegularExpression::CaseInsensitiveOption);
	return originTexture.match(line).hasMatch();
}

QString brushFaceTextureName(const QString& line, bool* isBrushFace = nullptr)
{
	if (isBrushFace) {
		*isBrushFace = false;
	}
	static const QRegularExpression faceTexture(QStringLiteral(R"vs(^\s*\([^)]*\)\s*\([^)]*\)\s*\([^)]*\)\s*(?:"([^"]*)"|([^\s]+))?)vs"));
	const QRegularExpressionMatch match = faceTexture.match(line);
	if (!match.hasMatch()) {
		return {};
	}
	if (isBrushFace) {
		*isBrushFace = true;
	}
	if (match.lastCapturedIndex() >= 1 && !match.captured(1).isNull()) {
		return match.captured(1).trimmed();
	}
	if (match.lastCapturedIndex() >= 2 && !match.captured(2).isNull()) {
		return match.captured(2).trimmed();
	}
	return {};
}

bool textureNameIsSkip(const QString& textureName)
{
	const QString normalized = QString(textureName).replace('\\', '/').trimmed().toLower();
	return normalized == QStringLiteral("skip") || normalized.endsWith(QStringLiteral("/skip"));
}

QStringList numericParts(const QString& value)
{
	return value.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
}

bool isNumericScalar(const QString& value)
{
	bool ok = false;
	value.trimmed().toDouble(&ok);
	return ok;
}

bool numericValueLooksInvalid(const QString& key, const QString& value)
{
	const QString normalizedKey = key.trimmed().toLower();
	const QStringList vector3Keys = {
		QStringLiteral("origin"),
		QStringLiteral("angles"),
		QStringLiteral("mangle"),
		QStringLiteral("_color"),
		QStringLiteral("color"),
		QStringLiteral("_sunlight_mangle"),
	};
	const QStringList scalarKeys = {
		QStringLiteral("angle"),
		QStringLiteral("delay"),
		QStringLiteral("spawnflags"),
		QStringLiteral("style"),
		QStringLiteral("wait"),
		QStringLiteral("light"),
		QStringLiteral("_light"),
		QStringLiteral("_minlight"),
		QStringLiteral("minlight"),
		QStringLiteral("_phong"),
		QStringLiteral("phong"),
		QStringLiteral("phong_angle"),
		QStringLiteral("_lmscale"),
		QStringLiteral("world_units_per_luxel"),
		QStringLiteral("_world_units_per_luxel"),
	};

	if (vector3Keys.contains(normalizedKey)) {
		const QStringList parts = numericParts(value);
		if (parts.size() != 3) {
			return true;
		}
		for (const QString& part : parts) {
			if (!isNumericScalar(part)) {
				return true;
			}
		}
		return false;
	}
	if (scalarKeys.contains(normalizedKey)) {
		const QStringList parts = numericParts(value);
		if (parts.isEmpty()) {
			return true;
		}
		for (const QString& part : parts) {
			if (!isNumericScalar(part)) {
				return true;
			}
		}
	}
	return false;
}

bool vectorZEquals(const QString& value, double expected)
{
	const QStringList parts = numericParts(value);
	if (parts.size() != 3) {
		return false;
	}
	bool ok = false;
	const double z = parts.at(2).toDouble(&ok);
	return ok && qAbs(z - expected) < 0.001;
}

bool vectorPitchLooksVertical(const QString& value)
{
	const QStringList parts = numericParts(value);
	if (parts.size() < 2) {
		return false;
	}
	bool ok = false;
	const double pitch = parts.at(1).toDouble(&ok);
	return ok && qAbs(qAbs(pitch) - 90.0) <= 5.0;
}

QString entityLocationHint(const MapEntity& entity)
{
	const QString origin = firstValue(entity, QStringLiteral("origin")).trimmed();
	if (!origin.isEmpty()) {
		return preflightText(" Entity origin: %1.").arg(origin);
	}
	const QString mangle = firstValue(entity, QStringLiteral("mangle")).trimmed();
	if (!mangle.isEmpty()) {
		return preflightText(" Entity mangle: %1.").arg(mangle);
	}
	return {};
}

bool looksLikeRegionEntity(const MapEntity& entity)
{
	const QString className = cleanClassName(entity).toLower();
	if (className.contains(QStringLiteral("region")) || className.contains(QStringLiteral("antiregion"))) {
		return true;
	}
	return entity.rawText.contains(QStringLiteral("region"), Qt::CaseInsensitive)
		|| entity.rawText.contains(QStringLiteral("antiregion"), Qt::CaseInsensitive);
}

bool looksLikeAreaportalEntity(const MapEntity& entity)
{
	return cleanClassName(entity).contains(QStringLiteral("areaportal"), Qt::CaseInsensitive)
		|| entity.rawText.contains(QStringLiteral("areaportal"), Qt::CaseInsensitive);
}

bool isBrushEntity(const MapEntity& entity)
{
	const QString className = cleanClassName(entity);
	return entity.hasBrush && !className.isEmpty() && className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0;
}

bool classUsuallyNeedsBrushes(const QString& className)
{
	const QString normalized = className.toLower();
	if (normalized.isEmpty()) {
		return false;
	}
	if (normalized == QStringLiteral("func_region") || normalized == QStringLiteral("func_areaportal")) {
		return false;
	}
	return normalized.startsWith(QStringLiteral("func_"))
		|| normalized.startsWith(QStringLiteral("trigger_"))
		|| normalized == QStringLiteral("illusionary")
		|| normalized == QStringLiteral("detail");
}

bool isLightEntity(const MapEntity& entity)
{
	const QString className = cleanClassName(entity).toLower();
	return className == QStringLiteral("light")
		|| className.startsWith(QStringLiteral("light_"))
		|| className == QStringLiteral("light_spot")
		|| className == QStringLiteral("light_environment");
}

void addIssue(
	EricwMapPreflightReport* report,
	int number,
	const QString& clusterId,
	const char* message,
	const MapEntity& entity,
	const QString& key = QString(),
	int line = 0)
{
	if (!report) {
		return;
	}
	EricwMapPreflightIssue issue;
	issue.issueId = issueId(number);
	issue.clusterId = clusterId;
	issue.severity = QStringLiteral("warning");
	issue.message = preflightText(message);
	issue.entityClassName = cleanClassName(entity);
	issue.key = key;
	issue.line = line > 0 ? line : entity.startLine;
	report->issues.push_back(issue);
}

void addIssueText(
	EricwMapPreflightReport* report,
	int number,
	const QString& clusterId,
	const QString& message,
	const MapEntity& entity,
	const QString& key = QString(),
	int line = 0)
{
	if (!report) {
		return;
	}
	EricwMapPreflightIssue issue;
	issue.issueId = issueId(number);
	issue.clusterId = clusterId;
	issue.severity = QStringLiteral("warning");
	issue.message = message;
	issue.entityClassName = cleanClassName(entity);
	issue.key = key;
	issue.line = line > 0 ? line : entity.startLine;
	report->issues.push_back(issue);
}

QVector<MapEntity> parseMapEntities(const QString& mapText)
{
	QVector<MapEntity> entities;
	MapEntity current;
	bool inEntity = false;
	int braceDepth = 0;
	int lineNumber = 0;
	static const QRegularExpression keyValuePattern(QStringLiteral(R"vs(^\s*"([^"]+)"\s+"([^"]*)")vs"));

	for (const QString& line : mapText.split('\n')) {
		++lineNumber;
		const QString trimmed = line.trimmed();

		if (!inEntity && trimmed == QStringLiteral("{")) {
			inEntity = true;
			current = {};
			current.startLine = lineNumber;
			current.rawText += line + QLatin1Char('\n');
			braceDepth = 1;
			continue;
		}

		if (inEntity) {
			current.rawText += line + QLatin1Char('\n');
			if (braceDepth == 1) {
				const QRegularExpressionMatch keyMatch = keyValuePattern.match(line);
				if (keyMatch.hasMatch()) {
					current.values.push_back({keyMatch.captured(1), keyMatch.captured(2), lineNumber});
				}
			}
			if (braceDepth >= 2 && lineHasNonIntegerBrushCoordinate(line)) {
				current.hasNonIntegerBrushCoordinate = true;
			}
			if (braceDepth >= 2) {
				bool isBrushFace = false;
				const QString textureName = brushFaceTextureName(line, &isBrushFace);
				if (isBrushFace) {
					if (textureName.isEmpty()) {
						current.hasEmptyTextureName = true;
					}
					if (textureNameIsSkip(textureName)) {
						current.hasSkipTexture = true;
					}
				}
			}
			if (braceDepth >= 2 && lineHasOriginBrushTexture(line)) {
				current.hasOriginBrush = true;
			}
		}

		int opens = 0;
		int closes = 0;
		bool inQuote = false;
		for (int index = 0; index < line.size(); ++index) {
			const QChar ch = line.at(index);
			if (ch == '"' && (index == 0 || line.at(index - 1) != '\\')) {
				inQuote = !inQuote;
			}
			if (inQuote) {
				continue;
			}
			if (ch == '{') {
				++opens;
			} else if (ch == '}') {
				++closes;
			}
		}
		if (inEntity && braceDepth == 1 && opens > 0 && trimmed == QStringLiteral("{")) {
			current.hasBrush = true;
			++current.brushCount;
		}
		braceDepth += opens;
		braceDepth -= closes;

		if (inEntity && braceDepth <= 0) {
			entities.push_back(current);
			current = {};
			inEntity = false;
			braceDepth = 0;
		}
	}

	return entities;
}

void inspectEntityValues(const MapEntity& entity, EricwMapPreflightReport* report)
{
	const QString className = cleanClassName(entity);
	QMap<QString, QString> seenKeysByLowercase;
	for (const MapKeyValue& item : entity.values) {
		const QString normalizedKey = item.key.toLower();
		const QString previousKey = seenKeysByLowercase.value(normalizedKey);
		if (!previousKey.isEmpty() && previousKey != item.key) {
			addIssueText(report, 121, QString(), preflightText("Entity contains keys that differ only by case; normalize the key spelling before compiling so editor and compiler behavior agree."), entity, item.key, item.line);
		} else if (previousKey.isEmpty()) {
			seenKeysByLowercase.insert(normalizedKey, item.key);
		}
		if (numericValueLooksInvalid(item.key, item.value)) {
			addIssueText(report, 135, QStringLiteral("D04"), preflightText("Entity key has an invalid numeric value; inspect the source line before compiling.") + entityLocationHint(entity), entity, item.key, item.line);
		}
		if (item.value.size() > 512) {
			addIssueText(report, 245, QStringLiteral("D04"), preflightText("Entity key value is longer than 512 characters; shorten it or move bulky metadata outside the BSP entity lump."), entity, item.key, item.line);
		}
		if (item.key.compare(QStringLiteral("wad"), Qt::CaseInsensitive) == 0) {
			const QStringList paths = item.value.split(';', Qt::SkipEmptyParts);
			bool absolutePathWarningAdded = false;
			for (const QString& path : paths) {
				if (isAbsoluteOrPrivatePath(path) && !absolutePathWarningAdded) {
					addIssueText(report, 293, QStringLiteral("D04"), preflightText("WAD key contains an absolute or private path; use project-relative mounts to avoid leaking local paths into compiled BSPs."), entity, item.key, item.line);
					absolutePathWarningAdded = true;
				}
				if (hasPathDirectoryComponent(path) && !isAbsoluteOrPrivatePath(path)) {
					addIssueText(report, 201, QStringLiteral("D08"), preflightText("WAD key uses a directory component; prefer compiler profile search paths or -waddir style mounts over process-current-directory assumptions."), entity, item.key, item.line);
				}
			}
			continue;
		}
		if (valueHasSuspiciousBackslash(item.value)) {
			addIssueText(report, 288, QStringLiteral("D04"), preflightText("Entity value contains a backslash escape that Quake-family parsers may reinterpret; prefer forward slashes or escaped text."), entity, item.key, item.line);
			addIssueText(report, 87, QStringLiteral("D04"), preflightText("Backslash and quote escaping differs across engines and qbsp versions; verify this value before compiling."), entity, item.key, item.line);
		}
		if (valueHasParserSensitiveBackslash(item.value)) {
			addIssueText(report, 129, QString(), preflightText("Entity value contains a C-style backslash escape such as \\b or \\n; verify qbsp and light parse the same text."), entity, item.key, item.line);
		}
	}

	if (classUsuallyNeedsBrushes(className) && !entity.hasBrush) {
		addIssue(report, 233, QString(), "Brush-style entity has no parsed brushes; add valid brushes or convert it to a point entity before compiling.", entity, QStringLiteral("classname"));
	}
	if (entity.hasEmptyTextureName) {
		addIssue(report, 342, QString(), "A brush face appears to have an empty texture name; assign a concrete texture before decompile or texture extraction workflows.", entity);
	}
	if (hasAnyKey(entity, {QStringLiteral("_minlight"), QStringLiteral("minlight")})) {
		addIssue(report, 470, QStringLiteral("D12"), "This entity uses minlight; confirm whether the target format expects integer or normalized float scale.", entity, QStringLiteral("_minlight"));
	}
	if (hasKey(entity, QStringLiteral("mangle")) && hasKey(entity, QStringLiteral("angle"))) {
		addIssue(report, 310, QString(), "This entity has both angle and mangle; Quake 2 light workflows have an upstream angle/mangle conflict.", entity, QStringLiteral("mangle"));
	}
	if (isBrushEntity(entity) && hasKey(entity, QStringLiteral("origin"))) {
		addIssue(report, 308, QString(), "Brush entity has an origin key; upstream qbsp can treat this as a leak participant.", entity, QStringLiteral("origin"));
	}
	if (isBrushEntity(entity) && className.compare(QStringLiteral("func_door_rotating"), Qt::CaseInsensitive) == 0 && (hasKey(entity, QStringLiteral("origin")) || entity.hasOriginBrush)) {
		addIssue(report, 417, QStringLiteral("D06"), "Rotating door with origin data is risky in region+ compiles; compile the full map or remove the partial region for validation.", entity, hasKey(entity, QStringLiteral("origin")) ? QStringLiteral("origin") : QStringLiteral("ORIGIN"));
	}
	if (entity.hasNonIntegerBrushCoordinate) {
		addIssue(report, 257, QString(), "Brush coordinates include non-integer vertices; snap or inspect this brush before qbsp to avoid missing faces.", entity);
	}
	if (isBrushEntity(entity) && (hasKey(entity, QStringLiteral("_phong")) || hasKey(entity, QStringLiteral("phong")) || hasKey(entity, QStringLiteral("phong_angle")))) {
		addIssue(report, 405, QStringLiteral("D07"), "Phong smoothing on brush entities can produce edge light bleed; inspect the compiled lightmap before release.", entity, QStringLiteral("_phong"));
	}
	if (isBrushEntity(entity) && hasAnyKey(entity, {QStringLiteral("_dirt"), QStringLiteral("_dirtmode"), QStringLiteral("_dirtdepth"), QStringLiteral("_dirtscale"), QStringLiteral("_dirtangle"), QStringLiteral("_dirtgain")})) {
		addIssue(report, 140, QStringLiteral("D07"), "Brush-model dirt keys are not consistently supported upstream; prefer worldspawn dirt defaults or verify this bmodel in a lighting test.", entity, QStringLiteral("_dirt"));
		addIssue(report, 349, QStringLiteral("D07"), "Dirt keys on bmodels have known upstream behavior gaps; inspect ambient occlusion on the compiled model.", entity, QStringLiteral("_dirt"));
	}
	if (isBrushEntity(entity) && hasAnyKey(entity, {QStringLiteral("_light"), QStringLiteral("light")})) {
		addIssue(report, 241, QString(), "Brush entity carries light-emission keys; brush-as-light authoring remains an upstream feature request, so verify qbsp/light treatment before release.", entity, QStringLiteral("_light"));
	}
	if (hasAnyKey(entity, {QStringLiteral("_mirrorinside"), QStringLiteral("mirrorinside")})) {
		addIssue(report, 262, QStringLiteral("D07"), "_mirrorinside is present; shadow output has version-specific regressions, so compare against the intended ericw-tools build.", entity, QStringLiteral("_mirrorinside"));
	}
	if (hasAnyKey(entity, {QStringLiteral("_shadowself"), QStringLiteral("_selfshadow"), QStringLiteral("shadowself"), QStringLiteral("selfshadow")})) {
		addIssue(report, 268, QStringLiteral("D07"), "Self-shadow keys are present; verify shadow/deviance behavior with the selected light compiler before release.", entity, QStringLiteral("_shadowself"));
	}
	if (hasAnyKey(entity, {QStringLiteral("_switchableshadow"), QStringLiteral("_switchable_shadow"), QStringLiteral("switchableshadow"), QStringLiteral("switchable_shadow")})) {
		if (entity.hasSkipTexture) {
			addIssue(report, 255, QStringLiteral("D07"), "Switchable shadow bmodel uses a skip-textured face; this combination has known upstream lighting risk.", entity, QStringLiteral("_switchableshadow"));
		}
		if (!keyValueEquals(entity, QStringLiteral("_shadow"), QStringLiteral("1")) && !keyValueEquals(entity, QStringLiteral("shadow"), QStringLiteral("1"))) {
			addIssue(report, 256, QStringLiteral("D07"), "Switchable shadow is set without an explicit _shadow 1; set the shadow key deliberately so the compiler intent is unambiguous.", entity, QStringLiteral("_switchableshadow"));
		}
	}
	if (className.compare(QStringLiteral("func_illusionary_visblocker"), Qt::CaseInsensitive) == 0 || className.compare(QStringLiteral("func_detail_illusionary"), Qt::CaseInsensitive) == 0) {
		addIssue(report, 456, QStringLiteral("D07"), "Illusionary/detail visblocking entities have known shadow/rendering regressions in ericw-tools; verify the affected compiler version.", entity, QStringLiteral("classname"));
		if (className.compare(QStringLiteral("func_illusionary_visblocker"), Qt::CaseInsensitive) == 0) {
			addIssue(report, 441, QStringLiteral("D07"), "func_illusionary_visblocker behavior differs between ericw-tools versions; inspect visibility from both sides.", entity, QStringLiteral("classname"));
		}
	}
	if (className.compare(QStringLiteral("func_detail_null"), Qt::CaseInsensitive) == 0) {
		addIssue(report, 455, QString(), "func_detail_null is an upstream feature request; keep this marker out of release compiles unless the selected compiler explicitly supports it.", entity, QStringLiteral("classname"));
	}
	if (className.compare(QStringLiteral("misc_external_map"), Qt::CaseInsensitive) == 0 || hasKey(entity, QStringLiteral("_external_map"))) {
		const QString externalMap = firstValue(entity, QStringLiteral("_external_map"));
		const QString externalMapClassname = firstValue(entity, QStringLiteral("_external_map_classname"));
		if (!hasKey(entity, QStringLiteral("_external_map_classname"))) {
			addIssue(report, 194, QStringLiteral("D05"), "misc_external_map is missing _external_map_classname; set it explicitly, commonly to func_detail, before qbsp.", entity, QStringLiteral("_external_map_classname"));
		}
		if (externalMap.trimmed().isEmpty()) {
			addIssue(report, 199, QStringLiteral("D05"), "misc_external_map is missing _external_map; set a portable map-relative prefab path before qbsp.", entity, QStringLiteral("_external_map"));
		} else if (isAbsoluteOrPrivatePath(externalMap) || externalMap.contains('\\') || externalMap.contains(QStringLiteral(".."))) {
			addIssue(report, 199, QStringLiteral("D05"), "External map path is absolute or platform-specific; prefer map/project-relative paths for portable prefab compiles.", entity, QStringLiteral("_external_map"));
		}
		if (externalMapClassname.compare(QStringLiteral("func_group"), Qt::CaseInsensitive) == 0) {
			addIssue(report, 207, QStringLiteral("D05"), "External map target classname is func_group; all-group prefab imports have known corruption risk upstream.", entity, QStringLiteral("_external_map_classname"));
		}
		if (hasAnyKey(entity, {QStringLiteral("_phong"), QStringLiteral("phong"), QStringLiteral("phong_angle"), QStringLiteral("_shadow"), QStringLiteral("_dirt"), QStringLiteral("_minlight")})) {
			addIssue(report, 231, QStringLiteral("D05"), "Lighting keys on misc_external_map can be ignored; move lighting keys into the external map geometry when possible.", entity, QStringLiteral("_phong"));
		}
		if (firstValue(entity, QStringLiteral("mangle")).contains(QStringLiteral("360")) || firstValue(entity, QStringLiteral("angles")).contains(QStringLiteral("360"))) {
			addIssue(report, 193, QStringLiteral("D05"), "External map rotation contains 360 degrees; normalize rotations to avoid prefab lighting surprises.", entity, QStringLiteral("mangle"));
		}
		if (!externalMap.isEmpty()) {
			addIssue(report, 333, QStringLiteral("D05"), "External map prefabs import limited content; entities inside external maps may not be handled as expected by upstream qbsp.", entity, QStringLiteral("_external_map"));
		}
	}
	if (className.compare(QStringLiteral("misc_model"), Qt::CaseInsensitive) == 0 && hasAnyKey(entity, {QStringLiteral("_shadow"), QStringLiteral("shadow"), QStringLiteral("_shadowself"), QStringLiteral("_selfshadow")})) {
		addIssue(report, 247, QStringLiteral("D07"), "misc_model shadow keys express an expectation that upstream light support may not satisfy; verify model shadows in the compiled map.", entity, QStringLiteral("_shadow"));
	}
	if (className.startsWith(QStringLiteral("monster_"), Qt::CaseInsensitive) && vectorZEquals(firstValue(entity, QStringLiteral("origin")), 24.0)) {
		addIssue(report, 270, QString(), "Monster origin is exactly 24 units above the map Z origin; verify outside-fill behavior and floor contact before qbsp.", entity, QStringLiteral("origin"));
	}
	if (isLightEntity(entity)) {
		if (hasKey(entity, QStringLiteral("style")) && hasKey(entity, QStringLiteral("targetname"))) {
			addIssue(report, 173, QStringLiteral("D02"), "Toggled light has an explicit style; upstream may overwrite it, so preserve intent in a separate key or script.", entity, QStringLiteral("style"));
			addIssue(report, 475, QStringLiteral("D02"), "Toggled light style preservation is not guaranteed upstream; check grouped lights before compiling.", entity, QStringLiteral("style"));
		}
		for (const MapKeyValue& surface : valuesForKey(entity, QStringLiteral("_surface"))) {
			const QString normalizedSurface = surface.value.trimmed().toLower();
			if (surface.value.startsWith('*') || normalizedSurface.contains(QStringLiteral("water")) || normalizedSurface.contains(QStringLiteral("slime")) || normalizedSurface.contains(QStringLiteral("lava"))) {
				addIssue(report, 217, QStringLiteral("D07"), "Surface light targets a liquid-like texture; liquid _surface settings can collide or be ignored upstream, so verify emitted light.", entity, surface.key, surface.line);
				addIssue(report, 389, QStringLiteral("D07"), "Q2/liquid surface-light behavior has upstream regressions; inspect surface light output before release.", entity, surface.key, surface.line);
			}
		}
		if (hasKey(entity, QStringLiteral("_surface")) && hasKey(entity, QStringLiteral("_project_texture"))) {
			addIssue(report, 209, QString(), "_project_texture is set on a _surface light; projected textures on surface lights are a known upstream risk.", entity, QStringLiteral("_project_texture"));
		}
		if (className.compare(QStringLiteral("light_spot"), Qt::CaseInsensitive) == 0 && hasAnyKey(entity, {QStringLiteral("_phong"), QStringLiteral("phong"), QStringLiteral("phong_angle")})) {
			addIssue(report, 172, QStringLiteral("D07"), "Spotlight entity also carries Phong keys; move Phong settings to geometry and verify spotlight output on smoothed models.", entity, QStringLiteral("_phong"));
		}
		if (keyValueEquals(entity, QStringLiteral("_sun"), QStringLiteral("1")) && hasAnyKey(entity, {QStringLiteral("_deviance"), QStringLiteral("deviance"), QStringLiteral("_penumbra"), QStringLiteral("penumbra")})) {
			addIssue(report, 268, QStringLiteral("D07"), "_sun 1 light uses deviance or penumbra keys; this combination has known upstream behavior risk.", entity, QStringLiteral("_sun"));
			addIssue(report, 291, QStringLiteral("D07"), "Sky/sun light entity uses deviance or penumbra keys; validate sky lighting softness with the selected compiler.", entity, QStringLiteral("_sun"));
		}
	}
	if (hasAnyKey(entity, {QStringLiteral("_deviance"), QStringLiteral("deviance"), QStringLiteral("_penumbra"), QStringLiteral("penumbra")})
		&& (vectorPitchLooksVertical(firstValue(entity, QStringLiteral("_sunlight_mangle"))) || vectorPitchLooksVertical(firstValue(entity, QStringLiteral("mangle"))))) {
		addIssue(report, 238, QStringLiteral("D07"), "Sunlight deviance or penumbra is used with a near-vertical mangle; compare pitch/yaw softness before release.", entity, QStringLiteral("_sunlight_mangle"));
	}
	if (hasAnyKey(entity, {QStringLiteral("_bounce"), QStringLiteral("bounce"), QStringLiteral("_bouncescale"), QStringLiteral("bouncescale")})
		&& hasAnyKey(entity, {QStringLiteral("_soft"), QStringLiteral("soft")})
		&& hasAnyKey(entity, {QStringLiteral("_extra"), QStringLiteral("extra"), QStringLiteral("_extra4"), QStringLiteral("extra4")})) {
		addIssue(report, 190, QStringLiteral("D07"), "Bounce, soft, and extra lighting hints are combined in map keys; run a comparison compile before trusting final lighting.", entity, QStringLiteral("_bounce"));
	}
	if (hasKey(entity, QStringLiteral("_sunlight2"))) {
		addIssue(report, 484, QStringLiteral("D07"), "_sunlight2 can produce BSPX LIGHTINGDIR artifacts in Q2 workflows; verify directional lighting output.", entity, QStringLiteral("_sunlight2"));
	}
	if (hasAnyKey(entity, {QStringLiteral("world_units_per_luxel"), QStringLiteral("_world_units_per_luxel")})) {
		addIssue(report, 485, QStringLiteral("D11"), "world_units_per_luxel is set per entity; upstream does not yet expose a stable force-override flag.", entity, QStringLiteral("world_units_per_luxel"));
	}
	if (hasAnyKey(entity, {QStringLiteral("_compile_condition"), QStringLiteral("_compile_if"), QStringLiteral("_compile_ifnot"), QStringLiteral("_if"), QStringLiteral("_ifnot"), QStringLiteral("_ifdef"), QStringLiteral("_ifndef")})) {
		addIssue(report, 343, QStringLiteral("D12"), "Conditional compile entity keys are present; route variants through explicit VibeStudio build profiles because upstream conditional entity compilation is not guaranteed.", entity, QStringLiteral("_compile_if"));
	}
	if (hasAnyKey(entity, {QStringLiteral("_modelindex"), QStringLiteral("_model_order"), QStringLiteral("modelindex")}) || firstValue(entity, QStringLiteral("model")).trimmed().startsWith('*')) {
		addIssue(report, 443, QString(), "Map-hack model ordering keys or *n model references are present; verify final entity order before relying on custom model indices.", entity, QStringLiteral("model"));
	}
	const QString entityText = entity.rawText.toLower();
	if (entityText.contains(QStringLiteral("forcegoodtree")) && (entityText.contains(QStringLiteral("hexen2")) || entityText.contains(QStringLiteral("bsp2")))) {
		addIssue(report, 254, QString(), "Hexen2/BSP2 forcegoodtree markers are present in map data; avoid this risky compiler combination unless a known-good tool version is selected.", entity, QStringLiteral("forcegoodtree"));
	}
}

void inspectMapPath(const QString& mapPath, EricwMapPreflightReport* report)
{
	if (mapPath.trimmed().isEmpty()) {
		return;
	}
	const QFileInfo info(mapPath);
	if (info.completeBaseName().contains('.')) {
		MapEntity entity;
		entity.startLine = 1;
		addIssueText(report, 230, QString(), preflightText("Map filename has multiple dots; verify compiler output naming because some ericw-tools paths have handled dotted names incorrectly."), entity);
	}
}

void inspectEntityGroups(const QVector<MapEntity>& entities, EricwMapPreflightReport* report)
{
	int regionCount = 0;
	int totalBrushCount = 0;
	int detailBrushCount = 0;
	bool hasRegion = false;
	bool hasAreaportal = false;
	bool hasSpotlight = false;
	bool hasPhongModelOrBrush = false;
	QMap<QString, LightGroupState> lightGroups;
	QMap<QString, int> externalMapMergeTargets;

	for (const MapEntity& entity : entities) {
		const QString className = cleanClassName(entity);
		totalBrushCount += entity.brushCount;
		if (className.contains(QStringLiteral("detail"), Qt::CaseInsensitive) || className.compare(QStringLiteral("func_group"), Qt::CaseInsensitive) == 0) {
			detailBrushCount += entity.brushCount;
		}
		if (looksLikeRegionEntity(entity)) {
			++regionCount;
			hasRegion = true;
		}
		hasAreaportal = hasAreaportal || looksLikeAreaportalEntity(entity);
		hasSpotlight = hasSpotlight || className.compare(QStringLiteral("light_spot"), Qt::CaseInsensitive) == 0;
		hasPhongModelOrBrush = hasPhongModelOrBrush
			|| ((isBrushEntity(entity) || className.compare(QStringLiteral("misc_model"), Qt::CaseInsensitive) == 0)
				&& hasAnyKey(entity, {QStringLiteral("_phong"), QStringLiteral("phong"), QStringLiteral("phong_angle")}));
		if (className.compare(QStringLiteral("misc_external_map"), Qt::CaseInsensitive) == 0) {
			const QString mergeTarget = firstValue(entity, QStringLiteral("_external_map_target")).trimmed().toLower();
			if (!mergeTarget.isEmpty()) {
				externalMapMergeTargets.insert(mergeTarget, externalMapMergeTargets.value(mergeTarget) + 1);
			}
		}
		if (isLightEntity(entity) && hasKey(entity, QStringLiteral("targetname"))) {
			const QString targetName = firstValue(entity, QStringLiteral("targetname")).trimmed().toLower();
			if (!targetName.isEmpty()) {
				LightGroupState state = lightGroups.value(targetName);
				if (state.firstLine <= 0) {
					state.firstLine = entity.startLine;
				}
				if (startOffFlagSet(firstValue(entity, QStringLiteral("spawnflags")))) {
					state.sawStartOff = true;
				} else {
					state.sawStartOn = true;
				}
				lightGroups.insert(targetName, state);
			}
		}
	}

	if (totalBrushCount >= 4096 || detailBrushCount >= 512) {
		const MapEntity entity = entities.isEmpty() ? MapEntity{} : entities.front();
		addIssueText(report, 136, QString(), preflightText("Map has a high brush/detail-brush count; watch clipnode and marksurface totals and consider simplifying detail geometry before qbsp."), entity);
	}
	if (regionCount > 1) {
		const MapEntity entity = entities.isEmpty() ? MapEntity{} : entities.front();
		addIssueText(report, 390, QStringLiteral("D06"), preflightText("Multiple region markers were detected; upstream qbsp supports limited region workflows, so use one explicit region or validate with a full compile."), entity);
	}
	if (hasRegion && hasAreaportal) {
		const MapEntity entity = entities.isEmpty() ? MapEntity{} : entities.front();
		addIssueText(report, 444, QStringLiteral("D06"), preflightText("Region or antiregion markers appear alongside areaportals; Quake 2 areaportal behavior may be invalid in partial compiles."), entity);
	}
	if (hasRegion) {
		const MapEntity entity = entities.isEmpty() ? MapEntity{} : entities.front();
		addIssueText(report, 422, QStringLiteral("D06"), preflightText("Region compiles may leave brush entities outside the intended slice; verify entity targets and run a full compile before release."), entity);
	}

	if (hasSpotlight && hasPhongModelOrBrush) {
		const MapEntity entity = entities.isEmpty() ? MapEntity{} : entities.front();
		addIssueText(report, 172, QStringLiteral("D07"), preflightText("Map combines spotlights with Phong-smoothed models or brush entities; validate spotlight output on smoothed geometry."), entity, QStringLiteral("_phong"));
	}
	for (auto it = externalMapMergeTargets.cbegin(); it != externalMapMergeTargets.cend(); ++it) {
		if (it.value() > 1) {
			MapEntity entity;
			entity.startLine = 1;
			entity.values.push_back({QStringLiteral("classname"), QStringLiteral("misc_external_map"), 1});
			addIssueText(report, 327, QStringLiteral("D05"), preflightText("Multiple misc_external_map entities target the same merge key; upstream merged-target behavior is not guaranteed, so verify the generated entity structure."), entity, QStringLiteral("_external_map_target"));
			break;
		}
	}

	for (auto it = lightGroups.cbegin(); it != lightGroups.cend(); ++it) {
		if (it.value().sawStartOff && it.value().sawStartOn) {
			MapEntity entity;
			entity.startLine = it.value().firstLine;
			entity.values.push_back({QStringLiteral("classname"), QStringLiteral("light"), it.value().firstLine});
			addIssueText(report, 122, QStringLiteral("D02"), preflightText("Lights sharing a targetname mix START_OFF and always-on spawnflags; make the group consistent before compiling."), entity, QStringLiteral("spawnflags"));
		}
	}
}

} // namespace

bool EricwMapPreflightReport::hasIssues() const
{
	return !issues.isEmpty();
}

QStringList EricwMapPreflightReport::warningMessages() const
{
	QStringList warnings;
	QSet<QString> seen;
	for (const EricwMapPreflightIssue& issue : issues) {
		QString location;
		if (issue.line > 0) {
			location = preflightText("line %1").arg(issue.line);
		}
		QString prefix = issue.issueId;
		if (!issue.clusterId.isEmpty()) {
			prefix += QStringLiteral(" [%1]").arg(issue.clusterId);
		}
		QString message = location.isEmpty()
			? QStringLiteral("%1: %2").arg(prefix, issue.message)
			: QStringLiteral("%1 at %2: %3").arg(prefix, location, issue.message);
		if (!issue.entityClassName.isEmpty()) {
			message += preflightText(" Entity: %1.").arg(issue.entityClassName);
		}
		if (!issue.key.isEmpty()) {
			message += preflightText(" Key: %1.").arg(issue.key);
		}
		if (!seen.contains(message)) {
			seen.insert(message);
			warnings.push_back(message);
		}
	}
	return warnings;
}

QString ericwMapPreflightSeverityId(EricwMapPreflightSeverity severity)
{
	switch (severity) {
	case EricwMapPreflightSeverity::Info:
		return QStringLiteral("info");
	case EricwMapPreflightSeverity::Warning:
		return QStringLiteral("warning");
	case EricwMapPreflightSeverity::Error:
		return QStringLiteral("error");
	}
	return QStringLiteral("warning");
}

EricwMapPreflightResult validateEricwMapPreflightText(const QString& mapText, const EricwMapPreflightOptions& options)
{
	EricwMapPreflightResult result;
	const QVector<MapEntity> entities = parseMapEntities(mapText);
	result.entityCount = entities.size();
	for (const MapEntity& entity : entities) {
		if (isBrushEntity(entity)) {
			++result.brushEntityCount;
		}
	}

	EricwMapPreflightReport report;
	inspectMapPath(options.mapPath, &report);
	for (const MapEntity& entity : entities) {
		inspectEntityValues(entity, &report);
	}
	inspectEntityGroups(entities, &report);

	for (const EricwMapPreflightIssue& issue : report.issues) {
		EricwMapPreflightWarning warning;
		warning.severity = EricwMapPreflightSeverity::Warning;
		warning.upstreamIssue = issue.issueId;
		warning.message = issue.message;
		warning.filePath = options.mapPath;
		warning.line = issue.line;
		warning.entityIndex = -1;
		warning.classname = issue.entityClassName;
		warning.key = issue.key;
		if (issue.issueId.endsWith(QStringLiteral("#293"))) {
			warning.code = QStringLiteral("wad-absolute-path");
		} else if (issue.issueId.endsWith(QStringLiteral("#121"))) {
			warning.code = QStringLiteral("entity-key-case-collision");
		} else if (issue.issueId.endsWith(QStringLiteral("#129"))) {
			warning.code = QStringLiteral("parser-sensitive-backslash");
		} else if (issue.issueId.endsWith(QStringLiteral("#136"))) {
			warning.code = QStringLiteral("high-brush-count-clipnode-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#140")) || issue.issueId.endsWith(QStringLiteral("#349"))) {
			warning.code = QStringLiteral("bmodel-dirt-key-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#172"))) {
			warning.code = QStringLiteral("spotlight-phong-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#190"))) {
			warning.code = QStringLiteral("bounce-soft-extra-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#209"))) {
			warning.code = QStringLiteral("surface-project-texture-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#217"))) {
			warning.code = QStringLiteral("liquid-surface-light-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#233"))) {
			warning.code = QStringLiteral("empty-brush-entity");
		} else if (issue.issueId.endsWith(QStringLiteral("#238"))) {
			warning.code = QStringLiteral("vertical-sunlight-penumbra-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#241"))) {
			warning.code = QStringLiteral("brush-as-light-marker");
		} else if (issue.issueId.endsWith(QStringLiteral("#247"))) {
			warning.code = QStringLiteral("misc-model-shadow-expectation");
		} else if (issue.issueId.endsWith(QStringLiteral("#254"))) {
			warning.code = QStringLiteral("hexen2-bsp2-forcegoodtree-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#255"))) {
			warning.code = QStringLiteral("switchable-shadow-skip-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#256"))) {
			warning.code = QStringLiteral("switchable-shadow-missing-shadow-key");
		} else if (issue.issueId.endsWith(QStringLiteral("#262"))) {
			warning.code = QStringLiteral("mirrorinside-shadow-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#268"))) {
			warning.code = QStringLiteral("sun-or-self-shadow-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#270"))) {
			warning.code = QStringLiteral("monster-outside-fill-origin-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#291"))) {
			warning.code = QStringLiteral("sky-light-penumbra-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#327"))) {
			warning.code = QStringLiteral("external-map-merge-target-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#342"))) {
			warning.code = QStringLiteral("empty-texture-name");
		} else if (issue.issueId.endsWith(QStringLiteral("#343"))) {
			warning.code = QStringLiteral("conditional-compile-entity-key");
		} else if (issue.issueId.endsWith(QStringLiteral("#443"))) {
			warning.code = QStringLiteral("maphack-model-ordering-key");
		} else if (issue.issueId.endsWith(QStringLiteral("#455"))) {
			warning.code = QStringLiteral("func-detail-null-marker");
		} else if (issue.issueId.endsWith(QStringLiteral("#245"))) {
			warning.code = QStringLiteral("long-key-value");
		} else if (issue.issueId.endsWith(QStringLiteral("#288")) || issue.issueId.endsWith(QStringLiteral("#87"))) {
			warning.code = QStringLiteral("escape-backslash");
		} else if (issue.issueId.endsWith(QStringLiteral("#194"))) {
			warning.code = QStringLiteral("external-map-missing-classname");
		} else if (issue.issueId.endsWith(QStringLiteral("#199"))) {
			warning.code = QStringLiteral("external-map-path-portability");
		} else if (issue.issueId.endsWith(QStringLiteral("#173")) || issue.issueId.endsWith(QStringLiteral("#475"))) {
			warning.code = QStringLiteral("light-style-conflict");
		} else if (issue.issueId.endsWith(QStringLiteral("#122"))) {
			warning.code = QStringLiteral("light-start-off-conflict");
		} else if (issue.issueId.endsWith(QStringLiteral("#310"))) {
			warning.code = QStringLiteral("q2-angle-mangle");
		} else if (issue.issueId.endsWith(QStringLiteral("#308"))) {
			warning.code = QStringLiteral("brush-entity-origin-key");
		} else if (issue.issueId.endsWith(QStringLiteral("#135"))) {
			warning.code = QStringLiteral("invalid-numeric-key-value");
		} else if (issue.issueId.endsWith(QStringLiteral("#201"))) {
			warning.code = QStringLiteral("wad-search-path-needed");
		} else if (issue.issueId.endsWith(QStringLiteral("#207"))) {
			warning.code = QStringLiteral("external-map-func-group-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#230"))) {
			warning.code = QStringLiteral("dotted-map-filename");
		} else if (issue.issueId.endsWith(QStringLiteral("#231"))) {
			warning.code = QStringLiteral("external-map-lighting-key-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#257"))) {
			warning.code = QStringLiteral("non-integer-brush-coordinate");
		} else if (issue.issueId.endsWith(QStringLiteral("#417"))) {
			warning.code = QStringLiteral("region-origin-brush-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#333"))) {
			warning.code = QStringLiteral("external-map-entity-import-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#390"))) {
			warning.code = QStringLiteral("multiple-region-markers");
		} else if (issue.issueId.endsWith(QStringLiteral("#405"))) {
			warning.code = QStringLiteral("phong-bmodel-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#422"))) {
			warning.code = QStringLiteral("region-brush-entity-cull-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#444"))) {
			warning.code = QStringLiteral("region-areaportal-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#470"))) {
			warning.code = QStringLiteral("minlight-scale-ambiguity");
		} else if (issue.issueId.endsWith(QStringLiteral("#484"))) {
			warning.code = QStringLiteral("q2-sunlight2-lightingdir-risk");
		} else if (issue.issueId.endsWith(QStringLiteral("#485"))) {
			warning.code = QStringLiteral("world-units-per-luxel-override-risk");
		} else {
			warning.code = issue.issueId.toLower().replace(QStringLiteral("ericw-tools #"), QStringLiteral("ericw-issue-"));
		}
		result.warnings.push_back(warning);
	}

	if (options.regionCompile) {
		for (const MapEntity& entity : entities) {
			if (cleanClassName(entity).compare(QStringLiteral("func_door_rotating"), Qt::CaseInsensitive) == 0 && (hasKey(entity, QStringLiteral("origin")) || entity.hasOriginBrush)) {
				bool alreadyPresent = false;
				for (const EricwMapPreflightWarning& warning : result.warnings) {
					alreadyPresent = alreadyPresent || warning.code == QStringLiteral("region-origin-brush-risk");
				}
				if (!alreadyPresent) {
					EricwMapPreflightWarning warning;
					warning.severity = EricwMapPreflightSeverity::Warning;
					warning.code = QStringLiteral("region-origin-brush-risk");
					warning.upstreamIssue = issueId(417);
					warning.message = preflightText("Region compile requested with a rotating origin brush; validate with a full compile before release.");
					warning.filePath = options.mapPath;
					warning.line = entity.startLine;
					warning.classname = cleanClassName(entity);
					warning.key = hasKey(entity, QStringLiteral("origin")) ? QStringLiteral("origin") : QStringLiteral("ORIGIN");
					result.warnings.push_back(warning);
				}
			}
		}
	}

	return result;
}

EricwMapPreflightResult validateEricwMapPreflightFile(const QString& mapPath, const EricwMapPreflightOptions& options)
{
	EricwMapPreflightOptions fileOptions = options;
	fileOptions.mapPath = mapPath;
	EricwMapPreflightResult result;
	QFile file(mapPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		result.parseComplete = false;
		EricwMapPreflightWarning warning;
		warning.severity = EricwMapPreflightSeverity::Error;
		warning.code = QStringLiteral("map-read-failed");
		warning.message = preflightText("Unable to read map file for ericw-tools preflight.");
		warning.filePath = mapPath;
		result.warnings.push_back(warning);
		return result;
	}
	return validateEricwMapPreflightText(QString::fromUtf8(file.readAll()), fileOptions);
}

EricwMapPreflightReport inspectEricwMapPreflightText(const QString& mapText, const QString& mapPath)
{
	EricwMapPreflightReport report;
	const QVector<MapEntity> entities = parseMapEntities(mapText);
	inspectMapPath(mapPath, &report);
	for (const MapEntity& entity : entities) {
		inspectEntityValues(entity, &report);
	}
	inspectEntityGroups(entities, &report);
	return report;
}

EricwMapPreflightReport inspectEricwMapPreflightFile(const QString& mapPath, QString* error)
{
	if (error) {
		error->clear();
	}
	EricwMapPreflightReport report;
	QFile file(mapPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		if (error) {
			*error = preflightText("Unable to read map file for ericw-tools preflight: %1").arg(QFileInfo(mapPath).fileName());
		}
		return report;
	}
	return inspectEricwMapPreflightText(QString::fromUtf8(file.readAll()), mapPath);
}

} // namespace vibestudio
