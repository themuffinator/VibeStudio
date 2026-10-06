#include "app/audio_session_dialog.h"
#include "app/audio_arrangement_dialog.h"
#include "app/audio_automation_editor.h"
#include "app/audio_effects_dialog.h"
#include "app/audio_media_dialog.h"
#include "app/audio_meter_dialog.h"
#include "app/audio_range_dialog.h"
#include "app/audio_recovery.h"
#include "app/audio_recovery_dialog.h"
#include "app/audio_routing_dialog.h"
#include "app/audio_session_timeline.h"
#include "app/audio_stem_export_dialog.h"
#include "app/audio_take_dialog.h"
#include "app/audio_recording_dialog.h"
#include "app/audio_tempo_dialog.h"
#include "app/studio_icons.h"
#include "core/studio_settings.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace vibestudio
{
struct AudioSessionWork {
	std::atomic_bool cancelled{false};
	std::atomic_int percent{-1};
	bool audition = false;
	AudioSession session;
	AudioProjectIdentity identity;
	QHash<QString, AudioWaveformData> waveforms;
	QString error;
	QByteArray wav;
	AudioProjectSaveReport saved;
	AudioSessionMixdownReport mixed;
	AudioStemExportReport stems;
	AudioMeterReport meters;
	bool meterJob = false;
	bool stemJob = false;
	QString addedTrack, addedRegion;
	QString recoverySourcePath;
	AudioWorkControl control()
	{
		return {[this] { return cancelled.load(); }};
	}
	void prepareWaveforms()
	{
		for (const auto &source : session.sources) {
			if (cancelled || !error.isEmpty()) {
				return;
			}
			if (!waveforms.contains(source.id)) {
				waveforms.insert(source.id, AudioWaveformData::build(source.audio.clip, control()));
			}
		}
	}
};
namespace
{
QDoubleSpinBox *numberBox(const QString &name, double low, double high, int decimals, QWidget *parent = nullptr)
{
	auto *box = new QDoubleSpinBox(parent);
	box->setRange(low, high);
	box->setDecimals(decimals);
	box->setAccessibleName(name);
	box->setKeyboardTracking(false);
	box->setAccelerated(true);
	return box;
}
QFormLayout *inspector(QDialog &dialog, const QString &title)
{
	dialog.setWindowTitle(title);
	dialog.setAccessibleName(title);
	dialog.resize(560, 580);
	auto *outer = new QVBoxLayout(&dialog);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(title);
	auto *contents = new QWidget;
	auto *form = new QFormLayout(contents);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(contents);
	outer->addWidget(scroll, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	outer->addWidget(buttons);
	return form;
}
} // namespace
AudioSessionDialog::AudioSessionDialog(QWidget *parent, AudioStreamDeviceFactory deviceFactory) : QDialog(parent)
{
	setObjectName(QStringLiteral("audioSessionDialog"));
	setWindowTitle(tr("Multitrack Session[*]"));
	setAccessibleName(tr("Multitrack audio session"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(1180, 840);
	auto *outer = new QVBoxLayout(this);
	auto *bodyScroll = new QScrollArea;
	bodyScroll->setWidgetResizable(true);
	bodyScroll->setAccessibleName(tr("Session editing controls"));
	auto *body = new QWidget;
	auto *layout = new QVBoxLayout(body);
	bodyScroll->setWidget(body);
	outer->addWidget(bodyScroll, 1);
	auto *files = new QToolBar(tr("Session document commands"));
	files->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	layout->addWidget(files);
	const auto action = [this](QToolBar *bar, const QString &text, const QString &name, const QString &icon,
	                           const QKeySequence &shortcut, std::function<void()> callback) {
		auto *result = bar->addAction(studioIcon(icon), text);
		result->setObjectName(name);
		result->setToolTip(text);
		result->setShortcut(shortcut);
		result->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		addAction(result);
		connect(result, &QAction::triggered, this, std::move(callback));
		m_actions.append(result);
		return result;
	};
	action(files, tr("New…"), "sessionNew", "new", QKeySequence::New, [this] {
		bool accepted = false;
		const int rate =
		    QInputDialog::getInt(this, tr("New Session"), tr("Sample rate (Hz)"), 48000, 1, 384000, 1, &accepted);
		if (accepted) {
			newSession(rate);
		}
	});
	action(files, tr("Open…"), "sessionOpen", "folder-open", QKeySequence::Open, [this] {
		const auto path =
		    QFileDialog::getOpenFileName(this, tr("Open Session"), {}, tr("Audio sessions (*.vssession)"));
		if (!path.isEmpty()) {
			openSession(path);
		}
	});
	action(files, tr("Save"), "sessionSave", "save", QKeySequence::Save, [this] { chooseSave(); });
	action(files, tr("Save As…"), "sessionSaveAs", "save-as", QKeySequence::SaveAs, [this] { chooseSave(true); });
	action(files, tr("Import…"), "sessionImport", "import", {}, [this] { chooseImport(); });
	action(files, tr("Media…"), "sessionMedia", "inspect", {}, [this] { editMedia(); });
	action(files, tr("Record Tracks…"), "sessionRecordTracks", "audio", {}, [this] { openRecording(); });
	action(files, tr("Single Take…"), "sessionRecordTake", "audio", {}, [this] { chooseTake(); });
	action(files, tr("Add Track…"), "sessionAddTrack", "add", {}, [this] {
		bool accepted = false;
		const auto name =
		    QInputDialog::getText(this, tr("Add Track"), tr("Name"), QLineEdit::Normal, tr("Audio track"), &accepted);
		if (accepted) {
			AudioSessionEdit edit;
			edit.operation = "add-track";
			edit.name = name;
			applyEdit(edit, tr("Add track"));
		}
	});
	m_undoAction = action(files, tr("Undo"), "sessionUndo", "undo", QKeySequence::Undo, [this] { undo(); });
	action(files, tr("Add Bus…"), "sessionAddBus", "add", {}, [this] {
		bool accepted = false;
		const auto name =
		    QInputDialog::getText(this, tr("Add Bus"), tr("Name"), QLineEdit::Normal, tr("Audio bus"), &accepted);
		if (accepted) {
			AudioSessionEdit edit;
			edit.operation = "add-bus";
			edit.name = name;
			applyEdit(edit, tr("Add bus"));
		}
	});
	m_redoAction = action(files, tr("Redo"), "sessionRedo", "redo", QKeySequence::Redo, [this] { redo(); });
	m_summary = new QLabel;
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setAccessibleName(tr("Session summary"));
	layout->addWidget(m_summary);
	auto *split = new QSplitter;
	split->setAccessibleName(tr("Session tracks and arrangement"));
	layout->addWidget(split, 1);
	m_tracks = new QTreeWidget;
	m_tracks->setObjectName("sessionTracks");
	m_tracks->setAccessibleName(tr("Tracks and clips"));
	m_tracks->setHeaderLabels({tr("Track / clip"), tr("Position"), tr("Length"), tr("Group")});
	m_tracks->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_tracks->setAccessibleDescription(
	    tr("Select clips across tracks. Control or Command toggles a clip; Shift extends the selection. Linked groups "
	       "include their other members in arrangement edits."));
	m_tracks->setMinimumWidth(150);
	m_tracks->header()->setStretchLastSection(false);
	m_tracks->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	split->addWidget(m_tracks);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Arrangement lanes"));
	m_timeline = new AudioSessionTimeline;
	scroll->setWidget(m_timeline);
	split->addWidget(scroll);
	split->setSizes({340, 820});
	connect(m_tracks, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
		if (!item) {
			return;
		}
		m_track = item->data(0, Qt::UserRole).toString();
		m_region = item->data(0, Qt::UserRole + 1).toString();
		refreshClipSelection();
	});
	connect(m_tracks, &QTreeWidget::itemSelectionChanged, this, [this] {
		m_selection.clear();
		for (const auto *item : m_tracks->selectedItems()) {
			const auto id = item->data(0, Qt::UserRole + 1).toString();
			if (!id.isEmpty())
				m_selection.append(id);
		}
		refreshClipSelection();
	});
	connect(m_tracks, &QTreeWidget::itemDoubleClicked, this, [this] {
		if (m_region.isEmpty()) {
			editTrack();
		} else {
			editRegion();
		}
	});
	connect(m_timeline, &AudioSessionTimeline::selectionRequested, this,
	        [this](const QString &track, const QString &region, Qt::KeyboardModifiers modifiers) {
		        const bool toggle = modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);
		        const bool add = modifiers.testFlag(Qt::ShiftModifier);
		        if (region.isEmpty()) {
			        if (!toggle && !add)
				        selectRegion(track, {});
			        return;
		        }
		        auto ids = m_selection;
		        if (toggle && ids.contains(region))
			        ids.removeAll(region);
		        else if (toggle || add) {
			        if (!ids.contains(region))
				        ids.append(region);
		        } else if (!selectedRegions(m_linkedGroups->isChecked()).contains(region))
			        ids = {region};
		        // A clicked linked member becomes explicit so it can anchor the drag.
		        else if (!ids.contains(region))
			        ids.append(region);
		        selectRegions(ids, region);
	        });
	connect(m_timeline, &AudioSessionTimeline::cursorChanged, this,
	        [this](qint64 frame) { m_cursor->setValue(double(frame)); });
	connect(m_timeline, &AudioSessionTimeline::moveRequested, this,
	        [this](const QString &track, const QString &region, qint64 frame) {
		        if (!selectedRegions(m_linkedGroups->isChecked()).contains(region))
			        selectRegion(track, region);
		        for (const auto &lane : session().tracks)
			        for (const auto &clip : lane.regions)
				        if (clip.id == region) {
					        AudioArrangementEdit edit;
					        edit.operation = "move";
					        edit.regionIds = m_selection;
					        edit.linkedGroups = m_linkedGroups->isChecked();
					        edit.offset = frame - clip.position;
					        applyArrangement(edit, tr("Move selected clips"));
					        return;
				        }
	        });
	auto *edits = new QToolBar(tr("Arrangement commands"));
	action(edits, tr("Effects…"), "sessionEffects", "settings", {}, [this] { editEffects(false); });
	action(edits, tr("Master Effects…"), "sessionMasterEffects", "settings", {}, [this] { editEffects(true); });
	edits->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	layout->addWidget(edits);
	action(edits, tr("Track…"), "sessionTrackControls", "settings", {}, [this] { editTrack(); });
	action(edits, tr("Clip…"), "sessionClipControls", "edit", {}, [this] {
		editRegion();
	})->setToolTip(tr("Edit only the focused clip, independently of group links."));
	action(edits, tr("Selection…"), "sessionSelectionControls", "edit", {}, [this] { editSelection(); });
	action(edits, tr("Range…"), "sessionRangeControls", "edit", {}, [this] { editRange(); });
	action(edits, tr("Automation…"), "sessionAutomation", "settings", {}, [this] { editAutomation(); });
	action(edits, tr("Routing / Sends…"), "sessionRouting", "settings", {}, [this] { editRouting(); });
	action(edits, tr("Split at Cursor"), "sessionSplit", "cut", {}, [this] {
		AudioArrangementEdit edit;
		edit.operation = "split";
		edit.regionIds = m_selection;
		edit.linkedGroups = m_linkedGroups->isChecked();
		edit.position = qint64(m_cursor->value());
		applyArrangement(edit, tr("Split selected clips"));
	});
	action(edits, tr("Duplicate"), "sessionDuplicate", "copy", {}, [this] {
		AudioArrangementEdit edit;
		edit.operation = "duplicate";
		edit.regionIds = m_selection;
		edit.linkedGroups = m_linkedGroups->isChecked();
		const auto ids = selectedRegions(edit.linkedGroups);
		qint64 first = AudioSessionFrameLimit, end = 0;
		for (const auto &track : session().tracks)
			for (const auto &clip : track.regions)
				if (ids.contains(clip.id)) {
					first = std::min(first, clip.position);
					end = std::max(end, clip.position + clip.length);
				}
		edit.offset = std::max<qint64>(0, end - first);
		applyArrangement(edit, tr("Duplicate selected clips"));
	});
	action(edits, tr("Remove"), "sessionRemove", "delete", {}, [this] {
		if (m_selection.isEmpty()) {
			auto edit = selectedEdit(false);
			edit.operation = "remove-track";
			applyEdit(edit, tr("Remove track"));
		} else {
			AudioArrangementEdit edit;
			edit.operation = "remove";
			edit.regionIds = m_selection;
			edit.linkedGroups = m_linkedGroups->isChecked();
			applyArrangement(edit, tr("Remove selected clips"));
		}
	});
	action(edits, tr("Master Gain…"), "sessionMaster", "settings", {}, [this] {
		bool accepted = false;
		const auto gain = QInputDialog::getDouble(this, tr("Master Gain"), tr("Gain (dB)"), session().masterGainDb, -96,
		                                          24, 2, &accepted);
		if (accepted) {
			AudioSessionEdit edit;
			edit.operation = "master";
			edit.gainDb = gain;
			applyEdit(edit, tr("Master gain"));
		}
	});
	action(edits, tr("Fit"), "sessionFit", "zoom-fit", {}, [this] {
		m_timeline->setVisibleRange(0, std::max<qint64>(session().sampleRate, audioSessionFrames(session())));
	});
	action(edits, tr("Tempo / Meter…"), "sessionTempoMap", "settings", {}, [this] {
		AudioTempoDialog dialog(session().musicalTime, session().sampleRate, qint64(m_cursor->value()), this);
		if (dialog.exec() == QDialog::Accepted && dialog.tempoMap() != session().musicalTime) {
			AudioSessionEdit edit;
			edit.operation = "tempo-map";
			edit.musicalTime = dialog.tempoMap();
			applyEdit(edit, tr("Tempo and meter map"));
		}
	});
	action(edits, tr("Zoom In"), "sessionZoomIn", "zoom-in", {}, [this] {
		const auto span = std::max<qint64>(1, (m_timeline->visibleEnd() - m_timeline->visibleFirst()) / 2);
		const auto first = std::min(qint64(m_cursor->value()), AudioSessionFrameLimit - span);
		m_timeline->setVisibleRange(first, first + span);
	});
	action(edits, tr("Zoom Out"), "sessionZoomOut", "zoom-out", {}, [this] {
		const auto span = std::min(AudioSessionFrameLimit, (m_timeline->visibleEnd() - m_timeline->visibleFirst()) * 2);
		const auto first = std::min(m_timeline->visibleFirst(), AudioSessionFrameLimit - span);
		m_timeline->setVisibleRange(first, first + span);
	});
	m_linkedGroups = new QCheckBox(tr("Link grouped clips"));
	m_linkedGroups->setObjectName("sessionLinkedGroups");
	m_linkedGroups->setChecked(true);
	m_linkedGroups->setToolTip(tr("Arrangement commands include all members of selected groups, across tracks. The "
	                              "Clip inspector edits only the focused clip."));
	layout->addWidget(m_linkedGroups);
	m_selectionStatus = new QLabel;
	m_selectionStatus->setObjectName("sessionClipSelection");
	m_selectionStatus->setWordWrap(true);
	m_selectionStatus->setAccessibleName(tr("Clip selection"));
	layout->addWidget(m_selectionStatus);
	connect(m_linkedGroups, &QCheckBox::toggled, this, &AudioSessionDialog::refreshClipSelection);
	auto *range = new QFormLayout;
	range->setRowWrapPolicy(QFormLayout::WrapLongRows);
	range->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->addLayout(range);
	m_cursor = numberBox(tr("Cursor frame"), 0, double(AudioSessionFrameLimit), 0);
	m_cursor->setObjectName("sessionCursor");
	m_first = numberBox(tr("Range start frame"), 0, double(AudioSessionFrameLimit), 0);
	m_first->setObjectName("sessionRangeStart");
	m_end = numberBox(tr("Range end frame, exclusive"), 0, double(AudioSessionFrameLimit), 0);
	m_end->setObjectName("sessionRangeEnd");
	m_snap = new QComboBox;
	m_snap->setObjectName("sessionSnap");
	m_snap->setAccessibleName(tr("Timeline snap step"));
	m_snap->addItem(tr("One frame"), 1);
	m_snap->addItem(tr("One millisecond"), 0);
	m_snap->addItem(tr("Quarter beat"), -1);
	m_snap->addItem(tr("One beat"), -2);
	m_snap->addItem(tr("One bar"), -3);
	m_snap->addItem(tr("Half beat"), -4);
	m_snap->addItem(tr("Beat triplet"), -5);
	m_snap->setToolTip(
	    tr("Musical steps follow the tempo map and time signature. A beat uses the current meter's denominator."));
	m_ruler = new QComboBox;
	m_ruler->setObjectName("sessionRuler");
	m_ruler->setAccessibleName(tr("Timeline ruler units"));
	m_ruler->addItem(tr("Seconds"), false);
	m_ruler->addItem(tr("Bars and beats"), true);
	range->addRow(tr("Ruler"), m_ruler);
	connect(m_ruler, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        [this] { m_timeline->setMusicalRuler(m_ruler->currentData().toBool()); });
	auto *positionRow = new QWidget;
	auto *positionLayout = new QHBoxLayout(positionRow);
	positionLayout->setContentsMargins(0, 0, 0, 0);
	m_musicalPosition = new QLineEdit;
	m_musicalPosition->setObjectName("sessionMusicalPosition");
	m_musicalPosition->setAccessibleName(tr("Cursor position, bar.beat.tick"));
	m_musicalPosition->setLayoutDirection(Qt::LeftToRight);
	positionLayout->addWidget(m_musicalPosition, 1);
	auto *go = new QPushButton(tr("Go"));
	go->setObjectName("sessionGoToPosition");
	go->setAccessibleName(tr("Go to musical position"));
	positionLayout->addWidget(go);
	connect(go, &QPushButton::clicked, this, &AudioSessionDialog::goToMusicalPosition);
	connect(m_musicalPosition, &QLineEdit::returnPressed, this, &AudioSessionDialog::goToMusicalPosition);
	range->addRow(tr("Position (bar.beat.tick)"), positionRow);
	range->addRow(tr("Cursor frame"), m_cursor);
	range->addRow(tr("Range start frame"), m_first);
	range->addRow(tr("Range end frame (exclusive)"), m_end);
	range->addRow(tr("Snap"), m_snap);
	connect(m_cursor, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double frame) {
		m_timeline->setCursor(qint64(frame));
		refreshMusicalPosition();
		if (m_playback)
			m_playback->seek(qint64(frame));
	});
	connect(m_snap, qOverload<int>(&QComboBox::currentIndexChanged), this, &AudioSessionDialog::refreshSnap);
	auto *transport = new QToolBar(tr("Session preview and delivery"));
	transport->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	layout->addWidget(transport);
	m_play = action(transport, tr("Play Range"), "sessionPlay", "play", {}, [this] { prepareMix(true); });
	m_stop = action(transport, tr("Stop"), "sessionStop", "stop", {}, [this] { stopPlayback(); });
	action(transport, tr("Meters…"), "sessionMeters", "inspect", {}, [this] { showMeters(); });
	m_loop = new QCheckBox(tr("Loop"));
	m_loop->setAccessibleName(tr("Loop session range"));
	transport->addWidget(m_loop);
	action(transport, tr("Edit Mixdown"), "sessionEditMixdown", "edit", {}, [this] { prepareMix(false); });
	action(transport, tr("Export WAV…"), "sessionExport", "export", {}, [this] {
		QDialog dialog(this);
		auto *form = inspector(dialog, tr("Export Session Mixdown"));
		auto *precision = new QComboBox;
		precision->setAccessibleName(tr("WAV precision"));
		for (const auto value : {AudioWavFormat::Float32, AudioWavFormat::Pcm24, AudioWavFormat::Pcm16,
		                         AudioWavFormat::Pcm32, AudioWavFormat::Pcm8}) {
			precision->addItem(audioWavFormatId(value), int(value));
		}
		form->addRow(tr("WAV precision"), precision);
		auto *dither = new QCheckBox;
		dither->setAccessibleName(tr("TPDF dither for integer PCM"));
		dither->setEnabled(false);
		form->addRow(tr("TPDF dither"), dither);
		connect(precision, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [precision, dither] {
			const bool integer = AudioWavFormat(precision->currentData().toInt()) != AudioWavFormat::Float32;
			dither->setEnabled(integer);
			if (!integer)
				dither->setChecked(false);
		});
		auto *detail = new QLabel(
		    tr("Exports the selected mix range. Float32 retains headroom; integer PCM clips "
		       "values above full scale. Optional TPDF dither uses a continuous, deterministic noise sequence."));
		detail->setWordWrap(true);
		form->addRow(detail);
		if (dialog.exec() != QDialog::Accepted) {
			return;
		}
		const auto path = QFileDialog::getSaveFileName(this, tr("Export Session Mixdown"), {}, tr("WAV audio (*.wav)"));
		if (!path.isEmpty()) {
			exportMix(path, true, AudioWavFormat(precision->currentData().toInt()), dither->isChecked());
		}
	});
	action(transport, tr("Export Stems…"), "sessionExportStems", "export", {}, [this] {
		AudioStemExportDialog dialog(session(), qint64(m_first->value()), qint64(m_end->value()), this);
		if (dialog.exec() == QDialog::Accepted)
			exportStems(dialog.request());
	});
	action(transport, tr("Delivery Report…"), "sessionDeliveryReport", "inspect", {}, [this] {
		if (m_deliveryReport.isEmpty()) {
			status(tr("Export stems to create a delivery report."));
			return;
		}
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Stem Delivery Report"));
		dialog.setAccessibleName(dialog.windowTitle());
		dialog.resize(720, 600);
		auto *layout = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(QString::fromUtf8(QJsonDocument(m_deliveryReport).toJson()), &dialog);
		text->setReadOnly(true);
		text->setAccessibleName(tr("Stem delivery manifest and file results"));
		text->setLayoutDirection(Qt::LeftToRight);
		layout->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);
		dialog.exec();
	});
	m_playbackFactory = deviceFactory;
	m_playback = new AudioSessionPlayback(this, std::move(deviceFactory));
	m_playback->setObjectName("sessionPlayback");
	connect(m_first, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { refreshMeters(); });
	connect(m_end, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { refreshMeters(); });
	m_playback->setVolume(0.7f);
	connect(m_loop, &QCheckBox::toggled, m_playback, &AudioSessionPlayback::setLoop);
	auto *volume = new QSlider(Qt::Horizontal);
	volume->setObjectName(QStringLiteral("sessionAuditionVolume"));
	volume->setAccessibleName(tr("Session audition volume"));
	volume->setToolTip(tr("Audition volume; does not change exported samples."));
	volume->setRange(0, 100);
	volume->setValue(70);
	volume->setMaximumWidth(140);
	transport->addWidget(volume);
	connect(volume, &QSlider::valueChanged, m_playback,
	        [this](int value) { m_playback->setVolume(float(value) / 100); });
	m_output = new QComboBox;
	m_output->setObjectName("sessionOutputDevice");
	m_output->setAccessibleName(tr("Audio output device"));
	m_output->addItem(tr("System default output"), QByteArray());
	m_output->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_output->setMinimumContentsLength(20);
	auto *deviceRow = new QWidget;
	auto *deviceLayout = new QHBoxLayout(deviceRow);
	deviceLayout->setContentsMargins(0, 0, 0, 0);
	deviceLayout->addWidget(m_output, 1);
	auto *refreshDevices = new QPushButton(tr("Refresh"));
	refreshDevices->setAccessibleName(tr("Refresh audio output devices"));
	deviceLayout->addWidget(refreshDevices);
	range->addRow(tr("Output"), deviceRow);
	connect(refreshDevices, &QPushButton::clicked, m_playback, &AudioSessionPlayback::refreshOutputs);
	connect(m_playback, &AudioSessionPlayback::outputsChanged, this, [this] {
		const auto selected = m_output->currentData().toByteArray();
		const QSignalBlocker guard(m_output);
		m_output->clear();
		m_output->addItem(tr("System default output"), QByteArray());
		for (const auto &device : m_playback->outputs())
			m_output->addItem(device.name, device.id);
		int index = m_output->findData(selected);
		if (index < 0) {
			m_output->addItem(tr("Unavailable output"), selected);
			index = m_output->count() - 1;
		}
		m_output->setCurrentIndex(index);
	});
	m_buffer = new QComboBox;
	m_buffer->setObjectName("sessionOutputBuffer");
	m_buffer->setAccessibleName(tr("Requested output buffer in frames"));
	m_buffer->setToolTip(tr("Smaller buffers reduce delay; larger buffers tolerate slower processing. The device may "
	                        "choose a different size."));
	for (const int frames : {256, 512, 1024, 2048, 4096, 8192, 16384})
		m_buffer->addItem(tr("%1 frames").arg(frames), frames);
	m_buffer->setCurrentIndex(m_buffer->findData(2048));
	range->addRow(tr("Output buffer"), m_buffer);
	connect(m_output, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { stopPlayback(); });
	connect(m_buffer, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { stopPlayback(); });
	for (auto *box : {m_first, m_end})
		connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
			stopPlayback();
			m_timeline->setTimeRange(qint64(m_first->value()), qint64(m_end->value()));
		});
	m_transport = new QLabel;
	m_transport->setObjectName("sessionTransportStatus");
	m_transport->setTextFormat(Qt::PlainText);
	m_transport->setWordWrap(true);
	m_transport->setAccessibleName(tr("Session transport state"));
	layout->addWidget(m_transport);
	connect(m_playback, &AudioSessionPlayback::changed, this, [this] {
		refreshTransport();
		const auto &snapshot = m_playback->snapshot();
		using State = AudioSessionPlaybackSnapshot::State;
		if (snapshot.state == State::Playing || snapshot.state == State::Paused || snapshot.state == State::Ended) {
			m_timeline->setCursor(snapshot.position);
			const QSignalBlocker guard(m_cursor);
			m_cursor->setValue(double(snapshot.position));
			refreshMusicalPosition();
		}
	});
	m_recoveryDirectory = audioRecoveryDirectory();
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_recovery = new AudioRecoveryWriter(m_recoveryDirectory, this, AudioRecoveryKind::Session);
	m_recoveryEnabled = new QCheckBox(tr("Keep local recovery copies"));
	m_recoveryEnabled->setObjectName(QStringLiteral("sessionRecoveryEnabled"));
	m_recoveryEnabled->setAccessibleName(tr("Keep local audio recovery copies"));
	m_recoveryEnabled->setToolTip(tr("Checkpoint unsaved sessions and waveform edits on this device. Copies contain "
	                                 "embedded audio. Turning this off retains existing copies."));
	m_recoveryEnabled->setChecked(StudioSettings().audioRecoveryEnabled());
	layout->addWidget(m_recoveryEnabled);
	m_recoveryStatus = new QLabel;
	m_recoveryStatus->setObjectName(QStringLiteral("sessionRecoveryStatus"));
	m_recoveryStatus->setAccessibleName(tr("Session recovery status"));
	m_recoveryStatus->setTextFormat(Qt::PlainText);
	m_recoveryStatus->setWordWrap(true);
	layout->addWidget(m_recoveryStatus);
	m_recovery->finished = [this](const QString &path, const QString &error) {
		m_recoveryStatus->setText(error.isEmpty() ? tr("Recovery copy updated.")
		                                          : tr("Recovery failed: %1").arg(error));
		m_recoveryStatus->setAccessibleDescription(m_recoveryStatus->text());
		m_recoveryStatus->setToolTip(path);
	};
	m_recoveryTimer = new QTimer(this);
	m_recoveryTimer->setSingleShot(true);
	m_recoveryTimer->setInterval(1000);
	connect(m_recoveryTimer, &QTimer::timeout, this, &AudioSessionDialog::checkpointRecovery);
	connect(m_recoveryEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
		StudioSettings settings;
		settings.setAudioRecoveryEnabled(enabled);
		settings.sync();
		Q_EMIT recoveryPreferenceChanged(enabled);
		if (enabled) {
			scheduleRecovery();
		} else {
			m_recoveryTimer->stop();
			m_recoveryStatus->setText(tr("Automatic recovery is off. Existing copies are retained."));
			m_recoveryStatus->setAccessibleDescription(m_recoveryStatus->text());
		}
	});
	auto *recover = new QPushButton(tr("Review Audio Recoveries…"));
	recover->setAccessibleName(tr("Review waveform and session recovery copies"));
	layout->addWidget(recover);
	connect(recover, &QPushButton::clicked, this, [this] {
		auto *dialog = new AudioRecoveryDialog(m_recoveryDirectory, this);
		dialog->restore = [this](const QString &path, const QByteArray &digest, AudioRecoveryKind kind) {
			if (kind == AudioRecoveryKind::Session) {
				restoreRecovery(path, digest);
			} else {
				Q_EMIT waveformRecoveryRequested(path, digest);
			}
		};
		dialog->show();
	});
	m_status = new QLabel(tr("Create tracks or import sounds to arrange a session."));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Session operation status"));
	outer->addWidget(m_status);
	auto *progress = new QHBoxLayout;
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Session operation progress"));
	progress->addWidget(m_progress, 1);
	m_cancel = new QPushButton(tr("Cancel Operation"));
	m_cancel->setAccessibleName(tr("Cancel session operation"));
	connect(m_cancel, &QPushButton::clicked, this, &AudioSessionDialog::cancelWork);
	progress->addWidget(m_cancel);
	outer->addLayout(progress);
	auto *timer = new QTimer(this);
	timer->setInterval(80);
	connect(timer, &QTimer::timeout, this, [this] {
		if (m_work) {
			const int value = m_work->percent.load();
			m_progress->setRange(0,
			                     value < 0 ? (StudioSettings().accessibilityPreferences().reducedMotion ? 1 : 0) : 100);
			m_progress->setTextVisible(value >= 0);
			if (value >= 0) {
				m_progress->setValue(value);
			}
		}
	});
	timer->start();
	m_state.revision = ++m_nextRevision;
	m_savedRevision = m_state.revision;
	refresh();
}

