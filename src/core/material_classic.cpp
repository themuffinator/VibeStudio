#include "core/material_classic.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

constexpr qint64 kLumpLimit = 16 * 1024 * 1024;

QString upperKey(const QString& name)
{
	return name.trimmed().toUpper();
}

QString fixedName(const QByteArray& bytes, qsizetype at, int length)
{
	const QByteArray field = bytes.mid(at, length);
	const qsizetype nul = field.indexOf('\0');
	return QString::fromLatin1(nul < 0 ? field : field.left(nul)).trimmed();
}

void writeFixedName(QByteArray* bytes, qsizetype at, int length, const QString& name)
{
	const QByteArray latin = name.toLatin1().left(length);
	for (int index = 0; index < length; ++index) {
		(*bytes)[at + index] = index < latin.size() ? latin.at(index) : '\0';
	}
}

qint32 readI32(const QByteArray& bytes, qsizetype at)
{
	return qFromLittleEndian<qint32>(bytes.constData() + at);
}

qint16 readI16(const QByteArray& bytes, qsizetype at)
{
	return qFromLittleEndian<qint16>(bytes.constData() + at);
}

quint32 readU32(const QByteArray& bytes, qsizetype at)
{
	return qFromLittleEndian<quint32>(bytes.constData() + at);
}

void appendI32(QByteArray* bytes, qint32 value)
{
	char buffer[4];
	qToLittleEndian(value, buffer);
	bytes->append(buffer, 4);
}

void appendI16(QByteArray* bytes, qint16 value)
{
	char buffer[2];
	qToLittleEndian(value, buffer);
	bytes->append(buffer, 2);
}

void appendName(QByteArray* bytes, const QString& name, int length)
{
	QByteArray field(length, '\0');
	const QByteArray latin = name.toUpper().toLatin1().left(length - 1);
	std::copy(latin.cbegin(), latin.cend(), field.begin());
	bytes->append(field);
}

MaterialDiagnostic diagnostic(MaterialDiagnosticSeverity severity, const QString& code, const QString& message, const QString& material = {},
	int line = 0)
{
	MaterialDiagnostic result;
	result.severity = severity;
	result.code = code;
	result.message = message;
	result.material = material;
	result.line = line;
	return result;
}

// A lump's name for Doom purposes: the file name without a .lmp/.txt suffix,
// so PK3 `animdefs.txt` and WAD `ANIMDEFS` match alike.
QString lumpKey(const QString& virtualPath)
{
	QString name = packageVirtualPathFileName(virtualPath);
	const int dot = name.lastIndexOf(QLatin1Char('.'));
	if (dot > 0) {
		name.truncate(dot);
	}
	return name.toUpper();
}

// The last occurrence of a lump wins, as W_GetNumForName finds it.
int lastLump(const QVector<PackageEntry>& entries, const QString& name, const QStringList& allowedFolders = {})
{
	int found = -1;
	for (int index = 0; index < entries.size(); ++index) {
		const PackageEntry& entry = entries.at(index);
		if (entry.kind != PackageEntryKind::File || lumpKey(entry.virtualPath) != name) {
			continue;
		}
		const QString folder = packageVirtualPathParent(entry.virtualPath).toLower();
		if (!folder.isEmpty() && !allowedFolders.isEmpty() && !allowedFolders.contains(folder)) {
			continue;
		}
		found = index;
	}
	return found;
}

bool readLump(const PackageArchiveReader& reader, int index, QByteArray* bytes, QString* error)
{
	if (index < 0) {
		return false;
	}
	return reader.readEntryAt(index, bytes, error, kLumpLimit);
}

// Tokens for ANIMDEFS and SWANTBLS: words, numbers and quoted strings, with
// `//`, `/* */` and (SWANTBLS) `#`/`;` comments.
struct ScanToken {
	QString text;
	int line = 1;
	int start = 0;
	int end = 0;
	bool quoted = false;
};

QVector<ScanToken> scanWords(const QString& text, bool hashComments)
{
	QVector<ScanToken> tokens;
	int line = 1;
	int position = 0;
	const int size = text.size();
	while (position < size) {
		const QChar c = text.at(position);
		if (c == QLatin1Char('\n')) {
			++line;
			++position;
			continue;
		}
		if (c.isSpace() || c == QLatin1Char(',')) {
			++position;
			continue;
		}
		if ((c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('/'))
			|| (hashComments && (c == QLatin1Char('#') || c == QLatin1Char(';')))) {
			while (position < size && text.at(position) != QLatin1Char('\n')) {
				++position;
			}
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('*')) {
			position += 2;
			while (position + 1 < size && !(text.at(position) == QLatin1Char('*') && text.at(position + 1) == QLatin1Char('/'))) {
				if (text.at(position) == QLatin1Char('\n')) {
					++line;
				}
				++position;
			}
			position = std::min(size, position + 2);
			continue;
		}
		ScanToken token;
		token.line = line;
		token.start = position;
		if (c == QLatin1Char('"')) {
			++position;
			const int contentStart = position;
			while (position < size && text.at(position) != QLatin1Char('"') && text.at(position) != QLatin1Char('\n')) {
				++position;
			}
			token.text = text.mid(contentStart, position - contentStart);
			token.quoted = true;
			if (position < size && text.at(position) == QLatin1Char('"')) {
				++position;
			}
		} else {
			while (position < size && !text.at(position).isSpace() && text.at(position) != QLatin1Char(',')) {
				++position;
			}
			token.text = text.mid(token.start, position - token.start);
		}
		token.end = position;
		tokens.push_back(token);
	}
	return tokens;
}

bool isWhole(const QString& text, int* value)
{
	bool ok = false;
	const int number = text.toInt(&ok);
	if (ok && value) {
		*value = number;
	}
	return ok;
}

QVector<MaterialFrame> rotatedFrames(const QStringList& names, int rotation, double seconds)
{
	QVector<MaterialFrame> frames;
	const int count = static_cast<int>(names.size());
	for (int index = 0; index < count; ++index) {
		MaterialFrame frame;
		frame.name = names.at((rotation + index) % count);
		frame.duration = seconds;
		frames.push_back(frame);
	}
	return frames;
}

} // namespace

// ---------------------------------------------------------------------------
// Doom catalog
// ---------------------------------------------------------------------------

int DoomMaterialCatalog::textureIndex(const QString& name) const
{
	const QString key = upperKey(name);
	for (int index = 0; index < textures.size(); ++index) {
		if (upperKey(textures.at(index).name) == key) {
			return index;
		}
	}
	return -1;
}

int DoomMaterialCatalog::flatIndex(const QString& name) const
{
	const QString key = upperKey(name);
	for (int index = 0; index < flats.size(); ++index) {
		if (upperKey(flats.at(index)) == key) {
			return index;
		}
	}
	return -1;
}

bool DoomMaterialCatalog::isFlat(const QString& name) const
{
	return flatIndex(name) >= 0;
}

