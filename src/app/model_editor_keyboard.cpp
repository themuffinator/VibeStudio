// Keyboard focus in the Mesh Editor: Tab keys that belong to the view only
// while it has focus, Ctrl+Tab and Ctrl+Shift+Tab (Control+Tab on macOS)
// between panels in every profile, tool bars as single Tab stops with arrow
// keys between their buttons, focus into and out of Adjust Last Operation
// with its settings keeping the keys they use, and key hints that follow
// the profile's keys.
//
// WCAG 2.1 success criterion 2.1.2 (No Keyboard Trap) asks that focus can
// always leave a component by keyboard; Blender's edit-mode Tab stays a view
// key without taking that away.
#include "app/model_editor_tools.h"

#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_sidebar.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QPointer>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <functional>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTools)
};

QString plain(QString text)
{
	return text.remove(QLatin1Char('&'));
}

// What Tab would stop on: the same test QWidget's own focus chain uses.
bool takesTabFocus(const QWidget *widget)
{
	return widget && (widget->focusPolicy() & Qt::TabFocus) == Qt::TabFocus && !widget->focusProxy() && widget->isEnabled() &&
		   widget->isVisible();
}

// The first control in Tab order that `accept` allows, starting after `from`.
QWidget *firstInTabOrder(QWidget *from, const std::function<bool(QWidget *)> &accept)
{
	for (QWidget *widget = from->nextInFocusChain(); widget && widget != from; widget = widget->nextInFocusChain())
	{
		if (takesTabFocus(widget) && accept(widget))
			return widget;
	}
	return takesTabFocus(from) && accept(from) ? from : nullptr;
}

QString nativeKeys(const QList<QKeySequence> &keys)
{
	QStringList text;
	for (const auto &key : keys)
		text << key.toString(QKeySequence::NativeText);
	return text.join(QStringLiteral(", "));
}

QString nativeKeys(const QStringList &portable)
{
	QList<QKeySequence> keys;
	for (const auto &key : portable)
		keys << QKeySequence::fromString(key, QKeySequence::PortableText);
	return nativeKeys(keys);
}

QList<QToolButton *> toolBarButtons(QToolBar *bar)
{
	QList<QToolButton *> buttons;
	for (auto *action : bar->actions())
	{
		if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action)))
			buttons << button;
	}
	return buttons;
}

// Hidden buttons (an invisible action, or past the end of a short bar) and
// disabled ones cannot hold the tool bar's Tab stop.
bool usableButton(const QWidget *button)
{
	return button->isEnabled() && !button->isHidden();
}

// Whether a focused control keeps a key from the view's commands. A setting
// keeps every key without Ctrl, Alt or Meta, as a text field would; a tool bar
// button keeps the keys that press it and move between buttons. Tab and the
// function keys stay with the editor.
bool claimsKey(const QKeyEvent *key, bool field)
{
	if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
		return false;
	const int code = key->key();
	if (code == Qt::Key_Tab || code == Qt::Key_Backtab || (code >= Qt::Key_F1 && code <= Qt::Key_F35))
		return false;
	if (field)
		return true;
	if (key->modifiers() & Qt::ShiftModifier)
		return false;
	switch (code)
	{
	case Qt::Key_Space:
	case Qt::Key_Return:
	case Qt::Key_Enter:
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down:
	case Qt::Key_Home:
	case Qt::Key_End:
		return true;
	default:
		return false;
	}
}
} // namespace

void ModelEditorTools::buildFocusActions()
{
	// Not profile commands: every profile keeps these keys, so they are not
	// in the controls table a user can rebind.
	for (const bool forward : {true, false})
	{
		auto *action = new QAction(forward ? Text::tr("Next Panel") : Text::tr("Previous Panel"), m_editor);
		action->setObjectName(forward ? QStringLiteral("meshFocusNextPanel") : QStringLiteral("meshFocusPreviousPanel"));
		QList<QKeySequence> keys;
		for (const auto &key : modelEditorFocusKeys(forward))
			keys << QKeySequence::fromString(key, QKeySequence::PortableText);
		action->setShortcuts(keys);
		action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		// Moving focus never edits, so it works while a worker runs too:
		// that is when a keyboard user needs to reach Cancel.
		connect(action, &QAction::triggered, this, [this, forward] { focusPanel(forward); });
		m_editor->addAction(action);
		m_actions.insert(action->objectName(), action);
	}
}

void ModelEditorTools::scopeShortcuts(QAction *action)
{
	if (!action)
		return;
	const auto keys = action->shortcuts();
	const bool tab = std::any_of(keys.cbegin(), keys.cend(),
								 [](const QKeySequence &key) { return isModelEditorTabKey(key.toString(QKeySequence::PortableText)); });
	if (tab && !m_viewport->actions().contains(action))
		m_viewport->addAction(action);
	// A widget shortcut matches only the widget that has focus, so the shelf,
	// the Adjust Last Operation fields and the panels keep Tab for focus.
	action->setShortcutContext(tab ? Qt::WidgetShortcut : Qt::WidgetWithChildrenShortcut);
}

