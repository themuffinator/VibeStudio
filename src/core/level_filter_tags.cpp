// Data-driven view filter tags and entity class filters. Workflow reference:
// TB ActionManager::createTagActions and createEntityDefinitionActions,
// MapViewBase::toggleTagVisible and toggleEntityDefinitionVisible, and the
// ViewEditor tag and entity definition lists at 4c756be1 (GPL-3.0-or-later);
// the tag layout follows TrenchBroom's documented game configuration format.
// Original Qt implementation; see docs/CREDITS.md.

#include "core/level_filter_tags.h"

#include "core/level_entity_create.h"
#include "core/level_face_flags.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace vibestudio {

namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelFilterTags)
};

const QLatin1String kTagPrefix("tag:");
const QLatin1String kClassPrefix("class:");
const QLatin1String kTagCamel("map.filterTag.");
const QLatin1String kTagKebab("map.filter-tag.");
const QLatin1String kClassCamel("map.filterClass.");
const QLatin1String kClassKebab("map.filter-class.");

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

// Preserve established ASCII ids; non-Latin names get a stable ASCII digest.
QString slugFor(const QString& name)
{
	QString slug;
	bool dash = false;
	for (const QChar ch : name.trimmed().toLower()) {
		if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
			if (dash && !slug.isEmpty()) {
				slug += QLatin1Char('-');
			}
			dash = false;
			slug += ch;
		} else {
			dash = true;
		}
	}
	if (slug.isEmpty() && std::any_of(name.cbegin(), name.cend(), [](QChar ch) { return ch.isLetterOrNumber(); })) {
		const QByteArray canonical = name.trimmed().normalized(QString::NormalizationForm_C).toLower().toUtf8();
		slug = QStringLiteral("name-%1").arg(QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex().left(16)));
	}
	return slug.left(64);
}

bool slugUsable(const QString& slug)
{
	return !slug.isEmpty() && slug.size() <= 64 && slugFor(slug) == slug;
}

// '*' any run, '?' one character and '\' escaping the next, ignoring case.
bool globMatches(const QString& pattern, const QString& text)
{
	qsizetype p = 0;
	qsizetype t = 0;
	qsizetype star = -1;
	qsizetype resume = 0;
	while (t < text.size()) {
		if (p < pattern.size() && pattern.at(p) == QLatin1Char('*')) {
			star = p++;
			resume = t;
			continue;
		}
		if (p < pattern.size()) {
			QChar expected = pattern.at(p);
			qsizetype width = 1;
			bool any = expected == QLatin1Char('?');
			if (expected == QLatin1Char('\\') && p + 1 < pattern.size()) {
				expected = pattern.at(p + 1);
				width = 2;
				any = false;
			}
			if (any || expected.toLower() == text.at(t).toLower()) {
				p += width;
				++t;
				continue;
			}
		}
		if (star < 0) {
			return false;
		}
		p = star + 1;
		t = ++resume;
	}
	while (p < pattern.size() && pattern.at(p) == QLatin1Char('*')) {
		++p;
	}
	return p == pattern.size();
}

// The texture's own name, or its path below textures/ for a pattern with
// a folder, lower case.
bool textureMatches(const QStringList& patterns, const QString& texture)
{
	QString path = texture.trimmed().toLower();
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	if (path.startsWith(QLatin1String("textures/"))) {
		path = path.mid(9);
	}
	const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
	const QString base = slash >= 0 ? path.mid(slash + 1) : path;
	return std::any_of(patterns.cbegin(), patterns.cend(), [&](const QString& pattern) {
		return globMatches(pattern, pattern.contains(QLatin1Char('/')) ? path : base);
	});
}

bool classMatches(const QStringList& patterns, const QString& className)
{
	const QString name = className.trimmed();
	return std::any_of(patterns.cbegin(), patterns.cend(), [&](const QString& pattern) { return globMatches(pattern, name); });
}

QStringList stringsOf(const QJsonValue& value)
{
	QStringList strings;
	if (value.isString()) {
		strings << value.toString();
	} else if (value.isArray()) {
		for (const QJsonValue& item : value.toArray()) {
			if (item.isString()) {
				strings << item.toString();
			}
		}
	}
	QStringList cleaned;
	for (const QString& text : std::as_const(strings)) {
		const QString lowered = text.trimmed().toLower();
		if (!lowered.isEmpty() && !cleaned.contains(lowered)) {
			cleaned << lowered;
		}
	}
	return cleaned;
}

// TrenchBroom's faceattribs lists: names in bit order, an entry without a
// name keeping its bit unnamed.
QStringList attributeNames(const QJsonValue& value)
{
	QStringList names;
	for (const QJsonValue& item : value.toArray()) {
		names << item.toObject().value(QStringLiteral("name")).toString().trimmed().toLower();
		if (names.size() >= 32) {
			break;
		}
	}
	return names;
}

