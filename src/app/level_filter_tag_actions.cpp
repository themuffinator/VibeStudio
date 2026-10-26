// View filters that data defines: tags from the project manifest or a chosen
// tag file (a TrenchBroom game configuration works as it is), and one filter
// for each entity class of the loaded definitions and the map, for the map
// session. Each has a checkable Toggle <name> Visible command, registered
// again whenever the tags or classes change, so the command palette finds
// it and Keyboard Shortcuts can bind it; the View tab shows the tags among
// the Filters and the classes in Entity Classes. They share the switched-off
// list of the built-in kinds (level_view_filter_actions.cpp), so a key, a
// check box and the menus always agree. Filters change what is drawn and
// picked, never the map, so nothing here touches undo.
//
// Workflow reference: TB ActionManager::createTagActions and
// createEntityDefinitionActions, MapViewBase::toggleTagVisible and
// toggleEntityDefinitionVisible, and ViewEditor's tag check boxes and
// EntityDefinitionCheckBoxList at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/studio_actions.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_sidebar.h"
#include "core/level_filter_tags.h"
#include "core/level_smart_tags.h"
#include "core/level_view_filters.h"
#include "core/project_manifest.h"

#include <QAction>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vibestudio {

namespace {

QString tagFileSettingsKey()
{
	return QStringLiteral("levelView/filterTagFile");
}

// When a file last changed and how large it is, so an edit made outside the
// studio reads the tags again.
QString fileStamp(const QString& path)
{
	if (path.isEmpty()) {
		return QStringLiteral("-");
	}
	const QFileInfo info(path);
	return info.exists() ? QString::number(info.lastModified().toMSecsSinceEpoch()) + QLatin1Char(':') + QString::number(info.size()) : QStringLiteral("none");
}

bool samePath(const QString& left, const QString& right)
{
	if (left.isEmpty() || right.isEmpty()) {
		return left == right;
	}
	const QString a = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
	const QString b = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
#ifdef Q_OS_WIN
	return a.compare(b, Qt::CaseInsensitive) == 0;
#else
	return a == b;
#endif
}

bool quakeFamily(const LevelMapDocument& document)
{
	return levelFilterTagsApply(document);
}

constexpr int kClassIdRole = Qt::UserRole + 1;

} // namespace

const LevelFilterTagSet& ApplicationShell::currentLevelFilterTags() const
{
	static const LevelFilterTagSet none;
	return m_levelFilterTags ? *m_levelFilterTags : none;
}

LevelViewFilterHidden ApplicationShell::currentLevelViewFilterHidden() const
{
	// Only what the open map can use: its format's kinds, the loaded tags and
	// class ids on a Quake-family map.
	QStringList off;
	const bool quake = quakeFamily(m_levelMapDocument);
	const LevelFilterTagSet& tags = currentLevelFilterTags();
	for (const QString& id : std::as_const(m_levelFiltersOff)) {
		if (isLevelFilterTagId(id)) {
			if (quake && tags.find(id)) {
				off << id;
			}
		} else if (levelFilterClassForId(id)) {
			if (quake) {
				off << id;
			}
		} else if (levelViewFilterForId(m_levelMapDocument, id)) {
			off << id;
		}
	}
	if (off.isEmpty()) {
		return {};
	}
	return levelViewFilterHidden(m_levelMapDocument, off, currentLevelFilterMaterials(), tags);
}

QString ApplicationShell::levelViewFilterTitle(const QString& filterId) const
{
	if (const LevelFilterTag* tag = currentLevelFilterTags().find(filterId)) {
		return tag->name;
	}
	QString className;
	if (levelFilterClassForId(filterId, &className)) {
		for (const LevelMapEntity& entity : m_levelMapDocument.entities) {
			if (entity.className.trimmed().compare(className, Qt::CaseInsensitive) == 0) {
				return entity.className.trimmed();
			}
		}
		return className;
	}
	LevelViewFilter filter;
	return levelViewFilterForId(m_levelMapDocument, filterId, &filter) ? filter.title : QString();
}

