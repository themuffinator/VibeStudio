#include "app/studio_actions.h"

#include "app/studio_icons.h"

#include "core/editor_profiles.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPalette>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QShowEvent>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace vibestudio {

namespace {

// StudioCommandRegistry is not a QObject, and neither are the free helpers in
// this file, so user-visible strings outside the palette dialog go through an
// explicit translation context. CommandPaletteDialog has Q_OBJECT and uses tr().
QString actionsText(const char* source)
{
	return QCoreApplication::translate("VibeStudioStudioActions", source);
}

// Same rule as core/studio_semantics.cpp: "shell.commandPalette" and
// "shell.command-palette" are one command.
QString normalizedToken(const QString& value)
{
	const QString trimmed = value.trimmed();
	QString normalized;
	normalized.reserve(trimmed.size() + 8);
	for (int index = 0; index < trimmed.size(); ++index) {
		const QChar ch = trimmed.at(index);
		if (ch.isUpper() && index > 0 && (trimmed.at(index - 1).isLower() || trimmed.at(index - 1).isDigit())) {
			normalized += QLatin1Char('-');
		}
		normalized += ch.toLower();
	}
	normalized.replace('_', '-');
	normalized.replace(' ', '-');
	return normalized;
}

// Portable, canonical spelling of a key sequence so that "ctrl+o" and "Ctrl+O"
// collide during conflict detection.
QString normalizedSequenceKey(const QKeySequence& sequence)
{
	return sequence.toString(QKeySequence::PortableText).toLower();
}

// Maps the registry's symbolic icon names onto the platform style's standard
// pixmaps. Unknown names intentionally produce a null icon rather than a
// misleading one.
bool standardPixmapForIconName(const QString& iconName, QStyle::StandardPixmap* out)
{
	static const QHash<QString, QStyle::StandardPixmap> mapping = {
		{QStringLiteral("open"), QStyle::SP_DirOpenIcon},
		{QStringLiteral("folder-open"), QStyle::SP_DirOpenIcon},
		{QStringLiteral("document-open"), QStyle::SP_DirOpenIcon},
		{QStringLiteral("project-open"), QStyle::SP_DirOpenIcon},
		{QStringLiteral("folder"), QStyle::SP_DirIcon},
		{QStringLiteral("project"), QStyle::SP_DirIcon},
		{QStringLiteral("file"), QStyle::SP_FileIcon},
		{QStringLiteral("document"), QStyle::SP_FileIcon},
		{QStringLiteral("new"), QStyle::SP_FileIcon},
		{QStringLiteral("document-new"), QStyle::SP_FileIcon},
		{QStringLiteral("save"), QStyle::SP_DialogSaveButton},
		{QStringLiteral("save-as"), QStyle::SP_DialogSaveButton},
		{QStringLiteral("export"), QStyle::SP_DialogSaveButton},
		{QStringLiteral("refresh"), QStyle::SP_BrowserReload},
		{QStringLiteral("reload"), QStyle::SP_BrowserReload},
		{QStringLiteral("rescan"), QStyle::SP_BrowserReload},
		{QStringLiteral("run"), QStyle::SP_MediaPlay},
		{QStringLiteral("play"), QStyle::SP_MediaPlay},
		{QStringLiteral("build"), QStyle::SP_MediaPlay},
		{QStringLiteral("compile"), QStyle::SP_MediaPlay},
		{QStringLiteral("launch"), QStyle::SP_MediaPlay},
		{QStringLiteral("stop"), QStyle::SP_MediaStop},
		{QStringLiteral("pause"), QStyle::SP_MediaPause},
		{QStringLiteral("cancel"), QStyle::SP_DialogCancelButton},
		{QStringLiteral("close"), QStyle::SP_DialogCloseButton},
		{QStringLiteral("quit"), QStyle::SP_DialogCloseButton},
		{QStringLiteral("delete"), QStyle::SP_TrashIcon},
		{QStringLiteral("remove"), QStyle::SP_TrashIcon},
		{QStringLiteral("trash"), QStyle::SP_TrashIcon},
		{QStringLiteral("apply"), QStyle::SP_DialogApplyButton},
		{QStringLiteral("accept"), QStyle::SP_DialogApplyButton},
		{QStringLiteral("validate"), QStyle::SP_DialogApplyButton},
		{QStringLiteral("reset"), QStyle::SP_DialogResetButton},
		{QStringLiteral("restore"), QStyle::SP_DialogResetButton},
		{QStringLiteral("help"), QStyle::SP_DialogHelpButton},
		{QStringLiteral("about"), QStyle::SP_DialogHelpButton},
		{QStringLiteral("info"), QStyle::SP_MessageBoxInformation},
		{QStringLiteral("information"), QStyle::SP_MessageBoxInformation},
		{QStringLiteral("warning"), QStyle::SP_MessageBoxWarning},
		{QStringLiteral("error"), QStyle::SP_MessageBoxCritical},
		{QStringLiteral("critical"), QStyle::SP_MessageBoxCritical},
		{QStringLiteral("question"), QStyle::SP_MessageBoxQuestion},
		{QStringLiteral("search"), QStyle::SP_FileDialogContentsView},
		{QStringLiteral("find"), QStyle::SP_FileDialogContentsView},
		{QStringLiteral("detect"), QStyle::SP_FileDialogContentsView},
		{QStringLiteral("details"), QStyle::SP_FileDialogDetailedView},
		{QStringLiteral("diagnostics"), QStyle::SP_FileDialogDetailedView},
		{QStringLiteral("report"), QStyle::SP_FileDialogDetailedView},
		{QStringLiteral("log"), QStyle::SP_FileDialogDetailedView},
		{QStringLiteral("settings"), QStyle::SP_FileDialogDetailedView},
		{QStringLiteral("preferences"), QStyle::SP_FileDialogDetailedView},
		{QStringLiteral("list"), QStyle::SP_FileDialogListView},
		{QStringLiteral("back"), QStyle::SP_ArrowBack},
		{QStringLiteral("previous"), QStyle::SP_ArrowBack},
		{QStringLiteral("forward"), QStyle::SP_ArrowForward},
		{QStringLiteral("next"), QStyle::SP_ArrowForward},
		{QStringLiteral("up"), QStyle::SP_ArrowUp},
		{QStringLiteral("import"), QStyle::SP_ArrowUp},
		{QStringLiteral("down"), QStyle::SP_ArrowDown},
		{QStringLiteral("computer"), QStyle::SP_ComputerIcon},
		{QStringLiteral("install"), QStyle::SP_ComputerIcon},
		{QStringLiteral("game"), QStyle::SP_ComputerIcon},
		{QStringLiteral("package"), QStyle::SP_DriveHDIcon},
		{QStringLiteral("archive"), QStyle::SP_DriveHDIcon},
		{QStringLiteral("drive"), QStyle::SP_DriveHDIcon},
		{QStringLiteral("link"), QStyle::SP_FileLinkIcon},
	};

	const auto it = mapping.constFind(normalizedToken(iconName));
	if (it == mapping.constEnd()) {
		return false;
	}
	if (out) {
		*out = it.value();
	}
	return true;
}

// "Open Project Folder (Ctrl+O)" on the first line, what it does beneath, so an
// icon-only tool button explains itself and names its shortcut.
QString commandToolTip(const QString& label, const QString& statusTip, const QString& shortcut)
{
	QString name = label;
	name.remove(QLatin1Char('&'));
	name.remove(QChar(0x2026));
	name = name.trimmed();
	if (!shortcut.isEmpty()) {
		name = actionsText("%1 (%2)").arg(name, shortcut.section(QStringLiteral(", "), 0, 0));
	}
	return statusTip.isEmpty() ? name : QStringLiteral("%1\n%2").arg(name, statusTip);
}

const char* const kExplicitlyDisabledProperty = "vibestudioExplicitlyDisabled";

bool explicitlyDisabled(const QAction* action)
{
	return action && action->property(kExplicitlyDisabledProperty).toBool();
}

// Palette row data roles for the item delegate below.
constexpr int kCategoryRole = Qt::UserRole + 1;
constexpr int kShortcutRole = Qt::UserRole + 2;
constexpr int kRowEnabledRole = Qt::UserRole + 3;
constexpr int kCommandIdRole = Qt::UserRole + 4;

// Paints "label  category ............ shortcut" so the shortcut column stays
// right-aligned regardless of the label width, and so unavailable rows can be
// dimmed without being removed from the list.
class PaletteRowDelegate final : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QSize hint = QStyledItemDelegate::sizeHint(option, index);
		hint.setHeight(std::max(hint.height(), option.fontMetrics.height() + 10));
		return hint;
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		const QString label = opt.text;
		opt.text.clear();