// One tag entry; false with a warning in `problem` when it cannot be used.
bool parseEntry(const QJsonObject& object, bool brushScope, bool scopeFromMatch, const QString& source, const QStringList& contentNames,
	const QStringList& surfaceNames, LevelFilterTag* tag, QString* problem)
{
	const QString name = object.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) {
		return fail(problem, Text::tr("A tag without a name was skipped."));
	}
	const QString match = object.value(QStringLiteral("match")).toString().trimmed().toLower();
	tag->name = name.left(80);
	tag->id = levelFilterTagId(name);
	tag->source = source;
	tag->texture = object.value(QStringLiteral("texture")).toString().trimmed();
	if (tag->id.isEmpty()) {
		return fail(problem, Text::tr("The tag %1 has no letters or digits to name it by, so it was skipped.").arg(name));
	}
	if (match == QLatin1String("texture") || match == QLatin1String("material")) {
		tag->match = LevelFilterTagMatch::Texture;
	} else if (match == QLatin1String("surfaceparm")) {
		tag->match = LevelFilterTagMatch::SurfaceParm;
	} else if (match == QLatin1String("contentflag")) {
		tag->match = LevelFilterTagMatch::ContentFlag;
	} else if (match == QLatin1String("surfaceflag")) {
		tag->match = LevelFilterTagMatch::SurfaceFlag;
	} else if (match == QLatin1String("classname")) {
		tag->match = LevelFilterTagMatch::ClassName;
	} else {
		return fail(problem, Text::tr("The tag %1 matches by \"%2\", which is not texture, material, surfaceparm, contentflag, surfaceflag or classname, so it was skipped.")
								 .arg(name, match));
	}
	const bool classTag = tag->match == LevelFilterTagMatch::ClassName;
	if (!scopeFromMatch && classTag != brushScope) {
		return fail(problem, brushScope ? Text::tr("The brush tag %1 must match by classname, so it was skipped.").arg(name)
										: Text::tr("The face tag %1 cannot match by classname, so it was skipped.").arg(name));
	}
	if (tag->match == LevelFilterTagMatch::ContentFlag || tag->match == LevelFilterTagMatch::SurfaceFlag) {
		QJsonArray flags;
		const QJsonValue value = object.value(QStringLiteral("flags"));
		if (value.isArray()) {
			flags = value.toArray();
		} else if (!value.isUndefined() && !value.isNull()) {
			flags.append(value);
		}
		for (const QJsonValue& flag : std::as_const(flags)) {
			if (flag.isDouble()) {
				const double number = flag.toDouble();
				if (number < 0 || number > 4294967295.0 || number != static_cast<double>(static_cast<qint64>(number))) {
					return fail(problem, Text::tr("The tag %1 has a flag number that does not fit a 32-bit flag word, so it was skipped.").arg(name));
				}
				tag->flagBits |= static_cast<quint32>(static_cast<qint64>(number));
				continue;
			}
			const QString text = flag.toString().trimmed().toLower();
			bool ok = false;
			const qulonglong number = text.startsWith(QLatin1String("0x")) ? text.mid(2).toULongLong(&ok, 16) : text.toULongLong(&ok, 10);
			if (ok && number <= 0xffffffffULL) {
				tag->flagBits |= static_cast<quint32>(number);
			} else if (!text.isEmpty() && !tag->flagNames.contains(text)) {
				tag->flagNames << text;
			}
		}
		if (tag->flagBits == 0 && tag->flagNames.isEmpty()) {
			return fail(problem, Text::tr("The flag tag %1 names no flags, so it was skipped.").arg(name));
		}
		tag->contentFlagNames = contentNames;
		tag->surfaceFlagNames = surfaceNames;
		return true;
	}
	tag->patterns = stringsOf(object.value(QStringLiteral("pattern")));
	if (tag->patterns.isEmpty()) {
		return fail(problem, Text::tr("The tag %1 has no pattern, so it was skipped.").arg(name));
	}
	return true;
}

void addTag(LevelFilterTagSet* set, const LevelFilterTag& tag)
{
	if (set->find(tag.id)) {
		set->warnings << Text::tr("A second tag named %1 was skipped; the first one stays.").arg(tag.name);
		return;
	}
	if (set->tags.size() >= kLevelFilterTagLimit) {
		set->warnings << Text::tr("Only %1 tags are kept; %2 and any after it were skipped.").arg(kLevelFilterTagLimit).arg(tag.name);
		return;
	}
	set->tags.push_back(tag);
}

QString sourceName(const QString& source)
{
	return QDir::toNativeSeparators(source);
}

int lineAt(const QByteArray& text, qsizetype offset)
{
	const qsizetype end = std::clamp<qsizetype>(offset, 0, text.size());
	return 1 + static_cast<int>(std::count(text.cbegin(), text.cbegin() + end, '\n'));
}