void ApplicationShell::syncLevelClassFilterSession()
{
	// Class filters belong to the map session, as TrenchBroom's hidden
	// entity definitions belong to its document: opening another map shows
	// every class again, while reloading the same file keeps them.
	if (m_levelClassFiltersLoad == m_levelMapLoadSerial) {
		return;
	}
	m_levelClassFiltersLoad = m_levelMapLoadSerial;
	if (samePath(m_levelClassFiltersPath, m_levelMapDocument.sourcePath) && !m_levelMapDocument.sourcePath.isEmpty()) {
		return;
	}
	m_levelClassFiltersPath = m_levelMapDocument.sourcePath;
	const QStringList kept = levelViewFiltersWithoutClasses(m_levelFiltersOff);
	if (kept.size() != m_levelFiltersOff.size()) {
		m_levelFiltersOff = kept;
		for (auto it = m_levelFilterBoxes.cbegin(); it != m_levelFilterBoxes.cend(); ++it) {
			if (QCheckBox* box = it.value()) {
				const QSignalBlocker blocker(box);
				box->setChecked(!m_levelFiltersOff.contains(it.key()));
			}
		}
		// The class commands and rows show every class again at once.
		refreshLevelDataFilterCommands();
		refreshLevelClassFilterPanel();
	}
}

