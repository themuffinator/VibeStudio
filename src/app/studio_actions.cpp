#include "app/studio_actions.h"

#include "app/studio_icons.h"
#include "app/studio_layout.h"

#include "core/editor_profiles.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QElapsedTimer>
#include <QHideEvent>
#include <QHBoxLayout>
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
#include <QProgressBar>
#include <QPushButton>
#include <QRect>
#include <QSet>
#include <QShowEvent>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace vibestudio {

namespace {

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

} // namespace

QString studioCommandToken(const QString& commandId)
{
	return normalizedToken(commandId);
}

namespace {

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
		name = QCoreApplication::translate("VibeStudioStudioActions", "%1 (%2)").arg(name, shortcut.section(QStringLiteral(", "), 0, 0));
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
// aligned at the trailing edge regardless of the label width, and so unavailable rows can be
// dimmed without being removed from the list.
class PaletteRowDelegate final : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QSize hint = QStyledItemDelegate::sizeHint(option, index);
		hint.setHeight(std::max(hint.height(), option.fontMetrics.height() + 14));
		return hint;
	}

	// Shortcuts are drawn as key caps a step smaller than the row's text.
	static QFont keyFont(const QFont& font)
	{
		QFont small = font;
		if (small.pointSizeF() > 0) {
			small.setPointSizeF(small.pointSizeF() * 0.88);
		}
		return small;
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

		// Allocate disjoint logical columns, then mirror their rectangles. Text
		// alignment is absolute so Qt does not mirror those positions a second
		// time in an RTL application. Each value is elided inside its own column.
		painter->setClipRect(content, Qt::IntersectClip);
		const bool rtl = opt.direction == Qt::RightToLeft;
		const int leading = Qt::AlignVCenter | Qt::AlignAbsolute | Qt::TextSingleLine | (rtl ? Qt::AlignRight : Qt::AlignLeft);
		const int trailing = Qt::AlignVCenter | Qt::AlignAbsolute | Qt::TextSingleLine | (rtl ? Qt::AlignLeft : Qt::AlignRight);
		const int gap = std::max(8, metrics.horizontalAdvance(QLatin1Char(' ')) * 2);
		// Key caps while they fit in a third of the row; plain text after that.
		const QFont caps = keyFont(opt.font);
		const int capsWidth = shortcut.isEmpty() ? 0 : keyCapsWidth(shortcut, caps);
		const bool drawCaps = capsWidth > 0 && capsWidth <= content.width() / 3;
		const int shortcutWidth = shortcut.isEmpty() ? 0 : (drawCaps ? capsWidth : std::min(metrics.horizontalAdvance(shortcut), content.width() / 3));
		const int textWidth = std::max(0, content.width() - (shortcutWidth ? shortcutWidth + gap : 0));
		if (shortcutWidth > 0) {
			const QRect logical(content.right() - shortcutWidth + 1, content.top(), shortcutWidth, content.height());
			const QRect visual = QStyle::visualRect(opt.direction, content, logical);
			if (drawCaps) {
				paintKeyCaps(painter, visual, shortcut, caps, opt.direction, rowEnabled, selected);
				painter->setFont(opt.font);
			} else {
				painter->setPen(secondary);
				painter->drawText(visual, trailing, metrics.elidedText(shortcut, Qt::ElideRight, shortcutWidth));
			}
		}
		const int labelWidth = std::min(metrics.horizontalAdvance(label), textWidth);
		const QRect labelRect(content.left(), content.top(), labelWidth, content.height());
		painter->setPen(primary);
		painter->drawText(QStyle::visualRect(opt.direction, content, labelRect), leading,
			metrics.elidedText(label, Qt::ElideRight, labelWidth));
		const int categoryWidth = textWidth - labelWidth - gap;
		if (!category.isEmpty() && categoryWidth > 0) {
			const QRect categoryRect(content.left() + labelWidth + gap, content.top(), categoryWidth, content.height());
			painter->setPen(secondary);
			painter->drawText(QStyle::visualRect(opt.direction, content, categoryRect), leading,
				metrics.elidedText(category, Qt::ElideRight, categoryWidth));
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

QString commandLabelWithoutMnemonic(const QString& label)
{
	QString text = label;
	text.replace(QStringLiteral("&&"), QString(QChar(0x0001)));
	text.remove(QLatin1Char('&'));
	text.replace(QChar(0x0001), QLatin1Char('&'));
	return text;
}

void bindCommandButton(QAbstractButton* button, QAction* action)
{
	if (!button || !action) { return; }
	button->setProperty("commandId", action->objectName());
	const auto sync = [button, action] {
		button->setEnabled(action->isEnabled());
		button->setStatusTip(action->statusTip());
		button->setAccessibleDescription(action->statusTip());
		button->setToolTip(action->toolTip());
		if (button->property("tipBeforeFold").isValid()) { button->setProperty("tipBeforeFold", action->toolTip()); }
	};
	sync();
	QObject::connect(action, &QAction::changed, button, sync);
	QObject::connect(action, &QObject::destroyed, button, [button] { button->setEnabled(false); });
	QObject::connect(button, &QAbstractButton::clicked, action, &QAction::trigger);
}

QString studioCommandGroupTitle(StudioCommandGroup group)
{
	switch (group) {
	case StudioCommandGroup::File:
		return QCoreApplication::translate("VibeStudioStudioActions", "File");
	case StudioCommandGroup::Edit:
		return QCoreApplication::translate("VibeStudioStudioActions", "Edit");
	case StudioCommandGroup::View:
		return QCoreApplication::translate("VibeStudioStudioActions", "View");
	case StudioCommandGroup::Project:
		return QCoreApplication::translate("VibeStudioStudioActions", "Project");
	case StudioCommandGroup::Build:
		return QCoreApplication::translate("VibeStudioStudioActions", "Build");
	case StudioCommandGroup::Tools:
		return QCoreApplication::translate("VibeStudioStudioActions", "Tools");
	case StudioCommandGroup::Help:
		return QCoreApplication::translate("VibeStudioStudioActions", "Help");
	}
	return QCoreApplication::translate("VibeStudioStudioActions", "Tools");
}

namespace {

// Menu titles keep their own translatable sources so translators can place the
// "&" mnemonic on a letter that makes sense in the target language.
QString studioCommandGroupMenuTitle(StudioCommandGroup group)
{
	switch (group) {
	case StudioCommandGroup::File:
		return QCoreApplication::translate("VibeStudioStudioActions", "&File");
	case StudioCommandGroup::Edit:
		return QCoreApplication::translate("VibeStudioStudioActions", "&Edit");
	case StudioCommandGroup::View:
		return QCoreApplication::translate("VibeStudioStudioActions", "&View");
	case StudioCommandGroup::Project:
		return QCoreApplication::translate("VibeStudioStudioActions", "&Project");
	case StudioCommandGroup::Build:
		return QCoreApplication::translate("VibeStudioStudioActions", "&Build");
	case StudioCommandGroup::Tools:
		return QCoreApplication::translate("VibeStudioStudioActions", "&Tools");
	case StudioCommandGroup::Help:
		return QCoreApplication::translate("VibeStudioStudioActions", "&Help");
	}
	return QCoreApplication::translate("VibeStudioStudioActions", "&Tools");
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

namespace {

// Two sets of surfaces a key could fire on meet when either is the whole
// window, or when one surface is, or holds, the other: Qt finds both
// shortcuts active there and runs neither.
bool scopesOverlap(const QList<QPointer<QWidget>>& left, const QList<QPointer<QWidget>>& right)
{
	const auto live = [](const QList<QPointer<QWidget>>& scopes) {
		return std::any_of(scopes.cbegin(), scopes.cend(), [](const QPointer<QWidget>& scope) {
			return !scope.isNull();
		});
	};
	if (!live(left) || !live(right)) {
		return true;
	}
	for (const QPointer<QWidget>& a : left) {
		for (const QPointer<QWidget>& b : right) {
			if (a && b && (a == b || a->isAncestorOf(b) || b->isAncestorOf(a))) {
				return true;
			}
		}
	}
	return false;
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

QHash<QString, QStringList> StudioCommandRegistry::profileOverrides() const
{
	// A selected editor profile may prefer different keys for commands the
	// shell already owns, or none where it needs a key for something else
	// (TrenchBroom's W flies the camera). Anything the profile does not
	// mention keeps the documented default from core/studio_semantics.h.
	QHash<QString, QStringList> overrides;
	EditorProfileDescriptor profile;
	if (m_editorProfileId.trimmed().isEmpty() || !editorProfileForId(m_editorProfileId, &profile)) {
		return overrides;
	}
	for (const EditorProfileBinding& binding : profile.bindings) {
		if (binding.commandId.trimmed().isEmpty() || !binding.implemented) {
			continue;
		}
		const QStringList keys = editorProfileBindingKeys(binding);
		if (keys.isEmpty() && !binding.clearsKeys) {
			continue;
		}
		overrides.insert(normalizedToken(binding.commandId), keys);
	}
	return overrides;
}

bool StudioCommandRegistry::documentedShortcut(const QString& commandId, ShortcutDescriptor* descriptor) const
{
	if (m_descriptors.isEmpty()) {
		return shortcutForCommandId(commandId, descriptor);
	}
	const auto found = m_descriptors.constFind(normalizedToken(commandId));
	if (found == m_descriptors.constEnd()) {
		return false;
	}
	if (descriptor) {
		*descriptor = found.value();
	}
	return true;
}

void StudioCommandRegistry::beginBatch()
{
	++m_batchDepth;
}

void StudioCommandRegistry::endBatch()
{
	if (m_batchDepth > 0 && --m_batchDepth == 0 && m_installPending) {
		m_installPending = false;
		installShortcuts();
	}
}

QStringList StudioCommandRegistry::builtInCandidates(const QString& commandId, const QHash<QString, QStringList>& overrides) const
{
	// A profile's keys stand in for the command's own, so the familiar keys
	// are the only ones: TrenchBroom's clip tool is C, not C and X.
	const auto override = overrides.constFind(normalizedToken(commandId));
	if (override != overrides.constEnd()) {
		return override.value();
	}
	QStringList candidates;
	ShortcutDescriptor descriptor;
	if (documentedShortcut(commandId, &descriptor)) {
		if (!descriptor.defaultSequence.trimmed().isEmpty()) {
			candidates << descriptor.defaultSequence;
		}
		for (const QString& alternate : descriptor.alternateSequences) {
			if (!alternate.trimmed().isEmpty()) {
				candidates << alternate;
			}
		}
	}
	return candidates;
}

void StudioCommandRegistry::installShortcuts()
{
	if (m_batchDepth > 0) {
		m_installPending = true;
		return;
	}
	// Shortcuts are installed for the whole registry at once, in registration
	// order, so conflict resolution does not depend on which command happened to
	// be registered last and a profile switch can rebind everything cleanly.
	// The first documented shortcut for an id wins, as a lookup would find it.
	m_descriptors.clear();
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		const QString id = normalizedToken(descriptor.commandId);
		if (!m_descriptors.contains(id)) {
			m_descriptors.insert(id, descriptor);
		}
	}
	m_conflicts.clear();
	m_shortcuts.clear();
	m_sequences.clear();
	const QHash<QString, QStringList> overrides = profileOverrides();

	// A sequence belongs to one window-wide command, or is shared by commands
	// scoped to separate surfaces, each firing only on its own.
	// `preferred` marks keys the user or the editor profile chose: a
	// built-in key they take from another command is simply theirs.
	struct Owner {
		QString commandId;
		QList<QPointer<QWidget>> scopes;
		bool preferred = false;
	};
	QHash<QString, QVector<Owner>> owners;
	auto clashWith = [&owners](const QString& key, const QList<QPointer<QWidget>>& scopes) -> Owner {
		for (const Owner& owner : owners.value(key)) {
			if (scopesOverlap(owner.scopes, scopes)) {
				return owner;
			}
		}
		return {};
	};
	// Keys the studio relies on go first, so nothing can take them; then the
	// user's own keys, so one they gave a command is taken from any command
	// whose built-in key it was rather than refused; then the editor
	// profile's, likewise; then everything else.
	QVector<const StudioCommandRegistration*> order;
	order.reserve(m_registrations.size());
	const auto pass = [this, &overrides](const StudioCommandRegistration& registration) {
		if (!shortcutRemappable(registration.commandId)) {
			return 0;
		}
		if (m_userShortcuts.contains(normalizedToken(registration.commandId))) {
			return 1;
		}
		return overrides.contains(normalizedToken(registration.commandId)) ? 2 : 3;
	};
	for (const int wanted : {0, 1, 2, 3}) {
		for (const StudioCommandRegistration& registration : m_registrations) {
			if (pass(registration) == wanted) {
				order.push_back(&registration);
			}
		}
	}
	for (const StudioCommandRegistration* registration : std::as_const(order)) {
		QAction* action = m_actions.value(registration->commandId, nullptr);
		if (!action) {
			continue;
		}
		action->setShortcuts({});
		// A command that loses its keys must not keep naming them.
		action->setToolTip(commandToolTip(registration->label, registration->statusTip, QString()));
		QList<QPointer<QWidget>> scopes;
		for (const QPointer<QWidget>& scope : m_shortcutScopes.value(normalizedToken(registration->commandId))) {
			if (scope) {
				scopes.push_back(scope);
			}
		}

		const auto user = m_userShortcuts.constFind(normalizedToken(registration->commandId));
		const bool userKeys = user != m_userShortcuts.constEnd() && shortcutRemappable(registration->commandId);
		const bool preferredKeys = userKeys || (shortcutRemappable(registration->commandId) && overrides.contains(normalizedToken(registration->commandId)));
		const QStringList candidates = userKeys ? user.value() : builtInCandidates(registration->commandId, overrides);
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
			const Owner owner = clashWith(key, scopes);
			if (!owner.commandId.isEmpty()) {
				// Sequences claimed by an earlier command win; a duplicate is
				// recorded and skipped rather than silently shadowing its owner.
				// A built-in key the user or the profile gave another command is
				// simply theirs.
				if (!owner.preferred || preferredKeys) {
					m_conflicts << QCoreApplication::translate("VibeStudioStudioActions", "Shortcut %1 requested by %2 is already bound to %3; it was not installed.")
						.arg(sequence.toString(QKeySequence::PortableText), registration->commandId, owner.commandId);
				}
				continue;
			}
			seenHere.insert(key);
			owners[key].push_back({registration->commandId, scopes, preferredKeys});
			accepted << sequence;
			acceptedText << sequence.toString(QKeySequence::PortableText);
		}

		if (accepted.isEmpty()) {
			continue;
		}
		action->setShortcuts(accepted);
		// A scoped command's key only fires while focus is inside one of its
		// surfaces. The action is added to each surface so Qt can match that
		// context; being in a menu as well does not widen it.
		for (const QPointer<QWidget>& scope : scopes) {
			if (!scope->actions().contains(action)) {
				scope->addAction(action);
			}
		}
		action->setShortcutContext(scopes.isEmpty() ? Qt::WindowShortcut : Qt::WidgetWithChildrenShortcut);
		// Qt's own list form: "; " between keys, so a chord ("Ctrl+K, Ctrl+P")
		// never reads as two keys.
		const QString shortcutText = QKeySequence::listToString(accepted, QKeySequence::PortableText);
		m_shortcuts.insert(registration->commandId, shortcutText);
		m_sequences.insert(registration->commandId, accepted);
		action->setToolTip(commandToolTip(registration->label, registration->statusTip, shortcutText));
	}
}

void StudioCommandRegistry::setUserShortcuts(const QHash<QString, QStringList>& shortcuts)
{
	m_userShortcuts.clear();
	for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it) {
		if (!it.key().trimmed().isEmpty()) {
			m_userShortcuts.insert(normalizedToken(it.key()), it.value());
		}
	}
	installShortcuts();
}

QHash<QString, QStringList> StudioCommandRegistry::userShortcuts() const
{
	QHash<QString, QStringList> shortcuts;
	for (const StudioCommandRegistration& registration : m_registrations) {
		const auto found = m_userShortcuts.constFind(normalizedToken(registration.commandId));
		if (found != m_userShortcuts.constEnd()) {
			shortcuts.insert(registration.commandId, found.value());
		}
	}
	return shortcuts;
}

bool StudioCommandRegistry::hasUserShortcut(const QString& commandId) const
{
	return m_userShortcuts.contains(normalizedToken(commandId));
}

QStringList StudioCommandRegistry::builtInShortcuts(const QString& commandId) const
{
	QStringList sequences;
	QSet<QString> seen;
	for (const QString& candidate : builtInCandidates(commandId, profileOverrides())) {
		const QKeySequence sequence = QKeySequence::fromString(candidate, QKeySequence::PortableText);
		if (!sequence.isEmpty() && !seen.contains(normalizedSequenceKey(sequence))) {
			seen.insert(normalizedSequenceKey(sequence));
			sequences << sequence.toString(QKeySequence::PortableText);
		}
	}
	return sequences;
}

QStringList StudioCommandRegistry::commandsUsingShortcut(const QString& sequenceText, const QString& commandId) const
{
	QStringList using_;
	const QKeySequence sequence = QKeySequence::fromString(sequenceText, QKeySequence::PortableText);
	if (sequence.isEmpty()) {
		return using_;
	}
	const QString key = normalizedSequenceKey(sequence);
	const QList<QPointer<QWidget>> mine = m_shortcutScopes.value(normalizedToken(commandId));
	// What is installed now is what the key would collide with.
	for (const StudioCommandRegistration& registration : m_registrations) {
		if (normalizedToken(registration.commandId) == normalizedToken(commandId)) {
			continue;
		}
		const QAction* action = m_actions.value(registration.commandId, nullptr);
		if (!action) {
			continue;
		}
		const QList<QKeySequence> installed = action->shortcuts();
		const bool uses = std::any_of(installed.cbegin(), installed.cend(), [&key](const QKeySequence& candidate) {
			return normalizedSequenceKey(candidate) == key;
		});
		if (uses && scopesOverlap(mine, m_shortcutScopes.value(normalizedToken(registration.commandId)))) {
			using_ << registration.commandId;
		}
	}
	return using_;
}

QList<QKeySequence> StudioCommandRegistry::shortcutSequences(const QString& commandId) const
{
	return m_sequences.value(commandId);
}

bool StudioCommandRegistry::shortcutRemappable(const QString& commandId) const
{
	ShortcutDescriptor descriptor;
	return !documentedShortcut(commandId, &descriptor) || descriptor.userRemappable;
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

void StudioCommandRegistry::setShortcutScopes(const QString& commandId, const QList<QWidget*>& scopes)
{
	QList<QPointer<QWidget>> guarded;
	for (QWidget* scope : scopes) {
		if (scope) {
			guarded.push_back(scope);
		}
	}
	// Keyed like the registry so the documented kebab-case ids and the shell's
	// camelCase ids name the same command.
	m_shortcutScopes.insert(normalizedToken(commandId), guarded);
	installShortcuts();
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
		QHash<QString, QMenu*> sections;
		for (const StudioCommandRegistration& registration : groupRegistrations) {
			QAction* target = m_actions.value(registration.commandId, nullptr);
			if (!target) {
				continue;
			}
			QMenu* destination = menu;
			if (!registration.menuSection.isEmpty()) {
				if (!sections.contains(registration.menuSection)) {
					sections.insert(registration.menuSection, menu->addMenu(registration.menuSection));
				}
				destination = sections.value(registration.menuSection);
			}
			if (registration.separatorBefore && !destination->actions().isEmpty()) {
				destination->addSeparator();
			}
			destination->addAction(target);
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
	// A floating panel: rounded, with a soft shadow, where the window system
	// can show one.
	m_shadowMargin = prepareFloatingPanel(this);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(12 + m_shadowMargin, 12 + m_shadowMargin, 12 + m_shadowMargin, 10 + m_shadowMargin);
	layout->setSpacing(8);

	m_filter = new QLineEdit(this);
	m_filter->setObjectName(QStringLiteral("commandPaletteFilter"));
	m_filter->setPlaceholderText(tr("Type a command name, category, or description"));
	m_filter->setClearButtonEnabled(true);
	m_filter->addAction(studioIcon(QStringLiteral("search"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	m_filter->setAccessibleName(tr("Command filter"));
	m_filter->setAccessibleDescription(tr("Filters the command list. Matching is case-insensitive and characters may be skipped, so \"opk\" finds \"Open Package\"."));
	m_filter->installEventFilter(this);
	layout->addWidget(m_filter);

	m_list = new QListWidget(this);
	m_list->setObjectName(QStringLiteral("commandPaletteList"));
	m_list->setUniformItemSizes(false);
	m_list->setAlternatingRowColors(false);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	// Rows sit straight on the panel, each command with its glyph.
	m_list->setProperty("flat", true);
	setBaseIconSize(m_list, QSize(16, 16));
	m_list->setIconSize(scaledIconSize(QSize(16, 16)));
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

void CommandPaletteDialog::setRecentCommands(const QStringList& commandIds)
{
	m_recentCommands = commandIds;
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
		resize(width + 2 * m_shadowMargin, height + 2 * m_shadowMargin);
		// Anchored near the top like other editors' command launchers, so the
		// list grows downward over the work surface rather than covering it.
		const QPoint topCentre = owner->mapToGlobal(QPoint(owner->width() / 2, 0));
		move(topCentre.x() - width / 2 - m_shadowMargin, topCentre.y() + std::min(96, owner->height() / 8) - m_shadowMargin);
	}

	m_selectedCommandId.clear();
	rebuildRows();
	applyFilter();
	focusFilter();
}

void CommandPaletteDialog::paintEvent(QPaintEvent* event)
{
	if (m_shadowMargin > 0) {
		paintFloatingPanel(this, m_shadowMargin);
		return;
	}
	QDialog::paintEvent(event);
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
		row.label = commandLabelWithoutMnemonic(registration.label);
		row.category = registration.menuSection.isEmpty() ? studioCommandGroupTitle(registration.group) : registration.menuSection;
		row.summary = registration.statusTip;
		row.shortcut = m_registry.shortcutForCommand(registration.commandId);
		row.iconName = registration.iconName;
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
		if (needle.isEmpty()) {
			// Recently run commands lead an unfiltered list, newest first.
			const qsizetype recent = m_recentCommands.indexOf(row.commandId);
			rank = recent >= 0 ? static_cast<int>(recent) - static_cast<int>(m_recentCommands.size()) : 0;
		} else {
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
		// Every row carries a glyph, so the names line up.
		const QString glyph = studioIconExists(entry.row.iconName) ? entry.row.iconName : QStringLiteral("command");
		item->setIcon(studioIcon(glyph, StudioIconTone::Muted));
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

QuickOpenDialog::QuickOpenDialog(QWidget* parent)
	: QDialog(parent)
{
	setObjectName(QStringLiteral("quickOpen"));
	setWindowTitle(tr("Go to File"));
	setModal(true);
	setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
	setAccessibleName(tr("Go to File"));
	setAccessibleDescription(tr("Searchable list of project files and package entries. Type part of a name, then press Enter to open the highlighted file where it belongs."));
	// The same floating panel as the command palette.
	m_shadowMargin = prepareFloatingPanel(this);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(12 + m_shadowMargin, 12 + m_shadowMargin, 12 + m_shadowMargin, 10 + m_shadowMargin);
	layout->setSpacing(8);

	m_filter = new QLineEdit(this);
	m_filter->setObjectName(QStringLiteral("quickOpenFilter"));
	m_filter->setPlaceholderText(tr("Type part of a file name or folder"));
	m_filter->setClearButtonEnabled(true);
	m_filter->addAction(studioIcon(QStringLiteral("search"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	m_filter->setAccessibleName(tr("File filter"));
	m_filter->setAccessibleDescription(tr("Filters the file list. Matching is case-insensitive and letters may be skipped, so \"bwall\" finds \"brick_wall.tga\"."));
	m_filter->installEventFilter(this);
	layout->addWidget(m_filter);

	m_list = new QListWidget(this);
	m_list->setObjectName(QStringLiteral("quickOpenList"));
	m_list->setProperty("flat", true);
	m_list->setUniformItemSizes(true);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setAccessibleName(tr("Matching files"));
	m_list->setAccessibleDescription(tr("Files that match the filter, with their folder and whether they come from the project or the open package."));
	m_list->setItemDelegate(new PaletteRowDelegate(m_list));
	layout->addWidget(m_list, 1);

	m_hint = new QLabel(this);
	m_hint->setObjectName(QStringLiteral("quickOpenHint"));
	m_hint->setWordWrap(true);
	m_hint->setAccessibleName(tr("Go to File keyboard help"));
	m_hint->setTextFormat(Qt::PlainText);
	layout->addWidget(m_hint);
	m_scanStatus = new QLabel(this);
	m_scanStatus->setObjectName(QStringLiteral("quickOpenStatus"));
	m_scanStatus->setTextFormat(Qt::PlainText);
	m_scanStatus->setWordWrap(true);
	m_scanStatus->setAccessibleName(tr("File discovery status"));
	m_scanStatus->hide();
	layout->addWidget(m_scanStatus);
	m_scanControls = new QWidget(this);
	auto* controls = new QHBoxLayout(m_scanControls);
	controls->setContentsMargins(0, 0, 0, 0);
	m_scanProgress = new QProgressBar;
	m_scanProgress->setRange(0, 0);
	m_scanProgress->setTextVisible(false);
	m_scanProgress->setAccessibleName(tr("Discovering files"));
	controls->addWidget(m_scanProgress, 1);
	m_cancelScan = new QPushButton(tr("Cancel Scan"));
	m_cancelScan->setObjectName(QStringLiteral("quickOpenCancel"));
	m_cancelScan->setAccessibleName(tr("Cancel file discovery"));
	m_cancelScan->setAutoDefault(false);
	controls->addWidget(m_cancelScan);
	m_refreshScan = new QPushButton(tr("Refresh"));
	m_refreshScan->setObjectName(QStringLiteral("quickOpenRefresh"));
	m_refreshScan->setAccessibleName(tr("Refresh discovered files"));
	m_refreshScan->setAutoDefault(false);
	controls->addWidget(m_refreshScan);
	m_scanControls->hide();
	layout->addWidget(m_scanControls);
	m_filterTimer = new QTimer(this);
	m_filterTimer->setSingleShot(true);
	connect(m_filterTimer, &QTimer::timeout, this, &QuickOpenDialog::filterBatch);
	m_catalog = new QuickOpenCatalog(this);
	m_catalog->progress = [this](int files, int entries) {
		m_scanStatus->setText(tr("Scanning: %1 project file(s), %2 entries checked…").arg(files).arg(entries));
		m_scanStatus->setAccessibleDescription(m_scanStatus->text());
		if (catalogProgress) { catalogProgress(files, entries); }
	};
	m_catalog->completed = [this](const QuickOpenResult& result) {
		setEntries(result.entries);
		QString summary = tr("%1 file(s) available.").arg(result.entries.size());
		if (result.state == OperationState::Cancelled) { summary = tr("Scan cancelled. %1 gathered file(s) remain available.").arg(result.entries.size()); }
		else if (result.state == OperationState::Failed) { summary = tr("Project scan failed: %1").arg(result.error); }
		else if (result.state == OperationState::Warning) { summary = tr("Partial list: %1 file(s). Filtering searches only gathered files; use a narrower project root to find more.").arg(result.entries.size()); }
		finishCatalog(result.state, summary, result.warnings);
	};
	connect(m_cancelScan, &QPushButton::clicked, this, &QuickOpenDialog::cancelCatalog);
	connect(m_refreshScan, &QPushButton::clicked, this, &QuickOpenDialog::refreshRequested);

	QObject::connect(m_filter, &QLineEdit::textChanged, this, [this](const QString&) {
		applyFilter();
	});
	QObject::connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem*) {
		activateCurrentRow();
	});
}

void QuickOpenDialog::setPurpose(const QuickOpenPurpose& purpose)
{
	m_purpose = purpose;
	if (!purpose.title.isEmpty()) {
		setWindowTitle(purpose.title);
		setAccessibleName(purpose.title);
	}
	if (m_filter && !purpose.placeholder.isEmpty()) {
		m_filter->setPlaceholderText(purpose.placeholder);
	}
	if (m_list && !purpose.listName.isEmpty()) {
		m_list->setAccessibleName(purpose.listName);
	}
	applyFilter();
}

void QuickOpenDialog::setEntries(const QVector<QuickOpenEntry>& entries, const QString& note)
{
	m_entries = entries;
	m_note = note;
	applyFilter(true);
}

void QuickOpenDialog::startCatalog(QuickOpenRequest request)
{
	m_catalog->reset();
	if (m_scanActive) { finishCatalog(OperationState::Cancelled, tr("File discovery superseded.")); }
	m_scanActive = true;
	m_scanStatus->setText(tr("Discovering files… Recent paths are being checked."));
	m_scanStatus->setAccessibleDescription(m_scanStatus->text());
	m_scanStatus->show();
	m_scanControls->show();
	m_scanProgress->show();
	m_cancelScan->setEnabled(true);
	m_refreshScan->setEnabled(false);
	setEntries(quickOpenRecentEntries(request.recentPaths));
	if (catalogStarted) { catalogStarted(); }
	m_catalog->start(std::move(request));
}

void QuickOpenDialog::cancelCatalog()
{
	if (!m_scanActive) { return; }
	m_cancelScan->setEnabled(false);
	m_scanStatus->setText(tr("Cancelling file discovery…"));
	m_scanStatus->setAccessibleDescription(m_scanStatus->text());
	m_catalog->cancel();
}

void QuickOpenDialog::finishCatalog(OperationState state, const QString& summary, const QStringList& warnings)
{
	m_scanActive = false;
	m_scanProgress->hide();
	m_cancelScan->setEnabled(false);
	m_refreshScan->setEnabled(true);
	m_scanStatus->setText(summary);
	m_scanStatus->setToolTip(warnings.join(QLatin1Char('\n')));
	m_scanStatus->setAccessibleDescription(summary + QLatin1Char('\n') + warnings.join(QLatin1Char('\n')));
	if (catalogFinished) { catalogFinished(state, summary, warnings); }
}

void QuickOpenDialog::hideEvent(QHideEvent* event)
{
	m_catalog->reset();
	if (m_scanActive) { finishCatalog(OperationState::Cancelled, tr("File discovery stopped when the picker closed.")); }
	m_filterTimer->stop();
	m_filtering = false;
	QDialog::hideEvent(event);
}

void QuickOpenDialog::focusFilter()
{
	if (m_filter) {
		m_filter->setFocus(Qt::OtherFocusReason);
		m_filter->selectAll();
	}
}

void QuickOpenDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	if (QWidget* owner = parentWidget()) {
		const int width = std::clamp(owner->width() * 3 / 5, 420, 900);
		const int height = std::clamp(owner->height() * 3 / 5, 320, 640);
		resize(width + 2 * m_shadowMargin, height + 2 * m_shadowMargin);
		const QPoint topCentre = owner->mapToGlobal(QPoint(owner->width() / 2, 0));
		move(topCentre.x() - width / 2 - m_shadowMargin, topCentre.y() + std::min(96, owner->height() / 8) - m_shadowMargin);
	}
	focusFilter();
}

void QuickOpenDialog::paintEvent(QPaintEvent* event)
{
	if (m_shadowMargin > 0) {
		paintFloatingPanel(this, m_shadowMargin);
		return;
	}
	QDialog::paintEvent(event);
}

bool QuickOpenDialog::eventFilter(QObject* watched, QEvent* event)
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
			m_list->setCurrentRow(std::clamp(std::max(0, m_list->currentRow()) + step, 0, rowCount - 1));
			m_list->scrollToItem(m_list->currentItem());
			return true;
		}
		default:
			break;
		}
	}
	return QDialog::eventFilter(watched, event);
}

void QuickOpenDialog::applyFilter(bool preserveSelection)
{
	if (!m_list) { return; }
	m_filterTimer->stop();
	m_retainedKey = preserveSelection && m_list->currentItem() ? m_list->currentItem()->data(kCommandIdRole).toString() : QString();
	m_filterNeedle = m_filter ? m_filter->text().trimmed().toLower() : QString();
	for (auto& bucket : m_ranked) { bucket.clear(); }
	m_filterCursor = 0;
	m_matchCount = 0;
	m_filtering = true;
	m_list->clear();
	m_hint->setText(tr("Matching files…"));
	// Small symbol/recent lists remain immediate. Large catalogs yield between
	// batches, including while typing; a new query retires all old matches.
	filterBatch();
}

void QuickOpenDialog::filterBatch()
{
	QElapsedTimer elapsed;
	elapsed.start();
	int processed = 0;
	while (m_filterCursor < m_entries.size() && processed++ < 512 && elapsed.elapsed() < 4) {
		const int index = m_filterCursor++;
		const QuickOpenEntry& entry = m_entries.at(index);
		int rank = 0;
		if (!m_filterNeedle.isEmpty()) {
			const QString name = entry.name.toLower();
			if (name.startsWith(m_filterNeedle)) { rank = 0; }
			else if (hasWordStart(name, m_filterNeedle)) { rank = 1; }
			else if (isSubsequence(entry.folder.toLower() + QLatin1Char('/') + name, m_filterNeedle)) { rank = 2; }
			else { continue; }
		}
		++m_matchCount;
		if (m_ranked[rank].size() < kMaximumRows) { m_ranked[rank] << index; }
	}
	if (m_filterCursor < m_entries.size()) { m_filterTimer->start(0); }
	else { finishFilter(); }
}

void QuickOpenDialog::finishFilter()
{
	m_filtering = false;
	QListWidgetItem* retained = nullptr;
	for (const auto& bucket : m_ranked) {
		for (const int index : bucket) {
			if (m_list->count() >= kMaximumRows) { break; }
			const QuickOpenEntry& entry = m_entries.at(index);
			auto* item = new QListWidgetItem(entry.name, m_list);
			item->setData(kCategoryRole, entry.folder);
			item->setData(kShortcutRole, entry.source);
			item->setData(kRowEnabledRole, true);
			item->setData(kCommandIdRole, entry.key);
			item->setToolTip(entry.folder.isEmpty() ? entry.name : QStringLiteral("%1/%2").arg(entry.folder, entry.name));
			item->setData(Qt::AccessibleTextRole, entry.folder.isEmpty()
				? tr("%1, from %2").arg(entry.name, entry.source)
				: tr("%1, in %2, from %3").arg(entry.name, entry.folder, entry.source));
			if (entry.key == m_retainedKey) { retained = item; }
		}
	}
	if (m_list->count() > 0) { m_list->setCurrentItem(retained ? retained : m_list->item(0)); }
	QStringList hint;
	const QString typed = m_filter ? m_filter->text().trimmed() : QString();
	if (m_entries.isEmpty() && !m_scanActive) {
		hint << (m_purpose.emptyHint.isEmpty() ? tr("Open a project or a package to find files in it.") : m_purpose.emptyHint);
	} else if (!m_matchCount && !m_entries.isEmpty()) {
		hint << (m_purpose.noMatchHint.isEmpty() ? tr("No file matches \"%1\".").arg(typed) : m_purpose.noMatchHint.arg(typed));
	} else if (m_matchCount > m_list->count()) {
		hint << tr("Showing the best %1 of %2 matches; type more to narrow them.").arg(m_list->count()).arg(m_matchCount);
	}
	if (!m_note.isEmpty()) { hint << m_note; }
	hint << (m_purpose.keyHelp.isEmpty() ? tr("Enter opens the highlighted file where it belongs; Escape closes.") : m_purpose.keyHelp);
	m_hint->setText(hint.join(QLatin1Char(' ')));
}

void QuickOpenDialog::activateCurrentRow()
{
	QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
	if (!item) {
		return;
	}
	const QString key = item->data(kCommandIdRole).toString();
	accept();
	Q_EMIT entryChosen(key);
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
