#include "cli/model_assembly.h"
#include "core/model_assembly.h"
#include "core/model_assembly_animation.h"
#include "core/model_assembly_recovery.h"
#include "core/model_player_bundle.h"
#include "core/model_player_import.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace vibestudio::cli
{
ModelAssemblyCliResult runModelAssembly(const QStringList &arguments)
{
	const auto fail = [](int code, const QString &message) { return ModelAssemblyCliResult{code, message, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--new", "--clear-skin", "--team-variants", "--lod-recipes"};
	const QSet<QString> options{"--input",
								"--output",
								"--operation",
								"--name",
								"--part",
								"--rename-to",
								"--model",
								"--kind",
								"--parent",
								"--tag",
								"--translation",
								"--rotation",
								"--scale",
								"--first-frame",
								"--last-frame",
								"--fps",
								"--phase",
								"--loop",
								"--interpolate",
								"--time",
								"--package",
								"--palette",
								"--directory",
								"--recovery",
								"--sha256",
								"--frames",
								"--sample-fps",
								"--clip-name",
								"--config",
								"--lower-part",
								"--upper-part",
								"--lower-animation",
								"--upper-animation",
								"--skin",
								"--skin-kind",
								"--skin-entry-index", "--player-name", "--skin-name", "--head-part", "--icon", "--icon-kind", "--icon-entry-index",
								"--skin-variants", "--variant-skin", "--variant-icon", "--sound", "--lod-levels", "--lod-ratio",
								"--from-player", "--config-entry", "--config-entry-index"};
	// Player options that may be given more than once, kept in order.
	const QSet<QString> repeatable{"--variant-skin", "--variant-icon", "--sound"};
	QHash<QString, QStringList> repeated;
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith('-'))
		{
			positional.append(arg);
			continue;
		}
		const int equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && !repeatable.contains(key))
		{
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
		{
			continue;
		}
		if (!options.contains(key) && !globals.contains(key))
		{
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Unexpected option: %1").arg(arg));
		}
		QString value;
		if (equal >= 0)
		{
			value = arg.mid(equal + 1);
		}
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith("--"))
		{
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty())
		{
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Missing value for %1.").arg(key));
		}
		if (repeatable.contains(key))
			repeated[key].append(value);
		else
			values.insert(key, value);
	}
	const bool fresh = seen.contains("--new"), dryRun = seen.contains("--dry-run"), overwrite = seen.contains("--overwrite");
	// --new --from-player NAME opens a package player as a new assembly.
	const bool fromPlayer = seen.contains("--from-player");
	const auto operation =
		fromPlayer ? QStringLiteral("open-player") : values.value("--operation", fresh ? QStringLiteral("add") : QStringLiteral("inspect"));
	const bool player = operation == "player-review" || operation == "player-export";
	// A package, a folder or a portable .vibepackage draft, read-only.
	const auto loadPackage = [](const QString &path, std::shared_ptr<const PackageArchiveReader> *result, QString *error) {
		PackageArchive archive;
		if (path.endsWith(".vibepackage", Qt::CaseInsensitive))
		{
			PackageStagingModel staging;
			if (!PackageDraft::load(path, &staging, error))
				return false;
			archive = packagePlannedArchive(staging, error);
			if (!archive.isOpen())
				return false;
		}
		else if (!archive.load(path, error))
			return false;
		*result = std::make_shared<PackageArchive>(std::move(archive));
		return true;
	};
	if (operation == "players")
	{
		QSet<QString> allowed{"--cli", "--json", "--quiet", "--verbose", "--operation", "--package"};
		allowed.unite(globals);
		if (positional.size() != 2 || !(seen - allowed).isEmpty() || !values.contains("--package"))
			return fail(2, QCoreApplication::translate("ModelAssemblyCli",
													   "players lists the Quake III players in --package PATH and takes no assembly source."));
		QString error;
		std::shared_ptr<const PackageArchiveReader> archive;
		QVector<ModelQ3PlayerListing> players;
		if (!loadPackage(values.value("--package"), &archive, &error))
			return fail(1, error);
		if (!listModelQ3Players(*archive, &players, &error))
			return fail(4, error);
		ModelAssemblyCliResult result;
		QJsonArray items;
		result.lines << QCoreApplication::translate("ModelAssemblyCli", "%n player(s) in %1.", nullptr, int(players.size()))
							.arg(QFileInfo(values.value("--package")).absoluteFilePath());
		for (const auto &item : players)
		{
			items.append(modelQ3PlayerListingJson(item));
			result.lines += modelQ3PlayerListingText(item);
		}
		result.payload = {{"operation", operation},
						  {"package", QFileInfo(values.value("--package")).absoluteFilePath()},
						  {"players", items}};
		return result;
	}
	if (QSet<QString>{"recoveries", "recover", "discard"}.contains(operation))
	{
		QSet<QString> allowed{"--cli", "--json", "--quiet", "--verbose", "--operation", "--directory"};
		allowed.unite(globals);
		if (operation != "recoveries")
		{
			allowed.unite({"--recovery", "--sha256", "--dry-run"});
		}
		if (operation == "recover")
		{
			allowed.unite({"--output", "--overwrite"});
		}
		if (positional.size() != 2 || !(seen - allowed).isEmpty())
		{
			return fail(2, QCoreApplication::translate("ModelAssemblyCli",
													   "Recovery operations accept --directory; recover/discard also require "
													   "--recovery ID and --sha256 DIGEST. Recover requires --output."));
		}
		const auto directory = values.value("--directory", modelAssemblyRecoveryDirectory());
		ModelAssemblyCliResult result;
		if (operation == "recoveries")
		{
			const auto scan = listModelAssemblyRecoveries(directory);
			if (!scan.error.isEmpty())
			{
				return fail(4, scan.error);
			}
			result.payload = modelAssemblyRecoveryJson(scan);
			result.payload.insert("operation", operation);
			result.lines << QCoreApplication::translate("ModelAssemblyCli", "%1 assembly recovery copies in %2.")
								.arg(scan.records.size())
								.arg(directory);
			for (const auto &record : scan.records)
			{
				result.lines << QStringLiteral("%1  %2  %3")
									.arg(record.id, QString::fromLatin1(record.sha256.toHex()),
										 record.isValid() ? record.title : record.error);
			}
			if (scan.limited)
			{
				result.lines << QCoreApplication::translate("ModelAssemblyCli", "The bounded recovery scan was incomplete.");
			}
			return result;
		}
		const auto id = values.value("--recovery");
		const auto path = modelAssemblyRecoveryPath(directory, id);
		const auto sha = QByteArray::fromHex(values.value("--sha256").toLatin1());
		if (path.isEmpty() || sha.size() != 32 || QString::fromLatin1(sha.toHex()) != values.value("--sha256").toLower() ||
			(operation == "recover" && !values.contains("--output")))
		{
			return fail(2, QCoreApplication::translate(
							   "ModelAssemblyCli", "Use a recovery ID and its SHA-256 from recoveries; recover also requires --output."));
		}
		QString error;
		result.payload = {{"operation", operation},
						  {"recovery", id},
						  {"sha256", QString::fromLatin1(sha.toHex())},
						  {"dryRun", dryRun},
						  {"written", false}};
		if (operation == "discard")
		{
			if (!discardModelAssemblyRecovery(directory, id, sha, dryRun, &error))
			{
				return fail(4, error);
			}
			result.payload.insert("discarded", !dryRun);
			result.lines << (dryRun
								 ? QCoreApplication::translate(
									   "ModelAssemblyCli",
									   "Validated recovery digest without removing the copy; active-session locks are checked on commit.")
								 : QCoreApplication::translate("ModelAssemblyCli", "Discarded assembly recovery: %1").arg(id));
			return result;
		}
		ModelAssemblyDocument document;
		ModelAssemblyRecoverySnapshot snapshot;
		if (!restoreModelAssemblyRecovery(path, sha, &document, &snapshot, &error) ||
			!document.save(values.value("--output"), overwrite, &error, {}, dryRun))
		{
			return fail(4, error);
		}
		result.payload.insert("assembly", modelAssemblyJson(document.assembly()));
		result.payload.insert("selectedPart", snapshot.selectedPart);
		result.payload.insert("timeSeconds", snapshot.seconds);
		result.payload.insert("sourcePath", snapshot.sourcePath);
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
		result.payload.insert("written", !dryRun);
		result.lines << (dryRun ? QCoreApplication::translate("ModelAssemblyCli", "Validated without writing: %1")
								: QCoreApplication::translate("ModelAssemblyCli", "Wrote: %1"))
							.arg(values.value("--output"));
		result.lines << QCoreApplication::translate(
			"ModelAssemblyCli",
			"The recovery copy was preserved. Input availability is checked when you open or inspect the recovered assembly.");
		return result;
	}
	if (!QSet<QString>{"inspect", "add", "update", "remove", "bake", "bake-animation", "animation-set", "animation-export",
					   "animation-clear", "player-review", "player-export", "open-player"}
			 .contains(operation) ||
		positional.size() != (fresh || values.contains("--input") ? 2 : 3) || (fresh && values.contains("--input")) ||
		(fresh && operation != "add" && operation != "open-player") || (fromPlayer && (!fresh || seen.contains("--operation"))))
	{
		return fail(
			2, QCoreApplication::translate(
				   "ModelAssemblyCli", "Use model assembly <source.assembly.json> with inspect, add, update, remove, bake, bake-animation, "
									   "animation-set, animation-export, animation-clear, player-review or player-export; --new starts an assembly with add, "
									   "and --new --from-player NAME opens a package player; --operation players lists them."));
	}
	QSet<QString> allowed{"--input", "--operation", "--package", "--palette"};
	if (fromPlayer)
		allowed.unite({"--from-player", "--skin-name"});
	if (operation == "inspect" || operation == "bake" || operation == "bake-animation")
	{
		allowed << "--time";
	}
	if (operation == "bake-animation")
	{
		allowed.unite({"--frames", "--sample-fps", "--clip-name"});
	}
	if (operation == "animation-set")
	{
		allowed.unite({"--config", "--config-entry", "--config-entry-index", "--lower-part", "--upper-part", "--lower-animation",
					   "--upper-animation"});
	}
	if (operation != "inspect" && operation != "player-review")
	{
		allowed << "--output" << "--overwrite" << "--dry-run";
	}
	if (player) allowed.unite({"--player-name", "--skin-name", "--head-part", "--icon", "--icon-kind", "--icon-entry-index", "--skin-variants",
							   "--variant-skin", "--variant-icon", "--sound", "--lod-levels", "--lod-ratio", "--team-variants", "--lod-recipes"});
	if (fresh)
	{
		allowed << "--new" << "--name";
	}
	if (operation == "add" || operation == "update")
	{
		allowed.unite({"--part", "--model", "--kind", "--parent", "--tag", "--translation", "--rotation", "--scale", "--first-frame",
					   "--last-frame", "--fps", "--phase", "--loop", "--interpolate", "--skin", "--skin-kind", "--skin-entry-index"});
	}
	if (operation == "update")
	{
		allowed << "--rename-to" << "--clear-skin";
	}
	if (operation == "remove")
	{
		allowed << "--part";
	}
	for (const auto &key : seen)
	{
		if (!allowed.contains(key) && !globals.contains(key) && !QSet<QString>{"--cli", "--json", "--quiet", "--verbose"}.contains(key))
		{
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Option %1 does not apply to %2.").arg(key, operation));
		}
	}
	if ((operation != "inspect" && operation != "player-review" && !values.contains("--output")) ||
		(QSet<QString>{"add", "update", "remove"}.contains(operation) && !values.contains("--part")) ||
		(operation == "add" && !values.contains("--model")))
	{
		return fail(
			2, QCoreApplication::translate("ModelAssemblyCli",
										   "Edits and bakes require --output; part edits require --part, and add also requires --model."));
	}
	if ((values.contains("--palette") && !values.contains("--package")) || (values.contains("--kind") && !values.contains("--model")))
	{
		return fail(2, QCoreApplication::translate("ModelAssemblyCli", "--palette requires --package; --kind requires --model."));
	}
	if ((fromPlayer && !values.contains("--package")) || (values.contains("--config") && values.contains("--config-entry")) ||
		(values.contains("--config-entry") && !values.contains("--package")) ||
		(values.contains("--config-entry-index") && !values.contains("--config-entry")))
		return fail(2, QCoreApplication::translate("ModelAssemblyCli",
												   "--from-player and --config-entry read from --package. Use --config FILE or --config-entry PATH "
												   "(with an optional --config-entry-index N), not both."));
	if (((values.contains("--skin-kind") || values.contains("--skin-entry-index")) && !values.contains("--skin")) ||
		(seen.contains("--clear-skin") && values.contains("--skin")))
		return fail(2, QCoreApplication::translate("ModelAssemblyCli",
												   "Skin kind and entry index require --skin. Use either --skin or --clear-skin."));
	ModelPlayerBundleOptions playerOptions;
	if (player)
	{
		playerOptions.modelName = values.value("--player-name");
		playerOptions.skinName = values.value("--skin-name", QStringLiteral("default"));
		playerOptions.headPart = values.value("--head-part");
		playerOptions.iconSource = values.value("--icon");
		const auto kind = values.value("--icon-kind", QStringLiteral("file"));
		bool indexValid = true;
		if (values.contains("--icon-entry-index")) playerOptions.iconEntryIndex = values.value("--icon-entry-index").toInt(&indexValid);
		if (playerOptions.modelName.isEmpty() || playerOptions.headPart.isEmpty() || playerOptions.iconSource.isEmpty() ||
			!values.contains("--package") || (kind != "file" && kind != "package") || !indexValid ||
			(values.contains("--icon-entry-index") && (kind != "package" || playerOptions.iconEntryIndex < 0)))
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Player operations require --player-name, --head-part, --icon and --package. Icon kind is file|package; an entry index must be nonnegative and package-only."));
		playerOptions.iconKind = kind == "file" ? ModelAssemblySource::File : ModelAssemblySource::Package;
		if (playerOptions.iconKind == ModelAssemblySource::File) playerOptions.iconSource = QFileInfo(playerOptions.iconSource).absoluteFilePath();
		// "package:PATH" names a package entry; anything else is a file here.
		const auto reference = [](const QString &text) {
			auto result = modelPlayerReference(text);
			if (result.sourceKind == ModelAssemblySource::File)
				result.source = QFileInfo(result.source).absoluteFilePath();
			return result;
		};
		const auto variant = [&](const QString &name) -> ModelPlayerSkinVariant & {
			for (auto &item : playerOptions.variants)
				if (item.name == name)
					return item;
			playerOptions.variants.append(ModelPlayerSkinVariant{name, {}, {}});
			return playerOptions.variants.last();
		};
		const auto usage = [&] {
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Use --skin-variants NAME,NAME, --variant-skin NAME:lower|upper|head=PATH, --variant-icon NAME=PATH and --sound NAME=PATH; PATH is a file or package:PATH, and each skin role, icon and sound is given once."));
		};
		if (values.contains("--skin-variants"))
			for (const auto &name : values.value("--skin-variants").split(','))
			{
				if (name.trimmed().isEmpty() || std::any_of(playerOptions.variants.cbegin(), playerOptions.variants.cend(), [&](const auto &item) { return item.name == name.trimmed(); }))
					return usage();
				variant(name.trimmed());
			}
		for (const auto &item : repeated.value("--variant-skin"))
		{
			const int colon = item.indexOf(':'), equal = item.indexOf('=', colon + 1);
			const QStringList roles{"lower", "upper", "head"};
			const int role = colon > 0 && equal > colon + 1 ? int(roles.indexOf(item.mid(colon + 1, equal - colon - 1))) : -1;
			if (role < 0 || equal + 1 >= item.size())
				return usage();
			auto &skin = variant(item.left(colon)).skins[role];
			if (skin)
				return usage();
			skin = reference(item.mid(equal + 1));
		}
		for (const auto &item : repeated.value("--variant-icon"))
		{
			const int equal = item.indexOf('=');
			if (equal <= 0 || equal + 1 >= item.size())
				return usage();
			auto &icon = variant(item.left(equal)).icon;
			if (icon)
				return usage();
			icon = reference(item.mid(equal + 1));
		}
		for (const auto &item : repeated.value("--sound"))
		{
			const int equal = item.indexOf('=');
			if (equal <= 0 || equal + 1 >= item.size())
				return usage();
			playerOptions.sounds.append({item.left(equal), reference(item.mid(equal + 1))});
		}
		playerOptions.teamVariants = seen.contains("--team-variants");
		if ((values.contains("--lod-levels") && seen.contains("--lod-recipes")) || (values.contains("--lod-ratio") && !values.contains("--lod-levels")))
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Use --lod-levels N (with an optional --lod-ratio) or --lod-recipes, not both."));
		if (seen.contains("--lod-recipes"))
			playerOptions.detailLevels = ModelPlayerDetailLevels::Stored;
		if (values.contains("--lod-levels"))
		{
			bool levelsValid = false, ratioValid = true;
			playerOptions.detailLevels = ModelPlayerDetailLevels::Generated;
			playerOptions.lodLevels = values.value("--lod-levels").toInt(&levelsValid);
			if (values.contains("--lod-ratio"))
				playerOptions.lodRatio = values.value("--lod-ratio").toDouble(&ratioValid);
			if (!levelsValid || !ratioValid)
				return fail(2, QCoreApplication::translate("ModelAssemblyCli", "--lod-levels takes 1 or 2 and --lod-ratio 0.05 to 0.95."));
		}
	}
	QString error;
	// The package is read before the assembly: a player and an animation.cfg
	// entry come from it. It is never written.
	std::shared_ptr<const PackageArchiveReader> package;
	if (values.contains("--package"))
	{
		if (!loadPackage(values.value("--package"), &package, &error))
			return fail(1, error);
		if (values.contains("--output") && package->protectsInputPath(values.value("--output")))
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Write outside the assembly's input package or portable draft."));
	}
	ModelAssemblyDocument document;
	const auto source = fresh ? QString() : values.value("--input", positional.value(2));
	ModelQ3PlayerImport imported;
	if (fromPlayer)
	{
		if (!importModelQ3Player(*package, values.value("--from-player"), values.value("--skin-name"), &imported, &error))
			return fail(4, error);
		if (values.contains("--name"))
			imported.assembly.name = values.value("--name");
		if (!document.setAssembly(imported.assembly, QDir::currentPath(), &error))
			return fail(4, error);
	}
	else if (fresh)
	{
		ModelAssembly assembly;
		assembly.name = values.value("--name", QCoreApplication::translate("ModelAssemblyCli", "Assembly"));
		if (!document.setAssembly(assembly, QDir::currentPath(), &error))
		{
			return fail(4, error);
		}
	}
	else if (!document.load(source, &error))
	{
		return fail(4, error);
	}
	QStringList configNotes;
	if (operation == "animation-set")
	{
		auto binding = document.assembly().q3Animation.value_or(ModelAssemblyQ3Animation{});
		if (!document.assembly().q3Animation && !values.contains("--config") && !values.contains("--config-entry"))
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "A new native animation binding requires --config or --config-entry, "
																		   "--lower-part and --upper-part."));
		if (values.contains("--config-entry"))
		{
			qsizetype index = -1, selected = -1;
			if (values.contains("--config-entry-index"))
			{
				bool valid = false;
				index = values.value("--config-entry-index").toLongLong(&valid);
				if (!valid || index < 0)
					return fail(2, QCoreApplication::translate("ModelAssemblyCli", "--config-entry-index must be a nonnegative integer."));
			}
			QByteArray bytes;
			if (!readModelQ3AnimationEntry(*package, values.value("--config-entry"), index, &bytes, &selected, &error) ||
				!parseModelQ3Animation(bytes, &binding.config, &error, &configNotes))
				return fail(4, error);
			configNotes.prepend(QCoreApplication::translate("ModelAssemblyCli", "Native animation read from package entry %1 (%2).")
									.arg(selected)
									.arg(package->entries()[selected].virtualPath));
		}
		if (values.contains("--config"))
		{
			if (modelPathsReferToSameFile(values.value("--config"), values.value("--output")))
				return fail(2, QCoreApplication::translate("ModelAssemblyCli",
														   "The assembly output cannot replace its imported animation configuration."));
			QByteArray bytes;
			if (QFileInfo(values.value("--config")).size() > modelQ3AnimationMaxBytes ||
				!readModelFile(values.value("--config"), &bytes, &error) ||
				!parseModelQ3Animation(bytes, &binding.config, &error, &configNotes))
				return fail(4, error.isEmpty()
								   ? QCoreApplication::translate("ModelAssemblyCli", "The native configuration exceeds 19,998 bytes.")
								   : error);
		}
		if (values.contains("--lower-part"))
			binding.lowerPart = values.value("--lower-part");
		if (values.contains("--upper-part"))
			binding.upperPart = values.value("--upper-part");
		if (values.contains("--lower-animation"))
			binding.lowerAnimation = modelQ3AnimationSlot(values.value("--lower-animation"));
		if (values.contains("--upper-animation"))
			binding.upperAnimation = modelQ3AnimationSlot(values.value("--upper-animation"));
		if (!document.setQ3Animation(binding, &error))
			return fail(4, error);
	}
	else if (operation == "animation-clear" && !document.setQ3Animation(std::nullopt, &error))
		return fail(4, error);
	if (operation == "add" || operation == "update")
	{
		ModelAssemblyPart part;
		part.id = values.value("--part");
		if (operation == "update")
		{
			const auto &parts = document.assembly().parts;
			const auto found = std::find_if(parts.cbegin(), parts.cend(), [&](const auto &p) { return p.id == part.id; });
			if (found == parts.cend())
			{
				return fail(2, QCoreApplication::translate("ModelAssemblyCli", "Choose an existing part ID."));
			}
			part = *found;
		}
		if (values.contains("--rename-to"))
		{
			part.id = values.value("--rename-to");
		}
		if (values.contains("--kind"))
		{
			const auto kind = values.value("--kind");
			if (kind != "file" && kind != "package")
			{
				return fail(2, QCoreApplication::translate("ModelAssemblyCli", "--kind accepts file or package."));
			}
			part.sourceKind = kind == "file" ? ModelAssemblySource::File : ModelAssemblySource::Package;
		}
		if (values.contains("--model"))
		{
			part.source = part.sourceKind == ModelAssemblySource::File ? QFileInfo(values.value("--model")).absoluteFilePath()
																	   : values.value("--model");
		}
		if (seen.contains("--clear-skin"))
			part.skin.reset();
		if (values.contains("--skin"))
		{
			const auto kind = values.value("--skin-kind", QStringLiteral("file"));
			if ((kind != "file" && kind != "package") || (kind == "file" && values.contains("--skin-entry-index")))
				return fail(2, QCoreApplication::translate(
								   "ModelAssemblyCli", "--skin-kind accepts file or package; --skin-entry-index requires a package skin."));
			ModelAssemblySkin skin;
			skin.sourceKind = kind == "file" ? ModelAssemblySource::File : ModelAssemblySource::Package;
			skin.source = kind == "file" ? QFileInfo(values.value("--skin")).absoluteFilePath() : values.value("--skin");
			if (values.contains("--skin-entry-index"))
			{
				bool valid = false;
				skin.entryIndex = values.value("--skin-entry-index").toInt(&valid);
				if (!valid || skin.entryIndex < 0)
					return fail(2, QCoreApplication::translate("ModelAssemblyCli", "--skin-entry-index must be a nonnegative integer."));
			}
			part.skin = skin;
		}
		if (values.contains("--parent"))
		{
			part.parent = values.value("--parent");
		}
		if (values.contains("--tag"))
		{
			part.tag = values.value("--tag");
		}
		const auto vector = [&](const QString &key, ModelVec3 *target) {
			if (!values.contains(key))
			{
				return true;
			}
			const auto components = values.value(key).split(',');
			if (components.size() != 3)
			{
				return false;
			}
			float *fields[]{&target->x, &target->y, &target->z};
			for (int i = 0; i < 3; ++i)
			{
				bool valid = false;
				const double v = components[i].toDouble(&valid);
				if (!valid || !std::isfinite(v) || std::abs(v) > 1000000)
				{
					return false;
				}
				*fields[i] = float(v);
			}
			return true;
		};
		const auto number = [&](const QString &key, double *target) {
			if (!values.contains(key))
			{
				return true;
			}
			bool valid = false;
			*target = values.value(key).toDouble(&valid);
			return valid && std::isfinite(*target);
		};
		const auto integer = [&](const QString &key, int *target) {
			if (!values.contains(key))
			{
				return true;
			}
			bool valid = false;
			*target = values.value(key).toInt(&valid);
			return valid;
		};
		const auto boolean = [&](const QString &key, bool *target) {
			if (!values.contains(key))
			{
				return true;
			}
			const auto v = values.value(key);
			if (v != "on" && v != "off")
			{
				return false;
			}
			*target = v == "on";
			return true;
		};
		if (!vector("--translation", &part.translation) || !vector("--rotation", &part.rotation) || !number("--scale", &part.scale) ||
			!number("--fps", &part.framesPerSecond) || !number("--phase", &part.phase) || !integer("--first-frame", &part.firstFrame) ||
			!integer("--last-frame", &part.lastFrame) || !boolean("--loop", &part.loop) || !boolean("--interpolate", &part.interpolate))
		{
			return fail(
				2, QCoreApplication::translate(
					   "ModelAssemblyCli", "Use finite numeric values, comma-separated X,Y,Z vectors and on|off for playback switches."));
		}
		if (!document.setPart(operation == "add" ? QString() : values.value("--part"), part, &error))
		{
			return fail(4, error);
		}
	}
	else if (operation == "remove" && !document.removeBranch(values.value("--part"), &error))
	{
		return fail(4, error);
	}
	ModelAssemblyContext context;
	context.directory = document.directory();
	context.paletteId = values.value("--palette");
	context.archive = package;
	double seconds = 0;
	if (values.contains("--time"))
	{
		bool valid = false;
		seconds = values.value("--time").toDouble(&valid);
		if (!valid || !std::isfinite(seconds) || seconds < 0 || seconds > 1000000)
		{
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "--time must be from 0 to 1,000,000 seconds."));
		}
	}
	ModelAssemblyAnimationOptions animationOptions;
	animationOptions.startSeconds = seconds;
	if (operation == "bake-animation")
	{
		bool countValid = false, rateValid = false;
		animationOptions.frameCount = values.value("--frames").toInt(&countValid);
		animationOptions.framesPerSecond = values.value("--sample-fps").toDouble(&rateValid);
		animationOptions.clipName = values.value("--clip-name", QStringLiteral("assembly"));
		if (!countValid || !rateValid || !std::isfinite(animationOptions.framesPerSecond) || animationOptions.framesPerSecond < .001 ||
			animationOptions.framesPerSecond > 1000 || animationOptions.frameCount < 1 || animationOptions.frameCount > 1024 ||
			seconds + (animationOptions.frameCount - 1) / animationOptions.framesPerSecond > 1000000)
		{
			return fail(2, QCoreApplication::translate("ModelAssemblyCli", "bake-animation requires --frames 1–1024 and --sample-fps "
																		   "0.001–1000; all samples must be within 0–1,000,000 seconds."));
		}
	}
	ModelAssemblyResolved resolved;
	ModelAssemblyPose pose;
	if (!document.assembly().parts.isEmpty() && (!resolveModelAssembly(document.assembly(), context, &resolved, &error) ||
												 !sampleModelAssembly(document.assembly(), resolved, seconds, &pose, &error)))
	{
		return fail(4, error);
	}
	ModelExportReport report;
	if (player)
	{
		ModelPlayerBundle bundle;
		if (!prepareModelPlayerBundle(document.assembly(), resolved, context, playerOptions, source, &bundle, &error)) return fail(4, error);
		ModelAssemblyCliResult result;
		result.payload = modelPlayerBundleJson(bundle);
		result.payload.insert("operation", operation);
		result.payload.insert("written", false);
		result.payload.insert("dryRun", dryRun);
		result.lines = modelPlayerBundleText(bundle);
		if (operation == "player-export")
		{
			const auto written = writeModelPlayerBundle(bundle, values.value("--output"), overwrite, dryRun);
			result.payload.insert("written", written.outputCommitted);
			result.payload.insert("write", QJsonObject{{"outputPath", written.outputPath}, {"outputCommitted", written.outputCommitted},
				{"backupPath", written.backupPath}, {"sha256", written.sha256}, {"bytes", double(written.bytesWritten)},
				{"deterministic", written.deterministic}, {"determinismVerified", written.determinismVerified},
				{"dryRun", written.dryRun}, {"cancelled", written.cancelled}, {"succeeded", written.succeeded()},
				{"warnings", QJsonArray::fromStringList(written.warnings)}, {"blockedMessages", QJsonArray::fromStringList(written.blockedMessages)},
				{"recoveryPaths", QJsonArray::fromStringList(written.recoveryPaths)}});
			result.lines << packageWriteReportText(written);
			if (!written.succeeded()) { result.exitCode = 4; result.error = packageWriteReportText(written); }
		}
		return result;
	}
	if (operation == "animation-export")
	{
		if (!exportModelAssemblyQ3Animation(document.assembly(), resolved, values.value("--output"), source, overwrite, dryRun, &error, {},
											context.archive))
			return fail(4, error);
	}
	else if (operation == "bake")
	{
		if (!exportModelAssemblyPose(document.assembly(), resolved, seconds, values.value("--output"), source, overwrite, dryRun, &error,
									 {}, &report, context.archive))
		{
			return fail(4, error);
		}
	}
	else if (operation == "bake-animation")
	{
		if (!exportModelAssemblyAnimation(document.assembly(), resolved, animationOptions, values.value("--output"), source, overwrite,
										  dryRun, &error, {}, &report, context.archive))
		{
			return fail(4, error);
		}
	}
	else if (operation != "inspect")
	{
		if (!document.save(values.value("--output"), overwrite, &error, {}, dryRun, context.archive))
		{
			return fail(4, error);
		}
	}
	QJsonArray inputs;
	pose.notes += configNotes;
	for (int i = 0; i < resolved.inputs.size(); ++i)
	{
		const auto &input = resolved.inputs[i];
		const auto sample = pose.samples.value(i);
		QJsonObject item{{"part", input.part},
						 {"source", input.source},
						 {"kind", input.sourceKind == ModelAssemblySource::File ? "file" : "package"},
						 {"sha256", QString::fromLatin1(input.sha256.toHex())},
						 {"bytes", input.bytes},
						 {"frame", sample.frame},
						 {"nextFrame", sample.nextFrame},
						 {"fraction", sample.fraction}};
		if (input.skin)
			item.insert("skin", modelAssemblySkinInputJson(*input.skin));
		inputs.append(item);
	}
	ModelAssemblyCliResult result;
	result.payload = {{"operation", operation},
					  {"assembly", modelAssemblyJson(document.assembly())},
					  {"inputs", inputs},
					  {"timeSeconds", seconds},
					  {"surfaces", pose.mesh.surfaceCount},
					  {"vertices", pose.mesh.vertexCount},
					  {"triangles", pose.mesh.triangleCount},
					  {"notes", QJsonArray::fromStringList(operation.startsWith("bake") ? report.notes : pose.notes)},
					  {"dryRun", dryRun},
					  {"written", operation != "inspect" && !dryRun}};
	if (document.assembly().q3Animation)
	{
		const auto &binding = *document.assembly().q3Animation;
		auto native = modelQ3AnimationJson(binding.config);
		native.insert("lowerPart", binding.lowerPart);
		native.insert("upperPart", binding.upperPart);
		native.insert("lowerAnimation", modelQ3AnimationName(binding.lowerAnimation));
		native.insert("upperAnimation", modelQ3AnimationName(binding.upperAnimation));
		result.payload.insert("nativeAnimation", native);
	}
	if (operation != "inspect")
	{
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
	}
	result.lines << QCoreApplication::translate("ModelAssemblyCli", "%1: %2 part(s), %3 vertices, %4 triangles at %5 seconds.")
						.arg(document.assembly().name)
						.arg(document.assembly().parts.size())
						.arg(pose.mesh.vertexCount)
						.arg(pose.mesh.triangleCount)
						.arg(seconds, 0, 'g', 10);
	for (const auto &input : resolved.inputs)
	{
		result.lines << QStringLiteral("%1: %2 [%3]").arg(input.part, input.source, QString::fromLatin1(input.sha256.toHex()));
		if (input.skin)
		{
			result.lines << QCoreApplication::translate("ModelAssemblyCli", "Skin: %1 [%2]; package entry: %3")
								.arg(input.skin->source, QString::fromLatin1(input.skin->sha256.toHex()))
								.arg(input.skin->entryIndex);
			result.lines += modelSkinBindingPlanText(input.skin->bindings);
		}
	}
	result.lines += operation.startsWith("bake") ? report.notes : pose.notes;
	if (fromPlayer)
	{
		result.payload.insert("player", modelQ3PlayerImportJson(imported));
		result.lines += modelQ3PlayerImportText(imported);
	}
	if (operation == "bake-animation")
	{
		result.payload.insert(
			"sampling", QJsonObject{{"startSeconds", seconds},
									{"frameCount", animationOptions.frameCount},
									{"framesPerSecond", animationOptions.framesPerSecond},
									{"clipName", animationOptions.clipName},
									{"lastSampleSeconds", seconds + (animationOptions.frameCount - 1) / animationOptions.framesPerSecond},
									{"durationSeconds", animationOptions.frameCount / animationOptions.framesPerSecond}});
		const bool measured = QFileInfo(values.value("--output")).suffix().compare(QStringLiteral("md2"), Qt::CaseInsensitive) == 0;
		QJsonObject details{{"storedVertices", report.storedVertices}, {"quantizationMeasured", measured}};
		if (measured)
		{
			details.insert("maxPositionError", report.maxPositionError);
			details.insert("maxUvError", report.maxUvError);
			details.insert("maxNormalAngleDegrees", report.maxNormalAngleDegrees);
		}
		result.payload.insert("export", details);
	}
	if (operation != "inspect")
	{
		result.lines << (dryRun ? QCoreApplication::translate("ModelAssemblyCli", "Validated without writing: %1")
								: QCoreApplication::translate("ModelAssemblyCli", "Wrote: %1"))
							.arg(values.value("--output"));
	}
	return result;
}
} // namespace vibestudio::cli