QVector<DoomAnimationRange> doomVanillaAnimations()
{
	// linuxdoom-1.10 p_spec.c animdefs[]: {istexture, endname, startname, 8}.
	struct Row {
		bool texture;
		const char* last;
		const char* first;
	};
	static const Row rows[] = {
		{false, "NUKAGE3", "NUKAGE1"},
		{false, "FWATER4", "FWATER1"},
		{false, "SWATER4", "SWATER1"},
		{false, "LAVA4", "LAVA1"},
		{false, "BLOOD3", "BLOOD1"},
		{false, "RROCK08", "RROCK05"},
		{false, "SLIME04", "SLIME01"},
		{false, "SLIME08", "SLIME05"},
		{false, "SLIME12", "SLIME09"},
		{true, "BLODGR4", "BLODGR1"},
		{true, "SLADRIP3", "SLADRIP1"},
		{true, "BLODRIP4", "BLODRIP1"},
		{true, "FIREWALL", "FIREWALA"},
		{true, "GSTFONT3", "GSTFONT1"},
		{true, "FIRELAVA", "FIRELAV3"},
		{true, "FIREMAG3", "FIREMAG1"},
		{true, "FIREBLU2", "FIREBLU1"},
		{true, "ROCKRED3", "ROCKRED1"},
		{true, "BFALL4", "BFALL1"},
		{true, "SFALL4", "SFALL1"},
		{true, "WFALL4", "WFALL1"},
		{true, "DBRAIN4", "DBRAIN1"},
	};
	QVector<DoomAnimationRange> ranges;
	for (const Row& row : rows) {
		DoomAnimationRange range;
		range.texture = row.texture;
		range.last = QString::fromLatin1(row.last);
		range.first = QString::fromLatin1(row.first);
		range.tics = 8;
		range.source = QStringLiteral("doom-vanilla");
		ranges.push_back(range);
	}
	return ranges;
}

QVector<DoomSwitchPair> doomVanillaSwitches()
{
	// linuxdoom-1.10 p_switch.c alphSwitchList[]: SW1xxx / SW2xxx pairs.
	struct Row {
		const char* name;
		int episode;
	};
	static const Row rows[] = {
		{"BRCOM", 1}, {"BRN1", 1}, {"BRN2", 1}, {"BRNGN", 1}, {"BROWN", 1}, {"COMM", 1}, {"COMP", 1}, {"DIRT", 1},
		{"EXIT", 1}, {"GRAY", 1}, {"GRAY1", 1}, {"METAL", 1}, {"PIPE", 1}, {"SLAD", 1}, {"STARG", 1}, {"STON1", 1},
		{"STON2", 1}, {"STONE", 1}, {"STRTN", 1}, {"BLUE", 2}, {"CMT", 2}, {"GARG", 2}, {"GSTON", 2}, {"HOT", 2},
		{"LION", 2}, {"SATYR", 2}, {"SKIN", 2}, {"VINE", 2}, {"WOOD", 2}, {"PANEL", 3}, {"ROCK", 3}, {"MET2", 3},
		{"WDMET", 3}, {"BRIK", 3}, {"MOD1", 3}, {"ZIM", 3}, {"STON6", 3}, {"TEK", 3}, {"MARB", 3}, {"SKULL", 3},
	};
	QVector<DoomSwitchPair> pairs;
	for (const Row& row : rows) {
		DoomSwitchPair pair;
		pair.off = QStringLiteral("SW1") + QString::fromLatin1(row.name);
		pair.on = QStringLiteral("SW2") + QString::fromLatin1(row.name);
		pair.episode = row.episode;
		pair.source = QStringLiteral("doom-vanilla");
		pairs.push_back(pair);
	}
	return pairs;
}

bool parseBoomAnimatedLump(const QByteArray& bytes, QVector<DoomAnimationRange>* ranges, QString* error)
{
	ranges->clear();
	qsizetype offset = 0;
	while (offset < bytes.size()) {
		const quint8 type = static_cast<quint8>(bytes.at(offset));
		if (type == 0xFF) {
			return true;
		}
		if (offset + 23 > bytes.size()) {
			if (error) {
				*error = Text::tr("ANIMATED ends inside a record; Boom records are 23 bytes ending with type 255.");
			}
			return false;
		}
		DoomAnimationRange range;
		// Bit 0 marks a texture; ZDoom uses bit 1 to allow decals.
		range.texture = type != 0;
		range.decals = (type & 2) != 0;
		range.last = fixedName(bytes, offset + 1, 9);
		range.first = fixedName(bytes, offset + 10, 9);
		range.tics = readI32(bytes, offset + 19);
		range.source = QStringLiteral("boom-animated");
		ranges->push_back(range);
		offset += 23;
	}
	if (error) {
		*error = Text::tr("ANIMATED has no terminating record (type 255).");
	}
	return !ranges->isEmpty();
}

bool parseBoomSwitchesLump(const QByteArray& bytes, QVector<DoomSwitchPair>* switches, QString* error)
{
	switches->clear();
	qsizetype offset = 0;
	while (offset + 20 <= bytes.size()) {
		const int episode = readI16(bytes, offset + 18);
		if (episode == 0) {
			return true;
		}
		DoomSwitchPair pair;
		pair.off = fixedName(bytes, offset, 9);
		pair.on = fixedName(bytes, offset + 9, 9);
		pair.episode = episode;
		pair.source = QStringLiteral("boom-switches");
		switches->push_back(pair);
		offset += 20;
	}
	if (error) {
		*error = Text::tr("SWITCHES has no terminating record (episode 0).");
	}
	return !switches->isEmpty();
}

QByteArray boomAnimatedLump(const QVector<DoomAnimationRange>& ranges)
{
	QByteArray bytes;
	for (const DoomAnimationRange& range : ranges) {
		quint8 type = range.texture ? 1 : 0;
		if (range.texture && range.decals) {
			type |= 2;
		}
		bytes.append(static_cast<char>(type));
		appendName(&bytes, range.last, 9);
		appendName(&bytes, range.first, 9);
		appendI32(&bytes, range.tics);
	}
	QByteArray terminator(23, '\0');
	terminator[0] = static_cast<char>(0xFF);
	bytes.append(terminator);
	return bytes;
}

QByteArray boomSwitchesLump(const QVector<DoomSwitchPair>& switches)
{
	QByteArray bytes;
	for (const DoomSwitchPair& pair : switches) {
		appendName(&bytes, pair.off, 9);
		appendName(&bytes, pair.on, 9);
		appendI16(&bytes, static_cast<qint16>(std::clamp(pair.episode, 1, 3)));
	}
	bytes.append(QByteArray(20, '\0'));
	return bytes;
}

QString swantblsText(const QVector<DoomAnimationRange>& ranges, const QVector<DoomSwitchPair>& switches)
{
	QString text = QStringLiteral("# Boom animation and switch tables (SWANTBLS format).\n");
	text += QStringLiteral("# Compile with VibeStudio or SWANTBLS into ANIMATED and SWITCHES lumps.\n\n");
	const auto section = [&](bool texture) {
		text += texture ? QStringLiteral("[TEXTURES]\n") : QStringLiteral("[FLATS]\n");
		// Boom's DEFSWANI.DAT columns: speed in tics, last frame, first frame.
		text += QStringLiteral("#spd    last        first\n");
		for (const DoomAnimationRange& range : ranges) {
			if (range.texture == texture) {
				text += QStringLiteral("%1 %2 %3%4\n")
							.arg(QString::number(range.tics).leftJustified(7), range.last.leftJustified(11), range.first,
								range.decals ? QStringLiteral(" decals") : QString());
			}
		}
		text += QLatin1Char('\n');
	};
	section(false);
	section(true);
	text += QStringLiteral("[SWITCHES]\n#epi    texture1    texture2\n");
	for (const DoomSwitchPair& pair : switches) {
		text += QStringLiteral("%1 %2 %3\n").arg(QString::number(pair.episode).leftJustified(7), pair.off.leftJustified(11), pair.on);
	}
	return text;
}