bool isNameStart(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool isDigit(char ch)
{
	return ch >= '0' && ch <= '9';
}

bool isHexDigit(char ch)
{
	return isDigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

// TrenchBroom writes its game configurations in its own expression language,
// which reads JSON and more: '//' and '/* */' comments, bare names such as
// modelscale in "scale": [ modelscale, modelscale_vec ], a comma before a
// closing bracket, and backslashes that JSON does not define, which stay in
// the string as written, so the stock Quake configuration's Liquid pattern
// "\**" is an escaped '*' and then any run. This turns such text into strict
// JSON with the same line breaks, so a parse error keeps its line, and leaves
// strict JSON as it is. False, with the problem and its line, for a string or
// comment that never closes. Workflow reference: TB's game configuration
// reader at 4c756be1 (GPL-3.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.
bool trenchBroomTextAsJson(const QByteArray& text, QByteArray* json, int* line, QString* problem)
{
	QByteArray out;
	out.reserve(text.size() + text.size() / 8 + 16);
	const qsizetype size = text.size();
	qsizetype at = text.startsWith("\xEF\xBB\xBF") ? 3 : 0;
	// A comma waits for the next token, so one before ']' or '}' drops out.
	bool comma = false;
	const auto flushComma = [&](char next) {
		if (comma && next != ']' && next != '}') {
			out += ',';
		}
		comma = false;
	};
	while (at < size) {
		const char ch = text.at(at);
		if (ch == '"') {
			const qsizetype start = at;
			flushComma(ch);
			out += '"';
			++at;
			bool closed = false;
			while (at < size) {
				const char c = text.at(at);
				if (c == '\\' && at + 1 < size) {
					const char escaped = text.at(at + 1);
					switch (escaped) {
					case '"':
					case '\\':
					case '/':
					case 'b':
					case 'f':
					case 'n':
					case 'r':
					case 't':
						out += c;
						out += escaped;
						at += 2;
						continue;
					case 'u':
						if (at + 5 < size && isHexDigit(text.at(at + 2)) && isHexDigit(text.at(at + 3)) && isHexDigit(text.at(at + 4))
							&& isHexDigit(text.at(at + 5))) {
							out += text.mid(at, 6);
							at += 6;
							continue;
						}
						break;
					default:
						break;
					}
					// Kept as written, as TrenchBroom keeps it: "\*" stays "\*".
					out += "\\\\";
					++at;
					continue;
				}
				if (c == '"') {
					out += '"';
					++at;
					closed = true;
					break;
				}
				if (c == '\t') {
					out += "\\t";
				} else {
					out += c;
				}
				++at;
			}
			if (!closed) {
				*line = lineAt(text, start);
				*problem = Text::tr("a string that never closes");
				return false;
			}
			continue;
		}
		if (ch == '/' && at + 1 < size && text.at(at + 1) == '/') {
			while (at < size && text.at(at) != '\n' && text.at(at) != '\r') {
				++at;
			}
			continue;
		}
		if (ch == '/' && at + 1 < size && text.at(at + 1) == '*') {
			const qsizetype end = text.indexOf("*/", at + 2);
			if (end < 0) {
				*line = lineAt(text, at);
				*problem = Text::tr("a comment that never closes");
				return false;
			}
			const auto breaks = std::count(text.cbegin() + at, text.cbegin() + end, '\n');
			out += breaks > 0 ? QByteArray(breaks, '\n') : QByteArray(" ");
			at = end + 2;
			continue;
		}
		if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
			out += ch;
			++at;
			continue;
		}
		if (ch == ',') {
			if (comma) {
				// Two in a row stay for the JSON reader to report.
				out += ',';
			}
			comma = true;
			++at;
			continue;
		}
		flushComma(ch);
		if (isDigit(ch) || ch == '-' || ch == '.') {
			// A number whole, so an exponent's letter is not read as a name.
			const qsizetype start = at;
			const auto numberPart = [](char c) { return isDigit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E'; };
			while (at < size && numberPart(text.at(at))) {
				++at;
			}
			out += text.mid(start, at - start);
			continue;
		}
		if (isNameStart(ch)) {
			const qsizetype start = at;
			while (at < size && (isNameStart(text.at(at)) || isDigit(text.at(at)))) {
				++at;
			}
			const QByteArray name = text.mid(start, at - start);
			const bool literal = name == "true" || name == "false" || name == "null";
			if (!literal) {
				out += '"';
			}
			out += name;
			if (!literal) {
				out += '"';
			}
			continue;
		}
		out += ch;
		++at;
	}
	if (comma) {
		out += ',';
	}
	*json = out;
	return true;
}

// Whether a brush owns brushes or patches, or the built-in catalogues make
// its class a brush class; worldspawn is never one.
QSet<int> brushEntityIds(const LevelMapDocument& document)
{
	QSet<int> owners;
	for (const LevelMapBrush& brush : document.brushes) {
		owners.insert(brush.entityId);
	}
	for (const LevelMapPatch& patch : document.patches) {
		owners.insert(patch.entityId);
	}
	return owners;
}

bool isWorldspawn(const QString& className)
{
	return className.trimmed().compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
}

// A class a filter can name: one a map can hold, without the ',' that
// separates the View tab's saved list and the CLI's lists.
bool classFilterNameUsable(const QString& className)
{
	return levelEntityCreateClassNameUsable(className) && !className.contains(QLatin1Char(','));
}

struct TagContext {
	const LevelFilterTag& tag;
	quint32 bits = 0;
	const LevelViewFilterMaterials* materials = nullptr;
	mutable QHash<QString, const LevelViewFilterShader*> resolved;
};

const LevelViewFilterShader* shaderFor(const TagContext& context, const QString& texture)
{
	if (!context.materials) {
		return nullptr;
	}
	const auto known = context.resolved.constFind(texture);
	if (known != context.resolved.cend()) {
		return known.value();
	}
	const LevelViewFilterShader* shader = context.materials->find(texture);
	context.resolved.insert(texture, shader);
	return shader;
}

bool surfaceMatches(const TagContext& context, const QString& texture, qint64 contentFlags, qint64 surfaceFlags, bool patch)
{
	switch (context.tag.match) {
	case LevelFilterTagMatch::Texture:
		return textureMatches(context.tag.patterns, texture);
	case LevelFilterTagMatch::SurfaceParm: {
		const LevelViewFilterShader* shader = shaderFor(context, texture);
		if (!shader) {
			return false;
		}
		return std::any_of(context.tag.patterns.cbegin(), context.tag.patterns.cend(), [shader](const QString& pattern) {
			return std::any_of(shader->surfaceParms.cbegin(), shader->surfaceParms.cend(), [&pattern](const QString& parm) { return globMatches(pattern, parm); });
		});
	}
	case LevelFilterTagMatch::ContentFlag:
		return !patch && (static_cast<quint32>(contentFlags & 0xffffffff) & context.bits) != 0;
	case LevelFilterTagMatch::SurfaceFlag:
		return !patch && (static_cast<quint32>(surfaceFlags & 0xffffffff) & context.bits) != 0;
	case LevelFilterTagMatch::ClassName:
		break;
	}
	return false;
}

bool brushMatchesWhole(const TagContext& context, const LevelMapBrush& brush)
{
	if (brush.faces.isEmpty()) {
		return false;
	}
	return std::all_of(brush.faces.cbegin(), brush.faces.cend(), [&context](const LevelMapBrushFace& face) {
		return surfaceMatches(context, face.textureName, face.contentFlags, face.surfaceFlags, false);
	});
}

TagContext contextFor(const LevelMapDocument& document, const LevelFilterTag& tag, const LevelViewFilterMaterials& materials)
{
	TagContext context {tag};
	context.bits = levelFilterTagFlagBits(tag, document);
	context.materials = document.format == LevelMapFormat::Quake3Map && !materials.isEmpty() ? &materials : nullptr;
	return context;
}

} // namespace

const LevelFilterTag* LevelFilterTagSet::find(const QString& id) const
{
	for (const LevelFilterTag& tag : tags) {
		if (tag.id == id) {
			return &tag;
		}
	}
	return nullptr;
}

QStringList LevelFilterTagSet::ids() const
{
	QStringList result;
	for (const LevelFilterTag& tag : tags) {
		result << tag.id;
	}
	return result;
}

QString levelFilterTagsManifestKey()
{
	return QStringLiteral("levelFilterTags");
}

bool parseLevelFilterTags(const QJsonValue& value, const QString& source, LevelFilterTagSet* set, QString* error)
{
	if (!set) {
		return fail(error, Text::tr("Missing tag result."));
	}
	QStringList contentNames;
	QStringList surfaceNames;
	QJsonObject layout;
	QJsonArray plain;
	bool plainList = false;
	if (value.isArray()) {
		plain = value.toArray();
		plainList = true;
	} else if (value.isObject()) {
		const QJsonObject root = value.toObject();
		// A whole TrenchBroom game configuration: its tags and flag names.
		const QJsonObject attributes = root.value(QStringLiteral("faceattribs")).toObject();
		contentNames = attributeNames(attributes.value(QStringLiteral("contentflags")));
		surfaceNames = attributeNames(attributes.value(QStringLiteral("surfaceflags")));
		if (root.value(QStringLiteral("tags")).isObject()) {
			layout = root.value(QStringLiteral("tags")).toObject();
		} else if (root.value(QStringLiteral("tags")).isArray()) {
			plain = root.value(QStringLiteral("tags")).toArray();
			plainList = true;
		} else {
			layout = root;
		}
		if (!plainList && !layout.value(QStringLiteral("brush")).isArray() && !layout.value(QStringLiteral("brushface")).isArray()) {
			return fail(error, Text::tr("%1 holds no tags: give a \"brush\" or \"brushface\" list, as a TrenchBroom game configuration's \"tags\" does.")
								   .arg(sourceName(source)));
		}
	} else {
		return fail(error, Text::tr("%1 holds no tags: give an object of \"brush\" and \"brushface\" lists, or a list of tags.").arg(sourceName(source)));
	}
	if (!set->sources.contains(source)) {
		set->sources << source;
	}
	const auto read = [&](const QJsonArray& entries, bool brushScope, bool scopeFromMatch) {
		for (const QJsonValue& entry : entries) {
			if (!entry.isObject()) {
				set->warnings << Text::tr("An entry of %1 that is not a tag object was skipped.").arg(sourceName(source));
				continue;
			}
			LevelFilterTag tag;
			QString problem;
			if (!parseEntry(entry.toObject(), brushScope, scopeFromMatch, source, contentNames, surfaceNames, &tag, &problem)) {
				set->warnings << problem;
				continue;
			}
			addTag(set, tag);
		}
	};
	if (plainList) {
		read(plain, false, true);
	} else {
		read(layout.value(QStringLiteral("brush")).toArray(), true, false);
		read(layout.value(QStringLiteral("brushface")).toArray(), false, false);
	}
	return true;
}

bool loadLevelFilterTagFile(const QString& path, LevelFilterTagSet* set, QString* error)
{
	const QFileInfo info(path);
	if (!info.isFile()) {
		return fail(error, Text::tr("There is no tag file at %1.").arg(sourceName(path)));
	}
	if (info.size() > kLevelFilterTagFileLimit) {
		return fail(error, Text::tr("%1 is larger than 4 MB, too large for a tag file.").arg(sourceName(path)));
	}
	QFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return fail(error, Text::tr("Could not read %1: %2").arg(sourceName(path), file.errorString()));
	}
	return parseLevelFilterTagText(file.readAll(), info.absoluteFilePath(), set, error);
}

