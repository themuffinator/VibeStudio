// The UV Editor dialog. Workflow reference: NetRadiant Custom's TexTool
// plugin (plugins/textool/TexTool.cpp at 449778b3, GPL-2.0-or-later): a
// texture-space view of one face or patch with live camera feedback,
// committed or cancelled as a whole. Original Qt implementation; see
// docs/CREDITS.md.

#include "app/level_uv_editor_dialog.h"

#include "app/level_uv_view.h"
#include "app/locale_number.h"
#include "app/model_viewport.h"
#include "core/map_preview_mesh.h"
#include "core/studio_settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace vibestudio {
namespace {

QString materialKey(QString name)
{
	return name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
}

} // namespace

struct LevelUvEditorDialog::Load {
	LevelPreviewAssets assets;
	std::atomic_bool cancelled {false};
};

LevelUvEditorDialog::LevelUvEditorDialog(const LevelMapDocument& document, const LevelMaterialTarget& target,
	std::shared_ptr<const PackageArchiveReader> archive, const QString& palette, const LevelPreviewMaterial* material, QWidget* parent)
	: QDialog(parent), m_source(document), m_visible(document), m_target(target), m_archive(std::move(archive)), m_palette(palette)
{
	setObjectName(QStringLiteral("levelUvEditorDialog"));
	setWindowTitle(tr("UV Editor"));
	setAccessibleName(windowTitle());
	resize(1180, 760);
	// The preview shows the target's brush, or the patch, alone.
	m_visible.entities.clear();
	m_visible.brushes.removeIf([&](const LevelMapBrush& brush) { return target.kind != LevelMaterialKind::BrushFace || brush.id != target.objectId; });
	m_visible.patches.removeIf([&](const LevelMapPatch& patch) { return target.kind != LevelMaterialKind::Patch || patch.id != target.objectId; });
	for (const LevelMapBrush& brush : std::as_const(m_visible.brushes)) {
		m_material = brush.faces.value(target.faceIndex).textureName;
	}
	for (const LevelMapPatch& patch : std::as_const(m_visible.patches)) {
		m_material = patch.textureName;
	}

	auto* layout = new QVBoxLayout(this);
	auto* split = new QSplitter(Qt::Horizontal, this);
	split->setChildrenCollapsible(false);
	layout->addWidget(split, 1);
	m_view = new LevelUvView(split);
	m_view->setEnabled(false);
	auto* side = new QSplitter(Qt::Vertical, split);
	side->setChildrenCollapsible(false);
	m_preview = new ModelViewport(side);
	m_preview->setObjectName(QStringLiteral("levelUvPreview"));
	m_preview->setAccessibleName(tr("UV Editor surface preview"));
	m_preview->setAccessibleDescription(
		tr("The face's brush or the patch with its material and the UV Editor's current coordinates, updated as you edit. The edited face is "
		   "highlighted. Game shader effects are not shown."));
	m_preview->setShowEdges(true);
	m_preview->setBackfaceCulling(false);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	const auto preferences = StudioSettings().accessibilityPreferences();
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(preferences.reducedMotion);

	auto* scroll = new QScrollArea(side);
	m_scroll = scroll;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* controls = new QWidget;
	m_controls = controls;
	auto* column = new QVBoxLayout(controls);
	const auto label = [&](const QString& name, const QString& accessible) {
		auto* text = new QLabel(controls);
		text->setObjectName(name);
		text->setAccessibleName(accessible);
		text->setTextFormat(Qt::PlainText);
		text->setWordWrap(true);
		text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		text->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
		column->addWidget(text);
		return text;
	};
	m_targetLabel = label(QStringLiteral("levelUvTarget"), tr("UV Editor target"));
	m_materialLabel = label(QStringLiteral("levelUvMaterial"), tr("UV Editor material image"));
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(form);
	m_snap = new QComboBox(controls);
	m_snap->setObjectName(QStringLiteral("levelUvSnap"));
	m_snap->setAccessibleName(tr("Snap step in texels"));
	m_snap->setToolTip(tr("Dragged points land on a grid of this many texels, and the arrow keys nudge by it. Off lets points move freely and "
						  "nudges by one texel."));
	m_snap->setLayoutDirection(Qt::LeftToRight);
	m_snap->addItem(tr("Off"), 0.0);
	for (const auto& [text, step] : {std::pair {"1/8", 0.125}, std::pair {"1/4", 0.25}, std::pair {"1/2", 0.5}}) {
		m_snap->addItem(QString::fromLatin1(text), step);
	}
	for (const int step : {1, 2, 4, 8, 16}) {
		m_snap->addItem(QLocale().toString(step), static_cast<double>(step));
	}
	m_snap->setCurrentIndex(m_snap->findData(1.0));
	form->addRow(tr("Snap (texels)"), m_snap);
	m_turnStep = new QDoubleSpinBox(controls);
	m_turnStep->setObjectName(QStringLiteral("levelUvTurnStep"));
	m_turnStep->setAccessibleName(tr("Turn step in degrees"));
	m_turnStep->setToolTip(tr("The [ and ] keys and the turn buttons turn by this many degrees; Shift snaps a dragged turn to it."));
	m_turnStep->setLayoutDirection(Qt::LeftToRight);
	m_turnStep->setRange(0.1, 180);
	m_turnStep->setDecimals(2);
	m_turnStep->setValue(15);
	form->addRow(tr("Turn step (degrees)"), m_turnStep);
	m_allowValve220 = new QCheckBox(tr("Convert to Valve 220 when a turn needs it"), controls);
	m_allowValve220->setObjectName(QStringLiteral("levelUvAllowValve220"));
	m_allowValve220->setAccessibleName(m_allowValve220->text());
	m_allowValve220->setToolTip(tr("A classic face whose texture is scaled differently along U and V cannot hold every turn. Ticked, it becomes a "
								   "Valve 220 face that holds the turn exactly; unticked, it takes the nearest classic mapping."));
	m_allowValve220->setAccessibleDescription(m_allowValve220->toolTip());
	form->addRow(m_allowValve220);
	m_override = new QCheckBox(tr("Use explicit texture size"), controls);
	m_override->setObjectName(QStringLiteral("levelUvSizeOverride"));
	m_override->setAccessibleName(m_override->text());
	m_override->setToolTip(tr("Texels in one repeat of the image. Brush-primitive faces and patches count repeats, so their texel coordinates, "
							  "snapping and turns depend on it. Change it before editing."));
	m_override->setAccessibleDescription(m_override->toolTip());
	form->addRow(m_override);
	const auto dimension = [&](const QString& name, const QString& text) {
		auto* spin = new QSpinBox(controls);
		spin->setObjectName(name);
		spin->setAccessibleName(text);
		spin->setLayoutDirection(Qt::LeftToRight);
		spin->setRange(1, 16384);
		spin->setValue(64);
		form->addRow(text, spin);
		return spin;
	};
	m_width = dimension(QStringLiteral("levelUvWidth"), tr("Width (texels)"));
	m_height = dimension(QStringLiteral("levelUvHeight"), tr("Height (texels)"));

	auto* buttons = new QGridLayout;
	column->addLayout(buttons);
	int cell = 0;
	const auto button = [&](const QString& name, const QString& text, const QString& tip, const std::function<void()>& run) {
		auto* push = new QPushButton(text, controls);
		push->setObjectName(name);
		push->setAccessibleName(text);
		push->setToolTip(tip);
		push->setAccessibleDescription(tip);
		push->setAutoDefault(false);
		connect(push, &QPushButton::clicked, this, [this, run] {
			run();
			m_view->setFocus(Qt::OtherFocusReason);
		});
		buttons->addWidget(push, cell / 2, cell % 2);
		++cell;
		return push;
	};
	m_undo = button(QStringLiteral("levelUvUndo"), tr("Undo"), tr("Undo the last change in the UV Editor (Ctrl+Z in the view)."), [this] { m_view->undo(); });
	m_redo = button(QStringLiteral("levelUvRedo"), tr("Redo"), tr("Redo the change undone last (Ctrl+Shift+Z in the view)."), [this] { m_view->redo(); });
	m_reset = button(QStringLiteral("levelUvReset"), tr("Reset"), tr("Put every point back where it started."), [this] { m_view->resetEdit(); });
	m_anchor = button(QStringLiteral("levelUvAnchor"), tr("Set Anchor"), tr("Make the current point the anchor that turns go about, or clear it (A in the view)."),
		[this] { m_view->setAnchor(m_view->anchor() >= 0 && m_view->anchor() == m_view->currentPoint() ? -1 : m_view->currentPoint()); });
	button(QStringLiteral("levelUvTurnAnticlockwise"), tr("Turn Anticlockwise"),
		tr("Turn by the turn step about the anchor, or the centre without one ([ in the view)."), [this] { m_view->rotate(-m_turnStep->value()); });
	button(QStringLiteral("levelUvTurnClockwise"), tr("Turn Clockwise"), tr("Turn by the turn step about the anchor, or the centre without one (] in the view)."),
		[this] { m_view->rotate(m_turnStep->value()); });
	button(QStringLiteral("levelUvFrame"), tr("Frame All"), tr("Fit the points in the view (Home in the view)."), [this] { m_view->frameAll(); });
	button(QStringLiteral("levelUvZoomIn"), tr("Zoom In"), tr("Zoom in on the middle of the view (+ or Delete in the view)."), [this] { m_view->zoomBy(2); });
	button(QStringLiteral("levelUvZoomOut"), tr("Zoom Out"), tr("Zoom out from the middle of the view (- or Insert in the view)."), [this] { m_view->zoomBy(0.5); });

	m_points = new QTableWidget(0, 3, controls);
	m_points->setObjectName(QStringLiteral("levelUvPoints"));
	m_points->setAccessibleName(tr("UV Editor points"));
	m_points->setAccessibleDescription(tr("Each point's U and V in texels. Edit a cell to move that point: a face's whole texture follows it."));
	m_points->setHorizontalHeaderLabels({tr("Point"), tr("U (texels)"), tr("V (texels)")});
	m_points->verticalHeader()->setVisible(false);
	m_points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_points->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_points->setLayoutDirection(Qt::LeftToRight);
	m_points->setMinimumHeight(150);
	column->addWidget(m_points, 1);
	scroll->setWidget(controls);
	side->addWidget(m_preview);
	side->addWidget(scroll);
	side->setSizes({300, 420});
	split->addWidget(m_view);
	split->addWidget(side);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);
	split->setSizes({720, 440});

	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("levelUvStatus"));
	m_status->setAccessibleName(tr("UV Editor status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	layout->addWidget(m_status);
	m_progress = new QProgressBar(this);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Reading the material image"));
	m_progress->hide();
	layout->addWidget(m_progress);
	auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = box->button(QDialogButtonBox::Ok);
	m_apply->setText(tr("Apply"));
	m_apply->setAccessibleName(tr("Apply the texture coordinates to the map"));
	m_apply->setEnabled(false);
	connect(box, &QDialogButtonBox::accepted, this, &LevelUvEditorDialog::accept);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(box);

	// Room for the controls at any text size, and for the view beside them.
	updateControlWidth();
	resize(std::max(1180, minimumSizeHint().width() + 480), std::max(760, minimumSizeHint().height()));

	m_previewTimer = new QTimer(this);
	m_previewTimer->setSingleShot(true);
	m_previewTimer->setInterval(15);
	connect(m_previewTimer, &QTimer::timeout, this, &LevelUvEditorDialog::rebuildPreview);
	connect(m_view, &LevelUvView::editChanged, this, [this] {
		refreshControls();
		refreshPoints();
		m_previewTimer->start();
	});
	connect(m_view, &LevelUvView::stateChanged, this, [this] {
		refreshControls();
		refreshPoints();
	});
	connect(m_snap, &QComboBox::currentIndexChanged, this, [this] { m_view->setSnapStep(m_snap->currentData().toDouble()); });
	connect(m_turnStep, &QDoubleSpinBox::valueChanged, this, [this](double value) { m_view->setAngleStep(value); });
	connect(m_allowValve220, &QCheckBox::toggled, this, [this](bool allow) { m_view->setAllowValve220(allow); });
	connect(m_override, &QCheckBox::toggled, this, [this] { loadLayout(); });
	connect(m_width, &QSpinBox::valueChanged, this, [this] {
		if (m_override->isChecked()) {
			loadLayout();
		}
	});
	connect(m_height, &QSpinBox::valueChanged, this, [this] {
		if (m_override->isChecked()) {
			loadLayout();
		}
	});
	connect(m_points, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (m_refreshing || !item || item->column() == 0) {
			return;
		}
		const int row = item->row();
		const QPointF at = m_view->result().uv.value(row);
		bool ok = false;
		const double value = QLocale().toDouble(item->text().trimmed(), &ok);
		const bool accepted = ok && std::isfinite(value) && m_view->movePointTo(row, item->column() == 1 ? QPointF(value, at.y()) : QPointF(at.x(), value));
		if (!accepted) {
			refreshPoints();
		}
	});
	connect(m_points, &QTableWidget::itemSelectionChanged, this, [this] {
		if (m_refreshing) {
			return;
		}
		QVector<int> rows;
		for (const QModelIndex& index : m_points->selectionModel()->selectedRows()) {
			rows.append(index.row());
		}
		if (m_view->layout().isPatch()) {
			m_view->setSelectedPoints(rows);
		}
		if (m_points->currentRow() >= 0) {
			m_view->setCurrentPoint(m_points->currentRow());
		}
	});

	if (material && material->ready()) {
		m_image = material->image;
		m_materialSize = material->sourceSize;
		m_assets.materials = {*material};
		m_materialNote = tr("Image: %1 by %2 texels, from %3.").arg(m_materialSize.width()).arg(m_materialSize.height()).arg(material->imagePath);
		loadLayout();
	} else if (m_archive) {
		startLoad();
	} else {
		m_materialNote = tr("No package is open, so the material image is not shown; a checker marks each repeat.");
		loadLayout();
	}
	m_view->setFocus(Qt::OtherFocusReason);
}

