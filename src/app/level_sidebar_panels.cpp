// Two Levels sidebar panels built from the map itself: the Map tab's
// worldspawn keys and pre-build checklist (after VibeRadiant's Globals panel,
// see docs/CREDITS.md), and the View tab's display filters.

#include "app/application_shell.h"
#include "app/level_object_list.h"
#include "app/level_object_model.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/entity_builtin_catalogue.h"
#include "core/level_placement.h"
#include "core/level_view_filters.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <cmath>

namespace vibestudio {

namespace {
// Gives each row of a word-wrapped list the height of its text wrapped at
// the list's width, and the list the height of its rows. The view's own
// size hints measure a row on one line until it has been laid out.
void fitWrappedListRows(QListWidget* list, int minimum, int maximum)
{
	const int width = list->viewport()->width();
	if (width <= 0) {
		return;
	}
	const StudioThemeMetrics& metrics = currentStudioTheme().metrics;
	const QFontMetrics font(list->font());
	const int icon = list->iconSize().width();
	// Room for the focus frame and the style's own margins as well.
	const int textWidth = std::max(40, width - 2 * metrics.itemPaddingHorizontal - (icon > 0 ? icon + 6 : 0) - 16);
	int total = 0;
	for (int row = 0; row < list->count(); ++row) {
		QListWidgetItem* item = list->item(row);
		const QRect text = font.boundingRect(QRect(0, 0, textWidth, 1 << 20), Qt::TextWordWrap, item->text());
		const int height = std::max(text.height(), icon) + 2 * metrics.itemPaddingVertical + 2;
		item->setSizeHint(QSize(width, height));
		total += height + list->spacing();
	}
	const int chrome = 2 * list->frameWidth() + 4;
	list->setMinimumHeight(minimum);
	list->setMaximumHeight(std::clamp(total + chrome, minimum, std::max(minimum, maximum)));
}

// Fits a word-wrapped list to its rows again whenever its width changes,
// since wrapped rows grow taller as the list narrows.
class RowFitter final : public QObject {
public:
	RowFitter(QAbstractItemView* view, int minimum, int maximum)
		: QObject(view)
		, m_view(view)
		, m_minimum(minimum)
		, m_maximum(maximum)
	{
		view->installEventFilter(this);
	}

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == m_view && (event->type() == QEvent::Resize || event->type() == QEvent::Show) && m_view->width() != m_width) {
			m_width = m_view->width();
			if (auto* list = qobject_cast<QListWidget*>(m_view)) {
				fitWrappedListRows(list, m_minimum, m_maximum);
			} else {
				fitListHeightToRows(m_view, m_minimum, m_maximum);
			}
		}
		return QObject::eventFilter(watched, event);
	}

private:
	QAbstractItemView* m_view;
	int m_minimum;
	int m_maximum;
	int m_width = -1;
};


int worldspawnEntityId(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			return entity.id;
		}
	}
	return -1;
}

const LevelMapEntity* worldspawnEntity(const LevelMapDocument& document)
{
	const int id = worldspawnEntityId(document);
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id == id) {
			return &entity;
		}
	}
	return nullptr;
}

QString propertyValue(const LevelMapEntity& entity, const QString& key)
{
	for (const LevelMapProperty& property : entity.properties) {
		if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
			return property.value;
		}
	}
	return QString();
}

bool hasClassPrefix(const LevelMapDocument& document, std::initializer_list<const char*> prefixes)
{
	for (const LevelMapEntity& entity : document.entities) {
		const QString lowered = entity.className.toLower();
		for (const char* prefix : prefixes) {
			if (lowered.startsWith(QLatin1String(prefix))) {
				return true;
			}
		}
	}
	return false;
}

// Keys worth offering on worldspawn for each game, by the conventions of
// its compilers and engines.
QStringList worldspawnKeySuggestions(const LevelMapDocument& document)
{
	if (document.format == LevelMapFormat::Quake3Map) {
		return {QStringLiteral("message"), QStringLiteral("music"), QStringLiteral("gravity"), QStringLiteral("_ambient"), QStringLiteral("_color"),
			QStringLiteral("gridsize"), QStringLiteral("_blocksize"), QStringLiteral("_keepLights"), QStringLiteral("enableDust"), QStringLiteral("_lightmapscale")};
	}
	return {QStringLiteral("message"), QStringLiteral("wad"), QStringLiteral("worldtype"), QStringLiteral("sounds"), QStringLiteral("sky"),
		QStringLiteral("_sunlight"), QStringLiteral("_sunlight_mangle"), QStringLiteral("_sunlight_color"), QStringLiteral("light"),
		QStringLiteral("_minlight"), QStringLiteral("_dirt"), QStringLiteral("_bounce"), QStringLiteral("gravity")};
}

QListWidgetItem* checklistRow(QListWidget* list, OperationState state, const QString& text, const QString& action)
{
	const QString icon = state == OperationState::Completed ? QStringLiteral("success")
		: state == OperationState::Failed ? QStringLiteral("error")
		: state == OperationState::Warning ? QStringLiteral("warning") : QStringLiteral("info");
	const StudioIconTone tone = state == OperationState::Completed ? StudioIconTone::Success
		: state == OperationState::Failed ? StudioIconTone::Danger
		: state == OperationState::Warning ? StudioIconTone::Warning : StudioIconTone::Muted;
	auto* item = new QListWidgetItem(studioIcon(icon, tone), text, list);
	item->setData(Qt::UserRole, action);
	item->setData(Qt::UserRole + 2, operationStateId(state));
	// Said in words as well as by the glyph.
	const QString stateText = state == OperationState::Completed ? QCoreApplication::translate("VibeStudioLevelSidebar", "Done")
		: state == OperationState::Failed ? QCoreApplication::translate("VibeStudioLevelSidebar", "Needs attention")
		: state == OperationState::Warning ? QCoreApplication::translate("VibeStudioLevelSidebar", "Worth checking")
		: QCoreApplication::translate("VibeStudioLevelSidebar", "Note");
	item->setData(Qt::AccessibleTextRole, QStringLiteral("%1: %2").arg(stateText, text));
	item->setToolTip(QStringLiteral("%1: %2").arg(stateText, text));
	return item;
}

} // namespace

