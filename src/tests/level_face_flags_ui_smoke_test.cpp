// Offscreen checks of the Surfaces tab's Flags section, Clear Face Flags and
// the Show Surfaces toggle, under the TrenchBroom profile at 100% and at 200%
// text with the high-visibility theme, an expanding translator and
// right-to-left layout, and under NetRadiant Custom. On a Quake II map the
// section names the game's flags, ticks those on every targeted face and
// partly ticks those on some, shows a shared value or "Mixed", and keeps
// detail read-only; a box, the Value field and Clear Face Flags each make the
// shared service's exact edit as one undo step, and the boxes follow undo.
// Hidden brushes are not targets. A Quake III map adds its own names and a
// note about its compiler, and a Quake map shows no boxes. Show Surfaces is
// checkable: it shows the tab, and hides it again back to the tab it replaced
// or by folding the sidebar it opened; in another mode it is unchecked and
// shows the tab, still remembering that tab. NetRadiant Custom binds it to S, which
// toggles the tab from a 2D view; other profiles leave it without a key.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/level_face_flags.h"
#include "core/studio_semantics.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

void settle(int milliseconds = 50)
{
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

// A Quake II map whose first face differs: window and player clip contents,
// light and sky surface flags and value 9; the others window, sky and 8.
QByteArray mixedQuake2()
{
	QByteArray bytes = tests::surfaceFixture(QStringLiteral("classic"));
	const qsizetype first = bytes.indexOf(" 2 4 8 // flags");
	bytes.replace(first, 15, " 65538 5 9 // flags");
	return bytes;
}

} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only; no system input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	QString error;
	const QString quake2 = temp.filePath(QStringLiteral("quake2.map"));
	const QString quake3 = temp.filePath(QStringLiteral("quake3.map"));
	const QString quake = temp.filePath(QStringLiteral("quake.map"));
	expect(tests::putMaterialFile(quake2, mixedQuake2()) && tests::putMaterialFile(quake3, tests::surfaceFixture(QStringLiteral("brushDef"))), "fixtures");
	{
		LevelMapDocument bare;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake");
		create.starterRoom = false;
		expect(createLevelMap(create, &bare, &error) && addLevelMapBoxBrush(&bare, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("wall"), nullptr, &error)
				&& tests::putMaterialFile(quake, serializeLevelMap(bare).bytes),
			"a Quake map", error);
	}
	for (const char* id : {"map.surfaceClearFlags", "map.sidebar.surfaces"}) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", QString::fromLatin1(id));
	}

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"trenchbroom", 100}, Pass {"trenchbroom", 200}, Pass {"netradiant-custom", 100}}) {
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Face flags UI " << qPrintable(phase) << std::endl;
		const bool nrc = QLatin1String(pass.profile) == QLatin1String("netradiant-custom");
		settings.setSelectedEditorProfileId(QString::fromLatin1(pass.profile));
		settings.setTextScalePercent(pass.scale);
		settings.setTheme(pass.scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, pass.scale));
		Expanded expanded;
		if (pass.scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		{
			ApplicationShell shell;
			shell.resize(pass.scale == 100 ? 1650 : 2400, pass.scale == 100 ? 1000 : 1500);
			QComboBox* engine = nullptr;
			for (auto* combo : shell.findChildren<QComboBox*>()) {
				if (combo->findData(QStringLiteral("idtech3")) >= 0 && combo->findData(QStringLiteral("idtech1")) >= 0) {
					engine = combo;
				}
			}
			const auto open = [&](const QString& path, const QString& family) {
				if (engine) {
					engine->setCurrentIndex(engine->findData(family));
				}
				shell.openPathFromCommandLine(path);
				return until([&] { return shell.levelDocument().sourcePath == path && !shell.levelDocument().brushes.isEmpty(); });
			};
			shell.show();
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			if (!expect(open(quake2, QStringLiteral("idtech2")), "Quake II map ready")) {
				return 1;
			}
			action(QStringLiteral("shell.mode.levels"))->trigger();
			if (!expect(until([&] { return shell.findChildren<MapViewport*>().size() >= 3; }), "plan views")) {
				return 1;
			}
			MapViewport* top = nullptr;
			for (auto* pane : shell.findChildren<MapViewport*>()) {
				if (pane->projection() == MapViewportProjection::TopXY) {
					top = pane;
				}
			}
			auto* tools = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>(QStringLiteral("levelSurfaceTools")));
			StudioSidebar* sidebar = nullptr;
			for (auto* candidate : shell.findChildren<StudioSidebar*>()) {
				if (candidate->indexOfPage(QStringLiteral("surfaces")) >= 0) {
					sidebar = candidate;
				}
			}
			if (!expect(top && tools && sidebar, "top view, Surfaces tab and its sidebar")) {
				return 1;
			}
			const auto box = [&](const QString& name) { return tools->findChild<QCheckBox*>(name); };
			auto* summary = tools->findChild<QLabel*>(QStringLiteral("surfaceFlagsSummary"));
			auto* value = tools->findChild<QLineEdit*>(QStringLiteral("surfaceFlagValue"));
			auto* body = tools->findChild<QWidget*>(QStringLiteral("surfaceFlagsBody"));
			auto* note = tools->findChild<QLabel*>(QStringLiteral("surfaceFlagsNote"));
			auto* clearButton = tools->findChild<QToolButton*>(QStringLiteral("map.surfaceClearFlags"));
			QAction* clear = action(QStringLiteral("map.surfaceClearFlags"));
			QAction* show = action(QStringLiteral("map.sidebar.surfaces"));
			if (!expect(summary && value && body && note && clearButton && clear && show, "the Flags section's controls and commands")) {
				return 1;
			}
			const int brushId = shell.levelDocument().brushes.first().id;
			QVector<LevelSurfaceFace> faces;
			for (int i = 0; i < shell.levelDocument().brushes.first().faces.size(); ++i) {
				faces.append({brushId, i});
			}
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto select = [&](bool brush) {
				const QVector<LevelMapSelectionRef> refs = brush ? QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakeBrush, brushId}}
																 : QVector<LevelMapSelectionRef> {};
				top->setSelectionSet(refs);
				top->selectionSetChanged(refs);
			};
			const auto reference = [&](const LevelFaceFlagsEdit& edit) {
				LevelMapDocument expected = shell.levelDocument();
				QString problem;
				expect(setLevelMapFaceFlags(&expected, faces, edit, nullptr, &problem), "reference edit", problem);
				return serializeLevelMap(expected).bytes;
			};
			// Runs `edit`, checks the map is then `wanted` with one more undo
			// step, and undoes it back to the exact bytes.
			const auto check = [&](const QByteArray& wanted, const std::function<void()>& edit, const QString& label) {
				const QByteArray start = bytes();
				const auto depth = shell.levelDocument().undoStack.size();
				edit();
				expect(bytes() == wanted && shell.levelDocument().undoStack.size() == depth + 1, "the shared service's edit, as one undo step",
					label + QLatin1Char(' ') + shell.statusBar()->currentMessage());
				action(QStringLiteral("map.undo"))->trigger();
				expect(bytes() == start && shell.levelDocument().undoStack.size() == depth, "undo restores the exact map", label);
			};

			// Registration.
			bool inMenu = false;
			for (auto* owner : clear->associatedObjects()) {
				inMenu |= qobject_cast<QMenu*>(owner) != nullptr;
			}
			auto* tile = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.surfaceClearFlags"));
			expect(!clear->text().isEmpty() && !clear->statusTip().isEmpty() && inMenu && tile && QAccessible::queryAccessibleInterface(tile)
					&& clear->shortcuts().isEmpty(),
				"Clear Face Flags is labelled, in a menu and on the Tools tab, with no key");
			expect(clearButton->accessibleName() == commandLabelWithoutMnemonic(clear->text()) && clearButton->focusPolicy() != Qt::NoFocus,
				"the Clear Flags button is named after its command");
			expect(show->isCheckable() && (nrc ? show->shortcuts().contains(QKeySequence(QStringLiteral("S"))) : show->shortcuts().isEmpty()),
				"Show Surfaces toggles; NetRadiant Custom binds S", show->shortcut().toString());

			// Nothing selected: the boxes wait.
			select(false);
			shell.showLevelSidebarTab(QStringLiteral("surfaces"));
			settle();
			auto* light = box(QStringLiteral("surfaceFlag-light"));
			auto* sky = box(QStringLiteral("surfaceFlag-sky"));
			auto* window = box(QStringLiteral("contentFlag-window"));
			auto* playerclip = box(QStringLiteral("contentFlag-playerclip"));
			auto* detail = box(QStringLiteral("contentFlag-detail"));
			if (!expect(light && sky && window && playerclip && detail, "Quake II flag boxes")) {
				return 1;
			}
			expect(body->isVisible() && !light->isEnabled() && !value->isEnabled() && !clear->isEnabled() && !summary->text().isEmpty(),
				"with nothing targeted the boxes are disabled", summary->text());

			// The brush: ticked on every face, partly ticked on some.
			select(true);
			settle();
			expect(sky->checkState() == Qt::Checked && window->checkState() == Qt::Checked && light->checkState() == Qt::PartiallyChecked
					&& playerclip->checkState() == Qt::PartiallyChecked && box(QStringLiteral("contentFlag-water"))->checkState() == Qt::Unchecked,
				"the boxes show every face's and some faces' flags");
			expect(value->text().isEmpty() && !value->placeholderText().isEmpty(), "a mixed value shows as Mixed", value->placeholderText());
			expect(light->isEnabled() && !detail->isEnabled() && clear->isEnabled() && value->isEnabled() && summary->text().contains(QLatin1Char('6')),
				"the boxes are enabled for six faces; detail is read-only", summary->text());
			{
				QAccessibleInterface* face = QAccessible::queryAccessibleInterface(light);
				expect(face && face->role() == QAccessible::CheckBox && face->state().checkable && face->state().checkStateMixed
						&& !light->accessibleName().isEmpty() && !light->accessibleDescription().isEmpty() && light->focusPolicy() != Qt::NoFocus,
					"a partly ticked box is accessible as mixed", light->accessibleName());
			}
			// VIBESTUDIO_FACE_FLAGS_GRAB=<folder> saves the tab for a look.
			if (const QString grab = qEnvironmentVariable("VIBESTUDIO_FACE_FLAGS_GRAB"); !grab.isEmpty()) {
				tools->ensureWidgetVisible(body);
				settle();
				tools->grab().save(QDir(grab).filePath(QStringLiteral("flags-%1-%2.png").arg(QLatin1String(pass.profile)).arg(pass.scale)));
			}
			// Every box fits its column, whatever the text size or direction.
			bool fits = true;
			for (auto* each : body->findChildren<QCheckBox*>()) {
				fits &= each->sizeHint().width() <= each->width() + 2 && each->geometry().right() <= each->parentWidget()->width();
			}
			expect(fits, "every flag box fits its column");

			// A partly ticked box ticks on every face; a ticked one clears.
			LevelFaceFlagsEdit edit;
			edit.setContent = 0x10000;
			check(reference(edit), [&] { playerclip->click(); }, QStringLiteral("tick player clip"));
			expect(until([&] { return playerclip->checkState() == Qt::PartiallyChecked; }), "the box follows undo");
			edit = {};
			edit.clearSurface = 0x4;
			check(reference(edit), [&] { sky->click(); }, QStringLiteral("clear sky"));
			expect(sky->checkState() == Qt::Checked, "sky is ticked again after undo");
			// The value goes to every face.
			edit = {};
			edit.setValue = true;
			edit.value = 300;
			check(reference(edit),
				[&] {
					value->setText(QStringLiteral("300"));
					emit value->editingFinished();
				},
				QStringLiteral("value"));
			// Clear Face Flags.
			check(reference(LevelFaceFlagsEdit::clearAll()), [&] { clear->trigger(); }, QStringLiteral("Clear Face Flags"));
			expect(tools->findChild<QLabel*>(QStringLiteral("surfaceQuickStatus")) != nullptr, "status label");

			// Hidden brushes are not targets.
			action(QStringLiteral("map.hideSelection"))->trigger();
			expect(until([&] { return !light->isEnabled() && !clear->isEnabled(); }), "a hidden brush is not a target");
			action(QStringLiteral("map.showAll"))->trigger();
			settle();

			// Show Surfaces shows the tab, and hides it again.
			select(false);
			shell.showLevelSidebarTab(QStringLiteral("inspector"));
			settle();
			expect(!show->isChecked() && sidebar->currentPageId() == QStringLiteral("inspector"), "another tab is showing");
			show->trigger();
			expect(sidebar->currentPageId() == QStringLiteral("surfaces") && !sidebar->isFolded() && show->isChecked(), "Show Surfaces shows the tab");
			show->trigger();
			expect(sidebar->currentPageId() == QStringLiteral("inspector") && !sidebar->isFolded() && !show->isChecked(),
				"Show Surfaces again goes back to the tab it replaced");
			// In another mode the tab cannot be seen, so the command is not
			// checked and shows it; the tab it replaced is still remembered.
			show->trigger();
			QAction* workspace = action(QStringLiteral("shell.mode.workspace"));
			if (expect(workspace != nullptr, "the Workspace mode command")) {
				workspace->trigger();
				settle();
				expect(!show->isChecked() && sidebar->currentPageId() == QStringLiteral("surfaces"), "unchecked while Levels is not showing");
				show->trigger();
				expect(show->isChecked() && sidebar->currentPageId() == QStringLiteral("surfaces") && !sidebar->isFolded(),
					"from another mode Show Surfaces shows the tab in Levels");
				show->trigger();
				expect(sidebar->currentPageId() == QStringLiteral("inspector") && !sidebar->isFolded() && !show->isChecked(),
					"and hiding it still goes back to the tab it replaced");
			}
			sidebar->setFolded(true);
			show->trigger();
			expect(sidebar->currentPageId() == QStringLiteral("surfaces") && !sidebar->isFolded() && show->isChecked(), "it opens a folded sidebar");
			show->trigger();
			expect(sidebar->isFolded() && !show->isChecked(), "and folds it again");
			sidebar->setFolded(false);
