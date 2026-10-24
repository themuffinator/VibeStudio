#include "core/model_player_import.h"

#include "core/model_archive.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>

// Player layout: id Software Quake III Arena cg_players.c
// CG_RegisterClientModelname (models/players/<model>/lower.md3, upper.md3,
// head.md3 and animation.cfg), CG_RegisterClientSkin (<role>_<skin>.skin),
// the icon_<skin> lookup through CG_FindClientHeadFile, CG_LoadClientInfo
// (sound/player/<model>/<sound>.wav) and CG_ParseAnimationFile (at most
// 19,998 bytes). GPL-2.0-or-later reference, reviewed 2026-10-22; original
// implementation. The importer mirrors core/model_player_bundle.cpp, which
// writes the same layout.
// https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_players.c
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelPlayerImport)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
const std::array<QString, 3> &roles()
{
	static const std::array<QString, 3> names{QStringLiteral("lower"), QStringLiteral("upper"), QStringLiteral("head")};
	return names;
}
int roleIndex(QStringView name)
{
	for (int role = 0; role < 3; ++role)
		if (name.compare(roles()[role], Qt::CaseInsensitive) == 0)
			return role;
	return -1;
}
void record(ModelQ3PlayerFile *file, const QString &path, qsizetype index)
{
	if (file->count++ == 0)
	{
		file->path = path;
		file->entryIndex = index;
	}
}
QJsonObject fileJson(const ModelQ3PlayerFile &file)
{
	return {{"path", file.path}, {"entryIndex", file.entryIndex}, {"count", file.count}};
}
QJsonObject referenceJson(const ModelPlayerReference &reference)
{
	return {{"source", reference.source},
			{"kind", reference.sourceKind == ModelAssemblySource::File ? "file" : "package"},
			{"entryIndex", reference.entryIndex}};
}
ModelPlayerReference packageReference(const ModelQ3PlayerFile &file)
{
	// Unique paths, checked by the listing, keep working when the package is
	// edited and its entries move.
	return {file.path, ModelAssemblySource::Package, -1};
}
// A role's file, a missing one or a repeated one, for the problem lines.
QString fileProblem(const QString &path, const ModelQ3PlayerFile &file)
{
	return file.count == 0 ? Text::tr("%1 is missing.").arg(path)
						   : Text::tr("%1 appears %n times; remove the repeats from the package.", nullptr, file.count).arg(file.path);
}
} // namespace

bool ModelQ3PlayerSkinSet::complete() const
{
	return std::all_of(skins.cbegin(), skins.cend(), [](const auto &file) { return file.present(); });
}

QStringList ModelQ3PlayerListing::completeSkins() const
{
	QStringList names;
	for (const auto &skin : skins)
		if (skin.complete())
			names << skin.name;
	return names;
}