void ApplicationShell::buildLevelMapSettings(SidebarPage* page)
{
	// Worldspawn: the map's own keys, edited in place.
	m_levelWorldspawn = new QTreeWidget;
	m_levelWorldspawn->setObjectName(QStringLiteral("levelWorldspawn"));
	m_levelWorldspawn->setAccessibleName(tr("Worldspawn keys"));
	m_levelWorldspawn->setAccessibleDescription(tr("The map's worldspawn keys. Double-click or press F2 on a value to change it."));
	m_levelWorldspawn->setColumnCount(2);
	m_levelWorldspawn->setHeaderLabels({tr("Key"), tr("Value")});
	m_levelWorldspawn->setRootIsDecorated(false);
	m_levelWorldspawn->setUniformRowHeights(true);
	m_levelWorldspawn->setAlternatingRowColors(true);
	m_levelWorldspawn->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_levelWorldspawn->header()->setStretchLastSection(true);
	m_levelWorldspawn->setColumnWidth(0, 120);
	m_levelWorldspawn->setMinimumHeight(140);
	connect(m_levelWorldspawn, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
		if (m_fillingLevelWorldspawn || column != 1 || !item) {
			return;
		}
		const int world = worldspawnEntityId(m_levelMapDocument);
		const QString key = item->data(0, Qt::UserRole).toString();
		if (world < 0 || key.isEmpty()) {
			return;
		}
		QString error;
		if (!setLevelMapEntityProperty(&m_levelMapDocument, world, key, item->text(1), &error)) {
			statusBar()->showMessage(tr("Could not change %1: %2").arg(key, error));
			refreshLevelMapSettings();
			return;
		}
		recordActivity(tr("Worldspawn key changed"), key, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Set worldspawn %1. Undo puts it back.").arg(key), 4000);
	});
	SidebarSection* worldspawn = addLevelSidebarSection(page, QStringLiteral("map.worldspawn"), tr("Worldspawn"), m_levelWorldspawn);
	auto* addKey = worldspawn->addHeaderButton(QStringLiteral("plus"), tr("Add Key…"), tr("Add a key to worldspawn, such as message or _sunlight."));
	addKey->setObjectName(QStringLiteral("levelWorldspawnAdd"));
	connect(addKey, &QToolButton::clicked, this, &ApplicationShell::addLevelWorldspawnKey);
	auto* select = worldspawn->addHeaderButton(QStringLiteral("crosshair"), tr("Select Worldspawn"),
		tr("Select worldspawn and edit it in the inspector, with its definition's keys and flags."));
	select->setObjectName(QStringLiteral("levelWorldspawnSelect"));
	connect(select, &QToolButton::clicked, this, [this]() {
		const int world = worldspawnEntityId(m_levelMapDocument);
		if (world >= 0 && selectLevelMapObjectBySelector(QStringLiteral("entity:%1").arg(world))) {
			showLevelSidebarPage(QStringLiteral("inspector"));
		}
	});

	// The checklist: what a map needs before it is worth building.
	m_levelChecklist = new QListWidget;
	m_levelChecklist->setObjectName(QStringLiteral("levelChecklist"));
	m_levelChecklist->setAccessibleName(tr("Pre-build checklist"));
	m_levelChecklist->setAccessibleDescription(tr("What the map has and still needs before a build. Press Enter on a row to go to what it is about."));
	m_levelChecklist->setWordWrap(true);
	m_levelChecklist->setTextElideMode(Qt::ElideNone);
	m_levelChecklist->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_levelChecklist->setResizeMode(QListView::Adjust);
	setBaseIconSize(m_levelChecklist, QSize(16, 16));
	connect(m_levelChecklist, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		const QString action = item ? item->data(Qt::UserRole).toString() : QString();
		if (action.startsWith(QStringLiteral("place:"))) {
			// The class to add is chosen in Entities, ready to drag or place.
			showLevelSidebarPage(QStringLiteral("entities"));
			if (m_levelMapPaletteFilter) {
				m_levelMapPaletteFilter->setText(action.mid(6));
			}
		} else if (action == QStringLiteral("leak")) {
			revealLeakTrail(m_leakTrailPath);
		} else if (action == QStringLiteral("definitions")) {
			showLevelSidebarPage(QStringLiteral("map"), QStringLiteral("map.definitions"), true);
		} else if (action == QStringLiteral("health")) {
			showLevelSidebarPage(QStringLiteral("health"), QString(), true);
		} else if (action.startsWith(QStringLiteral("key:"))) {
			addLevelWorldspawnKey();
		}
	});
	fitListHeightToRows(m_levelChecklist, 48, 360);
	new RowFitter(m_levelChecklist, 48, 360);
	addLevelSidebarSection(page, QStringLiteral("map.checklist"), tr("Checklist"), m_levelChecklist);
}

