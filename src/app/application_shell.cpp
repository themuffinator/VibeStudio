#include "app/application_shell.h"

#include "app/ui_primitives.h"
#include "core/studio_manifest.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio {

namespace {

QLabel* sectionLabel(const QString& text)
{
	auto* label = new QLabel(text);
	label->setObjectName("sectionLabel");
	return label;
}

QFrame* modulePanel(const StudioModule& module)
{
	auto* frame = new QFrame;
	frame->setObjectName("modulePanel");
	frame->setAccessibleName(module.name);
	frame->setAccessibleDescription(module.description);
	auto* layout = new QVBoxLayout(frame);
	layout->setContentsMargins(14, 12, 14, 12);
	layout->setSpacing(6);

	auto* name = new QLabel(module.name);
	name->setObjectName("moduleTitle");
	auto* meta = new QLabel(module.category + " / " + module.maturity);
	meta->setObjectName("moduleMeta");
	auto* description = new QLabel(module.description);
	description->setWordWrap(true);

	layout->addWidget(name);
	layout->addWidget(meta);
	layout->addWidget(description);
	layout->addStretch(1);
	return frame;
}

QString nativePath(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

QString settingsStatusText(QSettings::Status status)
{
	switch (status) {
	case QSettings::NoError:
		return ApplicationShell::tr("ready");
	case QSettings::AccessError:
		return ApplicationShell::tr("access error");
	case QSettings::FormatError:
		return ApplicationShell::tr("format error");
	}
	return ApplicationShell::tr("unknown");
}

QString localeDisplayName(const QString& localeName)
{
	const QLocale locale(localeName);
	QString nativeLanguage = locale.nativeLanguageName();
	if (nativeLanguage.isEmpty() || nativeLanguage == QStringLiteral("C")) {
		nativeLanguage = localeName;
	}
	return QStringLiteral("%1 [%2]").arg(nativeLanguage, localeName);
}

QString localizedThemeName(StudioTheme theme)
{
	switch (theme) {
	case StudioTheme::System:
		return ApplicationShell::tr("System");
	case StudioTheme::Dark:
		return ApplicationShell::tr("Dark");
	case StudioTheme::Light:
		return ApplicationShell::tr("Light");
	case StudioTheme::HighContrastDark:
		return ApplicationShell::tr("High Contrast Dark");
	case StudioTheme::HighContrastLight:
		return ApplicationShell::tr("High Contrast Light");
	}
	return ApplicationShell::tr("Dark");
}

QString localizedDensityName(UiDensity density)
{
	switch (density) {
	case UiDensity::Comfortable:
		return ApplicationShell::tr("Comfortable");
	case UiDensity::Standard:
		return ApplicationShell::tr("Standard");
	case UiDensity::Compact:
		return ApplicationShell::tr("Compact");
	}
	return ApplicationShell::tr("Standard");
}

QString setupStatusDisplayName(const QString& status)
{
	if (status == QStringLiteral("complete")) {
		return ApplicationShell::tr("Complete");
	}
	if (status == QStringLiteral("skipped")) {
		return ApplicationShell::tr("Skipped For Now");
	}
	if (status == QStringLiteral("in-progress")) {
		return ApplicationShell::tr("In Progress");
	}
	return ApplicationShell::tr("Not Started");
}

int setupStepProgressValue(SetupStep step)
{
	const QVector<SetupStep> steps = setupSteps();
	for (int index = 0; index < steps.size(); ++index) {
		if (steps[index] == step) {
			return index;
		}
	}
	return 0;
}

QString localizedOperationStateName(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return ApplicationShell::tr("Idle");
	case OperationState::Queued:
		return ApplicationShell::tr("Queued");
	case OperationState::Loading:
		return ApplicationShell::tr("Loading");
	case OperationState::Running:
		return ApplicationShell::tr("Running");
	case OperationState::Warning:
		return ApplicationShell::tr("Warning");
	case OperationState::Failed:
		return ApplicationShell::tr("Failed");
	case OperationState::Cancelled:
		return ApplicationShell::tr("Cancelled");
	case OperationState::Completed:
		return ApplicationShell::tr("Completed");
	}
	return ApplicationShell::tr("Idle");
}

QString localizedPackageFormatName(PackageArchiveFormat format)
{
	switch (format) {
	case PackageArchiveFormat::Folder:
		return ApplicationShell::tr("Folder");
	case PackageArchiveFormat::Pak:
		return ApplicationShell::tr("PAK");
	case PackageArchiveFormat::Wad:
		return ApplicationShell::tr("WAD");
	case PackageArchiveFormat::Zip:
		return ApplicationShell::tr("ZIP");
	case PackageArchiveFormat::Pk3:
		return ApplicationShell::tr("PK3");
	case PackageArchiveFormat::Unknown:
		break;
	}
	return ApplicationShell::tr("Unknown");
}

QString localizedPackageEntryKindName(PackageEntryKind kind)
{
	switch (kind) {
	case PackageEntryKind::File:
		return ApplicationShell::tr("File");
	case PackageEntryKind::Directory:
		return ApplicationShell::tr("Directory");
	}
	return ApplicationShell::tr("File");
}

QString byteSizeText(quint64 bytes)
{
	if (bytes >= 1024ull * 1024ull * 1024ull) {
		return ApplicationShell::tr("%1 GiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ull * 1024ull) {
		return ApplicationShell::tr("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ull) {
		return ApplicationShell::tr("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return ApplicationShell::tr("%1 B").arg(bytes);
}

} // namespace

ApplicationShell::ApplicationShell(QWidget* parent)
	: QMainWindow(parent)
{
	buildUi();
}

ApplicationShell::~ApplicationShell()
{
	saveShellState();
	m_settings.sync();
}

void ApplicationShell::buildUi()
{
	setWindowTitle(tr("VibeStudio"));
	resize(1280, 800);

	auto* root = new QWidget;
	auto* rootLayout = new QHBoxLayout(root);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(0);

	m_modeRail = new QListWidget;
	m_modeRail->setObjectName("modeRail");
	m_modeRail->setAccessibleName(tr("Mode rail"));
	m_modeRail->setAccessibleDescription(tr("Switches between studio work surfaces."));
	m_modeRail->setFixedWidth(190);
	m_modeRail->addItems({
		tr("Workspace"),
		tr("Levels"),
		tr("Models"),
		tr("Textures"),
		tr("Audio"),
		tr("Packages"),
		tr("Code"),
		tr("Shaders"),
		tr("Build"),
	});
	rootLayout->addWidget(m_modeRail);

	auto* splitter = new QSplitter(Qt::Horizontal);
	rootLayout->addWidget(splitter, 1);

	auto* center = new QWidget;
	auto* centerLayout = new QVBoxLayout(center);
	centerLayout->setContentsMargins(22, 18, 22, 18);
	centerLayout->setSpacing(16);

	auto* topRow = new QHBoxLayout;
	auto* title = new QLabel(tr("VibeStudio"));
	title->setObjectName("appTitle");
	auto* subtitle = new QLabel(tr("Integrated development studio for idTech1-3 projects"));
	subtitle->setObjectName("appSubtitle");
	auto* titleStack = new QVBoxLayout;
	titleStack->addWidget(title);
	titleStack->addWidget(subtitle);
	topRow->addLayout(titleStack, 1);

	auto* openProject = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open Project"));
	openProject->setAccessibleName(tr("Open project folder"));
	openProject->setToolTip(tr("Choose a project folder and add it to recent projects."));
	connect(openProject, &QPushButton::clicked, this, [this]() {
		openProjectFolder();
	});
	topRow->addWidget(openProject);

	auto* detectInstalls = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), tr("Detect Installs"));
	detectInstalls->setAccessibleName(tr("Detect game installations"));
	detectInstalls->setToolTip(tr("Game installation detection is planned for the project workbench milestone."));
	detectInstalls->setEnabled(false);
	topRow->addWidget(detectInstalls);
	centerLayout->addLayout(topRow);

	centerLayout->addWidget(sectionLabel(tr("First-Run Setup")));

	auto* setupPanel = new QFrame;
	setupPanel->setObjectName("setupPanel");
	setupPanel->setAccessibleName(tr("First-run setup"));
	setupPanel->setAccessibleDescription(tr("Setup status, current step, warnings, and actions."));
	auto* setupLayout = new QVBoxLayout(setupPanel);
	setupLayout->setContentsMargins(14, 12, 14, 12);
	setupLayout->setSpacing(8);

	auto* setupHeader = new QHBoxLayout;
	m_setupStatus = new QLabel;
	m_setupStatus->setObjectName("moduleTitle");
	m_setupStatus->setAccessibleName(tr("Setup status"));
	m_setupStep = new QLabel;
	m_setupStep->setObjectName("moduleMeta");
	m_setupStep->setAccessibleName(tr("Current setup step"));
	setupHeader->addWidget(m_setupStatus);
	setupHeader->addWidget(m_setupStep, 1, Qt::AlignRight);
	setupLayout->addLayout(setupHeader);

	m_setupProgress = new QProgressBar;
	m_setupProgress->setAccessibleName(tr("Setup progress"));
	m_setupProgress->setTextVisible(true);
	setupLayout->addWidget(m_setupProgress);

	m_setupNextAction = new QLabel;
	m_setupNextAction->setWordWrap(true);
	m_setupNextAction->setAccessibleName(tr("Setup next action"));
	setupLayout->addWidget(m_setupNextAction);

	m_setupSummary = new QListWidget;
	m_setupSummary->setObjectName("setupSummary");
	m_setupSummary->setAccessibleName(tr("Setup summary"));
	m_setupSummary->setAccessibleDescription(tr("Completed, pending, and warning items for first-run setup."));
	m_setupSummary->setMinimumHeight(136);
	setupLayout->addWidget(m_setupSummary);

	auto* setupActions = new QHBoxLayout;
	setupActions->addStretch(1);
	m_setupStartResume = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), tr("Start"));
	m_setupStartResume->setAccessibleName(tr("Start or resume setup"));
	connect(m_setupStartResume, &QPushButton::clicked, this, [this]() {
		startOrResumeSetup();
	});
	setupActions->addWidget(m_setupStartResume);