void ApplicationShell::refreshLevelFilterTags()
{
	// View > Filters lists the tags after the built-in kinds.
	if (m_viewMenu) {
		for (QMenu* menu : m_viewMenu->findChildren<QMenu*>()) {
			if (menu->title() == tr("Filters")) {
				attachLevelFilterTagMenu(menu);
			}
		}
	}
	const QString projectPath = m_settings.currentProjectPath();
	const QString manifestPath = projectPath.isEmpty() ? QString() : projectManifestPath(projectPath);
	const QString tagFile = QString::fromUtf8(m_settings.shellLayoutState(tagFileSettingsKey())).trimmed();
	QString key = projectPath + QLatin1Char('|') + fileStamp(manifestPath) + QLatin1Char('|') + tagFile + QLatin1Char('|') + fileStamp(tagFile);
	// A file the manifest names counts too.
	for (const QString& source : currentLevelFilterTags().sources) {
		if (!samePath(source, manifestPath) && !samePath(source, tagFile)) {
			key += QLatin1Char('|') + source + QLatin1Char('=') + fileStamp(source);
		}
	}
	if (key == m_levelFilterTagsKey) {
		return;
	}
	auto set = std::make_shared<LevelFilterTagSet>();
	if (!projectPath.isEmpty()) {
		ProjectManifest manifest;
		if (loadProjectManifest(projectPath, &manifest)) {
			mergeLevelFilterTags(set.get(), levelFilterTagsForProject(manifest));
		}
	}
	if (!tagFile.isEmpty()) {
		LevelFilterTagSet fromFile;
		QString error;
		if (!loadLevelFilterTagFile(tagFile, &fromFile, &error)) {
			set->warnings << error;
		}
		mergeLevelFilterTags(set.get(), fromFile);
	}
	// The key also names files the manifest pointed at, now that they are known.
	QString loadedKey = projectPath + QLatin1Char('|') + fileStamp(manifestPath) + QLatin1Char('|') + tagFile + QLatin1Char('|') + fileStamp(tagFile);
	for (const QString& source : std::as_const(set->sources)) {
		if (!samePath(source, manifestPath) && !samePath(source, tagFile)) {
			loadedKey += QLatin1Char('|') + source + QLatin1Char('=') + fileStamp(source);
		}
	}
	const QStringList before = currentLevelFilterTags().ids();
	m_levelFilterTags = set;
	m_levelFilterTagsKey = loadedKey;
	if (m_commands) {
		// Every tag gets a Toggle <tag> Visible command, registered again in
		// one batch when the tags change; keys given to a tag stay with it.
		m_commands->beginBatch();
		for (const QString& id : std::as_const(m_levelFilterTagCommandIds)) {
			m_commands->unregisterCommand(id);
		}
		m_levelFilterTagCommandIds.clear();
		for (const QString& id : std::as_const(m_levelSmartTagCommandIds)) { m_commands->unregisterCommand(id); }
		m_levelSmartTagCommandIds.clear();
		QWidget* levels = m_modeStack ? m_modeStack->widget(static_cast<int>(StudioMode::Levels)) : nullptr;
		for (const LevelFilterTag& tag : std::as_const(set->tags)) {
			StudioCommandRegistration command;
			command.commandId = levelFilterTagCommandId(tag.id);
			command.group = StudioCommandGroup::View;
			command.label = tr("Toggle %1 Visible").arg(tag.name);
			command.statusTip = levelFilterTagDescription(tag) + QLatin1Char(' ') + tr("Checked while the Levels views draw it; the map is unchanged.");
			command.iconName = QStringLiteral("filter");
			command.menuSection = tr("Filters");
			command.checkable = true;
			const QString id = command.commandId;
			command.handler = [this, id, tagId = tag.id]() {
				const QAction* action = m_commands->action(id);
				setLevelViewFilter(tagId, action && action->isChecked());
			};
			if (QAction* action = m_commands->registerCommand(command)) {
				action->setIcon(QIcon());
			}
			if (levels) {
				m_commands->setShortcutScopes(command.commandId, {levels});
			}
			m_levelFilterTagCommandIds << command.commandId;
			for (const bool enabled : {true, false}) {
				if (!enabled && !levelSmartTagCanDisable(tag)) { continue; }
				StudioCommandRegistration edit;
				edit.commandId = levelSmartTagCommandId(tag.id, enabled);
				edit.group = StudioCommandGroup::Edit;
				edit.label = enabled ? tr("Make %1").arg(tag.name) : tr("Make non-%1").arg(tag.name);
				edit.statusTip = enabled ? tr("Apply the %1 tag to the selection in one undo step.").arg(tag.name)
					: tr("Remove the %1 flag or classname tag from the selection in one undo step.").arg(tag.name);
				edit.iconName = QStringLiteral("brush");
				edit.menuSection = tr("Smart Tags");
				edit.handler = [this, tagId = tag.id, enabled]() { applyLevelSmartTagFromUi(tagId, enabled); };
				m_commands->registerCommand(edit);
				if (levels) { m_commands->setShortcutScopes(edit.commandId, {levels}); }
				m_levelSmartTagCommandIds << edit.commandId;
			}
		}
		m_commands->endBatch();
		refreshLevelViewportShortcuts();
	}
	// Never a real key, so the boxes are built again even for no tags.
	m_levelFilterTagBoxesKey = QStringLiteral("\t-");
	refreshLevelFilterTagBoxes();
	refreshLevelDataFilterCommands();
	if (before != set->ids() || !before.isEmpty()) {
		// Counts, every plan view and the camera follow the new tags.
		applyLevelViewFilters();
		synchronizeLevelPlanViews();
		refreshLevelMapObjectHiddenMarks();
		if (levelMap3DShowing()) {
			refreshLevelMap3D();
		}
	}
}