bool listModelQ3Players(const PackageArchiveReader &archive, QVector<ModelQ3PlayerListing> *players, QString *error,
						const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!players || !archive.isOpen())
		return fail(error, Text::tr("Open a package or folder before looking for players."));
	ModelWorkProgress work(control, ModelWorkPhase::Reading, error);
	if (!work.check())
		return false;
	static const QRegularExpression skinFile(QStringLiteral("^(lower|upper|head)_(.+)\\.skin$"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression iconFile(QStringLiteral("^icon_(.+)\\.tga$"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression detailFile(QStringLiteral("^(lower|upper|head)_[12]\\.md3$"), QRegularExpression::CaseInsensitiveOption);
	const QString modelsRoot = QStringLiteral("models/players/"), soundsRoot = QStringLiteral("sound/player/");
	// Keyed by the folded player and skin names, so output order is stable.
	QMap<QString, ModelQ3PlayerListing> found;
	QHash<QString, QMap<QString, ModelQ3PlayerSkinSet>> skins;
	QHash<QString, QHash<QString, ModelQ3PlayerFile>> sounds;
	const auto entries = archive.entries();
	for (qsizetype index = 0; index < entries.size(); ++index)
	{
		if (!work.step())
			return false;
		const auto &entry = entries[index];
		if (entry.kind != PackageEntryKind::File)
			continue;
		const auto &path = entry.virtualPath;
		if (path.startsWith(soundsRoot, Qt::CaseInsensitive))
		{
			const auto parts = path.mid(soundsRoot.size()).split(QLatin1Char('/'));
			if (parts.size() == 2 && !parts[0].isEmpty() && parts[1].endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive))
			{
				const auto name = parts[1].chopped(4).toLower();
				if (modelPlayerSoundNames().contains(name))
					record(&sounds[parts[0].toCaseFolded()][name], path, index);
			}
			continue;
		}
		if (!path.startsWith(modelsRoot, Qt::CaseInsensitive))
			continue;
		const auto rest = path.mid(modelsRoot.size());
		const int slash = int(rest.indexOf(QLatin1Char('/')));
		if (slash <= 0 || slash + 1 >= rest.size() || rest.indexOf(QLatin1Char('/'), slash + 1) >= 0)
			continue;
		const auto name = rest.left(slash), file = rest.mid(slash + 1);
		const auto key = name.toCaseFolded();
		const int role = file.endsWith(QStringLiteral(".md3"), Qt::CaseInsensitive) ? roleIndex(QStringView(file).chopped(4)) : -1;
		const auto skin = skinFile.match(file), icon = iconFile.match(file);
		const bool animation = file.compare(QStringLiteral("animation.cfg"), Qt::CaseInsensitive) == 0;
		const bool detail = detailFile.match(file).hasMatch();
		// Folders holding only textures or other models are not players.
		if (role < 0 && !animation && !detail && !skin.hasMatch() && !icon.hasMatch())
			continue;
		auto &player = found[key];
		if (player.name.isEmpty())
		{
			player.name = name;
			player.directory = path.left(modelsRoot.size() + slash + 1);
		}
		if (role >= 0)
			record(&player.models[role], path, index);
		else if (animation)
			record(&player.animation, path, index);
		else if (detail)
			player.detailLevels << path;
		else
		{
			const auto skinName = skin.hasMatch() ? skin.captured(2) : icon.captured(1);
			auto &set = skins[key][skinName.toCaseFolded()];
			if (set.name.isEmpty())
				set.name = skinName;
			if (skin.hasMatch())
				record(&set.skins[roleIndex(skin.capturedView(1))], path, index);
			else
				record(&set.icon, path, index);
		}
	}
	QVector<ModelQ3PlayerListing> result;
	for (auto i = found.begin(); i != found.end(); ++i)
	{
		auto player = i.value();
		const auto sets = skins.value(i.key());
		// default first: it is the skin the game asks for when none is named.
		if (sets.contains(QStringLiteral("default")))
			player.skins << sets.value(QStringLiteral("default"));
		for (auto set = sets.cbegin(); set != sets.cend(); ++set)
			if (set.key() != QStringLiteral("default"))
				player.skins << set.value();
		const auto voices = sounds.value(i.key());
		for (const auto &sound : modelPlayerSoundNames())
			if (voices.contains(sound))
				player.sounds << ModelQ3PlayerSoundFile{sound, voices.value(sound)};
		player.detailLevels.sort(Qt::CaseInsensitive);
		for (int role = 0; role < 3; ++role)
			if (!player.models[role].present())
				player.problems << fileProblem(player.directory + roles()[role] + QStringLiteral(".md3"), player.models[role]);
		if (!player.animation.present())
			player.problems << fileProblem(player.directory + QStringLiteral("animation.cfg"), player.animation);
		if (player.completeSkins().isEmpty())
			player.problems << Text::tr("No complete skin: the game needs lower_<skin>.skin, upper_<skin>.skin and head_<skin>.skin "
										"for one skin name.");
		result << player;
	}
	if (!work.check())
		return false;
	*players = std::move(result);
	return true;
}

bool readModelQ3AnimationEntry(const PackageArchiveReader &archive, const QString &path, qsizetype entryIndex, QByteArray *bytes,
							   qsizetype *selected, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!bytes || !archive.isOpen() || entryIndex < -1 || (entryIndex < 0 && path.isEmpty()))
		return fail(error, Text::tr("Choose an animation.cfg entry from an open package or folder."));
	ModelWorkProgress work(control, ModelWorkPhase::Reading, error);
	const auto entries = archive.entries();
	qsizetype index = entryIndex;
	if (index < 0)
	{
		QStringList matches;
		for (qsizetype i = 0; i < entries.size(); ++i)
		{
			if (!work.step())
				return false;
			if (entries[i].kind == PackageEntryKind::File && entries[i].virtualPath.compare(path, Qt::CaseInsensitive) == 0)
			{
				matches << QString::number(i);
				index = index < 0 ? i : index;
			}
		}
		if (matches.isEmpty())
			return fail(error, Text::tr("The package has no %1.").arg(path));
		if (matches.size() > 1)
			return fail(error, Text::tr("%1 appears %n times in the package (entries %2). Choose one by its entry index.", nullptr,
										int(matches.size()))
								   .arg(path, matches.join(QStringLiteral(", "))));
	}
	if (index >= entries.size() || entries[index].kind != PackageEntryKind::File ||
		(!path.isEmpty() && entries[index].virtualPath.compare(path, Qt::CaseInsensitive) != 0))
		return fail(error,
					Text::tr("Package entry %1 is not %2. The package may have changed; choose the entry again.").arg(index).arg(path));
	if (entries[index].sizeBytes > quint64(modelQ3AnimationMaxBytes))
		return fail(error, Text::tr("%1 is larger than 19,998 bytes, the most Quake III reads.").arg(entries[index].virtualPath));
	QByteArray candidate;
	if (!ModelArchiveReader(archive, control).readEntryAt(index, &candidate, error, modelQ3AnimationMaxBytes))
		return false;
	*bytes = std::move(candidate);
	if (selected)
		*selected = index;
	return true;
}

bool importModelQ3Player(const PackageArchiveReader &archive, const QString &player, const QString &skin, ModelQ3PlayerImport *result,
						 QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!result)
		return false;
	QVector<ModelQ3PlayerListing> players;
	if (!listModelQ3Players(archive, &players, error, control))
		return false;
	const auto found =
		std::find_if(players.cbegin(), players.cend(), [&](const auto &item) { return item.name.compare(player, Qt::CaseInsensitive) == 0; });
	if (found == players.cend())
	{
		QStringList names;
		for (const auto &item : players)
			names << item.name;
		return fail(error, Text::tr("The package has no player %1 under models/players/. Players found: %2.")
							   .arg(player, names.isEmpty() ? Text::tr("none") : names.join(QStringLiteral(", "))));
	}
	if (!found->canOpen())
		return fail(error, Text::tr("Player %1 cannot be opened. %2").arg(found->name, found->problems.join(QLatin1Char(' '))));
	const auto complete = found->completeSkins();
	const auto chosen =
		skin.isEmpty() ? (complete.contains(QStringLiteral("default"), Qt::CaseInsensitive) ? QStringLiteral("default") : complete.first()) : skin;
	const auto set = std::find_if(found->skins.cbegin(), found->skins.cend(),
								  [&](const auto &item) { return item.name.compare(chosen, Qt::CaseInsensitive) == 0; });
	if (set == found->skins.cend() || !set->complete())
		return fail(error, Text::tr("Player %1 has no complete %2 skin. Complete skins: %3.")
							   .arg(found->name, chosen, complete.join(QStringLiteral(", "))));
	QByteArray bytes;
	if (!readModelQ3AnimationEntry(archive, found->animation.path, found->animation.entryIndex, &bytes, nullptr, error, control))
		return false;
	ModelQ3PlayerImport candidate;
	QStringList configNotes;
	ModelQ3AnimationConfig config;
	if (!parseModelQ3Animation(bytes, &config, error, &configNotes))
		return fail(error, Text::tr("%1 cannot be read: %2").arg(found->animation.path, error ? *error : QString()));
	candidate.player = *found;
	candidate.skin = set->name;
	candidate.assembly.name = found->name;
	for (int role = 0; role < 3; ++role)
	{
		ModelAssemblyPart part;
		part.id = roles()[role];
		part.source = found->models[role].path;
		part.sourceKind = ModelAssemblySource::Package;
		if (role > 0)
		{
			part.parent = roles()[role - 1];
			part.tag = role == 1 ? QStringLiteral("tag_torso") : QStringLiteral("tag_head");
		}
		part.skin = packageReference(set->skins[role]);
		candidate.assembly.parts << part;
	}
	candidate.assembly.q3Animation = ModelAssemblyQ3Animation{config, roles()[0], roles()[1]};
	if (!validateModelAssembly(candidate.assembly, error))
		return fail(error, Text::tr("Player %1 cannot become an assembly: %2").arg(found->name, error ? *error : QString()));
	auto &publication = candidate.publication;
	publication.modelName = found->name;
	publication.skinName = set->name;
	publication.headPart = roles()[2];
	if (set->icon.present())
	{
		publication.iconSource = set->icon.path;
		publication.iconKind = ModelAssemblySource::Package;
	}
	QStringList carried, incomplete, withoutIcon;
	bool red = false, blue = false;
	for (const auto &other : found->skins)
	{
		if (!other.complete())
		{
			QStringList missing;
			for (int role = 0; role < 3; ++role)
				if (!other.skins[role].present())
					missing << roles()[role] + QLatin1Char('_') + other.name + QStringLiteral(".skin");
			incomplete << Text::tr("%1 (%2)").arg(other.name, missing.join(QStringLiteral(", ")));
			continue;
		}
		red = red || other.name.compare(QStringLiteral("red"), Qt::CaseInsensitive) == 0;
		blue = blue || other.name.compare(QStringLiteral("blue"), Qt::CaseInsensitive) == 0;
		if (&other == &*set)
			continue;
		ModelPlayerSkinVariant variant{other.name, {}, {}};
		for (int role = 0; role < 3; ++role)
			variant.skins[role] = packageReference(other.skins[role]);
		if (other.icon.present())
			variant.icon = packageReference(other.icon);
		else
			withoutIcon << other.name;
		publication.variants << variant;
		carried << other.name;
	}
	publication.teamVariants = red && blue;
	for (const auto &sound : found->sounds)
		if (sound.file.present())
			publication.sounds << ModelPlayerSound{sound.name, packageReference(sound.file)};
	auto &notes = candidate.notes;
	notes << Text::tr("Opened %1 as a new assembly: lower is the root, upper sits on lower's tag_torso and head on upper's tag_head, and each "
					  "part links its %2 skin from the package. The package itself is not changed.")
				 .arg(found->directory, set->name);
	notes += configNotes;
	if (!carried.isEmpty())
		notes << Text::tr("Player Package keeps the other complete skins with their .skin files and icons: %1.").arg(carried.join(QStringLiteral(", ")));
	if (publication.teamVariants)
		notes << Text::tr("The red and blue team skins are present, so Team skins is on.");
	if (!incomplete.isEmpty())
		notes << Text::tr("Incomplete skins are not carried over, because the game cannot load them: %1.").arg(incomplete.join(QStringLiteral("; ")));
	if (!set->icon.present())
		notes << Text::tr("There is no icon_%1.tga. Choose an icon in Player Package before publishing: without one the game shows its "
						  "default model instead.")
					 .arg(set->name);
	if (!withoutIcon.isEmpty())
		notes << Text::tr("Skins without an icon reuse the package icon when published: %1.").arg(withoutIcon.join(QStringLiteral(", ")));
	if (!found->detailLevels.isEmpty())
		notes << Text::tr("The package also has detail levels (%1). The assembly uses the full-detail models; Player Package can generate new "
						  "detail levels.")
					 .arg(found->detailLevels.join(QStringLiteral(", ")));
	if (!publication.sounds.isEmpty())
		notes << Text::tr("Player Package keeps %n custom sound(s) from sound/player/%1/.", nullptr, int(publication.sounds.size())).arg(found->name);
	static const QRegularExpression id(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,23}$"));
	if (!id.match(found->name).hasMatch())
		notes << Text::tr("Player Package needs a player ID of 1–24 ASCII letters, digits, underscores or hyphens, starting with a letter. "
						  "Change %1 there before publishing.")
					 .arg(found->name);
	if (publication.variants.size() + 1 > 16)
		notes << Text::tr("Player Package publishes at most 16 skins. Remove %n skin(s) there before publishing.", nullptr,
						  int(publication.variants.size()) + 1 - 16);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
		return false;
	*result = std::move(candidate);
	return true;
}

