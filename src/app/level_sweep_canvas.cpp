#include "app/level_sweep_canvas.h"
#include <QAccessible>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace vibestudio
{
LevelSweepCanvas::LevelSweepCanvas(QWidget* view, LevelSweepGizmo* gizmo, std::function<LevelComponentGizmoFrame()> frame,
                                   bool highContrast)
    : QWidget(view), m_view(view), m_gizmo(gizmo), m_frame(std::move(frame)), m_highContrast(highContrast)
{
	setObjectName(QStringLiteral("levelSweepCanvas"));
	setAccessibleName(tr("Sweep destination controls"));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAutoFillBackground(false);
	setFocusPolicy(Qt::NoFocus);
	setGeometry(view->rect());
	view->installEventFilter(this);
	show();
	raise();
	m_originalDescription = view->accessibleDescription();
	m_keys = {{Command::Apply, {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)}},
	          {Command::Cancel, {QKeySequence(Qt::Key_Escape)}},
	          {Command::DecreaseScale, {QKeySequence(Qt::Key_BracketLeft)}},
	          {Command::IncreaseScale, {QKeySequence(Qt::Key_BracketRight)}}};
	connect(gizmo, &LevelSweepGizmo::changed, this, [this] {
		if (!m_gizmo) {
			return;
		}
		setAccessibleDescription(m_gizmo->summary());
		if (m_view) {
			m_lastDescription = m_gizmo->summary();
			m_view->setAccessibleDescription(m_lastDescription);
			QAccessibleEvent viewEvent(m_view, QAccessible::DescriptionChanged);
			QAccessible::updateAccessibility(&viewEvent);
		}
		QAccessibleEvent event(this, QAccessible::DescriptionChanged);
		QAccessible::updateAccessibility(&event);
		update();
	});
	connect(gizmo, &QObject::destroyed, this, &LevelSweepCanvas::stop);
}
void LevelSweepCanvas::stop()
{
	if (m_view) {
		m_view->removeEventFilter(this);
		if (m_view->accessibleDescription() == m_lastDescription) {
			m_view->setAccessibleDescription(m_originalDescription);
			QAccessibleEvent event(m_view, QAccessible::DescriptionChanged);
			QAccessible::updateAccessibility(&event);
		}
	}
	hide();
	deleteLater();
}
void LevelSweepCanvas::paintEvent(QPaintEvent*)
{
	if (m_gizmo && m_view) {
		QPainter painter(this);
		m_gizmo->paint(painter, m_frame(), m_highContrast);
	}
}
void LevelSweepCanvas::setCommandKeys(Command command, const QList<QKeySequence>& keys)
{
	m_keys[command] = keys;
}
LevelSweepCanvas::Command LevelSweepCanvas::commandForKey(QKeyCombination combination) const
{
	const QKeySequence sequence(combination);
	for (auto it = m_keys.cbegin(); it != m_keys.cend(); ++it) {
		if (it.value().contains(sequence)) {
			return it.key();
		}
	}
	return Command::None;
}
void LevelSweepCanvas::executeCommand(Command command)
{
	if (!m_gizmo) {
		return;
	}
	if (command == Command::Cancel) {
		if (!m_gizmo->cancel()) {
			Q_EMIT leaveRequested();
		}
		m_ownedDrag = false;
	} else if (command == Command::Apply) {
		if (!m_gizmo->dragging()) {
			Q_EMIT applyRequested();
		}
	} else if (command == Command::DecreaseScale || command == Command::IncreaseScale) {
		m_gizmo->scaleStep(command == Command::DecreaseScale ? -1 : 1);
	}
}
bool LevelSweepCanvas::eventFilter(QObject* watched, QEvent* event)
{
	if (!m_view || !m_gizmo || watched != m_view) {
		return false;
	}
	if (event->type() == QEvent::Resize) {
		setGeometry(m_view->rect());
		raise();
		update();
	}
	if (event->type() == QEvent::Show) {
		update();
	}
	if (event->type() == QEvent::Hide || event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate) {
		if (m_ownedDrag) {
			m_ownedDrag = false;
			m_gizmo->finish(false);
		}
	}
	if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
		auto* key = static_cast<QKeyEvent*>(event);
		const auto command = commandForKey(key->keyCombination());
		if (command != Command::None) {
			event->accept();
			if (event->type() == QEvent::KeyPress) {
				executeCommand(command);
			}
			return true;
		}
		if (m_ownedDrag && levelComponentModifierKey(key->key())) {
			if (event->type() != QEvent::ShortcutOverride) {
				m_gizmo->updateDrag(m_lastPoint,
				                    levelComponentModifierState(key->key(), key->modifiers(), event->type() == QEvent::KeyPress));
			}
			event->accept();
			return true;
		}
	}
	if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
		auto* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() == Qt::LeftButton) {
			m_view->setFocus(Qt::MouseFocusReason);
			m_lastPoint = mouse->position();
			m_ownedDrag = m_gizmo->begin(mouse->position(), m_frame(), mouse->modifiers(), font());
			event->accept();
			return true;
		}
		if (m_ownedDrag) {
			m_ownedDrag = false;
			m_gizmo->finish(false);
		}
	}
	if (event->type() == QEvent::MouseMove && m_ownedDrag) {
		auto* mouse = static_cast<QMouseEvent*>(event);
		m_lastPoint = mouse->position();
		if (mouse->buttons() != Qt::LeftButton) {
			m_ownedDrag = false;
			m_gizmo->finish(false);
		} else {
			m_gizmo->updateDrag(mouse->position(), mouse->modifiers());
		}
		event->accept();
		return true;
	}
	if (event->type() == QEvent::MouseButtonRelease) {
		auto* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() == Qt::LeftButton) {
			if (m_ownedDrag) {
				m_gizmo->updateDrag(mouse->position(), mouse->modifiers());
				m_ownedDrag = false;
				m_gizmo->finish(true);
			}
			event->accept();
			return true;
		}
	}
	if (event->type() == QEvent::Wheel && m_ownedDrag) {
		m_ownedDrag = false;
		m_gizmo->finish(false);
	}
	return false;
}
} // namespace vibestudio
