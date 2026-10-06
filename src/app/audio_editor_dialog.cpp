#include "app/audio_editor_dialog.h"
#include "app/audio_placement_dialog.h"

#include "app/audio_analysis_dialog.h"
#include "app/audio_markers_dialog.h"
#include "app/audio_recovery.h"
#include "app/audio_recovery_dialog.h"
#include "app/audio_waveform_view.h"
#include "app/studio_icons.h"
#include "core/audio_resample.h"
#include "core/studio_settings.h"
#include "vibestudio_config.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <new>

namespace vibestudio
{
namespace
{
// Shared between audio editor windows. Never reads or replaces the user's
// operating-system clipboard, and retains exact float samples between projects.
AudioClip copiedAudio;

// Float equality treats opposite zero signs as equal. Native documents retain
// their bits, so edit/no-op decisions use the same exact-sample contract.
// Compare bounded blocks on the worker so cancellation remains observable.
bool sameAudioContent(const AudioClip& first, const AudioClip& second, const AudioWorkControl& control)
{
	if (first.channels != second.channels || first.sampleRate != second.sampleRate ||
	    first.markers != second.markers || first.samples.size() != second.samples.size()) {
		return false;
	}
	if (first.samples.constData() == second.samples.constData()) { return true; }
	for (qsizetype offset = 0; offset < first.samples.size(); offset += 4096) {
		if (control.cancelled && control.cancelled()) { return false; }
		const qsizetype count = std::min<qsizetype>(4096, first.samples.size() - offset);
		if (std::memcmp(first.samples.constData() + offset, second.samples.constData() + offset,
		                static_cast<size_t>(count) * sizeof(float)) != 0) { return false; }
	}
	return true;
}

// The scroll body's minimum height follows wrapped text. Buttons live outside
// that body, so larger fonts and translations cannot draw over the commit row.
class AudioOptionsSummary final : public QLabel {
  public:
	AudioOptionsSummary() { setWordWrap(true); }
	void setSummary(const QString& text)
	{
		setText(text);
		setAccessibleDescription(text);
		setMinimumHeight(std::max(0, heightForWidth(width())));
	}

  protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QLabel::resizeEvent(event);
		setMinimumHeight(std::max(0, heightForWidth(event->size().width())));
	}
};

QFormLayout* audioOptionsBody(QDialog& dialog, QDialogButtonBox* buttons)
{
	auto* outer = new QVBoxLayout(&dialog);
	auto* scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("audioOptionsScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto* content = new QWidget;
	auto* form = new QFormLayout(content);
	form->setContentsMargins(0, 0, 0, 0);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(content);
	outer->addWidget(scroll, 1);
	outer->addWidget(buttons);
	const int line = dialog.fontMetrics().height();
	const QSize preferred(std::clamp(line * 26, 420, 720), std::clamp(line * 18, 280, 600));
	dialog.resize(preferred.boundedTo(dialog.screen()->availableGeometry().size() - QSize(40, 40)));
	return form;
}

void addDeliveryPresets(QComboBox* combo)
{
	combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	combo->setMinimumContentsLength(18);
	for (const auto preset : {AudioDeliveryPreset::Wav, AudioDeliveryPreset::Doom, AudioDeliveryPreset::Quake,
	                          AudioDeliveryPreset::Quake2, AudioDeliveryPreset::Quake3}) {
		combo->addItem(audioDeliveryPresetLabel(preset), int(preset));
	}
}

QString suggestedSoundPath(const QString& sourceName, bool doom)
{
	QString base = QFileInfo(sourceName).completeBaseName();
	if (doom) {
		base = base.toUpper().remove(QRegularExpression(QStringLiteral("[^A-Z0-9_]")));
		if (base.startsWith(QStringLiteral("DS"))) {
			base.remove(0, 2);
		}
		return QStringLiteral("DS") + (base.isEmpty() ? QStringLiteral("SOUND") : base.left(6));
	}
	return QStringLiteral("sound/") + (base.isEmpty() ? QStringLiteral("untitled") : base) +
	       QStringLiteral(".wav");
}
} // namespace
struct AudioEditorWork {
	std::atomic_bool cancel{false};
	bool cancellable = true;
	bool playback = false; // UI-thread operation kind; the worker never reads it.
	bool contentChanged = true;
	AudioClipResult result;
	AssetAudioPeaks peaks;
	AudioWaveformData waveform;
	QByteArray wav;
	QString error;
	AudioProject project;
	AudioProjectIdentity identity;
	AudioProjectSaveReport saved;
	AudioClip fragment;
	AudioDeliveryResult delivery;
	AudioAnalysisResult analysis;
	AudioWorkControl control()
	{
		return {[this]() { return cancel.load(); }};
	}
	void discardPrepared()
	{
		result = {}; peaks = {}; waveform = {}; wav.clear(); project = {}; identity = {};
		saved = {}; fragment = {}; delivery = {}; analysis = {};
	}
};

AudioEditorDialog::AudioEditorDialog(QWidget* parent, std::unique_ptr<AudioPlaybackBackend> playbackBackend)
    : QDialog(parent)
{
	setObjectName(QStringLiteral("audioEditorDialog"));
	setWindowTitle(tr("Audio Editor[*]"));
	setAccessibleName(tr("Audio editor"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(980, 900);
	auto* outer = new QVBoxLayout(this);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setAccessibleName(tr("Audio document controls"));
	auto* contents = new QWidget;
	auto* layout = new QVBoxLayout(contents);
	layout->setContentsMargins(0, 0, 0, 0);
	scroll->setWidget(contents);
	outer->addWidget(scroll, 1);
	m_source = new QLabel(tr("Open WAV, MP3, FLAC, Ogg Vorbis or digital Doom DMX audio."));
	m_source->setTextFormat(Qt::PlainText);
	m_source->setWordWrap(true);
	m_source->setAccessibleName(tr("Audio source"));
	layout->addWidget(m_source);
	auto* files = new QToolBar(tr("Audio file and history commands"));
	files->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	layout->addWidget(files);
	const auto action = [this](QToolBar* bar, const QString& label, const QString& name, const QString& icon,
	                           const QKeySequence& shortcut, auto callback) {
		auto* item = bar->addAction(studioIcon(icon), label);
		item->setObjectName(name);
		item->setShortcut(shortcut);
		item->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		addAction(item);
		connect(item, &QAction::triggered, this, callback);
		return item;
	};
	m_new = action(files, tr("New…"), QStringLiteral("audioEditorNew"), QStringLiteral("new"),
	               QKeySequence::New, [this]() { chooseNew(); });
	m_open = action(files, tr("Open…"), QStringLiteral("audioEditorOpen"), QStringLiteral("folder-open"),
	                QKeySequence::Open, [this]() { chooseFile(); });
	m_save = action(files, tr("Save Project"), QStringLiteral("audioEditorSave"), QStringLiteral("save"),
	                QKeySequence::Save, [this]() { chooseProjectSave(); });
	m_saveAs = action(files, tr("Save As…"), QStringLiteral("audioEditorSaveAs"), QStringLiteral("save-as"),
	                  QKeySequence::SaveAs, [this]() { chooseProjectSave(true); });
	m_export =
	    action(files, tr("Export Audio…"), QStringLiteral("audioEditorExport"), QStringLiteral("export"),
	           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [this]() { chooseExport(); });
	m_export->setToolTip(tr("Export WAV or a game sound preset. Review cue and "
	                        "loop compatibility in the export settings."));
	m_toSession = action(files, tr("To Session"), QStringLiteral("audioEditorToSession"), QStringLiteral("audio"), {},
		[this]() { if (!isBusy() && clip().frameCount() > 0) { Q_EMIT sessionRequested(projectSnapshot()); } });
	m_toSession->setToolTip(tr("Add a copy of this document as an independently editable clip in the multitrack session."));
	files->addSeparator();
	m_undoAction = action(files, tr("Undo"), QStringLiteral("audioEditorUndo"), QStringLiteral("undo"),
	                      QKeySequence::Undo, [this]() { undo(); });
	m_redoAction = action(files, tr("Redo"), QStringLiteral("audioEditorRedo"), QStringLiteral("redo"),
	                      QKeySequence::Redo, [this]() { redo(); });
	m_recover = action(files, tr("Recoveries…"), QStringLiteral("audioEditorRecover"),
	                   QStringLiteral("history"), {}, [this]() { chooseRecovery(); });

	auto* edits = new QToolBar(tr("Audio editing commands"));
	edits->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	layout->addWidget(edits);
	m_cut = action(edits, tr("Cut"), QStringLiteral("audioEditorCut"), QStringLiteral("cut"), {},
	               [this]() { copySelection(true); });
	m_copy = action(edits, tr("Copy"), QStringLiteral("audioEditorCopy"), QStringLiteral("copy"), {},
	                [this]() { copySelection(); });
	m_paste = action(edits, tr("Paste"), QStringLiteral("audioEditorPaste"), QStringLiteral("paste"), {},
	                 [this]() { pasteSelection(); });
	m_copy->setToolTip(tr("Copy selected float samples to the shared VibeStudio audio clipboard."));
	m_paste->setToolTip(tr("Replace the selection, or insert at the cursor. Rate "
	                       "and channel count must match."));
	edits->addSeparator();
	const auto edit = [this, edits](const QString& label, const QString& id) {
		auto* item = edits->addAction(label);
		item->setObjectName(QStringLiteral("audioEdit-") + id);
		item->setToolTip(tr("Apply %1 to the selected frames.").arg(label));
		connect(item, &QAction::triggered, this, [this, id]() { applyEdit(id); });
		m_edits << item;
	};
	edit(tr("Trim"), QStringLiteral("trim"));
	edit(tr("Delete"), QStringLiteral("delete"));
	edit(tr("Fade In"), QStringLiteral("fade-in"));
	edit(tr("Fade Out"), QStringLiteral("fade-out"));
	auto* effects = new QToolButton;
	effects->setText(tr("Effects"));
	effects->setAccessibleName(tr("Audio effects"));
	effects->setPopupMode(QToolButton::InstantPopup);
	auto* menu = new QMenu(effects);
	const auto effect = [this, menu](const QString& label, const QString& id, bool parameter = false) {
		auto* item = menu->addAction(label);
		item->setObjectName(QStringLiteral("audioEdit-") + id);
		m_edits << item;
		connect(item, &QAction::triggered, this, [this, id, label, parameter]() {
			double value = 0;
			if (parameter) {
				bool accepted = false;
				const bool normalize = id == QStringLiteral("normalize");
				value = QInputDialog::getDouble(
				    this, label, normalize ? tr("Target peak (dBFS)") : tr("Gain (dB)"),
				    normalize ? -1.0 : 0.0, -96.0, normalize ? 0.0 : 24.0, 2, &accepted);
				if (!accepted) {
					return;
				}
			}
			applyEdit(id, value);
		});
	};
	effect(tr("Gain…"), QStringLiteral("gain"), true);
	effect(tr("Normalize…"), QStringLiteral("normalize"), true);
	effect(tr("Silence"), QStringLiteral("silence"));
	effect(tr("Reverse"), QStringLiteral("reverse"));
	effect(tr("Convert to Mono"), QStringLiteral("mono"));
	effect(tr("Convert Mono to Stereo"), QStringLiteral("stereo"));
	effect(tr("Invert Polarity"), QStringLiteral("invert"));
	effect(tr("Remove DC Offset"), QStringLiteral("remove-dc"));
	m_resample = menu->addAction(tr("Resample…"));
	m_resample->setObjectName(QStringLiteral("audioEditorResample"));
	m_resample->setToolTip(tr("Change the whole sound's sample rate while "
	                          "preserving its pitch and duration."));
	connect(m_resample, &QAction::triggered, this, &AudioEditorDialog::chooseSampleRate);
	menu->addSeparator();
	m_mix = menu->addAction(tr("Mix Clipboard at Selection Start"));
	m_mix->setObjectName(QStringLiteral("audioEditorMix"));
	m_mix->setToolTip(tr("Add every clipboard frame at the selection start; extend the sound "
	                     "when needed. Float headroom is retained."));
	connect(m_mix, &QAction::triggered, this, [this]() { pasteSelection(true); });
	m_insertSilence = menu->addAction(tr("Insert Silence…"));
	m_insertSilence->setObjectName(QStringLiteral("audioEditorInsertSilence"));
	connect(m_insertSilence, &QAction::triggered, this, [this]() {
		bool accepted = false;
		const int maximum = static_cast<int>(AudioSampleLimit / std::max(1, m_state.clip.channels) -
		                                     m_state.clip.frameCount());
		const int frames =
		    QInputDialog::getInt(this, tr("Insert Silence"), tr("Frames to insert at selection start"),
		                         std::min(m_state.clip.sampleRate, maximum), 1, maximum, 1, &accepted);
		if (accepted) {
			insertSilence(frames);
		}
	});
	effects->setMenu(menu);
	edits->addWidget(effects);
	edit(tr("Select All"), QStringLiteral("select-all"));
	m_analyze = action(edits, tr("Analyze…"), QStringLiteral("audioEditorAnalyze"),
	                   QStringLiteral("waveform"), {}, [this]() { reviewAnalysisChannels(); });
	m_analyze->setToolTip(tr("Measure sample and true peak, integrated loudness, RMS, DC offset, and full-scale "
	                         "samples for the selection, or the whole "
	                         "sound when there is no selection."));
	m_markers = action(edits, tr("Markers…"), QStringLiteral("audioEditorMarkers"), QStringLiteral("edit"),
	                   {}, [this]() { chooseMarkers(); });
	m_markers->setToolTip(tr("Edit named cue positions and one forward loop. "
	                         "Changes support undo and recovery."));
	m_selectLoop =
	    action(edits, tr("Select Loop"), QStringLiteral("audioEditorSelectLoop"), QStringLiteral("repeat"),
	           {}, [this]() {
		           if (m_state.clip.markers.loop) {
			           setSelection(m_state.clip.markers.loop->first, m_state.clip.markers.loop->end);
			           m_waveform->zoomToSelection();
		           }
	           });
	m_selectLoop->setToolTip(tr("Select and show the authored loop range. Play "
	                            "and Loop audition this range."));

	m_summary = new QLabel;
	m_summary->setObjectName(QStringLiteral("audioEditorSummary"));
	m_summary->setWordWrap(true);
	m_summary->setAccessibleName(tr("Edited sound format and peak level"));
	layout->addWidget(m_summary);
	m_waveform = new AudioWaveformView;
	m_waveform->setObjectName(QStringLiteral("audioEditorWaveform"));
	m_viewControls = new QToolBar(tr("Waveform view commands"));
	m_viewControls->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	const auto viewAction = [&](const QString& text, const QString& name, const QString& icon,
	                            const QKeySequence& shortcut, auto callback) {
		auto* item = action(m_viewControls, text, name, icon, shortcut, callback);
		removeAction(item);
		m_waveform->addAction(item);
		item->setShortcutContext(Qt::WidgetShortcut);
		return item;
	};
	viewAction(tr("Zoom In"), QStringLiteral("audioZoomIn"), QStringLiteral("zoom-in"), QKeySequence::ZoomIn,
	           [this]() { m_waveform->zoomIn(); });
	viewAction(tr("Zoom Out"), QStringLiteral("audioZoomOut"), QStringLiteral("zoom-out"),
	           QKeySequence::ZoomOut, [this]() { m_waveform->zoomOut(); });
	viewAction(tr("Fit Sound"), QStringLiteral("audioZoomFit"), QStringLiteral("fit"), {},
	           [this]() { m_waveform->zoomToFit(); });
	m_zoomSelection =
	    viewAction(tr("Fit Selection"), QStringLiteral("audioZoomSelection"), QStringLiteral("selection"), {},
	               [this]() { m_waveform->zoomToSelection(); });
	layout->addWidget(m_viewControls);
	layout->addWidget(m_waveform, 1);
	m_pan = new QScrollBar(Qt::Horizontal);
	m_pan->setObjectName(QStringLiteral("audioWaveformPan"));
	m_pan->setAccessibleName(tr("Waveform first visible frame"));
	m_pan->setLayoutDirection(Qt::LeftToRight);
	layout->addWidget(m_pan);
	m_viewRange = new QLabel;
	m_viewRange->setWordWrap(true);
	m_viewRange->setAccessibleName(tr("Visible audio frame range"));
	layout->addWidget(m_viewRange);
	connect(m_pan, &QScrollBar::valueChanged, this,
	        [this](int first) { m_waveform->panFrames(first - m_waveform->visibleStart()); });
	connect(m_waveform, &AudioWaveformView::viewChanged, this, [this]() { refreshView(); });
	// Editing shortcuts belong to the waveform, so Ctrl+C/X/V in path and frame
	// fields retain the field's own text-editing behavior.
	const QList<QPair<QAction*, QKeySequence>> clipboardShortcuts = {
	    {m_cut, QKeySequence::Cut}, {m_copy, QKeySequence::Copy}, {m_paste, QKeySequence::Paste}};
	for (const auto& shortcut : clipboardShortcuts) {
		removeAction(shortcut.first);
		m_waveform->addAction(shortcut.first);
		shortcut.first->setShortcut(shortcut.second);
		shortcut.first->setShortcutContext(Qt::WidgetShortcut);
	}
	connect(m_waveform, &AudioWaveformView::selectionChanged, this, &AudioEditorDialog::setSelection);
	connect(m_waveform, &AudioWaveformView::seekRequested, this, [this](qint64 frame) {
		m_waveform->setPlayheadFrame(frame);
		refreshPlayback();
	});

	auto* range = new QFormLayout;
	range->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_first = new QSpinBox;
	m_end = new QSpinBox;
	m_first->setObjectName(QStringLiteral("audioSelectionStart"));
	m_end->setObjectName(QStringLiteral("audioSelectionEnd"));
	m_first->setAccessibleName(tr("Selection start frame"));
	m_end->setAccessibleName(tr("Selection end frame, exclusive"));
	m_first->setKeyboardTracking(false);
	m_end->setKeyboardTracking(false);
	m_first->setGroupSeparatorShown(true);
	m_end->setGroupSeparatorShown(true);
	range->addRow(tr("&Start frame"), m_first);
	range->addRow(tr("&End frame (exclusive)"), m_end);
	layout->addLayout(range);
	connect(m_first, &QSpinBox::valueChanged, this,
	        [this](int value) { setSelection(value, std::max<qint64>(value, m_state.end)); });
	connect(m_end, &QSpinBox::valueChanged, this,
	        [this](int value) { setSelection(std::min<qint64>(value, m_state.first), value); });

	auto* transport = new QToolBar(tr("Edited audio playback"));
	transport->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_play = action(transport, tr("Play Selection"), QStringLiteral("audioEditorPlay"),
	                QStringLiteral("play"), QKeySequence(Qt::Key_Space), [this]() { play(); });
	// Space belongs to text fields and focused buttons elsewhere in the dialog.
	removeAction(m_play);
	m_waveform->addAction(m_play);
	m_play->setShortcutContext(Qt::WidgetShortcut);
	m_stop = action(transport, tr("Stop"), QStringLiteral("audioEditorStop"), QStringLiteral("stop"), {},
	                [this]() { stopPlayback(); });
	m_loop = new QCheckBox(tr("Loop"));
	m_loop->setObjectName(QStringLiteral("audioPlaybackLoop"));
	m_loop->setAccessibleName(tr("Loop edited playback"));
	transport->addWidget(m_loop);
	m_volume = new QSlider(Qt::Horizontal);
	m_volume->setObjectName(QStringLiteral("audioPlaybackVolume"));
	m_volume->setRange(0, 100);
	m_volume->setValue(70);
	m_volume->setMaximumWidth(130);
	m_volume->setAccessibleName(tr("Edited audio playback volume"));
	m_volume->setToolTip(tr("Playback volume in percent; does not change exported samples."));
	transport->addWidget(m_volume);
	m_clock = new QLabel;
	m_clock->setObjectName(QStringLiteral("audioPlaybackPosition"));
	m_clock->setWordWrap(true);
	m_clock->setAccessibleName(tr("Edited audio playback position"));
	layout->addWidget(transport);
	layout->addWidget(m_clock);
	auto* seekRow = new QHBoxLayout;
	m_seek = new QSlider(Qt::Horizontal);
	m_seek->setObjectName(QStringLiteral("audioPlaybackSeek"));
	m_seek->setAccessibleName(tr("Seek within the current audio audition"));
	m_seek->setLayoutDirection(Qt::LeftToRight);
	m_seek->setTracking(false);
	m_seek->setToolTip(tr("Seek during playback or pause without changing the selection. "
	                      "The audio backend seeks in milliseconds; selection boundaries remain exact frames."));
	m_seekFrame = new QSpinBox;
	m_seekFrame->setObjectName(QStringLiteral("audioPlaybackFrame"));
	m_seekFrame->setAccessibleName(tr("Audio playback frame"));
	m_seekFrame->setKeyboardTracking(false);
	m_seekFrame->setGroupSeparatorShown(true);
	m_seekFrame->setToolTip(m_seek->toolTip());
	seekRow->addWidget(m_seek, 1);
	seekRow->addWidget(m_seekFrame);
	auto* seekForm = new QFormLayout;
	seekForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto* seekLabel = new QLabel(tr("Playback &frame"));
	seekLabel->setBuddy(m_seekFrame);
	seekForm->addRow(seekLabel, seekRow);
	layout->addLayout(seekForm);
	m_playback = new AudioPlayback(this, std::move(playbackBackend));
	connect(m_playback, &AudioPlayback::changed, this, &AudioEditorDialog::refreshPlayback);
	connect(m_playback, &AudioPlayback::positionChanged, this, [this](qint64 frame) {
		m_waveform->setPlayheadFrame(frame);
		refreshPlayback();
	});
	connect(m_playback, &AudioPlayback::failed, this,
	        [this](const QString& message) { showStatus(tr("Playback failed: %1").arg(message)); });
	connect(m_volume, &QSlider::valueChanged, this,
	        [this](int value) { m_playback->setVolume(value / 100.0f); });
	connect(m_loop, &QCheckBox::toggled, m_playback, &AudioPlayback::setLoop);
	const auto seekFrame = [this](int frame) {
		m_playback->seekToFrame(frame);
		const QSignalBlocker block(m_seekFrame);
		m_seekFrame->setValue(static_cast<int>(m_playback->positionFrame()));
	};
	connect(m_seek, &QSlider::valueChanged, this, seekFrame);
	connect(m_seekFrame, &QSpinBox::valueChanged, this, seekFrame);
#if !VIBESTUDIO_HAVE_AUDIO_PLAYBACK
	m_volume->setEnabled(false);
	m_loop->setEnabled(false);
	m_play->setToolTip(tr("Playback requires a build with Qt Multimedia. Editing "
	                      "and export remain available."));
#endif

	auto* package = new QGroupBox(tr("Package Handoff"));
	auto* packageLayout = new QFormLayout(package);
	packageLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_contextLabel = new QLabel;
	m_contextLabel->setWordWrap(true);
	m_contextLabel->setTextFormat(Qt::PlainText);
	m_contextLabel->setAccessibleName(tr("Target package"));
	packageLayout->addRow(m_contextLabel);
	m_stagePreset = new QComboBox;
	m_stagePreset->setObjectName(QStringLiteral("audioStagePreset"));
	m_stagePreset->setAccessibleName(tr("Package sound delivery preset"));
	addDeliveryPresets(m_stagePreset);
	packageLayout->addRow(tr("Delivery &preset"), m_stagePreset);
	m_stageDither = new QCheckBox(tr("Triangular dither"));
	m_stageDither->setAccessibleName(tr("Dither package sound delivery"));
	m_stageDither->setToolTip(tr("Apply low-level triangular dither once, at "
	                             "final integer quantization."));
	packageLayout->addRow(m_stageDither);
	m_stageFormat = new QLabel;
	m_stageFormat->setWordWrap(true);
	m_stageFormat->setAccessibleName(tr("Package sound output format"));
	packageLayout->addRow(m_stageFormat);
	m_packagePath = new QLineEdit(QStringLiteral("sound/edited.wav"));
	m_packagePath->setObjectName(QStringLiteral("audioEditorPackagePath"));
	m_packagePath->setAccessibleName(tr("Audio package path"));
	packageLayout->addRow(tr("&Asset path"), m_packagePath);
	m_replace = new QCheckBox(tr("Replace existing entry"));
	m_replace->setAccessibleName(tr("Allow replacing an existing staged sound"));
	packageLayout->addRow(m_replace);
	auto* stageButton = new QPushButton(studioIcon(QStringLiteral("package")), tr("Stage Sound"));
	stageButton->setObjectName(QStringLiteral("audioEditorStageButton"));
	stageButton->setAutoDefault(false);
	stageButton->setAccessibleName(tr("Stage edited sound in the current package"));
	m_stage = new QAction(tr("Stage Sound"), this);
	m_stage->setObjectName(QStringLiteral("audioEditorStage"));
	connect(m_stage, &QAction::changed, stageButton,
	        [this, stageButton]() { stageButton->setEnabled(m_stage->isEnabled()); });
	connect(m_stage, &QAction::triggered, this, [this]() { stage(); });
	connect(stageButton, &QPushButton::clicked, m_stage, &QAction::trigger);
	connect(m_stagePreset, &QComboBox::currentIndexChanged, this, [this]() { refreshContext(); });
	packageLayout->addRow(stageButton);
	auto* placeButton = new QPushButton(tr("Stage && Place in Level…"));
	placeButton->setObjectName(QStringLiteral("audioEditorPlaceButton"));
	placeButton->setAutoDefault(false);
	placeButton->setAccessibleName(tr("Stage edited sound and place it in the current level"));
	placeButton->setToolTip(tr("Review a Quake II or Quake III speaker and its coordinates. Delivery uses the matching game preset."));
	m_stageAndPlace = new QAction(tr("Stage and Place in Level"), this);
	m_stageAndPlace->setObjectName(QStringLiteral("audioEditorPlace"));
	connect(m_stageAndPlace, &QAction::changed, placeButton,
	        [this, placeButton]() { placeButton->setEnabled(m_stageAndPlace->isEnabled()); });
	connect(m_stageAndPlace, &QAction::triggered, this, [this]() { stage(true); });
	connect(placeButton, &QPushButton::clicked, m_stageAndPlace, &QAction::trigger);
	packageLayout->addRow(placeButton);
	layout->addWidget(package);

	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("audioEditorStatus"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setAccessibleName(tr("Audio operation status"));
	outer->addWidget(m_status);
	m_recoveryEnabled = new QCheckBox(tr("Keep local recovery copies"));
	m_recoveryEnabled->setObjectName(QStringLiteral("audioRecoveryEnabled"));
	m_recoveryEnabled->setAccessibleName(m_recoveryEnabled->text());
	m_recoveryEnabled->setToolTip(tr("Checkpoint unsaved audio on this device. "
	                                 "Turning this off keeps existing copies."));
	m_recoveryEnabled->setChecked(StudioSettings().audioRecoveryEnabled());
	layout->addWidget(m_recoveryEnabled);
	m_recoveryStatus = new QLabel;
	m_recoveryStatus->setObjectName(QStringLiteral("audioRecoveryStatus"));
	m_recoveryStatus->setTextFormat(Qt::PlainText);
	m_recoveryStatus->setWordWrap(true);
	m_recoveryStatus->setAccessibleName(tr("Local audio recovery status"));
	layout->addWidget(m_recoveryStatus);
	m_recoveryDirectory = audioRecoveryDirectory();
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_recovery = new AudioRecoveryWriter(m_recoveryDirectory, this);
	m_recovery->finished = [this](const QString& path, const QString& error) {
		m_recoveryStatus->setText(error.isEmpty() ? tr("Recovery copy updated.")
		                                          : tr("Recovery failed: %1").arg(error));
		m_recoveryStatus->setAccessibleDescription(m_recoveryStatus->text());
		m_recoveryStatus->setToolTip(path);
	};
	connect(m_recoveryEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
		StudioSettings settings;
		settings.setAudioRecoveryEnabled(enabled);
		settings.sync();
		emit recoveryPreferenceChanged(enabled);
		if (enabled) {
			checkpointRecovery();
		} else {
			m_recoveryStatus->setText(tr("Automatic recovery is off. Existing copies are retained."));
			m_recoveryStatus->setAccessibleDescription(m_recoveryStatus->text());
		}
	});
	auto* progressRow = new QHBoxLayout;
	m_progress = new QProgressBar;
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Audio processing progress"));
	m_cancel = new QPushButton(tr("Cancel Operation"));
	m_cancel->setAutoDefault(false);
	m_cancel->setAccessibleName(tr("Cancel pending audio operation"));
	connect(m_cancel, &QPushButton::clicked, this, [this]() { cancelWork(); });
	progressRow->addWidget(m_progress, 1);
	progressRow->addWidget(m_cancel);
	outer->addLayout(progressRow);
	auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(close, &QDialogButtonBox::rejected, this, &QDialog::close);
	outer->addWidget(close);
	// Toolbars otherwise create mouse-only buttons on some platforms.
	for (auto* button : findChildren<QToolButton*>()) {
		button->setFocusPolicy(Qt::StrongFocus);
		button->setAccessibleName(button->text());
		if (auto* command = button->defaultAction()) {
			connect(command, &QAction::changed, button,
			        [button, command]() { button->setAccessibleName(command->text()); });
		}
	}
	refresh();
}

AudioEditorDialog::~AudioEditorDialog()
{
	if (m_work) {
		m_work->cancel = true;
	}
	stop();
	// Flush the latest queued checkpoint before the UI's labels are destroyed.
	delete m_recovery;
	m_recovery = nullptr;
}

void AudioEditorDialog::showStatus(const QString& text)
{
	m_status->setText(text);
	m_status->setAccessibleDescription(text);
}

void AudioEditorDialog::setHighContrast(bool enabled) { m_waveform->setHighContrast(enabled); }

void AudioEditorDialog::reviewAnalysisChannels()
{
	if (isBusy() || m_state.clip.frameCount() == 0) { return; }
	if (m_state.clip.channels <= 2) { analyze(); return; }
	if (auto* existing = findChild<AudioChannelMapDialog*>()) { existing->raise(); return; }
	auto* dialog = new AudioChannelMapDialog(m_state.clip.channels, this);
	const auto revision = m_state.revision;
	connect(dialog, &QDialog::accepted, this, [this, dialog, revision]() {
		if (m_state.revision != revision || isBusy()) { showStatus(tr("The sound changed. Review its speaker roles again.")); return; }
		analyze(dialog->options());
	});
	dialog->show();
}

void AudioEditorDialog::analyze(const AudioAnalysisOptions& options)
{
	if (isBusy() || m_state.clip.frameCount() == 0) {
		return;
	}
	const AudioClip clip = m_state.clip;
	const qint64 first = m_state.end > m_state.first ? m_state.first : 0;
	const qint64 end = m_state.end > m_state.first ? m_state.end : clip.frameCount();
	const QString source = m_projectIdentity.path.isEmpty() ? m_sourceName : m_projectIdentity.path;
	runWork(
	    tr("Analyzing audio samples…"),
	    [clip, first, end, options](AudioEditorWork& work) {
		    work.analysis = analyzeAudioClip(clip, first, end, work.control(), options);
		    work.error = work.analysis.error;
		    work.result.cancelled = work.analysis.cancelled;
	    },
	    [this, source](const AudioEditorWork& work) {
		    auto* dialog = new AudioAnalysisDialog(work.analysis.analysis, source, this);
		    dialog->show();
		    showStatus(tr("Analysis complete. The sound is unchanged."));
	    });
}

void AudioEditorDialog::runWork(const QString& title, std::function<void(AudioEditorWork&)> work,
                                std::function<void(const AudioEditorWork&)> complete, const AudioClip& comparison)
{
	if (isBusy()) {
		return;
	}
	stop();
	auto state = std::make_shared<AudioEditorWork>();
	m_work = state;
	showStatus(title);
	refresh();
	auto* worker = QThread::create([state, work, comparison]() {
		try {
			work(*state);
			if (!state->cancel && state->result.succeeded() && comparison.channels > 0) {
				state->contentChanged = !sameAudioContent(comparison, state->result.clip, state->control());
			}
			if (state->contentChanged && state->peaks.valid && !state->cancel && state->error.isEmpty() &&
			    state->result.error.isEmpty()) {
				state->waveform = AudioWaveformData::build(
				    state->result.succeeded() ? state->result.clip : state->project.clip, state->control());
			}
		} catch (const std::bad_alloc&) {
			state->discardPrepared();
			state->error = tr("Not enough memory to complete the audio operation. Existing edits remain available.");
		} catch (const std::exception& exception) {
			state->discardPrepared();
			state->error = tr("The audio worker failed: %1").arg(QString::fromUtf8(exception.what()).left(512));
		} catch (...) {
			state->discardPrepared();
			state->error = tr("The audio worker failed unexpectedly. Existing edits remain available.");
		}
	});
	connect(worker, &QThread::finished, this, [this, state, complete]() {
		if (m_work != state) {
			return;
		}
		m_work.reset();
		if (state->cancel || state->result.cancelled) {
			m_closeAfterSave = false;
			m_afterSave = {};
			showStatus(tr("Cancelled. The sound is unchanged."));
		} else if (!state->error.isEmpty() || !state->result.error.isEmpty()) {
			m_closeAfterSave = false;
			m_afterSave = {};
			showStatus(tr("Audio operation failed: %1")
			               .arg(state->error.isEmpty() ? state->result.error : state->error));
		} else {
			complete(*state);
		}
		refresh();
	});
	connect(worker, &QThread::finished, worker, &QObject::deleteLater);
	connect(qApp, &QCoreApplication::aboutToQuit, worker, [worker, state]() {
		state->cancel = true;
		worker->wait();
	});
	worker->start();
}

bool AudioEditorDialog::loadSource(const QString& name, const QString& protectedPath, Reader reader)
{
	if (isBusy() ||
	    !confirmDiscard([this, name, protectedPath, reader]() { loadSource(name, protectedPath, reader); })) {
		return false;
	}
	runWork(
	    tr("Loading and decoding %1…").arg(name),
	    [reader, name](AudioEditorWork& work) {
		    const QByteArray bytes = reader(&work.error);
		    if (!work.error.isEmpty() || work.cancel) {
			    return;
		    }
		    work.result = decodeAudioClip(name, bytes, work.control());
		    if (work.result.succeeded()) {
			    work.peaks = audioClipPeaks(work.result.clip);
		    }
	    },
	    [this, name, protectedPath](const AudioEditorWork& work) {
		    retireRecovery();
		    m_projectIdentity = {};
		    m_recoveryInputPath.clear();
		    m_projectMetadata = {};
		    if (!work.result.warnings.isEmpty()) {
			    m_projectMetadata.insert(QStringLiteral("importWarnings"),
			                             QJsonArray::fromStringList(work.result.warnings));
		    }
		    m_sourceName = name;
		    m_protectedPath = protectedPath;
		    m_undo.clear();
		    m_redo.clear();
		    m_state = {work.result.clip, work.peaks, 0, work.result.clip.frameCount(), ++m_nextRevision, {},
		               work.waveform};
		    m_waveformRevision = 0;
		    m_savedRevision = m_state.revision;
		    const QString suggested = name.contains(QLatin1Char('/')) && !QFileInfo(name).isAbsolute()
		                                  ? name
		                                  : QStringLiteral("sound/") + QFileInfo(name).fileName();
		    m_packagePath->setText(
		        suggested.left(suggested.size() - QFileInfo(suggested).suffix().size()) +
		        (QFileInfo(suggested).suffix().isEmpty() ? QStringLiteral(".wav") : QStringLiteral("wav")));
		    m_contextKey = context ? context().packagePath : QString();
		    if (context && context().doomWad) {
			    m_packagePath->setText(suggestedSoundPath(name, true));
		    }
		    m_replace->setChecked(false);
		    showStatus(work.result.warnings.isEmpty() ? tr("Ready. Save a project to preserve edited samples "
		                                                   "without quantization.")
		                                              : work.result.warnings.join(QLatin1Char(' ')));
	    });
	return true;
}

bool AudioEditorDialog::openFile(const QString& path)
{
	if (QFileInfo(path).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0) {
		return openProject(path);
	}
	return loadSource(QFileInfo(path).fileName(), QFileInfo(path).absoluteFilePath(), [path](QString* error) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			*error = file.errorString();
			return QByteArray();
		}
		if (file.size() > AudioInputByteLimit) {
			*error = tr("The audio input exceeds the 128 MiB editing limit.");
			return QByteArray();
		}
		QByteArray bytes = file.read(AudioInputByteLimit + 1);
		if (file.error() != QFileDevice::NoError) {
			*error = file.errorString();
		}
		return bytes;
	});
}

void AudioEditorDialog::chooseFile()
{
	const QString file =
	    QFileDialog::getOpenFileName(this, tr("Open Audio"), m_protectedPath,
	                                 tr("Audio projects and sounds (*.vsaudio *.wav *.mp3 *.flac *.ogg *.dmx "
	                                    "*.lmp);;All files (*)"));
	if (!file.isEmpty()) {
		openFile(file);
	}
}

AudioProject AudioEditorDialog::projectSnapshot() const
{
	return {m_state.clip, m_state.first, m_state.end, m_sourceName, m_protectedPath, m_projectMetadata};
}

bool AudioEditorDialog::openProject(const QString& path, bool recoverAsDraft,
                                    const QByteArray& expectedSha256)
{
	if (isBusy()) { return false; }
	if (audioPathsReferToSameFile(path, recoveryPath())) {
		if (recoveryBusy() || !m_recovery->retain(m_recoveryId)) {
			showStatus(tr("Wait for this document's recovery update to finish, then refresh and restore the copy."));
			return false;
		}
		// A save from the unsaved-change guard must not retire the reviewed input.
		m_recoveryActive = false;
		m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		checkpointRecovery();
	}
	if (!confirmDiscard([this, path, recoverAsDraft, expectedSha256]() {
		    openProject(path, recoverAsDraft, expectedSha256);
	    })) {
		return false;
	}
	runWork(
	    tr("Reading audio project…"),
	    [path, expectedSha256](AudioEditorWork& work) {
		    if (readAudioProject(path, &work.project, &work.identity, &work.error, work.control())) {
			    if (!expectedSha256.isEmpty() && work.identity.sha256 != expectedSha256) {
				    work.error = tr("Recovery changed since review. Refresh Recoveries "
				                    "before restoring it.");
				    return;
			    }
			    work.peaks = audioClipPeaks(work.project.clip);
		    }
	    },
	    [this, recoverAsDraft](const AudioEditorWork& work) {
		    retireRecovery();
		    m_undo.clear();
		    m_redo.clear();
		    m_state = {work.project.clip,     work.peaks,       work.project.firstFrame,
		               work.project.endFrame, ++m_nextRevision, {},
		               work.waveform};
		    m_waveformRevision = 0;
		    m_sourceName = work.project.sourceName;
		    m_protectedPath = work.project.sourcePath;
		    const bool draft =
		        recoverAsDraft || work.project.metadata.contains(QStringLiteral("recoveryWrittenUtc"));
		    m_projectIdentity = draft ? AudioProjectIdentity{} : work.identity;
		    m_recoveryInputPath = draft ? work.identity.path : QString();
		    m_projectMetadata = work.project.metadata;
		    m_projectMetadata.remove(QStringLiteral("recoveryWrittenUtc"));
		    m_projectMetadata.remove(QStringLiteral("recoveryOwnerPid"));
		    m_savedRevision = draft ? 0 : m_state.revision;
		    m_replace->setChecked(false);
		    m_packagePath->setText(suggestedSoundPath(m_sourceName, context && context().doomWad));
		    if (draft) {
			    checkpointRecovery();
			    showStatus(tr("Recovered as an unsaved draft. Save a project to a "
			                  "reviewed destination; the source and "
			                  "recovery copy are unchanged."));
		    } else {
			    showStatus(tr("Audio project opened without sample quantization."));
		    }
	    });
	return true;
}

bool AudioEditorDialog::saveProjectTo(const QString& path, bool overwrite)
{
	if (isBusy() || m_state.clip.channels == 0) {
		return false;
	}
	AudioProjectSaveRequest request;
	request.path = path;
	request.overwrite = overwrite;
	request.protectedPath = m_recoveryInputPath.isEmpty() ? m_protectedPath : m_recoveryInputPath;
	// Even Save As to the current file must retain its external-change guard.
	if (!m_projectIdentity.path.isEmpty() && audioPathsReferToSameFile(path, m_projectIdentity.path)) {
		request.expected = m_projectIdentity;
	}
	const AudioProject snapshot = projectSnapshot();
	runWork(
	    tr("Saving audio project…"),
	    [snapshot, request](AudioEditorWork& work) {
		    work.saved = writeAudioProject(snapshot, request);
		    work.error = work.saved.error;
	    },
	    [this](const AudioEditorWork& work) {
		    m_projectIdentity = work.saved.identity;
		    m_savedRevision = m_state.revision;
		    retireRecovery();
		    showStatus(tr("Audio project saved."));
		    if (m_closeAfterSave) {
			    m_closeAfterSave = false;
			    QTimer::singleShot(0, this, &QDialog::close);
		    } else if (m_afterSave) {
			    auto next = std::move(m_afterSave);
			    m_afterSave = {};
			    QTimer::singleShot(0, this, std::move(next));
		    }
	    });
	m_work->cancellable = false;
	m_cancel->setEnabled(false);
	return true;
}

bool AudioEditorDialog::chooseProjectSave(bool saveAs)
{
	QString path = m_projectIdentity.path;
	if (saveAs || path.isEmpty()) {
		path = QFileDialog::getSaveFileName(
		    this, tr("Save Audio Project"),
		    path.isEmpty() ? QFileInfo(m_sourceName).completeBaseName() + QStringLiteral(".vsaudio") : path,
		    tr("VibeStudio audio project (*.vsaudio)"));
	}
	return !path.isEmpty() && saveProjectTo(path, true);
}

void AudioEditorDialog::chooseRecovery()
{
	auto* dialog = new AudioRecoveryDialog(m_recoveryDirectory, this);
	dialog->restore = [this](const QString& path, const QByteArray& digest, AudioRecoveryKind kind) {
		if (kind == AudioRecoveryKind::Session) { Q_EMIT sessionRecoveryRequested(path, digest); }
		else { openProject(path, true, digest); }
	};
	dialog->show();
}

QString AudioEditorDialog::recoveryPath() const
{
	return audioRecoveryPath(m_recoveryDirectory, m_recoveryId);
}

bool AudioEditorDialog::recoveryBusy() const { return m_recovery && m_recovery->busy(); }

void AudioEditorDialog::setRecoveryEnabled(bool enabled) { m_recoveryEnabled->setChecked(enabled); }

void AudioEditorDialog::retireRecovery()
{
	m_recoveryStatus->clear();
	m_recoveryStatus->setAccessibleDescription({});
	m_recoveryStatus->setToolTip({});
	if (!m_recoveryActive) {
		return;
	}
	if (m_recovery) {
		m_recovery->retire(m_recoveryId);
	}
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_recoveryActive = false;
}

void AudioEditorDialog::checkpointRecovery()
{
	if (!hasChanges()) {
		retireRecovery();
		return;
	}
	if (!m_recovery || !m_recoveryEnabled->isChecked()) {
		return;
	}
	m_recoveryStatus->setText(tr("Updating local recovery copy…"));
	m_recoveryStatus->setAccessibleDescription(m_recoveryStatus->text());
	m_recoveryActive = true;
	m_recovery->checkpoint(m_recoveryId, ++m_recoverySerial, projectSnapshot());
}

void AudioEditorDialog::chooseNew()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("New Sound"));
	auto* layout = new QVBoxLayout(&dialog);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto* rate = new QSpinBox;
	rate->setRange(1, 384000);
	rate->setValue(44100);
	rate->setAccessibleName(tr("New sound sample rate"));
	auto* channels = new QSpinBox;
	channels->setRange(1, 8);
	channels->setValue(1);
	channels->setAccessibleName(tr("New sound channel count"));
	auto* seconds = new QDoubleSpinBox;
	seconds->setRange(0, 3600);
	seconds->setDecimals(3);
	seconds->setAccessibleName(tr("Initial silence duration in seconds"));
	seconds->setToolTip(tr("Zero creates an empty document for pasted audio."));
	form->addRow(tr("Sample &rate (Hz)"), rate);
	form->addRow(tr("&Channels"), channels);
	form->addRow(tr("Initial &silence (seconds)"), seconds);
	layout->addLayout(form);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() == QDialog::Accepted) {
		createNew(rate->value(), channels->value(), qRound64(seconds->value() * rate->value()));
	}
}

