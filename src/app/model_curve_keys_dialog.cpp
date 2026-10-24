#include "app/model_curve_keys_dialog.h"
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
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelCurveKeysDialog)
};
void accessible(QWidget *w, const QString &name, const QString &description)
{
	w->setAccessibleName(name);
	w->setAccessibleDescription(description);
	w->setToolTip(description);
}
void row(QFormLayout *form, const QString &text, QWidget *control)
{
	auto *label = new QLabel(text);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	label->setBuddy(control);
	form->addRow(label, control);
}
QPushButton *button(const QString &name, const char *id, const QString &description)
{
	auto *w = new QPushButton(name);
	w->setObjectName(id);
	accessible(w, name, description);
	return w;
}
void tableSetup(QTableView *table)
{
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->horizontalHeader()->setResizeContentsPrecision(100);
	table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
}
} // namespace
ModelCurveKeysDialog::ModelCurveKeysDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent)
	: QDialog(parent), m_source(mesh), m_result(mesh)
{
	setWindowTitle(Text::tr("Edit Animation Keys"));
	setAccessibleName(windowTitle());
	// The content scrolls on a small screen; the buttons stay in view.
	auto *outer = new QVBoxLayout(this);
	auto *layout = addScrollingDialogBody(outer);
	auto *context = new QFormLayout;
	context->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->addLayout(context);
	m_clip = new QComboBox;
	m_clip->setObjectName("curveKeysClip");
	for (int c = 0; c < mesh.skeleton.clips.size(); ++c)
		if (!mesh.skeleton.clips[c].curves.isEmpty())
			addModelSkeletalClipItem(m_clip, mesh, c);
	accessible(m_clip, Text::tr("Skeletal clip"),
			   Text::tr("Choose a clip with stored curves, in the working set or not. Capture curves in Animation Curves first."));
	row(context, Text::tr("&Clip"), m_clip);
	m_joint = new QComboBox;
	m_joint->setObjectName("curveKeysJoint");
	accessible(m_joint, Text::tr("Joint filter"),
			   Text::tr("Show one joint or all joints. Changing filters clears the key selection and review."));
	row(context, Text::tr("&Joint filter"), m_joint);
	m_channel = new QComboBox;
	m_channel->setObjectName("curveKeysChannel");
	m_channel->addItem(Text::tr("All channels"), -1);
	for (int c = 0; c < 9; ++c)
		m_channel->addItem(modelCurveChannelId(c), c);
	accessible(m_channel, Text::tr("Channel filter"),
			   Text::tr("Show all channels or one translation, rotation or scale channel. Changing filters clears selection."));
	row(context, Text::tr("C&hannel filter"), m_channel);
	m_tabs = new QTabWidget;
	m_tabs->setObjectName("curveKeysTabs");
	layout->addWidget(m_tabs, 1);
	auto *keysPage = new QWidget;
	auto *keysLayout = new QVBoxLayout(keysPage);
	m_table = new ModelCurveKeysTable(this);
	m_keys = new QTableView;
	m_keys->setObjectName("curveKeysTable");
	m_keys->setModel(m_table);
	tableSetup(m_keys);
	m_keys->setSelectionMode(QAbstractItemView::ExtendedSelection);
	keysLayout->addWidget(m_keys, 1);
	accessible(m_keys, Text::tr("Animation keys"),
			   Text::tr("Select rows across joints and channels. Use extended selection, Select All or an inclusive sample range. Values "
						"use model units, degrees or scale factors."));
	auto *rangeForm = new QFormLayout;
	rangeForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	keysLayout->addLayout(rangeForm);
	m_first = new QSpinBox;
	m_first->setObjectName("curveKeysFirst");
	m_last = new QSpinBox;
	m_last->setObjectName("curveKeysLast");
	accessible(m_first, Text::tr("First sample"), Text::tr("First inclusive clip sample for range selection."));
	accessible(m_last, Text::tr("Last sample"), Text::tr("Last inclusive clip sample for range selection."));
	row(rangeForm, Text::tr("&First sample"), m_first);
	row(rangeForm, Text::tr("&Last sample"), m_last);
	auto *range = button(Text::tr("Select range"), "curveKeysRange",
						 Text::tr("Replace the selection with visible keys in the inclusive sample range."));
	auto *all = button(Text::tr("Select all visible keys"), "curveKeysAll",
					   Text::tr("Select every key matching the current joint and channel filters."));
	auto *clear = button(Text::tr("Clear selection"), "curveKeysClear", Text::tr("Clear the selected key rows."));
	for (auto *b : {range, all, clear})
		keysLayout->addWidget(b);
	m_count = new QLabel;
	m_count->setWordWrap(true);
	m_count->setTextFormat(Qt::PlainText);
	m_count->setObjectName("curveKeysCount");
	keysLayout->addWidget(m_count);
	m_tabs->addTab(keysPage, Text::tr("&Keys"));
	auto *operationPage = new QWidget;
	auto *operationForm = new QFormLayout(operationPage);
	operationForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_operation = new QComboBox;
	m_operation->setObjectName("curveKeysOperation");
	m_operation->addItems({Text::tr("Retime selected keys"), Text::tr("Duplicate selected keys"), Text::tr("Remove selected keys"),
						   Text::tr("Reduce selected keys")});
	accessible(m_operation, Text::tr("Key operation"),
			   Text::tr("Retime, duplicate, remove or reduce the selected keys as one reviewed document edit."));
	row(operationForm, Text::tr("&Operation"), m_operation);
	const auto numeric = [&](const char *id, const QString &name, const QString &description, double low, double high, double value)
	{
		auto *w = new QDoubleSpinBox;
		w->setObjectName(id);
		w->setDecimals(8);
		w->setRange(low, high);
		w->setValue(value);
		w->setKeyboardTracking(false);
		accessible(w, name, description);
		row(operationForm, name, w);
		return w;
	};
	m_pivot = numeric("curveKeysPivot", Text::tr("Time pivot"), Text::tr("Scale time around this clip sample, then add the offset."), 0,
					  1000000, 0);
	m_factor = numeric("curveKeysFactor", Text::tr("Time scale"),
					   Text::tr("Positive time multiplier. Free tangent slopes are divided by this factor."), .00000001, 1000000, 1);
	m_offset = numeric("curveKeysOffset", Text::tr("Sample offset"),
					   Text::tr("Add this offset after scaling. Existing destination keys are never overwritten."), -1000000, 1000000, 0);
	m_snap = new QCheckBox(Text::tr("Round to nearest sample"));
	m_snap->setObjectName("curveKeysSnap");
	accessible(m_snap, Text::tr("Round to nearest sample"),
			   Text::tr("Round fractional results to the nearest sample, with halves away from zero. Collisions still fail."));
	operationForm->addRow(m_snap);
	m_tolerance[0] = numeric("curveKeysTranslationError", Text::tr("Translation error"),
							 Text::tr("Maximum continuous change in each selected translation channel, in model units."), 0, 1000000, .001);
	m_tolerance[1] = numeric(
		"curveKeysRotationError", Text::tr("Rotation error"),
		Text::tr("Maximum continuous change in each selected Euler channel, in degrees; this is not a world-space skin error bound."), 0,
		1000000, .01);
	m_tolerance[2] = numeric(
		"curveKeysScaleError", Text::tr("Scale error"),
		Text::tr("Maximum continuous change in each selected scale channel. Step changes and selected range endpoints are retained."), 0,
		1000000, .0001);
	auto *operationScroll = new QScrollArea;
	operationScroll->setWidgetResizable(true);
	operationScroll->setWidget(operationPage);
	m_tabs->addTab(operationScroll, Text::tr("&Operation"));
	auto *resultsPage = new QWidget;
	auto *resultsLayout = new QVBoxLayout(resultsPage);
	m_reportTable = new ModelCurveKeyReportTable(this);
	m_results = new QTableView;
	m_results->setObjectName("curveKeysResults");
	m_results->setModel(m_reportTable);
	tableSetup(m_results);
	m_results->setSelectionMode(QAbstractItemView::SingleSelection);
	resultsLayout->addWidget(m_results, 1);
	accessible(
		m_results, Text::tr("Key edit results"),
		Text::tr("Selected and retained key counts and the largest continuous scalar change for each channel, including between samples."));
	m_detail = new QLineEdit;
	m_detail->setObjectName("curveKeysDetail");
	m_detail->setReadOnly(true);
	resultsLayout->addWidget(m_detail);
	accessible(m_detail, Text::tr("Selected channel details"),
			   Text::tr("Full text and numeric values for the selected result row; select or scroll to inspect long values."));
	m_tabs->addTab(resultsPage, Text::tr("&Results"));
	auto *previewPage = new QWidget;
	auto *previewLayout = new QVBoxLayout(previewPage);
	auto *previewForm = new QFormLayout;
	previewForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	previewLayout->addLayout(previewForm);
	m_sample = new QSpinBox;
	m_sample->setObjectName("curveKeysPreviewSample");
	accessible(m_sample, Text::tr("Preview sample"), Text::tr("Compare the original and reviewed dense samples used for game export."));
	row(previewForm, Text::tr("Preview sa&mple"), m_sample);
	m_view = new QComboBox;
	m_view->setObjectName("curveKeysPreviewSource");
	m_view->addItems({Text::tr("Reviewed result"), Text::tr("Original model")});
	accessible(m_view, Text::tr("Preview source"), Text::tr("Compare motion at the same sample and camera."));
	row(previewForm, Text::tr("Preview s&ource"), m_view);
	m_preview = new ModelViewport;
	m_preview->setObjectName("curveKeysPreview");
	m_preview->setMinimumSize(240, 220);
	m_preview->setShowJoints(true);
	m_preview->setXrayJoints(true);
	previewLayout->addWidget(m_preview, 1);
	m_tabs->addTab(previewPage, Text::tr("&Preview"));
	m_status = new LiveStatusLabel;
	m_status->setObjectName("curveKeysStatus");
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	accessible(m_status, Text::tr("Key operation status"), Text::tr("Selection, review, validation, cancellation and apply status."));
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("curveKeysApply");
	m_review = buttons->addButton(Text::tr("Review"), QDialogButtonBox::ActionRole);
	m_review->setObjectName("curveKeysReview");
	accessible(m_review, Text::tr("Review animation keys"),
			   Text::tr("Prepare selected key edits, continuous change reports and sampled poses in a cancellable worker."));
	outer->addWidget(buttons);
	ModelJointFrame current;
	if (modelJointFrame(mesh, frame, &current))
		m_clip->setCurrentIndex(std::max(0, m_clip->findData(current.clip)));
	choose();
	if (!joint.isEmpty())
		m_joint->setCurrentIndex(std::max(0, m_joint->findData(joint)));
	filter();
	m_sample->setValue(current.local);
	connect(m_clip, &QComboBox::currentIndexChanged, this, [this] { choose(); });
	connect(m_joint, &QComboBox::currentIndexChanged, this, [this] { filter(); });
	connect(m_channel, &QComboBox::currentIndexChanged, this, [this] { filter(); });
	connect(m_keys->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { invalidate(); });
	connect(range, &QPushButton::clicked, this,
			[this]
			{
				m_keys->selectionModel()->select(m_table->sampleRange(m_first->value(), m_last->value()),
												 QItemSelectionModel::ClearAndSelect);
			});
	connect(all, &QPushButton::clicked, m_keys, &QTableView::selectAll);
	connect(clear, &QPushButton::clicked, m_keys, &QTableView::clearSelection);
	connect(m_operation, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });
	for (auto *w : {m_pivot, m_factor, m_offset, m_tolerance[0], m_tolerance[1], m_tolerance[2]})
		connect(w, &QDoubleSpinBox::valueChanged, this, [this] { invalidate(); });
	connect(m_snap, &QCheckBox::toggled, this, [this] { invalidate(); });
	connect(m_sample, &QSpinBox::valueChanged, this, [this] { showPreview(); });
	connect(m_view, &QComboBox::currentIndexChanged, this, [this] { showPreview(); });
	connect(m_results->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
			[this](const QModelIndex &index)
			{
				QStringList fields;
				for (int c = 0; c < m_reportTable->columnCount(); ++c)
					fields << m_reportTable->headerData(c, Qt::Horizontal).toString() + ": " +
								  m_reportTable->data(m_reportTable->index(index.row(), c)).toString();
				m_detail->setText(index.isValid() ? fields.join(" · ") : QString());
				m_detail->setCursorPosition(0);
			});
	connect(m_review, &QPushButton::clicked, this, &ModelCurveKeysDialog::review);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (m_ready && !m_busy)
					accept();
			});
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelCurveKeysDialog::reject);
	updateControls();
	fitDialogToScreen(this, QSize(1040, 880));
}
int ModelCurveKeysDialog::clip() const
{
	return m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
}
void ModelCurveKeysDialog::choose()
{
	if (m_busy)
		return;
	m_updating = true;
	QSignalBlocker block(m_joint);
	m_joint->clear();
	m_joint->addItem(Text::tr("All joints"), QString());
	const auto groups = clip() < 0 ? QVector<ModelPoseCurves>{} : m_source.skeleton.clips[clip()].curves;
	for (const auto &group : groups)
		m_joint->addItem(group.joint, group.joint);
	m_table->setGroups(groups);
	const int last = clip() < 0 ? 0 : int(m_source.skeleton.clips[clip()].frames.size()) - 1;
	for (auto *w : {m_first, m_last, m_sample})
		w->setRange(0, last);
	m_first->setValue(0);
	m_last->setValue(last);
	m_pivot->setMaximum(last);
	m_updating = false;
	filter();
}
void ModelCurveKeysDialog::filter()
{
	if (m_busy)
		return;
	m_updating = true;
	m_table->filter(m_joint->currentData().toString(), m_channel->currentData().toInt());
	m_updating = false;
	invalidate();
}
void ModelCurveKeysDialog::invalidate()
{
	if (m_busy || m_updating)
		return;
	m_ready = false;
	m_report = {};
	m_reportTable->setReport(m_report);
	m_detail->clear();
	m_result = m_source;
	m_status->setText(clip() < 0 ? Text::tr("Capture curves on a skeletal clip before editing keys.")
								 : Text::tr("Select keys, choose an operation, then Review before applying."));
	updateControls();
	showPreview();
}
void ModelCurveKeysDialog::updateControls()
{
	qint64 selected = 0;
	for (const auto &range : m_keys->selectionModel()->selection())
		selected += range.bottom() - range.top() + 1;
	m_count->setText(Text::tr("Selected keys: %1 · visible keys: %2.").arg(selected).arg(m_table->rowCount()));
	for (auto *w : {m_clip, m_joint, m_channel})
		w->setEnabled(!m_busy);
	m_tabs->setEnabled(!m_busy);
	m_review->setEnabled(!m_busy && selected > 0);
	m_apply->setEnabled(!m_busy && m_ready);
	const bool timing = m_operation->currentIndex() < 2, reduce = m_operation->currentIndex() == 3;
	for (auto *w : {m_pivot, m_factor, m_offset})
		w->setEnabled(timing);
	m_snap->setEnabled(timing);
	for (auto *w : m_tolerance)
		w->setEnabled(reduce);
}
void ModelCurveKeysDialog::showPreview()
{
	// Clips outside the working set preview one skinned sample at a time.
	int frame = -1;
	m_preview->setMesh(modelSkeletalSamplePreview(m_ready && m_view->currentIndex() == 0 ? m_result : m_source, clip(), m_sample->value(), &frame), true);
	m_preview->setFrame(std::max(0, frame));
	m_preview->setEditJoint(m_joint->currentData().toString());
}
void ModelCurveKeysDialog::review()
{
	if (m_busy || m_closing || !m_review->isEnabled())
		return;
	ModelEdit operation;
	operation.kind = ModelEditKind::EditCurveKeys;
	auto &o = operation.curveKeys;
	o.clip = clip();
	o.action = ModelCurveKeyAction(m_operation->currentIndex());
	if (m_operation->currentIndex() < 2)
	{
		o.pivot = m_pivot->value();
		o.factor = m_factor->value();
		o.offset = m_offset->value();
		o.snap = m_snap->isChecked();
	}
	if (o.action == ModelCurveKeyAction::Reduce)
		for (int i = 0; i < 3; ++i)
			o.tolerance[i] = m_tolerance[i]->value();
	QVector<QPair<int, int>> ranges;
	for (const auto &range : m_keys->selectionModel()->selection())
		ranges.append({range.top(), range.bottom()});
	const auto rows = m_table->snapshot();
	const auto source = m_source;
	ModelMesh candidate;
	ModelCurveKeyReport report;
	QString error;
	m_ready = false;
	m_busy = true;
	updateControls();
	m_status->setText(Text::tr("Editing keys, checking continuous curves and rebuilding motion…"));
	const bool ok = runModelTask(
		this, Text::tr("Review Animation Keys"),
		[rows, ranges, source, &operation, &candidate, &report](QString *failure, const ModelWorkControl &control)
		{
			if (!modelCurveKeysFromRows(rows, ranges, &operation.curveKeys.selection, failure, control))
				return false;
			candidate = source;
			if (!applyModelEdit(&candidate, operation, nullptr, failure, control))
				return false;
			const int clip = operation.curveKeys.clip;
			return describeModelCurveKeyChanges(source.skeleton.clips[clip].curves, candidate.skeleton.clips[clip].curves,
												int(source.skeleton.clips[clip].frames.size()), operation.curveKeys.selection, &report,
												failure, control);
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
	m_report = std::move(report);
	m_ready = true;
	m_reportTable->setReport(m_report);
	if (m_reportTable->rowCount())
		m_results->setCurrentIndex(m_reportTable->index(0, 0));
	m_status->setText(Text::tr("Reviewed %Ln channel(s). Inspect key counts, continuous changes and original/result poses before Apply.", nullptr,
							   int(m_report.channels.size())),
					  LiveStatus::Result);
	updateControls();
	showPreview();
}
void ModelCurveKeysDialog::reject()
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