AudioSessionDialog::~AudioSessionDialog()
{
	delete m_take.data();
	m_take = nullptr;
	delete m_recording.data();
	m_recording = nullptr;
	// Join the output worker before child widgets and document snapshots vanish.
	disconnect(m_playback, nullptr, this, nullptr);
	delete m_playback;
	m_playback = nullptr;
	// Abnormal destruction retains the latest accepted document, including an
	// edit still waiting for the short coalescing timer. Approved closes retire it.
	if (m_recoveryTimer->isActive()) {
		checkpointRecovery();
	}
	delete m_recovery;
	m_recovery = nullptr;
	if (m_work) {
		m_work->cancelled = true;
	}
	if (m_thread) {
		m_thread->wait();
	}
}
void AudioSessionDialog::status(const QString &text)
{
	m_status->setText(text);
	m_status->setAccessibleDescription(text);
}
void AudioSessionDialog::stopPlayback()
{
	if (m_work && m_work->audition) {
		m_work->cancelled = true;
	}
	if (m_playback) {
		m_playback->stop();
	}
}
void AudioSessionDialog::cancelWork()
{
	if (m_playback->snapshot().state == AudioSessionPlaybackSnapshot::State::Preparing) {
		stopPlayback();
	}
	if (m_work) {
		m_work->cancelled = true;
		status(tr("Cancelling session operation…"));
	}
}
void AudioSessionDialog::setSelection(qint64 first, qint64 end)
{
	if (isBusy()) {
		return;
	}
	m_first->setValue(double(first));
	m_end->setValue(double(end));
}
void AudioSessionDialog::selectRegion(const QString &track, const QString &region)
{
	m_track = track;
	selectRegions(region.isEmpty() ? QStringList{} : QStringList{region}, region);
}
void AudioSessionDialog::selectRegions(const QStringList &ids, const QString &primary)
{
	const auto requested = ids;
	const auto focus = primary.isEmpty() ? m_region : primary;
	const auto track = m_track;
	const QSignalBlocker guard(m_tracks);
	m_tracks->clearSelection();
	m_tracks->setCurrentItem(nullptr);
	m_selection.clear();
	m_track.clear();
	m_region.clear();
	QTreeWidgetItem *current = nullptr, *root = nullptr;
	for (int i = 0; i < m_tracks->topLevelItemCount(); ++i) {
		auto *item = m_tracks->topLevelItem(i);
		if (item->data(0, Qt::UserRole).toString() == track)
			root = item;
		for (int j = 0; j < item->childCount(); ++j) {
			auto *child = item->child(j);
			const auto id = child->data(0, Qt::UserRole + 1).toString();
			if (requested.contains(id)) {
				child->setSelected(true);
				m_selection.append(id);
				if (!current || id == focus)
					current = child;
			}
		}
	}
	if (!current)
		current = root;
	if (current) {
		m_track = current->data(0, Qt::UserRole).toString();
		m_region = current->data(0, Qt::UserRole + 1).toString();
		m_tracks->setCurrentItem(current, 0, QItemSelectionModel::NoUpdate);
		if (m_selection.isEmpty())
			current->setSelected(true);
	}
	refreshClipSelection();
}
QStringList AudioSessionDialog::selectedRegions(bool linkedGroups) const
{
	if (m_selection.isEmpty())
		return {};
	return audioArrangementSelection(session(), m_selection, linkedGroups, nullptr);
}
void AudioSessionDialog::refreshClipSelection()
{
	if (!m_linkedGroups || !m_selectionStatus)
		return;
	const auto targets = selectedRegions(m_linkedGroups->isChecked());
	m_timeline->setSelected(m_track, m_region);
	m_timeline->setSelection(targets);
	m_selectionStatus->setText(tr("Selected clips: %1 · Edit targets: %2").arg(m_selection.size()).arg(targets.size()));
	m_selectionStatus->setAccessibleDescription(m_selectionStatus->text());
}
void AudioSessionDialog::editSelection()
{
	if (isBusy())
		return;
	if (m_selection.isEmpty()) {
		status(tr("Select one or more clips first."));
		return;
	}
	AudioArrangementDialog dialog(session(), m_selection, m_linkedGroups->isChecked(), qint64(m_cursor->value()), this);
	if (dialog.exec() == QDialog::Accepted)
		applyArrangement(dialog.edit(), tr("Edit selected clips"));
}

