// Make Room and Make Room Options in the real shell (NetRadiant Custom's
// CSG_MakeRoom): by default the walls' hidden faces take the game's caulk and
// a face picked in face mode is left open, the walls beside it lining the
// opening with its material; the options dialog persists a named caulk
// material and turning either option off, validates the name and restores
// defaults. Each Make Room is one exact undo step. Runs for the NetRadiant
// Custom and TrenchBroom profiles at 100% and at 200% with high contrast, an
// expanding translation and right-to-left layout. Qt events go to the
// widgets directly; no native input or screen capture is used.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_hollow.h"
#include "core/studio_semantics.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <array>
#include <functional>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << message << ": " << qPrintable(detail) << '\n';
		ok = false;
	}
	return condition;
}
void drain(int ms = 20)
{
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
}
bool until(const std::function<bool()>& ready, int timeoutMs = 45000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < timeoutMs) {
		drain();
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
// An axis-aligned Quake III brush, faces in the order -X, +X, -Y, +Y, -Z,
// +Z, each wound out of the box; the top face takes `topMaterial`.
QString box(std::array<double, 3> lo, std::array<double, 3> hi, const QString& material, const QString& topMaterial)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[axis] = positive ? hi[axis] : lo[axis];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[first] += 1;
			b[second] += 1;
			const QString& name = axis == 2 && positive ? topMaterial : material;
			lines << QStringLiteral("%1 %2 %3 %4 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b), name);
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}
int faceCount(const LevelMapDocument& document, const QString& texture)
{
	int count = 0;
	for (const LevelMapBrush& brush : document.brushes) {
		for (const LevelMapBrushFace& face : brush.faces) {
			count += face.textureName == texture;
		}
	}
	return count;
}
} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	// Brush 0 is the room, its top face a door material; brush 1 stays as it
	// is. The patch makes the map Quake III's, whose caulk is common/caulk.
	const QString patch = QStringLiteral(
		"{\npatchDef2\n{\nstudio/patch\n( 3 3 0 0 0 )\n(\n( ( -128 -128 0 0 0 ) ( -128 -96 0 0 0.5 ) ( -128 -64 0 0 1 ) )\n"
		"( ( -96 -128 0 0.5 0 ) ( -96 -96 0 0.5 0.5 ) ( -96 -64 0 0.5 1 ) )\n( ( -64 -128 0 1 0 ) ( -64 -96 0 1 0.5 ) ( -64 -64 0 1 1 ) )\n)\n}\n}");
	const QString text = QStringLiteral("{\n\"classname\" \"worldspawn\"\n") + box({0, 0, 0}, {64, 64, 64}, QStringLiteral("studio/wall"), QStringLiteral("studio/door"))
		+ QLatin1Char('\n') + box({256, 0, 0}, {320, 64, 64}, QStringLiteral("studio/other"), QStringLiteral("studio/other")) + QLatin1Char('\n') + patch
		+ QStringLiteral("\n}\n");
	const QString mapPath = temp.filePath(QStringLiteral("room.map"));
	{
		QFile file(mapPath);
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size(), "fixture written")) {
			return 1;
		}
	}
	CommandPaletteEntry entry;
	phase = QStringLiteral("registry");
	expect(commandPaletteEntryForCommandId(QStringLiteral("map.make-room-options"), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(),
		"Make Room Options has a palette entry");
	expect(commandPaletteEntryForCommandId(QStringLiteral("map.make-room"), &entry) && entry.summary.contains(QStringLiteral("caulked")),
		"Make Room's palette entry names its options", entry.summary);
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (const int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Make Room UI " << qPrintable(phase) << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setLevelRoomPreferences({});
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(1600, 1000);
				shell.show();
				shell.openPathFromCommandLine(mapPath);
				const auto command = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				command("shell.mode.levels")->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				if (!expect(view && until([&] { return shell.levelDocument().sourcePath == mapPath && shell.levelDocument().brushes.size() == 2; }),
						"map ready")
					|| !expect(shell.levelDocument().format == LevelMapFormat::Quake3Map, "a Quake III map")) {
					return 1;
				}
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const QByteArray original = bytes();
				auto* makeRoom = command("map.makeRoom");
				auto* options = command("map.makeRoomOptions");
				auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.makeRoomOptions"));
				if (!expect(makeRoom && options && row, "Make Room commands and the Tools row")) {
					return 1;
				}
				expect(!options->toolTip().isEmpty() && !options->statusTip().isEmpty() && options->isEnabled(), "the options command explains itself");
				expect(!row->accessibleName().isEmpty() && row->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(row),
					"the Tools row is an accessible keyboard control");
				// Modal dialogs are answered from the event loop.
				std::function<bool(QWidget*)> onModal;
				QTimer drive;
				drive.setInterval(30);
				QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
					QWidget* modal = QApplication::activeModalWidget();
					if (!modal || !onModal) {
						return;
					}
					auto handler = std::move(onModal);
					onModal = nullptr;
					if (!handler(modal)) {
						if (auto* dialog = qobject_cast<QDialog*>(modal)) {
							dialog->reject();
						}
					}
				});
				drive.start();
				const auto withDialog = [&](QAction* action, const std::function<bool(QWidget*)>& handler) {
					bool handled = false;
					onModal = [&handled, handler](QWidget* modal) { return handled = handler(modal); };
					QTimer watchdog;
					watchdog.setSingleShot(true);
					QObject::connect(&watchdog, &QTimer::timeout, [] {
						if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
							dialog->reject();
						}
					});
					watchdog.start(30000);
					action->trigger();
					watchdog.stop();
					onModal = nullptr;
					return handled;
				};
				// Select the room's brush and pick its top face in face mode.
				const auto pickTop = [&] {
					view->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 0}});
					view->selectionSetChanged(view->selectionSet());
					if (view->componentMode() != MapViewport::ComponentMode::Faces) {
						command("map.faceTool")->trigger();
					}
					const bool picked = view->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {5}}});
					view->componentPicksChanged(view->pickedComponentCount());
					return picked && view->componentMode() == MapViewport::ComponentMode::Faces;
				};
				const auto leaveFaceMode = [&] {
					if (view->componentMode() == MapViewport::ComponentMode::Faces) {
						command("map.faceTool")->trigger();
					}
				};

				// The NetRadiant Custom defaults: caulk, and the picked face open.
				expect(pickTop(), "the top face is picked in face mode");
				// TrenchBroom's profile also records selection steps.
				const auto depth = shell.levelDocument().undoStack.size();
				makeRoom->trigger();
				const LevelMapDocument& document = shell.levelDocument();
				expect(document.brushes.size() == 6 && document.undoStack.size() == depth + 1, "five walls replace the room's brush in one step",
					QStringLiteral("%1 brushes, %2 steps after %3").arg(document.brushes.size()).arg(document.undoStack.size()).arg(depth));
				expect(faceCount(document, QStringLiteral("common/caulk")) == 21 && faceCount(document, QStringLiteral("studio/door")) == 4
						&& faceCount(document, QStringLiteral("studio/wall")) == 5 && faceCount(document, QStringLiteral("studio/other")) == 6,
					"hidden faces are caulk, the opening keeps the door material, the room its own",
					QStringLiteral("%1 caulk, %2 door, %3 wall").arg(faceCount(document, QStringLiteral("common/caulk"))).arg(faceCount(document, QStringLiteral("studio/door"))).arg(faceCount(document, QStringLiteral("studio/wall"))));
				double highest = -1e9;
				for (const LevelMapBrush& brush : document.brushes) {
					if (brush.faces.first().textureName != QLatin1String("studio/other")) {
						highest = std::max(highest, brush.maxs.z);
					}
				}
				expect(std::abs(highest - 64) < 0.01, "the walls stop flush with the opening", QString::number(highest));
				bool stale = false;
				for (const LevelMapComponentSet& set : view->pickedComponents()) {
					stale = stale || (set.object.kind == LevelMapSelectionKind::QuakeBrush && set.object.objectId == 0);
				}
				expect(!stale, "no pick is left on the removed brush");
				if (scale == 100) {
					expect(shell.statusBar()->currentMessage().contains(QStringLiteral("common/caulk")), "the status names the caulk material",
						shell.statusBar()->currentMessage());
				}
				const QByteArray made = bytes();
				command("map.undo")->trigger();
				expect(bytes() == original, "undo restores the room's brush exactly");
				command("map.redo")->trigger();
				expect(bytes() == made, "redo rebuilds the same walls");
				command("map.undo")->trigger();
				leaveFaceMode();

				// A named caulk material, and picked faces closed.
				expect(withDialog(options, [&](QWidget* modal) {
					auto* caulk = modal->findChild<QCheckBox*>(QStringLiteral("levelRoomCaulk"));
					auto* material = modal->findChild<QLineEdit*>(QStringLiteral("levelRoomCaulkTexture"));
					auto* open = modal->findChild<QCheckBox*>(QStringLiteral("levelRoomOpenFaces"));
					auto* summary = modal->findChild<QLabel*>(QStringLiteral("levelRoomCaulkSummary"));
					auto* problem = modal->findChild<QLabel*>(QStringLiteral("levelRoomOptionsError"));
					auto* buttons = modal->findChild<QDialogButtonBox*>();
					if (!expect(caulk && material && open && summary && problem && buttons, "the options dialog's controls")) {
						return false;
					}
					expect(modal->objectName() == QLatin1String("levelRoomOptionsDialog") && !modal->accessibleName().isEmpty(), "an accessible dialog");
					expect(caulk->isChecked() && open->isChecked() && material->text().isEmpty() && material->placeholderText() == QLatin1String("common/caulk"),
						"the defaults: caulk with the game's material, picked faces open", material->placeholderText());
					expect(!caulk->accessibleName().isEmpty() && !open->accessibleName().isEmpty() && !material->accessibleName().isEmpty()
							&& !summary->text().isEmpty() && summary->accessibleDescription() == summary->text(),
						"accessible names and the material in use");
					expect(scale != 100 || summary->text().contains(QStringLiteral("common/caulk")), "the summary names the game's caulk", summary->text());
					material->setText(QStringLiteral("bad name"));
					buttons->button(QDialogButtonBox::Ok)->click();
					expect(modal->isVisible() && problem->isVisible() && !problem->text().isEmpty(), "a material name with a space is refused in place");
					material->setText(QStringLiteral("studio/hidden"));
					expect(!problem->isVisible() && (scale != 100 || summary->text().contains(QStringLiteral("studio/hidden"))), "editing clears the error",
						summary->text());
					open->setChecked(false);
					buttons->button(QDialogButtonBox::Ok)->click();
					return !modal->isVisible();
				}), "Make Room Options saves a named material and closed picks");
				settings.sync();
				const LevelRoomPreferences saved = settings.levelRoomPreferences();
				expect(saved.caulk && saved.caulkTexture == QLatin1String("studio/hidden") && !saved.openPickedFaces, "the options persist",
					saved.caulkTexture);
				expect(pickTop(), "the top face is picked again");
				makeRoom->trigger();
				expect(shell.levelDocument().brushes.size() == 7 && faceCount(shell.levelDocument(), QStringLiteral("studio/hidden")) == 30
						&& faceCount(shell.levelDocument(), QStringLiteral("common/caulk")) == 0,
					"six walls caulked with the named material, the picked face closed", QString::number(shell.levelDocument().brushes.size()));
				command("map.undo")->trigger();
				expect(bytes() == original, "undo restores the brush again");
				leaveFaceMode();

				// Restore Defaults, then caulk off: the walls keep their materials.
				expect(withDialog(options, [&](QWidget* modal) {
					auto* caulk = modal->findChild<QCheckBox*>(QStringLiteral("levelRoomCaulk"));
					auto* material = modal->findChild<QLineEdit*>(QStringLiteral("levelRoomCaulkTexture"));
					auto* open = modal->findChild<QCheckBox*>(QStringLiteral("levelRoomOpenFaces"));
					auto* buttons = modal->findChild<QDialogButtonBox*>();
					if (!caulk || !material || !open || !buttons) {
						return false;
					}
					expect(material->text() == QLatin1String("studio/hidden") && !open->isChecked(), "the dialog opens with the saved options");
					buttons->button(QDialogButtonBox::RestoreDefaults)->click();
					expect(caulk->isChecked() && open->isChecked() && material->text().isEmpty(), "Restore Defaults brings back NetRadiant Custom's");
					caulk->setChecked(false);
					expect(!material->isEnabled(), "the material waits for caulk");
					buttons->button(QDialogButtonBox::Ok)->click();
					return !modal->isVisible();
				}), "Make Room Options turns caulk off");
				expect(pickTop(), "the top face is picked a third time");
				makeRoom->trigger();
				expect(shell.levelDocument().brushes.size() == 6 && faceCount(shell.levelDocument(), QStringLiteral("common/caulk")) == 0
						&& faceCount(shell.levelDocument(), QStringLiteral("studio/hidden")) == 0,
					"without caulk the five walls keep the brush's materials");
				command("map.undo")->trigger();
				expect(bytes() == original, "the last undo restores the brush");
				leaveFaceMode();
				drive.stop();
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	if (!ok) {
		return 1;
	}
	std::cout << "Make Room UI: caulk, open faces, options dialog, undo and redo passed for the NetRadiant Custom and TrenchBroom profiles." << std::endl;
	return 0;
}