LevelUvEditorDialog::~LevelUvEditorDialog()
{
	if (m_load) {
		m_load->cancelled = true;
	}
}

void LevelUvEditorDialog::startLoad()
{
	auto load = std::make_shared<Load>();
	m_load = load;
	m_progress->show();
	m_status->setText(tr("Reading the material image…"));
	const LevelMapDocument visible = m_visible;
	const auto archive = m_archive;
	const QString palette = m_palette;
	m_thread = QThread::create([load, visible, archive, palette] {
		LevelPreviewAssetOptions options;
		options.paletteId = palette;
		load->assets = resolveLevelPreviewAssets(visible, *archive, options, [load](int, int) { return !load->cancelled.load(); });
	});
	connect(m_thread, &QThread::finished, this, [this, load] {
		m_thread = nullptr;
		if (load->cancelled.load()) {
			return;
		}
		finishLoad(load->assets);
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}

void LevelUvEditorDialog::finishLoad(const LevelPreviewAssets& assets)
{
	m_progress->hide();
	m_assets = assets;
	const QString key = materialKey(m_material);
	m_materialNote = tr("The material image was not found in the package; a checker marks each repeat.");
	for (const LevelPreviewMaterial& material : std::as_const(m_assets.materials)) {
		if (material.key == key && material.ready()) {
			m_image = material.image;
			m_materialSize = material.sourceSize;
			m_materialNote = tr("Image: %1 by %2 texels, from %3.").arg(m_materialSize.width()).arg(m_materialSize.height()).arg(material.imagePath);
		}
	}
	loadLayout();
}

void LevelUvEditorDialog::updateControlWidth()
{
	if (!m_controls || !m_scroll) {
		return;
	}
	m_controls->layout()->activate();
	m_scroll->setMinimumWidth(std::max(320, m_controls->minimumSizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 12));
}

void LevelUvEditorDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::FontChange) {
		updateControlWidth();
	}
}

