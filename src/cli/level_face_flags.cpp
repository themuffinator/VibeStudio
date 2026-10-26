// map face-flags: the content and surface flags and value of Quake II and
// Quake III brush faces, read or changed through the same core service as the
// Flags section of the Levels Surfaces tab. Workflow reference: NetRadiant
// Custom's Surface Inspector flag boxes (radiant/surfacedialog.cpp at
// 449778b3, GPL-2.0-or-later) and TrenchBroom's Face Inspector flags
// (FaceAttribsEditor at 4c756be1, GPL-3.0-or-later). Original implementation;
// see docs/CREDITS.md.

#include "cli/level_face_flags.h"
#include "core/level_face_flags.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <limits>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelFaceFlagsCli)
};

QString gameId(LevelFaceFlagGame game)
{
	switch (game) {
	case LevelFaceFlagGame::Quake2:
		return QStringLiteral("quake2");
	case LevelFaceFlagGame::Quake3:
		return QStringLiteral("quake3");
	case LevelFaceFlagGame::None:
		break;
	}
	return QStringLiteral("none");
}

QJsonArray namesJson(LevelFaceFlagGame game, LevelFaceFlagWord word)
{
	QJsonArray names;
	for (const MaterialFlagDescriptor& flag : levelFaceFlagNames(game, word)) {
		names.append(QJsonObject{{QStringLiteral("bit"), static_cast<double>(flag.bit)}, {QStringLiteral("id"), flag.id},
			{QStringLiteral("label"), flag.displayName}, {QStringLiteral("description"), flag.description}});
	}
	return names;
}

QJsonArray idsJson(LevelFaceFlagGame game, LevelFaceFlagWord word, quint32 bits)
{
	return QJsonArray::fromStringList(levelFaceFlagIds(game, word, bits));
}
} // namespace

