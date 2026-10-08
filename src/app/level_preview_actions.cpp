#include "app/application_shell.h"
#include "app/level_preview_worker.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_layout.h"

#include <QAction>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace vibestudio
{

QWidget *ApplicationShell::buildLevelPreviewStatus()
{
	m_levelPreviewStatus = new QWidget;
	m_levelPreviewStatus->setObjectName(QStringLiteral("levelPreviewStatus"));
	auto *layout = new QHBoxLayout(m_levelPreviewStatus);
	layout->setContentsMargins(6, 3, 6, 3);
	m_levelPreviewLabel = new QLabel(tr("Open a package or asset folder for material previews."));
	m_levelPreviewLabel->setObjectName(QStringLiteral("levelPreviewLabel"));
	m_levelPreviewLabel->setAccessibleName(tr("Level material preview status"));
	m_levelPreviewLabel->setWordWrap(true);
	layout->addWidget(m_levelPreviewLabel, 1);
	m_levelPreviewProgress = new QProgressBar;
	m_levelPreviewProgress->setAccessibleName(tr("Loading level materials"));
	m_levelPreviewProgress->setMaximumWidth(100);
	m_levelPreviewProgress->hide();
	layout->addWidget(m_levelPreviewProgress);
	auto *textured = new QCheckBox(tr("Textures"));
	textured->setObjectName(QStringLiteral("levelPreviewTextured"));
	textured->setAccessibleName(tr("Show textures in the level camera"));
	textured->setToolTip(tr("Use package images and static shader previews on brushes, patches and placed models."));
	const bool showTextures = m_settings.shellLayoutState(QStringLiteral("levelPreviewTextured")) != QByteArrayLiteral("off");
	textured->setChecked(showTextures);
	m_levelPreviewTextured = new QAction(tr("Textures in Camera"), this);
	m_levelPreviewTextured->setToolTip(textured->toolTip());
	m_levelPreviewTextured->setCheckable(true);
	m_levelPreviewTextured->setChecked(showTextures);
	connect(textured, &QCheckBox::toggled, m_levelPreviewTextured, &QAction::setChecked);
	connect(m_levelPreviewTextured, &QAction::toggled, this, [this](bool enabled) {
		m_settings.setShellLayoutState(QStringLiteral("levelPreviewTextured"),
									   enabled ? QByteArrayLiteral("on") : QByteArrayLiteral("off"));
		if (m_levelMap3D) {
			m_levelMap3D->setRenderMode(enabled ? ModelViewportRenderMode::Textured : ModelViewportRenderMode::FlatShaded);
		}
	});
	layout->addWidget(textured);
	const auto button = [&](const QString &name, const QString &text, const QString &help) {
		auto *control = new QToolButton;
		control->setObjectName(name);
		control->setText(text);
		control->setAccessibleName(text);
		control->setToolTip(help);
		layout->addWidget(control);
		return control;
	};
	auto *reload = button(QStringLiteral("levelPreviewReload"), tr("Reload"),
						  tr("Reload material images from the current package and staged changes."));
	connect(reload, &QToolButton::clicked, this, [this] {
		++m_levelPreviewReload;
		refreshLevelMap3D();
	});
	auto *details = button(QStringLiteral("levelPreviewDetails"), tr("Details"),
						   tr("Inspect image sources, original dimensions and unsupported material effects."));
	connect(details, &QToolButton::clicked, this, &ApplicationShell::showLevelMaterialDetails);
	m_levelPreviewCancel = button(QStringLiteral("levelPreviewCancel"), tr("Cancel"), tr("Cancel loading the level preview."));
	m_levelPreviewCancel->hide();
	connect(m_levelPreviewCancel, &QToolButton::clicked, this, [this] {
		if (m_levelPreviewWorker) {
			m_levelPreviewWorker->cancel();
		}
	});
	m_levelPreviewStatus->hide();
	return m_levelPreviewStatus;
}

void ApplicationShell::showLevelMaterialDetails()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Level Materials"));
	dialog.setObjectName(QStringLiteral("levelMaterialDetails"));
	dialog.resize(760, 520);
	auto *layout = new QVBoxLayout(&dialog);
	auto *text = new QPlainTextEdit(levelPreviewAssetsText(m_levelPreviewAssets));
	text->setReadOnly(true);
	text->setAccessibleName(tr("Level material resolution details"));
	text->setAccessibleDescription(tr("Read-only material and model appearance results, including source files, skin hashes, omitted surfaces and diagnostics."));
	text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	layout->addWidget(text);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}