	m_setupNext = new QPushButton(style()->standardIcon(QStyle::SP_ArrowForward), tr("Next"));
	m_setupNext->setAccessibleName(tr("Advance setup step"));
	connect(m_setupNext, &QPushButton::clicked, this, [this]() {
		advanceSetup();
	});
	setupActions->addWidget(m_setupNext);

	m_setupSkip = new QPushButton(style()->standardIcon(QStyle::SP_DialogCloseButton), tr("Skip"));
	m_setupSkip->setAccessibleName(tr("Skip setup for now"));
	connect(m_setupSkip, &QPushButton::clicked, this, [this]() {
		skipSetup();
	});
	setupActions->addWidget(m_setupSkip);

	m_setupComplete = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), tr("Finish"));
	m_setupComplete->setAccessibleName(tr("Finish setup"));
	connect(m_setupComplete, &QPushButton::clicked, this, [this]() {
		completeSetup();
	});
	setupActions->addWidget(m_setupComplete);

	m_setupReset = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), tr("Reset"));
	m_setupReset->setAccessibleName(tr("Reset setup progress"));
	connect(m_setupReset, &QPushButton::clicked, this, [this]() {
		resetSetup();
	});
	setupActions->addWidget(m_setupReset);
	setupLayout->addLayout(setupActions);

	centerLayout->addWidget(setupPanel);

	auto* recentHeader = new QHBoxLayout;
	recentHeader->addWidget(sectionLabel(tr("Recent Projects")));
	m_recentSummary = new QLabel;
	m_recentSummary->setObjectName("panelMeta");
	recentHeader->addWidget(m_recentSummary, 1, Qt::AlignRight);
	centerLayout->addLayout(recentHeader);

	m_recentProjects = new QListWidget;
	m_recentProjects->setObjectName("recentProjects");
	m_recentProjects->setAccessibleName(tr("Recent projects"));
	m_recentProjects->setAccessibleDescription(tr("Project folders remembered from previous sessions."));
	m_recentProjects->setSelectionMode(QAbstractItemView::SingleSelection);
	m_recentProjects->setUniformItemSizes(false);
	m_recentProjects->setMinimumHeight(118);
	connect(m_recentProjects, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
		activateRecentProject(item);
	});
	connect(m_recentProjects, &QListWidget::itemSelectionChanged, this, [this]() {
		const QListWidgetItem* item = m_recentProjects->currentItem();
		if (item) {
			updateInspectorForProject(item->data(Qt::UserRole).toString());
		} else {
			updateInspector();
		}
	});
	centerLayout->addWidget(m_recentProjects);

	auto* recentActions = new QHBoxLayout;
	recentActions->addStretch(1);
	auto* removeRecent = new QPushButton(style()->standardIcon(QStyle::SP_DialogDiscardButton), tr("Remove"));
	removeRecent->setAccessibleName(tr("Remove selected recent project"));
	removeRecent->setToolTip(tr("Remove the selected project from the recent list without touching its files."));
	connect(removeRecent, &QPushButton::clicked, this, [this]() {
		removeSelectedRecentProject();
	});
	recentActions->addWidget(removeRecent);

	auto* clearRecent = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), tr("Clear"));
	clearRecent->setAccessibleName(tr("Clear recent projects"));
	clearRecent->setToolTip(tr("Clear the recent-project list without touching project files."));
	connect(clearRecent, &QPushButton::clicked, this, [this]() {
		clearRecentProjects();
	});
	recentActions->addWidget(clearRecent);
	centerLayout->addLayout(recentActions);

	auto* packageHeader = new QHBoxLayout;
	packageHeader->addWidget(sectionLabel(tr("Package Browser")));
	m_packageSummary = new QLabel;
	m_packageSummary->setObjectName("panelMeta");
	m_packageSummary->setAccessibleName(tr("Package summary"));
	packageHeader->addWidget(m_packageSummary, 1, Qt::AlignRight);
	centerLayout->addLayout(packageHeader);

	m_packageState = new LoadingPane;
	m_packageState->setAccessibleName(tr("Package loading state"));
	m_packageState->setPlaceholderRows({
		tr("Package entries"),
		tr("Directory tree"),
		tr("Entry metadata"),
	});
	centerLayout->addWidget(m_packageState);

	m_packageFilter = new QLineEdit;
	m_packageFilter->setObjectName("packageFilter");
	m_packageFilter->setAccessibleName(tr("Package entry filter"));
	m_packageFilter->setAccessibleDescription(tr("Filters loaded package entries by path or type hint."));
	m_packageFilter->setPlaceholderText(tr("Filter package entries"));
	connect(m_packageFilter, &QLineEdit::textChanged, this, [this]() {
		filterPackageEntries();
	});
	centerLayout->addWidget(m_packageFilter);

	m_packageEntries = new QListWidget;
	m_packageEntries->setObjectName("packageEntries");
	m_packageEntries->setAccessibleName(tr("Package entries"));
	m_packageEntries->setAccessibleDescription(tr("Read-only entries from the loaded folder, PAK, WAD, ZIP, or PK3 package."));
	m_packageEntries->setMinimumHeight(150);
	connect(m_packageEntries, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshPackageEntryDetails(selectedPackageEntryPath());
	});
	centerLayout->addWidget(m_packageEntries);

	m_packageDrawer = new DetailDrawer;
	m_packageDrawer->setAccessibleName(tr("Package entry detail drawer"));
	m_packageDrawer->setTitle(tr("Package Entry Details"));
	m_packageDrawer->setSubtitle(tr("Open a package to inspect entry metadata."));
	centerLayout->addWidget(m_packageDrawer);

	auto* packageActions = new QHBoxLayout;
	packageActions->addStretch(1);
	auto* openPackage = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open Package"));
	openPackage->setAccessibleName(tr("Open package file"));
	openPackage->setToolTip(tr("Open a PAK, WAD, ZIP, or PK3 package for read-only browsing."));
	connect(openPackage, &QPushButton::clicked, this, [this]() {
		openPackageFile();
	});
	packageActions->addWidget(openPackage);

	auto* openFolderPackage = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open Folder"));
	openFolderPackage->setAccessibleName(tr("Open folder package"));
	openFolderPackage->setToolTip(tr("Open a folder as a read-only package source."));
	connect(openFolderPackage, &QPushButton::clicked, this, [this]() {
		openPackageFolder();
	});
	packageActions->addWidget(openFolderPackage);
	centerLayout->addLayout(packageActions);

	centerLayout->addWidget(sectionLabel(tr("Accessibility And Language")));

	auto* preferencesPanel = new QFrame;
	preferencesPanel->setObjectName("preferencesPanel");
	preferencesPanel->setAccessibleName(tr("Accessibility and language preferences"));
	preferencesPanel->setAccessibleDescription(tr("Persistent preferences for language, theme, scaling, density, motion, and text to speech."));
	auto* preferencesLayout = new QFormLayout(preferencesPanel);
	preferencesLayout->setContentsMargins(14, 12, 14, 12);
	preferencesLayout->setHorizontalSpacing(14);
	preferencesLayout->setVerticalSpacing(8);

	m_localeCombo = new QComboBox;
	m_localeCombo->setAccessibleName(tr("Language"));
	for (const QString& localeName : supportedLocaleNames()) {
		m_localeCombo->addItem(localeDisplayName(localeName), localeName);
	}
	preferencesLayout->addRow(tr("Language"), m_localeCombo);

	m_themeCombo = new QComboBox;
	m_themeCombo->setAccessibleName(tr("Theme"));
	const QVector<StudioTheme> themes = {
		StudioTheme::System,
		StudioTheme::Dark,
		StudioTheme::Light,
		StudioTheme::HighContrastDark,
		StudioTheme::HighContrastLight,
	};
	for (StudioTheme theme : themes) {
		m_themeCombo->addItem(localizedThemeName(theme), themeId(theme));
	}
	preferencesLayout->addRow(tr("Theme"), m_themeCombo);

	m_textScaleCombo = new QComboBox;
	m_textScaleCombo->setAccessibleName(tr("Text scale"));
	for (int scale : {100, 125, 150, 175, 200}) {
		m_textScaleCombo->addItem(tr("%1%").arg(scale), scale);
	}
	preferencesLayout->addRow(tr("Text scale"), m_textScaleCombo);

	m_densityCombo = new QComboBox;
	m_densityCombo->setAccessibleName(tr("UI density"));
	const QVector<UiDensity> densities = {
		UiDensity::Comfortable,
		UiDensity::Standard,
		UiDensity::Compact,
	};
	for (UiDensity density : densities) {
		m_densityCombo->addItem(localizedDensityName(density), densityId(density));
	}
	preferencesLayout->addRow(tr("Density"), m_densityCombo);

	m_reducedMotion = new QCheckBox(tr("Reduced motion"));
	m_reducedMotion->setAccessibleName(tr("Reduced motion"));
	m_reducedMotion->setToolTip(tr("Stores the preference for future animated setup, task, and editor surfaces."));
	preferencesLayout->addRow(QString(), m_reducedMotion);

	m_textToSpeech = new QCheckBox(tr("Text to speech"));
	m_textToSpeech->setAccessibleName(tr("Text to speech"));
	m_textToSpeech->setToolTip(tr("Stores the OS-backed text-to-speech preference for the setup and task surfaces planned next."));
	preferencesLayout->addRow(QString(), m_textToSpeech);

	centerLayout->addWidget(preferencesPanel);

	centerLayout->addWidget(sectionLabel(tr("Studio Surface")));

	auto* grid = new QGridLayout;
	grid->setSpacing(12);
	const QVector<StudioModule> modules = plannedModules();
	for (int index = 0; index < modules.size(); ++index) {
		grid->addWidget(modulePanel(modules[index]), index / 2, index % 2);
	}
	centerLayout->addLayout(grid);
	centerLayout->addStretch(1);
	splitter->addWidget(center);

	auto* inspectorPage = new QWidget;
	auto* inspectorLayout = new QVBoxLayout(inspectorPage);
	inspectorLayout->setContentsMargins(12, 12, 12, 12);
	inspectorLayout->setSpacing(8);

	m_inspectorState = new LoadingPane;
	m_inspectorState->setAccessibleName(tr("Inspector state"));
	m_inspectorState->setPlaceholderRows({
		tr("Settings metadata"),
		tr("Setup status"),
		tr("Raw diagnostics"),
	});
	inspectorLayout->addWidget(m_inspectorState);

	m_inspector = new QTextEdit;
	m_inspector->setObjectName("inspector");
	m_inspector->setAccessibleName(tr("Inspector"));
	m_inspector->setAccessibleDescription(tr("Shows settings, recent project, compiler, and project diagnostics."));
	m_inspector->setReadOnly(true);
	m_inspector->setMinimumHeight(150);
	inspectorLayout->addWidget(m_inspector, 1);

	m_inspectorDrawer = new DetailDrawer;
	m_inspectorDrawer->setAccessibleName(tr("Inspector detail drawer"));
	m_inspectorDrawer->setTitle(tr("Inspector Details"));
	m_inspectorDrawer->setSubtitle(tr("Settings, setup, and raw diagnostics."));
	inspectorLayout->addWidget(m_inspectorDrawer, 2);

	auto* activity = new QWidget;
	auto* activityLayout = new QVBoxLayout(activity);
	activityLayout->setContentsMargins(12, 12, 12, 12);
	activityLayout->setSpacing(8);
	m_activitySummary = new QLabel;
	m_activitySummary->setObjectName("moduleTitle");
	m_activitySummary->setAccessibleName(tr("Activity summary"));
	activityLayout->addWidget(m_activitySummary);

	m_activityTasks = new QListWidget;
	m_activityTasks->setObjectName("activityTasks");
	m_activityTasks->setAccessibleName(tr("Activity tasks"));
	m_activityTasks->setAccessibleDescription(tr("Queued, running, warning, failed, cancelled, and completed tasks."));
	m_activityTasks->setMinimumHeight(190);
	connect(m_activityTasks, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshActivityDetails(selectedActivityTaskId());
	});
	activityLayout->addWidget(m_activityTasks);

	m_activityState = new LoadingPane;
	m_activityState->setAccessibleName(tr("Selected activity state"));
	m_activityState->setPlaceholderRows({
		tr("Task context"),
		tr("Result summary"),
		tr("Structured log"),
	});
	activityLayout->addWidget(m_activityState);

	m_activityDrawer = new DetailDrawer;
	m_activityDrawer->setAccessibleName(tr("Activity detail drawer"));
	m_activityDrawer->setTitle(tr("Task Details"));
	m_activityDrawer->setSubtitle(tr("Select an activity to inspect logs, warnings, timing, and raw task metadata."));
	activityLayout->addWidget(m_activityDrawer, 1);

	auto* activityActions = new QHBoxLayout;
	activityActions->addStretch(1);
	m_activityCancel = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), tr("Cancel"));
	m_activityCancel->setAccessibleName(tr("Cancel selected activity"));
	connect(m_activityCancel, &QPushButton::clicked, this, [this]() {
		cancelSelectedActivityTask();
	});
	activityActions->addWidget(m_activityCancel);

	m_activityClearFinished = new QPushButton(style()->standardIcon(QStyle::SP_DialogDiscardButton), tr("Clear Finished"));
	m_activityClearFinished->setAccessibleName(tr("Clear finished activities"));
	connect(m_activityClearFinished, &QPushButton::clicked, this, [this]() {
		clearFinishedActivityTasks();
	});
	activityActions->addWidget(m_activityClearFinished);
	activityLayout->addLayout(activityActions);

	auto* sideTabs = new QTabWidget;
	sideTabs->setAccessibleName(tr("Inspector and activity center"));
	sideTabs->addTab(inspectorPage, tr("Inspector"));
	sideTabs->addTab(activity, tr("Activity"));
	splitter->addWidget(sideTabs);
	splitter->setStretchFactor(0, 4);
	splitter->setStretchFactor(1, 1);

	setCentralWidget(root);
	statusBar()->showMessage(tr("Scaffold ready"));

	seedActivityCenter();
	refreshSetupPanel();
	refreshRecentProjects();
	refreshPackageBrowser();
	refreshPreferenceControls();
	loadShellState();
	applyPreferencesToUi();
	updateInspector();

	connect(m_modeRail, &QListWidget::currentRowChanged, this, [this](int row) {
		m_settings.setSelectedMode(row);
		statusBar()->showMessage(tr("Mode saved: %1").arg(m_modeRail->currentItem() ? m_modeRail->currentItem()->text() : tr("Workspace")));
	});

	auto persistPreferenceChange = [this]() {
		savePreferenceControls();
	};
	connect(m_localeCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_themeCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_textScaleCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_densityCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_reducedMotion, &QCheckBox::toggled, this, persistPreferenceChange);
	connect(m_textToSpeech, &QCheckBox::toggled, this, persistPreferenceChange);
}

