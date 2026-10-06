#include "app/level_primitive_dialog.h"
#include "app/model_viewport.h"
#include "core/level_placement.h"
#include "core/studio_settings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio
{
struct LevelPrimitiveDialog::Work {
	LevelMapDocument document;
	LevelMapPreviewMesh mesh;
	LevelPreviewAssets assets;
	QString error, material;
	std::atomic_bool cancelled{false};
	quint64 generation = 0;
	bool valid = false, loadedAssets = false;
	int vertices = 0;
};

LevelPrimitiveDialog::LevelPrimitiveDialog(const LevelMapDocument &source, const LevelBrushPrimitiveRequest &initial,
										   std::shared_ptr<const PackageArchiveReader> archive, const QString &palette, QWidget *parent)
	: QDialog(parent), m_source(source), m_archive(std::move(archive)), m_palette(palette)
{
	setObjectName(QStringLiteral("addBrushDialog"));
	setWindowTitle(tr("Add Brush"));
	setAccessibleName(windowTitle());
	resize(1150, 780);
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
	m_shape = new QComboBox(m_controls);
	m_shape->setObjectName(QStringLiteral("primitiveShape"));
	m_shape->setAccessibleName(tr("Brush shape"));
	const QStringList ids{QStringLiteral("box"), QStringLiteral("wedge"), QStringLiteral("cylinder"), QStringLiteral("cone"),
						  QStringLiteral("sphere")};
	for (const auto &id : ids) {
		m_shape->addItem(levelBrushPrimitiveLabel(id), id);
	}
	form->addRow(tr("Shape"), m_shape);
	m_axis = new QComboBox(m_controls);
	m_axis->setObjectName(QStringLiteral("primitiveAxis"));
	m_axis->setAccessibleName(tr("Primitive axis"));
	m_axis->addItems({tr("X axis"), tr("Y axis"), tr("Z axis")});
	m_axis->setToolTip(
		tr("Cylinder/cone length, sphere poles or wedge height. A Z-axis wedge rises along +X; rotate or mirror it after creation."));
	form->addRow(tr("Axis"), m_axis);
	m_sides = new QSpinBox(m_controls);
	m_sides->setObjectName(QStringLiteral("primitiveSides"));
	m_sides->setRange(3, 64);
	m_sides->setAccessibleName(tr("Radial sides"));
	m_sides->setLayoutDirection(Qt::LeftToRight);
	form->addRow(tr("Sides"), m_sides);
	m_bands = new QSpinBox(m_controls);
	m_bands->setObjectName(QStringLiteral("primitiveBands"));
	m_bands->setRange(2, 16);
	m_bands->setAccessibleName(tr("Sphere latitude bands"));
	m_bands->setToolTip(tr("Includes the two pole fans. Sides multiplied by bands must not exceed 128 faces."));
	m_bands->setLayoutDirection(Qt::LeftToRight);
	form->addRow(tr("Latitude bands"), m_bands);
	for (int group = 0; group < 2; ++group) {
		for (int axis = 0; axis < 3; ++axis) {
			auto *value = new QDoubleSpinBox(m_controls);
			value->setObjectName(
				QStringLiteral("primitive%1%2").arg(group == 0 ? QStringLiteral("Center") : QStringLiteral("Size")).arg(axis));
			value->setAccessibleName((group == 0 ? tr("Centre %1") : tr("Size %1")).arg(QString(QLatin1Char("XYZ"[axis]))));
			value->setAccessibleDescription(tr("Map units. Every resulting vertex must be within −32768 to 32768."));
			value->setDecimals(4);
			value->setRange(group == 0 ? -32768 : 1, group == 0 ? 32768 : 65536);
			value->setSingleStep(8);
			value->setLayoutDirection(Qt::LeftToRight);
			form->addRow(value->accessibleName(), value);
			(group == 0 ? m_center : m_size)[axis] = value;
			connect(value, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
		}
	}
	m_texture = new QComboBox(m_controls);
	m_texture->setObjectName(QStringLiteral("addBrushTexture"));
	m_texture->setEditable(true);
	m_texture->setInsertPolicy(QComboBox::NoInsert);
	m_texture->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_texture->setMinimumContentsLength(16);
	m_texture->setAccessibleName(tr("Material for every face"));
	m_texture->setAccessibleDescription(tr("Choose a material used by this map or enter its asset path. Images resolve from the open "
										   "package or folder, including staged assets."));
	for (const auto &use : levelMapTextureUsage(source)) {
		m_texture->addItem(use.name);
	}
	form->addRow(tr("Material"), m_texture);
	auto *details = new QPushButton(tr("Material details"), m_controls);
	details->setAccessibleName(details->text());
	connect(details, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Material details"));
		dialog.resize(740, 460);
		auto *body = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(&dialog);
		text->setReadOnly(true);
		text->setAccessibleName(tr("Resolved material paths and warnings"));
		text->setPlainText(m_archive ? levelPreviewAssetsText(m_assets)
									 : tr("Open a package or asset folder to preview its material images."));
		body->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		body->addWidget(buttons);
		dialog.exec();
	});
	column->addWidget(details);
	m_status = new QLabel(m_controls);
	m_status->setObjectName(QStringLiteral("primitiveStatus"));
	m_status->setAccessibleName(tr("Primitive preview status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_status);
	m_progress = new QProgressBar(m_controls);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Building primitive preview"));
	column->addWidget(m_progress);
	column->addStretch();
	m_scroll->setWidget(m_controls);
	m_preview = new ModelViewport(split);
	m_preview->setObjectName(QStringLiteral("primitivePreview"));
	m_preview->setAccessibleName(tr("Brush primitive preview"));
	m_preview->setAccessibleDescription(
		tr("The new solid brush with package material images. Missing images use flat shading. Shader effects are not simulated."));
	m_preview->setShowEdges(true);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	const auto prefs = StudioSettings().accessibilityPreferences();
	m_preview->setHighContrast(prefs.theme == StudioTheme::HighContrastDark || prefs.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(prefs.reducedMotion);
	auto *frame = new QPushButton(tr("Frame brush"), m_controls);
	frame->setAccessibleName(frame->text());
	connect(frame, &QPushButton::clicked, m_preview, &ModelViewport::frameModel);
	column->insertWidget(column->count() - 1, frame);
	updateControlWidth();
	split->setSizes({m_scroll->minimumWidth(), 750});
	split->setStretchFactor(1, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setText(tr("Add Brush"));
	m_apply->setAccessibleName(tr("Add primitive to map"));
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelPrimitiveDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(120);
	connect(m_debounce, &QTimer::timeout, this, &LevelPrimitiveDialog::startPreview);
	connect(m_shape, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	connect(m_axis, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	connect(m_sides, &QSpinBox::valueChanged, this, [this] { schedule(); });
	connect(m_bands, &QSpinBox::valueChanged, this, [this] { schedule(); });
	connect(m_texture, &QComboBox::currentTextChanged, this, [this] { schedule(); });
	setRequest(initial);
}

LevelPrimitiveDialog::~LevelPrimitiveDialog()
{
	if (m_work) {
		m_work->cancelled = true;
	}
}
void LevelPrimitiveDialog::setRequest(const LevelBrushPrimitiveRequest &value)
{
	m_updating = true;
	m_shape->setCurrentIndex(m_shape->findData(value.shape));
	m_axis->setCurrentIndex(value.axis);
	m_sides->setValue(value.sides);
	m_bands->setValue(value.bands);
	const std::array<double, 3> low{value.mins.x, value.mins.y, value.mins.z}, high{value.maxs.x, value.maxs.y, value.maxs.z};
	for (int axis = 0; axis < 3; ++axis) {
		m_center[axis]->setValue((low[axis] + high[axis]) / 2);
		m_size[axis]->setValue(high[axis] - low[axis]);
	}
	m_texture->setCurrentText(value.texture);
	m_updating = false;
	schedule();
}
LevelBrushPrimitiveRequest LevelPrimitiveDialog::request() const
{
	LevelBrushPrimitiveRequest result;
	result.shape = m_shape->currentData().toString();
	result.axis = m_axis->currentIndex();
	result.sides = m_sides->value();
	result.bands = m_bands->value();
	result.texture = m_texture->currentText();
	result.mins = {m_center[0]->value() - m_size[0]->value() / 2, m_center[1]->value() - m_size[1]->value() / 2,
				   m_center[2]->value() - m_size[2]->value() / 2, true};
	result.maxs = {m_center[0]->value() + m_size[0]->value() / 2, m_center[1]->value() + m_size[1]->value() / 2,
				   m_center[2]->value() + m_size[2]->value() / 2, true};
	return result;
}
void LevelPrimitiveDialog::setApplyHandler(std::function<bool(const LevelMapDocument &, QString *)> handler)
{
	m_applyHandler = std::move(handler);
}
void LevelPrimitiveDialog::schedule()
{
	if (m_updating || m_closed || !m_debounce) {
		return;
	}
	++m_generation;
	m_ready = m_valid = false;
	m_apply->setEnabled(false);
	m_progress->show();
	m_status->setText(tr("Building primitive preview…"));
	const auto shape = request().shape;
	m_axis->setEnabled(shape != QStringLiteral("box"));
	m_sides->setEnabled(shape == QStringLiteral("cylinder") || shape == QStringLiteral("cone") || shape == QStringLiteral("sphere"));
	m_bands->setEnabled(shape == QStringLiteral("sphere"));
	if (m_work) {
		m_work->cancelled = true;
	}
	m_debounce->start();
}
void LevelPrimitiveDialog::updateControlWidth()
{
	if (!m_controls || !m_scroll) {
		return;
	}
	m_controls->layout()->activate();
	m_scroll->setMinimumWidth(
		std::max(310, m_controls->minimumSizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 12));
}
void LevelPrimitiveDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::LayoutDirectionChange) {
		updateControlWidth();
	}
}
void LevelPrimitiveDialog::startPreview()
{
	if (m_thread || m_closed) {
		return;
	}
	auto work = std::make_shared<Work>();
	work->document = m_source;
	work->generation = m_generation;
	const auto primitive = request();
	work->material = primitive.texture.trimmed();
	const bool cached = work->material == m_cachedMaterial && !m_cachedMaterial.isEmpty();
	if (cached) {
		work->assets = m_assets;
	}
	m_work = work;
	const auto archive = m_archive;
	const auto palette = m_palette;
	m_thread = QThread::create([work, primitive, archive, palette, cached] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		LevelPlacementRequest request;
		request.operation = LevelPlacementOperation::AddBrush;
		request.primitive = primitive;
		LevelPlacementControl control;
		control.isCancelled = cancelled;
		auto result = prepareLevelPlacement(work->document, request, control);
		work->valid = result.succeeded;
		work->error = result.error;
		if (!work->valid || cancelled()) {
			return;
		}
		work->document = std::move(result.document);
		auto visible = work->document;
		visible.brushes = {work->document.brushes.last()};
		visible.entities.clear();
		visible.patches.clear();
		LevelBrushTopology topology;
		work->valid = levelBrushTopology(visible.brushes.first(), &topology, &work->error);
		work->vertices = static_cast<int>(topology.vertices.size());
		if (archive && !cached) {
			LevelPreviewAssetOptions options;
			options.paletteId = palette;
			work->assets = resolveLevelPreviewAssets(visible, *archive, options, [cancelled](int, int) { return !cancelled(); });
		}
		work->loadedAssets = !cancelled() && !work->assets.cancelled;
		if (cancelled()) {
			return;
		}
		LevelMapPreviewMeshOptions options;
		options.textureSizes = levelPreviewTextureSizes(work->assets);
		options.isCancelled = cancelled;
		work->mesh = buildLevelMapPreviewMesh(visible, options);
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread = nullptr;
		if (m_closed) { return; }
		if (work->generation != m_generation) {
			if (!m_debounce->isActive()) {
				startPreview();
			}
			return;
		}
		m_ready = true;
		m_valid = work->valid;
		m_progress->hide();
		m_apply->setEnabled(m_valid);
		m_previewDocument = work->document;
		m_assets = work->assets;
		if (work->loadedAssets) {
			m_cachedMaterial = work->material;
		} else {
			m_cachedMaterial.clear();
		}
		m_preview->setMesh(work->mesh.mesh, m_preview->hasMesh());
		m_preview->setSurfaceSkins(levelPreviewSurfaceImages(work->mesh.mesh, m_assets));
		QString status = work->error;
		if (m_valid) {
			const auto &brush = work->document.brushes.last();
			status = tr("Ready: %1 faces, %2 vertices. Face format: %3.").arg(brush.faceCount).arg(work->vertices).arg(brush.primitiveKind);
			if (!m_archive) {
				status += QLatin1Char('\n') + tr("Open a package or asset folder for material images.");
			} else if (m_assets.readyCount() == 0) {
				status += QLatin1Char('\n') + tr("Material image unavailable. See Material details.");
			}
		}
		m_status->setText(status);
		updateControlWidth();
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}
void LevelPrimitiveDialog::accept()
{
	if (!m_ready || !m_valid || m_closed) {
		return;
	}
	QString error;
	if (m_applyHandler && !m_applyHandler(m_previewDocument, &error)) {
		m_status->setText(error);
		return;
	}
	m_closed = true;
	QDialog::accept();
}
void LevelPrimitiveDialog::reject()
{
	m_closed = true;
	m_debounce->stop();
	if (m_work) { m_work->cancelled = true; }
	QDialog::reject();
}
} // namespace vibestudio
