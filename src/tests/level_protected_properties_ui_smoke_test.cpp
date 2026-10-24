#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_linked_groups.h"
#include "core/level_protected_properties.h"
#include "core/level_scene.h"
#include "tests/level_prefab_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
class Expanded final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char*, const char* text, const char*, int) const override
	{
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(source, QString(source.size() / 3, QLatin1Char('~')));
	}
};
int corresponding(const LevelMapDocument& document, const QString& group)
{
	const auto* node = levelSceneNode(document.scene, group);
	return node && node->objects.size() == 1 ? node->objects.first().section(':', 1).toInt() : -1;
}
QString value(const LevelMapDocument& document, int id, const QString& key)
{
	for (const auto& entity : document.entities) {
		if (entity.id == id) {
			return tests::prefabProperty(entity, key);
		}
	}
	return {};
}
QTreeWidgetItem* row(QTreeWidget* tree, const QString& key)
{
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		if ((*it)->data(0, Qt::UserRole).toString() == key) {
			return *it;
		}
	}
	return nullptr;
}
} // namespace
int main(int argc, char** argv)
{
	// Owned widget renders and semantic calls only. Never send keyboard/mouse
	// events or capture the desktop to exercise this workflow.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setSelectedEditorProfileId("trenchbroom");
	settings.sync();
	LevelMapDocument map;
	QString error, group, copy;
	if (!tests::loadPrefabFixture(&map, &error) || !createLevelSceneNode(&map, LevelSceneNodeKind::Group, "Relay", {}, &group, &error) ||
	    !assignLevelSceneObjects(&map, group, {"entity:2"}, &error) ||
	    !createLinkedLevelGroup(&map, group, {256, 0, 0, true}, &copy, &error) ||
	    !setLevelProtectedEntityProperty(&map, {2}, "targetname", {"local_relay"}, &error) ||
	    !setLevelEntityPropertyProtection(&map, {2}, {"optional"}, true, &error)) {
		expect(false, "UI fixture", error);
		return 1;
	}
	const auto path = temp.filePath("protected.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return 1;
	}
	file.write(serializeLevelMap(map).bytes);
	file.close();
	const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!captures.isEmpty()) {
		QDir().mkpath(captures);
	}
	bool ok = true;
	for (int scale : {100, 200}) {
		settings.setTheme(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark);
		settings.setTextScalePercent(scale);
		settings.sync();
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		Expanded translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		applyStudioTheme(app,
		                 studioThemeTokens(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scale));
		{
			ApplicationShell shell;
			ok &= expect(currentStudioTheme().textScalePercent == scale, "shell uses the requested persisted text scale");
			shell.resize(1800, 1200);
			shell.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			shell.show();
			shell.openPathFromCommandLine(path);
			shell.findChild<QAction*>("shell.mode.levels")->trigger();
			auto* plan = shell.findChild<MapViewport*>("mapViewport");
			auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
			auto* tree = shell.findChild<QTreeWidget*>("entityInspector");
			if (!plan || !camera || !tree || !until([&] { return shell.levelDocument().scene.nodes.size() == 2 && camera->isEnabled(); })) {
				expect(false, "UI document ready", shell.statusBar()->currentMessage());
				return 1;
			}
			const auto original = [&] { return corresponding(shell.levelDocument(), group); };
			ok &= expect(tree->tabKeyNavigation(), "property cells participate in keyboard column navigation");
			const auto peer = [&] { return corresponding(shell.levelDocument(), copy); };
			const auto select = [&](QVector<int> ids) {
				QVector<LevelMapSelectionRef> selection;
				for (int id : ids) {
					selection << LevelMapSelectionRef{LevelMapSelectionKind::Entity, id};
				}
				plan->selectSceneObjects(selection, LevelScenePick::Replace);
			};
			select({original()});
			auto* add = shell.findChild<QAction*>("map.addProtectedProperty");
			auto* clear = shell.findChild<QAction*>("map.clearProtectedProperties");
			ok &= expect(add && clear && add->isEnabled() && clear->isEnabled() && !tree->isColumnHidden(2),
			             "protected actions and column follow linked entity selection");
			if (!row(tree, "targetname") || !row(tree, "optional")) {
				expect(false, "UI protection rows",
				       QStringLiteral("entity %1, flags %2")
				           .arg(original())
				           .arg(levelProtectedEntityProperties(shell.levelDocument(), original()).join(',')));
				return 1;
			}
			ok &= expect(row(tree, "targetname")->checkState(2) == Qt::Checked && row(tree, "optional")->checkState(2) == Qt::Checked &&
			                 row(tree, "optional")->text(1).isEmpty(),
			             "protected values and absent keys are visible");
			ok &= expect(!row(tree, "targetname")->data(0, Qt::CheckStateRole).isValid() &&
			                 row(tree, "targetname")->data(2, Qt::AccessibleTextRole).toString().contains("Local") &&
			                 QAccessible::queryAccessibleInterface(tree),
			             "protection has a separate accessible checkbox from spawnflags");
			const auto before = serializeLevelMap(shell.levelDocument()).bytes;
			tree->setCurrentItem(row(tree, "targetname"), 2);
			row(tree, "targetname")->setCheckState(2, Qt::Unchecked);
			ok &= expect(until([&] { return row(tree, "targetname") && row(tree, "targetname")->text(2).contains("Shared"); }),
			             "checkbox refresh completes after the native signal");
			ok &= expect(value(shell.levelDocument(), original(), "targetname") == "relay" &&
			                 !levelEntityPropertyProtected(shell.levelDocument(), original(), "targetname") && tree->currentColumn() == 2,
			             "unprotect restores a shared value and keeps column focus", shell.statusBar()->currentMessage());
			shell.findChild<QAction*>("map.undo")->trigger();
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before, "inspector protection has one exact undo");
			row(tree, "targetname")->setText(1, "edited_local");
			ok &= expect(until([&] {
				             return row(tree, "targetname") && row(tree, "targetname")->data(1, Qt::UserRole).toString() == "edited_local";
			             }) &&
			                 value(shell.levelDocument(), original(), "targetname") == "edited_local" &&
			                 value(shell.levelDocument(), peer(), "targetname") == "relay",
			             "ordinary inspector edits respect instance protection");
			shell.findChild<QAction*>("map.undo")->trigger();
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before, "ordinary protected value edit has one exact undo");
			select({original(), peer()});
			ok &= expect(row(tree, "targetname") && row(tree, "targetname")->checkState(2) == Qt::PartiallyChecked,
			             "multi selection reports mixed protection");
			if (!row(tree, "targetname")) {
				return 1;
			}
			row(tree, "targetname")->setCheckState(2, Qt::Checked);
			ok &= expect(until([&] { return row(tree, "targetname") && row(tree, "targetname")->text(2).contains("Local"); }),
			             "mixed checkbox refresh completes");
			ok &= expect(levelEntityPropertyProtected(shell.levelDocument(), original(), "targetname") &&
			                 levelEntityPropertyProtected(shell.levelDocument(), peer(), "targetname"),
			             "mixed checkbox protects every selected entity");
			select({original()});
			// Accept the real dialog with direct form methods, including stale
			// selection and cancellation. No input events are synthesised.
			const auto dialog = [&](bool accept, bool stale, const QString& key) {
				bool visited = false;
				QTimer::singleShot(0, &shell, [&] {
					auto* form = shell.findChild<QDialog*>("levelProtectedPropertyDialog");
					if (!form) {
						return;
					}
					visited = true;
					auto* keyEdit = form->findChild<QLineEdit*>("protectedPropertyKey");
					auto* valueEdit = form->findChild<QLineEdit*>("protectedPropertyValue");
					auto* buttons = form->findChild<QDialogButtonBox*>();
					ok &= expect(!buttons->button(QDialogButtonBox::Ok)->isEnabled() && !keyEdit->accessibleName().isEmpty() &&
					                 !valueEdit->accessibleName().isEmpty(),
					             "dialog starts with a required accessible key");
					keyEdit->setText(key);
					valueEdit->setText("instance value");
					ok &= expect(buttons->button(QDialogButtonBox::Ok)->isEnabled(), "valid form enables commit");
					if (!captures.isEmpty()) {
						QImage image(form->size(), QImage::Format_ARGB32_Premultiplied);
						form->render(&image);
						ok &= image.save(QDir(captures).filePath(QStringLiteral("protected-dialog-%1.png").arg(scale)));
					}
					if (stale) {
						select({peer()});
					}
					if (accept) {
						form->accept();
					} else {
						form->reject();
					}
				});
				add->trigger();
				ok &= expect(visited, "protected property command opens its form");
			};
			const auto beforeDialog = serializeLevelMap(shell.levelDocument()).bytes;
			dialog(false, false, "cancelled");
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == beforeDialog, "cancel leaves the document unchanged");
			dialog(true, true, "stale");
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == beforeDialog &&
			                 shell.statusBar()->currentMessage().contains("changed"),
			             "changed selection rejects stale dialog publication");
			select({original()});
			dialog(true, false, "health");
			ok &= expect(value(shell.levelDocument(), original(), "health") == "instance value" &&
			                 value(shell.levelDocument(), peer(), "health").isEmpty() &&
			                 levelEntityPropertyProtected(shell.levelDocument(), original(), "health"),
			             "dialog adds a local property atomically");
			// Render the actual inspector at a useful panel width; the shell is
			// intentionally dense and users may resize its inspector dock.
			tree->resize(scale == 200 ? 1050 : 620, 800);
			QImage image(tree->size(), QImage::Format_ARGB32_Premultiplied);
			tree->render(&image);
			if (!captures.isEmpty()) {
				ok &= image.save(QDir(captures).filePath(QStringLiteral("protected-inspector-%1.png").arg(scale)));
			}
			ok &= expect(tree->header()->sectionSize(2) >= tree->fontMetrics().horizontalAdvance(tree->headerItem()->text(2)),
			             "scaled translated protection heading fits");
			clear->trigger();
			ok &= expect(levelProtectedEntityProperties(shell.levelDocument(), original()).isEmpty() && !clear->isEnabled(),
			             "clear command refreshes protection and availability");
			select({0});
			ok &= expect(tree->isColumnHidden(2) && !add->isEnabled() && !clear->isEnabled(),
			             "world properties never offer instance protection");
			shell.hide();
		}
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	if (ok) {
		std::cout << "Protected inspector, mixed selection, dialog transactions, accessibility, scaling and RTL passed.\n";
	}
	return ok ? 0 : 1;
}
