#include "app/model_assembly_dialog.h"
#include "app/model_assembly_recovery_dialog.h"
#include "app/model_assembly_recovery_writer.h"
#include "app/model_viewport.h"
#include "core/studio_settings.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
void field(QFormLayout *form, const QString &name, QWidget *widget, const char *id)
{
	widget->setObjectName(QString::fromLatin1(id));
	widget->setAccessibleName(name);
	auto *label = new QLabel(name);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	label->setBuddy(widget);
	form->addRow(label, widget);
}
QDoubleSpinBox *number(double min, double max, double value, int decimals = 3)
{
	auto *box = new QDoubleSpinBox;
	box->setRange(min, max);
	box->setDecimals(decimals);
	box->setValue(value);
	box->setKeyboardTracking(false);
	box->setLayoutDirection(Qt::LeftToRight);
	return box;
}
QWidget *vectorFields(QDoubleSpinBox **values, const QString &name, const char *prefix)
{
	auto *row = new QWidget;
	auto *layout = new QVBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	for (int i = 0; i < 3; ++i)
	{
		values[i] = number(-1000000, 1000000, 0);
		values[i]->setObjectName(QString::fromLatin1(prefix) + QString::number(i));
		values[i]->setAccessibleName(name + QLatin1Char(' ') + QString::fromLatin1("XYZ").mid(i, 1));
		values[i]->setPrefix(QString::fromLatin1("XYZ").mid(i, 1) + QLatin1Char(' '));
		values[i]->setMinimumWidth(0);
		layout->addWidget(values[i]);
	}
	return row;
}
ModelVec3 vectorValue(QDoubleSpinBox *const *values)
{
	return {float(values[0]->value()), float(values[1]->value()), float(values[2]->value())};
}
void setVector(QDoubleSpinBox **fields, ModelVec3 value)
{
	fields[0]->setValue(value.x);
	fields[1]->setValue(value.y);
	fields[2]->setValue(value.z);
}
} // namespace

