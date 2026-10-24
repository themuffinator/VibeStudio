#include "app/model_curve_transfer_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "app/model_curve_keys_table.h"
#include "app/model_document_work.h"
#include "app/model_skeletal_working_set_dialog.h"
#include "app/model_viewport.h"
#include "core/model_joint_tools.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelCurveTransferDialog)
};
void accessible(QWidget *w, const char *id, const QString &name, const QString &description)
{
	w->setObjectName(id);
	w->setAccessibleName(name);
	w->setAccessibleDescription(description);
	w->setToolTip(description);
}
void row(QFormLayout *form, const QString &text, QWidget *w)
{
	auto *label = new QLabel(text);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	label->setBuddy(w);
	form->addRow(label, w);
}
} // namespace
ModelCurveTransferDialog::ModelCurveTransferDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent)
	: QDialog(parent), m_source(mesh), m_result(mesh)
{
	setWindowTitle(Text::tr("Transfer Animation Range"));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	auto *context = new QFormLayout;
	context->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->addLayout(context);
	m_from = new QComboBox;
	m_to = new QComboBox;
	accessible(m_from, "curveTransferSource", Text::tr("Source clip"),
			   Text::tr("Read stored curves or capture sampled parent-local motion from this clip."));
	accessible(
		m_to, "curveTransferTarget", Text::tr("Destination clip"),
		Text::tr("Replace the selected channels inside the destination range. Use the source clip here to reverse or copy within it."));
	for (int c = 0; c < mesh.skeleton.clips.size(); ++c)
	{
		addModelSkeletalClipItem(m_from, mesh, c);
		addModelSkeletalClipItem(m_to, mesh, c);
	}
	row(context, Text::tr("&Source clip"), m_from);
	row(context, Text::tr("&Destination clip"), m_to);
	m_tabs = new QTabWidget;
	m_tabs->setObjectName("curveTransferTabs");
	layout->addWidget(m_tabs, 1);
	auto *rangePage = new QWidget;
	auto *form = new QFormLayout(rangePage);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	const auto integer = [&](const char *id, const QString &name, const QString &description)
	{
		auto *w = new QSpinBox;
		w->setKeyboardTracking(false);
		accessible(w, id, name, description);
		row(form, name, w);
		return w;
	};
	m_first = integer("curveTransferFirst", Text::tr("First source sample"),
					  Text::tr("Inclusive source range start; a boundary key is evaluated if needed."));
	m_last = integer("curveTransferLast", Text::tr("Last source sample"),
					 Text::tr("Inclusive source range end. Cubic pieces retain their boundary slopes."));
	m_targetFirst =
		integer("curveTransferTargetFirst", Text::tr("First destination sample"),
				Text::tr("Start of the replaced destination interval. Keys outside it stay, with their original tangent slopes."));
	m_factor = new QDoubleSpinBox;
	m_factor->setDecimals(12);
	m_factor->setRange(.000000000001, 1000000);
	m_factor->setValue(1);
	m_factor->setKeyboardTracking(false);
	accessible(m_factor, "curveTransferFactor", Text::tr("Time scale"),
			   Text::tr("Multiply source sample distances by this positive factor. Every key and boundary must land on an integer "
						"destination sample."));
	row(form, Text::tr("Time scale"), m_factor);
	m_matchRate = new QCheckBox(Text::tr("Match clip rates"));
	accessible(m_matchRate, "curveTransferMatchRate", Text::tr("Match clip rates"),
			   Text::tr("Use destination rate divided by source rate to preserve duration in seconds. Both rates must be set; integer-key "
						"restrictions still apply."));
	form->addRow(m_matchRate);
	m_reverse = new QCheckBox(Text::tr("Reverse source range"));
	accessible(m_reverse, "curveTransferReverse", Text::tr("Reverse source range"),
			   Text::tr("Reverse time, swap tangent sides and reverse step holds. Source motion is read before any destination changes."));
	form->addRow(m_reverse);
	m_summary = new QLabel;
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	accessible(m_summary, "curveTransferSummary", Text::tr("Transfer range summary"),
			   Text::tr("Clip rates and the destination interval that will be replaced."));
	form->addRow(m_summary);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setWidget(rangePage);
	m_tabs->addTab(scroll, Text::tr("&Range"));
	auto *tracks = new QWidget;
	auto *trackLayout = new QVBoxLayout(tracks);
	const auto list = [&](const char *id, const QString &name, const QString &description)
	{
		auto *group = new QGroupBox(name);
		auto *box = new QVBoxLayout(group);
		auto *w = new QListWidget;
		w->setSelectionMode(QAbstractItemView::ExtendedSelection);
		accessible(w, id, name, description);
		box->addWidget(w, 1);
		auto *buttons = new QHBoxLayout;
		for (bool all : {true, false})
		{
			auto *b = new QPushButton(all ? Text::tr("Select all") : Text::tr("Clear selection"));
			accessible(b, (QByteArray(id) + (all ? "All" : "Clear")).constData(), b->text(),
					   all ? Text::tr("Select every item in this list.") : Text::tr("Clear the selected items in this list."));
			connect(b, &QPushButton::clicked, w,
					[w, all]
					{
						if (all)
							w->selectAll();
						else
							w->clearSelection();
					});
			buttons->addWidget(b);
		}
		box->addLayout(buttons);
		trackLayout->addWidget(group, 1);
		return w;
	};
	m_joints = list("curveTransferJoints", Text::tr("Joints"),
					Text::tr("Select one or more joints by exact name. The current model's rig is shared by both clips."));
	for (const auto &j : mesh.skeleton.joints)
		m_joints->addItem(j.name);
	m_channels = list("curveTransferChannels", Text::tr("Channels"),
					  Text::tr("Select parent-local translation, Euler rotation and scale channels. Unselected channels are retained."));
	for (int c = 0; c < 9; ++c)
	{
		auto *item = new QListWidgetItem(modelCurveChannelId(c), m_channels);
		item->setData(Qt::UserRole, c);
	}
	m_joints->selectAll();
	m_channels->selectAll();
	if (!joint.isEmpty())
		for (int i = 0; i < m_joints->count(); ++i)
			m_joints->item(i)->setSelected(m_joints->item(i)->text() == joint);
	auto *trackScroll = new QScrollArea;
	trackScroll->setWidgetResizable(true);
	trackScroll->setWidget(tracks);
	m_tabs->addTab(trackScroll, Text::tr("&Tracks"));
	auto *results = new QWidget;
	auto *resultLayout = new QVBoxLayout(results);
	m_report = new ModelCurveKeyReportTable(this);
	m_results = new QTableView;
	m_results->setModel(m_report);
	m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_results->setSelectionMode(QAbstractItemView::SingleSelection);
	m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_results->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_results->horizontalHeader()->setResizeContentsPrecision(100);
	m_results->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	accessible(m_results, "curveTransferResults", Text::tr("Transfer results"),
			   Text::tr("Copied key counts include evaluated range boundaries. Before/after counts and largest continuous changes describe "
						"destination channels, including adjoining transitions."));
	resultLayout->addWidget(m_results, 1);
	m_detail = new QLineEdit;
	m_detail->setReadOnly(true);
	accessible(m_detail, "curveTransferDetail", Text::tr("Selected channel details"),
			   Text::tr("Full values for the selected destination channel; select or scroll the text to inspect it."));
	resultLayout->addWidget(m_detail);
	m_tabs->addTab(results, Text::tr("R&esults"));
	auto *previewPage = new QWidget;
	auto *previewLayout = new QVBoxLayout(previewPage);
	auto *previewForm = new QFormLayout;
	previewForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	previewLayout->addLayout(previewForm);
	m_sample = new QSpinBox;
	accessible(m_sample, "curveTransferSample", Text::tr("Destination preview sample"),
			   Text::tr("Compare original and reviewed destination motion, including samples outside the replaced range."));
	row(previewForm, Text::tr("Preview sample"), m_sample);
	m_view = new QComboBox;
	m_view->addItems({Text::tr("Reviewed result"), Text::tr("Original destination")});
	accessible(m_view, "curveTransferView", Text::tr("Preview source"),
			   Text::tr("Compare the same destination sample and camera before and after the transfer."));
	row(previewForm, Text::tr("Preview source"), m_view);
	m_preview = new ModelViewport;
	m_preview->setObjectName("curveTransferPreview");
	m_preview->setMinimumSize(240, 220);
	m_preview->setShowJoints(true);
	m_preview->setXrayJoints(true);
	previewLayout->addWidget(m_preview, 1);
	auto *previewScroll = new QScrollArea;
	previewScroll->setWidgetResizable(true);
	previewScroll->setWidget(previewPage);
	m_tabs->addTab(previewScroll, Text::tr("&Preview"));
	m_status = new LiveStatusLabel;
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, "curveTransferStatus", Text::tr("Transfer status"),
			   Text::tr("Review, validation, cancellation and apply status."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("curveTransferApply");
	m_review = buttons->addButton(Text::tr("Review"), QDialogButtonBox::ActionRole);
	accessible(m_review, "curveTransferReview", Text::tr("Review animation transfer"),
			   Text::tr("Prepare and validate curves, destination motion and change reports on a cancellable worker."));
	layout->addWidget(buttons);
	ModelJointFrame current;
	if (modelJointFrame(mesh, frame, &current))
		m_from->setCurrentIndex(std::max(0, m_from->findData(current.clip)));
	if (m_to->count() > 1)
		m_to->setCurrentIndex(m_to->itemData(0).toInt() == from() ? 1 : 0);
	choose(true);
	connect(m_from, &QComboBox::currentIndexChanged, this, [this] { choose(true); });
	connect(m_to, &QComboBox::currentIndexChanged, this, [this] { choose(false); });
	for (auto *w : {m_first, m_last, m_targetFirst})
		connect(w, &QSpinBox::valueChanged, this, [this] { invalidate(); });
	connect(m_factor, &QDoubleSpinBox::valueChanged, this, [this] { invalidate(); });
	for (auto *w : {m_reverse, m_matchRate})
		connect(w, &QCheckBox::toggled, this, [this] { invalidate(); });
	for (auto *w : {m_joints, m_channels})
		connect(w, &QListWidget::itemSelectionChanged, this, [this] { invalidate(); });
	connect(m_sample, &QSpinBox::valueChanged, this, [this] { showPreview(); });
	connect(m_view, &QComboBox::currentIndexChanged, this, [this] { showPreview(); });
	connect(m_results->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
			[this](const QModelIndex &index)
			{
				QStringList fields;
				for (int c = 0; c < m_report->columnCount(); ++c)
					fields << m_report->headerData(c, Qt::Horizontal).toString() + ": " +
								  m_report->data(m_report->index(index.row(), c)).toString();
				m_detail->setText(index.isValid() ? fields.join(" · ") : QString());
				m_detail->setCursorPosition(0);
			});
	connect(m_review, &QPushButton::clicked, this, &ModelCurveTransferDialog::review);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (m_ready && !m_busy)
					accept();
			});
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelCurveTransferDialog::reject);
	fitDialogToScreen(this, QSize(1040, 880));
}
int ModelCurveTransferDialog::from() const
{
	return m_from->currentIndex() < 0 ? -1 : m_from->currentData().toInt();
}
int ModelCurveTransferDialog::to() const
{
	return m_to->currentIndex() < 0 ? -1 : m_to->currentData().toInt();
}
double ModelCurveTransferDialog::factor() const
{
	if (!m_matchRate->isChecked())
		return m_factor->value();
	if (from() < 0 || to() < 0 || m_source.skeleton.clips[from()].framesPerSecond <= 0 ||
		m_source.skeleton.clips[to()].framesPerSecond <= 0)
		return 0;
	return m_source.skeleton.clips[to()].framesPerSecond / m_source.skeleton.clips[from()].framesPerSecond;
}
void ModelCurveTransferDialog::choose(bool sourceChanged)
{
	if (m_busy)
		return;
	m_updating = true;
	const int sourceLast = from() < 0 ? 0 : int(m_source.skeleton.clips[from()].frames.size()) - 1;
	const int targetLast = to() < 0 ? 0 : int(m_source.skeleton.clips[to()].frames.size()) - 1;
	m_first->setRange(0, sourceLast);
	m_last->setRange(0, sourceLast);
	m_targetFirst->setRange(0, targetLast);
	m_sample->setRange(0, targetLast);
	if (sourceChanged)
	{
		m_first->setValue(0);
		m_last->setValue(sourceLast);
	}
	m_updating = false;
	invalidate();
}
void ModelCurveTransferDialog::invalidate()
{
	if (m_busy || m_updating)
		return;
	m_ready = false;
	m_result = m_source;
	m_report->setReport({});
	m_detail->clear();
	m_status->setText(from() < 0 || to() < 0 ? Text::tr("A source and a destination clip are required.")
											 : Text::tr("Select a range and tracks, then Review the destination motion before Apply."));
	controls();
	showPreview();
}
void ModelCurveTransferDialog::controls()
{
	m_from->setEnabled(!m_busy);
	m_to->setEnabled(!m_busy);
	m_tabs->setEnabled(!m_busy);
	m_factor->setEnabled(!m_matchRate->isChecked());
	m_review->setEnabled(!m_busy && from() >= 0 && to() >= 0 && !m_joints->selectedItems().isEmpty() &&
						 !m_channels->selectedItems().isEmpty());
	m_apply->setEnabled(!m_busy && m_ready);
	if (from() >= 0 && to() >= 0)
		m_summary->setText(Text::tr("Source: %1 fps · destination: %2 fps. Replace destination samples %L3–%4 at time scale %5. Adjoining "
									"transitions may change.")
							   .arg(localeNumber(m_source.skeleton.clips[from()].framesPerSecond, 8),
									localeNumber(m_source.skeleton.clips[to()].framesPerSecond, 8))
							   .arg(m_targetFirst->value())
							   .arg(localeNumber(m_targetFirst->value() + (m_last->value() - m_first->value()) * factor(), 12),
									localeNumber(factor(), 12)));
}
void ModelCurveTransferDialog::showPreview()
{
	// Clips outside the working set preview one skinned sample at a time.
	int frame = -1;
	m_preview->setMesh(modelSkeletalSamplePreview(m_ready && m_view->currentIndex() == 0 ? m_result : m_source, to(), m_sample->value(), &frame), true);
	m_preview->setFrame(std::max(0, frame));
}
void ModelCurveTransferDialog::review()
{
	if (m_busy || m_closing || !m_review->isEnabled())
		return;
	ModelEdit operation;
	operation.kind = ModelEditKind::TransferAnimationCurves;
	auto &o = operation.curveTransfer;
	o.sourceClip = from();
	o.targetClip = to();
	o.first = m_first->value();
	o.last = m_last->value();
	o.targetFirst = m_targetFirst->value();
	o.factor = factor();
	o.reverse = m_reverse->isChecked();
	for (auto *item : m_joints->selectedItems())
		o.joints.append(item->text());
	for (auto *item : m_channels->selectedItems())
		o.channels.append(item->data(Qt::UserRole).toInt());
	const auto source = m_source;
	ModelMesh candidate;
	ModelCurveKeyReport report;
	QString error;
	m_ready = false;
	m_busy = true;
	controls();
	m_status->setText(Text::tr("Capturing curves, transferring the range and rebuilding destination motion…"));
	const bool ok = runModelTask(
		this, Text::tr("Review Animation Transfer"),
		[source, operation, &candidate, &report](QString *failure, const ModelWorkControl &control)
		{
			candidate = source;
			return applyModelEdit(&candidate, operation, nullptr, failure, control) &&
				   describeModelCurveTransfer(source, candidate, operation.curveTransfer, &report, failure, control);
		},
		&error, false, &m_cancel);
	m_busy = false;
	if (m_closing)
	{
		QDialog::reject();
		return;
	}
	if (!ok)
	{
		invalidate();
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_result = std::move(candidate);
	m_reviewed = std::move(operation);
	m_ready = true;
	m_report->setReport(report);
	if (m_report->rowCount())
		m_results->setCurrentIndex(m_report->index(0, 0));
	m_status->setText(
		Text::tr("Reviewed %Ln destination channel(s). Inspect changes, adjoining transitions and original/result poses before Apply.",
				 nullptr, int(report.channels.size())),
		LiveStatus::Result);
	controls();
	showPreview();
}
void ModelCurveTransferDialog::reject()
{
	m_closing = true;
	if (m_busy)
	{
		if (m_cancel)
			m_cancel();
		return;
	}
	QDialog::reject();
}
} // namespace vibestudio
