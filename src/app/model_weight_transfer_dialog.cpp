#include "app/model_weight_transfer_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "app/model_document_work.h"
#include "app/model_viewport.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelWeightTransferDialog)
};
void accessible(QWidget *w, const QString &name, const QString &description)
{
	w->setAccessibleName(name);
	w->setAccessibleDescription(description);
	w->setToolTip(description);
}
void row(QFormLayout *form, const QString &text, QWidget *widget)
{
	auto *label = new QLabel(text);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	label->setBuddy(widget);
	form->addRow(label, widget);
}
QString weights(const ModelSurface &s, int vertex, const ModelSkeleton &skeleton)
{
	if (s.skinning.isEmpty())
		return Text::tr("Unbound");
	QMap<int, double> values;
	for (int i = s.skinning.first[vertex]; i < s.skinning.first[vertex] + s.skinning.count[vertex]; ++i)
		values[s.skinning.influences[i].joint] += s.skinning.influences[i].weight;
	QStringList result;
	for (auto it = values.cbegin(); it != values.cend(); ++it)
		result << QStringLiteral("\u2066%1: %2\u2069").arg(skeleton.joints[it.key()].name, localeNumber(it.value(), 17));
	return result.join(QStringLiteral("; "));
}
} // namespace
class ModelTransferTable final : public QAbstractTableModel
{
  public:
	explicit ModelTransferTable(QObject *parent) : QAbstractTableModel(parent) {}
	void setMeshes(const ModelMesh &before, const ModelMesh &after, int surface)
	{
		beginResetModel();
		m_before = before;
		m_after = after;
		m_surface = surface;
		endResetModel();
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() || m_after.surfaces.isEmpty() ? 0 : m_after.surfaces[m_surface].vertexCount;
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 3;
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
			return {};
		return QStringList{Text::tr("Vertex"), Text::tr("Before"), Text::tr("After")}.value(section);
	}
	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() >= rowCount() ||
			(role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole))
			return {};
		if (index.column() == 0)
			return index.row();
		const auto &mesh = index.column() == 1 ? m_before : m_after;
		return weights(mesh.surfaces[m_surface], index.row(), mesh.skeleton);
	}

  private:
	ModelMesh m_before, m_after;
	int m_surface = 0;
};
ModelWeightTransferDialog::ModelWeightTransferDialog(const ModelMesh &mesh, const ModelSelection &selection, int frame, QWidget *parent)
	: QDialog(parent), m_source(mesh), m_result(mesh), m_selection(selection)
{
	setObjectName("modelWeightTransferDialog");
	setWindowTitle(Text::tr("Transfer Skin Weights"));
	accessible(
		this, windowTitle(),
		Text::tr("Review bind-space weight transfer or mirroring, joint mapping and animation poses before applying one undoable edit."));
	auto *layout = new QVBoxLayout(this);
	m_tabs = new QTabWidget;
	m_tabs->setObjectName("transferTabs");
	layout->addWidget(m_tabs);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *settings = new QWidget;
	auto *body = new QVBoxLayout(settings);
	auto *form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	body->addLayout(form);
	body->addStretch();
	scroll->setWidget(settings);
	m_tabs->addTab(scroll, Text::tr("&Transfer"));
	const auto combo = [&](const char *id, const QString &name, const QString &description)
	{
		auto *w = new QComboBox;
		w->setObjectName(id);
		w->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		w->setMinimumContentsLength(12);
		accessible(w, name, description);
		row(form, name, w);
		return w;
	};
	m_from = combo("transferSource", Text::tr("&Source surface"),
				   Text::tr("Weights are sampled from the original bind shape. The source is frozen throughout the transfer."));
	m_to = combo("transferTarget", Text::tr("&Destination surface"),
				 Text::tr("Only this surface receives weights. Unbound surfaces require a match for every vertex at full strength."));
	for (int i = 0; i < mesh.surfaces.size(); ++i)
	{
		const auto label = QStringLiteral("\u2066%1: %2\u2069").arg(i).arg(mesh.surfaces[i].name);
		if (!mesh.surfaces[i].skinning.isEmpty())
			m_from->addItem(label, i);
		m_to->addItem(label, i);
	}
	m_from->setCurrentIndex(std::max(0, m_from->findData(selection.surface)));
	m_to->setCurrentIndex(std::max(0, m_to->findData(selection.surface)));
	m_scope = combo("transferScope", Text::tr("&Destination scope"),
					Text::tr("Use the whole destination surface or the component selection captured when opening this dialog."));
	m_scope->addItem(Text::tr("Whole surface"), 0);
	if (!selection.vertices.isEmpty() || !selection.faces.isEmpty() || !selection.edges.isEmpty())
	{
		m_scope->addItem(Text::tr("Selected components"), 1);
		m_scope->setCurrentIndex(1);
	}
	m_method = combo("transferMethod", Text::tr("&Correspondence"),
					 Text::tr("Triangle sampling interpolates weights at the nearest surface point. Vertex sampling copies the nearest "
							  "vertex. Conflicting equidistant seam weights are refused."));
	m_method->addItem(Text::tr("Nearest triangle"), int(ModelWeightTransferMethod::Triangle));
	m_method->addItem(Text::tr("Nearest vertex"), int(ModelWeightTransferMethod::Vertex));
	m_axis = combo("transferMirror", Text::tr("&Mirror axis"),
				   Text::tr("Reflect destination queries through this bind-space plane; geometry and joint transforms stay in place."));
	m_axis->addItem(Text::tr("No mirror"), -1);
	for (int i = 0; i < 3; ++i)
		m_axis->addItem(QString("XYZ").mid(i, 1), i);
	m_side = combo("transferSide", Text::tr("&Direction"),
				   Text::tr("A one-way mirror uses source primitives wholly on the source side and leaves centre vertices unchanged. Both "
							"directions use the original weights."));
	m_side->addItem(Text::tr("Both directions / all vertices"), int(ModelWeightTransferSide::All));
	m_side->addItem(Text::tr("Positive to negative"), int(ModelWeightTransferSide::PositiveToNegative));
	m_side->addItem(Text::tr("Negative to positive"), int(ModelWeightTransferSide::NegativeToPositive));
	const auto number = [&](const char *id, const QString &name, double min, double max, double value, const QString &description)
	{
		auto *w = new QDoubleSpinBox;
		w->setObjectName(id);
		w->setDecimals(6);
		w->setRange(min, max);
		w->setValue(value);
		w->setLayoutDirection(Qt::LeftToRight);
		accessible(w, name, description);
		row(form, name, w);
		return w;
	};
	m_plane =
		number("transferPlane", Text::tr("Plane position"), -1048576, 1048576, 0, Text::tr("Bind-space coordinate of the mirror plane."));
	m_centre = number("transferCentre", Text::tr("Centre tolerance"), 0, 1048576, .0001,
					  Text::tr("One-way mirrors leave destination vertices within this distance of the plane unchanged."));
	m_distance = number("transferDistance", Text::tr("Maximum distance"), 0, 1048576, 1,
						Text::tr("Maximum bind-space distance from the reflected or direct query to its source sample."));
	m_angle = number("transferAngle", Text::tr("Normal angle limit"), 0, 180, 180,
					 Text::tr("Filter source samples by the angle between bind normals. 180 disables filtering; smaller angles distinguish "
							  "hard-normal seams and nearby layers."));
	m_strength =
		number("transferStrength", Text::tr("Strength"), .000001, 1, 1,
			   Text::tr("Blend the transferred distribution with existing destination weights while preserving locked weights exactly."));
	m_unmatched = combo(
		"transferUnmatched", Text::tr("Unmatched vertices"),
		Text::tr("Refuse the whole edit or explicitly retain unmatched bound vertices. Ambiguous seam matches always refuse the edit."));
	m_unmatched->addItem(Text::tr("Refuse transfer"), 0);
	m_unmatched->addItem(Text::tr("Keep existing weights"), 1);
	auto *mappingPage = new QWidget;
	auto *mappingLayout = new QVBoxLayout(mappingPage);
	auto *pairForm = new QFormLayout;
	pairForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_left = new QLineEdit(".L");
	m_right = new QLineEdit(".R");
	m_left->setObjectName("transferLeftSuffix");
	m_right->setObjectName("transferRightSuffix");
	accessible(m_left, Text::tr("First joint suffix"), Text::tr("Exact suffix for the first side of a joint pair."));
	accessible(m_right, Text::tr("Second joint suffix"), Text::tr("Exact suffix for the other side. Only existing pairs are mapped."));
	row(pairForm, Text::tr("First suffix"), m_left);
	row(pairForm, Text::tr("Second suffix"), m_right);
	mappingLayout->addLayout(pairForm);
	auto *pair = new QPushButton(Text::tr("Pair joint suffixes"));
	pair->setObjectName("transferPairJoints");
	accessible(pair, pair->text(),
			   Text::tr("Set both directions for existing joint pairs with these suffixes. Review the complete mapping below."));
	mappingLayout->addWidget(pair);
	m_mapping = new QTableWidget(mesh.skeleton.joints.size(), 3);
	m_mapping->setObjectName("transferJointMap");
	m_mapping->setHorizontalHeaderLabels({Text::tr("Source joint"), Text::tr("Destination joint"), Text::tr("Lock destination")});
	accessible(m_mapping, Text::tr("Joint mapping and locks"),
			   Text::tr("Edit exact destination joint names. Unchanged names map to themselves. The checkbox locks the mapped destination "
						"joint in that row."));
	m_mapping->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_mapping->setSelectionBehavior(QAbstractItemView::SelectRows);
	for (int i = 0; i < mesh.skeleton.joints.size(); ++i)
	{
		auto *a = new QTableWidgetItem(mesh.skeleton.joints[i].name);
		a->setFlags(a->flags() & ~Qt::ItemIsEditable);
		m_mapping->setItem(i, 0, a);
		m_mapping->setItem(i, 1, new QTableWidgetItem(a->text()));
		auto *lock = new QTableWidgetItem;
		lock->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		lock->setCheckState(Qt::Unchecked);
		lock->setData(Qt::AccessibleTextRole, Text::tr("Lock destination joint %1").arg(a->text()));
		m_mapping->setItem(i, 2, lock);
	}
	mappingLayout->addWidget(m_mapping);
	m_tabs->addTab(mappingPage, Text::tr("&Joint mapping"));
	connect(pair, &QPushButton::clicked, this,
			[this]
			{
				const auto a = m_left->text(), b = m_right->text();
				if (a.isEmpty() || b.isEmpty() || a.endsWith(b) || b.endsWith(a))
				{
					m_status->setText(Text::tr("Choose two nonempty suffixes that do not overlap."));
					return;
				}
				QSignalBlocker blocker(m_mapping);
				for (int i = 0; i < m_mapping->rowCount(); ++i)
				{
					const auto name = m_mapping->item(i, 0)->text();
					QString mapped;
					if (name.endsWith(a))
						mapped = name.chopped(a.size()) + b;
					else if (name.endsWith(b))
						mapped = name.chopped(b.size()) + a;
					if (mapped.isEmpty())
						continue;
					for (const auto &j : m_source.skeleton.joints)
						if (j.name == mapped)
							m_mapping->item(i, 1)->setText(mapped);
				}
				changed();
			});
	auto *previewPage = new QWidget;
	auto *previewLayout = new QVBoxLayout(previewPage);
	m_frame = new QComboBox;
	m_frame->setObjectName("transferFrame");
	accessible(m_frame, Text::tr("Preview frame"), Text::tr("Inspect the bind frame and retained animation samples before applying."));
	for (const auto &f : mesh.frames)
		m_frame->addItem(f.name);
	m_frame->setCurrentIndex(std::clamp(frame, 0, int(mesh.frames.size()) - 1));
	previewLayout->addWidget(m_frame);
	m_view = new QComboBox;
	m_view->setObjectName("transferView");
	m_view->addItems({Text::tr("Reviewed result"), Text::tr("Original model")});
	accessible(m_view, Text::tr("Preview source"),
			   Text::tr("Compare the reviewed result with the original model at the same frame and camera."));
	previewLayout->addWidget(m_view);
	m_preview = new ModelViewport;
	m_preview->setObjectName("transferPreview");
	m_preview->setMinimumSize(240, 220);
	m_preview->setMesh(mesh, true);
	m_preview->setFrame(m_frame->currentIndex());
	previewLayout->addWidget(m_preview, 1);
	m_tabs->addTab(previewPage, Text::tr("&Preview"));
	auto *weightPage = new QWidget;
	auto *weightLayout = new QVBoxLayout(weightPage);
	m_weights = new QTableView;
	m_weights->setObjectName("transferWeights");
	m_table = new ModelTransferTable(this);
	m_weights->setModel(m_table);
	m_weights->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_weights->setSelectionBehavior(QAbstractItemView::SelectRows);
	accessible(m_weights, Text::tr("Before and after weights"),
			   Text::tr("Inspect exact joint weights for every destination vertex. The detail field provides the selected row in full."));
	weightLayout->addWidget(m_weights, 1);
	auto *detail = new QPlainTextEdit;
	detail->setReadOnly(true);
	detail->setMaximumHeight(180);
	detail->setObjectName("transferWeightDetail");
	accessible(detail, Text::tr("Selected vertex weights"),
			   Text::tr("Full source and resulting weights for the selected destination row."));
	weightLayout->addWidget(detail);
	m_tabs->addTab(weightPage, Text::tr("&Weights"));
	connect(m_weights->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
			[this, detail](const QModelIndex &index)
			{
				detail->setPlainText(index.isValid() ? Text::tr("Vertex %1\nBefore: %2\nAfter: %3")
														   .arg(index.row())
														   .arg(m_table->data(m_table->index(index.row(), 1), Qt::DisplayRole).toString(),
																m_table->data(m_table->index(index.row(), 2), Qt::DisplayRole).toString())
													 : QString());
			});
	connect(m_table, &QAbstractItemModel::modelReset, detail, &QPlainTextEdit::clear);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("transferStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, Text::tr("Transfer status"), Text::tr("Review progress, validation failures and the number of changed vertices."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("transferApply");
	m_review = buttons->addButton(Text::tr("Review"), QDialogButtonBox::ActionRole);
	m_review->setObjectName("transferReview");
	accessible(m_review, Text::tr("Review transfer"),
			   Text::tr("Prepare the candidate in a cancellable worker. The source remains unchanged until Apply."));
	layout->addWidget(buttons);
	connect(m_review, &QPushButton::clicked, this, &ModelWeightTransferDialog::review);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (m_ready && !m_busy)
					accept();
			});
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelWeightTransferDialog::reject);
	for (auto *w : {m_from, m_to, m_scope, m_method, m_axis, m_side, m_unmatched})
		connect(w, &QComboBox::currentIndexChanged, this, [this] { changed(); });
	for (auto *w : {m_plane, m_centre, m_distance, m_angle, m_strength})
		connect(w, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
	connect(m_mapping, &QTableWidget::itemChanged, this, [this] { changed(); });
	connect(m_frame, &QComboBox::currentIndexChanged, this, [this] { m_preview->setFrame(m_frame->currentIndex()); });
	connect(m_view, &QComboBox::currentIndexChanged, this, [this] { showPreview(); });
	fitDialogToScreen(this, QSize(900, 780));
	changed();
}
ModelEdit ModelWeightTransferDialog::request() const
{
	ModelEdit result;
	result.kind = ModelEditKind::TransferSkinWeights;
	result.selection.surface = m_to->currentData().toInt();
	if (m_scope->currentData().toInt() == 1)
		result.selection = m_selection;
	auto &o = result.weightTransfer;
	o.sourceSurface = m_from->currentIndex() < 0 ? -1 : m_from->currentData().toInt();
	o.method = ModelWeightTransferMethod(m_method->currentData().toInt());
	o.mirrorAxis = m_axis->currentData().toInt();
	if (o.mirrorAxis >= 0)
	{
		o.side = ModelWeightTransferSide(m_side->currentData().toInt());
		o.plane = m_plane->value();
		o.centreTolerance = m_centre->value();
	}
	o.maximumDistance = m_distance->value();
	o.normalAngle = m_angle->value();
	o.strength = m_strength->value();
	o.skipUnmatched = m_unmatched->currentData().toInt() != 0;
	for (int i = 0; i < m_mapping->rowCount(); ++i)
	{
		const auto name = m_mapping->item(i, 0)->text(), mapped = m_mapping->item(i, 1)->text();
		if (mapped != name)
			o.joints[name] = mapped;
		if (m_mapping->item(i, 2)->checkState() == Qt::Checked && !o.lockedJoints.contains(mapped))
			o.lockedJoints << mapped;
	}
	return result;
}
void ModelWeightTransferDialog::changed()
{
	if (m_busy)
		return;
	{
		QSignalBlocker blocker(m_mapping);
		for (int i = 0; i < m_mapping->rowCount(); ++i)
			m_mapping->item(i, 2)->setData(Qt::AccessibleTextRole,
										   Text::tr("Lock destination joint %1").arg(m_mapping->item(i, 1)->text()));
	}
	m_ready = false;
	m_apply->setEnabled(false);
	m_result = m_source;
	m_table->setMeshes({}, {}, 0);
	if (m_scope->currentData().toInt() == 1)
	{
		QSignalBlocker blocker(m_to);
		m_to->setCurrentIndex(m_to->findData(m_selection.surface));
	}
	m_to->setEnabled(m_scope->currentData().toInt() == 0);
	const bool mirror = m_axis->currentData().toInt() >= 0;
	m_side->setEnabled(mirror);
	m_plane->setEnabled(mirror);
	m_centre->setEnabled(mirror);
	m_status->setText(Text::tr("Choose Review to inspect the transfer and joint mapping."));
	showPreview();
}
void ModelWeightTransferDialog::showPreview()
{
	m_preview->setMesh(m_ready && m_view->currentIndex() == 0 ? m_result : m_source, true);
	m_preview->setFrame(m_frame->currentIndex());
}
void ModelWeightTransferDialog::review()
{
	if (m_busy || m_closeAfter)
		return;
	const auto operation = request();
	const auto source = m_source;
	ModelMesh candidate;
	QString error;
	int changedCount = 0;
	m_ready = false;
	m_busy = true;
	m_tabs->setEnabled(false);
	m_apply->setEnabled(false);
	m_review->setEnabled(false);
	m_status->setText(Text::tr("Reviewing weight transfer…"));
	const bool ok = runModelTask(
		this, Text::tr("Review Weight Transfer"),
		[source, operation, &candidate, &changedCount](QString *failure, const ModelWorkControl &control)
		{
			candidate = source;
			if (!applyModelEdit(&candidate, operation, nullptr, failure, control))
				return false;
			ModelWorkProgress work(control, ModelWorkPhase::Validating, failure);
			const int surface = operation.selection.surface;
			for (int v = 0; v < candidate.surfaces[surface].vertexCount; ++v)
			{
				if (!work.step())
					return false;
				if (weights(source.surfaces[surface], v, source.skeleton) != weights(candidate.surfaces[surface], v, candidate.skeleton))
					++changedCount;
			}
			return work.check();
		},
		&error, false, &m_cancel);
	m_busy = false;
	m_tabs->setEnabled(true);
	m_review->setEnabled(true);
	if (m_closeAfter)
	{
		QDialog::reject();
		return;
	}
	if (!ok)
	{
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_result = std::move(candidate);
	m_reviewed = operation;
	m_ready = true;
	m_apply->setEnabled(true);
	const int surface = operation.selection.surface;
	m_table->setMeshes(m_source, m_result, surface);
	m_status->setText(Text::tr("Destination vertices: %L1 · changed: %L2. Source unchanged; ready to apply.")
						  .arg(m_result.surfaces[surface].vertexCount)
						  .arg(changedCount),
					  LiveStatus::Result);
	showPreview();
}
void ModelWeightTransferDialog::reject()
{
	m_closeAfter = true;
	if (m_busy)
	{
		if (m_cancel)
			m_cancel();
		return;
	}
	QDialog::reject();
}
} // namespace vibestudio