bool parseSwantblsText(const QString& text, QVector<DoomAnimationRange>* ranges, QVector<DoomSwitchPair>* switches,
	QVector<MaterialDiagnostic>* diagnostics)
{
	ranges->clear();
	switches->clear();
	enum class Section {
		None,
		Flats,
		Textures,
		Switches,
	} section = Section::None;
	bool ok = true;
	const QStringList lines = text.split(QLatin1Char('\n'));
	for (int index = 0; index < lines.size(); ++index) {
		QString line = lines.at(index);
		const int comment = line.indexOf(QLatin1Char('#'));
		if (comment >= 0) {
			line.truncate(comment);
		}
		line = line.trimmed();
		if (line.isEmpty()) {
			continue;
		}
		const QString upper = line.toUpper();
		if (upper == QStringLiteral("[FLATS]")) {
			section = Section::Flats;
			continue;
		}
		if (upper == QStringLiteral("[TEXTURES]")) {
			section = Section::Textures;
			continue;
		}
		if (upper == QStringLiteral("[SWITCHES]")) {
			section = Section::Switches;
			continue;
		}
		const QStringList words = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
		const auto bad = [&](const QString& message) {
			ok = false;
			if (diagnostics) {
				diagnostics->push_back(diagnostic(MaterialDiagnosticSeverity::Error, QStringLiteral("bad-line"), message, {}, index + 1));
			}
		};
		if (section == Section::Flats || section == Section::Textures) {
			// speed last first, as in Boom's DEFSWANI.DAT. A trailing
			// "decals" marks an MBF range that allows decals; SWANTBLS reads
			// three fields and ignores it.
			int tics = 0;
			if (words.size() < 3 || !isWhole(words.at(0), &tics) || tics <= 0) {
				bad(Text::tr("Expected: speed last first."));
				continue;
			}
			DoomAnimationRange range;
			range.texture = section == Section::Textures;
			range.last = words.at(1).toUpper();
			range.first = words.at(2).toUpper();
			range.tics = tics;
			range.decals = words.size() > 3 && words.at(3).compare(QStringLiteral("decals"), Qt::CaseInsensitive) == 0;
			range.source = QStringLiteral("boom-animated");
			if (range.last.size() > 8 || range.first.size() > 8) {
				bad(Text::tr("Doom lump names have at most 8 characters."));
				continue;
			}
			ranges->push_back(range);
		} else if (section == Section::Switches) {
			int episode = 0;
			if (words.size() < 3 || !isWhole(words.at(0), &episode) || episode < 1 || episode > 3) {
				bad(Text::tr("Expected: episode (1-3) off on."));
				continue;
			}
			DoomSwitchPair pair;
			pair.episode = episode;
			pair.off = words.at(1).toUpper();
			pair.on = words.at(2).toUpper();
			pair.source = QStringLiteral("boom-switches");
			switches->push_back(pair);
		} else {
			bad(Text::tr("Text before the first [FLATS], [TEXTURES] or [SWITCHES] section."));
		}
	}
	return ok;
}

