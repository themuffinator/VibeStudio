#include "app/model_lod_dialog.h"

#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelLod)
};
enum Mode
{
	Ratio,
	Triangles,
	HandMade,
};
} // namespace

ModelLodDialog::ModelLodDialog(ModelEditorDialog *editor) : QDialog(editor), m_editor(editor)
{
	setObjectName(QStringLiteral("meshLodDialog"));
	setWindowTitle(Text::tr("Detail Levels"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(Text::tr("Choose how each detail level is made, review the set the game will load, and store it in the source or export it."));
	findModelTargetProfile(QStringLiteral("md3"), editor->deliveryTarget(QStringLiteral("md3")), &m_target);
	const auto &document = editor->m_document;
	m_baseDirectory = document.path().isEmpty() ? QString() : QFileInfo(document.path()).absolutePath();
	const int loaded = std::max(0, std::min(modelLodMaxLevels, m_target.limits.detailLevels));
	auto *layout = new QVBoxLayout(this);
	m_summary = new QLabel(loaded == 0 ? Text::tr("%1 loads no detail levels beside the model; choose another MD3 delivery target to make them.").arg(m_target.label)
									   : Text::tr("%1 loads %n detail level(s) beside the model, name_1.md3 and on, chosen by distance.", nullptr, loaded)
											 .arg(m_target.label));
	m_summary->setObjectName(QStringLiteral("meshLodSummary"));
	m_summary->setWordWrap(true);
	layout->addWidget(m_summary);
	auto *countRow = new QFormLayout;
	m_count = new QSpinBox;
	m_count->setObjectName(QStringLiteral("meshLodCount"));
	m_count->setRange(0, loaded);
	m_count->setAccessibleName(Text::tr("Detail levels"));
	m_count->setAccessibleDescription(Text::tr("How many detail levels the source owns, up to what the delivery target loads."));
	countRow->addRow(Text::tr("&Detail levels:"), m_count);
	layout->addLayout(countRow);
	for (int level = 1; level <= modelLodMaxLevels; ++level)
	{
		LevelControls row;
		auto *group = new QGroupBox(Text::tr("Level %1").arg(level));
		group->setObjectName(QStringLiteral("meshLodLevel-%1").arg(level));
		auto *form = new QFormLayout(group);
		row.group = group;
		row.mode = new QComboBox;
		row.mode->setObjectName(QStringLiteral("meshLodMode-%1").arg(level));
		row.mode->addItems({Text::tr("Generated: keep a share of the triangles"), Text::tr("Generated: reduce to a triangle count"), Text::tr("Hand-made model")});
		row.mode->setAccessibleName(Text::tr("How level %1 is made").arg(level));
		form->addRow(Text::tr("Made by:"), row.mode);
		row.ratio = new QDoubleSpinBox;
		row.ratio->setObjectName(QStringLiteral("meshLodRatio-%1").arg(level));
		row.ratio->setRange(0.05, 0.95);
		row.ratio->setSingleStep(0.05);
		row.ratio->setDecimals(2);
		row.ratio->setValue(0.5);
		row.ratio->setAccessibleName(Text::tr("Share of the level above's triangles kept by level %1").arg(level));
		form->addRow(Text::tr("Share kept:"), row.ratio);
		row.triangles = new QSpinBox;
		row.triangles->setObjectName(QStringLiteral("meshLodTriangles-%1").arg(level));
		row.triangles->setRange(1, 1000000);
		row.triangles->setValue(500);
		row.triangles->setAccessibleName(Text::tr("Triangles of level %1").arg(level));
		form->addRow(Text::tr("Triangles:"), row.triangles);
		auto *sourceRow = new QHBoxLayout;
		row.source = new QLineEdit;
		row.source->setObjectName(QStringLiteral("meshLodSource-%1").arg(level));
		row.source->setAccessibleName(Text::tr("Model file of level %1").arg(level));
		row.source->setAccessibleDescription(Text::tr("An editable source or native model, absolute or relative to the source's folder. It needs the base model's frames and surface names."));
		row.source->setToolTip(row.source->accessibleDescription());
		row.browse = new QPushButton(Text::tr("Browse…"));
		row.browse->setObjectName(QStringLiteral("meshLodBrowse-%1").arg(level));
		row.browse->setAccessibleName(Text::tr("Choose the model file of level %1").arg(level));
		sourceRow->addWidget(row.source, 1);
		sourceRow->addWidget(row.browse);
		form->addRow(Text::tr("Model file:"), sourceRow);
		layout->addWidget(group);
		connect(row.mode, &QComboBox::currentIndexChanged, this, [this] { controlsChanged(); });
		connect(row.ratio, &QDoubleSpinBox::valueChanged, this, [this] { controlsChanged(); });
		connect(row.triangles, &QSpinBox::valueChanged, this, [this] { controlsChanged(); });
		connect(row.source, &QLineEdit::textChanged, this, [this] { controlsChanged(); });
		connect(row.browse, &QPushButton::clicked, this,
				[this, level]
				{
					const auto path = QFileDialog::getOpenFileName(this, Text::tr("Choose the Model of Level %1").arg(level), m_baseDirectory,
																   Text::tr("Models (*.mesh.json *.md3 *.md2 *.mdl *.obj)"));
					if (!path.isEmpty())
						m_rows[level - 1].source->setText(m_baseDirectory.isEmpty() ? path : QDir(m_baseDirectory).relativeFilePath(path));
				});
		m_rows.append(row);
	}
	connect(m_count, &QSpinBox::valueChanged, this, [this] { controlsChanged(); });
	auto *actions = new QHBoxLayout;
	m_review = new QPushButton(Text::tr("&Review"));
	m_review->setObjectName(QStringLiteral("meshLodReview"));
	m_review->setAccessibleName(Text::tr("Review the detail levels"));
	m_review->setAccessibleDescription(Text::tr("Builds every level on the worker and checks the set the way the game reads it."));
	m_preview = new QComboBox;
	m_preview->setObjectName(QStringLiteral("meshLodPreview"));
	m_preview->setAccessibleName(Text::tr("Level shown in the viewport"));
	m_preview->setAccessibleDescription(Text::tr("Shows a reviewed level in the viewport in place of the model; the model does not change."));
	actions->addWidget(m_review);
	actions->addStretch();
	actions->addWidget(new QLabel(Text::tr("Show:")));
	actions->addWidget(m_preview);
	layout->addLayout(actions);
	m_surfaces = new QTableWidget;
	m_surfaces->setObjectName(QStringLiteral("meshLodSurfaces"));
	m_surfaces->setAccessibleName(Text::tr("Triangles per surface and level"));
	m_surfaces->setEditTriggers(QAbstractItemView::NoEditTriggers);
	// Tab leaves the table for the next control; the arrow keys move between cells.
	m_surfaces->setTabKeyNavigation(false);
	m_surfaces->horizontalHeader()->setStretchLastSection(true);
	layout->addWidget(m_surfaces, 1);
	m_findings = new QPlainTextEdit;
	m_findings->setObjectName(QStringLiteral("meshLodFindings"));
	m_findings->setReadOnly(true);
	m_findings->setAccessibleName(Text::tr("Detail level review"));
	m_findings->setAccessibleDescription(Text::tr("Problems the game would have with the set, notes, and the distance at which each level shows."));
	layout->addWidget(m_findings, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_store = buttons->addButton(Text::tr("&Store in Source"), QDialogButtonBox::ActionRole);
	m_store->setObjectName(QStringLiteral("meshLodStore"));
	m_store->setAccessibleName(m_store->text().remove(QLatin1Char('&')));
	m_store->setAccessibleDescription(Text::tr("Keeps the reviewed levels in the editable source as one undo step; Stage in Package and Export use them."));
	m_export = buttons->addButton(Text::tr("&Export…"), QDialogButtonBox::ActionRole);
	m_export->setObjectName(QStringLiteral("meshLodExport"));
	m_export->setAccessibleName(Text::tr("Export the detail set"));
	m_export->setAccessibleDescription(Text::tr("Writes the base MD3 and each reviewed level beside it."));
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	connect(m_review, &QPushButton::clicked, this, [this] { review(); });
	connect(m_preview, &QComboBox::currentIndexChanged, this, [this](int index) { previewLevel(std::max(0, index)); });
	connect(m_store, &QPushButton::clicked, this, [this] { store(); });
	connect(m_export, &QPushButton::clicked, this,
			[this]
			{
				const auto path = QFileDialog::getSaveFileName(this, Text::tr("Export Detail Levels"), {}, Text::tr("Quake III model (*.md3)"));
				if (!path.isEmpty())
					exportTo(path);
			});
	const auto stored = document.mesh().detailLevels;
	setRecipe(stored.isEmpty() ? modelLodGeneratedRecipe(std::min(2, loaded), 0.5) : stored);
	resize(760, 720);
	m_count->setFocus(Qt::OtherFocusReason);
}

ModelLodDialog::~ModelLodDialog() = default;

QVector<ModelLodRecipeLevel> ModelLodDialog::recipe() const
{
	QVector<ModelLodRecipeLevel> result;
	for (int index = 0; index < m_count->value() && index < m_rows.size(); ++index)
	{
		const auto &row = m_rows.at(index);
		ModelLodRecipeLevel level;
		level.level = index + 1;
		level.preserveBoundary = index == 0;
		switch (row.mode->currentIndex())
		{
		case Triangles:
			level.targetTriangles = row.triangles->value();
			break;
		case HandMade:
			// Stored as a hand-made level keeps no generation settings.
			level.generated = false;
			level.preserveBoundary = true;
			// Shown relative to the source's folder, held absolute while it is
			// open (ModelDocument::save writes it relative again).
			level.source = row.source->text().trimmed();
			if (!level.source.isEmpty() && !m_baseDirectory.isEmpty())
				level.source = modelLodRecipeSourcePath(level, m_baseDirectory);
			break;
		default:
			level.ratio = row.ratio->value();
			break;
		}
		result.append(level);
	}
	return result;
}

void ModelLodDialog::setRecipe(const QVector<ModelLodRecipeLevel> &recipe)
{
	m_count->setValue(std::min(int(recipe.size()), m_count->maximum()));
	for (int index = 0; index < recipe.size() && index < m_rows.size(); ++index)
	{
		const auto &level = recipe.at(index);
		auto &row = m_rows[index];
		row.mode->setCurrentIndex(!level.generated ? HandMade : level.targetTriangles > 0 ? Triangles : Ratio);
		if (level.generated && level.targetTriangles == 0)
			row.ratio->setValue(level.ratio);
		if (level.targetTriangles > 0)
			row.triangles->setValue(level.targetTriangles);
		row.source->setText(!m_baseDirectory.isEmpty() && QFileInfo(level.source).isAbsolute() ? QDir(m_baseDirectory).relativeFilePath(level.source)
																							   : level.source);
	}
	controlsChanged();
}

bool ModelLodDialog::reviewed() const
{
	return !m_reviewedRevision.isEmpty() && m_reviewedRevision == m_editor->m_document.revisionFingerprint() && m_reviewedRecipe == recipe() &&
		   m_validation.ok() && !m_reviewedRecipe.isEmpty();
}

void ModelLodDialog::controlsChanged()
{
	for (int index = 0; index < m_rows.size(); ++index)
	{
		auto &row = m_rows[index];
		row.group->setVisible(index < m_count->value());
		const int mode = row.mode->currentIndex();
		row.ratio->setEnabled(mode == Ratio);
		row.triangles->setEnabled(mode == Triangles);
		row.source->setEnabled(mode == HandMade);
		row.browse->setEnabled(mode == HandMade);
	}
	m_review->setEnabled(m_count->value() > 0);
	// The review describes the controls it was made for.
	const bool current = reviewed();
	m_store->setEnabled(current || (m_count->value() == 0 && !m_editor->m_document.mesh().detailLevels.isEmpty()));
	m_export->setEnabled(current);
	if (!current && !m_reviewedRecipe.isEmpty() && m_reviewedRecipe != recipe())
		m_findings->setPlainText(Text::tr("The levels changed since the review. Review them again before storing or exporting."));
}

bool ModelLodDialog::review()
{
	const auto wanted = recipe();
	QString error;
	if (!validateModelLodRecipe(wanted, std::max(0, m_target.limits.detailLevels), &error))
	{
		m_findings->setPlainText(error);
		controlsChanged();
		return false;
	}
	QVector<ModelLodLevel> levels;
	ModelLodValidation validation;
	const auto target = m_target;
	const auto baseDirectory = m_baseDirectory;
	if (!m_editor->performWork(
			Text::tr("Review Detail Levels"),
			[wanted, target, baseDirectory, &levels, &validation](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				if (!buildModelLodsFromRecipe(candidate.mesh(), wanted, baseDirectory, &levels, failure, control))
					return false;
				QVector<ModelMesh> meshes;
				for (const auto &level : std::as_const(levels))
					meshes.append(level.mesh);
				return validateModelLodSet(candidate.mesh(), meshes, target, &validation, failure, control);
			},
			&error))
	{
		m_levels.clear();
		m_validation = {};
		m_reviewedRecipe.clear();
		m_reviewedRevision.clear();
		m_findings->setPlainText(error);
		showReview();
		controlsChanged();
		return false;
	}
	m_levels = std::move(levels);
	m_validation = std::move(validation);
	m_reviewedRecipe = wanted;
	m_reviewedRevision = m_editor->m_document.revisionFingerprint();
	showReview();
	controlsChanged();
	return m_validation.ok();
}

void ModelLodDialog::showReview()
{
	const auto &base = m_editor->m_document.mesh();
	QVector<ModelMesh> meshes;
	for (const auto &level : std::as_const(m_levels))
		meshes.append(level.mesh);
	const auto rows = modelLodSurfaceTable(base, meshes);
	m_surfaces->clear();
	m_surfaces->setColumnCount(int(meshes.size()) + 2);
	QStringList headers{Text::tr("Surface"), Text::tr("Base")};
	for (int level = 1; level <= meshes.size(); ++level)
		headers << Text::tr("Level %1").arg(level);
	m_surfaces->setHorizontalHeaderLabels(headers);
	m_surfaces->setRowCount(m_levels.isEmpty() ? 0 : int(rows.size()));
	for (int row = 0; !m_levels.isEmpty() && row < rows.size(); ++row)
	{
		m_surfaces->setItem(row, 0, new QTableWidgetItem(rows[row].surface));
		for (int column = 0; column < rows[row].triangles.size(); ++column)
		{
			const int count = rows[row].triangles[column];
			// Words, not colour, mark a level without the surface.
			auto *item = new QTableWidgetItem(count < 0 ? Text::tr("missing") : QString::number(count));
			item->setTextAlignment(Qt::AlignVCenter | Qt::AlignTrailing);
			m_surfaces->setItem(row, column + 1, item);
		}
	}
	{
		const QSignalBlocker blocker(m_preview);
		m_preview->clear();
		m_preview->addItem(Text::tr("The model"));
		for (int level = 1; level <= m_levels.size(); ++level)
			m_preview->addItem(Text::tr("Level %1 (%n triangle(s))", nullptr, m_levels[level - 1].triangles).arg(level));
		m_preview->setEnabled(!m_levels.isEmpty());
	}
	if (m_levels.isEmpty())
		return;
	QStringList lines;
	const int errors = m_validation.count(ModelLodSeverity::Error);
	lines << (errors == 0 ? Text::tr("The set is consistent for %1.").arg(m_target.label)
						  : Text::tr("The game would read this set wrongly (%n problem(s)):", nullptr, errors));
	lines += modelLodValidationLines(m_validation);
	for (const auto &level : std::as_const(m_levels))
		lines += level.notes;
	const auto distances = modelQuake3LodSwitchDistances(modelLodRadius(base), int(m_levels.size()) + 1);
	for (int level = 1; level < distances.size(); ++level)
		lines << (distances[level] < 0 ? Text::tr("Level %1 never shows with the default view.").arg(level)
									   : Text::tr("Level %1 shows from %2 units away (cg_fov 90, r_lodscale 5, r_lodbias 0).").arg(level).arg(distances[level], 0, 'f', 1));
	m_findings->setPlainText(lines.join(QLatin1Char('\n')));
}

void ModelLodDialog::previewLevel(int level)
{
	if (level > 0 && level <= m_levels.size())
		m_editor->previewTransientMesh(&m_levels[level - 1].mesh);
	else
		m_editor->previewTransientMesh(nullptr);
}

bool ModelLodDialog::store(QString *error)
{
	QString local;
	if (!error)
		error = &local;
	const auto wanted = recipe();
	if (!wanted.isEmpty() && !reviewed())
	{
		*error = Text::tr("Review the levels before storing them.");
		m_findings->setPlainText(*error);
		return false;
	}
	auto mesh = m_editor->m_document.mesh();
	mesh.detailLevels = wanted;
	const auto selection = m_editor->m_document.selection();
	const auto base = m_editor->m_document.revisionFingerprint();
	if (!m_editor->performWork(
			Text::tr("Store Detail Levels"),
			[mesh, selection, base](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{ return candidate.adopt(mesh, selection, base, failure, control); },
			error))
	{
		m_findings->setPlainText(*error);
		return false;
	}
	m_editor->refresh();
	{
		// The refresh shows the model again.
		const QSignalBlocker blocker(m_preview);
		m_preview->setCurrentIndex(0);
	}
	// The document changed, so the review follows its new revision.
	m_reviewedRevision = m_editor->m_document.revisionFingerprint();
	const int count = int(wanted.size());
	m_editor->m_status->setText(count == 0 ? Text::tr("The source no longer stores detail levels. Save to keep the change.")
										   : Text::tr("Stored %n detail level(s) in the source as one undo step. Save to keep them; Stage in Package now stages them too.",
													  nullptr, count));
	controlsChanged();
	return true;
}

bool ModelLodDialog::exportTo(const QString &basePath, QString *error)
{
	QString local;
	if (!error)
		error = &local;
	if (!reviewed())
	{
		*error = Text::tr("Review the levels before exporting them.");
		m_findings->setPlainText(*error);
		return false;
	}
	previewLevel(0);
	return m_editor->tools()->exportDetailLevels(basePath, m_reviewedRecipe, error);
}

void ModelLodDialog::done(int result)
{
	// The viewport shows the document again whichever way the dialog closes.
	m_editor->previewTransientMesh(nullptr);
	QDialog::done(result);
}

bool ModelEditorDialog::reviewDetailLevels(QString *error)
{
	if (m_working)
	{
		if (error)
			*error = QCoreApplication::translate("VibeStudioModelLod", "Wait for the current model operation to finish.");
		return false;
	}
	ModelLodDialog dialog(this);
	dialog.exec();
	return true;
}
} // namespace vibestudio
