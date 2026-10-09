// Drives the Levels sidebars and the tools they hold through the real shell:
// the studio and TrenchBroom arrangements and names, the built-in classes
// standing in for missing definitions, the Shapes tab adding an arch and
// turning a brush into stairs, the Inspector's Transform fields, the View
// tab's region hiding what lies outside, and placing a class, a model and a
// sound and applying a texture from their browsers. Semantic widget calls
// only: no input injection, screen capture or game launches.

#include "app/application_shell.h"
#include "app/level_object_list.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_map.h"
#include "core/level_scene.h"
#include "core/studio_settings.h"
#include "tests/level_object_test_helpers.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <cmath>
#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) {
		std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}

void drain(int milliseconds = 30)
{
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
}

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (!ready() && elapsed.elapsed() < 30000) {
		drain();
	}
	return ready();
}

bool select(ApplicationShell& shell, const QString& selector)
{
	auto* objects = shell.findChild<LevelObjectList*>(QStringLiteral("levelMapObjects"));
	const int row = objects ? objects->rowForSelector(selector) : -1;
	if (row < 0) {
		return false;
	}
	objects->clearSelection();
	tests::setObjectCurrentRow(objects, row);
	drain();
	return shell.levelDocument().selection.size() == 1;
}

// A tenth of a second of 16-bit mono PCM at 22050 Hz, for the Sounds tab.
bool writeWave(const QString& path)
{
	QByteArray samples;
	for (int index = 0; index < 2205; ++index) {
		const auto value = static_cast<qint16>(std::sin(index * 0.2) * 8000.0);
		samples.append(static_cast<char>(value & 0xff));
		samples.append(static_cast<char>((value >> 8) & 0xff));
	}
	QByteArray bytes;
	const auto number = [&bytes](quint32 value, int size) {
		for (int byte = 0; byte < size; ++byte) {
			bytes.append(static_cast<char>((value >> (8 * byte)) & 0xff));
		}
	};
	bytes.append("RIFF");
	number(static_cast<quint32>(36 + samples.size()), 4);
	bytes.append("WAVEfmt ");
	number(16, 4);
	number(1, 2);
	number(1, 2);
	number(22050, 4);
	number(22050 * 2, 4);
	number(2, 2);
	number(16, 2);
	bytes.append("data");
	number(static_cast<quint32>(samples.size()), 4);
	bytes.append(samples);
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QTreeWidgetItem* itemWithPayload(QTreeWidget* tree, const QString& payload)
{
	if (!tree) {
		return nullptr;
	}
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		if ((*it)->data(0, Qt::UserRole).toString() == payload) {
			return *it;
		}
	}
	return nullptr;
}

// A key of the one selected entity, its class for "classname"; a note when
// the selection is not one entity.
QString selectedEntityKey(const LevelMapDocument& document, const QString& key)
{
	if (document.selection.size() != 1 || document.selection.first().kind != LevelMapSelectionKind::Entity) {
		return QStringLiteral("(not one entity selected)");
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id != document.selection.first().objectId) {
			continue;
		}
		if (key == QLatin1String("classname")) {
			return entity.className;
		}
		for (const LevelMapProperty& property : entity.properties) {
			if (property.key == key) {
				return property.value;
			}
		}
	}
	return QString();
}

int brushNamed(const LevelMapDocument& document, const QString& texture)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (!brush.faces.isEmpty() && brush.faces.first().textureName == texture) {
			return brush.id;
		}
	}
	return -1;
}

