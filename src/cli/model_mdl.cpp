#include "cli/model_mdl.h"
#include "core/model_document.h"
#include "core/model_skin_source.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QMap>
#include <cmath>
#include <limits>

namespace vibestudio::cli
{
ModelMdlCliResult runModelMdl(const QStringList &arguments)
{
	const auto failure = [](int code, const QString &message) { return ModelMdlCliResult{code, message, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run"};
	const QSet<QString> options{"--input",	"--output",		"--operation", "--image",		"--palette-file", "--skin",
								"--member", "--name",		"--duration",  "--first-frame", "--last-frame",	  "--frame",
								"--flags",	"--sync",		"--eye",	   "--mdl-size",	"--time",		  "--native-frame",
								"--timing", "--sync-phase", "--package",   "--entry",		"--entry-index",  "--palette"};
	QHash<QString, QString> values;
	QSet<QString> seen;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith('-'))
		{
			positional << arg;
			continue;
		}
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
		{
			return failure(2, QCoreApplication::translate("ModelMdlCli", "Repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
		{
			continue;
		}
		if (!options.contains(key) && !globals.contains(key))
		{
			return failure(2, QCoreApplication::translate("ModelMdlCli", "Unexpected option: %1").arg(arg));
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
			return failure(2, QCoreApplication::translate("ModelMdlCli", "Missing value for %1.").arg(key));
		}
		values.insert(key, value);
	}
	if (positional.size() != (values.contains("--input") ? 2 : 3))
	{
		return failure(2, QCoreApplication::translate("ModelMdlCli", "Use model mdl <source.mesh.json>, or --input <source.mesh.json>."));
	}
	const auto path = values.contains("--input") ? values.value("--input") : positional[2];
	const auto operation = values.value("--operation");
	const QMap<QString, ModelEditKind> kinds{{"add-skin", ModelEditKind::AddMdlSkin},
											 {"replace-member", ModelEditKind::ReplaceMdlSkinMember},
											 {"append-member", ModelEditKind::AppendMdlSkinMember},
											 {"remove-skin", ModelEditKind::RemoveMdlSkin},
											 {"remove-member", ModelEditKind::RemoveMdlSkinMember},
											 {"skin-duration", ModelEditKind::SetMdlSkinDuration},
											 {"header", ModelEditKind::SetMdlHeader},
											 {"palette", ModelEditKind::SetMdlPalette},
											 {"group", ModelEditKind::GroupMdlFrames},
											 {"ungroup", ModelEditKind::UngroupMdlFrames},
											 {"pose-duration", ModelEditKind::SetMdlFrameDuration}};
	if (!operation.isEmpty() && !kinds.contains(operation))
	{
		return failure(2, QCoreApplication::translate("ModelMdlCli", "Choose --operation %1.").arg(kinds.keys().join('|')));
	}
	QSet<QString> allowed{"--input"}, required;
	if (operation.isEmpty() && values.contains("--time"))
	{
		allowed << "--time" << "--native-frame" << "--skin" << "--timing" << "--sync-phase";
	}
	if (!operation.isEmpty())
	{
		allowed << "--operation" << "--output" << "--overwrite" << "--dry-run";
		required << "--output";
	}
	if (operation == "add-skin")
	{
		allowed << "--name";
	}
	if (operation == "replace-member" || operation == "append-member")
	{
		allowed << "--skin";
		required << "--skin";
	}
	if (operation == "add-skin" || operation == "replace-member" || operation == "append-member")
	{
		allowed << "--image" << "--package" << "--entry" << "--entry-index" << "--palette";
		if (values.contains("--image") == values.contains("--package"))
		{
			return failure(2, QCoreApplication::translate("ModelMdlCli", "Choose one skin source: --image or --package."));
		}
		if (values.contains("--package"))
		{
			if (values.contains("--entry") == values.contains("--entry-index"))
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "Choose one package selector: --entry or --entry-index."));
			}
		}
		else if (values.contains("--entry") || values.contains("--entry-index") || values.contains("--palette"))
		{
			return failure(2, QCoreApplication::translate("ModelMdlCli", "Package selectors and --palette require --package."));
		}
	}
	if (operation == "replace-member" || operation == "remove-member" || operation == "skin-duration")
	{
		allowed << "--skin" << "--member";
		required << "--skin" << "--member";
	}
	if (operation == "remove-skin")
	{
		allowed << "--skin";
		required << "--skin";
	}
	if (operation == "skin-duration" || operation == "append-member" || operation == "group" || operation == "pose-duration")
	{
		allowed << "--duration";
		required << "--duration";
	}
	if (operation == "group" || operation == "ungroup")
	{
		allowed << "--first-frame" << "--last-frame";
		required << "--first-frame" << "--last-frame";
	}
	if (operation == "pose-duration")
	{
		allowed << "--frame";
		required << "--frame";
	}
	if (operation == "header")
	{
		allowed << "--flags" << "--sync" << "--eye" << "--mdl-size";
	}
	if (operation == "palette")
	{
		allowed << "--palette-file";
		required << "--palette-file";
	}
	for (const auto &key : seen)
	{
		if (!allowed.contains(key) && !globals.contains(key) && key != "--cli" && key != "--json" && key != "--quiet" && key != "--verbose")
		{
			return failure(2, QCoreApplication::translate("ModelMdlCli", "Option %1 does not apply to this MDL operation.").arg(key));
		}
	}
	for (const auto &key : required)
	{
		if (!values.contains(key))
		{
			return failure(2, QCoreApplication::translate("ModelMdlCli", "Missing required option %1.").arg(key));
		}
	}
	ModelSkinSourceReceipt skinReceipt;
	QJsonObject skinSource;
	ModelDocument document;
	QString error;
	if (!document.load(path, &error))
	{
		return failure(1, error);
	}
	if (!operation.isEmpty())
	{
		ModelEdit edit;
		edit.kind = kinds.value(operation);
		edit.text = values.value("--name");
		edit.mdlSettings = document.mesh().mdl;
		for (const auto &item : {QPair{QStringLiteral("--skin"), &edit.mdlSkinSlot}, QPair{QStringLiteral("--member"), &edit.mdlSkinMember},
								 QPair{QStringLiteral("--first-frame"), &edit.rangeFirst},
								 QPair{QStringLiteral("--last-frame"), &edit.rangeLast}, QPair{QStringLiteral("--frame"), &edit.frame}})
		{
			if (!values.contains(item.first))
			{
				continue;
			}
			bool valid = false;
			*item.second = values.value(item.first).toInt(&valid);
			if (!valid || *item.second < 0)
			{
				return failure(2,
							   QCoreApplication::translate("ModelMdlCli", "%1 requires a zero-based nonnegative index.").arg(item.first));
			}
		}
		if (values.contains("--duration"))
		{
			bool valid = false;
			edit.mdlDuration = values.value("--duration").toDouble(&valid);
			if (!valid || !std::isfinite(edit.mdlDuration))
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "Duration requires finite seconds."));
			}
		}
		if (values.contains("--flags"))
		{
			const auto text = values.value("--flags");
			bool valid = false;
			const auto flagsValue = text.toULongLong(&valid, text.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
			if (!valid || text.startsWith('-') || flagsValue > std::numeric_limits<quint32>::max())
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli",
															  "Flags require an unsigned 32-bit integer in decimal or 0x hexadecimal."));
			}
			edit.mdlSettings.flags = quint32(flagsValue);
		}
		if (values.contains("--sync"))
		{
			const auto sync = values.value("--sync");
			if (sync != "0" && sync != "1")
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "Sync type must be 0 (synchronized) or 1 (random)."));
			}
			edit.mdlSettings.syncType = sync.toInt();
		}
		if (values.contains("--mdl-size"))
		{
			bool valid = false;
			edit.mdlSettings.size = values.value("--mdl-size").toFloat(&valid);
			if (!valid || !std::isfinite(edit.mdlSettings.size) || edit.mdlSettings.size < 0)
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "MDL size requires a finite nonnegative value."));
			}
		}
		if (values.contains("--eye"))
		{
			const auto components = values.value("--eye").split(',');
			if (components.size() != 3)
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "Eye position requires x,y,z."));
			}
			float *axes[]{&edit.mdlSettings.eyePosition.x, &edit.mdlSettings.eyePosition.y, &edit.mdlSettings.eyePosition.z};
			for (int i = 0; i < 3; ++i)
			{
				bool valid = false;
				*axes[i] = components[i].toFloat(&valid);
				if (!valid || !std::isfinite(*axes[i]))
				{
					return failure(2, QCoreApplication::translate("ModelMdlCli", "Eye coordinates must be finite."));
				}
			}
		}
		if (values.contains("--image"))
		{
			QByteArray bytes;
			const auto imagePath = values.value("--image");
			if (!readModelFile(imagePath, &bytes, &error))
			{
				return failure(1, error);
			}
			if (!decodeModelMdlSkin(imagePath, bytes, modelMdlPreviewPalette(document.mesh()), &edit.mdlSkin, &error))
			{
				return failure(4, error);
			}
			if (modelPathsReferToSameFile(imagePath, values.value("--output")))
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "The output must be separate from the imported skin."));
			}
		}
		if (values.contains("--package"))
		{
			ModelSkinSourceReference reference;
			reference.path = values.value("--entry");
			if (values.contains("--entry-index"))
			{
				bool valid = false;
				const auto index = values.value("--entry-index").toLongLong(&valid);
				if (!valid || index < 0 || quint64(index) > quint64(std::numeric_limits<qsizetype>::max()))
				{
					return failure(2, QCoreApplication::translate("ModelMdlCli", "--entry-index requires a zero-based nonnegative index."));
				}
				reference.entryIndex = qsizetype(index);
			}
			const auto palette = values.value("--palette", "quake");
			if (!idTechPaletteDescriptorForId(palette))
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "Unknown palette family: %1").arg(palette));
			}
			PackageArchive archive;
			const auto package = values.value("--package");
			if (package.endsWith(".vibepackage", Qt::CaseInsensitive))
			{
				PackageStagingModel staging;
				if (!PackageDraft::load(package, &staging, &error))
				{
					return failure(1, error);
				}
				archive = packagePlannedArchive(staging, &error);
				if (!archive.isOpen()) { return failure(1, error); }
			}
			else if (!archive.load(package, &error))
			{
				return failure(1, error);
			}
			if (archive.protectsInputPath(values.value("--output")))
			{
				return failure(2, QCoreApplication::translate(
									  "ModelMdlCli", "The output must be separate from the package, draft and their input files."));
			}
			if (!readModelSkinSource(archive, reference, modelMdlPreviewPalette(document.mesh()), palette, &edit.mdlSkin, &skinReceipt,
									 &error))
			{
				return failure(4, error);
			}
			skinSource = QJsonObject{
				{"packagePath", QFileInfo(package).absoluteFilePath()},
				{"entryIndex", skinReceipt.entryIndex},
				{"path", skinReceipt.path},
				{"bytes", skinReceipt.bytes},
				{"sha256", QString::fromLatin1(skinReceipt.sha256.toHex())},
				{"paletteKind", skinReceipt.paletteKind},
				{"palettePath", skinReceipt.paletteSource},
				{"paletteEntryIndex", skinReceipt.paletteEntryIndex},
				{"paletteGenerated", edit.mdlSkin.paletteGenerated},
				{"paletteSha256", QString::fromLatin1(QCryptographicHash::hash(edit.mdlSkin.palette, QCryptographicHash::Sha256).toHex())}};
		}
		if (values.contains("--palette-file"))
		{
			if (!readModelFile(values.value("--palette-file"), &edit.mdlSettings.palette, &error))
			{
				return failure(1, error);
			}
			edit.mdlSettings.paletteGenerated = false;
			if (modelPathsReferToSameFile(values.value("--palette-file"), values.value("--output")))
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "The output must be separate from the imported palette."));
			}
		}
		if (!document.edit(edit, &error))
		{
			return failure(4, error);
		}
		const auto output = values.value("--output");
		if (!output.endsWith(".mesh.json", Qt::CaseInsensitive))
		{
			return failure(2, QCoreApplication::translate("ModelMdlCli",
														  "Save native MDL edits to a .mesh.json source, then use model build to export."));
		}
		const auto target = inspectModelWriteTarget(output);
		if (!target.isValid())
		{
			return failure(1, target.error);
		}
		if (target.existed && !seen.contains("--overwrite"))
		{
			return failure(1, QCoreApplication::translate("ModelMdlCli", "The output already exists; use --overwrite to replace it."));
		}
		if (!seen.contains("--dry-run") && !document.save(output, seen.contains("--overwrite"), &error))
		{
			return failure(1, error);
		}
	}
	ModelMdlCliResult result;
	if (!skinSource.isEmpty())
	{
		result.payload.insert("skinSource", skinSource);
		result.lines << QCoreApplication::translate("ModelMdlCli", "Skin source: entry %1, %2; SHA-256 %3.")
							.arg(skinReceipt.entryIndex)
							.arg(skinReceipt.path, QString::fromLatin1(skinReceipt.sha256.toHex()));
	}
	const auto &mesh = document.mesh();
	if (values.contains("--time"))
	{
		ModelMdlPlayback playback;
		const auto timing = values.value("--timing", "stored");
		if (timing != "stored" && timing != "glquake")
		{
			return failure(2, QCoreApplication::translate("ModelMdlCli", "Use --timing stored or glquake."));
		}
		playback.timing = timing == "glquake" ? ModelMdlTiming::GlQuake : ModelMdlTiming::Stored;
		for (const auto &item :
			 {QPair{QStringLiteral("--time"), &playback.seconds}, QPair{QStringLiteral("--sync-phase"), &playback.syncPhase}})
		{
			bool valid = false;
			*item.second = values.value(item.first, "0").toDouble(&valid);
			if (!valid || !std::isfinite(*item.second))
			{
				return failure(2, QCoreApplication::translate("ModelMdlCli", "%1 requires a finite number.").arg(item.first));
			}
		}
		for (const auto &item :
			 {QPair{QStringLiteral("--native-frame"), &playback.nativeFrame}, QPair{QStringLiteral("--skin"), &playback.skin}})
		{
			bool valid = false;
			*item.second = values.value(item.first, "0").toInt(&valid);
			if (!valid || *item.second < 0)
			{
				return failure(2,
							   QCoreApplication::translate("ModelMdlCli", "%1 requires a zero-based nonnegative index.").arg(item.first));
			}
		}
		ModelMdlPlaybackSample sample;
		if (!sampleModelMdl(mesh, playback, &sample, &error))
		{
			return failure(4, error);
		}
		result.payload.insert("sample", QJsonObject{{"seconds", playback.seconds},
													{"timing", timing},
													{"syncPhase", playback.syncPhase},
													{"nativeFrame", playback.nativeFrame},
													{"skin", playback.skin},
													{"pose", sample.frame},
													{"skinMember", sample.skinMember},
													{"frameCycleSeconds", sample.frameCycle},
													{"skinCycleSeconds", sample.skinCycle}});
		result.lines << QCoreApplication::translate("ModelMdlCli", "At %1 seconds (%2): native frame %3, pose %4; skin %5, member %6.")
							.arg(playback.seconds, 0, 'g', 12)
							.arg(timing)
							.arg(playback.nativeFrame)
							.arg(sample.frame)
							.arg(playback.skin)
							.arg(sample.skinMember);
	}
	result.payload.insert("enabled", mesh.mdl.enabled);
	result.payload.insert("source", QFileInfo(path).absoluteFilePath());
	result.payload.insert("written", !operation.isEmpty() && !seen.contains("--dry-run"));
	result.payload.insert("dryRun", seen.contains("--dry-run"));
	if (values.contains("--output"))
	{
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
	}
	result.lines << QCoreApplication::translate("ModelMdlCli", "MDL settings: %1; %2 skin slots; %3 native frames; %4 poses.")
						.arg(mesh.mdl.enabled ? QCoreApplication::translate("ModelMdlCli", "enabled")
											  : QCoreApplication::translate("ModelMdlCli", "not prepared"))
						.arg(mesh.embeddedSkins.size())
						.arg(mesh.mdl.frameGroups.size())
						.arg(mesh.frames.size());
	if (mesh.mdl.enabled)
	{
		auto settings = modelMdlSettingsJson(mesh.mdl);
		settings.remove("palette");
		settings.insert("paletteSha256",
						QString::fromLatin1(QCryptographicHash::hash(mesh.mdl.palette, QCryptographicHash::Sha256).toHex()));
		result.payload.insert("settings", settings);
		result.lines << QCoreApplication::translate("ModelMdlCli", "Flags: %1; sync: %2; skin: %3 × %4; generated palette: %5.")
							.arg(mesh.mdl.flags)
							.arg(mesh.mdl.syncType)
							.arg(mesh.mdl.skinSize.width())
							.arg(mesh.mdl.skinSize.height())
							.arg(mesh.mdl.paletteGenerated);
	}
	QJsonArray skins;
	for (const auto &skin : mesh.embeddedSkins)
	{
		QJsonArray times;
		QStringList ends;
		for (float time : skin.intervals)
		{
			times << time;
			ends << QString::number(time, 'g', 9);
		}
		skins << QJsonObject{{"index", skin.index}, {"name", skin.name}, {"members", skin.indexedFrames.size()}, {"intervals", times}};
		result.lines << QCoreApplication::translate("ModelMdlCli", "Skin %1 (%2): %3 members; cumulative ends [%4] seconds.")
							.arg(skin.index)
							.arg(skin.name)
							.arg(skin.indexedFrames.size())
							.arg(ends.join(", "));
	}
	result.payload.insert("skins", skins);
	for (int i = 0; i < mesh.mdl.frameGroups.size(); ++i)
	{
		const auto &group = mesh.mdl.frameGroups[i];
		QStringList ends;
		for (float time : group.intervals)
		{
			ends << QString::number(time, 'g', 9);
		}
		result.lines << QCoreApplication::translate("ModelMdlCli", "Native frame %1: poses %2–%3; cumulative ends [%4] seconds.")
							.arg(i)
							.arg(group.firstFrame)
							.arg(group.firstFrame + group.frameCount() - 1)
							.arg(ends.join(", "));
	}
	if (!operation.isEmpty())
	{
		result.lines << (seen.contains("--dry-run") ? QCoreApplication::translate("ModelMdlCli", "Validated; would write %1.")
													: QCoreApplication::translate("ModelMdlCli", "Wrote %1."))
							.arg(QFileInfo(values.value("--output")).absoluteFilePath());
	}
	return result;
}
} // namespace vibestudio::cli
