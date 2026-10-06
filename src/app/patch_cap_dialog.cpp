#include "app/patch_cap_dialog.h"
#include "app/model_viewport.h"
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
#include <QSet>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio
{
struct PatchCapDialog::Work {
	LevelPatchCapResult result;
	LevelPreviewAssets assets;
	LevelMapPreviewMesh mesh;
	QVector<int> highlights;
	QString error;
	std::atomic_bool cancelled{false};
	quint64 generation = 0;
	bool valid = false;
};
PatchCapDialog::PatchCapDialog(const LevelMapDocument &source, std::shared_ptr<const PackageArchiveReader> archive, const QString &palette,
							   QWidget *parent)
	: QDialog(parent), m_source(source), m_archive(std::move(archive)), m_palette(palette)
{
	setObjectName(QStringLiteral("patchCapDialog"));
	setWindowTitle(tr("Cap Patch"));
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
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	column->addLayout(form);
	const auto combo = [&](const QString &id, const QString &label, const QStringList &choices) {
		auto *box = new QComboBox(m_controls);
		box->setObjectName(id);
		box->setAccessibleName(label);
		box->addItems(choices);
		box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		int minimum = 16;
		for (const auto &choice : choices) {
			minimum = std::max(minimum, static_cast<int>(choice.size()));
		}
		box->setMinimumContentsLength(std::min(minimum, 40));
		form->addRow(label, box);
		return box;
	};
	m_patch = combo(QStringLiteral("capPatch"), tr("Source patch"), {});
	for (const auto &p : source.patches) {
		m_patch->addItem(tr("Patch %1 · %2").arg(p.id).arg(p.textureName), p.id);
	}
	if (source.selectionKind == LevelMapSelectionKind::QuakePatch) {
		m_patch->setCurrentIndex(m_patch->findData(source.selectedObjectId));
	}
	m_boundary =
		combo(QStringLiteral("capBoundary"), tr("Boundary"), {tr("First row"), tr("Last row"), tr("First column"), tr("Last column")});
	m_opposite = new QCheckBox(tr("Cap opposite boundary too"));
	m_opposite->setObjectName(QStringLiteral("capOpposite"));
	m_opposite->setAccessibleName(m_opposite->text());
	m_opposite->setChecked(true);
	form->addRow(m_opposite);
	m_texture = combo(QStringLiteral("capTexture"), tr("Material"), {});
	m_texture->setEditable(true);
	m_texture->addItem(tr("Use source material"), QString());
	m_texture->setMinimumContentsLength(std::max(16, static_cast<int>(m_texture->itemText(0).size())));
	for (const auto &use : levelMapTextureUsage(source)) {
		m_texture->addItem(use.name, use.name);
	}
	m_uv = combo(QStringLiteral("capUv"), tr("Texture mapping"), {tr("Planar"), tr("Match boundary")});
	const auto number = [&](const QString &id, const QString &label, double minimum, double maximum, double value) {
		auto *input = new QDoubleSpinBox(m_controls);
		input->setObjectName(id);
		input->setAccessibleName(label);
		input->setRange(minimum, maximum);
		input->setDecimals(6);
		input->setValue(value);
		input->setLayoutDirection(Qt::LeftToRight);
		form->addRow(label, input);
		return input;
	};
	m_units = number(QStringLiteral("capUnits"), tr("Map units per tile"), 0.000001, 1048576, 128);
	m_custom = new QCheckBox(tr("Custom center"));
	m_custom->setObjectName(QStringLiteral("capCustomCenter"));
	m_custom->setAccessibleName(m_custom->text());
	form->addRow(m_custom);
	m_x = number(QStringLiteral("capCenterX"), tr("Center X"), -1048576, 1048576, 0);
	m_y = number(QStringLiteral("capCenterY"), tr("Center Y"), -1048576, 1048576, 0);
	m_z = number(QStringLiteral("capCenterZ"), tr("Center Z"), -1048576, 1048576, 0);
	for (auto *c : {m_x, m_y, m_z}) {
		c->setEnabled(false);
	}
	m_invert = new QCheckBox(tr("Invert cap facing"));
	m_invert->setObjectName(QStringLiteral("capInvert"));
	m_invert->setAccessibleName(m_invert->text());
	form->addRow(m_invert);
	m_view = combo(QStringLiteral("capView"), tr("Preview"), {tr("Source and caps"), tr("Caps only"), tr("Original patch")});
	auto *frame = new QPushButton(tr("Frame patches"));
	column->addWidget(frame);
	auto *details = new QPushButton(tr("Cap and material details"));
	column->addWidget(details);
	connect(details, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Cap and material details"));
		dialog.resize(760, 500);
		auto *body = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(&dialog);
		text->setReadOnly(true);
		text->setAccessibleName(tr("Patch cap report and resolved materials"));
		text->setPlainText(QString::fromUtf8(QJsonDocument(levelPatchCapReportJson(m_result)).toJson()) + '\n' +
						   levelPreviewAssetsText(m_assets));
		body->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		body->addWidget(buttons);
		dialog.exec();
	});
	m_status = new QPlainTextEdit(m_controls);
	m_status->setObjectName(QStringLiteral("capStatus"));
	m_status->setAccessibleName(tr("Patch cap status"));
	m_status->setReadOnly(true);
	m_status->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	column->addWidget(m_status);
	m_progress = new QProgressBar;
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Preparing cap and material preview"));
	column->addWidget(m_progress);
	column->addStretch();
	m_scroll->setWidget(m_controls);
	m_preview = new ModelViewport(split);
	m_preview->setAccessibleName(tr("Patch cap preview"));
	m_preview->setAccessibleDescription(tr("New caps are hatched. Choose Original patch to compare with the source."));
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
	m_apply->setText(tr("Add Caps"));
	m_apply->setAccessibleName(tr("Apply patch caps"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &PatchCapDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(100);
	connect(m_debounce, &QTimer::timeout, this, &PatchCapDialog::startPreview);
	for (auto *c : {m_patch, m_boundary, m_uv, m_view}) {
		connect(c, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
	}
	connect(m_texture, &QComboBox::currentTextChanged, this, [this] { schedule(); });
	for (auto *c : {m_units, m_x, m_y, m_z}) {
		connect(c, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
	}
	for (auto *c : {m_opposite, m_invert}) {
		connect(c, &QCheckBox::toggled, this, [this] { schedule(); });
	}
	connect(m_custom, &QCheckBox::toggled, this, [this](bool enabled) {
		if (enabled) {
			m_opposite->setChecked(false);
		}
		m_opposite->setEnabled(!enabled);
		for (auto *c : {m_x, m_y, m_z}) {
			c->setEnabled(enabled);
		}
		schedule();
	});
	connect(m_uv, &QComboBox::currentIndexChanged, this, [this](int index) { m_units->setEnabled(index == 0); });
	schedule();
}
PatchCapDialog::~PatchCapDialog()
{
	if (m_work) {
		m_work->cancelled = true;
	}
}
int PatchCapDialog::patchId() const { return m_patch->currentIndex() < 0 ? -1 : m_patch->currentData().toInt(); }
LevelPatchCapRequest PatchCapDialog::request() const
{
	LevelPatchCapRequest r;
	const int edge = m_boundary->currentIndex();
	r.boundaries = {static_cast<LevelPatchBoundary>(edge)};
	if (m_opposite->isChecked()) {
		r.boundaries << static_cast<LevelPatchBoundary>(edge ^ 1);
	}
	r.texture = m_texture->currentText() == m_texture->itemText(m_texture->currentIndex()) ? m_texture->currentData().toString()
																						   : m_texture->currentText();
	r.uv = static_cast<LevelPatchCapUv>(m_uv->currentIndex());
	r.unitsPerTile = m_units->value();
	r.customCenter = m_custom->isChecked();
	r.center = {m_x->value(), m_y->value(), m_z->value(), true};
	r.invert = m_invert->isChecked();
	return r;
}
void PatchCapDialog::updateControlWidth()
{
	if (m_controls && m_scroll && m_status) {
		// Reserve the short selected label; long material paths remain available
		// in the popup and tooltip without forcing an unbounded panel width.
		m_patch->setMinimumContentsLength(std::clamp(static_cast<int>(m_patch->currentText().size()), 16, 40));
		m_patch->setToolTip(m_patch->currentText());
		m_status->setMinimumHeight(m_status->fontMetrics().lineSpacing() * 4 + 12);
		m_status->setMaximumHeight(m_status->fontMetrics().lineSpacing() * 8 + 12);
		m_controls->layout()->activate();
		m_scroll->setMinimumWidth(
			std::max(330, m_controls->minimumSizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 12));
	}
}
void PatchCapDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::LayoutDirectionChange) {
		updateControlWidth();
	}
}
void PatchCapDialog::schedule()
{
	++m_generation;
	m_ready = m_valid = false;
	m_apply->setEnabled(false);
	m_progress->show();
	m_status->setPlainText(tr("Preparing cap and material preview…"));
	if (m_work) {
		m_work->cancelled = true;
	}
	m_debounce->start();
}
void PatchCapDialog::startPreview()
{
	if (m_thread) {
		return;
	}
	auto work = std::make_shared<Work>();
	work->generation = m_generation;
	m_work = work;
	auto visible = m_source;
	const int id = patchId(), view = m_view->currentIndex();
	const auto r = request();
	const auto palette = m_palette;
	const auto archive = m_archive;
	m_thread = QThread::create([work, visible, id, view, r, archive, palette]() mutable {
		const auto cancelled = [work] { return work->cancelled.load(); };
		const auto source = std::find_if(visible.patches.cbegin(), visible.patches.cend(), [&](const auto &p) { return p.id == id; });
		if (source == visible.patches.cend()) {
			work->error = QCoreApplication::translate("PatchCapDialog", "Choose an existing patch.");
		} else {
			work->valid = prepareLevelPatchCaps(*source, r, &work->result, &work->error, cancelled);
			if (work->valid && !cancelled()) {
				QVector<int> ids;
				work->valid = addLevelMapPatches(&visible, work->result.caps, source->entityId, &ids, &work->error);
				if (work->valid) {
					for (int i = 0; i < ids.size(); ++i) {
						work->result.caps[i].id = ids[i];
					}
				}
			}
		}
		if (cancelled()) {
			return;
		}
		QSet<int> capIds;
		if (work->valid) {
			for (const auto &p : work->result.caps) {
				capIds.insert(p.id);
			}
		}
		visible.brushes.clear();
		visible.entities.clear();
		visible.patches.erase(std::remove_if(visible.patches.begin(), visible.patches.end(),
											 [&](const auto &p) {
												 return !validateLevelPatch(p) ||
														(p.id == id ? view == 1 && work->valid : view == 2 || !capIds.contains(p.id));
											 }),
							  visible.patches.end());
		if (archive) {
			LevelPreviewAssetOptions options;
			options.paletteId = palette;
			work->assets = resolveLevelPreviewAssets(visible, *archive, options, [cancelled](int, int) { return !cancelled(); });
		}
		if (cancelled()) {
			return;
		}
		for (auto &p : visible.patches) {
			p.subdivisionsX = std::min(p.subdivisionsX, 8);
			p.subdivisionsY = std::min(p.subdivisionsY, 8);
		}
		LevelMapPreviewMeshOptions options;
		options.triangleLimit = 100000;
		options.isCancelled = cancelled;
		options.textureSizes = levelPreviewTextureSizes(work->assets);
		work->mesh = buildLevelMapPreviewMesh(visible, options);
		for (int i = 0; i < work->mesh.owners.size(); ++i) {
			if (capIds.contains(work->mesh.owners[i].objectId)) {
				work->highlights << i;
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
			text = tr("%1 caps ready. The source patch stays intact.").arg(m_result.caps.size());
			text += '\n' + m_result.warnings.join('\n');
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
void PatchCapDialog::setApplyHandler(std::function<bool(int, const LevelPatchCapRequest &, QString *)> handler)
{
	m_handler = std::move(handler);
}
void PatchCapDialog::accept()
{
	if (!m_ready || !m_valid) {
		return;
	}
	QString error;
	if (m_handler && !m_handler(patchId(), request(), &error)) {
		m_status->setPlainText(error);
		return;
	}
	QDialog::accept();
}
} // namespace vibestudio