QVector<DoomAnimdefsEntry> parseAnimdefs(const QString& text, QVector<MaterialDiagnostic>* diagnostics)
{
	QVector<DoomAnimdefsEntry> entries;
	const QVector<ScanToken> tokens = scanWords(text, false);
	static const QSet<QString> topLevel {QStringLiteral("flat"), QStringLiteral("texture"), QStringLiteral("warp"), QStringLiteral("warp2"),
		QStringLiteral("switch"), QStringLiteral("cameratexture"), QStringLiteral("canvastexture"), QStringLiteral("animateddoor"),
		QStringLiteral("skyoffset"), QStringLiteral("firetexture")};
	int index = 0;
	const auto at = [&](int i) -> const ScanToken* { return i < tokens.size() ? &tokens.at(i) : nullptr; };
	const auto keyword = [&](int i) { return i < tokens.size() && !tokens.at(i).quoted ? tokens.at(i).text.toLower() : QString(); };
	const auto warn = [&](const ScanToken* token, const QString& message) {
		if (diagnostics) {
			diagnostics->push_back(diagnostic(MaterialDiagnosticSeverity::Warning, QStringLiteral("animdefs"), message, {}, token ? token->line : 0));
		}
	};
	// Reads `tics n` or `rand a b` after a frame; returns false on error.
	const auto timing = [&](int* tics, int* maximum) {
		const QString mode = keyword(index);
		if (mode == QStringLiteral("tics")) {
			const ScanToken* value = at(index + 1);
			double number = value ? value->text.toDouble() : 0.0;
			*tics = static_cast<int>(std::lround(number));
			*maximum = 0;
			index += 2;
			return value != nullptr;
		}
		if (mode == QStringLiteral("rand")) {
			const ScanToken* low = at(index + 1);
			const ScanToken* high = at(index + 2);
			*tics = low ? low->text.toInt() : 0;
			*maximum = high ? high->text.toInt() : 0;
			index += 3;
			return low && high;
		}
		return false;
	};
	while (index < tokens.size()) {
		const ScanToken& token = tokens.at(index);
		const QString key = keyword(index);
		if (key == QStringLiteral("flat") || key == QStringLiteral("texture")) {
			DoomAnimdefsEntry entry;
			entry.kind = key;
			entry.texture = key == QStringLiteral("texture");
			entry.line = token.line;
			++index;
			if (keyword(index) == QStringLiteral("optional")) {
				entry.optional = true;
				++index;
			}
			const ScanToken* name = at(index);
			if (!name) {
				warn(&token, Text::tr("%1 needs a name.").arg(token.text));
				break;
			}
			entry.base = name->text;
			++index;
			int end = name->end;
			while (index < tokens.size()) {
				const QString body = keyword(index);
				if (topLevel.contains(body)) {
					break;
				}
				const ScanToken* bodyToken = at(index);
				if (body == QStringLiteral("pic") || body == QStringLiteral("range")) {
					const ScanToken* frame = at(index + 1);
					if (!frame) {
						warn(bodyToken, Text::tr("%1 needs a frame.").arg(bodyToken->text));
						index = tokens.size();
						break;
					}
					index += 2;
					int tics = 8;
					int maximum = 0;
					if (!timing(&tics, &maximum)) {
						warn(bodyToken, Text::tr("Expected tics or rand after %1 %2.").arg(bodyToken->text, frame->text));
					}
					if (body == QStringLiteral("range")) {
						if (!entry.frames.isEmpty() || entry.range) {
							warn(bodyToken, Text::tr("A block has one range and no pic entries with it."));
						}
						entry.range = true;
						int number = 0;
						entry.rangeLast = isWhole(frame->text, &number) && !frame->quoted ? QString() : frame->text;
						if (entry.rangeLast.isEmpty()) {
							entry.rangeLast = QStringLiteral("#%1").arg(number);
						}
						entry.rangeTics = tics;
						entry.rangeMaximumTics = maximum;
					} else {
						DoomAnimdefsFrame pic;
						int number = 0;
						if (!frame->quoted && isWhole(frame->text, &number)) {
							pic.pic = number;
						} else {
							pic.name = frame->text;
						}
						pic.tics = tics;
						pic.maximumTics = maximum;
						entry.frames.push_back(pic);
					}
					end = tokens.at(std::min<int>(index, tokens.size()) - 1).end;
				} else if (body == QStringLiteral("oscillate")) {
					entry.oscillate = true;
					end = bodyToken->end;
					++index;
				} else if (body == QStringLiteral("allowdecals") || body == QStringLiteral("random") || body == QStringLiteral("notrim")) {
					entry.allowDecals |= body == QStringLiteral("allowdecals");
					end = bodyToken->end;
					++index;
				} else {
					warn(bodyToken, Text::tr("Unknown ANIMDEFS word '%1'.").arg(bodyToken->text));
					++index;
				}
			}
			entry.span.start = token.start;
			entry.span.end = end;
			entry.span.line = token.line;
			entries.push_back(entry);
			continue;
		}
		if (key == QStringLiteral("warp") || key == QStringLiteral("warp2")) {
			DoomAnimdefsEntry entry;
			entry.kind = key;
			entry.line = token.line;
			++index;
			const QString kind = keyword(index);
			if (kind != QStringLiteral("flat") && kind != QStringLiteral("texture")) {
				warn(&token, Text::tr("%1 needs flat or texture before the name.").arg(token.text));
			} else {
				entry.texture = kind == QStringLiteral("texture");
				++index;
			}
			const ScanToken* name = at(index);
			if (!name) {
				break;
			}
			entry.base = name->text;
			++index;
			int end = name->end;
			bool ok = false;
			const double speed = index < tokens.size() ? tokens.at(index).text.toDouble(&ok) : 0.0;
			if (ok) {
				entry.warpSpeed = speed;
				end = tokens.at(index).end;
				++index;
			}
			if (keyword(index) == QStringLiteral("allowdecals")) {
				entry.allowDecals = true;
				end = tokens.at(index).end;
				++index;
			}
			entry.span.start = token.start;
			entry.span.end = end;
			entry.span.line = token.line;
			entries.push_back(entry);
			continue;
		}
		if (key == QStringLiteral("switch")) {
			DoomAnimdefsEntry entry;
			entry.kind = key;
			entry.texture = true;
			entry.line = token.line;
			++index;
			static const QSet<QString> games {QStringLiteral("doom"), QStringLiteral("heretic"), QStringLiteral("hexen"),
				QStringLiteral("strife"), QStringLiteral("any")};
			if (games.contains(keyword(index))) {
				++index;
				int number = 0;
				if (index < tokens.size() && isWhole(tokens.at(index).text, &number)) {
					++index;
				}
			}
			const ScanToken* name = at(index);
			if (!name) {
				break;
			}
			entry.base = name->text;
			++index;
			int end = name->end;
			bool inOn = false;
			while (index < tokens.size()) {
				const QString body = keyword(index);
				if (topLevel.contains(body)) {
					break;
				}
				if (body == QStringLiteral("on")) {
					inOn = true;
				} else if (body == QStringLiteral("off")) {
					inOn = false;
				} else if (body == QStringLiteral("pic") && index + 1 < tokens.size()) {
					if (inOn && entry.switchOn.isEmpty()) {
						entry.switchOn = tokens.at(index + 1).text;
					}
					++index;
				} else if ((body == QStringLiteral("sound") || body == QStringLiteral("tics")) && index + 1 < tokens.size()) {
					++index;
				} else if (body == QStringLiteral("rand") && index + 2 < tokens.size()) {
					index += 2;
				}
				end = tokens.at(index).end;
				++index;
			}
			entry.span.start = token.start;
			entry.span.end = end;
			entry.span.line = token.line;
			entries.push_back(entry);
			continue;
		}
		if (topLevel.contains(key)) {
			// cameratexture, canvastexture, animateddoor, skyoffset,
			// firetexture: kept as recognised and skipped up to the next
			// top-level keyword.
			DoomAnimdefsEntry entry;
			entry.kind = key;
			entry.line = token.line;
			++index;
			if (const ScanToken* name = at(index)) {
				entry.base = name->text;
			}
			while (index < tokens.size() && !topLevel.contains(keyword(index))) {
				++index;
			}
			entry.span.start = token.start;
			entry.span.end = tokens.at(index - 1).end;
			entry.span.line = token.line;
			entries.push_back(entry);
			continue;
		}
		warn(&token, Text::tr("Unknown ANIMDEFS keyword '%1'.").arg(token.text));
		++index;
	}
	return entries;
}

QString animdefsEntryText(const DoomAnimdefsEntry& entry)
{
	const auto timing = [](int tics, int maximum) {
		return maximum > tics ? QStringLiteral("rand %1 %2").arg(tics).arg(maximum) : QStringLiteral("tics %1").arg(tics);
	};
	const auto quoted = [](const QString& name) {
		return name.contains(QLatin1Char(' ')) || name.contains(QLatin1Char('/')) ? QStringLiteral("\"%1\"").arg(name) : name;
	};
	if (entry.kind == QStringLiteral("warp") || entry.kind == QStringLiteral("warp2")) {
		QString text = QStringLiteral("%1 %2 %3").arg(entry.kind, entry.texture ? QStringLiteral("texture") : QStringLiteral("flat"), quoted(entry.base));
		if (std::abs(entry.warpSpeed - 1.0) > 1e-9) {
			text += QLatin1Char(' ') + QString::number(entry.warpSpeed);
		}
		if (entry.allowDecals) {
			text += QStringLiteral(" allowdecals");
		}
		return text + QLatin1Char('\n');
	}
	if (entry.kind != QStringLiteral("flat") && entry.kind != QStringLiteral("texture")) {
		return QString();
	}
	QString text = QStringLiteral("%1 %2%3\n").arg(entry.kind, entry.optional ? QStringLiteral("optional ") : QString(), quoted(entry.base));
	if (entry.range) {
		text += QStringLiteral("\trange %1 %2\n").arg(quoted(entry.rangeLast), timing(entry.rangeTics, entry.rangeMaximumTics));
	}
	for (const DoomAnimdefsFrame& frame : entry.frames) {
		const QString pic = frame.name.isEmpty() ? QString::number(frame.pic) : quoted(frame.name);
		text += QStringLiteral("\tpic %1 %2\n").arg(pic, timing(frame.tics, frame.maximumTics));
	}
	if (entry.oscillate) {
		text += QStringLiteral("\toscillate\n");
	}
	if (entry.allowDecals) {
		text += QStringLiteral("\tallowdecals\n");
	}
	return text;
}

