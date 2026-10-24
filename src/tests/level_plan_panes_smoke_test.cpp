// Show Top, Front and Side View (core/level_editor_controls.h): which plan
// panes the three-plan layouts show. Covers the pane ids and their parser,
// the last-pane refusal, the next shown pane, the per-combination splitter
// keys, the settings round trip, the palette entries and, with the
// vibestudio binary as the first argument, `editor layout --panes`.

#include "core/level_editor_controls.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <utility>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}

LevelPlanPanes panesOf(bool top, bool front, bool side)
{
	LevelPlanPanes panes;
	panes.shown = {top, front, side};
	return panes;
}

void identifiers()
{
	const LevelPlanPanes all;
	expect(levelPlanPanesShownCount(all) == 3 && levelPlanPanesId(all) == QStringLiteral("top,front,side"), "every pane is shown by default");
	expect(levelPlanPaneId(0) == QStringLiteral("top") && levelPlanPaneId(1) == QStringLiteral("front") && levelPlanPaneId(2) == QStringLiteral("side")
			&& levelPlanPaneId(3).isEmpty() && levelPlanPaneId(-1).isEmpty(),
		"pane ids follow the projection order");
	LevelPlanPanes parsed = panesOf(true, false, false);
	expect(levelPlanPanesForId(QStringLiteral("side, top"), &parsed) && parsed == panesOf(true, false, true), "ids parse in any order", levelPlanPanesId(parsed));
	expect(levelPlanPanesForId(QStringLiteral("all"), &parsed) && parsed == LevelPlanPanes {}, "all shows every pane");
	expect(levelPlanPanesForId(QStringLiteral("front"), &parsed) && parsed == panesOf(false, true, false), "one pane alone");
	const LevelPlanPanes before = parsed;
	for (const QString& bad : {QString(), QStringLiteral(" "), QStringLiteral("top,top"), QStringLiteral("bogus"), QStringLiteral("top,"),
			 QStringLiteral(",side"), QStringLiteral("Top"), QStringLiteral("top;side"), QStringLiteral("all,top")}) {
		expect(!levelPlanPanesForId(bad, &parsed) && parsed == before, "malformed pane lists are refused untouched", bad);
	}
	expect(levelPlanPanesForId(QStringLiteral("front,side"), nullptr), "parsing can only validate");
}

void toggling()
{
	LevelPlanPanes panes;
	expect(setLevelPlanPaneShown(&panes, 0, false) && panes == panesOf(false, true, true), "hide Top");
	expect(setLevelPlanPaneShown(&panes, 1, false) && panes == panesOf(false, false, true), "hide Front");
	expect(!setLevelPlanPaneShown(&panes, 2, false) && panes == panesOf(false, false, true), "the last shown pane stays");
	expect(setLevelPlanPaneShown(&panes, 2, true) && panes == panesOf(false, false, true), "showing a shown pane is a no-op success");
	expect(setLevelPlanPaneShown(&panes, 0, true) && panes == panesOf(true, false, true), "show Top again");
	expect(!setLevelPlanPaneShown(&panes, 3, true) && !setLevelPlanPaneShown(&panes, -1, false) && !setLevelPlanPaneShown(nullptr, 0, true)
			&& panes == panesOf(true, false, true),
		"out-of-range panes are refused");
	expect(levelPlanPaneNextShown(panes, 0) == 0 && levelPlanPaneNextShown(panes, 1) == 2 && levelPlanPaneNextShown(panes, 2) == 2
			&& levelPlanPaneNextShown(panes, 3) == 0 && levelPlanPaneNextShown(panes, -1) == 2,
		"the next shown pane skips hidden ones and wraps");
	expect(levelPlanPaneNextShown(panesOf(false, true, false), 2) == 1, "one shown pane is always next");
}

void splitterKeys()
{
	expect(levelViewSplitterStatePrefix(LevelViewLayout::FourViews, {}) == QStringLiteral("levelViews/four-views/"),
		"every pane shown keeps the existing key");
	expect(levelViewSplitterStatePrefix(LevelViewLayout::FourViews, panesOf(false, true, true)) == QStringLiteral("levelViews/four-views/front-side/"),
		"each combination keeps its own proportions");
	expect(levelViewSplitterStatePrefix(LevelViewLayout::CameraBesidePlans, panesOf(true, false, false))
			== QStringLiteral("levelViews/camera-beside-plans/top/"),
		"one pane combination key");
	expect(levelViewSplitterStatePrefix(LevelViewLayout::CameraAndPlan, panesOf(true, false, false)) == QStringLiteral("levelViews/camera-and-plan/")
			&& levelViewSplitterStatePrefix(LevelViewLayout::Single2D, panesOf(false, false, true)) == QStringLiteral("levelViews/single-2d/"),
		"one-plan layouts ignore the pane choice");
}