void ApplicationShell::loadShellState()
{
	if (m_modeRail && m_modeRail->count() > 0) {
		const int savedMode = std::min(m_settings.selectedMode(), m_modeRail->count() - 1);
		m_modeRail->setCurrentRow(std::max(0, savedMode));
	}

	const QByteArray geometry = m_settings.shellGeometry();
	if (!geometry.isEmpty()) {
		restoreGeometry(geometry);
	}

	const QByteArray windowState = m_settings.shellWindowState();
	if (!windowState.isEmpty()) {
		restoreState(windowState);
	}
}

void ApplicationShell::saveShellState()
{
	if (m_modeRail) {
		m_settings.setSelectedMode(m_modeRail->currentRow());
	}
	m_settings.setShellGeometry(saveGeometry());
	m_settings.setShellWindowState(saveState());
}

void ApplicationShell::refreshRecentProjects()
{
	m_recentProjects->clear();

	const QVector<RecentProject> projects = m_settings.recentProjects();
	m_recentSummary->setText(tr("%n remembered", nullptr, projects.size()));

	for (const RecentProject& project : projects) {
		const QString state = project.exists ? tr("Ready") : tr("Missing");
		const QString timestamp = QLocale::system().toString(project.lastOpenedUtc.toLocalTime(), QLocale::ShortFormat);
		auto* item = new QListWidgetItem(QStringLiteral("%1 [%2]\n%3\n%4")
			.arg(project.displayName, state, nativePath(project.path), timestamp));
		item->setData(Qt::UserRole, project.path);
		item->setToolTip(nativePath(project.path));
		if (!project.exists) {
			item->setForeground(QColor("#ffcf70"));
		}
		m_recentProjects->addItem(item);
	}

	if (projects.isEmpty()) {
		auto* item = new QListWidgetItem(tr("No recent projects"));
		item->setFlags(Qt::NoItemFlags);
		m_recentProjects->addItem(item);
	}
}

void ApplicationShell::openProjectFolder()
{
	const QString path = QFileDialog::getExistingDirectory(this, tr("Open Project Folder"));
	if (path.isEmpty()) {
		return;
	}

	m_settings.recordRecentProject(path);
	m_settings.sync();
	recordActivity(tr("Open Project"), nativePath(normalizedProjectPath(path)), tr("project"), OperationState::Completed, tr("Project folder remembered."));
	refreshSetupPanel();
	refreshRecentProjects();
	applyPreferencesToUi();
	updateInspectorForProject(normalizedProjectPath(path));
	statusBar()->showMessage(tr("Project folder remembered: %1").arg(nativePath(normalizedProjectPath(path))));
}

void ApplicationShell::openPackageFile()
{
	const QString path = QFileDialog::getOpenFileName(
		this,
		tr("Open Package"),
		QString(),
		tr("Packages (*.pak *.wad *.wad2 *.wad3 *.zip *.pk3);;All Files (*)"));
	if (path.isEmpty()) {
		return;
	}
	loadPackagePath(path);
}

void ApplicationShell::openPackageFolder()
{
	const QString path = QFileDialog::getExistingDirectory(this, tr("Open Folder Package"));
	if (path.isEmpty()) {
		return;
	}
	loadPackagePath(path);
}

void ApplicationShell::loadPackagePath(const QString& path)
{
	const QString absolutePath = QFileInfo(path).absoluteFilePath();
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();

	m_packageState->setReducedMotion(preferences.reducedMotion);
	m_packageState->setTitle(tr("Opening Package"));
	m_packageState->setDetail(nativePath(absolutePath));
	m_packageState->setState(OperationState::Loading, tr("Loading"));
	m_packageState->setProgress({0, 1});

	m_packageActivityId = m_activity.createTask(tr("Package Scan"), nativePath(absolutePath), tr("package"), OperationState::Loading, false);
	m_activity.setProgress(m_packageActivityId, 0, 1, tr("Opening package."));
	refreshActivityCenter(m_packageActivityId);

	PackageArchive loadedPackage;
	QString error;
	if (!loadedPackage.load(absolutePath, &error)) {
		m_activity.failTask(m_packageActivityId, error.isEmpty() ? tr("Package could not be opened.") : error);
		m_packageState->setTitle(tr("Package Open Failed"));
		m_packageState->setDetail(error.isEmpty() ? nativePath(absolutePath) : error);
		m_packageState->setState(OperationState::Failed, tr("Failed"));
		m_packageState->setProgress({0, 1});
		m_packageSummary->setText(tr("No package loaded"));
		m_packageEntries->clear();
		m_packageEntries->addItem(tr("Package open failed"));
		m_packageDrawer->setTitle(tr("Package Details"));
		m_packageDrawer->setSubtitle(error);
		m_packageDrawer->setSections({
			{QStringLiteral("error"), tr("Error"), nativePath(absolutePath), error, OperationState::Failed},
		});
		refreshActivityCenter(m_packageActivityId);
		statusBar()->showMessage(tr("Package open failed: %1").arg(error));
		return;
	}

	const PackageArchiveSummary summary = loadedPackage.summary();
	m_activity.setProgress(m_packageActivityId, summary.entryCount, std::max(1, summary.entryCount), tr("Indexed %n package entries.", nullptr, summary.entryCount));
	for (const PackageLoadWarning& warning : loadedPackage.warnings()) {
		m_activity.appendWarning(m_packageActivityId, warning.virtualPath.isEmpty() ? warning.message : QStringLiteral("%1: %2").arg(warning.virtualPath, warning.message));
	}
	m_activity.completeTask(m_packageActivityId, tr("Opened %1 with %n entries.", nullptr, summary.entryCount).arg(localizedPackageFormatName(summary.format)));

	m_packageArchive = loadedPackage;
	refreshPackageBrowser();
	refreshActivityCenter(m_packageActivityId);
	statusBar()->showMessage(tr("Package opened: %1").arg(nativePath(absolutePath)));
}

