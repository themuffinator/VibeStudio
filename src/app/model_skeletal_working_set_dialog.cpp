#include "app/model_skeletal_working_set_dialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
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
	Q_DECLARE_TR_FUNCTIONS(ModelSkeletalWorkingSetDialog)
};
enum Column
{
	NameColumn,
	FramesColumn,
	CostColumn,
	StateColumn,
};
QString number(qint64 value)
{
	return QLocale().toString(value);
}
int percentOf(const ModelSkeletalWorkingSetPlan &plan)
{
	return plan.budget.frameVertices > 0 ? int(plan.frameVertices * 100 / plan.budget.frameVertices) : 0;
}
QPushButton *button(const QString &text, const char *id, const QString &description)
{
	auto *result = new QPushButton(text);
	result->setObjectName(QString::fromLatin1(id));
	result->setAccessibleName(QString(text).remove(QLatin1Char('&')));
	result->setAccessibleDescription(description);
	result->setToolTip(description);
	return result;
}
} // namespace

QString modelSkeletalWorkingSetSummary(const ModelSkeletalWorkingSetPlan &plan)
{
	// Counts follow labels, so no translation needs a plural form for them.
	return Text::tr("Clips in the working set: %1 of %2 · Frames: %3 of %4 · Frame-vertex budget used: %5%")
		.arg(number(plan.clips.size()), number(plan.costs.size()), number(plan.frames), number(plan.budget.frames), number(percentOf(plan)));
}

void addModelSkeletalClipItem(QComboBox *combo, const ModelMesh &mesh, int clip)
{
	if (mesh.skeleton.clips[clip].frames.isEmpty())
		return;
	const auto &name = mesh.skeleton.clips[clip].name;
	if (mesh.skeleton.bakedClipForFrame.contains(clip))
	{
		combo->addItem(name, clip);
		return;
	}
	combo->addItem(Text::tr("%1 (outside the working set)").arg(name), clip);
	combo->setItemData(combo->count() - 1,
					   Text::tr("This clip has no editable frames. Edits still apply to it; the preview skins one sample at a time."),
					   Qt::ToolTipRole);
}

