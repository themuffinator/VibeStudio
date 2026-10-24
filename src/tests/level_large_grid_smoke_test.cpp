// Large grid sizes (512 and 1024) without a window: the shared size list,
// profile validation, the Radiant snap-on policy and its gesture override,
// Eureka's digit keys, the palette entries, and the editor CLI that reports
// them. Workflow reference: NRC radiant/grid.cpp at 449778b3; see
// docs/CREDITS.md.

#include "core/editor_profiles.h"
#include "core/level_editor_controls.h"
#include "core/level_gestures.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		ok = false;
		std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n';
	}
	return condition;
}

QStringList keysFor(const LevelEditorControls& controls, const QString& commandId, bool* found = nullptr)
{
	for (const LevelEditorKeyBinding& binding : controls.keys) {
		if (binding.commandId == commandId) {
			if (found) {
				*found = true;
			}
			return binding.keys;
		}
	}
	if (found) {
		*found = false;
	}
	return {};
}

QString rowGesture(const LevelEditorControls& controls, const QString& action)
{
	for (const LevelEditorControlRow& row : levelEditorControlRows(controls)) {
		if (row.action == action) {
			return row.gesture;
		}
	}
	return {};
}

QByteArray fileBytes(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(settingsPath);

	// One shared list, finest first, up to NetRadiant Custom's 1024.
	const QVector<double> sizes{0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
	expect(levelEditorGridSizes() == sizes, "grid sizes run from 0.125 to 1024 in powers of two");
	for (const double units : sizes) {
		expect(levelEditorGridSizeSupported(units), "every listed size is supported", QString::number(units));
	}
	for (const int units : {0, -1, 3, 48, 2048, 4096}) {
		expect(!levelEditorGridSizeSupported(units), "sizes outside the list are refused", QString::number(units));
	}

	// Profile validation accepts every listed default and names the new limit.
	for (const double units : sizes) {
		LevelEditorControls controls = vibeStudioLevelControls();
		controls.defaultGridUnits = units;
		const QStringList problems = levelEditorControlProblems(controls);
		expect(problems.isEmpty(), "a listed default grid validates", QString::number(units) + QLatin1Char(' ') + problems.join(QLatin1Char(';')));
	}
	for (const int units : {0, 3, 2048}) {
		LevelEditorControls controls = vibeStudioLevelControls();
		controls.defaultGridUnits = units;
		const QStringList problems = levelEditorControlProblems(controls);
		expect(problems.size() == 1 && problems.first().contains(QStringLiteral("1024")), "an unlisted default grid names the 1 to 1024 range",
			QString::number(units) + QLatin1Char(' ') + problems.join(QLatin1Char(';')));
	}

	// Radiant's snap-on policy: NetRadiant Custom and the NetRadiant baseline,
	// whose grid.cpp does the same; nobody else.
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		const bool radiant = profile.id == QLatin1String("netradiant-custom") || profile.id == QLatin1String("netradiant");
		expect(profile.controls.gridChangeEnablesSnap == radiant, "only the NetRadiant profiles turn Snap on with a grid change", profile.id);
		expect(levelEditorGridSizeSupported(profile.controls.defaultGridUnits), "every profile starts on a listed grid", profile.id);
		expect(rowGesture(profile.controls, QStringLiteral("Changing the grid"))
				== (radiant ? QStringLiteral("Turns Snap on") : QStringLiteral("Leaves Snap as it was")),
			"the controls reference states the snap policy", profile.id);
		expect(levelGestureValues(profile.controls).value(QStringLiteral("editing.gridChangeEnablesSnap")).toString()
				== (radiant ? QStringLiteral("on") : QStringLiteral("off")),
			"the gesture value mirrors the profile policy", profile.id);
	}

	// The policy is a portable gesture preference in both directions.
	{
		QString error;
		LevelEditorControls effective;
		QJsonObject normalized;
		expect(applyLevelGestureOverrides(netRadiantCustomLevelControls(), {{QStringLiteral("editing.gridChangeEnablesSnap"), QStringLiteral("off")}},
				   &effective, &normalized, &error)
				&& !effective.gridChangeEnablesSnap && normalized.value(QStringLiteral("editing.gridChangeEnablesSnap")) == QStringLiteral("off"),
			"NRC users can keep Snap as it was", error);
		expect(applyLevelGestureOverrides(trenchBroomLevelControls(), {{QStringLiteral("editing.gridChangeEnablesSnap"), QStringLiteral("on")}},
				   &effective, &normalized, &error)
				&& effective.gridChangeEnablesSnap,
			"other profiles can opt in", error);
		expect(applyLevelGestureOverrides(netRadiantCustomLevelControls(), {{QStringLiteral("editing.gridChangeEnablesSnap"), QStringLiteral("on")}},
				   &effective, &normalized, &error)
				&& normalized.isEmpty(),
			"the profile default normalises away", error);
		expect(!applyLevelGestureOverrides(netRadiantCustomLevelControls(), {{QStringLiteral("editing.gridChangeEnablesSnap"), QStringLiteral("maybe")}},
				   &effective, nullptr, &error),
			"an unknown policy value is refused");
		bool listed = false;
		for (const LevelGestureField& field : levelGestureFields()) {
			listed |= field.id == QLatin1String("editing.gridChangeEnablesSnap") && field.section == QLatin1String("editing")
				&& field.options.size() == 2 && !field.label.isEmpty();
		}
		expect(listed, "the policy is listed with the editing preferences");
	}

	// Upstream gives 512 and 1024 no default key in Radiant or TrenchBroom;
	// Eureka's bindings.cfg puts them on 9 and 0.
	for (const char* id : {"netradiant-custom", "trenchbroom", "gtkradiant-1-6", "vibestudio-default"}) {
		const LevelEditorControls controls = levelEditorControlsForProfile(QString::fromLatin1(id));
		bool found512 = true, found1024 = true;
		keysFor(controls, QStringLiteral("map.grid512"), &found512);
		keysFor(controls, QStringLiteral("map.grid1024"), &found1024);
		expect(!found512 && !found1024, "no default key for the large grids", QString::fromLatin1(id));
	}
	{
		EditorProfileDescriptor eureka;
		expect(editorProfileForId(QStringLiteral("eureka"), &eureka), "Eureka profile");
		const QHash<QString, QString> digits{{QStringLiteral("map.grid2"), QStringLiteral("1")}, {QStringLiteral("map.grid4"), QStringLiteral("2")},
			{QStringLiteral("map.grid8"), QStringLiteral("3")}, {QStringLiteral("map.grid16"), QStringLiteral("4")},
			{QStringLiteral("map.grid32"), QStringLiteral("5")}, {QStringLiteral("map.grid64"), QStringLiteral("6")},
			{QStringLiteral("map.grid128"), QStringLiteral("7")}, {QStringLiteral("map.grid256"), QStringLiteral("8")},
			{QStringLiteral("map.grid512"), QStringLiteral("9")}, {QStringLiteral("map.grid1024"), QStringLiteral("0")}};
		for (auto it = digits.cbegin(); it != digits.cend(); ++it) {
			expect(keysFor(eureka.controls, it.key()) == QStringList{it.value()}, "Eureka grid digit", it.key());
			EditorProfileBinding binding;
			expect(editorProfileBindingForAction(eureka, it.key(), &binding) && binding.implemented && binding.shortcut == it.value(),
				"Eureka's grid digit is a routed binding", it.key() + QLatin1Char(' ') + binding.shortcut);
		}
		bool grid1 = true;
		keysFor(eureka.controls, QStringLiteral("map.grid1"), &grid1);
		expect(!grid1, "Eureka has no 1-unit grid key");
		expect(levelEditorControlProblems(eureka.controls).isEmpty(), "Eureka's digits do not clash",
			levelEditorControlProblems(eureka.controls).join(QLatin1Char(';')));
		QString conflict;
		expect(!editorProfileHasShortcutConflict(eureka, &conflict), "Eureka bindings stay conflict-free", conflict);
		const QString adaptations = eureka.adaptations.join(QLatin1Char(' '));
		expect(adaptations.contains(QStringLiteral("1024")) && !adaptations.contains(QStringLiteral("512/1024")),
			"Eureka's adaptation note no longer lists the large grids as missing", adaptations);
	}

	// The palette and the shortcut registry know both commands and the new top.
	for (const int units : {512, 1024}) {
		const QString id = QStringLiteral("map.grid%1").arg(units);
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && entry.label == QStringLiteral("Grid %1").arg(units)
				&& entry.id == QStringLiteral("map-grid-%1").arg(units) && entry.summary.contains(QString::number(units)),
			"palette entry", id + QLatin1Char(' ') + entry.label + QLatin1Char(' ') + entry.summary);
		expect(shellCommandIdExists(id), "registered shell command", id);
	}
	ShortcutDescriptor larger;
	expect(shortcutForCommandId(QStringLiteral("map.grid-larger"), &larger) && larger.description.contains(QStringLiteral("1024"))
			&& !larger.description.contains(QStringLiteral("256")),
		"Larger Grid says where it stops", larger.description);
	QStringList conflicts;
	expect(!shortcutRegistryHasConflicts(&conflicts), "shortcut registry stays conflict-free", conflicts.join(QLatin1Char(';')));

	// The editor CLI reports the size list and the policy, and its gesture
	// route changes the policy per profile without touching other profiles.
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto cli = [&](const QStringList& arguments, int expected) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), settingsPath, QStringLiteral("editor")}
					+ arguments + QStringList{QStringLiteral("--json")});
			const bool finished = process.waitForFinished(60000);
			const QByteArray out = process.readAllStandardOutput();
			expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI exit",
				arguments.join(QLatin1Char(' ')) + QLatin1Char(' ') + QString::fromUtf8(out + process.readAllStandardError()));
			return QJsonDocument::fromJson(out).object();
		};
		QJsonArray expectedSizes;
		for (const double units : sizes) {
			expectedSizes.append(units);
		}
		const QJsonObject nrc = cli({QStringLiteral("controls"), QStringLiteral("netradiant-custom")}, 0).value(QStringLiteral("controls")).toObject();
		expect(nrc.value(QStringLiteral("gridSizes")).toArray() == expectedSizes && nrc.value(QStringLiteral("gridChangeTurnsSnapOn")).toBool()
				&& nrc.value(QStringLiteral("problems")).toArray().isEmpty(),
			"editor controls reports the grid list and NRC's snap policy", QString::fromUtf8(QJsonDocument(nrc).toJson(QJsonDocument::Compact)).left(400));
		const QJsonObject tb = cli({QStringLiteral("controls"), QStringLiteral("trenchbroom")}, 0).value(QStringLiteral("controls")).toObject();
		expect(tb.value(QStringLiteral("gridSizes")).toArray() == expectedSizes && !tb.value(QStringLiteral("gridChangeTurnsSnapOn")).toBool(true),
			"TrenchBroom leaves Snap alone");
		const QByteArray before = fileBytes(settingsPath);
		const QJsonObject preview = cli({QStringLiteral("gestures"), QStringLiteral("nrc"), QStringLiteral("--set"), QStringLiteral("editing.gridChangeEnablesSnap=off"),
			QStringLiteral("--dry-run")}, 0);
		expect(preview.value(QStringLiteral("values")).toObject().value(QStringLiteral("editing.gridChangeEnablesSnap")) == QStringLiteral("off")
				&& fileBytes(settingsPath) == before,
			"a dry run previews the policy without writing");
		cli({QStringLiteral("gestures"), QStringLiteral("nrc"), QStringLiteral("--set"), QStringLiteral("editing.gridChangeEnablesSnap=off")}, 0);
		expect(!cli({QStringLiteral("controls"), QStringLiteral("netradiant-custom")}, 0).value(QStringLiteral("controls")).toObject()
					.value(QStringLiteral("gridChangeTurnsSnapOn")).toBool(true)
				&& !cli({QStringLiteral("controls"), QStringLiteral("trenchbroom")}, 0).value(QStringLiteral("controls")).toObject()
					.value(QStringLiteral("gridChangeTurnsSnapOn")).toBool(true),
			"the saved preference changes only that profile's policy");
		cli({QStringLiteral("gestures"), QStringLiteral("nrc"), QStringLiteral("--set"), QStringLiteral("editing.gridChangeEnablesSnap=maybe")}, 4);
		cli({QStringLiteral("gestures"), QStringLiteral("nrc"), QStringLiteral("--set"), QStringLiteral("editing.gridChangeEnablesSnap=default")}, 0);
		expect(cli({QStringLiteral("controls"), QStringLiteral("netradiant-custom")}, 0).value(QStringLiteral("controls")).toObject()
				   .value(QStringLiteral("gridChangeTurnsSnapOn")).toBool(),
			"default restores NRC's policy");
		const QJsonObject eureka = cli({QStringLiteral("controls"), QStringLiteral("eureka")}, 0).value(QStringLiteral("controls")).toObject();
		bool row512 = false, row1024 = false;
		for (const QJsonValue& value : eureka.value(QStringLiteral("rows")).toArray()) {
			const QJsonObject row = value.toObject();
			row512 |= row.value(QStringLiteral("action")).toString() == QStringLiteral("Grid 512") && row.value(QStringLiteral("gesture")).toString() == QStringLiteral("9");
			row1024 |= row.value(QStringLiteral("action")).toString() == QStringLiteral("Grid 1024") && row.value(QStringLiteral("gesture")).toString() == QStringLiteral("0");
		}
		expect(row512 && row1024, "Eureka's controls reference names the large-grid keys");
	}

	if (!ok) {
		std::cerr << "level large grid smoke test failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "level large grid smoke test passed\n";
	return EXIT_SUCCESS;
}
