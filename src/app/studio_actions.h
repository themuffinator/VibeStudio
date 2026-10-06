#pragma once

// Shell command registry, shortcut wiring, and the command palette.
//
// The registry is the single place a shell command is declared. Menus, the
// toolbar, the command palette, and keyboard shortcuts are all generated from
// it, and the default shortcut for a command comes from
// core/studio_semantics.h so the documented registry and the running app cannot
// drift apart.

#include "core/studio_semantics.h"
#include "app/quick_open_catalog.h"

#include <QAction>
#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QLabel;
class QAbstractButton;
class QLineEdit;
class QListWidget;
class QMenu;
class QMenuBar;
class QToolBar;
class QProgressBar;
class QPushButton;
class QTimer;
class QWidget;

namespace vibestudio {

enum class StudioCommandGroup {
	File,
	Edit,
	View,
	Project,
	Build,
	Tools,
	Help,
};

struct StudioCommandRegistration {
	QString commandId;
	StudioCommandGroup group = StudioCommandGroup::Tools;
	QString label;
	QString statusTip;
	QString iconName;
	bool checkable = false;
	bool separatorBefore = false;
	bool toolbar = false;
	bool destructive = false;
	bool requiresProject = false;
	std::function<void()> handler;
	// Optional translated submenu inside the command group.
	QString menuSection;
};

// Owns the QAction instances for the shell. Actions are parented to the host
// widget, so the registry must not outlive it.
class StudioCommandRegistry final {
public:
	explicit StudioCommandRegistry(QWidget* host);

	QAction* registerCommand(const StudioCommandRegistration& registration);
	[[nodiscard]] QAction* action(const QString& commandId) const;
	[[nodiscard]] QStringList commandIds() const;
	[[nodiscard]] QVector<StudioCommandRegistration> registrations() const;
	[[nodiscard]] QVector<StudioCommandRegistration> registrationsForGroup(StudioCommandGroup group) const;

	// Rebinds shortcuts to the sequences an editor profile prefers. Commands the
	// profile does not mention keep the documented default from
	// core/studio_semantics.h. Passing an empty id restores every default, so
	// switching profiles is not cumulative.
	void applyEditorProfile(const QString& editorProfileId);
	[[nodiscard]] QString editorProfileId() const;

	void setEnabled(const QString& commandId, bool enabled);
	void setChecked(const QString& commandId, bool checked);

	// Limits a command's keyboard shortcut to the given surfaces: it fires only
	// while keyboard focus is inside one of them. Menus and the palette still
	// run the command from anywhere. Commands with no scope stay window-wide.
	// Used for keys that mean something different, or something destructive,
	// outside their own surface, such as Del staging a package deletion.
	void setShortcutScopes(const QString& commandId, const QList<QWidget*>& scopes);
	void setProjectAvailable(bool available);
	[[nodiscard]] bool projectAvailable() const;

	// Shortcut conflicts detected while wiring, for the diagnostics surface.
	[[nodiscard]] QStringList shortcutConflicts() const;
	[[nodiscard]] QString shortcutForCommand(const QString& commandId) const;

	// The user's own keys, keyed by command id. Each replaces every sequence
	// the command would otherwise get (the profile's, the default, and the
	// alternates), and an empty list leaves it without keys. They are
	// installed first, so a key the user gave one command is taken from any
	// command whose built-in key it was, rather than refused.
	void setUserShortcuts(const QHash<QString, QStringList>& shortcuts);
	// Keyed by registered command id; entries for commands that are not
	// registered are left out.
	[[nodiscard]] QHash<QString, QStringList> userShortcuts() const;
	[[nodiscard]] bool hasUserShortcut(const QString& commandId) const;
	// The keys a command gets without the user's own: the profile's, then the
	// documented default and alternates, in portable text.
	[[nodiscard]] QStringList builtInShortcuts(const QString& commandId) const;
	// The commands that already answer `sequence` wherever `commandId` would
	// fire, which giving the key to `commandId` would take it from; empty when
	// the key is free there. A surface inside another (the map view inside
	// Levels) counts as the same place, since Qt could not tell them apart.
	[[nodiscard]] QStringList commandsUsingShortcut(const QString& sequence, const QString& commandId) const;
	// A command's keys as installed, in the order they were accepted.
	[[nodiscard]] QList<QKeySequence> shortcutSequences(const QString& commandId) const;
	// False for the few keys the studio relies on, such as Escape cancelling
	// a task, which the user cannot rebind.
	[[nodiscard]] bool shortcutRemappable(const QString& commandId) const;

	void populateMenuBar(QMenuBar* menuBar) const;
	void populateToolBar(QToolBar* toolBar) const;

	// Holds shortcut installation back while many commands or scopes are set
	// at once, then installs every key once when the outermost batch ends.
	// Inside a batch, queries answer with the keys installed before it.
	void beginBatch();
	void endBatch();

private:
	void installShortcuts();
	// The keys the selected editor profile gives commands, by normalized
	// command id: a profile's keys replace the command's own, and an empty
	// list takes them away.
	[[nodiscard]] QHash<QString, QStringList> profileOverrides() const;
	[[nodiscard]] QStringList builtInCandidates(const QString& commandId, const QHash<QString, QStringList>& overrides) const;
	// The documented shortcut for a command, from the index each install
	// reads, falling back to the full list before the first install.
	[[nodiscard]] bool documentedShortcut(const QString& commandId, ShortcutDescriptor* descriptor) const;

