#include "cli/model_controls.h"

#include "core/model_document.h"
#include "core/model_editor_controls.h"
#include "core/model_mesh.h"
#include "core/model_sidebar.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelControlsCli)
};

ModelToolsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}

struct Arguments
{
	QSet<QString> flags;
	QHash<QString, QString> values;
	QStringList positional;
};

bool parse(const QStringList &arguments, const QSet<QString> &flags, const QSet<QString> &options, Arguments *parsed, QString *error)
{
	QSet<QString> seen;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const QString arg = arguments.at(i);
		if (!arg.startsWith(QStringLiteral("--")))
		{
			parsed->positional.append(arg);
			continue;
		}
		const int equal = arg.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
		{
			*error = Text::tr("Repeated option: %1.").arg(key);
			return false;
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
		{
			parsed->flags.insert(key);
			continue;
		}
		if (!options.contains(key))
		{
			*error = Text::tr("Unknown or invalid option: %1.").arg(key);
			return false;
		}
		const QString value = equal < 0 ? arguments.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith(QStringLiteral("--")))
		{
			*error = Text::tr("Option %1 requires a value.").arg(key);
			return false;
		}
		parsed->values.insert(key, value);
	}
	return true;
}

const QSet<QString> &commonFlags()
{
	static const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")};
	return flags;
}

QSet<QString> commonOptions()
{
	return {QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
}

QJsonArray strings(const QStringList &values)
{
	return QJsonArray::fromStringList(values);
}

QJsonObject profileJson(const ModelEditorProfile &profile)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("shortName"), profile.shortName);
	object.insert(QStringLiteral("description"), profile.description);
	object.insert(QStringLiteral("adaptations"), strings(profile.adaptations));
	object.insert(QStringLiteral("referenceUrl"), profile.referenceUrl);
	object.insert(QStringLiteral("aliases"), strings(profile.aliases));
	object.insert(QStringLiteral("layout"), modelViewLayoutId(profile.controls.layout.layout));
	object.insert(QStringLiteral("family"), profile.controls.layout.family);
	object.insert(QStringLiteral("transformStyle"), modelTransformStyleId(profile.controls.transform.style));
	object.insert(QStringLiteral("inheritsStudioKeys"), profile.controls.inheritDefaultKeys);
	return object;
}

QJsonArray rowsJson(const QVector<ModelEditorControlRow> &rows)
{
	QJsonArray array;
	for (const ModelEditorControlRow &row : rows)
	{
		QJsonObject object;
		object.insert(QStringLiteral("section"), row.section);
		object.insert(QStringLiteral("view"), row.view);
		object.insert(QStringLiteral("action"), row.action);
		object.insert(QStringLiteral("gesture"), row.gesture);
		array.append(object);
	}
	return array;
}

QJsonObject sidebarJson(const QString &family)
{
	QJsonObject object = modelSidebarArrangementJson(modelSidebarArrangementForFamily(family), family);
	object.insert(QStringLiteral("selectionModes"), strings(modelSelectionModeNames(family)));
	return object;
}
} // namespace

ModelToolsCliResult runModelProfiles(const QStringList &arguments)
{
	Arguments args;
	QString error;
	if (!parse(arguments, commonFlags(), commonOptions(), &args, &error))
		return fail(2, error);
	if (args.positional.size() > 1 && args.positional.at(1) != QStringLiteral("profiles"))
		return fail(2, Text::tr("model profiles takes no positional arguments."));
	const QString current = StudioSettings(StudioSettings::AccessMode::ReadOnly).modelEditorProfileId();
	ModelToolsCliResult result;
	QJsonArray profiles;
	for (const ModelEditorProfile &profile : modelEditorProfiles())
	{
		QJsonObject object = profileJson(profile);
		object.insert(QStringLiteral("current"), profile.id == current);
		object.insert(QStringLiteral("sidebars"), sidebarJson(profile.controls.layout.family));
		profiles.append(object);
		result.lines << Text::tr("%1%2 (%3): %4")
							.arg(profile.id == current ? QStringLiteral("* ") : QStringLiteral("  "), profile.displayName, profile.id, profile.description);
		for (const QString &adaptation : profile.adaptations)
			result.lines << Text::tr("    Differs: %1").arg(adaptation);
	}
	result.payload.insert(QStringLiteral("currentProfile"), current);
	result.payload.insert(QStringLiteral("defaultProfile"), defaultModelEditorProfileId());
	result.payload.insert(QStringLiteral("profiles"), profiles);
	return result;
}