ModelAssemblyDialog::ModelAssemblyDialog(QWidget *parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("modelAssemblyDialog"));
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QCoreApplication::translate("ModelAssemblyDialog", "Model Assembly"));
	setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Model Assembly"));
	resize(1180, 760);
	ModelAssembly empty;
	empty.name = QCoreApplication::translate("ModelAssemblyDialog", "Assembly");
	m_document.setAssembly(empty, QDir::currentPath());
	auto *layout = new QVBoxLayout(this);
	m_toolbar = new QToolBar;
	m_toolbar->setObjectName(QStringLiteral("assemblyToolbar"));
	m_toolbar->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly actions"));
	layout->addWidget(m_toolbar);
	const auto action = [&](const QString &title, const char *id, auto callback, const QKeySequence &shortcut = QKeySequence()) {
		auto *a = m_toolbar->addAction(title);
		a->setObjectName(QString::fromLatin1(id));
		a->setShortcut(shortcut);
		a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		addAction(a);
		connect(a, &QAction::triggered, this, callback);
		return a;
	};
	action(QCoreApplication::translate("ModelAssemblyDialog", "Open…"), "assemblyOpen", [this] { chooseOpen(); }, QKeySequence::Open);
	action(QCoreApplication::translate("ModelAssemblyDialog", "Recoveries…"), "assemblyRecoveries", [this] { chooseRecovery(); });
	action(QCoreApplication::translate("ModelAssemblyDialog", "Save"), "assemblySave", [this] { chooseSave(); }, QKeySequence::Save);
	action(
		QCoreApplication::translate("ModelAssemblyDialog", "Save As…"), "assemblySaveAs", [this] { chooseSave(true); },
		QKeySequence::SaveAs);
	m_toolbar->addSeparator();
	m_undo = action(
		QCoreApplication::translate("ModelAssemblyDialog", "Undo"), "assemblyUndo",
		[this] {
			QString e;
			undo(&e);
		},
		QKeySequence::Undo);
	m_redo = action(
		QCoreApplication::translate("ModelAssemblyDialog", "Redo"), "assemblyRedo",
		[this] {
			QString e;
			redo(&e);
		},
		QKeySequence::Redo);
	action(QCoreApplication::translate("ModelAssemblyDialog", "Add Part"), "assemblyAdd", [this] { newPart(); });
	m_remove = action(QCoreApplication::translate("ModelAssemblyDialog", "Remove Branch"), "assemblyRemove", [this] {
		QString e;
		removeBranch(m_document.selectedPart(), &e);
	});
	m_remove->setToolTip(QCoreApplication::translate("ModelAssemblyDialog",
													 "Remove the selected part and its descendants. Undo restores the complete branch."));
	action(QCoreApplication::translate("ModelAssemblyDialog", "Reload Inputs"), "assemblyReload", [this] {
		QString e;
		reloadInputs(&e);
	});
	m_bake = action(QCoreApplication::translate("ModelAssemblyDialog", "Bake Pose to Mesh"), "assemblyBake", [this] { bake(); });
	m_export = action(QCoreApplication::translate("ModelAssemblyDialog", "Export Pose…"), "assemblyExport", [this] { chooseExport(); });
	m_bakeAnimation = action(QCoreApplication::translate("ModelAssemblyDialog", "Bake Animation…"), "assemblyBakeAnimation",
							 [this] { chooseAnimationBake(false); });
	m_exportAnimation = action(QCoreApplication::translate("ModelAssemblyDialog", "Export Animation…"), "assemblyExportAnimation",
							   [this] { chooseAnimationBake(true); });
	action(QCoreApplication::translate("ModelAssemblyDialog", "Native Animation…"), "assemblyQ3Animation", [this] { chooseQ3Animation(); });
	action(QCoreApplication::translate("ModelAssemblyDialog", "Player Package…"), "assemblyPlayerBundle", [this] { choosePlayerBundle(); });
	auto *details = action(QCoreApplication::translate("ModelAssemblyDialog", "Details"), "assemblyDetails",
						   [this](bool checked) { m_details->setVisible(checked); });
	details->setCheckable(true);

	auto *splitter = new QSplitter;
	splitter->setChildrenCollapsible(false);
	layout->addWidget(splitter, 1);
	m_tree = new QTreeWidget;
	m_tree->setObjectName(QStringLiteral("assemblyParts"));
	m_tree->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly parts"));
	m_tree->setAccessibleDescription(QCoreApplication::translate(
		"ModelAssemblyDialog", "Parent and child attachment hierarchy. Select a part to edit its source, transform and animation."));
	m_tree->setHeaderLabel(QCoreApplication::translate("ModelAssemblyDialog", "Parts"));
	m_tree->setMinimumWidth(130);
	m_tree->setUniformRowHeights(true);
	splitter->addWidget(m_tree);
	auto *centre = new QWidget;
	auto *centreLayout = new QVBoxLayout(centre);
	centreLayout->setContentsMargins(0, 0, 0, 0);
	m_preview = new ModelViewport;
	m_preview->setObjectName(QStringLiteral("assemblyPreview"));
	m_preview->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly preview"));
	m_preview->setMinimumSize(240, 200);
	centreLayout->addWidget(m_preview, 1);
	m_timeline = new QWidget;
	auto *timeline = new QGridLayout(m_timeline);
	timeline->setContentsMargins(0, 0, 0, 0);
	auto *playbar = new QToolBar;
	playbar->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly playback"));
	m_play = playbar->addAction(QCoreApplication::translate("ModelAssemblyDialog", "Play"));
	m_play->setObjectName(QStringLiteral("assemblyPlay"));
	m_play->setCheckable(true);
	connect(m_play, &QAction::toggled, this, [this](bool enabled) { play(enabled); });
	timeline->addWidget(playbar, 0, 0);
	m_time = number(0, 1000000, 0, 4);
	m_time->setObjectName(QStringLiteral("assemblyTime"));
	m_time->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly time in seconds"));
	m_time->setSuffix(QCoreApplication::translate("ModelAssemblyDialog", " s"));
	timeline->addWidget(m_time, 0, 1);
	m_render = new QComboBox;
	m_render->setObjectName(QStringLiteral("assemblyRenderMode"));
	m_render->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly render mode"));
	m_render->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_render->setMinimumContentsLength(10);
	m_render->addItems({QCoreApplication::translate("ModelAssemblyDialog", "Shaded"),
						QCoreApplication::translate("ModelAssemblyDialog", "Wireframe"),
						QCoreApplication::translate("ModelAssemblyDialog", "Materials")});
	timeline->addWidget(m_render, 1, 1);
	auto *frame = new QPushButton(QCoreApplication::translate("ModelAssemblyDialog", "Frame All"));
	frame->setAutoDefault(false);
	connect(frame, &QPushButton::clicked, m_preview, &ModelViewport::frameModel);
	timeline->addWidget(frame, 1, 0);
	centreLayout->addWidget(m_timeline);
	m_materialStatus = new QLabel;
	m_materialStatus->setTextFormat(Qt::PlainText);
	m_materialStatus->setWordWrap(true);
	m_materialStatus->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly material status"));
	centreLayout->addWidget(m_materialStatus);
	m_details = new QPlainTextEdit;
	m_details->setReadOnly(true);
	m_details->setObjectName(QStringLiteral("assemblyDependencyDetails"));
	m_details->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly inputs and bake details"));
	m_details->hide();
	centreLayout->addWidget(m_details, 1);
	splitter->addWidget(centre);

	auto *scroll = new QScrollArea;
	m_inspectorScroll = scroll;
	scroll->setObjectName(QStringLiteral("assemblyInspectorScroll"));
	scroll->setWidgetResizable(true);
	scroll->setMinimumWidth(250);
	m_inspector = new QWidget;
	auto *form = new QFormLayout(m_inspector);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	m_id = new QLineEdit;
	m_id->setMaxLength(32);
	m_id->setLayoutDirection(Qt::LeftToRight);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Part ID"), m_id, "assemblyPartId");
	m_kind = new QComboBox;
	m_kind->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_kind->setMinimumContentsLength(10);
	m_kind->addItems({QCoreApplication::translate("ModelAssemblyDialog", "File"),
					  QCoreApplication::translate("ModelAssemblyDialog", "Current package")});
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Source kind"), m_kind, "assemblySourceKind");
	m_source = new QLineEdit;
	m_source->setMaxLength(4096);
	m_source->setLayoutDirection(Qt::LeftToRight);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Model source"), m_source, "assemblySource");
	auto *browse = new QPushButton(QCoreApplication::translate("ModelAssemblyDialog", "Browse…"));
	browse->setToolTip(QCoreApplication::translate("ModelAssemblyDialog", "Choose a model file or current-package entry."));
	browse->setAutoDefault(false);
	form->addRow(browse);
	connect(browse, &QPushButton::clicked, this, [this] {
		if (m_kind->currentIndex() == 1)
		{
			if (!m_materialSource.archive || !m_materialSource.archive->isOpen())
			{
				report(QCoreApplication::translate("ModelAssemblyDialog", "Open a package before choosing a package model."));
				return;
			}
			QStringList models;
			for (const auto &entry : m_materialSource.archive->entries())
			{
				const auto name = entry.virtualPath.toLower();
				if (entry.kind == PackageEntryKind::File && (name.endsWith(".mesh.json") || name.endsWith(".obj") ||
															 name.endsWith(".mdl") || name.endsWith(".md2") || name.endsWith(".md3")))
				{
					models << entry.virtualPath;
				}
			}
			models.removeDuplicates();
			models.sort(Qt::CaseInsensitive);
			if (models.isEmpty())
			{
				report(QCoreApplication::translate("ModelAssemblyDialog", "The current package has no supported model entries."));
				return;
			}
			bool accepted = false;
			const auto selected =
				QInputDialog::getItem(this, QCoreApplication::translate("ModelAssemblyDialog", "Choose Package Model"),
									  QCoreApplication::translate("ModelAssemblyDialog", "Model source"), models, 0, false, &accepted);
			if (accepted)
			{
				m_source->setText(selected);
			}
			return;
		}
		const auto path = QFileDialog::getOpenFileName(
			this, QCoreApplication::translate("ModelAssemblyDialog", "Choose Assembly Model"), m_source->text(),
			QCoreApplication::translate("ModelAssemblyDialog", "Models (*.mesh.json *.obj *.mdl *.md2 *.md3)"));
		if (!path.isEmpty())
		{
			m_kind->setCurrentIndex(0);
			m_source->setText(path);
		}
	});
	addSkinControls(form);
	m_parent = new QComboBox;
	m_parent->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_parent->setMinimumContentsLength(10);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Parent part"), m_parent, "assemblyParent");
	m_tag = new QComboBox;
	m_tag->setEditable(true);
	m_tag->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_tag->setMinimumContentsLength(10);
	m_tag->lineEdit()->setMaxLength(63);
	m_tag->lineEdit()->setLayoutDirection(Qt::LeftToRight);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Parent tag"), m_tag, "assemblyTag");
	connect(m_parent, &QComboBox::currentIndexChanged, this, [this] {
		if (!m_refreshing)
		{
			refreshTags();
		}
	});
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Local translation"),
		  vectorFields(m_translation, QCoreApplication::translate("ModelAssemblyDialog", "Local translation"), "assemblyTranslate"),
		  "assemblyTranslation");
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Local rotation in degrees"),
		  vectorFields(m_rotation, QCoreApplication::translate("ModelAssemblyDialog", "Local rotation"), "assemblyRotate"),
		  "assemblyRotation");
	m_scale = number(.000001, 1000, 1, 6);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Uniform scale"), m_scale, "assemblyScale");
	m_first = new QSpinBox;
	m_first->setLayoutDirection(Qt::LeftToRight);
	m_first->setRange(0, 1023);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "First frame"), m_first, "assemblyFirstFrame");
	m_last = new QSpinBox;
	m_last->setLayoutDirection(Qt::LeftToRight);
	m_last->setRange(-1, 1023);
	m_last->setSpecialValueText(QCoreApplication::translate("ModelAssemblyDialog", "Last available"));
	m_last->setValue(-1);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Last frame"), m_last, "assemblyLastFrame");
	m_fps = number(0, 1000, 10);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Frames per second"), m_fps, "assemblyFps");
	m_phase = number(0, 1000000, 0);
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Phase in frames"), m_phase, "assemblyPhase");
	m_loop = new QCheckBox;
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Loop this part"), m_loop, "assemblyLoop");
	m_interpolate = new QCheckBox;
	field(form, QCoreApplication::translate("ModelAssemblyDialog", "Interpolate this part"), m_interpolate, "assemblyInterpolate");
	auto *apply = new QPushButton(QCoreApplication::translate("ModelAssemblyDialog", "Apply"));
	apply->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Apply Part"));
	apply->setToolTip(
		QCoreApplication::translate("ModelAssemblyDialog", "Commit the part's source, attachment, transform and playback settings."));
	apply->setObjectName(QStringLiteral("assemblyApplyPart"));
	apply->setAutoDefault(false);
	connect(apply, &QPushButton::clicked, this, [this] { applyInspector(); });
	form->addRow(apply);
	scroll->setWidget(m_inspector);
	scroll->setMinimumWidth(m_inspector->minimumSizeHint().width() + 30);
	splitter->addWidget(scroll);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({185, 630, 310});
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("assemblyStatus"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly status"));
	layout->addWidget(m_status);
	auto *recoveryRow = new QHBoxLayout;
	m_recoveryEnabled = new QCheckBox(QCoreApplication::translate("ModelAssemblyDialog", "Keep local recovery copies"));
	m_recoveryEnabled->setObjectName(QStringLiteral("assemblyRecoveryEnabled"));
	m_recoveryEnabled->setAccessibleName(m_recoveryEnabled->text());
	m_recoveryEnabled->setToolTip(QCoreApplication::translate(
		"ModelAssemblyDialog", "Keep committed assembly edits, selected part and timeline position locally. Model inputs are linked, not "
							   "copied. This preference is shared with the Mesh Editor."));
	m_recoveryEnabled->setChecked(StudioSettings().modelRecoveryEnabled());
	recoveryRow->addWidget(m_recoveryEnabled);
	m_recoveryStatus = new QLabel;
	m_recoveryStatus->setObjectName(QStringLiteral("assemblyRecoveryStatus"));
	m_recoveryStatus->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Assembly recovery status"));
	m_recoveryStatus->setTextFormat(Qt::PlainText);
	m_recoveryStatus->setWordWrap(true);
	recoveryRow->addWidget(m_recoveryStatus, 1);
	layout->addLayout(recoveryRow);
	m_recoveryTimer = new QTimer(this);
	m_recoveryTimer->setInterval(5000);
	connect(m_recoveryTimer, &QTimer::timeout, this, [this] { checkpointRecovery(); });
	connect(m_recoveryEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
		StudioSettings settings;
		settings.setModelRecoveryEnabled(enabled);
		settings.sync();
		if (enabled)
		{
			checkpointRecovery();
		}
		else
		{
			if (m_recoveryWriter)
			{
				m_recoveryWriter->pause();
			}
			m_recoveryStatus->setText(
				QCoreApplication::translate("ModelAssemblyDialog", "Local recovery is off. Existing copies are retained."));
		}
	});
	m_recoveryTimer->start();

	m_timer = new QTimer(this);
	m_timer->setInterval(66);
	connect(m_timer, &QTimer::timeout, this, [this] {
		// Let a displayed pose finish rendering before replacing its mesh.
		// The elapsed clock skips obsolete samples on slower machines.
		if (m_working || m_preview->isRendering())
		{
			return;
		}
		QString error;
		const double time = std::min(1000000.0, m_playStart + m_clock.elapsed() / 1000.0);
		if (!setTime(time, &error) || time == 1000000)
		{
			play(false);
		}
	});
	connect(m_time, &QDoubleSpinBox::valueChanged, this, [this](double time) {
		if (m_refreshing)
		{
			return;
		}
		play(false);
		QString error;
		setTime(time, &error);
	});
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
		if (!m_refreshing && item && !m_working)
		{
			selectPart(item->data(0, Qt::UserRole).toString());
		}
	});
	connect(m_preview, &ModelViewport::trianglePicked, this, [this](int surface, int, int) {
		if (!m_working && surface >= 0 && surface < m_pose.surfaceParts.size())
		{
			selectPart(m_document.assembly().parts[m_pose.surfaceParts[surface]].id);
		}
	});
	m_materialWorker = new ModelMaterialWorker(this);
	m_materialWorker->started = [this] {
		m_materialStatus->setText(QCoreApplication::translate("ModelAssemblyDialog", "Loading material previews…"));
	};
	m_materialWorker->progress = [this](int done, int total) {
		m_materialStatus->setText(QCoreApplication::translate("ModelAssemblyDialog", "Material previews: %1 of %2").arg(done).arg(total));
	};
	m_materialWorker->completed = [this](const ModelMaterialResult &result) {
		m_materialAssets = result.assets;
		m_materialImages.clear();
		for (int surface = 0; surface < m_pose.mesh.surfaces.size(); ++surface)
		{
			const auto key = modelPreviewSurfaceMaterialKey(surface);
			for (const auto &material : result.assets.materials)
				if (material.key == key && material.ready())
				{
					m_materialImages.insert(surface, material.image);
					break;
				}
		}
		applyMaterialImages();
		m_materialStatus->setText(result.error.isEmpty()
									  ? QCoreApplication::translate("ModelAssemblyDialog", "%1 material previews ready; %2 need attention.")
											.arg(result.assets.readyCount())
											.arg(result.assets.problemCount())
									  : result.error);
		refreshDetails();
	};
	connect(m_render, &QComboBox::currentIndexChanged, this, [this] { refreshMaterials(); });
	refresh();
	newPart();
}
ModelAssemblyDialog::~ModelAssemblyDialog()
{
	m_recoveryTimer->stop();
	if (!m_approvedClose)
	{
		checkpointRecovery();
	}
	// Flush while the document and callbacks still exist. QObject then owns only
	// retired writers, which cannot resurrect an explicitly discarded copy.
	delete m_recoveryWriter;
	m_recoveryWriter = nullptr;
}