DoomMaterialCatalog readDoomMaterialCatalog(const PackageArchiveReader& reader, const PackageReadControl& control)
{
	DoomMaterialCatalog catalog;
	const QVector<PackageEntry> entries = reader.entries();
	QString error;
	QByteArray bytes;
	// Namespaces in directory order: flats and TX_ textures in WADs, flats/
	// and textures/ folders in ZDoom PK3s.
	for (const PackageEntry& entry : entries) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		const QString folder = packageVirtualPathParent(entry.virtualPath).toLower();
		if (entry.typeHint == QStringLiteral("wad-flat") || folder == QStringLiteral("flats")) {
			const QString name = lumpKey(entry.virtualPath);
			if (!catalog.flats.contains(name)) {
				catalog.flats << name;
			}
		} else if (folder == QStringLiteral("textures") || entry.typeHint == QStringLiteral("wad-texture")) {
			const QString name = lumpKey(entry.virtualPath);
			if (!catalog.directTextures.contains(name)) {
				catalog.directTextures << name;
			}
		}
	}
	if (control.isCancelled && control.isCancelled()) {
		return catalog;
	}
	const int pnames = lastLump(entries, QStringLiteral("PNAMES"));
	if (pnames >= 0 && readLump(reader, pnames, &bytes, &error)) {
		const qint32 count = bytes.size() >= 4 ? readI32(bytes, 0) : -1;
		if (count < 0 || count > 65536 || 4LL + 8LL * count > bytes.size()) {
			catalog.warnings << Text::tr("PNAMES is malformed.");
		} else {
			for (int index = 0; index < count; ++index) {
				catalog.patchNames << fixedName(bytes, 4 + 8 * index, 8).toUpper();
			}
		}
	}
	int order = 0;
	for (const QString& lump : {QStringLiteral("TEXTURE1"), QStringLiteral("TEXTURE2")}) {
		const int index = lastLump(entries, lump);
		if (index < 0 || !readLump(reader, index, &bytes, &error)) {
			continue;
		}
		const qint32 count = bytes.size() >= 4 ? readI32(bytes, 0) : -1;
		if (count < 0 || count > 65536 || 4LL + 4LL * count > bytes.size()) {
			catalog.warnings << Text::tr("%1 is malformed.").arg(lump);
			continue;
		}
		// Binary layout: maptexture_t and mappatch_t (r_data.c, R_InitTextures).
		for (int texture = 0; texture < count; ++texture) {
			const qint64 offset = readI32(bytes, 4 + 4 * texture);
			if (offset < 4 || offset + 22 > bytes.size()) {
				catalog.warnings << Text::tr("%1 has a bad texture offset.").arg(lump);
				break;
			}
			DoomCompositeTexture composite;
			composite.name = fixedName(bytes, offset, 8).toUpper();
			composite.size = QSize(readI16(bytes, offset + 12), readI16(bytes, offset + 14));
			composite.lump = lump;
			composite.order = order++;
			const int patchCount = readI16(bytes, offset + 20);
			if (patchCount < 0 || offset + 22 + 10LL * patchCount > bytes.size()) {
				catalog.warnings << Text::tr("Texture %1 has truncated patch records.").arg(composite.name);
				continue;
			}
			for (int patch = 0; patch < patchCount; ++patch) {
				const qint64 at = offset + 22 + 10LL * patch;
				const int patchIndex = readI16(bytes, at + 4);
				MaterialPatchPlacement placement;
				placement.x = readI16(bytes, at);
				placement.y = readI16(bytes, at + 2);
				placement.patch = patchIndex >= 0 && patchIndex < catalog.patchNames.size() ? catalog.patchNames.at(patchIndex)
																						   : QStringLiteral("#%1").arg(patchIndex);
				composite.patches.push_back(placement);
			}
			catalog.textures.push_back(composite);
		}
	}
	// Boom replaces the built-in tables wholesale when a lump is present.
	const int animated = lastLump(entries, QStringLiteral("ANIMATED"));
	if (animated >= 0 && readLump(reader, animated, &bytes, &error)) {
		QString lumpError;
		if (parseBoomAnimatedLump(bytes, &catalog.ranges, &lumpError)) {
			catalog.animationSource = QStringLiteral("boom-animated");
		} else {
			catalog.warnings << lumpError;
		}
	}
	if (catalog.animationSource.isEmpty()) {
		catalog.ranges = doomVanillaAnimations();
		catalog.animationSource = QStringLiteral("doom-vanilla");
	}
	const int switches = lastLump(entries, QStringLiteral("SWITCHES"));
	if (switches >= 0 && readLump(reader, switches, &bytes, &error)) {
		QString lumpError;
		if (parseBoomSwitchesLump(bytes, &catalog.switches, &lumpError)) {
			catalog.switchSource = QStringLiteral("boom-switches");
		} else {
			catalog.warnings << lumpError;
		}
	}
	if (catalog.switchSource.isEmpty()) {
		catalog.switches = doomVanillaSwitches();
		catalog.switchSource = QStringLiteral("doom-vanilla");
	}
	// Every ANIMDEFS adds to the definitions; the last one read wins ties.
	for (int index = 0; index < entries.size(); ++index) {
		const PackageEntry& entry = entries.at(index);
		if (entry.kind != PackageEntryKind::File || lumpKey(entry.virtualPath) != QStringLiteral("ANIMDEFS")) {
			continue;
		}
		if (!reader.readEntryAt(index, &bytes, &error, kLumpLimit)) {
			continue;
		}
		const QString text = QString::fromUtf8(bytes);
		catalog.animdefs += parseAnimdefs(text, &catalog.diagnostics);
		catalog.hasAnimdefs = true;
		catalog.animdefsPath = entry.virtualPath;
		catalog.animdefsText = text;
	}
	return catalog;
}

namespace {

// The names of a range in namespace order, or empty with `error` set.
QStringList rangeNames(const DoomMaterialCatalog& catalog, bool texture, const QString& first, const QString& last, int* firstIndex,
	QString* error)
{
	if (texture) {
		const int start = catalog.textureIndex(first);
		const int end = catalog.textureIndex(last);
		if (start < 0 || end < 0) {
			*error = Text::tr("%1 is not a known texture.").arg(start < 0 ? first : last);
			return {};
		}
		*firstIndex = start;
		QStringList names;
		const int step = end >= start ? 1 : -1;
		for (int index = start; index != end + step; index += step) {
			names << catalog.textures.at(index).name;
		}
		return names;
	}
	const int start = catalog.flatIndex(first);
	const int end = catalog.flatIndex(last);
	if (start < 0 || end < 0) {
		*error = Text::tr("%1 is not a known flat.").arg(start < 0 ? first : last);
		return {};
	}
	*firstIndex = start;
	QStringList names;
	const int step = end >= start ? 1 : -1;
	for (int index = start; index != end + step; index += step) {
		names << catalog.flats.at(index);
	}
	return names;
}

QString animdefsFrameName(const DoomMaterialCatalog& catalog, const DoomAnimdefsEntry& entry, const DoomAnimdefsFrame& frame)
{
	if (!frame.name.isEmpty()) {
		return frame.name.toUpper();
	}
	// pic N counts from the base texture: pic 1 is the base itself.
	const int base = entry.texture ? catalog.textureIndex(entry.base) : catalog.flatIndex(entry.base);
	const int index = base + frame.pic - 1;
	if (base < 0) {
		return QString();
	}
	if (entry.texture) {
		return index >= 0 && index < catalog.textures.size() ? catalog.textures.at(index).name : QString();
	}
	return index >= 0 && index < catalog.flats.size() ? catalog.flats.at(index) : QString();
}

} // namespace