		const QWidget* widget = opt.widget;
		QStyle* style = widget ? widget->style() : QApplication::style();
		style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

		const bool rowEnabled = index.data(kRowEnabledRole).toBool();
		const QString category = index.data(kCategoryRole).toString();
		const QString shortcut = index.data(kShortcutRole).toString();
		const bool selected = opt.state.testFlag(QStyle::State_Selected);

		QRect content = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
		content.adjust(4, 0, -4, 0);
		if (content.width() <= 0) {
			return;
		}

		const QFontMetrics metrics(opt.font);
		const QColor primary = selected
			? opt.palette.color(QPalette::Normal, QPalette::HighlightedText)
			: opt.palette.color(rowEnabled ? QPalette::Normal : QPalette::Disabled, QPalette::Text);
		QColor secondary = primary;
		secondary.setAlpha(rowEnabled ? 160 : 110);

		painter->save();
		painter->setFont(opt.font);

		int reserved = 0;
		if (!shortcut.isEmpty()) {
			reserved = metrics.horizontalAdvance(shortcut) + 16;
			QRect shortcutRect = content;
			shortcutRect.setLeft(std::max(content.left(), content.right() - reserved));
			painter->setPen(secondary);
			painter->drawText(shortcutRect, Qt::AlignRight | Qt::AlignVCenter, shortcut);
		}