void ApplicationShell::addLevelWorldspawnKey()
{
	const int world = worldspawnEntityId(m_levelMapDocument);
	if (world < 0) {
		statusBar()->showMessage(tr("Open a Quake-family map to edit worldspawn."));
		return;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelWorldspawnKeyDialog"));
	dialog.setWindowTitle(tr("Add Worldspawn Key"));
	auto* form = new QFormLayout(&dialog);
	auto* key = new QComboBox;
	key->setObjectName(QStringLiteral("levelWorldspawnKey"));
	key->setEditable(true);
	key->setAccessibleName(tr("Key"));
	QStringList suggestions = worldspawnKeySuggestions(m_levelMapDocument);
	EntityClassDefinition definition;
	if (m_entityDefinitions.classForName(QStringLiteral("worldspawn"), &definition)) {
		for (const EntityKeyDefinition& declared : definition.keys) {
			if (!suggestions.contains(declared.key, Qt::CaseInsensitive)) {
				suggestions << declared.key;
			}
		}
	}
	key->addItems(suggestions);
	auto* value = new QLineEdit;
	value->setObjectName(QStringLiteral("levelWorldspawnValue"));
	value->setAccessibleName(tr("Value"));
	form->addRow(tr("Key"), key);
	form->addRow(tr("Value"), value);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	value->setFocus();
	if (dialog.exec() != QDialog::Accepted || key->currentText().trimmed().isEmpty()) {
		return;
	}
	QString error;
	if (!setLevelMapEntityProperty(&m_levelMapDocument, world, key->currentText().trimmed(), value->text(), &error)) {
		statusBar()->showMessage(tr("Could not add the key: %1").arg(error));
		return;
	}
	recordActivity(tr("Worldspawn key changed"), key->currentText().trimmed(), QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
}

void ApplicationShell::refreshLevelMapSettings()
{
	if (!m_levelWorldspawn || !m_levelChecklist) {
		return;
	}
	// Edits, undo and opens all change the revision or the source.
	const QString source = m_levelMapDocument.sourcePath + QLatin1Char('|') + m_levelMapDocument.mapName + QLatin1Char('|')
		+ QString::number(m_levelMapLoadSerial) + QLatin1Char('|') + m_leakTrailPath + QLatin1Char('|') + QString::number(m_entityDefinitions.classes.size());
	if (source == m_levelMapSettingsSource && m_levelMapDocument.revision == m_levelMapSettingsRevision) {
		return;
	}
	m_levelMapSettingsSource = source;
	m_levelMapSettingsRevision = m_levelMapDocument.revision;

	{
		const QScopedValueRollback<bool> filling(m_fillingLevelWorldspawn, true);
		const QString current = m_levelWorldspawn->currentItem() ? m_levelWorldspawn->currentItem()->data(0, Qt::UserRole).toString() : QString();
		m_levelWorldspawn->clear();
		const LevelMapEntity* world = worldspawnEntity(m_levelMapDocument);
		m_levelWorldspawn->setEnabled(world != nullptr);
		if (world) {
			for (const LevelMapProperty& property : world->properties) {
				if (property.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
					continue;
				}
				auto* item = new QTreeWidgetItem(m_levelWorldspawn, {property.key, property.value});
				item->setData(0, Qt::UserRole, property.key);
				item->setFlags(item->flags() | Qt::ItemIsEditable);
				item->setToolTip(1, property.value);
				if (property.key == current) {
					m_levelWorldspawn->setCurrentItem(item);
				}
			}
		}
	}

	m_levelChecklist->clear();
	const LevelMapDocument& document = m_levelMapDocument;
	if (document.format == LevelMapFormat::Unknown) {
		m_levelChecklist->addItem(tr("Open a map to see what it still needs."));
		m_levelChecklist->item(0)->setFlags(Qt::NoItemFlags);
		return;
	}
	if (document.format == LevelMapFormat::DoomWad) {
		bool start = false;
		bool deathmatch = false;
		for (const LevelMapDoomThing& thing : document.doomThings) {
			start = start || thing.type == 1;
			deathmatch = deathmatch || thing.type == 11;
		}
		checklistRow(m_levelChecklist, start ? OperationState::Completed : OperationState::Failed,
			start ? tr("Player 1 start placed") : tr("No player 1 start: the map cannot be played"), start ? QString() : QStringLiteral("place:1"));
		checklistRow(m_levelChecklist, deathmatch ? OperationState::Completed : OperationState::Idle,
			deathmatch ? tr("Deathmatch starts placed") : tr("No deathmatch starts (needed only for deathmatch)"), deathmatch ? QString() : QStringLiteral("place:11"));
		checklistRow(m_levelChecklist, document.doomGeometryChanged ? OperationState::Warning : OperationState::Completed,
			document.doomGeometryChanged ? tr("Geometry changed since the nodes were built: build nodes before playing")
										 : tr("Nodes match the geometry"),
			document.doomGeometryChanged ? QStringLiteral("health") : QString());
	} else {
		const LevelMapEntity* world = worldspawnEntity(document);
		const bool start = hasClassPrefix(document, {"info_player_start"});
		const bool anyStart = start || hasClassPrefix(document, {"info_player_deathmatch", "info_player_coop", "info_player_team"});
		checklistRow(m_levelChecklist, start ? OperationState::Completed : (anyStart ? OperationState::Warning : OperationState::Failed),
			start ? tr("Player start placed")
				  : (anyStart ? tr("Only deathmatch or team starts: single player has nowhere to begin") : tr("No player start: the map cannot be played")),
			start ? QString() : QStringLiteral("place:info_player_start"));
		const bool lights = hasClassPrefix(document, {"light"});
		const bool sun = world && (!propertyValue(*world, QStringLiteral("_sunlight")).isEmpty() || !propertyValue(*world, QStringLiteral("_sun")).isEmpty()
			|| !propertyValue(*world, QStringLiteral("light")).isEmpty() || !propertyValue(*world, QStringLiteral("_minlight")).isEmpty());
		checklistRow(m_levelChecklist, lights || sun ? OperationState::Completed : OperationState::Warning,
			lights ? tr("Lights placed") : (sun ? tr("Lit by worldspawn sunlight or minimum light") : tr("No lights: the map will build in darkness")),
			lights || sun ? QString() : QStringLiteral("place:light"));
		const QString message = world ? propertyValue(*world, QStringLiteral("message")) : QString();
		checklistRow(m_levelChecklist, message.isEmpty() ? OperationState::Idle : OperationState::Completed,
			message.isEmpty() ? tr("No map title: worldspawn has no message key") : tr("Titled “%1”").arg(message),
			message.isEmpty() ? QStringLiteral("key:message") : QString());
		if (document.format == LevelMapFormat::QuakeMap && document.engineFamily.compare(QStringLiteral("idtech2"), Qt::CaseInsensitive) != 0) {
			const QString wad = world ? propertyValue(*world, QStringLiteral("wad")) : QString();
			checklistRow(m_levelChecklist, wad.isEmpty() ? OperationState::Idle : OperationState::Completed,
				wad.isEmpty() ? tr("No wad key: the compiler finds textures only in its default search") : tr("Textures from %1").arg(wad),
				wad.isEmpty() ? QStringLiteral("key:wad") : QString());
		}
		BuiltinEntityGame builtinGame = BuiltinEntityGame::Quake;
		if (m_entityDefinitionsBuiltin && builtinEntityGameFromId(m_entityDefinitionsBuiltinGame, &builtinGame)) {
			// The stock classes check the map, but a mod's own are unknown to them.
			checklistRow(m_levelChecklist, OperationState::Idle,
				tr("Checked against the built-in %1 classes; load a mod's own definitions for its classes").arg(builtinEntityGameDisplayName(builtinGame)),
				QStringLiteral("definitions"));
		} else {
			checklistRow(m_levelChecklist, m_entityDefinitions.isEmpty() ? OperationState::Warning : OperationState::Completed,
				m_entityDefinitions.isEmpty() ? tr("No entity definitions loaded: classes and keys are unchecked")
											   : tr("%n entity class(es) defined", nullptr, static_cast<int>(m_entityDefinitions.classes.size())),
				m_entityDefinitions.isEmpty() ? QStringLiteral("definitions") : QString());
		}
	}
	if (!m_leakTrailPath.isEmpty()) {
		checklistRow(m_levelChecklist, OperationState::Failed, tr("The last build leaked: follow the trail to the hole"), QStringLiteral("leak"));
	}
	int errors = 0;
	for (const LevelMapIssue& issue : document.issues) {
		errors += issue.severity == LevelMapIssueSeverity::Error ? 1 : 0;
	}
	if (errors > 0) {
		checklistRow(m_levelChecklist, OperationState::Failed, tr("%n error(s) in Health", nullptr, errors), QStringLiteral("health"));
	}
	fitWrappedListRows(m_levelChecklist, 48, 360);
}

QWidget* ApplicationShell::buildLevelFilterOptions()
{
	m_levelFilterPanel = new QWidget;
	m_levelFilterPanel->setObjectName(QStringLiteral("levelViewFilters"));
	auto* layout = new QVBoxLayout(m_levelFilterPanel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	const QString saved = QString::fromUtf8(m_settings.shellLayoutState(QStringLiteral("levelView/filtersOff")));
	m_levelFiltersOff = saved.split(QLatin1Char(','), Qt::SkipEmptyParts);
	refreshLevelViewFilters();
	return m_levelFilterPanel;
}

void ApplicationShell::refreshLevelViewFilters()
{
	if (!m_levelFilterPanel) {
		return;
	}
	auto* layout = qobject_cast<QVBoxLayout*>(m_levelFilterPanel->layout());
	const QString formatKey = QStringLiteral("%1|%2").arg(static_cast<int>(m_levelMapDocument.format)).arg(static_cast<int>(m_levelMapDocument.doomFormat));
	if (formatKey != m_levelFilterFormatKey) {
		m_levelFilterFormatKey = formatKey;
		// Hidden first, so nothing old stays painted while the layout drops it.
		for (QWidget* child : m_levelFilterPanel->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
			child->hide();
			child->deleteLater();
		}
		m_levelFilterBoxes.clear();
		const QVector<LevelViewFilter> filters = levelViewFilters(m_levelMapDocument);
		if (filters.isEmpty()) {
			auto* none = new QLabel(tr("Open a map to filter what the views draw."));
			none->setObjectName(QStringLiteral("sidebarHint"));
			none->setWordWrap(true);
			layout->addWidget(none);
		}
		for (const LevelViewFilter& filter : filters) {
			auto* box = new QCheckBox(filter.title);
			box->setObjectName(QStringLiteral("levelFilter-") + filter.id);
			box->setFocusPolicy(Qt::TabFocus);
			box->setToolTip(filter.description);
			box->setAccessibleDescription(filter.description);
			box->setChecked(!m_levelFiltersOff.contains(filter.id));
			connect(box, &QCheckBox::toggled, this, [this, id = filter.id](bool shown) { setLevelViewFilter(id, shown); });
			layout->addWidget(box);
			m_levelFilterBoxes.insert(filter.id, box);
		}
		if (!filters.isEmpty()) {
			auto* showAll = new QToolButton;
			showAll->setObjectName(QStringLiteral("levelFiltersShowAll"));
			showAll->setText(tr("Show Every Kind"));
			showAll->setIcon(studioIcon(QStringLiteral("eye")));
			showAll->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
			showAll->setAutoRaise(true);
			showAll->setFocusPolicy(Qt::TabFocus);
			setBaseIconSize(showAll, QSize(16, 16));
			connect(showAll, &QToolButton::clicked, this, [this]() {
				for (QCheckBox* box : std::as_const(m_levelFilterBoxes)) {
					const QSignalBlocker blocker(box);
					box->setChecked(true);
				}
				m_levelFiltersOff.clear();
				m_settings.setShellLayoutState(QStringLiteral("levelView/filtersOff"), QByteArray());
				applyLevelViewFilters();
				synchronizeLevelPlanViews();
				refreshLevelMapObjectHiddenMarks();
				if (levelMap3DShowing()) {
					refreshLevelMap3D();
				}
			});
			layout->addWidget(showAll);
		}
	}
	// Counts are said with each kind, for the open map, while the tab shows.
	SidebarPage* page = levelSidebarPage(QStringLiteral("view"));
	if (!page || !page->isVisible() || m_levelFilterBoxes.isEmpty()) {
		return;
	}
	const QHash<QString, int> counts = levelViewFilterCounts(m_levelMapDocument);
	for (const LevelViewFilter& filter : levelViewFilters(m_levelMapDocument)) {
		if (QCheckBox* box = m_levelFilterBoxes.value(filter.id)) {
			const int count = counts.value(filter.id);
			box->setText(tr("%1 (%2)").arg(filter.title).arg(count));
			box->setAccessibleName(tr("Show %1, %n object(s)", nullptr, count).arg(filter.title));
		}
	}
}

void ApplicationShell::applyLevelViewFilters()
{
	if (!m_levelMapViewport || !m_levelMapViewport->hasDocument()) {
		return;
	}
	QStringList off;
	for (const QString& id : std::as_const(m_levelFiltersOff)) {
		if (levelViewFilterForId(m_levelMapDocument, id)) {
			off << id;
		}
	}
	QVector<LevelMapSelectionRef> filtered = off.isEmpty() ? QVector<LevelMapSelectionRef>() : levelViewFilteredObjects(m_levelMapDocument, off);
	// A region hides what lies outside it, unless asked only to shade it.
	if (levelRegionActive() && m_levelRegionHideOutside) {
		filtered += levelMapOutsideRegionObjects(m_levelMapDocument, m_levelRegionMins, m_levelRegionMaxs);
	}
	const QSignalBlocker blocker(m_levelMapViewport);
	m_levelMapViewport->setFilteredObjects(filtered);
}

QWidget* ApplicationShell::buildLevelRegionPanel()
{
	auto* panel = new QWidget;
	panel->setObjectName(QStringLiteral("levelRegionPanel"));
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	m_levelRegionSummary = new QLabel;
	m_levelRegionSummary->setObjectName(QStringLiteral("levelRegionSummary"));
	m_levelRegionSummary->setProperty("sidebarHint", true);
	m_levelRegionSummary->setWordWrap(true);
	m_levelRegionSummary->setTextFormat(Qt::PlainText);
	layout->addWidget(m_levelRegionSummary);
	const auto button = [layout](const QString& name, const QString& text, const QString& icon, const QString& tip) {
		QPushButton* made = createButton(text, icon);
		made->setObjectName(name);
		made->setToolTip(tip);
		made->setAccessibleDescription(tip);
		layout->addWidget(made);
		return made;
	};
	m_levelRegionFromSelection = button(QStringLiteral("levelRegionFromSelection"), tr("Set to Selection"), QStringLiteral("select-box"),
		tr("Make the selection's bounds the region, as Radiant's Region menu does."));
	m_levelRegionFromView = button(QStringLiteral("levelRegionFromView"), tr("Set to View"), QStringLiteral("frame"),
		tr("Make what the active 2D view shows the region, through the whole map's depth."));
	m_levelRegionClear = button(QStringLiteral("levelRegionClear"), tr("Clear Region"), QStringLiteral("close"), tr("Show and keep the whole map again."));
	m_levelRegionCompile = button(QStringLiteral("levelRegionCompile"), tr("Compile Region"), QStringLiteral("play"),
		tr("Write the region beside the map as a sealed map of its own and compile it with the chosen profile, as Radiant compiles a region."));
	m_levelRegionSave = button(QStringLiteral("levelRegionSave"), tr("Save Region As…"), QStringLiteral("save"),
		tr("Write the region as a map of its own, sealed and with a player start, to compile and test on its own."));
	m_levelRegionHide = new QCheckBox(tr("Hide what lies outside"));
	m_levelRegionHide->setObjectName(QStringLiteral("levelRegionHide"));
	m_levelRegionHide->setToolTip(tr("Hide everything outside the region in the views, rather than only shading it."));
	m_levelRegionHide->setChecked(m_levelRegionHideOutside);
	layout->addWidget(m_levelRegionHide);
	connect(m_levelRegionFromSelection, &QPushButton::clicked, this, &ApplicationShell::setLevelRegionFromSelection);
	connect(m_levelRegionFromView, &QPushButton::clicked, this, &ApplicationShell::setLevelRegionFromView);
	connect(m_levelRegionClear, &QPushButton::clicked, this, &ApplicationShell::clearLevelRegion);
	connect(m_levelRegionSave, &QPushButton::clicked, this, &ApplicationShell::saveLevelRegionAs);
	connect(m_levelRegionCompile, &QPushButton::clicked, this, &ApplicationShell::compileLevelRegion);
	connect(m_levelRegionHide, &QCheckBox::toggled, this, [this](bool hide) {
		m_levelRegionHideOutside = hide;
		applyLevelViewFilters();
		synchronizeLevelPlanViews();
		refreshLevelMapObjectHiddenMarks();
		if (levelMap3DShowing()) {
			refreshLevelMap3D();
		}
	});
	refreshLevelRegionPanel();
	return panel;
}

bool ApplicationShell::levelRegionActive() const
{
	return m_levelRegionMins.valid && m_levelRegionMaxs.valid
		&& (m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map);
}

void ApplicationShell::refreshLevelRegionPanel()
{
	// A region belongs to the map it was set on.
	if (m_levelRegionSerial != m_levelMapLoadSerial && (m_levelRegionMins.valid || m_levelRegionMaxs.valid)) {
		m_levelRegionMins = {};
		m_levelRegionMaxs = {};
		for (MapViewport* view : std::as_const(m_levelPlanViews)) {
			view->setRegionBox();
		}
	}
	m_levelRegionSerial = m_levelMapLoadSerial;
	if (!m_levelRegionSummary) {
		return;
	}
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool active = levelRegionActive();
	m_levelRegionFromSelection->setEnabled(quake && !m_levelMapDocument.selection.isEmpty());
	m_levelRegionFromView->setEnabled(quake);
	m_levelRegionClear->setEnabled(active);
	m_levelRegionSave->setEnabled(active);
	m_levelRegionCompile->setEnabled(active && !m_levelMapDocument.sourcePath.isEmpty());
	m_levelRegionHide->setEnabled(active);
	if (!quake) {
		m_levelRegionSummary->setText(m_levelMapDocument.format == LevelMapFormat::Unknown ? tr("Open a map to set a region.")
																							 : tr("Regions are for Quake-family maps."));
		return;
	}
	if (!active) {
		m_levelRegionSummary->setText(tr("No region: the views show the whole map. A region keeps them to one area and saves it as a map of its own."));
		return;
	}
	const int kept = static_cast<int>(levelMapRegionObjects(m_levelMapDocument, m_levelRegionMins, m_levelRegionMaxs).size());
	m_levelRegionSummary->setText(tr("Region %1 × %2 × %3 units, keeping %n object(s).", nullptr, kept)
			.arg(m_levelRegionMaxs.x - m_levelRegionMins.x, 0, 'g', 8)
			.arg(m_levelRegionMaxs.y - m_levelRegionMins.y, 0, 'g', 8)
			.arg(m_levelRegionMaxs.z - m_levelRegionMins.z, 0, 'g', 8));
}

void ApplicationShell::setLevelRegion(const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	m_levelRegionMins = mins;
	m_levelRegionMaxs = maxs;
	m_levelRegionSerial = m_levelMapLoadSerial;
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		view->setRegionBox(mins, maxs);
	}
	applyLevelViewFilters();
	synchronizeLevelPlanViews();
	refreshLevelMapObjectHiddenMarks();
	if (levelMap3DShowing()) {
		refreshLevelMap3D();
	}
	refreshLevelRegionPanel();
}

void ApplicationShell::setLevelRegionFromSelection()
{
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs) || maxs.x - mins.x < 1 || maxs.y - mins.y < 1 || maxs.z - mins.z < 1) {
		statusBar()->showMessage(tr("Select objects that span some space on every axis to make a region of them."));
		return;
	}
	setLevelRegion(mins, maxs);
	showLevelSidebarPage(QStringLiteral("view"), QStringLiteral("view.region"));
	statusBar()->showMessage(tr("The region is set to the selection."), 4000);
}

