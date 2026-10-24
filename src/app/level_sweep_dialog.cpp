#include "app/level_sweep_dialog.h"
#include "app/model_viewport.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>

namespace vibestudio
{
struct LevelSweepDialog::Work {
	std::atomic_bool cancelled{false};
	quint64 generation = 0;
	LevelPlacementResult result;
	LevelMapPreviewMesh sourceMesh, resultMesh, combinedMesh;
	QVector<QPair<LevelMapVec3, LevelMapVec3>> edges;
	LevelPreviewAssets assets;
};
LevelSweepDialog::LevelSweepDialog(const LevelMapDocument& source, const LevelSweepRequest& request,
                                   std::shared_ptr<const PackageArchiveReader> archive, const QString& palette, QWidget* parent)
    : QDialog(parent), m_source(source), m_request(request), m_archive(std::move(archive)), m_palette(palette)
{
	setObjectName(QStringLiteral("levelSweepDialog"));
	setWindowTitle(tr("Sweep Faces"));
	setAccessibleName(windowTitle());
	if (parent) {
		setLayoutDirection(parent->layoutDirection());
	}
	resize(1160, 780);
	auto* layout = new QVBoxLayout(this);
	auto* splitter = new QSplitter(this);
	splitter->setChildrenCollapsible(false);
	layout->addWidget(splitter, 1);
	auto* scroll = new QScrollArea(splitter);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* controls = new QWidget;
	auto* column = new QVBoxLayout(controls);
	scroll->setWidget(controls);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	column->addLayout(form);
	m_material = new QLineEdit(request.texture);
	m_material->setObjectName(QStringLiteral("sweepMaterial"));
	m_material->setMaxLength(1024);
	m_material->setAccessibleName(tr("Sweep material"));
	m_material->setToolTip(
	    tr("New brushes use this material and default mapping. Source faces, their materials and flags remain unchanged."));
	form->addRow(tr("&Material"), m_material);
	m_path = new QComboBox;
	m_path->setObjectName(QStringLiteral("sweepPath"));
	m_path->setAccessibleName(tr("Sweep path"));
	m_path->addItems({tr("Arc"), tr("Straight"), tr("S-bend")});
	m_path->setCurrentIndex(int(request.path));
	m_path->setToolTip(tr("Arc revolves around the destination rotation axis, with any axial rise. S-bend follows the initial and final "
	                      "face normals. All modes reach the same destination cap."));
	form->addRow(tr("&Path"), m_path);
	const auto integer = [&](const QString& name, const QString& label, int maximum, int value) {
		auto* field = new QSpinBox;
		field->setObjectName(name);
		field->setAccessibleName(label);
		field->setRange(1, maximum);
		field->setValue(value);
		form->addRow(label, field);
		connect(field, &QSpinBox::valueChanged, this, [this] { schedule(); });
		return field;
	};
	m_segments = integer(QStringLiteral("sweepSegments"), tr("&Segments"), 64, request.segments);
	m_iterations = integer(QStringLiteral("sweepIterations"), tr("&Iterations"), 8, request.iterations);
	m_iterations->setToolTip(tr("Continue from each previous destination, including its rotation and scale."));
	m_integer = new QCheckBox(tr("Round generated stations to whole units"));
	m_integer->setObjectName(QStringLiteral("sweepInteger"));
	m_integer->setAccessibleName(m_integer->text());
	m_integer->setChecked(request.integerAlignment);
	m_integer->setToolTip(tr("The initial source face stays exact. Collapsed sections are omitted and listed in the result."));
	form->addRow(m_integer);
	const std::array<QString, 3> labels{tr("Translation"), tr("Rotation axis"), tr("Scale")};
	const std::array<QString, 3> names{QStringLiteral("Delta"), QStringLiteral("Axis"), QStringLiteral("Scale")};
	const std::array<LevelMapVec3, 3> vectors{request.translation, request.rotationAxis, request.scale};
	for (int row = 0; row < 3; ++row) {
		const double values[]{vectors[row].x, vectors[row].y, vectors[row].z};
		for (int axis = 0; axis < 3; ++axis) {
			auto* field = new QDoubleSpinBox;
			m_vectors[row][axis] = field;
			field->setObjectName(QStringLiteral("sweep%1%2").arg(names[row]).arg(QLatin1Char("XYZ"[axis])));
			const auto label = tr("%1 %2").arg(labels[row]).arg(QLatin1Char("XYZ"[axis]));
			field->setAccessibleName(label);
			field->setLayoutDirection(Qt::LeftToRight);
			field->setRange(row == 2 ? .01 : -32768, row == 2 ? 10000 : 32768);
			field->setDecimals(6);
			field->setSingleStep(row == 0 ? 16 : row == 1 ? 1 : .125);
			field->setValue(values[axis]);
			form->addRow(label, field);
			connect(field, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
		}
	}
	m_angle = new QDoubleSpinBox;
	m_angle->setObjectName(QStringLiteral("sweepAngle"));
	m_angle->setAccessibleName(tr("Destination rotation in degrees"));
	m_angle->setRange(-360, 360);
	m_angle->setDecimals(6);
	m_angle->setSingleStep(15);
	m_angle->setValue(request.angle);
	m_angle->setToolTip(tr("Use the shortest turn about the axis. A full turn is the identity; use iterations to continue a bend."));
	form->addRow(tr("Rotation &angle"), m_angle);
	auto* reset = new QPushButton(tr("Reset destination"));
	reset->setObjectName(QStringLiteral("sweepReset"));
	reset->setAccessibleName(reset->text());
	column->addWidget(reset);
	connect(reset, &QPushButton::clicked, this, [this] {
		for (int i = 0; i < 3; ++i) {
			m_vectors[0][i]->setValue(0);
			m_vectors[1][i]->setValue(i == 2 ? 1 : 0);
			m_vectors[2][i]->setValue(1);
		}
		m_angle->setValue(0);
	});
	m_view = new QComboBox;
	m_view->setObjectName(QStringLiteral("sweepView"));
	m_view->setAccessibleName(tr("Sweep preview contents"));
	m_view->addItems({tr("Sweep and source"), tr("Swept brushes"), tr("Source objects")});
	form->addRow(tr("&Preview"), m_view);
	auto* frame = new QPushButton(tr("Frame geometry"));
	frame->setAccessibleName(frame->text());
	column->addWidget(frame);
	column->addStretch();
	m_preview = new ModelViewport(splitter);
	m_preview->setObjectName(QStringLiteral("sweepPreview"));
	m_preview->setAccessibleName(tr("Sweep geometry preview"));
	m_preview->setShowEdges(true);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	const auto preferences = StudioSettings().accessibilityPreferences();
	m_preview->setReducedMotion(preferences.reducedMotion);
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	connect(frame, &QPushButton::clicked, m_preview, &ModelViewport::frameModel);
	connect(m_view, &QComboBox::currentIndexChanged, this, [this] { showMesh(); });
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	scroll->setMinimumWidth(std::max(340, controls->minimumSizeHint().width() + 24));
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("sweepStatus"));
	m_status->setAccessibleName(tr("Sweep validation"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	layout->addWidget(m_status);
	m_details = new QPlainTextEdit;
	m_details->setObjectName(QStringLiteral("sweepSkipped"));
	m_details->setAccessibleName(tr("Omitted sweep sections"));
	m_details->setReadOnly(true);
	m_details->setMaximumHeight(120);
	m_details->hide();
	layout->addWidget(m_details);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Checking sweep and loading materials"));
	m_progress->setTextVisible(false);
	m_progress->setRange(0, preferences.reducedMotion ? 1 : 0);
	layout->addWidget(m_progress);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	m_apply = buttons->addButton(tr("Create Sweep"), QDialogButtonBox::AcceptRole);
	m_apply->setObjectName(QStringLiteral("sweepApply"));
	m_apply->setAccessibleName(m_apply->text());
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelSweepDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	m_timer = new QTimer(this);
	m_timer->setSingleShot(true);
	m_timer->setInterval(120);
	connect(m_timer, &QTimer::timeout, this, [this] { start(); });
	connect(m_material, &QLineEdit::textChanged, this, [this] { schedule(); });
	connect(m_path, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	connect(m_integer, &QCheckBox::toggled, this, [this] { schedule(); });
	connect(m_angle, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
	schedule();
}
LevelSweepDialog::~LevelSweepDialog()
{
	if (m_work) {
		m_work->cancelled = true;
	}
	if (m_thread) {
		m_thread->disconnect(this);
		m_thread->wait();
		delete m_thread;
	}
}
void LevelSweepDialog::schedule()
{
	if (!m_timer) {
		return;
	}
	++m_generation;
	m_ready = false;
	m_apply->setEnabled(false);
	if (m_work) {
		m_work->cancelled = true;
	}
	m_preview->clearMesh();
	m_details->hide();
	m_progress->show();
	m_status->setText(tr("Checking sweep and loading materials…"));
	m_status->setAccessibleDescription(m_status->text());
	m_timer->start();
	Q_EMIT requestChanged(request());
	Q_EMIT previewChanged(true, {}, m_status->text());
}
LevelSweepRequest LevelSweepDialog::request() const
{
	auto r = m_request;
	r.texture = m_material->text();
	r.segments = m_segments->value();
	r.iterations = m_iterations->value();
	r.path = LevelSweepPath(m_path->currentIndex());
	r.integerAlignment = m_integer->isChecked();
	r.angle = m_angle->value();
	std::array<LevelMapVec3*, 3> vectors{&r.translation, &r.rotationAxis, &r.scale};
	for (int row = 0; row < 3; ++row) {
		*vectors[row] = {m_vectors[row][0]->value(), m_vectors[row][1]->value(), m_vectors[row][2]->value(), true};
	}
	return r;
}
void LevelSweepDialog::setDestination(const LevelSweepRequest& r)
{
	const std::array<LevelMapVec3, 3> vectors{r.translation, r.rotationAxis, r.scale};
	for (int row = 0; row < 3; ++row) {
		const double values[]{vectors[row].x, vectors[row].y, vectors[row].z};
		for (int axis = 0; axis < 3; ++axis) {
			const QSignalBlocker block(m_vectors[row][axis]);
			m_vectors[row][axis]->setValue(values[axis]);
		}
	}
	{
		const QSignalBlocker block(m_angle);
		m_angle->setValue(r.angle);
	}
	schedule();
}
void LevelSweepDialog::setViewportMode()
{
	setWindowFlag(Qt::Tool);
	setWindowTitle(tr("Sweep Tool"));
	setAccessibleName(windowTitle());
	auto* row = new QHBoxLayout;
	for (int direction : {-1, 1}) {
		auto* button = new QPushButton(direction < 0 ? tr("Decrease scale") : tr("Increase scale"));
		button->setObjectName(direction < 0 ? QStringLiteral("sweepDecreaseScale") : QStringLiteral("sweepIncreaseScale"));
		button->setAccessibleName(button->text());
		button->setAutoDefault(false);
		row->addWidget(button);
		connect(button, &QPushButton::clicked, this, [this, direction] { Q_EMIT scaleRequested(direction); });
	}
	static_cast<QVBoxLayout*>(layout())->insertLayout(1, row);
	auto* previewToggle = new QPushButton(tr("Show textured preview"));
	previewToggle->setObjectName(QStringLiteral("sweepShowPreview"));
	previewToggle->setAccessibleName(previewToggle->text());
	previewToggle->setCheckable(true);
	previewToggle->setAutoDefault(false);
	static_cast<QVBoxLayout*>(layout())->insertWidget(2, previewToggle);
	m_preview->hide();
	connect(previewToggle, &QPushButton::toggled, this, [this](bool show) {
		m_preview->setVisible(show);
		resize(show ? 1160 : 500, 780);
	});
	resize(500, 780);
}
void LevelSweepDialog::start()
{
	if (m_thread) {
		return;
	}
	const auto r = request();
	auto work = std::make_shared<Work>();
	work->generation = m_generation;
	m_work = work;
	m_thread = QThread::create([work, source = m_source, r, archive = m_archive, palette = m_palette] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		LevelPlacementRequest placement;
		placement.operation = LevelPlacementOperation::Sweep;
		placement.sweep = r;
		work->result = prepareLevelPlacement(source, placement, {cancelled, {}});
		if (cancelled()) {
			return;
		}
		auto original = source;
		QSet<int> selected;
		for (const auto& set : r.faces) {
			selected.insert(set.object.objectId);
		}
		original.brushes.removeIf([&](const auto& b) { return !selected.contains(b.id); });
		original.patches.clear();
		original.entities.clear();
		auto result = work->result.document;
		const auto ids = work->result.sweepReport.brushIds;
		result.brushes.removeIf([&](const auto& b) { return !ids.contains(b.id); });
		for (const auto& brush : result.brushes) {
			if (cancelled()) {
				return;
			}
			LevelBrushTopology topology;
			if (levelBrushTopology(brush, &topology)) {
				for (const auto& edge : topology.edges) {
					work->edges << qMakePair(topology.vertices[edge[0]], topology.vertices[edge[1]]);
				}
			}
		}
		result.patches.clear();
		result.entities.clear();
		auto combined = original;
		combined.brushes += result.brushes;
		if (archive) {
			LevelPreviewAssetOptions options;
			options.paletteId = palette;
			work->assets = resolveLevelPreviewAssets(combined, *archive, options, [cancelled](int, int) { return !cancelled(); });
		}
		if (cancelled()) {
			return;
		}
		LevelMapPreviewMeshOptions options;
		options.isCancelled = cancelled;
		options.textureSizes = levelPreviewTextureSizes(work->assets);
		work->sourceMesh = buildLevelMapPreviewMesh(original, options);
		work->resultMesh = buildLevelMapPreviewMesh(result, options);
		work->combinedMesh = buildLevelMapPreviewMesh(combined, options);
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread->deleteLater();
		m_thread = nullptr;
		if (work->generation != m_generation) {
			if (!m_timer->isActive()) {
				start();
			}
			return;
		}
		m_progress->hide();
		m_ready = work->result.succeeded && !work->cancelled && !work->assets.cancelled;
		m_apply->setEnabled(m_ready);
		if (m_ready) {
			const auto& r = work->result.sweepReport;
			m_status->setText(tr("Ready: %1 brushes from %2 faces; %3 sections omitted. Original objects remain. One undo step.")
			                      .arg(r.brushIds.size())
			                      .arg(r.sourceFaces)
			                      .arg(r.skipped.size()) +
			                  (work->assets.problemCount() ? tr(" Some material previews are unavailable.") : QString{}));
			QStringList details;
			for (const auto& s : r.skipped) {
				details << tr("Brush %1, face %2, iteration %3, segment %4: %5")
				               .arg(s.brushId)
				               .arg(s.faceId)
				               .arg(s.iteration)
				               .arg(s.segment)
				               .arg(s.reason);
			}
			m_details->setPlainText(details.join('\n'));
			m_details->setVisible(!details.isEmpty());
		} else {
			m_status->setText(work->result.error);
		}
		m_status->setAccessibleDescription(m_status->text());
		showMesh();
		Q_EMIT previewChanged(false, m_ready ? work->edges : QVector<QPair<LevelMapVec3, LevelMapVec3>>{}, m_status->text());
	});
	m_thread->start();
}
void LevelSweepDialog::showMesh()
{
	if (!m_work || m_work->cancelled || m_thread) {
		return;
	}
	const auto& mesh = !m_ready || m_view->currentIndex() == 2 ? m_work->sourceMesh
	                   : m_view->currentIndex() == 1           ? m_work->resultMesh
	                                                           : m_work->combinedMesh;
	m_preview->setAccessibleName(!m_ready || m_view->currentIndex() == 2 ? tr("Source objects preview")
	                             : m_view->currentIndex() == 1           ? tr("Swept brushes preview")
	                                                                     : tr("Sweep and source preview"));
	m_preview->setMesh(mesh.mesh, m_preview->hasMesh());
	m_preview->setSurfaceSkins(levelPreviewSurfaceImages(mesh.mesh, m_work->assets));
}
void LevelSweepDialog::accept()
{
	if (!m_ready || !m_work) {
		return;
	}
	QString error;
	if (m_applyHandler && !m_applyHandler(m_work->result.document, &error)) {
		m_status->setText(error);
		m_status->setAccessibleDescription(error);
		return;
	}
	QDialog::accept();
}
} // namespace vibestudio
