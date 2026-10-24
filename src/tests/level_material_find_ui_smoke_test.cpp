// Reveal Material and the modeless Find and Replace Textures window in the
// offscreen shell: profiles, 100% and 200% text, high contrast, expanded
// translation and right-to-left layout.
#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "tests/level_type_selection_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << detail.toStdString() << std::endl; }
	return value;
}
bool until(const std::function<bool()>& ready, int milliseconds = 20000)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < milliseconds) {
		QEventLoop loop; QTimer::singleShot(15, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override {
		const auto text = QString::fromUtf8(source); return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, '~'));
	}
};
QListWidgetItem* tileNamed(QListWidget* list, const QString& name)
{
	for (int row = 0; list && row < list->count(); ++row) {
		if (list->item(row)->data(Qt::UserRole).toString() == name) { return list->item(row); }
	}
	return nullptr;
}
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true);
	const auto source = tests::typeSelectionFixture(); const auto path = temp.filePath("find.map");
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) { return 1; }
	file.close();
	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (int scale : {100, 200}) {
			std::cout << "Material find UI " << profile << ' ' << scale << '%' << std::endl;
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600, 1000); shell.show(); shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger();
				auto* view = shell.findChild<MapViewport*>("mapViewport");
				auto* textures = shell.findChild<QListWidget*>("levelMapTextures");
				auto* filter = shell.findChild<QLineEdit*>("levelMapTextureFilter");
				auto* reveal = action("map.revealMaterial"); auto* findReplace = action("map.replaceTexture");
				auto* tool = shell.findChild<QPushButton*>("levelToolsRow-map.revealMaterial");
				if (!expect(view && textures && filter && reveal && findReplace && tool
						&& until([&] { return shell.levelDocument().sourcePath == path && textures->count() > 1; }), "map and controls ready")) { return 1; }
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto baseline = bytes();
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) { view->setSelectionSet(refs); view->selectionSetChanged(refs); };
				const auto texturesShown = [&] {
					return shell.levelSidebar(true)->currentPageId() == QStringLiteral("textures") || shell.levelSidebar(false)->currentPageId() == QStringLiteral("textures");
				};

				// Reveal Material: TrenchBroom has no default key, nor does NRC.
				expect(reveal->shortcuts().isEmpty() && findReplace->shortcuts().isEmpty(), "no profile binds the commands", reveal->shortcut().toString());
				expect(!reveal->isEnabled() && !tool->isEnabled() && !tool->accessibleName().isEmpty() && tool->focusPolicy() != Qt::NoFocus
					&& QAccessible::queryAccessibleInterface(tool), "nothing selected leaves Reveal Material off but reachable");
				pick({{LevelMapSelectionKind::QuakeBrush, 0}});
				expect(!reveal->isEnabled() && reveal->statusTip().contains("metal") && reveal->statusTip().contains("studio/checker")
					&& tool->accessibleDescription() == reveal->statusTip(), "a mixed brush disables Reveal and says why", reveal->statusTip());
				pick({{LevelMapSelectionKind::QuakeBrush, 1}});
				shell.showLevelSidebarTab(QStringLiteral("models"));
				filter->setText(QStringLiteral("metal"));
				expect(reveal->isEnabled() && reveal->statusTip().contains("studio/checker"), "one shared material enables Reveal");
				reveal->trigger();
				expect(texturesShown() && textures->currentItem() && textures->currentItem()->data(Qt::UserRole).toString() == QStringLiteral("studio/checker")
					&& textures->currentItem()->isSelected() && !textures->currentItem()->isHidden() && filter->text().isEmpty(),
					"Reveal shows, selects and unfilters the tile", shell.statusBar()->currentMessage());
				expect(shell.levelPaintMaterial() == QStringLiteral("studio/checker") && shell.statusBar()->currentMessage().contains("studio/checker"),
					"the revealed material becomes current");
				expect(bytes() == baseline, "Reveal never edits the map");

				// Find and Replace Textures stays open beside the shell.
				findReplace->trigger();
				auto* dialog = shell.findChild<QDialog*>("replaceTextureDialog");
				auto* find = dialog ? dialog->findChild<QComboBox*>("replaceTextureFrom") : nullptr;
				auto* replace = dialog ? dialog->findChild<QComboBox*>("replaceTextureTo") : nullptr;
				auto* scope = dialog ? dialog->findChild<QComboBox*>("replaceTextureScope") : nullptr;
				auto* preview = dialog ? dialog->findChild<QLabel*>("replaceTextureCount") : nullptr;
				auto* target = dialog ? dialog->findChild<QLabel*>("replaceTexturePickTarget") : nullptr;
				auto* findButton = dialog ? dialog->findChild<QPushButton*>("replaceTextureFind") : nullptr;
				auto* replaceButton = dialog ? dialog->findChild<QPushButton*>("replaceTextureApply") : nullptr;
				if (!expect(dialog && find && replace && scope && preview && target && findButton && replaceButton, "dialog controls")) { return 1; }
				expect(dialog->isVisible() && !dialog->isModal() && QApplication::activeModalWidget() == nullptr, "modeless window");
				expect(find->currentText() == QStringLiteral("studio/checker"), "Find starts from the selection's material", find->currentText());
				expect(!dialog->accessibleName().isEmpty() && !find->accessibleName().isEmpty() && !replace->accessibleName().isEmpty()
					&& !scope->accessibleName().isEmpty() && preview->accessibleDescription() == preview->text() && !target->text().isEmpty(), "accessible names and live text");
				if (scale == 200) { expect(dialog->windowTitle().startsWith('[') && preview->text().startsWith('['), "expanded translation reaches the window"); }
				auto* scopes = qobject_cast<QStandardItemModel*>(scope->model());
				expect(scopes && scope->count() == 3 && scopes->item(1)->isEnabled() && !scopes->item(2)->isEnabled() && !scopes->item(2)->toolTip().isEmpty(),
					"Picked faces is listed but unavailable without picks");
				replace->setEditText(QString());
				scope->setCurrentIndex(0);
				expect(until([&] { return preview->text().contains("14") && findButton->isEnabled() && !replaceButton->isEnabled(); }), "Find preview counts every use", preview->text());

				// Texture picks fill the field that last had focus.
				dialog->activateWindow(); replace->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return target->text().contains("Replace"); }), "focusing Replace with retargets picks", target->text());
				textures->setCurrentItem(tileNamed(textures, QStringLiteral("metal")));
				expect(replace->currentText() == QStringLiteral("metal") && find->currentText() == QStringLiteral("studio/checker"), "a tile pick fills Replace with");
				find->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return !target->text().contains("Replace"); }), "focusing Find retargets picks", target->text());
				Q_EMIT textures->itemActivated(tileNamed(textures, QStringLiteral("studio/checker")));
				expect(find->currentText() == QStringLiteral("studio/checker") && bytes() == baseline, "activating a tile fills the window instead of applying");
				replace->setEditText(QString());

				// Find picks faces and selects patches, additively.
				pick({}); find->setEditText(QStringLiteral("STUDIO/CHECKER"));
				expect(until([&] { return findButton->isEnabled(); }), "Find ready");
				findButton->click();
				expect(until([&] { return view->texturedSurfaceFaces().size() == 11; }) && shell.levelDocument().selection.size() == 3 && bytes() == baseline,
					"Find picks the matching faces and patches", shell.statusBar()->currentMessage());
				expect(until([&] { return scopes->item(2)->isEnabled(); }), "picks make Picked faces available");

				// Replace in the picked faces: one undo step, the window stays open.
				scope->setCurrentIndex(2); replace->setEditText(QStringLiteral("base/metal"));
				expect(until([&] { return preview->text().contains("14") && replaceButton->isEnabled(); }), "Replace preview", preview->text());
				const auto undo = shell.levelDocument().undoStack.size();
				replaceButton->click();
				expect(shell.levelDocument().undoStack.size() == undo + 1 && shell.levelDocument().brushes[1].faces[0].textureName == QStringLiteral("base/metal")
					&& shell.levelDocument().brushes[0].faces[0].textureName == QStringLiteral("metal") && shell.levelDocument().patches[0].textureName == QStringLiteral("base/metal"),
					"picked faces replace", shell.statusBar()->currentMessage());
				expect(dialog->isVisible() && view->texturedSurfaceFaces().size() == 11, "the window and the picks survive Replace");
				expect(until([&] { return !replaceButton->isEnabled(); }), "nothing is left to replace in the picks", preview->text());
				action("map.undo")->trigger();
				expect(bytes() == baseline, "Undo restores every texture");

				// Selected objects.
				action("map.selectNone")->trigger(); action("map.selectNone")->trigger();
				pick({{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}});
				find->setEditText(QStringLiteral("studio/checker")); scope->setCurrentIndex(1);
				expect(until([&] { return preview->text().contains("11") && replaceButton->isEnabled(); }), "selection preview", preview->text());
				replaceButton->click();
				expect(shell.levelDocument().brushes[1].faces[3].textureName == QStringLiteral("base/metal") && shell.levelDocument().brushes[0].faces[3].textureName == QStringLiteral("base/metal")
					&& shell.levelDocument().patches[0].textureName == QStringLiteral("studio/checker"), "selection replace leaves unselected surfaces", shell.statusBar()->currentMessage());
				action("map.undo")->trigger();
				expect(bytes() == baseline, "selection undo");
				// The whole map, with a brush the view filters out: it stays as it is.
				scope->setCurrentIndex(0);
				view->setFilteredObjects({{LevelMapSelectionKind::QuakeBrush, 0}});
				find->setEditText(QStringLiteral("studio/checker "));
				expect(until([&] { return preview->text().contains("9") && preview->text().contains("5"); }), "a filtered brush is skipped and counted", preview->text());
				replaceButton->click();
				expect(shell.levelDocument().brushes[1].faces[3].textureName == QStringLiteral("base/metal") && shell.levelDocument().brushes[0].faces[3].textureName == QStringLiteral("studio/checker")
					&& shell.levelDocument().patches[0].textureName == QStringLiteral("base/metal") && shell.statusBar()->currentMessage().contains("5"),
					"whole-map replace leaves the filtered brush", shell.statusBar()->currentMessage());
				view->setFilteredObjects({});
				action("map.undo")->trigger();
				expect(bytes() == baseline, "whole-map undo");

				if (QLatin1String(profile) == QLatin1String("netradiant-custom") && scale == 200) {
					const auto capture = qEnvironmentVariable("VIBESTUDIO_MATERIAL_FIND_CAPTURE");
					if (!capture.isEmpty()) {
						QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); dialog->render(&image); expect(image.save(capture), "owned dialog capture");
					}
				}
				dialog->close();
				expect(!dialog->isVisible(), "Close hides the window");
				Q_EMIT textures->currentItemChanged(tileNamed(textures, QStringLiteral("metal")), nullptr);
				expect(find->currentText() == QStringLiteral("studio/checker "), "a closed window ignores picks");
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	return ok ? 0 : 1;
}
