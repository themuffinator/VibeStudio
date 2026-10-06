#include "app/audio_media_dialog.h"
#include "app/audio_waveform_view.h"
#include "core/studio_settings.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <atomic>

namespace vibestudio
{
struct AudioMediaReview {
	std::atomic_bool cancelled{false};
	AudioMediaInventory inventory;
	AudioMediaCandidate candidate;
	AudioMediaResult result;
	AudioWaveformData before, after;
	QString error;
	AudioWorkControl control()
	{
		return {[this] { return cancelled.load(); }};
	}
};
namespace
{
QString availability(const QString &id)
{
	if (id == "available")
		return AudioMediaDialog::tr("File available");
	if (id == "missing")
		return AudioMediaDialog::tr("File missing; embedded audio retained");
	if (id == "not-file")
		return AudioMediaDialog::tr("Reference is not a file; embedded audio retained");
	if (id == "unreadable")
		return AudioMediaDialog::tr("File unreadable; embedded audio retained");
	return AudioMediaDialog::tr("Embedded audio only");
}
} // namespace
AudioMediaDialog::AudioMediaDialog(const AudioSession &session, const QString &source, QWidget *parent)
    : QDialog(parent), m_session(session), m_initialSource(source)
{
	setObjectName("audioMediaDialog");
	setWindowTitle(tr("Session Media"));
	setAccessibleName(windowTitle());
	resize(800, 780);
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Session media controls"));
	m_fields = new QWidget;
	auto *form = new QFormLayout(m_fields);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(m_fields);
	layout->addWidget(scroll, 1);
	m_sources = new QTreeWidget;
	m_sources->setObjectName("mediaSources");
	m_sources->setAccessibleName(tr("Embedded sources and clip usage"));
	m_sources->setHeaderLabels({tr("Source"), tr("Channels"), tr("Frames"), tr("Clips"), tr("File reference")});
	m_sources->setRootIsDecorated(false);
	m_sources->setMinimumHeight(120);
	m_sources->setMaximumHeight(240);
	m_sources->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_sources->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_sources->header()->setStretchLastSection(false);
	form->addRow(m_sources);
	m_details = new QPlainTextEdit;
	m_details->setReadOnly(true);
	m_details->setObjectName("mediaDetails");
	m_details->setAccessibleName(tr("Selected source paths and usage"));
	m_details->setMaximumHeight(90);
	m_details->setMinimumHeight(60);
	m_details->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	form->addRow(m_details);
	m_operation = new QComboBox;
	m_operation->setObjectName("mediaOperation");
	m_operation->setAccessibleName(tr("Media operation"));
	m_operation->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_operation->setMinimumContentsLength(12);
	m_operation->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_operation->addItem(tr("Rename source"), "rename");
	m_operation->addItem(tr("Relink identical audio"), "relink");
	m_operation->addItem(tr("Replace audio in all clips"), "replace");
	m_operation->addItem(tr("Remove selected unused sources"), "remove");
	m_operation->addItem(tr("Prune all unused sources"), "prune");
	form->addRow(tr("Operation"), m_operation);
	m_name = new QLineEdit;
	m_name->setObjectName("mediaName");
	m_name->setAccessibleName(tr("Source name"));
	m_name->setMaxLength(256);
	form->addRow(tr("Name"), m_name);
	auto *input = new QWidget;
	auto *inputLayout = new QHBoxLayout(input);
	inputLayout->setContentsMargins(0, 0, 0, 0);
	m_path = new QLineEdit;
	m_path->setObjectName("mediaPath");
	m_path->setAccessibleName(tr("Relink or replacement file"));
	m_path->setLayoutDirection(Qt::LeftToRight);
	inputLayout->addWidget(m_path, 1);
	m_browse = new QPushButton(tr("Browse…"));
	m_browse->setAccessibleName(tr("Choose relink or replacement file"));
	inputLayout->addWidget(m_browse);
	form->addRow(tr("Input file"), input);
	m_resample = new QCheckBox(tr("Resample"));
	m_resample->setObjectName("mediaResample");
	m_resample->setAccessibleName(tr("Resample replacement audio to the session rate"));
	m_resample->setToolTip(tr("Convert replacement audio to the session sample rate. Replacement preserves channel "
	                          "count and every clip's source offset. Shorter audio must "
	                          "still cover all used frames."));
	form->addRow(m_resample);
	m_review = new QPushButton(tr("Review"));
	m_review->setObjectName("mediaReview");
	m_review->setAccessibleName(tr("Review proposed media change"));
	form->addRow(m_review);
	m_summary = new QLabel;
	m_summary->setObjectName("mediaSummary");
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	m_summary->setAccessibleName(tr("Media review result"));
	m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	form->addRow(m_summary);
	m_waves = new QTabWidget;
	m_waves->setAccessibleName(tr("Reviewed audio comparison"));
	m_before = new AudioWaveformView;
	m_before->setAccessibleName(tr("Current embedded source waveform"));
	m_after = new AudioWaveformView;
	m_after->setAccessibleName(tr("Proposed source waveform"));
	const auto theme = StudioSettings().accessibilityPreferences().theme;
	const bool contrast = theme == StudioTheme::HighContrastDark || theme == StudioTheme::HighContrastLight;
	m_before->setHighContrast(contrast);
	m_after->setHighContrast(contrast);
	m_waves->addTab(m_before, tr("Current audio"));
	m_waves->addTab(m_after, tr("Proposed audio"));
	form->addRow(m_waves);
	m_waves->hide();
	auto *progressRow = new QHBoxLayout;
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Media review progress"));
	m_progress->setTextVisible(false);
	progressRow->addWidget(m_progress, 1);
	m_cancel = new QPushButton(tr("Cancel Review"));
	m_cancel->setObjectName("mediaCancelReview");
	progressRow->addWidget(m_cancel);
	layout->addLayout(progressRow);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
	m_buttons->button(QDialogButtonBox::Ok)->setAccessibleName(tr("Apply reviewed media change"));
	layout->addWidget(m_buttons);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &AudioMediaDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &AudioMediaDialog::reject);
	connect(m_review, &QPushButton::clicked, this, &AudioMediaDialog::review);
	connect(m_cancel, &QPushButton::clicked, this, &AudioMediaDialog::cancelReview);
	connect(m_sources, &QTreeWidget::itemSelectionChanged, this, &AudioMediaDialog::selectionChanged);
	connect(m_operation, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
		if (m_operation->currentData() != "remove") {
			const auto *current = m_sources->currentItem();
			for (int i = 0; i < m_sources->topLevelItemCount(); ++i)
				m_sources->topLevelItem(i)->setSelected(m_sources->topLevelItem(i) == current);
		}
		invalidate();
	});
	connect(m_name, &QLineEdit::textChanged, this, &AudioMediaDialog::invalidate);
	connect(m_path, &QLineEdit::textChanged, this, &AudioMediaDialog::invalidate);
	connect(m_resample, &QCheckBox::toggled, this, &AudioMediaDialog::invalidate);
	connect(m_browse, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getOpenFileName(
		    this, tr("Choose Session Media"), m_path->text(),
		    tr("Audio and native projects (*.wav *.dmx *.lmp *.mp3 *.flac *.ogg *.vsaudio);;All files (*)"));
		if (!path.isEmpty())
			m_path->setText(path);
	});
	start(true);
}
AudioMediaDialog::~AudioMediaDialog()
{
	if (m_thread) {
		m_work->cancelled = true;
		m_thread->wait();
	}
}
AudioMediaEdit AudioMediaDialog::edit() const
{
	AudioMediaEdit value;
	value.operation = m_operation->currentData().toString();
	if (value.operation != "prune")
		for (const auto *item : m_sources->selectedItems())
			value.sourceIds.append(item->data(0, Qt::UserRole).toString());
	if (value.operation == "rename")
		value.name = m_name->text();
	value.resample = value.operation == "replace" && m_resample->isChecked();
	return value;
}
void AudioMediaDialog::selectionChanged()
{
	QStringList details;
	for (const auto *item : m_sources->selectedItems())
		for (const auto &source : m_inventory.sources)
			if (source.id == item->data(0, Qt::UserRole).toString()) {
				QStringList tracks;
				for (const auto &track : m_session.tracks)
					if (source.tracks.contains(track.id))
						tracks.append(track.name);
				details.append(tr("%1 · Required source frames: 0–%2 · Tracks: %3\n%4")
				                   .arg(source.name)
				                   .arg(source.requiredFrames)
				                   .arg(tracks.join(", "), source.path));
				if (m_sources->selectedItems().size() == 1)
					m_name->setText(source.name);
			}
	m_details->setPlainText(details.join("\n\n"));
	invalidate();
}
void AudioMediaDialog::invalidate()
{
	m_ready = false;
	m_candidate = {};
	m_before->setData({});
	m_after->setData({});
	m_waves->hide();
	if (!isBusy())
		m_summary->setText(tr("Review the change before applying. Source files are never modified or deleted."));
	refresh();
}
void AudioMediaDialog::refresh()
{
	const bool busy = isBusy();
	const auto operation = m_operation->currentData().toString();
	m_fields->setEnabled(!busy);
	m_sources->setEnabled(operation != "prune");
	m_sources->setSelectionMode(operation == "remove" ? QAbstractItemView::ExtendedSelection
	                                                  : QAbstractItemView::SingleSelection);
	m_name->setEnabled(operation == "rename");
	m_path->setEnabled(operation == "relink" || operation == "replace");
	m_browse->setEnabled(m_path->isEnabled());
	m_resample->setEnabled(operation == "replace");
	m_progress->setVisible(busy);
	m_cancel->setVisible(busy);
	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_ready && !busy);
}
void AudioMediaDialog::review()
{
	if (!isBusy())
		start(false);
}
void AudioMediaDialog::cancelReview()
{
	if (m_work)
		m_work->cancelled = true;
}
void AudioMediaDialog::accept()
{
	if (m_ready && !isBusy())
		QDialog::accept();
}
void AudioMediaDialog::reject()
{
	if (isBusy()) {
		m_closing = true;
		cancelReview();
		return;
	}
	QDialog::reject();
}
void AudioMediaDialog::start(bool inspect)
{
	invalidate();
	const auto snapshot = m_session;
	const auto request = edit();
	const auto path = m_path->text();
	auto state = std::make_shared<AudioMediaReview>();
	m_work = state;
	m_summary->setText(inspect ? tr("Checking source usage and file references…")
	                           : tr("Decoding and validating the proposed change…"));
	m_progress->setRange(0, StudioSettings().accessibilityPreferences().reducedMotion ? 1 : 0);
	m_progress->setValue(0);
	auto *thread = QThread::create([state, snapshot, request, path, inspect] {
		try {
			if (inspect) {
				state->inventory = inspectAudioMedia(snapshot, state->control());
				state->error = state->inventory.error;
				return;
			}
			const bool file = request.operation == "relink" || request.operation == "replace";
			if (file) {
				state->candidate = readAudioMediaCandidate(path, state->control());
				state->error = state->candidate.error;
				if (!state->candidate.succeeded())
					return;
			}
			state->result = editAudioMedia(snapshot, request, state->candidate, false, state->control());
			state->error = state->result.error;
			if (!state->result.succeeded() || !file)
				return;
			for (const auto &source : snapshot.sources)
				if (request.sourceIds.contains(source.id))
					state->before = AudioWaveformData::build(source.audio.clip, state->control());
			for (const auto &source : state->result.session.sources)
				if (source.id == state->result.addedSourceId ||
				    (request.operation == "relink" && request.sourceIds.contains(source.id)))
					state->after = AudioWaveformData::build(source.audio.clip, state->control());
		} catch (const std::exception &) {
			state->error = tr("Media review could not finish. The session is unchanged.");
		} catch (...) {
			state->error = tr("Media review failed unexpectedly. The session is unchanged.");
		}
	});
	m_thread = thread;
	thread->setParent(this);
	connect(thread, &QThread::finished, this, [this, state, inspect, request, path] {
		m_thread = nullptr;
		m_work.reset();
		if (m_closing) {
			QDialog::reject();
			return;
		}
		if (state->cancelled)
			m_summary->setText(tr("Review cancelled. The session is unchanged."));
		else if (!inspect && (edit() != request || m_path->text() != path))
			m_summary->setText(tr("The request changed during review. Review it again."));
		else if (!state->error.isEmpty())
			m_summary->setText(state->error);
		else if (inspect) {
			m_inventory = state->inventory;
			for (const auto &source : m_inventory.sources) {
				auto *item = new QTreeWidgetItem(
				    m_sources, {source.name, QString::number(source.channels), QString::number(source.frames),
				                QString::number(source.clips), availability(source.availability)});
				item->setData(0, Qt::UserRole, source.id);
				item->setToolTip(0, source.path);
				if (source.id == m_initialSource)
					m_sources->setCurrentItem(item);
			}
			if (!m_sources->currentItem() && m_sources->topLevelItemCount())
				m_sources->setCurrentItem(m_sources->topLevelItem(0));
			selectionChanged();
		} else {
			m_candidate = state->candidate;
			m_ready = true;
			QString text;
			if (request.operation == "replace" || request.operation == "relink") {
				text = tr("Reviewed %1 Hz, %2 channels, %3 frames. %4\n%5 clips will use this source. SHA-256: %6")
				           .arg(state->after.clip().sampleRate)
				           .arg(state->after.clip().channels)
				           .arg(state->after.clip().frameCount())
				           .arg(state->result.identicalSamples ? tr("Decoded samples are identical.")
				                                               : tr("Decoded samples differ."))
				           .arg([&] {
					           for (const auto &source : m_inventory.sources)
						           if (request.sourceIds.contains(source.id))
							           return source.clips;
					           return 0;
				           }())
				           .arg(QString::fromLatin1(m_candidate.identity.sha256.toHex()));
				if (state->result.resampled)
					text += '\n' + tr("Replacement resampled to the session rate.");
				m_before->setData(state->before, true);
				m_after->setData(state->after, true);
				m_waves->show();
			} else if (request.operation == "rename")
				text = tr("Source name will become: %1").arg(request.name);
			else
				text = tr("Remove %1 unused embedded sources. Files on disk remain untouched.")
				           .arg(state->result.removedSourceIds.size());
			m_summary->setText(text);
		}
		refresh();
	});
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	connect(qApp, &QCoreApplication::aboutToQuit, thread, [thread, state] {
		state->cancelled = true;
		thread->wait();
	});
	refresh();
	thread->start();
}
} // namespace vibestudio
