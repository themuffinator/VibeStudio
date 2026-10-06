#include "app/model_material_slots_dialog.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelMaterialSlotsDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
} // namespace
ModelMaterialSlotsDialog::ModelMaterialSlotsDialog(const ModelMesh &mesh, const ModelSelection &selection, QWidget *parent)
	: QDialog(parent), m_selection(selection), m_original(mesh.surfaces.value(selection.surface).skinPaths), m_materials(m_original)
{
	setObjectName("modelMaterialSlotsDialog");
	setWindowTitle(Text::tr("Material Slots — %1").arg(mesh.surfaces.value(selection.surface).name));
	setAccessibleName(windowTitle());
	setAccessibleDescription(Text::tr("Review ordered material bindings for this surface before applying one undoable change."));
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *contents = new QWidget;
	auto *form = new QFormLayout(contents);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	scroll->setWidget(contents);
	layout->addWidget(scroll);
	m_list = new QListWidget;
	m_list->setObjectName("materialSlotList");
	m_list->setLayoutDirection(Qt::LeftToRight);
	m_list->setMinimumHeight(fontMetrics().height() * 7);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	accessible(m_list, Text::tr("Ordered material slots"),
			   Text::tr("Slot zero is the primary binding. Duplicate paths are preserved. Use Move Up or Move Down to change order."));
	form->addRow(m_list);
	m_path = new QLineEdit;
	m_path->setObjectName("materialSlotPath");
	m_path->setLayoutDirection(Qt::LeftToRight);
	m_path->setMaxLength(255);
	accessible(m_path, Text::tr("Material slot path"),
			   Text::tr("A package-relative texture or shader path. Add appends it; Replace changes the selected slot."));
	form->addRow(Text::tr("&Material"), m_path);
	auto *actions = new QGridLayout;
	const auto button = [&](const QString &label, const char *name, const QString &description, int row, int column, auto callback) {
		auto *control = new QPushButton(label);
		control->setObjectName(name);
		control->setAutoDefault(false);
		accessible(control, label, description);
		actions->addWidget(control, row, column);
		connect(control, &QPushButton::clicked, this, callback);
		return control;
	};
	m_add = button(Text::tr("&Add"), "addMaterialSlot", Text::tr("Append the entered material path as the last slot."), 0, 0,
				   [this] { change(ModelMaterialSlotAction::Insert); });
	m_replace = button(Text::tr("&Replace"), "replaceMaterialSlot", Text::tr("Replace the selected slot with the entered path."), 0, 1,
					   [this] { change(ModelMaterialSlotAction::Set); });
	m_remove =
		button(Text::tr("Re&move"), "removeMaterialSlot", Text::tr("Remove the selected slot; subsequent slots shift down one index."), 0,
			   2, [this] { change(ModelMaterialSlotAction::Remove); });
	m_up = button(Text::tr("Move &Up"), "moveMaterialSlotUp", Text::tr("Move the selected slot one index earlier."), 1, 0,
				  [this] { change(ModelMaterialSlotAction::Move); });
	m_down = button(Text::tr("Move &Down"), "moveMaterialSlotDown", Text::tr("Move the selected slot one index later."), 1, 1, [this] {
		ModelMaterialSlotEdit request;
		request.action = ModelMaterialSlotAction::Move;
		request.slot = m_list->currentRow();
		request.destination = request.slot + 1;
		QString error;
		if (editModelMaterialSlots(m_materials, request, &m_materials, &error))
			populate(request.destination);
		else
			m_summary->setText(error);
	});
	m_clear = button(Text::tr("&Clear All"), "clearMaterialSlots", Text::tr("Remove every external material slot from this surface."), 1, 2,
					 [this] { change(ModelMaterialSlotAction::Clear); });
	form->addRow(actions);
	m_summary = new QLabel;
	m_summary->setObjectName("materialSlotSummary");
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setWordWrap(true);
	m_summary->setAccessibleName(Text::tr("Material slot review"));
	form->addRow(m_summary);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("applyMaterialSlots");
	accessible(m_apply, Text::tr("Apply material slots"),
			   Text::tr("Commit the reviewed ordered list as one undoable source edit across every pose."));
	connect(m_apply, &QPushButton::clicked, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
		m_path->setText(m_materials.value(row));
		refresh();
	});
	connect(m_path, &QLineEdit::textChanged, this, [this] { refresh(); });
	populate(0);
	resize(fontMetrics().averageCharWidth() * 80, fontMetrics().height() * 26);
	m_list->setFocus(Qt::OtherFocusReason);
}
ModelEdit ModelMaterialSlotsDialog::edit() const
{
	ModelEdit result;
	result.kind = ModelEditKind::SetMaterialSlots;
	result.selection = m_selection;
	result.materialSlots = m_materials;
	return result;
}
void ModelMaterialSlotsDialog::populate(int selected)
{
	const QSignalBlocker block(m_list);
	m_list->clear();
	for (int i = 0; i < m_materials.size(); ++i)
	{
		auto *item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(i).arg(m_materials[i]), m_list);
		item->setToolTip(item->text());
	}
	selected = m_materials.isEmpty() ? -1 : std::clamp(selected, 0, int(m_materials.size()) - 1);
	m_list->setCurrentRow(selected);
	m_path->setText(m_materials.value(selected));
	refresh();
}
void ModelMaterialSlotsDialog::change(ModelMaterialSlotAction action)
{
	ModelMaterialSlotEdit request;
	request.action = action;
	request.slot = action == ModelMaterialSlotAction::Clear	   ? -1
				   : action == ModelMaterialSlotAction::Insert ? int(m_materials.size())
															   : m_list->currentRow();
	if (action == ModelMaterialSlotAction::Set || action == ModelMaterialSlotAction::Insert)
		request.materials.append(m_path->text().trimmed());
	if (action == ModelMaterialSlotAction::Move)
		request.destination = request.slot - 1;
	QString error;
	if (!editModelMaterialSlots(m_materials, request, &m_materials, &error))
	{
		m_summary->setText(error);
		return;
	}
	populate(action == ModelMaterialSlotAction::Move ? request.destination : request.slot);
}
void ModelMaterialSlotsDialog::refresh()
{
	if (!m_apply)
		return;
	const int row = m_list->currentRow();
	const auto path = m_path->text().trimmed();
	const bool pending = path != m_materials.value(row);
	ModelMaterialSlotEdit check;
	check.materials = {path};
	QStringList ignored;
	QString error;
	const bool pathValid = editModelMaterialSlots({}, check, &ignored, &error);
	m_add->setEnabled(pathValid && m_materials.size() < modelMaxMaterialSlots);
	m_replace->setEnabled(row >= 0 && pathValid && pending);
	m_remove->setEnabled(row >= 0);
	m_up->setEnabled(row > 0);
	m_down->setEnabled(row >= 0 && row + 1 < m_materials.size());
	m_clear->setEnabled(!m_materials.isEmpty());
	m_apply->setEnabled(m_materials != m_original && !pending);
	m_summary->setText(
		pending ? (pathValid ? Text::tr("The entered path is not in the reviewed list yet. Add or replace it before applying.") : error)
				: Text::tr("%1 material slots · Primary: %2").arg(m_materials.size()).arg(m_materials.value(0, Text::tr("Unassigned"))));
	m_summary->setAccessibleDescription(m_summary->text());
}
} // namespace vibestudio
