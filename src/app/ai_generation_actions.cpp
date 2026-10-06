// The Level and Texture Generators and Edit Map with AI in the shell:
// commands, the hooks that tie them to the editor, packages, Activity and
// consent, and the image model settings. The generators themselves are
// core/level_generation.h, core/texture_generation.h, and
// core/level_ai_edit.h; their dialogs are app/*_generation_dialog.* and
// app/level_ai_edit_dialog.*.

#include "app/application_shell.h"
#include "app/audio_editor_dialog.h"
#include "app/level_ai_edit_dialog.h"
#include "app/level_generation_dialog.h"
#include "app/level_texture_audit_panel.h"
#include "app/map_viewport.h"
#include "app/sound_generation_dialog.h"
#include "app/studio_actions.h"
#include "app/studio_layout.h"
#include "app/texture_editor_dialog.h"
#include "app/texture_generation_dialog.h"

#include "core/ai_audio_transport.h"
#include "core/ai_image_transport.h"
#include "core/audio_level.h"
#include "core/level_build_workspace.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

namespace vibestudio {

void ApplicationShell::registerAiGenerationCommands()
{
	const auto add = [this](const QString& id, const QString& section, const QString& label, const QString& description, const QString& icon,
						 std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::Tools;
		command.menuSection = section;
		command.label = label;
		command.statusTip = description;
		command.iconName = icon;
		command.handler = std::move(handler);
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.generate"), tr("Generate"), tr("Generate Level…"),
		tr("Plan and build a sealed, playable level from a description, by the rules or with your text model."), QStringLiteral("sparkle"),
		[this] { showLevelGenerator(); });
	add(QStringLiteral("texture.generate"), tr("Generate"), tr("Generate Texture…"),
		tr("Draw seamless, game-ready textures with your image model, or make one from a picture."), QStringLiteral("sparkle"),
		[this] { showTextureGenerator(); });
	add(QStringLiteral("map.aiEdit"), tr("Generate"), tr("Edit Map with AI…"),
		tr("Say what should change in the open map; review the edits your text model proposes, then apply the ones you keep, each undoable."),
		QStringLiteral("sparkle"), [this] { showLevelAiEditor(); });
	add(QStringLiteral("audio.generate"), tr("Generate"), tr("Generate Sound…"),
		tr("Make game-ready sound effects from a description with the synthesizer or your sound model, and save them where the game reads them."),
		QStringLiteral("sparkle"), [this] { showSoundGenerator(); });
}

QString ApplicationShell::beginGenerationTask(const QString& title, const QString& detail)
{
	// Shown as Activity rows, as the Assistant's questions are.
	const QString id = m_activity.createTask(title, detail, QStringLiteral("ai"), OperationState::Running, false);
	m_activity.appendLog(id, OperationState::Running, detail);
	refreshActivityCenter(id);
	return id;
}

void ApplicationShell::endGenerationTask(const QString& id, bool succeeded, bool cancelled, const QString& summary)
{
	if (!m_activity.contains(id)) {
		return;
	}
	cancelled ? m_activity.cancelTask(id, summary) : succeeded ? m_activity.completeTask(id, summary) : m_activity.failTask(id, summary);
	persistActivityTask(id);
	refreshRecentActivityTimeline();
	refreshActivityCenter(id);
}

QString ApplicationShell::generationGame() const
{
	// The open map's game first, then the selected installation's.
	switch (m_levelMapDocument.format) {
	case LevelMapFormat::DoomWad:
		return QStringLiteral("doom");
	case LevelMapFormat::Quake3Map:
		return QStringLiteral("quake3");
	case LevelMapFormat::QuakeMap:
		return levelBuildTargetForDocument(m_levelMapDocument);
	case LevelMapFormat::Unknown:
		break;
	}
	GameInstallationProfile installation;
	if (selectedGameInstallation(&installation)) {
		const QString key = installation.gameKey;
		if (key == QStringLiteral("quake") || key == QStringLiteral("quake2") || key == QStringLiteral("quake3") || key == QStringLiteral("doom")) {
			return key;
		}
		if (key == QStringLiteral("heretic-hexen")) {
			return QStringLiteral("doom");
		}
	}
	return {};
}

QStringList ApplicationShell::generationTextureNames(const QString& game) const
{
	// What the open package holds, as maps name it, and what the open map
	// already uses when it is the same game.
	QStringList names;
	for (const PackageEntry& entry : packageViewArchive().entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		if (entry.typeHint == QStringLiteral("wad-texture") || entry.typeHint == QStringLiteral("wad-flat")) {
			names << entry.virtualPath;
			continue;
		}
		if (entry.virtualPath.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
			const QFileInfo info(entry.virtualPath.mid(9));
			static const QStringList images = {QStringLiteral("wal"), QStringLiteral("tga"), QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg")};
			if (images.contains(info.suffix().toLower())) {
				names << (info.path() == QStringLiteral(".") ? QString() : info.path() + QLatin1Char('/')) + info.completeBaseName();
			}
		}
	}
	if (!game.isEmpty() && generationGame() == game) {
		names += levelMapTextureNames(m_levelMapDocument);
	}
	names.removeDuplicates();
	return names;
}

bool ApplicationShell::confirmGenerationSend(const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request,
	const QString& explanationText)
{
	const QString host = QUrl(endpoint).host();
	const QString destination = QStringLiteral("%1@%2").arg(connectorId, host);
	if (m_settings.aiContextConsentGiven(m_settings.currentProjectPath(), destination)) {
		return true;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("generationConsentDialog"));
	dialog.setWindowTitle(tr("Send to %1?").arg(displayName));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.resize(680, 540);
	auto* layout = new QVBoxLayout(&dialog);
	auto* explanation = new QLabel(!explanationText.isEmpty() ? explanationText
			: tr("The description and settings below go to %1 at %2; nothing else from the project goes with them, and your API key is sent only to "
				 "authenticate. This is the first request from this project to %2, so here is exactly what will be sent:")
				  .arg(displayName, host));
	explanation->setWordWrap(true);
	layout->addWidget(explanation);
	auto* text = new QPlainTextEdit;
	text->setObjectName(QStringLiteral("generationConsentRequest"));
	text->setAccessibleName(tr("The request that will be sent"));
	text->setReadOnly(true);
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	text->setPlainText(describeAiHttpRequest(request));
	layout->addWidget(text, 1);
	auto* raw = new QCheckBox(tr("Show the raw request"));
	connect(raw, &QCheckBox::toggled, text, [text, request](bool shown) { text->setPlainText(describeAiHttpRequest(request, shown ? AiRequestView::Raw : AiRequestView::Readable)); });
	layout->addWidget(raw);
	auto* remember = new QCheckBox(tr("Don't ask again for this project and %1").arg(host));
	remember->setObjectName(QStringLiteral("generationConsentRemember"));
	remember->setChecked(true);
	layout->addWidget(remember);
	auto* buttons = new QDialogButtonBox;
	QPushButton* send = buttons->addButton(tr("Send"), QDialogButtonBox::AcceptRole);
	send->setObjectName(QStringLiteral("generationConsentSend"));
	buttons->addButton(QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	// Cancel is the safe answer, so it is where Enter lands.
	buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
	if (dialog.exec() != QDialog::Accepted) {
		return false;
	}
	if (remember->isChecked()) {
		m_settings.setAiContextConsentGiven(m_settings.currentProjectPath(), destination, true);
		m_settings.sync();
	}
	return true;
}

void ApplicationShell::showLevelGenerator()
{
	if (!m_levelGenerationDialog) {
		LevelGenerationDialogHooks hooks;
		hooks.defaultGame = [this] { return generationGame(); };
		hooks.availableTextures = [this](const QString& game) { return generationTextureNames(game); };
		hooks.confirmSend = [this](const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request) {
			return confirmGenerationSend(connectorId, displayName, endpoint, request);
		};
		hooks.openInLevels = [this](const LevelGenerationResult& result, QString* error) {
			LevelMapDocument document;
			if (!levelGenerationDocument(result, &document, error)) {
				return false;
			}
			if (!confirmLevelMapEditsHandled(tr("Save the open map before opening the generated level?"))) {
				if (error) {
					*error = tr("The open map was kept; the generated level was not opened.");
				}
				return false;
			}
			adoptLevelMapDocument(std::move(document));
			recordActivity(tr("Generated level opened"), result.plan.title, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map"));
			statusBar()->showMessage(tr("Opened the generated level \"%1\". Choose Save to set its file location.").arg(result.plan.title), 8000);
			return true;
		};
		hooks.beginTask = [this](const QString& title, const QString& detail) { return beginGenerationTask(title, detail); };
		hooks.endTask = [this](const QString& id, bool succeeded, bool cancelled, const QString& summary) { endGenerationTask(id, succeeded, cancelled, summary); };
		hooks.saveFolder = [this] {
			const QString project = m_settings.currentProjectPath();
			if (project.isEmpty()) {
				return QDir::homePath();
			}
			const QString maps = QDir(project).filePath(QStringLiteral("maps"));
			return QFileInfo(maps).isDir() ? maps : project;
		};
		m_levelGenerationDialog = new LevelGenerationDialog(this, std::move(hooks));
	}
	m_levelGenerationDialog->refreshPlannerStatus();
	m_levelGenerationDialog->show();
	m_levelGenerationDialog->raise();
	m_levelGenerationDialog->activateWindow();
}

void ApplicationShell::showTextureGenerator()
{
	if (!m_textureGenerationDialog) {
		TextureGenerationDialogHooks hooks;
		hooks.defaultGame = [this] { return generationGame(); };
		hooks.paletteSource = [this] { return texturePreviewSource(); };
		hooks.outputFolder = [this] {
			const QString project = m_settings.currentProjectPath();
			return project.isEmpty() ? QDir::homePath() : project;
		};
		hooks.confirmSend = [this](const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request) {
			return confirmGenerationSend(connectorId, displayName, endpoint, request);
		};
		hooks.currentTexture = [this] { return m_textureDecoded.image; };
		hooks.openInEditor = [this](const QImage& image, const QString& name) {
			setMode(StudioMode::Textures);
			showTextureEditor();
			QString error;
			if (m_textureEditorDialog && !m_textureEditorDialog->setImage(image, name, &error)) {
				statusBar()->showMessage(error, 8000);
			}
		};
		hooks.applyToMap = [this](const QString& name, QString* error) {
			if (m_levelMapDocument.selection.isEmpty()) {
				if (error) {
					*error = tr("Saved, but nothing is selected in the open map to apply it to.");
				}
				return false;
			}
			int applied = 0;
			if (!applyLevelMapTexture(&m_levelMapDocument, name, &applied, error)) {
				return false;
			}
			rememberLevelMaterial(name);
			recordActivity(tr("Level map texture applied"), name, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
			refreshLevelMapWorkbench();
			return true;
		};
		hooks.written = [this](const QStringList& paths) {
			recordActivity(tr("Generated texture saved"), paths.value(0), QStringLiteral("texture"), OperationState::Completed,
				tr("%n file(s) written", nullptr, int(paths.size())));
		};
		hooks.missingTextures = [this] {
			// What the Levels texture audit found nothing for.
			QStringList names;
			if (m_levelTextureAudit && m_levelTextureAudit->result()) {
				for (const MapTextureReference& reference : m_levelTextureAudit->result()->references) {
					if (reference.resolution == MapTextureResolution::Missing) {
						names << reference.textureName;
					}
				}
			}
			return names;
		};
		hooks.beginTask = [this](const QString& title, const QString& detail) { return beginGenerationTask(title, detail); };
		hooks.endTask = [this](const QString& id, bool succeeded, bool cancelled, const QString& summary) { endGenerationTask(id, succeeded, cancelled, summary); };
		m_textureGenerationDialog = new TextureGenerationDialog(this, std::move(hooks));
	}
	m_textureGenerationDialog->refreshSourceStatus();
	m_textureGenerationDialog->show();
	m_textureGenerationDialog->raise();
	m_textureGenerationDialog->activateWindow();
}

void ApplicationShell::showLevelAiEditor()
{
	if (!m_levelAiEditDialog) {
		LevelAiEditDialogHooks hooks;
		hooks.document = [this]() -> const LevelMapDocument* {
			return m_levelMapDocument.format == LevelMapFormat::Unknown ? nullptr : &m_levelMapDocument;
		};
		hooks.projectRoot = [this] { return m_settings.currentProjectPath(); };
		hooks.confirmSend = [this](const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request) {
			const QString host = QUrl(endpoint).host();
			return confirmGenerationSend(connectorId, displayName, endpoint, request,
				tr("Your instruction and a summary of the open map (its entities and their keys, brush bounds, textures, and selection, with your home and project "
				   "folders shortened) go to %1 at %2; nothing else from the project goes with them, and your API key is sent only to authenticate. This is the "
				   "first request from this project to %2, so here is exactly what will be sent:")
					.arg(displayName, host));
		};
		hooks.apply = [this](const LevelAiEditProposal& proposal) {
			// Through the editor's own operations, so each edit is an undo step.
			const LevelAiEditApplyReport report = applyLevelAiEditProposal(&m_levelMapDocument, proposal);
			if (report.applied > 0) {
				recordActivity(tr("AI map edits applied"), tr("%n edit(s) to %1", nullptr, report.applied).arg(m_levelMapDocument.mapName), QStringLiteral("level-map"),
					OperationState::Warning, tr("Unsaved map edit"));
				refreshLevelMapWorkbench();
			}
			return report;
		};
		hooks.beginTask = [this](const QString& title, const QString& detail) { return beginGenerationTask(title, detail); };
		hooks.endTask = [this](const QString& id, bool succeeded, bool cancelled, const QString& summary) { endGenerationTask(id, succeeded, cancelled, summary); };
		hooks.proposalFolder = [this] {
			const QString project = m_settings.currentProjectPath();
			return project.isEmpty() ? QDir::homePath() : project;
		};
		m_levelAiEditDialog = new LevelAiEditDialog(this, std::move(hooks));
	}
	m_levelAiEditDialog->refreshStatus();
	m_levelAiEditDialog->show();
	m_levelAiEditDialog->raise();
	m_levelAiEditDialog->activateWindow();
}

void ApplicationShell::showSoundGenerator()
{
	if (!m_soundGenerationDialog) {
		SoundGenerationDialogHooks hooks;
		hooks.defaultGame = [this] { return generationGame(); };
		hooks.outputFolder = [this] {
			const QString project = m_settings.currentProjectPath();
			return project.isEmpty() ? QDir::homePath() : project;
		};
		hooks.confirmSend = [this](const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request) {
			return confirmGenerationSend(connectorId, displayName, endpoint, request);
		};
		hooks.openInEditor = [this](const QByteArray& wav, const QString& name) {
			setMode(StudioMode::Audio);
			ensureAudioEditor();
			unloadAudioPlayback();
			refreshAudioTransport();
			m_audioEditorDialog->show();
			m_audioEditorDialog->raise();
			m_audioEditorDialog->refreshContext();
			m_audioEditorDialog->loadSource(name + QStringLiteral(".wav"), QString(), [wav](QString*) { return wav; });
		};
		hooks.placeInMap = [this](const GeneratedSound& sound, const QString& game, QString* error) {
			// A target_speaker at the middle of the view, as the Audio editor places one.
			LevelSoundRequest request;
			request.game = game;
			request.virtualPath = sound.virtualPath;
			request.origin = m_levelMapViewport ? m_levelMapViewport->worldPositionAt(QPointF(m_levelMapViewport->rect().center()), levelMapHiddenAxisValue())
												: LevelMapVec3 {0, 0, 0, true};
			request.mode = sound.clip.markers.loop ? QStringLiteral("loop-on") : QStringLiteral("triggered");
			request.targetName = sound.clip.markers.loop ? QString() : sound.name;
			int entity = -1;
			if (!placeLevelSound(&m_levelMapDocument, request, sound.delivery.bytes, &entity, error)) {
				return false;
			}
			refreshLevelMapWorkbench();
			recordActivity(tr("Generated sound placed"), sound.reference, QStringLiteral("level-map"), OperationState::Warning,
				tr("entity:%1 added; save the map to keep it.").arg(entity));
			return true;
		};
		hooks.written = [this](const QStringList& paths) {
			recordActivity(tr("Generated sound saved"), paths.value(0), QStringLiteral("audio"), OperationState::Completed,
				tr("%n file(s) written", nullptr, int(paths.size())));
		};
		hooks.beginTask = [this](const QString& title, const QString& detail) { return beginGenerationTask(title, detail); };
		hooks.endTask = [this](const QString& id, bool succeeded, bool cancelled, const QString& summary) { endGenerationTask(id, succeeded, cancelled, summary); };
		m_soundGenerationDialog = new SoundGenerationDialog(this, std::move(hooks));
	}
	m_soundGenerationDialog->refreshSourceStatus();
	m_soundGenerationDialog->show();
	m_soundGenerationDialog->raise();
	m_soundGenerationDialog->activateWindow();
}

QWidget* ApplicationShell::buildAiImageSettingsGroup()
{
	// The model the Texture Generator and `ai image` draw with: one image
	// connector's model and endpoint at a time.
	auto* panel = new QGroupBox(tr("Image Model"));
	panel->setObjectName(QStringLiteral("aiImageModelPanel"));
	auto* layout = new QFormLayout(panel);
	layout->setHorizontalSpacing(16);
	layout->setVerticalSpacing(10);
	m_aiImageConnector = new QComboBox;
	m_aiImageConnector->setObjectName(QStringLiteral("aiImageConnector"));
	m_aiImageConnector->setAccessibleName(tr("Image connector to set up"));
	m_aiImageConnector->setToolTip(tr("The connector whose image model and endpoint are shown below."));
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		if (aiConnectorHasImageTransport(connector.id)) {
			m_aiImageConnector->addItem(connector.displayName, connector.id);
		}
	}
	layout->addRow(tr("Connector"), m_aiImageConnector);
	m_aiImageModel = new QLineEdit;
	m_aiImageModel->setObjectName(QStringLiteral("aiImageModel"));
	m_aiImageModel->setAccessibleName(tr("Image model name"));
	m_aiImageModel->setToolTip(tr("The image model's name as the provider lists it; a Stable Diffusion web UI takes a checkpoint name, or none for the one loaded."));
	layout->addRow(tr("Model"), m_aiImageModel);
	m_aiImageEndpoint = new QLineEdit;
	m_aiImageEndpoint->setObjectName(QStringLiteral("aiImageEndpoint"));
	m_aiImageEndpoint->setAccessibleName(tr("Image endpoint address"));
	m_aiImageEndpoint->setToolTip(tr("Leave empty for the provider's own address. The local connector speaks the Stable Diffusion web UI's API, or OpenAI's at an address ending in /v1."));
	layout->addRow(tr("Endpoint"), m_aiImageEndpoint);
	m_aiImageStatus = new QLabel;
	m_aiImageStatus->setObjectName(QStringLiteral("aiImageStatus"));
	m_aiImageStatus->setWordWrap(true);
	m_aiImageStatus->setAccessibleName(tr("Image connection status"));
	layout->addRow(tr("Status"), m_aiImageStatus);
	connect(m_aiImageConnector, &QComboBox::currentIndexChanged, this, [this]() { refreshAiImageModelFields(); });
	connect(m_aiImageModel, &QLineEdit::editingFinished, this, [this]() { saveAiImageModelFields(); });
	connect(m_aiImageEndpoint, &QLineEdit::editingFinished, this, [this]() { saveAiImageModelFields(); });
	refreshAiImageModelFields();
	return panel;
}

void ApplicationShell::refreshAiImageModelFields()
{
	if (!m_aiImageConnector || !m_aiImageModel || !m_aiImageEndpoint || !m_aiImageStatus) {
		return;
	}
	const QString id = m_aiImageConnector->currentData().toString();
	const AiAutomationPreferences preferences = m_settings.aiAutomationPreferences();
	const QSignalBlocker modelBlocker(m_aiImageModel);
	const QSignalBlocker endpointBlocker(m_aiImageEndpoint);
	m_aiImageModel->setText(preferences.connectorImageModels.value(id));
	const QString suggested = aiSuggestedImageModel(id);
	const AiImageApi api = aiImageApiFor(id, aiEffectiveImageEndpoint(id, preferences.connectorImageEndpoints.value(id)));
	m_aiImageModel->setPlaceholderText(!suggested.isEmpty() ? suggested
			: api == AiImageApi::StableDiffusionWebUi ? tr("The checkpoint the web UI has loaded")
													  : tr("The model name your provider lists"));
	m_aiImageEndpoint->setText(preferences.connectorImageEndpoints.value(id));
	const QString defaultEndpoint = aiDefaultImageEndpoint(id);
	m_aiImageEndpoint->setPlaceholderText(defaultEndpoint.isEmpty() ? tr("https://… (required)") : defaultEndpoint);
	// Which connector the generators will use, and what stops it.
	const AiImageConnection active = resolveAiImageConnection(preferences);
	const QString speaks = api == AiImageApi::OpenAiImages ? tr("This connector speaks OpenAI's Images API.")
		: api == AiImageApi::GeminiImages				   ? tr("This connector speaks Gemini's generateContent with image output.")
														   : tr("This connector speaks the Stable Diffusion web UI's API (AUTOMATIC1111, Forge, SD.Next).");
	m_aiImageStatus->setText(QStringLiteral("%1\n%2").arg(aiImageConnectionBlockText(active), speaks));
	m_aiImageStatus->setAccessibleDescription(m_aiImageStatus->text());
}

void ApplicationShell::saveAiImageModelFields()
{
	if (!m_aiImageConnector || !m_aiImageModel || !m_aiImageEndpoint) {
		return;
	}
	const QString id = m_aiImageConnector->currentData().toString();
	AiAutomationPreferences preferences = m_settings.aiAutomationPreferences();
	const QString model = m_aiImageModel->text().trimmed();
	const QString endpoint = m_aiImageEndpoint->text().trimmed();
	if (preferences.connectorImageModels.value(id) == model && preferences.connectorImageEndpoints.value(id) == endpoint) {
		return;
	}
	model.isEmpty() ? void(preferences.connectorImageModels.remove(id)) : void(preferences.connectorImageModels.insert(id, model));
	endpoint.isEmpty() ? void(preferences.connectorImageEndpoints.remove(id)) : void(preferences.connectorImageEndpoints.insert(id, endpoint));
	m_settings.setAiAutomationPreferences(preferences);
	m_settings.sync();
	refreshAiImageModelFields();
	if (m_textureGenerationDialog) {
		m_textureGenerationDialog->refreshSourceStatus();
	}
	statusBar()->showMessage(tr("Saved the %1 image model settings.").arg(m_aiImageConnector->currentText()), 4000);
}

QWidget* ApplicationShell::buildAiSoundSettingsGroup()
{
	// The model the Sound Generator and `asset audio-generate` make sounds
	// with, when they are not using the synthesizer.
	auto* panel = new QGroupBox(tr("Sound Model"));
	panel->setObjectName(QStringLiteral("aiSoundModelPanel"));
	auto* layout = new QFormLayout(panel);
	layout->setHorizontalSpacing(16);
	layout->setVerticalSpacing(10);
	m_aiSoundConnector = new QComboBox;
	m_aiSoundConnector->setObjectName(QStringLiteral("aiSoundConnector"));
	m_aiSoundConnector->setAccessibleName(tr("Sound connector to set up"));
	m_aiSoundConnector->setToolTip(tr("The connector whose sound model and endpoint are shown below."));
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		if (aiConnectorHasSoundTransport(connector.id)) {
			m_aiSoundConnector->addItem(connector.displayName, connector.id);
		}
	}
	layout->addRow(tr("Connector"), m_aiSoundConnector);
	m_aiSoundModel = new QLineEdit;
	m_aiSoundModel->setObjectName(QStringLiteral("aiSoundModel"));
	m_aiSoundModel->setAccessibleName(tr("Sound model name"));
	m_aiSoundModel->setToolTip(tr("The sound model's name as the provider lists it; empty takes the provider's default."));
	layout->addRow(tr("Model"), m_aiSoundModel);
	m_aiSoundEndpoint = new QLineEdit;
	m_aiSoundEndpoint->setObjectName(QStringLiteral("aiSoundEndpoint"));
	m_aiSoundEndpoint->setAccessibleName(tr("Sound endpoint address"));
	m_aiSoundEndpoint->setToolTip(tr("Leave empty for the provider's own address. A custom endpoint takes ElevenLabs' sound-generation request."));
	layout->addRow(tr("Endpoint"), m_aiSoundEndpoint);
	m_aiSoundStatus = new QLabel;
	m_aiSoundStatus->setObjectName(QStringLiteral("aiSoundStatus"));
	m_aiSoundStatus->setWordWrap(true);
	m_aiSoundStatus->setAccessibleName(tr("Sound connection status"));
	layout->addRow(tr("Status"), m_aiSoundStatus);
	connect(m_aiSoundConnector, &QComboBox::currentIndexChanged, this, [this]() { refreshAiSoundModelFields(); });
	connect(m_aiSoundModel, &QLineEdit::editingFinished, this, [this]() { saveAiSoundModelFields(); });
	connect(m_aiSoundEndpoint, &QLineEdit::editingFinished, this, [this]() { saveAiSoundModelFields(); });
	refreshAiSoundModelFields();
	return panel;
}

void ApplicationShell::refreshAiSoundModelFields()
{
	if (!m_aiSoundConnector || !m_aiSoundModel || !m_aiSoundEndpoint || !m_aiSoundStatus) {
		return;
	}
	const QString id = m_aiSoundConnector->currentData().toString();
	const AiAutomationPreferences preferences = m_settings.aiAutomationPreferences();
	const QSignalBlocker modelBlocker(m_aiSoundModel);
	const QSignalBlocker endpointBlocker(m_aiSoundEndpoint);
	m_aiSoundModel->setText(preferences.connectorAudioModels.value(id));
	const QString suggested = aiSuggestedSoundModel(id);
	m_aiSoundModel->setPlaceholderText(suggested.isEmpty() ? tr("The provider's default") : suggested);
	m_aiSoundEndpoint->setText(preferences.connectorAudioEndpoints.value(id));
	const QString defaultEndpoint = aiDefaultSoundEndpoint(id);
	m_aiSoundEndpoint->setPlaceholderText(defaultEndpoint.isEmpty() ? tr("https://… (required)") : defaultEndpoint);
	// Which connector the Sound Generator will use, and what stops it; the
	// synthesizer needs none of this.
	const AiSoundConnection active = resolveAiSoundConnection(preferences);
	m_aiSoundStatus->setText(QStringLiteral("%1\n%2").arg(aiSoundConnectionBlockText(active),
		tr("Choose the connector under Connectors > Audio. The synthesizer makes sounds without any of this.")));
	m_aiSoundStatus->setAccessibleDescription(m_aiSoundStatus->text());
}

void ApplicationShell::saveAiSoundModelFields()
{
	if (!m_aiSoundConnector || !m_aiSoundModel || !m_aiSoundEndpoint) {
		return;
	}
	const QString id = m_aiSoundConnector->currentData().toString();
	AiAutomationPreferences preferences = m_settings.aiAutomationPreferences();
	const QString model = m_aiSoundModel->text().trimmed();
	const QString endpoint = m_aiSoundEndpoint->text().trimmed();
	if (preferences.connectorAudioModels.value(id) == model && preferences.connectorAudioEndpoints.value(id) == endpoint) {
		return;
	}
	model.isEmpty() ? void(preferences.connectorAudioModels.remove(id)) : void(preferences.connectorAudioModels.insert(id, model));
	endpoint.isEmpty() ? void(preferences.connectorAudioEndpoints.remove(id)) : void(preferences.connectorAudioEndpoints.insert(id, endpoint));
	m_settings.setAiAutomationPreferences(preferences);
	m_settings.sync();
	refreshAiSoundModelFields();
	if (m_soundGenerationDialog) {
		m_soundGenerationDialog->refreshSourceStatus();
	}
	statusBar()->showMessage(tr("Saved the %1 sound model settings.").arg(m_aiSoundConnector->currentText()), 4000);
}

} // namespace vibestudio