bool parseLevelFilterTagText(const QByteArray& text, const QString& source, LevelFilterTagSet* set, QString* error)
{
	QByteArray json;
	int line = 0;
	QString problem;
	if (!trenchBroomTextAsJson(text, &json, &line, &problem)) {
		return fail(error, Text::tr("%1 is neither JSON nor a TrenchBroom game configuration: %2 on line %3.").arg(sourceName(source), problem).arg(line));
	}
	QJsonParseError parse;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parse);
	if (parse.error != QJsonParseError::NoError) {
		return fail(error, Text::tr("%1 is neither JSON nor a TrenchBroom game configuration: %2 on line %3.")
							   .arg(sourceName(source), parse.errorString())
							   .arg(lineAt(json, parse.offset)));
	}
	return parseLevelFilterTags(document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()), source, set, error);
}

LevelFilterTagSet levelFilterTagsForProject(const ProjectManifest& manifest)
{
	LevelFilterTagSet set;
	const QJsonValue value = manifest.extraFields.value(levelFilterTagsManifestKey());
	if (value.isUndefined() || value.isNull()) {
		return set;
	}
	QString error;
	if (value.isString()) {
		// A tag file beside the project, such as a game configuration.
		const QString named = value.toString().trimmed();
		const QString path = QFileInfo(named).isAbsolute() ? named : QDir(manifest.rootPath).absoluteFilePath(named);
		if (!loadLevelFilterTagFile(path, &set, &error)) {
			set.warnings << error;
		}
		return set;
	}
	if (!parseLevelFilterTags(value, projectManifestPath(manifest.rootPath), &set, &error)) {
		set.warnings << error;
	}
	return set;
}