// Shows every sidebar page and checks that none is wider than its sidebar,
// naming the widgets that are when one is.
bool pagesFit(ApplicationShell& shell, const char* label)
{
	bool ok = true;
	StudioSidebar* leading = shell.levelSidebar(true);
	StudioSidebar* trailing = shell.levelSidebar(false);
	if (!expect(leading && trailing, "the shell has both sidebars", QString::fromLatin1(label))) {
		return false;
	}
	for (const QString& id : leading->pageIds() + trailing->pageIds()) {
		shell.showLevelSidebarTab(id);
		drain();
		auto* page = shell.findChild<SidebarPage*>(QStringLiteral("levelSidebarPage-") + id);
		auto* scroll = page ? page->findChild<QScrollArea*>(QStringLiteral("sidebarPageScroll")) : nullptr;
		if (!scroll || !scroll->widget()) {
			continue;
		}
		const int available = scroll->viewport()->width();
		const bool fits = scroll->widget()->width() <= available;
		QStringList culprits;
		if (!fits) {
			for (QWidget* child : scroll->widget()->findChildren<QWidget*>()) {
				if (child->isVisibleTo(page) && child->minimumSizeHint().width() > available - 16) {
					// Named by the section holding it, and by what it holds when unnamed.
					QString section;
					for (QWidget* parent = child; parent && section.isEmpty(); parent = parent->parentWidget()) {
						if (auto* holder = qobject_cast<SidebarSection*>(parent)) {
							section = holder->title();
						}
					}
					QStringList holds;
					for (QObject* inner : child->children()) {
						if (auto* widget = qobject_cast<QWidget*>(inner); widget && widget->isVisibleTo(page)) {
							holds << QStringLiteral("%1:%2").arg(QString::fromLatin1(widget->metaObject()->className()))
										 .arg(widget->minimumSizeHint().width());
						}
					}
					culprits << QStringLiteral("[%1] %2 %3 %4 {%5}").arg(section, QString::fromLatin1(child->metaObject()->className()), child->objectName())
									.arg(child->minimumSizeHint().width())
									.arg(holds.join(QLatin1Char(' ')));
				}
			}
		}
		ok &= expect(fits, "every sidebar page fits its sidebar",
			QStringLiteral("%1, %2: %3 > %4 (%5)").arg(QString::fromLatin1(label), id).arg(scroll->widget()->width()).arg(available)
				.arg(culprits.join(QStringLiteral(", "))));
	}
	return ok;
}