void ModelAssemblyDialog::report(const QString &message)
{
	m_status->setText(message);
	m_status->setAccessibleDescription(message);
}
bool ModelAssemblyDialog::performWork(const QString &title, ModelTask task, QString *error, bool durableWrite)
{
	if (m_working)
	{
		if (error)
		{
			*error = QCoreApplication::translate("ModelAssemblyDialog", "Wait for the current assembly operation to finish.");
		}
		return false;
	}
	m_working = true;
	const QPointer<QWidget> focus = focusWidget();
	m_toolbar->setEnabled(false);
	m_inspector->setEnabled(false);
	m_tree->setEnabled(false);
	m_timeline->setEnabled(false);
	m_recoveryEnabled->setEnabled(false);
	QString failure;
	report(title);
	const bool success = runModelTask(this, title, std::move(task), &failure, durableWrite, &m_cancelWork);
	m_working = false;
	m_toolbar->setEnabled(true);
	m_inspector->setEnabled(true);
	m_tree->setEnabled(true);
	m_timeline->setEnabled(true);
	m_recoveryEnabled->setEnabled(true);
	if (focus && focus->isEnabled() && !m_closeAfterWork)
	{
		focus->setFocus(Qt::OtherFocusReason);
	}
	if (error)
	{
		*error = failure;
	}
	if (!success)
	{
		report(failure);
	}
	if (m_closeAfterWork)
	{
		m_closeAfterWork = false;
		QTimer::singleShot(0, this, [this] { close(); });
	}
	else if (m_contextPending)
	{
		m_contextPending = false;
		QTimer::singleShot(0, this, [this] {
			QString e;
			reloadInputs(&e);
		});
	}
	return success;
}
bool ModelAssemblyDialog::adopt(ModelAssemblyDocument candidate, QString *error, bool replacement,
								const ModelAssemblyRecoverySnapshot *recovery, const QString &preservedCopy)
{
	if (m_working)
	{
		if (error)
		{
			*error = QCoreApplication::translate("ModelAssemblyDialog", "Wait for the current assembly operation to finish.");
		}
		return false;
	}
	play(false);
	ModelAssemblyResolved resolved;
	ModelAssemblyPose pose;
	QString inputError;
	const ModelAssemblyContext context{candidate.directory(), m_materialSource.archive, m_materialSource.paletteId};
	const double seconds = recovery ? recovery->seconds : m_seconds;
	// Missing inputs do not discard a valid recipe. Cancellation still leaves
	// the complete document, selection and preview unchanged.
	if (!candidate.assembly().parts.isEmpty())
	{
		if (!performWork(
				QCoreApplication::translate("ModelAssemblyDialog", "Resolve Assembly Inputs"),
				[&](QString *failure, const ModelWorkControl &control) {
					if (!resolveModelAssembly(candidate.assembly(), context, &resolved, &inputError, control) ||
						!sampleModelAssembly(candidate.assembly(), resolved, seconds, &pose, &inputError, control))
					{
						resolved = {};
						pose = {};
						if (control.cancelled && control.cancelled())
						{
							*failure = inputError;
							return false;
						}
					}
					return true;
				},
				error))
		{
			return false;
		}
	}
	if (replacement)
	{
		retireRecovery(preservedCopy);
		m_recoveredSourceHash = recovery ? recovery->sourceSha256 : QByteArray();
	}
	m_document = std::move(candidate);
	m_seconds = seconds;
	m_resolved = std::move(resolved);
	m_pose = std::move(pose);
	m_previewError = inputError;
	m_materialWorker->reset();
	m_materialImages.clear();
	m_materialAssets = {};
	refresh();
	refreshMaterials();
	checkpointRecovery();
	if (error)
	{
		*error = inputError;
	}
	return true;
}
bool ModelAssemblyDialog::setAssembly(const ModelAssembly &assembly, const QString &directory, QString *error)
{
	ModelAssemblyDocument candidate;
	if (!candidate.setAssembly(assembly, directory, error))
	{
		return false;
	}
	return adopt(std::move(candidate), error, true);
}
bool ModelAssemblyDialog::openSource(const QString &path, QString *error)
{
	play(false);
	ModelAssemblyDocument candidate;
	if (!performWork(
			QCoreApplication::translate("ModelAssemblyDialog", "Open Assembly"),
			[&](QString *failure, const ModelWorkControl &control) { return candidate.load(path, failure, control); }, error))
	{
		return false;
	}
	return adopt(std::move(candidate), error, true);
}
bool ModelAssemblyDialog::saveSource(const QString &path, bool overwrite, QString *error)
{
	play(false);
	auto candidate = m_document;
	const auto archive = m_materialSource.archive;
	if (!performWork(
			QCoreApplication::translate("ModelAssemblyDialog", "Save Assembly"),
			[&](QString *failure, const ModelWorkControl &control) {
				return candidate.save(path, overwrite, failure, control, false, archive);
			},
			error, true))
	{
		return false;
	}
	m_document = std::move(candidate);
	retireRecovery();
	refresh(false);
	report(QCoreApplication::translate("ModelAssemblyDialog", "Saved assembly source: %1").arg(m_document.path()));
	return true;
}
bool ModelAssemblyDialog::applyPart(const QString &previousId, const ModelAssemblyPart &part, QString *error)
{
	auto candidate = m_document;
	if (!candidate.setPart(previousId, part, error))
	{
		if (error)
		{
			report(*error);
		}
		return false;
	}
	return adopt(std::move(candidate), error);
}
bool ModelAssemblyDialog::removeBranch(const QString &id, QString *error)
{
	auto candidate = m_document;
	if (!candidate.removeBranch(id, error))
	{
		if (error)
		{
			report(*error);
		}
		return false;
	}
	return adopt(std::move(candidate), error);
}
bool ModelAssemblyDialog::undo(QString *error)
{
	auto candidate = m_document;
	return candidate.undo() && adopt(std::move(candidate), error);
}
bool ModelAssemblyDialog::redo(QString *error)
{
	auto candidate = m_document;
	return candidate.redo() && adopt(std::move(candidate), error);
}
bool ModelAssemblyDialog::reloadInputs(QString *error)
{
	if (!adopt(m_document, error))
	{
		return false;
	}
	return m_document.assembly().parts.isEmpty() || previewReady();
}
bool ModelAssemblyDialog::setTime(double seconds, QString *error)
{
	const auto assembly = m_document.assembly();
	const auto resolved = m_resolved;
	ModelAssemblyPose candidate;
	if (!performWork(
			QCoreApplication::translate("ModelAssemblyDialog", "Sample Assembly Pose"),
			[&](QString *failure, const ModelWorkControl &control) {
				return sampleModelAssembly(assembly, resolved, seconds, &candidate, failure, control);
			},
			error))
	{
		const QSignalBlocker block(m_time);
		m_time->setValue(m_seconds);
		return false;
	}
	m_seconds = seconds;
	m_pose = std::move(candidate);
	refresh(false);
	applyMaterialImages();
	return true;
}
bool ModelAssemblyDialog::exportPose(const QString &path, bool overwrite, QString *error)
{
	play(false);
	const auto assembly = m_document.assembly();
	const auto resolved = m_resolved;
	const auto source = m_document.recoverySource().isEmpty() ? m_document.path() : m_document.recoverySource();
	const auto archive = m_materialSource.archive;
	const auto seconds = m_seconds;
	ModelExportReport result;
	if (!performWork(
			QCoreApplication::translate("ModelAssemblyDialog", "Export Assembly Pose"),
			[&](QString *failure, const ModelWorkControl &control) {
				return exportModelAssemblyPose(assembly, resolved, seconds, path, source, overwrite, false, failure, control, &result,
											   archive);
			},
			error, true))
	{
		return false;
	}
	report(QCoreApplication::translate("ModelAssemblyDialog", "Exported pose: %1. %2").arg(path, result.notes.join(QLatin1Char(' '))));
	return true;
}
void ModelAssemblyDialog::checkpointRecovery()
{
	if (!m_document.isModified())
	{
		retireRecovery();
		return;
	}
	if (!m_recoveryEnabled->isChecked() || m_approvedClose)
	{
		return;
	}
	if (!m_recoveryWriter)
	{
		m_recoveryWriter = new ModelAssemblyRecoveryWriter(modelAssemblyRecoveryDirectory(), this);
		m_recoveryWriter->finished = [this](const QString &path, const QString &error) {
			if (!m_recoveryEnabled->isChecked())
			{
				return;
			}
			m_recoveryStatus->setText(error.isEmpty() ? QCoreApplication::translate("ModelAssemblyDialog", "Local recovery copy ready.")
													  : error);
			m_recoveryStatus->setToolTip(path);
		};
	}
	ModelAssemblyRecoverySnapshot snapshot;
	snapshot.assembly = m_document.assembly();
	snapshot.directory = m_document.directory();
	snapshot.selectedPart = m_document.selectedPart();
	snapshot.seconds = m_seconds;
	snapshot.sourcePath = m_document.path().isEmpty() ? m_document.recoverySource() : m_document.path();
	snapshot.sourceSha256 = m_document.path().isEmpty() ? m_recoveredSourceHash : m_document.sourceFingerprint();
	if (m_recoveryWriter->checkpoint(std::move(snapshot)))
	{
		m_recoveryStatus->setText(QCoreApplication::translate("ModelAssemblyDialog", "Updating local recovery…"));
	}
}
bool ModelAssemblyDialog::recoveryBusy() const
{
	return m_recoveryWriter && m_recoveryWriter->busy();
}
QString ModelAssemblyDialog::recoveryPath() const
{
	return m_recoveryWriter ? m_recoveryWriter->path() : QString();
}
void ModelAssemblyDialog::retireRecovery(const QString &preservedCopy)
{
	if (!m_recoveryWriter)
	{
		return;
	}
	auto *writer = m_recoveryWriter;
	m_recoveryWriter = nullptr;
	writer->retired = [writer] { writer->deleteLater(); };
	m_recoveryStatus->clear();
	m_recoveryStatus->setToolTip({});
	if (modelPathsReferToSameFile(writer->path(), preservedCopy))
	{
		writer->preserve();
	}
	else
	{
		writer->retire();
	}
}
bool ModelAssemblyDialog::restoreRecovery(const QString &path, const QByteArray &sha256, QString *error)
{
	play(false);
	ModelAssemblyDocument candidate;
	ModelAssemblyRecoverySnapshot snapshot;
	if (!performWork(
			QCoreApplication::translate("ModelAssemblyDialog", "Restore Assembly Recovery"),
			[&](QString *failure, const ModelWorkControl &control) {
				return restoreModelAssemblyRecovery(path, sha256, &candidate, &snapshot, failure, control);
			},
			error))
	{
		return false;
	}
	return adopt(std::move(candidate), error, true, &snapshot, path);
}
void ModelAssemblyDialog::chooseRecovery()
{
	if (m_working)
	{
		return;
	}
	play(false);
	if (!maybeSave())
	{
		return;
	}
	m_recoveryTimer->stop();
	auto restored = chooseModelAssemblyRecovery(this, modelAssemblyRecoveryDirectory());
	m_recoveryTimer->start();
	if (!restored)
	{
		return;
	}
	QString error;
	if (adopt(std::move(restored->document), &error, true, &restored->snapshot, restored->record.path))
	{
		report(QCoreApplication::translate("ModelAssemblyDialog", "Recovered an unsaved assembly draft. Save it to a new source path.") +
			   (error.isEmpty() ? QString() : QLatin1Char(' ') + error));
	}
}
void ModelAssemblyDialog::selectPart(const QString &id)
{
	if (m_working)
	{
		return;
	}
	m_document.selectPart(id);
	const QSignalBlocker block(m_tree);
	QTreeWidgetItemIterator it(m_tree);
	while (*it)
	{
		if ((*it)->data(0, Qt::UserRole).toString() == m_document.selectedPart())
		{
			m_tree->setCurrentItem(*it);
			break;
		}
		++it;
	}
	refreshInspector();
	refreshSelection();
}
void ModelAssemblyDialog::refresh(bool rebuildTree)
{
	m_refreshing = true;
	setWindowTitle(QCoreApplication::translate("ModelAssemblyDialog", "Model Assembly — %1%2")
					   .arg(m_document.assembly().name, m_document.isModified() ? QStringLiteral(" *") : QString()));
	m_undo->setEnabled(m_document.canUndo());
	m_redo->setEnabled(m_document.canRedo());
	m_remove->setEnabled(!m_document.selectedPart().isEmpty());
	m_bake->setEnabled(previewReady());
	m_export->setEnabled(previewReady());
	m_bakeAnimation->setEnabled(previewReady());
	m_exportAnimation->setEnabled(previewReady());
	m_play->setEnabled(previewReady() && !m_reducedMotion);
	m_time->setEnabled(previewReady());
	m_time->setValue(m_seconds);
	if (rebuildTree)
	{
		m_tree->clear();
		QVector<int> order;
		validateModelAssembly(m_document.assembly(), nullptr, &order);
		QHash<QString, QTreeWidgetItem *> items;
		for (int index : order)
		{
			const auto &part = m_document.assembly().parts[index];
			auto *item = part.parent.isEmpty() ? new QTreeWidgetItem(m_tree) : new QTreeWidgetItem(items.value(part.parent));
			item->setText(0, part.id);
			item->setData(0, Qt::UserRole, part.id);
			item->setToolTip(
				0, part.parent.isEmpty()
					   ? part.source
					   : QCoreApplication::translate("ModelAssemblyDialog", "%1 at %2 / %3").arg(part.source, part.parent, part.tag));
			items.insert(part.id, item);
		}
		m_tree->expandAll();
		if (items.contains(m_document.selectedPart()))
		{
			m_tree->setCurrentItem(items.value(m_document.selectedPart()));
		}
		refreshInspector();
	}
	if (previewReady())
	{
		m_preview->setMesh(m_pose.mesh, m_preview->hasMesh());
	}
	else
	{
		m_preview->clearMesh();
	}
	refreshSelection();
	refreshDetails();
	m_refreshing = false;
	report(!m_previewError.isEmpty() ? QCoreApplication::translate("ModelAssemblyDialog", "Preview unavailable: %1").arg(m_previewError)
		   : m_document.assembly().parts.isEmpty()
			   ? QCoreApplication::translate("ModelAssemblyDialog", "Add a root model to start an assembly.")
			   : QCoreApplication::translate("ModelAssemblyDialog", "%1 parts · %2 vertices · %3 triangles · %4 s")
					 .arg(m_document.assembly().parts.size())
					 .arg(m_pose.mesh.vertexCount)
					 .arg(m_pose.mesh.triangleCount)
					 .arg(m_seconds, 0, 'f', 3));
}
void ModelAssemblyDialog::refreshInspector()
{
	const bool wasRefreshing = m_refreshing;
	m_refreshing = true;
	ModelAssemblyPart selected;
	for (const auto &part : m_document.assembly().parts)
	{
		if (part.id == m_document.selectedPart())
		{
			selected = part;
			break;
		}
	}
	m_editingId = selected.id;
	m_id->setText(selected.id);
	m_source->setText(selected.source);
	m_kind->setCurrentIndex(selected.sourceKind == ModelAssemblySource::File ? 0 : 1);
	refreshSkinControls(selected);
	m_parent->clear();
	m_parent->addItem(QCoreApplication::translate("ModelAssemblyDialog", "Root (no parent)"), QString());
	for (const auto &part : m_document.assembly().parts)
	{
		if (part.id != selected.id)
		{
			m_parent->addItem(part.id, part.id);
		}
	}
	m_parent->setCurrentIndex(std::max(0, m_parent->findData(selected.parent)));
	refreshTags(selected.tag);
	setVector(m_translation, selected.translation);
	setVector(m_rotation, selected.rotation);
	m_scale->setValue(selected.scale);
	m_first->setValue(selected.firstFrame);
	m_last->setValue(selected.lastFrame);
	m_fps->setValue(selected.framesPerSecond);
	m_phase->setValue(selected.phase);
	m_loop->setChecked(selected.loop);
	m_interpolate->setChecked(selected.interpolate);
	const auto &native = m_document.assembly().q3Animation;
	const bool nativePart = native && (native->lowerPart == selected.id || native->upperPart == selected.id);
	for (QWidget *field : std::array<QWidget *, 5>{m_first, m_last, m_fps, m_phase, m_loop})
		field->setEnabled(!nativePart);
	m_refreshing = wasRefreshing;
}
void ModelAssemblyDialog::refreshTags(const QString &selected)
{
	m_tag->clear();
	const auto parent = m_parent->currentData().toString();
	m_tag->setEnabled(!parent.isEmpty());
	for (const auto &input : m_resolved.inputs)
	{
		if (input.part != parent)
		{
			continue;
		}
		QSet<QString> seen;
		for (const auto &tag : input.mesh.tags)
		{
			if (!seen.contains(tag.name))
			{
				m_tag->addItem(tag.name);
				seen.insert(tag.name);
			}
		}
	}
	if (!selected.isEmpty())
	{
		m_tag->setCurrentText(selected);
	}
}
void ModelAssemblyDialog::refreshSelection()
{
	QVector<int> triangles;
	int offset = 0;
	for (int surface = 0; surface < m_pose.mesh.surfaces.size(); ++surface)
	{
		const auto count = m_pose.mesh.surfaces[surface].triangles.size();
		if (m_document.assembly().parts[m_pose.surfaceParts[surface]].id == m_document.selectedPart())
		{
			for (int i = 0; i < count; ++i)
			{
				triangles.append(offset + i);
			}
		}
		offset += int(count);
	}
	m_preview->setHighlightedTriangles(triangles);
}
void ModelAssemblyDialog::refreshDetails()
{
	QStringList lines = m_pose.notes;
	if (m_document.assembly().q3Animation)
	{
		const auto &binding = *m_document.assembly().q3Animation;
		lines.prepend(QCoreApplication::translate("ModelAssemblyDialog", "Native animation: %1 uses %2; %3 uses %4.")
						  .arg(binding.lowerPart, modelQ3AnimationName(binding.lowerAnimation), binding.upperPart,
							   modelQ3AnimationName(binding.upperAnimation)));
	}
	if (!m_previewError.isEmpty())
	{
		lines << m_previewError;
	}
	for (int i = 0; i < m_resolved.inputs.size(); ++i)
	{
		const auto &input = m_resolved.inputs[i];
		const auto sample = m_pose.samples.value(i);
		lines << QCoreApplication::translate("ModelAssemblyDialog", "%1: %2\nSHA-256: %3\n%4 bytes; frame %5 → %6, blend %7")
					 .arg(input.part, input.source, QString::fromLatin1(input.sha256.toHex()))
					 .arg(input.bytes)
					 .arg(sample.frame)
					 .arg(sample.nextFrame)
					 .arg(sample.fraction, 0, 'f', 4);
		if (input.skin)
		{
			const auto &skin = *input.skin;
			lines << QCoreApplication::translate("ModelAssemblyDialog", "Skin: %1\nSHA-256: %2\n%3 bytes; package entry: %4")
						 .arg(skin.source, QString::fromLatin1(skin.sha256.toHex()))
						 .arg(skin.bytes)
						 .arg(skin.entryIndex);
			lines += modelSkinBindingPlanText(skin.bindings);
		}
	}
	if (!m_materialAssets.materials.isEmpty())
	{
		lines << levelPreviewAssetsText(m_materialAssets);
	}
	m_details->setPlainText(lines.join(QStringLiteral("\n\n")));
}
void ModelAssemblyDialog::newPart()
{
	if (m_working)
	{
		return;
	}
	const auto parent = m_document.selectedPart();
	refreshInspector();
	m_editingId.clear();
	m_id->clear();
	m_source->clear();
	refreshSkinControls({});
	m_parent->clear();
	m_parent->addItem(QCoreApplication::translate("ModelAssemblyDialog", "Root (no parent)"), QString());
	for (const auto &part : m_document.assembly().parts)
	{
		m_parent->addItem(part.id, part.id);
	}
	m_parent->setCurrentIndex(std::max(0, m_parent->findData(parent)));
	setVector(m_translation, {});
	setVector(m_rotation, {});
	m_scale->setValue(1);
	m_first->setValue(0);
	m_last->setValue(-1);
	m_fps->setValue(10);
	m_phase->setValue(0);
	m_loop->setChecked(true);
	m_interpolate->setChecked(true);
	refreshTags();
	m_id->setFocus(Qt::OtherFocusReason);
}
void ModelAssemblyDialog::applyInspector()
{
	ModelAssemblyPart part;
	part.id = m_id->text().trimmed();
	part.source = m_source->text().trimmed();
	part.sourceKind = m_kind->currentIndex() == 0 ? ModelAssemblySource::File : ModelAssemblySource::Package;
	part.skin = skinFromInspector();
	part.parent = m_parent->currentData().toString();
	part.tag = part.parent.isEmpty() ? QString() : m_tag->currentText();
	part.translation = vectorValue(m_translation);
	part.rotation = vectorValue(m_rotation);
	part.scale = m_scale->value();
	part.firstFrame = m_first->value();
	part.lastFrame = m_last->value();
	part.framesPerSecond = m_fps->value();
	part.phase = m_phase->value();
	part.loop = m_loop->isChecked();
	part.interpolate = m_interpolate->isChecked();
	QString error;
	applyPart(m_editingId, part, &error);
}
void ModelAssemblyDialog::setMaterialSource(ModelMaterialSource source)
{
	if (source.revision == m_materialSource.revision && source.paletteId == m_materialSource.paletteId &&
		bool(source.archive) == bool(m_materialSource.archive))
	{
		return;
	}
	m_materialSource = std::move(source);
	if (m_working)
	{
		m_contextPending = true;
		return;
	}
	QString error;
	reloadInputs(&error);
}
void ModelAssemblyDialog::refreshMaterials()
{
	m_materialStatus->setVisible(m_render->currentIndex() == 2);
	if (m_render->currentIndex() == 2 && previewReady())
	{
		m_materialWorker->request(m_pose.mesh, m_materialSource);
	}
	else
	{
		m_materialWorker->reset();
	}
	applyMaterialImages();
}
void ModelAssemblyDialog::applyMaterialImages()
{
	auto images = m_render->currentIndex() == 2 ? m_materialImages : QHash<int, QImage>();
	if (m_render->currentIndex() == 2)
	{
		for (auto i = m_pose.embeddedSkins.cbegin(); i != m_pose.embeddedSkins.cend(); ++i)
		{
			images.insert(i.key(), i.value());
		}
	}
	m_preview->setSurfaceSkins(images);
	m_preview->setRenderMode(m_render->currentIndex() == 0	 ? ModelViewportRenderMode::FlatShaded
							 : m_render->currentIndex() == 1 ? ModelViewportRenderMode::Wireframe
															 : ModelViewportRenderMode::Textured);
}
void ModelAssemblyDialog::setAccessibility(bool highContrast, bool reducedMotion)
{
	m_preview->setHighContrast(highContrast);
	m_preview->setReducedMotion(reducedMotion);
	m_reducedMotion = reducedMotion;
	if (reducedMotion)
	{
		play(false);
	}
	m_play->setEnabled(previewReady() && !reducedMotion);
}
void ModelAssemblyDialog::play(bool playing)
{
	const bool enabled = playing && previewReady() && !m_reducedMotion;
	const QSignalBlocker block(m_play);
	m_play->setChecked(enabled);
	m_play->setText(enabled ? QCoreApplication::translate("ModelAssemblyDialog", "Pause")
							: QCoreApplication::translate("ModelAssemblyDialog", "Play"));
	if (enabled)
	{
		m_playStart = m_seconds;
		m_clock.restart();
		m_timer->start();
	}
	else
	{
		m_timer->stop();
	}
}
bool ModelAssemblyDialog::maybeSave()
{
	if (m_working)
	{
		return false;
	}
	if (!m_document.isModified())
	{
		return true;
	}
	const auto answer =
		QMessageBox::question(this, QCoreApplication::translate("ModelAssemblyDialog", "Unsaved Assembly"),
							  QCoreApplication::translate("ModelAssemblyDialog", "Save the assembly source before continuing?"),
							  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	return answer == QMessageBox::Discard || (answer == QMessageBox::Save && chooseSave());
}
bool ModelAssemblyDialog::chooseSave(bool saveAs)
{
	if (m_working)
	{
		return false;
	}
	play(false);
	QString path = m_document.path();
	if (saveAs || path.isEmpty())
	{
		path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("ModelAssemblyDialog", "Save Assembly"), path,
											QCoreApplication::translate("ModelAssemblyDialog", "Model assemblies (*.assembly.json)"));
	}
	if (path.isEmpty())
	{
		return false;
	}
	QString error;
	return saveSource(path, true, &error);
}
void ModelAssemblyDialog::chooseOpen()
{
	if (m_working)
	{
		return;
	}
	play(false);
	const auto path =
		QFileDialog::getOpenFileName(this, QCoreApplication::translate("ModelAssemblyDialog", "Open Assembly"), m_document.path(),
									 QCoreApplication::translate("ModelAssemblyDialog", "Model assemblies (*.assembly.json)"));
	if (path.isEmpty() || !maybeSave())
	{
		return;
	}
	QString error;
	openSource(path, &error);
}
void ModelAssemblyDialog::chooseExport()
{
	if (!previewReady() || m_working)
	{
		return;
	}
	play(false);
	const auto answer =
		QMessageBox::question(this, QCoreApplication::translate("ModelAssemblyDialog", "Bake One Pose"),
							  m_pose.notes.join(QStringLiteral("\n\n")), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
	if (answer != QMessageBox::Ok)
	{
		return;
	}
	const auto path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("ModelAssemblyDialog", "Export Assembly Pose"), {},
												   QCoreApplication::translate("ModelAssemblyDialog", "Model pose (*.obj *.md2 *.md3)"));
	if (path.isEmpty())
	{
		return;
	}
	QString error;
	exportPose(path, true, &error);
}
void ModelAssemblyDialog::bake()
{
	if (!previewReady() || m_working || !editBakedPose)
	{
		return;
	}
	play(false);
	const auto answer =
		QMessageBox::question(this, QCoreApplication::translate("ModelAssemblyDialog", "Bake One Pose"),
							  m_pose.notes.join(QStringLiteral("\n\n")), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
	if (answer != QMessageBox::Ok)
	{
		return;
	}
	QString error;
	if (!editBakedPose(m_pose.mesh, &error))
	{
		report(error);
		return;
	}
	report(QCoreApplication::translate(
		"ModelAssemblyDialog",
		"Opened the baked pose in the Mesh Editor. The assembly source retains the original links and animation settings."));
}
void ModelAssemblyDialog::cancelOperation()
{
	if (m_cancelWork)
	{
		m_cancelWork();
	}
}
bool ModelAssemblyDialog::requestClose(std::function<void()> afterDeferredClose)
{
	if (m_working || m_decidingClose)
	{
		m_afterDeferredClose = std::move(afterDeferredClose);
		close();
		return false;
	}
	return close();
}
void ModelAssemblyDialog::closeEvent(QCloseEvent *event)
{
	if (m_approvedClose)
	{
		event->accept();
		return;
	}
	if (m_decidingClose)
	{
		event->ignore();
		return;
	}
	if (m_working)
	{
		m_closeAfterWork = true;
		cancelOperation();
		event->ignore();
		return;
	}
	play(false);
	m_decidingClose = true;
	const bool approved = maybeSave();
	m_decidingClose = false;
	if (approved)
	{
		m_approvedClose = true;
		m_recoveryTimer->stop();
		retireRecovery();
		m_materialWorker->cancel();
		event->accept();
		if (m_afterDeferredClose)
		{
			auto resume = std::move(m_afterDeferredClose);
			m_afterDeferredClose = {};
			QTimer::singleShot(0, QCoreApplication::instance(), std::move(resume));
		}
	}
	else
	{
		m_afterDeferredClose = {};
		m_closeAfterWork = false;
		event->ignore();
	}
}
void ModelAssemblyDialog::reject()
{
	close();
}
void ModelAssemblyDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (m_inspectorScroll &&
		(event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange))
	{
		QTimer::singleShot(0, this, [this] { m_inspectorScroll->setMinimumWidth(m_inspector->minimumSizeHint().width() + 30); });
	}
}
} // namespace vibestudio