void ApplicationShell::refreshPackageBrowser()
{
	if (!m_packageEntries || !m_packageState || !m_packageDrawer) {
		return;
	}

	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	m_packageState->setReducedMotion(preferences.reducedMotion);

	if (!m_packageArchive.isOpen()) {
		m_packageSummary->setText(tr("No package loaded"));
		m_packageState->setTitle(tr("No Package Loaded"));
		m_packageState->setDetail(tr("Open a folder, PAK, WAD, ZIP, or PK3 to browse entries read-only."));
		m_packageState->setState(OperationState::Idle, tr("Idle"));
		m_packageState->setProgress({});
		m_packageEntries->clear();
		auto* item = new QListWidgetItem(tr("No package loaded"));
		item->setFlags(Qt::NoItemFlags);
		m_packageEntries->addItem(item);
		m_packageDrawer->setTitle(tr("Package Entry Details"));
		m_packageDrawer->setSubtitle(tr("Open a package to inspect entry metadata."));
		m_packageDrawer->setSections({});
		return;
	}

	const PackageArchiveSummary summary = m_packageArchive.summary();
	const OperationState state = summary.warningCount > 0 ? OperationState::Warning : OperationState::Completed;
	m_packageSummary->setText(tr("%1 / %n entries", nullptr, summary.entryCount).arg(localizedPackageFormatName(summary.format)));
	m_packageState->setTitle(tr("Package Browser"));
	m_packageState->setDetail(QStringLiteral("%1 / %2 files / %3 directories / %4 warnings")
		.arg(nativePath(summary.sourcePath))
		.arg(summary.fileCount)
		.arg(summary.directoryCount)
		.arg(summary.warningCount));
	m_packageState->setState(state, summary.warningCount > 0 ? tr("Warnings") : tr("Ready"));
	m_packageState->setProgress({summary.entryCount, std::max(1, summary.entryCount)});
	filterPackageEntries();
}

void ApplicationShell::refreshPackageEntryDetails(const QString& virtualPath)
{
	if (!m_packageDrawer || !m_packageArchive.isOpen()) {
		return;
	}

	PackageEntry selectedEntry;
	bool found = false;
	for (const PackageEntry& entry : m_packageArchive.entries()) {
		if (entry.virtualPath == virtualPath) {
			selectedEntry = entry;
			found = true;
			break;
		}
	}

	if (!found) {
		m_packageDrawer->setTitle(tr("Package Entry Details"));
		m_packageDrawer->setSubtitle(tr("No entry selected."));
		m_packageDrawer->setSections({});
		return;
	}

	QStringList summaryLines;
	summaryLines << tr("Path: %1").arg(selectedEntry.virtualPath);
	summaryLines << tr("Kind: %1").arg(localizedPackageEntryKindName(selectedEntry.kind));
	summaryLines << tr("Type: %1").arg(selectedEntry.typeHint);
	summaryLines << tr("Size: %1").arg(byteSizeText(selectedEntry.sizeBytes));
	summaryLines << tr("Compressed size: %1").arg(byteSizeText(selectedEntry.compressedSizeBytes));
	summaryLines << tr("Storage: %1").arg(selectedEntry.storageMethod.isEmpty() ? tr("unknown") : selectedEntry.storageMethod);
	summaryLines << tr("Readable now: %1").arg(selectedEntry.readable ? tr("yes") : tr("no"));
	summaryLines << tr("Nested archive candidate: %1").arg(selectedEntry.nestedArchiveCandidate ? tr("yes") : tr("no"));
	if (!selectedEntry.modifiedUtc.isValid()) {
		summaryLines << tr("Modified: unknown");
	} else {
		summaryLines << tr("Modified: %1").arg(QLocale::system().toString(selectedEntry.modifiedUtc.toLocalTime(), QLocale::LongFormat));
	}
	if (!selectedEntry.note.isEmpty()) {
		summaryLines << tr("Note: %1").arg(selectedEntry.note);
	}

	QStringList rawLines;
	rawLines << tr("Source: %1").arg(nativePath(m_packageArchive.sourcePath()));
	rawLines << tr("Format: %1").arg(packageArchiveFormatId(m_packageArchive.format()));
	rawLines << tr("Virtual path: %1").arg(selectedEntry.virtualPath);
	rawLines << tr("Kind id: %1").arg(packageEntryKindId(selectedEntry.kind));
	rawLines << tr("Data offset: %1").arg(selectedEntry.dataOffset);
	rawLines << tr("Size bytes: %1").arg(selectedEntry.sizeBytes);
	rawLines << tr("Compressed bytes: %1").arg(selectedEntry.compressedSizeBytes);
	rawLines << tr("Source archive id: %1").arg(selectedEntry.sourceArchiveId);

	QStringList warningLines;
	for (const PackageLoadWarning& warning : m_packageArchive.warnings()) {
		if (warning.virtualPath.isEmpty() || warning.virtualPath == selectedEntry.virtualPath) {
			warningLines << QStringLiteral("%1: %2").arg(warning.virtualPath.isEmpty() ? tr("package") : warning.virtualPath, warning.message);
		}
	}
	if (warningLines.isEmpty()) {
		warningLines << tr("No warnings for this entry.");
	}

	const OperationState entryState = !selectedEntry.note.isEmpty() ? OperationState::Warning : OperationState::Completed;
	m_packageDrawer->setTitle(tr("Package Entry Details"));
	m_packageDrawer->setSubtitle(selectedEntry.virtualPath);
	m_packageDrawer->setSections({
		{QStringLiteral("summary"), tr("Summary"), selectedEntry.typeHint, summaryLines.join('\n'), entryState},
		{QStringLiteral("warnings"), tr("Warnings"), tr("%n package warnings", nullptr, m_packageArchive.warnings().size()), warningLines.join('\n'), warningLines.size() == 1 && warningLines.front() == tr("No warnings for this entry.") ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("raw"), tr("Raw Metadata"), selectedEntry.virtualPath, rawLines.join('\n'), entryState},
	});
}

void ApplicationShell::filterPackageEntries()
{
	if (!m_packageEntries) {
		return;
	}

	const QString selectedPath = selectedPackageEntryPath();
	const QString filter = m_packageFilter ? m_packageFilter->text().trimmed().toCaseFolded() : QString();
	m_packageEntries->clear();

	if (!m_packageArchive.isOpen()) {
		auto* item = new QListWidgetItem(tr("No package loaded"));
		item->setFlags(Qt::NoItemFlags);
		m_packageEntries->addItem(item);
		refreshPackageEntryDetails(QString());
		return;
	}

	int selectedRow = -1;
	int visibleRow = 0;
	for (const PackageEntry& entry : m_packageArchive.entries()) {
		const QString haystack = QStringLiteral("%1 %2 %3").arg(entry.virtualPath, entry.typeHint, entry.storageMethod).toCaseFolded();
		if (!filter.isEmpty() && !haystack.contains(filter)) {
			continue;
		}
		auto* item = new QListWidgetItem(QStringLiteral("%1 [%2]\n%3 / %4 / %5")
			.arg(entry.virtualPath,
				localizedPackageEntryKindName(entry.kind),
				byteSizeText(entry.sizeBytes),
				entry.typeHint,
				entry.storageMethod.isEmpty() ? tr("unknown") : entry.storageMethod));
		item->setData(Qt::UserRole, entry.virtualPath);
		item->setData(Qt::UserRole + 1, entry.note.isEmpty() ? QStringLiteral("completed") : QStringLiteral("warning"));
		m_packageEntries->addItem(item);
		if (entry.virtualPath == selectedPath) {
			selectedRow = visibleRow;
		}
		++visibleRow;
	}

	if (m_packageEntries->count() == 0) {
		auto* item = new QListWidgetItem(tr("No matching package entries"));
		item->setFlags(Qt::NoItemFlags);
		m_packageEntries->addItem(item);
		refreshPackageEntryDetails(QString());
		return;
	}

	m_packageEntries->setCurrentRow(selectedRow >= 0 ? selectedRow : 0);
	refreshPackageEntryDetails(selectedPackageEntryPath());
	applyPreferencesToUi();
}