void mergeLevelFilterTags(LevelFilterTagSet* into, const LevelFilterTagSet& from)
{
	if (!into) {
		return;
	}
	for (const QString& source : from.sources) {
		if (!into->sources.contains(source)) {
			into->sources << source;
		}
	}
	into->warnings += from.warnings;
	for (const LevelFilterTag& tag : from.tags) {
		addTag(into, tag);
	}
}

QString levelFilterTagId(const QString& name)
{
	const QString slug = slugFor(name);
	return slug.isEmpty() ? QString() : kTagPrefix + slug;
}

bool isLevelFilterTagId(const QString& id)
{
	return id.startsWith(kTagPrefix) && slugUsable(id.mid(kTagPrefix.size()));
}

QString levelFilterTagCommandId(const QString& tagId)
{
	return isLevelFilterTagId(tagId) ? kTagCamel + tagId.mid(kTagPrefix.size()) : QString();
}

bool levelFilterTagForCommand(const QString& commandId, QString* tagId)
{
	const QString id = commandId.trimmed();
	for (const QLatin1String& prefix : {kTagCamel, kTagKebab}) {
		if (id.startsWith(prefix) && slugUsable(id.mid(prefix.size()))) {
			if (tagId) {
				*tagId = kTagPrefix + id.mid(prefix.size());
			}
			return true;
		}
	}
	return false;
}