QList<QWidget *> ModelEditorTools::focusPanels() const
{
	QList<QWidget *> panels;
	const auto add = [&panels](QWidget *panel)
	{
		if (panel && !panels.contains(panel))
			panels << panel;
	};
	// The document commands are the Model menu's, reached from the menu bar.
	add(m_editor->m_leadingSidebar);
	// The editing header (with the shading and view controls) sits above the
	// views. Material loading is in the status strip, reached with the rest of
	// the editor after the panels.
	add(m_header);
	// The shelf shares the 3D tab with the views but is a panel of its own,
	// since Tab in a view can be a command.
	add(m_shelf);
	add(m_editor->findChild<QTabWidget *>(QStringLiteral("meshViewTabs")));
	// The animation transport below the views.
	add(m_editor->m_frame ? m_editor->m_frame->parentWidget() : nullptr);
	add(m_editor->m_trailingSidebar);
	return panels;
}

void ModelEditorTools::focusPanel(bool forward)
{
	if (m_shelf)
		refreshToolBarStop(m_shelf);
	const auto panels = focusPanels();
	const int count = int(panels.size()) + 1;
	const auto panelOf = [&panels, count](const QWidget *widget)
	{
		for (int index = 0; index < panels.size(); ++index)
		{
			if (panels[index] == widget || panels[index]->isAncestorOf(widget))
				return index;
		}
		return count - 1;
	};
	const auto entry = [&](int index) -> QWidget *
	{
		if (index == count - 1)
			return firstInTabOrder(m_editor, [&](QWidget *widget) { return m_editor->isAncestorOf(widget) && panelOf(widget) == count - 1; });
		QWidget *panel = panels[index];
		// The views: the active view first, else the open tab's first control.
		if (auto *tabs = qobject_cast<QTabWidget *>(panel))
		{
			if (tabs->currentWidget() == m_viewArea && takesTabFocus(m_viewport))
				return m_viewport;
			if (auto *page = tabs->currentWidget())
			{
				if (auto *first = firstInTabOrder(page, [page](QWidget *widget) { return page->isAncestorOf(widget); }))
					return first;
			}
		}
		return firstInTabOrder(panel, [panel](QWidget *widget) { return panel->isAncestorOf(widget); });
	};
	QWidget *focus = QApplication::focusWidget();
	const int current = focus && (focus == m_editor || m_editor->isAncestorOf(focus)) ? panelOf(focus) : -1;
	for (int step = 1; step <= count; ++step)
	{
		const int index = current < 0 ? (forward ? step - 1 : count - step) : ((current + (forward ? step : -step)) % count + count) % count;
		if (auto *target = entry(index))
		{
			target->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
			return;
		}
	}
}

void ModelEditorTools::focusLastOperation()
{
	if (!m_lastPanel || !m_lastPanel->isVisible() || !m_lastFields)
		return;
	if (auto *field = firstInTabOrder(m_lastFields, [this](QWidget *widget) { return m_lastFields->isAncestorOf(widget); }))
		field->setFocus(Qt::ShortcutFocusReason);
}

void ModelEditorTools::makeToolBarKeyboardReachable(QToolBar *bar)
{
	if (!bar)
		return;
	for (auto *action : bar->actions())
	{
		if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action)))
			button->installEventFilter(this);
		// A button that is hidden or disabled hands the Tab stop on.
		connect(action, &QAction::changed, this, [this, bar = QPointer<QToolBar>(bar)] { if (bar) refreshToolBarStop(bar); });
	}
	refreshToolBarStop(bar);
}

void ModelEditorTools::refreshToolBarStop(QToolBar *bar, QToolButton *preferred)
{
	const auto buttons = toolBarButtons(bar);
	// The button focused last while it can still take focus, else the first
	// one that can; a click never moves keyboard focus off the view.
	QToolButton *stop = preferred && usableButton(preferred) ? preferred : nullptr;
	for (auto *button : buttons)
	{
		if (!stop && (button->focusPolicy() & Qt::TabFocus) && usableButton(button))
			stop = button;
	}
	for (auto *button : buttons)
	{
		if (!stop && usableButton(button))
			stop = button;
	}
	for (auto *button : buttons)
		button->setFocusPolicy(button == stop ? Qt::TabFocus : Qt::NoFocus);
}

