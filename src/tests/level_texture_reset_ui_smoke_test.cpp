// Offscreen checks of the texture resets on the Levels page, under the
// TrenchBroom profile at 100% and at 200% text with the high-visibility theme,
// an expanding translator and right-to-left layout, and under NetRadiant
// Custom. Reset Texture Alignment, Radiant Texture Reset and Reset Textures…
// are registered, labelled, in the Surfaces menu, the Tools tab, the Surfaces
// tab and the palette; TrenchBroom binds Shift+R and Shift+Alt+R (Natural) and
// NetRadiant Custom Shift+N. Each reset makes the shared service's exact edit
// as one undo step, from the menu command and from its key; the Reset Textures
// window is accessible, refuses a request that changes nothing and applies
// its ticked values; the whole map leaves hidden objects alone.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/level_texture_reset_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_editor_controls.h"
#include "core/level_visibility.h"
#include "core/studio_semantics.h"
#include "tests/level_surface_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <cmath>
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
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
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
	// Valve 220 faces, where the two resets differ.
	const QString path = temp.filePath(QStringLiteral("reset.map"));
	{
		QFile file(path);
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(tests::surfaceFixture(QStringLiteral("valve220"))) > 0, "fixture")) {
			return 1;
		}
	}
	const QStringList ids {QStringLiteral("map.surfaceReset"), QStringLiteral("map.surfaceRadiantReset"), QStringLiteral("map.resetTextures")};
	for (const auto& id : ids) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", id);
	}

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"trenchbroom", 100}, Pass {"trenchbroom", 200}, Pass {"netradiant-custom", 100}}) {
		const bool tb = QLatin1String(pass.profile) == QLatin1String("trenchbroom");
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Texture reset UI " << qPrintable(phase) << std::endl;
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
			shell.show();
			shell.openPathFromCommandLine(path);
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			action(QStringLiteral("shell.mode.levels"))->trigger();
			auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
			if (!expect(top && until([&] { return shell.levelDocument().sourcePath == path && !shell.levelDocument().brushes.isEmpty(); }), "map ready")) {
				return 1;
			}
			const int brushId = shell.levelDocument().brushes.first().id;
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto settle = [&] { return until([&] { return !shell.levelSurfaceEditsPending(); }); };
			const auto select = [&](bool brush) {
				const QVector<LevelMapSelectionRef> refs = brush ? QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakeBrush, brushId}}
																 : QVector<LevelMapSelectionRef> {};
				top->setSelectionSet(refs);
				top->selectionSetChanged(refs);
			};
			const double scale = levelSurfaceDefaultScale(shell.levelDocument());
			const auto reference = [&](LevelSurfaceOperation operation) {
				auto expected = shell.levelDocument();
				QVector<LevelSurfaceFace> faces;
				for (int i = 0; i < expected.brushes.first().faces.size(); ++i) {
					faces.append({brushId, i});
				}
				LevelSurfaceRequest request;
				request.operation = operation;
				request.x = request.y = scale;
				LevelSurfaceEditPlan plan;
				QString problem;
				expect(prepareLevelSurfaceEdits(expected, faces, {}, {request}, {}, &plan, &problem) && plan.faceCount() > 0
						&& commitLevelSurfaceEdit(&expected, plan, &problem),
					"reference reset", problem);
				return serializeLevelMap(expected).bytes;
			};
			// Runs `edit`, checks the map is then `wanted` with one more undo
			// step, and undoes it back to the exact bytes.
			const auto check = [&](const QByteArray& wanted, const std::function<void()>& edit, const QString& label) {
				const QByteArray start = bytes();
				const auto startDepth = shell.levelDocument().undoStack.size();
				edit();
				const bool settled = settle();
				expect(settled && bytes() == wanted && shell.levelDocument().undoStack.size() == startDepth + 1, "the shared service's edit, as one undo step",
					label + QLatin1Char(' ') + shell.statusBar()->currentMessage());
				action(QStringLiteral("map.undo"))->trigger();
				expect(settle() && bytes() == start && shell.levelDocument().undoStack.size() == startDepth, "undo restores the exact map", label);
			};

			// Registration: menus, the Tools tab, the Surfaces tab and accessibility.
			select(false);
			for (const auto& id : ids) {
				QAction* command = action(id);
				if (!expect(command != nullptr, "registered", id)) {
					return 1;
				}
				bool menu = false;
				for (auto* owner : command->associatedObjects()) {
					menu |= qobject_cast<QMenu*>(owner) != nullptr;
				}
				auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
				expect(!command->text().isEmpty() && !command->statusTip().isEmpty() && menu, "labelled and in a menu", id);
				expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
					"accessible Tools tile", id);
			}
			for (const auto& id : {QStringLiteral("map.surfaceReset"), QStringLiteral("map.surfaceRadiantReset")}) {
				auto* button = shell.findChild<QToolButton*>(id);
				expect(button && !button->accessibleName().isEmpty() && !action(id)->isEnabled(), "a Surfaces tab button, disabled with nothing to reset", id);
			}
			expect(action(QStringLiteral("map.resetTextures"))->isEnabled(), "Reset Textures can always reach the whole map");
			const auto keys = [&](const QString& id) { return action(id)->shortcuts(); };
			if (tb) {
				expect(keys(QStringLiteral("map.surfaceReset")) == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::Key_R)}
						&& keys(QStringLiteral("map.surfaceNatural")) == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::AltModifier | Qt::Key_R)}
						&& keys(QStringLiteral("map.surfaceRadiantReset")).isEmpty(),
					"TrenchBroom's reset keys", QKeySequence::listToString(keys(QStringLiteral("map.surfaceReset"))));
			} else {
				expect(keys(QStringLiteral("map.surfaceRadiantReset")) == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::Key_N)}
						&& keys(QStringLiteral("map.surfaceReset")).isEmpty() && keys(QStringLiteral("map.rotatePrecisely")) == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::Key_R)},
					"NetRadiant Custom's reset key", QKeySequence::listToString(keys(QStringLiteral("map.surfaceRadiantReset"))));
			}

			// The menu commands on the selected brush.
			select(true);
			expect(action(QStringLiteral("map.surfaceReset"))->isEnabled() && action(QStringLiteral("map.surfaceRadiantReset"))->isEnabled(), "enabled with a brush selected");
			const QByteArray tbReset = reference(LevelSurfaceOperation::Reset), radiantReset = reference(LevelSurfaceOperation::RadiantReset);
			expect(tbReset != radiantReset, "the resets differ on Valve 220 faces");
			check(tbReset, [&] { action(QStringLiteral("map.surfaceReset"))->trigger(); }, QStringLiteral("Reset Texture Alignment"));
			check(radiantReset, [&] { action(QStringLiteral("map.surfaceRadiantReset"))->trigger(); }, QStringLiteral("Radiant Texture Reset"));