		QRect textRect = content.adjusted(0, 0, -reserved, 0);
		if (textRect.width() <= 0) {
			painter->restore();
			return;
		}

		const QString elidedLabel = metrics.elidedText(label, Qt::ElideRight, textRect.width());
		painter->setPen(primary);
		painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedLabel);

		if (!category.isEmpty()) {
			const int used = metrics.horizontalAdvance(elidedLabel) + 12;
			QRect categoryRect = textRect.adjusted(used, 0, 0, 0);
			if (categoryRect.width() > 24) {
				painter->setPen(secondary);
				painter->drawText(categoryRect,
					Qt::AlignLeft | Qt::AlignVCenter,
					metrics.elidedText(category, Qt::ElideRight, categoryRect.width()));
			}
		}

		painter->restore();
	}
};

// Case-insensitive subsequence test: every character of `needle` must appear in
// `haystack` in order, but not necessarily adjacently, so "opk" matches
// "Open Package". Both arguments are expected to be lower-cased already.
bool isSubsequence(const QString& haystack, const QString& needle)
{
	int cursor = 0;
	for (const QChar ch : needle) {
		if (ch.isSpace()) {
			continue;
		}
		const int found = haystack.indexOf(ch, cursor);
		if (found < 0) {
			return false;
		}
		cursor = found + 1;
	}
	return true;
}

bool hasWordStart(const QString& haystack, const QString& needle)
{
	if (needle.isEmpty()) {
		return true;
	}
	int index = haystack.indexOf(needle);
	while (index >= 0) {
		if (index == 0 || !haystack.at(index - 1).isLetterOrNumber()) {
			return true;
		}
		index = haystack.indexOf(needle, index + 1);
	}
	return false;
}

} // namespace

QString studioCommandGroupId(StudioCommandGroup group)
{
	switch (group) {
	case StudioCommandGroup::File:
		return QStringLiteral("file");
	case StudioCommandGroup::Edit:
		return QStringLiteral("edit");
	case StudioCommandGroup::View:
		return QStringLiteral("view");
	case StudioCommandGroup::Project:
		return QStringLiteral("project");
	case StudioCommandGroup::Build:
		return QStringLiteral("build");
	case StudioCommandGroup::Tools:
		return QStringLiteral("tools");
	case StudioCommandGroup::Help:
		return QStringLiteral("help");
	}
	return QStringLiteral("tools");
}