void ApplicationShell::setLevelRegionFromView()
{
	if (!m_levelMapViewport) {
		return;
	}
	const LevelMapStatistics statistics = levelMapStatistics(m_levelMapDocument);
	if (!statistics.mins.valid || !statistics.maxs.valid) {
		statusBar()->showMessage(tr("The map has nothing to make a region of yet."));
		return;
	}
	// What the view shows across its plane, through the map's whole depth.
	const LevelMapVec3 a = m_levelMapViewport->worldPositionAt(QPointF(0, 0), 0.0);
	const LevelMapVec3 b = m_levelMapViewport->worldPositionAt(QPointF(m_levelMapViewport->width(), m_levelMapViewport->height()), 0.0);
	LevelMapVec3 mins {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z), true};
	LevelMapVec3 maxs {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z), true};
	switch (m_levelMapViewport->projection()) {
	case MapViewportProjection::TopXY:
		mins.z = statistics.mins.z;
		maxs.z = statistics.maxs.z;
		break;
	case MapViewportProjection::FrontXZ:
		mins.y = statistics.mins.y;
		maxs.y = statistics.maxs.y;
		break;
	case MapViewportProjection::SideZY:
		mins.x = statistics.mins.x;
		maxs.x = statistics.maxs.x;
		break;
	}
	setLevelRegion(mins, maxs);
	statusBar()->showMessage(tr("The region is set to what the view shows."), 4000);
}

