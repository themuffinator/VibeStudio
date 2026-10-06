#include "app/level_surface_dialog.h"
#include "app/model_viewport.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio
{
struct LevelSurfaceDialog::Work {
	LevelMapDocument document;
	LevelSurfaceEditPlan plan;
	LevelMapPreviewMesh mesh;
	LevelPreviewAssets assets;
	QString error;
	std::atomic_bool cancelled{false};
	quint64 generation = 0;
	bool valid = false, loadedAssets = false;
};

LevelSurfaceDialog::LevelSurfaceDialog(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces,
									   std::shared_ptr<const PackageArchiveReader> archive, const QString &palette, QWidget *parent)
	: QDialog(parent), m_source(document), m_visibleSource(document), m_faces(faces), m_archive(std::move(archive)), m_palette(palette)
{
	setObjectName(QStringLiteral("levelSurfaceDialog"));
	setWindowTitle(tr("Surface Alignment"));
	setAccessibleName(windowTitle());
	resize(1180, 800);
	QSet<int> ids;
	for (const auto &face : faces) {
		ids.insert(face.brushId);
	}
	m_visibleSource.brushes.removeIf([&](const auto &brush) { return !ids.contains(brush.id); });
	m_visibleSource.entities.clear();
	m_visibleSource.patches.clear();
	auto *layout = new QVBoxLayout(this);
	auto *split = new QSplitter(this);
	split->setChildrenCollapsible(false);
	layout->addWidget(split, 1);
	auto *scroll = new QScrollArea(split);
	m_scroll = scroll;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *controls = new QWidget;
	m_controls = controls;
	auto *column = new QVBoxLayout(controls);
	m_faceList = new QListWidget(controls);
	m_faceList->setObjectName(QStringLiteral("surfaceFaces"));
	m_faceList->setAccessibleName(tr("Brush faces to align"));
	m_faceList->setAccessibleDescription(
		tr("Select one or more faces. Face numbers start at one; arrow keys and Shift extend the selection."));
	m_faceList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_faceList->setMinimumHeight(140);
	m_faceList->setMaximumHeight(220);
	for (const auto &ref : faces) {
		const auto brush =
			std::find_if(document.brushes.cbegin(), document.brushes.cend(), [&](const auto &b) { return b.id == ref.brushId; });
		if (brush == document.brushes.cend() || ref.faceIndex < 0 || ref.faceIndex >= brush->faces.size()) {
			continue;
		}
		auto *row = new QListWidgetItem(
			tr("Brush %1 · Face %2 · %3").arg(ref.brushId).arg(ref.faceIndex + 1).arg(brush->faces[ref.faceIndex].textureName), m_faceList);
		row->setData(Qt::UserRole, ref.brushId);
		row->setData(Qt::UserRole + 1, ref.faceIndex);
		row->setToolTip(row->text());
		row->setSelected(true);
	}
	column->addWidget(m_faceList);
	auto *all = new QPushButton(tr("Select all faces"), controls);
	all->setAccessibleName(all->text());
	connect(all, &QPushButton::clicked, m_faceList, &QListWidget::selectAll);
	column->addWidget(all);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(form);
	m_operation = new QComboBox(controls);
	m_operation->setObjectName(QStringLiteral("surfaceOperation"));
	m_operation->setAccessibleName(tr("Surface operation"));
	m_operation->addItems({tr("Shift"), tr("Scale"), tr("Rotate"), tr("Fit"), tr("Align")});
	m_operation->setCurrentIndex(static_cast<int>(LevelSurfaceOperation::Fit));
	form->addRow(tr("Operation"), m_operation);
	const auto number = [&](const QString &name, const QString &description) {
		auto *spin = new QDoubleSpinBox(controls);
		spin->setObjectName(name);
		spin->setAccessibleDescription(description);
		spin->setLayoutDirection(Qt::LeftToRight);
		spin->setRange(-1e6, 1e6);
		spin->setDecimals(6);
		spin->setValue(1);
		connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
		return spin;
	};
	m_x = number(QStringLiteral("surfaceX"),
				 tr("Horizontal texel offset, texture size multiplier, or repeat count, depending on the operation."));
	m_y = number(QStringLiteral("surfaceY"),
				 tr("Vertical texel offset, texture size multiplier, or repeat count, depending on the operation."));
	m_xLabel = new QLabel(controls);
	m_yLabel = new QLabel(controls);
	m_xLabel->setBuddy(m_x);
	m_yLabel->setBuddy(m_y);
	form->addRow(m_xLabel, m_x);
	form->addRow(m_yLabel, m_y);
	m_degrees = number(QStringLiteral("surfaceDegrees"),
					   tr("Rotate the mapping around the face centre while keeping its centre texture coordinate fixed."));
	m_degrees->setAccessibleName(tr("Texture rotation in degrees"));
	m_degrees->setRange(-360000, 360000);
	m_degrees->setValue(15);
	form->addRow(tr("Angle (degrees)"), m_degrees);
	const auto align = [&](const QString &name, const QString &label) {
		auto *combo = new QComboBox(controls);
		combo->setObjectName(name);
		combo->setAccessibleName(label);
		combo->setAccessibleDescription(
			tr("Place the face's UV bounds at the first texture tile's minimum, centre or maximum; Keep leaves this axis alone."));
		combo->addItems({tr("Keep"), tr("Minimum"), tr("Centre"), tr("Maximum")});
		combo->setCurrentIndex(2);
		form->addRow(label, combo);
		connect(combo, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
		return combo;
	};
	m_alignU = align(QStringLiteral("surfaceAlignU"), tr("Align U"));
	m_alignV = align(QStringLiteral("surfaceAlignV"), tr("Align V"));
	m_override = new QCheckBox(tr("Use explicit texture size"), controls);
	m_override->setObjectName(QStringLiteral("surfaceSizeOverride"));
	m_override->setAccessibleName(m_override->text());
	m_override->setToolTip(
		tr("Override package image dimensions for every selected face. This also changes the preview's texel-to-repeat conversion."));
	form->addRow(m_override);
	const auto dimension = [&](const QString &name, const QString &label) {
		auto *spin = new QSpinBox(controls);
		spin->setObjectName(name);
		spin->setAccessibleName(label);
		spin->setLayoutDirection(Qt::LeftToRight);
		spin->setRange(1, 16384);
		spin->setValue(64);
		form->addRow(label, spin);
		connect(spin, &QSpinBox::valueChanged, this, [this] { schedule(); });
		return spin;
	};
	m_width = dimension(QStringLiteral("surfaceWidth"), tr("Width (texels)"));
	m_height = dimension(QStringLiteral("surfaceHeight"), tr("Height (texels)"));
	m_materialStatus = new QLabel(controls);
	m_materialStatus->setObjectName(QStringLiteral("surfaceMaterials"));
	m_materialStatus->setAccessibleName(tr("Surface material status"));
	m_materialStatus->setTextFormat(Qt::PlainText);
	m_materialStatus->setWordWrap(true);
	m_materialStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	column->addWidget(m_materialStatus);
	auto *details = new QPushButton(tr("Material details…"), controls);
	details->setAccessibleName(details->text());
	connect(details, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Surface Materials"));
		dialog.resize(680, 450);
		auto *body = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(levelPreviewAssetsText(m_assets), &dialog);
		text->setReadOnly(true);
		text->setAccessibleName(tr("Material paths, dimensions and diagnostics"));
		body->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		body->addWidget(buttons);
		dialog.exec();
	});
	column->addWidget(details);
	m_status = new QLabel(controls);
	m_status->setObjectName(QStringLiteral("surfaceStatus"));
	m_status->setAccessibleName(tr("Surface preview status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_status);
	m_progress = new QProgressBar(controls);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Preparing surface preview"));
	column->addWidget(m_progress);
	column->addStretch();
	scroll->setWidget(controls);
	m_preview = new ModelViewport(split);
	m_preview->setObjectName(QStringLiteral("surfacePreview"));
	m_preview->setAccessibleName(tr("Surface material preview"));
	m_preview->setAccessibleDescription(tr("Selected brushes with package material images. Missing images use flat shading. This preview "
										   "does not simulate game shader effects."));
	m_preview->setShowEdges(true);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	connect(m_preview, &ModelViewport::trianglePicked, this, [this](int, int triangle, int pick) {
		if (!m_ready || triangle < 0 || triangle >= m_previewMesh.owners.size() || triangle >= m_previewMesh.ownerFaces.size()) {
			return;
		}
		const LevelSurfaceFace face{m_previewMesh.owners[triangle].objectId, m_previewMesh.ownerFaces[triangle]};
		if (!m_faces.contains(face)) {
			return;
		}
		auto selected = selectedFaces();
		if (pick == static_cast<int>(ModelViewportPick::Toggle) || pick == static_cast<int>(ModelViewportPick::FaceToggle)) {
			if (!selected.removeOne(face)) {
				selected.append(face);
			}
		} else {
			selected = {face};
		}
		setSelectedFaces(selected);
	});
	const auto preferences = StudioSettings().accessibilityPreferences();
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(preferences.reducedMotion);
	updateControls();
	updateControlWidth();
	const int width = std::max(340, controls->minimumSizeHint().width() + 28);
	scroll->setMinimumWidth(width);
	split->setSizes({width, 750});
	split->setStretchFactor(1, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setText(tr("Apply"));
	m_apply->setAccessibleName(tr("Apply surface alignment to map"));
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelSurfaceDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(120);
	connect(m_debounce, &QTimer::timeout, this, &LevelSurfaceDialog::startPreview);
	connect(m_faceList, &QListWidget::itemSelectionChanged, this, [this] { schedule(); });
	connect(m_override, &QCheckBox::toggled, this, [this] { schedule(); });
	connect(m_operation, &QComboBox::currentIndexChanged, this, [this] {
		m_updating = true;
		m_x->setValue(m_operation->currentIndex() == 0 ? 0 : 1);
		m_y->setValue(m_operation->currentIndex() == 0 ? 0 : 1);
		m_updating = false;
		schedule();
	});
	schedule();
}

LevelSurfaceDialog::~LevelSurfaceDialog()
{
	if (m_work) {
		m_work->cancelled = true;
	}
}
LevelSurfaceRequest LevelSurfaceDialog::request() const
{
	return {static_cast<LevelSurfaceOperation>(m_operation->currentIndex()),
			m_x->value(),
			m_y->value(),
			m_degrees->value(),
			static_cast<LevelSurfaceAlignment>(m_alignU->currentIndex()),
			static_cast<LevelSurfaceAlignment>(m_alignV->currentIndex())};
}
void LevelSurfaceDialog::setRequest(const LevelSurfaceRequest &value)
{
	m_updating = true;
	{
		const QSignalBlocker block(m_operation);
		m_operation->setCurrentIndex(static_cast<int>(value.operation));
	}
	m_x->setValue(value.x);
	m_y->setValue(value.y);
	m_degrees->setValue(value.degrees);
	m_alignU->setCurrentIndex(static_cast<int>(value.alignU));
	m_alignV->setCurrentIndex(static_cast<int>(value.alignV));
	m_updating = false;
	schedule();
}
QVector<LevelSurfaceFace> LevelSurfaceDialog::selectedFaces() const
{
	QVector<LevelSurfaceFace> result;
	for (auto *row : m_faceList->selectedItems()) {
		result.append({row->data(Qt::UserRole).toInt(), row->data(Qt::UserRole + 1).toInt()});
	}
	return result;
}
void LevelSurfaceDialog::setSelectedFaces(const QVector<LevelSurfaceFace> &faces)
{
	m_updating = true;
	for (int i = 0; i < m_faceList->count(); ++i) {
		auto *row = m_faceList->item(i);
		row->setSelected(faces.contains({row->data(Qt::UserRole).toInt(), row->data(Qt::UserRole + 1).toInt()}));
	}
	m_updating = false;
	schedule();
}
void LevelSurfaceDialog::setTextureSizeOverride(QSize size)
{
	m_updating = true;
	m_override->setChecked(size.width() > 0 && size.height() > 0);
	if (m_override->isChecked()) {
		m_width->setValue(size.width());
		m_height->setValue(size.height());
	}
	m_updating = false;
	schedule();
}
void LevelSurfaceDialog::setApplyHandler(std::function<bool(const LevelSurfaceEditPlan &, QString *)> handler)
{
	m_applyHandler = std::move(handler);
}
void LevelSurfaceDialog::updateControls()
{
	const auto op = request().operation;
	m_xLabel->setText(op == LevelSurfaceOperation::Shift   ? tr("U offset (texels)")
					  : op == LevelSurfaceOperation::Scale ? tr("U size multiplier")
														   : tr("U repeats"));
	m_yLabel->setText(op == LevelSurfaceOperation::Shift   ? tr("V offset (texels)")
					  : op == LevelSurfaceOperation::Scale ? tr("V size multiplier")
														   : tr("V repeats"));
	m_x->setAccessibleName(m_xLabel->text());
	m_y->setAccessibleName(m_yLabel->text());
	m_x->setEnabled(op == LevelSurfaceOperation::Shift || op == LevelSurfaceOperation::Scale || op == LevelSurfaceOperation::Fit);
	m_y->setEnabled(m_x->isEnabled());
	m_degrees->setEnabled(op == LevelSurfaceOperation::Rotate);
	m_alignU->setEnabled(op == LevelSurfaceOperation::Align);
	m_alignV->setEnabled(op == LevelSurfaceOperation::Align);
	m_width->setEnabled(m_override->isChecked());
	m_height->setEnabled(m_override->isChecked());
}
void LevelSurfaceDialog::schedule()
{
	if (m_updating || !m_debounce) {
		return;
	}
	++m_generation;
	m_ready = m_valid = false;
	m_apply->setEnabled(false);
	m_preview->setEnabled(false);
	m_progress->show();
	m_status->setText(tr("Preparing surface preview…"));
	updateControls();
	if (m_work) {
		m_work->cancelled = true;
	}
	m_debounce->start();
}
void LevelSurfaceDialog::updateControlWidth()
{
	if (!m_controls || !m_scroll) {
		return;
	}
	m_controls->layout()->activate();
	m_scroll->setMinimumWidth(
		std::max(340, m_controls->minimumSizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 12));
}
void LevelSurfaceDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::FontChange) {
		updateControlWidth();
	}
}
void LevelSurfaceDialog::startPreview()
{
	if (m_thread) {
		return;
	}
	auto work = std::make_shared<Work>();
	work->document = m_source;
	work->generation = m_generation;
	work->assets = m_assets;
	m_work = work;
	const auto operation = request();
	const auto faces = selectedFaces();
	const auto visible = m_visibleSource;
	const auto archive = m_archive;
	const auto palette = m_palette;
	const auto size = m_override->isChecked() ? QSize(m_width->value(), m_height->value()) : QSize();
	const bool loadAssets = !m_assetsReady;
	m_thread = QThread::create([work, operation, faces, visible, archive, palette, size, loadAssets] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		if (loadAssets) {
			if (archive) {
				LevelPreviewAssetOptions options;
				options.paletteId = palette;
				work->assets = resolveLevelPreviewAssets(visible, *archive, options, [cancelled](int, int) { return !cancelled(); });
			}
			work->loadedAssets = !work->assets.cancelled && !cancelled();
		}
		if (cancelled()) {
			return;
		}
		auto sizes = levelPreviewTextureSizes(work->assets);
		if (size.isValid()) {
			for (const auto &brush : visible.brushes) {
				for (const auto &face : brush.faces) {
					sizes.insert(face.textureName.trimmed().replace('\\', '/').toCaseFolded(), size);
				}
			}
		}
		work->valid = prepareLevelSurfaceEdit(work->document, faces, operation, sizes, &work->plan, &work->error, cancelled) &&
					  commitLevelSurfaceEdit(&work->document, work->plan, &work->error);
		if (cancelled()) {
			return;
		}
		auto preview = visible;
		for (auto &brush : preview.brushes) {
			const auto found = std::find_if(work->document.brushes.cbegin(), work->document.brushes.cend(),
											[&](const auto &b) { return b.id == brush.id; });
			if (found != work->document.brushes.cend()) {
				brush = *found;
			}
		}
		LevelMapPreviewMeshOptions options;
		options.triangleLimit = 50000;
		options.textureSizes = sizes;
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
		m_valid = work->valid;
		m_assetsReady = m_assetsReady || work->loadedAssets;
		m_assets = work->assets;
		m_plan = work->plan;
		m_previewDocument = work->document;
		m_previewMesh = work->mesh;
		m_progress->hide();
		m_apply->setEnabled(m_valid);
		m_preview->setEnabled(true);
		m_preview->setMesh(work->mesh.mesh, m_preview->hasMesh());
		m_preview->setSurfaceSkins(levelPreviewSurfaceImages(work->mesh.mesh, m_assets));
		QVector<int> highlighted;
		const auto faces = selectedFaces();
		if (faces.size() < m_faces.size()) {
			QSet<quint64> selected;
			for (const auto &face : faces) {
				selected.insert((quint64(face.brushId) << 32) | quint32(face.faceIndex));
			}
			for (int i = 0; i < work->mesh.owners.size(); ++i) {
				if (selected.contains((quint64(work->mesh.owners[i].objectId) << 32) | quint32(work->mesh.ownerFaces[i]))) {
					highlighted.append(i);
				}
			}
		}
		m_preview->setHighlightedTriangles(highlighted);
		m_materialStatus->setText(m_archive ? tr("Package materials: %1/%2. See Material details for paths and warnings.")
												  .arg(m_assets.readyCount())
												  .arg(m_assets.requestedMaterials)
											: tr("No package is open. Provide a texture size for operations that need texel dimensions."));
		QString status = !m_valid				   ? work->error
						 : m_plan.faceCount() == 0 ? tr("Ready. The selected surfaces already match these values.")
												   : tr("Ready. %n face(s) will change in one undo step.", nullptr, m_plan.faceCount());
		if (work->mesh.truncated) {
			status += QLatin1Char('\n') + tr("The preview is limited to 50,000 triangles.");
		}
		m_status->setText(status);
		updateControlWidth();
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}
void LevelSurfaceDialog::accept()
{
	if (!m_ready || !m_valid) {
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
