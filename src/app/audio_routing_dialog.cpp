#include "app/audio_routing_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
QDoubleSpinBox *number(const QString &name, const char *object, double low, double high, int decimals)
{
	auto *box = new QDoubleSpinBox;
	box->setAccessibleName(name);
	box->setObjectName(QLatin1String(object));
	box->setRange(low, high);
	box->setDecimals(decimals);
	box->setKeyboardTracking(false);
	return box;
}
} // namespace
AudioRoutingDialog::AudioRoutingDialog(const AudioSession &session, const QString &trackId, QWidget *parent)
    : QDialog(parent), m_draft(session)
{
	setObjectName("audioRoutingDialog");
	for (int i = 0; i < m_draft.tracks.size(); ++i)
		if (m_draft.tracks[i].id == trackId)
			m_track = i;
	setWindowTitle(tr("Routing — %1").arg(m_track < 0 ? tr("Missing track") : m_draft.tracks[m_track].name));
	setAccessibleName(windowTitle());
	resize(720, 720);
	auto *outer = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Track output and sends"));
	auto *body = new QWidget;
	auto *form = new QFormLayout(body);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body);
	outer->addWidget(scroll, 1);
	m_output = new QComboBox;
	m_output->setObjectName("routingOutput");
	m_output->setAccessibleName(tr("Main output destination"));
	destinations(m_output);
	form->addRow(tr("Output"), m_output);
	const auto toggle = [&](const QString &label, const QString &name, const char *object, QFormLayout *layout) {
		auto *box = new QCheckBox;
		box->setAccessibleName(name);
		box->setObjectName(QLatin1String(object));
		box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
		layout->addRow(label, box);
		return box;
	};
	m_outputEnabled = toggle(tr("Output enabled"), tr("Enable main output"), "routingOutputEnabled", form);
	m_swap = toggle(tr("Swap left/right"), tr("Swap stereo channels"), "routingSwap", form);
	m_left = toggle(tr("Invert left polarity"), tr("Invert left channel polarity"), "routingLeft", form);
	m_right = toggle(tr("Invert right polarity"), tr("Invert right channel polarity"), "routingRight", form);
	m_sends = new QListWidget;
	m_sends->setObjectName("routingSends");
	m_sends->setAccessibleName(tr("Sends"));
	m_sends->setWordWrap(true);
	m_sends->setResizeMode(QListView::Adjust);
	m_sends->setMinimumHeight(110);
	m_sends->setMaximumHeight(190);
	form->addRow(new QLabel(tr("Sends")));
	form->addRow(m_sends);
	auto *sendButtons = new QWidget;
	auto *buttonLayout = new QHBoxLayout(sendButtons);
	buttonLayout->setContentsMargins(0, 0, 0, 0);
	m_add = new QPushButton(tr("Add"));
	m_add->setAccessibleName(tr("Add send"));
	m_add->setObjectName("routingAddSend");
	m_remove = new QPushButton(tr("Remove"));
	m_remove->setAccessibleName(tr("Remove selected send"));
	m_remove->setObjectName("routingRemoveSend");
	buttonLayout->addWidget(m_add);
	buttonLayout->addWidget(m_remove);
	form->addRow(sendButtons);
	m_sendForm = new QWidget;
	auto *sendForm = new QFormLayout(m_sendForm);
	sendForm->setContentsMargins(0, 0, 0, 0);
	sendForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	sendForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->addRow(m_sendForm);
	m_target = new QComboBox;
	m_target->setObjectName("routingSendTarget");
	m_target->setAccessibleName(tr("Selected send destination"));
	destinations(m_target);
	sendForm->addRow(tr("Send to"), m_target);
	m_tap = new QComboBox;
	m_tap->setObjectName("routingSendTap");
	m_tap->setAccessibleName(tr("Send signal point"));
	m_tap->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_tap->setMinimumContentsLength(18);
	m_tap->addItem(tr("After fader and pan"), false);
	m_tap->addItem(tr("Before fader and pan"), true);
	sendForm->addRow(tr("Signal point"), m_tap);
	m_gain = number(tr("Send gain in decibels"), "routingSendGain", -96, 24, 2);
	m_pan = number(tr("Send stereo balance"), "routingSendPan", -1, 1, 3);
	m_pan->setSingleStep(.1);
	sendForm->addRow(tr("Send gain (dB)"), m_gain);
	sendForm->addRow(tr("Send balance"), m_pan);
	m_enabled = toggle(tr("Send enabled"), tr("Enable selected send"), "routingSendEnabled", sendForm);
	for (auto *label : body->findChildren<QLabel *>()) {
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
	}
	m_status = new QLabel;
	m_status->setObjectName("routingStatus");
	m_status->setAccessibleName(tr("Routing validation"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setToolTip(
	    tr("Mute silences outputs and all sends; solo admits paths through selected tracks or buses."));
	outer->addWidget(m_status);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply Routing"));
	m_buttons->button(QDialogButtonBox::Ok)
	    ->setToolTip(tr("Stops playback and applies this routing as one undoable edit."));
	outer->addWidget(m_buttons);
	connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
		validate();
		if (m_buttons->button(QDialogButtonBox::Ok)->isEnabled())
			accept();
	});
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	if (m_track >= 0) {
		const auto &routing = m_draft.tracks[m_track].routing;
		m_output->setCurrentIndex(m_output->findData(routing.outputId));
		m_outputEnabled->setChecked(routing.outputEnabled);
		m_swap->setChecked(routing.swapChannels);
		m_left->setChecked(routing.invertLeft);
		m_right->setChecked(routing.invertRight);
	}
	const auto outputChanged = [this] {
		if (m_track < 0)
			return;
		auto &routing = m_draft.tracks[m_track].routing;
		routing.outputId = m_output->currentData().toString();
		routing.outputEnabled = m_outputEnabled->isChecked();
		routing.swapChannels = m_swap->isChecked();
		routing.invertLeft = m_left->isChecked();
		routing.invertRight = m_right->isChecked();
		validate();
	};
	connect(m_output, &QComboBox::currentIndexChanged, this, outputChanged);
	for (auto *box : {m_outputEnabled, m_swap, m_left, m_right})
		connect(box, &QCheckBox::toggled, this, outputChanged);
	connect(m_sends, &QListWidget::currentRowChanged, this, [this] { selectSend(); });
	connect(m_add, &QPushButton::clicked, this, [this] {
		if (m_track < 0)
			return;
		auto &sends = m_draft.tracks[m_track].routing.sends;
		if (sends.size() >= AudioSessionSendLimit)
			return;
		for (int i = 0; i < m_target->count(); ++i) {
			const auto id = m_target->itemData(i).toString();
			if (std::none_of(sends.cbegin(), sends.cend(), [&](const auto &send) { return send.targetId == id; })) {
				sends.append({id, -12, 0, false, false});
				refreshSends(int(sends.size() - 1));
				return;
			}
		}
	});
	connect(m_remove, &QPushButton::clicked, this, [this] {
		const int row = m_sends->currentRow();
		if (m_track < 0 || row < 0)
			return;
		m_draft.tracks[m_track].routing.sends.removeAt(row);
		refreshSends(row);
	});
	for (auto *combo : {m_target, m_tap})
		connect(combo, &QComboBox::currentIndexChanged, this, [this] { updateSend(); });
	for (auto *box : {m_gain, m_pan})
		connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updateSend(); });
	connect(m_enabled, &QCheckBox::toggled, this, [this] { updateSend(); });
	refreshSends(0);
	if (m_track < 0)
		body->setEnabled(false);
}
void AudioRoutingDialog::destinations(QComboBox *combo)
{
	combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	combo->setMinimumContentsLength(18);
	combo->addItem(tr("Master"), QString());
	for (int i = 0; i < m_draft.tracks.size(); ++i) {
		const auto &track = m_draft.tracks[i];
		if (!track.routing.bus || i == m_track)
			continue;
		auto name = track.name;
		if (std::count_if(m_draft.tracks.cbegin(), m_draft.tracks.cend(),
		                  [&](const auto &other) { return other.routing.bus && other.name == name; }) > 1)
			name += QStringLiteral(" · ") + track.id.left(8);
		combo->addItem(name, track.id);
		combo->setItemData(combo->count() - 1, track.id, Qt::ToolTipRole);
	}
}
void AudioRoutingDialog::validate()
{
	const auto error =
	    m_track < 0 ? tr("The selected track no longer exists.") : validateAudioSessionStructure(m_draft);
	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty());
	m_status->setText(error.isEmpty() ? tr("Routing is valid.") : error);
	if (m_track >= 0) {
		const auto &sends = m_draft.tracks[m_track].routing.sends;
		m_add->setEnabled(sends.size() < AudioSessionSendLimit && sends.size() < m_target->count());
	}
	m_remove->setEnabled(m_sends->currentRow() >= 0);
}
void AudioRoutingDialog::refreshSends(int selection)
{
	{
		const QSignalBlocker guard(m_sends);
		m_sends->clear();
		if (m_track >= 0)
			for (const auto &send : m_draft.tracks[m_track].routing.sends) {
				const auto name = m_target->itemText(m_target->findData(send.targetId));
				m_sends->addItem(tr("%1 · %2 dB · %3 · %4")
				                     .arg(name, locale().toString(send.gainDb, 'f', 2),
				                          send.preFader ? tr("Before fader") : tr("After fader"),
				                          send.enabled ? tr("Enabled") : tr("Disabled")));
			}
		m_sends->setCurrentRow(m_sends->count() ? std::clamp(selection, 0, m_sends->count() - 1) : -1);
	}
	selectSend();
	validate();
}
void AudioRoutingDialog::selectSend()
{
	const int row = m_sends->currentRow();
	m_sendForm->setEnabled(row >= 0);
	m_remove->setEnabled(row >= 0);
	if (row < 0 || m_track < 0)
		return;
	m_updating = true;
	const auto &send = m_draft.tracks[m_track].routing.sends[row];
	m_target->setCurrentIndex(m_target->findData(send.targetId));
	m_tap->setCurrentIndex(send.preFader ? 1 : 0);
	m_gain->setValue(send.gainDb);
	m_pan->setValue(send.pan);
	m_enabled->setChecked(send.enabled);
	m_updating = false;
}
void AudioRoutingDialog::updateSend()
{
	const int row = m_sends->currentRow();
	if (m_updating || m_track < 0 || row < 0)
		return;
	m_draft.tracks[m_track].routing.sends[row] = {m_target->currentData().toString(), m_gain->value(), m_pan->value(),
	                                              m_tap->currentData().toBool(), m_enabled->isChecked()};
	refreshSends(row);
}
AudioSessionEdit AudioRoutingDialog::edit() const
{
	AudioSessionEdit result;
	result.operation = "routing";
	if (m_track >= 0) {
		result.trackId = m_draft.tracks[m_track].id;
		result.routing = m_draft.tracks[m_track].routing;
	}
	return result;
}
} // namespace vibestudio
