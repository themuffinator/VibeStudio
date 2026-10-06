#include "app/audio_range_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
AudioRangeDialog::AudioRangeDialog(const AudioSession &session, const QStringList &tracks, qint64 first, qint64 end,
                                   QWidget *parent)
    : QDialog(parent), m_session(session)
{
	setObjectName("audioRangeDialog");
	setWindowTitle(tr("Edit Time Range"));
	setAccessibleName(windowTitle());
	resize(600, 620);
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Time-range edit controls"));
	auto *body = new QWidget;
	auto *form = new QFormLayout(body);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(body);
	layout->addWidget(scroll, 1);
	m_operation = new QComboBox;
	m_operation->setObjectName("rangeOperation");
	m_operation->setAccessibleName(tr("Range operation"));
	m_operation->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_operation->setMinimumContentsLength(12);
	m_operation->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_operation->addItem(tr("Clear clips, leave gap"), "clear");
	m_operation->addItem(tr("Delete range, close gap"), "ripple-delete");
	m_operation->addItem(tr("Insert silence at start"), "insert-silence");
	m_operation->addItem(tr("Repeat range after end"), "repeat");
	m_operation->setToolTip(
	    tr("Insert and repeat shift later material by the range length. Tempo and meter markers stay in place."));
	form->addRow(tr("Operation"), m_operation);
	const auto frameBox = [&](const QString &label, const QString &id, qint64 value) {
		auto *box = new QDoubleSpinBox;
		box->setObjectName(id);
		box->setAccessibleName(label);
		box->setDecimals(0);
		box->setRange(0, double(AudioSessionFrameLimit));
		box->setValue(double(value));
		box->setKeyboardTracking(false);
		form->addRow(label, box);
		return box;
	};
	m_first = frameBox(tr("Start frame"), "rangeFirst", first);
	m_end = frameBox(tr("End frame (exclusive)"), "rangeEnd", end);
	m_all = new QCheckBox(tr("Enabled"));
	m_all->setObjectName("rangeAllTracks");
	m_all->setAccessibleName(tr("All tracks"));
	m_all->setChecked(tracks.isEmpty());
	form->addRow(tr("All tracks"), m_all);
	m_tracks = new QListWidget;
	m_tracks->setObjectName("rangeTracks");
	m_tracks->setAccessibleName(tr("Tracks included in the range edit"));
	m_tracks->setToolTip(
	    tr("Check tracks to edit. Clip group links do not add unchecked tracks. Buses can carry automation."));
	m_tracks->setMinimumHeight(fontMetrics().height() * 5 + 12);
	m_tracks->setMinimumWidth(fontMetrics().horizontalAdvance("MMMMMMMMMMMM") + 24);
	m_tracks->setTextElideMode(Qt::ElideRight);
	for (const auto &track : session.tracks) {
		const auto label = track.routing.bus ? tr("Bus: %1").arg(track.name) : track.name;
		auto *item = new QListWidgetItem(label, m_tracks);
		item->setToolTip(label);
		item->setData(Qt::UserRole, track.id);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(tracks.contains(track.id) ? Qt::Checked : Qt::Unchecked);
	}
	form->addRow(tr("Included tracks"), m_tracks);
	m_follow = new QCheckBox(tr("Enabled"));
	m_follow->setObjectName("rangeFollowAutomation");
	m_follow->setAccessibleName(tr("Follow track automation"));
	m_follow->setToolTip(
	    tr("Move gain, balance and effect curves with time edits. Inserted silence holds the boundary value."));
	m_follow->setChecked(true);
	form->addRow(tr("Follow track automation"), m_follow);
	m_master = new QCheckBox(tr("Enabled"));
	m_master->setObjectName("rangeMasterAutomation");
	m_master->setAccessibleName(tr("Follow master automation"));
	m_master->setToolTip(tr("Apply the same time edit to master effect curves. Available when all tracks and "
	                        "automation following are enabled."));
	m_master->setChecked(true);
	form->addRow(tr("Follow master automation"), m_master);
	m_summary = new QLabel;
	m_summary->setObjectName("rangeSummary");
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setAccessibleName(tr("Range edit summary"));
	form->addRow(m_summary);
	m_error = new QLabel;
	m_error->setObjectName("rangeError");
	m_error->setWordWrap(true);
	m_error->setTextFormat(Qt::PlainText);
	m_error->setAccessibleName(tr("Range validation"));
	form->addRow(m_error);
	for (int row = 0; row < form->rowCount(); ++row)
		if (auto *item = form->itemAt(row, QFormLayout::LabelRole))
			if (auto *label = qobject_cast<QLabel *>(item->widget()))
				label->setWordWrap(true);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &AudioRangeDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_operation, &QComboBox::currentIndexChanged, this, &AudioRangeDialog::refresh);
	for (auto *box : {m_all, m_follow, m_master})
		connect(box, &QCheckBox::toggled, this, &AudioRangeDialog::refresh);
	for (auto *box : {m_first, m_end})
		connect(box, &QDoubleSpinBox::valueChanged, this, &AudioRangeDialog::refresh);
	connect(m_tracks, &QListWidget::itemChanged, this, &AudioRangeDialog::refresh);
	refresh();
}
AudioRangeEdit AudioRangeDialog::edit() const
{
	AudioRangeEdit result;
	result.operation = m_operation->currentData().toString();
	result.allTracks = m_all->isChecked();
	if (!result.allTracks)
		for (int i = 0; i < m_tracks->count(); ++i)
			if (m_tracks->item(i)->checkState() == Qt::Checked)
				result.trackIds.append(m_tracks->item(i)->data(Qt::UserRole).toString());
	result.first = qint64(m_first->value());
	result.end = qint64(m_end->value());
	result.followAutomation = result.operation != "clear" && m_follow->isChecked();
	result.masterAutomation = result.allTracks && result.followAutomation && m_master->isChecked();
	return result;
}
void AudioRangeDialog::refresh()
{
	const auto request = edit();
	m_tracks->setEnabled(!request.allTracks);
	m_follow->setEnabled(request.operation != "clear");
	m_master->setEnabled(request.allTracks && request.followAutomation);
	m_summary->setText(tr("%1 tracks · %2 frames · Tempo and meter unchanged")
	                       .arg(request.allTracks ? m_session.tracks.size() : request.trackIds.size())
	                       .arg(request.end - request.first));
	m_summary->setAccessibleDescription(m_summary->text());
	m_error->clear();
}
void AudioRangeDialog::accept()
{
	try {
		const auto changed = editAudioRange(m_session, edit());
		if (!changed.succeeded()) {
			m_error->setText(changed.error);
			m_error->setAccessibleDescription(changed.error);
			return;
		}
		QDialog::accept();
	} catch (const std::bad_alloc &) {
		m_error->setText(tr("Insufficient memory for the edit. The session is unchanged."));
	}
}
} // namespace vibestudio
