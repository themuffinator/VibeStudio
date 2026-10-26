// View > Filters: one checkable command for each kind of object the Levels
// views can stop drawing, Invert Filters and Show Every Kind. They share the
// View tab's Filters check boxes and their saved list, so a key, the menu,
// command search and the check boxes always agree, and a change applies to
// every plan view and the camera at once. Filters change what is drawn and
// picked, never the map, so nothing here touches undo.
//
// Workflow reference: NRC filters.cpp ToggleFilterFlag, InvertFilters and
// ResetFilters at 449778b3 (GPL-2.0-or-later) / TB ActionManager "View
// Filter > Toggle show brushes" at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/studio_actions.h"
#include "core/level_filter_tags.h"
#include "core/level_view_filters.h"
#include "core/package_staging.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QThread>
#include <QToolButton>

#include <functional>

namespace vibestudio {

void ApplicationShell::registerLevelViewFilterCommands()
{
	if (!m_commands) {
		return;
	}
	// The saved list is read here as well as by the View tab, so a key pressed
	// before that tab is built never saves over the kinds switched off last time.
	m_levelFiltersOff = levelViewFiltersFromSaved(m_settings.shellLayoutState(levelViewFiltersSettingsKey()));
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, bool checkable, bool separatorBefore,
						 std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::View;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.menuSection = tr("Filters");
		command.checkable = checkable;
		command.separatorBefore = separatorBefore;
		command.handler = std::move(handler);
		if (QAction* action = m_commands->registerCommand(command); action && checkable) {
			// Leave the menu's indicator column to the check mark.
			action->setIcon(QIcon());
		}
	};
	for (const LevelViewFilter& kind : levelViewFilterKinds()) {
		const QString id = levelViewFilterCommandId(kind.id);
		add(id, kind.title, tr("%1 Checked while the Levels views draw them; the map is unchanged.").arg(kind.description), QStringLiteral("filter"), true,
			false, [this, id, filterId = kind.id]() {
				const QAction* action = m_commands->action(id);
				setLevelViewFilter(filterId, action && action->isChecked());
			});
	}
	add(QStringLiteral("map.invertFilters"), tr("Invert Filters"),
		tr("Hide every kind the filters show and show every kind they hide, as Radiant's Invert filters does. The map is unchanged."),
		QStringLiteral("refresh"), false, true, [this]() { invertLevelViewFilters(); });
	add(QStringLiteral("map.resetFilters"), tr("Show Every Kind"),
		tr("Show every kind of object in the Levels views again, as Radiant's Reset filters does. The map is unchanged."), QStringLiteral("eye"),
		false, false, [this]() { resetLevelViewFilters(); });
	refreshLevelViewFilterCommands();
}

