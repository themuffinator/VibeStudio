#include "app/model_q3_animation_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
namespace
{
QComboBox *combo()
{
	auto *result = new QComboBox;
	result->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	result->setMinimumContentsLength(12);
	return result;
}
} // namespace
ModelQ3AnimationDialog::ModelQ3AnimationDialog(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, QWidget *parent)
	: QDialog(parent), m_assembly(assembly), m_resolved(resolved)
{
	setObjectName(QStringLiteral("modelQ3AnimationDialog"));
	setWindowTitle(QCoreApplication::translate("ModelQ3AnimationDialog", "Quake III Animation"));
	setAccessibleName(windowTitle());
	resize(1050, 720);
	m_binding = assembly.q3Animation.value_or(ModelAssemblyQ3Animation{});
	auto *layout = new QVBoxLayout(this);
	m_enabled = new QCheckBox(QCoreApplication::translate("ModelQ3AnimationDialog", "Use native animation"));
	m_enabled->setObjectName(QStringLiteral("q3Enabled"));
	m_enabled->setAccessibleName(m_enabled->text());
	m_enabled->setChecked(true);
	m_enabled->setToolTip(QCoreApplication::translate("ModelQ3AnimationDialog",
													  "Clear this option and apply to remove the configuration in one undoable edit."));
	layout->addWidget(m_enabled);
	auto *split = new QSplitter;
	split->setChildrenCollapsible(false);
	layout->addWidget(split, 1);
	m_slots = new QTreeWidget;
	m_slots->setObjectName(QStringLiteral("q3Slots"));
	m_slots->setAccessibleName(QCoreApplication::translate("ModelQ3AnimationDialog", "Native animation slots"));
	m_slots->setAccessibleDescription(QCoreApplication::translate(
		"ModelQ3AnimationDialog", "Select one of the 31 native slots. Columns show adjusted model frames and the loop tail."));
	m_slots->setHeaderLabels({QCoreApplication::translate("ModelQ3AnimationDialog", "Animation"),
							  QCoreApplication::translate("ModelQ3AnimationDialog", "Model frames"),
							  QCoreApplication::translate("ModelQ3AnimationDialog", "Loop tail")});
	m_slots->setRootIsDecorated(false);
	m_slots->setUniformRowHeights(true);
	m_slots->setMinimumWidth(180);
	m_slots->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	for (int i = 0; i < modelQ3AnimationCount; ++i)
	{
		auto *item = new QTreeWidgetItem(m_slots);
		item->setText(0, modelQ3AnimationName(i));
		item->setData(0, Qt::UserRole, i);
	}
	split->addWidget(m_slots);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setMinimumWidth(240);
	auto *body = new QWidget;
	auto *form = new QFormLayout(body);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const auto field = [&](QWidget *widget, const char *id, const QString &name, const QString &description = {}) {
		widget->setObjectName(QString::fromLatin1(id));
		widget->setAccessibleName(name);
		widget->setAccessibleDescription(description);
		widget->setToolTip(description);
		widget->setMinimumWidth(0);
		auto *label = new QLabel(name);
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		label->setBuddy(widget);
		form->addRow(label, widget);
	};
	m_lower = combo();
	m_upper = combo();
	for (auto *part : {m_lower, m_upper})
	{
		part->addItem(QCoreApplication::translate("ModelQ3AnimationDialog", "Choose part…"), QString());
		for (const auto &value : assembly.parts)
			part->addItem(value.id, value.id);
	}
	field(m_lower, "q3LowerPart", QCoreApplication::translate("ModelQ3AnimationDialog", "Lower model"));
	field(m_upper, "q3UpperPart", QCoreApplication::translate("ModelQ3AnimationDialog", "Upper model"));
	m_lowerClip = combo();
	m_upperClip = combo();
	for (int i = 0; i < modelQ3AnimationCount; ++i)
	{
		if (modelQ3AnimationUsesLower(i))
			m_lowerClip->addItem(modelQ3AnimationName(i), i);
		if (modelQ3AnimationUsesUpper(i))
			m_upperClip->addItem(modelQ3AnimationName(i), i);
	}
	field(m_lowerClip, "q3LowerAnimation", QCoreApplication::translate("ModelQ3AnimationDialog", "Preview lower animation"));
	field(m_upperClip, "q3UpperAnimation", QCoreApplication::translate("ModelQ3AnimationDialog", "Preview upper animation"));
	m_first = new QSpinBox;
	m_first->setRange(0, 1000000);
	m_count = new QSpinBox;
	m_count->setRange(1, 1024);
	m_loop = new QSpinBox;
	m_loop->setRange(0, 1024);
	for (auto *spin : {m_first, m_count, m_loop})
	{
		spin->setKeyboardTracking(false);
		spin->setLayoutDirection(Qt::LeftToRight);
	}
	field(m_first, "q3First", QCoreApplication::translate("ModelQ3AnimationDialog", "Native first frame"),
		  QCoreApplication::translate("ModelQ3AnimationDialog", "The number written to animation.cfg. Leg rows subtract LEGS_WALKCR first "
																"minus TORSO_GESTURE first to obtain the model frame."));
	field(m_count, "q3Count", QCoreApplication::translate("ModelQ3AnimationDialog", "Frame count"));
	field(m_loop, "q3Loop", QCoreApplication::translate("ModelQ3AnimationDialog", "Loop tail"),
		  QCoreApplication::translate(
			  "ModelQ3AnimationDialog",
			  "Zero holds the final pose. A positive value loops only the last N playback steps after the introduction."));
	m_fps = new QDoubleSpinBox;
	m_fps->setDecimals(12);
	m_fps->setRange(.001, 1000);
	m_fps->setKeyboardTracking(false);
	m_fps->setLayoutDirection(Qt::LeftToRight);
	field(m_fps, "q3Fps", QCoreApplication::translate("ModelQ3AnimationDialog", "Frames per second"),
		  QCoreApplication::translate(
			  "ModelQ3AnimationDialog",
			  "Quake III truncates 1000 / FPS to an integer millisecond period. The effective period appears below."));
	m_reverse = new QCheckBox(QCoreApplication::translate("ModelQ3AnimationDialog", "Reverse playback"));
	field(m_reverse, "q3Reverse", QCoreApplication::translate("ModelQ3AnimationDialog", "Playback direction"));
	m_modelRange = new QLabel;
	m_modelRange->setTextFormat(Qt::PlainText);
	m_modelRange->setWordWrap(true);
	field(m_modelRange, "q3ModelRange", QCoreApplication::translate("ModelQ3AnimationDialog", "Effective model range"));
	m_sourceClip = combo();
	field(m_sourceClip, "q3SourceClip", QCoreApplication::translate("ModelQ3AnimationDialog", "Use a model clip"),
		  QCoreApplication::translate(
			  "ModelQ3AnimationDialog",
			  "Copy the selected model clip's range and saved FPS into this native slot. Loop and direction remain explicit choices."));
	auto *use = new QPushButton(QCoreApplication::translate("ModelQ3AnimationDialog", "Use Selected Clip"));
	use->setObjectName(QStringLiteral("q3UseClip"));
	use->setAutoDefault(false);
	form->addRow(use);
	connect(use, &QPushButton::clicked, this, [this] { useSourceClip(); });
	m_footsteps = combo();
	for (const auto &value : {"normal", "boot", "flesh", "mech", "energy"})
		m_footsteps->addItem(QString::fromLatin1(value), QString::fromLatin1(value));
	field(m_footsteps, "q3Footsteps", QCoreApplication::translate("ModelQ3AnimationDialog", "Footsteps"));
	m_sex = combo();
	m_sex->addItem(QCoreApplication::translate("ModelQ3AnimationDialog", "Male"), "m");
	m_sex->addItem(QCoreApplication::translate("ModelQ3AnimationDialog", "Female"), "f");
	m_sex->addItem(QCoreApplication::translate("ModelQ3AnimationDialog", "Neuter"), "n");
	field(m_sex, "q3Sex", QCoreApplication::translate("ModelQ3AnimationDialog", "Voice set"));
	const char *headIds[]{"q3HeadX", "q3HeadY", "q3HeadZ"};
	const QString headNames[]{QCoreApplication::translate("ModelQ3AnimationDialog", "Head offset X"),
							  QCoreApplication::translate("ModelQ3AnimationDialog", "Head offset Y"),
							  QCoreApplication::translate("ModelQ3AnimationDialog", "Head offset Z")};
	for (int i = 0; i < 3; ++i)
	{
		m_head[i] = new QDoubleSpinBox;
		m_head[i]->setDecimals(12);
		m_head[i]->setRange(-1000000, 1000000);
		m_head[i]->setKeyboardTracking(false);
		m_head[i]->setLayoutDirection(Qt::LeftToRight);
		field(m_head[i], headIds[i], headNames[i]);
	}
	m_fixedLegs = new QCheckBox(QCoreApplication::translate("ModelQ3AnimationDialog", "Fixed legs"));
	m_fixedTorso = new QCheckBox(QCoreApplication::translate("ModelQ3AnimationDialog", "Fixed torso"));
	field(m_fixedLegs, "q3FixedLegs", QCoreApplication::translate("ModelQ3AnimationDialog", "Leg aiming"));
	field(m_fixedTorso, "q3FixedTorso", QCoreApplication::translate("ModelQ3AnimationDialog", "Torso aiming"));
	scroll->setWidget(body);
	split->addWidget(scroll);
	split->setSizes({470, 530});
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("q3Status"));
	m_status->setAccessibleName(QCoreApplication::translate("ModelQ3AnimationDialog", "Native animation validation"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setMinimumWidth(0);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("ModelQ3AnimationDialog", "Apply"));
	auto *import = buttons->addButton(QCoreApplication::translate("ModelQ3AnimationDialog", "Import…"), QDialogButtonBox::ActionRole);
	import->setObjectName(QStringLiteral("q3Import"));
	auto *exportButton =
		buttons->addButton(QCoreApplication::translate("ModelQ3AnimationDialog", "Apply and Export…"), QDialogButtonBox::ActionRole);
	exportButton->setObjectName(QStringLiteral("q3Export"));
	layout->addWidget(buttons);
	connect(import, &QPushButton::clicked, this, [this] { chooseImport(); });
	connect(exportButton, &QPushButton::clicked, this, [this] { finish(true); });
	connect(buttons, &QDialogButtonBox::accepted, this, [this] { finish(false); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_slots, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
		if (item && !m_refreshing)
		{
			m_slot = item->data(0, Qt::UserRole).toInt();
			refreshSlot();
		}
	});
	for (auto *spin : {m_first, m_count, m_loop})
		connect(spin, &QSpinBox::valueChanged, this, [this] { updateClip(); });
	connect(m_fps, &QDoubleSpinBox::valueChanged, this, [this] { updateClip(); });
	connect(m_reverse, &QCheckBox::toggled, this, [this] { updateClip(); });
	for (auto *selector : {m_lower, m_upper, m_lowerClip, m_upperClip, m_footsteps, m_sex})
		connect(selector, &QComboBox::currentIndexChanged, this, [this, selector] {
			if (!m_refreshing)
			{
				if (selector == m_lower || selector == m_upper)
					refreshSlot();
				updateStatus();
			}
		});
	for (int i = 0; i < 3; ++i)
		connect(m_head[i], &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
			if (!m_refreshing)
			{
				float *fields[]{&m_binding.config.headOffset.x, &m_binding.config.headOffset.y, &m_binding.config.headOffset.z};
				*fields[i] = float(value);
				updateStatus();
			}
		});
	for (auto *check : {m_enabled, m_fixedLegs, m_fixedTorso})
		connect(check, &QCheckBox::toggled, this, [this] {
			if (!m_refreshing)
				updateStatus();
		});
	refresh();
	m_slots->setCurrentItem(m_slots->topLevelItem(0));
}
std::optional<ModelAssemblyQ3Animation> ModelQ3AnimationDialog::animation() const
{
	if (!m_enabled->isChecked())
		return std::nullopt;
	auto binding = m_binding;
	binding.lowerPart = m_lower->currentData().toString();
	binding.upperPart = m_upper->currentData().toString();
	binding.lowerAnimation = m_lowerClip->currentData().toInt();
	binding.upperAnimation = m_upperClip->currentData().toInt();
	binding.config.footsteps = m_footsteps->currentData().toString();
	binding.config.sex = m_sex->currentData().toString();
	// Keep imported sub-display-precision values until their field is edited.
	binding.config.fixedLegs = m_fixedLegs->isChecked();
	binding.config.fixedTorso = m_fixedTorso->isChecked();
	return binding;
}
bool ModelQ3AnimationDialog::validateDraft(QString *error) const
{
	auto assembly = m_assembly;
	assembly.q3Animation = animation();
	return validateModelAssembly(assembly, error) && validateModelAssemblyQ3Animation(assembly, m_resolved, error);
}
void ModelQ3AnimationDialog::refresh()
{
	m_refreshing = true;
	m_lower->setCurrentIndex(std::max(0, m_lower->findData(m_binding.lowerPart)));
	m_upper->setCurrentIndex(std::max(0, m_upper->findData(m_binding.upperPart)));
	m_lowerClip->setCurrentIndex(m_lowerClip->findData(m_binding.lowerAnimation));
	m_upperClip->setCurrentIndex(m_upperClip->findData(m_binding.upperAnimation));
	m_footsteps->setCurrentIndex(m_footsteps->findData(m_binding.config.footsteps));
	m_sex->setCurrentIndex(m_sex->findData(m_binding.config.sex));
	m_head[0]->setValue(m_binding.config.headOffset.x);
	m_head[1]->setValue(m_binding.config.headOffset.y);
	m_head[2]->setValue(m_binding.config.headOffset.z);
	m_fixedLegs->setChecked(m_binding.config.fixedLegs);
	m_fixedTorso->setChecked(m_binding.config.fixedTorso);
	m_refreshing = false;
	refreshSlot();
	updateStatus();
}
void ModelQ3AnimationDialog::refreshSlot()
{
	m_refreshing = true;
	const auto &clip = m_binding.config.clips[m_slot];
	m_first->setValue(clip.firstFrame);
	m_count->setValue(clip.frameCount);
	m_loop->setValue(clip.loopFrames);
	m_fps->setValue(clip.framesPerSecond);
	m_reverse->setChecked(clip.reversed);
	m_sourceClip->clear();
	const auto id = modelQ3AnimationUsesLower(m_slot) ? m_lower->currentData().toString() : m_upper->currentData().toString();
	for (const auto &input : m_resolved.inputs)
		if (input.part == id)
			for (int i = 0; i < input.mesh.animations.size(); ++i)
				m_sourceClip->addItem(input.mesh.animations[i].name, i);
	m_refreshing = false;
	updateStatus();
}
void ModelQ3AnimationDialog::updateClip()
{
	if (m_refreshing)
		return;
	auto &clip = m_binding.config.clips[m_slot];
	clip.firstFrame = m_first->value();
	clip.frameCount = m_count->value();
	clip.loopFrames = m_loop->value();
	clip.framesPerSecond = float(m_fps->value());
	clip.reversed = m_reverse->isChecked();
	updateStatus();
}
void ModelQ3AnimationDialog::updateStatus()
{
	for (int i = 0; i < modelQ3AnimationCount; ++i)
	{
		const auto &clip = m_binding.config.clips[i];
		const int first = modelQ3AnimationFirstFrame(m_binding.config, i);
		const auto range = QStringLiteral("\u2066%1–%2\u2069").arg(first).arg(first + clip.frameCount - 1);
		m_slots->topLevelItem(i)->setText(
			1, range + (clip.reversed ? QCoreApplication::translate("ModelQ3AnimationDialog", " (reverse)") : QString()));
		m_slots->topLevelItem(i)->setText(2, QString::number(clip.loopFrames));
	}
	const int first = modelQ3AnimationFirstFrame(m_binding.config, m_slot), period = modelQ3AnimationPeriod(m_binding.config.clips[m_slot]);
	m_modelRange->setText(
		QCoreApplication::translate("ModelQ3AnimationDialog", "%1 · %2 ms per pose · %3 effective FPS")
			.arg(QStringLiteral("\u2066%1–%2\u2069").arg(first).arg(first + m_binding.config.clips[m_slot].frameCount - 1))
			.arg(period)
			.arg(period ? 1000.0 / period : 0, 0, 'g', 6));
	QString error;
	const bool valid = validateDraft(&error);
	m_status->setText(
		valid ? (m_enabled->isChecked()
					 ? QCoreApplication::translate("ModelQ3AnimationDialog",
												   "All native slots fit their models. Apply to preview or bake the selected lower and "
												   "upper clips. Game aiming, movement and transition blends are not simulated.")
					 : QCoreApplication::translate("ModelQ3AnimationDialog",
												   "Apply to remove native animation. Ordinary per-part playback settings will be used."))
			  : error);
}
bool ModelQ3AnimationDialog::setConfigBytes(const QByteArray &bytes, QString *error)
{
	ModelQ3AnimationConfig config;
	QStringList notes;
	if (!parseModelQ3Animation(bytes, &config, error, &notes))
		return false;
	// Retain part and playback choices made since the dialog was opened.
	if (auto draft = animation())
		m_binding = *draft;
	m_binding.config = std::move(config);
	m_enabled->setChecked(true);
	refresh();
	if (!notes.isEmpty())
		m_status->setText(notes.join(' ') + ' ' + m_status->text());
	return true;
}
void ModelQ3AnimationDialog::chooseImport()
{
	const auto path =
		QFileDialog::getOpenFileName(this, QCoreApplication::translate("ModelQ3AnimationDialog", "Import Quake III Animation"), {},
									 QCoreApplication::translate("ModelQ3AnimationDialog", "Animation configuration (*.cfg)"));
	if (path.isEmpty())
		return;
	QByteArray bytes;
	QString error;
	if (QFileInfo(path).size() > modelQ3AnimationMaxBytes)
		error = QCoreApplication::translate("ModelQ3AnimationDialog", "The configuration exceeds 19,998 bytes.");
	else if (readModelFile(path, &bytes, &error) && setConfigBytes(bytes, &error))
		return;
	m_status->setText(error);
}
void ModelQ3AnimationDialog::useSourceClip()
{
	const auto id = modelQ3AnimationUsesLower(m_slot) ? m_lower->currentData().toString() : m_upper->currentData().toString();
	for (const auto &input : m_resolved.inputs)
		if (input.part == id)
		{
			const int index = m_sourceClip->currentData().isValid() ? m_sourceClip->currentData().toInt() : -1;
			if (index < 0 || index >= input.mesh.animations.size())
				return;
			const auto &source = input.mesh.animations[index];
			if (m_slot == 13 && source.firstFrame != m_binding.config.clips[6].firstFrame)
			{
				m_status->setText(QCoreApplication::translate("ModelQ3AnimationDialog",
															  "LEGS_WALKCR must start at the same model frame as TORSO_GESTURE. Adjust the "
															  "torso anchor before assigning this clip."));
				return;
			}
			auto &clip = m_binding.config.clips[m_slot];
			const int offset =
				m_slot >= 13 && m_slot <= 24 ? m_binding.config.clips[13].firstFrame - m_binding.config.clips[6].firstFrame : 0;
			if (source.firstFrame + offset < 0 || source.firstFrame + offset > 1000000)
			{
				m_status->setText(QCoreApplication::translate(
					"ModelQ3AnimationDialog", "This model clip would exceed the native first-frame range. Adjust the leg offset first."));
				return;
			}
			clip.firstFrame = source.firstFrame + offset;
			clip.frameCount = source.frameCount;
			if (source.framesPerSecond > 0)
				clip.framesPerSecond = float(source.framesPerSecond);
			refreshSlot();
			return;
		}
}
void ModelQ3AnimationDialog::finish(bool exporting)
{
	QString error;
	if (!validateDraft(&error))
	{
		m_status->setText(error);
		m_status->setFocus(Qt::OtherFocusReason);
		return;
	}
	if (exporting && !animation())
	{
		m_status->setText(
			QCoreApplication::translate("ModelQ3AnimationDialog", "Enable native animation before exporting a configuration."));
		return;
	}
	m_exportRequested = exporting;
	accept();
}
} // namespace vibestudio
