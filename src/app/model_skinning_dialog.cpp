#include "app/model_skinning_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "app/model_document_work.h"
#include "app/model_viewport.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinningDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
QString issues(const ModelSkinVertex &v, bool hasBindFrame)
{
	QStringList labels;
	if (v.unbound)
		labels << Text::tr("Unbound");
	if (v.negative)
		labels << Text::tr("Negative weights");
	if (v.unnormalised)
		labels << Text::tr("Not normalised");
	if (v.duplicate)
		labels << Text::tr("Duplicate joints");
	if (v.noncanonical)
		labels << Text::tr("Distinct bind offsets");
	if (v.overLimit)
		labels << Text::tr("Over influence limit");
	if (!hasBindFrame && !v.weights.isEmpty())
		labels << Text::tr("Bind offsets not checked");
	return labels.isEmpty() ? Text::tr("Valid") : labels.join(QStringLiteral(", "));
}
} // namespace
class ModelSkinTable final : public QAbstractTableModel
{
  public:
	explicit ModelSkinTable(QObject *parent) : QAbstractTableModel(parent) {}
	void setReport(const ModelSkinReport &report, const ModelMesh &mesh)
	{
		beginResetModel();
		m_report = report;
		m_mesh = mesh;
		endResetModel();
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : int(m_report.vertices.size());
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 5;
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
			return {};
		const QStringList labels{Text::tr("Surface"), Text::tr("Vertex"), Text::tr("Joint weights"), Text::tr("Sum"), Text::tr("Status")};
		return labels.value(section);
	}
	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() >= m_report.vertices.size())
			return {};
		if (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::AccessibleTextRole)
			return {};
		const auto &vertex = m_report.vertices[index.row()];
		if (index.column() == 0)
			return vertex.surface;
		if (index.column() == 1)
			return vertex.vertex;
		if (index.column() == 3)
			return localeNumber(vertex.sum, 8);
		if (index.column() == 4)
			return issues(vertex, m_report.hasBindFrame);
		QStringList weights;
		for (const auto &w : vertex.weights)
			weights << QStringLiteral("\u2066%1: %2\u2069").arg(m_mesh.skeleton.joints[w.joint].name, localeNumber(w.weight, 8));
		return weights.isEmpty() ? Text::tr("None") : weights.join(QStringLiteral("; "));
	}

  private:
	ModelSkinReport m_report;
	ModelMesh m_mesh;
};

