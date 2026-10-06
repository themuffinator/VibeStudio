#include "app/model_surface_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSurfaceDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
} // namespace
ModelSurfaceDialog::ModelSurfaceDialog(const ModelMesh &mesh, const ModelSelection &selection, QWidget *parent)
	: QDialog(parent), m_mesh(mesh), m_selection(selection)
{
	setObjectName("modelSurfaceDialog");
	setWindowTitle(Text::tr("Manage Surfaces"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(Text::tr("Review a surface operation across every animation pose."));
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *contents = new QWidget;
	m_form = new QFormLayout(contents);
	m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	scroll->setWidget(contents);
	layout->addWidget(scroll);
	m_operation = new QComboBox;
	m_operation->setObjectName("surfaceOperation");
	m_operation->addItem(Text::tr("Rename surface"), int(ModelEditKind::RenameSurface));
	m_operation->addItem(Text::tr("Separate selected faces"), int(ModelEditKind::SeparateFaces));
	m_operation->addItem(Text::tr("Move selected faces"), int(ModelEditKind::MoveFacesToSurface));
	m_operation->addItem(Text::tr("Duplicate surface"), int(ModelEditKind::DuplicateSurface));
	m_operation->addItem(Text::tr("Delete surface"), int(ModelEditKind::DeleteSurface));
	m_operation->addItem(Text::tr("Join surfaces"), int(ModelEditKind::JoinSurfaces));
	accessible(m_operation, Text::tr("Surface operation"), Text::tr("Choose the operation to review before applying one undoable edit."));
	m_form->addRow(Text::tr("&Operation"), m_operation);
	m_name = new QLineEdit;
	m_name->setObjectName("surfaceName");
	m_name->setMaxLength(128);
	accessible(m_name, Text::tr("Surface name"), Text::tr("Names must be unique. Game exports enforce their own stricter naming rules."));
	m_form->addRow(Text::tr("&Name"), m_name);
	m_target = new QComboBox;
	m_target->setObjectName("surfaceTarget");
	m_target->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_target->setMinimumContentsLength(16);
	accessible(m_target, Text::tr("Target surface"), Text::tr("The target keeps its name, material slots and existing vertex order."));
	m_form->addRow(Text::tr("&Target"), m_target);
	m_sources = new QTreeWidget;
	m_sources->setObjectName("surfaceJoinSources");
	m_sources->setHeaderLabels({Text::tr("Surface"), Text::tr("Triangles"), Text::tr("Materials")});
	m_sources->setRootIsDecorated(false);
	m_sources->setSelectionMode(QAbstractItemView::SingleSelection);
	m_sources->setMinimumHeight(fontMetrics().height() * 6);
	m_sources->header()->setSectionResizeMode(QHeaderView::Interactive);
	m_sources->header()->setStretchLastSection(true);
	accessible(m_sources, Text::tr("Surfaces to join"),
			   Text::tr("Check at least two surfaces, including the target. Space toggles the focused checkbox."));
	for (int i = 0; i < mesh.surfaces.size(); ++i)
	{
		const auto &surface = mesh.surfaces[i];
		const auto identity = QStringLiteral("%1: %2").arg(i).arg(surface.name);
		m_target->addItem(identity, i);
		auto *item = new QTreeWidgetItem(m_sources, {identity, QString::number(surface.triangles.size()), surface.skinPaths.join(", ")});
		item->setData(0, Qt::UserRole, i);
		item->setCheckState(0, (selection.surfaces.isEmpty() ? i == selection.surface : selection.surfaces.contains(i)) ? Qt::Checked : Qt::Unchecked);
		for (int column = 0; column < 3; ++column)
			item->setToolTip(column, item->text(column));
	}
	m_sources->setCurrentItem(m_sources->topLevelItem(selection.surface));
	sizeColumns();
	m_form->addRow(m_sources);
	m_adopt = new QCheckBox(Text::tr("Use target material bindings"));
	m_adopt->setObjectName("surfaceAdoptMaterials");
	accessible(m_adopt, m_adopt->text(), Text::tr("Explicitly replace every incoming material slot with the target surface's bindings."));
	m_form->addRow(m_adopt);
	m_summary = new QLabel;
	m_summary->setObjectName("surfaceSummary");
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setWordWrap(true);
	m_summary->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	m_summary->setAccessibleName(Text::tr("Surface operation summary"));
	m_form->addRow(m_summary);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("applySurfaceOperation");
	accessible(m_apply, Text::tr("Apply surface operation"), Text::tr("Apply this operation to every pose as one undoable change."));
	connect(m_apply, &QPushButton::clicked, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	connect(m_operation, &QComboBox::currentIndexChanged, this, [this] { refresh(true); });
	connect(m_target, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
	connect(m_name, &QLineEdit::textChanged, this, [this] { refresh(); });
	connect(m_sources, &QTreeWidget::itemChanged, this, [this] { refresh(); });
	connect(m_adopt, &QCheckBox::toggled, this, [this] { refresh(); });
	resize(fontMetrics().averageCharWidth() * 86, fontMetrics().height() * 29);
	refresh(true);
	m_operation->setFocus(Qt::OtherFocusReason);
}
void ModelSurfaceDialog::sizeColumns()
{
	if (!m_sources)
		return;
	m_sources->resizeColumnToContents(0);
	m_sources->resizeColumnToContents(1);
	// At most 32 rows are measured. Long names remain available in tooltips and
	// accessible cell text; keep room for material paths and manual resizing.
	const int limit = std::max(m_sources->header()->sectionSizeHint(0), fontMetrics().averageCharWidth() * 40);
	m_sources->setColumnWidth(0, std::min(m_sources->columnWidth(0), limit));
}
void ModelSurfaceDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::StyleChange)
		sizeColumns();
}
ModelEdit ModelSurfaceDialog::edit() const
{
	ModelEdit result;
	result.kind = ModelEditKind(m_operation->currentData().toInt());
	result.selection = m_selection;
	// The review explicitly names the active source for single-surface actions.
	if (result.kind != ModelEditKind::JoinSurfaces)
		result.selection.surfaces.clear();
	// Surface-wide operations work independently of a tag/collision inspector.
	if (result.kind != ModelEditKind::SeparateFaces && result.kind != ModelEditKind::MoveFacesToSurface)
	{
		result.selection.tag.clear();
		result.selection.collision.clear();
	}
	if (result.kind == ModelEditKind::RenameSurface || result.kind == ModelEditKind::SeparateFaces ||
		result.kind == ModelEditKind::DuplicateSurface)
		result.text = m_name->text();
	if (result.kind == ModelEditKind::MoveFacesToSurface || result.kind == ModelEditKind::JoinSurfaces)
	{
		result.targetSurface = m_target->currentData().toInt();
		result.adoptTargetMaterials = m_adopt->isChecked();
	}
	if (result.kind == ModelEditKind::JoinSurfaces)
		for (int i = 0; i < m_sources->topLevelItemCount(); ++i)
			if (m_sources->topLevelItem(i)->checkState(0) == Qt::Checked)
				result.surfaces.insert(m_sources->topLevelItem(i)->data(0, Qt::UserRole).toInt());
	return result;
}
void ModelSurfaceDialog::refresh(bool operationChanged)
{
	const auto kind = ModelEditKind(m_operation->currentData().toInt());
	const auto &source = m_mesh.surfaces[m_selection.surface];
	const bool separate = kind == ModelEditKind::SeparateFaces, move = kind == ModelEditKind::MoveFacesToSurface;
	const bool join = kind == ModelEditKind::JoinSurfaces, duplicate = kind == ModelEditKind::DuplicateSurface;
	const bool named = kind == ModelEditKind::RenameSurface || separate || duplicate;
	if (operationChanged)
	{
		QSignalBlocker nameBlock(m_name), targetBlock(m_target), adoptBlock(m_adopt);
		auto name = source.name;
		if (separate || duplicate)
		{
			int suffix = 1;
			do
				name = source.name.left(104) + (duplicate ? QStringLiteral("_copy%1") : QStringLiteral("_part%1")).arg(suffix++);
			while (std::any_of(m_mesh.surfaces.cbegin(), m_mesh.surfaces.cend(), [&](const auto &s) { return s.name == name; }));
		}
		m_name->setText(name);
		m_target->setCurrentIndex(move && m_mesh.surfaces.size() > 1 ? (m_selection.surface + 1) % m_mesh.surfaces.size()
																	 : m_selection.surface);
		m_adopt->setChecked(false);
	}
	m_form->setRowVisible(m_name, named);
	m_form->setRowVisible(m_target, move || join);
	m_form->setRowVisible(m_sources, join);
	m_form->setRowVisible(m_adopt, move || join);
	const auto request = edit();
	QString problem, summary;
	const bool all = m_selection.faces.size() == source.triangles.size();
	if (named)
	{
		if (request.text.isEmpty() || std::any_of(request.text.cbegin(), request.text.cend(),
												  [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; }))
			problem = Text::tr("Enter a name without control characters.");
		for (int i = 0; i < m_mesh.surfaces.size(); ++i)
			if (m_mesh.surfaces[i].name == request.text &&
				!(i == m_selection.surface && (kind == ModelEditKind::RenameSurface || (separate && all))))
				problem = Text::tr("This surface name already exists.");
	}
	if ((separate || move) && (m_selection.faces.isEmpty() || !m_selection.vertices.isEmpty() || !m_selection.edges.isEmpty() ||
							   !m_selection.tag.isEmpty() || !m_selection.collision.isEmpty()))
		problem = Text::tr("Select faces in the editor before separating or moving geometry.");
	if ((duplicate || (separate && !all)) && m_mesh.surfaces.size() >= modelDocumentMaxSurfaces)
		problem = Text::tr("The model already has the maximum number of surfaces.");
	if (move && request.targetSurface == m_selection.surface)
		problem = Text::tr("Choose a different target surface.");
	if (join && (request.surfaces.size() < 2 || !request.surfaces.contains(request.targetSurface)))
		problem = Text::tr("Check at least two surfaces, including the target.");
	if (kind == ModelEditKind::DeleteSurface && m_mesh.surfaces.size() == 1)
		problem = Text::tr("The last surface cannot be deleted.");
	if (move || join)
	{
		const auto incoming = move ? QSet<int>{m_selection.surface} : request.surfaces;
		const auto &target = m_mesh.surfaces[request.targetSurface];
		for (int i : incoming)
			if (!request.adoptTargetMaterials && m_mesh.surfaces[i].skinPaths != target.skinPaths)
				problem = Text::tr("Material bindings differ. Choose target materials to replace incoming bindings.");
		summary = Text::tr("Target: %1\nTarget materials: %2\n%3")
					  .arg(target.name, target.skinPaths.join(", "),
						   request.adoptTargetMaterials ? Text::tr("Incoming material bindings will be replaced.")
														: Text::tr("All ordered material bindings must match."));
	}
	if (separate)
		summary = all ? Text::tr("Every face is selected: rename the existing surface without creating an empty surface.")
					  : Text::tr("Move %1 selected faces to a new surface; copy shared boundary vertices and keep their exact attributes.")
							.arg(m_selection.faces.size());
	else if (duplicate)
		summary = Text::tr("Copy the complete surface, including unused vertices, every pose, UVs, seams and material slots.");
	else if (kind == ModelEditKind::DeleteSurface)
		summary = Text::tr("Delete %1 and its %2 triangles from every pose. Undo restores the surface.")
					  .arg(source.name)
					  .arg(source.triangles.size());
	else if (kind == ModelEditKind::RenameSurface)
		summary = Text::tr("Rename %1. External .skin files and other name-based references may need updating.").arg(source.name);
	if (move)
		summary.prepend(
			Text::tr("Move %1 selected faces. %2\n")
				.arg(m_selection.faces.size())
				.arg(all ? Text::tr("The empty source surface will be removed.") : Text::tr("Shared boundary vertices will be copied.")));
	if (join)
		summary.prepend(Text::tr("Join %1 surfaces without welding vertices.\n").arg(request.surfaces.size()));
	summary.prepend(Text::tr("Source: %1 · %2 poses\n").arg(source.name).arg(m_mesh.frames.size()));
	if (!problem.isEmpty())
		summary += QLatin1Char('\n') + problem;
	m_summary->setText(summary);
	m_apply->setEnabled(problem.isEmpty());
}
} // namespace vibestudio
