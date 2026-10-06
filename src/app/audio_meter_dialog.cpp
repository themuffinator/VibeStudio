#include "app/audio_meter_dialog.h"
#include "app/audio_meter_widgets.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOptionProgressBar>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
AudioMeterDialog::AudioMeterDialog(QWidget *parent) : QDialog(parent)
{
	setObjectName("audioMeterDialog");
	setWindowTitle(tr("Session Meters"));
	setAccessibleName(windowTitle());
	resize(1080, 600);
	auto *outer = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setObjectName("meterScroll");
	scroll->setAccessibleName(tr("Meter readings and signal point"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *contents = new QWidget;
	auto *layout = new QVBoxLayout(contents);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSizeConstraint(QLayout::SetMinimumSize);
	scroll->setWidget(contents);
	outer->addWidget(scroll, 1);
	const auto label = [&](const QString &name) {
		auto *item = new QLabel;
		item->setTextFormat(Qt::PlainText);
		item->setWordWrap(true);
		item->setAccessibleName(name);
		layout->addWidget(item);
		return item;
	};
	m_range = label(tr("Analysis range"));
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_tap = new QComboBox;
	m_tap->setObjectName("meterTap");
	m_tap->setAccessibleName(tr("Meter signal point"));
	m_tap->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_tap->setMinimumContentsLength(10);
	m_tap->addItem(tr("After fader and inserts"));
	m_tap->addItem(tr("Before fader and inserts"));
	m_tap->setToolTip(tr("Pre-fader includes clip gain, fades and channel polarity/swap. Post-fader includes strip "
	                     "gain, pan and inserts. Master post is before audition volume and clipping."));
	form->addRow(tr("Signal point"), m_tap);
	layout->addLayout(form);
	m_status = label(tr("Meter status"));
	m_status->setText(tr("Stopped. Play a range or analyze it offline."));
	m_table = new QTreeWidget;
	m_table->setObjectName("meterTable");
	m_table->setAccessibleName(tr("Track, bus and master meters"));
	m_table->setAccessibleDescription(
	    tr("Levels are in dBFS. Over-range counts samples strictly above full scale. Correlation ranges from −1 for "
	       "opposite phase to +1 for matching phase; a silent channel has no correlation reading."));
	m_table->setRootIsDecorated(false);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setAlternatingRowColors(true);
	m_table->setMinimumHeight(m_table->fontMetrics().height() * 7 + 30);
	m_table->setHeaderLabels({tr("Strip"), tr("Peak L"), tr("Peak R"), tr("RMS L"), tr("RMS R"), tr("Max L / R"),
	                          tr("Over L / R"), tr("Correlation")});
	m_table->header()->setStretchLastSection(false);
	m_table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->setItemDelegate(new AudioMeterDelegate(m_table));
	layout->addWidget(m_table, 1);
	m_detail = label(tr("Selected meter details"));
	m_detail->setToolTip(tr("Sample peaks; RMS time constant: 300 ms; peak decay: 24 dB/s."));
	auto *controls = new QFormLayout;
	controls->setRowWrapPolicy(QFormLayout::WrapLongRows);
	controls->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_analyze = new QPushButton(tr("Analyze Range"));
	m_analyze->setObjectName("meterAnalyze");
	m_reset = new QPushButton(tr("Reset Meters"));
	m_reset->setObjectName("meterReset");
	m_cancel = new QPushButton(tr("Cancel Analysis"));
	m_cancel->setObjectName("meterCancel");
	for (auto *button : {m_analyze, m_reset, m_cancel}) {
		button->setAccessibleName(button->text());
		button->setAutoDefault(false);
	}
	controls->addRow(m_analyze, m_reset);
	controls->addRow(m_cancel);
	outer->addLayout(controls);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
	outer->addWidget(buttons);
	connect(m_tap, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
	connect(m_table, &QTreeWidget::currentItemChanged, this, [this] { refresh(); });
	connect(m_analyze, &QPushButton::clicked, this, &AudioMeterDialog::analyzeRequested);
	connect(m_cancel, &QPushButton::clicked, this, &AudioMeterDialog::cancelRequested);
	connect(m_reset, &QPushButton::clicked, this, [this] {
		if (m_offline) {
			m_meters = {};
			m_offline = false;
			m_status->setText(tr("Meters reset."));
			refresh();
		} else
			Q_EMIT resetRequested();
	});
	setBusy(false);
}
void AudioMeterDialog::setContext(const AudioSession &session, quint64 revision, qint64 first, qint64 end)
{
	if (!m_hasContext || revision != m_revision) {
		m_hasContext = true;
		m_revision = revision;
		m_meters = {};
		m_offline = false;
		m_table->clear();
		for (const auto &track : session.tracks) {
			auto *item = new QTreeWidgetItem(m_table, {track.routing.bus ? tr("Bus: %1").arg(track.name) : track.name});
			item->setData(0, Qt::UserRole, track.id);
		}
		new QTreeWidgetItem(m_table, {tr("Master")});
		m_table->setCurrentItem(m_table->topLevelItem(0));
	}
	m_first = first;
	m_end = end;
	m_range->setText(tr("Analysis range: frames %1–%2 · %3 Hz").arg(first).arg(end).arg(session.sampleRate));
	m_analyze->setEnabled(!m_busy && m_end > m_first);
	refresh();
}
void AudioMeterDialog::setBusy(bool busy, bool analysis)
{
	m_busy = busy;
	m_analyze->setEnabled(!busy && m_end > m_first);
	m_reset->setEnabled(!busy);
	m_cancel->setVisible(busy && analysis);
	if (busy && analysis)
		m_status->setText(tr("Analyzing range…"));
}
void AudioMeterDialog::setPlayback(const AudioSessionPlaybackSnapshot &snapshot)
{
	using State = AudioSessionPlaybackSnapshot::State;
	if (m_busy)
		return;
	if (snapshot.state == State::Preparing || snapshot.state == State::Playing)
		m_offline = false;
	if (m_offline)
		return;
	m_meters = snapshot.meters;
	m_status->setText(snapshot.state == State::Error       ? tr("Playback failed: %1").arg(snapshot.error)
	                  : snapshot.state == State::Preparing ? tr("Preparing playback…")
	                  : snapshot.state == State::Playing
	                      ? tr("Live render meters · Buffered audio can lead the audible cursor.")
	                  : snapshot.state == State::Paused ? tr("Paused · Readings retained.")
	                  : snapshot.state == State::Ended  ? tr("Range finished · Readings retained.")
	                                                    : tr("Stopped. Play a range or analyze it offline."));
	refresh();
}
void AudioMeterDialog::setReport(const AudioMeterReport &report)
{
	setBusy(false);
	m_offline = true;
	m_meters = report.succeeded() ? report.meters : AudioMeterSnapshot{};
	m_status->setText(report.cancelled ? tr("Analysis cancelled.")
	                  : !report.succeeded()
	                      ? tr("Analysis failed: %1").arg(report.error)
	                      : tr("Range analysis: frames %1–%2 · RMS and correlation cover the entire range.")
	                            .arg(report.first)
	                            .arg(report.end));
	refresh();
}
void AudioMeterDialog::refresh()
{
	const bool post = m_tap->currentIndex() == 0;
	for (int i = 0; i < m_table->topLevelItemCount(); ++i) {
		auto *item = m_table->topLevelItem(i);
		const auto &strip = m_meters.strips[size_t(std::min(i, AudioMeterStripLimit - 1))];
		const auto &reading = post ? strip.post : strip.pre;
		const bool valid = i < m_meters.count && reading.valid && reading.frames;
		if (valid) {
			for (size_t c = 0; c < 2; ++c) {
				const double peak = m_offline ? reading.maximum[c] : reading.peak[c];
				const double rms = m_offline ? reading.integratedRms[c] : reading.rms[c];
				item->setText(int(c) + 1, audioMeterDb(peak));
				item->setText(int(c) + 3, audioMeterDb(rms));
				item->setData(int(c) + 1, AudioMeterLevelRole, peak > 0 ? 20 * std::log10(peak) : -72.0);
				item->setData(int(c) + 3, AudioMeterLevelRole, rms > 0 ? 20 * std::log10(rms) : -72.0);
			}
			item->setText(5, tr("%1 / %2").arg(audioMeterDb(reading.maximum[0]), audioMeterDb(reading.maximum[1])));
			item->setText(6, tr("%1 / %2").arg(reading.samplesAboveFullScale[0]).arg(reading.samplesAboveFullScale[1]));
			if (m_offline ? reading.integratedCorrelationValid : reading.correlationValid)
				item->setText(7, audioMeterNumeric(QString::number(
				                     m_offline ? reading.integratedCorrelation : reading.correlation, 'f', 3)));
			else
				item->setText(7, tr("—"));
		} else {
			for (int col = 1; col < 8; ++col) {
				item->setText(col, tr("—"));
				item->setData(col, AudioMeterLevelRole, {});
			}
		}
		const auto detail =
		    !reading.valid ? tr("Invalid signal")
		    : valid ? tr("%1 measured frames · Last logical frame: %2").arg(reading.frames).arg(reading.lastFrame)
		            : tr("No measured frames");
		for (int col = 0; col < 8; ++col) {
			item->setToolTip(col, detail + '\n' + m_detail->toolTip());
			item->setData(col, Qt::AccessibleTextRole, item->text(col));
		}
		if (item == m_table->currentItem())
			m_detail->setText(detail);
	}
}
} // namespace vibestudio