QSize LevelUvEditorDialog::effectiveSize() const
{
	return m_override->isChecked() ? QSize(m_width->value(), m_height->value()) : m_materialSize;
}

void LevelUvEditorDialog::loadLayout()
{
	if (m_progress->isVisible()) {
		return;
	}
	// Brush primitives and patches count repeats: without the image's size,
	// say so and use an explicit size the user can see and change.
	const bool repeats = m_target.kind == LevelMaterialKind::Patch
		|| std::any_of(m_visible.brushes.cbegin(), m_visible.brushes.cend(),
			[this](const LevelMapBrush& brush) { return brush.faces.value(m_target.faceIndex).explicitTextureMatrix; });
	QString note = m_materialNote;
	if (repeats && !effectiveSize().isValid()) {
		const QSignalBlocker block(m_override);
		m_override->setChecked(true);
	}
	if (repeats && m_override->isChecked() && !m_materialSize.isValid()) {
		note += QLatin1Char(' ')
			+ tr("This surface counts its texture coordinates in repeats, so the explicit texture size below stands in for the image's.");
	}
	LevelUvLayout layout;
	QString error;
	m_ready = readLevelUvLayout(m_source, m_target, effectiveSize(), &layout, &error);
	if (!m_ready) {
		m_view->setEnabled(false);
		m_status->setText(error);
		refreshControls();
		return;
	}
	m_view->setSource(layout, m_image);
	m_view->setSnapStep(m_snap->currentData().toDouble());
	m_view->setAngleStep(m_turnStep->value());
	m_view->setAllowValve220(m_allowValve220->isChecked());
	m_view->setEnabled(true);
	const QString format = layout.format == LevelUvFormat::Classic ? tr("classic face")
		: layout.format == LevelUvFormat::Valve220				   ? tr("Valve 220 face")
		: layout.format == LevelUvFormat::BrushPrimitive		   ? tr("brush-primitive face")
																   : tr("patch");
	m_targetLabel->setText(layout.isPatch()
			? tr("Patch %1 (%2 by %3 control points), %4.").arg(layout.target.objectId).arg(layout.columns).arg(layout.rows).arg(layout.material)
			: tr("Brush %1, face %2: %3, %4.").arg(layout.target.objectId).arg(layout.target.faceIndex + 1).arg(format, layout.material));
	m_materialLabel->setText(note);
	m_allowValve220->setVisible(layout.format == LevelUvFormat::Classic);
	refreshControls();
	refreshPoints();
	m_previewTimer->start();
}