#ifdef VIBESTUDIO_HAVE_QTEST
			{
				// The profile's key in a plan view.
				shell.activateWindow();
				expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
				top->setFocus(Qt::OtherFocusReason);
				if (expect(until([&] { return top->hasFocus(); }), "plan view focus")) {
					check(tb ? tbReset : radiantReset, [&] { QTest::keyClick(top, tb ? Qt::Key_R : Qt::Key_N, Qt::ShiftModifier); },
						tb ? QStringLiteral("Shift+R") : QStringLiteral("Shift+N"));
				}
			}
#endif

			// The Reset Textures window: nothing ticked is refused; a ticked
			// shift is applied to the selection in one undo step.
			bool sawDialog = false;
			QTimer::singleShot(0, &shell, [&] {
				// The window declares no Q_OBJECT; its object name identifies it.
				auto* dialog = static_cast<LevelTextureResetDialog*>(shell.findChild<QDialog*>(QStringLiteral("levelTextureResetDialog")));
				if (!expect(dialog != nullptr, "the Reset Textures window opens")) {
					return;
				}
				sawDialog = true;
				auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("textureResetApply"));
				auto* scope = dialog->findChild<QComboBox*>(QStringLiteral("textureResetScope"));
				auto* shiftU = dialog->findChild<QCheckBox*>(QStringLiteral("textureResetShiftU"));
				auto* shiftValue = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("textureResetShiftUValue"));
				auto* filter = dialog->findChild<QCheckBox*>(QStringLiteral("textureResetFilter"));
				auto* filterName = dialog->findChild<QLineEdit*>(QStringLiteral("textureResetFilterName"));
				auto* status = dialog->findChild<QLabel*>(QStringLiteral("textureResetStatus"));
				if (!expect(apply && scope && shiftU && shiftValue && filter && filterName && status, "the window's controls")) {
					dialog->reject();
					return;
				}
				for (const auto* name : {"textureResetScope", "textureResetFilterName", "textureResetRenameName", "textureResetScaleUValue",
						 "textureResetScaleVValue", "textureResetShiftUValue", "textureResetShiftVValue", "textureResetRotationValue", "textureResetStatus"}) {
					const auto* widget = dialog->findChild<QWidget*>(QString::fromLatin1(name));
					expect(widget && !widget->accessibleName().isEmpty(), "every field has an accessible name", QString::fromLatin1(name));
				}
				for (const auto* name : {"textureResetFilter", "textureResetRename", "textureResetScaleU", "textureResetScaleV", "textureResetShiftU",
						 "textureResetShiftV", "textureResetRotation"}) {
					const auto* box = dialog->findChild<QCheckBox*>(QString::fromLatin1(name));
					expect(box && !box->text().isEmpty() && box->focusPolicy() != Qt::NoFocus, "every tick box is labelled and takes focus",
						QString::fromLatin1(name));
				}
				expect(scope->currentIndex() == 0 && !apply->isEnabled() && !status->text().isEmpty() && !shiftValue->isEnabled(),
					"nothing ticked: Reset is unavailable and the status says why", status->text());
				shiftU->setChecked(true);
				shiftValue->setValue(5);
				expect(apply->isEnabled() && shiftValue->isEnabled(), "a ticked value makes Reset available");
				filter->setChecked(true);
				filterName->clear();
				expect(!apply->isEnabled(), "a filter needs a texture name", status->text());
				filterName->setText(QStringLiteral("studio/grid"));
				expect(apply->isEnabled(), "with a name the filter is ready");
				const auto request = dialog->request();
				expect(!request.wholeMap && request.setShiftU && request.shiftU == 5 && request.onlyTexture == QStringLiteral("studio/grid") && !request.setScaleU,
					"the window's request");
				apply->click();
			});
			const QByteArray beforeDialog = bytes();
			const auto depth = shell.levelDocument().undoStack.size();
			action(QStringLiteral("map.resetTextures"))->trigger();
			expect(sawDialog && settle() && shell.levelDocument().undoStack.size() == depth + 1, "Reset Textures applies in one undo step",
				shell.statusBar()->currentMessage());
			for (const auto& face : shell.levelDocument().brushes.first().faces) {
				expect(face.uOffset == 5 && face.vOffset == 9, "the ticked U shift is set; the V shift stays");
			}
			action(QStringLiteral("map.undo"))->trigger();
			expect(settle() && bytes() == beforeDialog, "Reset Textures undoes exactly");

			// The whole map leaves hidden objects alone.
			action(QStringLiteral("map.hideSelection"))->trigger();
			until([&] { return !action(QStringLiteral("map.surfaceReset"))->isEnabled(); });
			LevelTextureResetRequest whole;
			whole.wholeMap = true;
			whole.setRotation = true;
			whole.rotation = 45;
			QString error;
			const auto hiddenDepth = shell.levelDocument().undoStack.size();
			expect(shell.startLevelTextureReset(whole, &error) && settle() && shell.levelDocument().undoStack.size() == hiddenDepth && bytes() == beforeDialog,
				"a whole-map reset leaves the hidden brush alone", error + QLatin1Char(' ') + shell.statusBar()->currentMessage());
			action(QStringLiteral("map.showAll"))->trigger();
			// The views catch up with Show All before their filters are read.
			expect(until([&] { return !top->isHidden(LevelMapSelectionKind::QuakeBrush, brushId); }), "the brush is shown again",
				QStringLiteral("Show All %1, hidden in the map %2, %3")
					.arg(action(QStringLiteral("map.showAll"))->isEnabled())
					.arg(levelMapHiddenObjects(shell.levelDocument()).size())
					.arg(shell.statusBar()->currentMessage()));
			// TrenchBroom records selection changes as undo steps too.
			const auto shownDepth = shell.levelDocument().undoStack.size();
			expect(shell.startLevelTextureReset(whole, &error) && settle() && shell.levelDocument().undoStack.size() == shownDepth + 1
					&& shell.levelDocument().brushes.first().faces.first().rotation == 45,
				"shown again, the whole map resets it", error + QLatin1Char(' ') + shell.statusBar()->currentMessage());
			action(QStringLiteral("map.undo"))->trigger();
			settle();
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
	std::cout << (ok ? "Texture reset UI checks passed" : "Texture reset UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
