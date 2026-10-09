// Releases in the shell: the Package and Release commands, the Workspace
// Releases card, indexing an installation's game assets, and the hooks that
// tie the ReleaseDialog (app/release_dialog.*) to the project, Activity, the
// Build page and Packages. Planning, notes and publishing live in core
// (core/release_plan.h, core/release_notes.h, core/release_publish.h); the
// game asset register is core/game_asset_register.h.

#include "app/application_shell.h"
#include "app/release_dialog.h"
#include "app/studio_actions.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"

#include "core/game_asset_register.h"
#include "core/package_staging.h"
#include "core/project_content.h"
#include "core/release_notes.h"
#include "core/release_plan.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <limits>

namespace vibestudio {

namespace {

QString nativeReleasePath(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

bool samePath(const QString& left, const QString& right)
{
	if (left.isEmpty() || right.isEmpty()) {
		return false;
	}
	const QString a = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
	const QString b = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
	return a.compare(b, Qt::CaseInsensitive) == 0;
#else
	return a == b;
#endif
}

struct IndexProgress {
	QMutex mutex;
	QString phase;
	qint64 done = 0;
	qint64 total = 0;
};

} // namespace

void ApplicationShell::registerReleaseCommands()
{
	const auto add = [this](const QString& id, const QString& label, const QString& description, const QString& icon, std::function<void()> handler,
						 bool separatorBefore = false) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::Project;
		command.menuSection = tr("Release");
		command.label = label;
		command.statusTip = description;
		command.iconName = icon;
		command.separatorBefore = separatorBefore;
		command.handler = std::move(handler);
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("release.package"), tr("&Package and Release…"),
		tr("Package the project, a map, a model or textures with only your own files, write the release notes, and publish."), QStringLiteral("rocket"),
		[this] { showReleaseDialog(); });
	add(QStringLiteral("release.packageMap"), tr("Package This &Map…"),
		tr("Release the open map with its build and every custom asset it uses, leaving the game's own files out."), QStringLiteral("map"),
		[this] { packageCurrentMapFromUi(); });
	add(QStringLiteral("release.packageModel"), tr("Package This M&odel…"),
		tr("Release the selected model with its skins, shaders and images."), QStringLiteral("model"), [this] { packageCurrentModelFromUi(); });
	add(QStringLiteral("release.packageTextures"), tr("Package These &Textures…"),
		tr("Release the selected texture's folder with the shader scripts that use it."), QStringLiteral("texture"), [this] { packageCurrentTexturesFromUi(); });
	add(QStringLiteral("release.recordChange"), tr("&Record a Change…"),
		tr("Add a line to the project's changelog for the next release."), QStringLiteral("book"), [this] { recordReleaseChangeFromUi(); }, true);
	add(QStringLiteral("install.indexAssets"), tr("&Index Game Assets"),
		tr("Read the selected installation's own packages once, so releases leave the game's files out. Nothing in the installation changes."),
		QStringLiteral("index"), [this] { indexGameAssets(selectedGameInstallationId()); });
}

ProjectManifest ApplicationShell::currentProjectManifestOrDefault() const
{
	const QString projectPath = m_settings.currentProjectPath();
	if (projectPath.isEmpty()) {
		return {};
	}
	ProjectManifest manifest;
	if (!loadProjectManifest(projectPath, &manifest)) {
		manifest = defaultProjectManifest(projectPath);
		manifest.selectedInstallationId = m_settings.selectedGameInstallationId();
	}
	return manifest;
}

bool ApplicationShell::releaseGameInstallation(GameInstallationProfile* profile) const
{
	GameInstallationProfile current;
	const QString selected = selectedGameInstallation(&current) ? current.id : QString();
	return releaseInstallationFor(currentProjectManifestOrDefault(), m_settings.gameInstallations(), selected, profile);
}

bool ApplicationShell::saveProjectReleaseSettings(const ProjectReleaseSettings& release, const QString& gameKey, QString* error)
{
	const QString projectPath = m_settings.currentProjectPath();
	if (projectPath.isEmpty()) {
		// Nothing to save into: a loose release keeps its settings in the dialog.
		return true;
	}
	ProjectManifest manifest;
	if (!loadProjectManifest(projectPath, &manifest)) {
		manifest = defaultProjectManifest(projectPath);
	}
	if (manifest.gameKey.isEmpty() && gameKey != QStringLiteral("custom")) {
		manifest.gameKey = gameKey;
	}
	// Store what the author chose, not the defaults, so later defaults still apply.
	ProjectManifest bare = manifest;
	bare.release = {};
	const ProjectReleaseSettings defaults = effectiveProjectReleaseSettings(bare, gameKey);
	ProjectReleaseSettings& stored = manifest.release;
	stored.title = release.title == defaults.title ? QString() : release.title;
	stored.version = release.version;
	stored.authors = release.authors;
	stored.description = release.description;
	stored.website = release.website;
	stored.license = release.license;
	stored.packageName = release.packageName == releaseSlug(release.title.isEmpty() ? defaults.title : release.title) ? QString() : release.packageName;
	stored.packageFormat = release.packageFormat;
	stored.gameFolder = release.gameFolder == defaults.gameFolder ? QString() : release.gameFolder;
	stored.includeSources = release.includeSources;
	return saveOwnProjectManifest(manifest, error);
}

void ApplicationShell::showReleaseDialog(const QString& scopeId, const QStringList& items)
{
	bool known = false;
	const ReleaseScope scope = releaseScopeFromId(scopeId.isEmpty() ? QStringLiteral("project") : scopeId, &known);
	if (m_releaseDialog) {
		m_releaseDialog->setSelection(scope, items);
		m_releaseDialog->show();
		m_releaseDialog->raise();
		m_releaseDialog->activateWindow();
		return;
	}
	ReleaseDialogHooks hooks;
	hooks.project = [this]() { return currentProjectManifestOrDefault(); };
	hooks.installation = [this](GameInstallationProfile* profile) { return releaseGameInstallation(profile); };
	hooks.openPackage = [this]() -> std::shared_ptr<const PackageArchiveReader> {
		if (!m_packageArchive.isOpen() || !m_packageStaging.summary().canSave) {
			return {};
		}
		return std::make_shared<PackageStagingArchive>(m_packageStaging);
	};
	hooks.openPackageLabel = [this]() {
		if (!m_packageArchive.isOpen()) {
			return QString();
		}
		const QString source = m_packageStaging.sourcePath().isEmpty() ? m_packageArchive.sourcePath() : m_packageStaging.sourcePath();
		return source.isEmpty() ? tr("untitled package") : QFileInfo(source).fileName();
	};
	hooks.saveReleaseSettings = [this](const ProjectReleaseSettings& release, const QString& gameKey, QString* error) {
		return saveProjectReleaseSettings(release, gameKey, error);
	};
	hooks.indexInstallation = [this](const QString& installationId, std::function<void(bool)> done) { indexGameAssets(installationId, std::move(done)); };
	hooks.buildMap = [this](const QString& mapPath) {
		if (!samePath(m_levelMapDocument.sourcePath, mapPath)) {
			openDroppedPath(mapPath);
		}
		setMode(StudioMode::Build);
		raise();
		activateWindow();
	};
	hooks.reveal = [](const QString& path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); };
	hooks.openInPackages = [this](const QString& packagePath) {
		loadPackagePath(packagePath);
		setMode(StudioMode::Packages);
		raise();
		activateWindow();
	};
	hooks.beginTask = [this](const QString& title, const QString& detail) {
		const QString id = m_activity.createTask(title, detail, QStringLiteral("release"), OperationState::Running, true);
		m_activity.appendLog(id, OperationState::Running, detail);
		refreshActivityCenter(id);
		return id;
	};
	hooks.endTask = [this](const QString& task, bool succeeded, bool cancelled, const QString& summary) {
		if (!m_activity.contains(task)) {
			return;
		}
		cancelled ? m_activity.cancelTask(task, summary) : succeeded ? m_activity.completeTask(task, summary) : m_activity.failTask(task, summary);
		persistActivityTask(task);
		refreshRecentActivityTimeline();
		refreshActivityCenter(task);
	};
	hooks.published = [this](const ReleasePublishResult& result) {
		refreshReleaseCard();
		refreshWorkspaceContextPanels();
		statusBar()->showMessage(tr("Release written to %1").arg(nativeReleasePath(result.outputDirectory)), 8000);
	};
	auto* dialog = new ReleaseDialog(this, std::move(hooks));
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	m_releaseDialog = dialog;
	if (known && !scopeId.isEmpty()) {
		dialog->setSelection(scope, items);
	}
	dialog->show();
}