void AudioSessionDialog::editRange()
{
	if (isBusy())
		return;
	QStringList tracks;
	for (const auto &track : session().tracks)
		for (const auto &region : track.regions)
			if (m_selection.contains(region.id) && !tracks.contains(track.id))
				tracks.append(track.id);
	if (tracks.isEmpty() && !m_track.isEmpty())
		tracks.append(m_track);
	AudioRangeDialog dialog(session(), tracks, qint64(m_first->value()), qint64(m_end->value()), this);
	if (dialog.exec() == QDialog::Accepted)
		applyRange(dialog.edit(), tr("Edit time range"));
}

void AudioSessionDialog::editMedia()
{
	if (isBusy())
		return;
	stopPlayback();
	QString source;
	for (const auto &track : session().tracks)
		for (const auto &region : track.regions)
			if (region.id == m_region)
				source = region.sourceId;
	const auto revision = m_state.revision;
	AudioMediaDialog dialog(session(), source, this);
	if (dialog.exec() == QDialog::Accepted) {
		if (revision != m_state.revision) {
			status(tr("The session changed during media review. Review the change again."));
			return;
		}
		applyMedia(dialog.edit(), dialog.candidate());
	}
}

QStringList AudioSessionDialog::protectedMediaPaths() const
{
	QSet<QString> paths{m_recoveryInputPath, m_recoverySourcePath};
	const auto collect = [&](const State &state) {
		for (const auto &source : state.session.sources) {
			paths.insert(source.audio.sourcePath);
			const QFileInfo media(source.audio.sourcePath);
			if (media.suffix().compare("vstake", Qt::CaseInsensitive) == 0 &&
			    media.dir().dirName().endsWith(".vsrecord", Qt::CaseInsensitive))
				for (const auto &path : audioRecordingProtectedPaths(media.absolutePath())) paths.insert(path);
		}
	};
	collect(m_state);
	for (const auto &state : m_undo)
		collect(state);
	for (const auto &state : m_redo)
		collect(state);
	paths.remove(QString());
	return paths.values();
}

