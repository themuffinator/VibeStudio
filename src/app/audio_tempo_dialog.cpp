#include "app/audio_tempo_dialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
AudioTempoDialog::AudioTempoDialog(const AudioTempoMap &map, int sampleRate, qint64 cursor, QWidget *parent)
    : QDialog(parent), m_map(map), m_rate(sampleRate)
{
	setObjectName("audioTempoDialog");
	setWindowTitle(tr("Tempo and Meter"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(
	    tr("Edit tempo and time signatures. Audio clips and automation keep their sample positions."));
	resize(760, 580);
	auto *outer = new QVBoxLayout(this);
	auto *tabs = new QTabWidget;
	tabs->setAccessibleName(tr("Tempo and meter maps"));
	outer->addWidget(tabs, 1);
	const auto page = [tabs](const QString &name) {
		auto *scroll = new QScrollArea;
		scroll->setWidgetResizable(true);
		auto *content = new QWidget;
		scroll->setWidget(content);
		tabs->addTab(scroll, name);
		return new QVBoxLayout(content);
	};
	auto *tempo = page(tr("Tempo"));
	m_tempos = new QTreeWidget;
	m_tempos->setObjectName("tempoChanges");
	m_tempos->setAccessibleName(tr("Tempo changes"));
	m_tempos->setHeaderLabels({tr("Position"), tr("BPM"), tr("Frame")});
	m_tempos->setRootIsDecorated(false);
	m_tempos->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tempos->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_tempos->header()->setStretchLastSection(true);
	tempo->addWidget(m_tempos, 1);
	auto *tempoForm = new QFormLayout;
	tempoForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_position = new QLineEdit;
	m_position->setObjectName("tempoPosition");
	m_position->setAccessibleName(tr("Tempo change position, bar.beat.tick"));
	m_position->setToolTip(tr("Bars and beats start at 1; ticks start at 0. A quarter note has 960 ticks. Beats follow "
	                          "the time signature's denominator."));
	m_position->setLayoutDirection(Qt::LeftToRight);
	tempoForm->addRow(tr("Position (bar.beat.tick)"), m_position);
	m_bpm = new QDoubleSpinBox;
	m_bpm->setObjectName("tempoBpm");
	m_bpm->setAccessibleName(tr("Quarter-note tempo in beats per minute"));
	m_bpm->setRange(20, 400);
	m_bpm->setDecimals(6);
	m_bpm->setValue(map.tempo);
	tempoForm->addRow(tr("Tempo (BPM)"), m_bpm);
	tempo->addLayout(tempoForm);
	auto *tempoButtons = new QFormLayout;
	tempoButtons->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto *set = new QPushButton(tr("Set Tempo"));
	set->setAutoDefault(false);
	set->setObjectName("tempoSet");
	m_removeTempo = new QPushButton(tr("Remove Change"));
	m_removeTempo->setAutoDefault(false);
	m_removeTempo->setObjectName("tempoRemove");
	m_removeTempo->setAccessibleName(tr("Remove selected tempo change"));
	tempoButtons->addRow(set, m_removeTempo);
	tempo->addLayout(tempoButtons);
	connect(set, &QPushButton::clicked, this, &AudioTempoDialog::setTempo);
	connect(m_removeTempo, &QPushButton::clicked, this, [this] {
		const auto *item = m_tempos->currentItem();
		if (!item || item->data(0, Qt::UserRole).toInt() < 0)
			return;
		auto next = m_map;
		next.tempoChanges.removeAt(item->data(0, Qt::UserRole).toInt());
		replace(next);
	});
	connect(m_tempos, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
		m_removeTempo->setEnabled(item && item->data(0, Qt::UserRole).toInt() >= 0);
		if (!item)
			return;
		m_position->setText(item->text(0));
		const int index = item->data(0, Qt::UserRole).toInt();
		m_bpm->setValue(index < 0 ? m_map.tempo : m_map.tempoChanges[index].bpm);
	});
	auto *meter = page(tr("Meter"));
	m_meters = new QTreeWidget;
	m_meters->setObjectName("meterChanges");
	m_meters->setAccessibleName(tr("Time signature changes at bar starts"));
	m_meters->setHeaderLabels({tr("Bar"), tr("Time signature"), tr("Frame")});
	m_meters->setRootIsDecorated(false);
	m_meters->setSelectionMode(QAbstractItemView::SingleSelection);
	m_meters->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_meters->header()->setStretchLastSection(true);
	meter->addWidget(m_meters, 1);
	auto *meterForm = new QFormLayout;
	meterForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_bar = new QDoubleSpinBox;
	m_bar->setObjectName("meterBar");
	m_bar->setAccessibleName(tr("Meter change bar"));
	m_bar->setDecimals(0);
	m_bar->setRange(1, double(AudioMusicalTickLimit / 120 + 1));
	meterForm->addRow(tr("Start bar"), m_bar);
	m_beats = new QSpinBox;
	m_beats->setObjectName("meterBeats");
	m_beats->setAccessibleName(tr("Beats per bar"));
	m_beats->setRange(1, 32);
	m_beats->setValue(map.beatsPerBar);
	meterForm->addRow(tr("Beats per bar"), m_beats);
	m_unit = new QComboBox;
	m_unit->setObjectName("meterUnit");
	m_unit->setAccessibleName(tr("Time signature denominator"));
	for (const int unit : {1, 2, 4, 8, 16, 32})
		m_unit->addItem(QString::number(unit), unit);
	m_unit->setCurrentIndex(m_unit->findData(map.beatUnit));
	meterForm->addRow(tr("Beat unit"), m_unit);
	meter->addLayout(meterForm);
	auto *meterButtons = new QFormLayout;
	meterButtons->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto *setMeterButton = new QPushButton(tr("Set Meter"));
	setMeterButton->setAutoDefault(false);
	setMeterButton->setObjectName("meterSet");
	m_removeMeter = new QPushButton(tr("Remove Change"));
	m_removeMeter->setAutoDefault(false);
	m_removeMeter->setObjectName("meterRemove");
	m_removeMeter->setAccessibleName(tr("Remove selected meter change"));
	meterButtons->addRow(setMeterButton, m_removeMeter);
	meter->addLayout(meterButtons);
	connect(setMeterButton, &QPushButton::clicked, this, &AudioTempoDialog::setMeter);
	connect(m_removeMeter, &QPushButton::clicked, this, [this] {
		const auto *item = m_meters->currentItem();
		if (!item || item->data(0, Qt::UserRole).toInt() < 0)
			return;
		auto next = m_map;
		next.meterChanges.removeAt(item->data(0, Qt::UserRole).toInt());
		replace(next);
	});
	connect(m_meters, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
		m_removeMeter->setEnabled(item && item->data(0, Qt::UserRole).toInt() >= 0);
		if (!item)
			return;
		const int index = item->data(0, Qt::UserRole).toInt();
		const AudioMeterChange change =
		    index < 0 ? AudioMeterChange{1, m_map.beatsPerBar, m_map.beatUnit} : m_map.meterChanges[index];
		m_bar->setValue(double(change.bar));
		m_beats->setValue(change.beatsPerBar);
		m_unit->setCurrentIndex(m_unit->findData(change.beatUnit));
	});
	m_status = new QLabel;
	m_status->setObjectName("tempoMapStatus");
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Tempo map validation"));
	outer->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->setObjectName("tempoMapButtons");
	outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	for (auto *label : findChildren<QLabel *>())
		label->setWordWrap(true);
	refresh();
	m_position->setText(audioMusicalPositionText(m_time.positionAtFrame(cursor)));
	m_bar->setValue(double(std::max<qint64>(1, m_time.positionAtFrame(cursor).bar)));
	m_bpm->setValue(m_time.tempoAtFrame(cursor));
	const auto meterAtCursor = m_time.meterAtFrame(cursor);
	m_beats->setValue(meterAtCursor.beatsPerBar);
	m_unit->setCurrentIndex(m_unit->findData(meterAtCursor.beatUnit));
}
bool AudioTempoDialog::replace(const AudioTempoMap &map)
{
	const auto error = validateAudioTempoMap(map, m_rate);
	if (!error.isEmpty()) {
		m_status->setText(error);
		m_status->setAccessibleDescription(error);
		return false;
	}
	m_map = map;
	refresh();
	return true;
}
void AudioTempoDialog::refresh()
{
	m_time.prepare(m_map, m_rate);
	const QSignalBlocker tempoGuard(m_tempos), meterGuard(m_meters);
	m_tempos->clear();
	m_meters->clear();
	for (int i = -1; i < m_map.tempoChanges.size(); ++i) {
		const auto change = i < 0 ? AudioTempoChange{0, m_map.tempo} : m_map.tempoChanges[i];
		auto *item = new QTreeWidgetItem(m_tempos, {audioMusicalPositionText(m_time.positionAtTick(change.tick)),
		                                            locale().toString(change.bpm, 'g', 12),
		                                            QString::number(m_time.frameAtTick(change.tick))});
		item->setData(0, Qt::UserRole, i);
		item->setToolTip(0, tr("Quarter-note tick %1").arg(change.tick));
	}
	for (int i = -1; i < m_map.meterChanges.size(); ++i) {
		const auto change = i < 0 ? AudioMeterChange{1, m_map.beatsPerBar, m_map.beatUnit} : m_map.meterChanges[i];
		auto *item =
		    new QTreeWidgetItem(m_meters, {QString::number(change.bar),
		                                   QStringLiteral("%1/%2").arg(change.beatsPerBar).arg(change.beatUnit),
		                                   QString::number(m_time.frameAtPosition({change.bar, 1, 0}))});
		item->setData(0, Qt::UserRole, i);
	}
	m_removeTempo->setEnabled(false);
	m_removeMeter->setEnabled(false);
	m_status->setText(tr("Audio clips and automation keep their sample positions."));
	m_status->setAccessibleDescription(m_status->text());
}
void AudioTempoDialog::setTempo()
{
	AudioMusicalPosition position;
	const auto tick = parseAudioMusicalPosition(m_position->text(), &position) ? m_time.tickAtPosition(position) : -1;
	if (tick < 0 || m_time.frameAtTick(tick) < 0) {
		m_status->setText(tr("Enter a valid bar.beat.tick position for this meter within the session timeline."));
		m_status->setAccessibleDescription(m_status->text());
		m_position->setFocus();
		return;
	}
	auto next = m_map;
	if (tick == 0)
		next.tempo = m_bpm->value();
	else {
		auto it = std::lower_bound(next.tempoChanges.begin(), next.tempoChanges.end(), tick,
		                           [](const auto &change, qint64 t) { return change.tick < t; });
		if (it != next.tempoChanges.end() && it->tick == tick)
			it->bpm = m_bpm->value();
		else
			next.tempoChanges.insert(it, {tick, m_bpm->value()});
	}
	replace(next);
}
void AudioTempoDialog::setMeter()
{
	auto next = m_map;
	const auto bar = qint64(m_bar->value());
	const int beats = m_beats->value(), unit = m_unit->currentData().toInt();
	if (bar == 1) {
		next.beatsPerBar = beats;
		next.beatUnit = unit;
	} else {
		auto it = std::lower_bound(next.meterChanges.begin(), next.meterChanges.end(), bar,
		                           [](const auto &change, qint64 b) { return change.bar < b; });
		if (it != next.meterChanges.end() && it->bar == bar)
			*it = {bar, beats, unit};
		else
			next.meterChanges.insert(it, {bar, beats, unit});
	}
	replace(next);
}
} // namespace vibestudio