void ApplicationShell::refreshLevelFilterTagBoxes()
{
	QWidget* panel = m_levelFilterTagPanel;
	if (!panel) {
		return;
	}
	const bool quake = quakeFamily(m_levelMapDocument);
	panel->setVisible(quake);
	const LevelFilterTagSet& tags = currentLevelFilterTags();
	QStringList parts;
	for (const LevelFilterTag& tag : tags.tags) {
		parts << tag.id + QLatin1Char('\t') + tag.name + QLatin1Char('\t') + tag.source;
	}
	const QString key = parts.join(QLatin1Char('\n'));
	auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
	if (key != m_levelFilterTagBoxesKey && layout) {
		m_levelFilterTagBoxesKey = key;
		for (auto it = m_levelFilterBoxes.begin(); it != m_levelFilterBoxes.end();) {
			if (isLevelFilterTagId(it.key())) {
				if (QCheckBox* box = it.value()) {
					box->hide();
					box->deleteLater();
				}
				it = m_levelFilterBoxes.erase(it);
			} else {
				++it;
			}
		}
		int insertAt = 0;
		for (const LevelFilterTag& tag : tags.tags) {
			auto* box = new QCheckBox(tag.name);
			box->setObjectName(QStringLiteral("levelFilterTag-") + tag.id.mid(4));
			box->setFocusPolicy(Qt::TabFocus);
			const QString description = levelFilterTagDescription(tag);
			box->setToolTip(description);
			box->setAccessibleDescription(description);
			box->setChecked(!m_levelFiltersOff.contains(tag.id));
			connect(box, &QCheckBox::toggled, this, [this, id = tag.id](bool shown) { setLevelViewFilter(id, shown); });
			// The heading is first; the boxes go after it, the status and the
			// buttons stay last.
			layout->insertWidget(1 + insertAt++, box);
			m_levelFilterBoxes.insert(tag.id, box);
		}
	}
	if (QLabel* status = m_levelFilterTagStatus) {
		QStringList names;
		for (const QString& source : tags.sources) {
			names << QFileInfo(source).fileName();
		}
		const QString tagFile = QString::fromUtf8(m_settings.shellLayoutState(tagFileSettingsKey())).trimmed();
		QString text;
		if (tags.isEmpty()) {
			text = tr("No tags yet. Add levelFilterTags to the project manifest, or choose a tag file such as a TrenchBroom game configuration.");
		} else {
			const int count = static_cast<int>(tags.tags.size());
			text = tr("%n tag(s) from %1.", nullptr, count).arg(names.join(QStringLiteral(", ")));
		}
		if (!tags.warnings.isEmpty()) {
			const int problems = static_cast<int>(tags.warnings.size());
			text += QLatin1Char(' ') + tr("%n problem(s); point here for details.", nullptr, problems);
		}
		status->setText(text);
		QStringList tip;
		for (const QString& source : tags.sources) {
			tip << QDir::toNativeSeparators(source);
		}
		tip += tags.warnings.mid(0, 12);
		status->setToolTip(tip.join(QLatin1Char('\n')));
		status->setAccessibleDescription(text);
		status->setProperty("tagCount", static_cast<int>(tags.tags.size()));
		if (m_levelFilterTagClear) {
			m_levelFilterTagClear->setEnabled(!tagFile.isEmpty());
		}
	}
	// Counts for the open map while the tab shows, objects and faces apart.
	SidebarPage* page = levelSidebarPage(QStringLiteral("view"));
	if (!quake || !page || !page->isVisible()) {
		return;
	}
	for (const LevelFilterTag& tag : tags.tags) {
		QCheckBox* box = m_levelFilterBoxes.value(tag.id);
		if (!box) {
			continue;
		}
		const int objects = static_cast<int>(levelFilterTagMatches(m_levelMapDocument, tag, currentLevelFilterMaterials()).size());
		const int faces = static_cast<int>(levelFilterTagFaceMatches(m_levelMapDocument, tag, currentLevelFilterMaterials()).size());
		box->setText(faces > 0 ? tr("%1 (%2, %n face(s))", nullptr, faces).arg(tag.name).arg(objects) : tr("%1 (%2)").arg(tag.name).arg(objects));
		const QString objectText = tr("%n object(s)", nullptr, objects);
		box->setAccessibleName(faces > 0 ? tr("Show %1, %2 and %n face(s)", nullptr, faces).arg(tag.name, objectText)
										 : tr("Show %1, %n object(s)", nullptr, objects).arg(tag.name));
	}
}