void AudioSessionDialog::refreshSnap()
{
	const int choice = m_snap->currentData().toInt();
	if (choice >= 0)
		m_timeline->setSnapFrames(choice == 1 ? 1 : std::max(1, session().sampleRate / 1000));
	else
		m_timeline->setMusicalSnap(choice == -1   ? AudioMusicalGrid::QuarterBeat
		                           : choice == -2 ? AudioMusicalGrid::Beat
		                           : choice == -3 ? AudioMusicalGrid::Bar
		                           : choice == -4 ? AudioMusicalGrid::HalfBeat
		                                          : AudioMusicalGrid::BeatTriplet);
}
void AudioSessionDialog::refreshMusicalPosition()
{
	const auto frame = qint64(m_cursor->value());
	if (!m_musicalPosition->hasFocus() || !m_musicalPosition->isModified()) {
		m_musicalPosition->setText(audioMusicalPositionText(m_musicalTime.positionAtFrame(frame)));
		m_musicalPosition->setModified(false);
	}
	const auto meter = m_musicalTime.meterAtFrame(frame);
	const auto description =
	    tr("%1 BPM · %2/%3. Bars and beats start at 1; ticks start at 0. There are 960 ticks per quarter note.")
	        .arg(m_musicalTime.tempoAtFrame(frame))
	        .arg(meter.beatsPerBar)
	        .arg(meter.beatUnit);
	m_musicalPosition->setToolTip(description);
	m_musicalPosition->setAccessibleDescription(description);
}
void AudioSessionDialog::goToMusicalPosition()
{
	if (isBusy())
		return;
	AudioMusicalPosition position;
	const auto frame =
	    parseAudioMusicalPosition(m_musicalPosition->text(), &position) ? m_musicalTime.frameAtPosition(position) : -1;
	if (frame < 0) {
		status(tr("Enter a valid bar.beat.tick position for this meter within the session timeline."));
		m_musicalPosition->setFocus();
		return;
	}
	m_musicalPosition->setModified(false);
	m_cursor->setValue(double(frame));
	refreshMusicalPosition();
	if (frame < m_timeline->visibleFirst() || frame >= m_timeline->visibleEnd()) {
		const auto span = m_timeline->visibleEnd() - m_timeline->visibleFirst();
		const auto first = std::min(frame, AudioSessionFrameLimit - span);
		m_timeline->setVisibleRange(first, first + span);
	}
}
void AudioSessionDialog::refreshTransport()
{
	using State = AudioSessionPlaybackSnapshot::State;
	const auto &snapshot = m_playback->snapshot();
	if (m_meters)
		m_meters->setPlayback(snapshot);
	const auto state = snapshot.state;
	m_play->setText(state == State::Playing ? tr("Pause") : state == State::Paused ? tr("Resume") : tr("Play Range"));
	QString text = state == State::Error       ? tr("Playback failed: %1").arg(snapshot.error)
	               : state == State::Preparing ? tr("Preparing session playback…")
	               : state == State::Playing   ? tr("Playing session")
	               : state == State::Paused    ? tr("Paused")
	               : state == State::Ended     ? tr("Range finished")
	               : m_playback->available()   ? tr("Stopped")
	                                           : tr("Playback unavailable in this build");
	if (state == State::Playing || state == State::Paused || state == State::Ended) {
		text += tr(" · Frame: %1 · Output buffer: %2 frames · Dropouts: %3")
		            .arg(snapshot.position)
		            .arg(snapshot.bufferFrames)
		            .arg(snapshot.underruns);
		const auto db = [](float peak) {
			return peak > 0 ? QString::number(20 * std::log10(peak), 'f', 1) : QStringLiteral("−∞");
		};
		text += tr(" · Master peak L/R: %1 / %2 dBFS · Over-range samples: %3")
		            .arg(db(snapshot.peak[0]), db(snapshot.peak[1]))
		            .arg(snapshot.samplesAboveFullScale);
	}
	m_transport->setText(text);
	m_transport->setAccessibleDescription(text);
	m_transport->setToolTip(tr("Position estimates device-processed frames. Peaks are measured before audition volume; "
	                           "audition clips at full scale. Exports retain their selected precision."));
	m_play->setEnabled(!isBusy() && state != State::Preparing && m_playback->available() &&
	                   audioSessionFrames(session()) > 0);
	m_stop->setEnabled(m_playback->available());
	if (m_progress && !isBusy()) {
		m_progress->setVisible(state == State::Preparing);
		if (state == State::Preparing) {
			m_progress->setRange(0, StudioSettings().accessibilityPreferences().reducedMotion ? 1 : 0);
			m_progress->setTextVisible(false);
		}
		m_cancel->setVisible(state == State::Preparing);
	}
}

void AudioSessionDialog::refresh()
{
	setWindowModified(hasChanges());
	const bool busy = isBusy();
	for (auto *item : m_actions) {
		item->setEnabled(!busy);
	}
	m_play->setEnabled(!busy && m_playback->available() && audioSessionFrames(session()) > 0);
	m_stop->setEnabled(!busy && m_playback->available());
	m_undoAction->setEnabled(!busy && !m_undo.isEmpty());
	m_redoAction->setEnabled(!busy && !m_redo.isEmpty());
	m_undoAction->setText(m_undo.isEmpty() ? tr("Undo") : tr("Undo %1").arg(m_state.description));
	m_redoAction->setText(m_redo.isEmpty() ? tr("Redo") : tr("Redo %1").arg(m_redo.last().description));
	m_tracks->setEnabled(!busy);
	m_linkedGroups->setEnabled(!busy);
	m_timeline->setEnabled(!busy);
	m_first->setEnabled(!busy);
	m_end->setEnabled(!busy);
	m_cursor->setEnabled(!busy);
	m_snap->setEnabled(!busy);
	m_ruler->setEnabled(!busy);
	m_musicalPosition->parentWidget()->setEnabled(!busy);
	m_progress->setVisible(bool(m_work));
	m_cancel->setVisible(bool(m_work));
	m_summary->setText(
	    tr("%1 · Tracks: %2 · %3 Hz · Initial tempo: %4 BPM · Frames: %5")
	        .arg(m_identity.path.isEmpty() ? tr("Unsaved session") : QFileInfo(m_identity.path).fileName())
	        .arg(session().tracks.size())
	        .arg(session().sampleRate)
	        .arg(session().musicalTime.tempo)
	        .arg(audioSessionFrames(session())));
	m_summary->setAccessibleDescription(m_summary->text());
	const QSignalBlocker guard(m_tracks);
	m_tracks->clear();
	for (const auto &track : session().tracks) {
		auto *item =
		    new QTreeWidgetItem(m_tracks, {track.routing.bus ? tr("Bus: %1").arg(track.name) : track.name, {}, {}});
		item->setData(0, Qt::UserRole, track.id);
		item->setToolTip(0, tr("Gain %1 dB; pan %2; mute %3; solo %4")
		                        .arg(track.gainDb)
		                        .arg(track.pan)
		                        .arg(track.muted ? tr("on") : tr("off"), track.solo ? tr("on") : tr("off")));
		for (const auto &region : track.regions) {
			auto *child = new QTreeWidgetItem(
			    item, {region.name, QString::number(region.position), QString::number(region.length)});
			for (const auto &group : session().groups)
				if (group.id == region.groupId)
					child->setText(3, group.name);
			child->setData(0, Qt::UserRole, track.id);
			child->setData(0, Qt::UserRole + 1, region.id);
			child->setToolTip(0, tr("Source offset %1; fade in %2; fade out %3 frames")
			                         .arg(region.sourceOffset)
			                         .arg(region.fadeIn)
			                         .arg(region.fadeOut));
		}
		item->setExpanded(true);
	}
	m_timeline->setSession(session(), m_waveforms);
	m_musicalTime.prepare(session().musicalTime, session().sampleRate);
	refreshMusicalPosition();
	selectRegions(m_selection, m_region);
	refreshSnap();
	refreshMeters();
	refreshTransport();
}