QString studioCommandGroupTitle(StudioCommandGroup group)
{
	switch (group) {
	case StudioCommandGroup::File:
		return actionsText("File");
	case StudioCommandGroup::Edit:
		return actionsText("Edit");
	case StudioCommandGroup::View:
		return actionsText("View");
	case StudioCommandGroup::Project:
		return actionsText("Project");
	case StudioCommandGroup::Build:
		return actionsText("Build");
	case StudioCommandGroup::Tools:
		return actionsText("Tools");
	case StudioCommandGroup::Help:
		return actionsText("Help");
	}
	return actionsText("Tools");
}

namespace {

// Menu titles keep their own translatable sources so translators can place the
// "&" mnemonic on a letter that makes sense in the target language.
QString studioCommandGroupMenuTitle(StudioCommandGroup group)
{
	switch (group) {
	case StudioCommandGroup::File:
		return actionsText("&File");
	case StudioCommandGroup::Edit:
		return actionsText("&Edit");
	case StudioCommandGroup::View:
		return actionsText("&View");
	case StudioCommandGroup::Project:
		return actionsText("&Project");
	case StudioCommandGroup::Build:
		return actionsText("&Build");
	case StudioCommandGroup::Tools:
		return actionsText("&Tools");
	case StudioCommandGroup::Help:
		return actionsText("&Help");
	}
	return actionsText("&Tools");
}

const QVector<StudioCommandGroup>& studioCommandGroupOrder()
{
	static const QVector<StudioCommandGroup> order = {
		StudioCommandGroup::File,
		StudioCommandGroup::Edit,
		StudioCommandGroup::View,
		StudioCommandGroup::Project,
		StudioCommandGroup::Build,
		StudioCommandGroup::Tools,
		StudioCommandGroup::Help,
	};
	return order;
}

} // namespace

StudioCommandRegistry::StudioCommandRegistry(QWidget* host)
	: m_host(host)
{
}

QAction* StudioCommandRegistry::registerCommand(const StudioCommandRegistration& registration)
{
	if (registration.commandId.trimmed().isEmpty()) {
		return nullptr;
	}
	if (QAction* existing = m_actions.value(registration.commandId, nullptr)) {
		return existing;
	}

	auto* action = new QAction(registration.label, m_host);
	action->setObjectName(registration.commandId);
	action->setData(registration.commandId);
	action->setStatusTip(registration.statusTip);
	action->setToolTip(commandToolTip(registration.label, registration.statusTip, QString()));
	action->setCheckable(registration.checkable);
	action->setProperty("commandId", registration.commandId);
	action->setProperty("destructive", registration.destructive);
	action->setProperty("requiresProject", registration.requiresProject);

	// Studio glyphs follow the active theme; the platform's standard pixmaps are
	// only a fallback for names the glyph set does not cover.
	if (!registration.iconName.trimmed().isEmpty()) {
		QIcon icon = studioIcon(registration.iconName);
		QStyle::StandardPixmap pixmap = QStyle::SP_FileIcon;
		if (icon.isNull() && standardPixmapForIconName(registration.iconName, &pixmap)) {
			QStyle* style = m_host ? m_host->style() : QApplication::style();
			if (style) {
				icon = style->standardIcon(pixmap, nullptr, m_host);
			}
		}
		action->setIcon(icon);
	}

	if (registration.handler) {
		const std::function<void()> handler = registration.handler;
		QObject::connect(action, &QAction::triggered, action, [handler](bool) {
			handler();
		});
	}

	if (registration.requiresProject && !m_projectAvailable) {
		action->setEnabled(false);
	}

	m_actions.insert(registration.commandId, action);
	m_registrations.push_back(registration);
	installShortcuts();
	return action;
}

QAction* StudioCommandRegistry::action(const QString& commandId) const
{
	return m_actions.value(commandId, nullptr);
}

QStringList StudioCommandRegistry::commandIds() const
{
	QStringList ids;
	ids.reserve(m_registrations.size());
	for (const StudioCommandRegistration& registration : m_registrations) {
		ids << registration.commandId;
	}
	return ids;
}

QVector<StudioCommandRegistration> StudioCommandRegistry::registrations() const
{
	return m_registrations;
}