void ApplicationShell::refreshLevelDataFilterCommands()
{
	if (!m_commands) {
		return;
	}
	const bool quake = quakeFamily(m_levelMapDocument);
	refreshLevelSmartTagCommands();
	for (const LevelFilterTag& tag : currentLevelFilterTags().tags) {
		const QString id = levelFilterTagCommandId(tag.id);
		if (QAction* action = m_commands->action(id)) {
			action->setChecked(!m_levelFiltersOff.contains(tag.id));
			m_commands->setEnabled(id, quake);
		}
	}
	// A Toggle <class> Visible command for every class the definitions or
	// the map name, registered again in one batch when the classes change.
	const QVector<LevelFilterClass> classes = levelFilterClasses(m_levelMapDocument, quake ? levelEntityCatalogue() : EntityDefinitionCatalogue());
	QStringList names;
	names.reserve(classes.size());
	for (const LevelFilterClass& entry : classes) {
		names << entry.className;
	}
	const QString key = names.join(QLatin1Char('\n'));
	if (key != m_levelClassFilterCommandsKey) {
		m_levelClassFilterCommandsKey = key;
		m_commands->beginBatch();
		for (const QString& id : std::as_const(m_levelClassFilterCommandIds)) {
			m_commands->unregisterCommand(id);
		}
		m_levelClassFilterCommandIds.clear();
		QWidget* levels = m_modeStack ? m_modeStack->widget(static_cast<int>(StudioMode::Levels)) : nullptr;
		const QString section = tr("Entity Classes");
		QSet<QString> tokens;
		for (const LevelFilterClass& entry : classes) {
			const QString commandId = levelFilterClassCommandId(entry.className);
			// Two spellings of one class share a command.
			QString token = commandId.toLower();
			token.replace(QLatin1Char('_'), QLatin1Char('-'));
			if (commandId.isEmpty() || tokens.contains(token)) {
				continue;
			}
			tokens.insert(token);
			StudioCommandRegistration command;
			command.commandId = commandId;
			command.group = StudioCommandGroup::View;
			command.label = tr("Toggle %1 Visible").arg(entry.className);
			command.statusTip = tr("Show or hide every %1 entity in the Levels views, those added later included, for this map session. "
								   "Checked while the views draw them; the map is unchanged.")
									.arg(entry.className);
			command.iconName = QStringLiteral("entity");
			command.menuSection = section;
			command.checkable = true;
			command.handler = [this, commandId, classId = entry.id]() {
				const QAction* action = m_commands->action(commandId);
				setLevelClassFiltersShown({classId}, action && action->isChecked());
			};
			if (QAction* action = m_commands->registerCommand(command)) {
				action->setIcon(QIcon());
			}
			if (levels) {
				m_commands->setShortcutScopes(commandId, {levels});
			}
			m_levelClassFilterCommandIds << commandId;
		}
		m_commands->endBatch();
		refreshLevelViewportShortcuts();
	}
	for (const QString& commandId : std::as_const(m_levelClassFilterCommandIds)) {
		QString className;
		QAction* action = m_commands->action(commandId);
		if (action && levelFilterClassForCommand(commandId, &className)) {
			action->setChecked(!m_levelFiltersOff.contains(levelFilterClassId(className)));
			m_commands->setEnabled(commandId, quake);
		}
	}
}

void ApplicationShell::chooseLevelFilterTagFile()
{
	const QString current = QString::fromUtf8(m_settings.shellLayoutState(tagFileSettingsKey())).trimmed();
	const QString start = current.isEmpty() ? m_settings.currentProjectPath() : QFileInfo(current).absolutePath();
	const QString path = QFileDialog::getOpenFileName(this, tr("Choose a Tag File"), start,
		tr("Tag files and TrenchBroom game configurations (*.json *.cfg);;All files (*.*)"));
	if (path.isEmpty()) {
		return;
	}
	LevelFilterTagSet probe;
	QString error;
	if (!loadLevelFilterTagFile(path, &probe, &error)) {
		statusBar()->showMessage(tr("Could not use %1 for tags: %2").arg(QDir::toNativeSeparators(path), error), 8000);
		return;
	}
	m_settings.setShellLayoutState(tagFileSettingsKey(), QDir::cleanPath(path).toUtf8());
	refreshLevelFilterTags();
	const int count = static_cast<int>(probe.tags.size());
	statusBar()->showMessage(tr("%n tag(s) read from %1; each has a Toggle Visible command. The map is unchanged.", nullptr, count)
								 .arg(QFileInfo(path).fileName()),
		6000);
}

