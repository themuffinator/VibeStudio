#include "app/level_merge_dialog.h"
#include "app/model_viewport.h"
#include "core/studio_settings.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio
{
struct LevelMergeDialog::Work {
	LevelBrushMergePlan plan;
	LevelPreviewAssets assets;
	LevelMapPreviewMesh mesh;
	QString error;
	std::atomic_bool cancelled{false};
	quint64 generation = 0;
	bool prepared = false, assetsReady = false, sourceView = false;
};
LevelMergeDialog::LevelMergeDialog(const LevelMapDocument &document, std::shared_ptr<const PackageArchiveReader> archive,
								   const QString &palette, QWidget *parent)
	: QDialog(parent), m_source(document), m_visibleSource(document), m_archive(std::move(archive)), m_palette(palette)
{
	setObjectName(QStringLiteral("mergeBrushesDialog"));
	setWindowTitle(tr("Merge Brushes"));
	setAccessibleName(windowTitle());
	resize(1150, 780);
	QSet<int> brushes, entities;
	for (const auto &ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes.insert(ref.objectId);
		}
		if (ref.kind == LevelMapSelectionKind::Entity) {
			entities.insert(ref.objectId);
		}
	}
	m_visibleSource.brushes.erase(std::remove_if(m_visibleSource.brushes.begin(), m_visibleSource.brushes.end(),
												 [&](const auto &b) { return !brushes.contains(b.id) && !entities.contains(b.entityId); }),
								  m_visibleSource.brushes.end());
	m_visibleSource.entities.clear();
	m_visibleSource.patches.clear();
	auto *layout = new QVBoxLayout(this);
	auto *splitter = new QSplitter(this);
	splitter->setChildrenCollapsible(false);
	layout->addWidget(splitter, 1);
	m_scroll = new QScrollArea(splitter);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_controls = new QWidget;
	auto *column = new QVBoxLayout(m_controls);
	m_faces = new QListWidget(m_controls);
	m_faces->setObjectName(QStringLiteral("mergeFaces"));
	m_faces->setAccessibleName(tr("Merged surfaces"));
	m_faces->setAccessibleDescription(
		tr("Conflicting surfaces require an explicit source choice. Materials, UVs and flags are kept together."));
	m_faces->setMinimumHeight(170);
	column->addWidget(m_faces, 1);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(form);
	m_sourceFace = new QComboBox(m_controls);
	m_sourceFace->setObjectName(QStringLiteral("mergeSource"));
	m_sourceFace->setAccessibleName(tr("Source for selected surface"));
	m_sourceFace->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_sourceFace->setMinimumContentsLength(18);
	m_sourceFace->setToolTip(tr("Keep this face's material, texture coordinates and surface flags across the entire merged surface."));
	form->addRow(tr("Keep surface from"), m_sourceFace);
	m_surface = new QLabel(m_controls);
	m_surface->setAccessibleName(tr("Selected surface details"));
	m_surface->setTextFormat(Qt::PlainText);
	m_surface->setWordWrap(true);
	m_surface->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_surface->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_surface);
	m_view = new QComboBox(m_controls);
	m_view->setObjectName(QStringLiteral("mergeView"));
	m_view->setAccessibleName(tr("Merge preview view"));
	m_view->addItems({tr("Merged brush"), tr("Source brushes")});
	form->addRow(tr("Preview"), m_view);
	auto *frame = new QPushButton(tr("Frame brushes"), m_controls);
	frame->setAccessibleName(frame->text());
	column->addWidget(frame);
	auto *details = new QPushButton(tr("Merge and material details"), m_controls);
	details->setAccessibleName(details->text());
	column->addWidget(details);
	connect(details, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Merge and material details"));
		dialog.resize(760, 500);
		auto *body = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(&dialog);
		text->setReadOnly(true);
		text->setAccessibleName(tr("Merge report and resolved asset paths"));
		text->setPlainText(QString::fromUtf8(QJsonDocument(levelBrushMergeReportJson(m_plan)).toJson()) + '\n' +
						   levelPreviewAssetsText(m_assets));
		body->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		body->addWidget(buttons);
		dialog.exec();
	});
	m_status = new QLabel(m_controls);
	m_status->setObjectName(QStringLiteral("mergeStatus"));
	m_status->setAccessibleName(tr("Brush merge status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_status);
	m_progress = new QProgressBar(m_controls);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Checking merge and loading materials"));
	column->addWidget(m_progress);
	m_scroll->setWidget(m_controls);
	m_preview = new ModelViewport(splitter);
	m_preview->setObjectName(QStringLiteral("mergePreview"));
	m_preview->setAccessibleName(tr("Brush merge preview"));
	m_preview->setAccessibleDescription(
		tr("The merged solid or its source brushes with package material images. Unresolved surfaces use the first source until chosen."));
	m_preview->setShowEdges(true);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	StudioSettings settings;
	m_preview->setHighContrast(settings.accessibilityPreferences().theme == StudioTheme::HighContrastDark ||
							   settings.accessibilityPreferences().theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(settings.accessibilityPreferences().reducedMotion);
	connect(frame, &QPushButton::clicked, m_preview, &ModelViewport::frameModel);
	connect(m_preview, &ModelViewport::triangleClicked, this, [this](int, int triangle) {
		if (!m_ready || triangle < 0 || triangle >= m_mesh.ownerFaces.size()) {
			return;
		}
		if (!m_sourceView) {
			m_faces->setCurrentRow(m_mesh.ownerFaces[triangle]);
			return;
		}
		const LevelBrushMergeSource source{m_mesh.owners[triangle].objectId, m_mesh.ownerFaces[triangle]};
		for (int f = 0; f < m_plan.geometry().faces.size(); ++f) {
			if (m_plan.geometry().faces[f].sources.contains(source)) {
				m_faces->setCurrentRow(f);
				break;
			}
		}
	});
	updateControlWidth();
	splitter->setSizes({m_scroll->minimumWidth(), 750});
	splitter->setStretchFactor(1, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setText(tr("Merge Brushes"));
	m_apply->setAccessibleName(tr("Apply brush merge"));
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelMergeDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(120);
	connect(m_debounce, &QTimer::timeout, this, &LevelMergeDialog::startPreview);
	connect(m_faces, &QListWidget::currentRowChanged, this, [this] { showSources(); });
	connect(m_sourceFace, &QComboBox::currentIndexChanged, this, [this](int index) {
		const int f = m_faces->currentRow();
		if (f < 0 || f >= m_plan.geometry().faces.size()) {
			return;
		}
		const int source = m_sourceFace->itemData(index).toInt();
		if (source < 0) {
			m_request.faceSources.remove(f);
		} else {
			m_request.faceSources.insert(f, m_plan.geometry().faces[f].sources[source]);
		}
		schedule();
	});
	connect(m_view, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	schedule();
}
LevelMergeDialog::~LevelMergeDialog()
{
	if (m_work) {
		m_work->cancelled = true;
	}
}
void LevelMergeDialog::setRequest(const LevelBrushMergeRequest &request)
{
	m_request = request;
	schedule();
}
void LevelMergeDialog::setApplyHandler(std::function<bool(const LevelBrushMergePlan &, QString *)> handler)
{
	m_applyHandler = std::move(handler);
}
void LevelMergeDialog::schedule()
{
	++m_generation;
	m_ready = false;
	m_apply->setEnabled(false);
	m_progress->show();
	m_status->setText(tr("Checking merge and loading materials…"));
	if (m_work) {
		m_work->cancelled = true;
	}
	m_debounce->start();
}
void LevelMergeDialog::updateControlWidth()
{
	if (!m_controls || !m_scroll) {
		return;
	}
	m_controls->layout()->activate();
	m_scroll->setMinimumWidth(
		std::max(310, m_controls->minimumSizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 12));
}
void LevelMergeDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::LayoutDirectionChange) {
		updateControlWidth();
	}
}
void LevelMergeDialog::showSources()
{
	const QSignalBlocker blocker(m_sourceFace);
	m_sourceFace->clear();
	m_surface->clear();
	const int f = m_faces->currentRow();
	m_sourceFace->setEnabled(f >= 0 && f < m_plan.geometry().faces.size());
	if (!m_sourceFace->isEnabled()) {
		return;
	}
	const auto &face = m_plan.geometry().faces[f];
	QVector<int> highlighted;
	for (int t = 0; t < m_mesh.owners.size(); ++t) {
		const auto owner = m_mesh.owners[t];
		if ((!m_sourceView && m_mesh.ownerFaces[t] == f) ||
			(m_sourceView && face.sources.contains({owner.objectId, m_mesh.ownerFaces[t]}))) {
			highlighted << t;
		}
	}
	m_preview->setHighlightedTriangles(highlighted);
	if (!face.resolved) {
		m_sourceFace->addItem(tr("Choose a source…"), -1);
	}
	for (int s = 0; s < face.sources.size(); ++s) {
		const auto ref = face.sources[s];
		const auto brush =
			std::find_if(m_source.brushes.cbegin(), m_source.brushes.cend(), [&](const auto &b) { return b.id == ref.brushId; });
		const auto &source = brush->faces[ref.faceIndex];
		const auto label = tr("Brush %1 · face %2 · %3").arg(ref.brushId).arg(ref.faceIndex + 1).arg(source.textureName);
		m_sourceFace->addItem(label, s);
		m_sourceFace->setItemData(m_sourceFace->count() - 1, label, Qt::ToolTipRole);
		if (face.resolved && face.chosen == ref) {
			m_sourceFace->setCurrentIndex(m_sourceFace->count() - 1);
		}
	}
	const auto &source = m_plan.brush().faces[f];
	m_surface->setText(
		(face.resolved ? tr("Material: %1") : tr("Preview only: %1")).arg(source.textureName) + '\n' +
		tr("Contents %1 · surface %2 · value %3").arg(source.contentFlags).arg(source.surfaceFlags).arg(source.surfaceValue));
}
void LevelMergeDialog::startPreview()
{
	if (m_thread) {
		return;
	}
	auto work = std::make_shared<Work>();
	work->generation = m_generation;
	work->assets = m_assets;
	m_work = work;
	const auto source = m_source, visible = m_visibleSource;
	const auto request = m_request;
	const auto archive = m_archive;
	const auto palette = m_palette;
	const bool sourceView = m_view->currentIndex() == 1, cached = m_assetsReady;
	m_thread = QThread::create([work, source, visible, request, archive, palette, sourceView, cached] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		work->prepared = prepareLevelBrushMerge(source, request, &work->plan, &work->error, cancelled);
		if (cancelled()) {
			return;
		}
		if (archive && !cached) {
			LevelPreviewAssetOptions options;
			options.paletteId = palette;
			work->assets = resolveLevelPreviewAssets(visible, *archive, options, [cancelled](int, int) { return !cancelled(); });
		}
		work->assetsReady = !cancelled() && !work->assets.cancelled;
		if (cancelled()) {
			return;
		}
		auto preview = visible;
		if (work->prepared && !sourceView) {
			preview.brushes = {work->plan.brush()};
		}
		work->sourceView = sourceView || !work->prepared;
		LevelMapPreviewMeshOptions options;
		options.textureSizes = levelPreviewTextureSizes(work->assets);
		options.isCancelled = cancelled;
		work->mesh = buildLevelMapPreviewMesh(preview, options);
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread = nullptr;
		if (work->generation != m_generation) {
			if (!m_debounce->isActive()) {
				startPreview();
			}
			return;
		}
		m_ready = true;
		m_progress->hide();
		m_plan = work->plan;
		m_mesh = work->mesh;
		m_sourceView = work->sourceView;
		m_assets = work->assets;
		m_assetsReady = work->assetsReady;
		m_apply->setEnabled(m_plan.ready());
		m_preview->setMesh(work->mesh.mesh, m_preview->hasMesh());
		m_preview->setSurfaceSkins(levelPreviewSurfaceImages(work->mesh.mesh, m_assets));
		const int previous = m_faces->currentRow();
		{
			const QSignalBlocker blocker(m_faces);
			m_faces->clear();
			for (int f = 0; f < m_plan.geometry().faces.size(); ++f) {
				const auto &face = m_plan.geometry().faces[f];
				m_faces->addItem(tr("Face %1 — %2")
									 .arg(f + 1)
									 .arg(!face.resolved  ? tr("Choose source")
										  : face.conflict ? tr("Source chosen")
														  : tr("Compatible")));
			}
			m_faces->setCurrentRow(std::clamp(previous, 0, std::max(0, m_faces->count() - 1)));
		}
		showSources();
		QString status = work->error;
		if (work->prepared) {
			status = tr("%1 brushes → 1 solid. %2 source faces → %3 exterior faces.")
						 .arg(m_plan.brushCount())
						 .arg(m_plan.geometry().sourceFaceCount)
						 .arg(m_plan.brush().faceCount);
			status += '\n' + (m_plan.ready() ? tr("Ready to merge. Internal faces will be removed.")
											 : tr("Choose sources for %1 conflicting surfaces.").arg(m_plan.geometry().unresolvedCount()));
			if (!m_archive) {
				status += '\n' + tr("Open a package or asset folder for material images.");
			}
		}
		m_status->setText(status);
		updateControlWidth();
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}
void LevelMergeDialog::accept()
{
	if (!m_ready || !m_plan.ready()) {
		return;
	}
	QString error;
	if (m_applyHandler && !m_applyHandler(m_plan, &error)) {
		m_status->setText(error);
		return;
	}
	QDialog::accept();
}
} // namespace vibestudio