QString ApplicationShell::selectedPackageEntryPath() const
{
	const QListWidgetItem* item = m_packageEntries ? m_packageEntries->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void ApplicationShell::activateRecentProject(QListWidgetItem* item)
{
	if (!item) {
		return;
	}

	const QString path = item->data(Qt::UserRole).toString();
	if (path.isEmpty()) {
		return;
	}

	if (!QFileInfo::exists(path)) {
		recordActivity(tr("Open Recent Project"), nativePath(path), tr("project"), OperationState::Warning, tr("Recent project path is missing."), {tr("The remembered folder could not be found.")});
		statusBar()->showMessage(tr("Recent project path is missing: %1").arg(nativePath(path)));
		updateInspectorForProject(path);
		return;
	}

	m_settings.recordRecentProject(path, item->text().section('\n', 0, 0).section(" [", 0, 0));
	m_settings.sync();
	recordActivity(tr("Open Recent Project"), nativePath(path), tr("project"), OperationState::Completed, tr("Project folder ready."));
	refreshSetupPanel();
	refreshRecentProjects();
	applyPreferencesToUi();
	updateInspectorForProject(path);
	statusBar()->showMessage(tr("Project folder ready: %1").arg(nativePath(path)));
}

void ApplicationShell::removeSelectedRecentProject()
{
	const QListWidgetItem* item = m_recentProjects->currentItem();
	if (!item) {
		return;
	}

	const QString path = item->data(Qt::UserRole).toString();
	if (path.isEmpty()) {
		return;
	}

	m_settings.removeRecentProject(path);
	m_settings.sync();
	recordActivity(tr("Remove Recent Project"), nativePath(path), tr("project"), OperationState::Completed, tr("Recent project removed from settings."));
	refreshSetupPanel();
	refreshRecentProjects();
	applyPreferencesToUi();
	updateInspector();
	statusBar()->showMessage(tr("Recent project removed: %1").arg(nativePath(path)));
}

void ApplicationShell::clearRecentProjects()
{
	m_settings.clearRecentProjects();
	m_settings.sync();
	recordActivity(tr("Clear Recent Projects"), tr("Recent project list"), tr("project"), OperationState::Completed, tr("Recent project list cleared."));
	refreshSetupPanel();
	refreshRecentProjects();
	applyPreferencesToUi();
	updateInspector();
	statusBar()->showMessage(tr("Recent projects cleared"));
}

void ApplicationShell::refreshSetupPanel()
{
	const SetupProgress progress = m_settings.setupProgress();
	const SetupSummary summary = m_settings.setupSummary();
	const QVector<SetupStep> steps = setupSteps();
	const int currentValue = progress.completed ? steps.size() : setupStepProgressValue(progress.currentStep);

	m_setupStatus->setText(tr("Setup: %1").arg(setupStatusDisplayName(summary.status)));
	m_setupStep->setText(tr("%1 [%2]").arg(summary.currentStepName, summary.currentStepId));
	m_setupProgress->setRange(0, steps.size());
	m_setupProgress->setValue(currentValue);
	m_setupProgress->setFormat(tr("%1 of %2").arg(currentValue).arg(steps.size()));
	m_setupNextAction->setText(summary.nextAction);

	m_setupSummary->clear();
	auto addSetupItem = [this](const QString& label, const QString& value, const QString& kind) {
		auto* item = new QListWidgetItem(QStringLiteral("%1: %2").arg(label, value));
		item->setData(Qt::UserRole, kind);
		m_setupSummary->addItem(item);
	};

	addSetupItem(tr("Current"), QStringLiteral("%1 - %2").arg(summary.currentStepName, summary.currentStepDescription), QStringLiteral("current"));
	for (const QString& item : summary.completedItems) {
		addSetupItem(tr("Done"), item, QStringLiteral("done"));
	}
	for (const QString& item : summary.pendingItems) {
		addSetupItem(tr("Pending"), item, QStringLiteral("pending"));
	}
	for (const QString& warning : summary.warnings) {
		addSetupItem(tr("Warning"), warning, QStringLiteral("warning"));
	}
	if (summary.warnings.isEmpty()) {
		addSetupItem(tr("Ready"), tr("No setup warnings."), QStringLiteral("ready"));
	}

	const bool complete = progress.completed;
	const bool skipped = progress.skipped;
	const bool started = progress.started && !skipped && !complete;
	m_setupStartResume->setText(complete ? tr("Review") : (skipped ? tr("Resume") : (started ? tr("Resume") : tr("Start"))));
	m_setupNext->setEnabled(started);
	m_setupSkip->setEnabled(!complete && !skipped);
	m_setupComplete->setEnabled(started || skipped);
	m_setupReset->setEnabled(progress.started || skipped || complete);
	updateSetupActivity();
}

void ApplicationShell::startOrResumeSetup()
{
	const SetupProgress progress = m_settings.setupProgress();
	m_settings.startOrResumeSetup(progress.currentStep);
	m_settings.sync();
	recordActivity(tr("First-Run Setup"), setupStepDisplayName(progress.currentStep), tr("setup"), OperationState::Running, tr("Setup resumed."));
	refreshSetupPanel();
	updateInspector();
	statusBar()->showMessage(tr("Setup resumed: %1").arg(setupStepDisplayName(progress.currentStep)));
}

void ApplicationShell::advanceSetup()
{
	m_settings.advanceSetup();
	m_settings.sync();
	refreshSetupPanel();
	updateInspector();
	const SetupProgress progress = m_settings.setupProgress();
	recordActivity(tr("First-Run Setup"), setupStepDisplayName(progress.currentStep), tr("setup"), progress.completed ? OperationState::Completed : OperationState::Running, progress.completed ? tr("Setup completed.") : tr("Setup advanced."));
	statusBar()->showMessage(tr("Setup step: %1").arg(setupStepDisplayName(progress.currentStep)));
}

void ApplicationShell::skipSetup()
{
	m_settings.skipSetup();
	m_settings.sync();
	recordActivity(tr("First-Run Setup"), tr("Skipped for now"), tr("setup"), OperationState::Warning, tr("Setup skipped for now."), {tr("Setup can be resumed later.")});
	refreshSetupPanel();
	updateInspector();
	statusBar()->showMessage(tr("Setup skipped for now"));
}

void ApplicationShell::completeSetup()
{
	m_settings.completeSetup();
	m_settings.sync();
	recordActivity(tr("First-Run Setup"), tr("Review And Finish"), tr("setup"), OperationState::Completed, tr("Setup marked complete."));
	refreshSetupPanel();
	updateInspector();
	statusBar()->showMessage(tr("Setup marked complete"));
}

void ApplicationShell::resetSetup()
{
	m_settings.resetSetup();
	m_settings.sync();
	recordActivity(tr("First-Run Setup"), tr("Setup progress reset"), tr("setup"), OperationState::Completed, tr("Setup progress reset."));
	refreshSetupPanel();
	updateInspector();
	statusBar()->showMessage(tr("Setup progress reset"));
}

void ApplicationShell::seedActivityCenter()
{
	m_settingsActivityId = m_activity.createTask(tr("Settings Storage"), nativePath(m_settings.storageLocation()), tr("settings"), OperationState::Running, false);
	if (m_settings.status() == QSettings::NoError) {
		m_activity.completeTask(m_settingsActivityId, tr("Settings loaded successfully."));
	} else {
		m_activity.failTask(m_settingsActivityId, tr("Settings storage reported %1.").arg(settingsStatusText(m_settings.status())));
	}

	recordActivity(tr("Activity Center"), tr("Global task list, progress, results, warnings, failures, and cancellation."), tr("shell"), OperationState::Completed, tr("Activity center ready."));

	const SetupSummary setup = m_settings.setupSummary();
	OperationState setupState = OperationState::Idle;
	if (setup.status == QStringLiteral("complete")) {
		setupState = OperationState::Completed;
	} else if (setup.status == QStringLiteral("skipped") || !setup.warnings.isEmpty()) {
		setupState = OperationState::Warning;
	} else if (setup.status == QStringLiteral("in-progress")) {
		setupState = OperationState::Running;
	}
	m_setupActivityId = m_activity.createTask(tr("First-Run Setup"), setup.currentStepName, tr("setup"), setupState, setupState == OperationState::Running);
	for (const QString& warning : setup.warnings) {
		m_activity.appendWarning(m_setupActivityId, warning);
	}
	if (setupState == OperationState::Completed) {
		m_activity.completeTask(m_setupActivityId, setup.nextAction);
	} else if (setupState == OperationState::Idle || setupState == OperationState::Running) {
		m_activity.appendLog(m_setupActivityId, setupState, setup.nextAction);
	}
	refreshActivityCenter(m_setupActivityId);
}

void ApplicationShell::recordActivity(const QString& title, const QString& detail, const QString& source, OperationState state, const QString& resultSummary, const QStringList& warnings)
{
	const bool cancellable = operationStateAllowsCancellation(state);
	const QString taskId = m_activity.createTask(title, detail, source, operationStateAllowsCancellation(state) ? state : OperationState::Queued, cancellable);
	for (const QString& warning : warnings) {
		m_activity.appendWarning(taskId, warning);
	}

	switch (state) {
	case OperationState::Completed:
		m_activity.completeTask(taskId, resultSummary);
		break;
	case OperationState::Warning:
		if (warnings.isEmpty()) {
			m_activity.appendWarning(taskId, resultSummary.trimmed().isEmpty() ? detail : resultSummary);
		}
		m_activity.completeTask(taskId, resultSummary);
		break;
	case OperationState::Failed:
		m_activity.failTask(taskId, resultSummary);
		break;
	case OperationState::Cancelled:
		m_activity.transitionTask(taskId, OperationState::Running);
		m_activity.cancelTask(taskId, resultSummary);
		break;
	case OperationState::Running:
	case OperationState::Loading:
	case OperationState::Queued:
	case OperationState::Idle:
		m_activity.transitionTask(taskId, state, resultSummary);
		break;
	}

	refreshActivityCenter(taskId);
}

void ApplicationShell::updateSetupActivity()
{
	if (m_setupActivityId.isEmpty() || !m_activity.contains(m_setupActivityId)) {
		return;
	}

	const SetupSummary setup = m_settings.setupSummary();
	OperationState state = OperationState::Idle;
	if (setup.status == QStringLiteral("complete")) {
		state = OperationState::Completed;
	} else if (setup.status == QStringLiteral("skipped") || !setup.warnings.isEmpty()) {
		state = OperationState::Warning;
	} else if (setup.status == QStringLiteral("in-progress")) {
		state = OperationState::Running;
	}
	const OperationTask task = m_activity.task(m_setupActivityId);
	if (task.state != state) {
		m_activity.transitionTask(m_setupActivityId, state, setup.nextAction);
	}
	refreshActivityCenter(m_setupActivityId);
}

void ApplicationShell::refreshActivityCenter(const QString& preferredTaskId)
{
	if (!m_activityTasks) {
		return;
	}

	const QString selectedId = preferredTaskId.isEmpty() ? selectedActivityTaskId() : preferredTaskId;
	m_activitySummary->setText(tr("Activity: %1").arg(m_activity.summaryText()));
	m_activityTasks->clear();

	int selectedRow = -1;
	const QVector<OperationTask> tasks = m_activity.tasks();
	for (int index = 0; index < tasks.size(); ++index) {
		const OperationTask& task = tasks[index];
		const QString detail = task.detail.isEmpty() ? task.source : task.detail;
		auto* item = new QListWidgetItem(QStringLiteral("%1 [%2]\n%3")
			.arg(task.title, localizedOperationStateName(task.state), detail));
		item->setData(Qt::UserRole, task.id);
		item->setData(Qt::UserRole + 1, operationStateId(task.state));
		if (task.id == selectedId) {
			selectedRow = index;
		}
		m_activityTasks->addItem(item);
	}

	if (tasks.isEmpty()) {
		auto* item = new QListWidgetItem(tr("No activity"));
		item->setFlags(Qt::NoItemFlags);
		m_activityTasks->addItem(item);
		m_activityCancel->setEnabled(false);
		m_activityClearFinished->setEnabled(false);
		refreshActivityDetails(QString());
		return;
	}

	m_activityClearFinished->setEnabled(true);
	m_activityTasks->setCurrentRow(selectedRow >= 0 ? selectedRow : 0);
	refreshActivityDetails(selectedActivityTaskId());
	applyPreferencesToUi();
}

void ApplicationShell::refreshActivityDetails(const QString& taskId)
{
	const OperationTask task = m_activity.task(taskId);
	if (task.id.isEmpty()) {
		m_activityState->setTitle(tr("No task selected"));
		m_activityState->setDetail(tr("Choose an activity to inspect its progress, result, logs, and raw diagnostics."));
		m_activityState->setState(OperationState::Idle);
		m_activityState->setProgress({});
		m_activityDrawer->setTitle(tr("Task Details"));
		m_activityDrawer->setSubtitle(tr("No activity is selected."));
		m_activityDrawer->setSections({});
		m_activityCancel->setEnabled(false);
		return;
	}

	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	m_activityState->setReducedMotion(preferences.reducedMotion);
	m_activityState->setTitle(task.title);
	m_activityState->setDetail(QStringLiteral("%1 / %2").arg(task.source, task.resultSummary.isEmpty() ? task.detail : task.resultSummary));
	m_activityState->setState(task.state, localizedOperationStateName(task.state));
	m_activityState->setProgress(task.progress);

	QStringList logLines;
	for (const OperationLogEntry& entry : task.log) {
		logLines << QStringLiteral("[%1] %2: %3")
			.arg(QLocale::system().toString(entry.timestampUtc.toLocalTime(), QLocale::ShortFormat), localizedOperationStateName(entry.state), entry.message);
	}
	if (logLines.isEmpty()) {
		logLines << tr("No log entries.");
	}

	QStringList summaryLines;
	summaryLines << tr("Title: %1").arg(task.title);
	summaryLines << tr("Source: %1").arg(task.source.isEmpty() ? tr("unknown") : task.source);
	summaryLines << tr("State: %1").arg(localizedOperationStateName(task.state));
	summaryLines << tr("Detail: %1").arg(task.detail.isEmpty() ? tr("none") : task.detail);
	summaryLines << tr("Result: %1").arg(task.resultSummary.isEmpty() ? tr("pending") : task.resultSummary);
	summaryLines << tr("Progress: %1").arg(task.progress.total > 0 ? tr("%1%").arg(operationProgressPercent(task.progress)) : localizedOperationStateName(task.state));
	summaryLines << tr("Cancellable: %1").arg(operationStateAllowsCancellation(task.state) ? tr("yes") : tr("no"));

	QStringList warningLines;
	if (task.warnings.isEmpty()) {
		warningLines << tr("No warnings.");
	} else {
		for (const QString& warning : task.warnings) {
			warningLines << tr("Warning: %1").arg(warning);
		}
	}

	QStringList rawLines;
	rawLines << tr("Task id: %1").arg(task.id);
	rawLines << tr("State id: %1").arg(operationStateId(task.state));
	rawLines << tr("Created: %1").arg(QLocale::system().toString(task.createdUtc.toLocalTime(), QLocale::LongFormat));
	rawLines << tr("Updated: %1").arg(QLocale::system().toString(task.updatedUtc.toLocalTime(), QLocale::LongFormat));
	rawLines << tr("Finished: %1").arg(task.finishedUtc.isValid() ? QLocale::system().toString(task.finishedUtc.toLocalTime(), QLocale::LongFormat) : tr("not finished"));
	rawLines << tr("Progress current: %1").arg(task.progress.current);
	rawLines << tr("Progress total: %1").arg(task.progress.total);
	rawLines << tr("Warnings: %1").arg(task.warnings.size());
	rawLines << tr("Log entries: %1").arg(task.log.size());

	m_activityDrawer->setTitle(tr("Task Details"));
	m_activityDrawer->setSubtitle(QStringLiteral("%1 / %2").arg(task.title, localizedOperationStateName(task.state)));
	m_activityDrawer->setSections({
		{QStringLiteral("summary"), tr("Summary"), task.resultSummary.isEmpty() ? task.detail : task.resultSummary, summaryLines.join('\n'), task.state},
		{QStringLiteral("log"), tr("Log"), tr("%n entries", nullptr, task.log.size()), logLines.join('\n'), task.state},
		{QStringLiteral("warnings"), tr("Warnings"), tr("%n warnings", nullptr, task.warnings.size()), warningLines.join('\n'), task.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("raw"), tr("Raw Task"), task.id, rawLines.join('\n'), task.state},
	});
	m_activityDrawer->showSection(task.warnings.isEmpty() ? QStringLiteral("log") : QStringLiteral("warnings"));
	m_activityCancel->setEnabled(operationStateAllowsCancellation(task.state));
}

QString ApplicationShell::selectedActivityTaskId() const
{
	const QListWidgetItem* item = m_activityTasks ? m_activityTasks->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void ApplicationShell::cancelSelectedActivityTask()
{
	const QString taskId = selectedActivityTaskId();
	if (taskId.isEmpty()) {
		return;
	}
	if (m_activity.cancelTask(taskId, tr("Cancelled from the activity center."))) {
		refreshActivityCenter(taskId);
		statusBar()->showMessage(tr("Activity cancelled"));
	} else {
		statusBar()->showMessage(tr("Selected activity cannot be cancelled"));
	}
}

void ApplicationShell::clearFinishedActivityTasks()
{
	if (m_activity.clearTerminalTasks()) {
		refreshActivityCenter();
		statusBar()->showMessage(tr("Finished activities cleared"));
		return;
	}
	statusBar()->showMessage(tr("No finished activities to clear"));
}

void ApplicationShell::refreshInspectorDrawerForSettings()
{
	if (!m_inspectorDrawer || !m_inspectorState) {
		return;
	}

	const QVector<RecentProject> projects = m_settings.recentProjects();
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	const SetupSummary setup = m_settings.setupSummary();
	const OperationState settingsState = m_settings.status() == QSettings::NoError ? OperationState::Completed : OperationState::Failed;

	m_inspectorState->setTitle(tr("Settings Inspector"));
	m_inspectorState->setDetail(tr("Persistent shell, setup, language, accessibility, and recent-project metadata."));
	m_inspectorState->setState(settingsState, settingsStatusText(m_settings.status()));
	m_inspectorState->setProgress({1, 1});
	m_inspectorState->setPlaceholderRows({
		tr("Settings metadata"),
		tr("Setup status"),
		tr("Raw diagnostics"),
	});

	QStringList settingsLines;
	settingsLines << tr("Storage: %1").arg(nativePath(m_settings.storageLocation()));
	settingsLines << tr("Schema: %1").arg(m_settings.schemaVersion());
	settingsLines << tr("Status: %1").arg(settingsStatusText(m_settings.status()));
	settingsLines << tr("Selected mode index: %1").arg(m_settings.selectedMode());
	settingsLines << tr("Recent projects: %1").arg(projects.size());

	QStringList preferenceLines;
	preferenceLines << tr("Locale: %1").arg(preferences.localeName);
	preferenceLines << tr("Theme: %1").arg(localizedThemeName(preferences.theme));
	preferenceLines << tr("Text scale: %1%").arg(preferences.textScalePercent);
	preferenceLines << tr("Density: %1").arg(localizedDensityName(preferences.density));
	preferenceLines << tr("Reduced motion: %1").arg(preferences.reducedMotion ? tr("enabled") : tr("disabled"));
	preferenceLines << tr("Text to speech: %1").arg(preferences.textToSpeechEnabled ? tr("enabled") : tr("disabled"));

	QStringList setupLines;
	setupLines << tr("Status: %1").arg(setupStatusDisplayName(setup.status));
	setupLines << tr("Current step: %1 [%2]").arg(setup.currentStepName, setup.currentStepId);
	setupLines << tr("Description: %1").arg(setup.currentStepDescription);
	setupLines << tr("Next action: %1").arg(setup.nextAction);
	setupLines << QString();
	setupLines << tr("Completed items:");
	setupLines << (setup.completedItems.isEmpty() ? tr("- none") : QStringLiteral("- %1").arg(setup.completedItems.join(QStringLiteral("\n- "))));
	setupLines << tr("Pending items:");
	setupLines << (setup.pendingItems.isEmpty() ? tr("- none") : QStringLiteral("- %1").arg(setup.pendingItems.join(QStringLiteral("\n- "))));
	setupLines << tr("Warnings:");
	setupLines << (setup.warnings.isEmpty() ? tr("- none") : QStringLiteral("- %1").arg(setup.warnings.join(QStringLiteral("\n- "))));

	QStringList primitiveLines;
	for (const UiPrimitiveDescriptor& primitive : uiPrimitiveDescriptors()) {
		primitiveLines << QStringLiteral("%1 [%2]").arg(primitive.title, primitive.id);
		primitiveLines << primitive.description;
		primitiveLines << tr("Use cases: %1").arg(primitive.useCases.join(QStringLiteral(", ")));
		primitiveLines << QString();
	}

	m_inspectorDrawer->setTitle(tr("Inspector Details"));
	m_inspectorDrawer->setSubtitle(tr("Summary-first details for settings, preferences, setup, and UI primitive coverage."));
	m_inspectorDrawer->setSections({
		{QStringLiteral("settings"), tr("Settings Metadata"), nativePath(m_settings.storageLocation()), settingsLines.join('\n'), settingsState},
		{QStringLiteral("preferences"), tr("Preferences"), preferences.localeName, preferenceLines.join('\n'), OperationState::Completed},
		{QStringLiteral("setup"), tr("Setup Summary"), setup.nextAction, setupLines.join('\n'), setup.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("ui-primitives"), tr("UI Primitives"), tr("%n primitives", nullptr, uiPrimitiveDescriptors().size()), primitiveLines.join('\n').trimmed(), OperationState::Completed},
	});
}

void ApplicationShell::refreshInspectorDrawerForProject(const QString& path)
{
	if (!m_inspectorDrawer || !m_inspectorState) {
		return;
	}

	const QString normalizedPath = normalizedProjectPath(path);
	RecentProject selectedProject;
	for (const RecentProject& project : m_settings.recentProjects()) {
		if (project.path == normalizedPath) {
			selectedProject = project;
			break;
		}
	}

	if (selectedProject.path.isEmpty()) {
		selectedProject.path = normalizedPath;
		selectedProject.displayName = recentProjectDisplayName(normalizedPath);
		selectedProject.exists = QFileInfo::exists(normalizedPath);
	}

	const OperationState projectState = selectedProject.exists ? OperationState::Completed : OperationState::Warning;
	m_inspectorState->setTitle(tr("Project Inspector"));
	m_inspectorState->setDetail(nativePath(selectedProject.path));
	m_inspectorState->setState(projectState, selectedProject.exists ? tr("Ready") : tr("Missing"));
	m_inspectorState->setProgress({1, 1});
	m_inspectorState->setPlaceholderRows({
		tr("Project metadata"),
		tr("Package roots"),
		tr("Raw diagnostics"),
	});

	QStringList projectLines;
	projectLines << tr("Name: %1").arg(selectedProject.displayName);
	projectLines << tr("Path: %1").arg(nativePath(selectedProject.path));
	projectLines << tr("State: %1").arg(selectedProject.exists ? tr("ready") : tr("missing"));
	if (selectedProject.lastOpenedUtc.isValid()) {
		projectLines << tr("Last opened: %1").arg(QLocale::system().toString(selectedProject.lastOpenedUtc.toLocalTime(), QLocale::LongFormat));
	}

	QStringList nextLines;
	nextLines << tr("Packages: planned for the package browsing milestone.");
	nextLines << tr("Compilers: planned for the compiler registry milestone.");
	nextLines << tr("Project manifest: planned for the project schema milestone.");
	nextLines << tr("Activity logs: available in the Activity tab.");

	QStringList rawLines;
	rawLines << tr("Normalized path: %1").arg(nativePath(normalizedPath));
	rawLines << tr("Exists: %1").arg(QFileInfo::exists(normalizedPath) ? tr("yes") : tr("no"));
	rawLines << tr("Absolute path: %1").arg(nativePath(QFileInfo(normalizedPath).absoluteFilePath()));
	rawLines << tr("Settings storage: %1").arg(nativePath(m_settings.storageLocation()));

	m_inspectorDrawer->setTitle(tr("Project Details"));
	m_inspectorDrawer->setSubtitle(selectedProject.displayName);
	m_inspectorDrawer->setSections({
		{QStringLiteral("metadata"), tr("Project Metadata"), selectedProject.exists ? tr("Ready") : tr("Missing"), projectLines.join('\n'), projectState},
		{QStringLiteral("next-actions"), tr("Next Actions"), tr("Planned workbench connections"), nextLines.join('\n'), OperationState::Idle},
		{QStringLiteral("raw"), tr("Raw Diagnostics"), nativePath(normalizedPath), rawLines.join('\n'), projectState},
	});
}

void ApplicationShell::refreshPreferenceControls()
{
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	QSignalBlocker localeBlocker(m_localeCombo);
	QSignalBlocker themeBlocker(m_themeCombo);
	QSignalBlocker scaleBlocker(m_textScaleCombo);
	QSignalBlocker densityBlocker(m_densityCombo);
	QSignalBlocker motionBlocker(m_reducedMotion);
	QSignalBlocker speechBlocker(m_textToSpeech);

	m_localeCombo->setCurrentIndex(std::max(0, m_localeCombo->findData(preferences.localeName)));
	m_themeCombo->setCurrentIndex(std::max(0, m_themeCombo->findData(themeId(preferences.theme))));
	m_textScaleCombo->setCurrentIndex(std::max(0, m_textScaleCombo->findData(preferences.textScalePercent)));
	m_densityCombo->setCurrentIndex(std::max(0, m_densityCombo->findData(densityId(preferences.density))));
	m_reducedMotion->setChecked(preferences.reducedMotion);
	m_textToSpeech->setChecked(preferences.textToSpeechEnabled);
}

void ApplicationShell::savePreferenceControls()
{
	AccessibilityPreferences preferences;
	preferences.localeName = m_localeCombo->currentData().toString();
	preferences.theme = themeFromId(m_themeCombo->currentData().toString());
	preferences.textScalePercent = m_textScaleCombo->currentData().toInt();
	preferences.density = densityFromId(m_densityCombo->currentData().toString());
	preferences.reducedMotion = m_reducedMotion->isChecked();
	preferences.textToSpeechEnabled = m_textToSpeech->isChecked();

	m_settings.setAccessibilityPreferences(preferences);
	m_settings.sync();
	QLocale::setDefault(QLocale(preferences.localeName));
	recordActivity(tr("Preferences Saved"), tr("Accessibility and language preferences"), tr("settings"), OperationState::Completed, tr("Preferences saved."));
	refreshSetupPanel();
	refreshRecentProjects();
	applyPreferencesToUi();
	updateInspector();
	statusBar()->showMessage(tr("Preferences saved"));
}

void ApplicationShell::applyPreferencesToUi()
{
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	QLocale::setDefault(QLocale(preferences.localeName));

	StudioTheme effectiveTheme = preferences.theme;
	if (effectiveTheme == StudioTheme::System) {
		const QColor windowColor = QApplication::palette().color(QPalette::Window);
		effectiveTheme = windowColor.lightness() > 127 ? StudioTheme::Light : StudioTheme::Dark;
	}

	const bool highContrast = effectiveTheme == StudioTheme::HighContrastDark || effectiveTheme == StudioTheme::HighContrastLight;
	const bool light = effectiveTheme == StudioTheme::Light || effectiveTheme == StudioTheme::HighContrastLight;

	const QString window = light ? QStringLiteral("#f7f8fb") : QStringLiteral("#17191c");
	const QString rail = light ? QStringLiteral("#e9edf3") : QStringLiteral("#101215");
	const QString panel = light ? QStringLiteral("#ffffff") : QStringLiteral("#20242a");
	const QString panelBorder = highContrast ? (light ? QStringLiteral("#000000") : QStringLiteral("#ffffff")) : (light ? QStringLiteral("#c9d1dc") : QStringLiteral("#323943"));
	const QString text = light ? QStringLiteral("#15191f") : QStringLiteral("#e8edf2");
	const QString muted = highContrast ? (light ? QStringLiteral("#202020") : QStringLiteral("#f2f2f2")) : (light ? QStringLiteral("#4e5a67") : QStringLiteral("#9daab8"));
	const QString section = light ? QStringLiteral("#111820") : QStringLiteral("#d6dde5");
	const QString button = highContrast ? (light ? QStringLiteral("#0033cc") : QStringLiteral("#ffd800")) : (light ? QStringLiteral("#1f66a6") : QStringLiteral("#2d5f88"));
	const QString buttonBorder = highContrast ? (light ? QStringLiteral("#000000") : QStringLiteral("#ffffff")) : (light ? QStringLiteral("#174c7d") : QStringLiteral("#447aa8"));
	const QString buttonText = highContrast && !light ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
	const QString buttonHover = highContrast ? (light ? QStringLiteral("#002080") : QStringLiteral("#fff06a")) : (light ? QStringLiteral("#2d77bc") : QStringLiteral("#386f9c"));
	const QString selection = highContrast ? (light ? QStringLiteral("#0033cc") : QStringLiteral("#ffd800")) : (light ? QStringLiteral("#cfe5ff") : QStringLiteral("#294f6f"));
	const QString selectionText = highContrast && !light ? QStringLiteral("#000000") : (light ? QStringLiteral("#0b1520") : QStringLiteral("#ffffff"));
	const QString warning = highContrast ? (light ? QStringLiteral("#8a0000") : QStringLiteral("#ffd800")) : QStringLiteral("#ffcf70");
	const QString failure = highContrast ? (light ? QStringLiteral("#a00000") : QStringLiteral("#ff6b6b")) : QStringLiteral("#ff7a7a");
	const QString success = highContrast ? (light ? QStringLiteral("#006000") : QStringLiteral("#95ff95")) : (light ? QStringLiteral("#216e3a") : QStringLiteral("#84d994"));
	const QString running = highContrast ? (light ? QStringLiteral("#0033cc") : QStringLiteral("#6ab7ff")) : (light ? QStringLiteral("#1f66a6") : QStringLiteral("#8cc8ff"));
	const QString skeleton = light ? QStringLiteral("#eef2f7") : QStringLiteral("#2a3038");
	const QString skeletonBorder = light ? QStringLiteral("#d5dde7") : QStringLiteral("#3a4451");

	int verticalPadding = 7;
	int horizontalPadding = 12;
	int itemPadding = 8;
	if (preferences.density == UiDensity::Comfortable) {
		verticalPadding = 10;
		horizontalPadding = 14;
		itemPadding = 11;
	} else if (preferences.density == UiDensity::Compact) {
		verticalPadding = 5;
		horizontalPadding = 9;
		itemPadding = 6;
	}

	const double scale = static_cast<double>(preferences.textScalePercent) / 100.0;
	const QString baseFont = QString::number(10.5 * scale, 'f', 1);
	const QString titleFont = QString::number(24.0 * scale, 'f', 1);
	const QString sectionFont = QString::number(13.0 * scale, 'f', 1);
	const QString moduleFont = QString::number(12.5 * scale, 'f', 1);

	setStyleSheet(QStringLiteral(R"(
		QMainWindow, QWidget {
			background: %1;
			color: %2;
			font-size: %3pt;
		}
		QListWidget#modeRail {
			background: %4;
			border: 0;
			padding: 12px 8px;
		}
		QListWidget#modeRail::item {
			padding: %5px 10px;
			border-radius: 4px;
		}
		QListWidget#modeRail::item:selected {
			background: %6;
			color: %7;
		}
		QLabel#appTitle {
			font-size: %8pt;
			font-weight: 700;
		}
		QLabel#appSubtitle, QLabel#moduleMeta, QLabel#panelMeta {
			color: %9;
		}
		QLabel#sectionLabel {
			color: %10;
			font-size: %11pt;
			font-weight: 600;
		}
		QFrame#modulePanel, QFrame#preferencesPanel, QFrame#loadingPane, QFrame#detailDrawer, QTextEdit#inspector, QTextEdit#detailContent, QListWidget#recentProjects, QListWidget#packageEntries, QListWidget#activityTasks, QListWidget#detailSections, QLineEdit#packageFilter {
			background: %12;
			border: 1px solid %13;
			border-radius: 6px;
		}
		QFrame#setupPanel, QListWidget#setupSummary {
			background: %12;
			border: 1px solid %13;
			border-radius: 6px;
		}
		QListWidget#recentProjects, QListWidget#packageEntries, QListWidget#activityTasks, QListWidget#detailSections {
			padding: 6px;
		}
		QListWidget#setupSummary {
			padding: 6px;
		}
		QListWidget#recentProjects::item, QListWidget#packageEntries::item, QListWidget#activityTasks::item, QListWidget#detailSections::item {
			border-radius: 4px;
			padding: %14px 10px;
		}
		QListWidget#setupSummary::item {
			border-radius: 4px;
			padding: %14px 10px;
		}
		QListWidget#recentProjects::item:selected, QListWidget#packageEntries::item:selected, QListWidget#activityTasks::item:selected, QListWidget#detailSections::item:selected {
			background: %6;
			color: %7;
		}
		QTabWidget::pane {
			border: 1px solid %13;
			background: %1;
		}
		QTabBar::tab {
			background: %12;
			color: %2;
			border: 1px solid %13;
			padding: %19px %20px;
		}
		QTabBar::tab:selected {
			background: %6;
			color: %7;
		}
		QLabel#moduleTitle {
			font-size: %15pt;
			font-weight: 700;
		}
		QLabel#drawerTitle, QLabel#loadingTitle {
			font-size: %15pt;
			font-weight: 700;
		}
		QLabel#drawerSubtitle, QLabel#loadingDetail {
			color: %9;
		}
		QLabel#statusChip {
			background: %6;
			color: %7;
			border: 1px solid %13;
			border-radius: 4px;
			padding: 3px 8px;
			font-weight: 600;
		}
		QLabel#skeletonRow {
			background: %22;
			border: 1px dashed %23;
			border-radius: 4px;
			color: %9;
			padding: 5px 8px;
		}
		QPushButton, QComboBox {
			background: %16;
			border: 1px solid %17;
			border-radius: 4px;
			color: %18;
			padding: %19px %20px;
		}
		QPushButton:hover, QComboBox:hover {
			background: %21;
		}
		QCheckBox {
			spacing: 8px;
		}
		QStatusBar {
			background: %4;
		}
	)")
		.arg(window, text, baseFont, rail)
		.arg(itemPadding)
		.arg(selection, selectionText, titleFont, muted, section, sectionFont, panel, panelBorder)
		.arg(itemPadding)
		.arg(moduleFont, button, buttonBorder, buttonText)
		.arg(verticalPadding)
		.arg(horizontalPadding)
		.arg(buttonHover)
		.arg(skeleton, skeletonBorder));

	for (int index = 0; index < m_recentProjects->count(); ++index) {
		QListWidgetItem* item = m_recentProjects->item(index);
		if (item && !item->data(Qt::UserRole).toString().isEmpty() && !QFileInfo::exists(item->data(Qt::UserRole).toString())) {
			item->setForeground(QColor(warning));
		}
	}

	for (int index = 0; index < m_setupSummary->count(); ++index) {
		QListWidgetItem* item = m_setupSummary->item(index);
		if (item && item->data(Qt::UserRole).toString() == QStringLiteral("warning")) {
			item->setForeground(QColor(warning));
		}
	}

	auto applyStateColor = [&](QListWidget* list) {
		if (!list) {
			return;
		}
		for (int index = 0; index < list->count(); ++index) {
			QListWidgetItem* item = list->item(index);
			if (!item) {
				continue;
			}
			const QString state = item->data(Qt::UserRole + 1).toString();
			if (state == QStringLiteral("warning") || state == QStringLiteral("cancelled")) {
				item->setForeground(QColor(warning));
			} else if (state == QStringLiteral("failed")) {
				item->setForeground(QColor(failure));
			} else if (state == QStringLiteral("completed")) {
				item->setForeground(QColor(success));
			} else if (state == QStringLiteral("queued") || state == QStringLiteral("loading") || state == QStringLiteral("running")) {
				item->setForeground(QColor(running));
			}
		}
	};
	applyStateColor(m_packageEntries);
	applyStateColor(m_activityTasks);
	if (m_inspectorState) {
		m_inspectorState->setReducedMotion(preferences.reducedMotion);
	}
	if (m_activityState) {
		m_activityState->setReducedMotion(preferences.reducedMotion);
	}
}