QJsonObject modelQ3PlayerListingJson(const ModelQ3PlayerListing &player)
{
	QJsonObject models;
	for (int role = 0; role < 3; ++role)
		models.insert(roles()[role], fileJson(player.models[role]));
	QJsonArray skins, sounds;
	for (const auto &skin : player.skins)
	{
		QJsonObject node{{"name", skin.name}, {"complete", skin.complete()}, {"icon", fileJson(skin.icon)}};
		for (int role = 0; role < 3; ++role)
			node.insert(roles()[role], fileJson(skin.skins[role]));
		skins.append(node);
	}
	for (const auto &sound : player.sounds)
	{
		auto node = fileJson(sound.file);
		node.insert("name", sound.name);
		sounds.append(node);
	}
	return {{"name", player.name},
			{"directory", player.directory},
			{"canOpen", player.canOpen()},
			{"models", models},
			{"animation", fileJson(player.animation)},
			{"skins", skins},
			{"completeSkins", QJsonArray::fromStringList(player.completeSkins())},
			{"detailLevels", QJsonArray::fromStringList(player.detailLevels)},
			{"sounds", sounds},
			{"problems", QJsonArray::fromStringList(player.problems)}};
}

QStringList modelQ3PlayerListingText(const ModelQ3PlayerListing &player)
{
	QStringList lines{player.canOpen() ? Text::tr("%1 (%2)").arg(player.name, player.directory)
									   : Text::tr("%1 (%2): cannot be opened").arg(player.name, player.directory)};
	QStringList skins;
	for (const auto &skin : player.skins)
	{
		QString state = skin.complete() ? (skin.icon.present() ? Text::tr("complete") : Text::tr("complete, no icon")) : Text::tr("incomplete");
		skins << Text::tr("%1 (%2)").arg(skin.name, state);
	}
	lines << Text::tr("Skins: %1").arg(skins.isEmpty() ? Text::tr("none") : skins.join(QStringLiteral(", ")));
	if (!player.detailLevels.isEmpty())
		lines << Text::tr("Detail levels: %1").arg(player.detailLevels.join(QStringLiteral(", ")));
	lines << Text::tr("Custom sounds: %1 of %2").arg(player.sounds.size()).arg(modelPlayerSoundNames().size());
	lines += player.problems;
	return lines;
}

