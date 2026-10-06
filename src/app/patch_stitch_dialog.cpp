#include "app/patch_stitch_dialog.h"
#include "app/model_viewport.h"
#include "core/map_geometry.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace vibestudio
{
struct PatchStitchDialog::Work {
	LevelPatchStitchResult result;
	LevelPreviewAssets assets;
	LevelMapPreviewMesh mesh;
	QVector<int> highlights;
	std::atomic_bool cancelled{false};
	quint64 generation = 0;
	QString error;
	bool valid = false;
};
PatchStitchDialog::PatchStitchDialog(const LevelMapDocument &source, std::shared_ptr<const PackageArchiveReader> archive,
									 const QString &palette, QWidget *parent)
	: QDialog(parent), m_source(source), m_archive(std::move(archive)), m_palette(palette)
{
	setObjectName(QStringLiteral("patchStitchDialog"));
	setWindowTitle(tr("Stitch Patches"));
	setAccessibleName(windowTitle());
	resize(1150, 800);
	auto *layout = new QVBoxLayout(this);
	auto *split = new QSplitter(this);
	split->setChildrenCollapsible(false);
	layout->addWidget(split, 1);
	m_scroll = new QScrollArea(split);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_controls = new QWidget;
	auto *column = new QVBoxLayout(m_controls);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(form);
	const auto combo = [&](const QString &name, const QString &label, const QStringList &choices) {
		auto *box = new QComboBox(m_controls);
		box->setObjectName(name);
		box->setAccessibleName(label);
		box->addItems(choices);
		box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		box->setMinimumContentsLength(16);
		form->addRow(label, box);
		return box;
	};
	m_first = combo(QStringLiteral("stitchFirst"), tr("First patch"), {});
	m_firstEdge = combo(QStringLiteral("stitchFirstEdge"), tr("First boundary"),
						{tr("First row"), tr("Last row"), tr("First column"), tr("Last column")});
	m_second = combo(QStringLiteral("stitchSecond"), tr("Second patch"), {});
	m_secondEdge = combo(QStringLiteral("stitchSecondEdge"), tr("Second boundary"),
						 {tr("First row"), tr("Last row"), tr("First column"), tr("Last column")});
	for (const auto &p : source.patches) {
		const auto label = tr("Patch %1 · %2").arg(p.id).arg(p.textureName);
		m_first->addItem(label, p.id);
		m_second->addItem(label, p.id);
	}
	QVector<int> selected;
	for (const auto &ref : source.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakePatch && !selected.contains(ref.objectId)) {
			selected << ref.objectId;
		}
	}
	if (!selected.isEmpty()) {
		m_first->setCurrentIndex(m_first->findData(selected.first()));
	}
	m_second->setCurrentIndex(selected.size() > 1 ? m_second->findData(selected[1])
												  : (m_first->currentIndex() == 0 ? std::min(1, m_second->count() - 1) : 0));
	m_order = combo(QStringLiteral("stitchOrder"), tr("Boundary direction"), {tr("Automatic"), tr("Forward"), tr("Reversed")});
	m_target = combo(QStringLiteral("stitchTarget"), tr("Join positions at"), {tr("First boundary"), tr("Second boundary"), tr("Average")});
	m_uv = combo(QStringLiteral("stitchUv"), tr("Texture coordinates"),
				 {tr("Preserve each patch"), tr("Match first boundary"), tr("Match second boundary"), tr("Average boundaries")});
	m_uv->setToolTip(tr("Matching changes boundary UVs and translates their adjacent UV handles. Materials stay on their own patches."));
	m_gap = new QDoubleSpinBox(m_controls);
	m_gap->setObjectName(QStringLiteral("stitchGap"));
	m_gap->setAccessibleName(tr("Maximum boundary gap"));
	m_gap->setRange(0, 1048576);
	m_gap->setDecimals(6);
	m_gap->setLayoutDirection(Qt::LeftToRight);
	form->addRow(tr("Maximum gap"), m_gap);
	m_tangents = new QCheckBox(tr("Match geometric tangents"), m_controls);
	m_tangents->setAccessibleName(m_tangents->text());
	m_tangents->setToolTip(
		tr("Align the adjacent control rows to equal, opposite derivatives across the seam. This reshapes both neighboring spans."));
	column->addWidget(m_tangents);
	m_view = combo(QStringLiteral("stitchView"), tr("Preview"), {tr("Stitched patches"), tr("Original patches")});
	auto *frame = new QPushButton(tr("Frame patches"), m_controls);
	column->addWidget(frame);
	auto *details = new QPushButton(tr("Seam and material details"), m_controls);
	column->addWidget(details);
	connect(details, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Seam and material details"));
		dialog.resize(760, 500);
		auto *body = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(&dialog);
		text->setReadOnly(true);
		text->setAccessibleName(tr("Patch seam report and resolved material paths"));
		text->setPlainText(QString::fromUtf8(QJsonDocument(levelPatchStitchReportJson(m_result)).toJson()) + '\n' +
						   levelPreviewAssetsText(m_assets));
		body->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		body->addWidget(buttons);
		dialog.exec();
	});
	m_status = new QPlainTextEdit(m_controls);
	m_status->setObjectName(QStringLiteral("stitchStatus"));
	m_status->setAccessibleName(tr("Patch stitch status"));
	m_status->setReadOnly(true);
	m_status->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	column->addWidget(m_status);
	m_progress = new QProgressBar(m_controls);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Preparing seam and material preview"));
	column->addWidget(m_progress);
	column->addStretch();
	m_scroll->setWidget(m_controls);
	m_preview = new ModelViewport(split);
	m_preview->setAccessibleName(tr("Patch seam preview"));
	m_preview->setAccessibleDescription(
		tr("The two patch boundaries are hatched. Switch between original and stitched surfaces to review the join."));
	m_preview->setBackfaceCulling(false);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	m_preview->setShowEdges(true);
	StudioSettings settings;
	const auto prefs = settings.accessibilityPreferences();
	m_preview->setHighContrast(prefs.theme == StudioTheme::HighContrastDark || prefs.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(prefs.reducedMotion);
	connect(frame, &QPushButton::clicked, m_preview, &ModelViewport::frameModel);
	updateControlWidth();
	split->setSizes({m_scroll->minimumWidth(), 750});
	split->setStretchFactor(1, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setText(tr("Stitch Patches"));
	m_apply->setAccessibleName(tr("Apply patch seam"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &PatchStitchDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(100);
	connect(m_debounce, &QTimer::timeout, this, &PatchStitchDialog::startPreview);
	for (auto *c : {m_first, m_second, m_firstEdge, m_secondEdge, m_order, m_target, m_uv, m_view}) {
		connect(c, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	}
	connect(m_gap, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
	connect(m_tangents, &QCheckBox::toggled, this, [this] { schedule(); });
	setRequest({});
}
PatchStitchDialog::~PatchStitchDialog()
{
	if (m_work) {
		m_work->cancelled = true;
	}
}
int PatchStitchDialog::firstId() const { return m_first->currentIndex() < 0 ? -1 : m_first->currentData().toInt(); }
int PatchStitchDialog::secondId() const { return m_second->currentIndex() < 0 ? -1 : m_second->currentData().toInt(); }
LevelPatchStitchRequest PatchStitchDialog::request() const
{
	return {static_cast<LevelPatchBoundary>(m_firstEdge->currentIndex()),
			static_cast<LevelPatchBoundary>(m_secondEdge->currentIndex()),
			static_cast<LevelPatchStitchOrder>(m_order->currentIndex()),
			static_cast<LevelPatchStitchTarget>(m_target->currentIndex()),
			static_cast<LevelPatchStitchUv>(m_uv->currentIndex()),
			m_gap->value(),
			m_tangents->isChecked()};
}
void PatchStitchDialog::setRequest(const LevelPatchStitchRequest &r)
{
	m_setting = true;
	m_firstEdge->setCurrentIndex(static_cast<int>(r.first));
	m_secondEdge->setCurrentIndex(static_cast<int>(r.second));
	m_order->setCurrentIndex(static_cast<int>(r.order));
	m_target->setCurrentIndex(static_cast<int>(r.target));
	m_uv->setCurrentIndex(static_cast<int>(r.uv));
	m_gap->setValue(r.maxDistance);
	m_tangents->setChecked(r.matchTangents);
	m_setting = false;
	schedule();
}
void PatchStitchDialog::updateControlWidth()
{
	if (m_controls && m_scroll) {
		// Status may include long translated words or paths. Keep it readable at
		// every text scale, with native wrapping, selection and vertical scrolling.
		m_status->setMinimumHeight(m_status->fontMetrics().lineSpacing() * 4 + 12);
		m_status->setMaximumHeight(m_status->fontMetrics().lineSpacing() * 8 + 12);
		m_controls->layout()->activate();
		m_scroll->setMinimumWidth(
			std::max(330, m_controls->minimumSizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 12));
	}
}
void PatchStitchDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::LayoutDirectionChange) {
		updateControlWidth();
	}
}
void PatchStitchDialog::schedule()
{
	if (m_setting) {
		return;
	}
	++m_generation;
	m_ready = m_valid = false;
	m_apply->setEnabled(false);
	m_progress->show();
	m_status->setPlainText(tr("Preparing seam and material preview…"));
	if (m_work) {
		m_work->cancelled = true;
	}
	m_debounce->start();
}
void PatchStitchDialog::startPreview()
{
	if (m_thread) {
		return;
	}
	auto work = std::make_shared<Work>();
	work->generation = m_generation;
	m_work = work;
	auto visible = m_source;
	visible.brushes.clear();
	visible.entities.clear();
	const int first = firstId(), second = secondId();
	visible.patches.erase(
		std::remove_if(visible.patches.begin(), visible.patches.end(), [&](const auto &p) { return p.id != first && p.id != second; }),
		visible.patches.end());
	const auto r = request();
	const auto archive = m_archive;
	const auto palette = m_palette;
	const bool original = m_view->currentIndex() == 1;
	m_thread = QThread::create([work, visible, first, second, r, archive, palette, original]() mutable {
		const auto cancelled = [work] { return work->cancelled.load(); };
		const auto get = [&](int id) {
			return std::find_if(visible.patches.cbegin(), visible.patches.cend(), [&](const auto &p) { return p.id == id; });
		};
		const auto a = get(first), b = get(second);
		if (a != visible.patches.cend() && b != visible.patches.cend()) {
			work->valid = prepareLevelPatchStitch(*a, *b, r, &work->result, &work->error, cancelled);
		} else {
			work->error = QCoreApplication::translate("PatchStitchDialog", "Choose two existing patches.");
		}
		if (cancelled()) {
			return;
		}
		if (work->valid) {
			// Check the same source-line ownership and atomic document replacement
			// contract as Apply before enabling it. The live map is never touched.
			auto probe = visible;
			work->valid = replaceLevelMapPatches(&probe, {{first, work->result.first}, {second, work->result.second}}, &work->error);
		}
		if (work->valid && !original) {
			visible.patches = {work->result.first, work->result.second};
		}
		if (archive) {
			LevelPreviewAssetOptions options;
			options.paletteId = palette;
			work->assets = resolveLevelPreviewAssets(visible, *archive, options, [cancelled](int, int) { return !cancelled(); });
		}
		if (cancelled()) {
			return;
		}
		QHash<int, QVector<LevelMapVec3>> boundaries;
		visible.patches.erase(
			std::remove_if(visible.patches.begin(), visible.patches.end(), [](const auto &patch) { return !validateLevelPatch(patch); }),
			visible.patches.end());
		for (auto &p : visible.patches) {
			// Bound preview work independently of the saved tessellation settings.
			p.subdivisionsX = std::min(p.subdivisionsX, 8);
			p.subdivisionsY = std::min(p.subdivisionsY, 8);
			const auto edge = p.id == first ? r.first : r.second;
			const auto grid = tessellatePatchMesh(p, 3);
			if (grid.isEmpty()) {
				continue;
			}
			if (edge == LevelPatchBoundary::FirstRow || edge == LevelPatchBoundary::LastRow) {
				boundaries[p.id] = edge == LevelPatchBoundary::FirstRow ? grid.first() : grid.last();
			} else {
				for (const auto &row : grid) {
					if (!row.isEmpty()) {
						boundaries[p.id] << (edge == LevelPatchBoundary::FirstColumn ? row.first() : row.last());
					}
				}
			}
		}
		LevelMapPreviewMeshOptions options;
		options.triangleLimit = 100000;
		options.isCancelled = cancelled;
		options.textureSizes = levelPreviewTextureSizes(work->assets);
		work->mesh = buildLevelMapPreviewMesh(visible, options);
		int index = 0;
		for (const auto &surface : work->mesh.mesh.surfaces) {
			if (surface.frames.isEmpty()) {
				continue;
			}
			for (const auto &triangle : surface.triangles) {
				if (cancelled()) {
					return;
				}
				int on = 0;
				const auto &boundary = boundaries[work->mesh.owners.value(index).objectId];
				for (int vertex : {triangle.a, triangle.b, triangle.c}) {
					const auto p = surface.frames.first().positions[vertex];
					if (std::any_of(boundary.cbegin(), boundary.cend(), [&](const auto &q) {
							return p.x == static_cast<float>(q.x) && p.y == static_cast<float>(q.y) && p.z == static_cast<float>(q.z);
						})) {
						++on;
					}
				}
				if (on >= 2) {
					work->highlights << index;
				}
				++index;
			}
		}
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
		m_valid = work->valid;
		m_result = work->result;
		m_assets = work->assets;
		m_progress->hide();
		m_apply->setEnabled(m_valid);
		m_preview->setMesh(work->mesh.mesh, m_preview->hasMesh());
		m_preview->setSurfaceSkins(levelPreviewSurfaceImages(work->mesh.mesh, m_assets));
		m_preview->setHighlightedTriangles(work->highlights);
		QString text = work->error;
		if (m_valid) {
			text = tr("%1 paired controls · %2 direction\nMaximum gap: %3 · Movement: %4\nGrids: %5 × %6 and %7 × %8")
					   .arg(m_result.boundaryPoints)
					   .arg(m_result.reversed ? tr("Reversed") : tr("Forward"))
					   .arg(m_result.maxGap, 0, 'g', 6)
					   .arg(m_result.maxMovement, 0, 'g', 6)
					   .arg(m_result.first.width)
					   .arg(m_result.first.height)
					   .arg(m_result.second.width)
					   .arg(m_result.second.height);
			if (!m_result.warnings.isEmpty()) {
				text += '\n' + m_result.warnings.join('\n');
			}
			if (!m_archive) {
				text += '\n' + tr("Open a package or asset folder for material images.");
			}
		}
		m_status->setPlainText(text);
		updateControlWidth();
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}
void PatchStitchDialog::setApplyHandler(std::function<bool(int, int, const LevelPatchStitchRequest &, QString *)> handler)
{
	m_handler = std::move(handler);
}
void PatchStitchDialog::accept()
{
	if (!m_ready || !m_valid) {
		return;
	}
	QString error;
	if (m_handler && !m_handler(firstId(), secondId(), request(), &error)) {
		m_status->setPlainText(error);
		return;
	}
	QDialog::accept();
}
} // namespace vibestudio
