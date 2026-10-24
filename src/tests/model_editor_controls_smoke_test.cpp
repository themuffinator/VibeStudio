// Modeller profiles as data: every profile holds together, keys and gestures
// match the reference editors, overrides and shareable files round-trip, the
// sidebars arrange per family, and the CLI reports and changes the same
// controls through an isolated settings file.
#include "core/model_editor_controls.h"
#include "core/model_sidebar.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

QStringList keys(const QString &profile, const QString &command)
{
	return modelEditorCommandKeys(modelEditorControlsForProfile(profile), command);
}

void checkProfiles()
{
	const QStringList ids = modelEditorProfileIds();
	expect(ids == QStringList({QStringLiteral("studio"), QStringLiteral("blender"), QStringLiteral("3ds-max"), QStringLiteral("milkshape-3d")}),
		   "the four modeller profiles are listed in order");
	expect(defaultModelEditorProfileId() == QStringLiteral("studio"), "the studio profile is the default");
	for (const auto &profile : modelEditorProfiles())
	{
		const auto problems = modelEditorControlProblems(profile.controls);
		if (!expect(problems.isEmpty(), "every profile's controls hold together"))
			std::cerr << "  " << profile.id.toStdString() << ": " << problems.join(QStringLiteral(" | ")).toStdString() << '\n';
		expect(!profile.description.isEmpty() && !profile.adaptations.isEmpty() && profile.referenceUrl.startsWith(QStringLiteral("https://")),
			   "each profile describes itself, its differences and its reference");
		expect(profile.controls.profileId == profile.id, "controls carry their profile id");
		for (const auto &alias : profile.aliases)
			expect(normalizedModelEditorProfileId(alias) == profile.id, "aliases resolve to their profile");
	}
	expect(normalizedModelEditorProfileId(QStringLiteral("3DS Max")) == QStringLiteral("3ds-max"), "names normalise with spaces and case");
	expect(normalizedModelEditorProfileId(QStringLiteral("MS3D")) == QStringLiteral("milkshape-3d"), "MilkShape's short name resolves");
	expect(normalizedModelEditorProfileId(QStringLiteral("maya")).isEmpty(), "unknown profiles do not resolve");
	expect(modelEditorControlsForProfile(QStringLiteral("nonsense")).profileId == QStringLiteral("studio"), "unknown ids fall back to studio");
	expect(modelEditorProfileForLevelProfile(QStringLiteral("blender")) == QStringLiteral("blender") &&
			   modelEditorProfileForLevelProfile(QStringLiteral("trenchbroom")) == QStringLiteral("studio"),
		   "a Blender level profile starts the modeller as Blender");
}