void AudioSessionDialog::refreshMeters()
{
	if (m_meters) {
		m_meters->setContext(session(), m_state.revision, qint64(m_first->value()), qint64(m_end->value()));
		m_meters->setBusy(isBusy(), m_work && m_work->meterJob);
	}
}
void AudioSessionDialog::showMeters()
{
	if (!m_meters) {
		m_meters = new AudioMeterDialog(this);
		connect(m_meters, &AudioMeterDialog::analyzeRequested, this, &AudioSessionDialog::analyzeMeters);
		connect(m_meters, &AudioMeterDialog::resetRequested, m_playback, &AudioSessionPlayback::resetMeters);
		connect(m_meters, &AudioMeterDialog::cancelRequested, this, &AudioSessionDialog::cancelWork);
	}
	refreshMeters();
	m_meters->setPlayback(m_playback->snapshot());
	m_meters->show();
	m_meters->raise();
}
void AudioSessionDialog::analyzeMeters()
{
	if (isBusy())
		return;
	showMeters();
	const auto snapshot = session();
	const auto first = qint64(m_first->value()), end = qint64(m_end->value());
	work(
	    tr("Analyzing session meters…"),
	    [snapshot, first, end](AudioSessionWork &state) {
		    state.meters = measureAudioSessionMeters(
		        snapshot, first, end, 4096, state.control(),
		        [&state](qint64 done, qint64 total) { state.percent = int(done * 100 / total); });
		    state.error = state.meters.error;
	    },
	    [this](const AudioSessionWork &state) {
		    auto report = state.meters;
		    report.cancelled |= state.cancelled.load();
		    if (!state.error.isEmpty())
			    report.error = state.error;
		    if (m_meters)
			    m_meters->setReport(report);
		    status(report.cancelled     ? tr("Meter analysis cancelled.")
		           : report.succeeded() ? tr("Meter analysis complete.")
		                                : tr("Meter analysis failed: %1").arg(report.error));
	    },
	    false, true);
}

void AudioSessionDialog::work(const QString &title, std::function<void(AudioSessionWork &)> perform,
                              std::function<void(const AudioSessionWork &)> complete, bool audition, bool meterJob)
{
	if (isBusy()) {
		return;
	}
	stopPlayback();
	auto state = std::make_shared<AudioSessionWork>();
	state->audition = audition;
	state->meterJob = meterJob;
	m_work = state;
	m_progress->setRange(0, StudioSettings().accessibilityPreferences().reducedMotion ? 1 : 0);
	m_progress->setTextVisible(false);
	m_progress->setValue(0);
	status(title);
	refresh();
	auto *thread = QThread::create([state, perform] {
		try {
			perform(*state);
		} catch (const std::bad_alloc &) {
			state->session = {};
			state->waveforms.clear();
			state->wav.clear();
			state->error = tr("Insufficient memory. Existing session edits remain available.");
		} catch (const std::exception &exception) {
			state->error = tr("Session operation failed: %1").arg(QString::fromUtf8(exception.what()).left(512));
		} catch (...) {
			state->error = tr("Session operation failed unexpectedly. Existing edits remain available.");
		}
	});
	m_thread = thread;
	thread->setParent(this);
	connect(thread, &QThread::finished, this, [this, state, complete] {
		if (m_work != state) {
			return;
		}
		m_thread = nullptr;
		m_work.reset();
		const bool committed = state->saved.written || state->mixed.saved.written;
		if (state->stemJob || state->meterJob) {
			complete(*state);
		} else if (state->cancelled && !committed) {
			m_afterSave = {};
			status(tr("Operation cancelled. The session is unchanged."));
		} else if (!state->error.isEmpty()) {
			m_afterSave = {};
			status(state->error);
		} else {
			complete(*state);
		}
		refresh();
	});
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	connect(qApp, &QCoreApplication::aboutToQuit, thread, [thread, state] {
		state->cancelled = true;
		thread->wait();
	});
	thread->start();
}

void AudioSessionDialog::trimHistory()
{
	const auto bytes = [](const State &state) {
		// Count shared allocations for every state, deliberately overestimating.
		// Each source also reserves the native metadata ceiling plus marker/name storage.
		qint64 total = qint64(sizeof(State)) + state.description.size() * 2 + state.session.name.size() * 2;
		total += state.session.musicalTime.tempoChanges.size() * sizeof(AudioTempoChange) +
		         state.session.musicalTime.meterChanges.size() * sizeof(AudioMeterChange);
		for (const auto &group : state.session.groups)
			total += sizeof(AudioSessionGroup) + (group.id.size() + group.name.size()) * 2;
		const auto countEffects = [&](const AudioEffectChain &effects) {
			for (const auto &effect : effects) {
				total += sizeof(AudioEffect) + (effect.id.size() + effect.type.size()) * 2;
				for (auto it = effect.parameters.cbegin(); it != effect.parameters.cend(); ++it)
					total += 80 + it.key().size() * 2;
			}
		};
		const auto countLanes = [&](const AudioEffectAutomation &lanes) {
			for (const auto &lane : lanes)
				total += sizeof(AudioEffectAutomationLane) + (lane.effectId.size() + lane.parameter.size()) * 2 +
				         lane.points.size() * sizeof(AudioAutomationPoint);
		};
		countLanes(state.session.masterEffectAutomation);
		countEffects(state.session.masterEffects);
		for (const auto &source : state.session.sources) {
			total += sizeof(AudioSessionSource) + source.audio.clip.samples.size() * 4 + 256 * 1024;
			total += (source.id.size() + source.audio.sourceName.size() + source.audio.sourcePath.size()) * 2;
			for (const auto &cue : source.audio.clip.markers.cues) {
				total += sizeof(AudioCue) + cue.name.size() * 2;
			}
		}
		for (const auto &track : state.session.tracks) {
			countEffects(track.effects);
			countLanes(track.effectAutomation);
			total += sizeof(AudioSessionTrack) + (track.id.size() + track.name.size()) * 2;
			total += (track.gainAutomation.size() + track.panAutomation.size()) * sizeof(AudioAutomationPoint);
			total += track.routing.outputId.size() * 2;
			for (const auto &send : track.routing.sends)
				total += sizeof(AudioSend) + send.targetId.size() * 2;
			for (const auto &region : track.regions) {
				total += sizeof(AudioSessionRegion) +
				         (region.id.size() + region.name.size() + region.sourceId.size() + region.groupId.size()) * 2;
			}
		}
		return total;
	};
	const auto retained = [&] {
		qint64 total = 0;
		for (const auto &state : m_undo) {
			total += bytes(state);
		}
		for (const auto &state : m_redo) {
			total += bytes(state);
		}
		return total;
	};
	while (m_undo.size() + m_redo.size() > 64 || retained() > 256LL * 1024 * 1024) {
		if (!m_undo.isEmpty()) {
			m_undo.removeFirst();
		} else if (!m_redo.isEmpty()) {
			m_redo.removeFirst();
		} else {
			break;
		}
	}
	QSet<QString> needed;
	const auto retain = [&needed](const State &state) {
		for (const auto &source : state.session.sources) {
			needed.insert(source.id);
		}
	};
	retain(m_state);
	for (const auto &state : m_undo)
		retain(state);
	for (const auto &state : m_redo)
		retain(state);
	for (auto it = m_waveforms.begin(); it != m_waveforms.end();) {
		if (!needed.contains(it.key()))
			it = m_waveforms.erase(it);
		else
			++it;
	}
}
qint64 AudioSessionDialog::retainedWaveformBytes() const
{
	qint64 bytes = 0;
	for (const auto &waveform : m_waveforms) {
		bytes += waveform.clip().samples.size() * 4 + waveform.cacheBytes();
	}
	return bytes;
}
void AudioSessionDialog::adopt(const AudioSession &next, const QString &description)
{
	const qint64 previousEnd = audioSessionFrames(session());
	m_undo.append(m_state);
	m_redo.clear();
	m_state = {next, ++m_nextRevision, description};
	trimHistory();
	if (qint64(m_end->value()) == previousEnd) {
		m_end->setValue(double(audioSessionFrames(next)));
	}
	status(description);
	refresh();
	scheduleRecovery();
}
bool AudioSessionDialog::applyArrangement(const AudioArrangementEdit &edit, const QString &description)
{
	if (isBusy())
		return false;
	stopPlayback();
	try {
		const auto changed = editAudioArrangement(session(), edit);
		if (!changed.succeeded()) {
			status(changed.error);
			return false;
		}
		if (audioSessionSummary(changed.session) == audioSessionSummary(session())) {
			status(tr("No session changes."));
			return true;
		}
		m_selection =
		    changed.addedRegionIds.isEmpty() && edit.operation != "remove" ? edit.regionIds : changed.selectedRegionIds;
		if (!m_selection.contains(m_region))
			m_region = m_selection.value(0);
		adopt(changed.session, description);
		return true;
	} catch (const std::bad_alloc &) {
		status(tr("Insufficient memory for the edit. Existing session content is unchanged."));
		return false;
	}
}
bool AudioSessionDialog::applyRange(const AudioRangeEdit &edit, const QString &description)
{
	if (isBusy())
		return false;
	stopPlayback();
	try {
		const auto changed = editAudioRange(session(), edit);
		if (!changed.succeeded()) {
			status(changed.error);
			return false;
		}
		if (audioSessionSummary(changed.session) != audioSessionSummary(session())) {
			if (!changed.addedRegionIds.isEmpty()) {
				m_selection = changed.addedRegionIds;
				m_region = m_selection.value(0);
			}
			adopt(changed.session, description);
		} else
			status(tr("No session changes."));
		setSelection(changed.first, changed.end);
		m_cursor->setValue(double(changed.first));
		return true;
	} catch (const std::bad_alloc &) {
		status(tr("Insufficient memory for the edit. Existing session content is unchanged."));
		return false;
	}
}
bool AudioSessionDialog::applyMedia(const AudioMediaEdit &edit, const AudioMediaCandidate &candidate)
{
	if (isBusy())
		return false;
	const auto snapshot = session();
	const auto waveforms = m_waveforms;
	work(
	    tr("Applying reviewed media change…"),
	    [snapshot, waveforms, edit, candidate](AudioSessionWork &state) {
		    const auto result = editAudioMedia(snapshot, edit, candidate, true, state.control());
		    state.error = result.error;
		    if (result.cancelled)
			    state.cancelled = true;
		    if (!result.succeeded())
			    return;
		    state.session = result.session;
		    state.waveforms = waveforms;
		    state.prepareWaveforms();
	    },
	    [this](const AudioSessionWork &state) {
		    if (audioSessionSummary(state.session) == audioSessionSummary(session())) {
			    status(tr("No session changes."));
			    return;
		    }
		    m_waveforms = state.waveforms;
		    adopt(state.session, tr("Session media change"));
	    });
	return true;
}

