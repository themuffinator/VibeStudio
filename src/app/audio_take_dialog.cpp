#include "app/audio_take_dialog.h"
#include "core/studio_settings.h"
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
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
struct AudioTakeDialogWork {
	std::atomic_bool cancel{false};
	AudioTakeInfo info;
	AudioProject audio;
	QString error;
};
namespace
{
QDoubleSpinBox *frames(const QString &name, const QString &object, double low = 0)
{
	auto *box = new QDoubleSpinBox;
	box->setObjectName(object);
	box->setAccessibleName(name);
	box->setRange(low, double(AudioTakeFrameLimit));
	box->setDecimals(0);
	box->setKeyboardTracking(false);
	return box;
}
QVector<int> channels(const QString &text)
{
	QVector<int> result;
	QSet<int> seen;
	for (const auto &item : text.split(',')) {
		bool ok = false;
		const qint64 number = item.trimmed().toLongLong(&ok);
		if (!ok || number < 1 || number > 32 || seen.contains(int(number - 1)))
			return {};
		seen.insert(int(number - 1));
		result.append(int(number - 1));
	}
	return result;
}
QFormLayout *formTab(QTabWidget *tabs, const QString &title, QWidget **content)
{
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(title);
	*content = new QWidget;
	auto *form = new QFormLayout(*content);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(*content);
	tabs->addTab(scroll, title);
	return form;
}
// The nested toggle row needs an explicit minimum height when a scrolling
// QFormLayout recalculates its width after the vertical scrollbar appears.
class TakeToggleLabel final : public QLabel {
  public:
	explicit TakeToggleLabel(const QString &text) : QLabel(text)
	{
		setTextFormat(Qt::PlainText);
		setWordWrap(true);
		auto policy = sizePolicy();
		policy.setHorizontalPolicy(QSizePolicy::Ignored);
		setSizePolicy(policy);
	}

  protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QLabel::resizeEvent(event);
		setMinimumHeight(std::max(0, heightForWidth(width())));
	}
	void changeEvent(QEvent *event) override
	{
		QLabel::changeEvent(event);
		if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
			setMinimumHeight(std::max(0, heightForWidth(width())));
	}
};
QCheckBox *wrappedToggle(QFormLayout *form, const QString &text)
{
	auto *row = new QWidget;
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *toggle = new QCheckBox;
	toggle->setAccessibleName(text);
	auto *label = new TakeToggleLabel(text);
	label->setBuddy(toggle);
	layout->addWidget(toggle, 0, Qt::AlignTop);
	layout->addWidget(label, 1);
	form->addRow(row);
	return toggle;
}
} // namespace
AudioTakeDialog::AudioTakeDialog(int rate, QString track, QString sessionPath, qint64 position, QWidget *parent,
                                 AudioCaptureDeviceFactory factory)
    : QDialog(parent), m_rate(rate), m_track(std::move(track)), m_sessionPath(std::move(sessionPath))
{
	setObjectName("audioTakeDialog");
	setWindowTitle(tr("Record and Review Takes"));
	setAccessibleName(windowTitle());
	resize(760, 700);
	auto *outer = new QVBoxLayout(this);
	m_tabs = new QTabWidget;
	m_tabs->setAccessibleName(tr("Recording and take review"));
	outer->addWidget(m_tabs, 1);
	auto *form = formTab(m_tabs, tr("Record"), &m_recordForm);
	m_name = new QLineEdit(tr("Recorded take"));
	m_name->setMaxLength(256);
	m_name->setAccessibleName(tr("Take name"));
	m_name->setObjectName("takeName");
	form->addRow(tr("Name"), m_name);
	m_path = new QLineEdit;
	m_path->setAccessibleName(tr("New take file"));
	m_path->setObjectName("takePath");
	m_path->setPlaceholderText(tr("Choose a new .vstake file"));
	form->addRow(tr("Take file"), m_path);
	auto *choose = new QPushButton(tr("Choose File…"));
	form->addRow(QString(), choose);
	connect(choose, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getSaveFileName(this, tr("New Recorded Take"), {}, tr("Audio takes (*.vstake)"));
		if (!path.isEmpty())
			m_path->setText(path);
	});
	m_input = new QComboBox;
	m_input->setObjectName("takeInput");
	m_input->setAccessibleName(tr("Audio input device"));
	m_input->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_input->setMinimumContentsLength(18);
	form->addRow(tr("Input"), m_input);
	auto *refreshInputs = new QPushButton(tr("Refresh Inputs"));
	form->addRow(QString(), refreshInputs);
	m_inputChannels = new QSpinBox;
	m_inputChannels->setObjectName("takeInputChannels");
	m_inputChannels->setRange(1, 32);
	m_inputChannels->setAccessibleName(tr("Device channel count"));
	form->addRow(tr("Device channels"), m_inputChannels);
	m_channels = new QLineEdit("1");
	m_channels->setObjectName("takeChannels");
	m_channels->setAccessibleName(tr("Channels to record, in order"));
	m_channels->setToolTip(
	    tr("Input channel numbers start at 1. Use commas, for example 3,4. Record up to eight distinct channels."));
	form->addRow(tr("Record channels"), m_channels);
	auto *rateLabel = new QLabel(tr("%1 Hz (session rate)").arg(locale().toString(rate)));
	form->addRow(tr("Sample rate"), rateLabel);
	m_buffer = new QComboBox;
	m_buffer->setAccessibleName(tr("Input buffer frames"));
	for (int size : {256, 512, 1024, 2048, 4096, 8192, 16384})
		m_buffer->addItem(locale().toString(size), size);
	m_buffer->setCurrentIndex(3);
	form->addRow(tr("Buffer (frames)"), m_buffer);
	m_position = frames(tr("Recorded timeline position"), "takePosition");
	m_position->setValue(double(position));
	form->addRow(tr("Position (frames)"), m_position);
	m_latency = new QSpinBox;
	m_latency->setObjectName("takeLatency");
	m_latency->setRange(-rate * 10, rate * 10);
	m_latency->setAccessibleName(tr("Input latency compensation in frames"));
	m_latency->setToolTip(tr("Positive values place imported audio earlier. Enter a measured offset; this is not "
	                         "automatic hardware latency calibration."));
	form->addRow(tr("Compensation (frames)"), m_latency);
	m_arm = wrappedToggle(form, tr("Arm selected input channels"));
	m_arm->setObjectName("takeArm");
	m_arm->setToolTip(tr("Input opens only when Record is pressed. Session playback stops during capture; input "
	                     "monitoring is not enabled."));
	m_record = new QPushButton(tr("Record"));
	m_record->setObjectName("takeRecord");
	form->addRow(QString(), m_record);
	auto *reviewForm = formTab(m_tabs, tr("Review / Import"), &m_reviewForm);
	auto *open = new QPushButton(tr("Open Take…"));
	open->setObjectName("takeOpen");
	reviewForm->addRow(QString(), open);
	connect(open, &QPushButton::clicked, this, [this] {
		const auto path =
		    QFileDialog::getOpenFileName(this, tr("Review Recorded Take"), {}, tr("Audio takes (*.vstake)"));
		if (!path.isEmpty())
			inspectTake(path);
	});
	m_details = new QLabel(tr("Open a take or finish recording to review its verified frames."));
	m_details->setObjectName("takeDetails");
	m_details->setAccessibleName(tr("Verified take details"));
	m_details->setTextFormat(Qt::PlainText);
	m_details->setWordWrap(true);
	auto detailsPolicy = m_details->sizePolicy();
	detailsPolicy.setHorizontalPolicy(QSizePolicy::Ignored);
	m_details->setSizePolicy(detailsPolicy);
	m_details->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	reviewForm->addRow(m_details);
	auto *details = new QPushButton(tr("File Details"));
	details->setCheckable(true);
	details->setAccessibleName(tr("Show take path and checksum details"));
	reviewForm->addRow(details);
	m_fileDetails = new QPlainTextEdit;
	m_fileDetails->setObjectName("takeFileDetails");
	m_fileDetails->setAccessibleName(tr("Take path and verified checksum"));
	m_fileDetails->setReadOnly(true);
	m_fileDetails->setMaximumHeight(180);
	m_fileDetails->setVisible(false);
	reviewForm->addRow(m_fileDetails);
	connect(details, &QPushButton::toggled, m_fileDetails, &QPlainTextEdit::setVisible);
	m_first = frames(tr("First take frame"), "takeFirst");
	m_end = frames(tr("Exclusive end take frame"), "takeEnd");
	reviewForm->addRow(tr("First frame"), m_first);
	reviewForm->addRow(tr("End frame (exclusive)"), m_end);
	m_storedChannels = new QLineEdit("1");
	m_storedChannels->setObjectName("takeStoredChannels");
	m_storedChannels->setAccessibleName(tr("Stored channels to import"));
	m_storedChannels->setToolTip(tr("Choose one or two stored channel numbers, starting at 1. Import additional pairs "
	                                "as separate session tracks."));
	reviewForm->addRow(tr("Stored channels"), m_storedChannels);
	m_compensate = wrappedToggle(reviewForm, tr("Use recorded placement and compensation"));
	m_compensate->setChecked(true);
	m_compensate->setObjectName("takeCompensate");
	m_place = frames(tr("Import timeline position"), "takePlace", -3840000);
	m_place->setMaximum(double(AudioTakeFrameLimit * 2 + 3840000));
	reviewForm->addRow(tr("Import position (frames)"), m_place);
	m_recover = wrappedToggle(reviewForm, tr("Accept the verified prefix of this incomplete take"));
	m_recover->setObjectName("takeAcceptPrefix");
	m_import = new QPushButton(tr("Import into Session"));
	m_import->setObjectName("takeImport");
	reviewForm->addRow(QString(), m_import);
	m_status = new QLabel;
	m_status->setObjectName("takeStatus");
	m_status->setAccessibleName(tr("Recording status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	outer->addWidget(m_status);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Recording operation progress"));
	m_progress->setRange(0, StudioSettings().accessibilityPreferences().reducedMotion ? 1 : 0);
	outer->addWidget(m_progress);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_stop = buttons->addButton(tr("Stop / Cancel"), QDialogButtonBox::ActionRole);
	m_stop->setObjectName("takeStop");
	m_stop->setAccessibleName(tr("Stop recording or cancel take verification"));
	connect(buttons, &QDialogButtonBox::rejected, this, &AudioTakeDialog::reject);
	outer->addWidget(buttons);
	m_capture = new AudioCapture(this, std::move(factory));
	connect(refreshInputs, &QPushButton::clicked, m_capture, &AudioCapture::refreshInputs);
	connect(m_capture, &AudioCapture::inputsChanged, this, [this] {
		const auto selected = m_input->currentData().toByteArray();
		m_input->clear();
		m_input->addItem(tr("Select an input…"), QByteArray{});
		for (const auto &device : m_capture->inputs())
			m_input->addItem(device.name, device.id);
		m_input->setCurrentIndex(std::max(0, m_input->findData(selected)));
		refresh();
	});
	connect(m_input, &QComboBox::currentIndexChanged, this, [this] {
		for (const auto &device : m_capture->inputs())
			if (device.id == m_input->currentData().toByteArray()) {
				m_inputChannels->setRange(std::clamp(device.minimumChannels, 1, 32),
				                          std::clamp(device.maximumChannels, 1, 32));
				break;
			}
		refresh();
	});
	connect(m_capture, &AudioCapture::changed, this, [this] {
		const auto &snapshot = m_capture->snapshot();
		if (!m_capture->busy() && snapshot.take.headerValid && snapshot.take.path != m_info.path)
			review(snapshot.take);
		refresh();
		if (m_closePending && !busy())
			QDialog::reject();
	});
	connect(m_arm, &QCheckBox::toggled, this, &AudioTakeDialog::refresh);
	connect(m_tabs, &QTabWidget::currentChanged, this, &AudioTakeDialog::refresh);
	connect(m_path, &QLineEdit::textChanged, this, &AudioTakeDialog::refresh);
	connect(m_recover, &QCheckBox::toggled, this, &AudioTakeDialog::refresh);
	connect(m_record, &QPushButton::clicked, this, &AudioTakeDialog::record);
	connect(m_import, &QPushButton::clicked, this, &AudioTakeDialog::importTake);
	connect(m_first, &QDoubleSpinBox::valueChanged, this, &AudioTakeDialog::placement);
	connect(m_compensate, &QCheckBox::toggled, this, &AudioTakeDialog::placement);
	connect(m_stop, &QPushButton::clicked, this, [this] {
		if (m_work)
			m_work->cancel = true;
		m_capture->stop();
	});
	placement();
	refresh();
}
AudioTakeDialog::~AudioTakeDialog()
{
	if (m_work)
		m_work->cancel = true;
	if (m_thread)
		m_thread->wait();
	delete m_capture; // Stop/join while its status widgets still exist.
}
bool AudioTakeDialog::busy() const { return bool(m_work) || m_capture->busy(); }
void AudioTakeDialog::reject()
{
	if (busy()) {
		m_closePending = true;
		if (m_work)
			m_work->cancel = true;
		m_capture->stop();
		return;
	}
	QDialog::reject();
}
void AudioTakeDialog::closeEvent(QCloseEvent *event)
{
	if (busy()) {
		event->ignore();
		reject();
		return;
	}
	QDialog::closeEvent(event);
}
void AudioTakeDialog::record()
{
	if (busy() || !m_arm->isChecked())
		return;
	AudioCaptureRequest request;
	request.path = m_path->text();
	request.deviceId = m_input->currentData().toByteArray();
	request.bufferFrames = m_buffer->currentData().toInt();
	request.metadata.name = m_name->text();
	request.metadata.sampleRate = m_rate;
	request.metadata.inputChannels = m_inputChannels->value();
	request.metadata.channelMap = channels(m_channels->text());
	request.metadata.position = qint64(m_position->value());
	request.metadata.latencyFrames = m_latency->value();
	request.metadata.trackId = m_track;
	request.metadata.sourceSessionPath = m_sessionPath;
	request.metadata.deviceName = m_input->currentText();
	if (beforeRecord)
		beforeRecord();
	m_capture->start(request);
}
void AudioTakeDialog::refresh()
{
	const bool active = busy();
	m_recordForm->setEnabled(!active);
	m_reviewForm->setEnabled(!active);
	m_record->setEnabled(!active && m_capture->available() && m_arm->isChecked() &&
	                     !m_input->currentData().toByteArray().isEmpty() && !m_path->text().trimmed().isEmpty());
	m_import->setEnabled(!active && m_info.recoverable() && (m_info.complete || m_recover->isChecked()));
	m_stop->setEnabled(active);
	m_progress->setVisible(active);
	if (m_work) {
		m_status->setText(tr("Verifying take data…"));
		return;
	}
	if (!m_capture->busy() && m_tabs->currentIndex() == 1 && !m_info.path.isEmpty()) {
		m_status->setText(m_info.headerValid && m_info.frames == 0
		                      ? tr("This take contains no recoverable sample frames.")
		                  : m_info.complete ? tr("Complete take. Reviewed frames are ready for import.")
		                                    : m_info.error);
		return;
	}
	const auto &s = m_capture->snapshot();
	QString state;
	switch (s.state) {
	case AudioCaptureSnapshot::State::Unavailable:
		state = tr("Input capture unavailable in this build; recorded takes can still be reviewed and imported.");
		break;
	case AudioCaptureSnapshot::State::Ready:
		state = tr("Ready. Select and arm an input, then press Record.");
		break;
	case AudioCaptureSnapshot::State::Permission:
		state = tr("Waiting for microphone permission…");
		break;
	case AudioCaptureSnapshot::State::Preparing:
		state = tr("Preparing take storage…");
		break;
	case AudioCaptureSnapshot::State::Recording:
		state = tr("Recording");
		break;
	case AudioCaptureSnapshot::State::Stopping:
		state = tr("Stopping and saving queued blocks…");
		break;
	case AudioCaptureSnapshot::State::Finished:
		state = tr("Recording finished. Review the take before importing.");
		break;
	case AudioCaptureSnapshot::State::Error:
		state = tr("Recording stopped: %1").arg(s.error);
		break;
	}
	if (s.receivedFrames || m_capture->busy()) {
		const auto peak = *std::max_element(s.peak.begin(), s.peak.end());
		state += tr("\nReceived %1 frames · stored %2 · queue %3/16 · peak %4 dBFS · overs %5 · buffer %6 frames")
		             .arg(locale().toString(s.receivedFrames), locale().toString(s.storedFrames),
		                  locale().toString(s.queuedBlocks),
		                  peak > 0 ? locale().toString(20 * std::log10(peak), 'f', 1) : tr("−∞"),
		                  locale().toString(s.samplesAboveFullScale), locale().toString(s.bufferFrames));
	}
	m_status->setText(state);
}
void AudioTakeDialog::placement()
{
	m_place->setReadOnly(m_compensate->isChecked());
	if (m_compensate->isChecked())
		m_place->setValue(double(m_info.metadata.position + qint64(m_first->value()) - m_info.metadata.latencyFrames));
}
void AudioTakeDialog::review(const AudioTakeInfo &info)
{
	m_info = info;
	QStringList map;
	for (int channel : info.metadata.channelMap)
		map << locale().toString(channel + 1);
	m_details->setText(tr("%1\n%2\n%3 frames at %4 Hz · stored input channels: %5\n%6")
	                       .arg(info.headerValid ? info.metadata.name : tr("Unreadable take"),
	                            QFileInfo(info.path).fileName(), locale().toString(info.frames),
	                            locale().toString(info.metadata.sampleRate), map.join(", "),
	                            info.complete ? tr("Complete") : tr("Incomplete")));
	m_fileDetails->setPlainText(tr("Path: %1\nPrefix SHA-256: %2\nVerified bytes: %3 of %4")
	                                .arg(info.path, QString::fromLatin1(info.prefixSha256.toHex()),
	                                     locale().toString(info.verifiedBytes), locale().toString(info.bytes)));
	if (!info.headerValid)
		m_details->setText(tr("Unreadable take: %1").arg(QFileInfo(info.path).fileName()));
	m_first->setMaximum(double(info.frames));
	m_first->setValue(0);
	m_end->setMaximum(double(info.frames));
	m_end->setValue(double(info.frames));
	m_storedChannels->setText(info.metadata.channelMap.size() >= 2 ? "1,2" : "1");
	m_recover->setChecked(false);
	m_recover->parentWidget()->setVisible(!info.complete);
	placement();
	m_tabs->setCurrentIndex(1);
}
void AudioTakeDialog::launch(std::function<void(AudioTakeDialogWork &)> perform, bool importing)
{
	m_work = std::make_shared<AudioTakeDialogWork>();
	const auto state = m_work;
	m_thread = QThread::create([state, perform] {
		try {
			perform(*state);
		} catch (const std::exception &) {
			state->error = tr("The take could not be processed because a worker or memory allocation failed.");
		}
	});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this, [this, state, importing] {
		m_thread->deleteLater();
		m_thread = nullptr;
		m_work.reset();
		refresh();
		if (state->cancel)
			m_status->setText(tr("Take operation cancelled. The recorded file is preserved."));
		else if (!state->error.isEmpty())
			m_status->setText(state->error);
		else if (importing) {
			m_imported = state->audio;
			accept();
		} else {
			review(state->info);
			refresh();
		}
		if (m_closePending)
			QDialog::reject();
	});
	m_thread->start();
	refresh();
}
bool AudioTakeDialog::inspectTake(const QString &path)
{
	if (busy())
		return false;
	launch(
	    [path](AudioTakeDialogWork &work) {
		    work.info = inspectAudioTake(path, {[&work] { return work.cancel.load(); }});
	    },
	    false);
	return true;
}
void AudioTakeDialog::importTake()
{
	if (busy() || !m_info.recoverable())
		return;
	const auto selected = channels(m_storedChannels->text());
	const auto first = qint64(m_first->value()), end = qint64(m_end->value()), position = qint64(m_place->value());
	if (selected.isEmpty() || selected.size() > 2 || first >= end || end - first > AudioSampleLimit / selected.size() ||
	    position < 0 || position > AudioTakeFrameLimit - (end - first) || m_info.metadata.sampleRate != m_rate) {
		m_status->setText(tr("Choose one or two channels and a nonempty range within %1 samples. Placement must fit "
		                     "the session timeline and rates must match. Adjust recorded compensation or choose a "
		                     "manual position if it falls before frame zero.")
		                      .arg(locale().toString(AudioSampleLimit)));
		return;
	}
	m_importPosition = position;
	const auto info = m_info;
	const bool incomplete = m_recover->isChecked();
	launch(
	    [info, first, end, selected, incomplete](AudioTakeDialogWork &work) {
		    const auto result = readAudioTakeRange(info.path, info.prefixSha256, first, end, selected, incomplete,
		                                           {[&work] { return work.cancel.load(); }});
		    work.error = result.error;
		    work.audio = result.audio;
	    },
	    true);
}
} // namespace vibestudio