void checkKeys()
{
	// Studio and Blender: Blender's edit-mode keys.
	expect(keys(QStringLiteral("studio"), QStringLiteral("meshMove")) == QStringList{QStringLiteral("G")}, "studio G moves");
	expect(keys(QStringLiteral("studio"), QStringLiteral("meshHide")) == QStringList{QStringLiteral("H")}, "studio H hides");
	expect(keys(QStringLiteral("studio"), QStringLiteral("meshToggleEditMode")) == QStringList{QStringLiteral("Tab")}, "studio Tab toggles edit mode");
	expect(keys(QStringLiteral("studio"), QStringLiteral("redoMesh")).contains(QStringLiteral("Ctrl+Y")), "studio also redoes with Ctrl+Y");
	expect(keys(QStringLiteral("blender"), QStringLiteral("redoMesh")) == QStringList{QStringLiteral("Ctrl+Shift+Z")}, "Blender redoes with Shift+Ctrl+Z only");
	expect(keys(QStringLiteral("blender"), QStringLiteral("meshPlay")) == QStringList{QStringLiteral("Space")}, "Blender plays with Space");
	expect(keys(QStringLiteral("blender"), QStringLiteral("meshToggleQuadView")) == QStringList{QStringLiteral("Ctrl+Alt+Q")}, "Blender toggles quad view");
	// 3ds Max: tools, sub-object levels and views.
	expect(keys(QStringLiteral("3ds-max"), QStringLiteral("meshToolMove")) == QStringList{QStringLiteral("W")}, "Max W picks the move tool");
	expect(keys(QStringLiteral("3ds-max"), QStringLiteral("meshMove")).isEmpty(), "Max has no modal grab key");
	expect(keys(QStringLiteral("3ds-max"), QStringLiteral("meshModeFace")) == QStringList{QStringLiteral("4")}, "Max 4 is polygon level");
	expect(keys(QStringLiteral("3ds-max"), QStringLiteral("meshModeBorder")) == QStringList{QStringLiteral("3")}, "Max 3 is border level");
	expect(keys(QStringLiteral("3ds-max"), QStringLiteral("meshHideUnselected")) == QStringList{QStringLiteral("Alt+I")}, "Max hides unselected with Alt+I");
	expect(keys(QStringLiteral("3ds-max"), QStringLiteral("meshViewSelected")) == QStringList{QStringLiteral("Z")}, "Max Z zooms to the selection");
	expect(keys(QStringLiteral("3ds-max"), QStringLiteral("meshSelectNone")) == QStringList{QStringLiteral("Ctrl+D")}, "Max deselects with Ctrl+D");
	expect(keys(QStringLiteral("3ds-max"), QStringLiteral("redoMesh")) == QStringList{QStringLiteral("Ctrl+Y")}, "Max redoes with Ctrl+Y");
	// MilkShape 3D: function-key tools and Ctrl edits.
	expect(keys(QStringLiteral("milkshape-3d"), QStringLiteral("meshToolSelect")) == QStringList{QStringLiteral("F1")}, "MilkShape F1 selects");
	expect(keys(QStringLiteral("milkshape-3d"), QStringLiteral("meshToolScale")) == QStringList{QStringLiteral("F4")}, "MilkShape F4 scales");
	expect(keys(QStringLiteral("milkshape-3d"), QStringLiteral("redoMesh")) == QStringList{QStringLiteral("Ctrl+R")}, "MilkShape redoes with Ctrl+R");
	expect(keys(QStringLiteral("milkshape-3d"), QStringLiteral("meshMergeDistance")) == QStringList{QStringLiteral("Ctrl+W")}, "MilkShape welds with Ctrl+W");
	expect(keys(QStringLiteral("milkshape-3d"), QStringLiteral("meshLoopCut")).isEmpty(), "MilkShape inherits no Blender keys");
	// Keyboard routes to what was pointer-only: loops and rings of the
	// selection (Alt+L and Alt+R, as 3ds Max binds them; MilkShape has no
	// loops) and Next View / Previous View between four views in every profile.
	for (const auto &id : {QStringLiteral("studio"), QStringLiteral("blender"), QStringLiteral("3ds-max")})
		expect(keys(id, QStringLiteral("meshSelectLoop")) == QStringList{QStringLiteral("Alt+L")} &&
				   keys(id, QStringLiteral("meshSelectRing")) == QStringList{QStringLiteral("Alt+R")},
			   "Alt+L and Alt+R grow the selected edges to loops and rings");
	expect(keys(QStringLiteral("milkshape-3d"), QStringLiteral("meshSelectLoop")).isEmpty(), "MilkShape keeps loops in the menus");
	for (const auto &id : modelEditorProfileIds())
		expect(keys(id, QStringLiteral("meshNextView")) == QStringList{QStringLiteral("Ctrl+Alt+PgDown")} &&
				   keys(id, QStringLiteral("meshPreviousView")) == QStringList{QStringLiteral("Ctrl+Alt+PgUp")},
			   "every profile steps the active view with Ctrl+Alt+Page Down and Page Up");
	// Every command a profile lists exists.
	for (const auto &profile : modelEditorProfiles())
	{
		for (const auto &binding : profile.controls.keys)
			expect(modelEditorCommandForId(binding.commandId), "profile keys name real commands");
	}
	const auto effective = modelEditorEffectiveKeys(modelEditorControlsForProfile(QStringLiteral("studio")));
	expect(effective.size() == modelEditorCommands().size(), "every command has an effective key list");
}

