#include "app/model_import_repair_dialog.h"
#include "app/model_viewport.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelImportRepair)
};
QString description(const ModelImportRepairChange &change)
{
	switch (change.kind)
	{
	case ModelImportRepairKind::InvalidIndex:
		return Text::tr("Remove face: invalid index");
	case ModelImportRepairKind::RepeatedVertex:
		return Text::tr("Remove face: repeated vertex");
	case ModelImportRepairKind::CollapsedFace:
		return Text::tr("Remove face: collapsed pose");
	case ModelImportRepairKind::RebuildNormal:
		return change.fallback ? Text::tr("Rebuild normal: +Z fallback") : Text::tr("Rebuild normal: adjacent faces");
	case ModelImportRepairKind::RemoveSeam:
		return Text::tr("Remove orphaned seam");
	}
	return {};
}
class Changes final : public QAbstractTableModel
{
  public:
	Changes(const ModelImportRepairPlan &plan, QObject *parent) : QAbstractTableModel(parent), m_changes(plan.changes)
	{
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : int(m_changes.size());
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 4;
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section > 3)
			return {};
		return QStringList{Text::tr("Surface"), Text::tr("Change"), Text::tr("Pose"), Text::tr("Original element")}[section];
	}
	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() < 0 || index.row() >= m_changes.size() || index.column() < 0 || index.column() > 3)
			return {};
		if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole)
			return {};
		const auto &c = m_changes[index.row()];
		if (index.column() == 0)
			return c.surface;
		if (index.column() == 1)
			return description(c);
		if (index.column() == 2)
			return c.frame < 0 ? Text::tr("All") : QString::number(c.frame);
		return c.kind == ModelImportRepairKind::RemoveSeam ? QStringLiteral("%1:%2").arg(c.element).arg(c.other)
														   : QString::number(c.element);
	}

  private:
	QVector<ModelImportRepairChange> m_changes;
};
} // namespace

