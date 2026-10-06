#include "app/application_shell.h"
#include "app/asset_views.h"
#include "app/model_editor_dialog.h"
#include "app/model_preview_worker.h"
#include "app/studio_actions.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"

#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QStatusBar>
#include <QToolButton>

namespace vibestudio {

void ApplicationShell::registerAssetWorkbenchCommands()
{
	const auto add = [this](const QString& id, const QString& section, const QString& label,
		const QString& description, const QString& icon, std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id; command.group = StudioCommandGroup::Tools;
		command.menuSection = section; command.label = label; command.statusTip = description;
		command.iconName = icon; command.handler = std::move(handler);
		m_commands->registerCommand(command);
	};
	add("texture.editor", tr("Textures"), tr("Texture Editor"), tr("Create or open a texture document, then export or stage it for the project."), "image",
		[this] { setMode(StudioMode::Textures); showTextureEditor(); });
	add("texture.edit", tr("Textures"), tr("Edit Selected Texture"), tr("Edit the displayed mip level or sprite frame with its native export metadata."), "edit",
		[this] { setMode(StudioMode::Textures); showTextureEditor(true); });
	add("texture.export", tr("Textures"), tr("Export Texture as PNG…"), tr("Export the displayed texture as a separate PNG image."), "export",
		[this] { setMode(StudioMode::Textures); exportSelectedTexture(); });
	add("model.editor", tr("Models"), tr("Mesh Editor"), tr("Edit the selected model or resume the open mesh document. With no model selected, start a new mesh."), "cube",
		[this] { setMode(StudioMode::Models); showModelEditor(); });
	add("model.design", tr("Models"), tr("Prop Designer"), tr("Build a prop from primitives, then export, stage or place it in a level."), "add",
		[this] { setMode(StudioMode::Models); showModelDesign(); });
	add("model.assembly", tr("Models"), tr("Model Assembly"), tr("Link models at named tags, save the assembly or bake a pose for editing."), "layers",
		[this] { setMode(StudioMode::Models); showModelAssembly(); });
	add("model.export", tr("Models"), tr("Export &Model Frame…"), tr("Write the selected model's current frame to a Wavefront OBJ file."), "export",
		[this] { setMode(StudioMode::Models); exportSelectedModel(); });
	add("audio.open", tr("Audio"), tr("Open Audio…"), tr("Open an audio file, clip project or multitrack session for editing."), "folder-open",
		[this] { setMode(StudioMode::Audio); showAudioEditor(false); });
	add("audio.edit", tr("Audio"), tr("Edit Selected Sound"), tr("Edit the selected package sound, then export or stage the result."), "edit",
		[this] { setMode(StudioMode::Audio); showAudioEditor(true); });
	add("audio.session", tr("Audio"), tr("Multitrack Session"), tr("Create or resume a multitrack audio session."), "waveform",
		[this] { setMode(StudioMode::Audio); showAudioSession(); });
	add("audio.export", tr("Audio"), tr("Export Sound as WAV…"), tr("Export the selected sound as a separate PCM WAV file."), "export",
		[this] { setMode(StudioMode::Audio); exportSelectedAudio(); });
	for (const auto mode : {StudioMode::Textures, StudioMode::Models, StudioMode::Audio}) {
		const auto id = mode == StudioMode::Textures ? QStringLiteral("texture.reveal") : mode == StudioMode::Models ? QStringLiteral("model.reveal") : QStringLiteral("audio.reveal");
		const auto section = mode == StudioMode::Textures ? tr("Textures") : mode == StudioMode::Models ? tr("Models") : tr("Audio");
		add(id, section, tr("Show Selected Asset in Package"), tr("Return to the exact package entry to review its source and staged changes."), "package",
			[this, mode] { revealWorkbenchAsset(mode); });
	}
}

QString ApplicationShell::selectedWorkbenchAssetPath(StudioMode mode) const
{
	const auto* list = mode == StudioMode::Textures ? m_textureEntries : mode == StudioMode::Models ? m_modelEntries : mode == StudioMode::Audio ? m_audioEntries : nullptr;
	const auto* item = list ? list->currentItem() : nullptr;
	return item && item->isSelected() && !item->isHidden() && item->flags().testFlag(Qt::ItemIsSelectable)
		? item->data(Qt::UserRole).toString() : QString();
}

void ApplicationShell::refreshAssetWorkbenchCommands()
{
	if (!m_commands) { return; }
	const bool packageReady = m_packageStaging.isLoaded() && !m_packageReadRunning && !m_packageSaveRunning && !m_packageExtractionRunning;
	const bool texture = packageReady && !selectedWorkbenchAssetPath(StudioMode::Textures).isEmpty() &&
		!m_texturePreviewPending && m_texturePreview && m_texturePreview->hasImage();
	m_commands->setEnabled("texture.edit", texture);
	m_commands->setEnabled("texture.export", texture);
	m_commands->setEnabled("model.editor", m_modelEditorDialog || !m_modelPreview || !m_modelPreview->busy());
	m_commands->setEnabled("model.export", packageReady && !selectedWorkbenchAssetPath(StudioMode::Models).isEmpty() &&
		m_modelMesh.geometryAvailable && !m_modelExportRunning && (!m_modelPreview || !m_modelPreview->busy()));
	const bool audio = packageReady && !selectedWorkbenchAssetPath(StudioMode::Audio).isEmpty() && !m_audioPreviewPending && m_audioSelectedEditable;
	m_commands->setEnabled("audio.edit", audio);
	m_commands->setEnabled("audio.export", audio);
	for (const auto mode : {StudioMode::Textures, StudioMode::Models, StudioMode::Audio}) {
		const auto id = mode == StudioMode::Textures ? QStringLiteral("texture.reveal") : mode == StudioMode::Models ? QStringLiteral("model.reveal") : QStringLiteral("audio.reveal");
		m_commands->setEnabled(id, packageReady && !selectedWorkbenchAssetPath(mode).isEmpty());
		if (auto* header = m_pageHeaders.value(int(mode))) {
			const auto path = header->property("assetViewReady").toBool() ? selectedWorkbenchAssetPath(mode) : QString();
			const auto package = QFileInfo(packageOpenPath()).fileName();
			const auto summary = path.isEmpty() ? header->property("assetSummary").toString()
				: (package.isEmpty() ? tr("Untitled package") : package) + QStringLiteral("  ·  ") + path;
			if (!summary.isEmpty() && header->subtitleLabel()->fullText() != summary) { header->setSubtitle(summary); }
		}
	}
	refreshWorkspaceTiles();
}

void ApplicationShell::addAssetPackageMenu(PageHeader* header, StudioMode mode)
{
	const auto id = mode == StudioMode::Textures ? QStringLiteral("texture.reveal") : mode == StudioMode::Models ? QStringLiteral("model.reveal") : QStringLiteral("audio.reveal");
	auto* button = new QToolButton;
	button->setObjectName(id + QStringLiteral(".packageMenu"));
	button->setText(tr("Package")); button->setIcon(studioIcon(QStringLiteral("package")));
	button->setToolButtonStyle(Qt::ToolButtonIconOnly);
	button->setPopupMode(QToolButton::InstantPopup);
	button->setAccessibleName(tr("Package actions"));
	button->setAccessibleDescription(tr("Show the selected source, review staged changes, or save the package draft."));
	button->setToolTip(tr("Package actions") + QLatin1Char('\n') + button->accessibleDescription());
	button->setFocusPolicy(Qt::StrongFocus);
	auto* menu = new QMenu(button);
	menu->addAction(m_commands->action(id));
	menu->addSeparator();
	menu->addAction(m_commands->action(QStringLiteral("package.reviewChanges")));
	menu->addAction(m_commands->action(QStringLiteral("package.saveDraft")));
	button->setMenu(menu);
	const auto sync = [button, menu] {
		bool available = false;
		for (const auto* action : menu->actions()) { available |= !action->isSeparator() && action->isEnabled(); }
		button->setEnabled(available);
	};
	for (auto* action : menu->actions()) { connect(action, &QAction::changed, button, sync); }
	sync(); header->addActionWidget(button);
}

void ApplicationShell::revealWorkbenchAsset(StudioMode mode)
{
	const auto path = selectedWorkbenchAssetPath(mode);
	if (path.isEmpty() || !packageViewArchive().isOpen()) { return; }
	qint64 ordinal = -1;
	if (mode == StudioMode::Audio && m_audioEntries->currentItem()->data(Qt::UserRole + 7).isValid()) {
		ordinal = m_audioEntries->currentItem()->data(Qt::UserRole + 7).toLongLong();
	}
	int matches = 0;
	for (const auto& entry : packageViewArchive().entries()) {
		if (entry.virtualPath == path && (ordinal < 0 || entry.sourceOrdinal == ordinal)) { ++matches; }
	}
	if (matches != 1) {
		statusBar()->showMessage(tr("The asset is unavailable or has several package entries. Select its exact occurrence in Packages.")); return;
	}
	revealPackageEntry(path, ordinal);
}

} // namespace vibestudio