void checkGestures()
{
	const auto max = modelEditorControlsForProfile(QStringLiteral("3ds-max"));
	expect(max.navigation.view3D.orbitButton == Qt::MiddleButton && max.navigation.view3D.orbitModifiers == Qt::AltModifier, "Max orbits with Alt+middle");
	expect(max.navigation.zoomButton == Qt::MiddleButton && max.navigation.zoomModifiers == (Qt::ControlModifier | Qt::AltModifier),
		   "Max zooms with Ctrl+Alt+middle");
	expect(max.selection.extendModifiers == Qt::ControlModifier && max.selection.subtractModifiers == Qt::AltModifier, "Max: Ctrl adds, Alt removes");
	expect(max.layout.layout == ModelViewLayout::FourViews && max.layout.panes.value(3) == ModelPaneView::Perspective, "Max starts in four views");
	expect(max.transform.style == ModelTransformStyle::ToolMode && max.transform.duplicateDragModifiers == Qt::ShiftModifier,
		   "Max picks lasting tools and Shift-drags duplicates");
	const auto milk = modelEditorControlsForProfile(QStringLiteral("milkshape-3d"));
	expect(milk.navigation.orthographic.orbitButton == Qt::NoButton && !milk.navigation.orbitLeavesOrthographic, "MilkShape's 2D views never rotate");
	expect(milk.navigation.view3D.orbitButton == Qt::LeftButton && milk.navigation.view3D.leftPanModifiers == Qt::ControlModifier,
		   "MilkShape's 3D view rotates with left drag and pans with Ctrl");
	expect(milk.navigation.zoomButton == Qt::LeftButton && milk.navigation.zoomModifiers == Qt::ShiftModifier && milk.navigation.invertWheel3D,
		   "MilkShape zooms with Shift+left and an inverted 3D wheel");
	expect(milk.selection.subtractDragButton == Qt::RightButton && milk.selection.subtractDragModifiers == Qt::ShiftModifier && !milk.selection.extendToggles,
		   "MilkShape adds with Shift and removes with Shift+right drag");
	const auto blender = modelEditorControlsForProfile(QStringLiteral("blender"));
	expect(blender.navigation.view3D.orbitButton == Qt::MiddleButton && blender.navigation.view3D.panModifiers == Qt::ShiftModifier &&
			   blender.navigation.zoomModifiers == Qt::ControlModifier && !blender.navigation.emulateMiddleButton,
		   "Blender: middle orbits, Shift+middle pans, Ctrl+middle zooms, no emulation");
	expect(modelEditorControlsForProfile(QStringLiteral("studio")).navigation.emulateMiddleButton, "studio orbits with Alt+left too");
	Qt::MouseButton button = Qt::NoButton;
	Qt::KeyboardModifiers modifiers;
	expect(parseModelGesture(QStringLiteral("Ctrl+Alt+Middle"), &button, &modifiers) && button == Qt::MiddleButton &&
			   modifiers == (Qt::ControlModifier | Qt::AltModifier),
		   "gestures parse");
	expect(modelGestureText(Qt::MiddleButton, Qt::ControlModifier | Qt::AltModifier) == QStringLiteral("Ctrl+Alt+Middle"), "gestures format back");
	expect(!parseModelGesture(QStringLiteral("Ctrl+Wheel"), &button, &modifiers), "unknown buttons are refused");
	expect(parseModelModifiers(QStringLiteral("None"), &modifiers) && modifiers == Qt::NoModifier, "None means no keys");
	// Rows describe the scheme in words.
	const auto rows = modelEditorControlRows(max);
	bool orbitRow = false, keyRow = false;
	for (const auto &row : rows)
	{
		orbitRow |= row.section == QStringLiteral("navigation") && !row.gesture.isEmpty();
		keyRow |= row.section == QStringLiteral("keys") && row.gesture == QStringLiteral("W");
	}
	expect(orbitRow && keyRow, "the reference lists gestures and keys");
}