	QWidget* m_host = nullptr;
	QHash<QString, QAction*> m_actions;
	QVector<StudioCommandRegistration> m_registrations;
	QHash<QString, QString> m_shortcuts;
	QHash<QString, QList<QKeySequence>> m_sequences;
	QHash<QString, QList<QPointer<QWidget>>> m_shortcutScopes;
	QString m_editorProfileId;
	QHash<QString, QStringList> m_userShortcuts;
	QStringList m_conflicts;
	bool m_projectAvailable = false;
	// The documented shortcuts by normalized command id, read once per
	// install: building that list translates every label, so reading it once
	// per command made each install, and so startup, very slow.
	QHash<QString, ShortcutDescriptor> m_descriptors;
	int m_batchDepth = 0;
	bool m_installPending = false;
};

// The spelling registry and semantics ids share: "map.deleteSelection" and
// "map.delete-selection" give the same token.
QString studioCommandToken(const QString& commandId);
QString studioCommandGroupId(StudioCommandGroup group);
QString studioCommandGroupTitle(StudioCommandGroup group);
// A menu label without its mnemonic marker: "&Open Project" reads
// "Open Project", and a doubled "&&" stays one literal ampersand.
QString commandLabelWithoutMnemonic(const QString& label);
// Keeps header and empty-state buttons on the same enabled state and handler
// as menus, the command palette and custom shortcuts. The button owns its label.
void bindCommandButton(QAbstractButton* button, QAction* action);

// Type-to-filter command launcher. Lists every enabled registry command plus
// the documented palette entries from core/studio_semantics.h.
class CommandPaletteDialog final : public QDialog {
	Q_OBJECT

public:
	explicit CommandPaletteDialog(const StudioCommandRegistry& registry, QWidget* parent = nullptr);

	void setExtraEntries(const QVector<CommandPaletteEntry>& entries);
	// Commands run from the palette before, newest first; with an empty
	// filter they lead the list.
	void setRecentCommands(const QStringList& commandIds);
	void focusFilter();
	[[nodiscard]] QString selectedCommandId() const;

Q_SIGNALS:
	void commandChosen(const QString& commandId);

protected:
	void showEvent(QShowEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	struct Row {
		QString commandId;
		QString label;
		QString category;
		QString summary;
		QString shortcut;
		QString iconName;
		bool enabled = true;
		bool destructive = false;
	};

	void rebuildRows();
	void applyFilter();
	void activateCurrentRow();

	const StudioCommandRegistry& m_registry;
	QVector<CommandPaletteEntry> m_extraEntries;
	QStringList m_recentCommands;
	QVector<Row> m_rows;
	QVector<Row> m_visibleRows;
	QLineEdit* m_filter = nullptr;
	QListWidget* m_list = nullptr;
	QLabel* m_hint = nullptr;
	QString m_selectedCommandId;
	// Room left around the panel for its shadow (see prepareFloatingPanel()).
	int m_shadowMargin = 0;
};

// The words a QuickOpenDialog uses, so the same picker can list something
// other than files, such as the open file's symbols. Empty fields keep the
// file wording.
struct QuickOpenPurpose {
	QString title;
	QString placeholder;
	QString listName;
	QString emptyHint;
	QString noMatchHint;
	QString keyHelp;
};

// Type-to-find launcher for files, matched the way the command palette matches
// commands: names that start with the text first, then names with a word that
// does, then names whose folder and name hold its letters in order.
class QuickOpenDialog final : public QDialog {
	Q_OBJECT

public:
	static constexpr int kMaximumRows = 200;

	explicit QuickOpenDialog(QWidget* parent = nullptr);

	// `note` is shown under the list, for example when the project had more
	// files than were gathered.
	void setEntries(const QVector<QuickOpenEntry>& entries, const QString& note = QString());
	void setPurpose(const QuickOpenPurpose& purpose);
	void focusFilter();
	void startCatalog(QuickOpenRequest request);
	void cancelCatalog();
	bool catalogBusy() const { return m_scanActive; }
	bool filtering() const { return m_filtering; }
	std::function<void()> catalogStarted;
	std::function<void(int, int)> catalogProgress;
	std::function<void(OperationState, const QString&, const QStringList&)> catalogFinished;

Q_SIGNALS:
	void entryChosen(const QString& key);
	void refreshRequested();

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void applyFilter(bool preserveSelection = false);
	void filterBatch();
	void finishFilter();
	void finishCatalog(OperationState state, const QString& summary, const QStringList& warnings = {});
	void activateCurrentRow();

	QVector<QuickOpenEntry> m_entries;
	QuickOpenPurpose m_purpose;
	QString m_note;
	QLineEdit* m_filter = nullptr;
	QListWidget* m_list = nullptr;
	QLabel* m_hint = nullptr;
	QLabel* m_scanStatus = nullptr;
	QProgressBar* m_scanProgress = nullptr;
	QPushButton* m_cancelScan = nullptr;
	QPushButton* m_refreshScan = nullptr;
	QWidget* m_scanControls = nullptr;
	QuickOpenCatalog* m_catalog = nullptr;
	QTimer* m_filterTimer = nullptr;
	QString m_filterNeedle;
	QString m_retainedKey;
	QVector<int> m_ranked[3];
	int m_filterCursor = 0;
	int m_matchCount = 0;
	bool m_filtering = false;
	bool m_scanActive = false;
	// Room left around the panel for its shadow (see prepareFloatingPanel()).
	int m_shadowMargin = 0;
};

} // namespace vibestudio