ModelSkinningDialog::ModelSkinningDialog(const ModelMesh &mesh, const ModelSelection &selection, const QString &joint, int frame,
										 QWidget *parent)
	: QDialog(parent), m_mesh(mesh), m_selection(selection)
{
	setObjectName("modelSkinningDialog");
	setWindowTitle(Text::tr("Skin Weights and Binding"));
	accessible(this, windowTitle(),
			   Text::tr("Inspect influences, select affected vertices and review binding changes before applying one undoable edit."));
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *content = new QWidget;
	m_content = content;
	auto *body = new QVBoxLayout(content);
	m_form = new QFormLayout;
	m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	body->addLayout(m_form);
	scroll->setWidget(content);
	layout->addWidget(scroll);
	const auto combo = [&](const char *id, const QString &label, const QString &description)
	{
		auto *widget = new QComboBox;
		widget->setObjectName(id);
		widget->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		widget->setMinimumContentsLength(12);
		accessible(widget, label, description);
		m_form->addRow(label, widget);
		return widget;
	};
	m_operation = combo("skinOperation", Text::tr("&Operation"),
						Text::tr("Inspection leaves the source unchanged. Editing requires Review and Apply."));
	for (const auto &item : {qMakePair(Text::tr("Inspect weights"), -1), qMakePair(Text::tr("Bind vertices"), int(ModelEditKind::BindSkin)),
							 qMakePair(Text::tr("Rebind existing weights"), int(ModelEditKind::RebindSkin)),
							 qMakePair(Text::tr("Reduce and normalise weights"), int(ModelEditKind::PruneSkinWeights)),
							 qMakePair(Text::tr("Unbind surfaces"), int(ModelEditKind::UnbindSkin))})
		m_operation->addItem(item.first, item.second);
	m_surface = combo("skinSurface", Text::tr("&Surface"), Text::tr("Active surface for inspection and component binding."));
	for (int i = 0; i < mesh.surfaces.size(); ++i)
		m_surface->addItem(QStringLiteral("\u2066%1: %2\u2069").arg(i).arg(mesh.surfaces[i].name), i);
	m_surface->setCurrentIndex(selection.surface);
	m_scope = combo("skinScope", Text::tr("&Scope"),
					Text::tr("Choose the complete active surface or the component/surface selection captured when opening this dialog."));
	m_scope->addItem(Text::tr("Whole active surface"), 0);
	if (!selection.vertices.isEmpty() || !selection.faces.isEmpty() || !selection.edges.isEmpty())
		m_scope->addItem(Text::tr("Selected components"), 1);
	if (!selection.surfaces.isEmpty())
		m_scope->addItem(Text::tr("Selected surfaces"), 2);
	if (m_scope->count() > 1)
		m_scope->setCurrentIndex(1);
	m_method =
		combo("skinMethod", Text::tr("Binding &method"),
			  Text::tr("Nearest joints uses origins. Nearest bones measures each joint's segment to its parent; roots use their origin."));
	m_method->addItem(Text::tr("Rigid joint"), int(ModelSkinBindingMethod::Rigid));
	m_method->addItem(Text::tr("Nearest joints"), int(ModelSkinBindingMethod::NearestJoints));
	m_method->addItem(Text::tr("Nearest bones"), int(ModelSkinBindingMethod::NearestBones));
	m_method->setCurrentIndex(2);
	m_joint = combo("skinJoint", Text::tr("&Joint"), Text::tr("Joint for rigid binding or weighted-vertex selection."));
	for (const auto &j : mesh.skeleton.joints)
		m_joint->addItem(j.name, j.name);
	if (const int index = m_joint->findData(joint); index >= 0)
		m_joint->setCurrentIndex(index);
	m_allJoints = new QCheckBox(Text::tr("Consider every joint"));
	m_allJoints->setObjectName("skinAllJoints");
	m_allJoints->setChecked(true);
	accessible(m_allJoints, m_allJoints->text(), Text::tr("Turn off to choose an eligible joint pool for automatic binding."));
	m_form->addRow(m_allJoints);
	m_pool = new QListWidget;
	m_pool->setObjectName("skinJointPool");
	m_pool->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_pool->setMaximumHeight(150);
	accessible(m_pool, Text::tr("Eligible joints"),
			   Text::tr("Select candidate joints. Control adds individual joints; Shift selects a range."));
	for (const auto &j : mesh.skeleton.joints)
		m_pool->addItem(j.name);
	m_form->addRow(Text::tr("Eligible joints"), m_pool);
	m_replace = new QCheckBox(Text::tr("Replace existing weights"));
	m_replace->setObjectName("skinReplace");
	accessible(m_replace, m_replace->text(), Text::tr("Allow selected vertices on already skinned surfaces to receive a new binding."));
	m_form->addRow(m_replace);
	m_limit = new QSpinBox;
	m_limit->setObjectName("skinInfluenceLimit");
	m_limit->setRange(1, 64);
	m_limit->setValue(4);
	m_limit->setLayoutDirection(Qt::LeftToRight);
	accessible(m_limit, Text::tr("Maximum influences"),
			   Text::tr("Inspection threshold and automatic binding or reduction limit. Check the target exporter's limits separately."));
	m_form->addRow(Text::tr("Maximum influences"), m_limit);
	const auto number = [&](const char *id, const QString &label, double minimum, double maximum, double value, const QString &description)
	{
		auto *field = new QDoubleSpinBox;
		field->setObjectName(id);
		field->setDecimals(6);
		field->setRange(minimum, maximum);
		field->setValue(value);
		field->setLayoutDirection(Qt::LeftToRight);
		accessible(field, label, description);
		m_form->addRow(label, field);
		return field;
	};
	m_falloff = number("skinFalloff", Text::tr("Distance falloff"), .1, 16, 2,
					   Text::tr("Larger exponents concentrate automatic weights near the closest eligible joints or bones."));
	m_threshold = number("skinThreshold", Text::tr("Minimum retained weight"), 0, 1, .001,
						 Text::tr("Drop normalised weights below this value. A strongest influence always remains."));
	m_filter = combo("skinFilter", Text::tr("Selection filter"),
					 Text::tr("Choose a weight range or an issue to select matching vertices on the active surface."));
	for (const auto &item :
		 {qMakePair(Text::tr("Joint weight range"), ModelSkinFilter::Weighted), qMakePair(Text::tr("Unbound"), ModelSkinFilter::Unbound),
		  qMakePair(Text::tr("Not normalised"), ModelSkinFilter::Unnormalised),
		  qMakePair(Text::tr("Negative weights"), ModelSkinFilter::Negative),
		  qMakePair(Text::tr("Duplicate joints"), ModelSkinFilter::Duplicate),
		  qMakePair(Text::tr("Distinct bind offsets"), ModelSkinFilter::Noncanonical),
		  qMakePair(Text::tr("Over influence limit"), ModelSkinFilter::OverLimit)})
		m_filter->addItem(item.first, int(item.second));
	m_minimum = number("skinMinimum", Text::tr("Minimum selected weight"), 0, 1, .001,
					   Text::tr("Inclusive raw joint weight. Duplicate entries are summed."));
	m_maximum = number("skinMaximum", Text::tr("Maximum selected weight"), 0, 1, 1, Text::tr("Inclusive upper weight bound."));
	m_contract = new QLabel;
	m_contract->setWordWrap(true);
	m_contract->setTextFormat(Qt::PlainText);
	m_contract->setAccessibleName(Text::tr("Skinning operation scope"));
	body->addWidget(m_contract);
	auto *tabs = new QTabWidget;
	tabs->setObjectName("skinReviewTabs");
	tabs->setMinimumHeight(320);
	auto *previewPage = new QWidget;
	auto *previewLayout = new QVBoxLayout(previewPage);
	m_preview = new ModelViewport;
	m_preview->setObjectName("skinPreview");
	m_preview->setMinimumHeight(250);
	m_preview->setShowJoints(true);
	m_preview->setMesh(mesh);
	previewLayout->addWidget(m_preview, 1);
	m_frame = new QComboBox;
	m_frame->setObjectName("skinPreviewFrame");
	accessible(m_frame, Text::tr("Preview frame"),
			   Text::tr("Inspect the bind frame and retained animation samples before applying the reviewed binding."));
	for (int f = 0; f < mesh.frames.size(); ++f)
		m_frame->addItem(QStringLiteral("\u2066%1: %2\u2069").arg(f).arg(mesh.frames[f].name), f);
	m_frame->setCurrentIndex(std::clamp(frame, 0, int(mesh.frames.size()) - 1));
	m_preview->setFrame(m_frame->currentIndex());
	previewLayout->addWidget(m_frame);
	tabs->addTab(previewPage, Text::tr("Preview"));
	m_table = new QTableView;
	m_table->setObjectName("skinWeightsTable");
	m_model = new ModelSkinTable(this);
	m_table->setModel(m_model);
	accessible(m_table, Text::tr("Vertex weights"),
			   Text::tr("Exact joint weights, their raw sum and issue labels. Status never depends on colour."));
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->horizontalHeader()->setResizeContentsPrecision(200);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_table->verticalHeader()->hide();
	auto *weightsPage = new QWidget;
	auto *weightsLayout = new QVBoxLayout(weightsPage);
	weightsLayout->setContentsMargins(0, 0, 0, 0);
	weightsLayout->addWidget(m_table, 1);
	auto *details = new QPlainTextEdit;
	details->setObjectName("skinWeightDetails");
	details->setReadOnly(true);
	details->setMaximumHeight(180);
	details->setPlaceholderText(Text::tr("Select a row to read all of its joint weights."));
	accessible(details, Text::tr("Selected vertex weights"),
			   Text::tr("Complete influence values for the selected row. Read or copy values when the table columns are too narrow."));
	weightsLayout->addWidget(details);
	connect(
		m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
		[this, details](const QModelIndex &index)
		{
			if (!index.isValid())
			{
				details->clear();
				return;
			}
			const auto cell = [&](int column) { return m_model->data(m_model->index(index.row(), column), Qt::DisplayRole).toString(); };
			details->setPlainText(Text::tr("Surface %1 · vertex %2 · sum %3\n%4\n%5")
									  .arg(cell(0), cell(1), cell(3), cell(2).replace(QStringLiteral("; "), QLatin1String("\n")), cell(4)));
		});
	connect(m_model, &QAbstractItemModel::modelReset, details, &QPlainTextEdit::clear);
	tabs->addTab(weightsPage, Text::tr("Weights"));
	body->addWidget(tabs);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("skinReviewStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setAccessibleName(Text::tr("Skinning review status"));
	body->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_review = buttons->addButton(Text::tr("&Review"), QDialogButtonBox::ActionRole);
	m_review->setObjectName("reviewSkinning");
	m_review->setDefault(true);
	m_apply = buttons->addButton(Text::tr("&Apply"), QDialogButtonBox::ActionRole);
	m_apply->setObjectName("applySkinning");
	m_select = buttons->addButton(Text::tr("Select vertices"), QDialogButtonBox::ActionRole);
	m_select->setObjectName("selectSkinVertices");
	accessible(m_review, Text::tr("Review skinning"),
			   Text::tr("Validate in a cancellable worker, then show the proposed skin and exact resulting weights."));
	accessible(m_apply, Text::tr("Apply skinning"), Text::tr("Commit the reviewed binding as one undoable document operation."));
	accessible(m_select, m_select->text(), Text::tr("Return matching vertex indices to the Mesh Editor without editing geometry."));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelSkinningDialog::reject);
	connect(m_review, &QPushButton::clicked, this, &ModelSkinningDialog::review);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (m_ready && m_operation->currentData().toInt() >= 0)
					accept();
			});
	connect(m_select, &QPushButton::clicked, this, &ModelSkinningDialog::chooseVertices);
	connect(m_frame, &QComboBox::currentIndexChanged, this, [this](int f) { m_preview->setFrame(f); });
	for (auto *field : {m_operation, m_surface, m_scope, m_method})
		connect(field, &QComboBox::currentIndexChanged, this, [this] { changed(); });
	connect(m_joint, &QComboBox::currentIndexChanged, this,
			[this]
			{
				if (m_operation->currentData().toInt() >= 0)
					changed();
			});
	for (auto *field : {m_allJoints, m_replace})
		connect(field, &QCheckBox::toggled, this, [this] { changed(); });
	connect(m_pool, &QListWidget::itemSelectionChanged, this, [this] { changed(); });
	connect(m_limit, &QSpinBox::valueChanged, this, [this] { changed(); });
	for (auto *field : {m_falloff, m_threshold})
		connect(field, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
	connect(m_filter, &QComboBox::currentIndexChanged, this,
			[this]
			{
				const bool weighted = m_filter->currentData().toInt() == int(ModelSkinFilter::Weighted);
				m_minimum->setEnabled(weighted);
				m_maximum->setEnabled(weighted);
			});
	fitDialogToScreen(this, QSize(920, 820));
	changed();
	QTimer::singleShot(0, this, [this] { review(); });
}
ModelSelection ModelSkinningDialog::target() const
{
	ModelSelection result;
	result.surface = m_surface->currentData().toInt();
	if (m_scope->currentData().toInt() == 1)
	{
		result = m_selection;
		result.surfaces.clear();
	}
	if (m_scope->currentData().toInt() == 2)
		result.surfaces = m_selection.surfaces;
	return result;
}
ModelEdit ModelSkinningDialog::request() const
{
	ModelEdit result;
	result.kind = ModelEditKind(m_operation->currentData().toInt());
	result.selection = target();
	if (result.kind == ModelEditKind::BindSkin)
	{
		result.skin.method = ModelSkinBindingMethod(m_method->currentData().toInt());
		result.skin.replaceExisting = m_replace->isChecked();
		if (result.skin.method == ModelSkinBindingMethod::Rigid)
			result.skin.joints = {m_joint->currentData().toString()};
		else
		{
			result.skin.maxInfluences = m_limit->value();
			result.skin.falloff = m_falloff->value();
			if (!m_allJoints->isChecked())
				for (const auto *item : m_pool->selectedItems())
					result.skin.joints << item->text();
		}
	}
	if (result.kind == ModelEditKind::PruneSkinWeights)
	{
		result.skin.maxInfluences = m_limit->value();
		result.skin.minimumWeight = m_threshold->value();
	}
	return result;
}
ModelEdit ModelSkinningDialog::edit() const
{
	return m_reviewed;
}
void ModelSkinningDialog::changed()
{
	m_ready = false;
	m_apply->setEnabled(false);
	m_select->setEnabled(false);
	const int kind = m_operation->currentData().toInt();
	const bool inspect = kind < 0, bind = kind == int(ModelEditKind::BindSkin), prune = kind == int(ModelEditKind::PruneSkinWeights);
	const bool automatic = bind && m_method->currentData().toInt() != int(ModelSkinBindingMethod::Rigid);
	m_surface->setEnabled(m_scope->currentData().toInt() == 0);
	m_form->setRowVisible(m_method, bind);
	m_form->setRowVisible(m_joint, inspect || (bind && !automatic));
	m_form->setRowVisible(m_allJoints, automatic);
	m_form->setRowVisible(m_pool, automatic && !m_allJoints->isChecked());
	m_form->setRowVisible(m_replace, bind);
	m_form->setRowVisible(m_limit, inspect || automatic || prune);
	m_form->setRowVisible(m_falloff, automatic);
	m_form->setRowVisible(m_threshold, prune);
	for (auto *field : QVector<QWidget *>{m_filter, m_minimum, m_maximum})
		m_form->setRowVisible(field, inspect);
	m_select->setVisible(inspect);
	m_apply->setVisible(!inspect);
	m_contract->setText(
		inspect
			? Text::tr("Inspect raw weights and select vertices on one surface. Distinct offsets need a retained bind frame to diagnose.")
		: kind == int(ModelEditKind::UnbindSkin) ? Text::tr("Remove skinning from complete surfaces and keep every baked pose. Skeletal "
															"export and later clip edits may need rebinding.")
		: kind == int(ModelEditKind::RebindSkin)
			? Text::tr("Keep positive joint assignments, normalise them and rebuild joint-space offsets from the current bind geometry. "
					   "This explicitly changes imported deformation; review every relevant pose.")
			: Text::tr("Preserve the bind shape and update retained skeletal poses on the target surfaces. Independent geometry and normal "
					   "edits block replacement. Attachments and collision keep their own tracks."));
	m_status->setText(Text::tr("Choose Review to inspect the current scope and settings."));
	m_model->setReport({}, m_mesh);
	m_preview->setMesh(m_mesh, true);
	m_preview->setFrame(m_frame->currentIndex());
}
void ModelSkinningDialog::review()
{
	if (m_loading)
		return;
	const auto operation = request();
	const auto selection = target();
	const int limit = m_limit->value();
	const bool inspect = m_operation->currentData().toInt() < 0;
	if (!inspect && operation.kind == ModelEditKind::BindSkin && operation.skin.method != ModelSkinBindingMethod::Rigid &&
		!m_allJoints->isChecked() && operation.skin.joints.isEmpty())
	{
		m_status->setText(Text::tr("Select eligible joints or enable Consider every joint."));
		return;
	}
	m_ready = false;
	m_loading = true;
	m_content->setEnabled(false);
	m_review->setEnabled(false);
	m_apply->setEnabled(false);
	m_select->setEnabled(false);
	ModelMesh result;
	ModelSkinReport report;
	QString error;
	const auto source = m_mesh;
	const bool ok = runModelTask(
		this, Text::tr("Review Skinning"),
		[source, operation, selection, limit, inspect, &result, &report](QString *failure, const ModelWorkControl &control)
		{
			result = source;
			if (!inspect)
			{
				ModelSelection output;
				if (!applyModelEdit(&result, operation, &output, failure, control))
					return false;
			}
			return inspectModelSkinning(result, selection, limit, &report, failure, control);
		},
		&error, false, &m_cancel);
	m_loading = false;
	m_content->setEnabled(true);
	m_review->setEnabled(true);
	if (m_closeAfter)
	{
		QDialog::reject();
		return;
	}
	if (!ok)
	{
		m_apply->setEnabled(false);
		m_select->setEnabled(false);
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_report = std::move(report);
	m_reviewed = operation;
	m_ready = true;
	m_model->setReport(m_report, result);
	m_preview->setMesh(result, true);
	m_preview->setFrame(m_frame->currentIndex());
	m_apply->setEnabled(!inspect);
	m_select->setEnabled(inspect && m_scope->currentData().toInt() != 2);
	m_status->setText(Text::tr("Reviewed %Ln vert(ex)(ices) · unbound %L1 · not normalised %L2 · over limit %L3 · distinct bind offsets %L4. %5", nullptr,
							   int(m_report.vertices.size()))
						  .arg(m_report.unbound)
						  .arg(m_report.unnormalised)
						  .arg(m_report.overLimit)
						  .arg(m_report.noncanonical)
						  .arg(inspect ? Text::tr("Source unchanged.") : Text::tr("Preview only; Apply commits one undo step.")),
					  LiveStatus::Result);
}
void ModelSkinningDialog::chooseVertices()
{
	if (!m_ready || m_operation->currentData().toInt() >= 0)
		return;
	QString error;
	if (!selectModelSkinVertices(m_mesh, m_report, target().surface, ModelSkinFilter(m_filter->currentData().toInt()),
								 m_joint->currentData().toString(), m_minimum->value(), m_maximum->value(), &m_chosen, &error))
	{
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_choosingSelection = true;
	accept();
}
void ModelSkinningDialog::reject()
{
	if (m_loading)
	{
		m_closeAfter = true;
		if (m_cancel)
			m_cancel();
		return;
	}
	QDialog::reject();
}
} // namespace vibestudio
