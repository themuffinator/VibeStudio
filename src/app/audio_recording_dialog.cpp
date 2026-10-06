#include "app/audio_recording_dialog.h"
#include "app/audio_recording_meters.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
struct AudioRecordingDialogWork {
	std::atomic<bool> cancelled{false};
	AudioRecordingInfo info;
	AudioRecordingImportResult imported;
	AudioRecordingAuditionResult audition;
	AudioRecordingReviewResult opened;
	AudioProjectSaveReport saved;
	QJsonObject savedReview;
	bool auditioning = false;
	QString activity;
	QString error;
	AudioWorkControl control()
	{
		return {[this] { return cancelled.load(); }};
	}
};
namespace
{
QVector<int> channelList(const QString &text)
{
	QVector<int> values;
	for (const auto &part : text.split(',')) {
		bool ok;
		const auto value = part.trimmed().toLongLong(&ok);
		if (!ok || value < 1 || value > 32 || values.contains(int(value - 1)))
			return {};
		values << int(value - 1);
	}
	return values;
}
QString channelText(const QVector<int> &values)
{
	QStringList text;
	for (int value : values)
		text << QString::number(value + 1);
	return text.join(',');
}
QFormLayout *tabForm(QTabWidget *tabs, const QString &name, QWidget **contents)
{
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(name);
	*contents = new QWidget;
	auto *form = new QFormLayout(*contents);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(*contents);
	tabs->addTab(scroll, name);
	return form;
}
QDoubleSpinBox *frameBox(const QString &name, const char *object)
{
	auto *box = new QDoubleSpinBox;
	box->setObjectName(QLatin1String(object));
	box->setAccessibleName(name);
	box->setRange(0, double(AudioSessionFrameLimit));
	box->setDecimals(0);
	box->setKeyboardTracking(false);
	box->setLayoutDirection(Qt::LeftToRight);
	return box;
}
QString numericText(const QString &text) { return QChar(0x2066) + text + QChar(0x2069); }
QTreeWidget *list(const QString &name, const char *object, const QStringList &columns)
{
	auto *view = new QTreeWidget;
	view->setObjectName(QLatin1String(object));
	view->setAccessibleName(name);
	view->setHeaderLabels(columns);
	view->setRootIsDecorated(false);
	view->setAlternatingRowColors(true);
	view->setMinimumHeight(160);
	view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	view->header()->setStretchLastSection(true);
	return view;
}
class RecordingToggleLabel final : public QLabel {
  public:
	explicit RecordingToggleLabel(const QString &text) : QLabel(text)
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
QCheckBox *toggle(QFormLayout *form, const QString &text)
{
	auto *row = new QWidget;
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *box = new QCheckBox;
	box->setAccessibleName(text);
	auto *label = new RecordingToggleLabel(text);
	label->setBuddy(box);
	layout->addWidget(box, 0, Qt::AlignTop);
	layout->addWidget(label, 1);
	form->addRow(row);
	return box;
}
} // namespace

AudioRecordingDialog::AudioRecordingDialog(AudioSession session, QString sessionPath, qint64 first, qint64 end,
                                           QWidget *parent, AudioDuplexDeviceFactory device,
                                           AudioCaptureStorageFactory storage, AudioRecordingGate permission,
                                           AudioRecordingGate beforeStart, AudioStreamDeviceFactory auditionDevice)
    : QDialog(parent), m_session(std::move(session)), m_sessionPath(std::move(sessionPath)),
      m_beforePlayback(std::move(beforeStart))
{
	setObjectName(QStringLiteral("audioRecordingDialog"));
	setWindowTitle(tr("Record Tracks and Review Takes"));
	setAccessibleName(windowTitle());
	first = std::clamp<qint64>(first, 0, AudioSessionFrameLimit);
	resize(920, 780);
	auto *outer = new QVBoxLayout(this);
	m_tabs = new QTabWidget;
	m_tabs->setAccessibleName(tr("Recording and grouped take review"));
	outer->addWidget(m_tabs, 1);
	auto *form = tabForm(m_tabs, tr("Record"), &m_recordForm);
	m_name = new QLineEdit(tr("Recorded pass"));
	m_name->setObjectName("recordingName");
	m_name->setAccessibleName(tr("Recording name"));
	m_name->setMaxLength(256);
	form->addRow(tr("Name"), m_name);
	m_path = new QLineEdit;
	m_path->setObjectName("recordingPath");
	m_path->setAccessibleName(tr("New recording folder"));
	m_path->setPlaceholderText(tr("Choose a new .vsrecord folder"));
	form->addRow(tr("Folder"), m_path);
	auto *choose = new QPushButton(tr("Choose New Folder…"));
	choose->setAccessibleName(choose->text());
	form->addRow(QString{}, choose);
	connect(choose, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getSaveFileName(this, tr("New Recording Folder"), m_sessionPath,
		                                               tr("Recording folders (*.vsrecord)"));
		if (!path.isEmpty())
			m_path->setText(path.endsWith(".vsrecord", Qt::CaseInsensitive) ? path : path + ".vsrecord");
	});
	m_input = new QComboBox;
	m_input->setObjectName("recordingInput");
	m_input->setAccessibleName(tr("Recording input device"));
	m_output = new QComboBox;
	m_output->setObjectName("recordingOutput");
	m_output->setAccessibleName(tr("Backing and monitoring output device"));
	form->addRow(tr("Input"), m_input);
	form->addRow(tr("Output"), m_output);
	auto *refreshDevicesButton = new QPushButton(tr("Refresh Devices"));
	refreshDevicesButton->setObjectName("recordingRefreshDevices");
	form->addRow(QString{}, refreshDevicesButton);
	m_inputChannels = new QSpinBox;
	m_inputChannels->setObjectName("recordingInputChannels");
	m_inputChannels->setAccessibleName(tr("Device input channel count"));
	m_inputChannels->setRange(1, 32);
	form->addRow(tr("Input channels"), m_inputChannels);
	m_playbackFirst = frameBox(tr("Backing playback first frame"), "recordingPlaybackFirst");
	m_punchFirst = frameBox(tr("First recorded timeline frame"), "recordingPunchFirst");
	m_punchEnd = frameBox(tr("Exclusive recording end frame"), "recordingPunchEnd");
	m_playbackFirst->setValue(double(std::max<qint64>(0, first - m_session.sampleRate)));
	m_punchFirst->setValue(double(first));
	m_punchEnd->setValue(
	    double(end > first ? end : std::min(AudioSessionFrameLimit, first + qint64(m_session.sampleRate) * 10)));
	form->addRow(tr("Play from frame"), m_playbackFirst);
	form->addRow(tr("Punch in frame"), m_punchFirst);
	form->addRow(tr("Punch out frame"), m_punchEnd);
	m_loopPasses = new QSpinBox;
	m_loopPasses->setObjectName("recordingLoopPasses");
	m_loopPasses->setAccessibleName(tr("Number of recording passes"));
	m_loopPasses->setRange(1, AudioDuplexLoopPassLimit);
	m_loopPasses->setToolTip(
	    tr("Repeat the punch range with continuous monitoring and effect tails. Preroll plays once. "
	       "Each pass can be selected separately in Review."));
	form->addRow(tr("Loop passes"), m_loopPasses);
	m_armList =
	    list(tr("Audio tracks to arm"), "recordingArms", {tr("Arm / Track"), tr("Inputs"), tr("Stored frames")});
	form->addRow(m_armList);
	for (const auto &track : m_session.tracks) {
		if (track.routing.bus)
			continue;
		AudioDuplexArm arm;
		arm.trackId = track.id;
		m_arms << arm;
		auto *item = new QTreeWidgetItem(m_armList, {track.name, "1", {}});
		item->setData(0, Qt::UserRole, track.id);
		item->setCheckState(0, Qt::Unchecked);
		item->setToolTip(0, tr("Arm this track for recording. Input opens only after Record is pressed."));
	}
	m_channels = new QLineEdit("1");
	m_channels->setObjectName("recordingChannels");
	m_channels->setAccessibleName(tr("Selected track input channels in order"));
	m_channels->setToolTip(tr("One or two input channel numbers, starting at 1. Comma order is preserved."));
	form->addRow(tr("Track inputs"), m_channels);
	m_monitor = toggle(form, tr("Monitor this track"));
	m_monitor->setObjectName("recordingMonitor");
	m_monitor->setToolTip(
	    tr("Listen through the session effects and routing. Use headphones or isolated routing to avoid feedback."));
	m_monitorGain = new QDoubleSpinBox;
	m_monitorGain->setObjectName("recordingMonitorGain");
	m_monitorGain->setAccessibleName(tr("Selected track monitor level in decibels"));
	m_monitorGain->setRange(-60, 0);
	m_monitorGain->setDecimals(1);
	m_monitorGain->setSuffix(tr(" dB"));
	form->addRow(tr("Monitor level"), m_monitorGain);
	m_replacePlayback = toggle(form, tr("Silence this track's clips during the punch"));
	m_replacePlayback->setObjectName("recordingReplacePlayback");
	auto *advanced = new QGroupBox(tr("Device timing"));
	auto *advancedForm = new QFormLayout(advanced);
	advancedForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_buffer = new QComboBox;
	m_buffer->setObjectName("recordingBuffer");
	m_buffer->setAccessibleName(tr("Requested device buffer frames"));
	for (int frames : {128, 256, 512, 1024, 2048, 4096, 8192, 16384})
		m_buffer->addItem(locale().toString(frames), frames);
	m_buffer->setCurrentIndex(4);
	advancedForm->addRow(tr("Buffer frames"), m_buffer);
	m_calibration = new QSpinBox;
	m_calibration->setObjectName("recordingCalibration");
	m_calibration->setAccessibleName(tr("Additional measured timing correction in frames"));
	m_calibration->setRange(-m_session.sampleRate * 10, m_session.sampleRate * 10);
	m_calibration->setToolTip(
	    tr("Positive values place input earlier. Device timing and effect latency are already accounted for."));
	advancedForm->addRow(tr("Calibration frames"), m_calibration);
	m_clockTolerance = new QSpinBox;
	m_clockTolerance->setObjectName("recordingClockTolerance");
	m_clockTolerance->setAccessibleName(tr("Accepted device clock deviation in frames"));
	m_clockTolerance->setRange(0, m_session.sampleRate);
	m_clockTolerance->setValue(std::min(128, m_session.sampleRate));
	m_clockTolerance->setToolTip(tr("A larger change in input or output timing stops recording and retains verified "
	                                "takes. This does not calibrate hardware latency."));
	advancedForm->addRow(tr("Clock tolerance frames"), m_clockTolerance);
	form->addRow(advanced);
	m_record = new QPushButton(tr("Record Armed Tracks"));
	m_record->setObjectName("recordingRecord");
	form->addRow(m_record);
	auto *reviewForm = tabForm(m_tabs, tr("Review"), &m_reviewForm);
	auto *open = new QPushButton(tr("Open Recording…"));
	open->setObjectName("recordingOpen");
	reviewForm->addRow(open);
	connect(open, &QPushButton::clicked, this, [this] {
		const auto directory = QFileDialog::getExistingDirectory(
		    this, tr("Review Recording Folder"), {}, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
		if (!directory.isEmpty())
			confirmReviewChange([this, directory] { inspectRecording(directory); });
	});
	auto *openReviewButton = new QPushButton(tr("Open Saved Review…"));
	openReviewButton->setObjectName("recordingOpenReview");
	openReviewButton->setAccessibleName(openReviewButton->text());
	reviewForm->addRow(openReviewButton);
	connect(openReviewButton, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getOpenFileName(this, tr("Open Recording Review"), m_reviewIdentity.path,
		                                               tr("Recording review files (*.json)"));
		if (!path.isEmpty())
			confirmReviewChange([this, path] { openReview(path); });
	});
	m_reviewFileStatus = new QLabel;
	m_reviewFileStatus->setObjectName("recordingReviewFileStatus");
	m_reviewFileStatus->setAccessibleName(tr("Saved recording review and change status"));
	m_reviewFileStatus->setTextFormat(Qt::PlainText);
	m_reviewFileStatus->setWordWrap(true);
	reviewForm->addRow(m_reviewFileStatus);
	m_reviewStatus = new QLabel(tr("Finish a pass or open a recording folder to review its takes."));
	m_reviewStatus->setObjectName("recordingReviewStatus");
	m_reviewStatus->setTextFormat(Qt::PlainText);
	m_reviewStatus->setWordWrap(true);
	m_reviewStatus->setAccessibleName(tr("Recording verification summary"));
	reviewForm->addRow(m_reviewStatus);
	m_takeList = list(tr("Verified recorded takes to import"), "recordingTakes",
	                  {tr("Import / Track"), tr("Verified frames"), tr("State"), tr("Passes")});
	reviewForm->addRow(m_takeList);
	m_target = new QComboBox;
	m_target->setObjectName("recordingTarget");
	m_target->setAccessibleName(tr("Selected take destination track"));
	m_target->addItem(tr("Choose an audio track"), QString{});
	for (const auto &track : m_session.tracks)
		if (!track.routing.bus)
			m_target->addItem(track.name, track.id);
	reviewForm->addRow(tr("Destination track"), m_target);
	m_takePass = new QSpinBox;
	m_takePass->setObjectName("recordingTakePass");
	m_takePass->setAccessibleName(tr("Selected recorded loop pass"));
	m_takePass->setRange(1, 1);
	m_takePass->setToolTip(tr("Choose a recorded pass for this arm. First and end frames are local to that pass. "
	                          "Changing pass resets its selected range; source journals are retained."));
	reviewForm->addRow(tr("Recorded pass"), m_takePass);
	m_first = frameBox(tr("First selected take frame"), "recordingTakeFirst");
	m_end = frameBox(tr("Exclusive selected take end frame"), "recordingTakeEnd");
	m_position = frameBox(tr("Imported clip timeline position"), "recordingTakePosition");
	reviewForm->addRow(tr("Take first frame"), m_first);
	reviewForm->addRow(tr("Take end frame"), m_end);
	m_useRecordedPlacement = toggle(reviewForm, tr("Keep recorded timeline alignment"));
	m_useRecordedPlacement->setObjectName("recordingUseRecordedPlacement");
	reviewForm->addRow(tr("Place at frame"), m_position);
	m_storedChannels = new QLineEdit;
	m_storedChannels->setObjectName("recordingStoredChannels");
	m_storedChannels->setAccessibleName(tr("Selected stored take channels in order"));
	m_storedChannels->setToolTip(
	    tr("Stored channel numbers start at 1. These are the channels shown in the take, not hardware input numbers."));
	reviewForm->addRow(tr("Stored channels"), m_storedChannels);
	m_replaceExisting = toggle(reviewForm, tr("Replace existing clips in this imported range"));
	m_replaceExisting->setObjectName("recordingReplaceExisting");
	m_useQueue = toggle(reviewForm, tr("Use queued take selections"));
	m_useQueue->setObjectName("recordingUseQueue");
	m_useQueue->setToolTip(
	    tr("Use the selection queue instead of checked whole takes. Saved reviews reopen here "
	       "with every selection in its original order. Enable comp assembly for adjacent crossfades."));
	m_comp = toggle(reviewForm, tr("Assemble comp sections"));
	m_comp->setObjectName("recordingComp");
	m_comp->setToolTip(tr("Queue sections from recorded passes. Import creates editable clips with optional "
	                      "crossfades; source journals are retained."));
	m_compForm = new QWidget;
	auto *compForm = new QFormLayout(m_compForm);
	compForm->setContentsMargins(0, 0, 0, 0);
	compForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	compForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_addSection = new QPushButton(tr("Add Current Section"));
	m_addSection->setObjectName("recordingCompAdd");
	compForm->addRow(m_addSection);
	m_compList = list(tr("Queued comp sections"), "recordingCompSections",
	                  {tr("Track"), tr("Pass"), tr("Take range"), tr("Place at"), tr("Fade in / out")});
	compForm->addRow(m_compList);
	m_updateSection = new QPushButton(tr("Update Selected Section"));
	m_updateSection->setObjectName("recordingCompUpdate");
	compForm->addRow(m_updateSection);
	m_removeSection = new QPushButton(tr("Remove Selected Section"));
	m_removeSection->setObjectName("recordingCompRemove");
	compForm->addRow(m_removeSection);
	m_crossfade = frameBox(tr("Comp crossfade length in frames"), "recordingCompCrossfade");
	m_crossfade->setMaximum(double(AudioSampleLimit));
	m_crossfade->setToolTip(tr("Zero makes hard cuts. Use at least two frames for complementary linear fades after "
	                           "adjacent cuts. The outgoing pass must contain the extra audio."));
	compForm->addRow(tr("Crossfade frames"), m_crossfade);
	m_compSummary = new QLabel;
	m_compSummary->setObjectName("recordingCompSummary");
	m_compSummary->setTextFormat(Qt::PlainText);
	m_compSummary->setWordWrap(true);
	m_compSummary->setAccessibleName(tr("Comp validation and section count"));
	compForm->addRow(m_compSummary);
	m_compForm->setVisible(false);
	reviewForm->addRow(m_compForm);
	m_acceptInterrupted = toggle(reviewForm, tr("Accept verified prefixes from this interrupted pass"));
	m_acceptInterrupted->setObjectName("recordingAcceptInterrupted");
	m_group = toggle(reviewForm, tr("Link imported clips as a group"));
	m_group->setObjectName("recordingGroup");
	m_group->setChecked(true);
	m_saveReview = new QPushButton(tr("Save Review"));
	m_saveReview->setObjectName("recordingSaveReview");
	m_saveReview->setAccessibleName(m_saveReview->text());
	m_saveReviewAs = new QPushButton(tr("Save Review As…"));
	m_saveReviewAs->setObjectName("recordingSaveReviewAs");
	m_saveReviewAs->setAccessibleName(m_saveReviewAs->text());
	reviewForm->addRow(m_saveReview);
	reviewForm->addRow(m_saveReviewAs);
	const auto chooseReviewOutput = [this] {
		QFileDialog chooser(this, tr("Save Recording Review"), m_reviewIdentity.path,
		                    tr("Recording review files (*.json)"));
		chooser.setAcceptMode(QFileDialog::AcceptSave);
		chooser.setLayoutDirection(layoutDirection());
		chooser.setFileMode(QFileDialog::AnyFile);
		chooser.setDefaultSuffix("json");
		if (chooser.exec() == QDialog::Accepted && !chooser.selectedFiles().isEmpty()) {
			const auto path = chooser.selectedFiles().first();
			saveReview(path, QFileInfo::exists(path));
		}
	};
	connect(m_saveReviewAs, &QPushButton::clicked, this, chooseReviewOutput);
	connect(m_saveReview, &QPushButton::clicked, this, [this, chooseReviewOutput] {
		if (m_reviewIdentity.path.isEmpty())
			chooseReviewOutput();
		else
			saveReview(m_reviewIdentity.path, true);
	});
	m_import = new QPushButton(tr("Import Selected Takes"));
	m_import->setObjectName("recordingImport");
	reviewForm->addRow(m_import);
	auto *details = new QPushButton(tr("Show File and Import Details"));
	details->setCheckable(true);
	reviewForm->addRow(details);
	m_details = new QPlainTextEdit;
	m_details->setObjectName("recordingDetails");
	m_details->setAccessibleName(tr("Recording verification and import plan JSON"));
	m_details->setReadOnly(true);
	m_details->setMinimumHeight(180);
	m_details->setVisible(false);
	reviewForm->addRow(m_details);
	connect(details, &QPushButton::toggled, m_details, &QWidget::setVisible);
	connect(details, &QPushButton::toggled, this, [this] { refresh(); });
	auto *meterScroll = new QScrollArea;
	meterScroll->setObjectName("recordingMeterScroll");
	meterScroll->setAccessibleName(tr("Recording meters and reset"));
	meterScroll->setWidgetResizable(true);
	m_meters = new AudioRecordingMeters;
	meterScroll->setWidget(m_meters);
	m_tabs->addTab(meterScroll, tr("Meters"));
	m_audition = new AudioSessionPlayback(this, std::move(auditionDevice));
	m_audition->setObjectName("recordingAuditionPlayback");
	createAuditionControls(reviewForm);
	m_status = new QLabel;
	m_status->setObjectName("recordingStatus");
	m_status->setAccessibleName(tr("Recording operation status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	outer->addWidget(m_status);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Recording and verification progress"));
	outer->addWidget(m_progress);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_stop = buttons->addButton(tr("Stop / Cancel"), QDialogButtonBox::ActionRole);
	m_stop->setObjectName("recordingStop");
	m_stop->setAccessibleName(tr("Stop recording or cancel take verification"));
	outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &AudioRecordingDialog::reject);
	m_recording = new AudioRecording(
	    this, std::move(device), std::move(storage), std::move(permission),
	    [guard = QPointer<AudioRecordingDialog>(this)](QObject *context, auto complete) {
		    if (!guard)
			    return;
		    guard->m_audition->stopAndWait(
		        context, [guard, context = QPointer<QObject>(context), complete](bool stopped) {
			        if (!guard || !context)
				        return;
			        if (!stopped) {
				        complete(tr("Audition changed while preparing recording. Stop playback and try again."));
				        return;
			        }
			        if (guard->m_beforePlayback)
				        guard->m_beforePlayback(context, complete);
			        else
				        complete({});
		        });
	    });
	connect(m_recording, &AudioRecording::devicesChanged, this, &AudioRecordingDialog::refreshDevices);
	connect(m_meters, &AudioRecordingMeters::resetRequested, m_recording, &AudioRecording::resetMeters);
	connect(m_recording, &AudioRecording::changed, this, [this] {
		const auto &snapshot = m_recording->snapshot();
		if (!m_recording->busy() && snapshot.result.planValid &&
		    (m_info.directory != snapshot.result.directory || m_info.planSha256 != snapshot.result.planSha256))
			review(snapshot.result);
		refresh();
		if (m_closePending && !busy())
			finishDialog(QDialog::Rejected);
	});
	connect(refreshDevicesButton, &QPushButton::clicked, m_recording, &AudioRecording::refreshDevices);
	connect(m_record, &QPushButton::clicked, this, [this] { confirmReviewChange([this] { record(); }); });
	connect(m_import, &QPushButton::clicked, this, &AudioRecordingDialog::importTakes);
	connect(m_stop, &QPushButton::clicked, this, [this] {
		invalidateAudition();
		if (m_work)
			m_work->cancelled = true;
		m_recording->stop();
		refresh();
	});
	connect(m_armList, &QTreeWidget::currentItemChanged, this, [this] { loadArm(); });
	connect(m_armList, &QTreeWidget::itemChanged, this, [this] {
		if (!m_loading)
			refresh();
	});
	connect(m_channels, &QLineEdit::textChanged, this, [this] { saveArm(); });
	connect(m_monitor, &QCheckBox::toggled, this, [this] { saveArm(); });
	connect(m_replacePlayback, &QCheckBox::toggled, this, [this] { saveArm(); });
	connect(m_monitorGain, &QDoubleSpinBox::valueChanged, this, [this] { saveArm(); });
	connect(m_takeList, &QTreeWidget::currentItemChanged, this, [this] {
		invalidateAudition();
		loadTake();
	});
	connect(m_takeList, &QTreeWidget::itemChanged, this, [this] {
		if (!m_loading) {
			invalidateAudition();
			refresh();
		}
	});
	connect(m_target, &QComboBox::currentIndexChanged, this, [this] { saveTake(); });
	connect(m_takePass, &QSpinBox::valueChanged, this, [this](int pass) {
		if (m_loading || busy())
			return;
		const int row = m_takeList->indexOfTopLevelItem(m_takeList->currentItem());
		if (row < 0 || row >= m_selections.size())
			return;
		auto &selection = m_selections[row];
		const auto length = m_info.plan.pass.punchEnd - m_info.plan.pass.punchFirst;
		invalidateAudition();
		selection.loopPass = pass - 1;
		selection.first = 0;
		selection.end = std::min(length, m_info.takes[row].frames - length * selection.loopPass);
		if (m_recordedPlacement[row])
			selection.position = m_info.takes[row].metadata.position;
		loadTake();
		refresh();
	});
	for (auto *box : {m_first, m_end, m_position})
		connect(box, &QDoubleSpinBox::valueChanged, this, [this] { saveTake(); });
	connect(m_storedChannels, &QLineEdit::textChanged, this, [this] { saveTake(); });
	connect(m_replaceExisting, &QCheckBox::toggled, this, [this] { saveTake(); });
	connect(m_useRecordedPlacement, &QCheckBox::toggled, this, [this] { saveTake(); });
	connect(m_acceptInterrupted, &QCheckBox::toggled, this, [this] {
		invalidateAudition();
		refresh();
	});
	connect(m_group, &QCheckBox::toggled, this, [this] {
		invalidateAudition();
		refresh();
	});
	connect(m_comp, &QCheckBox::toggled, this, [this](bool enabled) {
		if (m_loading)
			return;
		invalidateAudition();
		if (enabled)
			m_useQueue->setChecked(true);
		refreshCompSections();
		refresh();
	});
	connect(m_useQueue, &QCheckBox::toggled, this, [this](bool enabled) {
		if (m_loading)
			return;
		invalidateAudition();
		if (!enabled)
			m_comp->setChecked(false);
		refresh();
	});
	connect(m_crossfade, &QDoubleSpinBox::valueChanged, this, [this] {
		invalidateAudition();
		m_problem.clear();
		refreshCompSections();
		refresh();
	});
	connect(m_addSection, &QPushButton::clicked, this, [this] { editCompSection(false); });
	connect(m_updateSection, &QPushButton::clicked, this, [this] { editCompSection(true); });
	connect(m_compList, &QTreeWidget::currentItemChanged, this, [this] {
		invalidateAudition();
		loadCompSection();
	});
	connect(m_removeSection, &QPushButton::clicked, this, [this] {
		const int row = m_compList->indexOfTopLevelItem(m_compList->currentItem());
		if (!busy() && row >= 0 && row < m_compSections.size()) {
			invalidateAudition();
			m_compSections.removeAt(row);
			m_problem.clear();
			refreshCompSections();
			loadCompSection();
		}
	});
	connect(m_tabs, &QTabWidget::currentChanged, this, &AudioRecordingDialog::refresh);
	connect(m_path, &QLineEdit::textChanged, this, &AudioRecordingDialog::refresh);
	connect(m_name, &QLineEdit::textChanged, this, &AudioRecordingDialog::refresh);
	connect(m_inputChannels, &QSpinBox::valueChanged, this, &AudioRecordingDialog::refresh);
	connect(m_loopPasses, &QSpinBox::valueChanged, this, &AudioRecordingDialog::refresh);
	for (auto *box : {m_playbackFirst, m_punchFirst, m_punchEnd})
		connect(box, &QDoubleSpinBox::valueChanged, this, &AudioRecordingDialog::refresh);
	connect(m_input, &QComboBox::currentIndexChanged, this, [this] {
		int maximum = 32;
		for (const auto &device : m_recording->devices())
			if (device.id == m_input->currentData().toByteArray())
				maximum = device.inputChannels;
		m_inputChannels->setMaximum(std::max(1, maximum));
		refresh();
	});
	connect(m_output, &QComboBox::currentIndexChanged, this, &AudioRecordingDialog::refresh);
	if (m_armList->topLevelItemCount())
		m_armList->setCurrentItem(m_armList->topLevelItem(0));
	refreshDevices();
	loadTake();
	for (auto *button : findChildren<QPushButton *>())
		button->setAutoDefault(false);
	refresh();
}