void ApplicationShell::clearLevelFilterTagFile()
{
	m_settings.setShellLayoutState(tagFileSettingsKey(), QByteArray());
	refreshLevelFilterTags();
	statusBar()->showMessage(tr("The tag file is no longer used; the project's own tags stay."), 4000);
}

void ApplicationShell::attachLevelFilterTagMenu(QMenu* menu)
{
	if (!menu || menu->property("levelFilterTagsAttached").toBool()) {
		return;
	}
	menu->setProperty("levelFilterTagsAttached", true);
	// The tag commands follow the loaded tags, so the menu adds the current
	// ones each time it opens, after the built-in kinds.
	connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
		for (QAction* action : menu->actions()) {
			if (action->property("levelFilterTagEntry").toBool()) {
				menu->removeAction(action);
				if (action->isSeparator()) {
					action->deleteLater();
				}
			}
		}
		QList<QAction*> tagActions;
		for (const QString& id : std::as_const(m_levelFilterTagCommandIds)) {
			if (QAction* action = m_commands ? m_commands->action(id) : nullptr) {
				tagActions << action;
			}
		}
		if (tagActions.isEmpty()) {
			return;
		}
		// Before Invert Filters and Show Every Kind, when the menu has them.
		QAction* before = m_commands ? m_commands->action(QStringLiteral("map.invertFilters")) : nullptr;
		if (before && !menu->actions().contains(before)) {
			before = nullptr;
		}
		QAction* separator = new QAction(menu);
		separator->setSeparator(true);
		separator->setProperty("levelFilterTagEntry", true);
		menu->insertAction(before, separator);
		for (QAction* action : std::as_const(tagActions)) {
			action->setProperty("levelFilterTagEntry", true);
			menu->insertAction(before, action);
		}
	});
}

QWidget* ApplicationShell::buildLevelClassFilterPanel()
{
	auto* panel = new QWidget;
	panel->setObjectName(QStringLiteral("levelClassFilters"));
	m_levelClassFilterPanel = panel;
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	auto* summary = new QLabel;
	summary->setObjectName(QStringLiteral("levelClassFilterSummary"));
	summary->setProperty("sidebarHint", true);
	summary->setWordWrap(true);
	summary->setTextFormat(Qt::PlainText);
	summary->setAccessibleName(tr("Entity class filters"));
	layout->addWidget(summary);
	m_levelClassFilterSummary = summary;
	auto* find = new QLineEdit;
	find->setObjectName(QStringLiteral("levelClassFilterFind"));
	find->setPlaceholderText(tr("Find a class"));
	find->setAccessibleName(tr("Find an entity class to filter"));
	find->setClearButtonEnabled(true);
	layout->addWidget(find);
	m_levelClassFilterFind = find;
	auto* tree = new QTreeWidget;
	tree->setObjectName(QStringLiteral("levelClassFilterTree"));
	tree->setAccessibleName(tr("Entity classes the views draw"));
	tree->setAccessibleDescription(tr("Untick a class to hide every entity of it in the Levels views for this map session. Groups tick or untick their classes together."));
	tree->setHeaderHidden(true);
	tree->setColumnCount(1);
	tree->setUniformRowHeights(true);
	tree->setRootIsDecorated(true);
	tree->setMinimumHeight(160);
	tree->setFocusPolicy(Qt::StrongFocus);
	layout->addWidget(tree, 1);
	m_levelClassFilterTree = tree;
	connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
		if (!item || column != 0) {
			return;
		}
		// A group shows or hides all of its classes; a partly ticked group
		// ticks them all.
		const bool shown = item->checkState(0) != Qt::Unchecked;
		QStringList ids;
		if (item->childCount() > 0) {
			for (int index = 0; index < item->childCount(); ++index) {
				ids << item->child(index)->data(0, kClassIdRole).toString();
			}
		} else {
			ids << item->data(0, kClassIdRole).toString();
		}
		setLevelClassFiltersShown(ids, shown);
	});
	connect(find, &QLineEdit::textChanged, this, [this]() { refreshLevelClassFilterPanel(); });
	auto* buttons = new QHBoxLayout;
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->setSpacing(4);
	const auto button = [this, buttons](const QString& name, const QString& text, const QString& icon, const QString& tip, bool shown) {
		auto* made = new QToolButton;
		made->setObjectName(name);
		made->setText(text);
		made->setIcon(studioIcon(icon));
		made->setToolTip(tip);
		made->setAccessibleName(text);
		made->setAccessibleDescription(tip);
		made->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		made->setAutoRaise(true);
		made->setFocusPolicy(Qt::TabFocus);
		setBaseIconSize(made, QSize(16, 16));
		connect(made, &QToolButton::clicked, this, [this, shown]() {
			QStringList ids;
			for (const LevelFilterClass& entry : levelFilterClasses(m_levelMapDocument, levelEntityCatalogue())) {
				ids << entry.id;
			}
			setLevelClassFiltersShown(ids, shown);
		});
		buttons->addWidget(made);
	};
	button(QStringLiteral("levelClassFiltersShowAll"), tr("Show All Classes"), QStringLiteral("eye"),
		tr("Show every entity class in the Levels views again. The map is unchanged."), true);
	button(QStringLiteral("levelClassFiltersHideAll"), tr("Hide All Classes"), QStringLiteral("filter"),
		tr("Hide every entity class in the Levels views for this map session; tick the ones to keep. The map is unchanged."), false);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	refreshLevelClassFilterPanel();
	return panel;
}