void ApplicationShell::refreshLevelMap3D()
{
	if (!m_levelMap3D) {
		return;
	}
	if (m_levelPreviewStatus) {
		m_levelPreviewStatus->setVisible(levelMap3DShowing());
	}
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		if (m_levelPreviewWorker) {
			m_levelPreviewWorker->reset();
		}
		m_levelMap3D->clearSkin();
		m_levelMap3D->clearMesh();
		m_levelMap3D->setEnabled(true);
		m_levelMap3DSource.clear();
		m_levelPreviewRequestKey.clear();
		m_levelPreviewAssets = {};
		m_levelMap3DOwners.clear();
		m_levelMap3DFaces.clear();
		rebuildLevelMaterialTargets({});
		if (m_levelPreviewProgress) {
			m_levelPreviewProgress->hide();
			m_levelPreviewCancel->hide();
		}
		return;
	}
	if (!m_levelPreviewWorker) {
		m_levelPreviewWorker = new LevelPreviewWorker(this);
		m_levelPreviewWorker->started = [this] {
			// Picking old triangles against a changed document can edit the wrong
			// object. Retain the picture and camera, but wait for current owners.
			m_levelMap3D->setEnabled(false);
			refreshCommandEnablement();
			if (m_levelPreviewLabel) {
				m_levelPreviewLabel->setText(tr("Loading level materials and geometry…"));
				m_levelPreviewProgress->setRange(0, 0);
				m_levelPreviewProgress->show();
				m_levelPreviewCancel->show();
			}
		};
		m_levelPreviewWorker->progress = [this](int done, int total) {
			if (m_levelPreviewProgress) {
				m_levelPreviewProgress->setRange(0, total);
				m_levelPreviewProgress->setValue(done);
			}
		};
		m_levelPreviewWorker->completed = [this](const LevelPreviewResult &result) {
			if (result.loadSerial != m_levelMapLoadSerial || result.revision != m_levelMapDocument.revision) {
				return;
			}
			m_levelPreviewProgress->hide();
			m_levelPreviewCancel->hide();
			if (m_levelBookmarkCameraPending) {
				// Plan gestures can still drive the camera while mesh picking is
				// disabled. Preserve that newer navigation through mesh framing.
				const auto latest = m_levelMap3D->navigationState();
				if (validateCameraViewState(latest)) { m_levelBookmarkCamera = latest; }
			}
			if (result.assets.cancelled || !result.error.isEmpty()) {
				m_levelPreviewRequestKey.clear();
				m_levelMap3DOwners.clear();
				m_levelMap3DFaces.clear();
				rebuildLevelMaterialTargets({});
				m_levelMap3D->clearSkin();
				m_levelMap3D->clearMesh();
				m_levelMap3D->setEnabled(true);
				if (m_levelBookmarkCameraPending) { m_levelMap3D->restoreNavigationState(m_levelBookmarkCamera); }
				m_levelPreviewLabel->setText(result.assets.cancelled ? tr("Level preview cancelled. Reload to try again.")
																	 : tr("Level preview failed: %1").arg(result.error));
				refreshCommandEnablement();
				return;
			}
			m_levelPreviewAssets = result.assets;
			m_levelPreviewAssets.warnings += result.preview.warnings;
			m_levelMap3D->setBackfaceCulling(true);
			m_levelMap3D->clearSkin();
			m_levelMap3D->setMesh(result.preview.mesh, result.sourceKey == m_levelMap3DSource);
			m_levelMap3D->setSurfaceSkins(levelPreviewSurfaceImages(result.preview.mesh, result.assets));
			m_levelMap3D->setRenderMode(m_levelPreviewTextured && m_levelPreviewTextured->isChecked()
											? ModelViewportRenderMode::Textured
											: ModelViewportRenderMode::FlatShaded);
			m_levelMap3DSource = result.sourceKey;
			m_levelMap3DOwners = result.preview.owners;
			m_levelMap3DFaces = result.preview.ownerFaces;
			rebuildLevelMaterialTargets(result.preview.materialTargets);
			m_levelMap3D->setEnabled(true);
			refreshLevelMap3DHighlight();
			applyPendingLevelBookmarkCamera();
			refreshLevelCameraMarker();
			if (m_levelMapHover && levelMap3DShowing()) {
				m_levelMapHover->setText(m_levelMap3D->hoverSummary());
			}
			QString status = tr("Materials %1/%2 · Models %3/%4")
								 .arg(result.assets.readyCount())
								 .arg(result.assets.requestedMaterials)
								 .arg(result.assets.models.size())
								 .arg(result.assets.requestedModels);
			if (!result.assets.complete || result.assets.problemCount() > 0 || !result.assets.warnings.isEmpty() || !result.preview.warnings.isEmpty()) {
				status += tr(" · See Details");
			}
			if (result.preview.truncated) {
				status += tr(" · First %1 triangles").arg(result.preview.triangles);
			}
			m_levelPreviewLabel->setText(status);
			m_levelPreviewLabel->setToolTip(result.assets.sourcePath.isEmpty() ? tr("Open a package or asset folder for material previews.")
																			   : result.assets.sourcePath);
			refreshLevelMapTextures();
			refreshCommandEnablement();
		};
	}
	const bool filtered = m_levelMapViewport && (m_levelMapViewport->hiddenCount() > 0 || m_levelMapViewport->filteredCount() > 0);
	LevelPreviewRequest request;
	request.document = filtered ? m_levelMapViewport->displayDocument() : m_levelMapDocument;
	request.options.paletteId = activePaletteId();
	request.sourceKey = QString::number(m_levelMapLoadSerial);
	request.loadSerial = m_levelMapLoadSerial;
	request.assetKey = m_packageArchive.sourcePath() + QLatin1Char('|') + QString::number(m_packageStaging.revision()) + QLatin1Char('|') +
					   QString::number(m_levelPreviewReload);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData((request.sourceKey + QLatin1Char('|') + request.document.sourcePath + QLatin1Char('|') + request.document.mapName +
				  QLatin1Char('|') + QString::number(request.document.revision) + QLatin1Char('|') + request.assetKey + QLatin1Char('|') +
				  request.options.paletteId)
					 .toUtf8());
	// Hidden IDs, not only their count: two equally sized isolates differ.
	if (filtered) {
		const auto add = [&hash](char kind, int id) { hash.addData(QByteArray(1, kind) + QByteArray::number(id) + ','); };
		for (const auto &brush : request.document.brushes) {
			add('b', brush.id);
		}
		for (const auto &patch : request.document.patches) {
			add('p', patch.id);
		}
		for (const auto &entity : request.document.entities) {
			add('e', entity.id);
		}
		for (const auto &line : request.document.doomLinedefs) {
			add('l', line.id);
		}
	}
	const QString key = QString::fromLatin1(hash.result().toHex());
	if (key == m_levelPreviewRequestKey) {
		refreshLevelMap3DHighlight();
		return;
	}
	m_levelPreviewRequestKey = key;
	m_levelPreviewAssets = {}; // Thumbnails must not keep a previous package's images.
	request.archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!request.archive && m_packageArchive.isOpen()) { request.archive = std::make_shared<const PackageArchive>(m_packageArchive); }
	m_levelPreviewWorker->request(std::move(request));
}
} // namespace vibestudio