QVector<StudioCommandRegistration> StudioCommandRegistry::registrationsForGroup(StudioCommandGroup group) const
{
	QVector<StudioCommandRegistration> filtered;
	for (const StudioCommandRegistration& registration : m_registrations) {
		if (registration.group == group) {
			filtered.push_back(registration);
		}
	}
	return filtered;
}

void StudioCommandRegistry::installShortcuts()
{
	// Shortcuts are installed for the whole registry at once, in registration
	// order, so conflict resolution does not depend on which command happened to
	// be registered last and a profile switch can rebind everything cleanly.
	m_conflicts.clear();
	m_shortcuts.clear();

	// A selected editor profile may prefer different sequences for commands the
	// shell already owns. Anything the profile does not mention keeps the
	// documented default from core/studio_semantics.h.
	QHash<QString, QString> profileOverrides;
	if (!m_editorProfileId.trimmed().isEmpty()) {
		EditorProfileDescriptor profile;
		if (editorProfileForId(m_editorProfileId, &profile)) {
			for (const EditorProfileBinding& binding : profile.bindings) {
				if (binding.commandId.trimmed().isEmpty() || binding.shortcut.trimmed().isEmpty()) {
					continue;
				}
				if (!binding.implemented) {
					continue;
				}
				profileOverrides.insert(normalizedToken(binding.commandId), binding.shortcut);
			}
		}
	}

	QHash<QString, QString> owners;
	for (const StudioCommandRegistration& registration : m_registrations) {
		QAction* action = m_actions.value(registration.commandId, nullptr);
		if (!action) {
			continue;
		}
		action->setShortcuts({});

		QStringList candidates;
		const auto override = profileOverrides.constFind(normalizedToken(registration.commandId));
		if (override != profileOverrides.constEnd()) {
			candidates << override.value();
		}
		ShortcutDescriptor descriptor;
		if (shortcutForCommandId(registration.commandId, &descriptor)) {
			if (!descriptor.defaultSequence.trimmed().isEmpty()) {
				candidates << descriptor.defaultSequence;
			}
			for (const QString& alternate : descriptor.alternateSequences) {
				if (!alternate.trimmed().isEmpty()) {
					candidates << alternate;
				}
			}
		}
		if (candidates.isEmpty()) {
			continue;
		}

		QList<QKeySequence> accepted;
		QStringList acceptedText;
		QSet<QString> seenHere;
		for (const QString& candidate : candidates) {
			const QKeySequence sequence = QKeySequence::fromString(candidate, QKeySequence::PortableText);
			if (sequence.isEmpty()) {
				continue;
			}
			const QString key = normalizedSequenceKey(sequence);
			// A profile binding that repeats the documented default is the same
			// shortcut asked for twice, not a conflict with another command.
			if (seenHere.contains(key)) {
				continue;
			}
			const auto owner = owners.constFind(key);
			if (owner != owners.constEnd()) {
				// Sequences claimed by an earlier command win; a duplicate is
				// recorded and skipped rather than silently shadowing its owner.
				m_conflicts << actionsText("Shortcut %1 requested by %2 is already bound to %3; it was not installed.")
					.arg(sequence.toString(QKeySequence::PortableText), registration.commandId, owner.value());
				continue;
			}
			seenHere.insert(key);
			owners.insert(key, registration.commandId);
			accepted << sequence;
			acceptedText << sequence.toString(QKeySequence::PortableText);
		}

		if (accepted.isEmpty()) {
			continue;
		}
		action->setShortcuts(accepted);
		action->setShortcutContext(Qt::WindowShortcut);
		const QString shortcutText = acceptedText.join(QStringLiteral(", "));
		m_shortcuts.insert(registration.commandId, shortcutText);
		action->setToolTip(commandToolTip(registration.label, registration.statusTip, shortcutText));
	}
}

void StudioCommandRegistry::applyEditorProfile(const QString& editorProfileId)
{
	if (m_editorProfileId == editorProfileId) {
		return;
	}
	m_editorProfileId = editorProfileId;
	installShortcuts();
}

QString StudioCommandRegistry::editorProfileId() const
{
	return m_editorProfileId;
}