void ApplicationShell::refreshLevelClassFilterPanel()
{
	QTreeWidget* tree = m_levelClassFilterTree;
	if (!tree) {
		return;
	}
	const bool quake = quakeFamily(m_levelMapDocument);
	const QVector<LevelFilterClass> classes = quake ? levelFilterClasses(m_levelMapDocument, levelEntityCatalogue()) : QVector<LevelFilterClass>();
	QStringList names;
	for (const LevelFilterClass& entry : classes) {
		names << entry.id;
	}
	const QSignalBlocker blocker(tree);
	const QString key = names.join(QLatin1Char('\n'));
	if (key != m_levelClassFilterTreeKey) {
		m_levelClassFilterTreeKey = key;
		tree->clear();
		QHash<QString, QTreeWidgetItem*> groups;
		for (const LevelFilterClass& entry : classes) {
			QTreeWidgetItem* group = groups.value(entry.group);
			if (!group) {
				group = new QTreeWidgetItem(tree);
				group->setText(0, entry.group);
				group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
				group->setData(0, kClassIdRole, QString());
				group->setData(0, Qt::UserRole, entry.group);
				groups.insert(entry.group, group);
			}
			auto* item = new QTreeWidgetItem(group);
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren);
			item->setData(0, kClassIdRole, entry.id);
			item->setData(0, Qt::UserRole, entry.className);
		}
	}
	QHash<QString, LevelFilterClass> byId;
	for (const LevelFilterClass& entry : classes) {
		byId.insert(entry.id, entry);
	}
	const QString needle = m_levelClassFilterFind ? m_levelClassFilterFind->text().trimmed() : QString();
	int hidden = 0;
	for (int g = 0; g < tree->topLevelItemCount(); ++g) {
		QTreeWidgetItem* group = tree->topLevelItem(g);
		int shownChildren = 0;
		int visibleChildren = 0;
		int entities = 0;
		for (int c = 0; c < group->childCount(); ++c) {
			QTreeWidgetItem* item = group->child(c);
			const LevelFilterClass entry = byId.value(item->data(0, kClassIdRole).toString());
			const bool shown = !m_levelFiltersOff.contains(entry.id);
			shownChildren += shown ? 1 : 0;
			hidden += shown ? 0 : 1;
			entities += entry.count;
			item->setText(0, tr("%1 (%2)").arg(entry.className).arg(entry.count));
			item->setCheckState(0, shown ? Qt::Checked : Qt::Unchecked);
			const QString kind = entry.kind == EntityClassKind::Brush ? tr("brush class") : tr("point class");
			const QString origin = entry.defined ? tr("from the entity definitions") : tr("used by this map only");
			const QString tip = tr("%1: %2, %3, %n in this map.", nullptr, entry.count).arg(entry.className, kind, origin);
			item->setToolTip(0, tip);
			item->setData(0, Qt::AccessibleTextRole, shown ? tr("%1, shown, %n in this map", nullptr, entry.count).arg(entry.className)
														 : tr("%1, hidden, %n in this map", nullptr, entry.count).arg(entry.className));
			const bool matches = needle.isEmpty() || entry.className.contains(needle, Qt::CaseInsensitive);
			item->setHidden(!matches);
			visibleChildren += matches ? 1 : 0;
		}
		const Qt::CheckState state = shownChildren == group->childCount() ? Qt::Checked : shownChildren == 0 ? Qt::Unchecked : Qt::PartiallyChecked;
		group->setCheckState(0, state);
		const QString groupName = group->data(0, Qt::UserRole).toString();
		group->setText(0, tr("%1 (%2)").arg(groupName).arg(entities));
		const int classCount = group->childCount();
		group->setToolTip(0, tr("%n class(es) whose names begin with %1; tick or untick them together.", nullptr, classCount).arg(groupName));
		group->setHidden(visibleChildren == 0);
		if (!needle.isEmpty() && visibleChildren > 0) {
			group->setExpanded(true);
		}
	}
	if (QLabel* summary = m_levelClassFilterSummary) {
		const int total = static_cast<int>(classes.size());
		QString text;
		if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
			text = tr("Open a map to filter its entity classes.");
		} else if (!quake) {
			text = tr("Class filters are for Quake-family maps; Doom maps filter things by category above.");
		} else if (hidden == 0) {
			text = tr("All %n class(es) shown. Class filters last until another map opens and never change the map.", nullptr, total);
		} else {
			text = tr("%1 of %n class(es) hidden from the views until another map opens; the map is unchanged.", nullptr, total).arg(hidden);
		}
		summary->setText(text);
		summary->setAccessibleDescription(text);
		summary->setProperty("hiddenClasses", hidden);
	}
	tree->setEnabled(quake && !classes.isEmpty());
	if (m_levelClassFilterFind) {
		m_levelClassFilterFind->setEnabled(quake && !classes.isEmpty());
	}
	for (QToolButton* button : m_levelClassFilterPanel ? m_levelClassFilterPanel->findChildren<QToolButton*>() : QList<QToolButton*>()) {
		button->setEnabled(quake && !classes.isEmpty());
	}
}