QString levelFilterClassId(const QString& className)
{
	const QString name = className.trimmed();
	return classFilterNameUsable(name) ? kClassPrefix + name.toLower() : QString();
}

bool levelFilterClassForId(const QString& id, QString* className)
{
	if (!id.startsWith(kClassPrefix) || !classFilterNameUsable(id.mid(kClassPrefix.size()))) {
		return false;
	}
	if (className) {
		*className = id.mid(kClassPrefix.size());
	}
	return true;
}

QString levelFilterClassCommandId(const QString& className)
{
	const QString name = className.trimmed();
	return classFilterNameUsable(name) ? kClassCamel + name : QString();
}

bool levelFilterClassForCommand(const QString& commandId, QString* className)
{
	const QString id = commandId.trimmed();
	for (const QLatin1String& prefix : {kClassCamel, kClassKebab}) {
		if (id.startsWith(prefix) && classFilterNameUsable(id.mid(prefix.size()))) {
			if (className) {
				*className = id.mid(prefix.size());
			}
			return true;
		}
	}
	return false;
}

QString levelFilterTagDescription(const LevelFilterTag& tag)
{
	QString what;
	const QString patterns = tag.patterns.join(QStringLiteral(", "));
	QStringList flags = tag.flagNames;
	if (tag.flagBits != 0) {
		flags << QStringLiteral("0x%1").arg(tag.flagBits, 0, 16);
	}
	switch (tag.match) {
	case LevelFilterTagMatch::Texture:
		what = Text::tr("Brushes textured all over, and faces and patches elsewhere, whose texture matches %1.").arg(patterns);
		break;
	case LevelFilterTagMatch::SurfaceParm:
		what = Text::tr("Brushes, faces and patches whose Quake III shader declares surfaceparm %1.").arg(patterns);
		break;
	case LevelFilterTagMatch::ContentFlag:
		what = Text::tr("Brushes, and faces elsewhere, with any of the content flags %1.").arg(flags.join(QStringLiteral(", ")));
		break;
	case LevelFilterTagMatch::SurfaceFlag:
		what = Text::tr("Brushes, and faces elsewhere, with any of the surface flags %1.").arg(flags.join(QStringLiteral(", ")));
		break;
	case LevelFilterTagMatch::ClassName:
		what = Text::tr("Brush entities whose class matches %1, with their brushes.").arg(patterns);
		break;
	}
	return Text::tr("%1 Defined in %2.").arg(what, sourceName(tag.source));
}

bool levelFilterTagMatchesName(const LevelFilterTag& tag, const QString& name, const LevelViewFilterMaterials& materials)
{
	if (tag.match == LevelFilterTagMatch::ClassName) {
		return classMatches(tag.patterns, name);
	}
	TagContext context {tag};
	context.materials = &materials;
	return surfaceMatches(context, name, 0, 0, true);
}

quint32 levelFilterTagFlagBits(const LevelFilterTag& tag, const LevelMapDocument& document, QStringList* unresolved)
{
	quint32 bits = tag.flagBits;
	if (tag.match != LevelFilterTagMatch::ContentFlag && tag.match != LevelFilterTagMatch::SurfaceFlag) {
		return bits;
	}
	const bool content = tag.match == LevelFilterTagMatch::ContentFlag;
	const QStringList& own = content ? tag.contentFlagNames : tag.surfaceFlagNames;
	const QVector<MaterialFlagDescriptor> game =
		levelFaceFlagNames(levelFaceFlagGame(document), content ? LevelFaceFlagWord::Content : LevelFaceFlagWord::Surface);
	for (const QString& name : tag.flagNames) {
		const qsizetype index = own.indexOf(name);
		if (index >= 0 && index < 32) {
			bits |= 1u << index;
			continue;
		}
		const auto found = std::find_if(game.cbegin(), game.cend(), [&name](const MaterialFlagDescriptor& flag) { return flag.id == name; });
		if (found != game.cend()) {
			bits |= found->bit;
		} else if (unresolved) {
			*unresolved << name;
		}
	}
	return bits;
}