void ApplicationShell::clearLevelRegion()
{
	m_levelRegionMins = {};
	m_levelRegionMaxs = {};
	setLevelRegion({}, {});
	statusBar()->showMessage(tr("Region cleared: the views show the whole map."), 4000);
}

void ApplicationShell::compileLevelRegion()
{
	if (!levelRegionActive()) {
		statusBar()->showMessage(tr("Set a region before compiling it."));
		return;
	}
	if (m_compilerRunThread) {
		statusBar()->showMessage(tr("A compiler task is already running"));
		return;
	}
	if (m_levelMapDocument.sourcePath.isEmpty()) {
		statusBar()->showMessage(tr("Save the map first; the region is written beside it."));
		return;
	}
	// The region map sits beside the map, as Radiant writes its region, so
	// its BSP lands in the same folder; unsaved edits are included.
	const QFileInfo source(m_levelMapDocument.sourcePath);
	const QString path = source.absoluteDir().filePath(source.completeBaseName() + QStringLiteral("-region.map"));
	LevelMapVec3 start;
	if (m_levelMap3D) {
		const ModelVec3 camera = m_levelMap3D->cameraPosition();
		start = {camera.x, camera.y, camera.z, true};
	}
	const QString seal = m_levelMapDocument.format == LevelMapFormat::Quake3Map ? QStringLiteral("common/caulk") : levelBrushMaterial();
	LevelMapDocument region;
	LevelMapRegionReport report;
	QString error;
	if (!levelMapRegionDocument(m_levelMapDocument, m_levelRegionMins, m_levelRegionMaxs, start, seal, &region, &report, &error)) {
		statusBar()->showMessage(tr("Could not make the region map: %1").arg(error), 6000);
		return;
	}
	const LevelMapSaveReport saved = saveLevelMapAs(region, path, false, true);
	if (!saved.written) {
		statusBar()->showMessage(tr("Could not write the region: %1").arg(saved.errors.join(QStringLiteral("; "))), 6000);
		return;
	}
	region.sourcePath = path;
	runLevelMapCompile(region);
}