MaterialDefinition doomMaterialDefinition(const DoomMaterialCatalog& catalog, const QString& name, bool flat)
{
	MaterialDefinition definition;
	definition.name = upperKey(name);
	definition.engine = MaterialEngine::Doom;
	definition.kind = flat ? QStringLiteral("doom-flat") : QStringLiteral("doom-wall");
	definition.classic.kind = flat ? QStringLiteral("flat") : QStringLiteral("wall");
	const QString key = definition.name;
	if (flat) {
		definition.classic.size = QSize(64, 64);
	} else {
		const int index = catalog.textureIndex(key);
		if (index >= 0) {
			const DoomCompositeTexture& composite = catalog.textures.at(index);
			definition.classic.size = composite.size;
			definition.classic.patches = composite.patches;
			definition.classic.definitionLump = composite.lump;
		}
	}
	// Animation: Boom or vanilla ranges first, then ANIMDEFS, which replaces
	// earlier definitions of the same texture.
	for (const DoomAnimationRange& range : catalog.ranges) {
		if (range.texture == flat) {
			continue;
		}
		QString error;
		int firstIndex = -1;
		const QStringList names = rangeNames(catalog, range.texture, range.first, range.last, &firstIndex, &error);
		if (names.isEmpty()) {
			continue;
		}
		const qsizetype position = names.indexOf(key);
		if (position < 0) {
			continue;
		}
		if (names.size() < 2) {
			definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Error, QStringLiteral("bad-cycle"),
				Text::tr("Bad animation cycle from %1 to %2: Doom stops with an error.").arg(range.first, range.last), definition.name));
			continue;
		}
		const int count = static_cast<int>(names.size());
		// P_UpdateSpecials: member i shows (leveltime / speed + i) % numpics,
		// where i is the absolute texture or flat number.
		const int rotation = range.source == QStringLiteral("doom-vanilla") || range.source == QStringLiteral("boom-animated")
			? (firstIndex + static_cast<int>(position)) % count
			: static_cast<int>(position);
		definition.classic.frames = rotatedFrames(names, rotation, range.tics / kDoomTicsPerSecond);
		definition.classic.animationSource = range.source;
	}
	for (const DoomAnimdefsEntry& entry : catalog.animdefs) {
		if (entry.texture == flat && (entry.kind == QStringLiteral("flat") || entry.kind == QStringLiteral("texture"))) {
			continue;
		}
		if (entry.kind == QStringLiteral("warp") || entry.kind == QStringLiteral("warp2")) {
			if (entry.texture != flat && upperKey(entry.base) == key) {
				definition.classic.warp = entry.kind == QStringLiteral("warp") ? MaterialWarpStyle::DoomWarp : MaterialWarpStyle::DoomWarp2;
				definition.classic.warpSpeed = entry.warpSpeed;
			}
			continue;
		}
		if (entry.kind == QStringLiteral("switch")) {
			if (upperKey(entry.base) == key && !entry.switchOn.isEmpty()) {
				definition.classic.switchPartner = upperKey(entry.switchOn);
				definition.classic.switchSource = QStringLiteral("animdefs");
			}
			continue;
		}
		if (entry.kind != QStringLiteral("flat") && entry.kind != QStringLiteral("texture")) {
			continue;
		}
		if (entry.range) {
			QString last = entry.rangeLast;
			if (last.startsWith(QLatin1Char('#'))) {
				DoomAnimdefsFrame frame;
				frame.pic = last.mid(1).toInt();
				last = animdefsFrameName(catalog, entry, frame);
			}
			QString error;
			int firstIndex = -1;
			const QStringList names = rangeNames(catalog, entry.texture, entry.base, last, &firstIndex, &error);
			const qsizetype position = names.indexOf(key);
			if (position < 0) {
				continue;
			}
			QVector<MaterialFrame> frames = rotatedFrames(names, static_cast<int>(position), entry.rangeTics / kDoomTicsPerSecond);
			if (entry.rangeMaximumTics > entry.rangeTics) {
				for (MaterialFrame& frame : frames) {
					frame.maximumDuration = entry.rangeMaximumTics / kDoomTicsPerSecond;
				}
			}
			if (entry.oscillate && frames.size() > 2) {
				for (int index = static_cast<int>(frames.size()) - 2; index > 0; --index) {
					frames.push_back(frames.at(index));
				}
			}
			definition.classic.frames = frames;
			definition.classic.animationSource = QStringLiteral("animdefs");
			definition.classic.oscillate = entry.oscillate;
			continue;
		}
		// A pic list animates only its base texture.
		if (upperKey(entry.base) != key || entry.frames.isEmpty()) {
			continue;
		}
		QVector<MaterialFrame> frames;
		for (const DoomAnimdefsFrame& pic : entry.frames) {
			MaterialFrame frame;
			frame.name = animdefsFrameName(catalog, entry, pic);
			if (frame.name.isEmpty()) {
				definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Warning, QStringLiteral("animdefs"),
					Text::tr("ANIMDEFS frame pic %1 of %2 is outside the namespace.").arg(pic.pic).arg(entry.base), definition.name, entry.line));
				continue;
			}
			frame.duration = pic.tics / kDoomTicsPerSecond;
			frame.maximumDuration = pic.maximumTics > pic.tics ? pic.maximumTics / kDoomTicsPerSecond : 0.0;
			frames.push_back(frame);
		}
		if (entry.oscillate && frames.size() > 2) {
			for (int index = static_cast<int>(frames.size()) - 2; index > 0; --index) {
				frames.push_back(frames.at(index));
			}
		}
		definition.classic.frames = frames;
		definition.classic.animationSource = QStringLiteral("animdefs");
		definition.classic.oscillate = entry.oscillate;
	}
	if (!flat && definition.classic.switchPartner.isEmpty()) {
		for (const DoomSwitchPair& pair : catalog.switches) {
			if (upperKey(pair.off) == key) {
				definition.classic.switchPartner = upperKey(pair.on);
			} else if (upperKey(pair.on) == key) {
				definition.classic.switchPartner = upperKey(pair.off);
			} else {
				continue;
			}
			definition.classic.switchSource = pair.source;
			break;
		}
	}
	return definition;
}

QVector<MaterialDefinition> doomMaterialDefinitions(const DoomMaterialCatalog& catalog)
{
	QVector<MaterialDefinition> definitions;
	QSet<QString> seen;
	for (const DoomCompositeTexture& texture : catalog.textures) {
		if (!seen.contains(texture.name)) {
			seen.insert(texture.name);
			definitions.push_back(doomMaterialDefinition(catalog, texture.name, false));
		}
	}
	for (const QString& texture : catalog.directTextures) {
		if (!seen.contains(texture)) {
			seen.insert(texture);
			definitions.push_back(doomMaterialDefinition(catalog, texture, false));
		}
	}
	QSet<QString> flatsSeen;
	for (const QString& flat : catalog.flats) {
		if (!flatsSeen.contains(flat)) {
			flatsSeen.insert(flat);
			definitions.push_back(doomMaterialDefinition(catalog, flat, true));
		}
	}
	return definitions;
}