void ApplicationShell::refreshLevelViewFilterCommands()
{
	if (!m_commands) {
		return;
	}
	QSet<QString> offered;
	for (const LevelViewFilter& filter : levelViewFilters(m_levelMapDocument)) {
		offered.insert(filter.id);
	}
	for (const LevelViewFilter& kind : levelViewFilterKinds()) {
		const QString id = levelViewFilterCommandId(kind.id);
		if (QAction* action = m_commands->action(id)) {
			// Checked while shown, like the View tab's check boxes. Setting the
			// state emits no trigger, so this never runs the command.
			action->setChecked(!m_levelFiltersOff.contains(kind.id));
			m_commands->setEnabled(id, offered.contains(kind.id));
		}
	}
	m_commands->setEnabled(QStringLiteral("map.invertFilters"), !offered.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.resetFilters"), !offered.isEmpty());
	// The tag and class commands follow the same list.
	refreshLevelDataFilterCommands();
}

void ApplicationShell::applyLevelViewFilterState(const QStringList& hiddenFilterIds)
{
	m_levelFiltersOff = levelViewFiltersFromSaved(levelViewFiltersSavedValue(hiddenFilterIds));
	// Kinds and tags are remembered; class filters last for the map session.
	m_settings.setShellLayoutState(levelViewFiltersSettingsKey(), levelViewFiltersSavedValue(levelViewFiltersWithoutClasses(m_levelFiltersOff)));
	for (auto it = m_levelFilterBoxes.cbegin(); it != m_levelFilterBoxes.cend(); ++it) {
		if (QCheckBox* box = it.value()) {
			const QSignalBlocker blocker(box);
			box->setChecked(!m_levelFiltersOff.contains(it.key()));
		}
	}
	refreshLevelClassFilterPanel();
	refreshLevelViewFilterCommands();
	applyLevelViewFilters();
	synchronizeLevelPlanViews();
	refreshLevelMapObjectHiddenMarks();
	if (levelMap3DShowing()) {
		refreshLevelMap3D();
	}
}

void ApplicationShell::invertLevelViewFilters()
{
	const QVector<LevelViewFilter> offered = levelViewFilters(m_levelMapDocument);
	if (offered.isEmpty()) {
		statusBar()->showMessage(tr("Open a map to invert its view filters."), 4000);
		return;
	}
	// The tags game or project data define flip with the built-in kinds;
	// class filters stay as they are.
	const bool withTags = levelFilterTagsApply(m_levelMapDocument) && !currentLevelFilterTags().isEmpty();
	QStringList inverted = levelViewFiltersInverted(m_levelMapDocument, m_levelFiltersOff);
	if (withTags) {
		inverted = levelFilterTagsInverted(currentLevelFilterTags(), inverted);
	}
	applyLevelViewFilterState(inverted);
	int hidden = 0;
	for (const LevelViewFilter& filter : offered) {
		hidden += m_levelFiltersOff.contains(filter.id) ? 1 : 0;
	}
	if (!withTags) {
		statusBar()->showMessage(tr("Filters inverted: %n kind(s) now hidden from the views; the map is unchanged.", nullptr, hidden), 4000);
		return;
	}
	int hiddenTags = 0;
	for (const LevelFilterTag& tag : currentLevelFilterTags().tags) {
		hiddenTags += m_levelFiltersOff.contains(tag.id) ? 1 : 0;
	}
	const QString tags = tr("%n tag(s)", nullptr, hiddenTags);
	statusBar()->showMessage(tr("Filters inverted: %n kind(s) and %1 now hidden from the views; the map is unchanged.", nullptr, hidden).arg(tags), 4000);
}

void ApplicationShell::resetLevelViewFilters()
{
	applyLevelViewFilterState({});
	statusBar()->showMessage(tr("Every kind of object is shown again; the map is unchanged."), 4000);
}

// The material kinds (Translucent, Liquids, Clip and its parts, Bot Clip, Sky
// and Nodraw) read a Quake III map's shaders from the package the camera reads
// its materials from, staged edits included. The scan runs on a worker keyed
// by packageViewKey(), so opening another package, staging a shader edit or
// pressing Reload reads again, and a newer request cancels an older one.
const LevelViewFilterMaterials& ApplicationShell::currentLevelFilterMaterials() const
{
	static const LevelViewFilterMaterials none;
	return m_levelFilterMaterials ? *m_levelFilterMaterials : none;
}

void ApplicationShell::refreshLevelViewFilterMaterials()
{
	const auto applyMaterials = [this]() {
		// Counts, every plan view and the camera follow the new shaders.
		applyLevelViewFilters();
		synchronizeLevelPlanViews();
		refreshLevelMapObjectHiddenMarks();
		if (levelMap3DShowing()) {
			refreshLevelMap3D();
		}
		refreshLevelViewFilters();
	};
	if (m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		// Quake and Quake II faces carry their own flags; what was read stays
		// for the next Quake III map from the same package.
		if (!m_levelFilterMaterialsPendingKey.isEmpty()) {
			++m_levelFilterMaterialsSerial;
			if (m_levelFilterMaterialsCancel) {
				*m_levelFilterMaterialsCancel = true;
			}
			m_levelFilterMaterialsCancel.reset();
			m_levelFilterMaterialsPendingKey.clear();
		}
		refreshLevelViewFilterMaterialStatus();
		return;
	}
	const QString key = packageViewKey();
	if (key == m_levelFilterMaterialsKey || key == m_levelFilterMaterialsPendingKey) {
		return;
	}
	if (m_levelFilterMaterialsCancel) {
		*m_levelFilterMaterialsCancel = true;
	}
	const quint64 serial = ++m_levelFilterMaterialsSerial;
	std::shared_ptr<const PackageArchiveReader> archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	if (!archive) {
		// No package: the material kinds go by texture names.
		const bool had = m_levelFilterMaterials != nullptr;
		m_levelFilterMaterials.reset();
		m_levelFilterMaterialsKey = key;
		m_levelFilterMaterialsPendingKey.clear();
		m_levelFilterMaterialsCancel.reset();
		refreshLevelViewFilterMaterialStatus();
		if (had) {
			applyMaterials();
		}
		return;
	}
	m_levelFilterMaterialsPendingKey = key;
	auto cancel = std::make_shared<std::atomic_bool>(false);
	m_levelFilterMaterialsCancel = cancel;
	auto result = std::make_shared<LevelViewFilterMaterials>();
	QThread* worker = QThread::create([archive, cancel, result] { *result = scanLevelViewFilterMaterials(*archive, [cancel] { return cancel->load(); }); });
	connect(worker, &QThread::finished, this, [this, serial, key, result, applyMaterials]() {
		if (serial != m_levelFilterMaterialsSerial) {
			return; // A newer read replaced this one.
		}
		m_levelFilterMaterialsPendingKey.clear();
		m_levelFilterMaterialsCancel.reset();
		if (result->cancelled) {
			refreshLevelViewFilterMaterialStatus();
			return;
		}
		m_levelFilterMaterials = result;
		m_levelFilterMaterialsKey = key;
		refreshLevelViewFilterMaterialStatus();
		applyMaterials();
	});
	connect(worker, &QThread::finished, worker, &QObject::deleteLater);
	// The scan holds no pointer to the shell; closing it or the application
	// stops the scan between scripts and waits for it.
	connect(this, &QObject::destroyed, worker, [worker, cancel] {
		*cancel = true;
		worker->wait();
	});
	connect(qApp, &QCoreApplication::aboutToQuit, worker, [worker, cancel] {
		*cancel = true;
		worker->wait();
	});
	refreshLevelViewFilterMaterialStatus();
	worker->start();
}

void ApplicationShell::reloadLevelViewFilterMaterials()
{
	// The same counter the camera's Reload advances, so both read the package
	// again.
	++m_levelPreviewReload;
	refreshLevelViewFilterMaterials();
	if (levelMap3DShowing()) {
		refreshLevelMap3D();
	}
	statusBar()->showMessage(tr("Reading the shader scripts again for the material filters; the map is unchanged."), 4000);
}

void ApplicationShell::refreshLevelViewFilterMaterialStatus()
{
	QLabel* label = m_levelFilterMaterialStatus;
	if (!label) {
		return;
	}
	const bool quake3 = m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	label->setVisible(quake3);
	if (m_levelFilterMaterialReload) {
		m_levelFilterMaterialReload->setVisible(quake3);
		m_levelFilterMaterialReload->setEnabled(m_levelFilterMaterialsPendingKey.isEmpty());
	}
	if (!quake3) {
		return;
	}
	const LevelViewFilterMaterials& materials = currentLevelFilterMaterials();
	const QString source = QDir::toNativeSeparators(materials.sourcePath);
	const QString name = QFileInfo(materials.sourcePath).fileName().isEmpty() ? source : QFileInfo(materials.sourcePath).fileName();
	const int shaders = materials.scriptedShaderCount;
	QString text;
	if (!m_levelFilterMaterialsPendingKey.isEmpty()) {
		text = tr("Reading the package's shader scripts for the material filters…");
	} else if (!m_levelFilterMaterials) {
		text = tr("Open the game's package or folder to read its shader scripts. Until then Translucent finds nothing on this map and Liquids, Clip, Sky and Nodraw go by texture names.");
	} else if (shaders == 0) {
		text = tr("%1 has no shader scripts, so Translucent finds nothing on this map and Liquids, Clip, Sky and Nodraw go by texture names.").arg(name);
	} else {
		text = tr("%n shader(s) from %1 decide Translucent, Liquids, Clip, Sky and Nodraw; textures the package does not hold go by name.", nullptr, shaders)
				   .arg(name);
		if (!materials.complete) {
			text += QLatin1Char(' ') + tr("Some scripts could not be read.");
		}
	}
	label->setText(text);
	QString tip = source;
	if (!materials.warnings.isEmpty()) {
		tip += QLatin1Char('\n') + materials.warnings.mid(0, 8).join(QLatin1Char('\n'));
	}
	label->setToolTip(tip);
	label->setAccessibleDescription(text);
	label->setProperty("shaderCount", shaders);
	label->setProperty("reading", !m_levelFilterMaterialsPendingKey.isEmpty());
}

} // namespace vibestudio
