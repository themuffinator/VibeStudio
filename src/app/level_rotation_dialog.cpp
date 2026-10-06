#include "app/level_rotation_dialog.h"
#include "app/model_viewport.h"
#include "core/level_placement.h"
#include "core/map_preview_mesh.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio {
struct LevelRotationDialog::Work {
	LevelMapDocument document;
	LevelMapPreviewMesh mesh;
	QString error;
	std::atomic_bool cancelled{false};
	bool valid = false;
	int converted = 0;
	quint64 generation = 0;
};

LevelRotationDialog::LevelRotationDialog(const LevelMapDocument& source, int axis, QWidget* parent) : QDialog(parent), m_source(source) {
	setObjectName(QStringLiteral("levelRotationDialog"));
	setWindowTitle(tr("Rotate Selection"));
	setAccessibleName(windowTitle());
	resize(1100, 720);
	auto* layout = new QVBoxLayout(this);
	auto* split = new QSplitter(this);
	split->setChildrenCollapsible(false);
	layout->addWidget(split, 1);
	auto* scroll = new QScrollArea(split);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setMinimumWidth(270);
	auto* controls = new QWidget;
	auto* column = new QVBoxLayout(controls);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(form);
	m_axis = new QComboBox(controls);
	m_axis->setObjectName(QStringLiteral("rotationAxis"));
	m_axis->setAccessibleName(tr("Rotation axis"));
	m_axis->addItems({tr("X axis"), tr("Y axis"), tr("Z axis")});
	m_axis->setCurrentIndex(axis);
	form->addRow(tr("Axis"), m_axis);
	m_angle = new QDoubleSpinBox(controls);
	m_angle->setObjectName(QStringLiteral("rotationDegrees"));
	m_angle->setAccessibleName(tr("Rotation in degrees"));
	m_angle->setAccessibleDescription(tr("Positive angles follow the right-hand rule around the selected axis."));
	m_angle->setRange(-360000, 360000);
	m_angle->setDecimals(4);
	m_angle->setSingleStep(15);
	m_angle->setValue(45);
	m_angle->setSuffix(tr("°"));
	m_angle->setLayoutDirection(Qt::LeftToRight);
	form->addRow(tr("Angle"), m_angle);
	m_pivotMode = new QComboBox(controls);
	m_pivotMode->setAccessibleName(tr("Rotation pivot"));
	m_pivotMode->setObjectName(QStringLiteral("rotationPivotMode"));
	m_pivotMode->addItems({tr("Selection centre"), tr("World origin"), tr("Custom coordinates")});
	form->addRow(tr("Pivot"), m_pivotMode);
	LevelMapVec3 low, high;
	levelMapSelectionBounds(source, &low, &high);
	const std::array<double, 3> centre{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
	for (int i = 0; i < 3; ++i) {
		auto* coordinate = new QDoubleSpinBox(controls);
		coordinate->setRange(source.doomUdmf ? -1e7 : -32768, source.doomUdmf ? 1e7 : 32768);
		coordinate->setDecimals(6);
		coordinate->setValue(centre[i]);
		coordinate->setLayoutDirection(Qt::LeftToRight);
		coordinate->setObjectName(QStringLiteral("rotationPivot%1").arg(i));
		coordinate->setAccessibleName(tr("Pivot %1").arg(QString(QLatin1Char("XYZ"[i]))));
		form->addRow(coordinate->accessibleName(), coordinate);
		m_pivot[i] = coordinate;
		connect(coordinate, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
	}
	m_lock = new QCheckBox(tr("Lock textures to faces"), controls);
	m_lock->setObjectName(QStringLiteral("rotationTextureLock"));
	m_lock->setAccessibleName(m_lock->text());
	m_lock->setChecked(true);
	m_lock->setToolTip(
		tr("Keep the same texture coordinates on each rotated face. Patches always keep their control-point UV coordinates."));
	column->addWidget(m_lock);
	m_allowValve = new QCheckBox(tr("Allow Valve 220 conversion"), controls);
	m_allowValve->setObjectName(QStringLiteral("rotationAllowValve"));
	m_allowValve->setAccessibleName(m_allowValve->text());
	m_allowValve->setToolTip(tr("If a locked rotation needs explicit axes, convert all classic faces in the map to Valve 220 while "
								"preserving their appearance. Check that the target compiler accepts Valve 220 maps."));
	column->addWidget(m_allowValve);
	m_status = new QLabel(controls);
	m_status->setObjectName(QStringLiteral("rotationStatus"));
	m_status->setAccessibleName(tr("Rotation preview status"));
	m_status->setWordWrap(true);
	// Worker status changes after the splitter is laid out. A long translated
	// token must wrap inside the controls, not widen the scrollable column.
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_status);
	m_progress = new QProgressBar(controls);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Building rotation preview"));
	column->addWidget(m_progress);
	column->addStretch();
	scroll->setWidget(controls);
	m_preview = new ModelViewport(split);
	m_preview->setObjectName(QStringLiteral("rotationGeometryPreview"));
	m_preview->setAccessibleName(tr("Rotation geometry preview"));
	m_preview->setAccessibleDescription(tr("Geometry preview of the rotated map. Material images are not loaded in this view."));
	m_preview->setShowEdges(true);
	const auto preferences = StudioSettings().accessibilityPreferences();
	if (preferences.reducedMotion) {
		m_progress->setRange(0, 1);
	}
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(preferences.reducedMotion);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	// Reserve the actual translated control width at the current text scale.
	// A fixed splitter width clipped LTR coordinates inside an RTL form at 200%.
	controls->ensurePolished();
	column->activate();
	const int controlsWidth = std::max(300, controls->minimumSizeHint().width() + 28);
	scroll->setMinimumWidth(controlsWidth);
	split->setSizes({controlsWidth, 750});
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setText(tr("Rotate"));
	m_apply->setAccessibleName(tr("Apply rotation to map"));
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelRotationDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(150);
	connect(m_debounce, &QTimer::timeout, this, &LevelRotationDialog::startPreview);
	connect(m_axis, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	connect(m_pivotMode, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	connect(m_angle, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
	connect(m_lock, &QCheckBox::toggled, this, [this] { schedule(); });
	connect(m_allowValve, &QCheckBox::toggled, this, [this] { schedule(); });
	if (source.format == LevelMapFormat::DoomWad) {
		m_axis->setCurrentIndex(2);
		m_axis->setEnabled(false);
		m_lock->setEnabled(false);
		m_allowValve->setVisible(false);
	}
	schedule();
}

LevelRotationDialog::~LevelRotationDialog() {
	if (m_work) {
		m_work->cancelled = true;
	}
	// Worker owns immutable data and deletes its thread after finishing. It
	// never accesses the dialog, so closing a draft does not block the UI.
}
LevelMapRotationRequest LevelRotationDialog::request() const {
	LevelMapRotationRequest result;
	result.axis = m_axis->currentIndex();
	result.degrees = m_angle->value();
	result.textureLock = m_lock->isChecked();
	result.allowValve220 = m_allowValve->isChecked();
	if (m_pivotMode->currentIndex() == 1) {
		result.pivot = {0, 0, 0, true};
	}
	if (m_pivotMode->currentIndex() == 2) {
		result.pivot = {m_pivot[0]->value(), m_pivot[1]->value(), m_pivot[2]->value(), true};
	}
	return result;
}
void LevelRotationDialog::setRequest(const LevelMapRotationRequest& value) {
	m_updating = true;
	m_axis->setCurrentIndex(value.axis);
	m_angle->setValue(value.degrees);
	m_pivotMode->setCurrentIndex(value.pivot.valid ? 2 : 0);
	m_pivot[0]->setValue(value.pivot.x);
	m_pivot[1]->setValue(value.pivot.y);
	m_pivot[2]->setValue(value.pivot.z);
	m_lock->setChecked(value.textureLock);
	m_allowValve->setChecked(value.allowValve220);
	m_updating = false;
	schedule();
}
void LevelRotationDialog::setApplyHandler(std::function<bool(const LevelMapRotationRequest&, QString*)> handler) {
	m_applyHandler = std::move(handler);
}
void LevelRotationDialog::schedule() {
	if (m_updating) {
		return;
	}
	++m_generation;
	m_ready = false;
	m_valid = false;
	m_apply->setEnabled(false);
	m_progress->show();
	m_status->setText(tr("Building preview…"));
	for (auto* coordinate : m_pivot) {
		coordinate->setEnabled(m_pivotMode->currentIndex() == 2);
	}
	m_allowValve->setEnabled(m_lock->isChecked());
	if (m_work) {
		m_work->cancelled = true;
	}
	m_debounce->start();
}
void LevelRotationDialog::startPreview() {
	if (m_thread) {
		return;
	}
	auto work = std::make_shared<Work>();
	work->document = m_source;
	work->generation = m_generation;
	m_work = work;
	const auto rotation = request();
	m_thread = QThread::create([work, rotation] {
		const auto sourceRevision = work->document.revision;
		LevelPlacementRequest request;
		request.operation = LevelPlacementOperation::Rotate;
		request.rotation = rotation;
		LevelPlacementControl control;
		control.isCancelled = [work] { return work->cancelled.load(); };
		auto result = prepareLevelPlacement(work->document, request, control);
		work->valid = result.succeeded;
		work->error = result.error;
		if (work->valid) {
			work->document = std::move(result.document);
		}
		if (!work->valid || work->cancelled) {
			return;
		}
		if (work->document.revision != sourceRevision && !work->document.undoStack.isEmpty()) {
			for (const auto& brush : work->document.undoStack.last().brushResults) {
				for (const auto& before : work->document.undoStack.last().brushSnapshots) {
					if (before.id != brush.id) {
						continue;
					}
					for (int i = 0; i < brush.faces.size(); ++i) {
						if (brush.faces[i].explicitTextureAxes && !before.faces[i].explicitTextureAxes) {
							++work->converted;
						}
					}
				}
			}
		}
		LevelMapPreviewMeshOptions options;
		options.triangleLimit = 50000;
		options.isCancelled = [work] { return work->cancelled.load(); };
		work->mesh = buildLevelMapPreviewMesh(work->document, options);
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
		m_progress->hide();
		m_apply->setEnabled(m_valid);
		if (!m_valid) {
			m_status->setText(work->error);
			m_preview->setMesh({});
			return;
		}
		m_previewDocument = std::move(work->document);
		m_preview->setMesh(work->mesh.mesh);
		QString status = work->converted > 0 ? tr("Ready. %n face(s) will use Valve 220 axes.", nullptr, work->converted)
											 : tr("Ready. The rotation will be one undo step.");
		if (m_source.format == LevelMapFormat::DoomWad) {
			status +=
				QLatin1Char('\n') +
				(m_source.doomUdmf
					 ? tr("UDMF keeps fractional coordinates. Thing headings use whole degrees. Changed geometry requires a node rebuild.")
					 : tr("Coordinates round to whole WAD units. Changed geometry requires a node rebuild."));
		}
		if (work->mesh.truncated) {
			status += QLatin1Char('\n') + tr("The preview is limited to 50,000 triangles.");
		}
		m_status->setText(status);
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}
void LevelRotationDialog::accept() {
	if (!m_ready || !m_valid) {
		return;
	}
	QString error;
	if (m_applyHandler && !m_applyHandler(request(), &error)) {
		m_status->setText(error);
		return;
	}
	QDialog::accept();
}
} // namespace vibestudio