// ---------------------------------------------------------------------------
// Quake
// ---------------------------------------------------------------------------

QuakeTextureName parseQuakeTextureName(const QString& name)
{
	QuakeTextureName parsed;
	parsed.name = name;
	parsed.base = name;
	const QString lower = name.toLower();
	if (name.size() >= 2 && name.at(0) == QLatin1Char('+')) {
		const QChar frame = name.at(1).toLower();
		if (frame.isDigit()) {
			parsed.frame = frame.digitValue();
			parsed.base = name.mid(2);
		} else if (frame >= QLatin1Char('a') && frame <= QLatin1Char('j')) {
			parsed.frame = frame.unicode() - 'a';
			parsed.alternate = true;
			parsed.base = name.mid(2);
		}
	}
	const QString base = parsed.base.toLower();
	parsed.liquid = base.startsWith(QLatin1Char('*')) || base.startsWith(QLatin1Char('!'));
	parsed.sky = base.startsWith(QStringLiteral("sky"));
	parsed.masked = base.startsWith(QLatin1Char('{'));
	parsed.randomTiling = lower.size() >= 2 && lower.at(0) == QLatin1Char('-') && lower.at(1).isDigit();
	static const QStringList specials {QStringLiteral("clip"), QStringLiteral("trigger"), QStringLiteral("skip"),
		QStringLiteral("hint"), QStringLiteral("hintskip"), QStringLiteral("origin"), QStringLiteral("aaatrigger"),
		QStringLiteral("null"), QStringLiteral("nodraw")};
	parsed.special = specials.contains(base);
	return parsed;
}

QStringList quakeCompanionSuffixes()
{
	return {QStringLiteral("_glow"), QStringLiteral("_luma"), QStringLiteral("_norm"), QStringLiteral("_gloss"), QStringLiteral("_bump"),
		QStringLiteral("_pants"), QStringLiteral("_shirt")};
}

MaterialDefinition quakeMaterialDefinition(const QString& name, const QStringList& siblings, const QSize& size, MaterialEngine engine)
{
	MaterialDefinition definition;
	definition.name = name;
	definition.engine = engine;
	definition.kind = QStringLiteral("quake-miptex");
	definition.classic.kind = QStringLiteral("miptex");
	definition.classic.size = size;
	const QuakeTextureName parsed = parseQuakeTextureName(name);
	definition.classic.masked = parsed.masked;
	definition.classic.fullbrights = true;
	if (parsed.liquid) {
		definition.classic.warp = MaterialWarpStyle::QuakeTurbulent;
	} else if (parsed.sky) {
		definition.classic.warp = MaterialWarpStyle::QuakeSky;
		if (size.isValid() && size != QSize(256, 128)) {
			definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Warning, QStringLiteral("sky-size"),
				Text::tr("Classic Quake skies are 256 x 128: two 128-wide layers side by side."), name));
		}
	}
	if (parsed.special) {
		definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Info, QStringLiteral("compiler-texture"),
			Text::tr("The compiler treats '%1' specially; it is not drawn in game.").arg(name), name));
	}
	if (parsed.randomTiling) {
		definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Info, QStringLiteral("random-tiling"),
			Text::tr("Half-Life picks one of the -0..-9 variants per surface when the map loads."), name));
	}
	if (parsed.frame < 0) {
		return definition;
	}
	// Mod_LoadTextures: frames 0-9 and alternates A-J share the base name.
	QVector<QString> frames(10);
	QVector<QString> alternates(10);
	int maximum = 0;
	int alternateMaximum = 0;
	const auto place = [&](const QString& candidate) {
		const QuakeTextureName other = parseQuakeTextureName(candidate);
		if (other.frame < 0 || other.base.compare(parsed.base, Qt::CaseInsensitive) != 0) {
			return;
		}
		if (other.alternate) {
			alternates[other.frame] = candidate;
			alternateMaximum = std::max(alternateMaximum, other.frame + 1);
		} else {
			frames[other.frame] = candidate;
			maximum = std::max(maximum, other.frame + 1);
		}
	};
	place(name);
	for (const QString& sibling : siblings) {
		place(sibling);
	}
	for (int index = 0; index < maximum; ++index) {
		if (frames.at(index).isEmpty()) {
			definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Error, QStringLiteral("missing-frame"),
				Text::tr("Missing frame %1 of %2: Quake stops with an error.").arg(index).arg(parsed.base), name));
			return definition;
		}
		MaterialFrame frame;
		frame.name = frames.at(index);
		frame.duration = kQuakeAnimationFrameSeconds;
		definition.classic.frames.push_back(frame);
	}
	for (int index = 0; index < alternateMaximum; ++index) {
		if (alternates.at(index).isEmpty()) {
			definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Error, QStringLiteral("missing-frame"),
				Text::tr("Missing alternate frame %1 of %2: Quake stops with an error.").arg(QChar('A' + index)).arg(parsed.base), name));
			definition.classic.alternateFrames.clear();
			break;
		}
		MaterialFrame frame;
		frame.name = alternates.at(index);
		frame.duration = kQuakeAnimationFrameSeconds;
		definition.classic.alternateFrames.push_back(frame);
	}
	if (!definition.classic.frames.isEmpty() || !definition.classic.alternateFrames.isEmpty()) {
		definition.classic.animationSource = QStringLiteral("quake-name");
	}
	if (definition.classic.frames.isEmpty() && !definition.classic.alternateFrames.isEmpty()) {
		// A sequence of alternates alone plays as the main sequence.
		definition.classic.frames = definition.classic.alternateFrames;
	}
	return definition;
}

// ---------------------------------------------------------------------------
// Quake II
// ---------------------------------------------------------------------------

bool readQuake2WalInfo(const QByteArray& bytes, Quake2WalInfo* info, QString* error)
{
	// miptex_t in qfiles.h: name[32], width, height, offsets[4],
	// animname[32], flags, contents, value.
	if (bytes.size() < 100) {
		if (error) {
			*error = Text::tr("A WAL header is 100 bytes; this file is shorter.");
		}
		return false;
	}
	info->name = fixedName(bytes, 0, 32);
	info->size = QSize(static_cast<int>(readU32(bytes, 32)), static_cast<int>(readU32(bytes, 36)));
	info->nextFrame = fixedName(bytes, 56, 32);
	info->surfaceFlags = readU32(bytes, 88);
	info->contentFlags = readU32(bytes, 92);
	info->value = readI32(bytes, 96);
	return true;
}

