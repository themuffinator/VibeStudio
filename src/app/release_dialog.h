#pragma once

// Package and Release: one place to turn a project, a map, a model or a set
// of textures into something players can install.
//
// The left column says what is released and how it is described; the right
// column reviews the plan as it updates: what ships, what the game already
// provides, and what blocks publishing, with the release notes and readme to
// edit before anything is written. Planning and publishing run on workers
// (core/release_plan.h, core/release_publish.h); the shell supplies the
// project, its installation, indexing, Activity and navigation through hooks,
// so the dialog never edits shell state itself.

#include "core/game_installation.h"
#include "core/project_manifest.h"
#include "core/release_notes.h"
#include "core/release_plan.h"
#include "core/release_publish.h"

#include <QCoreApplication>
#include <QDialog>
#include <QSet>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vibestudio {

class CompositionChart;
class LoadingPane;
class NoticeBar;

struct ReleaseDialogHooks {
	// The project the release comes from. An empty root means no project is
	// open; maps and models chosen by path still work.
	std::function<ProjectManifest()> project;
	// The installation the project plays in, if any.
	std::function<bool(GameInstallationProfile*)> installation;
	// The open package or draft, offered as an opt-in layer over the project.
	std::function<std::shared_ptr<const PackageArchiveReader>()> openPackage;
	std::function<QString()> openPackageLabel;
	// Saves the release settings and game into the project manifest.
	std::function<bool(const ProjectReleaseSettings& release, const QString& gameKey, QString* error)> saveReleaseSettings;
	// Indexes the installation's stock packages; `done` runs on the GUI thread.
	std::function<void(const QString& installationId, std::function<void(bool succeeded)> done)> indexInstallation;
	// Opens the Build page on a map the plan says is not built.
	std::function<void(const QString& mapPath)> buildMap;
	std::function<void(const QString& path)> reveal;
	std::function<void(const QString& packagePath)> openInPackages;
	// The Activity panel: a task starts, then ends.
	std::function<QString(const QString& title, const QString& detail)> beginTask;
	std::function<void(const QString& task, bool succeeded, bool cancelled, const QString& summary)> endTask;
	// Called after a release is written, so the shell can refresh its cards.
	std::function<void(const ReleasePublishResult& result)> published;
};

class ReleaseDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioReleaseDialog)

public:
	ReleaseDialog(QWidget* parent, ReleaseDialogHooks hooks);
	~ReleaseDialog() override;

	// Chooses what to release; items are absolute paths. Plans again.
	void setSelection(ReleaseScope scope, const QStringList& items);
	// Reloads the project, its catalog and the game's index, then plans.
	void reload();
	void publish();
	// Reviewing, publishing, or about to review after a change.
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const ReleasePlan& plan() const;
	[[nodiscard]] const ReleasePublishResult& lastResult() const;
	[[nodiscard]] QString notesText() const;
	[[nodiscard]] QString readmeText() const;
	[[nodiscard]] QString outputDirectory() const;
	void setOutputDirectory(const QString& path);

protected:
	void reject() override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	struct Work;
	struct PlanOutcome;

	void buildUi();
	void loadProjectFields();
	void populateItems();
	void schedulePlan();
	void startPlan();
	void applyPlan(const std::shared_ptr<PlanOutcome>& outcome);
	void showStockNotice();
	void regenerateNotes();
	void addChangelogEntry();
	void refreshUnreleased();
	void refreshVersionState();
	void refreshPublishState();
	void refreshDefaultOutput();
	void setBusy(bool busy, const QString& status);
	void finishPublish(const ReleasePublishResult& result);
	void chooseOutput();
	void addItems();
	void activateProblem(QTreeWidgetItem* item);
	[[nodiscard]] ReleaseScope currentScope() const;
	[[nodiscard]] QStringList checkedItems() const;
	[[nodiscard]] ProjectReleaseSettings releaseFromFields() const;

	ReleaseDialogHooks m_hooks;
	std::shared_ptr<Work> m_work;
	ProjectManifest m_manifest;
	GameInstallationProfile m_installation;
	bool m_hasInstallation = false;
	QString m_gameKey;
	ReleaseCatalog m_catalog;
	ReleaseStockContext m_stock;
	ReleasePlan m_plan;
	QVector<ChangelogEntry> m_changes;
	QVector<ChangelogEntry> m_additions;
	ReleaseInventoryDiff m_diff;
	ProjectChangelog m_changelog;
	ReleasePublishResult m_result;
	ReleaseScope m_pendingScope = ReleaseScope::Project;
	QStringList m_pendingItems;
	QStringList m_extraItems;
	// Ticked items of every scope, so switching scope keeps each one's choice.
	QSet<QString> m_ticked;
	QString m_task;
	quint64 m_generation = 0;
	bool m_busy = false;
	bool m_notesEdited = false;
	bool m_readmeEdited = false;
	bool m_settingText = false;
	bool m_loaded = false;
	bool m_catalogLoaded = false;
	// The output folder last filled in automatically, so edits to it stick.
	QString m_defaultOutput;

	NoticeBar* m_notice = nullptr;
	QLabel* m_context = nullptr;
	QLineEdit* m_title = nullptr;
	QLineEdit* m_version = nullptr;
	QToolButton* m_nextVersion = nullptr;
	QLabel* m_versionState = nullptr;
	QLineEdit* m_authors = nullptr;
	QPlainTextEdit* m_description = nullptr;
	QLineEdit* m_website = nullptr;
	QLineEdit* m_license = nullptr;
	QComboBox* m_scope = nullptr;
	QLabel* m_scopeHint = nullptr;
	QListWidget* m_items = nullptr;
	QPushButton* m_addItems = nullptr;
	QCheckBox* m_useOpenPackage = nullptr;
	QComboBox* m_format = nullptr;
	QLineEdit* m_packageName = nullptr;
	QLineEdit* m_gameFolder = nullptr;
	QCheckBox* m_includeSources = nullptr;
	QComboBox* m_compression = nullptr;
	LoadingPane* m_state = nullptr;
	QToolButton* m_includedChip = nullptr;
	QToolButton* m_stockChip = nullptr;
	QToolButton* m_overrideChip = nullptr;
	QToolButton* m_problemChip = nullptr;
	CompositionChart* m_chart = nullptr;
	QTabWidget* m_tabs = nullptr;
	QTreeWidget* m_included = nullptr;
	QTreeWidget* m_provided = nullptr;
	QTreeWidget* m_problems = nullptr;
	QPlainTextEdit* m_notes = nullptr;
	QPlainTextEdit* m_readme = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QListWidget* m_unreleased = nullptr;
	QComboBox* m_changeCategory = nullptr;
	QLineEdit* m_changeText = nullptr;
	QPushButton* m_addChange = nullptr;
	QPushButton* m_regenerate = nullptr;
	QLineEdit* m_output = nullptr;
	QCheckBox* m_archive = nullptr;
	QCheckBox* m_updateChangelog = nullptr;
	QCheckBox* m_replace = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_publish = nullptr;
	QPushButton* m_cancel = nullptr;
	QPushButton* m_reveal = nullptr;
	QTimer* m_planTimer = nullptr;
	QTimer* m_progressTimer = nullptr;
};

} // namespace vibestudio