void StudioCommandRegistry::setEnabled(const QString& commandId, bool enabled)
{
	QAction* target = m_actions.value(commandId, nullptr);
	if (!target) {
		return;
	}
	// Remember the explicit decision separately from project gating so that
	// opening a project never re-enables something the shell switched off.
	target->setProperty(kExplicitlyDisabledProperty, !enabled);

	bool requiresProject = false;
	for (const StudioCommandRegistration& registration : m_registrations) {
		if (registration.commandId == commandId) {
			requiresProject = registration.requiresProject;
			break;
		}
	}
	target->setEnabled(enabled && (!requiresProject || m_projectAvailable));
}

void StudioCommandRegistry::setChecked(const QString& commandId, bool checked)
{
	QAction* target = m_actions.value(commandId, nullptr);
	if (!target || !target->isCheckable()) {
		return;
	}
	target->setChecked(checked);
}

void StudioCommandRegistry::setProjectAvailable(bool available)
{
	m_projectAvailable = available;
	for (const StudioCommandRegistration& registration : m_registrations) {
		if (!registration.requiresProject) {
			continue;
		}
		QAction* target = m_actions.value(registration.commandId, nullptr);
		if (!target) {
			continue;
		}
		target->setEnabled(available && !explicitlyDisabled(target));
	}
}

bool StudioCommandRegistry::projectAvailable() const
{
	return m_projectAvailable;
}

QStringList StudioCommandRegistry::shortcutConflicts() const
{
	return m_conflicts;
}

QString StudioCommandRegistry::shortcutForCommand(const QString& commandId) const
{
	return m_shortcuts.value(commandId);
}

void StudioCommandRegistry::populateMenuBar(QMenuBar* menuBar) const
{
	if (!menuBar) {
		return;
	}
	for (const StudioCommandGroup group : studioCommandGroupOrder()) {
		const QVector<StudioCommandRegistration> groupRegistrations = registrationsForGroup(group);
		if (groupRegistrations.isEmpty()) {
			continue;
		}
		QMenu* menu = menuBar->addMenu(studioCommandGroupMenuTitle(group));
		menu->setObjectName(QStringLiteral("menu-%1").arg(studioCommandGroupId(group)));
		bool addedAny = false;
		for (const StudioCommandRegistration& registration : groupRegistrations) {
			QAction* target = m_actions.value(registration.commandId, nullptr);
			if (!target) {
				continue;
			}
			if (registration.separatorBefore && addedAny) {
				menu->addSeparator();
			}
			menu->addAction(target);
			addedAny = true;
		}
	}
}

void StudioCommandRegistry::populateToolBar(QToolBar* toolBar) const
{
	if (!toolBar) {
		return;
	}
	bool addedAny = false;
	for (const StudioCommandRegistration& registration : m_registrations) {
		if (!registration.toolbar) {
			continue;
		}
		QAction* target = m_actions.value(registration.commandId, nullptr);
		if (!target) {
			continue;
		}
		if (registration.separatorBefore && addedAny) {
			toolBar->addSeparator();
		}
		toolBar->addAction(target);
		// Destructive commands are flagged for the stylesheet instead of being
		// given a hardcoded colour here.
		if (registration.destructive) {
			if (QWidget* button = toolBar->widgetForAction(target)) {
				button->setProperty("destructive", true);
			}
		}
		addedAny = true;
	}
}