void ApplicationShell::updateInspector()
{
	const QVector<RecentProject> projects = m_settings.recentProjects();
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	const SetupSummary setup = m_settings.setupSummary();
	QStringList lines;
	lines << tr("Settings");
	lines << tr("Storage: %1").arg(nativePath(m_settings.storageLocation()));
	lines << tr("Schema: %1").arg(m_settings.schemaVersion());
	lines << tr("Status: %1").arg(settingsStatusText(m_settings.status()));
	lines << tr("Selected mode: %1").arg(m_modeRail && m_modeRail->currentItem() ? m_modeRail->currentItem()->text() : tr("Workspace"));
	lines << tr("Recent projects: %1").arg(projects.size());
	lines << tr("Locale: %1").arg(preferences.localeName);
	lines << tr("Theme: %1").arg(localizedThemeName(preferences.theme));
	lines << tr("Text scale: %1%").arg(preferences.textScalePercent);
	lines << tr("Density: %1").arg(localizedDensityName(preferences.density));
	lines << tr("Reduced motion: %1").arg(preferences.reducedMotion ? tr("enabled") : tr("disabled"));
	lines << tr("Text to speech: %1").arg(preferences.textToSpeechEnabled ? tr("enabled") : tr("disabled"));
	lines << tr("Setup status: %1").arg(setupStatusDisplayName(setup.status));
	lines << tr("Setup step: %1").arg(setup.currentStepName);
	lines << tr("Setup next action: %1").arg(setup.nextAction);
	lines << QString();
	lines << tr("Compiler imports and project diagnostics will surface here.");
	lines << tr("Run vibestudio --cli --compiler-report for the current imported toolchain manifest.");
	m_inspector->setPlainText(lines.join('\n'));
	refreshInspectorDrawerForSettings();
}

