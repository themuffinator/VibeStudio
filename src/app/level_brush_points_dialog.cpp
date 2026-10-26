#include "app/level_brush_points_dialog.h"
#include "app/model_viewport.h"
#include "core/map_preview_mesh.h"
#include "core/level_placement_control_p.h"
#include "core/studio_settings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
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
struct LevelBrushPointsDialog::Work {
	std::atomic_bool cancelled {false};
	quint64 generation = 0;
	LevelMapDocument document;
	LevelBrushPointsReport report;
	LevelMapPreviewMesh mesh;
	LevelPreviewAssets assets;
	QString error;
	bool valid = false;
};

LevelBrushPointsDialog::LevelBrushPointsDialog(const LevelMapDocument& source, const QString& points, const QString& material,
	const QString& selectionPoints, std::shared_ptr<const PackageArchiveReader> archive, const QString& palette, QWidget* parent)
	: QDialog(parent), m_source(source), m_archive(std::move(archive)), m_palette(palette), m_selectionPoints(selectionPoints)
{
	setObjectName(QStringLiteral("levelBrushPointsDialog"));
	setWindowTitle(tr("Create Brush from Points"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Type four or more points. The new brush is the convex solid that encloses them."));
	if (parent) {
		setLayoutDirection(parent->layoutDirection());
	}
	resize(1100, 720);
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

	auto* pointsLabel = new QLabel(tr("&Points"), controls);
	column->addWidget(pointsLabel);
	m_points = new QPlainTextEdit(controls);
	m_points->setObjectName(QStringLiteral("brushPointsText"));
	m_points->setAccessibleName(tr("Points"));
	m_points->setAccessibleDescription(tr("One x y z per line, x,y,z groups separated by spaces, or (x y z) groups. Text after // is ignored."));
	m_points->setToolTip(m_points->accessibleDescription());
	m_points->setPlaceholderText(QStringLiteral("0,0,0 128,0,0 0,128,0 0,0,64"));
	m_points->setTabChangesFocus(true);
	// Coordinates read left to right in every layout direction.
	m_points->setLayoutDirection(Qt::LeftToRight);
	m_points->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	m_points->setPlainText(points);
	pointsLabel->setBuddy(m_points);
	column->addWidget(m_points, 1);

	m_addSelection = new QPushButton(tr("Add Selected &Corners"), controls);
	m_addSelection->setObjectName(QStringLiteral("brushPointsAddSelection"));
	m_addSelection->setAccessibleName(tr("Add selected corners"));
	m_addSelection->setToolTip(tr("Append the corners of the selected brushes, or of the picked faces, edges and vertices, as points."));
	m_addSelection->setEnabled(!m_selectionPoints.trimmed().isEmpty());
	connect(m_addSelection, &QPushButton::clicked, this, [this] {
		if (m_selectionPointsProvider) {
			m_selectionPoints = m_selectionPointsProvider();
		}
		QString text = m_points->toPlainText();
		if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
			text += QLatin1Char('\n');
		}
		setPointsText(text + m_selectionPoints);
	});
	column->addWidget(m_addSelection);

	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(form);
	m_material = new QComboBox(controls);
	m_material->setObjectName(QStringLiteral("brushPointsMaterial"));
	m_material->setEditable(true);
	m_material->setInsertPolicy(QComboBox::NoInsert);
	m_material->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_material->setMinimumContentsLength(16);
	m_material->setAccessibleName(tr("Material for every face"));
	m_material->setAccessibleDescription(tr("Choose a material used by this map or enter its asset path."));
	for (const auto& use : levelMapTextureUsage(source)) {
		m_material->addItem(use.name);
	}
	m_material->setCurrentText(material);
	form->addRow(tr("&Material"), m_material);

	auto* frame = new QPushButton(tr("Frame brush"), controls);
	frame->setAccessibleName(frame->text());
	column->addWidget(frame);
	m_status = new QLabel(controls);
	m_status->setObjectName(QStringLiteral("brushPointsStatus"));
	m_status->setAccessibleName(tr("Brush from points status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_status);
	m_progress = new QProgressBar(controls);
	m_progress->setAccessibleName(tr("Checking the brush"));
	m_progress->setTextVisible(false);
	column->addWidget(m_progress);

	m_preview = new ModelViewport(splitter);
	m_preview->setObjectName(QStringLiteral("brushPointsPreview"));
	m_preview->setAccessibleName(tr("Brush from points preview"));
	m_preview->setAccessibleDescription(tr("The new brush with package material images. Missing images use flat shading."));
	m_preview->setShowEdges(true);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	const auto preferences = StudioSettings().accessibilityPreferences();
	m_preview->setReducedMotion(preferences.reducedMotion);
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_progress->setRange(0, preferences.reducedMotion ? 1 : 0);
	connect(frame, &QPushButton::clicked, m_preview, &ModelViewport::frameModel);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	scroll->setMinimumWidth(std::max(360, controls->minimumSizeHint().width() + 24));

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	m_apply = buttons->addButton(tr("Create Brush"), QDialogButtonBox::AcceptRole);
	m_apply->setObjectName(QStringLiteral("brushPointsApply"));
	m_apply->setAccessibleName(tr("Create brush from points"));
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelBrushPointsDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &LevelBrushPointsDialog::reject);
	layout->addWidget(buttons);

	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(150);
	connect(m_debounce, &QTimer::timeout, this, &LevelBrushPointsDialog::start);
	connect(m_points, &QPlainTextEdit::textChanged, this, &LevelBrushPointsDialog::schedule);
	connect(m_material, &QComboBox::currentTextChanged, this, &LevelBrushPointsDialog::schedule);
	m_points->setFocus(Qt::OtherFocusReason);
	schedule();
}

LevelBrushPointsDialog::~LevelBrushPointsDialog()
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

QString LevelBrushPointsDialog::pointsText() const
{
	return m_points->toPlainText();
}

void LevelBrushPointsDialog::setPointsText(const QString& text)
{
	m_points->setPlainText(text);
}

void LevelBrushPointsDialog::setSelectionPointsProvider(std::function<QString()> provider, bool available)
{
	m_selectionPointsProvider = std::move(provider);
	m_addSelection->setEnabled(available);
}

QString LevelBrushPointsDialog::material() const
{
	return m_material->currentText().trimmed();
}

QString LevelBrushPointsDialog::statusText() const
{
	return m_status->text();
}

void LevelBrushPointsDialog::showStatus(const QString& text)
{
	m_status->setText(text);
	m_status->setAccessibleDescription(text);
}

void LevelBrushPointsDialog::schedule()
{
	if (m_closed || !m_debounce) {
		return;
	}
	++m_generation;
	m_ready = m_valid = false;
	m_apply->setEnabled(false);
	if (m_work) {
		m_work->cancelled = true;
	}
	m_progress->show();
	showStatus(tr("Checking the brush…"));
	m_debounce->start();
}

void LevelBrushPointsDialog::start()
{
	if (m_thread || m_closed) {
		return;
	}
	LevelBrushPointsRequest request;
	request.texture = material();
	QString error;
	// Reading the text is quick; only a well-formed list goes to the worker.
	if (!parseLevelBrushPoints(m_points->toPlainText(), &request.points, &error) || request.points.size() < kLevelBrushPointsMinimum) {
		m_ready = true;
		m_valid = false;
		m_progress->hide();
		m_preview->clearMesh();
		const int count = static_cast<int>(request.points.size());
		showStatus(error.isEmpty() ? tr("Type at least %1 points. Entered: %n point(s).", nullptr, count).arg(kLevelBrushPointsMinimum) : error);
		return;
	}
	auto work = std::make_shared<Work>();
	work->generation = m_generation;
	work->document = m_source;
	m_work = work;
	m_thread = QThread::create([work, request, archive = m_archive, palette = m_palette] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		try {
			const LevelPlacementControl control {cancelled, {}};
			const detail::PlacementControlScope scope(control);
			work->valid = addLevelMapBrushFromPoints(&work->document, request, &work->report, &work->error);
		} catch (const detail::PlacementCancelled&) {
			work->valid = false;
			return;
		}
		if (!work->valid || cancelled()) {
			return;
		}
		auto visible = work->document;
		visible.brushes.removeIf([&](const LevelMapBrush& brush) { return brush.id != work->report.brushId; });
		visible.patches.clear();
		visible.entities.clear();
		if (archive) {
			LevelPreviewAssetOptions options;
			options.paletteId = palette;
			work->assets = resolveLevelPreviewAssets(visible, *archive, options, [cancelled](int, int) { return !cancelled(); });
		}
		if (cancelled()) {
			return;
		}
		LevelMapPreviewMeshOptions options;
		options.isCancelled = cancelled;
		options.textureSizes = levelPreviewTextureSizes(work->assets);
		work->mesh = buildLevelMapPreviewMesh(visible, options);
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread->deleteLater();
		m_thread = nullptr;
		if (m_closed) {
			return;
		}
		if (work->generation != m_generation) {
			if (!m_debounce->isActive()) {
				start();
			}
			return;
		}
		m_progress->hide();
		m_ready = true;
		m_valid = work->valid && !work->cancelled;
		m_apply->setEnabled(m_valid);
		if (!m_valid) {
			m_preview->clearMesh();
			showStatus(work->error);
			return;
		}
		m_candidate = work->document;
		m_report = work->report;
		m_preview->setMesh(work->mesh.mesh, m_preview->hasMesh());
		m_preview->setSurfaceSkins(levelPreviewSurfaceImages(work->mesh.mesh, work->assets));
		const int unused = m_report.unusedPoints;
		QString status = tr("Ready. Corners: %1. Faces: %2. Input points: %3. Face format: %4.")
							 .arg(m_report.corners)
							 .arg(m_report.faces)
							 .arg(m_report.inputPoints)
							 .arg(m_report.faceDialect);
		if (unused > 0) {
			status += QLatin1Char(' ') + tr("Unused, inside the brush or on its faces: %n point(s).", nullptr, unused);
		}
		showStatus(status);
	});
	m_thread->start();
}

void LevelBrushPointsDialog::accept()
{
	if (!m_ready || !m_valid || m_closed) {
		return;
	}
	QString error;
	if (m_applyHandler && !m_applyHandler(m_candidate, &error)) {
		showStatus(error);
		return;
	}
	m_closed = true;
	QDialog::accept();
}

void LevelBrushPointsDialog::reject()
{
	m_closed = true;
	m_debounce->stop();
	if (m_work) {
		m_work->cancelled = true;
	}
	QDialog::reject();
}
} // namespace vibestudio