#ifdef VIBESTUDIO_HAVE_QTEST
			if (nrc) {
				// S in a 2D view toggles the tab, as in NetRadiant Custom.
				shell.showLevelSidebarTab(QStringLiteral("inspector"));
				shell.activateWindow();
				expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
				top->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return top->hasFocus(); }), "the 2D view takes focus");
				QTest::keyClick(top, Qt::Key_S);
				expect(until([&] { return sidebar->currentPageId() == QStringLiteral("surfaces"); }), "S shows the Surfaces tab", sidebar->currentPageId());
				top->setFocus(Qt::OtherFocusReason);
				QTest::keyClick(top, Qt::Key_S);
				expect(until([&] { return sidebar->currentPageId() == QStringLiteral("inspector"); }), "S hides it again", sidebar->currentPageId());
			}
#endif

			// Quake III: its own names and a note on what its compiler reads.
			if (expect(open(quake3, QStringLiteral("idtech3")), "Quake III map ready")) {
				shell.showLevelSidebarTab(QStringLiteral("surfaces"));
				const int id = shell.levelDocument().brushes.first().id;
				top->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, id}});
				top->selectionSetChanged({{LevelMapSelectionKind::QuakeBrush, id}});
				settle();
				auto* botclip = box(QStringLiteral("contentFlag-botclip"));
				auto* nodraw = box(QStringLiteral("surfaceFlag-nodraw"));
				expect(botclip && nodraw && !box(QStringLiteral("surfaceFlag-light")) && note->isVisible() && !note->text().isEmpty() && value->text() == QStringLiteral("8"),
					"Quake III names, its note and the shared value");
				if (nodraw) {
					faces.clear();
					for (int i = 0; i < shell.levelDocument().brushes.first().faces.size(); ++i) {
						faces.append({id, i});
					}
					edit = {};
					edit.setSurface = 0x80;
					check(reference(edit), [&] { nodraw->click(); }, QStringLiteral("Quake III nodraw"));
				}
			}
			// Quake: no flags.
			if (expect(open(quake, QStringLiteral("idtech2")), "Quake map ready")) {
				settle();
				expect(!body->isVisible() && !summary->text().isEmpty() && tools->findChildren<QCheckBox*>(QRegularExpression(QStringLiteral("Flag-"))).isEmpty(),
					"a Quake map shows no flag boxes", summary->text());
			}
			shell.close();
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Face flags UI checks passed" : "Face flags UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