AudioRecordingDialog::~AudioRecordingDialog()
{
	++m_auditionEpoch;
	if (m_work)
		m_work->cancelled = true;
	if (m_thread)
		m_thread->wait();
	disconnect(m_recording, nullptr, this, nullptr);
	delete m_recording;
	disconnect(m_audition, nullptr, this, nullptr);
	delete m_audition;
}
bool AudioRecordingDialog::busy() const
{
	return bool(m_work) || m_recording->busy() || m_auditionWaiting || m_closingOutput;
}
void AudioRecordingDialog::reject()
{
	if (m_closePending)
		return;
	if (reviewModified() && !m_reviewDiscardApproved) {
		confirmReviewChange([this] {
			m_reviewDiscardApproved = true;
			reject();
		});
		return;
	}
	m_closePending = true;
	invalidateAudition();
	if (m_work)
		m_work->cancelled = true;
	m_recording->stop();
	if (!busy())
		finishDialog(QDialog::Rejected);
	else
		refresh();
}
void AudioRecordingDialog::closeEvent(QCloseEvent *event)
{
	event->ignore();
	reject();
}
void AudioRecordingDialog::finishDialog(int result)
{
	if (m_closingOutput)
		return;
	m_closingOutput = true;
	invalidateAudition();
	m_audition->stopAndWait(this, [this, result](bool stopped) {
		m_closingOutput = false;
		if (!stopped) {
			m_closePending = false;
			m_problem = tr("Audition changed while closing review. Stop playback and try again.");
			refresh();
			return;
		}
		QDialog::done(m_closePending ? QDialog::Rejected : result);
	});
	refresh();
}
void AudioRecordingDialog::createAuditionControls(QFormLayout *reviewForm)
{
	m_auditionPlaySection = new QPushButton(tr("Audition Current Section"));
	m_auditionPlaySection->setObjectName("recordingAuditionPlaySection");
	m_auditionPlaySection->setToolTip(
	    tr("Hear the focused take's current pass, range, channels and placement without importing it."));
	int row = -1;
	QFormLayout::ItemRole role;
	reviewForm->getWidgetPosition(m_comp->parentWidget(), &row, &role);
	reviewForm->insertRow(row < 0 ? reviewForm->rowCount() : row, m_auditionPlaySection);
	m_auditionPlayReview = new QPushButton(tr("Audition Review"));
	m_auditionPlayReview->setObjectName("recordingAuditionPlayReview");
	m_auditionPlayReview->setToolTip(
	    tr("Hear all queued comp sections or checked takes, with the reviewed placement, replacement and fades."));
	reviewForm->getWidgetPosition(m_import, &row, &role);
	reviewForm->insertRow(row, m_auditionPlayReview);
	auto *form = tabForm(m_tabs, tr("Audition"), &m_auditionForm);
	m_auditionBacking = toggle(form, tr("Include backing clips"));
	m_auditionBacking->setObjectName("recordingAuditionBacking");
	m_auditionBacking->setChecked(true);
	m_auditionBacking->setToolTip(
	    tr("Turn off to hear only the reviewed clips. Both modes retain track, bus and master effects, routing, mute "
	       "and solo. Audition spans the imported clips without added preroll or tails."));
	m_auditionOutput = new QComboBox;
	m_auditionOutput->setObjectName("recordingAuditionOutput");
	m_auditionOutput->setAccessibleName(tr("Recording review audition output device"));
	m_auditionOutput->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_auditionOutput->setMinimumContentsLength(20);
	m_auditionOutput->addItem(tr("System default output"), QByteArray{});
	form->addRow(tr("Output"), m_auditionOutput);
	auto *outputs = new QPushButton(tr("Refresh Audition Outputs"));
	outputs->setObjectName("recordingAuditionRefresh");
	form->addRow(outputs);
	m_auditionBuffer = new QComboBox;
	m_auditionBuffer->setObjectName("recordingAuditionBuffer");
	m_auditionBuffer->setAccessibleName(tr("Requested audition output buffer in frames"));
	for (int frames : {256, 512, 1024, 2048, 4096, 8192, 16384})
		m_auditionBuffer->addItem(tr("%1 frames").arg(frames), frames);
	m_auditionBuffer->setCurrentIndex(m_auditionBuffer->findData(2048));
	form->addRow(tr("Output buffer"), m_auditionBuffer);
	auto *volume = new QSlider(Qt::Horizontal);
	volume->setObjectName("recordingAuditionVolume");
	volume->setAccessibleName(tr("Recording review audition volume"));
	volume->setRange(0, 100);
	volume->setValue(70);
	volume->setToolTip(tr("Playback volume only; imported clips and CLI preview exports retain their original level."));
	form->addRow(tr("Volume"), volume);
	m_auditionLoop = toggle(form, tr("Repeat audition range"));
	m_auditionLoop->setObjectName("recordingAuditionLoop");
	m_auditionPosition = frameBox(tr("Audition timeline position in frames"), "recordingAuditionPosition");
	form->addRow(tr("Position"), m_auditionPosition);
	m_auditionReplay = new QPushButton(tr("Play Again"));
	m_auditionReplay->setObjectName("recordingAuditionReplay");
	m_auditionReplay->setToolTip(
	    tr("Reverify and play the current section or whole review again using the current choices."));
	form->addRow(m_auditionReplay);
	m_auditionPause = new QPushButton(tr("Pause"));
	m_auditionPause->setObjectName("recordingAuditionPause");
	form->addRow(m_auditionPause);
	m_auditionStatus = new QLabel;
	m_auditionStatus->setObjectName("recordingAuditionStatus");
	m_auditionStatus->setAccessibleName(tr("Recording audition state, range and output progress"));
	m_auditionStatus->setTextFormat(Qt::PlainText);
	m_auditionStatus->setWordWrap(true);
	form->addRow(m_auditionStatus);
	connect(m_auditionPlaySection, &QPushButton::clicked, this, [this] { audition(true); });
	connect(m_auditionPlayReview, &QPushButton::clicked, this, [this] { audition(false); });
	connect(m_auditionReplay, &QPushButton::clicked, this, [this] { audition(m_auditionSection); });
	connect(m_auditionPause, &QPushButton::clicked, this, [this] {
		if (m_audition->snapshot().state == AudioSessionPlaybackSnapshot::State::Paused)
			m_audition->resume();
		else
			m_audition->pause();
	});
	connect(volume, &QSlider::valueChanged, this, [this](int value) { m_audition->setVolume(float(value) / 100); });
	connect(m_auditionLoop, &QCheckBox::toggled, m_audition, &AudioSessionPlayback::setLoop);
	connect(m_auditionPosition, &QDoubleSpinBox::valueChanged, this,
	        [this](double frame) { m_audition->seek(qint64(frame)); });
	connect(m_auditionOutput, &QComboBox::currentIndexChanged, this, [this] {
		m_auditionOutput->setToolTip(m_auditionOutput->currentText());
		invalidateAudition();
		refresh();
	});
	connect(m_auditionBuffer, &QComboBox::currentIndexChanged, this, [this] {
		invalidateAudition();
		refresh();
	});
	connect(m_auditionBacking, &QCheckBox::toggled, this, [this] {
		invalidateAudition();
		refresh();
	});
	connect(outputs, &QPushButton::clicked, this, [this] {
		invalidateAudition();
		m_audition->refreshOutputs();
	});
	connect(m_audition, &AudioSessionPlayback::changed, this, &AudioRecordingDialog::refresh);
	connect(m_audition, &AudioSessionPlayback::outputsChanged, this, [this] {
		const auto selected = m_auditionOutput->currentData();
		const QSignalBlocker blocked(m_auditionOutput);
		m_auditionOutput->clear();
		m_auditionOutput->addItem(tr("System default output"), QByteArray{});
		for (const auto &device : m_audition->outputs())
			m_auditionOutput->addItem(device.name, device.id);
		int index = m_auditionOutput->findData(selected);
		if (index < 0) {
			m_auditionOutput->addItem(tr("Unavailable output"), selected);
			index = m_auditionOutput->count() - 1;
		}
		m_auditionOutput->setCurrentIndex(index);
		m_auditionOutput->setToolTip(m_auditionOutput->currentText());
		refresh();
	});
}
void AudioRecordingDialog::invalidateAudition()
{
	++m_auditionEpoch;
	m_auditionWaiting = false;
	m_auditionReady = false;
	if (m_work && m_work->auditioning)
		m_work->cancelled = true;
	if (m_audition)
		m_audition->stop();
}
AudioRecordingImportRequest AudioRecordingDialog::auditionRequest(bool currentSection) const
{
	auto request = importRequest();
	if (currentSection) {
		request.comp = false;
		request.crossfadeFrames = 0;
		request.groupRegions = false;
		request.selections.clear();
		const int row = m_takeList->indexOfTopLevelItem(m_takeList->currentItem());
		if (row >= 0 && row < m_selections.size())
			request.selections << m_selections[row];
	}
	return request;
}
void AudioRecordingDialog::audition(bool currentSection)
{
	if (busy() || m_closePending || !m_audition->available())
		return;
	saveTake();
	const auto request = auditionRequest(currentSection);
	const auto prepared = prepareAudioRecordingImport(m_session, m_info, request);
	if (!prepared.succeeded()) {
		m_problem = prepared.error;
		refresh();
		return;
	}
	invalidateAudition();
	m_problem.clear();
	m_auditionSection = currentSection;
	m_auditionWaiting = true;
	const auto epoch = m_auditionEpoch;
	const bool backing = m_auditionBacking->isChecked();
	m_tabs->setCurrentWidget(m_auditionForm->parentWidget()->parentWidget());
	refresh();
	const auto ready = [guard = QPointer<AudioRecordingDialog>(this), epoch, request, backing](QString error) {
		if (!guard || guard->m_auditionEpoch != epoch || !guard->m_auditionWaiting || guard->m_closePending)
			return;
		guard->m_auditionWaiting = false; // Ignore duplicate/late external acknowledgements.
		if (!error.isEmpty()) {
			guard->m_problem = error;
			guard->refresh();
			return;
		}
		const auto base = guard->m_session;
		guard->launch(
		    [base, request, backing](AudioRecordingDialogWork &work) {
			    work.audition = prepareAudioRecordingAudition(base, request, backing, work.control());
			    work.error = work.audition.imported.error;
			    if (work.audition.imported.cancelled)
				    work.cancelled = true;
		    },
		    WorkKind::Audition);
	};
	m_audition->stopAndWait(this, [guard = QPointer<AudioRecordingDialog>(this), epoch, ready](bool stopped) {
		if (!guard || guard->m_auditionEpoch != epoch || !guard->m_auditionWaiting)
			return;
		if (!stopped) {
			ready(tr("Audition changed while preparing playback. Stop playback and try again."));
			return;
		}
		if (guard->m_beforePlayback)
			guard->m_beforePlayback(guard, ready);
		else
			ready({});
	});
}
void AudioRecordingDialog::refreshAudition()
{
	const bool working = busy();
	m_auditionForm->setEnabled(!working);
	const bool canStart = !working && !m_closePending && m_info.planValid && m_audition->available();
	m_auditionPlaySection->setEnabled(canStart && m_takeList->currentItem());
	m_auditionPlayReview->setEnabled(canStart && !importRequest().selections.isEmpty());
	m_auditionReplay->setEnabled(canStart);
	using State = AudioSessionPlaybackSnapshot::State;
	const auto &snapshot = m_audition->snapshot();
	m_auditionPause->setText(snapshot.state == State::Paused ? tr("Resume") : tr("Pause"));
	m_auditionPause->setEnabled(!working && (snapshot.state == State::Playing || snapshot.state == State::Paused));
	m_auditionPosition->setEnabled(m_auditionReady && m_auditionPause->isEnabled());
	if (m_auditionReady) {
		const QSignalBlocker blocked(m_auditionPosition);
		m_auditionPosition->setRange(double(m_auditionFirst), double(m_auditionEnd));
		m_auditionPosition->setValue(double(snapshot.position));
	}
	QString state;
	switch (snapshot.state) {
	case State::Unavailable:
		state = tr("Audition playback is unavailable in this build.");
		break;
	case State::Stopped:
		state = tr("Audition stopped.");
		break;
	case State::Preparing:
		state = tr("Preparing audition output…");
		break;
	case State::Playing:
		state = tr("Playing");
		break;
	case State::Paused:
		state = tr("Paused");
		break;
	case State::Ended:
		state = tr("Audition finished.");
		break;
	case State::Error:
		state = tr("Audition failed: %1").arg(snapshot.error);
		break;
	}
	if (m_auditionReady)
		state +=
		    tr("\n%1 · range %2–%3 · position %4\nUnderruns %5 · samples above full scale %6")
		        .arg(m_auditionSection ? tr("Current section") : tr("Reviewed import"),
		             numericText(locale().toString(m_auditionFirst)), numericText(locale().toString(m_auditionEnd)),
		             numericText(locale().toString(snapshot.position)),
		             numericText(locale().toString(snapshot.underruns)),
		             numericText(locale().toString(snapshot.samplesAboveFullScale)));
	if (m_auditionWaiting)
		state = tr("Waiting for existing playback to release its output…");
	if (m_work && m_work->auditioning)
		state = tr("Verifying recorded audio and preparing audition…");
	m_auditionStatus->setText(state);
	m_auditionStatus->setAccessibleDescription(state);
}

