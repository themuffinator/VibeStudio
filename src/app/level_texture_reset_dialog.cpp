#include "app/level_texture_reset_dialog.h"

#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
LevelTextureResetDialog::LevelTextureResetDialog(int selectionFaces, int selectionPatches, const QString &texture, double defaultScale, QWidget *parent)
	: QDialog(parent), m_selectionFaces(selectionFaces), m_selectionPatches(selectionPatches)
{
	setObjectName(QStringLiteral("levelTextureResetDialog"));
	setWindowTitle(tr("Reset Textures"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Set absolute texture values, or a new texture name, on the selection or the whole map."));
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->addLayout(form);

	m_scope = new QComboBox(this);
	m_scope->setObjectName(QStringLiteral("textureResetScope"));
	m_scope->addItems({tr("Selection"), tr("Whole map")});
	m_scope->setCurrentIndex(selectionFaces + selectionPatches > 0 ? 0 : 1);
	m_scope->setAccessibleName(tr("Apply to"));
	m_scope->setToolTip(tr("Selection: the Surfaces target, or the picked faces. Whole map: every visible, unlocked surface in the open group; what the views hide stays as it is."));
	m_scope->setAccessibleDescription(m_scope->toolTip());
	auto *scopeLabel = new QLabel(tr("Apply &to"), this);
	scopeLabel->setBuddy(m_scope);
	form->addRow(scopeLabel, m_scope);

	// A ticked box beside its field, as bobToolz's checkable groups.
	const auto nameRow = [&](QCheckBox **box, QLineEdit **edit, const QString &label, const QString &name, const QString &field, const QString &tip) {
		*box = new QCheckBox(label, this);
		(*box)->setObjectName(name);
		(*box)->setToolTip(tip);
		(*box)->setAccessibleDescription(tip);
		*edit = new QLineEdit(texture, this);
		(*edit)->setObjectName(name + QStringLiteral("Name"));
		(*edit)->setAccessibleName(field);
		(*edit)->setAccessibleDescription(tip);
		(*edit)->setToolTip(tip);
		(*edit)->setClearButtonEnabled(true);
		form->addRow(*box, *edit);
		connect(*box, &QCheckBox::toggled, this, [this] { updateState(); });
		connect(*edit, &QLineEdit::textChanged, this, [this] { updateState(); });
	};
	nameRow(&m_filter, &m_filterName, tr("&Only faces using"), QStringLiteral("textureResetFilter"), tr("Texture to reset"),
		tr("Change only faces and patches using this texture, ignoring case and a leading textures/."));
	nameRow(&m_rename, &m_newName, tr("Re&name to"), QStringLiteral("textureResetRename"), tr("New texture name"),
		tr("Give the targeted faces and patches this texture. Patches take only the new name."));

	const auto valueRow = [&](QCheckBox **box, QDoubleSpinBox **spin, const QString &label, const QString &name, const QString &field, double minimum,
							  double maximum, double value, double step, const QString &suffix, const QString &tip) {
		*box = new QCheckBox(label, this);
		(*box)->setObjectName(name);
		(*box)->setToolTip(tip);
		(*box)->setAccessibleDescription(tip);
		*spin = new QDoubleSpinBox(this);
		(*spin)->setObjectName(name + QStringLiteral("Value"));
		(*spin)->setLayoutDirection(Qt::LeftToRight);
		(*spin)->setRange(minimum, maximum);
		(*spin)->setDecimals(4);
		(*spin)->setSingleStep(step);
		(*spin)->setValue(value);
		(*spin)->setSuffix(suffix);
		(*spin)->setKeyboardTracking(false);
		(*spin)->setAccessibleName(field);
		(*spin)->setAccessibleDescription(tip);
		(*spin)->setToolTip(tip);
		form->addRow(*box, *spin);
		connect(*box, &QCheckBox::toggled, this, [this] { updateState(); });
		connect(*spin, &QDoubleSpinBox::valueChanged, this, [this] { updateState(); });
	};
	const QString scaleTip = tr("Map units per texel; a negative scale mirrors the texture and 0 is not allowed. Valve 220 faces keep their axes.");
	valueRow(&m_setScaleU, &m_scaleU, tr("Scale &U"), QStringLiteral("textureResetScaleU"), tr("New U scale"), -1024, 1024, defaultScale, 0.05, {},
		scaleTip);
	valueRow(&m_setScaleV, &m_scaleV, tr("Scale &V"), QStringLiteral("textureResetScaleV"), tr("New V scale"), -1024, 1024, defaultScale, 0.05, {},
		scaleTip);
	const QString shiftTip = tr("Texture offset in texels. Brush-primitive faces read it, and their scale and rotation, as Radiant's Surface Inspector does, from the image size.");
	valueRow(&m_setShiftU, &m_shiftU, tr("&Shift U"), QStringLiteral("textureResetShiftU"), tr("New U shift"), -1e6, 1e6, 0, 1, {}, shiftTip);
	valueRow(&m_setShiftV, &m_shiftV, tr("S&hift V"), QStringLiteral("textureResetShiftV"), tr("New V shift"), -1e6, 1e6, 0, 1, {}, shiftTip);
	valueRow(&m_setRotation, &m_rotation, tr("&Rotation"), QStringLiteral("textureResetRotation"), tr("New rotation"), -360, 360, 0, 15,
		QStringLiteral("°"), tr("Texture rotation in degrees. Valve 220 faces turn their axes by the change."));

	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("textureResetStatus"));
	m_status->setAccessibleName(tr("Reset Textures status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	layout->addWidget(m_status);
	layout->addStretch();

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setObjectName(QStringLiteral("textureResetApply"));
	m_apply->setText(tr("Reset"));
	m_apply->setAccessibleName(tr("Reset textures"));
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		if (m_apply->isEnabled()) {
			accept();
		}
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	connect(m_scope, &QComboBox::currentIndexChanged, this, [this] { updateState(); });
	updateState();
	resize(std::max(sizeHint().width(), 460), sizeHint().height());
}

LevelTextureResetRequest LevelTextureResetDialog::request() const
{
	LevelTextureResetRequest result;
	result.wholeMap = m_scope->currentIndex() == 1;
	result.onlyTexture = m_filter->isChecked() ? m_filterName->text().trimmed() : QString();
	result.renameTo = m_rename->isChecked() ? m_newName->text().trimmed() : QString();
	result.setScaleU = m_setScaleU->isChecked();
	result.setScaleV = m_setScaleV->isChecked();
	result.setShiftU = m_setShiftU->isChecked();
	result.setShiftV = m_setShiftV->isChecked();
	result.setRotation = m_setRotation->isChecked();
	result.scaleU = m_scaleU->value();
	result.scaleV = m_scaleV->value();
	result.shiftU = m_shiftU->value();
	result.shiftV = m_shiftV->value();
	result.rotation = m_rotation->value();
	return result;
}

void LevelTextureResetDialog::updateState()
{
	m_filterName->setEnabled(m_filter->isChecked());
	m_newName->setEnabled(m_rename->isChecked());
	m_scaleU->setEnabled(m_setScaleU->isChecked());
	m_scaleV->setEnabled(m_setScaleV->isChecked());
	m_shiftU->setEnabled(m_setShiftU->isChecked());
	m_shiftV->setEnabled(m_setShiftV->isChecked());
	m_rotation->setEnabled(m_setRotation->isChecked());
	const auto request = this->request();
	const bool selection = !request.wholeMap;
	QString text;
	bool ready = false;
	if (selection && m_selectionFaces + m_selectionPatches == 0) {
		text = tr("Nothing is selected. Select brushes or patches, pick faces, or reset the whole map.");
	} else if (m_filter->isChecked() && request.onlyTexture.isEmpty()) {
		text = tr("Name the texture to reset, or untick Only faces using.");
	} else if (m_rename->isChecked() && request.renameTo.isEmpty()) {
		text = tr("Give the new texture name, or untick Rename to.");
	} else if ((request.setScaleU && std::abs(request.scaleU) < 1e-6) || (request.setScaleV && std::abs(request.scaleV) < 1e-6)) {
		text = tr("A texture scale cannot be 0.");
	} else if (!request.changesSomething()) {
		text = tr("Tick a value to set, or a new name.");
	} else {
		ready = true;
		text = selection ? tr("The selection holds %n brush face(s).", nullptr, m_selectionFaces)
						 : tr("Every visible, unlocked surface in the open group, as the views show it.");
		if (selection && m_selectionPatches > 0) {
			text += QLatin1Char(' ') + tr("It also holds %n patch(es), which take only a new name.", nullptr, m_selectionPatches);
		}
		if (!request.onlyTexture.isEmpty()) {
			text += QLatin1Char(' ') + tr("Only surfaces using %1 change.").arg(request.onlyTexture);
		}
	}
	m_apply->setEnabled(ready);
	if (m_status->text() != text) {
		m_status->setText(text);
		m_status->setAccessibleDescription(text);
		QAccessibleEvent changed(m_status, QAccessible::DescriptionChanged);
		QAccessible::updateAccessibility(&changed);
	}
}
} // namespace vibestudio