void ApplicationShell::updateInspectorForProject(const QString& path)
{
	const QString normalizedPath = normalizedProjectPath(path);
	if (normalizedPath.isEmpty()) {
		updateInspector();
		return;
	}

	RecentProject selectedProject;
	for (const RecentProject& project : m_settings.recentProjects()) {
		if (project.path == normalizedPath) {
			selectedProject = project;
			break;
		}
	}

	if (selectedProject.path.isEmpty()) {
		selectedProject.path = normalizedPath;
		selectedProject.displayName = recentProjectDisplayName(normalizedPath);
		selectedProject.exists = QFileInfo::exists(normalizedPath);
	}

	QStringList lines;
	lines << tr("Recent Project");
	lines << tr("Name: %1").arg(selectedProject.displayName);
	lines << tr("Path: %1").arg(nativePath(selectedProject.path));
	lines << tr("State: %1").arg(selectedProject.exists ? tr("ready") : tr("missing"));
	if (selectedProject.lastOpenedUtc.isValid()) {
		lines << tr("Last opened: %1").arg(QLocale::system().toString(selectedProject.lastOpenedUtc.toLocalTime(), QLocale::LongFormat));
	}
	lines << QString();
	lines << tr("Settings storage: %1").arg(nativePath(m_settings.storageLocation()));
	lines << tr("This project will connect to manifests, packages, compilers, and activity logs as those roadmap slices land.");
	m_inspector->setPlainText(lines.join('\n'));
	refreshInspectorDrawerForProject(normalizedPath);
}

} // namespace vibestudio