void AudioRecordingDialog::refreshDevices()
{
	const auto input = m_input->currentData(), output = m_output->currentData();
	const QSignalBlocker inputBlock(m_input), outputBlock(m_output);
	m_input->clear();
	m_output->clear();
	m_input->addItem(tr("Choose input device"), QByteArray{});
	m_output->addItem(tr("Choose output device"), QByteArray{});
	for (const auto &device : m_recording->devices()) {
		const auto text = tr("%1 — %2").arg(device.name, device.host);
		if (device.inputChannels > 0)
			m_input->addItem(text, device.id);
		if (device.outputChannels >= 2)
			m_output->addItem(text, device.id);
	}
	m_input->setCurrentIndex(std::max(0, m_input->findData(input)));
	m_output->setCurrentIndex(std::max(0, m_output->findData(output)));
	refresh();
}
void AudioRecordingDialog::loadArm()
{
	const int row = m_armList->indexOfTopLevelItem(m_armList->currentItem());
	const bool valid = row >= 0 && row < m_arms.size();
	m_loading = true;
	for (QWidget *control : std::array<QWidget *, 4>{m_channels, m_monitor, m_replacePlayback, m_monitorGain})
		control->setEnabled(valid);
	if (valid) {
		const auto &arm = m_arms[row];
		QVector<int> channels;
		for (int c = 0; c < arm.channels && c < 2; ++c)
			channels << arm.channelMap[size_t(c)];
		m_channels->setText(channelText(channels));
		m_monitor->setChecked(arm.monitor);
		m_replacePlayback->setChecked(arm.replacePlayback);
		m_monitorGain->setValue(arm.monitorGain > 0 ? 20 * std::log10(arm.monitorGain) : -60);
		m_replacePlayback->setEnabled(arm.monitor);
	}
	m_loading = false;
}
void AudioRecordingDialog::saveArm()
{
	if (m_loading || busy())
		return;
	const int row = m_armList->indexOfTopLevelItem(m_armList->currentItem());
	if (row < 0 || row >= m_arms.size())
		return;
	auto &arm = m_arms[row];
	const auto channels = channelList(m_channels->text());
	arm.channels = int(channels.size());
	for (int c = 0; c < channels.size() && c < 2; ++c)
		arm.channelMap[size_t(c)] = channels[c];
	arm.monitor = m_monitor->isChecked();
	arm.replacePlayback = arm.monitor && m_replacePlayback->isChecked();
	arm.monitorGain = std::pow(10.0, m_monitorGain->value() / 20);
	m_loading = true;
	m_replacePlayback->setEnabled(arm.monitor);
	if (!arm.monitor)
		m_replacePlayback->setChecked(false);
	m_armList->topLevelItem(row)->setText(1, m_channels->text());
	m_loading = false;
	refresh();
}
void AudioRecordingDialog::record()
{
	if (busy())
		return;
	invalidateAudition();
	saveArm();
	m_problem = recordingProblem();
	if (!m_problem.isEmpty()) {
		refresh();
		return;
	}
	AudioRecordingRequest request;
	request.directory = m_path->text().trimmed();
	request.plan.name = m_name->text().trimmed();
	request.plan.sourceSessionPath = m_sessionPath;
	request.plan.sampleRate = m_session.sampleRate;
	request.device = {m_input->currentData().toByteArray(), m_output->currentData().toByteArray(), m_session.sampleRate,
	                  m_inputChannels->value(), m_buffer->currentData().toInt()};
	for (const auto &device : m_recording->devices())
		if (device.id == request.device.inputId)
			request.plan.inputDeviceName = device.name;
	auto &pass = request.plan.pass;
	pass.playbackFirst = qint64(m_playbackFirst->value());
	pass.punchFirst = qint64(m_punchFirst->value());
	pass.punchEnd = qint64(m_punchEnd->value());
	pass.loopPasses = m_loopPasses->value();
	m_livePasses = pass.loopPasses;
	m_livePassFrames = pass.punchEnd - pass.punchFirst;
	pass.inputChannels = request.device.inputChannels;
	pass.blockFrames = std::min(1024, request.device.bufferFrames);
	pass.clockToleranceFrames = m_clockTolerance->value();
	pass.calibrationFrames = m_calibration->value();
	pass.outputGain = .7;
	for (int i = 0; i < m_arms.size(); ++i)
		if (m_armList->topLevelItem(i)->checkState(0) == Qt::Checked)
			pass.arms << m_arms[i];
	m_meters->setPass(m_session, request.plan);
	if (m_recording->start(m_session, request) && m_recording->busy())
		m_tabs->setCurrentIndex(2);
	refresh();
}

