#include "app/level_prefab_dialog.h"
#include "app/model_viewport.h"
#include "core/level_dependencies.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
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

namespace vibestudio
{
struct LevelPrefabDialog::Work {
	LevelMapDocument candidate;
	LevelPrefab prefab;
	QByteArray bytes;
	LevelPrefabReport report;
	LevelPrefabWriteReport written;
	LevelPreviewAssets assets;
	LevelMapPreviewMesh mesh;
	QString error, assetKey, dependencyDetails;
	quint64 generation = 0;
	std::atomic_bool cancelled{false};
	bool valid = false;
};
LevelPrefabDialog::LevelPrefabDialog(const LevelMapDocument &source, LevelPrefabDialogMode mode, const QString &input, bool packageEntry,
									 std::shared_ptr<const PackageArchiveReader> archive, const QString &palette, QWidget *parent)
	: QDialog(parent), m_mode(mode), m_source(source), m_input(input), m_palette(palette), m_packageEntry(packageEntry),
	  m_archive(std::move(archive))
{
	const bool inserting = mode == LevelPrefabDialogMode::Insert;
	setObjectName(QStringLiteral("levelPrefabDialog"));
	setWindowTitle(inserting ? tr("Insert Prefab") : mode == LevelPrefabDialogMode::Stage ? tr("Stage Prefab") : tr("Export Prefab"));
	setAccessibleName(windowTitle());
	resize(1200, 840);
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
	m_name = new QLineEdit(m_controls);
	m_name->setObjectName(QStringLiteral("prefabName"));
	m_name->setAccessibleName(tr("Prefab name"));
	m_name->setMaxLength(128);
	m_name->setText(tr("New prefab"));
	m_name->setReadOnly(inserting);
	form->addRow(tr("Name"), m_name);
	m_description = new QPlainTextEdit(m_controls);
	m_description->setObjectName(QStringLiteral("prefabDescription"));
	m_description->setAccessibleName(tr("Prefab description"));
	m_description->setMaximumHeight(110);
	m_description->setReadOnly(inserting);
	form->addRow(tr("Description"), m_description);
	if (!inserting) {
		m_autoAnchor = new QCheckBox(tr("Anchor at bottom centre"), m_controls);
		m_autoAnchor->setObjectName(QStringLiteral("prefabAutoAnchor"));
		m_autoAnchor->setAccessibleName(m_autoAnchor->text());
		m_autoAnchor->setChecked(true);
		form->addRow(m_autoAnchor);
	}
	for (int group = 0; group < (inserting ? 2 : 1); ++group) {
		for (int axis = 0; axis < 3; ++axis) {
			auto *value = new QDoubleSpinBox(m_controls);
			value->setObjectName(
				QStringLiteral("prefab%1%2").arg(group ? QStringLiteral("Rotation") : QStringLiteral("Position")).arg(axis));
			value->setAccessibleName((group		  ? tr("Rotation %1")
									  : inserting ? tr("Position %1")
												  : tr("Anchor %1"))
										 .arg(QString(QLatin1Char("XYZ"[axis]))));
			value->setAccessibleDescription(group ? tr("Degrees around the prefab anchor, applied in X, Y, Z order.")
												  : tr("Map units, from −32768 to 32768."));
			value->setRange(group ? -360000 : -32768, group ? 360000 : 32768);
			value->setDecimals(4);
			value->setSingleStep(group ? 15 : 8);
			value->setLayoutDirection(Qt::LeftToRight);
			value->setEnabled(inserting);
			form->addRow(value->accessibleName(), value);
			(group ? m_rotation : m_position)[axis] = value;
			connect(value, &QDoubleSpinBox::valueChanged, this, [this] { schedule(); });
		}
	}
	if (inserting) {
		m_lock = new QCheckBox(tr("Lock textures to geometry"), m_controls);
		m_lock->setObjectName(QStringLiteral("prefabTextureLock"));
		m_lock->setAccessibleName(m_lock->text());
		m_lock->setChecked(true);
		form->addRow(m_lock);
		m_prefix = new QLineEdit(m_controls);
		m_prefix->setObjectName(QStringLiteral("prefabTargetPrefix"));
		m_prefix->setAccessibleName(tr("Internal target prefix"));
		m_prefix->setAccessibleDescription(tr("Leave empty to allocate a unique prefix. External links keep their names; see Details."));
		m_prefix->setPlaceholderText(tr("Automatic"));
		m_prefix->setMaxLength(32);
		m_prefix->setLayoutDirection(Qt::LeftToRight);
		form->addRow(tr("Target prefix"), m_prefix);
		connect(m_lock, &QCheckBox::toggled, this, [this] { schedule(); });
		connect(m_prefix, &QLineEdit::textChanged, this, [this] { schedule(); });
	} else {
		m_output = new QLineEdit(m_controls);
		m_output->setObjectName(QStringLiteral("prefabOutput"));
		m_output->setAccessibleName(mode == LevelPrefabDialogMode::Stage ? tr("Package prefab path") : tr("Prefab output path"));
		m_output->setLayoutDirection(Qt::LeftToRight);
		m_output->setText(mode == LevelPrefabDialogMode::Stage ? QStringLiteral("prefabs/assembly.vprefab") : QString());
		form->addRow(mode == LevelPrefabDialogMode::Stage ? tr("Package path") : tr("Output file"), m_output);
		if (mode == LevelPrefabDialogMode::Export) {
			auto *browse = new QPushButton(tr("Choose file…"), m_controls);
			browse->setAccessibleName(tr("Choose prefab output file"));
			form->addRow(browse);
			connect(browse, &QPushButton::clicked, this, [this] {
				const auto path = QFileDialog::getSaveFileName(
					this, tr("Export Prefab"), m_output->text().isEmpty() ? QFileInfo(m_source.sourcePath).absolutePath() : m_output->text(),
					tr("VibeStudio prefabs (*.vprefab)"));
				if (!path.isEmpty()) {
					m_output->setText(QFileInfo(path).suffix().isEmpty() ? path + QStringLiteral(".vprefab") : path);
				}
			});
		}
		m_overwrite = new QCheckBox(mode == LevelPrefabDialogMode::Stage ? tr("Replace existing package entry")
																		 : tr("Replace existing file with backup"),
									m_controls);
		m_overwrite->setAccessibleName(m_overwrite->text());
		m_overwrite->setObjectName(QStringLiteral("prefabOverwrite"));
		form->addRow(m_overwrite);
		connect(m_name, &QLineEdit::textChanged, this, [this] { schedule(); });
		connect(m_description, &QPlainTextEdit::textChanged, this, [this] { schedule(); });
		connect(m_autoAnchor, &QCheckBox::toggled, this, [this](bool automatic) {
			for (auto *v : m_position) {
				v->setEnabled(!automatic);
			}
			schedule();
		});
	}
	auto *details = new QPushButton(tr("Details"), m_controls);
	details->setAccessibleName(tr("Prefab links, materials and dependencies"));
	column->addWidget(details);
	connect(details, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Prefab details"));
		dialog.resize(800, 550);
		auto *body = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(&dialog);
		text->setReadOnly(true);
		text->setAccessibleName(tr("Prefab dependency and placement report"));
		text->setPlainText(m_details);
		body->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		body->addWidget(buttons);
		dialog.exec();
	});
	m_status = new QLabel(m_controls);
	m_status->setObjectName(QStringLiteral("prefabStatus"));
	m_status->setAccessibleName(tr("Prefab status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	column->addWidget(m_status);
	m_progress = new QProgressBar(m_controls);
	m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Preparing prefab"));
	column->addWidget(m_progress);
	column->addStretch();
	m_scroll->setWidget(m_controls);
	m_preview = new ModelViewport(split);
	m_preview->setObjectName(QStringLiteral("prefabPreview"));
	m_preview->setAccessibleName(tr("Prefab geometry and model preview"));
	m_preview->setAccessibleDescription(tr("Only this assembly is shown. Material images and static models come from the open package or "
										   "folder; shader effects and sound are not simulated."));
	m_preview->setShowEdges(true);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	const auto prefs = StudioSettings().accessibilityPreferences();
	m_preview->setReducedMotion(prefs.reducedMotion);
	m_preview->setHighContrast(prefs.theme == StudioTheme::HighContrastDark || prefs.theme == StudioTheme::HighContrastLight);
	auto *frame = new QPushButton(tr("Frame prefab"), m_controls);
	frame->setAccessibleName(frame->text());
	connect(frame, &QPushButton::clicked, m_preview, &ModelViewport::frameModel);
	column->insertWidget(column->count() - 1, frame);
	updateControlWidth();
	split->setSizes({m_scroll->minimumWidth(), 760});
	split->setStretchFactor(1, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setText(inserting ? tr("Insert Prefab") : mode == LevelPrefabDialogMode::Stage ? tr("Stage Prefab") : tr("Save Prefab"));
	m_apply->setAccessibleName(m_apply->text());
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelPrefabDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &LevelPrefabDialog::reject);
	layout->addWidget(buttons);
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(120);
	connect(m_debounce, &QTimer::timeout, this, &LevelPrefabDialog::startPreview);
	schedule();
}
LevelPrefabDialog::~LevelPrefabDialog()
{
	if (m_work) {
		m_work->cancelled = true;
	}
}
void LevelPrefabDialog::updateControlWidth()
{
	if (!m_scroll || !m_controls) {
		return;
	}
	m_controls->layout()->activate();
	m_scroll->setMinimumWidth(
		std::max(340, m_controls->minimumSizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 12));
}
void LevelPrefabDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::LayoutDirectionChange) {
		updateControlWidth();
	}
}
LevelPrefabPlacement LevelPrefabDialog::placement() const
{
	LevelPrefabPlacement value;
	value.position = {m_position[0]->value(), m_position[1]->value(), m_position[2]->value(), true};
	if (m_rotation[0]) {
		value.rotation = {m_rotation[0]->value(), m_rotation[1]->value(), m_rotation[2]->value(), true};
	}
	value.textureLock = m_lock && m_lock->isChecked();
	value.targetPrefix = m_prefix ? m_prefix->text() : QString();
	return value;
}
void LevelPrefabDialog::setPlacement(const LevelPrefabPlacement &value)
{
	m_updating = true;
	const double p[3]{value.position.x, value.position.y, value.position.z}, r[3]{value.rotation.x, value.rotation.y, value.rotation.z};
	for (int axis = 0; axis < 3; ++axis) {
		m_position[axis]->setValue(p[axis]);
		if (m_rotation[axis]) {
			m_rotation[axis]->setValue(r[axis]);
		}
	}
	if (m_lock) {
		m_lock->setChecked(value.textureLock);
	}
	if (m_prefix) {
		m_prefix->setText(value.targetPrefix);
	}
	m_updating = false;
	schedule();
}
void LevelPrefabDialog::schedule()
{
	if (m_updating || !m_debounce || m_publishing || m_closed) {
		return;
	}
	++m_generation;
	m_ready = m_valid = false;
	m_apply->setEnabled(false);
	m_progress->show();
	m_status->setText(tr("Preparing prefab preview…"));
	if (m_work) {
		m_work->cancelled = true;
	}
	m_debounce->start();
}
void LevelPrefabDialog::startPreview()
{
	if (m_thread || m_closed) {
		return;
	}
	auto work = std::make_shared<Work>();
	work->candidate = m_source;
	work->generation = m_generation;
	work->assets = m_assets;
	work->assetKey = m_assetKey;
	work->dependencyDetails = m_dependencyDetails;
	m_work = work;
	const auto mode = m_mode;
	const auto input = m_input;
	const auto archive = m_archive;
	const auto palette = m_palette;
	const auto fromPackage = m_packageEntry;
	const auto place = placement();
	const LevelPrefabCreateRequest capture{m_name->text(), m_description->toPlainText(),
										   m_autoAnchor && m_autoAnchor->isChecked() ? LevelMapVec3{} : place.position};
	m_thread = QThread::create([work, mode, input, archive, palette, fromPackage, place, capture] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		LevelMapDocument visible;
		if (mode == LevelPrefabDialogMode::Insert) {
			if (fromPackage) {
				if (!archive) {
					work->error = tr("Open the package containing this prefab.");
					return;
				}
				QByteArray bytes;
				if (!archive->readEntryBytes(input, &bytes, &work->error, levelPrefabByteLimit) ||
					!parseLevelPrefab(bytes, &work->prefab, &work->error, cancelled)) {
					return;
				}
			} else if (!readLevelPrefab(input, &work->prefab, &work->error, cancelled)) {
				return;
			}
			if (!insertLevelPrefab(&work->candidate, work->prefab, place, &work->report, &work->error, cancelled)) {
				return;
			}
			visible = work->candidate;
			QSet<int> entities, brushes, patches;
			for (const auto &ref : work->report.inserted) {
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
			visible.entities.removeIf([&](const auto &e) { return !entities.contains(e.id); });
			visible.brushes.removeIf([&](const auto &b) { return !brushes.contains(b.id) && !entities.contains(b.entityId); });
			visible.patches.removeIf([&](const auto &p) { return !patches.contains(p.id) && !entities.contains(p.entityId); });
		} else {
			if (!createLevelPrefab(work->candidate, capture, &work->prefab, &work->report, &work->error, cancelled) ||
				!inspectLevelPrefab(work->prefab, &visible, nullptr, &work->error, cancelled)) {
				return;
			}
			work->bytes = serializeLevelPrefab(work->prefab, &work->error, cancelled);
			if (work->bytes.isEmpty()) {
				return;
			}
		}
		if (cancelled()) {
			return;
		}
		if (work->assetKey != work->prefab.mapText) {
			work->assets = {};
			work->dependencyDetails.clear();
			if (archive) {
				LevelPreviewAssetOptions options;
				options.paletteId = palette;
				const auto progress = [cancelled](int, int) { return !cancelled(); };
				work->assets = resolveLevelPreviewAssets(visible, *archive, options, progress);
				const auto dependencies = inspectLevelDependencies(visible, *archive, progress);
				if (dependencies.cancelled || work->assets.cancelled || cancelled()) {
					return;
				}
				work->dependencyDetails = levelDependencyReportText(dependencies);
			}
			work->assetKey = work->prefab.mapText;
		}
		LevelMapPreviewMeshOptions options;
		options.textureSizes = levelPreviewTextureSizes(work->assets);
		options.modelMeshes = work->assets.models;
		options.isCancelled = cancelled;
		work->mesh = buildLevelMapPreviewMesh(visible, options);
		work->valid = !cancelled() && !work->mesh.cancelled;
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
		m_apply->setEnabled(m_valid);
		m_progress->hide();
		m_candidate = work->candidate;
		m_prefab = work->prefab;
		m_report = work->report;
		m_bytes = work->bytes;
		m_assets = work->assets;
		m_assetKey = work->assetKey;
		m_dependencyDetails = work->dependencyDetails;
		m_preview->setMesh(work->mesh.mesh, m_preview->hasMesh());
		m_preview->setSurfaceSkins(levelPreviewSurfaceImages(work->mesh.mesh, m_assets));
		QString status = work->error;
		if (m_valid) {
			if (m_mode == LevelPrefabDialogMode::Insert) {
				m_name->setText(m_prefab.name);
				m_description->setPlainText(m_prefab.description);
			} else if (m_autoAnchor->isChecked()) {
				m_updating = true;
				m_position[0]->setValue(m_prefab.anchor.x);
				m_position[1]->setValue(m_prefab.anchor.y);
				m_position[2]->setValue(m_prefab.anchor.z);
				m_updating = false;
			}
			status = tr("Ready. Brushes: %1; patches: %2; entities: %3.")
						 .arg(m_report.statistics.brushCount)
						 .arg(m_report.statistics.patchCount)
						 .arg(m_report.statistics.entityCount - 1);
			status += '\n' + tr("Anchor: %1, %2, %3").arg(m_prefab.anchor.x).arg(m_prefab.anchor.y).arg(m_prefab.anchor.z);
			if (!m_report.renamedTargets.isEmpty()) {
				status += '\n' + tr("Internal target prefix: %1").arg(m_report.prefix);
			}
			if (!m_report.warnings.isEmpty()) {
				status += '\n' + m_report.warnings.join('\n');
			}
			if (!m_archive) {
				status += '\n' + tr("Open a package or asset folder to check dependencies and preview materials and models.");
			} else if (m_assets.problemCount() > 0 || m_assets.unavailableModels > 0 || !m_assets.complete) {
				status += '\n' + tr("Some preview assets are unavailable. Inspect Details before compiling or packaging.");
			}
			if (work->mesh.truncated) {
				status += '\n' + tr("The preview triangle limit was reached; all objects remain in the prefab.");
			}
		}
		m_status->setText(status);
		m_details = status;
		for (auto it = m_report.renamedTargets.cbegin(); it != m_report.renamedTargets.cend(); ++it) {
			m_details += '\n' + it.key() + QStringLiteral(" → ") + it.value();
		}
		m_details += QStringLiteral("\n\n") + levelPreviewAssetsText(m_assets) + QStringLiteral("\n\n") + m_dependencyDetails;
		updateControlWidth();
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}
void LevelPrefabDialog::accept()
{
	if (!m_ready || !m_valid || m_publishing) {
		return;
	}
	QString error;
	if (m_guard && !m_guard(&error)) {
		m_status->setText(error);
		return;
	}
	if (m_mode == LevelPrefabDialogMode::Insert) {
		if (m_insert && m_insert(m_candidate, m_report, &error)) {
			QDialog::accept();
		} else {
			m_status->setText(error.isEmpty() ? tr("No map is available for insertion.") : error);
		}
		return;
	}
	if (m_output->text().trimmed().isEmpty()) {
		m_status->setText(tr("Choose an output path."));
		m_output->setFocus();
		return;
	}
	if (QFileInfo(m_output->text()).suffix().compare(QStringLiteral("vprefab"), Qt::CaseInsensitive) != 0) {
		m_status->setText(tr("Use the .vprefab extension for reusable level assets."));
		return;
	}
	if (m_mode == LevelPrefabDialogMode::Stage) {
		if (m_stage && m_stage(m_bytes, m_output->text(), m_overwrite->isChecked(), &error)) {
			QDialog::accept();
		} else {
			m_status->setText(error.isEmpty() ? tr("No package is available for staging.") : error);
		}
		return;
	}
	m_publishing = true;
	m_apply->setEnabled(false);
	m_controls->setEnabled(false);
	m_progress->show();
	m_status->setText(tr("Saving prefab…"));
	auto work = std::make_shared<Work>();
	work->prefab = m_prefab;
	m_work = work;
	const auto path = m_output->text();
	const bool overwrite = m_overwrite->isChecked();
	m_thread = QThread::create([work, path, overwrite] {
		work->valid =
			writeLevelPrefab(work->prefab, path, overwrite, false, &work->written, &work->error, [work] { return work->cancelled.load(); });
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread = nullptr;
		m_publishing = false;
		m_progress->hide();
		m_controls->setEnabled(true);
		m_apply->setEnabled(m_valid);
		m_written = work->written;
		if (m_written.committed) {
			QDialog::accept();
			return;
		}
		m_status->setText(work->error);
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}
void LevelPrefabDialog::reject()
{
	if (m_work) {
		m_work->cancelled = true;
	}
	if (m_publishing) {
		m_status->setText(tr("Cancelling prefab save…"));
		return;
	}
	++m_generation;
	m_closed = true;
	m_debounce->stop();
	QDialog::reject();
}
} // namespace vibestudio
