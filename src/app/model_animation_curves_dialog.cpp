#include "app/model_animation_curves_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "app/model_animation_curve_view.h"
#include "app/model_document_work.h"
#include "app/model_skeletal_working_set_dialog.h"
#include "app/model_viewport.h"
#include "core/model_joint_tools.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
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
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationCurvesDialog)
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
QStringList modes()
{
	return {Text::tr("Step"), Text::tr("Linear"), Text::tr("Cubic"), Text::tr("Hold next")};
}
QStringList tangents()
{
	return {Text::tr("Automatic"), Text::tr("Clamped"), Text::tr("Free"), Text::tr("Flat")};
}
QPushButton *button(const QString &name, const char *id, const QString &description)
{
	auto *b = new QPushButton(name);
	b->setObjectName(id);
	accessible(b, name, description);
	return b;
}
} // namespace
class ModelCurveKeyTable final : public QAbstractTableModel
{
  public:
	explicit ModelCurveKeyTable(QObject *parent) : QAbstractTableModel(parent) {}
	QVector<ModelPoseCurveKey> keys;
	void setKeys(const QVector<ModelPoseCurveKey> &value)
	{
		beginResetModel();
		keys = value;
		endResetModel();
	}
	int rowCount(const QModelIndex &p = {}) const override
	{
		return p.isValid() ? 0 : int(keys.size());
	}
	int columnCount(const QModelIndex &p = {}) const override
	{
		return p.isValid() ? 0 : 6;
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (role != Qt::DisplayRole)
			return {};
		if (orientation == Qt::Vertical)
			return section + 1;
		return QStringList{Text::tr("Sample"),	 Text::tr("Value"),			 Text::tr("Outgoing curve"),
						   Text::tr("Tangents"), Text::tr("Incoming slope"), Text::tr("Outgoing slope")}
			.value(section);
	}
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
	{
		if (!index.isValid() || index.row() >= keys.size() ||
			(role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole))
			return {};
		const auto &k = keys[index.row()];
		const auto slopes = modelCurveSlopes(keys, index.row());
		switch (index.column())
		{
		case 0:
			return k.frame;
		case 1:
			return localeNumber(k.value, 12);
		case 2:
			return modes().value(int(k.interpolation));
		case 3:
			return tangents().value(int(k.tangent));
		case 4:
			return localeNumber(slopes.first, 12);
		case 5:
			return localeNumber(slopes.second, 12);
		default:
			return {};
		}
	}
};
ModelAnimationCurvesDialog::ModelAnimationCurvesDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent)
	: QDialog(parent), m_source(mesh), m_result(mesh)
{
	setWindowTitle(Text::tr("Animation Curves"));
	setAccessibleName(windowTitle());
	// The content scrolls on a small screen; the buttons stay in view.
	auto *outer = new QVBoxLayout(this);
	auto *layout = addScrollingDialogBody(outer);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->addLayout(form);
	m_clip = new QComboBox;
	m_clip->setObjectName("curvesClip");
	for (int c = 0; c < mesh.skeleton.clips.size(); ++c)
		addModelSkeletalClipItem(m_clip, mesh, c);
	accessible(
		m_clip, Text::tr("Skeletal clip"),
		Text::tr("Choose any retained clip, in the working set or not. Apply or discard draft changes before choosing another clip."));
	row(form, Text::tr("&Clip"), m_clip);
	m_joint = new QComboBox;
	m_joint->setObjectName("curvesJoint");
	for (const auto &j : mesh.skeleton.joints)
		m_joint->addItem(j.name);
	accessible(m_joint, Text::tr("Animated joint"),
			   Text::tr("Edit the joint's parent-local translation, XYZ rotation and scale. Descendants follow its motion."));
	row(form, Text::tr("&Joint"), m_joint);
	m_channel = new QComboBox;
	m_channel->setObjectName("curvesChannel");
	m_channel->addItems({Text::tr("Translation X"), Text::tr("Translation Y"), Text::tr("Translation Z"), Text::tr("Rotation X"),
						 Text::tr("Rotation Y"), Text::tr("Rotation Z"), Text::tr("Scale X"), Text::tr("Scale Y"), Text::tr("Scale Z")});
	accessible(
		m_channel, Text::tr("Curve channel"),
		Text::tr("Translations use model units, rotations use unwrapped degrees in X then Y then Z order, and scales remain positive."));
	row(form, Text::tr("C&hannel"), m_channel);
	auto *draftButtons = new QHBoxLayout;
	layout->addLayout(draftButtons);
	m_undo = button(Text::tr("Undo draft"), "curvesUndo", Text::tr("Undo the last staged key or curve change."));
	m_redo = button(Text::tr("Redo draft"), "curvesRedo", Text::tr("Redo a staged change."));
	m_discard = button(Text::tr("Discard draft"), "curvesDiscard", Text::tr("Discard all staged changes and restore the source curves."));
	for (auto *b : {m_undo, m_redo, m_discard})
		draftButtons->addWidget(b);
	m_tabs = new QTabWidget;
	m_tabs->setObjectName("curvesTabs");
	layout->addWidget(m_tabs, 1);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *keysPage = new QWidget;
	auto *keysLayout = new QVBoxLayout(keysPage);
	scroll->setWidget(keysPage);
	m_tabs->addTab(scroll, Text::tr("&Keys"));
	m_capture =
		button(Text::tr("Capture Sampled Motion"), "curvesCapture",
			   Text::tr("Create nine channels with a key at every existing sample, preserving the imported motion before editing."));
	keysLayout->addWidget(m_capture);
	m_keys = new QTableView;
	m_keys->setObjectName("curvesKeys");
	m_table = new ModelCurveKeyTable(this);
	m_keys->setModel(m_table);
	m_keys->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_keys->setSelectionMode(QAbstractItemView::SingleSelection);
	m_keys->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_keys->setMinimumHeight(180);
	accessible(m_keys, Text::tr("Animation keys"),
			   Text::tr("Select a key to inspect and edit its sample, value, interpolation and tangent slopes in the controls below."));
	keysLayout->addWidget(m_keys);
	auto *keyForm = new QFormLayout;
	keyForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	keysLayout->addLayout(keyForm);
	m_frame = new QSpinBox;
	m_frame->setObjectName("curvesKeyFrame");
	accessible(m_frame, Text::tr("Key sample"),
			   Text::tr("Sample index within the clip. Set key inserts or replaces at this index; Move key moves the selected key here."));
	row(keyForm, Text::tr("&Sample"), m_frame);
	m_value = new QDoubleSpinBox;
	m_value->setObjectName("curvesKeyValue");
	m_in = new QDoubleSpinBox;
	m_in->setObjectName("curvesInSlope");
	m_out = new QDoubleSpinBox;
	m_out->setObjectName("curvesOutSlope");
	for (auto *w : {m_value, m_in, m_out})
	{
		w->setDecimals(8);
		w->setRange(-1000000, 1000000);
		w->setKeyboardTracking(false);
	}
	accessible(m_value, Text::tr("Key value"), Text::tr("Absolute parent-local channel value. Rotation values may exceed a full turn."));
	row(keyForm, Text::tr("&Value"), m_value);
	m_mode = new QComboBox;
	m_mode->setObjectName("curvesInterpolation");
	m_mode->addItems(modes());
	accessible(m_mode, Text::tr("Outgoing interpolation"),
			   Text::tr("Interpolation between keys: hold this value, linear, cubic Hermite or hold the next value. Exact key times retain "
						"their own values."));
	row(keyForm, Text::tr("&Interpolation"), m_mode);
	m_tangent = new QComboBox;
	m_tangent->setObjectName("curvesTangent");
	m_tangent->addItems(tangents());
	accessible(m_tangent, Text::tr("Tangent mode"),
			   Text::tr("Automatic uses neighbouring slopes; clamped limits overshoot; free uses separate incoming and outgoing slopes; "
						"flat uses zero slope."));
	row(keyForm, Text::tr("&Tangents"), m_tangent);
	accessible(m_in, Text::tr("Incoming slope"), Text::tr("Channel units per sample approaching this key; active for free tangents."));
	row(keyForm, Text::tr("Incoming s&lope"), m_in);
	accessible(m_out, Text::tr("Outgoing slope"), Text::tr("Channel units per sample leaving this key; active for free tangents."));
	row(keyForm, Text::tr("Outgoing slo&pe"), m_out);
	auto *keyButtons = new QHBoxLayout;
	keysLayout->addLayout(keyButtons);
	m_set = button(Text::tr("Set key"), "curvesSetKey", Text::tr("Insert or replace a key using the numeric controls."));
	m_move = button(Text::tr("Move key"), "curvesMoveKey",
					Text::tr("Move the selected key to the sample in the numeric control. Occupied samples are refused."));
	m_remove =
		button(Text::tr("Remove key"), "curvesRemoveKey", Text::tr("Remove the selected key. Every channel retains at least one key."));
	for (auto *b : {m_set, m_move, m_remove})
		keyButtons->addWidget(b);
	m_bake =
		button(Text::tr("Stage Bake Joint Curves"), "curvesBake",
			   Text::tr("Stage removal of this joint's stored curves while keeping its sampled motion. Apply or discard key edits first."));
	keysLayout->addWidget(m_bake);
	auto *graphPage = new QWidget;
	auto *graphLayout = new QVBoxLayout(graphPage);
	m_graph = new ModelAnimationCurveView;
	graphLayout->addWidget(m_graph, 1);
	auto *fit = button(Text::tr("Fit curve"), "curvesFit", Text::tr("Fit all keys and cubic extrema in the graph."));
	graphLayout->addWidget(fit);
	m_tabs->addTab(graphPage, Text::tr("&Curve"));
	auto *previewPage = new QWidget;
	auto *previewLayout = new QVBoxLayout(previewPage);
	auto *previewForm = new QFormLayout;
	previewForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	previewLayout->addLayout(previewForm);
	m_sample = new QSpinBox;
	m_sample->setObjectName("curvesPreviewSample");
	accessible(m_sample, Text::tr("Preview sample"),
			   Text::tr("Compare dense samples used by native game exporters. The graph shows the continuous curve between samples."));
	row(previewForm, Text::tr("Preview sa&mple"), m_sample);
	m_view = new QComboBox;
	m_view->setObjectName("curvesPreviewSource");
	m_view->addItems({Text::tr("Reviewed result"), Text::tr("Original model")});
	accessible(m_view, Text::tr("Preview source"), Text::tr("Compare reviewed and original motion using the same sample and camera."));
	row(previewForm, Text::tr("Preview s&ource"), m_view);
	m_preview = new ModelViewport;
	m_preview->setObjectName("curvesPreview");
	m_preview->setMinimumSize(240, 220);
	m_preview->setShowJoints(true);
	m_preview->setXrayJoints(true);
	previewLayout->addWidget(m_preview, 1);
	m_tabs->addTab(previewPage, Text::tr("&Preview"));
	m_status = new LiveStatusLabel;
	m_status->setObjectName("curvesStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, Text::tr("Animation curve status"), Text::tr("Draft, review, validation, cancellation and apply status."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("curvesApply");
	m_review = buttons->addButton(Text::tr("Review"), QDialogButtonBox::ActionRole);
	m_review->setObjectName("curvesReview");
	accessible(m_review, Text::tr("Review animation curves"),
			   Text::tr("Rebuild and validate sampled motion in a cancellable worker before applying."));
	outer->addWidget(buttons);
	ModelJointFrame current;
	if (modelJointFrame(mesh, frame, &current))
		m_clip->setCurrentIndex(std::max(0, m_clip->findData(current.clip)));
	const int named = m_joint->findText(joint);
	if (named >= 0)
		m_joint->setCurrentIndex(named);
	choose();
	m_sample->setValue(current.local);
	connect(m_clip, &QComboBox::currentIndexChanged, this, [this] { choose(); });
	connect(m_joint, &QComboBox::currentIndexChanged, this, [this] { choose(); });
	connect(m_channel, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
	connect(m_keys->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
			[this](const QModelIndex &index)
			{
				if (!m_updating)
					selectedKey(index.row());
			});
	connect(m_tangent, &QComboBox::currentIndexChanged, this,
			[this]
			{
				m_in->setEnabled(m_tangent->currentIndex() == 2 && m_set->isEnabled());
				m_out->setEnabled(m_in->isEnabled());
			});
	connect(m_capture, &QPushButton::clicked, this, &ModelAnimationCurvesDialog::capture);
	connect(m_set, &QPushButton::clicked, this, [this] { commitKey(-1, keyFromControls()); });
	connect(m_move, &QPushButton::clicked, this,
			[this]
			{
				const int row = m_keys->currentIndex().row();
				if (row >= 0)
				{
					auto key = m_table->keys[row];
					const int old = key.frame;
					key.frame = m_frame->value();
					commitKey(old, key);
				}
			});
	connect(m_remove, &QPushButton::clicked, this,
			[this]
			{
				const int row = m_keys->currentIndex().row();
				if (row >= 0)
					removeKey(m_table->keys[row].frame);
			});
	connect(m_undo, &QPushButton::clicked, this,
			[this]
			{
				if (m_historyIndex > 0)
				{
					--m_historyIndex;
					m_ready = false;
					m_result = m_source;
					refresh();
				}
			});
	connect(m_redo, &QPushButton::clicked, this,
			[this]
			{
				if (m_historyIndex + 1 < m_history.size())
				{
					++m_historyIndex;
					m_ready = false;
					m_result = m_source;
					refresh();
				}
			});
	connect(m_discard, &QPushButton::clicked, this, [this] { choose(); });
	connect(m_bake, &QPushButton::clicked, this,
			[this]
			{
				auto next = draft();
				next.bake = true;
				stage(next, 0);
			});
	connect(fit, &QPushButton::clicked, m_graph, &ModelAnimationCurveView::fit);
	m_graph->selected = [this](int index)
	{
		if (index >= 0)
			m_keys->setCurrentIndex(m_table->index(index, 0));
	};
	m_graph->edited = [this](int old, const ModelPoseCurveKey &key) { commitKey(old, key); };
	m_graph->removed = [this](int sample) { removeKey(sample); };
	connect(m_sample, &QSpinBox::valueChanged, this, [this] { showPreview(); });
	connect(m_view, &QComboBox::currentIndexChanged, this, [this] { showPreview(); });
	connect(m_review, &QPushButton::clicked, this, &ModelAnimationCurvesDialog::review);
	connect(m_apply, &QPushButton::clicked, this,
			[this]
			{
				if (m_ready && !m_busy)
					accept();
			});
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelAnimationCurvesDialog::reject);
	fitDialogToScreen(this, QSize(1040, 860));
}
int ModelAnimationCurvesDialog::clip() const
{
	return m_clip->currentIndex() < 0 ? -1 : m_clip->currentData().toInt();
}
int ModelAnimationCurvesDialog::frameCount() const
{
	return clip() < 0 ? 0 : int(m_source.skeleton.clips[clip()].frames.size());
}
const ModelAnimationCurvesDialog::Draft &ModelAnimationCurvesDialog::draft() const
{
	return m_history[m_historyIndex];
}
void ModelAnimationCurvesDialog::choose()
{
	if (m_busy)
		return;
	Draft initial;
	initial.group.joint = m_joint->currentText();
	if (clip() >= 0)
		for (const auto &g : m_source.skeleton.clips[clip()].curves)
			if (g.joint == initial.group.joint)
				initial.group = g;
	m_initial = initial;
	m_history = {initial};
	m_historyIndex = 0;
	m_ready = false;
	m_result = m_source;
	m_frame->setRange(0, std::max(0, frameCount() - 1));
	m_sample->setRange(0, std::max(0, frameCount() - 1));
	refresh();
}
void ModelAnimationCurvesDialog::refresh(int selectedFrame)
{
	m_updating = true;
	const bool has = !draft().group.channels[0].isEmpty(), dirty = !(draft() == m_initial), editable = has && !draft().bake;
	m_clip->setEnabled(!dirty && !m_busy);
	m_joint->setEnabled(!dirty && !m_busy);
	m_channel->setEnabled(!m_busy);
	m_capture->setEnabled(!has && clip() >= 0 && m_joint->count() > 0 && !m_busy);
	m_bake->setEnabled(has && !dirty && !m_busy);
	m_undo->setEnabled(m_historyIndex > 0 && !m_busy);
	m_redo->setEnabled(m_historyIndex + 1 < m_history.size() && !m_busy);
	m_discard->setEnabled(dirty && !m_busy);
	const auto &keys = draft().group.channels[m_channel->currentIndex()];
	m_table->setKeys(keys);
	int selected = 0;
	for (int i = 0; i < keys.size(); ++i)
		if (keys[i].frame <= selectedFrame)
			selected = i;
	m_graph->setCurve(keys, frameCount(), m_channel->currentIndex(), keys.isEmpty() ? -1 : selected);
	m_graph->setEnabled(editable && !m_busy);
	// The field takes what the graph does, and a screen reader hears that range.
	m_value->setRange(m_graph->minimumValue(), m_graph->maximumValue());
	for (auto *w : QList<QWidget *>{m_frame, m_value, m_mode, m_tangent, m_set, m_move, m_remove})
		w->setEnabled(editable && !m_busy);
	m_in->setEnabled(editable && !m_busy && m_tangent->currentIndex() == 2);
	m_out->setEnabled(m_in->isEnabled());
	m_review->setEnabled(has && !m_busy);
	m_apply->setEnabled(m_ready && !m_busy);
	if (!keys.isEmpty())
		m_keys->setCurrentIndex(m_table->index(selected, 0));
	m_updating = false;
	if (!keys.isEmpty())
		selectedKey(selected);
	if (!m_ready)
		m_status->setText(draft().bake
							  ? Text::tr("Stored curves will be removed; sampled motion will be retained. Choose Review before applying.")
						  : has ? Text::tr("Edit keys or tangents, then choose Review to inspect the sampled motion.")
								: Text::tr("Capture sampled motion to create editable curves without changing the imported poses."));
	showPreview();
}
void ModelAnimationCurvesDialog::selectedKey(int index)
{
	if (index < 0 || index >= m_table->keys.size())
		return;
	const auto &key = m_table->keys[index];
	m_frame->setValue(key.frame);
	m_value->setValue(key.value);
	m_mode->setCurrentIndex(int(key.interpolation));
	m_tangent->setCurrentIndex(int(key.tangent));
	m_in->setValue(key.inSlope);
	m_out->setValue(key.outSlope);
	m_graph->setCurve(m_table->keys, frameCount(), m_channel->currentIndex(), index);
}
void ModelAnimationCurvesDialog::stage(Draft next, int selectedFrame)
{
	if (m_busy || next == draft())
		return;
	m_history.resize(m_historyIndex + 1);
	m_history.append(std::move(next));
	++m_historyIndex;
	if (m_history.size() > draftLimit)
	{
		m_history.removeFirst();
		--m_historyIndex;
	}
	m_ready = false;
	m_result = m_source;
	refresh(selectedFrame);
}
ModelPoseCurveKey ModelAnimationCurvesDialog::keyFromControls() const
{
	return {m_frame->value(),
			m_value->value(),
			m_in->value(),
			m_out->value(),
			ModelCurveInterpolation(m_mode->currentIndex()),
			ModelCurveTangent(m_tangent->currentIndex())};
}
void ModelAnimationCurvesDialog::commitKey(int oldFrame, const ModelPoseCurveKey &key)
{
	if (m_busy || draft().bake || draft().group.channels[0].isEmpty())
		return;
	auto next = draft();
	auto &keys = next.group.channels[m_channel->currentIndex()];
	if (oldFrame >= 0 && oldFrame != key.frame)
	{
		if (std::any_of(keys.cbegin(), keys.cend(), [&](const auto &k) { return k.frame == key.frame; }))
		{
			m_status->setText(Text::tr("Another key already occupies the destination sample."));
			m_graph->setCurve(m_table->keys, frameCount(), m_channel->currentIndex(), m_keys->currentIndex().row());
			return;
		}
		keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const auto &k) { return k.frame == oldFrame; }), keys.end());
	}
	ModelCurveEditOptions o;
	o.action = ModelCurveAction::SetKey;
	o.joint = next.group.joint;
	o.channel = m_channel->currentIndex();
	o.frame = key.frame;
	o.key = key;
	QString error;
	if (!editModelCurveGroup(&next.group, frameCount(), o, &error))
	{
		m_status->setText(error, LiveStatus::Problem);
		m_graph->setCurve(m_table->keys, frameCount(), m_channel->currentIndex(), m_keys->currentIndex().row());
		return;
	}
	stage(next, key.frame);
}
void ModelAnimationCurvesDialog::removeKey(int frame)
{
	if (m_busy || draft().bake)
		return;
	auto next = draft();
	ModelCurveEditOptions o;
	o.action = ModelCurveAction::RemoveKey;
	o.joint = next.group.joint;
	o.channel = m_channel->currentIndex();
	o.frame = frame;
	QString error;
	if (!editModelCurveGroup(&next.group, frameCount(), o, &error))
	{
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	stage(next, frame);
}
void ModelAnimationCurvesDialog::capture()
{
	if (m_busy || m_closeAfter || clip() < 0 || !m_capture->isEnabled())
		return;
	const auto source = m_source;
	const int selectedClip = clip();
	const auto joint = m_joint->currentText();
	ModelPoseCurves group;
	QString error;
	m_busy = true;
	m_tabs->setEnabled(false);
	refresh();
	m_status->setText(Text::tr("Capturing sampled motion…"));
	const bool ok = runModelTask(
		this, Text::tr("Capture Animation Curves"), [source, selectedClip, joint, &group](QString *failure, const ModelWorkControl &control)
		{ return captureModelPoseCurves(source, selectedClip, joint, &group, failure, control); }, &error, false, &m_cancel);
	m_busy = false;
	m_tabs->setEnabled(true);
	if (m_closeAfter)
	{
		QDialog::reject();
		return;
	}
	if (!ok)
	{
		refresh();
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	stage({group, false}, 0);
}
void ModelAnimationCurvesDialog::showPreview()
{
	// Clips outside the working set preview one skinned sample at a time.
	int frame = -1;
	m_preview->setMesh(modelSkeletalSamplePreview(m_ready && m_view->currentIndex() == 0 ? m_result : m_source, clip(), m_sample->value(), &frame), true);
	m_preview->setFrame(std::max(0, frame));
	m_preview->setEditJoint(m_joint->currentText());
	m_graph->setCursor(m_sample->value());
}
void ModelAnimationCurvesDialog::review()
{
	if (m_busy || m_closeAfter || draft().group.channels[0].isEmpty())
		return;
	ModelEdit operation;
	operation.kind = ModelEditKind::EditAnimationCurves;
	operation.curves.clip = clip();
	operation.curves.joint = m_joint->currentText();
	operation.curves.action = draft().bake ? ModelCurveAction::Bake : ModelCurveAction::Replace;
	if (!draft().bake)
		operation.curves.group = draft().group;
	const auto source = m_source;
	ModelMesh candidate;
	QString error;
	m_ready = false;
	m_busy = true;
	m_tabs->setEnabled(false);
	refresh();
	m_status->setText(Text::tr("Rebuilding and validating curve motion…"));
	const bool ok = runModelTask(
		this, Text::tr("Review Animation Curves"),
		[source, operation, &candidate](QString *failure, const ModelWorkControl &control)
		{
			candidate = source;
			return applyModelEdit(&candidate, operation, nullptr, failure, control);
		},
		&error, false, &m_cancel);
	m_busy = false;
	m_tabs->setEnabled(true);
	if (m_closeAfter)
	{
		QDialog::reject();
		return;
	}
	if (!ok)
	{
		m_result = m_source;
		refresh();
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_result = std::move(candidate);
	m_reviewed = operation;
	m_ready = true;
	refresh();
	m_status->setText(
		Text::tr("Reviewed samples: %L1. Compare the original and resulting motion, then Apply one document edit.").arg(frameCount()),
		LiveStatus::Result);
}
void ModelAnimationCurvesDialog::reject()
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