bool AudioEditorDialog::createNew(int sampleRate, int channels, qint64 frames)
{
	if (isBusy() || !confirmDiscard([this, sampleRate, channels, frames]() {
		    createNew(sampleRate, channels, frames);
	    })) {
		return false;
	}
	runWork(
	    tr("Creating sound…"),
	    [sampleRate, channels, frames](AudioEditorWork& work) {
		    work.result = createAudioClip(sampleRate, channels, frames, work.control());
		    if (work.result.succeeded()) {
			    work.peaks = audioClipPeaks(work.result.clip);
		    }
	    },
	    [this](const AudioEditorWork& work) {
		    retireRecovery();
		    m_projectIdentity = {};
		    m_projectMetadata = {};
		    m_protectedPath.clear();
		    m_recoveryInputPath.clear();
		    m_sourceName = tr("Untitled sound");
		    m_undo.clear();
		    m_redo.clear();
		    m_state = {work.result.clip, work.peaks, 0, work.result.clip.frameCount(), ++m_nextRevision, {},
		               work.waveform};
		    m_waveformRevision = 0;
		    m_savedRevision = 0;
		    m_packagePath->setText(
		        suggestedSoundPath(QStringLiteral("untitled"), context && context().doomWad));
		    m_replace->setChecked(false);
		    checkpointRecovery();
		    showStatus(tr("New sound ready. Paste audio or insert silence to begin."));
	    });
	return true;
}

