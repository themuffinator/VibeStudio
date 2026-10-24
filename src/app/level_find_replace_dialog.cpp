#include "app/level_find_replace_dialog.h"

#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace vibestudio {

namespace {
QComboBox* textureField(QWidget* parent, const QString& objectName, const QString& accessibleName)
{
	auto* field = new QComboBox(parent);
	field->setObjectName(objectName);
	field->setEditable(true);
	field->setInsertPolicy(QComboBox::NoInsert);
	field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	field->setMinimumContentsLength(18);
	field->setAccessibleName(accessibleName);
	if (field->completer()) {
		field->completer()->setCaseSensitivity(Qt::CaseInsensitive);
		field->completer()->setFilterMode(Qt::MatchContains);
	}
	return field;
}
} // namespace

LevelFindReplaceDialog::LevelFindReplaceDialog(QWidget* parent)
	: QDialog(parent)
{
	setObjectName(QStringLiteral("replaceTextureDialog"));
	setWindowTitle(tr("Find and Replace Textures"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Find the faces and patches that use a texture, or replace it, across the map, the selected objects or the picked faces. "
		"Picking a texture on the Textures tab fills the field that last had focus."));
	setModal(false);
	auto* layout = new QVBoxLayout(this);
	auto* form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_find = textureField(this, QStringLiteral("replaceTextureFrom"), tr("Texture to find"));
	m_find->setToolTip(tr("The texture to find or replace. Case does not matter."));
	m_replace = textureField(this, QStringLiteral("replaceTextureTo"), tr("Texture to use instead"));
	m_replace->setToolTip(tr("Leave empty to find only."));
	if (m_replace->lineEdit()) { m_replace->lineEdit()->setPlaceholderText(tr("Empty: find only")); }
	m_scope = new QComboBox(this);
	m_scope->setObjectName(QStringLiteral("replaceTextureScope"));
	m_scope->setAccessibleName(tr("Where to find and replace"));
	m_scope->addItem(tr("Whole map"), static_cast<int>(Scope::Map));
	m_scope->addItem(tr("Selected objects"), static_cast<int>(Scope::Selection));
	m_scope->addItem(tr("Picked faces"), static_cast<int>(Scope::Picks));
	m_scope->setToolTip(tr("Hidden and locked objects, and anything outside an open group, are always left alone."));
	auto* findLabel = new QLabel(tr("Fin&d"), this);
	findLabel->setBuddy(m_find);
	auto* replaceLabel = new QLabel(tr("Re&place with"), this);
	replaceLabel->setBuddy(m_replace);
	auto* scopeLabel = new QLabel(tr("&Search in"), this);
	scopeLabel->setBuddy(m_scope);
	form->addRow(findLabel, m_find);
	form->addRow(replaceLabel, m_replace);
	form->addRow(scopeLabel, m_scope);
	layout->addLayout(form);
	// Which field a pick fills, said in words rather than by a highlight.
	m_pickTarget = new QLabel(this);
	m_pickTarget->setObjectName(QStringLiteral("replaceTexturePickTarget"));
	m_pickTarget->setWordWrap(true);
	m_pickTarget->setAccessibleName(tr("Field that texture picks fill"));
	layout->addWidget(m_pickTarget);
	m_preview = new QLabel(this);
	m_preview->setObjectName(QStringLiteral("replaceTextureCount"));
	m_preview->setWordWrap(true);
	m_preview->setTextFormat(Qt::PlainText);
	m_preview->setAccessibleName(tr("What Find or Replace will do"));
	layout->addWidget(m_preview);
	auto* buttons = new QDialogButtonBox(this);
	m_findButton = buttons->addButton(tr("&Find"), QDialogButtonBox::ActionRole);
	m_findButton->setObjectName(QStringLiteral("replaceTextureFind"));
	m_findButton->setToolTip(tr("Pick the faces and patches that use the texture, adding them to the current picks."));
	m_findButton->setAccessibleDescription(m_findButton->toolTip());
	m_replaceButton = buttons->addButton(tr("&Replace"), QDialogButtonBox::ApplyRole);
	m_replaceButton->setObjectName(QStringLiteral("replaceTextureApply"));
	m_replaceButton->setToolTip(tr("Replace the texture in the chosen scope as one undo step. The window stays open."));
	m_replaceButton->setAccessibleDescription(m_replaceButton->toolTip());
	auto* close = buttons->addButton(QDialogButtonBox::Close);
	close->setObjectName(QStringLiteral("replaceTextureClose"));
	layout->addWidget(buttons);
	connect(m_findButton, &QPushButton::clicked, this, &LevelFindReplaceDialog::findRequested);
	connect(m_replaceButton, &QPushButton::clicked, this, &LevelFindReplaceDialog::replaceRequested);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_find, &QComboBox::editTextChanged, this, &LevelFindReplaceDialog::inputsChanged);
	connect(m_replace, &QComboBox::editTextChanged, this, [this] { refreshDefaultButton(); emit inputsChanged(); });
	connect(m_scope, &QComboBox::currentIndexChanged, this, &LevelFindReplaceDialog::inputsChanged);
	// NRC tracks the field that last took focus; a pick goes there.
	connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
		if (!now) { return; }
		if (now == m_find || m_find->isAncestorOf(now)) { setReplaceActive(false); }
		else if (now == m_replace || m_replace->isAncestorOf(now)) { setReplaceActive(true); }
	});
	setReplaceActive(false);
	setPreview(QString(), false, false);
	refreshDefaultButton();
}

