#include "app/application_shell.h"

#include "core/studio_manifest.h"
#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
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

	m_inspector = new QTextEdit;
	m_inspector->setObjectName("inspector");
	m_inspector->setAccessibleName(tr("Inspector"));
	m_inspector->setAccessibleDescription(tr("Shows settings, recent project, compiler, and project diagnostics."));
	m_inspector->setReadOnly(true);
	splitter->addWidget(m_inspector);
	splitter->setStretchFactor(0, 4);
	splitter->setStretchFactor(1, 1);

	setCentralWidget(root);
	statusBar()->showMessage(tr("Scaffold ready"));

	refreshRecentProjects();
	loadShellState();
	updateInspector();

	connect(m_modeRail, &QListWidget::currentRowChanged, this, [this](int row) {
		m_settings.setSelectedMode(row);
		statusBar()->showMessage(tr("Mode saved: %1").arg(m_modeRail->currentItem() ? m_modeRail->currentItem()->text() : tr("Workspace")));
	});

	setStyleSheet(R"(
		QMainWindow, QWidget {
			background: #17191c;
			color: #e8edf2;
			font-size: 10.5pt;
		}
		QListWidget#modeRail {
			background: #101215;
			border: 0;
			padding: 12px 8px;
		}
		QListWidget#modeRail::item {
			padding: 9px 10px;
			border-radius: 4px;
		}
		QListWidget#modeRail::item:selected {
			background: #2d5f88;
			color: #ffffff;
		}
		QLabel#appTitle {
			font-size: 24pt;
			font-weight: 700;
		}
		QLabel#appSubtitle, QLabel#moduleMeta {
			color: #9daab8;
		}
		QLabel#panelMeta {
			color: #9daab8;
		}
		QLabel#sectionLabel {
			color: #d6dde5;
			font-size: 13pt;
			font-weight: 600;
		}
		QFrame#modulePanel, QTextEdit#inspector, QListWidget#recentProjects {
			background: #20242a;
			border: 1px solid #323943;
			border-radius: 6px;
		}
		QListWidget#recentProjects {
			padding: 6px;
		}
		QListWidget#recentProjects::item {
			border-radius: 4px;
			padding: 8px 10px;
		}
		QListWidget#recentProjects::item:selected {
			background: #294f6f;
			color: #ffffff;
		}
		QLabel#moduleTitle {
			font-size: 12.5pt;
			font-weight: 700;
		}
		QPushButton {
			background: #2d5f88;
			border: 1px solid #447aa8;
			border-radius: 4px;
			color: #ffffff;
			padding: 7px 12px;
		}
		QPushButton:hover {
			background: #386f9c;
		}
		QStatusBar {
			background: #101215;
		}
	)");
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
	refreshRecentProjects();
	updateInspectorForProject(normalizedProjectPath(path));
	statusBar()->showMessage(tr("Project folder remembered: %1").arg(nativePath(normalizedProjectPath(path))));
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
		statusBar()->showMessage(tr("Recent project path is missing: %1").arg(nativePath(path)));
		updateInspectorForProject(path);
		return;
	}

	m_settings.recordRecentProject(path, item->text().section('\n', 0, 0).section(" [", 0, 0));
	m_settings.sync();
	refreshRecentProjects();
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
	refreshRecentProjects();
	updateInspector();
	statusBar()->showMessage(tr("Recent project removed: %1").arg(nativePath(path)));
}

void ApplicationShell::clearRecentProjects()
{
	m_settings.clearRecentProjects();
	m_settings.sync();
	refreshRecentProjects();
	updateInspector();
	statusBar()->showMessage(tr("Recent projects cleared"));
}

void ApplicationShell::updateInspector()
{
	const QVector<RecentProject> projects = m_settings.recentProjects();
	QStringList lines;
	lines << tr("Settings");
	lines << tr("Storage: %1").arg(nativePath(m_settings.storageLocation()));
	lines << tr("Schema: %1").arg(m_settings.schemaVersion());
	lines << tr("Status: %1").arg(settingsStatusText(m_settings.status()));
	lines << tr("Selected mode: %1").arg(m_modeRail && m_modeRail->currentItem() ? m_modeRail->currentItem()->text() : tr("Workspace"));
	lines << tr("Recent projects: %1").arg(projects.size());
	lines << QString();
	lines << tr("Compiler imports and project diagnostics will surface here.");
	lines << tr("Run vibestudio --cli --compiler-report for the current imported toolchain manifest.");
	m_inspector->setPlainText(lines.join('\n'));
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
}

} // namespace vibestudio