bool levelFilterTagsApply(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

QVector<LevelMapSelectionRef> levelFilterTagMatches(const LevelMapDocument& document, const LevelFilterTag& tag, const LevelViewFilterMaterials& materials)
{
	QVector<LevelMapSelectionRef> matches;
	if (!levelFilterTagsApply(document)) {
		return matches;
	}
	if (tag.match == LevelFilterTagMatch::ClassName) {
		// TrenchBroom's brush tags: brush entities, with their brushes.
		const QSet<int> owners = brushEntityIds(document);
		for (const LevelMapEntity& entity : document.entities) {
			if (!isWorldspawn(entity.className) && owners.contains(entity.id) && classMatches(tag.patterns, entity.className)) {
				matches.push_back({LevelMapSelectionKind::Entity, entity.id});
			}
		}
		return matches;
	}
	const TagContext context = contextFor(document, tag, materials);
	for (const LevelMapBrush& brush : document.brushes) {
		if (brushMatchesWhole(context, brush)) {
			matches.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (surfaceMatches(context, patch.textureName, 0, 0, true)) {
			matches.push_back({LevelMapSelectionKind::QuakePatch, patch.id});
		}
	}
	return matches;
}

QVector<LevelSurfaceFace> levelFilterTagFaceMatches(const LevelMapDocument& document, const LevelFilterTag& tag, const LevelViewFilterMaterials& materials)
{
	QVector<LevelSurfaceFace> faces;
	if (!levelFilterTagsApply(document) || tag.match == LevelFilterTagMatch::ClassName) {
		return faces;
	}
	const TagContext context = contextFor(document, tag, materials);
	for (const LevelMapBrush& brush : document.brushes) {
		if (brushMatchesWhole(context, brush)) {
			continue;
		}
		for (int index = 0; index < brush.faces.size(); ++index) {
			const LevelMapBrushFace& face = brush.faces.at(index);
			if (surfaceMatches(context, face.textureName, face.contentFlags, face.surfaceFlags, false)) {
				faces.push_back({brush.id, index});
			}
		}
	}
	return faces;
}

QVector<LevelFilterClass> levelFilterClasses(const LevelMapDocument& document, const EntityDefinitionCatalogue& catalogue)
{
	QVector<LevelFilterClass> classes;
	if (!levelFilterTagsApply(document)) {
		return classes;
	}
	QHash<QString, int> counts;
	QHash<QString, QString> spelling;
	for (const LevelMapEntity& entity : document.entities) {
		const QString name = entity.className.trimmed();
		if (name.isEmpty() || isWorldspawn(name) || !classFilterNameUsable(name)) {
			continue;
		}
		const QString key = name.toLower();
		counts[key] += 1;
		if (!spelling.contains(key)) {
			spelling.insert(key, name);
		}
	}
	QSet<QString> seen;
	for (const EntityClassDefinition& definition : catalogue.classes) {
		const QString name = definition.className.trimmed();
		if ((definition.kind != EntityClassKind::Point && definition.kind != EntityClassKind::Brush) || !classFilterNameUsable(name)) {
			continue;
		}
		const QString key = name.toLower();
		if (seen.contains(key)) {
			continue;
		}
		seen.insert(key);
		LevelFilterClass entry;
		entry.className = name;
		entry.kind = definition.kind;
		entry.count = counts.value(key);
		entry.defined = true;
		classes.push_back(entry);
	}
	const QSet<int> owners = brushEntityIds(document);
	QHash<QString, bool> owning;
	for (const LevelMapEntity& entity : document.entities) {
		if (owners.contains(entity.id)) {
			owning.insert(entity.className.trimmed().toLower(), true);
		}
	}
	for (auto it = spelling.cbegin(); it != spelling.cend(); ++it) {
		if (seen.contains(it.key())) {
			continue;
		}
		seen.insert(it.key());
		LevelFilterClass entry;
		entry.className = it.value();
		entry.kind = owning.value(it.key()) ? EntityClassKind::Brush : EntityClassKind::Point;
		entry.count = counts.value(it.key());
		classes.push_back(entry);
	}
	for (LevelFilterClass& entry : classes) {
		entry.id = levelFilterClassId(entry.className);
		const QString lowered = entry.className.toLower();
		const qsizetype underscore = lowered.indexOf(QLatin1Char('_'));
		entry.group = underscore > 0 ? lowered.left(underscore) : lowered;
	}
	std::sort(classes.begin(), classes.end(), [](const LevelFilterClass& left, const LevelFilterClass& right) {
		const int order = left.className.compare(right.className, Qt::CaseInsensitive);
		return order != 0 ? order < 0 : left.className < right.className;
	});
	return classes;
}

QVector<LevelMapSelectionRef> levelFilterClassMatches(const LevelMapDocument& document, const QString& className)
{
	QVector<LevelMapSelectionRef> matches;
	const QString name = className.trimmed();
	if (!levelFilterTagsApply(document) || name.isEmpty() || isWorldspawn(name)) {
		return matches;
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.trimmed().compare(name, Qt::CaseInsensitive) == 0) {
			matches.push_back({LevelMapSelectionKind::Entity, entity.id});
		}
	}
	return matches;
}

LevelViewFilterHidden levelViewFilterHidden(const LevelMapDocument& document, const QStringList& hiddenIds, const LevelViewFilterMaterials& materials,
	const LevelFilterTagSet& tags)
{
	LevelViewFilterHidden hidden;
	QStringList kinds;
	QVector<const LevelFilterTag*> hiddenTags;
	QStringList hiddenClasses;
	for (const QString& id : hiddenIds) {
		QString className;
		if (isLevelFilterTagId(id)) {
			if (const LevelFilterTag* tag = tags.find(id); tag && !hiddenTags.contains(tag)) {
				hiddenTags << tag;
			}
		} else if (levelFilterClassForId(id, &className)) {
			if (!hiddenClasses.contains(className)) {
				hiddenClasses << className;
			}
		} else if (!kinds.contains(id)) {
			kinds << id;
		}
	}
	if (!kinds.isEmpty()) {
		hidden.objects = levelViewFilteredObjects(document, kinds, materials);
	}
	QSet<QPair<int, int>> seen;
	for (const LevelMapSelectionRef& ref : std::as_const(hidden.objects)) {
		seen.insert({static_cast<int>(ref.kind), ref.objectId});
	}
	const auto add = [&](const QVector<LevelMapSelectionRef>& refs) {
		for (const LevelMapSelectionRef& ref : refs) {
			if (!seen.contains({static_cast<int>(ref.kind), ref.objectId})) {
				seen.insert({static_cast<int>(ref.kind), ref.objectId});
				hidden.objects.push_back(ref);
			}
		}
	};
	for (const LevelFilterTag* tag : std::as_const(hiddenTags)) {
		add(levelFilterTagMatches(document, *tag, materials));
	}
	for (const QString& className : std::as_const(hiddenClasses)) {
		add(levelFilterClassMatches(document, className));
	}
	// Faces go one by one only on brushes nothing hides whole, directly or
	// with their entity.
	QVector<LevelSurfaceFace> faces = kinds.isEmpty() ? QVector<LevelSurfaceFace>() : levelViewFilteredFaces(document, kinds, materials);
	for (const LevelFilterTag* tag : std::as_const(hiddenTags)) {
		faces += levelFilterTagFaceMatches(document, *tag, materials);
	}
	if (faces.isEmpty()) {
		return hidden;
	}
	QSet<int> hiddenBrushes;
	QSet<int> hiddenEntities;
	for (const LevelMapSelectionRef& ref : std::as_const(hidden.objects)) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			hiddenBrushes.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::Entity) {
			hiddenEntities.insert(ref.objectId);
		}
	}
	QHash<int, int> ownerOf;
	for (const LevelMapBrush& brush : document.brushes) {
		ownerOf.insert(brush.id, brush.entityId);
	}
	// A brush whose every face goes one by one, under several kinds or tags
	// together (caulk and skip sides, say), is hidden whole, plan views
	// included, as TrenchBroom's EditorContext hides a brush whose faces all
	// carry one of the hidden tags (BrushNode::allFacesHaveAnyTagInMask).
	QHash<int, QSet<int>> facesOf;
	for (const LevelSurfaceFace& face : std::as_const(faces)) {
		facesOf[face.brushId].insert(face.faceIndex);
	}
	for (const LevelMapBrush& brush : document.brushes) {
		const auto named = facesOf.constFind(brush.id);
		if (named == facesOf.cend() || brush.faces.isEmpty() || named->size() < brush.faces.size() || hiddenBrushes.contains(brush.id)
			|| hiddenEntities.contains(brush.entityId)) {
			continue;
		}
		bool every = true;
		for (int index = 0; every && index < brush.faces.size(); ++index) {
			every = named->contains(index);
		}
		if (every) {
			hiddenBrushes.insert(brush.id);
			hidden.objects.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
		}
	}
	QSet<QPair<int, int>> seenFaces;
	for (const LevelSurfaceFace& face : std::as_const(faces)) {
		if (hiddenBrushes.contains(face.brushId) || hiddenEntities.contains(ownerOf.value(face.brushId, -1)) || seenFaces.contains({face.brushId, face.faceIndex})) {
			continue;
		}
		seenFaces.insert({face.brushId, face.faceIndex});
		hidden.faces.push_back(face);
	}
	return hidden;
}

QStringList levelViewFiltersWithoutClasses(const QStringList& hiddenIds)
{
	QStringList kept;
	for (const QString& id : hiddenIds) {
		if (!id.startsWith(kClassPrefix) && !kept.contains(id)) {
			kept << id;
		}
	}
	return kept;
}

QStringList levelFilterTagsInverted(const LevelFilterTagSet& tags, const QStringList& hiddenIds)
{
	QStringList inverted;
	const QStringList ids = tags.ids();
	for (const QString& id : hiddenIds) {
		if (!ids.contains(id) && !inverted.contains(id)) {
			inverted << id;
		}
	}
	for (const QString& id : ids) {
		if (!hiddenIds.contains(id)) {
			inverted << id;
		}
	}
	return inverted;
}

} // namespace vibestudio