void LevelUvEditorDialog::setTextureSizeOverride(QSize size)
{
	if (size.width() <= 0 || size.height() <= 0 || (m_view->hasSource() && m_view->changed())) {
		return;
	}
	{
		const QSignalBlocker blockWidth(m_width), blockHeight(m_height), blockOverride(m_override);
		m_width->setValue(size.width());
		m_height->setValue(size.height());
		m_override->setChecked(true);
	}
	loadLayout();
}

void LevelUvEditorDialog::setTurnStep(double degrees)
{
	if (std::isfinite(degrees) && degrees > 0) {
		m_turnStep->setValue(degrees);
	}
}

void LevelUvEditorDialog::setApplyHandler(std::function<bool(const LevelSurfaceEditPlan&, QString*)> handler)
{
	m_applyHandler = std::move(handler);
}

void LevelUvEditorDialog::refreshControls()
{
	const bool ready = m_ready && m_view->hasSource();
	m_undo->setEnabled(ready && m_view->canUndo());
	m_redo->setEnabled(ready && m_view->canRedo());
	m_reset->setEnabled(ready && (m_view->changed() || m_view->canUndo()));
	const bool anchored = m_view->anchor() >= 0 && m_view->anchor() == m_view->currentPoint();
	m_anchor->setText(anchored ? tr("Clear Anchor") : tr("Set Anchor"));
	m_anchor->setAccessibleName(m_anchor->text());
	m_anchor->setEnabled(ready && (m_view->currentPoint() >= 0 || m_view->anchor() >= 0));
	// The size can change only while nothing has: coordinates follow it.
	const bool sizeFree = !m_progress->isVisible() && !(m_view->hasSource() && m_view->changed());
	m_override->setEnabled(sizeFree);
	m_width->setEnabled(sizeFree && m_override->isChecked());
	m_height->setEnabled(sizeFree && m_override->isChecked());
	m_apply->setEnabled(ready && m_view->error().isEmpty());
	if (ready) {
		m_status->setText(m_view->summary());
	}
}

