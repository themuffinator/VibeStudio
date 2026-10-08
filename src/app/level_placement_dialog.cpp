#include "app/level_placement_dialog.h"
#include "app/model_viewport.h"
#include "core/level_dependencies.h"
#include "core/level_placement.h"
#include "core/map_preview_mesh.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio {
struct LevelPlacementDialog::Work {
	LevelMapDocument candidate;
	LevelPreviewAssets assets;
	LevelMapPreviewMesh mesh;
	QString error, dependencies;
	QMutex progressMutex;
	QString progressLabel;
	qint64 completed = 0, total = 0;
	std::atomic_bool cancelled{false};
	quint64 generation = 0;
	bool valid = false;
};

LevelPlacementDialog::LevelPlacementDialog(const LevelMapDocument& source, LevelPlacementMode mode, const QString& clipboardText,
										   std::shared_ptr<const PackageArchiveReader> archive, const QString& palette, QWidget* parent)
	: QDialog(parent), m_source(source), m_mode(mode), m_text(clipboardText), m_palette(palette), m_archive(std::move(archive)) {
	setObjectName(QStringLiteral("levelPlacementDialog"));
	setWindowTitle(mode == LevelPlacementMode::Duplicate ? tr("Duplicate with Offset") : tr("Paste with Offset"));
	setAccessibleName(windowTitle());
	resize(1120, 720);
	auto* layout = new QVBoxLayout(this);
	auto* split = new QSplitter(this);
	split->setChildrenCollapsible(false);
	layout->addWidget(split, 1);
	m_scroll = new QScrollArea(split);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_controls = new QWidget;
	auto* column = new QVBoxLayout(m_controls);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	column->addLayout(form);
	if (mode == LevelPlacementMode::Duplicate) {
		m_copies = new QSpinBox(m_controls);
		m_copies->setObjectName(QStringLiteral("placementCopies"));
		m_copies->setRange(1, kLevelMapMaxArrayCopies);
		m_copies->setValue(1);
		m_copies->setAccessibleName(tr("Copies"));
		m_copies->setAccessibleDescription(tr("Additional copies of the selection. Each copy advances by the offset below. All copies form one undo step."));
		m_copies->setToolTip(m_copies->accessibleDescription());
		m_copies->setLayoutDirection(Qt::LeftToRight);
		form->addRow(tr("&Copies"), m_copies);
		connect(m_copies, &QSpinBox::valueChanged, this, [this] { schedule(); });
	}
	for (int axis = 0; axis < 3; ++axis) {
		auto* value = new QDoubleSpinBox(m_controls);
		value->setObjectName(QStringLiteral("placementOffset%1").arg(axis));
		value->setAccessibleName(tr("Offset %1").arg(QString(QLatin1Char("XYZ"[axis]))));
		value->setAccessibleDescription(mode == LevelPlacementMode::Duplicate
			? tr("Offset per copy in map units. Copy 1 uses this offset; copy 2 uses twice the offset, and so on.")
			: tr("Translation in map units relative to the source objects. All inserted objects share this offset."));
		value->setToolTip(value->accessibleDescription());
		const bool udmf = source.doomFormat == LevelMapDoomFormat::Udmf;
		value->setRange(udmf ? -1e7 : -65536, udmf ? 1e7 : 65536);
		value->setDecimals(source.format == LevelMapFormat::DoomWad && !udmf ? 0 : 6);
		value->setSingleStep(16);
		value->setLayoutDirection(Qt::LeftToRight);
		form->addRow(value->accessibleName(), value);
		m_offset[axis] = value;
		connect(value, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
	}
	m_lock = new QCheckBox(tr("Lock textures to faces"), m_controls);
	m_lock->setObjectName(QStringLiteral("placementTextureLock"));
	m_lock->setAccessibleName(m_lock->text());
	m_lock->setToolTip(tr("Preserve each brush face's texture coordinates during placement. Patch UVs travel with their control points."));
	m_lock->setChecked(true);
	column->addWidget(m_lock);
	connect(m_lock, &QCheckBox::toggled, this, [this] { schedule(); });
	if (source.format == LevelMapFormat::DoomWad) {
		m_lock->setVisible(false);
		m_offset[2]->setEnabled(source.doomFormat == LevelMapDoomFormat::Hexen || source.doomFormat == LevelMapDoomFormat::Udmf);
	}
	m_status = new QLabel(m_controls);
	m_status->setObjectName(QStringLiteral("placementStatus"));
	m_status->setAccessibleName(tr("Placement preview status"));
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_status);
	m_progress = new QProgressBar(m_controls);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Building placement preview"));
	m_progress->setTextVisible(false);
	column->addWidget(m_progress);
	auto* details = new QPushButton(tr("Details…"), m_controls);
	details->setAccessibleName(tr("Placement assets and dependencies"));
	column->addWidget(details);
	connect(details, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Placement Details"));
		dialog.resize(720, 520);
		auto* layout = new QVBoxLayout(&dialog);
		auto* text = new QPlainTextEdit(m_details, &dialog);
		text->setReadOnly(true);
		text->setAccessibleName(tr("Placement diagnostics"));
		layout->addWidget(text);
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);
		dialog.exec();
	});
	column->addStretch();
	m_scroll->setWidget(m_controls);
	m_preview = new ModelViewport(split);
	m_preview->setObjectName(QStringLiteral("placementPreview"));
	m_preview->setAccessibleName(tr("Placed objects preview"));
	m_preview->setAccessibleDescription(
		tr("Preview of the inserted objects with materials and static models from the current package draft."));
	m_preview->setShowEdges(true);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	const auto preferences = StudioSettings().accessibilityPreferences();
	m_reducedMotion = preferences.reducedMotion;
	m_progress->setRange(0, m_reducedMotion ? 1 : 0);
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(preferences.reducedMotion);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	updateControlWidth();
	split->setSizes({m_scroll->minimumWidth(), 750});
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setText(tr("Place"));
	m_apply->setAccessibleName(tr("Place objects into map"));
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelPlacementDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &LevelPlacementDialog::reject);
	layout->addWidget(buttons);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(120);
	connect(m_debounce, &QTimer::timeout, this, &LevelPlacementDialog::startPreview);
	m_progressPoll = new QTimer(this);
	m_progressPoll->setInterval(40);
	connect(m_progressPoll, &QTimer::timeout, this, [this] {
		if (!m_work || m_ready || m_closed || m_work->generation != m_generation) {
			return;
		}
		QMutexLocker lock(&m_work->progressMutex);
		if (!m_work->progressLabel.isEmpty()) {
			m_status->setText(m_work->progressLabel);
		}
		if (m_work->total > 0) {
			m_progress->setRange(0, 1000);
			m_progress->setValue(static_cast<int>(1000.0 * std::clamp(m_work->completed, qint64(0), m_work->total) / m_work->total));
		} else {
			m_progress->setRange(0, m_reducedMotion ? 1 : 0);
		}
	});
	schedule();
}