CommandPaletteDialog::CommandPaletteDialog(const StudioCommandRegistry& registry, QWidget* parent)
	: QDialog(parent)
	, m_registry(registry)
	, m_extraEntries(commandPaletteEntries())
{
	setObjectName(QStringLiteral("commandPalette"));
	setWindowTitle(tr("Command Palette"));
	setModal(true);
	setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
	setAccessibleName(tr("Command palette"));
	setAccessibleDescription(tr("Searchable list of studio commands. Type to filter, then press Enter to run the highlighted command."));

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(8);

	m_filter = new QLineEdit(this);
	m_filter->setObjectName(QStringLiteral("commandPaletteFilter"));
	m_filter->setPlaceholderText(tr("Type a command name, category, or description"));
	m_filter->setClearButtonEnabled(true);
	m_filter->setAccessibleName(tr("Command filter"));
	m_filter->setAccessibleDescription(tr("Filters the command list. Matching is case-insensitive and characters may be skipped, so \"opk\" finds \"Open Package\"."));
	m_filter->installEventFilter(this);
	layout->addWidget(m_filter);

	m_list = new QListWidget(this);
	m_list->setObjectName(QStringLiteral("commandPaletteList"));
	m_list->setUniformItemSizes(false);
	m_list->setAlternatingRowColors(false);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setAccessibleName(tr("Matching commands"));
	m_list->setAccessibleDescription(tr("Commands that match the filter. Unavailable commands stay listed and are shown dimmed."));
	m_list->setItemDelegate(new PaletteRowDelegate(m_list));
	layout->addWidget(m_list, 1);

	m_hint = new QLabel(this);
	m_hint->setObjectName(QStringLiteral("commandPaletteHint"));
	m_hint->setWordWrap(true);
	m_hint->setAccessibleName(tr("Command palette keyboard help"));
	layout->addWidget(m_hint);

	QObject::connect(m_filter, &QLineEdit::textChanged, this, [this](const QString&) {
		applyFilter();
	});
	QObject::connect(m_filter, &QLineEdit::returnPressed, this, [this]() {
		activateCurrentRow();
	});
	QObject::connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem*) {
		activateCurrentRow();
	});
	QObject::connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
		activateCurrentRow();
	});

	rebuildRows();
	applyFilter();
}

void CommandPaletteDialog::setExtraEntries(const QVector<CommandPaletteEntry>& entries)
{
	m_extraEntries = entries;
	rebuildRows();
	applyFilter();
}

void CommandPaletteDialog::focusFilter()
{
	if (!m_filter) {
		return;
	}
	m_filter->setFocus(Qt::OtherFocusReason);
	m_filter->selectAll();
}

QString CommandPaletteDialog::selectedCommandId() const
{
	return m_selectedCommandId;
}

void CommandPaletteDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);

	if (QWidget* owner = parentWidget()) {
		const int width = std::clamp(owner->width() * 3 / 5, 420, 900);
		const int height = std::clamp(owner->height() * 3 / 5, 320, 640);
		resize(width, height);
		// Anchored near the top like other editors' command launchers, so the
		// list grows downward over the work surface rather than covering it.
		const QPoint topCentre = owner->mapToGlobal(QPoint(owner->width() / 2, 0));
		move(topCentre.x() - width / 2, topCentre.y() + std::min(96, owner->height() / 8));
	}

	m_selectedCommandId.clear();
	rebuildRows();
	applyFilter();
	focusFilter();
}

bool CommandPaletteDialog::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_filter && event->type() == QEvent::KeyPress) {
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		const int rowCount = m_list ? m_list->count() : 0;
		switch (keyEvent->key()) {
		case Qt::Key_Escape:
			reject();
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			activateCurrentRow();
			return true;
		case Qt::Key_Down:
		case Qt::Key_Up:
		case Qt::Key_PageDown:
		case Qt::Key_PageUp: {
			if (rowCount <= 0) {
				return true;
			}
			int step = 1;
			if (keyEvent->key() == Qt::Key_Up) {
				step = -1;
			} else if (keyEvent->key() == Qt::Key_PageDown) {
				step = 10;
			} else if (keyEvent->key() == Qt::Key_PageUp) {
				step = -10;
			}
			const int current = std::max(0, m_list->currentRow());
			const int next = std::clamp(current + step, 0, rowCount - 1);
			m_list->setCurrentRow(next);
			m_list->scrollToItem(m_list->currentItem());
			return true;
		}
		default:
			break;
		}
	}
	return QDialog::eventFilter(watched, event);
}

