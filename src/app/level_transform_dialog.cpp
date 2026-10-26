#include "app/level_transform_dialog.h"
#include "app/model_viewport.h"
#include "core/map_preview_mesh.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHash>
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

namespace vibestudio
{
struct LevelTransformDialog::Work {
	LevelMapDocument document;
	LevelMapPreviewMesh mesh;
	QString error;
	std::atomic_bool cancelled{false};
	bool valid = false;
	int converted = 0;
	quint64 generation = 0;
};

LevelTransformDialog::LevelTransformDialog(const LevelMapDocument& source, QWidget* parent) : QDialog(parent), m_source(source)
{
	resize(1100, 720);
	auto* layout = new QVBoxLayout(this);
	auto* split = new QSplitter(this);
	split->setChildrenCollapsible(false);
	layout->addWidget(split, 1);
	auto* scroll = new QScrollArea(split);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setMinimumWidth(270);
	m_controls = new QWidget;
	auto* column = new QVBoxLayout(m_controls);
	m_values = new QFormLayout;
	m_values->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(m_values);
	auto* pivotForm = new QFormLayout;
	pivotForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(pivotForm);
	m_pivotMode = new QComboBox(m_controls);
	m_pivotMode->setObjectName(QStringLiteral("transformPivotMode"));
	m_pivotMode->setAccessibleName(tr("Pivot"));
	m_pivotMode->setAccessibleDescription(tr("The point that stays where it is."));
	m_pivotMode->addItems({tr("Selection centre"), tr("World origin"), tr("Custom coordinates")});
	pivotForm->addRow(tr("Pivot"), m_pivotMode);
	for (int i = 0; i < 3; ++i) {
		auto* coordinate = new QDoubleSpinBox(m_controls);
		coordinate->setDecimals(6);
		coordinate->setLayoutDirection(Qt::LeftToRight);
		coordinate->setObjectName(QStringLiteral("transformPivot%1").arg(i));
		coordinate->setAccessibleName(tr("Pivot %1").arg(QString(QLatin1Char("XYZ"[i]))));
		pivotForm->addRow(coordinate->accessibleName(), coordinate);
		m_pivot[i] = coordinate;
		connect(coordinate, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
	}
	m_lock = new QCheckBox(m_controls);
	m_lock->setObjectName(QStringLiteral("transformTextureLock"));
	m_lock->setChecked(true);
	column->addWidget(m_lock);
	m_allowValve = new QCheckBox(tr("Allow Valve 220 conversion"), m_controls);
	m_allowValve->setObjectName(QStringLiteral("transformAllowValve"));
	m_allowValve->setAccessibleName(m_allowValve->text());
	m_allowValve->setToolTip(tr("If locked textures need explicit axes, convert all classic faces in the map to Valve 220 while "
								"preserving their appearance. Check that the target compiler accepts Valve 220 maps."));
	column->addWidget(m_allowValve);
	m_status = new QLabel(m_controls);
	m_status->setObjectName(QStringLiteral("transformStatus"));
	m_status->setAccessibleName(tr("Preview status"));
	m_status->setWordWrap(true);
	// Worker status changes after the splitter is laid out. A long translated
	// token must wrap inside the controls, not widen the scrollable column.
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_status);
	m_progress = new QProgressBar(m_controls);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Building preview"));
	column->addWidget(m_progress);
	column->addStretch();
	scroll->setWidget(m_controls);
	m_preview = new ModelViewport(split);
	m_preview->setObjectName(QStringLiteral("transformGeometryPreview"));
	m_preview->setAccessibleName(tr("Geometry preview"));
	m_preview->setAccessibleDescription(tr("Geometry preview of the edited map. Material images are not loaded in this view."));
	m_preview->setShowEdges(true);
	const auto preferences = StudioSettings().accessibilityPreferences();
	if (preferences.reducedMotion) {
		m_progress->setRange(0, 1);
	}
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(preferences.reducedMotion);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
	m_ok = buttons->button(QDialogButtonBox::Ok);
	m_ok->setObjectName(QStringLiteral("transformOk"));
	m_ok->setAccessibleName(tr("Apply and close"));
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName(QStringLiteral("transformApply"));
	m_apply->setAccessibleName(tr("Apply and keep open"));
	m_apply->setToolTip(tr("Apply to the current selection and keep this window open for another step."));
	connect(m_ok, &QPushButton::clicked, this, &LevelTransformDialog::accept);
	connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
	connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QDialog::reject);
	layout->addWidget(buttons);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(150);
	connect(m_debounce, &QTimer::timeout, this, &LevelTransformDialog::startPreview);
	connect(m_pivotMode, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	connect(m_lock, &QCheckBox::toggled, this, [this] { schedule(); });
	connect(m_allowValve, &QCheckBox::toggled, this, [this] { schedule(); });
}

LevelTransformDialog::~LevelTransformDialog()
{
	if (m_work) {
		m_work->cancelled = true;
	}
	// Worker owns immutable data and deletes its thread after finishing. It
	// never accesses the dialog, so closing a draft does not block the UI.
}

void LevelTransformDialog::finishSetup()
{
	m_finished = true;
	refreshPivotFields();
	sourceChanged();
	// Reserve the actual translated control width at the current text scale.
	// A fixed splitter width clipped LTR coordinates inside an RTL form at 200%.
	m_controls->ensurePolished();
	m_controls->layout()->activate();
	const int controlsWidth = std::max(300, m_controls->minimumSizeHint().width() + 28);
	if (auto* scroll = findChild<QScrollArea*>()) {
		scroll->setMinimumWidth(controlsWidth);
	}
	if (auto* split = findChild<QSplitter*>()) {
		split->setSizes({controlsWidth, 750});
	}
	schedule();
}

QDoubleSpinBox* LevelTransformDialog::addValueField(const QString& objectName, const QString& label, const QString& accessibleName,
	const QString& description, double minimum, double maximum, double value, int decimals, double step)
{
	auto* field = new QDoubleSpinBox(m_controls);
	field->setObjectName(objectName);
	field->setRange(minimum, maximum);
	field->setDecimals(decimals);
	field->setSingleStep(step);
	field->setValue(value);
	field->setLayoutDirection(Qt::LeftToRight);
	field->setAccessibleName(accessibleName);
	field->setAccessibleDescription(description);
	field->setToolTip(description);
	m_values->addRow(label, field);
	connect(field, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
	return field;
}

void LevelTransformDialog::setSource(const LevelMapDocument& source)
{
	m_source = source;
	m_updating = true;
	refreshPivotFields();
	sourceChanged();
	m_updating = false;
	schedule();
}

void LevelTransformDialog::refreshPivotFields()
{
	const double limit = m_source.doomUdmf ? 1e7 : 32768;
	LevelMapVec3 low, high;
	const bool bounded = levelMapSelectionBounds(m_source, &low, &high);
	const std::array<double, 3> centre{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
	const bool previous = m_updating;
	m_updating = true;
	for (int i = 0; i < 3; ++i) {
		m_pivot[i]->setRange(-limit, limit);
		// Typed coordinates stay; the others show the selection's centre.
		if (m_pivotMode->currentIndex() != 2 && bounded) {
			m_pivot[i]->setValue(centre[i]);
		}
	}
	const bool doom = m_source.format == LevelMapFormat::DoomWad;
	m_lock->setEnabled(!doom);
	m_allowValve->setVisible(!doom);
	m_updating = previous;
}

LevelMapVec3 LevelTransformDialog::pivot() const
{
	if (m_pivotMode->currentIndex() == 1) {
		return {0, 0, 0, true};
	}
	if (m_pivotMode->currentIndex() == 2) {
		return {m_pivot[0]->value(), m_pivot[1]->value(), m_pivot[2]->value(), true};
	}
	return {};
}

void LevelTransformDialog::setPivot(const LevelMapVec3& value)
{
	const bool previous = m_updating;
	m_updating = true;
	m_pivotMode->setCurrentIndex(value.valid ? 2 : 0);
	if (value.valid) {
		m_pivot[0]->setValue(value.x);
		m_pivot[1]->setValue(value.y);
		m_pivot[2]->setValue(value.z);
	} else {
		refreshPivotFields();
	}
	m_updating = previous;
}

bool LevelTransformDialog::textureLock() const { return m_lock->isChecked(); }
bool LevelTransformDialog::allowValve220() const { return m_allowValve->isChecked(); }

void LevelTransformDialog::setTextureOptions(bool lock, bool allowValve220)
{
	const bool previous = m_updating;
	m_updating = true;
	m_lock->setChecked(lock);
	m_allowValve->setChecked(allowValve220);
	m_updating = previous;
}

void LevelTransformDialog::setTextureLockText(const QString& text, const QString& toolTip)
{
	m_lock->setText(text);
	m_lock->setAccessibleName(text);
	m_lock->setToolTip(toolTip);
}

void LevelTransformDialog::setBlockedReason(const QString& reason)
{
	if (m_blocked == reason) {
		return;
	}
	m_blocked = reason;
	schedule();
}

void LevelTransformDialog::setPlacementAdapter(std::function<LevelPlacementRequest(const LevelPlacementRequest&)> adapter)
{
	m_adapter = std::move(adapter);
}

void LevelTransformDialog::setStatusNote(const QString& note)
{
	m_note = note;
}

void LevelTransformDialog::schedule()
{
	if (m_updating || !m_finished) {
		return;
	}
	++m_generation;
	m_ready = false;
	m_valid = false;
	m_ok->setEnabled(false);
	m_apply->setEnabled(false);
	for (auto* coordinate : m_pivot) {
		coordinate->setEnabled(m_pivotMode->currentIndex() == 2);
	}
	m_allowValve->setEnabled(m_lock->isChecked());
	if (m_work) {
		m_work->cancelled = true;
	}
	if (!m_blocked.isEmpty()) {
		// Nothing to preview: say why, and let the user see it at once.
		m_debounce->stop();
		m_progress->hide();
		m_ready = true;
		m_status->setText(m_blocked);
		m_preview->setMesh({});
		return;
	}
	m_progress->show();
	m_status->setText(tr("Building preview…"));
	m_debounce->start();
}

void LevelTransformDialog::startPreview()
{
	if (m_thread) {
		return;
	}
	// Values no service accepts, whatever it applies to, fail at once.
	if (const QString problem = requestProblem(); !problem.isEmpty()) {
		m_ready = true;
		m_valid = false;
		m_progress->hide();
		m_status->setText(problem);
		m_preview->setMesh({});
		return;
	}
	auto work = std::make_shared<Work>();
	work->document = m_source;
	work->generation = m_generation;
	m_work = work;
	const auto request = m_adapter ? m_adapter(placementRequest()) : placementRequest();
	m_thread = QThread::create([work, request] {
		LevelPlacementControl control;
		control.isCancelled = [work] { return work->cancelled.load(); };
		auto result = prepareLevelPlacement(work->document, request, control);
		work->valid = result.succeeded;
		work->error = result.error;
		if (!work->valid || work->cancelled) {
			return;
		}
		// Faces whose texture lock needed explicit Valve 220 axes.
		QHash<int, qsizetype> before;
		for (qsizetype i = 0; i < work->document.brushes.size(); ++i) {
			before.insert(work->document.brushes[i].id, i);
		}
		for (const auto& brush : result.document.brushes) {
			const auto found = before.constFind(brush.id);
			if (found == before.constEnd()) {
				continue;
			}
			const auto& source = work->document.brushes[*found];
			for (qsizetype f = 0; f < brush.faces.size() && f < source.faces.size(); ++f) {
				if (brush.faces[f].explicitTextureAxes && !source.faces[f].explicitTextureAxes) {
					++work->converted;
				}
			}
		}
		work->document = std::move(result.document);
		LevelMapPreviewMeshOptions options;
		options.triangleLimit = 50000;
		options.isCancelled = [work] { return work->cancelled.load(); };
		work->mesh = buildLevelMapPreviewMesh(work->document, options);
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread = nullptr;
		if (work->generation != m_generation) {
			if (!m_debounce->isActive() && m_blocked.isEmpty()) {
				startPreview();
			}
			return;
		}
		m_ready = true;
		m_valid = work->valid;
		m_progress->hide();
		m_ok->setEnabled(m_valid);
		m_apply->setEnabled(m_valid);
		if (!m_valid) {
			m_status->setText(work->error);
			m_preview->setMesh({});
			return;
		}
		m_previewDocument = std::move(work->document);
		m_preview->setMesh(work->mesh.mesh);
		QString status = readyText();
		if (!m_note.isEmpty()) {
			status += QLatin1Char('\n') + m_note;
		}
		if (work->converted > 0) {
			status += QLatin1Char('\n') + tr("%n face(s) will use Valve 220 axes.", nullptr, work->converted);
		}
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

bool LevelTransformDialog::apply()
{
	if (!m_blocked.isEmpty()) {
		m_status->setText(m_blocked);
		return false;
	}
	if (!m_ready || !m_valid) {
		return false;
	}
	QString error;
	if (!commit(&error)) {
		m_status->setText(error);
		return false;
	}
	return true;
}

void LevelTransformDialog::accept()
{
	if (apply()) {
		QDialog::accept();
	}
}
} // namespace vibestudio
