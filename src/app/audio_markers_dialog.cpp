#include "app/audio_markers_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
// Only the active table cell owns an editor; 256 cues do not create 256 controls.
class CueDelegate final : public QStyledItemDelegate
{
public:
	CueDelegate(int lastFrame, QObject* parent) : QStyledItemDelegate(parent), m_lastFrame(lastFrame) {}
	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override
	{
		if (index.column() == 1) {
			auto* field = new QSpinBox(parent);
			field->setRange(0, m_lastFrame);
			field->setAccessibleName(AudioMarkersDialog::tr("Cue frame"));
			field->setKeyboardTracking(false);
			return field;
		}
		auto* field = new QLineEdit(parent);
		field->setMaxLength(AudioCueNameLimit);
		field->setAccessibleName(AudioMarkersDialog::tr("Cue name"));
		return field;
	}

private:
	int m_lastFrame;
};
} // namespace

AudioMarkersDialog::AudioMarkersDialog(const AudioMarkers& initial, qint64 frames, qint64 selectionFirst,
                                       qint64 selectionEnd, QWidget* parent)
    : QDialog(parent), m_frames(frames)
{
	setObjectName(QStringLiteral("audioMarkersDialog"));
	setWindowTitle(tr("Cue Markers and Loop"));
	setAccessibleName(tr("Audio cue and loop authoring"));
	setWindowModality(Qt::WindowModal);
	if (parent) {
		setLayoutDirection(parent->layoutDirection());
	}
	auto* outer = new QVBoxLayout(this);
	auto* scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("audioMarkersBody"));
	scroll->setAccessibleName(tr("Cue and loop controls"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body);
	outer->addWidget(scroll, 1);
	m_loop = new QCheckBox(tr("Forward loop"));
	m_loop->setObjectName(QStringLiteral("audioMarkersLoop"));
	m_loop->setAccessibleDescription(tr("One forward loop that repeats indefinitely in supporting players."));
	m_loop->setChecked(initial.loop.has_value());
	m_loop->setEnabled(frames > 0);
	layout->addWidget(m_loop);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_first = new QSpinBox;
	m_first->setObjectName(QStringLiteral("audioMarkersFirst"));
	m_first->setRange(0, int(std::max<qint64>(0, frames - 1)));
	m_first->setAccessibleName(tr("Loop start frame"));
	m_first->setValue(
	    int(initial.loop ? initial.loop->first : std::min(selectionFirst, std::max<qint64>(0, frames - 1))));
	m_end = new QSpinBox;
	m_end->setObjectName(QStringLiteral("audioMarkersEnd"));
	m_end->setRange(frames > 0 ? 1 : 0, int(frames));
	m_end->setAccessibleName(tr("Loop end frame, exclusive"));
	m_end->setToolTip(tr("The sample at this frame is outside the loop."));
	m_end->setValue(int(initial.loop ? initial.loop->end : selectionEnd > selectionFirst ? selectionEnd : frames));
	form->addRow(tr("Start frame"), m_first);
	form->addRow(tr("End frame (exclusive)"), m_end);
	layout->addLayout(form);
	auto* useSelection = new QPushButton(tr("Use Selection for Loop"));
	useSelection->setAutoDefault(false);
	useSelection->setObjectName(QStringLiteral("audioMarkersUseSelection"));
	useSelection->setEnabled(selectionEnd > selectionFirst);
	connect(useSelection, &QPushButton::clicked, this, [=, this]() {
		m_first->setValue(int(selectionFirst));
		m_end->setValue(int(selectionEnd));
		m_loop->setChecked(true);
	});
	layout->addWidget(useSelection, 0, Qt::AlignLeading);
	m_cues = new QTableWidget(0, 2);
	m_cues->setObjectName(QStringLiteral("audioMarkersCues"));
	m_cues->setAccessibleName(tr("Cue markers"));
	m_cues->setAccessibleDescription(tr("Editable cue names and exact frames. F2 edits the selected cell."));
	m_cues->setHorizontalHeaderLabels({tr("Name"), tr("Frame")});
	m_cues->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_cues->setSelectionMode(QAbstractItemView::SingleSelection);
	m_cues->setItemDelegate(new CueDelegate(int(std::max<qint64>(0, frames - 1)), m_cues));
	m_cues->setAlternatingRowColors(true);
	m_cues->verticalHeader()->hide();
	m_cues->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_cues->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_cues->setMinimumHeight(fontMetrics().height() * 8);
	for (const auto& cue : initial.cues) {
		appendCue(cue);
	}
	layout->addWidget(m_cues, 1);
	auto* cueButtons = new QHBoxLayout;
	m_add = new QPushButton(tr("Add Cue"));
	m_add->setAccessibleName(tr("Add cue at selection start"));
	m_add->setToolTip(tr("Add a cue at the selection start frame."));
	m_add->setAutoDefault(false);
	m_add->setObjectName(QStringLiteral("audioMarkersAdd"));
	m_canAdd = frames > 0 && selectionFirst < frames;
	m_remove = new QPushButton(tr("Remove Cue"));
	m_remove->setAutoDefault(false);
	m_remove->setObjectName(QStringLiteral("audioMarkersRemove"));
	cueButtons->addWidget(m_add);
	cueButtons->addWidget(m_remove);
	cueButtons->addStretch();
	layout->addLayout(cueButtons);
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("audioMarkersStatus"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Marker validation status"));
	layout->addWidget(m_status);
	connect(m_add, &QPushButton::clicked, this, [=, this]() {
		if (m_cues->rowCount() >= AudioCueLimit || selectionFirst >= frames) {
			return;
		}
		QSet<quint32> ids;
		for (const auto& cue : markers().cues) {
			ids.insert(cue.id);
		}
		quint32 id = 1;
		while (ids.contains(id)) {
			++id;
		}
		appendCue({id, selectionFirst, tr("Cue %1").arg(id)});
		m_cues->setCurrentCell(m_cues->rowCount() - 1, 0,
		                       QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		refreshControls();
	});
	connect(m_remove, &QPushButton::clicked, this, [this]() {
		if (m_cues->currentRow() >= 0) {
			m_cues->removeRow(m_cues->currentRow());
		}
		refreshControls();
	});
	connect(m_loop, &QCheckBox::toggled, this, [this]() { refreshControls(); });
	connect(m_cues, &QTableWidget::itemSelectionChanged, this, &AudioMarkersDialog::refreshControls);
	const auto clearStatus = [this]() {
		m_status->clear();
		m_status->setAccessibleDescription({});
	};
	connect(m_cues, &QTableWidget::itemChanged, this, clearStatus);
	connect(m_cues->model(), &QAbstractItemModel::rowsRemoved, this, clearStatus);
	connect(m_loop, &QCheckBox::toggled, this, clearStatus);
	connect(m_first, &QSpinBox::valueChanged, this, clearStatus);
	connect(m_end, &QSpinBox::valueChanged, this, clearStatus);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &AudioMarkersDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &AudioMarkersDialog::reject);
	outer->addWidget(buttons);
	if (m_cues->rowCount() > 0) {
		m_cues->setCurrentCell(0, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
	}
	refreshControls();
	const QSize available = screen() ? screen()->availableGeometry().size() : QSize(1000, 800);
	resize(std::min(std::max(560, fontMetrics().height() * 34), std::max(360, available.width() - 40)),
	       std::min(std::max(440, fontMetrics().height() * 25), std::max(280, available.height() - 80)));
}

void AudioMarkersDialog::appendCue(const AudioCue& cue)
{
	const int row = m_cues->rowCount();
	m_cues->insertRow(row);
	auto* name = new QTableWidgetItem(cue.name);
	name->setData(Qt::UserRole, cue.id);
	m_cues->setItem(row, 0, name);
	auto* frame = new QTableWidgetItem;
	frame->setData(Qt::EditRole, int(cue.frame));
	frame->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_cues->setItem(row, 1, frame);
	m_cues->resizeRowToContents(row);
}

AudioMarkers AudioMarkersDialog::markers() const
{
	AudioMarkers result;
	for (int row = 0; row < m_cues->rowCount(); ++row) {
		result.cues.append({m_cues->item(row, 0)->data(Qt::UserRole).toUInt(),
		                    m_cues->item(row, 1)->data(Qt::EditRole).toLongLong(), m_cues->item(row, 0)->text()});
	}
	if (m_loop->isChecked()) {
		result.loop = AudioLoop{m_first->value(), m_end->value()};
	}
	std::sort(result.cues.begin(), result.cues.end(), [](const AudioCue& a, const AudioCue& b) {
		return a.frame == b.frame ? a.id < b.id : a.frame < b.frame;
	});
	return result;
}

void AudioMarkersDialog::refreshControls()
{
	m_first->setEnabled(m_loop->isChecked());
	m_end->setEnabled(m_loop->isChecked());
	m_remove->setEnabled(m_cues->currentRow() >= 0);
	m_add->setEnabled(m_canAdd && m_cues->rowCount() < AudioCueLimit);
}

void AudioMarkersDialog::accept()
{
	// Finish an active cell edit before validating the pending table model.
	m_cues->setFocus(Qt::OtherFocusReason);
	const QString error = validateAudioMarkers(markers(), m_frames);
	if (!error.isEmpty()) {
		m_status->setText(error);
		m_status->setAccessibleDescription(error);
		return;
	}
	QDialog::accept();
}
} // namespace vibestudio