void ApplicationShell::saveLevelRegionAs()
{
	if (!levelRegionActive()) {
		statusBar()->showMessage(tr("Set a region before saving it."));
		return;
	}
	const QFileInfo source(m_levelMapDocument.sourcePath);
	const QString suggested = source.absoluteDir().filePath(source.completeBaseName() + QStringLiteral("-region.map"));
	const QString path = QFileDialog::getSaveFileName(this, tr("Save Region As"), suggested, tr("Quake-family maps (*.map)"));
	if (path.isEmpty()) {
		return;
	}
	// The player starts where the camera stands, when that is in the region.
	LevelMapVec3 start;
	if (m_levelMap3D) {
		const ModelVec3 camera = m_levelMap3D->cameraPosition();
		start = {camera.x, camera.y, camera.z, true};
	}
	const QString seal = m_levelMapDocument.format == LevelMapFormat::Quake3Map ? QStringLiteral("common/caulk") : levelBrushMaterial();
	LevelMapDocument region;
	LevelMapRegionReport report;
	QString error;
	if (!levelMapRegionDocument(m_levelMapDocument, m_levelRegionMins, m_levelRegionMaxs, start, seal, &region, &report, &error)) {
		statusBar()->showMessage(tr("Could not make the region map: %1").arg(error), 6000);
		return;
	}
	const LevelMapSaveReport saved = saveLevelMapAs(region, path, false, true);
	if (!saved.written) {
		statusBar()->showMessage(tr("Could not save the region: %1").arg(saved.errors.join(QStringLiteral("; "))), 6000);
		return;
	}
	recordActivity(tr("Region saved"), QDir::toNativeSeparators(path), QStringLiteral("level-map"), OperationState::Completed,
		tr("%n object(s) kept, sealed by %1 brushes.", nullptr, report.kept).arg(report.sealBrushes));
	statusBar()->showMessage(report.playerStartAdded
			? tr("Saved the region with %n object(s) and a player start to %1.", nullptr, report.kept).arg(QDir::toNativeSeparators(path))
			: tr("Saved the region with %n object(s) to %1.", nullptr, report.kept).arg(QDir::toNativeSeparators(path)),
		6000);
}