QString AudioRecordingDialog::recordingProblem() const
{
	if (m_arms.isEmpty())
		return tr("Add an audio track to the session before recording.");
	if (m_name->text().trimmed().isEmpty())
		return tr("Enter a recording name.");
	if (!m_path->text().trimmed().endsWith(".vsrecord", Qt::CaseInsensitive))
		return tr("Choose a new folder ending in .vsrecord.");
	if (m_input->currentData().toByteArray().isEmpty() || m_output->currentData().toByteArray().isEmpty())
		return tr("Choose both the recording input and playback output.");
	if (m_playbackFirst->value() > m_punchFirst->value() || m_punchEnd->value() <= m_punchFirst->value())
		return tr("Play from must be at or before punch in, and punch out must be later than punch in.");
	if (qint64(m_punchEnd->value() - m_punchFirst->value()) >
	    (AudioSessionFrameLimit - qint64(m_punchFirst->value())) / m_loopPasses->value())
		return tr("The requested passes exceed the recording timeline limit. Reduce the range or pass count.");
	int count = 0;
	for (int i = 0; i < m_arms.size(); ++i) {
		if (m_armList->topLevelItem(i)->checkState(0) != Qt::Checked)
			continue;
		++count;
		const auto &arm = m_arms[i];
		if (arm.channels < 1 || arm.channels > 2)
			return tr("Choose one or two distinct input channels for each armed track.");
		for (int channel = 0; channel < arm.channels; ++channel)
			if (arm.channelMap[size_t(channel)] < 0 || arm.channelMap[size_t(channel)] >= m_inputChannels->value())
				return tr("An armed track uses a channel outside the selected device input count.");
	}
	return count > 0 && count <= AudioDuplexArmLimit ? QString{} : tr("Arm between one and eight audio tracks.");
}

