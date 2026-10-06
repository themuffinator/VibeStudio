#include "app/sound_generation_dialog.h"

#include "app/audio_playback.h"
#include "core/studio_settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>

namespace vibestudio {

struct SoundGenerationDialog::Work {
	std::atomic_bool discard = false;
	QThread* thread = nullptr;
	QVector<GeneratedSound> sounds;
};

namespace {

// The waveform as a strip: the outline of each column's lowest and highest
// sample about a centre line.
QImage waveformImage(const AudioClip& clip, QSize size, const QColor& wave, const QColor& axis)
{
	QImage image(size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	const int middle = size.height() / 2;
	painter.setPen(axis);
	painter.drawLine(0, middle, size.width() - 1, middle);
	const qint64 frames = clip.frameCount();
	if (frames <= 0 || clip.channels < 1) {
		return image;
	}
	painter.setPen(wave);
	for (int x = 0; x < size.width(); ++x) {
		const qint64 first = frames * x / size.width();
		const qint64 end = std::max(first + 1, frames * (x + 1) / size.width());
		float low = 0.0f;
		float high = 0.0f;
		for (qint64 frame = first; frame < end && frame < frames; ++frame) {
			const float sample = clip.samples.at(frame * clip.channels);
			low = std::min(low, sample);
			high = std::max(high, sample);
		}
		painter.drawLine(x, middle - int(high * float(middle - 2)), x, middle - int(low * float(middle - 2)));
	}
	return image;
}

} // namespace

SoundGenerationDialog::SoundGenerationDialog(QWidget* parent, SoundGenerationDialogHooks hooks)
	: QDialog(parent)
	, m_hooks(std::move(hooks))
	, m_work(std::make_shared<Work>())
	, m_client(std::make_unique<AiSoundClient>())
{
	setObjectName(QStringLiteral("soundGenerationDialog"));
	setWindowTitle(tr("Generate Sound"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Describe a sound effect, make variants with the synthesizer or your sound model, listen, then save one where the game reads it."));
	resize(1080, 700);

	auto* root = new QVBoxLayout(this);
	auto* splitter = new QSplitter(Qt::Horizontal);
	splitter->setChildrenCollapsible(false);
	root->addWidget(splitter, 1);

	// The brief and the options.
	auto* form = new QWidget;
	auto* formLayout = new QVBoxLayout(form);
	formLayout->setContentsMargins(0, 0, 8, 0);
	auto* promptLabel = new QLabel(tr("&Describe the sound"));
	m_prompt = new QPlainTextEdit;
	m_prompt->setObjectName(QStringLiteral("soundGenerationPrompt"));
	m_prompt->setAccessibleName(tr("Sound description"));
	m_prompt->setPlaceholderText(tr("For example: a heavy metal door slamming shut, or a distant reactor hum"));
	m_prompt->setTabChangesFocus(true);
	m_prompt->setMaximumHeight(90);
	promptLabel->setBuddy(m_prompt);
	formLayout->addWidget(promptLabel);
	formLayout->addWidget(m_prompt);

	auto* options = new QFormLayout;
	options->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_game = new QComboBox;
	m_game->setObjectName(QStringLiteral("soundGenerationGame"));
	m_game->setAccessibleName(tr("Game"));
	for (const SoundGameProfile& profile : soundGameProfiles()) {
		m_game->addItem(profile.displayName, profile.id);
	}
	const QString game = m_hooks.defaultGame ? m_hooks.defaultGame() : QString();
	SoundGameProfile known;
	if (soundGameProfileForId(game, &known)) {
		m_game->setCurrentIndex(m_game->findData(known.id));
	} else {
		m_game->setCurrentIndex(m_game->findData(QStringLiteral("quake")));
	}
	options->addRow(tr("&Game:"), m_game);
	m_kind = new QComboBox;
	m_kind->setObjectName(QStringLiteral("soundGenerationKind"));
	m_kind->setAccessibleName(tr("Kind of sound"));
	m_kind->addItem(tr("From the description"), QString());
	for (const QString& kind : soundGenerationKindIds()) {
		m_kind->addItem(soundGenerationKindDisplayName(kind), kind);
	}
	options->addRow(tr("&Kind:"), m_kind);
	m_duration = new QDoubleSpinBox;
	m_duration->setObjectName(QStringLiteral("soundGenerationDuration"));
	m_duration->setAccessibleName(tr("Length in seconds"));
	m_duration->setRange(0.0, 30.0);
	m_duration->setSingleStep(0.1);
	m_duration->setDecimals(1);
	m_duration->setSuffix(tr(" s"));
	m_duration->setSpecialValueText(tr("The kind's own"));
	options->addRow(tr("&Length:"), m_duration);
	m_loop = new QCheckBox(tr("L&oop seamlessly"));
	m_loop->setObjectName(QStringLiteral("soundGenerationLoop"));
	m_loop->setAccessibleName(tr("Loop seamlessly"));
	m_loop->setToolTip(tr("Join the end to the start so the game can repeat it; Quake-family WAVs are marked to loop."));
	options->addRow(QString(), m_loop);
	m_variants = new QSpinBox;
	m_variants->setObjectName(QStringLiteral("soundGenerationVariantCount"));
	m_variants->setAccessibleName(tr("Number of variants"));
	m_variants->setRange(1, 4);
	m_variants->setValue(2);
	options->addRow(tr("&Variants:"), m_variants);
	m_seed = new QSpinBox;
	m_seed->setObjectName(QStringLiteral("soundGenerationSeed"));
	m_seed->setAccessibleName(tr("Seed"));
	m_seed->setRange(-1, 2147483647);
	m_seed->setValue(-1);
	m_seed->setSpecialValueText(tr("From the description"));
	m_seed->setToolTip(tr("The synthesizer makes the same sound for the same description and seed."));
	options->addRow(tr("S&eed:"), m_seed);
	m_name = new QLineEdit;
	m_name->setObjectName(QStringLiteral("soundGenerationName"));
	m_name->setAccessibleName(tr("Sound name"));
	m_name->setPlaceholderText(tr("From the description"));
	options->addRow(tr("&Name:"), m_name);
	m_folder = new QLineEdit(QStringLiteral("vibestudio"));
	m_folder->setObjectName(QStringLiteral("soundGenerationFolder"));
	m_folder->setAccessibleName(tr("Folder under sound/"));
	m_folderLabel = new QLabel(tr("&Folder:"));
	m_folderLabel->setBuddy(m_folder);
	options->addRow(m_folderLabel, m_folder);
	m_wad = new QLineEdit;
	m_wad->setObjectName(QStringLiteral("soundGenerationWad"));
	m_wad->setAccessibleName(tr("PWAD for the sound lumps"));
	m_wad->setPlaceholderText(QStringLiteral("wads/vibestudio_sounds.wad"));
	m_wadLabel = new QLabel(tr("&WAD:"));
	m_wadLabel->setBuddy(m_wad);
	options->addRow(m_wadLabel, m_wad);
	formLayout->addLayout(options);

	auto* source = new QGroupBox(tr("Who makes it"));
	auto* sourceLayout = new QVBoxLayout(source);
	m_synthSource = new QRadioButton(tr("The &synthesizer, on this machine"));
	m_synthSource->setObjectName(QStringLiteral("soundGenerationSynthSource"));
	m_synthSource->setAccessibleName(tr("Make it with the synthesizer"));
	m_modelSource = new QRadioButton(tr("The sound &model"));
	m_modelSource->setObjectName(QStringLiteral("soundGenerationModelSource"));
	m_modelSource->setAccessibleName(tr("Make it with the sound model"));
	m_synthSource->setChecked(true);
	m_sourceStatus = new QLabel;
	m_sourceStatus->setObjectName(QStringLiteral("soundGenerationSourceStatus"));
	m_sourceStatus->setWordWrap(true);
	m_sourceStatus->setTextFormat(Qt::PlainText);
	m_previewRequest = new QPushButton(tr("Preview Re&quest…"));
	m_previewRequest->setAccessibleName(tr("Preview the sound request"));
	m_previewRequest->setToolTip(tr("Show exactly what would be sent to the sound model, without sending it."));
	sourceLayout->addWidget(m_synthSource);
	sourceLayout->addWidget(m_modelSource);
	sourceLayout->addWidget(m_sourceStatus);
	sourceLayout->addWidget(m_previewRequest, 0, Qt::AlignLeft);
	formLayout->addWidget(source);

	auto* actions = new QHBoxLayout;
	m_generate = new QPushButton(tr("&Generate"));
	m_generate->setObjectName(QStringLiteral("soundGenerationGenerate"));
	m_generate->setAccessibleName(tr("Generate the sounds"));
	m_generate->setToolTip(tr("Make the variants (Ctrl+Enter)."));
	m_generate->setDefault(true);
	m_cancel = new QPushButton(tr("Cancel"));
	m_cancel->setObjectName(QStringLiteral("soundGenerationCancel"));
	m_cancel->setAccessibleName(tr("Cancel generation"));
	m_cancel->setEnabled(false);
	actions->addWidget(m_generate);
	actions->addWidget(m_cancel);
	actions->addStretch(1);
	formLayout->addLayout(actions);
	m_status = new QLabel(tr("Describe the sound and press Generate."));
	m_status->setObjectName(QStringLiteral("soundGenerationStatus"));
	m_status->setAccessibleName(tr("Generation status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Generation progress"));
	m_progress->setRange(0, 0);
	m_progress->setVisible(false);
	formLayout->addWidget(m_status);
	formLayout->addWidget(m_progress);
	formLayout->addStretch(1);
	splitter->addWidget(form);

	// The variants.
	auto* review = new QWidget;
	auto* reviewLayout = new QVBoxLayout(review);
	reviewLayout->setContentsMargins(8, 0, 0, 0);
	m_variantList = new QListWidget;
	m_variantList->setObjectName(QStringLiteral("soundGenerationVariantList"));
	m_variantList->setAccessibleName(tr("Generated sounds"));
	m_variantList->setAccessibleDescription(tr("Each variant's name, length, format, and level, with its waveform."));
	m_variantList->setIconSize(QSize(360, 72));
	m_variantList->setSpacing(4);
	m_variantList->setWordWrap(true);
	reviewLayout->addWidget(m_variantList, 1);
	auto* playRow = new QHBoxLayout;
	m_play = new QPushButton(tr("&Play"));
	m_play->setObjectName(QStringLiteral("soundGenerationPlay"));
	m_play->setAccessibleName(tr("Play the selected sound"));
	m_play->setEnabled(false);
	playRow->addWidget(m_play);
	playRow->addStretch(1);
	reviewLayout->addLayout(playRow);
	m_details = new QPlainTextEdit;
	m_details->setObjectName(QStringLiteral("soundGenerationDetails"));
	m_details->setAccessibleName(tr("Sound details"));
	m_details->setReadOnly(true);
	m_details->setMaximumHeight(140);
	m_details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	reviewLayout->addWidget(m_details);
	splitter->addWidget(review);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({400, 680});

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_save = buttons->addButton(tr("&Save to Project"), QDialogButtonBox::ActionRole);
	m_save->setObjectName(QStringLiteral("soundGenerationSave"));
	m_save->setAccessibleName(tr("Save the selected sound to the project"));
	m_save->setToolTip(tr("Write it where the game reads it, with a record of what made it."));
	m_savePlace = buttons->addButton(tr("Save and Place in &Map"), QDialogButtonBox::ActionRole);
	m_savePlace->setObjectName(QStringLiteral("soundGenerationSavePlace"));
	m_savePlace->setAccessibleName(tr("Save the selected sound and place it in the open map"));
	m_savePlace->setToolTip(tr("Quake II and III: also adds a target_speaker at the view's centre in the open map."));
	m_openEditor = buttons->addButton(tr("Open in &Audio Editor"), QDialogButtonBox::ActionRole);
	m_openEditor->setObjectName(QStringLiteral("soundGenerationOpenEditor"));
	m_openEditor->setAccessibleName(tr("Open the selected sound in the Audio editor"));
	for (QPushButton* button : {m_save, m_savePlace, m_openEditor}) {
		button->setEnabled(false);
	}
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &SoundGenerationDialog::reject);
	connect(m_generate, &QPushButton::clicked, this, &SoundGenerationDialog::generate);
	connect(m_cancel, &QPushButton::clicked, this, &SoundGenerationDialog::cancelGeneration);
	connect(m_previewRequest, &QPushButton::clicked, this, &SoundGenerationDialog::showRequestPreview);
	connect(m_synthSource, &QRadioButton::toggled, this, &SoundGenerationDialog::refreshSourceStatus);
	connect(m_game, &QComboBox::currentIndexChanged, this, [this] { updateGameControls(); });
	connect(m_loop, &QCheckBox::clicked, this, [this] { m_loopTouched = true; });
	connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
		// Ambience and alarms loop by default, until the box is set by hand.
		if (!m_loopTouched) {
			m_loop->setChecked(soundGenerationKindLoops(m_kind->currentData().toString()));
		}
	});
	connect(m_variantList, &QListWidget::currentRowChanged, this, [this] { showSelected(); });
	connect(m_play, &QPushButton::clicked, this, &SoundGenerationDialog::togglePlayback);
	connect(m_save, &QPushButton::clicked, this, [this] { saveSelected(false); });
	connect(m_savePlace, &QPushButton::clicked, this, [this] { saveSelected(true); });
	connect(m_openEditor, &QPushButton::clicked, this, [this] {
		const int row = m_variantList->currentRow();
		if (row >= 0 && row < m_sounds.size() && m_hooks.openInEditor) {
			m_hooks.openInEditor(encodeAudioWav(m_sounds.at(row).clip), m_sounds.at(row).name);
		}
	});
	auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
	connect(shortcut, &QShortcut::activated, this, &SoundGenerationDialog::generate);
	updateGameControls();
	refreshSourceStatus();
}

SoundGenerationDialog::~SoundGenerationDialog()
{
	m_work->discard = true;
	if (m_work->thread) {
		m_work->thread->disconnect(this);
		m_work->thread->wait();
	}
}

void SoundGenerationDialog::setPrompt(const QString& prompt)
{
	m_prompt->setPlainText(prompt);
}

bool SoundGenerationDialog::busy() const
{
	return m_busy;
}

const QVector<GeneratedSound>& SoundGenerationDialog::sounds() const
{
	return m_sounds;
}

void SoundGenerationDialog::updateGameControls()
{
	SoundGameProfile profile;
	soundGameProfileForId(m_game->currentData().toString(), &profile);
	m_folder->setVisible(!profile.lumps && profile.id != QStringLiteral("generic"));
	m_folderLabel->setVisible(m_folder->isVisibleTo(this));
	m_wad->setVisible(profile.lumps);
	m_wadLabel->setVisible(profile.lumps);
	const bool speakers = profile.id == QStringLiteral("quake2") || profile.id == QStringLiteral("quake3");
	m_savePlace->setVisible(speakers);
}

void SoundGenerationDialog::refreshSourceStatus()
{
	if (m_modelSource->isChecked()) {
		const AiSoundConnection connection = resolveAiSoundConnection(StudioSettings().aiAutomationPreferences());
		m_sourceStatus->setText(aiSoundConnectionBlockText(connection));
	} else {
		m_sourceStatus->setText(tr("The synthesizer makes the sound here, with no AI and no network: the same sound for the same description and seed."));
	}
	m_sourceStatus->setAccessibleDescription(m_sourceStatus->text());
	m_previewRequest->setEnabled(m_modelSource->isChecked() && !m_busy);
}

SoundGenerationSpec SoundGenerationDialog::specFromControls() const
{
	SoundGenerationSpec spec;
	spec.prompt = m_prompt->toPlainText().trimmed();
	spec.game = m_game->currentData().toString();
	spec.kind = m_kind->currentData().toString();
	spec.name = m_name->text().trimmed();
	spec.folder = m_folder->text().trimmed();
	spec.durationSeconds = m_duration->value();
	spec.loop = m_loop->isChecked();
	spec.seed = m_seed->value();
	spec.variants = m_variants->value();
	return normalizedSoundGenerationSpec(spec);
}

AiSoundRequest SoundGenerationDialog::soundRequest(const SoundGenerationSpec& variant, const AiSoundConnection& connection) const
{
	AiSoundRequest request;
	request.connectorId = connection.connectorId;
	request.model = connection.model;
	request.endpoint = connection.endpoint;
	request.prompt = soundGenerationPrompt(variant);
	// The model's shortest is half a second.
	request.durationSeconds = variant.durationSeconds > 0.0 ? std::max(0.5, variant.durationSeconds) : 0.0;
	request.promptInfluence = variant.promptInfluence;
	request.loop = variant.loop;
	return request;
}

void SoundGenerationDialog::generate()
{
	if (m_busy) {
		return;
	}
	if (m_playback && m_playback->active()) {
		m_playback->stop();
		updatePlayButton();
	}
	// A kind read from the description loops as that kind does, unless the
	// box was set by hand.
	if (!m_loopTouched && m_kind->currentData().toString().isEmpty()) {
		m_loop->setChecked(soundGenerationKindLoops(soundKindFromPrompt(m_prompt->toPlainText())));
	}
	const SoundGenerationSpec spec = specFromControls();
	if (spec.prompt.isEmpty() && m_kind->currentData().toString().isEmpty()) {
		m_status->setText(tr("Describe the sound, or choose its kind."));
		m_prompt->setFocus();
		return;
	}
	m_specs.clear();
	for (int index = 0; index < spec.variants; ++index) {
		m_specs << soundGenerationVariantSpec(spec, index, spec.variants);
	}
	m_answers.clear();
	m_provenance = QJsonObject();
	if (m_synthSource->isChecked()) {
		m_provenance.insert(QStringLiteral("connector"), QStringLiteral("synth"));
		if (m_hooks.beginTask) {
			m_task = m_hooks.beginTask(tr("Generate Sound"), tr("Synthesizing \"%1\".").arg(soundGenerationName(spec)));
		}
		process();
		return;
	}
	if (spec.prompt.isEmpty()) {
		m_status->setText(tr("Describe the sound for the model."));
		m_prompt->setFocus();
		return;
	}
	const AiSoundConnection connection = resolveAiSoundConnection(StudioSettings().aiAutomationPreferences());
	refreshSourceStatus();
	if (!connection.ready()) {
		m_status->setText(aiSoundConnectionBlockText(connection));
		return;
	}
	if (!connection.local && m_hooks.confirmSend) {
		AiHttpRequest http;
		QString error;
		if (!buildAiSoundHttpRequest(soundRequest(m_specs.first(), connection), QString(), &http, &error)) {
			m_status->setText(error);
			return;
		}
		if (!m_hooks.confirmSend(connection.connectorId, connection.displayName, connection.endpoint, http)) {
			m_status->setText(tr("Nothing was sent."));
			return;
		}
	}
	m_provenance.insert(QStringLiteral("connector"), connection.connectorId);
	m_provenance.insert(QStringLiteral("model"), connection.model);
	m_provenance.insert(QStringLiteral("host"), QUrl(connection.endpoint).host());
	if (m_hooks.beginTask) {
		m_task = m_hooks.beginTask(tr("Generate Sound"), tr("Asking %1 (%2) for %n sound(s).", nullptr, spec.variants).arg(connection.displayName, connection.model));
	}
	askModel(connection, 0);
}

void SoundGenerationDialog::askModel(const AiSoundConnection& connection, int index)
{
	setBusy(true, tr("Asking %1 for sound %2 of %3…").arg(connection.displayName).arg(index + 1).arg(m_specs.size()));
	QString error;
	const bool started = m_client->send(soundRequest(m_specs.at(index), connection), aiSoundConnectionApiKey(connection),
		[this, connection, index](const AiSoundResponse& response) {
			if (!response.ok) {
				const bool cancelled = response.failure == AiChatFailure::Cancelled;
				if (!m_answers.isEmpty()) {
					// Keep what came in before the failure.
					m_specs.resize(m_answers.size());
					m_status->setText(cancelled ? tr("Cancelled; keeping the %n sound(s) already made.", nullptr, int(m_answers.size()))
												: tr("The sound model stopped: %1").arg(response.errorMessage));
					process();
					return;
				}
				const QString message = cancelled ? tr("Cancelled. Nothing was made.") : tr("The sound model did not make the sound: %1").arg(response.errorMessage);
				setBusy(false, message);
				finishTask(false, cancelled, message);
				return;
			}
			m_answers.push_back({response.audio, response.mimeType});
			if (index + 1 < m_specs.size()) {
				askModel(connection, index + 1);
				return;
			}
			process();
		},
		&error);
	if (!started) {
		setBusy(false, error);
		finishTask(false, false, error);
	}
}

void SoundGenerationDialog::process()
{
	setBusy(true, tr("Shaping %n sound(s) for the game…", nullptr, int(m_specs.size())));
	const auto work = m_work;
	work->discard = false;
	work->sounds.clear();
	const QVector<SoundGenerationSpec> specs = m_specs;
	const QVector<QPair<QByteArray, QString>> answers = m_answers;
	const QString source = answers.isEmpty() ? QStringLiteral("synth")
											 : QStringLiteral("ai:%1/%2").arg(m_provenance.value(QStringLiteral("connector")).toString(),
												   m_provenance.value(QStringLiteral("model")).toString());
	QThread* thread = QThread::create([work, specs, answers, source] {
		for (int index = 0; index < specs.size() && !work->discard; ++index) {
			work->sounds << (answers.isEmpty() ? processGeneratedSound(synthesizeSound(specs.at(index)), specs.at(index), source)
											   : processGeneratedSoundBytes(answers.at(index).first, answers.at(index).second, specs.at(index), source));
		}
	});
	work->thread = thread;
	connect(thread, &QThread::finished, this, [this, work, thread] {
		thread->deleteLater();
		work->thread = nullptr;
		if (work->discard) {
			setBusy(false, tr("Cancelled. The sounds were not shown."));
			finishTask(false, true, tr("Cancelled."));
			return;
		}
		m_sounds = work->sounds;
		showSounds();
	});
	thread->start();
}

void SoundGenerationDialog::showSounds()
{
	m_variantList->clear();
	QStringList failures;
	int made = 0;
	const QColor wave = palette().color(QPalette::Highlight);
	const QColor axis = palette().color(QPalette::Mid);
	// A selected row is filled with the highlight colour, so its waveform is
	// drawn in the highlighted text's colour instead.
	const QColor selectedWave = palette().color(QPalette::HighlightedText);
	QColor selectedAxis = selectedWave;
	selectedAxis.setAlphaF(0.4f);
	for (const GeneratedSound& sound : m_sounds) {
		auto* item = new QListWidgetItem(m_variantList);
		const QString summary = generatedSoundSummary(sound);
		item->setText(summary);
		item->setData(Qt::AccessibleDescriptionRole, summary);
		// A failed variant stays reachable, so its reason can be read; the
		// actions stay off for it.
		if (sound.ok) {
			QIcon icon;
			icon.addPixmap(QPixmap::fromImage(waveformImage(sound.clip, m_variantList->iconSize(), wave, axis)), QIcon::Normal);
			icon.addPixmap(QPixmap::fromImage(waveformImage(sound.clip, m_variantList->iconSize(), selectedWave, selectedAxis)), QIcon::Selected);
			item->setIcon(icon);
			++made;
		} else {
			failures << sound.error;
		}
	}
	if (made > 0) {
		for (int row = 0; row < m_sounds.size(); ++row) {
			if (m_sounds.at(row).ok) {
				m_variantList->setCurrentRow(row);
				break;
			}
		}
	}
	const QString message = made == 0 ? tr("No sound could be made: %1").arg(failures.join(QLatin1Char(' ')))
		: failures.isEmpty()			 ? tr("Made %n sound(s). Play them, then save the one you want.", nullptr, made)
										 : tr("Made %1 of %2 sounds; the rest failed (see details).").arg(made).arg(m_sounds.size());
	setBusy(false, message);
	finishTask(made > 0, false, message);
	showSelected();
}

void SoundGenerationDialog::showSelected()
{
	const int row = m_variantList->currentRow();
	const bool valid = !m_busy && row >= 0 && row < m_sounds.size() && m_sounds.at(row).ok;
	for (QPushButton* button : {m_save, m_savePlace, m_openEditor, m_play}) {
		button->setEnabled(valid);
	}
	if (!valid) {
		m_details->clear();
		return;
	}
	const GeneratedSound& sound = m_sounds.at(row);
	QStringList lines;
	lines << generatedSoundSummary(sound);
	lines << tr("Goes to: %1").arg(sound.virtualPath);
	lines << tr("Maps and scripts name it: %1").arg(sound.reference);
	lines << tr("Made by: %1, seed %2").arg(sound.source == QStringLiteral("synth") ? tr("the synthesizer") : sound.source).arg(sound.seed);
	lines += sound.notes;
	if (row < m_specs.size() && sound.source != QStringLiteral("synth")) {
		lines << tr("Sent: %1").arg(soundGenerationPrompt(m_specs.at(row)));
	}
	m_details->setPlainText(lines.join(QLatin1Char('\n')));
	m_details->setAccessibleDescription(lines.join(QLatin1Char(' ')));
}

bool SoundGenerationDialog::saveSelected(bool place)
{
	const int row = m_variantList->currentRow();
	if (m_busy || row < 0 || row >= m_sounds.size() || row >= m_specs.size() || !m_sounds.at(row).ok) {
		return false;
	}
	const GeneratedSound& sound = m_sounds.at(row);
	SoundGenerationOutput output;
	output.folder = m_hooks.outputFolder ? m_hooks.outputFolder() : QDir::homePath();
	output.wadPath = m_wad->text().trimmed().isEmpty() ? QString() : QDir(output.folder).filePath(m_wad->text().trimmed());
	output.provenance = m_provenance;
	if (sound.source != QStringLiteral("synth")) {
		output.provenance.insert(QStringLiteral("promptSent"), soundGenerationPrompt(m_specs.at(row)));
	}
	SoundGenerationWriteReport report = writeGeneratedSound(sound, m_specs.at(row), output);
	if (!report.ok && report.alreadyExists) {
		QMessageBox question(QMessageBox::Question, tr("Replace Sound"), tr("%1 Replace it?").arg(report.error), QMessageBox::Yes | QMessageBox::No, this);
		question.setDefaultButton(QMessageBox::No);
		if (question.exec() != QMessageBox::Yes) {
			m_status->setText(tr("Kept the existing sound; nothing was written."));
			return false;
		}
		output.replaceExisting = true;
		report = writeGeneratedSound(sound, m_specs.at(row), output);
	}
	if (!report.ok) {
		m_status->setText(report.error);
		return false;
	}
	if (m_hooks.written) {
		m_hooks.written(report.writtenPaths);
	}
	QString message = tr("Saved %1 to %2.").arg(sound.reference, QDir::toNativeSeparators(report.writtenPaths.value(0)));
	if (!report.notes.isEmpty()) {
		message += QLatin1Char(' ') + report.notes.join(QLatin1Char(' '));
	}
	if (place && m_hooks.placeInMap) {
		QString error;
		if (!m_hooks.placeInMap(sound, m_specs.at(row).game, &error)) {
			m_status->setText(tr("Saved %1, but it was not placed: %2").arg(sound.reference, error));
			return false;
		}
		message = sound.clip.markers.loop ? tr("Saved %1 and placed a looping target_speaker for it in the map.").arg(sound.reference)
										  : tr("Saved %1 and placed a target_speaker named %2 in the map; target it from a trigger to play it.").arg(sound.reference, sound.name);
	}
	m_status->setText(message);
	return true;
}

void SoundGenerationDialog::togglePlayback()
{
	const int row = m_variantList->currentRow();
	if (!m_playback) {
		m_playback = new AudioPlayback(this);
		connect(m_playback, &AudioPlayback::changed, this, [this] { updatePlayButton(); });
		connect(m_playback, &AudioPlayback::failed, this, [this](const QString& message) { m_status->setText(message); });
	}
	if (m_playback->active()) {
		m_playback->stop();
		updatePlayButton();
		return;
	}
	if (row < 0 || row >= m_sounds.size() || !m_sounds.at(row).ok) {
		return;
	}
	if (!m_playback->available()) {
		m_status->setText(tr("Sound playback is not available here; save the sound or open it in the Audio editor to listen."));
		return;
	}
	const AudioClip& clip = m_sounds.at(row).clip;
	m_playback->setLoop(clip.markers.loop.has_value());
	m_playback->start(encodeAudioWav(clip), 0, clip.frameCount(), clip.sampleRate);
	updatePlayButton();
}

void SoundGenerationDialog::updatePlayButton()
{
	const bool playing = m_playback && m_playback->active();
	m_play->setText(playing ? tr("&Stop") : tr("&Play"));
	m_play->setAccessibleName(playing ? tr("Stop the sound") : tr("Play the selected sound"));
}

void SoundGenerationDialog::setBusy(bool busy, const QString& status)
{
	m_busy = busy;
	m_status->setText(status);
	m_progress->setVisible(busy);
	m_cancel->setEnabled(busy);
	m_generate->setEnabled(!busy);
	m_previewRequest->setEnabled(!busy && m_modelSource->isChecked());
	showSelected();
}

void SoundGenerationDialog::finishTask(bool succeeded, bool cancelled, const QString& summary)
{
	if (!m_task.isEmpty() && m_hooks.endTask) {
		m_hooks.endTask(m_task, succeeded, cancelled, summary);
	}
	m_task.clear();
}

void SoundGenerationDialog::cancelGeneration()
{
	if (!m_busy) {
		return;
	}
	if (m_client->busy()) {
		m_client->cancel();
		return;
	}
	m_work->discard = true;
	m_status->setText(tr("Cancelling…"));
}

void SoundGenerationDialog::showRequestPreview()
{
	const SoundGenerationSpec spec = specFromControls();
	const AiSoundConnection connection = resolveAiSoundConnection(StudioSettings().aiAutomationPreferences());
	AiHttpRequest http;
	QString error;
	SoundGenerationSpec first = soundGenerationVariantSpec(spec, 0, spec.variants);
	if (first.prompt.isEmpty()) {
		first.prompt = tr("(your description)");
	}
	const bool built = buildAiSoundHttpRequest(soundRequest(first, connection), QString(), &http, &error);
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("soundGenerationRequestPreview"));
	dialog.setWindowTitle(tr("Sound Request"));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.resize(720, 480);
	auto* layout = new QVBoxLayout(&dialog);
	auto* explanation = new QLabel(built ? tr("Nothing has been sent. This is the request Generate would send for each variant, without your API key. %1")
												.arg(aiSoundConnectionBlockText(connection))
										 : error);
	explanation->setWordWrap(true);
	layout->addWidget(explanation);
	auto* text = new QPlainTextEdit;
	text->setObjectName(QStringLiteral("soundGenerationRequestText"));
	text->setAccessibleName(tr("The sound request"));
	text->setReadOnly(true);
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	text->setPlainText(built ? describeAiHttpRequest(http) : QString());
	layout->addWidget(text, 1);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.exec();
}

void SoundGenerationDialog::reject()
{
	if (m_busy) {
		cancelGeneration();
		return;
	}
	if (m_playback && m_playback->active()) {
		m_playback->stop();
	}
	QDialog::reject();
}

void SoundGenerationDialog::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape && m_busy) {
		cancelGeneration();
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

} // namespace vibestudio
