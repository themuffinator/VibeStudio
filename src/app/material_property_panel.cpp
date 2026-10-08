#include "app/material_property_panel.h"

#include "core/material_script.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace vibestudio {

namespace {

// A number field: Up and Down step it (Shift ten times, Ctrl a tenth), and
// each step is committed at once so the preview follows.
class NumberField final : public QLineEdit {
public:
	std::function<void()> stepped;

	using QLineEdit::QLineEdit;

protected:
	void keyPressEvent(QKeyEvent* event) override
	{
		if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
			bool ok = false;
			const double value = text().trimmed().toDouble(&ok);
			if (ok) {
				double step = 0.1;
				if (event->modifiers() & Qt::ShiftModifier) {
					step = 1.0;
				} else if (event->modifiers() & Qt::ControlModifier) {
					step = 0.01;
				}
				const double next = value + (event->key() == Qt::Key_Up ? step : -step);
				// Round away float noise: 0.30000000000000004 reads as 0.3.
				setText(QString::number(std::round(next * 10000.0) / 10000.0, 'g', 10));
				if (stepped) {
					stepped();
				}
				event->accept();
				return;
			}
		}
		QLineEdit::keyPressEvent(event);
	}
};

} // namespace

MaterialPropertyPanel::MaterialPropertyPanel(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("materialProperties"));
	setAccessibleName(tr("Node properties"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	m_title = new QLabel(this);
	m_title->setObjectName(QStringLiteral("materialPropertiesTitle"));
	QFont titleFont = m_title->font();
	titleFont.setBold(true);
	m_title->setFont(titleFont);
	m_title->setWordWrap(true);
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_subtitle = new QLabel(this);
	m_subtitle->setWordWrap(true);
	m_subtitle->setForegroundRole(QPalette::PlaceholderText);
	m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_empty = new QLabel(tr("Select a node to edit its properties."), this);
	m_empty->setWordWrap(true);
	m_empty->setAlignment(Qt::AlignCenter);
	m_empty->setForegroundRole(QPalette::PlaceholderText);
	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setAccessibleName(tr("Node properties"));
	m_formHost = new QWidget(m_scroll);
	m_form = new QFormLayout(m_formHost);
	m_form->setContentsMargins(0, 0, 0, 0);
	m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_scroll->setWidget(m_formHost);
	layout->addWidget(m_title);
	layout->addWidget(m_subtitle);
	layout->addWidget(m_empty, 1);
	layout->addWidget(m_scroll, 1);
	rebuild();
}

void MaterialPropertyPanel::setImageChoices(const QStringList& images)
{
	m_images = images;
}

QString MaterialPropertyPanel::signature(const MaterialGraphNode& node) const
{
	QStringList parts {node.id};
	for (const MaterialGraphProperty& property : node.properties) {
		parts << property.id + QLatin1Char(':') + materialGraphPropertyTypeId(property.type) + QLatin1Char(':')
				+ property.options.join(QLatin1Char('|'));
	}
	return parts.join(QLatin1Char(';'));
}

void MaterialPropertyPanel::setNode(const MaterialGraphNode* node, bool readOnly)
{
	if (!node) {
		m_hasNode = false;
		m_node = MaterialGraphNode();
		m_readOnly = readOnly;
		rebuild();
		return;
	}
	const bool sameShape = m_hasNode && readOnly == m_readOnly && signature(*node) == signature(m_node);
	m_node = *node;
	m_hasNode = true;
	m_readOnly = readOnly;
	if (sameShape) {
		m_title->setText(m_node.title);
		m_title->setAccessibleName(m_node.title);
		m_subtitle->setText(m_node.subtitle);
		m_subtitle->setVisible(!m_node.subtitle.isEmpty());
		refreshValues();
	} else {
		rebuild();
	}
}

void MaterialPropertyPanel::rebuild()
{
	while (m_form->rowCount() > 0) {
		m_form->removeRow(0);
	}
	m_editors.clear();
	m_problems.clear();
	m_title->setVisible(m_hasNode);
	m_subtitle->setVisible(m_hasNode && !m_node.subtitle.isEmpty());
	m_empty->setVisible(!m_hasNode);
	m_scroll->setVisible(m_hasNode);
	if (!m_hasNode) {
		return;
	}
	m_title->setText(m_node.title);
	m_title->setAccessibleName(m_node.title);
	m_subtitle->setText(m_node.subtitle);
	if (m_node.properties.isEmpty()) {
		auto* none = new QLabel(tr("This node has nothing to set."), m_formHost);
		none->setWordWrap(true);
		m_form->addRow(none);
		return;
	}
	for (const MaterialGraphProperty& property : m_node.properties) {
		QWidget* editor = createEditor(property);
		auto* label = new QLabel(property.label, m_formHost);
		label->setBuddy(editor);
		label->setToolTip(property.help);
		auto* problem = new QLabel(m_formHost);
		problem->setWordWrap(true);
		problem->setVisible(false);
		problem->setForegroundRole(QPalette::BrightText);
		auto* field = new QWidget(m_formHost);
		auto* fieldLayout = new QVBoxLayout(field);
		fieldLayout->setContentsMargins(0, 0, 0, 0);
		fieldLayout->setSpacing(2);
		fieldLayout->addWidget(editor);
		fieldLayout->addWidget(problem);
		m_form->addRow(label, field);
		m_editors.insert(property.id, editor);
		m_problems.insert(property.id, problem);
	}
}

QWidget* MaterialPropertyPanel::createEditor(const MaterialGraphProperty& property)
{
	const QString id = property.id;
	QWidget* editor = nullptr;
	switch (property.type) {
	case MaterialGraphPropertyType::Bool: {
		auto* box = new QCheckBox(m_formHost);
		box->setChecked(property.value == QStringLiteral("true"));
		box->setEnabled(!m_readOnly);
		connect(box, &QCheckBox::toggled, this, [this, id](bool checked) {
			commit(id, checked ? QStringLiteral("true") : QStringLiteral("false"));
		});
		editor = box;
		break;
	}
	case MaterialGraphPropertyType::Enum: {
		auto* combo = new QComboBox(m_formHost);
		combo->addItems(property.options);
		if (!property.value.isEmpty() && combo->findText(property.value) < 0) {
			combo->addItem(property.value);
		}
		combo->setCurrentText(property.value);
		combo->setEnabled(!m_readOnly);
		connect(combo, &QComboBox::activated, this, [this, id, combo](int) { commit(id, combo->currentText()); });
		editor = combo;
		break;
	}
	case MaterialGraphPropertyType::Integer: {
		auto* spin = new QSpinBox(m_formHost);
		spin->setRange(-1000000, 1000000);
		spin->setKeyboardTracking(false);
		spin->setValue(property.value.toInt());
		spin->setEnabled(!m_readOnly);
		connect(spin, &QSpinBox::valueChanged, this, [this, id](int value) { commit(id, QString::number(value)); });
		editor = spin;
		break;
	}
	case MaterialGraphPropertyType::Number: {
		auto* field = new NumberField(property.value, m_formHost);
		field->setReadOnly(m_readOnly);
		field->setToolTip(tr("Up and Down step by 0.1; Shift steps by 1 and Ctrl by 0.01."));
		const auto apply = [this, id, field]() {
			bool ok = false;
			field->text().trimmed().toDouble(&ok);
			QLabel* problem = m_problems.value(id);
			if (problem) {
				problem->setText(ok ? QString() : tr("Enter a number, such as 0.5."));
				problem->setVisible(!ok);
			}
			field->setAccessibleDescription(ok ? QString() : tr("Enter a number, such as 0.5."));
			if (ok) {
				commit(id, field->text().trimmed());
			}
		};
		field->stepped = apply;
		connect(field, &QLineEdit::editingFinished, this, [this, id, field, apply]() {
			const MaterialGraphProperty* current = m_node.property(id);
			if (!current || field->text().trimmed() != current->value) {
				apply();
			}
		});
		editor = field;
		break;
	}
	case MaterialGraphPropertyType::Expression: {
		auto* field = new QLineEdit(property.value, m_formHost);
		field->setReadOnly(m_readOnly);
		const auto check = [this, id, field]() {
			QVector<MaterialExpressionNode> nodes;
			QString error;
			const bool ok = parseMaterialExpression(field->text(), &nodes, &error) >= 0;
			if (QLabel* problem = m_problems.value(id)) {
				problem->setText(ok ? QString() : tr("Not an expression: %1").arg(error));
				problem->setVisible(!ok);
			}
			field->setAccessibleDescription(ok ? QString() : error);
			return ok;
		};
		connect(field, &QLineEdit::textEdited, this, [check]() { check(); });
		connect(field, &QLineEdit::editingFinished, this, [this, id, field, check]() {
			const MaterialGraphProperty* current = m_node.property(id);
			if ((!current || field->text().trimmed() != current->value) && check()) {
				commit(id, field->text().trimmed());
			}
		});
		editor = field;
		break;
	}
	case MaterialGraphPropertyType::Image: {
		auto* host = new QWidget(m_formHost);
		auto* row = new QHBoxLayout(host);
		row->setContentsMargins(0, 0, 0, 0);
		row->setSpacing(4);
		auto* field = new QLineEdit(property.value, host);
		field->setReadOnly(m_readOnly);
		field->setAccessibleName(property.label);
		if (!m_images.isEmpty()) {
			auto* completer = new QCompleter(m_images, field);
			completer->setCaseSensitivity(Qt::CaseInsensitive);
			completer->setFilterMode(Qt::MatchContains);
			field->setCompleter(completer);
		}
		auto* show = new QToolButton(host);
		show->setText(tr("Show"));
		show->setAccessibleName(tr("Show %1 in Textures").arg(property.label));
		show->setToolTip(tr("Show this image on the Textures page."));
		connect(show, &QToolButton::clicked, this, [this, field]() { Q_EMIT showImageRequested(field->text().trimmed()); });
		connect(field, &QLineEdit::editingFinished, this, [this, id, field]() {
			const MaterialGraphProperty* current = m_node.property(id);
			if (!current || field->text().trimmed() != current->value) {
				commit(id, field->text().trimmed());
			}
		});
		row->addWidget(field, 1);
		row->addWidget(show);
		host->setFocusProxy(field);
		editor = host;
		break;
	}
	case MaterialGraphPropertyType::ReadOnly: {
		auto* label = new QLabel(property.value.isEmpty() ? QStringLiteral("-") : property.value, m_formHost);
		label->setWordWrap(true);
		label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
		label->setFocusPolicy(Qt::TabFocus);
		editor = label;
		break;
	}
	case MaterialGraphPropertyType::Text: {
		auto* field = new QLineEdit(property.value, m_formHost);
		field->setReadOnly(m_readOnly);
		connect(field, &QLineEdit::editingFinished, this, [this, id, field]() {
			const MaterialGraphProperty* current = m_node.property(id);
			if (!current || field->text() != current->value) {
				commit(id, field->text());
			}
		});
		editor = field;
		break;
	}
	}
	editor->setObjectName(QStringLiteral("materialProperty_%1").arg(id));
	if (editor->accessibleName().isEmpty()) {
		editor->setAccessibleName(property.label);
	}
	if (!property.help.isEmpty()) {
		editor->setToolTip(editor->toolTip().isEmpty() ? property.help : property.help + QLatin1Char('\n') + editor->toolTip());
		if (editor->accessibleDescription().isEmpty()) {
			editor->setAccessibleDescription(property.help);
		}
	}
	return editor;
}

void MaterialPropertyPanel::refreshValues()
{
	for (const MaterialGraphProperty& property : m_node.properties) {
		QWidget* editor = m_editors.value(property.id);
		if (!editor || editor->hasFocus() || editor->isAncestorOf(QApplication::focusWidget())) {
			continue;
		}
		const QSignalBlocker blocker(editor);
		if (auto* box = qobject_cast<QCheckBox*>(editor)) {
			box->setChecked(property.value == QStringLiteral("true"));
		} else if (auto* combo = qobject_cast<QComboBox*>(editor)) {
			if (combo->findText(property.value) < 0) {
				combo->addItem(property.value);
			}
			combo->setCurrentText(property.value);
		} else if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
			spin->setValue(property.value.toInt());
		} else if (auto* field = qobject_cast<QLineEdit*>(editor)) {
			field->setText(property.value);
		} else if (auto* label = qobject_cast<QLabel*>(editor)) {
			label->setText(property.value.isEmpty() ? QStringLiteral("-") : property.value);
		} else if (auto* field = editor->findChild<QLineEdit*>()) {
			const QSignalBlocker inner(field);
			field->setText(property.value);
		}
		if (QLabel* problem = m_problems.value(property.id)) {
			problem->setVisible(false);
		}
	}
}

void MaterialPropertyPanel::commit(const QString& propertyId, const QString& value)
{
	if (!m_hasNode || m_readOnly) {
		return;
	}
	const MaterialGraphProperty* current = m_node.property(propertyId);
	if (current && current->value == value) {
		return;
	}
	Q_EMIT propertyEdited(m_node.id, propertyId, value);
}

void MaterialPropertyPanel::focusFirstField()
{
	for (const MaterialGraphProperty& property : m_node.properties) {
		QWidget* editor = m_editors.value(property.id);
		if (editor && editor->isEnabled() && property.type != MaterialGraphPropertyType::ReadOnly) {
			editor->setFocus(Qt::OtherFocusReason);
			return;
		}
	}
	if (!m_editors.isEmpty()) {
		if (QWidget* any = m_editors.begin().value()) {
			any->setFocus(Qt::OtherFocusReason);
		}
	}
}

QWidget* MaterialPropertyPanel::editorFor(const QString& propertyId) const
{
	return m_editors.value(propertyId);
}

void MaterialPropertyPanel::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::LanguageChange) {
		setAccessibleName(tr("Node properties"));
		m_empty->setText(tr("Select a node to edit its properties."));
	}
}

} // namespace vibestudio