AudioRecordingImportRequest AudioRecordingDialog::importRequest() const
{
	AudioRecordingImportRequest request;
	request.directory = m_info.directory;
	request.expectedPlanSha256 = m_info.planSha256;
	request.expectedReceiptSha256 = m_info.receiptSha256;
	request.allowInterrupted = m_acceptInterrupted->isChecked();
	request.groupRegions = m_group->isChecked();
	request.comp = m_comp->isChecked();
	if (m_useQueue->isChecked()) {
		request.crossfadeFrames = request.comp ? qint64(m_crossfade->value()) : 0;
		request.selections = m_compSections;
		return request;
	}
	for (int i = 0; i < m_selections.size(); ++i)
		if (m_takeList->topLevelItem(i)->checkState(0) == Qt::Checked)
			request.selections << m_selections[i];
	return request;
}
void AudioRecordingDialog::editCompSection(bool replace)
{
	if (busy() || !m_useQueue->isChecked())
		return;
	saveTake();
	const int arm = m_takeList->indexOfTopLevelItem(m_takeList->currentItem());
	const int row = m_compList->indexOfTopLevelItem(m_compList->currentItem());
	if (arm < 0 || arm >= m_selections.size() || (replace && (row < 0 || row >= m_compSections.size())))
		return;
	auto candidate = importRequest();
	if (replace)
		candidate.selections[row] = m_selections[arm];
	else
		candidate.selections.append(m_selections[arm]);
	const auto prepared = prepareAudioRecordingImport(m_session, m_info, candidate);
	if (!prepared.succeeded())
		m_problem = prepared.error;
	else {
		m_problem.clear();
		m_compSections = candidate.selections;
		refreshCompSections();
		m_compList->setCurrentItem(m_compList->topLevelItem(replace ? row : m_compSections.size() - 1));
	}
	refresh();
}
void AudioRecordingDialog::loadCompSection()
{
	if (busy() || m_loading)
		return;
	const int row = m_compList->indexOfTopLevelItem(m_compList->currentItem());
	if (row >= 0 && row < m_compSections.size()) {
		const auto &selection = m_compSections[row];
		const QSignalBlocker blocked(m_takeList);
		m_selections[selection.arm] = selection;
		m_recordedPlacement[selection.arm] =
		    selection.position == m_info.takes[selection.arm].metadata.position + selection.first;
		m_takeList->setCurrentItem(m_takeList->topLevelItem(selection.arm));
		loadTake();
	}
	refresh();
}
void AudioRecordingDialog::refreshCompSections()
{
	const QSignalBlocker blocked(m_compList);
	const int selected = m_compList->indexOfTopLevelItem(m_compList->currentItem());
	m_compList->clear();
	const auto plan = prepareAudioRecordingImport(m_session, m_info, importRequest());
	for (int i = 0; i < m_compSections.size(); ++i) {
		const auto &section = m_compSections[i];
		const auto found = std::find_if(m_session.tracks.cbegin(), m_session.tracks.cend(),
		                                [&](const auto &track) { return track.id == section.trackId; });
		const auto fades =
		    plan.succeeded() && i < plan.slices.size()
		        ? tr("%1 / %2").arg(locale().toString(plan.slices[i].fadeIn), locale().toString(plan.slices[i].fadeOut))
		        : tr("Not ready");
		new QTreeWidgetItem(
		    m_compList, {found == m_session.tracks.cend() ? section.trackId : found->name,
		                 numericText(locale().toString(section.loopPass + 1)),
		                 numericText(tr("%1–%2").arg(locale().toString(section.first), locale().toString(section.end))),
		                 numericText(locale().toString(section.position)), numericText(fades)});
	}
	if (!m_compSections.isEmpty())
		m_compList->setCurrentItem(m_compList->topLevelItem(std::clamp(selected, 0, int(m_compSections.size() - 1))));
}
void AudioRecordingDialog::loadTake()
{
	const int row = m_takeList->indexOfTopLevelItem(m_takeList->currentItem());
	const bool valid = row >= 0 && row < m_selections.size() && m_info.takes[row].recoverable();
	m_loading = true;
	for (auto *control : std::array<QWidget *, 8>{m_target, m_takePass, m_first, m_end, m_position, m_storedChannels,
	                                              m_replaceExisting, m_useRecordedPlacement})
		control->setEnabled(valid);
	if (valid) {
		const auto &selection = m_selections[row];
		m_target->setCurrentIndex(std::max(0, m_target->findData(selection.trackId)));
		const auto length = m_info.plan.pass.punchEnd - m_info.plan.pass.punchFirst;
		const auto available =
		    std::min<qint64>(m_info.plan.pass.loopPasses, (m_info.takes[row].frames - 1) / length + 1);
		m_takePass->setMaximum(int(available));
		m_takePass->setValue(selection.loopPass + 1);
		m_takePass->setEnabled(available > 1);
		const auto end = std::min(length, m_info.takes[row].frames - length * selection.loopPass);
		m_first->setMaximum(double(std::max<qint64>(0, end - 1)));
		m_end->setMaximum(double(end));
		m_first->setValue(double(selection.first));
		m_end->setValue(double(selection.end));
		m_position->setValue(double(selection.position));
		m_useRecordedPlacement->setChecked(m_recordedPlacement[row]);
		m_position->setReadOnly(m_recordedPlacement[row]);
		m_storedChannels->setText(channelText(selection.channels));
		m_replaceExisting->setChecked(selection.replaceExisting);
	}
	m_loading = false;
}
void AudioRecordingDialog::saveTake()
{
	if (m_loading || busy())
		return;
	const int row = m_takeList->indexOfTopLevelItem(m_takeList->currentItem());
	if (row < 0 || row >= m_selections.size())
		return;
	invalidateAudition();
	auto &selection = m_selections[row];
	selection.trackId = m_target->currentData().toString();
	selection.first = qint64(m_first->value());
	selection.end = qint64(m_end->value());
	m_recordedPlacement[row] = m_useRecordedPlacement->isChecked();
	m_position->setReadOnly(m_recordedPlacement[row]);
	if (m_recordedPlacement[row]) {
		const QSignalBlocker block(m_position);
		m_position->setValue(double(m_info.takes[row].metadata.position + selection.first));
	}
	selection.position = qint64(m_position->value());
	selection.channels = channelList(m_storedChannels->text());
	selection.replaceExisting = m_replaceExisting->isChecked();
	refresh();
}
void AudioRecordingDialog::review(const AudioRecordingInfo &info)
{
	const QSignalBlocker compBlock(m_comp), queueBlock(m_useQueue), acceptBlock(m_acceptInterrupted);
	m_info = info;
	m_reviewIdentity = {};
	const QSignalBlocker block(m_takeList);
	m_takeList->clear();
	m_selections.clear();
	m_compSections.clear();
	m_compList->clear();
	m_comp->setChecked(false);
	m_useQueue->setChecked(false);
	m_recordedPlacement.clear();
	m_acceptInterrupted->setChecked(false);
	const bool interrupted =
	    !info.receiptMatches || info.receipt.outcome == AudioRecordingReceipt::Outcome::Interrupted;
	m_acceptInterrupted->parentWidget()->setVisible(interrupted);
	m_reviewStatus->setText(tr("%1\n%2\n%3")
	                            .arg(info.plan.name, info.directory,
	                                 info.error.isEmpty()
	                                     ? (interrupted ? tr("Interrupted pass: review each verified prefix.")
	                                                    : tr("Verified recording pass."))
	                                     : info.error));
	for (int i = 0; i < info.takes.size(); ++i) {
		const auto &take = info.takes[i];
		const auto found = std::find_if(m_session.tracks.cbegin(), m_session.tracks.cend(),
		                                [&](const auto &track) { return track.id == take.metadata.trackId; });
		const auto track = found == m_session.tracks.cend() ? take.metadata.trackId : found->name;
		auto *item = new QTreeWidgetItem(m_takeList, {track, locale().toString(take.frames),
		                                              !take.recoverable() ? tr("Unavailable")
		                                              : take.complete     ? tr("Complete")
		                                                                  : tr("Incomplete")});
		item->setCheckState(0, take.recoverable() ? Qt::Checked : Qt::Unchecked);
		if (!take.recoverable())
			item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
		item->setToolTip(2, take.error);
		const auto length = info.plan.pass.punchEnd - info.plan.pass.punchFirst;
		item->setText(3, tr("%1 complete · %2 frames in next pass")
		                     .arg(locale().toString(take.frames / length), locale().toString(take.frames % length)));
		AudioRecordingSelection selection;
		selection.arm = i;
		selection.expectedPrefixSha256 = take.prefixSha256;
		selection.end = std::min(take.frames, info.plan.pass.punchEnd - info.plan.pass.punchFirst);
		selection.position = take.metadata.position;
		selection.trackId = take.metadata.trackId;
		selection.replaceExisting = info.planValid && info.plan.pass.arms[i].replacePlayback;
		for (int c = 0; c < take.metadata.channelMap.size(); ++c)
			selection.channels << c;
		m_selections << selection;
		m_recordedPlacement << true;
	}
	if (m_takeList->topLevelItemCount())
		m_takeList->setCurrentItem(m_takeList->topLevelItem(0));
	loadTake();
	m_savedReview = audioRecordingImportRequestJson(importRequest());
	m_tabs->setCurrentIndex(1);
}
void AudioRecordingDialog::restoreReview(const AudioRecordingReviewResult &opened)
{
	review(opened.recording);
	const QSignalBlocker compBlock(m_comp), queueBlock(m_useQueue), acceptBlock(m_acceptInterrupted),
	    groupBlock(m_group), fadeBlock(m_crossfade);
	m_compSections = opened.review.selections;
	m_useQueue->setChecked(true);
	m_comp->setChecked(opened.review.comp);
	m_crossfade->setValue(double(opened.review.crossfadeFrames));
	m_acceptInterrupted->setChecked(opened.review.allowInterrupted);
	m_group->setChecked(opened.review.groupRegions);
	m_reviewIdentity = opened.identity;
	refreshCompSections();
	loadCompSection();
	m_savedReview = audioRecordingImportRequestJson(importRequest());
}
bool AudioRecordingDialog::reviewModified() const
{
	return m_info.planValid && audioRecordingImportRequestJson(importRequest()) != m_savedReview;
}
void AudioRecordingDialog::confirmReviewChange(std::function<void()> proceed)
{
	if (!reviewModified()) {
		proceed();
		return;
	}
	if (m_reviewGuard)
		return;
	auto *box = new QMessageBox(QMessageBox::Warning, tr("Unsaved Recording Review"),
	                            tr("Save the changed recording review before continuing? Recorded audio is retained."),
	                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
	box->setObjectName("recordingUnsavedReview");
	box->setLayoutDirection(layoutDirection());
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->setDefaultButton(QMessageBox::Cancel);
	box->button(QMessageBox::Save)->setEnabled(!busy() && m_saveReview->isEnabled());
	m_reviewGuard = box;
	connect(box, &QMessageBox::finished, this, [this, box, proceed](int) {
		m_reviewGuard = nullptr;
		const auto chosen = box->standardButton(box->clickedButton());
		if (chosen == QMessageBox::Discard)
			proceed();
		else if (chosen == QMessageBox::Save) {
			m_afterReviewSave = proceed;
			m_saveReview->click();
			if (!busy()) // The file chooser was cancelled.
				m_afterReviewSave = {};
		}
	});
	box->open();
}
void AudioRecordingDialog::refresh()
{
	if (!m_recording)
		return;
	const bool working = busy();
	m_recordForm->setEnabled(!working);
	m_reviewForm->setEnabled(!working);
	using AuditionState = AudioSessionPlaybackSnapshot::State;
	const auto auditionState = m_audition->snapshot().state;
	m_stop->setEnabled(working || auditionState == AuditionState::Preparing ||
	                   auditionState == AuditionState::Playing || auditionState == AuditionState::Paused);
	refreshAudition();
	int armed = 0;
	m_loading = true;
	const auto &snapshot = m_recording->snapshot();
	m_meters->setSnapshot(snapshot, m_recording->meterResetPending());
	for (int i = 0; i < m_arms.size(); ++i) {
		auto *row = m_armList->topLevelItem(i);
		const bool selected = row->checkState(0) == Qt::Checked;
		row->setText(2, m_recording->busy() && selected && armed < AudioDuplexArmLimit
		                    ? locale().toString(snapshot.storedFrames[size_t(armed)])
		                    : QString{});
		if (selected)
			++armed;
	}
	m_loading = false;
	const auto recordProblem = recordingProblem();
	m_record->setEnabled(!working && m_recording->available() && recordProblem.isEmpty());
	m_record->setToolTip(recordProblem);
	const auto request = importRequest();
	const auto prepared = prepareAudioRecordingImport(m_session, m_info, request);
	const bool interrupted =
	    !m_info.receiptMatches || m_info.receipt.outcome == AudioRecordingReceipt::Outcome::Interrupted;
	m_import->setEnabled(!working && prepared.succeeded() && (!interrupted || request.allowInterrupted));
	m_import->setToolTip(prepared.error);
	m_import->setText(request.comp              ? tr("Import Comp")
	                  : m_useQueue->isChecked() ? tr("Import Queued Takes")
	                                            : tr("Import Selected Takes"));
	m_saveReview->setEnabled(!working && prepared.succeeded() && (m_reviewIdentity.path.isEmpty() || reviewModified()));
	m_saveReviewAs->setEnabled(!working && prepared.succeeded());
	m_saveReview->setToolTip(prepared.error);
	m_saveReviewAs->setToolTip(prepared.error);
	const auto reviewStatus =
	    m_reviewIdentity.path.isEmpty()
	        ? (reviewModified() ? tr("Review changes have not been saved.") : tr("Review not saved."))
	        : tr("%1\n%2").arg(m_reviewIdentity.path,
	                           reviewModified() ? tr("Unsaved review changes") : tr("Review saved"));
	m_reviewFileStatus->setText(reviewStatus);
	m_reviewFileStatus->setAccessibleDescription(reviewStatus);
	m_compForm->setVisible(m_useQueue->isChecked());
	m_crossfade->setEnabled(m_comp->isChecked());
	{
		const QSignalBlocker blocked(m_takeList);
		m_takeList->headerItem()->setText(0, m_useQueue->isChecked() ? tr("Source / Track") : tr("Import / Track"));
		for (int i = 0; i < m_takeList->topLevelItemCount(); ++i) {
			auto *item = m_takeList->topLevelItem(i);
			item->setFlags(m_useQueue->isChecked() ? item->flags() & ~Qt::ItemIsUserCheckable
			                                       : item->flags() | Qt::ItemIsUserCheckable);
		}
	}
	const int compRow = m_compList->indexOfTopLevelItem(m_compList->currentItem());
	m_addSection->setEnabled(!working && m_info.planValid && m_takeList->currentItem() &&
	                         m_compSections.size() < AudioSessionSourceLimit);
	m_updateSection->setEnabled(!working && compRow >= 0);
	m_removeSection->setEnabled(!working && compRow >= 0);
	m_compSummary->setText(m_compSections.isEmpty() ? tr("No queued sections.")
	                       : prepared.succeeded()
	                           ? (request.comp ? tr("Comp ready · Sections: %1") : tr("Review ready · Selections: %1"))
	                                 .arg(locale().toString(m_compSections.size()))
	                           : prepared.error);
	m_progress->setVisible(working);
	m_progress->setRange(0, StudioSettings().accessibilityPreferences().reducedMotion ? 1 : 0);
	m_progress->setTextVisible(false);
	using State = AudioRecordingSnapshot::State;
	QString message;
	switch (snapshot.state) {
	case State::Unavailable:
		message = tr("Synchronized recording is unavailable in this build. Recorded passes can still be reviewed.");
		break;
	case State::Ready:
		message = recordProblem.isEmpty() ? tr("Choose devices and arm up to eight tracks, then press Record.")
		                                  : recordProblem;
		break;
	case State::Permission:
		message = tr("Waiting for microphone permission…");
		break;
	case State::WaitingForPlayback:
		message = tr("Waiting for existing playback to release its output…");
		break;
	case State::Preparing:
		message = tr("Preparing the recording graph and take files…");
		break;
	case State::Recording:
		message = m_livePasses > 1 && m_livePassFrames > 0
		              ? tr("Recording · %1 of %2 passes captured")
		                    .arg(locale().toString(snapshot.progress.capturedFrames / m_livePassFrames),
		                         locale().toString(m_livePasses))
		              : tr("Recording");
		break;
	case State::Draining:
		message = tr("Finishing output playback…");
		break;
	case State::Saving:
		message = tr("Saving queued audio and verifying take files…");
		break;
	case State::Finished:
		message = tr("Recording finished. Review the takes before importing.");
		break;
	case State::Error:
		message = tr("Recording stopped: %1").arg(snapshot.error);
		break;
	}
	if (m_recording->busy() || snapshot.progress.capturedFrames > 0)
		message +=
		    tr("\nCaptured %1 frames · queued %2 · round trip %3 frames · graph latency %4 frames")
		        .arg(locale().toString(snapshot.progress.capturedFrames), locale().toString(snapshot.queuedFrames),
		             locale().toString(snapshot.progress.roundTripFrames),
		             locale().toString(snapshot.progress.processingLatencyFrames));
	if (!m_problem.isEmpty())
		message = m_problem;
	else if (!working && m_tabs->currentIndex() == 1 && m_info.planValid)
		message = tr("Select takes and review their destination, range and placement before importing.");
	if (m_work)
		message = m_work->activity;
	if (m_auditionWaiting)
		message = tr("Waiting for existing playback to release its output…");
	if (m_closingOutput)
		message = tr("Waiting for audition output to close…");
	if (m_closePending)
		message = tr("Stopping and preserving recorded files before closing…");
	m_status->setText(message);
	m_status->setVisible(m_tabs->currentIndex() != 3 || working || !m_problem.isEmpty());
	if (m_details->isVisible() && m_info.planValid)
		m_details->setPlainText(
		    QString::fromUtf8(QJsonDocument(QJsonObject{{"verification", audioRecordingInfoJson(m_info)},
		                                                {"importPlan", audioRecordingImportRequestJson(request)}})
		                          .toJson(QJsonDocument::Indented)));
}
void AudioRecordingDialog::launch(std::function<void(AudioRecordingDialogWork &)> operation, WorkKind kind)
{
	m_problem.clear();
	m_work = std::make_shared<AudioRecordingDialogWork>();
	m_work->auditioning = kind == WorkKind::Audition;
	m_work->activity = kind == WorkKind::Audition     ? tr("Verifying recorded audio and preparing audition…")
	                   : kind == WorkKind::OpenReview ? tr("Opening and verifying saved review…")
	                   : kind == WorkKind::SaveReview
	                       ? tr("Verifying and saving recording review…")
	                       : tr("Verifying recording files and preparing the selected take ranges…");
	const auto work = m_work;
	m_thread = QThread::create([work, operation] {
		try {
			operation(*work);
		} catch (const std::exception &) {
			work->error = tr("The recording operation failed because a worker or memory allocation failed. Existing "
			                 "files and session edits were retained.");
		}
	});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this, [this, work, kind] {
		m_thread->deleteLater();
		m_thread = nullptr;
		m_work.reset();
		if (kind == WorkKind::SaveReview && work->saved.written) {
			m_reviewIdentity = work->saved.identity;
			m_savedReview = work->savedReview;
			if (work->cancelled)
				m_problem = tr("The review was saved before cancellation completed.");
		} else if (work->cancelled)
			m_problem = tr("Operation cancelled. Recorded files and the session are unchanged.");
		else if (!work->error.isEmpty())
			m_problem = work->error;
		else if (kind == WorkKind::Import) {
			m_imported = std::move(work->imported);
			if (!m_closePending) {
				finishDialog(QDialog::Accepted);
				return;
			}
		} else if (kind == WorkKind::Audition) {
			if (!m_closePending) {
				m_auditionFirst = work->audition.first;
				m_auditionEnd = work->audition.end;
				m_auditionReady = true;
				m_audition->start(work->audition.imported.session,
				                  {m_auditionFirst, m_auditionEnd, m_auditionLoop->isChecked()},
				                  {m_auditionOutput->currentData().toByteArray(), m_session.sampleRate,
				                   m_auditionBuffer->currentData().toInt()});
			}
		} else if (kind == WorkKind::OpenReview) {
			if (!m_closePending)
				restoreReview(work->opened);
		} else
			review(work->info);
		refresh();
		if (m_closePending)
			finishDialog(QDialog::Rejected);
		if (kind == WorkKind::SaveReview) {
			auto proceed = std::move(m_afterReviewSave);
			m_afterReviewSave = {};
			if (proceed && work->saved.written && !work->cancelled && !m_closePending)
				proceed();
		}
	});
	m_thread->start();
	refresh();
}
bool AudioRecordingDialog::inspectRecording(const QString &directory)
{
	if (busy())
		return false;
	invalidateAudition();
	launch(
	    [directory](AudioRecordingDialogWork &work) {
		    work.info = inspectAudioRecording(directory, work.control());
		    if (!work.info.planValid)
			    work.error = work.info.error;
	    },
	    WorkKind::Review);
	return true;
}
bool AudioRecordingDialog::openReview(const QString &path)
{
	if (busy() || m_closePending)
		return false;
	invalidateAudition();
	const auto base = m_session;
	launch(
	    [base, path](AudioRecordingDialogWork &work) {
		    work.opened = openAudioRecordingReview(base, path, work.control());
		    work.error = work.opened.error;
		    if (work.opened.cancelled)
			    work.cancelled = true;
	    },
	    WorkKind::OpenReview);
	return true;
}
bool AudioRecordingDialog::saveReview(const QString &path, bool overwrite)
{
	if (busy() || m_closePending)
		return false;
	saveTake();
	const auto request = importRequest();
	const auto base = m_session;
	AudioProjectSaveRequest output;
	output.path = path;
	output.overwrite = overwrite;
	output.protectedPath = m_sessionPath;
	if (overwrite && !m_reviewIdentity.path.isEmpty() && audioPathsReferToSameFile(path, m_reviewIdentity.path))
		output.expected = m_reviewIdentity;
	launch(
	    [base, request, output](AudioRecordingDialogWork &work) {
		    work.saved = writeAudioRecordingReview(base, request, output, work.control());
		    work.error = work.saved.error;
		    if (!work.saved.written && work.error.isEmpty())
			    work.error = tr("The recording review was not saved. Existing files and choices were retained.");
		    work.savedReview = audioRecordingImportRequestJson(request);
	    },
	    WorkKind::SaveReview);
	return true;
}
void AudioRecordingDialog::importTakes()
{
	if (busy())
		return;
	saveTake();
	const auto request = importRequest();
	const auto base = m_session;
	launch(
	    [request, base](AudioRecordingDialogWork &work) {
		    work.imported = importAudioRecording(base, request, work.control());
		    work.error = work.imported.error;
		    if (work.imported.cancelled)
			    work.cancelled = true;
	    },
	    WorkKind::Import);
}
} // namespace vibestudio