void ApplicationShell::setLevelViewFilter(const QString& filterId, bool shown)
{
	if (shown) {
		m_levelFiltersOff.removeAll(filterId);
	} else if (!m_levelFiltersOff.contains(filterId)) {
		m_levelFiltersOff << filterId;
	}
	m_settings.setShellLayoutState(QStringLiteral("levelView/filtersOff"), m_levelFiltersOff.join(QLatin1Char(',')).toUtf8());
	applyLevelViewFilters();
	synchronizeLevelPlanViews();
	refreshLevelMapObjectHiddenMarks();
	if (levelMap3DShowing()) {
		refreshLevelMap3D();
	}
	LevelViewFilter filter;
	if (levelViewFilterForId(m_levelMapDocument, filterId, &filter)) {
		statusBar()->showMessage(shown ? tr("%1 shown again.").arg(filter.title) : tr("%1 hidden from the views; the map is unchanged.").arg(filter.title), 4000);
	}
}

QWidget* ApplicationShell::buildLevelTransformPanel()
{
	auto* panel = new QWidget;
	panel->setObjectName(QStringLiteral("levelTransformPanel"));
	auto* grid = new QGridLayout(panel);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(4);
	grid->setVerticalSpacing(4);
	const QStringList axes {tr("X"), tr("Y"), tr("Z")};
	for (int axis = 0; axis < 3; ++axis) {
		auto* heading = new QLabel(axes.at(axis));
		heading->setObjectName(QStringLiteral("sidebarFieldLabel"));
		heading->setAlignment(Qt::AlignCenter);
		grid->addWidget(heading, 0, axis + 1);
	}
	const auto field = [this, grid](bool size, int axis) {
		auto* spin = new QDoubleSpinBox;
		spin->setObjectName(QStringLiteral("levelTransform%1%2").arg(size ? QStringLiteral("Size") : QStringLiteral("Centre")).arg(QChar(u'X' + axis)));
		spin->setRange(size ? 0.0 : -131072.0, 131072.0);
		spin->setDecimals(2);
		spin->setKeyboardTracking(false);
		spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
		spin->setAlignment(Qt::AlignRight);
		spin->setMinimumWidth(40);
		spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		const QString axisName = QStringList {tr("X"), tr("Y"), tr("Z")}.at(axis);
		spin->setAccessibleName(size ? tr("Size along %1").arg(axisName) : tr("Centre %1").arg(axisName));
		spin->setToolTip(size ? tr("The selection's size along %1, in map units. A new size keeps its lower corner where it is.").arg(axisName)
							  : tr("Where the middle of the selection is along %1, in map units. A new value moves it there.").arg(axisName));
		connect(spin, &QDoubleSpinBox::editingFinished, this, [this, size, axis]() { applyLevelTransformField(size, axis); });
		grid->addWidget(spin, size ? 2 : 1, axis + 1);
		(size ? m_levelSizeFields : m_levelPositionFields)[axis] = spin;
	};
	auto* centre = new QLabel(tr("Centre"));
	centre->setObjectName(QStringLiteral("sidebarFieldLabel"));
	auto* size = new QLabel(tr("Size"));
	size->setObjectName(QStringLiteral("sidebarFieldLabel"));
	grid->addWidget(centre, 1, 0);
	grid->addWidget(size, 2, 0);
	for (int axis = 0; axis < 3; ++axis) {
		field(false, axis);
		field(true, axis);
	}
	grid->setColumnStretch(1, 1);
	grid->setColumnStretch(2, 1);
	grid->setColumnStretch(3, 1);
	m_levelTransformNote = new QLabel;
	m_levelTransformNote->setProperty("sidebarHint", true);
	m_levelTransformNote->setObjectName(QStringLiteral("levelTransformNote"));
	m_levelTransformNote->setWordWrap(true);
	grid->addWidget(m_levelTransformNote, 3, 0, 1, 4);
	refreshLevelTransformPanel();
	return panel;
}