// The tile pickers (layouts on the View tab, shapes on the Shapes tab) show
// every tile they offer at full height, with room for its whole label.
bool tilesWhole(ApplicationShell& shell, const char* label)
{
	bool ok = true;
	const QList<std::pair<QString, QString>> pickers = {{QStringLiteral("view"), QStringLiteral("levelLayoutTiles")},
		{QStringLiteral("shapes"), QStringLiteral("levelShapeGrid")}};
	for (const auto& [tab, name] : pickers) {
		shell.showLevelSidebarTab(tab);
		drain();
		drain();
		auto* grid = shell.findChild<QWidget*>(name);
		QString state;
		if (grid) {
			auto* first = grid->findChild<QToolButton*>();
			state = QStringLiteral("visible %1, %2x%3, hint %4x%5, minimum %6x%7, hfw %8; first tile hidden %9 explicit %10 visible %11 %12x%13")
						.arg(grid->isVisible())
						.arg(grid->width())
						.arg(grid->height())
						.arg(grid->sizeHint().width())
						.arg(grid->sizeHint().height())
						.arg(grid->minimumSizeHint().width())
						.arg(grid->minimumSizeHint().height())
						.arg(grid->heightForWidth(grid->width()))
						.arg(first ? first->isHidden() : -1)
						.arg(first ? first->testAttribute(Qt::WA_WState_ExplicitShowHide) : -1)
						.arg(first ? first->isVisible() : -1)
						.arg(first ? first->width() : -1)
						.arg(first ? first->height() : -1);
		}
		if (!expect(grid && grid->isVisible() && grid->height() > 0, "the tile picker shows",
				QStringLiteral("%1, %2: %3").arg(QString::fromLatin1(label), name, state))) {
			ok = false;
			continue;
		}
		int shown = 0;
		QStringList cut;
		for (auto* tile : grid->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
			if (!tile->isVisible()) {
				continue;
			}
			++shown;
			if (tile->height() < tile->sizeHint().height() || tile->width() < tile->sizeHint().width()) {
				cut << QStringLiteral("%1 %2x%3 < %4x%5").arg(tile->text()).arg(tile->width()).arg(tile->height())
						   .arg(tile->sizeHint().width()).arg(tile->sizeHint().height());
			}
		}
		ok &= expect(shown >= 4 && cut.isEmpty(), "every tile shows whole",
			QStringLiteral("%1, %2: %3 shown; %4").arg(QString::fromLatin1(label), name).arg(shown).arg(cut.join(QStringLiteral(", "))));
	}
	return ok;
}

} // namespace

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
	QTemporaryDir temp;
	QString error;
	bool ok = temp.isValid();
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.setRestoreSession(false);
	settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));

	// A Quake map with no definitions beside it: a floor, a box, a light
	// and a player start.
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	LevelMapDocument fixture;
	ok &= createLevelMap(create, &fixture, &error);
	// Quake's worldtype key says which game's built-in classes apply.
	ok &= setLevelMapEntityProperty(&fixture, fixture.entities.first().id, QStringLiteral("worldtype"), QStringLiteral("0"), &error);
	ok &= addLevelMapBoxBrush(&fixture, {-256, -256, -16, true}, {256, 256, 0, true}, QStringLiteral("studio/floor"), nullptr, &error);
	ok &= addLevelMapBoxBrush(&fixture, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/box"), nullptr, &error);
	ok &= addLevelMapEntity(&fixture, QStringLiteral("light"), {32, 32, 128, true}, {}, nullptr, &error);
	ok &= addLevelMapEntity(&fixture, QStringLiteral("info_player_start"), {-128, -128, 24, true}, {}, nullptr, &error);
	const QString path = temp.filePath(QStringLiteral("sidebar.map"));
	QFile file(path);
	ok &= file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(fixture).bytes) > 0;
	file.close();

	ApplicationShell shell;
	shell.resize(1700, 1100);
	shell.show();
	shell.openPathFromCommandLine(path);
	shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
	if (!expect(until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 2; }), "the map opens", path)) {
		return 1;
	}

	// The studio's arrangement: browsing on one side, properties on the other.
	StudioSidebar* leading = shell.levelSidebar(true);
	StudioSidebar* trailing = shell.levelSidebar(false);
	ok &= expect(leading && trailing && leading->count() + trailing->count() == 14, "every tab is placed once");
	// The Tools tab holds the commands themselves.
	auto* clip = shell.findChild<QAbstractButton*>(QStringLiteral("levelToolsRow-map.clipTool"));
	auto* intersect = shell.findChild<QAbstractButton*>(QStringLiteral("levelToolsRow-map.intersect"));
	ok &= expect(clip && clip->isCheckable() && intersect && !intersect->isEnabled(), "the Tools tab holds the clip tool and an idle CSG Intersect");
	ok &= expect(leading->pageIds().value(0) == QStringLiteral("outliner") && leading->pageIds().contains(QStringLiteral("shapes"))
			&& trailing->pageIds().value(0) == QStringLiteral("inspector"),
		"the studio browses on the left and inspects on the right", leading->pageIds().join(QLatin1Char(',')));

	// TrenchBroom keeps everything in one inspector, with its own names.
	auto* profiles = shell.findChild<QComboBox*>(QStringLiteral("editorProfileCombo"));
	if (profiles) {
		profiles->setCurrentIndex(profiles->findData(QStringLiteral("trenchbroom")));
		drain();
		ok &= expect(leading->count() == 0 && trailing->pageIds().mid(0, 3) == QStringList({QStringLiteral("map"), QStringLiteral("inspector"), QStringLiteral("surfaces")}),
			"TrenchBroom puts Map, Entity and Face first in one sidebar", trailing->pageIds().join(QLatin1Char(',')));
		auto* face = shell.findChild<SidebarPage*>(QStringLiteral("levelSidebarPage-surfaces"));
		ok &= expect(face && face->title() == QStringLiteral("Face"), "TrenchBroom calls surfaces Face", face ? face->title() : QString());
		profiles->setCurrentIndex(profiles->findData(defaultEditorProfileId()));
		drain();
		ok &= expect(leading->pageIds().value(0) == QStringLiteral("outliner") && face && face->title() == QStringLiteral("Surfaces"),
			"the studio's arrangement and names come back");
	} else {
		ok &= expect(false, "the editor profile list exists");
	}

	// Built-in Quake classes stand in for the missing definitions.
	auto* palette = shell.findChild<QTreeWidget*>(QStringLiteral("levelMapPalette"));
	int classes = 0;
	bool army = false;
	bool door = false;
	for (QTreeWidgetItemIterator it(palette); *it; ++it) {
		const QString payload = (*it)->data(0, Qt::UserRole).toString();
		classes += payload.isEmpty() ? 0 : 1;
		army = army || payload == QStringLiteral("entity:monster_army");
		door = door || payload == QStringLiteral("brush-entity:func_door");
	}
	ok &= expect(classes > 90 && army && door, "the Entities tab lists Quake's built-in point and brush classes", QString::number(classes));

	// Shapes: an arch added in the middle of the view is eight brushes, one
	// undo step.
	ok &= expect(shell.showLevelSidebarTab(QStringLiteral("shapes")), "the Shapes tab shows");
	auto* arch = shell.findChild<QToolButton*>(QStringLiteral("levelShape-arch"));
	auto* add = shell.findChild<QAbstractButton*>(QStringLiteral("levelShapeAdd"));
	auto* sides = shell.findChild<QSpinBox*>(QStringLiteral("levelShapeSides"));
	if (arch && add && sides) {
		arch->click();
		drain();
		sides->setValue(8);
		const int brushes = static_cast<int>(shell.levelDocument().brushes.size());
		const int steps = static_cast<int>(shell.levelDocument().undoStack.size());
		add->click();
		ok &= expect(shell.levelDocument().brushes.size() == brushes + 8 && shell.levelDocument().undoStack.size() == steps + 1,
			"Add at View Centre makes an eight-segment arch in one step", QString::number(shell.levelDocument().brushes.size()));
		shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
		ok &= expect(shell.levelDocument().brushes.size() == brushes, "undo takes the arch away");
	} else {
		ok &= expect(false, "the Shapes tab has its tiles, sides and Add button");
	}

	// Replace Selected Brushes turns the box into four steps filling it.
	auto* stairs = shell.findChild<QToolButton*>(QStringLiteral("levelShape-stairs"));
	auto* stepsField = shell.findChild<QSpinBox*>(QStringLiteral("levelShapeSteps"));
	auto* replace = shell.findChild<QAbstractButton*>(QStringLiteral("levelShapeReplace"));
	const int box = brushNamed(shell.levelDocument(), QStringLiteral("studio/box"));
	if (stairs && stepsField && replace && select(shell, QStringLiteral("brush:%1").arg(box))) {
		stairs->click();
		stepsField->setValue(4);
		drain();
		ok &= expect(replace->isEnabled(), "Replace Selected Brushes is offered for a selected brush");
		const int brushes = static_cast<int>(shell.levelDocument().brushes.size());
		replace->click();
		const auto boxGone = [&shell, box]() {
			for (const LevelMapBrush& brush : shell.levelDocument().brushes) {
				if (brush.id == box) {
					return false;
				}
			}
			return true;
		};
		ok &= expect(shell.levelDocument().brushes.size() == brushes + 3 && boxGone() && shell.levelDocument().selection.size() == 4,
			"the box becomes four selected steps", shell.statusBar()->currentMessage());
		shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
		ok &= expect(!boxGone() && shell.levelDocument().brushes.size() == brushes, "undo brings the box back");
	} else {
		ok &= expect(false, "the box can be selected and the stairs chosen");
	}

	// Transform: a typed centre moves the selection there.
	if (select(shell, QStringLiteral("brush:%1").arg(box))) {
		auto* centreX = shell.findChild<QDoubleSpinBox*>(QStringLiteral("levelTransformCentreX"));
		ok &= expect(centreX && centreX->isEnabled() && std::abs(centreX->value() - 32) < 1e-6, "the Transform section shows the box's centre",
			centreX ? QString::number(centreX->value()) : QString());
		if (centreX) {
			centreX->setValue(96);
			QMetaObject::invokeMethod(centreX, "editingFinished", Qt::DirectConnection);
			drain();
			const int moved = brushNamed(shell.levelDocument(), QStringLiteral("studio/box"));
			double minX = 0;
			for (const LevelMapBrush& brush : shell.levelDocument().brushes) {
				minX = brush.id == moved ? brush.mins.x : minX;
			}
			ok &= expect(std::abs(minX - 64) < 1e-6, "typing a centre of 96 moves the box to start at 64", QString::number(minX));
			shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
		}
	}

	// The 3D preview lights the selected box once its build is in: twelve
	// triangles, six faces of two.
	{
		auto* preview = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* toggle = shell.findChild<QAction*>(QStringLiteral("map.toggle3D"));
		if (expect(preview && toggle && select(shell, QStringLiteral("brush:%1").arg(box)), "the preview and the box are there")) {
			if (!toggle->isChecked()) {
				toggle->trigger();
			}
			ok &= expect(until([&] { return preview->isEnabled() && preview->hasMesh() && preview->highlightedTriangleCount() > 0; })
					&& preview->highlightedTriangleCount() == 12,
				"the selected box is lit in the 3D preview", QString::number(preview->highlightedTriangleCount()));
			toggle->trigger();
			drain();
		}
	}

	// Shear Tool: dragging the top side's handle in the Top view slants the
	// box about its bottom side, as TrenchBroom's shear tool does.
	{
		auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		auto* shearTool = shell.findChild<QAction*>(QStringLiteral("map.shearTool"));
		if (expect(view && shearTool && select(shell, QStringLiteral("brush:%1").arg(box)), "the Shear Tool and the box are there")) {
			shearTool->trigger();
			// Close enough for the handles to show.
			shell.findChild<QAction*>(QStringLiteral("map.frameSelection"))->trigger();
			drain();
			ok &= expect(view->shearMode() && shearTool->isChecked() && view->hasResizeHandles(), "the Shear Tool shows the shear handles",
				QStringLiteral("mode %1, checked %2, handles %3, views %4, selected %5")
					.arg(view->shearMode())
					.arg(shearTool->isChecked())
					.arg(view->hasResizeHandles())
					.arg(shell.findChildren<MapViewport*>().size())
					.arg(shell.levelDocument().selection.size())
				+ QStringLiteral(", %1x%2 visible %3, 100px = %4 units")
					  .arg(view->width())
					  .arg(view->height())
					  .arg(view->isVisible())
					  .arg(view->worldPositionAt(QPointF(100, 0), 0.0).x - view->worldPositionAt(QPointF(0, 0), 0.0).x));
			const QPointF handle = view->resizeHandlePosition(MapViewport::ResizeMaxVertical);
			// The box is 64 units across between its left and right handles.
			const double across = std::abs(view->resizeHandlePosition(MapViewport::ResizeMaxHorizontal).x()
				- view->resizeHandlePosition(MapViewport::ResizeMinHorizontal).x());
			const double perPixel = 64.0 / std::max(1.0, across);
			const QPointF to = handle + QPointF(64.0 / perPixel, 0.0);
			const auto send = [view](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons) {
				QMouseEvent event(type, at, view->mapToGlobal(at), button, buttons, Qt::NoModifier);
				QCoreApplication::sendEvent(view, &event);
			};
			QString asked;
			const auto watch = QObject::connect(view, &MapViewport::shearRequested, [&asked](int axis, int along, double factor, double anchor) {
				asked = QStringLiteral("axis %1 along %2 factor %3 anchor %4").arg(axis).arg(along).arg(factor).arg(anchor);
			});
			send(QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
			send(QEvent::MouseMove, (handle + to) / 2.0, Qt::NoButton, Qt::LeftButton);
			send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
			send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
			drain();
			double minX = 0;
			double maxX = 0;
			for (const LevelMapBrush& brush : shell.levelDocument().brushes) {
				if (brush.id == box) {
					minX = brush.mins.x;
					maxX = brush.maxs.x;
				}
			}
			ok &= expect(std::abs(minX) < 1e-6 && std::abs(maxX - 128) < 1e-6, "the top side slides 64 along x and the bottom stays",
				QStringLiteral("%1 .. %2: %3 (%4; handle %5,%6 to %7,%8)").arg(minX).arg(maxX).arg(shell.statusBar()->currentMessage(), asked)
					.arg(handle.x()).arg(handle.y()).arg(to.x()).arg(to.y()));
			QObject::disconnect(watch);
			shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
			drain();
			QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
			QCoreApplication::sendEvent(view, &escape);
			drain();
			ok &= expect(!view->shearMode() && !shearTool->isChecked(), "Escape turns the Shear Tool off");
		}
	}

	// Region: what lies outside the box is hidden, then shown again.
	auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* fromSelection = shell.findChild<QAbstractButton*>(QStringLiteral("levelRegionFromSelection"));
	auto* clear = shell.findChild<QAbstractButton*>(QStringLiteral("levelRegionClear"));
	if (view && fromSelection && clear && select(shell, QStringLiteral("brush:%1").arg(box))) {
		fromSelection->click();
		drain();
		ok &= expect(view->hasRegionBox() && view->filteredCount() > 0 && clear->isEnabled(), "the region hides what lies outside the box",
			QString::number(view->filteredCount()));
		clear->click();
		drain();
		ok &= expect(!view->hasRegionBox() && view->filteredCount() == 0, "clearing the region shows the whole map");
	} else {
		ok &= expect(false, "the region controls and the box are there");
	}

	// Linked groups: the box in a scene group, a linked copy made by command,
	// and a new size typed for the original reaching the copy in one step.
	{
		auto* name = shell.findChild<QLineEdit*>(QStringLiteral("levelSceneName"));
		auto* newGroup = shell.findChild<QAbstractButton*>(QStringLiteral("levelSceneNewGroup"));
		auto* assign = shell.findChild<QAbstractButton*>(QStringLiteral("levelSceneAssign"));
		auto* tree = shell.findChild<QTreeWidget*>(QStringLiteral("levelSceneTree"));
		auto* linkCopy = shell.findChild<QAction*>(QStringLiteral("map.createLinkedCopy"));
		auto* toolsRow = shell.findChild<QAbstractButton*>(QStringLiteral("levelToolsRow-map.createLinkedCopy"));
		const auto findRow = [tree](const QString& text) -> QTreeWidgetItem* {
			for (QTreeWidgetItemIterator it(tree); tree && *it; ++it) {
				if ((*it)->text(0) == text) {
					return *it;
				}
			}
			return nullptr;
		};
		if (expect(name && newGroup && assign && tree && linkCopy && toolsRow && select(shell, QStringLiteral("brush:%1").arg(box)),
				"the scene controls and the linked copy command are there")) {
			ok &= expect(!linkCopy->isEnabled(), "a linked copy needs a group");
			name->setText(QStringLiteral("Crate"));
			newGroup->click();
			drain();
			tree->setCurrentItem(findRow(QStringLiteral("Crate")));
			select(shell, QStringLiteral("brush:%1").arg(box));
			assign->click();
			drain();
			select(shell, QStringLiteral("brush:%1").arg(box));
			ok &= expect(linkCopy->isEnabled(), "a box in a group can be copied linked");
			const int steps = static_cast<int>(shell.levelDocument().undoStack.size());
			linkCopy->trigger();
			drain();
			const LevelMapDocument& document = shell.levelDocument();
			QString crate;
			for (const LevelSceneNode& node : document.scene.nodes) {
				crate = node.name == QStringLiteral("Crate") ? node.id : crate;
			}
			const QStringList copies = levelLinkedGroupNodes(document.scene, crate);
			QTreeWidgetItem* copyRow = findRow(QStringLiteral("Crate 2"));
			ok &= expect(copies.size() == 2 && copyRow && copyRow->text(1).contains(QStringLiteral("Linked"))
					&& document.undoStack.size() == steps + 1,
				"Create Linked Copy makes Crate 2, linked, in one step", shell.statusBar()->currentMessage());
			// A new size for the original: the copy, 64 units along, takes it on.
			auto* sizeX = shell.findChild<QDoubleSpinBox*>(QStringLiteral("levelTransformSizeX"));
			if (copies.size() == 2 && sizeX && select(shell, QStringLiteral("brush:%1").arg(box))) {
				sizeX->setValue(96);
				QMetaObject::invokeMethod(sizeX, "editingFinished", Qt::DirectConnection);
				drain();
				const LevelSceneNode* copyNode = levelSceneNode(shell.levelDocument().scene, copies.last());
				double copyWidth = 0;
				double copyStart = 0;
				for (const LevelMapBrush& brush : shell.levelDocument().brushes) {
					if (copyNode && copyNode->objects.contains(QStringLiteral("brush:%1").arg(brush.id))) {
						copyWidth = brush.maxs.x - brush.mins.x;
						copyStart = brush.mins.x;
					}
				}
				ok &= expect(std::abs(copyWidth - 96) < 1e-6 && std::abs(copyStart - 64) < 1e-6, "the copy takes on the original's new size in its own place",
					QStringLiteral("%1 from %2").arg(copyWidth).arg(copyStart));
				auto* selectCopies = shell.findChild<QAction*>(QStringLiteral("map.selectLinkedCopies"));
				if (selectCopies) {
					selectCopies->trigger();
					drain();
				}
				ok &= expect(shell.levelDocument().selection.size() == 2, "Select Linked Copies selects both crates",
					QString::number(shell.levelDocument().selection.size()));
				shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
				drain();
				for (const LevelMapBrush& brush : shell.levelDocument().brushes) {
					if (copyNode && copyNode->objects.contains(QStringLiteral("brush:%1").arg(brush.id))) {
						copyWidth = brush.maxs.x - brush.mins.x;
					}
				}
				ok &= expect(std::abs(copyWidth - 64) < 1e-6, "one undo returns both crates to their old size", QString::number(copyWidth));
			} else {
				ok &= expect(false, "the size field and the copy are there");
			}
			// Back to the plain box for the checks that follow.
			for (int step = 0; step < 3; ++step) {
				shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
			}
			drain();
			ok &= expect(shell.levelDocument().scene.nodes.isEmpty(), "undo takes the group and the copy away");
		}
	}

	// A block's precondition: when it does not hold the test fails, as well as
	// skipping the block.
	const auto required = [&ok](bool value) {
		ok &= value;
		return value;
	};

	// Placing from the browsers, each one undo step and selected at once: the
	// Entities tab's Place in View adds the chosen class; a package's model and
	// sound go in from the Models and Sounds tabs, whose hints say what they
	// make in a Quake map; Give to Selection keys a selected entity; a texture
	// tile dresses the selected brush.
	{
		const QDir assets(temp.filePath(QStringLiteral("assets")));
		ok &= assets.mkpath(QStringLiteral("models/props")) && assets.mkpath(QStringLiteral("sound/items"));
		QFile model(assets.filePath(QStringLiteral("models/props/crate.md3")));
		ok &= model.open(QIODevice::WriteOnly) && model.write("IDP3") == 4;
		model.close();
		ok &= writeWave(assets.filePath(QStringLiteral("sound/items/chime.wav")));
		shell.openPathFromCommandLine(assets.path());
		shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
		QAction* undo = shell.findChild<QAction*>(QStringLiteral("map.undo"));
		const auto entities = shell.levelDocument().entities.size();
		const auto takeBack = [&](const char* message) {
			undo->trigger();
			drain();
			ok &= expect(shell.levelDocument().entities.size() == entities, message, QString::number(shell.levelDocument().entities.size()));
		};
		const auto selected = [&shell](const char* key) { return selectedEntityKey(shell.levelDocument(), QString::fromLatin1(key)); };

		ok &= expect(shell.showLevelSidebarTab(QStringLiteral("entities")), "the Entities tab shows");
		auto* placeClass = shell.findChild<QAbstractButton*>(QStringLiteral("levelPlaceInView"));
		QTreeWidgetItem* lightClass = itemWithPayload(palette, QStringLiteral("entity:light"));
		if (required(expect(placeClass && lightClass, "the Entities tab offers light and Place in View"))) {
			palette->setCurrentItem(lightClass);
			drain();
			ok &= expect(placeClass->isEnabled(), "Place in View is offered once a class is chosen");
			placeClass->click();
			drain();
			ok &= expect(shell.levelDocument().entities.size() == entities + 1 && selected("classname") == QStringLiteral("light"),
				"Place in View adds the chosen class and selects it", selected("classname"));
			takeBack("undo takes the placed light away");
		}

		ok &= expect(shell.showLevelSidebarTab(QStringLiteral("models")), "the Models tab shows");
		auto* models = shell.findChild<QTreeWidget*>(QStringLiteral("levelModelList"));
		auto* placeModel = shell.findChild<QAbstractButton*>(QStringLiteral("levelModelPlace"));
		auto* modelHint = shell.findChild<QLabel*>(QStringLiteral("levelModelPlaceHint"));
		const QString crate = QStringLiteral("model:models/props/crate.md3");
		if (required(expect(models && placeModel && modelHint && until([&] { return itemWithPayload(models, crate) != nullptr; }),
				"the Models tab lists the package's model"))) {
			// Quake's own classes have no misc_model, and no q3map2 bakes it.
			ok &= expect(modelHint->text().contains(QStringLiteral("misc_model")) && !modelHint->text().contains(QStringLiteral("VibeMap3"))
					&& modelHint->text().contains(QStringLiteral("No loaded definition")),
				"the Models tab says what a Quake map makes of a model", modelHint->text());
			models->setCurrentItem(itemWithPayload(models, crate));
			drain();
			ok &= expect(placeModel->isEnabled(), "Place in View is offered for a chosen model");
			placeModel->click();
			drain();
			ok &= expect(selected("classname") == QStringLiteral("misc_model") && selected("model") == QStringLiteral("models/props/crate.md3"),
				"the model goes in as a selected misc_model with its model key", selected("classname") + QLatin1Char(' ') + selected("model"));
			takeBack("undo takes the model away");
		}

		ok &= expect(shell.showLevelSidebarTab(QStringLiteral("sounds")), "the Sounds tab shows");
		auto* sounds = shell.findChild<QTreeWidget*>(QStringLiteral("levelSoundList"));
		auto* placeSound = shell.findChild<QAbstractButton*>(QStringLiteral("levelSoundPlace"));
		auto* giveSound = shell.findChild<QAbstractButton*>(QStringLiteral("levelSoundAssign"));
		auto* soundHint = shell.findChild<QLabel*>(QStringLiteral("levelSoundPlaceHint"));
		const QString chime = QStringLiteral("sound:sound/items/chime.wav");
		if (required(expect(sounds && placeSound && giveSound && soundHint && until([&] { return itemWithPayload(sounds, chime) != nullptr; }),
				"the Sounds tab lists the package's sound"))) {
			sounds->setCurrentItem(itemWithPayload(sounds, chime));
			drain();
			placeSound->click();
			drain();
			// Quake names sounds from below sound/.
			const QString speaker = selected("classname");
			ok &= expect(!speaker.startsWith(QLatin1Char('(')) && soundHint->text().contains(speaker) && selected("noise") == QStringLiteral("items/chime.wav"),
				"the sound goes in as the speaker the hint names, with its noise key", speaker + QStringLiteral(" / ") + soundHint->text());
			takeBack("undo takes the speaker away");
			int lamp = -1;
			for (const LevelMapEntity& entity : shell.levelDocument().entities) {
				if (entity.className == QStringLiteral("light")) {
					lamp = entity.id;
					break;
				}
			}
			// Selecting an entity is enough to offer Give to Selection.
			if (required(expect(lamp >= 0 && select(shell, QStringLiteral("entity:%1").arg(lamp)), "the light can be selected"))) {
				ok &= expect(giveSound->isEnabled(), "Give to Selection is offered once an entity is selected");
				const auto steps = shell.levelDocument().undoStack.size();
				giveSound->click();
				drain();
				ok &= expect(selected("noise") == QStringLiteral("items/chime.wav") && shell.levelDocument().undoStack.size() == steps + 1,
					"Give to Selection sets the light's noise key in one step", selected("noise"));
				undo->trigger();
				drain();
				ok &= expect(selected("noise").isEmpty(), "undo takes the noise key off again", selected("noise"));
			}
		}

		// A texture tile on the selected box.
		ok &= expect(shell.showLevelSidebarTab(QStringLiteral("textures")), "the Textures tab shows");
		auto* textures = shell.findChild<QListWidget*>(QStringLiteral("levelMapTextures"));
		const int crateBox = brushNamed(shell.levelDocument(), QStringLiteral("studio/box"));
		QListWidgetItem* floorTile = nullptr;
		for (int row = 0; textures && row < textures->count(); ++row) {
			if (textures->item(row)->data(Qt::UserRole).toString() == QStringLiteral("studio/floor")) {
				floorTile = textures->item(row);
			}
		}
		if (required(expect(floorTile && crateBox >= 0 && select(shell, QStringLiteral("brush:%1").arg(crateBox)), "the floor tile and the box are there"))) {
			Q_EMIT textures->itemActivated(floorTile);
			drain();
			ok &= expect(brushNamed(shell.levelDocument(), QStringLiteral("studio/box")) < 0, "activating a texture tile dresses the selected box in it");
			undo->trigger();
			drain();
			ok &= expect(brushNamed(shell.levelDocument(), QStringLiteral("studio/box")) == crateBox, "undo gives the box its texture back");
		}
	}

	// The Surfaces tab comes in sections like the rest, and remembers them.
	{
		ok &= expect(shell.showLevelSidebarTab(QStringLiteral("surfaces")), "the Surfaces tab shows");
		auto* page = shell.findChild<SidebarPage*>(QStringLiteral("levelSidebarPage-surfaces"));
		SidebarSection* clipboard = page ? page->section(QStringLiteral("surfaces.clipboard")) : nullptr;
		if (required(expect(page && clipboard && page->section(QStringLiteral("surfaces.target")) && page->section(QStringLiteral("surfaces.adjust")),
				"the Surfaces tab has Target, Adjust and Copy and Paste sections"))) {
			clipboard->setExpanded(false);
			const StudioSettings saved;
			ok &= expect(saved.shellLayoutState(QStringLiteral("levelSidebar/section/surfaces.clipboard")) == QByteArrayLiteral("off"),
				"folding Copy and Paste is remembered");
			clipboard->setExpanded(true);
		}
	}

	// Every page fits its sidebar: nothing on it is wider than the room.
	ok &= pagesFit(shell, "at 100% text");
	ok &= tilesWhole(shell, "at 100% text");

	// Folding a sidebar gives its room to the views.
	const int wide = leading->width();
	leading->setFolded(true);
	drain();
	ok &= expect(leading->isFolded() && leading->width() < wide, "a folded sidebar narrows to its tabs",
		QStringLiteral("%1 -> %2").arg(wide).arg(leading->width()));
	leading->setFolded(false);

	// At 200% text in high contrast, right to left, every page still fits.
	{
		settings.setTheme(StudioTheme::HighContrastDark);
		settings.setTextScalePercent(200);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 200));
		app.setLayoutDirection(Qt::RightToLeft);
		ApplicationShell large;
		large.setLayoutDirection(Qt::RightToLeft);
		large.resize(2400, 1500);
		large.show();
		large.openPathFromCommandLine(path);
		large.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
		if (expect(until([&] { return large.levelDocument().sourcePath == path; }), "the map opens at 200% text", path)) {
			ok &= pagesFit(large, "at 200% text, right to left");
			ok &= tilesWhole(large, "at 200% text, right to left");
		} else {
			ok = false;
		}
		app.setLayoutDirection(Qt::LeftToRight);
	}

	if (ok) {
		std::cout << "Level sidebar UI smoke test passed.\n";
	}
	return ok ? 0 : 1;
}