ModelToolsCliResult runModelControls(const QStringList &arguments)
{
	const QSet<QString> flags = commonFlags() + QSet<QString>{QStringLiteral("--check"), QStringLiteral("--defaults"), QStringLiteral("--select"),
		QStringLiteral("--reset"), QStringLiteral("--overwrite")};
	QSet<QString> options = commonOptions();
	options += {QStringLiteral("--profile"), QStringLiteral("--section"), QStringLiteral("--export"), QStringLiteral("--import")};
	Arguments args;
	QString error;
	if (!parse(arguments, flags, options, &args, &error))
		return fail(2, error);
	if (args.positional.size() > 1 && args.positional.at(1) != QStringLiteral("controls"))
		return fail(2, Text::tr("model controls takes no positional arguments; choose a profile with --profile."));
	const bool importing = args.values.contains(QStringLiteral("--import"));
	const bool writes = importing || args.flags.contains(QStringLiteral("--select")) || args.flags.contains(QStringLiteral("--reset"));
	StudioSettings settings(writes ? StudioSettings::AccessMode::ReadWrite : StudioSettings::AccessMode::ReadOnly);
	QString profileId = settings.modelEditorProfileId();
	QJsonObject importedOverrides;
	if (importing)
	{
		QFile file(args.values.value(QStringLiteral("--import")));
		if (!file.open(QIODevice::ReadOnly))
			return fail(1, Text::tr("Cannot read %1: %2").arg(file.fileName(), file.errorString()));
		if (!parseModelEditorControlsFile(file.readAll(), &profileId, &importedOverrides, &error))
			return fail(4, error);
	}
	if (args.values.contains(QStringLiteral("--profile")))
	{
		const QString requested = normalizedModelEditorProfileId(args.values.value(QStringLiteral("--profile")));
		if (requested.isEmpty())
			return fail(2, Text::tr("Unknown modeller profile: %1. Choose one of: %2.")
							   .arg(args.values.value(QStringLiteral("--profile")), modelEditorProfileIds().join(QStringLiteral(", "))));
		if (importing && requested != profileId)
			return fail(2, Text::tr("The controls file is for %1, not %2.").arg(profileId, requested));
		profileId = requested;
	}
	const QString section = args.values.value(QStringLiteral("--section"));
	const QStringList sections{QStringLiteral("navigation"), QStringLiteral("selection"), QStringLiteral("transform"), QStringLiteral("layout"),
		QStringLiteral("keys")};
	if (!section.isEmpty() && !sections.contains(section))
		return fail(2, Text::tr("Use --section %1.").arg(sections.join(QLatin1Char('|'))));

	ModelToolsCliResult result;
	if (args.flags.contains(QStringLiteral("--reset")))
	{
		if (!settings.setModelEditorControlOverrides(profileId, {}, &error))
			return fail(1, error);
		result.lines << Text::tr("Restored the %1 defaults.").arg(profileId);
	}
	if (importing)
	{
		if (!settings.setModelEditorControlOverrides(profileId, importedOverrides, &error))
			return fail(1, error);
		result.lines << Text::tr("Imported controls for %1 from %2.").arg(profileId, args.values.value(QStringLiteral("--import")));
	}
	if (args.flags.contains(QStringLiteral("--select")))
	{
		settings.setModelEditorProfileId(profileId);
		settings.sync();
		if (settings.discardedWriteCount() > 0 || settings.status() != QSettings::NoError)
			return fail(1, Text::tr("The settings store refused the change."));
		result.lines << Text::tr("The modeller now uses %1.").arg(profileId);
	}

	QStringList warnings;
	const ModelEditorControls base = modelEditorControlsForProfile(profileId);
	const QJsonObject overrides = args.flags.contains(QStringLiteral("--defaults")) ? QJsonObject{} : settings.modelEditorControlOverrides(profileId);
	ModelEditorControls controls = base;
	applyModelEditorControlOverrides(base, overrides, &controls, &warnings);
	const QStringList problems = modelEditorControlProblems(controls);

	if (args.values.contains(QStringLiteral("--export")))
	{
		const QString path = args.values.value(QStringLiteral("--export"));
		if (QFileInfo::exists(path) && !args.flags.contains(QStringLiteral("--overwrite")))
			return fail(2, Text::tr("%1 exists; pass --overwrite to replace it.").arg(path));
		QSaveFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(modelEditorControlsFile(profileId, overrides)) < 0 || !file.commit())
			return fail(1, Text::tr("Cannot write %1: %2").arg(path, file.errorString()));
		result.lines << Text::tr("Wrote the %1 controls to %2.").arg(profileId, path);
	}

	QVector<ModelEditorControlRow> rows;
	for (const ModelEditorControlRow &row : modelEditorControlRows(controls))
	{
		if (section.isEmpty() || row.section == section)
			rows.append(row);
	}
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(profileId, &profile));
	result.lines << Text::tr("%1 controls%2:").arg(profile.displayName, overrides.isEmpty() ? QString() : Text::tr(" with your changes"));
	QString lastView;
	for (const ModelEditorControlRow &row : rows)
	{
		if (row.view != lastView)
		{
			result.lines << QStringLiteral("  %1").arg(row.view);
			lastView = row.view;
		}
		result.lines << QStringLiteral("    %1: %2").arg(row.action, row.gesture);
	}
	for (const QString &warning : warnings)
		result.lines << Text::tr("Warning: %1").arg(warning);
	for (const QString &problem : problems)
		result.lines << Text::tr("Problem: %1").arg(problem);
	result.payload.insert(QStringLiteral("profile"), profileJson(profile));
	result.payload.insert(QStringLiteral("controls"), modelEditorControlsJson(controls));
	result.payload.insert(QStringLiteral("overrides"), overrides);
	result.payload.insert(QStringLiteral("rows"), rowsJson(rows));
	result.payload.insert(QStringLiteral("sidebars"), sidebarJson(controls.layout.family));
	result.payload.insert(QStringLiteral("warnings"), strings(warnings));
	result.payload.insert(QStringLiteral("problems"), strings(problems));
	if (args.flags.contains(QStringLiteral("--check")) && !problems.isEmpty())
	{
		result.exitCode = 4;
		result.error = Text::tr("%1 has %2 control problem(s): %3").arg(profileId).arg(problems.size()).arg(problems.join(QStringLiteral(" ")));
	}
	return result;
}