void ApplicationShell::packageCurrentMapFromUi()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || m_levelMapDocument.sourcePath.isEmpty()) {
		statusBar()->showMessage(tr("Open or save a map in the project, then package it."));
		showReleaseDialog(QStringLiteral("maps"));
		return;
	}
	if (levelMapHasUnsavedEdits()) {
		const auto answer = QMessageBox::question(this, tr("Package This Map"),
			tr("The map has unsaved edits. A release reads the saved map and its last build. Save the map first?"),
			QMessageBox::Save | QMessageBox::Ignore | QMessageBox::Cancel, QMessageBox::Save);
		if (answer == QMessageBox::Cancel) {
			return;
		}
		if (answer == QMessageBox::Save) {
			saveLevelMapFromUi();
			if (levelMapHasUnsavedEdits()) {
				return;
			}
		}
	}
	showReleaseDialog(QStringLiteral("maps"), {m_levelMapDocument.sourcePath});
}

QString ApplicationShell::openPackageFilePathFor(const QString& virtualPath) const
{
	// A folder package's entries are files on disk; archives' are not.
	if (!m_packageArchive.isOpen() || m_packageArchive.format() != PackageArchiveFormat::Folder || virtualPath.isEmpty()) {
		return {};
	}
	const QString path = QDir(m_packageArchive.sourcePath()).absoluteFilePath(virtualPath);
	return QFileInfo::exists(path) ? QDir::cleanPath(path) : QString();
}