void LevelFindReplaceDialog::setTextureNames(const QStringList& names)
{
	for (QComboBox* field : {m_find, m_replace}) {
		const QSignalBlocker blocker(field);
		const QString text = field->currentText();
		field->clear();
		field->addItems(names);
		field->setEditText(text);
	}
}

QString LevelFindReplaceDialog::findText() const { return m_find->currentText(); }
QString LevelFindReplaceDialog::replaceText() const { return m_replace->currentText(); }

void LevelFindReplaceDialog::setFindText(const QString& text)
{
	if (m_find->currentText() != text) { m_find->setEditText(text); }
}

void LevelFindReplaceDialog::setReplaceText(const QString& text)
{
	if (m_replace->currentText() != text) { m_replace->setEditText(text); }
}

LevelFindReplaceDialog::Scope LevelFindReplaceDialog::scope() const
{
	return static_cast<Scope>(m_scope->currentData().toInt());
}

void LevelFindReplaceDialog::setScope(Scope scope)
{
	const int index = m_scope->findData(static_cast<int>(scope));
	if (index >= 0) { m_scope->setCurrentIndex(index); }
}

void LevelFindReplaceDialog::setScopeAvailable(Scope scope, bool available, const QString& reason)
{
	auto* model = qobject_cast<QStandardItemModel*>(m_scope->model());
	const int index = m_scope->findData(static_cast<int>(scope));
	if (!model || index < 0) { return; }
	QStandardItem* item = model->item(index);
	if (item->isEnabled() != available) { item->setEnabled(available); }
	const QString tip = available ? QString() : reason;
	if (item->toolTip() != tip) { item->setToolTip(tip); }
}

void LevelFindReplaceDialog::setPreview(const QString& text, bool canFind, bool canReplace)
{
	if (m_preview->text() != text) {
		m_preview->setText(text);
		m_preview->setAccessibleDescription(text);
		QAccessibleEvent changed(m_preview, QAccessible::DescriptionChanged);
		QAccessible::updateAccessibility(&changed);
	}
	m_findButton->setEnabled(canFind);
	m_replaceButton->setEnabled(canReplace);
}

QString LevelFindReplaceDialog::previewText() const { return m_preview->text(); }

bool LevelFindReplaceDialog::fillFocusedField(const QString& name)
{
	(m_replaceActive ? m_replace : m_find)->setEditText(name);
	return m_replaceActive;
}

void LevelFindReplaceDialog::setReplaceActive(bool active)
{
	m_replaceActive = active;
	const QString text = active ? tr("Picking a texture fills Replace with.") : tr("Picking a texture fills Find.");
	if (m_pickTarget->text() != text) {
		m_pickTarget->setText(text);
		m_pickTarget->setAccessibleDescription(text);
		QAccessibleEvent changed(m_pickTarget, QAccessible::DescriptionChanged);
		QAccessible::updateAccessibility(&changed);
	}
}

void LevelFindReplaceDialog::refreshDefaultButton()
{
	// Enter replaces once a replacement is named; until then it finds.
	const bool replacing = !m_replace->currentText().trimmed().isEmpty();
	m_replaceButton->setDefault(replacing);
	m_findButton->setDefault(!replacing);
}

} // namespace vibestudio