ModelToolsCliResult runModelFormats(const QStringList &arguments)
{
	Arguments args;
	QString error;
	if (!parse(arguments, commonFlags(), commonOptions(), &args, &error))
		return fail(2, error);
	if (args.positional.size() > 1 && args.positional.at(1) != QStringLiteral("formats"))
		return fail(2, Text::tr("model formats takes no positional arguments."));
	ModelToolsCliResult result;
	QJsonArray formats;
	for (const ModelFormatCapability &format : modelFormatCapabilities())
	{
		QJsonObject object;
		object.insert(QStringLiteral("id"), format.id);
		object.insert(QStringLiteral("name"), format.name);
		object.insert(QStringLiteral("suffixes"), strings(format.suffixes));
		object.insert(QStringLiteral("engines"), strings(format.engines));
		object.insert(QStringLiteral("games"), strings(format.games));
		object.insert(QStringLiteral("skeletal"), format.skeletal);
		object.insert(QStringLiteral("animationOnly"), format.animationOnly);
		object.insert(QStringLiteral("companions"), strings(format.companions));
		object.insert(QStringLiteral("writes"), format.writes);
		object.insert(QStringLiteral("notes"), format.notes);
		formats.append(object);
		result.lines << Text::tr("%1 (.%2): %3%4%5")
							.arg(format.name, format.suffixes.join(QStringLiteral(", .")), format.games.join(QStringLiteral(", ")),
								 format.writes ? Text::tr("; read and write") : Text::tr("; read"),
								 format.skeletal ? Text::tr("; skeletal") : QString());
	}
	result.payload.insert(QStringLiteral("formats"), formats);
	QJsonArray exports;
	for (const QString &id : modelExportFormatIds())
		exports.append(id);
	result.payload.insert(QStringLiteral("exportFormats"), exports);
	return result;
}
} // namespace vibestudio::cli