void AudioEditorDialog::commitEdit(const State& previous, const AudioEditorWork& work, const QString& label,
                                   qint64 first, qint64 end)
{
	if (!work.contentChanged) {
		showStatus(tr("The operation did not change the sound."));
		return;
	}
	m_undo << previous;
	m_redo.clear();
	m_state = {work.result.clip, work.peaks, first, end, ++m_nextRevision, label, work.waveform};
	trimHistory();
	checkpointRecovery();
	QString message = tr("%1 complete. Undo is available.").arg(label);
	if (previous.clip.markers.loop && !m_state.clip.markers.loop) {
		message += QLatin1Char(' ') + tr("The edit removed the loop range.");
	}
	if (previous.clip.markers.cues.size() > m_state.clip.markers.cues.size()) {
		message +=
		    QLatin1Char(' ') + tr("Removed cue markers: %1.")
		                           .arg(previous.clip.markers.cues.size() - m_state.clip.markers.cues.size());
	}
	showStatus(message);
}

bool AudioEditorDialog::setMarkers(const AudioMarkers& markers)
{
	if (isBusy() || m_state.clip.channels == 0) {
		return false;
	}
	const QString error = validateAudioMarkers(markers, m_state.clip.frameCount());
	if (!error.isEmpty()) {
		showStatus(error);
		return false;
	}
	if (markers == m_state.clip.markers) {
		return true;
	}
	stop();
	const State previous = m_state;
	// Marker-only revisions share the sample buffer and extrema cache.
	AudioEditorWork work;
	work.result.clip = m_state.clip;
	work.result.clip.markers = markers;
	work.peaks = m_state.peaks;
	work.waveform = m_state.waveform;
	commitEdit(previous, work, tr("Markers"), previous.first, previous.end);
	refresh();
	return true;
}