void checkOverrides()
{
	const auto base = modelEditorControlsForProfile(QStringLiteral("blender"));
	const QJsonObject overrides{
		{QStringLiteral("keys"), QJsonObject{{QStringLiteral("meshMove"), QJsonArray{QStringLiteral("W")}}, {QStringLiteral("meshScale"), QJsonArray{}}}},
		{QStringLiteral("navigation"), QJsonObject{{QStringLiteral("orbit"), QStringLiteral("Alt+Left")}, {QStringLiteral("emulateMiddleButton"), true}}},
		{QStringLiteral("selection"), QJsonObject{{QStringLiteral("extend"), QStringLiteral("Ctrl")}}},
		{QStringLiteral("layout"), QJsonObject{{QStringLiteral("layout"), QStringLiteral("four-views")}}},
		{QStringLiteral("bogus"), QJsonObject{}},
	};
	ModelEditorControls edited;
	QStringList warnings;
	expect(applyModelEditorControlOverrides(base, overrides, &edited, &warnings), "overrides apply");
	expect(warnings.size() == 1, "an unknown section is reported");
	expect(modelEditorCommandKeys(edited, QStringLiteral("meshMove")) == QStringList{QStringLiteral("W")} &&
			   modelEditorCommandKeys(edited, QStringLiteral("meshScale")).isEmpty(),
		   "key overrides replace and clear keys");
	expect(edited.navigation.view3D.orbitButton == Qt::LeftButton && edited.navigation.view3D.orbitModifiers == Qt::AltModifier,
		   "gesture overrides apply");
	expect(edited.selection.extendModifiers == Qt::ControlModifier && edited.layout.layout == ModelViewLayout::FourViews, "selection and layout apply");
	const auto diff = modelEditorControlOverrides(base, edited);
	ModelEditorControls again;
	expect(applyModelEditorControlOverrides(base, diff, &again), "the difference applies");
	expect(modelEditorControlsJson(again) == modelEditorControlsJson(edited), "the difference reproduces the edit");
	expect(modelEditorControlOverrides(base, base).isEmpty(), "an unchanged profile has no overrides");
	ModelEditorControls bad;
	QStringList badWarnings;
	expect(applyModelEditorControlOverrides(base,
											QJsonObject{{QStringLiteral("keys"), QJsonObject{{QStringLiteral("noSuchCommand"), QJsonArray{QStringLiteral("K")}}}},
														{QStringLiteral("navigation"), QJsonObject{{QStringLiteral("zoom"), QStringLiteral("Ctrl+Foot")}}}},
											&bad, &badWarnings) &&
			   badWarnings.size() == 2 && bad.navigation.zoomButton == base.navigation.zoomButton,
		   "malformed overrides are skipped with warnings");
	// Shareable files.
	const QByteArray file = modelEditorControlsFile(QStringLiteral("blender"), diff);
	QString profile, error;
	QJsonObject read;
	expect(parseModelEditorControlsFile(file, &profile, &read, &error) && profile == QStringLiteral("blender") && read == diff, "controls files round-trip");
	expect(!parseModelEditorControlsFile("{}", &profile, &read, &error) && !error.isEmpty(), "foreign JSON is refused");
	expect(!parseModelEditorControlsFile("{\"format\":\"vibestudio.modeller-controls\",\"version\":1,\"profile\":\"maya\"}", &profile, &read, &error),
		   "files for unknown profiles are refused");
	// A conflicting customisation is reported.
	auto clash = base;
	clash.keys.append({QStringLiteral("meshInset"), {QStringLiteral("G")}});
	expect(!modelEditorControlProblems(clash).isEmpty(), "a key bound twice is a problem");
}

