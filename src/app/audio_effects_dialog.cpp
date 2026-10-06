#include "app/audio_effects_dialog.h"
#include "app/audio_automation_editor.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio
{
namespace
{
class EffectListDelegate final : public QStyledItemDelegate {
  public:
	explicit EffectListDelegate(QListWidget *view) : QStyledItemDelegate(view), m_view(view) {}
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		// Long translated rows must wrap inside the actual viewport, including
		// the space consumed by its vertical scrollbar in either layout direction.
		auto bounded = option;
		bounded.rect.setWidth(std::max(1, m_view->viewport()->width()));
		auto size = QStyledItemDelegate::sizeHint(bounded, index);
		size.setWidth(bounded.rect.width());
		return size;
	}

  private:
	QListWidget *m_view;
};
} // namespace
struct AudioEffectPresetWork {
	std::atomic<bool> cancel{false};
	AudioEffectPreset preset;
	AudioProjectIdentity identity;
	AudioProjectSaveReport saved;
	QString error;
};
AudioEffectsDialog::AudioEffectsDialog(const AudioSession &session, const QString &trackId, QWidget *parent,
                                       const QStringList &protectedPaths)
    : QDialog(parent), m_draft(session), m_protectedPaths(protectedPaths), m_trackId(trackId)
{
	for (int i = 0; i < session.tracks.size(); ++i)
		if (session.tracks[i].id == trackId)
			m_track = i;
	m_missing = !trackId.isEmpty() && m_track < 0;
	setObjectName("audioEffectsDialog");
	setWindowTitle(tr("Effects — %1")
	                   .arg(m_missing     ? tr("Missing track")
	                        : m_track < 0 ? tr("Master")
	                                      : session.tracks[m_track].name));
	setAccessibleName(windowTitle());
	resize(740, 760);
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setAccessibleName(tr("Effect chain and parameters"));
	scroll->setWidgetResizable(true);
	auto *body = new QWidget;
	m_body = body;
	auto *form = new QFormLayout(body);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body);
	layout->addWidget(scroll, 1);
	auto *placement = new QLabel(m_track < 0 ? tr("After master gain") : tr("After fader and pan"));
	placement->setToolTip(tr("Inserts run in list order. Post-fader sends include inserts; pre-fader sends stay dry. "
	                         "Applying stops playback."));
	form->addRow(placement);
	auto *disclosure = new QToolButton;
	disclosure->setObjectName("effectsPresets");
	disclosure->setText(tr("Presets"));
	disclosure->setAccessibleName(tr("Show effect presets"));
	disclosure->setCheckable(true);
	disclosure->setToolButtonStyle(Qt::ToolButtonTextOnly);
	form->addRow(disclosure);
	auto *presets = new QWidget;
	auto *presetForm = new QFormLayout(presets);
	presetForm->setContentsMargins(0, 0, 0, 0);
	presetForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	presetForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_factory = new QComboBox;
	m_factory->setObjectName("effectsFactoryPreset");
	m_factory->setAccessibleName(tr("Factory effect preset"));
	m_factory->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_factory->setMinimumContentsLength(12);
	for (const auto &preset : audioEffectFactoryPresets()) {
		m_factory->addItem(preset.name, preset.id);
		m_factory->setItemData(m_factory->count() - 1, preset.description, Qt::ToolTipRole);
	}
	presetForm->addRow(tr("Factory preset"), m_factory);
	auto *factoryLoad = new QPushButton(tr("Load"));
	factoryLoad->setObjectName("effectsLoadFactoryPreset");
	factoryLoad->setAccessibleName(tr("Replace staged chain with factory preset"));
	factoryLoad->setToolTip(tr("Replaces this draft chain. Apply Effects commits the change to the session."));
	presetForm->addRow(factoryLoad);
	m_presetName = new QLineEdit;
	m_presetName->setObjectName("effectsPresetName");
	m_presetName->setAccessibleName(tr("Effect preset name"));
	m_presetName->setMaxLength(128);
	m_presetName->setText(tr("Custom effects"));
	presetForm->addRow(tr("Preset name"), m_presetName);
	auto *openPreset = new QPushButton(tr("Open Preset…"));
	openPreset->setAccessibleName(tr("Open effect preset file"));
	openPreset->setObjectName("effectsOpenPreset");
	presetForm->addRow(openPreset);
	auto *savePreset = new QPushButton(tr("Save Preset…"));
	savePreset->setAccessibleName(tr("Save staged effect chain as preset"));
	savePreset->setObjectName("effectsSavePreset");
	presetForm->addRow(savePreset);
	form->addRow(presets);
	presets->hide();
	connect(disclosure, &QToolButton::toggled, presets, &QWidget::setVisible);
	connect(factoryLoad, &QPushButton::clicked, this, [this] {
		QString error;
		const auto preset = makeAudioEffectPreset(m_factory->currentData().toString(), m_draft.sampleRate, &error);
		if (error.isEmpty())
			applyPreset(preset);
		else
			m_status->setText(error);
	});
	connect(openPreset, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getOpenFileName(this, tr("Open Effect Preset"), m_presetIdentity.path,
		                                               tr("VibeStudio effect presets (*.vsfx)"));
		if (!path.isEmpty())
			loadPresetFile(path);
	});
	connect(savePreset, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getSaveFileName(this, tr("Save Effect Preset"),
		                                               m_presetIdentity.path.isEmpty() ? QStringLiteral("effects.vsfx")
		                                                                               : m_presetIdentity.path,
		                                               tr("VibeStudio effect presets (*.vsfx)"));
		if (!path.isEmpty())
			savePresetFile(path, true);
	});
	m_list = new QListWidget;
	m_list->setObjectName("effectsChain");
	m_list->setAccessibleName(tr("Ordered effect chain"));
	m_list->setWordWrap(true);
	m_list->setItemDelegate(new EffectListDelegate(m_list));
	m_list->setResizeMode(QListView::Adjust);
	m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	const int chainHeight = std::max(120, 4 * m_list->fontMetrics().lineSpacing() + 24);
	m_list->setMinimumHeight(chainHeight);
	m_list->setMaximumHeight(std::max(200, chainHeight));
	form->addRow(m_list);
	auto *order = new QWidget;
	auto *orderLayout = new QHBoxLayout(order);
	orderLayout->setContentsMargins(0, 0, 0, 0);
	m_remove = new QPushButton(tr("Remove"));
	m_remove->setAccessibleName(tr("Remove selected effect"));
	m_remove->setObjectName("effectsRemove");
	m_up = new QPushButton(QStringLiteral("↑"));
	m_up->setAccessibleName(tr("Move selected effect earlier"));
	m_up->setObjectName("effectsUp");
	m_down = new QPushButton(QStringLiteral("↓"));
	m_down->setAccessibleName(tr("Move selected effect later"));
	m_down->setObjectName("effectsDown");
	for (auto *button : {m_up, m_down}) {
		button->setToolTip(button->accessibleName());
		button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	}
	orderLayout->addWidget(m_remove, 1);
	orderLayout->addWidget(m_up);
	orderLayout->addWidget(m_down);
	form->addRow(order);
	m_type = new QComboBox;
	m_type->setObjectName("effectsType");
	m_type->setAccessibleName(tr("Effect to add"));
	m_type->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_type->setMinimumContentsLength(16);
	for (const auto &type : audioEffectTypes())
		m_type->addItem(audioEffectName(type), type);
	form->addRow(tr("Processor"), m_type);
	m_add = new QPushButton(tr("Add"));
	m_add->setObjectName("effectsAdd");
	m_add->setAccessibleName(tr("Add processor to chain"));
	form->addRow(m_add);
	m_enabled = new QCheckBox;
	m_enabled->setObjectName("effectsEnabled");
	m_enabled->setAccessibleName(tr("Enable selected effect"));
	m_enabled->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	form->addRow(tr("Enabled"), m_enabled);
	auto *parameters = new QWidget;
	m_parameters = new QFormLayout(parameters);
	m_parameters->setContentsMargins(0, 0, 0, 0);
	m_parameters->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_parameters->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->addRow(parameters);
	m_automation = new QPushButton(tr("Automation…"));
	m_automation->setObjectName("effectsAutomation");
	m_automation->setAccessibleName(tr("Edit selected effect automation"));
	m_automation->setToolTip(tr("Edit session curves for this effect. Presets contain static values; replacing a chain "
	                            "removes its automation."));
	form->addRow(m_automation);
	connect(m_automation, &QPushButton::clicked, this, &AudioEffectsDialog::editAutomation);
	m_tail = new QDoubleSpinBox;
	m_tail->setObjectName("effectsTail");
	m_tail->setAccessibleName(tr("Session effect tail in seconds"));
	m_tail->setDecimals(3);
	m_tail->setRange(0, 60);
	m_tail->setValue(session.effectTailSeconds);
	m_tail->setKeyboardTracking(false);
	m_tail->setToolTip(
	    tr("Extends the default range after the last clip when filters, delays or reverb are enabled. Set enough time "
	       "for the "
	       "decay; explicit ranges still use their chosen end. Seek and loop restart with fresh effect history."));
	form->addRow(tr("Session tail (s)"), m_tail);
	for (auto *label : body->findChildren<QLabel *>()) {
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
	}
	m_status = new QLabel;
	m_status->setObjectName("effectsStatus");
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Effect validation"));
	layout->addWidget(m_status);
	m_progress = new QProgressBar;
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Effect preset file operation"));
	m_progress->hide();
	layout->addWidget(m_progress);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply Effects"));
	layout->addWidget(m_buttons);
	connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
		validate();
		if (m_buttons->button(QDialogButtonBox::Ok)->isEnabled())
			accept();
	});
	connect(m_buttons, &QDialogButtonBox::rejected, this, &AudioEffectsDialog::reject);
	connect(m_list, &QListWidget::currentRowChanged, this, [this] { select(); });
	connect(m_add, &QPushButton::clicked, this, [this] {
		if (m_missing || chain().size() >= AudioEffectChainLimit)
			return;
		chain().append(makeAudioEffect(m_type->currentData().toString(), m_draft.sampleRate));
		refresh(int(chain().size() - 1));
	});
	connect(m_remove, &QPushButton::clicked, this, [this] {
		const int row = m_list->currentRow();
		if (row < 0)
			return;
		chain().removeAt(row);
		retainAudioEffectAutomation(chain(), &automation());
		refresh(row);
	});
	for (const auto &[button, delta] : {std::pair{m_up, -1}, std::pair{m_down, 1}})
		connect(button, &QPushButton::clicked, this, [this, delta] {
			const int row = m_list->currentRow(), next = row + delta;
			if (row < 0 || next < 0 || next >= chain().size())
				return;
			chain().move(row, next);
			refresh(next);
		});
	connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
		if (m_updating || m_list->currentRow() < 0)
			return;
		chain()[m_list->currentRow()].enabled = enabled;
		updateItem();
		validate();
	});
	connect(m_tail, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		m_draft.effectTailSeconds = value;
		validate();
	});
	refresh(0);
	if (m_missing)
		body->setEnabled(false);
}
AudioEffectChain &AudioEffectsDialog::chain()
{
	return m_track < 0 ? m_draft.masterEffects : m_draft.tracks[m_track].effects;
}
AudioEffectAutomation &AudioEffectsDialog::automation()
{
	return m_track < 0 ? m_draft.masterEffectAutomation : m_draft.tracks[m_track].effectAutomation;
}
void AudioEffectsDialog::editAutomation()
{
	const int row = m_list->currentRow();
	if (busy() || m_missing || row < 0)
		return;
	const auto effect = chain().at(row);
	auto parameters = audioEffectParameters(effect.type, m_draft.sampleRate);
	parameters.erase(std::remove_if(parameters.begin(), parameters.end(), [](const auto &p) { return !p.automatable; }),
	                 parameters.end());
	if (parameters.isEmpty())
		return;
	QDialog dialog(this);
	dialog.setObjectName("effectAutomationDialog");
	dialog.setWindowTitle(tr("%1 Automation").arg(audioEffectName(effect.type)));
	dialog.resize(680, 720);
	auto *layout = new QVBoxLayout(&dialog);
	auto *parameter = new QComboBox;
	parameter->setObjectName("automationParameter");
	parameter->setAccessibleName(tr("Effect parameter"));
	for (const auto &p : parameters)
		parameter->addItem(p.label, p.key);
	layout->addWidget(parameter);
	auto *enabled = new QCheckBox(tr("Read automation"));
	enabled->setObjectName("automationEnabled");
	enabled->setAccessibleName(enabled->text());
	enabled->setToolTip(tr("When off, points are retained and the static effect value is used."));
	layout->addWidget(enabled);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	layout->addWidget(scroll, 1);
	auto *error = new QLabel;
	error->setWordWrap(true);
	error->setAccessibleName(tr("Automation validation"));
	layout->addWidget(error);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	AudioEffectAutomation draft = automation();
	AudioAutomationEditor *editor = nullptr;
	QString selected;
	const auto store = [&] {
		if (!editor)
			return;
		draft.erase(
		    std::remove_if(draft.begin(), draft.end(),
		                   [&](const auto &lane) { return lane.effectId == effect.id && lane.parameter == selected; }),
		    draft.end());
		if (!editor->points().isEmpty())
			draft.append({effect.id, selected, enabled->isChecked(), editor->points()});
	};
	const auto show = [&] {
		store();
		delete scroll->takeWidget();
		const auto &p = parameters.at(parameter->currentIndex());
		selected = p.key;
		editor = new AudioAutomationEditor(p.label, p.minimum, p.maximum, effect.parameters.value(p.key),
		                                   m_automationCursor, audioSessionFrames(m_draft));
		enabled->setChecked(true);
		for (const auto &lane : std::as_const(draft))
			if (lane.effectId == effect.id && lane.parameter == selected) {
				editor->setPoints(lane.points);
				enabled->setChecked(lane.enabled);
			}
		scroll->setWidget(editor);
	};
	show();
	connect(parameter, &QComboBox::currentIndexChanged, &dialog, show);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		buttons->setFocus();
		store();
		auto proposed = edit();
		proposed.effectAutomation = draft;
		const auto checked = editAudioSession(m_draft, proposed);
		if (!checked.succeeded()) {
			error->setText(checked.error);
			return;
		}
		dialog.accept();
	});
	if (dialog.exec() == QDialog::Accepted) {
		automation() = std::move(draft);
		select();
	}
}
void AudioEffectsDialog::refresh(int selection)
{
	{
		const QSignalBlocker guard(m_list);
		m_list->clear();
		for (const auto &effect : chain()) {
			auto *item = new QListWidgetItem(
			    tr("%1 · %2").arg(audioEffectName(effect.type), effect.enabled ? tr("Enabled") : tr("Bypassed")),
			    m_list);
			item->setToolTip(effect.id);
		}
		m_list->setCurrentRow(chain().isEmpty() ? -1 : std::clamp(selection, 0, int(chain().size() - 1)));
	}
	select();
}
void AudioEffectsDialog::select()
{
	m_updating = true;
	while (m_parameters->count()) {
		auto *item = m_parameters->takeAt(0);
		delete item->widget();
		delete item;
	}
	const int row = m_list->currentRow();
	m_enabled->setEnabled(row >= 0);
	m_enabled->setChecked(row >= 0 && chain()[row].enabled);
	if (row >= 0) {
		const auto &effect = chain()[row];
		for (const auto &parameter : audioEffectParameters(effect.type, m_draft.sampleRate)) {
			auto *box = new QDoubleSpinBox;
			box->setObjectName("effectParameter_" + parameter.key);
			box->setAccessibleName(parameter.label);
			box->setDecimals(parameter.decimals);
			box->setRange(parameter.minimum, parameter.maximum);
			box->setValue(effect.parameters.value(parameter.key));
			box->setKeyboardTracking(false);
			if (!parameter.automatable) {
				const auto description =
				    tr("Changing lookahead rebuilds latency compensation. This parameter cannot be automated.");
				box->setToolTip(description);
				box->setAccessibleDescription(description);
			}
			m_parameters->addRow(parameter.label, box);
			connect(box, &QDoubleSpinBox::valueChanged, this, [this, key = parameter.key](double value) {
				if (m_updating || m_list->currentRow() < 0)
					return;
				chain()[m_list->currentRow()].parameters[key] = value;
				validate();
			});
		}
		for (auto *label : m_parameters->parentWidget()->findChildren<QLabel *>()) {
			label->setTextFormat(Qt::PlainText);
			label->setWordWrap(true);
		}
	}
	m_updating = false;
	validate();
}
void AudioEffectsDialog::updateItem()
{
	if (auto *item = m_list->currentItem()) {
		const auto &effect = chain()[m_list->currentRow()];
		item->setText(tr("%1 · %2").arg(audioEffectName(effect.type), effect.enabled ? tr("Enabled") : tr("Bypassed")));
	}
}
void AudioEffectsDialog::validate()
{
	const auto error = m_missing ? tr("The selected track no longer exists.") : validateAudioSessionStructure(m_draft);
	m_status->setText(error.isEmpty()
	        ? tr("Latency %1 frames · %2 automation lanes.")
	                            .arg(audioEffectLatencyFrames(chain(), m_draft.sampleRate))
	                            .arg(automation().size())
	                      : error);
	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty() && !busy());
	m_body->setEnabled(!m_missing && !busy());
	m_progress->setVisible(busy());
	const int row = m_list->currentRow();
	m_add->setEnabled(!m_missing && chain().size() < AudioEffectChainLimit);
	m_remove->setEnabled(row >= 0);
	m_automation->setEnabled(row >= 0);
	m_up->setEnabled(row > 0);
	m_down->setEnabled(row >= 0 && row + 1 < chain().size());
}
AudioSessionEdit AudioEffectsDialog::edit() const
{
	AudioSessionEdit result;
	result.operation = "effects";
	result.trackId = m_trackId;
	result.effects = m_track < 0 ? m_draft.masterEffects : m_draft.tracks[m_track].effects;
	result.effectAutomation = m_track < 0 ? m_draft.masterEffectAutomation : m_draft.tracks[m_track].effectAutomation;
	result.effectTailSeconds = m_draft.effectTailSeconds;
	return result;
}
AudioEffectsDialog::~AudioEffectsDialog()
{
	if (m_work)
		m_work->cancel = true;
	if (m_thread)
		m_thread->wait();
}
void AudioEffectsDialog::reject()
{
	if (busy()) {
		m_closePending = true;
		m_work->cancel = true;
		m_status->setText(tr("Cancelling preset operation…"));
		return;
	}
	QDialog::reject();
}
void AudioEffectsDialog::closeEvent(QCloseEvent *event)
{
	if (busy()) {
		event->ignore();
		reject();
		return;
	}
	QDialog::closeEvent(event);
}
bool AudioEffectsDialog::applyPreset(const AudioEffectPreset &preset)
{
	if (busy() || m_missing)
		return false;
	AudioSessionEdit change = edit();
	QString error;
	if (!instantiateAudioEffectPreset(preset, m_draft.sampleRate, &change.effects, &error)) {
		m_status->setText(error);
		return false;
	}
	change.effectTailSeconds = std::max(m_draft.effectTailSeconds, preset.tailSeconds);
	change.effectAutomation = AudioEffectAutomation{};
	const auto result = editAudioSession(m_draft, change);
	if (!result.succeeded()) {
		m_status->setText(result.error);
		return false;
	}
	m_draft = result.session;
	m_presetName->setText(preset.name);
	m_tail->setValue(m_draft.effectTailSeconds);
	refresh(0);
	m_status->setText(tr("Preset loaded into the draft. Apply Effects to update the session."));
	return true;
}
void AudioEffectsDialog::launchPreset(std::function<void(AudioEffectPresetWork &)> perform, bool loading)
{
	m_work = std::make_shared<AudioEffectPresetWork>();
	const auto state = m_work;
	m_thread = QThread::create([state, perform] {
		try {
			perform(*state);
		} catch (const std::exception &) {
			state->error = tr("Preset operation failed because a worker or memory allocation failed.");
		}
	});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this, [this, state, loading] {
		m_thread->deleteLater();
		m_thread = nullptr;
		m_work.reset();
		validate();
		if (state->saved.written) {
			m_presetIdentity = state->saved.identity;
			m_status->setText(tr("Preset saved: %1").arg(m_presetIdentity.path));
		} else if (state->cancel)
			m_status->setText(tr("Preset operation cancelled."));
		else if (!state->error.isEmpty())
			m_status->setText(state->error);
		else if (loading && applyPreset(state->preset))
			m_presetIdentity = state->identity;
		if (m_closePending)
			QDialog::reject();
	});
	m_thread->start();
	validate();
	m_status->setText(loading ? tr("Loading effect preset…") : tr("Saving effect preset…"));
}
bool AudioEffectsDialog::loadPresetFile(const QString &path)
{
	if (busy() || m_missing)
		return false;
	launchPreset(
	    [path](AudioEffectPresetWork &work) {
		    readAudioEffectPreset(path, &work.preset, &work.identity, &work.error,
		                          {[&work] { return work.cancel.load(); }});
	    },
	    true);
	return true;
}
bool AudioEffectsDialog::savePresetFile(const QString &path, bool overwrite)
{
	if (busy() || m_missing)
		return false;
	AudioEffectPreset preset{m_presetName->text(), m_draft.sampleRate, m_draft.effectTailSeconds, chain()};
	const auto error = validateAudioEffectPreset(preset);
	if (!error.isEmpty()) {
		m_status->setText(error);
		return false;
	}
	AudioProjectSaveRequest request;
	request.path = path;
	request.overwrite = overwrite;
	const auto sameName = QFileInfo(path).absoluteFilePath().compare(m_presetIdentity.path,
#ifdef Q_OS_WIN
	                                                                 Qt::CaseInsensitive
#else
	                                                                 Qt::CaseSensitive
#endif
	                                                                 ) == 0;
	if (sameName || (!m_presetIdentity.path.isEmpty() && audioPathsReferToSameFile(path, m_presetIdentity.path)))
		request.expected = m_presetIdentity;
	auto protectedPaths = m_protectedPaths;
	for (const auto &source : m_draft.sources)
		protectedPaths << source.audio.sourcePath;
	launchPreset(
	    [preset, request, protectedPaths](AudioEffectPresetWork &work) {
		    work.saved =
		        writeAudioEffectPreset(preset, request, protectedPaths, {[&work] { return work.cancel.load(); }});
		    work.error = work.saved.error;
	    },
	    false);
	return true;
}
} // namespace vibestudio
