#include "app/model_ik_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "app/model_document_work.h"
#include "app/model_skeletal_working_set_dialog.h"
#include "app/model_viewport.h"
#include "core/model_joint_tools.h"
#include "core/model_skeleton.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelIkDialog)
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
QString coordinates(ModelVec3 p)
{
	return QStringLiteral("\u2066%1\u2069").arg(localeNumberList({p.x, p.y, p.z}, 9));
}
} // namespace
class ModelIkTable final : public QAbstractTableModel
{
  public:
	explicit ModelIkTable(QObject *parent) : QAbstractTableModel(parent) {}
	QVector<ModelIkSample> samples;
	void setSamples(QVector<ModelIkSample> data)
	{
		beginResetModel();
		samples = std::move(data);
		endResetModel();
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : int(samples.size());
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 5;
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (role != Qt::DisplayRole)
			return {};
		if (orientation == Qt::Vertical)
			return section + 1;
		return QStringList{Text::tr("Sample"), Text::tr("Target status"), Text::tr("End position"), Text::tr("Target distance"),
						   Text::tr("Tolerance")}
			.value(section);
	}
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
	{
		if (!index.isValid() || index.row() >= samples.size() ||
			(role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole))
			return {};
		const auto &s = samples[index.row()];
		switch (index.column())
		{
		case 0:
			return s.frame;
		case 1:
			return s.reached ? Text::tr("Reached") : Text::tr("Nearest reachable");
		case 2:
			return coordinates(s.end);
		case 3:
			return localeNumber(s.error, 9);
		case 4:
			return localeNumber(s.tolerance, 9);
		default:
			return {};
		}
	}
};
ModelIkDialog::ModelIkDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent)
	: QDialog(parent), m_source(mesh), m_result(mesh)
{
	setWindowTitle(Text::tr("Two-Bone Inverse Kinematics"));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	m_tabs = new QTabWidget;
	m_tabs->setObjectName("ikTabs");
	layout->addWidget(m_tabs, 1);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *settings = new QWidget;
	auto *form = new QFormLayout(settings);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(settings);
	m_tabs->addTab(scroll, Text::tr("&Chain and target"));
	m_clip = new QComboBox;
	m_clip->setObjectName("ikClip");
	for (int i = 0; i < mesh.skeleton.clips.size(); ++i)
		addModelSkeletalClipItem(m_clip, mesh, i);
	accessible(m_clip, Text::tr("Skeletal clip"),
			   Text::tr("Choose any retained clip, in the working set or not. Sample indices start at zero within this clip."));
	row(form, Text::tr("&Clip"), m_clip);
	m_end = new QComboBox;
	m_end->setObjectName("ikEndJoint");
	for (int i = 0; i < mesh.skeleton.joints.size(); ++i)
	{
		const int parentIndex = mesh.skeleton.joints[i].parent;
		if (parentIndex >= 0 && mesh.skeleton.joints[parentIndex].parent >= 0)
			m_end->addItem(mesh.skeleton.joints[i].name, i);
	}
	accessible(m_end, Text::tr("End joint"),
			   Text::tr("The end joint, its parent and its grandparent form the chain. The grandparent stays in place."));
	row(form, Text::tr("&End joint"), m_end);
	m_chain = new QLabel;
	m_chain->setWordWrap(true);
	m_chain->setTextFormat(Qt::PlainText);
	m_chain->setObjectName("ikChain");
	accessible(m_chain, Text::tr("Resolved chain"), Text::tr("Root, middle and end joint in hierarchy order."));
	form->addRow(m_chain);
	m_first = new QSpinBox;
	m_first->setObjectName("ikFirst");
	m_last = new QSpinBox;
	m_last->setObjectName("ikLast");
	accessible(m_first, Text::tr("First sample"), Text::tr("First affected clip sample, inclusive."));
	accessible(
		m_last, Text::tr("Last sample"),
		Text::tr("Last affected clip sample, inclusive. Each sample uses its own root and bone lengths with the same model-space target."));
	row(form, Text::tr("&First sample"), m_first);
	row(form, Text::tr("&Last sample"), m_last);
	const QStringList targetLabels{Text::tr("Target X"), Text::tr("Target Y"), Text::tr("Target Z")};
	const QStringList poleLabels{Text::tr("Pole X"), Text::tr("Pole Y"), Text::tr("Pole Z")};
	for (int i = 0; i < 3; ++i)
	{
		m_target[i] = new QDoubleSpinBox;
		m_pole[i] = new QDoubleSpinBox;
		for (auto *w : {m_target[i], m_pole[i]})
		{
			w->setDecimals(6);
			w->setRange(-1000000, 1000000);
			w->setKeyboardTracking(false);
		}
		m_target[i]->setObjectName(QString("ikTarget%1").arg(i));
		m_pole[i]->setObjectName(QString("ikPole%1").arg(i));
		accessible(m_target[i], targetLabels[i], Text::tr("End-joint target in model-space units."));
		accessible(m_pole[i], poleLabels[i],
				   Text::tr("A point in model space that chooses the elbow or knee bend direction. Keep it off the root-to-target line."));
		row(form, targetLabels[i], m_target[i]);
	}
	for (int i = 0; i < 3; ++i)
		row(form, poleLabels[i], m_pole[i]);
	auto *use = new QPushButton(Text::tr("Use First Sample Pose"));
	use->setObjectName("ikUsePose");
	accessible(use, Text::tr("Use first sample pose"),
			   Text::tr("Set the target to the current end position and the pole to the current bend. A straight chain uses a "
						"perpendicular model axis."));
	form->addRow(use);
	m_reach = new QComboBox;
	m_reach->setObjectName("ikReach");
	m_reach->addItems({Text::tr("Require target"), Text::tr("Nearest reachable")});
	accessible(m_reach, Text::tr("Unreachable targets"),
			   Text::tr("Refuse the entire edit if any target is unreachable, or clamp to the fixed-length chain's reach and report the "
						"remaining distance."));
	row(form, Text::tr("&Reach policy"), m_reach);
	m_orientation = new QComboBox;
	m_orientation->setObjectName("ikOrientation");
	m_orientation->addItems({Text::tr("Keep end orientation"), Text::tr("Follow lower bone")});
	accessible(m_orientation, Text::tr("End orientation"),
			   Text::tr("Keep the hand or foot's original model-space orientation, or rotate it with the lower bone. Its descendants "
						"follow this choice."));
	row(form, Text::tr("End &orientation"), m_orientation);
	auto *previewPage = new QWidget;
	auto *previewLayout = new QVBoxLayout(previewPage);
	auto *previewForm = new QFormLayout;
	previewForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	previewLayout->addLayout(previewForm);
	m_sample = new QSpinBox;
	m_sample->setObjectName("ikPreviewSample");
	accessible(m_sample, Text::tr("Preview sample"), Text::tr("Inspect any sample in the selected clip, including unaffected samples."));
	row(previewForm, Text::tr("Preview &sample"), m_sample);
	m_view = new QComboBox;
	m_view->setObjectName("ikPreviewSource");
	m_view->addItems({Text::tr("Reviewed result"), Text::tr("Original model")});
	accessible(m_view, Text::tr("Preview source"), Text::tr("Compare before and after using the same sample and camera."));
	row(previewForm, Text::tr("Preview &source"), m_view);
	m_preview = new ModelViewport;
	m_preview->setObjectName("ikPreview");
	m_preview->setMinimumSize(240, 220);
	m_preview->setShowJoints(true);
	m_preview->setXrayJoints(true);
	previewLayout->addWidget(m_preview, 1);
	m_tabs->addTab(previewPage, Text::tr("&Preview"));
	auto *results = new QWidget;
	auto *resultLayout = new QVBoxLayout(results);
	auto *table = new QTableView;
	table->setObjectName("ikSamples");
	m_table = new ModelIkTable(this);
	table->setModel(m_table);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	accessible(table, Text::tr("Solved samples"),
			   Text::tr("Per-sample end positions, target distance and numerical tolerance. Select a row for full details."));
	resultLayout->addWidget(table, 1);
	auto *detail = new QPlainTextEdit;
	detail->setObjectName("ikSampleDetail");
	detail->setReadOnly(true);
	detail->setMaximumHeight(190);
	accessible(detail, Text::tr("Selected sample result"), Text::tr("Full values for the selected sample."));
	resultLayout->addWidget(detail);
	m_tabs->addTab(results, Text::tr("&Results"));
	connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
			[this, detail](const QModelIndex &index)
			{
				QStringList lines;
				if (index.isValid())
					for (int c = 0; c < 5; ++c)
						lines << Text::tr("%1: %2").arg(m_table->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString(),
														m_table->data(m_table->index(index.row(), c)).toString());
				detail->setPlainText(lines.join('\n'));
			});
	connect(m_table, &QAbstractItemModel::modelReset, detail, &QPlainTextEdit::clear);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("ikStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, Text::tr("Inverse kinematics status"), Text::tr("Review progress, validation failures and target reach summary."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("ikApply");
	m_review = buttons->addButton(Text::tr("Review"), QDialogButtonBox::ActionRole);
	m_review->setObjectName("ikReview");
	accessible(m_review, Text::tr("Review inverse kinematics"),
			   Text::tr("Prepare and validate the pose change in a cancellable worker before applying."));
	ModelJointFrame current;
	if (modelJointFrame(mesh, frame, &current))
	{
		m_clip->setCurrentIndex(std::max(0, m_clip->findData(current.clip)));
	}
	const int named = m_end->findText(joint);
	if (named >= 0)
		m_end->setCurrentIndex(named);
	clipChanged();
	m_first->setValue(current.local);
	m_last->setValue(current.local);
	m_sample->setValue(current.local);
	usePose();
	connect(m_clip, &QComboBox::currentIndexChanged, this,
			[this]
			{
				clipChanged();
				changed();
			});
	for (auto *w : {m_end, m_reach, m_orientation})
		connect(w, &QComboBox::currentIndexChanged, this, [this] { changed(); });
	for (auto *w : {m_first, m_last})
		connect(w, &QSpinBox::valueChanged, this, [this] { changed(); });
	for (int i = 0; i < 3; ++i)
		for (auto *w : {m_target[i], m_pole[i]})
			connect(w, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
	connect(use, &QPushButton::clicked, this,
			[this]
			{
				usePose();
				changed();
			});
	connect(m_sample, &QSpinBox::valueChanged, this, [this] { showPreview(); });
	connect(m_view, &QComboBox::currentIndexChanged, this, [this] { showPreview(); });
	connect(m_review, &QPushButton::clicked, this, &ModelIkDialog::review);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (m_ready && !m_busy)
					accept();
			});
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelIkDialog::reject);
	fitDialogToScreen(this, QSize(940, 800));
	changed();
}
ModelEdit ModelIkDialog::request() const
{
	ModelEdit edit;
	edit.kind = ModelEditKind::SolveTwoBoneIk;
	auto &o = edit.ik;
	o.clip = m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
	o.endJoint = m_end->currentText();
	o.first = m_first->value();
	o.last = m_last->value();
	o.target = {float(m_target[0]->value()), float(m_target[1]->value()), float(m_target[2]->value())};
	o.pole = {float(m_pole[0]->value()), float(m_pole[1]->value()), float(m_pole[2]->value())};
	o.nearestReachable = m_reach->currentIndex() == 1;
	o.keepEndOrientation = m_orientation->currentIndex() == 0;
	return edit;
}
void ModelIkDialog::clipChanged()
{
	const int clip = m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
	const int count = clip < 0 ? 1 : int(m_source.skeleton.clips[clip].frames.size());
	for (auto *w : {m_first, m_last, m_sample})
	{
		QSignalBlocker blocker(w);
		w->setRange(0, std::max(0, count - 1));
		w->setValue(0);
	}
}
void ModelIkDialog::usePose()
{
	const auto o = request().ik;
	if (o.clip < 0 || m_end->currentIndex() < 0)
		return;
	const int end = m_end->currentData().toInt(), middle = m_source.skeleton.joints[end].parent,
			  root = m_source.skeleton.joints[middle].parent;
	const auto &pose = m_source.skeleton.clips[o.clip].frames[o.first];
	const auto convert = [](ModelVec3 p) { return QVector3D(p.x, p.y, p.z); };
	const auto a = convert(modelJointTranslation(pose[root])), b = convert(modelJointTranslation(pose[middle])),
			   c = convert(modelJointTranslation(pose[end]));
	auto pole = b;
	const auto direction = (c - a).normalized();
	if (QVector3D::crossProduct(b - a, direction).length() < 0.00001f)
	{
		const QVector3D basis = std::abs(direction.z()) < 0.8f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
		pole = b + (basis - direction * QVector3D::dotProduct(basis, direction)).normalized() * std::max(1.0f, (b - a).length());
	}
	for (int i = 0; i < 3; ++i)
	{
		QSignalBlocker blockTarget(m_target[i]), blockPole(m_pole[i]);
		m_target[i]->setValue(c[i]);
		m_pole[i]->setValue(pole[i]);
	}
}
void ModelIkDialog::changed()
{
	if (m_busy)
		return;
	m_ready = false;
	m_apply->setEnabled(false);
	m_result = m_source;
	m_table->setSamples({});
	QStringList chain;
	if (m_end->currentIndex() >= 0)
		for (int j = m_end->currentData().toInt(), n = 0; n < 3 && j >= 0; ++n, j = m_source.skeleton.joints[j].parent)
			chain.prepend(QStringLiteral("\u2068%1\u2069").arg(m_source.skeleton.joints[j].name));
	m_chain->setText(chain.join(QStringLiteral(" → ")));
	m_review->setEnabled(m_clip->count() > 0 && m_end->count() > 0);
	m_status->setText(m_review->isEnabled() ? Text::tr("Choose Review to inspect the solved pose and target distance.")
											: Text::tr("A skeletal clip and a three-joint chain are required."));
	showPreview();
}
void ModelIkDialog::showPreview()
{
	// Clips outside the working set preview one skinned sample at a time.
	const int clip = m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
	int frame = -1;
	m_preview->setMesh(modelSkeletalSamplePreview(m_ready && m_view->currentIndex() == 0 ? m_result : m_source, clip, m_sample->value(), &frame), true);
	m_preview->setFrame(std::max(0, frame));
	m_preview->setEditJoint(m_end->currentText());
}
void ModelIkDialog::review()
{
	if (m_busy || m_closeAfter)
		return;
	const auto operation = request();
	const auto source = m_source;
	ModelMesh candidate;
	QVector<ModelIkSample> samples;
	QString error;
	m_ready = false;
	m_busy = true;
	m_tabs->setEnabled(false);
	m_apply->setEnabled(false);
	m_review->setEnabled(false);
	m_table->setSamples({});
	m_status->setText(Text::tr("Solving and validating inverse kinematics…"));
	const bool ok = runModelTask(
		this, Text::tr("Review Inverse Kinematics"),
		[source, operation, &candidate, &samples](QString *failure, const ModelWorkControl &control)
		{
			candidate = source;
			if (!applyModelEdit(&candidate, operation, nullptr, failure, control))
				return false;
			samples = modelIkSamples(candidate, operation.ik);
			return modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, failure);
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
		m_result = m_source;
		showPreview();
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_result = std::move(candidate);
	m_reviewed = operation;
	m_ready = true;
	m_apply->setEnabled(true);
	int reached = 0;
	double residual = 0;
	for (const auto &s : samples)
	{
		reached += s.reached;
		residual = std::max(residual, s.error);
	}
	m_status->setText(Text::tr("Samples reviewed: %L1 · targets reached: %L2. Largest remaining distance: %3 model units. Ready to apply.")
						  .arg(samples.size())
						  .arg(reached)
						  .arg(localeNumber(residual, 9)),
					  LiveStatus::Result);
	m_table->setSamples(std::move(samples));
	m_sample->setValue(operation.ik.first);
	showPreview();
}
void ModelIkDialog::reject()
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