void LevelUvEditorDialog::refreshPoints()
{
	if (!m_view->hasSource()) {
		return;
	}
	m_refreshing = true;
	const QVector<QPointF>& uv = m_view->result().uv;
	const QSignalBlocker block(m_points);
	m_points->setRowCount(static_cast<int>(uv.size()));
	for (int row = 0; row < uv.size(); ++row) {
		const auto cell = [&](int column, const QString& text, bool editable) {
			QTableWidgetItem* item = m_points->item(row, column);
			if (!item) {
				item = new QTableWidgetItem;
				m_points->setItem(row, column, item);
			}
			// Only changed cells, so a drag across a large patch stays quick.
			if (item->text() != text) {
				item->setText(text);
			}
			const Qt::ItemFlags flags = editable ? Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable : Qt::ItemIsSelectable | Qt::ItemIsEnabled;
			if (item->flags() != flags) {
				item->setFlags(flags);
			}
		};
		QString name = QString::number(row + 1);
		if (row == m_view->anchor()) {
			name = tr("%1 (anchor)").arg(row + 1);
		}
		cell(0, name, false);
		cell(1, localeNumber(uv[row].x(), 9), true);
		cell(2, localeNumber(uv[row].y(), 9), true);
	}
	m_points->clearSelection();
	const QVector<int> selected = m_view->layout().isPatch() ? m_view->selectedPoints() : QVector<int> {};
	for (const int row : selected) {
		m_points->selectionModel()->select(m_points->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}
	if (m_view->currentPoint() >= 0 && m_view->currentPoint() < uv.size()) {
		m_points->setCurrentCell(m_view->currentPoint(), m_points->currentColumn() < 0 ? 1 : m_points->currentColumn(),
			selected.isEmpty() ? QItemSelectionModel::NoUpdate : QItemSelectionModel::Current);
	}
	m_refreshing = false;
}

void LevelUvEditorDialog::rebuildPreview()
{
	if (!m_view->hasSource()) {
		return;
	}
	LevelMapDocument document = m_visible;
	const LevelUvResult& result = m_view->result();
	for (LevelMapBrush& brush : document.brushes) {
		if (m_target.kind == LevelMaterialKind::BrushFace && brush.id == m_target.objectId && m_target.faceIndex < brush.faces.size()) {
			brush.faces[m_target.faceIndex] = result.face;
		}
	}
	for (LevelMapPatch& patch : document.patches) {
		if (m_target.kind == LevelMaterialKind::Patch && patch.id == m_target.objectId) {
			patch = result.patch;
		}
	}
	LevelMapPreviewMeshOptions options;
	options.triangleLimit = 20000;
	options.textureSizes = levelPreviewTextureSizes(m_assets);
	// The tile the view shows, so the preview repeats as the view does.
	const QSize tile = m_view->layout().imageSize.isValid() ? m_view->layout().imageSize : QSize(64, 64);
	options.textureSizes.insert(materialKey(m_material), tile);
	const LevelMapPreviewMesh mesh = buildLevelMapPreviewMesh(document, options);
	m_preview->setMesh(mesh.mesh, m_preview->hasMesh());
	QHash<int, QImage> skins = levelPreviewSurfaceImages(mesh.mesh, m_assets);
	if (m_image.isNull()) {
		// A checker for the edited material, so turns and moves show.
		QImage checker(64, 64, QImage::Format_RGB32);
		for (int y = 0; y < checker.height(); ++y) {
			for (int x = 0; x < checker.width(); ++x) {
				checker.setPixelColor(x, y, ((x / 8 + y / 8) % 2) ? QColor(64, 88, 110) : QColor(220, 226, 230));
			}
		}
		for (int surface = 0; surface < mesh.mesh.surfaces.size(); ++surface) {
			if (materialKey(mesh.mesh.surfaces[surface].name) == materialKey(m_material)) {
				skins.insert(surface, checker);
			}
		}
	}
	m_preview->setSurfaceSkins(skins);
	QVector<int> highlighted;
	if (m_target.kind == LevelMaterialKind::BrushFace) {
		for (int i = 0; i < mesh.owners.size() && i < mesh.ownerFaces.size(); ++i) {
			if (mesh.owners[i].objectId == m_target.objectId && mesh.ownerFaces[i] == m_target.faceIndex) {
				highlighted.append(i);
			}
		}
	}
	m_preview->setHighlightedTriangles(highlighted);
}

bool LevelUvEditorDialog::preparePlan(LevelSurfaceEditPlan* plan, QString* error, LevelUvFit* fit) const
{
	if (!m_ready || !m_view->hasSource()) {
		if (error) {
			*error = tr("The UV Editor is still reading the surface.");
		}
		return false;
	}
	return prepareLevelUvEdit(m_source, m_view->layout(), m_view->edit(), plan, error, fit);
}

void LevelUvEditorDialog::accept()
{
	if (!m_ready || m_view->dragging()) {
		return;
	}
	LevelSurfaceEditPlan plan;
	QString error;
	LevelUvFit fit = LevelUvFit::Unchanged;
	if (!preparePlan(&plan, &error, &fit)) {
		m_status->setText(error);
		return;
	}
	if (fit != LevelUvFit::Unchanged && m_applyHandler && !m_applyHandler(plan, &error)) {
		m_status->setText(error);
		return;
	}
	QDialog::accept();
}

} // namespace vibestudio