ModelSkeletalWorkingSetDialog::ModelSkeletalWorkingSetDialog(const ModelMesh &mesh, QWidget *parent) : QDialog(parent), m_mesh(mesh)
{
	setObjectName(QStringLiteral("meshSkeletalWorkingSetDialog"));
	setWindowTitle(Text::tr("Skeletal Working Set"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(Text::tr("Choose the skeletal clips baked into editable frames. Every clip stays in the skeleton; "
									  "pose, curve, key and inverse kinematics tools still edit clips outside the working set."));
	// Everything but the dialog buttons scrolls, so large text and long
	// translations never overlap at small window sizes.
	auto *outer = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("workingSetScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget;
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(0, 0, 0, 0);
	scroll->setWidget(content);
	outer->addWidget(scroll, 1);
	m_summary = new QLabel;
	m_summary->setObjectName(QStringLiteral("workingSetSummary"));
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setAccessibleName(Text::tr("Working set summary"));
	layout->addWidget(m_summary);
	m_budget = new QProgressBar;
	m_budget->setObjectName(QStringLiteral("workingSetBudget"));
	m_budget->setRange(0, 1000);
	m_budget->setTextVisible(true);
	m_budget->setAccessibleName(Text::tr("Frame-vertex budget"));
	layout->addWidget(m_budget);
	m_clips = new QTreeWidget;
	m_clips->setObjectName(QStringLiteral("workingSetClips"));
	m_clips->setAccessibleName(Text::tr("Skeletal clips"));
	m_clips->setAccessibleDescription(Text::tr("Check the clips to bake into editable frames. Space toggles the current clip. Each "
											   "clip costs its frames times the model's vertices."));
	m_clips->setToolTip(m_clips->accessibleDescription());
	m_clips->setHeaderLabels({Text::tr("Clip"), Text::tr("Frames"), Text::tr("Frame vertices"), Text::tr("State")});
	m_clips->setRootIsDecorated(false);
	m_clips->setUniformRowHeights(true);
	m_clips->setAllColumnsShowFocus(true);
	// Names and numbers stay whole; a narrow window scrolls the list instead.
	m_clips->header()->setStretchLastSection(true);
	for (int column : {NameColumn, FramesColumn, CostColumn})
		m_clips->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
	m_clips->setMinimumHeight(200);
	const auto current = modelSkeletalWorkingSet(mesh);
	for (const auto &cost : current.costs)
	{
		auto *item = new QTreeWidgetItem(m_clips);
		// Clip names are technical identifiers: keep them in their own
		// direction inside a right-to-left layout.
		item->setText(NameColumn, QStringLiteral("⁨%1⁩").arg(cost.name));
		item->setText(FramesColumn, number(cost.frames));
		item->setText(CostColumn, number(cost.frameVertices));
		item->setTextAlignment(FramesColumn, Qt::AlignTrailing | Qt::AlignVCenter);
		item->setTextAlignment(CostColumn, Qt::AlignTrailing | Qt::AlignVCenter);
		item->setData(NameColumn, Qt::UserRole, cost.clip);
		if (cost.frames == 0)
			item->setFlags(Qt::ItemIsSelectable);
		else if (!cost.baked && cost.state == ModelSkeletalClipState::TooLarge)
			// Reachable, so its row explains why it cannot be checked.
			item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		else
		{
			item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
			item->setCheckState(NameColumn, cost.baked ? Qt::Checked : Qt::Unchecked);
		}
	}
	if (m_clips->topLevelItemCount() > 0)
		m_clips->setCurrentItem(m_clips->topLevelItem(0));
	layout->addWidget(m_clips, 1);
	auto *actions = new QHBoxLayout;
	// Enter applies (the dialog's default button); a focused action button
	// still takes Enter for itself.
	auto *fit = button(Text::tr("&Fit Automatically"), "workingSetFit",
					   Text::tr("Check every clip that fits three quarters of the budget, in clip order, leaving room for mesh tools."));
	auto *clear = button(Text::tr("&Bind Pose Only"), "workingSetClear",
						 Text::tr("Uncheck every clip, so only the bind pose stays in editable frames."));
	m_showDetails = button(Text::tr("Show &Details"), "workingSetShowDetails", Text::tr("Show the full working set report."));
	m_showDetails->setCheckable(true);
	actions->addWidget(fit);
	actions->addWidget(clear);
	actions->addStretch(1);
	layout->addLayout(actions);
	// The details toggle sits on its own row, right above what it reveals.
	auto *disclosure = new QHBoxLayout;
	disclosure->addWidget(m_showDetails);
	disclosure->addStretch(1);
	layout->addLayout(disclosure);
	m_details = new QLabel;
	m_details->setObjectName(QStringLiteral("workingSetDetails"));
	m_details->setWordWrap(true);
	m_details->setTextFormat(Qt::PlainText);
	m_details->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_details->setAccessibleName(Text::tr("Working set report"));
	m_details->setVisible(false);
	layout->addWidget(m_details);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName(QStringLiteral("applyWorkingSet"));
	m_apply->setAccessibleName(Text::tr("Apply working set"));
	m_apply->setAccessibleDescription(Text::tr("Re-bake the checked clips as one undoable edit. Frames with their own edits are "
											   "protected: a clip that holds them stays until those edits are undone."));
	m_apply->setToolTip(m_apply->accessibleDescription());
	outer->addWidget(buttons);
	// After the button box joins the dialog, so the dialog keeps Apply as its
	// default instead of the first action button.
	m_apply->setDefault(true);
	connect(m_apply, &QPushButton::clicked, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_clips, &QTreeWidget::itemChanged, this, [this] { refresh(); });
	connect(fit, &QPushButton::clicked, this,
			[this] { setCheckedClips(planModelSkeletalWorkingSet(m_mesh, nullptr, modelSkeletalAutomaticBudget()).clips); });
	connect(clear, &QPushButton::clicked, this, [this] { setCheckedClips({}); });
	connect(m_showDetails, &QPushButton::toggled, this, [this](bool shown) {
		m_details->setVisible(shown);
		m_showDetails->setText(shown ? Text::tr("Hide &Details") : Text::tr("Show &Details"));
		m_showDetails->setAccessibleName(QString(m_showDetails->text()).remove(QLatin1Char('&')));
	});
	refresh();
	resize(std::max(640, fontMetrics().averageCharWidth() * 80), 560);
}

QVector<int> ModelSkeletalWorkingSetDialog::checkedClips() const
{
	QVector<int> clips;
	for (int row = 0; row < m_clips->topLevelItemCount(); ++row)
	{
		const auto *item = m_clips->topLevelItem(row);
		if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState(NameColumn) == Qt::Checked)
			clips.append(item->data(NameColumn, Qt::UserRole).toInt());
	}
	return clips;
}

void ModelSkeletalWorkingSetDialog::setCheckedClips(const QVector<int> &clips)
{
	{
		const QSignalBlocker blocker(m_clips);
		for (int row = 0; row < m_clips->topLevelItemCount(); ++row)
		{
			auto *item = m_clips->topLevelItem(row);
			if (item->flags() & Qt::ItemIsUserCheckable)
				item->setCheckState(NameColumn, clips.contains(item->data(NameColumn, Qt::UserRole).toInt()) ? Qt::Checked : Qt::Unchecked);
		}
	}
	refresh();
}

ModelEdit ModelSkeletalWorkingSetDialog::edit() const
{
	ModelEdit result;
	result.kind = ModelEditKind::SetSkeletalWorkingSet;
	result.skeletal.workingSet = checkedClips();
	return result;
}

void ModelSkeletalWorkingSetDialog::refresh()
{
	const auto checked = checkedClips();
	const auto plan = planModelSkeletalWorkingSet(m_mesh, &checked, modelSkeletalEditableBudget());
	{
		const QSignalBlocker blocker(m_clips);
		for (int row = 0; row < m_clips->topLevelItemCount() && row < plan.costs.size(); ++row)
		{
			auto *item = m_clips->topLevelItem(row);
			const auto &cost = plan.costs[row];
			const bool in = cost.state == ModelSkeletalClipState::Baked;
			// Words, not only colour or a check mark, say what Apply changes.
			const QString state = cost.state == ModelSkeletalClipState::Empty	  ? Text::tr("No poses")
								  : in && cost.baked						  ? Text::tr("In working set")
								  : in										  ? Text::tr("Joins working set")
								  : cost.baked								  ? Text::tr("Leaves working set")
								  : cost.state == ModelSkeletalClipState::TooLarge ? Text::tr("Too long to bake")
																			  : Text::tr("Outside");
			item->setText(StateColumn, state);
			const QString detail = cost.state == ModelSkeletalClipState::TooLarge
									   ? Text::tr("%1 needs more frames than the budget holds, so it cannot be checked. Edit it through "
												  "the skeletal tools, which preview one sample at a time; exports still write it.")
											 .arg(cost.name)
								   : cost.state == ModelSkeletalClipState::Empty
									   ? Text::tr("%1 has no poses to bake.").arg(cost.name)
									   : Text::tr("%1. Frames: %2. Cost in frame vertices: %3.").arg(cost.name, number(cost.frames), number(cost.frameVertices));
			for (int column = 0; column < m_clips->columnCount(); ++column)
				item->setToolTip(column, detail);
			item->setData(StateColumn, Qt::AccessibleDescriptionRole, detail);
		}
	}
	const int permille = plan.budget.frameVertices > 0 ? int(std::min<qint64>(1000, plan.frameVertices * 1000 / plan.budget.frameVertices)) : 0;
	m_budget->setValue(permille);
	const QString meter = plan.fits ? Text::tr("Frame vertices: %1 of %2 (%3%)").arg(number(plan.frameVertices), number(plan.budget.frameVertices), number(percentOf(plan)))
									: Text::tr("Over budget. Frame vertices: %1 of %2 (%3%)")
										  .arg(number(plan.frameVertices), number(plan.budget.frameVertices), number(percentOf(plan)));
	m_budget->setFormat(meter);
	m_budget->setAccessibleDescription(meter);
	m_budget->setToolTip(meter);
	QString summary = modelSkeletalWorkingSetSummary(plan);
	if (!plan.fits)
		summary += QLatin1Char('\n') + Text::tr("The checked clips are over the budget. Frames: %1 of %2. Frame vertices: %3 of %4. "
												"Uncheck clips to apply.")
										  .arg(number(plan.frames), number(plan.budget.frames), number(plan.frameVertices),
											   number(plan.budget.frameVertices));
	else
	{
		summary += QLatin1Char('\n') + Text::tr("Vertices mesh tools can still add to every frame: %1.").arg(number(plan.vertexRoom()));
		if (!plan.changes())
			summary += QLatin1Char(' ') + Text::tr("These clips are already the working set.");
	}
	m_summary->setText(summary);
	m_details->setText(modelSkeletalWorkingSetLines(plan, QLocale()).join(QLatin1Char('\n')));
	m_apply->setEnabled(plan.fits && plan.changes());
}
} // namespace vibestudio