ModelImportRepairDialog::ModelImportRepairDialog(const ModelImportRepairPlan &plan, bool highContrast, QWidget *parent) : QDialog(parent)
{
	setObjectName("modelImportRepairDialog");
	setWindowTitle(Text::tr("Review Import Repair"));
	setAccessibleName(windowTitle());
	resize(1180, 800);
	auto *layout = new QVBoxLayout(this);
	auto *summary = new QLabel(Text::tr("Faces removed: %1 · Normals rebuilt: %2 · +Z fallbacks: %3 · Seams removed: %4")
								   .arg(plan.removedFaces)
								   .arg(plan.rebuiltNormals)
								   .arg(plan.fallbackNormals)
								   .arg(plan.removedSeams));
	summary->setObjectName("importRepairSummary");
	summary->setWordWrap(true);
	summary->setTextFormat(Qt::PlainText);
	summary->setAccessibleName(summary->text());
	layout->addWidget(summary);
	auto *scope = new QLabel(Text::tr("Face removal applies to every pose; the table shows the first collapsed pose. Positions, UVs and "
									  "usable normals remain unchanged. +Z is used only when adjacent faces cannot supply a normal."));
	scope->setWordWrap(true);
	scope->setObjectName("importRepairScope");
	layout->addWidget(scope);
	auto *split = new QSplitter;
	split->setObjectName("importRepairSplit");
	auto *table = new QTableView;
	table->setObjectName("importRepairChanges");
	table->setAccessibleName(Text::tr("Proposed import changes"));
	table->setAccessibleDescription(Text::tr("Exact zero-based source indices. Removing a face applies to all stored poses. Normals "
											 "identify one pose and vertex; seams identify their two endpoints."));
	table->setModel(new Changes(plan, table));
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setWordWrap(false);
	table->horizontalHeader()->setResizeContentsPrecision(64);
	table->resizeColumnsToContents();
	// Keep exact indices visible when translations make the description wider.
	table->horizontalHeader()->moveSection(1, 3);
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	table->verticalHeader()->setDefaultSectionSize(table->fontMetrics().height() + 12);
	split->addWidget(table);
	auto *right = new QWidget;
	auto *previewLayout = new QVBoxLayout(right);
	auto *caption = new QLabel(Text::tr("Prepared copy"));
	previewLayout->addWidget(caption);
	auto *poses = new QComboBox;
	poses->setObjectName("importRepairPose");
	poses->setAccessibleName(Text::tr("Prepared copy pose"));
	for (int pose = 0; pose < plan.mesh.frames.size(); ++pose)
		poses->addItem(QStringLiteral("%1: %2").arg(pose).arg(plan.mesh.frames[pose].name));
	previewLayout->addWidget(poses);
	auto *preview = new ModelViewport;
	preview->setObjectName("importRepairPreview");
	preview->setAccessibleName(Text::tr("Repaired model preview"));
	preview->setHighContrast(highContrast);
	preview->setReducedMotion(true);
	preview->setBackfaceCulling(false);
	preview->setMesh(plan.mesh);
	previewLayout->addWidget(preview, 1);
	auto *detail = new QLabel;
	detail->setObjectName("importRepairDetail");
	detail->setWordWrap(true);
	detail->setTextFormat(Qt::PlainText);
	detail->setAccessibleName(Text::tr("Selected import change"));
	connect(poses, &QComboBox::currentIndexChanged, preview, &ModelViewport::setFrame);
	connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
			[poses, detail, table, changes = plan.changes](const QModelIndex &row) {
				if (!row.isValid() || row.row() >= changes.size())
					return;
				const auto &change = changes[row.row()];
				if (change.frame >= 0)
					poses->setCurrentIndex(change.frame);
				detail->setText(Text::tr("Surface %1 · %2 · pose %3 · original element %4")
									.arg(change.surface)
									.arg(description(change))
									.arg(table->model()->data(table->model()->index(row.row(), 2)).toString())
									.arg(table->model()->data(table->model()->index(row.row(), 3)).toString()));
			});
	split->addWidget(right);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);
	split->setSizes({700, 440});
	layout->addWidget(split, 1);
	layout->addWidget(detail);
	auto *destination = new QLabel(Text::tr("New editable source"));
	m_output = new QLineEdit;
	m_output->setObjectName("importRepairOutput");
	m_output->setAccessibleName(destination->text());
	m_output->setLayoutDirection(Qt::LeftToRight);
	m_output->setAccessibleDescription(
		Text::tr("A new .mesh.json path. The damaged input and all existing files are protected from replacement."));
	destination->setBuddy(m_output);
	layout->addWidget(destination);
	auto *outputRow = new QHBoxLayout;
	outputRow->addWidget(m_output, 1);
	auto *browse = new QPushButton(Text::tr("Browse…"));
	browse->setAccessibleName(Text::tr("Choose repaired copy destination"));
	outputRow->addWidget(browse);
	layout->addLayout(outputRow);
	const QFileInfo input(plan.inputPath);
	QString stem = input.fileName();
	stem.chop(stem.endsWith(".mesh.json", Qt::CaseInsensitive) ? 10 : input.suffix().size() + 1);
	m_output->setText(input.dir().filePath(stem + QStringLiteral("-repaired.mesh.json")));
	connect(browse, &QPushButton::clicked, this, [this] {
		const auto path =
			QFileDialog::getSaveFileName(this, Text::tr("Save Repaired Copy"), outputPath(), Text::tr("VibeStudio mesh (*.mesh.json)"));
		if (!path.isEmpty())
			m_output->setText(path);
	});
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	auto *save = buttons->addButton(Text::tr("Save and Open Copy"), QDialogButtonBox::AcceptRole);
	save->setObjectName("saveImportRepair");
	save->setAccessibleName(save->text());
	save->setMinimumWidth(save->fontMetrics().horizontalAdvance(save->text()) + 24);
	save->setAccessibleDescription(Text::tr("Save the reviewed repairs to a new editable source, then open that copy in the modeller."));
	const auto update = [this, save] { save->setEnabled(outputPath().endsWith(".mesh.json", Qt::CaseInsensitive)); };
	connect(m_output, &QLineEdit::textChanged, this, update);
	update();
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
	layout->addWidget(buttons);
	table->setFocus();
}
QString ModelImportRepairDialog::outputPath() const
{
	return m_output->text().trimmed();
}
} // namespace vibestudio