LevelFaceFlagsCliResult runLevelFaceFlags(const QStringList& arguments)
{
	LevelFaceFlagsCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--clear-all"};
	const QSet<QString> options{"--input", "--object", "--face", "--set-content", "--clear-content", "--set-surface", "--clear-surface", "--value",
		"--context", "--output", "--engine", "--map-name", "--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> repeated{"--object", "--face"};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments[i];
		// A negative --value is a value, not an option.
		if (!argument.startsWith(QStringLiteral("--"))) {
			positional << argument;
			continue;
		}
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (seen.contains(name) && !repeated.contains(name)) {
			return fail(2, Text::tr("Repeated option: %1. Only --object and --face may be repeated.").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				return fail(2, Text::tr("Flag %1 does not take a value.").arg(name));
			}
			continue;
		}
		if (!options.contains(name)) {
			return fail(2, Text::tr("Unknown option for map face-flags: %1").arg(name));
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		values[name] << value;
		if (values[name].size() > 16384) {
			return fail(2, Text::tr("At most 16,384 values are allowed for %1.").arg(name));
		}
	}
	if (positional.size() != (seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map face-flags needs one input map, positionally or with --input, and --object or --face targets; changes also need --output."));
	}
	const QString path = seen.contains(QStringLiteral("--input")) ? values.value(QStringLiteral("--input")).first() : positional[2];
	const QStringList changeOptions{QStringLiteral("--set-content"), QStringLiteral("--clear-content"), QStringLiteral("--set-surface"),
		QStringLiteral("--clear-surface"), QStringLiteral("--value"), QStringLiteral("--clear-all")};
	const bool changing = std::any_of(changeOptions.cbegin(), changeOptions.cend(), [&seen](const QString& option) { return seen.contains(option); });
	const QString output = values.value(QStringLiteral("--output")).value(0);
	if (changing && output.isEmpty()) {
		return fail(2, Text::tr("Changing face flags requires --output <save-as path>; use --dry-run to check the change."));
	}
	if (!changing && (!output.isEmpty() || seen.contains(QStringLiteral("--dry-run")) || seen.contains(QStringLiteral("--overwrite")))) {
		return fail(2, Text::tr("--output, --dry-run and --overwrite go with a change: --set-content, --clear-content, --set-surface, --clear-surface, --value or --clear-all."));
	}
	if (seen.contains(QStringLiteral("--clear-all")) && (seen.contains(QStringLiteral("--set-content")) || seen.contains(QStringLiteral("--set-surface"))
			|| seen.contains(QStringLiteral("--clear-content")) || seen.contains(QStringLiteral("--clear-surface")) || seen.contains(QStringLiteral("--value")))) {
		return fail(2, Text::tr("--clear-all clears every flag but detail and the value; give it alone."));
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), values.value(QStringLiteral("--map-name")).value(0), values.value(QStringLiteral("--engine")).value(0)},
			&document, &error)) {
		return fail(QFileInfo::exists(path) ? 1 : 3, error);
	}
	if (seen.contains(QStringLiteral("--context")) && !openLevelSceneGroup(&document, values.value(QStringLiteral("--context")).first(), &error)) {
		return fail(4, error);
	}
	const LevelFaceFlagGame game = levelFaceFlagGame(document);

	// Targets: the faces of --object brushes and entities, then --face picks.
	QVector<LevelSurfaceFace> faces;
	const QStringList objects = values.value(QStringLiteral("--object"));
	if (!objects.isEmpty()) {
		QVector<LevelMapSelectionRef> selection;
		for (const QString& value : objects) {
			for (const QString& selector : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
				const QStringList parts = selector.trimmed().split(QLatin1Char(':'));
				bool valid = false;
				const int id = parts.value(1).toInt(&valid);
				const LevelMapSelectionKind kind = parts.size() == 2 ? levelMapSelectionKindFromId(parts.first().trimmed().toLower()) : LevelMapSelectionKind::None;
				if (!valid || id < 0 || (kind != LevelMapSelectionKind::QuakeBrush && kind != LevelMapSelectionKind::Entity)) {
					return fail(2, Text::tr("--object takes brush:ID or entity:ID; patches carry no face flags: %1").arg(selector.trimmed()));
				}
				selection.removeAll(LevelMapSelectionRef{kind, id});
				selection << LevelMapSelectionRef{kind, id};
			}
		}
		if (!setLevelMapSelection(&document, selection, &error)) {
			return fail(2, Text::tr("Unable to select map objects: %1").arg(error));
		}
		faces = levelMapSelectedSurfaces(document);
	}
	for (const QString& token : values.value(QStringLiteral("--face"))) {
		const QStringList parts = token.trimmed().split(QLatin1Char(':'));
		bool brushOk = false, faceOk = false;
		const int brush = parts.value(0).toInt(&brushOk), face = parts.value(1).toInt(&faceOk);
		if (parts.size() != 2 || !brushOk || !faceOk || brush < 0 || face < 1) {
			return fail(2, Text::tr("--face takes brushId:faceNumber; face numbers start at one."));
		}
		if (!faces.contains(LevelSurfaceFace{brush, face - 1})) {
			faces.append({brush, face - 1});
		}
	}

	result.payload.insert(QStringLiteral("game"), gameId(game));
	result.payload.insert(QStringLiteral("contentNames"), namesJson(game, LevelFaceFlagWord::Content));
	result.payload.insert(QStringLiteral("surfaceNames"), namesJson(game, LevelFaceFlagWord::Surface));
	result.payload.insert(QStringLiteral("targetFaces"), static_cast<int>(faces.size()));

	if (!changing) {
		// Read only: the targets' flags, or with none the game's flag names.
		result.payload.insert(QStringLiteral("mapWritten"), false);
		QJsonArray rows;
		QHash<int, const LevelMapBrush*> brushes;
		for (const LevelMapBrush& brush : document.brushes) {
			brushes.insert(brush.id, &brush);
		}
		for (const LevelSurfaceFace& target : std::as_const(faces)) {
			const LevelMapBrush* brush = brushes.value(target.brushId);
			if (!brush || target.faceIndex < 0 || target.faceIndex >= brush->faces.size()) {
				return fail(4, Text::tr("Brush %1 has no face %2.").arg(target.brushId).arg(target.faceIndex + 1));
			}
			const LevelMapBrushFace& face = brush->faces[target.faceIndex];
			const quint32 content = static_cast<quint32>(static_cast<quint64>(face.contentFlags) & 0xffffffffULL);
			const quint32 surface = static_cast<quint32>(static_cast<quint64>(face.surfaceFlags) & 0xffffffffULL);
			if (rows.size() < 4096) {
				rows.append(QJsonObject{{QStringLiteral("face"), QStringLiteral("%1:%2").arg(target.brushId).arg(target.faceIndex + 1)},
					{QStringLiteral("texture"), face.textureName}, {QStringLiteral("content"), static_cast<double>(content)},
					{QStringLiteral("surface"), static_cast<double>(surface)}, {QStringLiteral("value"), static_cast<double>(face.surfaceValue)},
					{QStringLiteral("contentFlags"), idsJson(game, LevelFaceFlagWord::Content, content)},
					{QStringLiteral("surfaceFlags"), idsJson(game, LevelFaceFlagWord::Surface, surface)}, {QStringLiteral("written"), face.flagsWritten}});
			}
			result.lines << Text::tr("Face %1:%2 (%3): content %4, surface %5, value %6")
								.arg(target.brushId)
								.arg(target.faceIndex + 1)
								.arg(face.textureName)
								.arg(levelFaceFlagIds(game, LevelFaceFlagWord::Content, content).join(QLatin1Char(',')),
									levelFaceFlagIds(game, LevelFaceFlagWord::Surface, surface).join(QLatin1Char(',')))
								.arg(face.surfaceValue);
		}
		const LevelFaceFlagsSummary summary = levelFaceFlagsSummary(document, faces);
		result.payload.insert(QStringLiteral("faces"), rows);
		result.payload.insert(QStringLiteral("summary"),
			QJsonObject{{QStringLiteral("faces"), summary.faces}, {QStringLiteral("contentAll"), idsJson(game, LevelFaceFlagWord::Content, summary.contentAll)},
				{QStringLiteral("contentAny"), idsJson(game, LevelFaceFlagWord::Content, summary.contentAny)},
				{QStringLiteral("surfaceAll"), idsJson(game, LevelFaceFlagWord::Surface, summary.surfaceAll)},
				{QStringLiteral("surfaceAny"), idsJson(game, LevelFaceFlagWord::Surface, summary.surfaceAny)},
				{QStringLiteral("value"), summary.valueMixed || summary.faces == 0 ? QJsonValue() : QJsonValue(static_cast<double>(summary.value))},
				{QStringLiteral("valueMixed"), summary.valueMixed}});
		if (faces.isEmpty()) {
			result.lines << (game == LevelFaceFlagGame::None ? Text::tr("This map's faces carry no flags; Quake II and Quake III maps do.")
															 : Text::tr("Name brush faces with --object or --face to read their flags."));
		}
		return result;
	}

	// A change: parse the flag lists against the map's game.
	if (game == LevelFaceFlagGame::None) {
		return fail(4, Text::tr("Face flags belong to Quake II and Quake III maps; this map's faces carry none."));
	}
	LevelFaceFlagsEdit edit;
	const struct {
		const char* option;
		LevelFaceFlagWord word;
		quint32* bits;
	} lists[] {{"--set-content", LevelFaceFlagWord::Content, &edit.setContent}, {"--clear-content", LevelFaceFlagWord::Content, &edit.clearContent},
		{"--set-surface", LevelFaceFlagWord::Surface, &edit.setSurface}, {"--clear-surface", LevelFaceFlagWord::Surface, &edit.clearSurface}};
	for (const auto& list : lists) {
		const QString option = QString::fromLatin1(list.option);
		if (seen.contains(option) && !parseLevelFaceFlagBits(game, list.word, values.value(option).first(), list.bits, &error)) {
			return fail(2, Text::tr("%1: %2").arg(option, error));
		}
	}
	if (seen.contains(QStringLiteral("--value"))) {
		bool ok = false;
		edit.value = values.value(QStringLiteral("--value")).first().trimmed().toLongLong(&ok);
		if (!ok || edit.value < std::numeric_limits<qint32>::min() || edit.value > std::numeric_limits<qint32>::max()) {
			return fail(2, Text::tr("--value takes a whole number from -2147483648 to 2147483647."));
		}
		edit.setValue = true;
	}
	if (seen.contains(QStringLiteral("--clear-all"))) {
		edit = LevelFaceFlagsEdit::clearAll();
	}
	if (faces.isEmpty()) {
		return fail(2, Text::tr("Name the brush faces to change with --object brush:ID, entity:ID or --face brushId:faceNumber."));
	}
	LevelFaceFlagsReport report;
	if (!setLevelMapFaceFlags(&document, faces, edit, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, seen.contains(QStringLiteral("--dry-run")), seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	result.payload.insert(QStringLiteral("changedFaces"), report.changedFaces);
	result.payload.insert(QStringLiteral("brushes"), report.brushes);
	result.payload.insert(QStringLiteral("setContent"), idsJson(game, LevelFaceFlagWord::Content, edit.setContent));
	result.payload.insert(QStringLiteral("clearContent"), seen.contains(QStringLiteral("--clear-all")) ? QJsonArray{QStringLiteral("all-but-detail")}
																									 : idsJson(game, LevelFaceFlagWord::Content, edit.clearContent));
	result.payload.insert(QStringLiteral("setSurface"), idsJson(game, LevelFaceFlagWord::Surface, edit.setSurface));
	result.payload.insert(QStringLiteral("clearSurface"), seen.contains(QStringLiteral("--clear-all")) ? QJsonArray{QStringLiteral("all")}
																									 : idsJson(game, LevelFaceFlagWord::Surface, edit.clearSurface));
	result.payload.insert(QStringLiteral("value"), edit.setValue ? QJsonValue(static_cast<double>(edit.value)) : QJsonValue());
	result.payload.insert(QStringLiteral("mapWritten"), saved.written);
	result.payload.insert(QStringLiteral("output"), saved.outputPath);
	result.payload.insert(QStringLiteral("written"), saved.written);
	result.payload.insert(QStringLiteral("dryRun"), saved.dryRun);
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings));
	result.lines << Text::tr("Changed the flags of %n brush face(s) in one undo step.", nullptr, report.changedFaces);
	result.lines << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
