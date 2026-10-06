#include "app/audio_recording_meters.h"
#include "app/audio_meter_widgets.h"
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
AudioRecordingMeters::AudioRecordingMeters(QWidget *parent) : QWidget(parent)
{
	setObjectName("recordingMeters");
	setAccessibleName(tr("Recording input and output meters"));
	auto *layout = new QVBoxLayout(this);
	m_status = new QLabel;
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Recording meter status"));
	layout->addWidget(m_status);
	m_table = new QTreeWidget;
	m_table->setObjectName("recordingMeterTable");
	m_table->setAccessibleName(tr("Dry input and device output levels"));
	m_table->setAccessibleDescription(tr("Sample peaks and RMS in dBFS. Headroom is measured from the held maximum. "
	                                     "Over counts samples strictly above full scale. Mono inputs have one row. "
	                                     "Input includes preroll; output is before clipping."));
	m_table->setHeaderLabels({tr("Signal"), tr("Peak dBFS"), tr("RMS dBFS"), tr("Max dBFS"), tr("Headroom dB"),
	                          tr("Over samples"), tr("State")});
	m_table->setRootIsDecorated(false);
	m_table->setAlternatingRowColors(true);
	m_table->setMinimumHeight(fontMetrics().height() * 7 + 30);
	m_table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->header()->setStretchLastSection(false);
	m_table->setItemDelegate(new AudioMeterDelegate(m_table));
	layout->addWidget(m_table, 1);
	m_detail = new QLabel;
	m_detail->setObjectName("recordingMeterDetail");
	m_detail->setAccessibleName(tr("Selected recording meter details"));
	m_detail->setTextFormat(Qt::PlainText);
	m_detail->setWordWrap(true);
	layout->addWidget(m_detail);
	m_reset = new QPushButton(tr("Reset Meter History"));
	m_reset->setObjectName("recordingMeterReset");
	m_reset->setAccessibleName(m_reset->text());
	m_reset->setToolTip(tr("Clear level history, held maxima and over-range counts. Recorded samples, timing and "
	                       "monitoring are unchanged."));
	m_reset->setAutoDefault(false);
	layout->addWidget(m_reset);
	connect(m_reset, &QPushButton::clicked, this, &AudioRecordingMeters::resetRequested);
	connect(m_table, &QTreeWidget::currentItemChanged, this, [this] { refresh(); });
	refresh();
}
void AudioRecordingMeters::setPass(const AudioSession &session, const AudioRecordingPlan &plan)
{
	m_rows.clear();
	m_table->clear();
	m_name = plan.name;
	m_snapshot = {};
	for (int arm = 0; arm < plan.pass.arms.size() && arm < AudioDuplexArmLimit; ++arm) {
		const auto &mapping = plan.pass.arms[arm];
		const auto track = std::find_if(session.tracks.cbegin(), session.tracks.cend(),
		                                [&](const auto &value) { return value.id == mapping.trackId; });
		const auto name = track == session.tracks.cend() ? mapping.trackId : track->name;
		for (int channel = 0; channel < mapping.channels && channel < 2; ++channel) {
			new QTreeWidgetItem(m_table, {tr("%1 · Input %2").arg(name).arg(mapping.channelMap[size_t(channel)] + 1)});
			m_rows << Row{arm, channel};
		}
	}
	for (int channel = 0; channel < 2; ++channel) {
		new QTreeWidgetItem(m_table, {channel ? tr("Device output R") : tr("Device output L")});
		m_rows << Row{-1, channel};
	}
	m_table->setCurrentItem(m_table->topLevelItem(0));
	refresh();
}
void AudioRecordingMeters::setSnapshot(const AudioRecordingSnapshot &snapshot, bool resetPending)
{
	m_snapshot = snapshot;
	m_resetPending = resetPending;
	refresh();
}
void AudioRecordingMeters::refresh()
{
	using State = AudioRecordingSnapshot::State;
	const bool live = m_snapshot.state == State::Recording || m_snapshot.state == State::Draining;
	m_status->setText(m_resetPending     ? tr("Reset requested; waiting for the recording worker…")
	                  : m_rows.isEmpty() ? tr("Record a pass to view its dry inputs and device output.")
	                  : live             ? tr("Live levels · %1").arg(m_name)
	                                     : tr("Last levels · %1").arg(m_name));
	bool measured = false;
	QString detail;
	for (int row = 0; row < m_rows.size(); ++row) {
		const auto [arm, channel] = m_rows[row];
		const auto &meter = arm < 0 ? m_snapshot.levels.output : m_snapshot.levels.input[size_t(arm)];
		const auto c = size_t(channel);
		const bool valid =
		    !m_resetPending && meter.valid && meter.frames &&
		    (arm < 0 || (arm < m_snapshot.levels.count && channel < m_snapshot.levels.channels[size_t(arm)]));
		auto *item = m_table->topLevelItem(row);
		QString state;
		if (valid) {
			measured = true;
			item->setText(1, audioMeterDb(meter.peak[c]));
			item->setText(2, audioMeterDb(meter.rms[c]));
			item->setText(3, audioMeterDb(meter.maximum[c]));
			item->setText(4, audioMeterNumeric(meter.maximum[c] > 0
			                                       ? locale().toString(-20 * std::log10(meter.maximum[c]), 'f', 1)
			                                       : QStringLiteral("∞")));
			item->setText(5, locale().toString(meter.samplesAboveFullScale[c]));
			item->setData(1, AudioMeterLevelRole, meter.peak[c] > 0 ? 20 * std::log10(meter.peak[c]) : -72.0);
			item->setData(2, AudioMeterLevelRole, meter.rms[c] > 0 ? 20 * std::log10(meter.rms[c]) : -72.0);
			state = meter.samplesAboveFullScale[c] ? (arm < 0 ? tr("Output clipped") : tr("Over full scale"))
			        : meter.maximum[c] >= 1        ? tr("Full scale reached")
			                                       : tr("Within range");
		} else {
			for (int col = 1; col < 6; ++col) {
				item->setText(col, tr("—"));
				item->setData(col, AudioMeterLevelRole, {});
			}
			state = !meter.valid ? tr("Invalid signal") : tr("No measured frames");
		}
		item->setText(6, state);
		for (int col = 0; col < 7; ++col)
			item->setData(col, Qt::AccessibleTextRole, item->text(col));
		if (item == m_table->currentItem()) {
			detail = valid ? tr("%1\nPeak %2 dBFS · RMS %3 dBFS · Maximum %4 dBFS · Headroom %5 dB\n"
			                    "%6 over-range samples · %7 measured frames · %8")
			                     .arg(item->text(0), item->text(1), item->text(2), item->text(3), item->text(4),
			                          item->text(5), locale().toString(meter.frames), state)
			               : tr("%1 · %2").arg(item->text(0), state);
		}
	}
	m_detail->setText(detail);
	m_detail->setToolTip(
	    tr("RMS time constant: 300 ms. Peak decay: 24 dB/s. Maxima and counts persist until Reset or a new pass. "
	       "These are sample meters; analog clipping and true peaks require separate checks."));
	m_reset->setEnabled(!m_resetPending && measured);
}
} // namespace vibestudio