// The platform's panel keys in portable and native text.
QString focusKey(bool forward)
{
	return modelEditorFocusKeys(forward).value(0);
}
QString nativeFocusKey(bool forward)
{
	return QKeySequence::fromString(focusKey(forward), QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

// Ctrl+Tab and Ctrl+Shift+Tab move focus between panels in every profile, so
// a view that takes Tab never traps keyboard focus. On macOS, where Qt's Ctrl
// is Command and the system keeps Command+Tab, they are Control+Tab (Meta).
void checkFocusKeys()
{
#ifdef Q_OS_MACOS
	const Qt::KeyboardModifier modifier = Qt::MetaModifier;
	const QString portable = QStringLiteral("Meta"), other = QStringLiteral("Ctrl");
#else
	const Qt::KeyboardModifier modifier = Qt::ControlModifier;
	const QString portable = QStringLiteral("Ctrl"), other = QStringLiteral("Meta");
#endif
	const auto combination = [](const QString &key) { return QKeySequence::fromString(key, QKeySequence::PortableText)[0]; };
	expect(modelEditorFocusKeys(true).size() == 1 && modelEditorFocusKeys(false).size() == 1 &&
			   combination(focusKey(true)) == QKeyCombination(modifier, Qt::Key_Tab) &&
			   combination(focusKey(false)) == QKeyCombination(modifier | Qt::ShiftModifier, Qt::Key_Tab),
		   "Ctrl+Tab and Ctrl+Shift+Tab (Control+Tab on macOS) are the focus keys");
	expect(isModelEditorFocusKey(focusKey(true)) && isModelEditorFocusKey(focusKey(false)) &&
			   isModelEditorFocusKey(portable + QStringLiteral("+Shift+Backtab")) && !isModelEditorFocusKey(QStringLiteral("Tab")) &&
			   !isModelEditorFocusKey(QStringLiteral("Shift+Tab")) && !isModelEditorFocusKey(portable + QStringLiteral("+T")) &&
			   !isModelEditorFocusKey(other + QStringLiteral("+Tab")),
		   "any key with the platform's focus modifier and Tab is a focus key, and only that modifier");
	expect(modelEditorFocusKeyText().contains(nativeFocusKey(true)) && modelEditorFocusKeyText().contains(nativeFocusKey(false)),
		   "refusals name both focus keys as the platform shows them");
	expect(isModelEditorTabKey(QStringLiteral("Tab")) && isModelEditorTabKey(QStringLiteral("Shift+Tab")) && !isModelEditorTabKey(QStringLiteral("Ctrl+Tab")) &&
			   !isModelEditorTabKey(QStringLiteral("Alt+Tab")) && !isModelEditorTabKey(QStringLiteral("T")),
		   "plain Tab and Shift+Tab are the view's Tab keys");
	for (const auto &profile : modelEditorProfiles())
	{
		bool forward = false, backward = false;
		for (const auto &row : modelEditorControlRows(profile.controls))
		{
			forward |= row.section == QStringLiteral("keys") && row.gesture == nativeFocusKey(true);
			backward |= row.section == QStringLiteral("keys") && row.gesture == nativeFocusKey(false);
		}
		expect(forward && backward, "every profile's reference lists the focus keys");
		for (const auto &binding : modelEditorEffectiveKeys(profile.controls))
		{
			for (const auto &key : binding.keys)
				expect(!isModelEditorFocusKey(key), "no profile gives a command a focus key");
		}
	}
	expect(keys(QStringLiteral("blender"), QStringLiteral("meshToggleEditMode")) == QStringList{QStringLiteral("Tab")} &&
			   keys(QStringLiteral("3ds-max"), QStringLiteral("meshToggleEditMode")) == QStringList{QStringLiteral("Ctrl+B")},
		   "Blender keeps Tab for edit mode; 3ds Max leaves Tab to focus");
	// Overrides cannot take them: the key is skipped with a warning and any
	// other keys in the list still apply.
	const auto base = modelEditorControlsForProfile(QStringLiteral("studio"));
	ModelEditorControls edited;
	QStringList warnings;
	expect(applyModelEditorControlOverrides(base,
											QJsonObject{{QStringLiteral("keys"),
														 QJsonObject{{QStringLiteral("meshHide"), QJsonArray{focusKey(true), QStringLiteral("J")}},
																	 {QStringLiteral("meshReveal"), QJsonArray{focusKey(false)}}}}},
											&edited, &warnings) &&
			   warnings.size() == 2 && warnings.join(QLatin1Char(' ')).contains(nativeFocusKey(true)) &&
			   warnings.join(QLatin1Char(' ')).contains(QStringLiteral("Hide Selected")) && !warnings.join(QLatin1Char(' ')).contains(QStringLiteral("meshHide")),
		   "overrides that take a focus key are reported by the command's title and the key's own name");
	expect(modelEditorCommandKeys(edited, QStringLiteral("meshHide")) == QStringList{QStringLiteral("J")} &&
			   modelEditorCommandKeys(edited, QStringLiteral("meshReveal")) == QStringList{QStringLiteral("Alt+H")},
		   "a skipped focus key leaves the other keys, or the profile's when it was the only one");
	auto taken = base;
	taken.keys.append({QStringLiteral("meshHide"), {focusKey(true)}});
	const auto problems = modelEditorControlProblems(taken);
	expect(!problems.isEmpty() && problems.join(QLatin1Char(' ')).contains(focusKey(true)), "a focus key on a command is a problem");
}

void checkSidebars()
{
	for (const auto &family : modelSidebarFamilies())
	{
		const auto arrangement = modelSidebarArrangementForFamily(family);
		QString error;
		expect(validateModelSidebarArrangement(arrangement, &error), "every family places every tab once");
		const auto normalized = normalizedModelSidebarArrangement(arrangement, family);
		expect(normalized.leading == arrangement.leading && normalized.trailing == arrangement.trailing, "family arrangements are already normal");
		ModelSidebarArrangement read;
		expect(modelSidebarArrangementFromJson(modelSidebarArrangementJson(arrangement, family), &read) && read.leading == arrangement.leading &&
				   read.trailing == arrangement.trailing,
			   "arrangements round-trip through JSON");
		expect(modelSelectionModeNames(family).size() == 6, "each family names all six selection modes");
	}
	expect(modelSidebarArrangementForFamily(QStringLiteral("milkshape")).leading.isEmpty(), "MilkShape keeps one panel on the right");
	expect(modelSidebarTabTitle(QStringLiteral("item"), QStringLiteral("max")) == QStringLiteral("Modify") &&
			   modelSidebarTabTitle(QStringLiteral("outliner"), QStringLiteral("milkshape")) == QStringLiteral("Groups") &&
			   modelSidebarTabTitle(QStringLiteral("skeleton"), QStringLiteral("blender")) == QStringLiteral("Armature"),
		   "families name tabs their own way");
	expect(modelSelectionModeNames(QStringLiteral("max")).first() == QStringLiteral("Polygon") &&
			   modelSelectionModeNames(QStringLiteral("milkshape")).last() == QStringLiteral("Group"),
		   "families name selection modes their own way");
	ModelSidebarArrangement broken;
	broken.leading = {QStringLiteral("outliner"), QStringLiteral("outliner"), QStringLiteral("ghost")};
	const auto fixed = normalizedModelSidebarArrangement(broken, QStringLiteral("studio"));
	QString error;
	expect(validateModelSidebarArrangement(fixed, &error) && fixed.leading.count(QStringLiteral("outliner")) == 1, "normalising repairs arrangements");
	expect(!validateModelSidebarArrangement(broken, &error) && !error.isEmpty(), "broken arrangements are refused");
}

void checkSettings(const QString &directory)
{
	const QString path = QDir(directory).filePath(QStringLiteral("settings.ini"));
	{
		StudioSettings settings(path);
		expect(settings.modelEditorProfileId() == QStringLiteral("studio"), "an empty store uses the studio profile");
		settings.setSelectedEditorProfileId(QStringLiteral("blender"));
		expect(settings.modelEditorProfileId() == QStringLiteral("blender"), "an unset modeller profile follows a Blender level profile");
		settings.setModelEditorProfileId(QStringLiteral("max"));
		expect(settings.modelEditorProfileId() == QStringLiteral("3ds-max"), "the chosen profile is stored by id");
		QString error;
		expect(settings.setModelEditorControlOverrides(QStringLiteral("3ds-max"),
													   QJsonObject{{QStringLiteral("keys"), QJsonObject{{QStringLiteral("meshToolMove"), QJsonArray{QStringLiteral("M")}}}}},
													   &error),
			   "overrides save");
		settings.sync();
	}
	StudioSettings reread(path, StudioSettings::AccessMode::ReadOnly);
	const auto effective = reread.effectiveModelEditorControls(QStringLiteral("3ds-max"));
	expect(modelEditorCommandKeys(effective, QStringLiteral("meshToolMove")) == QStringList{QStringLiteral("M")}, "saved overrides come back");
	expect(modelEditorCommandKeys(effective, QStringLiteral("meshToolRotate")) == QStringList{QStringLiteral("E")}, "untouched keys keep the profile's");
}

void checkCli(const QString &executable, const QString &directory)
{
	const QString settings = QDir(directory).filePath(QStringLiteral("cli.ini"));
	const auto run = [&](QStringList arguments, int *code = nullptr)
	{
		QProcess process;
		arguments.prepend(QStringLiteral("--cli"));
		arguments << QStringLiteral("--settings-file") << settings;
		process.start(executable, arguments);
		process.waitForFinished(60000);
		if (code)
			*code = process.exitCode();
		return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
	};
	int code = -1;
	const auto profiles = run({QStringLiteral("model"), QStringLiteral("profiles"), QStringLiteral("--json")}, &code);
	expect(code == 0 && profiles.value(QStringLiteral("profiles")).toArray().size() == 4, "model profiles lists four profiles");
	const auto controls =
		run({QStringLiteral("model"), QStringLiteral("controls"), QStringLiteral("--profile"), QStringLiteral("3ds-max"), QStringLiteral("--check"),
			 QStringLiteral("--section"), QStringLiteral("keys"), QStringLiteral("--json")},
			&code);
	expect(code == 0 && controls.value(QStringLiteral("problems")).toArray().isEmpty() && !controls.value(QStringLiteral("rows")).toArray().isEmpty(),
		   "model controls checks a clean profile");
	const QString exported = QDir(directory).filePath(QStringLiteral("max-controls.json"));
	run({QStringLiteral("model"), QStringLiteral("controls"), QStringLiteral("--profile"), QStringLiteral("3ds-max"), QStringLiteral("--export"), exported},
		&code);
	expect(code == 0 && QFile::exists(exported), "model controls exports a file");
	int again = -1;
	run({QStringLiteral("model"), QStringLiteral("controls"), QStringLiteral("--profile"), QStringLiteral("3ds-max"), QStringLiteral("--export"), exported},
		&again);
	expect(again != 0, "exporting over a file needs --overwrite");
	QFile custom(QDir(directory).filePath(QStringLiteral("custom.json")));
	expect(custom.open(QIODevice::WriteOnly) &&
			   custom.write(modelEditorControlsFile(QStringLiteral("milkshape-3d"),
													QJsonObject{{QStringLiteral("keys"), QJsonObject{{QStringLiteral("meshToolMove"), QJsonArray{QStringLiteral("M")}}}}})) > 0,
		   "a custom controls file is written");
	custom.close();
	const auto imported =
		run({QStringLiteral("model"), QStringLiteral("controls"), QStringLiteral("--import"), custom.fileName(), QStringLiteral("--select"), QStringLiteral("--json")},
			&code);
	expect(code == 0 && imported.value(QStringLiteral("profile")).toObject().value(QStringLiteral("id")).toString() == QStringLiteral("milkshape-3d") &&
			   imported.value(QStringLiteral("overrides")).toObject().contains(QStringLiteral("keys")),
		   "model controls imports and selects a profile");
	expect(StudioSettings(settings, StudioSettings::AccessMode::ReadOnly).modelEditorProfileId() == QStringLiteral("milkshape-3d"),
		   "the CLI's choice is the editor's");
	run({QStringLiteral("model"), QStringLiteral("controls"), QStringLiteral("--profile"), QStringLiteral("milkshape-3d"), QStringLiteral("--reset")}, &code);
	expect(code == 0 && StudioSettings(settings, StudioSettings::AccessMode::ReadOnly).modelEditorControlOverrides(QStringLiteral("milkshape-3d")).isEmpty(),
		   "model controls resets a profile");
	// A shared file that gives a command Ctrl+Tab imports without it, saying so.
	QFile focus(QDir(directory).filePath(QStringLiteral("focus.json")));
	expect(focus.open(QIODevice::WriteOnly) &&
			   focus.write(modelEditorControlsFile(QStringLiteral("blender"),
												   QJsonObject{{QStringLiteral("keys"), QJsonObject{{QStringLiteral("meshHide"), QJsonArray{focusKey(true)}}}}})) > 0,
		   "a controls file with a focus key is written");
	focus.close();
	const auto focused = run({QStringLiteral("model"), QStringLiteral("controls"), QStringLiteral("--import"), focus.fileName(), QStringLiteral("--section"),
							  QStringLiteral("keys"), QStringLiteral("--json")},
							 &code);
	bool focusRow = false;
	for (const auto &row : focused.value(QStringLiteral("rows")).toArray())
		focusRow |= row.toObject().value(QStringLiteral("gesture")).toString() == nativeFocusKey(true);
	const auto hide = focused.value(QStringLiteral("controls")).toObject().value(QStringLiteral("keys")).toObject().value(QStringLiteral("meshHide")).toArray();
	expect(code == 0 && focusRow && hide.size() == 1 && hide.first().toString() == QStringLiteral("H") &&
			   focused.value(QStringLiteral("warnings")).toArray().size() == 1 &&
			   focused.value(QStringLiteral("warnings")).toArray().first().toString().contains(nativeFocusKey(true)),
		   "model controls keeps Ctrl+Tab for focus, lists it and warns about the import");
	run({QStringLiteral("model"), QStringLiteral("controls"), QStringLiteral("--profile"), QStringLiteral("blender"), QStringLiteral("--reset")}, &code);
	run({QStringLiteral("model"), QStringLiteral("controls"), QStringLiteral("--profile"), QStringLiteral("maya")}, &code);
	expect(code == 2, "an unknown profile is a usage error");
	const auto formats = run({QStringLiteral("model"), QStringLiteral("formats"), QStringLiteral("--json")}, &code);
	QSet<QString> ids;
	for (const auto &value : formats.value(QStringLiteral("formats")).toArray())
		ids.insert(value.toObject().value(QStringLiteral("id")).toString());
	for (const auto *id : {"mdl", "md2", "md3", "mdc", "mds", "mdm", "mdx", "mdr", "glm", "gla", "iqm", "md5mesh", "md5anim", "lwo", "ase", "studio-mdl",
						   "hexen2-mdl", "fm", "kvx", "obj"})
		expect(ids.contains(QString::fromLatin1(id)), "model formats lists every format");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir directory;
	if (!directory.isValid())
	{
		std::cerr << "Unable to create a temporary directory.\n";
		return EXIT_FAILURE;
	}
	checkProfiles();
	checkKeys();
	checkGestures();
	checkOverrides();
	checkFocusKeys();
	checkSidebars();
	checkSettings(directory.path());
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), directory.path());
	if (failures == 0)
		std::cout << "model editor controls smoke passed\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