void settingsRoundTrip(const QTemporaryDir& temp)
{
	const QString path = temp.filePath(QStringLiteral("core-settings.ini"));
	{
		StudioSettings settings(path);
		expect(settings.levelPlanPanes() == LevelPlanPanes {}, "unset settings show every pane");
		expect(settings.setLevelPlanPanes(panesOf(false, true, false)), "pane choice saved");
		expect(!settings.setLevelPlanPanes(panesOf(false, false, false)), "no shown pane is refused");
		settings.sync();
	}
	{
		StudioSettings settings(path);
		expect(settings.levelPlanPanes() == panesOf(false, true, false), "pane choice survives a new settings object");
		StudioSettings readOnly(path, StudioSettings::AccessMode::ReadOnly);
		expect(!readOnly.setLevelPlanPanes(LevelPlanPanes {}) && readOnly.levelPlanPanes() == panesOf(false, true, false),
			"a read-only store refuses the choice");
	}
	{
		QSettings raw(path, QSettings::IniFormat);
		raw.setValue(QStringLiteral("levels/planPanes"), QStringLiteral("top,bogus"));
		raw.sync();
	}
	{
		StudioSettings settings(path);
		expect(settings.levelPlanPanes() == LevelPlanPanes {}, "an unreadable stored choice shows every pane");
	}
}

void paletteEntries()
{
	for (const auto& [id, label] : {std::pair {"map.showTopView", "Show Top View"}, std::pair {"map.showFrontView", "Show Front View"},
			 std::pair {"map.showSideView", "Show Side View"}}) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && entry.label == QString::fromLatin1(label)
				&& !entry.summary.isEmpty(),
			"palette entry for the pane command", QString::fromLatin1(id));
	}
}

void cli(const QString& executable, const QTemporaryDir& temp)
{
	const QString settingsPath = temp.filePath(QStringLiteral("cli-settings.ini"));
	const auto run = [&](const QStringList& args, int code = 0, bool json = true) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		QStringList command {QStringLiteral("--cli")};
		if (json) {
			command << QStringLiteral("--json");
		}
		command << QStringLiteral("--settings-file") << settingsPath << QStringLiteral("editor") << QStringLiteral("layout");
		process.start(executable, command + args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status",
			args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return std::make_pair(QJsonDocument::fromJson(out).object(), QString::fromUtf8(out + err));
	};
	auto [report, text] = run({});
	expect(report.value(QStringLiteral("planPanes")).toArray() == QJsonArray {QStringLiteral("top"), QStringLiteral("front"), QStringLiteral("side")},
		"a fresh store reports every pane", text);
	std::tie(report, text) = run({QStringLiteral("four-views"), QStringLiteral("--panes"), QStringLiteral("side,front")});
	expect(report.value(QStringLiteral("effectiveLayout")) == QStringLiteral("four-views")
			&& report.value(QStringLiteral("planPanes")).toArray() == QJsonArray {QStringLiteral("front"), QStringLiteral("side")}
			&& report.value(QStringLiteral("planPanesApply")) == true,
		"layout and panes saved together", text);
	std::tie(report, text) = run({});
	expect(report.value(QStringLiteral("preference")) == QStringLiteral("four-views")
			&& report.value(QStringLiteral("planPanes")).toArray() == QJsonArray {QStringLiteral("front"), QStringLiteral("side")},
		"a read reports the saved panes", text);
	std::tie(report, text) = run({QStringLiteral("--panes=top")});
	expect(report.value(QStringLiteral("preference")) == QStringLiteral("four-views")
			&& report.value(QStringLiteral("planPanes")).toArray() == QJsonArray {QStringLiteral("top")},
		"panes alone keep the layout", text);
	std::tie(report, text) = run({QStringLiteral("single-2d")});
	expect(report.value(QStringLiteral("planPanes")).toArray() == QJsonArray {QStringLiteral("top")} && report.value(QStringLiteral("planPanesApply")) == false,
		"a layout alone keeps the panes, which one-plan layouts do not use", text);
	std::tie(report, text) = run({}, 0, false);
	expect(text.contains(QStringLiteral("Plan panes: top")) && text.contains(QStringLiteral("three-plan")), "text report names the panes", text);
	for (const QStringList& bad : QVector<QStringList> {{QStringLiteral("--panes")}, {QStringLiteral("--panes"), QStringLiteral("top,top")},
			 {QStringLiteral("--panes"), QStringLiteral("bogus")}, {QStringLiteral("--panes="), QStringLiteral("four-views")},
			 {QStringLiteral("--panes"), QStringLiteral("top"), QStringLiteral("--panes"), QStringLiteral("side")},
			 {QStringLiteral("unknown"), QStringLiteral("--panes"), QStringLiteral("front")},
			 {QStringLiteral("four-views"), QStringLiteral("--panes"), QStringLiteral("none")}}) {
		run(bad, 2);
	}
	std::tie(report, text) = run({});
	expect(report.value(QStringLiteral("preference")) == QStringLiteral("single-2d")
			&& report.value(QStringLiteral("planPanes")).toArray() == QJsonArray {QStringLiteral("top")},
		"refused calls write neither preference", text);
	std::tie(report, text) = run({QStringLiteral("camera-above-plans"), QStringLiteral("--panes"), QStringLiteral("all")});
	expect(report.value(QStringLiteral("planPanes")).toArray().size() == 3 && report.value(QStringLiteral("planPanesApply")) == true,
		"all brings every pane back", text);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	identifiers();
	toggling();
	splitterKeys();
	settingsRoundTrip(temp);
	paletteEntries();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp);
	}
	if (ok) {
		std::cout << "Level plan panes: ids, last-pane refusal, splitter keys, settings, palette" << (argc == 2 ? " and CLI" : "") << " passed." << std::endl;
	}
	return ok ? 0 : 1;
}