LevelPlacementDialog::~LevelPlacementDialog() {
	if (m_work) {
		m_work->cancelled = true;
	}
}
LevelMapVec3 LevelPlacementDialog::offset() const { return {m_offset[0]->value(), m_offset[1]->value(), m_offset[2]->value(), true}; }
void LevelPlacementDialog::setOffset(const LevelMapVec3& offset) {
	m_updating = true;
	m_offset[0]->setValue(offset.x);
	m_offset[1]->setValue(offset.y);
	m_offset[2]->setValue(offset.z);
	m_updating = false;
	schedule();
}
void LevelPlacementDialog::setTextureLock(bool enabled) { m_lock->setChecked(enabled); }
void LevelPlacementDialog::setCopies(int count) { if (m_copies) { m_copies->setValue(count); } }
int LevelPlacementDialog::copies() const { return m_copies ? m_copies->value() : 1; }
void LevelPlacementDialog::updateControlWidth() {
	if (m_scroll && m_controls) {
		m_scroll->setMinimumWidth(std::max(280, m_controls->minimumSizeHint().width() + 28));
	}
}
void LevelPlacementDialog::changeEvent(QEvent* event) {
	QDialog::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange) {
		updateControlWidth();
	}
}
void LevelPlacementDialog::schedule() {
	if (m_updating || m_closed || !m_debounce) {
		return;
	}
	++m_generation;
	m_ready = false;
	m_valid = false;
	m_apply->setEnabled(false);
	m_status->setText(tr("Building placement preview…"));
	m_progress->setRange(0, m_reducedMotion ? 1 : 0);
	m_progress->show();
	if (m_work) {
		m_work->cancelled = true;
	}
	m_debounce->start();
}
void LevelPlacementDialog::startPreview() {
	if (m_thread || m_closed) {
		return;
	}
	auto work = std::make_shared<Work>();
	work->candidate = m_source;
	work->generation = m_generation;
	m_work = work;
	const auto mode = m_mode;
	const auto delta = offset();
	const auto copyCount = copies();
	const auto text = m_text;
	const auto archive = m_archive;
	const auto palette = m_palette;
	const LevelMapTextureLockOptions textures{m_lock->isChecked(), false};
	m_thread = QThread::create([work, mode, delta, copyCount, text, textures, archive, palette] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		const auto progress = [work](const QString& label, qint64 done = 0, qint64 total = 0) {
			QMutexLocker lock(&work->progressMutex);
			work->progressLabel = label;
			work->completed = done;
			work->total = total;
		};
		if (cancelled()) {
			return;
		}
		LevelPlacementRequest request;
		request.operation = mode == LevelPlacementMode::Duplicate ? LevelPlacementOperation::Duplicate : LevelPlacementOperation::Paste;
		request.offset = delta;
		request.copies = copyCount;
		request.text = text;
		request.textures = textures;
		LevelPlacementControl control;
		control.isCancelled = cancelled;
		control.progress = [progress](const auto& value) { progress(levelPlacementPhaseName(value.phase), value.completed, value.total); };
		auto result = prepareLevelPlacement(work->candidate, request, control);
		work->error = result.error;
		if (!result.succeeded || cancelled()) {
			return;
		}
		work->candidate = std::move(result.document);
		progress(tr("Preparing preview objects…"));
		// Preview only the added objects; package dependencies and actual native
		// insertion still see the full document. Never renumber Doom topology.
		auto visible = work->candidate;
		QSet<int> entities, brushes, patches;
		for (const auto& ref : visible.selection) {
			if (ref.kind == LevelMapSelectionKind::Entity) {
				entities.insert(ref.objectId);
			}
			if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
				brushes.insert(ref.objectId);
			}
			if (ref.kind == LevelMapSelectionKind::QuakePatch) {
				patches.insert(ref.objectId);
			}
		}
		if (visible.format != LevelMapFormat::DoomWad) {
			visible.entities.removeIf([&](const auto& e) { return !entities.contains(e.id); });
			visible.brushes.removeIf([&](const auto& b) { return !brushes.contains(b.id) && !entities.contains(b.entityId); });
			visible.patches.removeIf([&](const auto& p) { return !patches.contains(p.id) && !entities.contains(p.entityId); });
		}
		if (archive) {
			LevelPreviewAssetOptions options;
			options.paletteId = palette;
			progress(tr("Resolving preview assets…"));
			const auto assetProgress = [cancelled, progress](int done, int total) {
				progress(tr("Resolving preview assets…"), done, total);
				return !cancelled();
			};
			work->assets = resolveLevelPreviewAssets(visible, *archive, options, assetProgress);
			if (cancelled() || work->assets.cancelled) {
				return;
			}
			progress(tr("Checking placement dependencies…"));
			const auto report = inspectLevelDependencies(visible, *archive, [cancelled, progress](int done, int total) {
				progress(tr("Checking placement dependencies…"), done, total);
				return !cancelled();
			});
			work->dependencies = levelDependencyReportText(report);
			if (cancelled() || report.cancelled) {
				return;
			}
		}
		progress(tr("Building placement geometry…"));
		LevelMapPreviewMeshOptions options;
		options.textureSizes = levelPreviewTextureSizes(work->assets);
		options.modelMeshes = work->assets.models;
		options.isCancelled = cancelled;
		work->mesh = buildLevelMapPreviewMesh(visible, options);
		work->valid = !cancelled() && !work->mesh.cancelled;
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread = nullptr;
		if (m_closed) {
			return;
		}
		if (work->generation != m_generation) {
			if (!m_debounce->isActive()) {
				startPreview();
			}
			return;
		}
		m_ready = true;
		m_progressPoll->stop();
		m_valid = work->valid;
		m_apply->setEnabled(m_valid);
		m_progress->hide();
		m_candidate = std::move(work->candidate);
		m_preview->setMesh(work->mesh.mesh, m_preview->hasMesh());
		m_preview->setSurfaceSkins(levelPreviewSurfaceImages(work->mesh.mesh, work->assets));
		m_assets = work->assets;
		QString status = work->error;
		if (m_valid) {
			status = tr("Ready. %n object(s) will be placed in one undo step.", nullptr, m_candidate.selection.size());
			if (!m_archive) {
				status += '\n' + tr("Open a package or asset folder to preview materials and models.");
			} else if (work->assets.problemCount() || work->assets.unavailableModels || !work->assets.complete) {
				status += '\n' + tr("Some preview assets are unavailable. See Details.");
			}
			if (work->mesh.truncated) {
				status += '\n' + tr("The preview triangle limit was reached. All objects will still be placed.");
			}
			if (m_candidate.format == LevelMapFormat::DoomWad) {
				status += '\n' + tr("Thing coordinates round to whole WAD units. The preview shows the map geometry.");
			}
		}
		m_status->setText(status);
		m_details = status + QStringLiteral("\n\n") + levelPreviewAssetsText(work->assets) + QStringLiteral("\n\n") + work->dependencies;
		updateControlWidth();
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_progressPoll->start();
	m_thread->start();
}
void LevelPlacementDialog::accept() {
	if (!m_ready || !m_valid || m_closed) {
		return;
	}
	QString error;
	if (!m_applyHandler || !m_applyHandler(m_candidate, &error)) {
		m_status->setText(error.isEmpty() ? tr("No map is available for placement.") : error);
		return;
	}
	m_closed = true;
	QDialog::accept();
}
void LevelPlacementDialog::reject() {
	m_closed = true;
	m_debounce->stop();
	if (m_work) {
		m_work->cancelled = true;
	}
	QDialog::reject();
}
} // namespace vibestudio
