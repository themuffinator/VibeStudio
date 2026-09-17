#pragma once

// Shell command registry, shortcut wiring, and the command palette.
//
// The registry is the single place a shell command is declared. Menus, the
// toolbar, the command palette, and keyboard shortcuts are all generated from
// it, and the default shortcut for a command comes from
// core/studio_semantics.h so the documented registry and the running app cannot
// drift apart.

#include "core/studio_semantics.h"

#include <QAction>
#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QMenuBar;
class QToolBar;
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
	void setProjectAvailable(bool available);
	[[nodiscard]] bool projectAvailable() const;

	// Shortcut conflicts detected while wiring, for the diagnostics surface.
	[[nodiscard]] QStringList shortcutConflicts() const;
	[[nodiscard]] QString shortcutForCommand(const QString& commandId) const;

	void populateMenuBar(QMenuBar* menuBar) const;
	void populateToolBar(QToolBar* toolBar) const;

private:
	void installShortcuts();

	QWidget* m_host = nullptr;
	QHash<QString, QAction*> m_actions;
	QVector<StudioCommandRegistration> m_registrations;
	QHash<QString, QString> m_shortcuts;
	QString m_editorProfileId;
	QStringList m_conflicts;
	bool m_projectAvailable = false;
};

QString studioCommandGroupId(StudioCommandGroup group);
QString studioCommandGroupTitle(StudioCommandGroup group);

// Type-to-filter command launcher. Lists every enabled registry command plus
// the documented palette entries from core/studio_semantics.h.
class CommandPaletteDialog final : public QDialog {
	Q_OBJECT

public:
	explicit CommandPaletteDialog(const StudioCommandRegistry& registry, QWidget* parent = nullptr);

	void setExtraEntries(const QVector<CommandPaletteEntry>& entries);
	void focusFilter();
	[[nodiscard]] QString selectedCommandId() const;

Q_SIGNALS:
	void commandChosen(const QString& commandId);

protected:
	void showEvent(QShowEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	struct Row {
		QString commandId;
		QString label;
		QString category;
		QString summary;
		QString shortcut;
		bool enabled = true;
		bool destructive = false;
	};

	void rebuildRows();
	void applyFilter();
	void activateCurrentRow();

	const StudioCommandRegistry& m_registry;
	QVector<CommandPaletteEntry> m_extraEntries;
	QVector<Row> m_rows;
	QVector<Row> m_visibleRows;
	QLineEdit* m_filter = nullptr;
	QListWidget* m_list = nullptr;
	QLabel* m_hint = nullptr;
	QString m_selectedCommandId;
};

} // namespace vibestudio