bool AudioSessionDialog::applyEdit(const AudioSessionEdit &edit, const QString &description)
{
	if (isBusy()) {
		return false;
	}
	stopPlayback();
	try {
		const auto changed = editAudioSession(session(), edit);
		if (!changed.succeeded()) {
			status(changed.error);
			return false;
		}
		if (audioSessionSummary(changed.session) == audioSessionSummary(session())) {
			status(tr("No session changes."));
			return true;
		}
		if (!changed.addedTrackId.isEmpty()) {
			m_track = changed.addedTrackId;
			m_region.clear();
			m_selection.clear();
		}
		if (!changed.addedRegionId.isEmpty()) {
			m_track = changed.addedTrackId.isEmpty() ? edit.trackId : changed.addedTrackId;
			m_region = changed.addedRegionId;
			m_selection = {m_region};
		}
		adopt(changed.session, description);
		return true;
	} catch (const std::bad_alloc &) {
		status(tr("Insufficient memory for the edit. Existing session content is unchanged."));
		return false;
	}
}
void AudioSessionDialog::undo()
{
	if (isBusy() || m_undo.isEmpty()) {
		return;
	}
	stopPlayback();
	const auto description = m_state.description;
	m_redo.append(m_state);
	m_state = m_undo.takeLast();
	trimHistory();
	status(tr("Undid %1").arg(description));
	m_end->setValue(double(audioSessionFrames(session())));
	refresh();
	scheduleRecovery();
}
void AudioSessionDialog::redo()
{
	if (isBusy() || m_redo.isEmpty()) {
		return;
	}
	stopPlayback();
	m_undo.append(m_state);
	m_state = m_redo.takeLast();
	trimHistory();
	status(tr("Redid %1").arg(m_state.description));
	m_end->setValue(double(audioSessionFrames(session())));
	refresh();
	scheduleRecovery();
}
bool AudioSessionDialog::guardChanges(std::function<void()> continuation)
{
	if (!hasChanges()) {
		return true;
	}
	const auto answer =
	    QMessageBox::question(this, tr("Unsaved Session"), tr("Save the session before continuing?"),
	                          QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (answer == QMessageBox::Discard) {
		return true;
	}
	if (answer == QMessageBox::Save) {
		m_afterSave = std::move(continuation);
		if (!chooseSave()) {
			m_afterSave = {};
		}
	}
	return false;
}
bool AudioSessionDialog::newSession(int sampleRate, double tempo)
{
	if (isBusy() || !guardChanges([this, sampleRate, tempo] { newSession(sampleRate, tempo); })) {
		return false;
	}
	AudioSession next;
	next.sampleRate = sampleRate;
	next.musicalTime.tempo = tempo;
	const auto error = validateAudioSessionStructure(next);
	if (!error.isEmpty()) {
		status(error);
		return false;
	}
	stopPlayback();
	retireRecovery();
	m_state = {next, ++m_nextRevision, {}};
	m_savedRevision = 0;
	m_identity = {};
	m_recoveryInputPath.clear();
	m_recoverySourcePath.clear();
	m_undo.clear();
	m_redo.clear();
	m_waveforms.clear();
	m_track.clear();
	m_region.clear();
	m_selection.clear();
	setSelection(0, 0);
	m_cursor->setValue(0);
	m_timeline->setVisibleRange(0, sampleRate * 10LL);
	status(tr("New session ready."));
	refresh();
	scheduleRecovery();
	return true;
}
bool AudioSessionDialog::openSession(const QString &path)
{
	if (isBusy() || !guardChanges([this, path] { openSession(path); })) {
		return false;
	}
	work(
	    tr("Opening session and building waveform caches…"),
	    [path](AudioSessionWork &state) {
		    if (readAudioSession(path, &state.session, &state.identity, &state.error, state.control())) {
			    state.prepareWaveforms();
		    }
	    },
	    [this](const AudioSessionWork &state) {
		    retireRecovery();
		    m_state = {state.session, ++m_nextRevision, {}};
		    m_savedRevision = m_state.revision;
		    m_identity = state.identity;
		    m_recoveryInputPath.clear();
		    m_recoverySourcePath.clear();
		    m_waveforms = state.waveforms;
		    m_undo.clear();
		    m_redo.clear();
		    m_track.clear();
		    m_region.clear();
		    m_selection.clear();
		    setSelection(0, audioSessionFrames(session()));
		    m_timeline->setVisibleRange(0, std::max<qint64>(session().sampleRate, audioSessionFrames(session())));
		    status(tr("Session opened with original media and non-destructive edits."));
	    });
	return true;
}
bool AudioSessionDialog::chooseSave(bool separate)
{
	QString path = m_identity.path;
	if (separate || path.isEmpty()) {
		path = QFileDialog::getSaveFileName(this, tr("Save Session"), path, tr("Audio sessions (*.vssession)"));
	}
	return !path.isEmpty() && saveSession(path, true);
}

QString AudioSessionDialog::recoveryPath() const { return audioSessionRecoveryPath(m_recoveryDirectory, m_recoveryId); }
bool AudioSessionDialog::recoveryBusy() const
{
	return m_recoveryTimer->isActive() || (m_recovery && m_recovery->busy());
}
void AudioSessionDialog::setRecoveryEnabled(bool enabled) { m_recoveryEnabled->setChecked(enabled); }
void AudioSessionDialog::scheduleRecovery()
{
	if (!hasChanges()) {
		retireRecovery();
		return;
	}
	if (!m_recoveryEnabled->isChecked()) {
		return;
	}
	// Do not restart an active timer: continuous edits must still be checkpointed.
	if (!m_recoveryTimer->isActive()) {
		m_recoveryTimer->start();
	}
	m_recoveryStatus->setText(tr("Local recovery update queued…"));
	m_recoveryStatus->setAccessibleDescription(m_recoveryStatus->text());
}
void AudioSessionDialog::checkpointRecovery()
{
	m_recoveryTimer->stop();
	if (!hasChanges()) {
		retireRecovery();
		return;
	}
	if (!m_recovery || !m_recoveryEnabled->isChecked()) {
		return;
	}
	m_recoveryActive = true;
	m_recoveryStatus->setText(tr("Updating local session recovery…"));
	m_recoveryStatus->setAccessibleDescription(m_recoveryStatus->text());
	m_recovery->checkpoint(
	    m_recoveryId, m_state.revision,
	    AudioSessionRecovery{session(), m_identity.path.isEmpty() ? m_recoverySourcePath : m_identity.path, {}});
}
void AudioSessionDialog::retireRecovery()
{
	m_recoveryTimer->stop();
	m_recoveryStatus->clear();
	m_recoveryStatus->setAccessibleDescription({});
	m_recoveryStatus->setToolTip({});
	if (m_recoveryActive) {
		m_recovery->retire(m_recoveryId);
		m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		m_recoveryActive = false;
	}
}
bool AudioSessionDialog::restoreRecovery(const QString &path, const QByteArray &expectedSha256)
{
	if (isBusy()) {
		return false;
	}
	if (audioPathsReferToSameFile(path, recoveryPath())) {
		if (recoveryBusy() || !m_recovery->retain(m_recoveryId)) {
			status(tr("Wait for this session's recovery update to finish, then refresh and restore the copy."));
			return false;
		}
		// Preserve the reviewed bytes before the unsaved-change guard can save
		// and retire the current draft. Cancel still leaves the document intact.
		m_recoveryActive = false;
		m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		scheduleRecovery();
	}
	if (!guardChanges([this, path, expectedSha256] { restoreRecovery(path, expectedSha256); })) {
		return false;
	}
	work(
	    tr("Verifying session recovery and preparing media…"),
	    [path, expectedSha256](AudioSessionWork &state) {
		    AudioSessionRecovery recovery;
		    if (readAudioSessionRecovery(path, expectedSha256, &recovery, &state.error, state.control())) {
			    state.session = std::move(recovery.session);
			    state.recoverySourcePath = recovery.sourcePath;
			    state.prepareWaveforms();
		    }
	    },
	    [this, path](const AudioSessionWork &state) {
		    retireRecovery();
		    m_state = {state.session, ++m_nextRevision, {}};
		    m_savedRevision = 0;
		    m_identity = {};
		    m_recoveryInputPath = QFileInfo(path).absoluteFilePath();
		    m_recoverySourcePath = state.recoverySourcePath;
		    m_waveforms = state.waveforms;
		    m_undo.clear();
		    m_redo.clear();
		    m_track.clear();
		    m_region.clear();
		    m_selection.clear();
		    setSelection(0, audioSessionFrames(session()));
		    m_cursor->setValue(0);
		    m_timeline->setVisibleRange(0, std::max<qint64>(session().sampleRate, audioSessionFrames(session())));
		    scheduleRecovery();
		    status(tr("Session recovered as an unsaved draft. Save to a separate destination; the original session and "
		              "recovery copy are preserved."));
	    });
	return true;
}
bool AudioSessionDialog::saveSession(const QString &path, bool overwrite)
{
	if (isBusy()) {
		return false;
	}
	AudioProjectSaveRequest request;
	request.path = path;
	request.overwrite = overwrite;
	if (!m_identity.path.isEmpty() && audioPathsReferToSameFile(path, m_identity.path)) {
		request.expected = m_identity;
	}
	const auto snapshot = session();
	const auto revision = m_state.revision;
	const auto protectedPaths = protectedMediaPaths();
	work(
	    tr("Saving lossless session…"),
	    [snapshot, request, protectedPaths](AudioSessionWork &state) {
		    state.saved = writeAudioSession(snapshot, request, state.control(), protectedPaths);
		    state.error = state.saved.error;
	    },
	    [this, revision](const AudioSessionWork &state) {
		    m_identity = state.saved.identity;
		    m_savedRevision = revision;
		    retireRecovery();
		    status(tr("Session saved."));
		    auto continuation = std::move(m_afterSave);
		    m_afterSave = {};
		    if (continuation) {
			    continuation();
		    }
	    });
	return true;
}
void AudioSessionDialog::chooseTake() { openTake(); }
bool AudioSessionDialog::openRecording(const QString &directory, AudioDuplexDeviceFactory device,
                                       AudioCaptureStorageFactory storage, AudioRecordingGate permission,
                                       AudioStreamDeviceFactory auditionDevice)
{
	if (isBusy()) {
		status(tr("Finish the current session or recording operation first."));
		return false;
	}
	const auto revision = m_state.revision;
	const AudioRecordingGate prepare = [guard = QPointer<AudioSessionDialog>(this)](QObject *context, auto complete) {
		if (!guard)
			return;
		const auto stopSession = [guard, context = QPointer<QObject>(context), complete](QString error) {
			if (!guard || !context)
				return;
			if (!error.isEmpty()) {
				complete(error);
				return;
			}
			guard->m_playback->stopAndWait(context, [complete](bool stopped) {
				complete(stopped ? QString{}
				                 : tr("Playback changed while preparing recording. Stop playback and try again."));
			});
		};
		if (guard->beforeRecording)
			guard->beforeRecording(context, stopSession);
		else {
			if (guard->beforePlayback)
				guard->beforePlayback();
			stopSession({});
		}
	};
	auto *dialog = new AudioRecordingDialog(session(), sessionPath(), qint64(m_first->value()), qint64(m_end->value()),
	                                        this, std::move(device), std::move(storage), std::move(permission), prepare,
	                                        auditionDevice ? std::move(auditionDevice) : m_playbackFactory);
	m_recording = dialog;
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowModality(Qt::ApplicationModal);
	connect(dialog, &QDialog::finished, this, [this, dialog, revision](int result) {
		m_recording = nullptr;
		if (result == QDialog::Accepted)
			adoptRecording(dialog->imported(), revision);
		refresh();
		scheduleRecovery();
	});
	if (!directory.isEmpty())
		dialog->inspectRecording(directory);
	dialog->show();
	refresh();
	return true;
}
void AudioSessionDialog::adoptRecording(const AudioRecordingImportResult &imported, quint64 revision)
{
	if (isBusy() || m_state.revision != revision || !imported.succeeded()) {
		status(tr("The session changed after recording review. Reopen the retained takes and review their destination "
		          "tracks."));
		return;
	}
	const auto waveforms = m_waveforms;
	work(tr("Preparing recorded track waveforms…"), [imported, waveforms](AudioSessionWork &state) {
		state.session = imported.session;
		state.waveforms = waveforms;
		state.prepareWaveforms();
	}, [this, imported, revision](const AudioSessionWork &state) {
		if (m_state.revision != revision) {
			status(tr("The session changed after recording review. Reopen the retained takes and review their destination tracks."));
			return;
		}
		m_waveforms = state.waveforms;
		m_track = imported.trackIds.value(0);
		m_region = imported.regionIds.value(0);
		m_selection = imported.regionIds;
		adopt(state.session, tr("Import recorded tracks"));
	});
}
bool AudioSessionDialog::openTake(const QString &path)
{
	if (isBusy()) {
		status(tr("Finish the current session or take operation before opening another take."));
		return false;
	}
	for (const auto &track : session().tracks) {
		if (track.id == m_track && track.routing.bus) {
			status(
			    tr("Select or add an audio track before importing or recording a take. Buses receive routed audio."));
			return false;
		}
	}
	auto *dialog = new AudioTakeDialog(session().sampleRate, m_track, sessionPath(), qint64(m_cursor->value()), this);
	m_take = dialog;
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->beforeRecord = [this] {
		stopPlayback();
		if (beforePlayback)
			beforePlayback();
	};
	const auto track = m_track;
	connect(dialog, &QDialog::finished, this, [this, dialog, track](int result) {
		m_take = nullptr;
		if (result == QDialog::Accepted)
			importSource(dialog->importedAudio(), track, dialog->importPosition());
		refresh();
		scheduleRecovery();
	});
	if (!path.isEmpty())
		dialog->inspectTake(path);
	dialog->open();
	refresh();
	return true;
}
bool AudioSessionDialog::importSource(const AudioProject &source, const QString &track, qint64 position, bool resample)
{
	if (isBusy()) {
		return false;
	}
	const auto snapshot = session();
	const auto waveforms = m_waveforms;
	work(
	    tr("Importing media and preparing waveform…"),
	    [snapshot, waveforms, source, track, position, resample](AudioSessionWork &state) {
		    const auto imported =
		        importAudioSessionSource(snapshot, source, track, position, resample, state.control());
		    state.error = imported.error;
		    if (imported.cancelled) {
			    state.cancelled = true;
		    }
		    if (!imported.succeeded()) {
			    return;
		    }
		    state.session = imported.session;
		    state.addedTrack = imported.addedTrackId.isEmpty() ? track : imported.addedTrackId;
		    state.addedRegion = imported.addedRegionId;
		    state.waveforms = waveforms;
		    state.prepareWaveforms();
	    },
	    [this](const AudioSessionWork &state) {
		    m_waveforms = state.waveforms;
		    m_track = state.addedTrack;
		    m_region = state.addedRegion;
		    m_selection = {m_region};
		    adopt(state.session, tr("Import audio"));
	    });
	return true;
}
bool AudioSessionDialog::importFile(const QString &path, const QString &track, qint64 position, bool resample)
{
	if (isBusy()) {
		return false;
	}
	const auto snapshot = session();
	const auto waveforms = m_waveforms;
	work(
	    tr("Reading, decoding and importing audio…"),
	    [snapshot, waveforms, path, track, position, resample](AudioSessionWork &state) {
		    QFile file(path);
		    if (!file.open(QIODevice::ReadOnly) || file.size() > AudioInputByteLimit) {
			    state.error = tr("Audio input is unreadable or exceeds 128 MiB.");
			    return;
		    }
		    const auto bytes = file.read(AudioInputByteLimit + 1);
		    if (file.error() != QFileDevice::NoError) {
			    state.error = file.errorString();
			    return;
		    }
		    AudioProject source;
		    if (bytes.startsWith(QByteArrayLiteral("VSAUD\r\n\x1a"))) {
			    if (!decodeAudioProject(bytes, &source, &state.error, state.control())) {
				    return;
			    }
		    } else {
			    const auto decoded = decodeAudioClip(path, bytes, state.control());
			    if (!decoded.succeeded()) {
				    state.error = decoded.error;
				    state.cancelled = decoded.cancelled;
				    return;
			    }
			    source.clip = decoded.clip;
			    source.sourceName = QFileInfo(path).fileName();
			    source.sourcePath = QFileInfo(path).absoluteFilePath();
		    }
		    const auto imported =
		        importAudioSessionSource(snapshot, source, track, position, resample, state.control());
		    state.error = imported.error;
		    if (imported.cancelled) {
			    state.cancelled = true;
		    }
		    if (!imported.succeeded()) {
			    return;
		    }
		    state.session = imported.session;
		    state.addedTrack = imported.addedTrackId.isEmpty() ? track : imported.addedTrackId;
		    state.addedRegion = imported.addedRegionId;
		    state.waveforms = waveforms;
		    state.prepareWaveforms();
	    },
	    [this](const AudioSessionWork &state) {
		    m_waveforms = state.waveforms;
		    m_track = state.addedTrack;
		    m_region = state.addedRegion;
		    m_selection = {m_region};
		    adopt(state.session, tr("Import audio"));
	    });
	return true;
}
void AudioSessionDialog::chooseImport()
{
	const auto path = QFileDialog::getOpenFileName(
	    this, tr("Import Session Audio"), {},
	    tr("Audio documents and sounds (*.vsaudio *.wav *.dmx *.lmp *.mp3 *.flac *.ogg);;All files (*)"));
	if (path.isEmpty()) {
		return;
	}
	QDialog dialog(this);
	auto *form = inspector(dialog, tr("Import Audio"));
	auto *destination = new QComboBox;
	destination->setAccessibleName(tr("Destination track"));
	destination->addItem(tr("New track"), QString());
	for (const auto &track : session().tracks) {
		if (!track.routing.bus)
			destination->addItem(track.name, track.id);
	}
	auto *position = numberBox(tr("Timeline start frame"), 0, double(AudioSessionFrameLimit), 0);
	position->setValue(m_cursor->value());
	auto *resample = new QCheckBox(tr("Resample to session rate if needed"));
	resample->setAccessibleName(tr("Explicitly resample imported audio"));
	form->addRow(tr("Track"), destination);
	form->addRow(tr("Start frame"), position);
	form->addRow(resample);
	if (dialog.exec() == QDialog::Accepted) {
		importFile(path, destination->currentData().toString(), qint64(position->value()), resample->isChecked());
	}
}
AudioSessionEdit AudioSessionDialog::selectedEdit(bool region) const
{
	AudioSessionEdit edit;
	edit.operation = region ? "region" : "track";
	edit.trackId = m_track;
	edit.regionId = m_region;
	for (const auto &track : session().tracks) {
		if (track.id != m_track) {
			continue;
		}
		edit.name = track.name;
		edit.gainDb = track.gainDb;
		edit.pan = track.pan;
		edit.muted = track.muted;
		edit.solo = track.solo;
		edit.gainAutomation = track.gainAutomation;
		edit.panAutomation = track.panAutomation;
		if (region) {
			for (const auto &clip : track.regions) {
				if (clip.id != m_region) {
					continue;
				}
				edit.name = clip.name;
				edit.gainDb = clip.gainDb;
				edit.muted = clip.muted;
				edit.position = clip.position;
				edit.sourceOffset = clip.sourceOffset;
				edit.length = clip.length;
				edit.fadeIn = clip.fadeIn;
				edit.fadeOut = clip.fadeOut;
			}
		}
	}
	return edit;
}
void AudioSessionDialog::editTrack()
{
	if (isBusy() || m_track.isEmpty()) {
		status(tr("Select a track first."));
		return;
	}
	auto edit = selectedEdit(false);
	QDialog dialog(this);
	auto *form = inspector(dialog, tr("Track Mixer"));
	auto *name = new QLineEdit(edit.name);
	name->setAccessibleName(tr("Track name"));
	name->setMaxLength(256);
	auto *gain = numberBox(tr("Track gain in decibels"), -96, 24, 2);
	gain->setValue(edit.gainDb);
	auto *pan = numberBox(tr("Pan or stereo balance, left minus one to right plus one"), -1, 1, 3);
	pan->setSingleStep(0.1);
	pan->setValue(edit.pan);
	auto *mute = new QCheckBox(tr("Mute"));
	mute->setChecked(edit.muted);
	auto *solo = new QCheckBox(tr("Solo"));
	solo->setChecked(edit.solo);
	form->addRow(tr("Name"), name);
	form->addRow(tr("Gain (dB)"), gain);
	form->addRow(tr("Pan / balance"), pan);
	form->addRow(mute);
	form->addRow(solo);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	edit.name = name->text();
	edit.gainDb = gain->value();
	edit.pan = pan->value();
	edit.muted = mute->isChecked();
	edit.solo = solo->isChecked();
	applyEdit(edit, tr("Track mixer"));
}
void AudioSessionDialog::editRouting()
{
	if (isBusy() || m_track.isEmpty()) {
		status(tr("Select a track or bus first."));
		return;
	}
	AudioRoutingDialog dialog(session(), m_track, this);
	if (dialog.exec() == QDialog::Accepted)
		applyEdit(dialog.edit(), tr("Routing and sends"));
}
void AudioSessionDialog::editEffects(bool master)
{
	if (isBusy())
		return;
	if (!master && m_track.isEmpty()) {
		status(tr("Select a track or bus first."));
		return;
	}
	auto protectedPaths = protectedMediaPaths();
	protectedPaths.append(sessionPath());
	AudioEffectsDialog dialog(session(), master ? QString() : m_track, this, protectedPaths);
	dialog.setAutomationCursor(qint64(m_cursor->value()));
	if (dialog.exec() == QDialog::Accepted)
		applyEdit(dialog.edit(), tr("Effect chain and tail"));
}
void AudioSessionDialog::editRegion()
{
	if (isBusy() || m_region.isEmpty()) {
		status(tr("Select a clip first."));
		return;
	}
	auto edit = selectedEdit(true);
	QDialog dialog(this);
	auto *form = inspector(dialog, tr("Clip Inspector"));
	auto *name = new QLineEdit(edit.name);
	name->setAccessibleName(tr("Clip name"));
	name->setMaxLength(256);
	form->addRow(tr("Name"), name);
	const auto field = [&](const QString &label, qint64 value) {
		auto *box = numberBox(label, 0, double(AudioSessionFrameLimit), 0);
		box->setValue(double(value));
		form->addRow(label, box);
		return box;
	};
	auto *position = field(tr("Timeline start frame"), edit.position);
	auto *offset = field(tr("Source offset frame"), edit.sourceOffset);
	auto *length = field(tr("Length in frames"), edit.length);
	auto *fadeIn = field(tr("Fade-in frames"), edit.fadeIn);
	auto *fadeOut = field(tr("Fade-out frames"), edit.fadeOut);
	auto *gain = numberBox(tr("Clip gain in decibels"), -96, 24, 2);
	gain->setValue(edit.gainDb);
	form->addRow(tr("Gain (dB)"), gain);
	auto *resetFades = new QCheckBox(tr("Reset inherited fade segment"));
	resetFades->setObjectName("sessionResetFades");
	resetFades->setToolTip(
	    tr("Start new fades over this clip's length. Changing either fade value also resets its inherited segment."));
	bool inherited = false;
	for (const auto &track : session().tracks)
		for (const auto &region : track.regions)
			if (region.id == m_region && region.fadeSpan) {
				inherited = true;
				auto *info = new QLabel(tr("Inherited fade segment: frames %1–%2 of %3")
				                            .arg(region.fadeStart)
				                            .arg(region.fadeStart + region.length)
				                            .arg(region.fadeSpan));
				info->setWordWrap(true);
				form->addRow(info);
			}
	form->addRow(resetFades);
	resetFades->setVisible(inherited);
	auto *mute = new QCheckBox(tr("Mute clip"));
	mute->setChecked(edit.muted);
	form->addRow(mute);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	edit.name = name->text();
	edit.position = qint64(position->value());
	edit.sourceOffset = qint64(offset->value());
	edit.length = qint64(length->value());
	edit.fadeIn = qint64(fadeIn->value());
	edit.fadeOut = qint64(fadeOut->value());
	edit.gainDb = gain->value();
	edit.muted = mute->isChecked();
	edit.resetFades = resetFades->isChecked();
	applyEdit(edit, tr("Clip properties"));
}
void AudioSessionDialog::editAutomation()
{
	if (isBusy() || m_track.isEmpty()) {
		status(tr("Select a track first."));
		return;
	}
	auto edit = selectedEdit(false);
	edit.operation = "automation";
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Track Automation"));
	dialog.setObjectName("trackAutomationDialog");
	dialog.resize(680, 740);
	auto *layout = new QVBoxLayout(&dialog);
	auto *tabs = new QTabWidget;
	layout->addWidget(tabs, 1);
	const auto editor = [&](const QString &label, const QVector<AudioAutomationPoint> &points, double low, double high,
	                        double fallback) {
		auto *scroll = new QScrollArea;
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		auto *value = new AudioAutomationEditor(label, low, high, fallback, qint64(m_cursor->value()),
		                                        audioSessionFrames(session()));
		value->setPoints(points);
		scroll->setWidget(value);
		tabs->addTab(scroll, label);
		return value;
	};
	auto *gain = editor(tr("Gain offset (dB)"), edit.gainAutomation, -96, 24, 0);
	auto *pan = editor(tr("Pan / balance"), edit.panAutomation, -1, 1, edit.pan);
	auto *error = new QLabel;
	error->setWordWrap(true);
	error->setAccessibleName(tr("Automation validation"));
	layout->addWidget(error);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		// Moving focus commits any pending numeric input before reading the model.
		buttons->setFocus();
		edit.gainAutomation = gain->points();
		edit.panAutomation = pan->points();
		const auto checked = editAudioSession(session(), edit);
		if (!checked.succeeded()) {
			error->setText(checked.error);
			return;
		}
		dialog.accept();
	});
	if (dialog.exec() == QDialog::Accepted) {
		applyEdit(edit, tr("Track automation"));
	}
}
void AudioSessionDialog::prepareMix(bool audition)
{
	if (isBusy()) {
		return;
	}
	if (audition) {
		using State = AudioSessionPlaybackSnapshot::State;
		const auto state = m_playback->snapshot().state;
		if (state == State::Preparing)
			return;
		if (state == State::Playing) {
			m_playback->pause();
			return;
		}
		if (state == State::Paused) {
			m_playback->resume();
			return;
		}
		if (beforePlayback)
			beforePlayback();
		AudioStreamConfiguration config;
		config.deviceId = m_output->currentData().toByteArray();
		config.sampleRate = session().sampleRate;
		config.bufferFrames = m_buffer->currentData().toInt();
		m_playback->start(session(), {qint64(m_first->value()), qint64(m_end->value()), m_loop->isChecked()}, config);
		return;
	}
	const auto snapshot = session();
	const auto first = qint64(m_first->value()), end = qint64(m_end->value());
	const auto source = m_identity.path.isEmpty() ? m_recoverySourcePath : m_identity.path;
	work(
	    tr("Rendering mix range…"),
	    [snapshot, first, end](AudioSessionWork &state) {
		    const auto result = renderAudioSession(snapshot, first, end, state.control());
		    state.error = result.error;
		    if (result.cancelled) {
			    state.cancelled = true;
		    }
		    if (!result.succeeded()) {
			    return;
		    }
		    AudioWavOptions options;
		    options.format = AudioWavFormat::Float32;
		    state.wav = encodeAudioWav(result.clip, options, &state.error, state.control());
	    },
	    [this, source](const AudioSessionWork &state) {
		    status(tr("Mixdown ready for analysis, editing and game delivery."));
		    Q_EMIT mixReady(state.wav, source);
	    });
}
bool AudioSessionDialog::exportMix(const QString &path, bool overwrite, AudioWavFormat format, bool dither,
                                   quint64 ditherSeed)
{
	if (isBusy()) {
		return false;
	}
	const auto snapshot = session();
	AudioSessionMixdown request;
	request.output.path = path;
	request.output.overwrite = overwrite;
	request.output.protectedPath = m_identity.path;
	request.protectedPaths = protectedMediaPaths();
	request.first = qint64(m_first->value());
	request.end = qint64(m_end->value());
	request.format = format;
	request.dither = dither;
	request.ditherSeed = ditherSeed;
	work(
	    tr("Rendering and exporting session…"),
	    [snapshot, request](AudioSessionWork &state) mutable {
		    request.progress = [&state](qint64 complete, qint64 total) { state.percent = int(complete * 100 / total); };
		    state.mixed = writeAudioSessionMixdown(snapshot, request, state.control());
		    state.error = state.mixed.saved.error;
	    },
	    [this, format](const AudioSessionWork &state) {
		    status(state.mixed.samplesAboveFullScale > 0
		               ? tr("Exported %1 frames. %2 samples exceed full scale; %3.")
		                     .arg(state.mixed.frames)
		                     .arg(state.mixed.samplesAboveFullScale)
		                     .arg(format == AudioWavFormat::Float32 ? tr("float headroom retained")
		                                                            : tr("integer samples clipped"))
		               : tr("Exported %1 frames. Peak %2.").arg(state.mixed.frames).arg(state.mixed.peak, 0, 'f', 4));
	    });
	return true;
}
bool AudioSessionDialog::exportStems(AudioStemExportRequest request)
{
	if (isBusy())
		return false;
	const auto snapshot = session();
	request.protectedPaths << m_identity.path;
	request.protectedPaths.append(protectedMediaPaths());
	work(
	    tr("Checking destinations and exporting stems…"),
	    [snapshot, request](AudioSessionWork &state) mutable {
		    state.stemJob = true;
		    request.progress = [previous = request.progress, &state](int index, int count, qint64 frames,
		                                                             qint64 total) {
			    state.percent = int((index * 100 + frames * 100 / total) / count);
			    if (previous)
				    previous(index, count, frames, total);
		    };
		    state.stems = writeAudioSessionStems(snapshot, request, state.control());
	    },
	    [this](const AudioSessionWork &state) {
		    const auto &report = state.stems;
		    m_deliveryReport = {{"manifestPath", report.plan.manifestPath},
		                        {"delivery", report.manifest},
		                        {"completed", report.completed},
		                        {"error", state.error.isEmpty() ? report.error : state.error}};
		    const auto summary = tr("Stem delivery: %1 of %2 files. Report: %3.")
		                             .arg(report.completed)
		                             .arg(report.plan.files.size())
		                             .arg(report.plan.manifestPath);
		    status(report.succeeded ? summary : summary + ' ' + (state.error.isEmpty() ? report.error : state.error));
	    });
	return true;
}
void AudioSessionDialog::closeEvent(QCloseEvent *event)
{
	if (isBusy()) {
		status(tr("Wait for the current operation, or cancel it before closing."));
		event->ignore();
		return;
	}
	if (!guardChanges([this] { close(); })) {
		event->ignore();
		return;
	}
	stopPlayback();
	retireRecovery();
	event->accept();
}
void AudioSessionDialog::reject() { close(); }
} // namespace vibestudio
