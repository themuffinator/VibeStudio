#include "app/level_wrap_dialog.h"
#include "app/model_viewport.h"
#include "core/studio_settings.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio {
struct LevelWrapDialog::Work {
	std::atomic_bool cancelled{false};
	quint64 generation = 0;
	LevelPlacementResult result;
	LevelMapPreviewMesh sourceMesh, resultMesh;
	LevelPreviewAssets assets;
};
LevelWrapDialog::LevelWrapDialog(const LevelMapDocument& source,const LevelWrapRequest& request,
	std::shared_ptr<const PackageArchiveReader> archive,const QString& palette,QWidget* parent)
	: QDialog(parent),m_source(source),m_request(request),m_archive(std::move(archive)),m_palette(palette)
{
	setObjectName(QStringLiteral("levelWrapDialog")); setWindowTitle(tr("Wrap/Merge with Points")); setAccessibleName(windowTitle());
	if (parent) { setLayoutDirection(parent->layoutDirection()); } resize(1100,720);
	auto* layout = new QVBoxLayout(this); auto* splitter = new QSplitter(this); splitter->setChildrenCollapsible(false); layout->addWidget(splitter,1);
	auto* scroll = new QScrollArea(splitter); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
	auto* controls = new QWidget; auto* column = new QVBoxLayout(controls); scroll->setWidget(controls);
	auto* scope = new QLabel(request.componentsOnly ? tr("Picked components form a new brush. Original objects and picks remain.")
		: tr("Selected brushes become one convex hull. Gaps and concavities are filled."));
	scope->setWordWrap(true); scope->setAccessibleName(tr("Wrap scope")); scope->setTextFormat(Qt::PlainText); column->addWidget(scope);
	auto* form = new QFormLayout; form->setRowWrapPolicy(QFormLayout::WrapAllRows); column->addLayout(form);
	m_material = new QLineEdit(request.texture); m_material->setObjectName(QStringLiteral("wrapMaterial")); m_material->setAccessibleName(tr("New hull face material")); m_material->setMaxLength(1024);
	m_material->setToolTip(tr("All component-hull faces use this material and default UVs. Whole-brush wrapping retains exterior source surfaces."));
	form->addRow(tr("New face &material"),m_material);
	m_count = new QSpinBox; m_count->setObjectName(QStringLiteral("wrapPointCount")); m_count->setAccessibleName(tr("Extra point count")); m_count->setRange(0,3); m_count->setValue(int(request.points.size()));
	form->addRow(tr("Extra &points"),m_count);
	auto* grid = new QGridLayout; column->addLayout(grid);
	for (int row = 0; row < 3; ++row) {
		const auto initial = request.points.value(row,{0,0,0,true}); const double values[]{initial.x,initial.y,initial.z};
		for (int axis = 0; axis < 3; ++axis) {
			auto* value = new QDoubleSpinBox; m_points[row][axis] = value;
			value->setObjectName(QStringLiteral("wrapPoint%1%2").arg(row+1).arg(QLatin1Char("XYZ"[axis])));
			value->setLayoutDirection(Qt::LeftToRight);
			value->setAccessibleName(tr("Point %1 %2").arg(row+1).arg(QLatin1Char("XYZ"[axis])));
			value->setRange(-32768,32768); value->setDecimals(6); value->setSingleStep(16); value->setValue(values[axis]); value->setMinimumWidth(100);
			auto* label = new QLabel(value->accessibleName()); label->setBuddy(value);
			grid->addWidget(label,row*3+axis,0); grid->addWidget(value,row*3+axis,1);
			connect(value,&QDoubleSpinBox::valueChanged,this,[this] { schedule(); });
		}
	}
	const auto showPoints = [this,grid] { for (int row = 0; row < 9; ++row) { for (int c = 0; c < 2; ++c) { grid->itemAtPosition(row,c)->widget()->setVisible(row/3 < m_count->value()); } } };
	connect(m_count,&QSpinBox::valueChanged,this,[this,showPoints] { showPoints(); schedule(); }); showPoints();
	m_view = new QComboBox; m_view->setObjectName(QStringLiteral("wrapView")); m_view->setAccessibleName(tr("Wrap preview view")); m_view->addItems({tr("Wrapped brush"),tr("Source objects")}); form->addRow(tr("&Preview"),m_view);
	auto* frame = new QPushButton(tr("Frame geometry")); frame->setAccessibleName(frame->text()); column->addWidget(frame);
	m_status = new QLabel; m_status->setObjectName(QStringLiteral("wrapStatus")); m_status->setAccessibleName(tr("Wrap validation")); m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText); m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard|Qt::TextSelectableByMouse); layout->addWidget(m_status);
	m_progress = new QProgressBar; m_progress->setAccessibleName(tr("Checking hull and loading materials")); m_progress->setTextVisible(false); layout->addWidget(m_progress); column->addStretch();
	m_preview = new ModelViewport(splitter); m_preview->setObjectName(QStringLiteral("wrapPreview")); m_preview->setAccessibleName(tr("Wrapped brush preview"));
	m_preview->setShowEdges(true); m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	const auto preferences = StudioSettings().accessibilityPreferences(); m_preview->setReducedMotion(preferences.reducedMotion);
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_progress->setRange(0,preferences.reducedMotion ? 1 : 0);
	connect(frame,&QPushButton::clicked,m_preview,&ModelViewport::frameModel); connect(m_view,&QComboBox::currentIndexChanged,this,[this] { showMesh(); });
	splitter->setStretchFactor(0,0); splitter->setStretchFactor(1,1); scroll->setMinimumWidth(std::max(360,controls->minimumSizeHint().width()+24));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel); m_apply = buttons->addButton(tr("Apply Wrap/Merge"),QDialogButtonBox::AcceptRole);
	m_apply->setObjectName(QStringLiteral("wrapApply")); m_apply->setAccessibleName(m_apply->text()); layout->addWidget(buttons);
	connect(buttons,&QDialogButtonBox::accepted,this,&LevelWrapDialog::accept); connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
	m_timer = new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(100); connect(m_timer,&QTimer::timeout,this,[this] { start(); });
	connect(m_material,&QLineEdit::textChanged,this,[this] { schedule(); }); schedule();
}
LevelWrapDialog::~LevelWrapDialog()
{
	if (m_work) { m_work->cancelled = true; }
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
void LevelWrapDialog::schedule()
{
	++m_generation; m_ready = false; m_apply->setEnabled(false); if (m_work) { m_work->cancelled = true; }
	m_preview->clearMesh(); m_progress->show(); m_status->setText(tr("Checking hull and loading materials…"));
	m_status->setAccessibleDescription(m_status->text()); m_timer->start();
}
void LevelWrapDialog::start()
{
	if (m_thread) { return; }
	auto request = m_request; request.texture = m_material->text(); request.points.clear();
	for (int row = 0; row < m_count->value(); ++row) { request.points << LevelMapVec3{m_points[row][0]->value(),m_points[row][1]->value(),m_points[row][2]->value(),true}; }
	auto work = std::make_shared<Work>(); work->generation = m_generation; m_work = work;
	m_thread = QThread::create([work,source=m_source,request,archive=m_archive,palette=m_palette] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		LevelPlacementRequest placement; placement.operation = LevelPlacementOperation::WrapMerge; placement.wrap = request;
		work->result = prepareLevelPlacement(source,placement,{cancelled,{}});
		if (!work->result.succeeded || cancelled()) { return; }
		auto original = source; QSet<int> brushes,patches,entities;
		if (request.componentsOnly) { for (const auto& set : request.components) {
			if (set.object.kind == LevelMapSelectionKind::QuakeBrush) { brushes.insert(set.object.objectId); } else { patches.insert(set.object.objectId); }
		} } else { for (const auto& ref : source.selection) { if (ref.kind == LevelMapSelectionKind::Entity) { entities.insert(ref.objectId); } else { brushes.insert(ref.objectId); } } }
		original.brushes.removeIf([&](const auto& b) { return !brushes.contains(b.id) && !entities.contains(b.entityId); });
		original.patches.removeIf([&](const auto& p) { return !patches.contains(p.id); }); original.entities.clear();
		auto result = work->result.document; result.brushes.removeIf([&](const auto& b) { return b.id != work->result.wrapReport.brushId; }); result.patches.clear(); result.entities.clear();
		if (archive) { auto materials = original; materials.brushes += result.brushes; LevelPreviewAssetOptions options; options.paletteId = palette;
			work->assets = resolveLevelPreviewAssets(materials,*archive,options,[cancelled](int,int) { return !cancelled(); }); }
		if (cancelled()) { return; }
		LevelMapPreviewMeshOptions options; options.isCancelled = cancelled; options.textureSizes = levelPreviewTextureSizes(work->assets);
		work->sourceMesh = buildLevelMapPreviewMesh(original,options); work->resultMesh = buildLevelMapPreviewMesh(result,options);
	});
	connect(m_thread,&QThread::finished,this,[this,work] {
		m_thread->deleteLater(); m_thread = nullptr;
		if (work->generation != m_generation) { if (!m_timer->isActive()) { start(); } return; }
		m_progress->hide(); m_ready = work->result.succeeded && !work->cancelled && !work->assets.cancelled;
		m_apply->setEnabled(m_ready);
		if (m_ready) {
			const auto& r = work->result.wrapReport;
			m_status->setText(tr("Checked hull: %1 vertices, %2 faces. %3").arg(r.vertices).arg(r.faces).arg(r.sourcesRetained ? tr("Original objects remain.") : tr("Selected brushes will be replaced."))
				+ (work->assets.problemCount() ? tr(" Some materials are unavailable in the preview.") : QString{})); showMesh();
		} else { m_status->setText(work->result.error); }
		m_status->setAccessibleDescription(m_status->text());
	});
	m_thread->start();
}
void LevelWrapDialog::showMesh()
{
	if (!m_ready || !m_work) { return; }
	const auto& mesh = m_view->currentIndex() == 0 ? m_work->resultMesh : m_work->sourceMesh;
	m_preview->setAccessibleName(m_view->currentIndex() == 0 ? tr("Wrapped brush preview") : tr("Source objects preview"));
	m_preview->setMesh(mesh.mesh,m_preview->hasMesh()); m_preview->setSurfaceSkins(levelPreviewSurfaceImages(mesh.mesh,m_work->assets));
}
void LevelWrapDialog::accept()
{
	if (!m_ready || !m_work) { return; }
	QString error; if (m_applyHandler && !m_applyHandler(m_work->result.document,&error)) { m_status->setText(error); m_status->setAccessibleDescription(error); return; }
	QDialog::accept();
}
} // namespace vibestudio