bool rewriteQuake2WalHeader(QByteArray* bytes, const Quake2WalInfo& info, QString* error)
{
	if (bytes->size() < 100) {
		if (error) {
			*error = Text::tr("A WAL header is 100 bytes; this file is shorter.");
		}
		return false;
	}
	if (info.name.toLatin1().size() > 31 || info.nextFrame.toLatin1().size() > 31) {
		if (error) {
			*error = Text::tr("WAL names have at most 31 characters.");
		}
		return false;
	}
	writeFixedName(bytes, 0, 32, info.name);
	writeFixedName(bytes, 56, 32, info.nextFrame);
	qToLittleEndian(info.surfaceFlags, bytes->data() + 88);
	qToLittleEndian(info.contentFlags, bytes->data() + 92);
	qToLittleEndian(info.value, bytes->data() + 96);
	return true;
}

QString quake2WalPath(const QString& textureName)
{
	QString name = textureName.trimmed();
	name.replace(QLatin1Char('\\'), QLatin1Char('/'));
	if (name.endsWith(QStringLiteral(".wal"), Qt::CaseInsensitive)) {
		name.chop(4);
	}
	if (name.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
		name = name.mid(9);
	}
	return QStringLiteral("textures/%1.wal").arg(name);
}

MaterialDefinition quake2MaterialDefinition(const QString& name, const Quake2WalInfo& info,
	const std::function<bool(const QString& name, Quake2WalInfo* info)>& lookup)
{
	MaterialDefinition definition;
	definition.name = name;
	definition.engine = MaterialEngine::Quake2;
	definition.kind = QStringLiteral("quake2-wal");
	definition.classic.kind = QStringLiteral("wal");
	definition.classic.size = info.size;
	definition.classic.surfaceFlags = info.surfaceFlags;
	definition.classic.contentFlags = info.contentFlags;
	definition.classic.surfaceValue = info.value;
	definition.classic.nextFrame = info.nextFrame;
	definition.classic.masked = (info.surfaceFlags & (1u << 25)) != 0;
	if ((info.surfaceFlags & kQuake2SurfWarp) != 0) {
		definition.classic.warp = MaterialWarpStyle::QuakeTurbulent;
	}
	if ((info.surfaceFlags & kQuake2SurfNoDraw) != 0) {
		definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Info, QStringLiteral("nodraw"),
			Text::tr("Surfaces with the nodraw flag are never drawn."), name));
	}
	if (info.nextFrame.isEmpty()) {
		return definition;
	}
	// R_TextureAnimation steps `frame % numframes` links from this texture.
	QVector<MaterialFrame> frames;
	MaterialFrame first;
	first.name = name;
	first.duration = kQuake2AnimationFrameSeconds;
	frames.push_back(first);
	QSet<QString> seen {materialLookupKey(name)};
	QString next = info.nextFrame;
	while (!next.isEmpty() && frames.size() < 64) {
		const QString key = materialLookupKey(next);
		if (seen.contains(key)) {
			break;
		}
		Quake2WalInfo nextInfo;
		if (!lookup || !lookup(next, &nextInfo)) {
			definition.diagnostics.push_back(diagnostic(MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-frame"),
				Text::tr("Animation frame %1 is missing; the chain stops there.").arg(next), name));
			break;
		}
		seen.insert(key);
		MaterialFrame frame;
		frame.name = next;
		frame.duration = kQuake2AnimationFrameSeconds;
		frames.push_back(frame);
		next = nextInfo.nextFrame;
	}
	if (frames.size() > 1) {
		definition.classic.frames = frames;
		definition.classic.animationSource = QStringLiteral("quake2-wal");
	}
	return definition;
}

QString quake2WalInfoText(const Quake2WalInfo& info)
{
	// ericw-tools' .wal_json: every field optional, flags and contents as
	// numbers so any tool version reads them.
	QJsonObject object;
	if (info.size.isValid()) {
		object.insert(QStringLiteral("width"), info.size.width());
		object.insert(QStringLiteral("height"), info.size.height());
	}
	object.insert(QStringLiteral("flags"), static_cast<qint64>(info.surfaceFlags));
	object.insert(QStringLiteral("contents"), static_cast<qint64>(info.contentFlags));
	object.insert(QStringLiteral("value"), info.value);
	if (!info.nextFrame.isEmpty()) {
		object.insert(QStringLiteral("animation"), info.nextFrame);
	}
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

namespace {

bool flagValue(const QJsonValue& value, const QVector<MaterialFlagDescriptor>& descriptors, const QString& prefix, quint32* out, QString* error)
{
	if (value.isUndefined() || value.isNull()) {
		return true;
	}
	if (value.isDouble()) {
		const double number = value.toDouble();
		if (number < 0 || number > 4294967295.0 || number != std::floor(number)) {
			*error = Text::tr("Flag numbers must be whole and unsigned.");
			return false;
		}
		*out = static_cast<quint32>(number);
		return true;
	}
	const QJsonArray items = value.isArray() ? value.toArray() : QJsonArray {value};
	quint32 flags = 0;
	for (const QJsonValue& item : items) {
		if (item.isDouble()) {
			flags |= static_cast<quint32>(item.toDouble());
			continue;
		}
		QString name = item.toString().trimmed().toLower();
		if (name.startsWith(prefix)) {
			name = name.mid(prefix.size());
		}
		bool found = false;
		for (const MaterialFlagDescriptor& descriptor : descriptors) {
			if (descriptor.id == name) {
				flags |= descriptor.bit;
				found = true;
				break;
			}
		}
		if (!found) {
			*error = Text::tr("Unknown flag name '%1'.").arg(item.toString());
			return false;
		}
	}
	*out = flags;
	return true;
}

} // namespace

bool parseQuake2WalInfoText(const QString& text, Quake2WalInfo* info, QString* error)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		if (error) {
			*error = Text::tr("The WAL metadata is not a JSON object: %1").arg(parseError.errorString());
		}
		return false;
	}
	const QJsonObject object = document.object();
	static const QSet<QString> known {QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("flags"), QStringLiteral("contents"),
		QStringLiteral("value"), QStringLiteral("animation"), QStringLiteral("color")};
	for (const QString& key : object.keys()) {
		if (!known.contains(key)) {
			if (error) {
				*error = Text::tr("Unknown WAL metadata field '%1'.").arg(key);
			}
			return false;
		}
	}
	QString flagError;
	quint32 flags = info->surfaceFlags;
	quint32 contents = info->contentFlags;
	if (!flagValue(object.value(QStringLiteral("flags")), quake2SurfaceFlagDescriptors(), QStringLiteral("surf_"), &flags, &flagError)
		|| !flagValue(object.value(QStringLiteral("contents")), quake2ContentFlagDescriptors(), QStringLiteral("contents_"), &contents, &flagError)) {
		if (error) {
			*error = flagError;
		}
		return false;
	}
	info->surfaceFlags = flags;
	info->contentFlags = contents;
	if (object.contains(QStringLiteral("value"))) {
		info->value = object.value(QStringLiteral("value")).toInt();
	}
	if (object.contains(QStringLiteral("animation"))) {
		info->nextFrame = object.value(QStringLiteral("animation")).toString().trimmed();
	}
	if (object.contains(QStringLiteral("width")) || object.contains(QStringLiteral("height"))) {
		info->size = QSize(object.value(QStringLiteral("width")).toInt(info->size.width()),
			object.value(QStringLiteral("height")).toInt(info->size.height()));
	}
	return true;
}

} // namespace vibestudio
