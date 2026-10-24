#include "app/model_weight_paint_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "app/model_document_work.h"
#include "app/model_vertex_overlay.h"
#include "app/model_viewport.h"
#include "core/model_skeleton.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelWeightPaintDialog)
};
void accessible(QWidget *w, const QString &name, const QString &description = {})
{
	w->setAccessibleName(name);
	w->setAccessibleDescription(description);
	if (!description.isEmpty())
		w->setToolTip(description);
}
bool finite(QPointF p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}
} // namespace
class ModelWeightTable final : public QAbstractTableModel
{
  public:
	using QAbstractTableModel::QAbstractTableModel;
	int surfaceIndex() const
	{
		return m_surfaceIndex;
	}
	void setSurface(const ModelSurface &surface, int joint, int index)
	{
		beginResetModel();
		m_surface = surface;
		m_joint = joint;
		m_surfaceIndex = index;
		endResetModel();
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : m_surface.vertexCount;
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 4;
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
			return {};
		return QStringList{Text::tr("Vertex"), Text::tr("Weight"), Text::tr("Count"), Text::tr("Total")}.value(section);
	}
	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole))
			return {};
		const int v = index.row();
		if (index.column() == 0)
			return v;
		if (index.column() == 1)
			return localeNumber(modelVertexJointWeight(m_surface, v, m_joint), 8);
		if (m_surface.skinning.isEmpty())
			return index.column() == 2 ? QVariant(0) : QVariant(QStringLiteral("0"));
		if (index.column() == 2)
			return m_surface.skinning.count[v];
		double sum = 0;
		for (int i = m_surface.skinning.first[v]; i < m_surface.skinning.first[v] + m_surface.skinning.count[v]; ++i)
			sum += m_surface.skinning.influences[i].weight;
		return localeNumber(sum, 8);
	}

  private:
	ModelSurface m_surface;
	int m_joint = -1;
	int m_surfaceIndex = -1;
};
ModelWeightPaintDialog::ModelWeightPaintDialog(const ModelMesh &mesh, const ModelSelection &selection, const QString &joint, int frame,
											   QWidget *parent)
	: QDialog(parent), m_originalSelection(selection)
{
	setWindowTitle(Text::tr("Paint Skin Weights"));
	setObjectName("modelWeightPaintDialog");
	m_busy = true;
	auto *layout = new QVBoxLayout(this);
	m_content = new QSplitter(Qt::Horizontal, this);
	auto *splitter = static_cast<QSplitter *>(m_content);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setMinimumWidth(230);
	accessible(scroll, Text::tr("Weight brush settings"));
	auto *fields = new QWidget;
	auto *fieldLayout = new QVBoxLayout(fields);
	fieldLayout->setContentsMargins(0, 0, 0, 0);
	auto *form = new QFormLayout;
	fieldLayout->addLayout(form);
	fieldLayout->addStretch(1);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setFormAlignment(Qt::AlignTop);
	form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
	const auto combo = [&](const char *name, const QString &label)
	{
		auto *w = new QComboBox;
		w->setObjectName(name);
		w->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		w->setMinimumContentsLength(8);
		accessible(w, label);
		form->addRow(label, w);
		return w;
	};
	m_surface = combo("weightPaintSurface", Text::tr("Surface"));
	for (int i = 0; i < mesh.surfaces.size(); ++i)
		m_surface->addItem(QString("%1 · %2").arg(i).arg(mesh.surfaces[i].name), i);
	m_surface->setCurrentIndex(std::clamp(selection.surface, 0, std::max(0, int(mesh.surfaces.size()) - 1)));
	m_joint = combo("weightPaintJoint", Text::tr("Painted joint"));
	for (const auto &j : mesh.skeleton.joints)
		m_joint->addItem(j.name, j.name);
	if (const int index = m_joint->findData(joint); index >= 0)
		m_joint->setCurrentIndex(index);
	m_mode = combo("weightPaintMode", Text::tr("Brush mode"));
	m_mode->addItems({Text::tr("Replace"), Text::tr("Add"), Text::tr("Subtract"), Text::tr("Smooth")});
	const auto number = [&](const char *name, const QString &label, double value, double minimum)
	{
		auto *w = new QDoubleSpinBox;
		w->setObjectName(name);
		w->setDecimals(3);
		w->setRange(minimum, 1);
		w->setSingleStep(.05);
		w->setValue(value);
		accessible(w, label);
		form->addRow(label, w);
		return w;
	};
	m_value = number("weightPaintValue", Text::tr("Weight / amount"), 1, 0);
	m_strength = number("weightPaintStrength", Text::tr("Strength"), .5, .001);
	m_radius = new QSpinBox;
	m_radius->setObjectName("weightPaintRadius");
	m_radius->setRange(2, 512);
	m_radius->setValue(48);
	m_radius->setSuffix(Text::tr(" px"));
	accessible(m_radius, Text::tr("Brush radius"), Text::tr("Radius in logical pixels, independent of display scaling."));
	form->addRow(Text::tr("Radius"), m_radius);
	m_falloff = combo("weightPaintFalloff", Text::tr("Falloff"));
	m_falloff->addItems({Text::tr("Smooth"), Text::tr("Linear"), Text::tr("Constant")});
	const auto check = [&](const char *name, const QString &label, const QString &description)
	{
		auto *row = new QWidget;
		auto *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		auto *w = new QCheckBox;
		w->setObjectName(name);
		accessible(w, label, description);
		auto *wrapped = new QLabel(label);
		wrapped->setWordWrap(true);
		wrapped->setAlignment(Qt::AlignLeading | Qt::AlignTop);
		wrapped->setTextFormat(Qt::PlainText);
		wrapped->setBuddy(w);
		rowLayout->addWidget(w, 0, Qt::AlignTop);
		rowLayout->addWidget(wrapped, 1);
		form->addRow(row);
		return w;
	};
	m_paint = check("weightPaintEnabled", Text::tr("Paint with primary drag"),
					Text::tr("Drag with the primary mouse button to stage one stroke. Other navigation gestures remain available between "
							 "strokes. Escape cancels a stroke."));
	m_paint->setChecked(true);
	m_through = check("weightPaintThrough", Text::tr("Paint through mesh"),
					  Text::tr("Include occluded vertices inside the brush. Otherwise use depth-tested visible vertices."));
	m_selectionOnly = check("weightPaintSelectionOnly", Text::tr("Limit brush to selected rows"),
							Text::tr("Only vertices selected in the weight table can receive paint. An empty mask paints nothing."));
	m_remainder = combo("weightPaintRemainder", Text::tr("Remainder joint"));
	m_remainder->addItem(Text::tr("Require existing influences"), QString());
	for (const auto &j : mesh.skeleton.joints)
		m_remainder->addItem(j.name, j.name);
	accessible(m_remainder, Text::tr("Remainder joint"),
			   Text::tr("Explicit destination when reducing a vertex's sole unlocked influence. Existing other influences always keep "
						"their relative proportions."));
	m_locks = new QListWidget;
	m_locks->setObjectName("weightPaintLocks");
	m_locks->setMaximumHeight(130);
	accessible(m_locks, Text::tr("Locked joint weights"),
			   Text::tr("Checked joint weights stay exact. The painted and remainder joints must be unlocked."));
	for (const auto &j : mesh.skeleton.joints)
	{
		auto *item = new QListWidgetItem(j.name, m_locks);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
	}
	form->addRow(Text::tr("Lock weights"), m_locks);
	m_selected = new QPushButton(Text::tr("Paint rows"));
	m_selected->setObjectName("weightPaintSelected");
	accessible(m_selected, Text::tr("Paint selected rows"),
			   Text::tr("Apply the current brush numerically at full coverage to the selected vertex rows, without pointer input."));
	form->addRow(m_selected);
	for (int row = 0; row < form->rowCount(); ++row)
		if (auto *item = form->itemAt(row, QFormLayout::LabelRole))
			if (auto *label = qobject_cast<QLabel *>(item->widget()))
			{
				label->setWordWrap(true);
				label->setTextFormat(Qt::PlainText);
			}
	scroll->setWidget(fields);
	splitter->addWidget(scroll);
	auto *right = new QWidget;
	auto *rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	m_frame = new QComboBox;
	m_frame->setObjectName("weightPaintFrame");
	accessible(m_frame, Text::tr("Preview animation frame"));
	for (int i = 0; i < mesh.frames.size(); ++i)
		m_frame->addItem(QString("%1 · %2").arg(i).arg(mesh.frames[i].name), i);
	m_frame->setCurrentIndex(std::clamp(frame, 0, std::max(0, int(mesh.frames.size()) - 1)));
	rightLayout->addWidget(m_frame);
	m_preview = new ModelViewport;
	m_preview->setObjectName("weightPaintPreview");
	m_preview->setMinimumSize(280, 220);
	m_preview->setFocusPolicy(Qt::StrongFocus);
	accessible(m_preview, Text::tr("Weight paint preview"),
			   Text::tr("Joint weights appear as coloured vertex rings; ring fill and the table also show their magnitude. Pending "
						"coverage is outlined. Poses update after each reviewed stroke."));
	m_preview->setBackfaceCulling(false);
	m_preview->setVertexPicking(true);
	m_preview->setShowJoints(true);
	m_preview->setXrayJoints(false);
	m_preview->setOverlayPainter([this](QPainter &p) { paintOverlay(p); });
	m_preview->installEventFilter(this);
	rightLayout->addWidget(m_preview, 3);
	m_probe = new QLabel(Text::tr("Weight 0: empty ring · Weight 1: full ring"));
	m_probe->setWordWrap(true);
	m_probe->setTextFormat(Qt::PlainText);
	m_probe->setObjectName("weightPaintProbe");
	accessible(m_probe, Text::tr("Weight probe"));
	rightLayout->addWidget(m_probe);
	m_table = new QTableView;
	m_table->setObjectName("weightPaintTable");
	accessible(m_table, Text::tr("Exact vertex weights"),
			   Text::tr("Select rows with the keyboard or pointer, then use Paint rows. Values describe the currently staged model."));
	m_tableModel = new ModelWeightTable(this);
	m_table->setModel(m_tableModel);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->verticalHeader()->hide();
	m_table->setMinimumHeight(110);
	rightLayout->addWidget(m_table, 1);
	splitter->addWidget(right);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({300, 620});
	layout->addWidget(splitter, 1);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("weightPaintStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	accessible(m_status, Text::tr("Weight painting status"));
	layout->addWidget(m_status);
	auto *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
	m_undo = box->addButton(Text::tr("Undo stroke"), QDialogButtonBox::ActionRole);
	m_undo->setObjectName("weightPaintUndo");
	m_redo = box->addButton(Text::tr("Redo stroke"), QDialogButtonBox::ActionRole);
	m_redo->setObjectName("weightPaintRedo");
	m_apply = box->addButton(Text::tr("Apply staged strokes"), QDialogButtonBox::ActionRole);
	m_apply->setObjectName("weightPaintApply");
	for (auto *b : {m_undo, m_redo, m_apply})
		accessible(b, b->text());
	accessible(m_apply, m_apply->text(),
			   Text::tr("Commit every reviewed stroke as one Mesh Editor undo step. Closing with Cancel discards all staged strokes."));
	layout->addWidget(box);
	connect(box, &QDialogButtonBox::rejected, this, &ModelWeightPaintDialog::reject);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (!m_busy && !strokeActive() && !m_strokes.isEmpty())
					accept();
			});
	connect(m_undo, &QPushButton::clicked, this, &ModelWeightPaintDialog::undoStroke);
	connect(m_redo, &QPushButton::clicked, this, &ModelWeightPaintDialog::redoStroke);
	connect(m_selected, &QPushButton::clicked, this, [this] { paintSelected(); });
	for (auto *field : {m_surface, m_joint, m_mode, m_falloff, m_remainder})
		connect(field, &QComboBox::currentIndexChanged, this, [this] { changed(); });
	for (auto *field : {m_value, m_strength})
		connect(field, &QDoubleSpinBox::valueChanged, this, [this] { finishStroke(false); });
	connect(m_radius, &QSpinBox::valueChanged, this,
			[this]
			{
				finishStroke(false);
				m_preview->update();
			});
	for (auto *field : {m_paint, m_through, m_selectionOnly})
		connect(field, &QCheckBox::toggled, this,
				[this]
				{
					finishStroke(false);
					m_preview->update();
				});
	connect(m_locks, &QListWidget::itemChanged, this, [this] { finishStroke(false); });
	connect(m_frame, &QComboBox::currentIndexChanged, this,
			[this](int f)
			{
				finishStroke(false);
				m_preview->setFrame(f);
			});
	connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
			[this]
			{
				if (!m_refreshing)
					finishStroke(false);
				buttons();
			});
	connect(m_preview, &ModelViewport::viewChanged, this, [this] { finishStroke(false); });
	m_content->setEnabled(false);
	m_status->setText(Text::tr("Preparing the weight painting document…"));
	buttons();
	QTimer::singleShot(0, this,
					   [this, mesh, selection]
					   {
						   if (m_closeAfter)
						   {
							   m_busy = false;
							   QDialog::reject();
							   return;
						   }
						   QString error;
						   const bool valid = runModelDocumentWork(
							   this, Text::tr("Prepare Weight Painting"), &m_document,
							   [mesh](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
							   { return candidate.setMesh(mesh, failure, control); }, &error, false, &m_cancel);
						   m_busy = false;
						   m_content->setEnabled(valid);
						   changed(true);
						   QSet<int> selected = selection.vertices;
						   if (valid && selection.surface >= 0 && selection.surface < mesh.surfaces.size())
						   {
							   for (int face : selection.faces)
								   if (face >= 0 && face < mesh.surfaces[selection.surface].triangles.size())
								   {
									   const auto t = mesh.surfaces[selection.surface].triangles[face];
									   selected.unite({t.a, t.b, t.c});
								   }
							   for (auto edge : selection.edges)
								   selected.unite({edge.first, edge.second});
							   for (int v : selected)
								   if (v >= 0 && v < m_tableModel->rowCount())
									   m_table->selectionModel()->select(m_tableModel->index(v, 0),
																		 QItemSelectionModel::Select | QItemSelectionModel::Rows);
						   }
						   m_selectionOnly->setChecked(!selected.isEmpty());
						   m_status->setText(valid ? Text::tr("Ready. Strokes are reviewed locally; Apply commits the staged result.")
												   : error);
						   if (!valid)
							   m_content->setEnabled(false);
						   buttons();
						   if (m_closeAfter)
							   QDialog::reject();
					   });
	fitDialogToScreen(this, QSize(1000, 820));
}
ModelWeightStroke ModelWeightPaintDialog::settings() const
{
	ModelWeightStroke s;
	s.surface = m_surface->currentIndex();
	s.joint = m_joint->currentData().toString();
	s.mode = ModelWeightPaintMode(m_mode->currentIndex());
	s.value = s.mode == ModelWeightPaintMode::Smooth ? 1 : m_value->value();
	s.strength = m_strength->value();
	s.remainderJoint = m_remainder->currentData().toString();
	for (int i = 0; i < m_locks->count(); ++i)
		if (m_locks->item(i)->checkState() == Qt::Checked)
			s.lockedJoints.append(m_locks->item(i)->text());
	return s;
}
ModelEdit ModelWeightPaintDialog::edit() const
{
	ModelEdit result;
	result.kind = ModelEditKind::PaintSkinWeights;
	result.selection.surface = std::max(0, m_originalSelection.surface);
	result.weightStrokes = m_strokes;
	return result;
}
QSet<int> ModelWeightPaintDialog::selectedVertices() const
{
	QSet<int> selected;
	for (const auto &row : m_table->selectionModel()->selectedRows())
		selected.insert(row.row());
	return selected;
}
void ModelWeightPaintDialog::buttons()
{
	const bool idle = !m_busy && !strokeActive();
	m_apply->setEnabled(idle && !m_strokes.isEmpty());
	m_undo->setEnabled(idle && m_document.canUndo());
	m_redo->setEnabled(idle && m_document.canRedo());
	m_selected->setEnabled(idle && !selectedVertices().isEmpty());
}
void ModelWeightPaintDialog::changed(bool mesh)
{
	if (m_busy)
		return;
	finishStroke(false);
	const int surface = m_surface->currentIndex(), joint = m_joint->currentIndex();
	const auto &model = m_document.mesh();
	if (surface < 0 || surface >= model.surfaces.size())
		return;
	m_value->setEnabled(m_mode->currentIndex() != int(ModelWeightPaintMode::Smooth));
	m_refreshing = true;
	const auto selected = m_tableModel->surfaceIndex() == surface ? selectedVertices() : QSet<int>{};
	m_tableModel->setSurface(model.surfaces[surface], joint, surface);
	for (int v : selected)
		m_table->selectionModel()->select(m_tableModel->index(v, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	m_refreshing = false;
	if (mesh)
	{
		m_preview->setMesh(model, true);
		m_preview->setFrame(m_frame->currentIndex());
	}
	m_preview->setEditSelection(surface, {});
	m_preview->setXrayVertices(false);
	m_preview->update();
	buttons();
}
bool ModelWeightPaintDialog::beginStroke(QPointF point)
{
	if (m_busy || strokeActive() || !m_paint->isChecked() || (m_preview->isLooking() || m_preview->isPointerDriving()) || !finite(point) ||
		!m_preview->rect().contains(point.toPoint()))
		return false;
	if (m_preview->isRendering())
	{
		m_status->setText(Text::tr("The preview is updating. Start painting when it is ready."));
		return false;
	}
	const auto projection = m_preview->editVertexProjection();
	const auto s = settings();
	if (!projection || projection->surface != s.surface || projection->logicalSize != m_preview->size() || s.joint.isEmpty())
	{
		m_status->setText(Text::tr("Choose a bound surface and joint, then wait for the preview."));
		return false;
	}
	if (m_document.mesh().surfaces[s.surface].skinning.isEmpty())
	{
		m_status->setText(Text::tr("Bind this surface before painting weights."));
		return false;
	}
	m_mask = m_selectionOnly->isChecked() ? selectedVertices() : QSet<int>{};
	m_projection = projection;
	m_pending = s;
	m_coverage.clear();
	m_last = point;
	m_pointer = point;
	m_hover = true;
	extendStroke(point);
	buttons();
	return true;
}
bool ModelWeightPaintDialog::extendStroke(QPointF point)
{
	if (!strokeActive() || !finite(point))
		return false;
	const auto delta = point - m_last;
	const double length = QPointF::dotProduct(delta, delta), radius = m_radius->value();
	for (int v = 0; v < m_projection->positions.size(); ++v)
	{
		if ((!m_through->isChecked() && !m_projection->visible.testBit(v)) || (m_selectionOnly->isChecked() && !m_mask.contains(v)))
			continue;
		const auto p = m_projection->positions[v];
		if (!finite(p))
			continue;
		const double t = length > 0 ? std::clamp(QPointF::dotProduct(p - m_last, delta) / length, 0., 1.) : 0;
		const auto d = p - (m_last + delta * t);
		const double distance = std::sqrt(QPointF::dotProduct(d, d));
		if (distance > radius)
			continue;
		double amount = std::clamp(1 - distance / radius, 0., 1.);
		if (m_falloff->currentIndex() == 0)
			amount = amount * amount * (3 - 2 * amount);
		else if (m_falloff->currentIndex() == 2)
			amount = 1;
		if (amount > 0)
			m_coverage[v] = std::max(m_coverage.value(v), amount);
	}
	m_last = point;
	m_pointer = point;
	m_status->setText(Text::tr("Pending stroke. Vertices covered: %1. Release to review; Escape cancels.").arg(m_coverage.size()));
	m_preview->update();
	return true;
}
bool ModelWeightPaintDialog::finishStroke(bool commit)
{
	if (!strokeActive())
		return false;
	auto s = m_pending;
	for (auto it = m_coverage.cbegin(); it != m_coverage.cend(); ++it)
		s.samples.append({it.key(), it.value()});
	m_projection.reset();
	m_coverage.clear();
	m_preview->update();
	buttons();
	if (!commit)
	{
		m_status->setText(Text::tr("Stroke cancelled. Reviewed strokes are unchanged."));
		return false;
	}
	if (s.samples.isEmpty())
	{
		m_status->setText(Text::tr("The brush covered no eligible vertices."));
		return false;
	}
	return stage(s);
}
bool ModelWeightPaintDialog::paintSelected()
{
	if (m_busy || strokeActive())
		return false;
	auto s = settings();
	auto selected = selectedVertices().values();
	std::sort(selected.begin(), selected.end());
	for (int v : selected)
		s.samples.append({v, 1});
	return stage(s);
}
bool ModelWeightPaintDialog::stage(const ModelWeightStroke &s)
{
	if (m_busy)
		return false;
	qint64 count = s.samples.size();
	for (const auto &old : m_strokes)
		count += old.samples.size();
	if (m_strokes.size() >= modelWeightMaxStrokes || count > modelWeightMaxSamples)
	{
		m_status->setText(Text::tr("The staged stroke limit is reached. Apply this batch before continuing."));
		return false;
	}
	ModelEdit operation;
	operation.kind = ModelEditKind::PaintSkinWeights;
	operation.weightStrokes = {s};
	m_busy = true;
	m_content->setEnabled(false);
	buttons();
	m_status->setText(Text::tr("Validating weights and updating skeletal poses…"));
	QString error;
	const auto before = m_document.revisionFingerprint();
	const bool ok = runModelDocumentWork(
		this, Text::tr("Review Weight Stroke"), &m_document,
		[operation](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
		{ return candidate.edit(operation, failure, control); }, &error, false, &m_cancel);
	m_busy = false;
	m_content->setEnabled(true);
	if (ok && before != m_document.revisionFingerprint())
	{
		m_strokes.append(s);
		m_redoStrokes.clear();
		changed(true);
		m_status->setText(
			Text::tr("Reviewed strokes: %1. Inspect animation frames, undo a stroke, or apply the batch.").arg(m_strokes.size()));
	}
	else
		m_status->setText(ok ? Text::tr("This stroke leaves the weights unchanged.") : error);
	buttons();
	if (m_closeAfter)
		QDialog::reject();
	return ok;
}
void ModelWeightPaintDialog::undoStroke()
{
	if (m_busy || strokeActive() || !m_document.canUndo() || m_strokes.isEmpty())
		return;
	if (m_document.undo())
	{
		m_redoStrokes.append(m_strokes.takeLast());
		changed(true);
		m_status->setText(Text::tr("Stroke undone. Staged strokes: %1.").arg(m_strokes.size()));
	}
}
void ModelWeightPaintDialog::redoStroke()
{
	if (m_busy || strokeActive() || !m_document.canRedo() || m_redoStrokes.isEmpty())
		return;
	if (m_document.redo())
	{
		m_strokes.append(m_redoStrokes.takeLast());
		changed(true);
		m_status->setText(Text::tr("Stroke restored. Staged strokes: %1.").arg(m_strokes.size()));
	}
}
void ModelWeightPaintDialog::reject()
{
	if (strokeActive())
	{
		finishStroke(false);
		return;
	}
	if (m_busy)
	{
		m_closeAfter = true;
		if (m_cancel)
			m_cancel();
		return;
	}
	QDialog::reject();
}
void ModelWeightPaintDialog::probe(QPointF point)
{
	const auto projection = m_preview->editVertexProjection();
	if (!projection)
		return;
	const int v = pickModelVertex(*projection, point, 10, m_through->isChecked());
	if (v >= 0)
		m_probe->setText(
			Text::tr("Vertex %L1 · %2: %3")
				.arg(v)
				.arg(m_joint->currentText(),
					 localeNumber(modelVertexJointWeight(m_document.mesh().surfaces[projection->surface], v, m_joint->currentIndex()), 8)));
}
void ModelWeightPaintDialog::paintOverlay(QPainter &painter)
{
	const auto projection = m_projection ? m_projection : m_preview->editVertexProjection();
	if (!projection || projection->surface != m_surface->currentIndex() || m_document.mesh().surfaces.isEmpty())
		return;
	const auto &surface = m_document.mesh().surfaces[projection->surface];
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	for (int v = 0; v < projection->positions.size(); ++v)
	{
		const auto p = projection->positions[v];
		if (!finite(p) || !m_preview->rect().adjusted(-5, -5, 5, 5).contains(p.toPoint()) ||
			(!m_through->isChecked() && !projection->visible.testBit(v)))
			continue;
		const double weight = std::clamp(modelVertexJointWeight(surface, v, m_joint->currentIndex()), 0., 1.);
		const QColor colour = QColor::fromHsvF((1 - weight) * .65, .85, .95);
		painter.setBrush(Qt::black);
		painter.setPen(QPen(Qt::white, 1));
		painter.drawEllipse(p, 4, 4);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(colour, 2.5));
		painter.drawArc(QRectF(p - QPointF(3, 3), QSizeF(6, 6)), 90 * 16, -int(weight * 360 * 16));
		if (m_coverage.contains(v))
		{
			painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
			painter.drawRect(QRectF(p - QPointF(6, 6), QSizeF(12, 12)));
		}
	}
	if (m_hover && m_paint->isChecked())
	{
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(Qt::black, 3));
		painter.drawEllipse(m_pointer, m_radius->value(), m_radius->value());
		painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
		painter.drawEllipse(m_pointer, m_radius->value(), m_radius->value());
	}
	painter.restore();
}
bool ModelWeightPaintDialog::eventFilter(QObject *watched, QEvent *event)
{
	if (watched != m_preview || m_busy)
		return false;
	switch (event->type())
	{
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	{
		const auto *mouse = static_cast<QMouseEvent *>(event);
		if (strokeActive())
		{
			if (mouse->button() == Qt::RightButton)
				finishStroke(false);
			return true;
		}
		if (m_paint->isChecked() && mouse->button() == Qt::LeftButton && mouse->modifiers() == Qt::NoModifier)
		{
			beginStroke(mouse->position());
			return true;
		}
		return false;
	}
	case QEvent::MouseMove:
	{
		const auto *mouse = static_cast<QMouseEvent *>(event);
		m_pointer = mouse->position();
		m_hover = true;
		if (strokeActive())
		{
			extendStroke(m_pointer);
			return true;
		}
		probe(m_pointer);
		m_preview->update();
		return false;
	}
	case QEvent::MouseButtonRelease:
		if (strokeActive())
		{
			const auto *mouse = static_cast<QMouseEvent *>(event);
			if (mouse->button() == Qt::LeftButton)
			{
				extendStroke(mouse->position());
				finishStroke(true);
			}
			return true;
		}
		return false;
	case QEvent::ShortcutOverride:
		if (strokeActive())
		{
			event->accept();
			return true;
		}
		return false;
	case QEvent::KeyPress:
		if (strokeActive())
		{
			if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
				finishStroke(false);
			return true;
		}
		return false;
	case QEvent::KeyRelease:
		return false;
	case QEvent::Wheel:
		return strokeActive();
	case QEvent::FocusOut:
	case QEvent::Hide:
	case QEvent::Resize:
		finishStroke(false);
		return false;
	case QEvent::Leave:
		m_hover = false;
		m_preview->update();
		return false;
	default:
		return false;
	}
}
} // namespace vibestudio