QJsonObject modelQ3PlayerImportJson(const ModelQ3PlayerImport &result)
{
	const auto &options = result.publication;
	QJsonArray skins, sounds;
	for (const auto &variant : options.variants)
	{
		QJsonObject node{{"name", variant.name}};
		for (int role = 0; role < 3; ++role)
			node.insert(roles()[role], variant.skins[role] ? QJsonValue(referenceJson(*variant.skins[role])) : QJsonValue());
		node.insert("icon", variant.icon ? QJsonValue(referenceJson(*variant.icon)) : QJsonValue());
		skins.append(node);
	}
	for (const auto &sound : options.sounds)
	{
		auto node = referenceJson(sound.source);
		node.insert("name", sound.name);
		sounds.append(node);
	}
	const QJsonValue icon = options.iconSource.isEmpty()
								? QJsonValue()
								: QJsonValue(referenceJson({options.iconSource, options.iconKind, options.iconEntryIndex}));
	return {{"player", modelQ3PlayerListingJson(result.player)},
			{"skin", result.skin},
			{"assembly", modelAssemblyJson(result.assembly)},
			{"publication",
			 QJsonObject{{"modelName", options.modelName},
						 {"skinName", options.skinName},
						 {"headPart", options.headPart},
						 {"icon", icon},
						 {"skins", skins},
						 {"teamVariants", options.teamVariants},
						 {"sounds", sounds},
						 {"arguments", QJsonArray::fromStringList(modelPlayerBundleArguments(options))}}},
			{"notes", QJsonArray::fromStringList(result.notes)}};
}