void ApplicationShell::refreshLevelTransformPanel()
{
	if (!m_levelTransformNote || !m_levelPositionFields[0]) {
		return;
	}
	const QScopedValueRollback<bool> filling(m_fillingLevelTransform, true);
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	const bool bounded = m_levelMapDocument.format != LevelMapFormat::Unknown && !m_levelMapDocument.selection.isEmpty()
		&& levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs);
	const bool resizable = bounded && levelMapSelectionResizable();
	const double low[3] = {mins.x, mins.y, mins.z};
	const double high[3] = {maxs.x, maxs.y, maxs.z};
	// Doom has no heights to place things at here; its Z stays out of reach.
	const bool doom = m_levelMapDocument.format == LevelMapFormat::DoomWad;
	for (int axis = 0; axis < 3; ++axis) {
		QDoubleSpinBox* position = m_levelPositionFields[axis];
		QDoubleSpinBox* size = m_levelSizeFields[axis];
		const bool usable = bounded && !(doom && axis == 2);
		position->setEnabled(usable);
		size->setEnabled(usable && resizable);
		position->setValue(usable ? (low[axis] + high[axis]) / 2.0 : 0.0);
		size->setValue(usable ? high[axis] - low[axis] : 0.0);
		position->setSpecialValueText(usable ? QString() : QStringLiteral("–"));
		size->setSpecialValueText(usable && resizable ? QString() : QStringLiteral("–"));
		if (!usable) {
			position->setValue(position->minimum());
			size->setValue(size->minimum());
		}
	}
	const int count = static_cast<int>(m_levelMapDocument.selection.size());
	m_levelTransformNote->setText(!bounded ? tr("Select objects to see and set where they are and how big.")
										   : (resizable ? tr("%n object(s). Type a centre to move them there, or a size to resize them.", nullptr, count)
														: tr("%n object(s). Type a centre to move them there.", nullptr, count)));
	if (m_levelTransformSection) {
		m_levelTransformSection->setBadge(bounded ? QString::number(count) : QString());
	}
}

void ApplicationShell::applyLevelTransformField(bool size, int axis)
{
	if (m_fillingLevelTransform || axis < 0 || axis > 2) {
		return;
	}
	QDoubleSpinBox* spin = (size ? m_levelSizeFields : m_levelPositionFields)[axis];
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!spin || !spin->isEnabled() || !levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs)) {
		return;
	}
	const double low[3] = {mins.x, mins.y, mins.z};
	const double high[3] = {maxs.x, maxs.y, maxs.z};
	const double wanted = spin->value();
	// Only the field edited applies, and only when it moved by more than the
	// spin box's own rounding, so typing Tab through the fields changes nothing.
	const double current = size ? high[axis] - low[axis] : (low[axis] + high[axis]) / 2.0;
	if (std::abs(wanted - current) < 0.005) {
		return;
	}
	if (size) {
		LevelMapVec3 newMaxs = maxs;
		(axis == 0 ? newMaxs.x : (axis == 1 ? newMaxs.y : newMaxs.z)) = low[axis] + wanted;
		if (applyLevelMapResize(mins, newMaxs)) {
			statusBar()->showMessage(tr("Resized the selection along %1 to %2 units.").arg(QStringList {tr("X"), tr("Y"), tr("Z")}.at(axis)).arg(wanted), 4000);
		}
		refreshLevelTransformPanel();
		return;
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::Move;
	request.offset = {axis == 0 ? wanted - current : 0.0, axis == 1 ? wanted - current : 0.0, axis == 2 ? wanted - current : 0.0, true};
	request.grid = 0.0;
	request.snapMoveDelta = false;
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	QString error;
	if (!runLevelPlacementFromUi(request, &error)) {
		statusBar()->showMessage(tr("Move failed: %1").arg(error), 5000);
		refreshLevelTransformPanel();
		return;
	}
	recordActivity(tr("Level map selection moved"), QString::number(wanted), QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
}

void ApplicationShell::refreshLevelSidebarBadges()
{
	const auto badge = [this](const QString& id, int count) {
		if (SidebarPage* page = levelSidebarPage(id)) {
			page->setBadge(count > 0 ? QString::number(count) : QString());
		}
	};
	const bool hasMap = m_levelMapDocument.format != LevelMapFormat::Unknown;
	badge(QStringLiteral("outliner"), hasMap && m_levelMapObjects ? m_levelMapObjects->objectModel()->objectCount() : 0);
	int problems = 0;
	for (const LevelMapIssue& issue : std::as_const(m_levelMapDocument.issues)) {
		problems += issue.severity != LevelMapIssueSeverity::Info ? 1 : 0;
	}
	problems += m_entityDefinitions.isEmpty() ? 0 : m_entityValidation.issueCount;
	badge(QStringLiteral("health"), hasMap ? problems : 0);
	badge(QStringLiteral("history"), hasMap ? static_cast<int>(m_levelMapDocument.undoStack.size()) : 0);
	int tiles = 0;
	if (m_levelMapTextures) {
		for (int row = 0; row < m_levelMapTextures->count(); ++row) {
			tiles += m_levelMapTextures->item(row)->flags().testFlag(Qt::ItemIsSelectable) ? 1 : 0;
		}
	}
	badge(QStringLiteral("textures"), hasMap ? tiles : 0);
	int classes = 0;
	if (m_levelMapPalette) {
		for (QTreeWidgetItemIterator it(m_levelMapPalette); *it; ++it) {
			classes += (*it)->data(0, Qt::UserRole).toString().isEmpty() ? 0 : 1;
		}
	}
	badge(QStringLiteral("entities"), hasMap ? classes : 0);
}

} // namespace vibestudio