void CommandPaletteDialog::rebuildRows()
{
	m_rows.clear();

	QSet<QString> known;
	const QVector<StudioCommandRegistration> registrations = m_registry.registrations();
	for (const StudioCommandRegistration& registration : registrations) {
		const QAction* target = m_registry.action(registration.commandId);
		Row row;
		row.commandId = registration.commandId;
		row.label = registration.label;
		row.category = studioCommandGroupTitle(registration.group);
		row.summary = registration.statusTip;
		row.shortcut = m_registry.shortcutForCommand(registration.commandId);
		row.enabled = target ? target->isEnabled() : false;
		row.destructive = registration.destructive;
		m_rows.push_back(row);
		known.insert(normalizedToken(registration.commandId));
	}

	// Documented-but-unimplemented commands stay discoverable: they are listed
	// as unavailable rather than hidden, so the palette matches the registry in
	// core/studio_semantics.h.
	for (const CommandPaletteEntry& entry : m_extraEntries) {
		if (known.contains(normalizedToken(entry.commandId))) {
			continue;
		}
		Row row;
		row.commandId = entry.commandId;
		row.label = entry.label;
		row.category = entry.category;
		row.summary = entry.summary;
		row.shortcut = entry.defaultShortcut;
		row.enabled = false;
		row.destructive = entry.destructive;
		m_rows.push_back(row);
		known.insert(normalizedToken(entry.commandId));
	}
}

void CommandPaletteDialog::applyFilter()
{
	if (!m_list) {
		return;
	}

	const QString needle = m_filter ? m_filter->text().trimmed().toLower() : QString();

	struct Ranked {
		int rank = 0;
		int order = 0;
		Row row;
	};
	QVector<Ranked> ranked;
	ranked.reserve(m_rows.size());

	for (int index = 0; index < m_rows.size(); ++index) {
		const Row& row = m_rows.at(index);
		int rank = 0;
		if (!needle.isEmpty()) {
			const QString label = row.label.toLower();
			const QString haystack = QStringLiteral("%1 %2 %3").arg(label, row.category.toLower(), row.summary.toLower());
			if (label.startsWith(needle)) {
				rank = 0;
			} else if (hasWordStart(label, needle)) {
				rank = 1;
			} else if (isSubsequence(haystack, needle)) {
				rank = 2;
			} else {
				continue;
			}
		}
		ranked.push_back({rank, index, row});
	}

	std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& left, const Ranked& right) {
		if (left.rank != right.rank) {
			return left.rank < right.rank;
		}
		return left.order < right.order;
	});

	m_visibleRows.clear();
	m_visibleRows.reserve(ranked.size());
	m_list->clear();

	for (const Ranked& entry : ranked) {
		m_visibleRows.push_back(entry.row);

		QString label = entry.row.label;
		if (entry.row.destructive) {
			label = tr("%1 [destructive]").arg(label);
		}

		auto* item = new QListWidgetItem(label, m_list);
		item->setData(kCategoryRole, entry.row.category);
		item->setData(kShortcutRole, entry.row.shortcut);
		item->setData(kRowEnabledRole, entry.row.enabled);
		item->setData(kCommandIdRole, entry.row.commandId);
		item->setToolTip(entry.row.summary);
		const QString availability = entry.row.enabled ? tr("available") : tr("unavailable");
		item->setData(Qt::AccessibleTextRole,
			tr("%1, %2, %3").arg(label, entry.row.category, availability));
		item->setData(Qt::AccessibleDescriptionRole, entry.row.summary);
	}

	if (m_list->count() > 0) {
		m_list->setCurrentRow(0);
	}

	if (m_hint) {
		if (m_list->count() == 0 && !needle.isEmpty()) {
			m_hint->setText(tr("No command matches \"%1\". Escape closes the palette.").arg(m_filter ? m_filter->text().trimmed() : QString()));
		} else {
			m_hint->setText(tr("Up and Down move, Page Up and Page Down jump, Enter runs the highlighted command, Escape closes."));
		}
	}
}

void CommandPaletteDialog::activateCurrentRow()
{
	if (!m_list) {
		return;
	}
	const int row = m_list->currentRow();
	if (row < 0 || row >= m_visibleRows.size()) {
		return;
	}

	const Row& selected = m_visibleRows.at(row);
	if (!selected.enabled) {
		if (m_hint) {
			m_hint->setText(tr("\"%1\" is not available right now. Open a project or finish the blocking task, then try again.")
					.arg(selected.label));
		}
		return;
	}

	m_selectedCommandId = selected.commandId;
	Q_EMIT commandChosen(m_selectedCommandId);
	accept();
}

} // namespace vibestudio