void ApplicationShell::packageCurrentModelFromUi()
{
	QStringList items;
	if (m_modelEntries && m_modelEntries->currentItem()) {
		const QString path = openPackageFilePathFor(m_modelEntries->currentItem()->data(Qt::UserRole).toString());
		if (!path.isEmpty()) {
			items << path;
		}
	}
	showReleaseDialog(QStringLiteral("models"), items);
}

void ApplicationShell::packageCurrentTexturesFromUi()
{
	QStringList items;
	if (m_textureEntries && m_textureEntries->currentItem()) {
		const QString path = openPackageFilePathFor(m_textureEntries->currentItem()->data(Qt::UserRole).toString());
		if (!path.isEmpty()) {
			items << QFileInfo(path).absolutePath();
		}
	}
	showReleaseDialog(QStringLiteral("textures"), items);
}

void ApplicationShell::recordReleaseChangeFromUi()
{
	const ProjectManifest manifest = currentProjectManifestOrDefault();
	if (manifest.rootPath.isEmpty()) {
		statusBar()->showMessage(tr("Open a project to keep a changelog."));
		return;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("recordChangeDialog"));
	dialog.setWindowTitle(tr("Record a Change"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto* layout = new QVBoxLayout(&dialog);
	auto* intro = new QLabel(tr("The change goes into the Unreleased section of the project's changelog, and into the notes of the next release."));
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto* form = new QFormLayout;
	auto* category = new QComboBox;
	category->setObjectName(QStringLiteral("recordChangeCategory"));
	category->setAccessibleName(tr("Change category"));
	for (const QString& id : changelogCategories()) {
		category->addItem(changelogCategoryDisplayName(id), id);
	}
	auto* text = new QLineEdit;
	text->setObjectName(QStringLiteral("recordChangeText"));
	text->setAccessibleName(tr("Change to record"));
	text->setPlaceholderText(tr("Describe a change, such as \"New arena: The Pit\""));
	text->setMinimumWidth(420);
	form->addRow(tr("&Kind:"), category);
	form->addRow(tr("&Change:"), text);
	layout->addLayout(form);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Record"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted || text->text().trimmed().isEmpty()) {
		return;
	}
	const ProjectReleaseSettings release = effectiveProjectReleaseSettings(manifest, effectiveProjectGameKey(manifest));
	const QString path = QDir::isAbsolutePath(release.changelogFile) ? release.changelogFile : QDir(manifest.rootPath).absoluteFilePath(release.changelogFile);
	ProjectChangelog changelog;
	QString error;
	if (!loadProjectChangelog(path, release.title, &changelog, &error) || !addChangelogEntry(&changelog, category->currentData().toString(), text->text(), &error)
		|| !saveProjectChangelog(changelog, &error)) {
		statusBar()->showMessage(tr("The change was not recorded: %1").arg(error));
		return;
	}
	statusBar()->showMessage(tr("Recorded in %1.").arg(nativeReleasePath(path)), 6000);
	refreshReleaseCard();
}

void ApplicationShell::indexGameAssets(const QString& installationId, std::function<void(bool)> done)
{
	GameInstallationProfile profile;
	bool found = false;
	for (const GameInstallationProfile& candidate : m_settings.gameInstallations()) {
		if (sameGameInstallationId(candidate.id, installationId)) {
			profile = candidate;
			found = true;
			break;
		}
	}
	if (!found) {
		statusBar()->showMessage(tr("Select a saved game installation to index."));
		if (done) {
			done(false);
		}
		return;
	}
	if (done) {
		m_assetIndexWaiters[profile.id] << std::move(done);
	}
	if (m_assetIndexJobs.contains(profile.id)) {
		// The running job answers this caller too.
		statusBar()->showMessage(tr("%1 is already being indexed.").arg(profile.displayName));
		return;
	}
	const QString task = m_activity.createTask(tr("Index Game Assets"), profile.displayName, QStringLiteral("install"), OperationState::Running, true);
	m_activity.appendLog(task, OperationState::Running, tr("Reading the stock packages of %1.").arg(nativeReleasePath(profile.rootPath)));
	refreshActivityCenter(task);
	auto cancel = std::make_shared<std::atomic_bool>(false);
	auto progress = std::make_shared<IndexProgress>();
	auto result = std::make_shared<GameAssetRegisterBuildResult>();
	auto saveError = std::make_shared<QString>();
	m_assetIndexJobs.insert(profile.id, {task, cancel});
	refreshGameInstallations();
	auto* thread = QThread::create([profile, cancel, progress, result, saveError]() {
		GameAssetRegisterBuildRequest request;
		request.installation = profile;
		request.control.isCancelled = [cancel]() { return cancel->load(); };
		request.control.progress = [progress](const QString& phase, qint64 completed, qint64 total) {
			QMutexLocker locker(&progress->mutex);
			progress->phase = phase;
			progress->done = completed;
			progress->total = total;
		};
		*result = buildGameAssetRegister(request);
		if (result->succeeded && !saveGameAssetRegister(result->registerData, gameAssetRegisterPath(profile.id), saveError.get())) {
			result->succeeded = false;
			result->error = *saveError;
		}
	});
	auto* timer = new QTimer(this);
	timer->setInterval(250);
	connect(timer, &QTimer::timeout, this, [this, task, progress]() {
		QMutexLocker locker(&progress->mutex);
		if (progress->total > 0) {
			// Bytes can exceed an int; report thousandths.
			m_activity.setProgress(task, int(std::clamp<qint64>(progress->done * 1000 / progress->total, 0, 1000)), 1000, progress->phase);
		} else if (!progress->phase.isEmpty()) {
			m_activity.setProgress(task, 0, 0, progress->phase);
		}
		refreshActivityCenter(task);
	});
	connect(thread, &QThread::finished, this, [this, thread, timer, task, profile, result]() {
		timer->stop();
		timer->deleteLater();
		thread->deleteLater();
		m_assetIndexJobs.remove(profile.id);
		const GameAssetRegister& reg = result->registerData;
		if (result->cancelled) {
			m_activity.cancelTask(task, tr("Indexing cancelled; the previous index, if any, is unchanged."));
		} else if (!result->succeeded) {
			m_activity.failTask(task, result->error);
		} else {
			for (const QString& warning : result->warnings) {
				m_activity.appendWarning(task, warning);
			}
			m_activity.completeTask(task, tr("%1: %2 stock files in %3 packages.").arg(profile.displayName, QLocale().toString(reg.files.size())).arg(reg.sources.size()));
		}
		persistActivityTask(task);
		refreshActivityCenter(task);
		refreshRecentActivityTimeline();
		refreshGameInstallations();
		refreshWorkspaceContextPanels();
		statusBar()->showMessage(result->succeeded ? tr("Indexed %1: %2 stock files.").arg(profile.displayName, QLocale().toString(reg.files.size()))
												   : result->cancelled ? tr("Indexing %1 was cancelled.").arg(profile.displayName)
																	   : tr("Indexing %1 failed: %2").arg(profile.displayName, result->error), 8000);
		for (const std::function<void(bool)>& waiter : m_assetIndexWaiters.take(profile.id)) {
			waiter(result->succeeded);
		}
	});
	connect(qApp, &QCoreApplication::aboutToQuit, thread, [thread, cancel]() {
		*cancel = true;
		thread->wait();
	});
	timer->start();
	thread->start();
}

bool ApplicationShell::cancelGameAssetIndexing(const QString& taskId)
{
	for (auto it = m_assetIndexJobs.cbegin(); it != m_assetIndexJobs.cend(); ++it) {
		if (it.value().first == taskId) {
			*it.value().second = true;
			m_activity.appendLog(taskId, OperationState::Running, tr("Cancelling…"));
			refreshActivityCenter(taskId);
			return true;
		}
	}
	return false;
}

QString ApplicationShell::gameAssetIndexStateText(const GameInstallationProfile& profile) const
{
	if (m_assetIndexJobs.contains(profile.id)) {
		return tr("indexing assets");
	}
	const GameAssetRegisterStatus status = gameAssetRegisterStatus(profile);
	if (!status.exists) {
		return tr("assets not indexed");
	}
	if (!status.loaded) {
		return tr("asset index unreadable");
	}
	return status.fresh ? tr("assets indexed") : tr("asset index out of date");
}

void ApplicationShell::refreshReleaseCard()
{
	if (!m_releaseHistory || !m_releaseSummary) {
		return;
	}
	m_releaseHistory->clear();
	const ProjectManifest manifest = currentProjectManifestOrDefault();
	if (manifest.rootPath.isEmpty()) {
		m_releaseSummary->setText(tr("No project open"));
		m_releaseHistory->addItem(disabledReleaseItem(tr("Open a project to package and release it.")));
		fitWorkspaceLists();
		return;
	}
	GameInstallationProfile installation;
	const bool hasInstallation = releaseGameInstallation(&installation);
	const QString gameKey = effectiveProjectGameKey(manifest, hasInstallation ? &installation : nullptr);
	const ProjectReleaseSettings release = effectiveProjectReleaseSettings(manifest, gameKey);
	QStringList warnings;
	const QVector<ReleaseRecord> records = listReleaseRecords(manifest.rootPath, &warnings);
	ProjectChangelog changelog;
	const QString changelogPath = QDir::isAbsolutePath(release.changelogFile) ? release.changelogFile : QDir(manifest.rootPath).absoluteFilePath(release.changelogFile);
	loadProjectChangelog(changelogPath, release.title, &changelog);
	const int unreleased = int(changelog.unreleasedEntries().size());
	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	m_releaseSummary->setText(records.isEmpty() ? tr("Not released yet") : tr("Latest: %1").arg(records.last().version));

	auto* changes = new QListWidgetItem(studioIcon(QStringLiteral("book"), unreleased > 0 ? StudioIconTone::Accent : StudioIconTone::Muted),
		QStringLiteral("%1\n%2").arg(unreleased > 0 ? tr("%n unreleased change(s)", nullptr, unreleased) : tr("No unreleased changes recorded"),
			QFileInfo::exists(changelogPath) ? nativeReleasePath(changelogPath) : tr("Record a change to start %1.").arg(QFileInfo(changelogPath).fileName())));
	changes->setData(Qt::UserRole, QFileInfo::exists(changelogPath) ? changelogPath : QString());
	// Rows can name a command to run when activated (see openWorkspaceRow).
	changes->setData(Qt::UserRole + 40, QFileInfo::exists(changelogPath) ? QString() : QStringLiteral("release.recordChange"));
	changes->setData(Qt::AccessibleTextRole, changes->text().replace(QLatin1Char('\n'), QStringLiteral(", ")));
	m_releaseHistory->addItem(changes);
	int shown = 0;
	for (auto it = records.crbegin(); it != records.crend() && shown < 5; ++it, ++shown) {
		const ReleaseRecord& record = *it;
		const QString when = QLocale().toString(record.publishedUtc.toLocalTime().date(), QLocale::ShortFormat);
		const QString size = QLocale().formattedDataSize(qint64(std::min<quint64>(record.packageBytes, quint64(std::numeric_limits<qint64>::max()))));
		auto* item = new QListWidgetItem(studioIcon(QStringLiteral("rocket"), shown == 0 ? StudioIconTone::Success : StudioIconTone::Muted),
			QStringLiteral("%1\n%2").arg(record.version + separator + record.packageFileName, when + separator + size + separator
				+ tr("%n file(s)", nullptr, int(record.files.size()))));
		const QString folder = record.outputDirectory.isEmpty() ? QString()
			: QDir::isAbsolutePath(record.outputDirectory) ? record.outputDirectory : QDir(manifest.rootPath).absoluteFilePath(record.outputDirectory);
		item->setData(Qt::UserRole, folder);
		item->setToolTip(tr("%1 %2\nSHA-256 %3\n%4").arg(record.title, record.version, record.packageSha256, nativeReleasePath(folder)));
		item->setData(Qt::AccessibleTextRole, tr("Release %1, %2, %3, %4").arg(record.version, record.packageFileName, when, size));
		m_releaseHistory->addItem(item);
	}
	if (records.isEmpty()) {
		m_releaseHistory->addItem(disabledReleaseItem(tr("No releases yet. Package and Release writes the first.")));
	}
	fitWorkspaceLists();
}

QListWidgetItem* ApplicationShell::disabledReleaseItem(const QString& text) const
{
	auto* item = new QListWidgetItem(text);
	item->setFlags(Qt::NoItemFlags);
	return item;
}

} // namespace vibestudio