bool ModelEditorTools::handleControlKey(QObject *watched, QEvent *event)
{
	auto *widget = qobject_cast<QWidget *>(watched);
	if (!widget)
		return false;
	const auto type = event->type();
	// Keys a setting did not use stop at the panel instead of reaching the
	// view behind it, where an arrow would orbit and a letter run a command.
	if (watched == m_lastPanel)
	{
		if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
			return false;
		event->accept();
		return true;
	}
	const bool field = m_lastFields && m_lastFields->isAncestorOf(widget);
	auto *button = qobject_cast<QToolButton *>(widget);
	auto *bar = button ? qobject_cast<QToolBar *>(button->parentWidget()) : nullptr;
	const bool barButton = bar && bar == m_shelf;
	if (!field && !barButton)
		return false;
	if (type == QEvent::FocusIn && barButton)
	{
		refreshToolBarStop(bar, button);
		return false;
	}
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
		return false;
	auto *key = static_cast<QKeyEvent *>(event);
	if (type == QEvent::ShortcutOverride)
	{
		// The profile's commands listen anywhere in the modelling area, so a
		// key the focused control uses itself has to be claimed first: Space
		// toggles a check box rather than playing (Blender), letters search a
		// choice and arrows step a value. Keys with Ctrl, Alt or Meta, Tab
		// and the function keys stay commands (Ctrl+Tab, F9, Ctrl+Z).
		if (!claimsKey(key, field))
			return false;
		event->accept();
		return true;
	}
	if (field)
	{
		// Esc in Adjust Last Operation goes back to the view, which keeps the
		// panel; F9 hides it.
		if (key->key() != Qt::Key_Escape)
			return false;
		m_viewport->setFocus(Qt::ShortcutFocusReason);
		return true;
	}
	if (key->modifiers() & ~Qt::KeypadModifier)
		return false;
	if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
	{
		if (button->menu() || (button->defaultAction() && button->defaultAction()->menu()))
			button->showMenu();
		else
			button->click();
		return true;
	}
	const auto buttons = [&]
	{
		QList<QToolButton *> usable;
		for (auto *candidate : toolBarButtons(bar))
		{
			if (usableButton(candidate))
				usable << candidate;
		}
		return usable;
	}();
	const int index = int(buttons.indexOf(button));
	if (index < 0 || buttons.isEmpty())
		return false;
	// Left and Right follow the reading direction of a horizontal tool bar.
	const bool mirrored = bar->orientation() == Qt::Horizontal && bar->layoutDirection() == Qt::RightToLeft;
	int target = index;
	switch (key->key())
	{
	case Qt::Key_Up:
		target = index - 1;
		break;
	case Qt::Key_Down:
		target = index + 1;
		break;
	case Qt::Key_Left:
		target = mirrored ? index + 1 : index - 1;
		break;
	case Qt::Key_Right:
		target = mirrored ? index - 1 : index + 1;
		break;
	case Qt::Key_Home:
		target = 0;
		break;
	case Qt::Key_End:
		target = int(buttons.size()) - 1;
		break;
	default:
		return false;
	}
	target = (target % int(buttons.size()) + int(buttons.size())) % int(buttons.size());
	buttons[target]->setFocus(Qt::TabFocusReason);
	return true;
}

QString ModelEditorTools::commandKeys(const QString &command) const
{
	const auto *action = m_actions.value(command);
	return action ? nativeKeys(action->shortcuts()) : QString();
}

QString ModelEditorTools::keyboardHelp() const
{
	const QString panels = Text::tr("%1 and %2 move keyboard focus to the next or previous panel, even where Tab is a command key in this view.")
							   .arg(nativeKeys(modelEditorFocusKeys(true)), nativeKeys(modelEditorFocusKeys(false)));
	// The profile's own key for Adjust Last Operation, where it has one.
	const QString keys = commandKeys(QStringLiteral("meshAdjustLast"));
	const QString last = keys.isEmpty() ? Text::tr("View > Adjust Last Operation moves focus to its settings; Esc there returns here.")
										: Text::tr("%1 moves focus to Adjust Last Operation; Esc there returns here.").arg(keys);
	return panels + QLatin1Char(' ') + last;
}

void ModelEditorTools::refreshKeyHints()
{
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(m_controls.profileId, &profile));
	const auto tip = [](const QString &name, const QString &keys)
	{ return keys.isEmpty() ? name : Text::tr("%1 (%2)", "a command and its keys").arg(name, keys); };
	// Accessible names stay free of keys; the description says which keys the
	// current profile gives the command, or that it has none.
	for (const auto &hint : std::as_const(m_keyHints))
	{
		if (!hint.button)
			continue;
		const auto *action = m_actions.value(hint.command);
		const QString keys = action ? nativeKeys(action->shortcuts()) : QString();
		const QString keyText = keys.isEmpty() ? Text::tr("The %1 controls give this no key.").arg(profile.displayName) : Text::tr("Keyboard: %1.").arg(keys);
		hint.button->setAccessibleName(hint.name);
		hint.button->setToolTip(hint.detail.isEmpty() ? tip(hint.name, keys) : tip(hint.name, keys) + QLatin1Char('\n') + hint.detail);
		hint.button->setAccessibleDescription(hint.detail.isEmpty() ? keyText : keyText + QLatin1Char(' ') + hint.detail);
	}
	// The shelf and the search button show their command's current keys.
	auto entries = m_shelf ? m_shelf->actions() : QList<QAction *>{};
	entries << m_actions.value(QStringLiteral("meshOperatorSearch"));
	for (auto *entry : entries)
	{
		if (entry && !entry->isSeparator())
			entry->setToolTip(tip(plain(entry->text()), nativeKeys(entry->shortcuts())));
	}
	// The empty Adjust Last Operation panel says how to reach it.
	if (!m_hasLast)
		refreshLastPanel();
}
} // namespace vibestudio