void AudioEditorDialog::chooseMarkers()
{
	if (isBusy() || m_state.clip.frameCount() == 0) {
		return;
	}
	auto* dialog = new AudioMarkersDialog(m_state.clip.markers, m_state.clip.frameCount(), m_state.first,
	                                      m_state.end, this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	const quint64 revision = m_state.revision;
	connect(dialog, &QDialog::accepted, this, [this, dialog, revision]() {
		if (revision != m_state.revision || isBusy()) {
			showStatus(tr("The sound changed while markers were open. Reopen Markers "
			              "to review the current sound."));
			return;
		}
		setMarkers(dialog->markers());
	});
	dialog->open();
}

void AudioEditorDialog::copySelection(bool cut)
{
	if (isBusy() || m_state.end <= m_state.first) {
		return;
	}
	const State previous = m_state;
	runWork(
	    cut ? tr("Cutting audio…") : tr("Copying audio…"),
	    [previous, cut](AudioEditorWork& work) {
		    work.result = applyAudioEdit(
		        previous.clip, {QStringLiteral("trim"), previous.first, previous.end}, work.control());
		    if (!work.result.succeeded()) {
			    return;
		    }
		    work.fragment = work.result.clip;
		    if (cut) {
			    work.result = applyAudioEdit(
			        previous.clip, {QStringLiteral("delete"), previous.first, previous.end}, work.control());
			    if (work.result.succeeded()) {
				    work.peaks = audioClipPeaks(work.result.clip);
			    }
		    }
	    },
	    [this, previous, cut](const AudioEditorWork& work) {
		    copiedAudio = work.fragment;
		    if (cut) {
			    commitEdit(previous, work, tr("Cut"), previous.first, previous.first);
		    } else {
			    showStatus(
			        tr("Copied %1 frames to the VibeStudio audio clipboard.").arg(copiedAudio.frameCount()));
		    }
		    for (QWidget* widget : QApplication::allWidgets()) {
			    if (auto* editor = qobject_cast<AudioEditorDialog*>(widget)) {
				    editor->refreshClipboardActions();
			    }
		    }
	    }, cut ? previous.clip : AudioClip{});
}

void AudioEditorDialog::pasteSelection(bool mix)
{
	if (isBusy() || m_state.clip.channels == 0 || copiedAudio.frameCount() == 0) {
		return;
	}
	const State previous = m_state;
	const AudioClip fragment = copiedAudio;
	runWork(
	    mix ? tr("Mixing audio…") : tr("Pasting audio…"),
	    [previous, fragment, mix](AudioEditorWork& work) {
		    work.result = mix ? mixAudioClip(previous.clip, previous.first, fragment, work.control())
		                      : replaceAudioRange(previous.clip, previous.first, previous.end, fragment,
		                                          work.control());
		    if (work.result.succeeded()) {
			    work.peaks = audioClipPeaks(work.result.clip);
		    }
	    },
	    [this, previous, fragment, mix](const AudioEditorWork& work) {
		    commitEdit(previous, work, mix ? tr("Mix") : tr("Paste"), previous.first,
		               previous.first + fragment.frameCount());
	    }, previous.clip);
}

void AudioEditorDialog::insertSilence(qint64 frames)
{
	if (isBusy() || m_state.clip.channels == 0) {
		return;
	}
	const State previous = m_state;
	runWork(
	    tr("Inserting silence…"),
	    [previous, frames](AudioEditorWork& work) {
		    work.result = insertAudioSilence(previous.clip, previous.first, frames, work.control());
		    if (work.result.succeeded()) {
			    work.peaks = audioClipPeaks(work.result.clip);
		    }
	    },
	    [this, previous, frames](const AudioEditorWork& work) {
		    commitEdit(previous, work, tr("Insert Silence"), previous.first, previous.first + frames);
	    }, previous.clip);
}

void AudioEditorDialog::chooseSampleRate()
{
	QDialog dialog(this);
	dialog.setLayoutDirection(layoutDirection());
	dialog.setWindowTitle(tr("Resample Audio"));
	dialog.setObjectName(QStringLiteral("audioResampleDialog"));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = audioOptionsBody(dialog, buttons);
	auto* presets = new QComboBox;
	presets->setAccessibleName(tr("Sample rate preset"));
	presets->addItem(tr("Custom"), 0);
	for (int rate : {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000}) {
		presets->addItem(tr("%1 Hz").arg(rate), rate);
	}
	auto* target = new QSpinBox;
	target->setObjectName(QStringLiteral("audioResampleRate"));
	target->setAccessibleName(tr("Target sample rate in Hz"));
	target->setRange(1, 384000);
	target->setValue(m_state.clip.sampleRate);
	target->setGroupSeparatorShown(true);
	auto* preview = new AudioOptionsSummary;
	preview->setAccessibleName(tr("Resampling output summary"));
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Resample"));
	const auto update = [&]() {
		const qint64 frames =
		    m_state.clip.frameCount() == 0
		        ? 0
		        : std::max<qint64>(1, audioFrameAtSampleRate(m_state.clip.frameCount(),
		                                                     m_state.clip.sampleRate, target->value()));
		const bool fits = frames <= AudioSampleLimit / m_state.clip.channels;
		preview->setSummary(fits ? tr("%1 → %2 Hz · %3 output frames · %4 s\nApplies to the whole "
		                              "sound; selection "
		                              "follows its time position.")
		                               .arg(m_state.clip.sampleRate)
		                               .arg(target->value())
		                               .arg(frames)
		                               .arg(frames / double(target->value()), 0, 'f', 3)
		                         : tr("This conversion would exceed the sample limit. Choose a "
		                              "lower rate."));
		buttons->button(QDialogButtonBox::Ok)->setEnabled(fits && target->value() != m_state.clip.sampleRate);
		const QSignalBlocker blocker(presets);
		presets->setCurrentIndex(std::max(0, presets->findData(target->value())));
	};
	connect(presets, &QComboBox::currentIndexChanged, &dialog, [&](int) {
		if (const int rate = presets->currentData().toInt(); rate > 0) {
			target->setValue(rate);
		}
	});
	connect(target, &QSpinBox::valueChanged, &dialog, [&](int) { update(); });
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addRow(tr("&Preset"), presets);
	layout->addRow(tr("Target &rate (Hz)"), target);
	layout->addRow(preview);
	update();
	if (dialog.exec() == QDialog::Accepted) {
		resample(target->value());
	}
}

void AudioEditorDialog::resample(int targetRate)
{
	if (isBusy() || m_state.clip.channels == 0 || targetRate == m_state.clip.sampleRate) {
		return;
	}
	const State previous = m_state;
	runWork(
	    tr("Resampling audio…"),
	    [previous, targetRate](AudioEditorWork& work) {
		    work.result = resampleAudioClip(previous.clip, targetRate, work.control());
		    if (work.result.succeeded()) {
			    work.peaks = audioClipPeaks(work.result.clip);
		    }
	    },
	    [this, previous, targetRate](const AudioEditorWork& work) {
		    const auto mapped = [&](qint64 frame) {
			    return frame == previous.clip.frameCount()
			               ? work.result.clip.frameCount()
			               : std::min(work.result.clip.frameCount(),
			                          audioFrameAtSampleRate(frame, previous.clip.sampleRate, targetRate));
		    };
		    commitEdit(previous, work, tr("Resample to %1 Hz").arg(targetRate), mapped(previous.first),
		               mapped(previous.end));
	    }, previous.clip);
}

void AudioEditorDialog::refreshClipboardActions()
{
	const bool range = m_state.end > m_state.first;
	m_copy->setEnabled(!isBusy() && range);
	m_cut->setEnabled(!isBusy() && range);
	const bool paste = !isBusy() && m_state.clip.channels > 0 && copiedAudio.frameCount() > 0;
	m_paste->setEnabled(paste);
	m_mix->setEnabled(paste);
	m_insertSilence->setEnabled(!isBusy() && m_state.clip.channels > 0 &&
	                            m_state.clip.frameCount() < AudioSampleLimit / m_state.clip.channels);
	m_resample->setEnabled(!isBusy() && m_state.clip.channels > 0);
}

void AudioEditorDialog::applyEdit(const QString& operation, double decibels)
{
	if (isBusy() || m_state.clip.frameCount() == 0) {
		return;
	}
	if (operation == QStringLiteral("select-all")) {
		setSelection(0, m_state.clip.frameCount());
		return;
	}
	const State previous = m_state;
	const AudioEdit edit{operation, previous.first, previous.end, decibels};
	QString label = operation;
	for (auto* action : m_edits) {
		if (action->objectName() == QStringLiteral("audioEdit-") + operation) {
			label = action->text();
			break;
		}
	}
	runWork(
	    tr("Processing audio…"),
	    [previous, edit](AudioEditorWork& work) {
		    work.result = applyAudioEdit(previous.clip, edit, work.control());
		    if (work.result.succeeded()) {
			    work.peaks = audioClipPeaks(work.result.clip);
		    }
	    },
	    [this, previous, operation, label](const AudioEditorWork& work) {
		    const qint64 first = operation == QStringLiteral("trim")
		                             ? 0
		                             : std::min(previous.first, work.result.clip.frameCount());
		    const qint64 end = operation == QStringLiteral("trim")     ? work.result.clip.frameCount()
		                       : operation == QStringLiteral("delete") ? first
		                                                               : previous.end;
		    commitEdit(previous, work, label, first, end);
	    }, previous.clip);
}

void AudioEditorDialog::setSelection(qint64 firstFrame, qint64 endFrame)
{
	if (isBusy()) {
		return;
	}
	stop();
	m_state.first = std::clamp<qint64>(firstFrame, 0, m_state.clip.frameCount());
	m_state.end = std::clamp<qint64>(endFrame, m_state.first, m_state.clip.frameCount());
	m_waveform->setPlayheadFrame(m_state.first);
	refreshSelection();
	checkpointRecovery();
}

void AudioEditorDialog::trimHistory()
{
	const auto storage = [](const State& state) {
		return (state.clip.samples.size() + state.peaks.peaks.size()) * qint64(sizeof(float)) +
		       state.waveform.cacheBytes() + (state.clip.markers.empty() ? 0 : AudioMarkerByteLimit * 2);
	};
	qint64 bytes = 0;
	for (const State& state : m_undo) {
		bytes += storage(state);
	}
	for (const State& state : m_redo) {
		bytes += storage(state);
	}
	while (m_undo.size() + m_redo.size() > 32 ||
	       (bytes > 256 * 1024 * 1024 && m_undo.size() + m_redo.size() > 1)) {
		// Drop the farthest past/future state while retaining the nearest step
		// in each direction when possible. Undoing a short edit into a long clip
		// can increase redo storage, so both stacks share the same budget.
		auto& history = m_undo.size() > 1 || m_redo.isEmpty() ? m_undo : m_redo;
		bytes -= storage(history.first());
		history.removeFirst();
	}
}

void AudioEditorDialog::undo()
{
	if (isBusy() || m_undo.isEmpty()) {
		return;
	}
	stop();
	m_redo << m_state;
	m_state = m_undo.takeLast();
	trimHistory();
	checkpointRecovery();
	showStatus(tr("Edit undone."));
	refresh();
}

void AudioEditorDialog::redo()
{
	if (isBusy() || m_redo.isEmpty()) {
		return;
	}
	stop();
	m_undo << m_state;
	m_state = m_redo.takeLast();
	trimHistory();
	checkpointRecovery();
	showStatus(tr("Edit reapplied."));
	refresh();
}

void AudioEditorDialog::refreshSelection()
{
	const QSignalBlocker firstBlock(m_first), endBlock(m_end);
	m_first->setRange(0, static_cast<int>(m_state.clip.frameCount()));
	m_end->setRange(0, static_cast<int>(m_state.clip.frameCount()));
	m_first->setValue(static_cast<int>(m_state.first));
	m_end->setValue(static_cast<int>(m_state.end));
	m_waveform->setSelectionFrames(m_state.first, m_state.end);
	for (QAction* edit : m_edits) {
		const QString id = edit->objectName();
		bool enabled = m_state.end > m_state.first;
		if (id.endsWith(QStringLiteral("select-all"))) {
			enabled = m_state.clip.frameCount() > 0;
		}
		if (id.endsWith(QStringLiteral("mono"))) {
			enabled = m_state.first == 0 && m_state.end > 0 && m_state.end == m_state.clip.frameCount() &&
			          m_state.clip.channels > 1;
		}
		if (id.endsWith(QStringLiteral("stereo"))) {
			enabled = m_state.first == 0 && m_state.end > 0 && m_state.end == m_state.clip.frameCount() &&
			          m_state.clip.channels == 1;
		}
		edit->setEnabled(!isBusy() && enabled);
	}
	refreshClipboardActions();
	refreshView();
	refreshPlayback();
}

void AudioEditorDialog::refreshView()
{
	const qint64 first = m_waveform->visibleStart(), end = m_waveform->visibleEnd();
	const qint64 span = end - first;
	const QSignalBlocker blocker(m_pan);
	m_pan->setRange(0, static_cast<int>(std::max<qint64>(0, m_state.clip.frameCount() - span)));
	m_pan->setPageStep(static_cast<int>(span));
	m_pan->setSingleStep(static_cast<int>(std::max<qint64>(1, span / 10)));
	m_pan->setValue(static_cast<int>(first));
	m_pan->setEnabled(!isBusy() && span < m_state.clip.frameCount());
	m_viewControls->setEnabled(!isBusy() && m_state.clip.frameCount() > 0);
	m_zoomSelection->setEnabled(!isBusy() && m_state.end > m_state.first);
	m_viewRange->setText(tr("View: %1–%2 frames · %3 ms")
	                         .arg(first)
	                         .arg(end)
	                         .arg(span * 1000.0 / std::max(1, m_state.clip.sampleRate), 0, 'f', 3));
	m_viewRange->setAccessibleDescription(m_viewRange->text());
}

void AudioEditorDialog::refresh()
{
	const bool loaded = m_state.clip.channels > 0;
	m_source->setText(loaded ? (m_projectIdentity.path.isEmpty() ? m_sourceName : m_projectIdentity.path)
	                         : tr("Open a sound or audio project."));
	// A fixed accessible name replaces QLabel's displayed text in Qt's name channel.
	m_source->setAccessibleDescription(m_source->text());
	if (loaded) {
		float peak = 0;
		for (float value : m_state.peaks.peaks) {
			peak = std::max(peak, std::abs(value));
		}
		m_summary->setText(tr("%1 Hz · Channels: %2 · Frames: %3 · %4 s · Peak %5 dBFS%6")
		                       .arg(m_state.clip.sampleRate)
		                       .arg(m_state.clip.channels)
		                       .arg(m_state.clip.frameCount())
		                       .arg(double(m_state.clip.frameCount()) / m_state.clip.sampleRate, 0, 'f', 3)
		                       .arg(peak > 0 ? QString::number(20.0 * std::log10(peak), 'f', 1) : tr("−∞"))
		                       .arg(peak > 1 ? tr(" — integer output will clip; reduce gain or "
		                                          "normalize")
		                                     : QString()));
		if (m_waveformRevision != m_state.revision) {
			m_waveform->setData(m_state.waveform, m_waveformRevision == 0);
			m_waveformRevision = m_state.revision;
		}
	} else {
		m_summary->clear();
		m_waveform->setData({}, true);
	}
	m_summary->setAccessibleDescription(m_summary->text());
	m_waveform->setEnabled(!isBusy());
	m_waveform->setMarkers(m_state.clip.markers);
	m_first->setEnabled(loaded && !isBusy());
	m_end->setEnabled(loaded && !isBusy());
	m_progress->setVisible(isBusy());
	m_cancel->setVisible(isBusy());
	m_cancel->setEnabled(isBusy() && m_work->cancellable && !m_work->cancel);
	m_open->setEnabled(!isBusy());
	m_new->setEnabled(!isBusy());
	m_save->setEnabled(loaded && !isBusy());
	m_saveAs->setEnabled(loaded && !isBusy());
	m_recover->setEnabled(!isBusy());
	m_export->setEnabled(m_state.clip.frameCount() > 0 && !isBusy());
	m_toSession->setEnabled(m_state.clip.frameCount() > 0 && !isBusy());
	m_analyze->setEnabled(m_state.clip.frameCount() > 0 && !isBusy());
	m_markers->setEnabled(m_state.clip.frameCount() > 0 && !isBusy());
	m_selectLoop->setEnabled(m_state.clip.markers.loop.has_value() && !isBusy());
	m_undoAction->setEnabled(!isBusy() && !m_undo.isEmpty());
	m_redoAction->setEnabled(!isBusy() && !m_redo.isEmpty());
	m_undoAction->setText(m_undo.isEmpty() ? tr("Undo") : tr("Undo %1").arg(m_state.editLabel));
	m_redoAction->setText(m_redo.isEmpty() ? tr("Redo") : tr("Redo %1").arg(m_redo.last().editLabel));
	setWindowModified(hasChanges());
	refreshSelection();
	refreshContext();
}

void AudioEditorDialog::refreshContext()
{
	const AudioEditorContext current = context ? context() : AudioEditorContext();
	m_contextToken = current.packageToken;
	if (m_contextKey != current.packagePath || m_contextDoomWad != current.doomWad) {
		m_contextKey = current.packagePath;
		m_contextDoomWad = current.doomWad;
		m_replace->setChecked(false);
		const QSignalBlocker blocker(m_stagePreset);
		m_stagePreset->setCurrentIndex(m_stagePreset->findData(
		    int(current.doomWad ? AudioDeliveryPreset::Doom : AudioDeliveryPreset::Wav)));
		m_packagePath->setText(suggestedSoundPath(m_sourceName, current.doomWad));
	}
	m_contextLabel->setText(current.canStage ? (current.packagePath.isEmpty() ? tr("Untitled package") : current.packagePath)
	                                         : tr("Open a folder, PAK, ZIP, PK3, or Doom WAD "
	                                              "to stage edited sounds."));
	m_contextLabel->setAccessibleDescription(m_contextLabel->text());
	const AudioDeliveryOptions options{static_cast<AudioDeliveryPreset>(m_stagePreset->currentData().toInt()),
	                                   {AudioWavFormat::Pcm16, m_stageDither->isChecked()}};
	const auto plan = planAudioDelivery(m_state.clip, options);
	const bool compatible = current.doomWad == (options.preset == AudioDeliveryPreset::Doom);
	m_stageFormat->setText(compatible        ? audioDeliveryDescription(plan)
	                       : current.doomWad ? tr("Choose the Doom preset for this WAD.")
	                                         : tr("Choose a WAV preset for this package."));
	m_stageFormat->setAccessibleDescription(m_stageFormat->text());
	m_stagePreset->setToolTip(m_stagePreset->currentText());
	m_stagePreset->setEnabled(!isBusy());
	m_stageDither->setEnabled(!isBusy());
	m_stage->setEnabled(!isBusy() && plan.error.isEmpty() && compatible && current.canStage && bool(handoff));
	m_stageAndPlace->setEnabled(!isBusy() && m_state.clip.frameCount() > 0 && current.canStage &&
	                           !current.doomWad && bool(levelHandoff) &&
	                           (current.levelTarget.format == LevelMapFormat::QuakeMap ||
	                            current.levelTarget.format == LevelMapFormat::Quake3Map));
	m_packagePath->setEnabled(!isBusy());
	m_replace->setEnabled(!isBusy());
}

void AudioEditorDialog::cancelWork()
{
	if (m_work && m_work->cancellable) {
		m_work->cancel = true;
		m_cancel->setEnabled(false);
		showStatus(tr("Cancelling audio operation…"));
	}
}

bool AudioEditorDialog::exportTo(const QString& path, bool overwrite, const AudioWavOptions& options)
{
	return exportDeliveryTo(path, overwrite, {AudioDeliveryPreset::Wav, options});
}

bool AudioEditorDialog::exportDeliveryTo(const QString& path, bool overwrite,
                                         const AudioDeliveryOptions& options)
{
	if (isBusy() || m_state.clip.frameCount() == 0) {
		return false;
	}
	const AudioClip clip = m_state.clip;
	const QStringList protectedPaths{m_protectedPath, m_projectIdentity.path, m_recoveryInputPath};
	runWork(
	    tr("Preparing audio delivery…"),
	    [clip, options](AudioEditorWork& work) {
		    work.delivery = renderAudioDelivery(clip, options, work.control());
		    work.error = work.delivery.error;
		    work.result.cancelled = work.delivery.cancelled;
	    },
	    [this, path, protectedPaths, overwrite](const AudioEditorWork& prepared) {
		    const auto delivery = prepared.delivery;
		    runWork(
		        tr("Saving audio delivery…"),
		        [delivery, path, overwrite, protectedPaths](AudioEditorWork& work) {
			        writeAudioDelivery(delivery, path, overwrite, protectedPaths, &work.error);
		        },
		        [this, path, delivery](const AudioEditorWork&) {
			        QString message = tr("Exported %1. Save the audio project to "
			                             "preserve editable work.")
			                              .arg(path);
			        if (delivery.plan.wav.format != AudioWavFormat::Float32 &&
			            delivery.samplesAboveFullScale > 0) {
				        message += tr(" Converted samples exceeded full scale; reduce "
				                      "gain before integer delivery.");
			        }
			        showStatus(message);
		        });
		    // Preparation is cancellable. A completed atomic commit must never be
		    // reported as cancelled just because a late Cancel arrived.
		    m_work->cancellable = false;
		    m_cancel->setEnabled(false);
	    });
	return true;
}

void AudioEditorDialog::chooseExport()
{
	QDialog settings(this);
	settings.setLayoutDirection(layoutDirection());
	settings.setObjectName(QStringLiteral("audioWavExportDialog"));
	settings.setWindowTitle(tr("Audio Export Options"));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = audioOptionsBody(settings, buttons);
	auto* preset = new QComboBox;
	preset->setObjectName(QStringLiteral("audioDeliveryPreset"));
	preset->setAccessibleName(tr("Audio export delivery preset"));
	addDeliveryPresets(preset);
	auto* precision = new QComboBox;
	precision->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	precision->setMinimumContentsLength(18);
	precision->setObjectName(QStringLiteral("audioWavPrecision"));
	precision->setAccessibleName(tr("WAV sample precision"));
	precision->addItem(tr("8-bit unsigned PCM"), int(AudioWavFormat::Pcm8));
	precision->addItem(tr("16-bit PCM"), int(AudioWavFormat::Pcm16));
	precision->addItem(tr("24-bit PCM"), int(AudioWavFormat::Pcm24));
	precision->addItem(tr("32-bit PCM"), int(AudioWavFormat::Pcm32));
	precision->addItem(tr("32-bit float (retain headroom)"), int(AudioWavFormat::Float32));
	precision->setCurrentIndex(1);
	auto* dither = new QCheckBox(tr("Triangular dither"));
	dither->setAccessibleName(tr("Apply triangular dither at integer export"));
	dither->setToolTip(tr("Add low-level triangular noise when reducing to "
	                      "integer precision. Apply only at final "
	                      "delivery; float exports need no dither."));
	auto* summary = new AudioOptionsSummary;
	summary->setAccessibleName(tr("Audio export format summary"));
	AudioWavFormat customFormat = AudioWavFormat::Pcm16;
	const auto update = [&]() {
		const auto chosen = static_cast<AudioDeliveryPreset>(preset->currentData().toInt());
		const bool custom = chosen == AudioDeliveryPreset::Wav;
		const auto plan = planAudioDelivery(
		    m_state.clip,
		    {chosen,
		     {customFormat, dither->isChecked() && !(custom && customFormat == AudioWavFormat::Float32)}});
		precision->setEnabled(custom);
		{
			const QSignalBlocker blocker(precision);
			precision->setCurrentIndex(precision->findData(int(plan.wav.format)));
		}
		preset->setToolTip(preset->currentText());
		precision->setToolTip(precision->currentText());
		const bool floating = plan.wav.format == AudioWavFormat::Float32;
		dither->setEnabled(!floating);
		if (floating) {
			dither->setChecked(false);
		}
		summary->setSummary(audioDeliveryDescription(plan) + QLatin1Char('\n') +
		                    (floating ? tr("Float samples and headroom are retained. Game playback "
		                                   "support varies.")
		                              : tr("Integer output clips values outside full scale. "
		                                   "Normalize or reduce gain if needed.")) +
		                    (custom ? QString()
		                            : QLatin1Char('\n') + tr("The delivery copy uses an equal-weight "
		                                                     "mono mix and resampling.")));
		buttons->button(QDialogButtonBox::Ok)->setEnabled(plan.error.isEmpty());
	};
	connect(precision, &QComboBox::currentIndexChanged, &settings, [&](int) {
		customFormat = static_cast<AudioWavFormat>(precision->currentData().toInt());
		update();
	});
	connect(preset, &QComboBox::currentIndexChanged, &settings, [&](int) { update(); });
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Choose File…"));
	connect(buttons, &QDialogButtonBox::accepted, &settings, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &settings, &QDialog::reject);
	layout->addRow(tr("Delivery &preset"), preset);
	layout->addRow(tr("&Precision"), precision);
	layout->addRow(dither);
	layout->addRow(summary);
	update();
	if (settings.exec() != QDialog::Accepted) {
		return;
	}
	const AudioDeliveryOptions options{
	    static_cast<AudioDeliveryPreset>(preset->currentData().toInt()),
	    {static_cast<AudioWavFormat>(precision->currentData().toInt()), dither->isChecked(), 0}};
	const bool doom = options.preset == AudioDeliveryPreset::Doom;
	const QString path = QFileDialog::getSaveFileName(
	    this, tr("Export Edited Sound"),
	    QFileInfo(m_sourceName).completeBaseName() +
	        (doom ? QStringLiteral("-edited.dmx") : QStringLiteral("-edited.wav")),
	    doom ? tr("Doom DMX sound (*.dmx *.lmp)") : tr("WAV audio (*.wav)"));
	if (!path.isEmpty()) {
		exportDeliveryTo(path, true, options);
	}
}

void AudioEditorDialog::stage(bool place)
{
	const AudioEditorContext current = context ? context() : AudioEditorContext();
	if (isBusy() || !current.canStage || (place ? !levelHandoff : !handoff)) {
		return;
	}
	if (current.packageToken != m_contextToken || current.packagePath != m_contextKey) {
		refreshContext();
		showStatus(tr("The target package changed. Review the destination and stage again."));
		return;
	}
	const QString path = m_packagePath->text().trimmed();
	const bool replace = m_replace->isChecked();
	const AudioClip clip = m_state.clip;
	AudioDeliveryOptions options{static_cast<AudioDeliveryPreset>(m_stagePreset->currentData().toInt()),
	                                   {AudioWavFormat::Pcm16, m_stageDither->isChecked()}};
	LevelSoundRequest placement;
	if (place) {
		placement.virtualPath = path;
		placement.origin = current.levelOrigin;
		AudioPlacementDialog dialog(current.levelTarget, current.levelName, current.packagePath, placement, this);
		if (dialog.exec() != QDialog::Accepted) { return; }
		placement = dialog.request();
		const auto plan = planLevelSound(current.levelTarget, placement);
		if (!plan.valid()) { showStatus(plan.error); return; }
		options.preset = plan.game == QStringLiteral("quake2") ? AudioDeliveryPreset::Quake2 : AudioDeliveryPreset::Quake3;
	}
	const auto unchanged = [this, current, place]() {
		const AudioEditorContext now = context ? context() : AudioEditorContext();
		return now.canStage && (place ? bool(levelHandoff) : bool(handoff)) &&
		       now.packagePath == current.packagePath && now.doomWad == current.doomWad &&
		       now.packageToken == current.packageToken && (!place || now.levelToken == current.levelToken);
	};
	if (!unchanged()) {
		refreshContext();
		showStatus(tr("The map or package changed. Review the destination and try again."));
		return;
	}
	runWork(
	    tr("Preparing sound for the package…"),
	    [clip, options](AudioEditorWork& work) {
		    work.delivery = renderAudioDelivery(clip, options, work.control());
		    work.error = work.delivery.error;
		    work.result.cancelled = work.delivery.cancelled;
	    },
	    [this, unchanged, path, replace, place, placement](const AudioEditorWork& work) {
		    if (!unchanged()) {
			    showStatus(tr("The map or package changed. Review the destination and try again."));
			    return;
		    }
		    QString error;
		    const bool applied = place ? levelHandoff(work.delivery.bytes, placement, replace, &error)
		                               : handoff(work.delivery.bytes, path, replace, &error);
		    if (!applied) {
			    showStatus(tr("Unable to stage sound: %1").arg(error));
			    return;
		    }
		    QString message = place ? tr("Staged %1 and placed a sound entity. Review dependencies, then save both the map and the package. Undo each in its own editor.").arg(path)
		                            : tr("Staged %1. Save the pending package changes in Packages.").arg(path);
		    if (work.delivery.samplesAboveFullScale > 0) {
			    message += tr(" Converted samples exceeded full scale; reduce gain "
			                  "before integer delivery.");
		    }
		    showStatus(message);
	    });
}

bool AudioEditorDialog::confirmDiscard(std::function<void()> afterSave)
{
	if (!hasChanges()) {
		return true;
	}
	const auto response = QMessageBox::question(
	    this, tr("Unsaved Audio Project"),
	    tr("Save the edits before opening another sound? Exporting or staging "
	       "does not save the audio project."),
	    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (response == QMessageBox::Save && chooseProjectSave()) {
		m_afterSave = std::move(afterSave);
	}
	return response == QMessageBox::Discard;
}

void AudioEditorDialog::closeEvent(QCloseEvent* event)
{
	if (isBusy()) {
		showStatus(tr("Wait for the operation to finish, or cancel it before closing."));
		event->ignore();
		return;
	}
	if (hasChanges()) {
		const auto response = QMessageBox::question(
		    this, tr("Unsaved Audio Project"), tr("Save the audio project before closing?"),
		    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
		if (response == QMessageBox::Cancel) {
			event->ignore();
			return;
		}
		if (response == QMessageBox::Save) {
			m_closeAfterSave = chooseProjectSave();
			event->ignore();
			return;
		}
	}
	retireRecovery();
	stop();
	// The base closeEvent calls our reject() override and would re-enter close.
	event->accept();
	QDialog::reject();
}

void AudioEditorDialog::reject() { close(); }

void AudioEditorDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
		refreshContext();
	}
}

void AudioEditorDialog::play()
{
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
	if (playbackAllowed && !playbackAllowed()) {
		showStatus(tr("Finish recording and take review before starting another audition."));
		return;
	}
	if (isBusy() || m_state.clip.frameCount() == 0 || !m_playback->available()) {
		return;
	}
	if (m_playback->state() == AudioPlayback::State::Playing) {
		m_playback->pause();
		return;
	}
	if (m_playback->state() == AudioPlayback::State::Loading) {
		return;
	}
	if (beforePlayback) {
		beforePlayback();
	}
	if (m_playback->state() == AudioPlayback::State::Paused) {
		m_playback->resume();
		return;
	}
	const qint64 first = m_state.first;
	const qint64 end = m_state.end > m_state.first ? m_state.end : m_state.clip.frameCount();
	if (first >= end) {
		return;
	}
	const AudioClip clip = m_state.clip;
	runWork(
	    tr("Preparing edited playback…"),
	    [clip, first, end](AudioEditorWork& work) {
		    work.result = applyAudioEdit(clip, {QStringLiteral("trim"), first, end}, work.control());
		    if (work.result.succeeded()) {
			    AudioWavOptions playback;
			    playback.format = AudioWavFormat::Float32;
			    playback.markers = AudioWavMarkers::Omit;
			    work.wav = encodeAudioWav(work.result.clip, playback, &work.error, work.control());
		    }
	    },
	    [this, first, end, rate = clip.sampleRate](const AudioEditorWork& work) {
		    if (playbackAllowed && !playbackAllowed()) { return; }
		    // Set this before start: a synchronous backend error must win the status.
		    showStatus(tr("Auditioning edited samples. Output uses the system audio device."));
		    m_waveform->setPlayheadFrame(first);
		    m_playback->start(work.wav, first, end, rate);
	    });
	m_work->playback = true;
	refreshPlayback();
#endif
}

void AudioEditorDialog::stop()
{
	if (m_playback) { m_playback->stop(); }
	refreshPlayback();
}

void AudioEditorDialog::stopPlayback()
{
	// A browser audition also cancels a pending selection encode, so it cannot
	// start a second player after the browser has already begun.
	if (isBusy() && m_work->playback) { cancelWork(); }
	stop();
}

void AudioEditorDialog::stopPlaybackAndWait(QObject* context, std::function<void(bool)> complete)
{
	if (isBusy() && m_work->playback) { cancelWork(); }
	m_playback->stopAndWait(context, std::move(complete));
	refreshPlayback();
}

void AudioEditorDialog::refreshPlayback()
{
	if (!m_play || !m_stop || !m_playback) { return; }
	const auto state = m_playback->state();
	const bool preparing = isBusy() && m_work->playback;
	const bool playing = state == AudioPlayback::State::Playing;
	const bool paused = state == AudioPlayback::State::Paused;
	const bool loading = state == AudioPlayback::State::Loading;
	const bool active = m_playback->active();
	m_play->setText(playing ? tr("Pause") : paused ? tr("Resume")
	                        : (m_state.end > m_state.first ? tr("Play Selection")
	                           : m_state.first > 0         ? tr("Play From Cursor") : tr("Play Sound")));
	m_play->setIcon(studioIcon(playing ? QStringLiteral("pause") : QStringLiteral("play")));
	m_play->setEnabled(VIBESTUDIO_HAVE_AUDIO_PLAYBACK && m_playback->available() && !isBusy() &&
	                   m_state.first < m_state.clip.frameCount() && !loading);
	m_stop->setEnabled((active && !isBusy()) || (preparing && !m_work->cancel));
	m_loop->setEnabled(VIBESTUDIO_HAVE_AUDIO_PLAYBACK && (!isBusy() || preparing));
	m_volume->setEnabled(VIBESTUDIO_HAVE_AUDIO_PLAYBACK);
	const qint64 frame = m_waveform->playheadFrame();
	const int rate = std::max(1, m_state.clip.sampleRate);
	const QString status = preparing ? tr("Preparing playback") : loading ? tr("Loading playback")
	                       : paused ? tr("Paused") : m_playback->stalled() ? tr("Buffering audio")
	                       : playing ? tr("Playing") : state == AudioPlayback::State::Error ? tr("Playback failed")
	                       : !VIBESTUDIO_HAVE_AUDIO_PLAYBACK ? tr("Playback unavailable") : tr("Stopped");
	m_clock->setText(tr("%1 · %2 s · Frame %3").arg(status).arg(frame / double(rate), 0, 'f', 3).arg(frame));
	m_clock->setAccessibleDescription(status);
	const QSignalBlocker seekBlock(m_seek), frameBlock(m_seekFrame);
	const int first = static_cast<int>(active ? m_playback->firstFrame() : 0);
	const int end = static_cast<int>(active ? m_playback->endFrame() : m_state.clip.frameCount());
	m_seek->setRange(first, end);
	m_seekFrame->setRange(first, end);
	m_seek->setSingleStep(std::max(1, rate / 1000));
	m_seek->setPageStep(std::min(end - first, rate));
	if (!m_seek->isSliderDown()) { m_seek->setValue(static_cast<int>(frame)); }
	if (!m_seekFrame->hasFocus()) { m_seekFrame->setValue(static_cast<int>(frame)); }
	const bool seekable = VIBESTUDIO_HAVE_AUDIO_PLAYBACK && !isBusy() && m_playback->canSeek();
	m_seek->setEnabled(seekable);
	m_seekFrame->setEnabled(seekable);
}
} // namespace vibestudio