QStringList modelQ3PlayerImportText(const ModelQ3PlayerImport &result)
{
	QStringList arguments;
	for (const auto &argument : modelPlayerBundleArguments(result.publication))
		arguments << (argument.contains(QLatin1Char(' ')) ? QLatin1Char('"') + argument + QLatin1Char('"') : argument);
	return QStringList{Text::tr("Player %1 with skin %2: lower, upper and head from the package, and native animation from %3.")
						   .arg(result.player.name, result.skin, result.player.animation.path)} +
		   result.notes +
		   QStringList{Text::tr("Player Package settings for --operation player-review: %1").arg(arguments.join(QLatin1Char(' ')))};
}

QStringList modelPlayerBundleArguments(const ModelPlayerBundleOptions &options)
{
	QStringList arguments{QStringLiteral("--player-name"), options.modelName, QStringLiteral("--skin-name"), options.skinName,
						  QStringLiteral("--head-part"), options.headPart};
	if (!options.iconSource.isEmpty())
	{
		arguments << QStringLiteral("--icon") << options.iconSource << QStringLiteral("--icon-kind")
				  << (options.iconKind == ModelAssemblySource::File ? QStringLiteral("file") : QStringLiteral("package"));
		if (options.iconEntryIndex >= 0)
			arguments << QStringLiteral("--icon-entry-index") << QString::number(options.iconEntryIndex);
	}
	// Name every skin first so the CLI keeps their order; files and icons follow.
	QStringList names;
	for (const auto &variant : options.variants)
		names << variant.name;
	if (!names.isEmpty())
		arguments << QStringLiteral("--skin-variants") << names.join(QLatin1Char(','));
	for (const auto &variant : options.variants)
	{
		for (int role = 0; role < 3; ++role)
			if (variant.skins[role])
				arguments << QStringLiteral("--variant-skin")
						  << QStringLiteral("%1:%2=%3").arg(variant.name, roles()[role], modelPlayerReferenceText(*variant.skins[role]));
		if (variant.icon)
			arguments << QStringLiteral("--variant-icon") << QStringLiteral("%1=%2").arg(variant.name, modelPlayerReferenceText(*variant.icon));
	}
	if (options.teamVariants)
		arguments << QStringLiteral("--team-variants");
	if (options.detailLevels == ModelPlayerDetailLevels::Generated)
		arguments << QStringLiteral("--lod-levels") << QString::number(options.lodLevels) << QStringLiteral("--lod-ratio")
				  << QString::number(options.lodRatio, 'g', 6);
	else if (options.detailLevels == ModelPlayerDetailLevels::Stored)
		arguments << QStringLiteral("--lod-recipes");
	for (const auto &sound : options.sounds)
		arguments << QStringLiteral("--sound") << QStringLiteral("%1=%2").arg(sound.name, modelPlayerReferenceText(sound.source));
	return arguments;
}
} // namespace vibestudio