void ApplicationShell::setLevelClassFiltersShown(const QStringList& classIds, bool shown)
{
	if (!quakeFamily(m_levelMapDocument)) {
		statusBar()->showMessage(tr("Class filters are for Quake-family maps."), 4000);
		return;
	}
	QStringList off = m_levelFiltersOff;
	int changed = 0;
	QString single;
	for (const QString& id : classIds) {
		if (!levelFilterClassForId(id)) {
			continue;
		}
		const bool wasHidden = off.contains(id);
		if (shown && wasHidden) {
			off.removeAll(id);
			++changed;
			single = id;
		} else if (!shown && !wasHidden) {
			off << id;
			++changed;
			single = id;
		}
	}
	if (changed == 0) {
		refreshLevelClassFilterPanel();
		refreshLevelDataFilterCommands();
		return;
	}
	applyLevelViewFilterState(off);
	if (changed == 1) {
		const QString name = levelViewFilterTitle(single);
		statusBar()->showMessage(shown ? tr("%1 entities shown again.").arg(name)
									   : tr("%1 entities hidden from the views for this map session; the map is unchanged.").arg(name),
			4000);
	} else {
		statusBar()->showMessage(shown ? tr("%n entity class(es) shown again.", nullptr, changed)
									   : tr("%n entity class(es) hidden from the views for this map session; the map is unchanged.", nullptr, changed),
			4000);
	}
}

} // namespace vibestudio
