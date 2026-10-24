#include "app/model_uv_density_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"
#include "app/form_row_label_lines.h"
#include "app/model_document_work.h"
#include "app/model_uv_view.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
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
	Q_DECLARE_TR_FUNCTIONS(ModelUvDensityDialog)
};
void accessible(QWidget *w, const char *id, const QString &name, const QString &description)
{
	w->setObjectName(id);
	w->setAccessibleName(name);
	w->setToolTip(description);
	w->setAccessibleDescription(description);
}
void row(QFormLayout *form, const QString &text, QWidget *w)
{
	auto *label = new QLabel(text);
	label->setTextFormat(Qt::PlainText);
	// Wrapped, with the height of its lines kept, at large text sizes.
	keepFormRowLabelLines(label);
	label->setBuddy(w);
	label->setMaximumWidth(420);
	form->addRow(label, w);
}
QString details(const ModelUvDensityIsland &r)
{
	return Text::tr("Island %L1: %Ln face(s); model area %2; pixel area %3; density %4 px/unit; face range %5–%6; collapsed geometry %L7, "
					"collapsed UVs %L8, mirrored faces %L9.", nullptr, int(r.faces.size()))
		.arg(r.island)
		.arg(localeNumber(r.modelArea, 9))
		.arg(localeNumber(r.pixelArea, 9))
		.arg(localeNumber(r.density, 9))
		.arg(localeNumber(r.minimum, 9))
		.arg(localeNumber(r.maximum, 9))
		.arg(r.collapsedGeometry)
		.arg(r.collapsedUvs)
		.arg(r.mirroredFaces);
}
} // namespace
class ModelUvDensityTable final : public QAbstractTableModel
{
  public:
	explicit ModelUvDensityTable(QObject *parent) : QAbstractTableModel(parent) {}
	void setReports(const ModelUvDensityReport &before, const ModelUvDensityReport &after)
	{
		beginResetModel();
		m_before = before;
		m_after = after;
		endResetModel();
	}
	int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_before.islands.size(); }
	int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 6; }
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (orientation != Qt::Horizontal || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
			return {};
		return QStringList{Text::tr("Island"),
						   Text::tr("Faces"),
						   Text::tr("Before (px/unit)"),
						   Text::tr("After (px/unit)"),
						   Text::tr("Face density range"),
						   Text::tr("State")}
			.value(section);
	}
	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() < 0 || index.row() >= m_before.islands.size())
			return {};
		const auto &r = m_before.islands[index.row()];
		if (role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole)
			return details(r) +
				   (index.row() < m_after.islands.size() ? QLatin1Char('\n') + details(m_after.islands[index.row()]) : QString());
		if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole)
			return {};
		switch (index.column())
		{
		case 0:
			return r.island;
		case 1:
			return r.faces.size();
		case 2:
			return localeNumber(r.density, 8);
		case 3:
			return index.row() < m_after.islands.size() ? localeNumber(m_after.islands[index.row()].density, 8)
														: Text::tr("Not previewed");
		case 4:
			return QStringLiteral("%1–%2").arg(localeNumber(r.minimum, 7), localeNumber(r.maximum, 7));
		case 5:
			return r.scalable() ? Text::tr("Ready") : Text::tr("Repair required");
		default:
			return {};
		}
	}

  private:
	ModelUvDensityReport m_before, m_after;
};

ModelUvDensityDialog::ModelUvDensityDialog(const ModelMesh &mesh, const ModelSelection &selection, int frame, const QImage &texture,
										   QSize materialSize, const QString &materialName, QWidget *parent)
	: QDialog(parent), m_source(mesh), m_result(mesh), m_selection(selection), m_texture(texture)
{
	setObjectName("modelUvDensityDialog");
	setWindowTitle(Text::tr("Texel Density"));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	m_tabs = new QTabWidget;
	m_tabs->setUsesScrollButtons(true);
	m_tabs->setElideMode(Qt::ElideRight);
	m_tabs->setObjectName("uvDensityTabs");
	layout->addWidget(m_tabs, 1);
	auto *settings = new QWidget;
	auto *form = new QFormLayout(settings);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	auto *context =
		new QLabel(materialSize.isValid()
					   ? Text::tr("Material: %1 · %L2 × %L3 px.").arg(materialName).arg(materialSize.width()).arg(materialSize.height())
					   : Text::tr("No material dimensions are available. Enter the intended texture size."));
	context->setWordWrap(true);
	context->setTextFormat(Qt::PlainText);
	context->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	accessible(context, "uvDensityMaterial", Text::tr("Texture context"), context->text());
	form->addRow(context);
	m_scope = new QComboBox;
	m_scope->setMinimumContentsLength(16);
	m_scope->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_scope->addItems({Text::tr("Selected islands"), Text::tr("All surface islands")});
	m_scope->setCurrentIndex(selection.faces.isEmpty() && selection.vertices.isEmpty() && selection.edges.isEmpty() ? 1 : 0);
	accessible(m_scope, "uvDensityScope", Text::tr("Island scope"),
			   Text::tr("Selected components expand to whole UV islands, including any hidden faces in those islands."));
	row(form, Text::tr("&Scope"), m_scope);
	const auto integer = [&](const char *id, const QString &name, const QString &description, int low, int high, int value)
	{
		auto *w = new QSpinBox;
		w->setRange(low, high);
		w->setValue(value);
		w->setKeyboardTracking(false);
		w->setLayoutDirection(Qt::LeftToRight);
		accessible(w, id, name, description);
		row(form, name, w);
		return w;
	};
	m_width = integer("uvDensityWidth", Text::tr("Texture &width"), Text::tr("Original texture width in pixels, before preview resizing."),
					  1, 65536, materialSize.isValid() ? materialSize.width() : 512);
	m_height = integer("uvDensityHeight", Text::tr("Texture &height"),
					   Text::tr("Original texture height in pixels. Rectangular textures are measured at their actual dimensions."), 1,
					   65536, materialSize.isValid() ? materialSize.height() : 512);
	auto *useMaterial = new QPushButton(Text::tr("Use material size"));
	accessible(useMaterial, "uvDensityUseMaterial", useMaterial->text(),
			   Text::tr("Restore the original dimensions reported for the resolved material."));
	useMaterial->setEnabled(materialSize.isValid());
	form->addRow(useMaterial);
	connect(useMaterial, &QPushButton::clicked, this,
			[this, materialSize]
			{
				m_width->setValue(materialSize.width());
				m_height->setValue(materialSize.height());
			});
	m_frame = integer("uvDensityFrame", Text::tr("Reference &pose"),
					  Text::tr("Measure model area in this pose. UV adjustments remain shared across every pose."), 0,
					  std::max(0, int(mesh.frames.size()) - 1), frame);
	m_target = new QDoubleSpinBox;
	m_target->setDecimals(6);
	m_target->setRange(.000001, 1000000);
	m_target->setValue(1);
	m_target->setKeyboardTracking(false);
	m_target->setLayoutDirection(Qt::LeftToRight);
	accessible(m_target, "uvDensityTarget", Text::tr("Target pixels per model unit"),
			   Text::tr("Area-based texture density. Uniform scaling preserves each island's shape and orientation."));
	row(form, Text::tr("Target &px/unit"), m_target);
	m_mode = new QComboBox;
	m_mode->setMinimumContentsLength(16);
	m_mode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_mode->addItems({Text::tr("Match each island"), Text::tr("Preserve relative densities")});
	accessible(m_mode, "uvDensityMode", Text::tr("Density adjustment mode"),
			   Text::tr("Match each island to the target, or apply one factor so the combined selection reaches the target while keeping "
						"relative densities. Island centres stay fixed."));
	row(form, Text::tr("&Mode"), m_mode);
	m_useMeasured = new QPushButton(Text::tr("Use measured density"));
	accessible(m_useMeasured, "uvDensityUseMeasured", m_useMeasured->text(),
			   Text::tr("Use the inspected selection's combined density as the target."));
	form->addRow(m_useMeasured);
	connect(m_useMeasured, &QPushButton::clicked, this,
			[this]
			{
				if (!m_before.islands.isEmpty() && m_before.density > 0)
					m_target->setValue(m_before.density);
			});
	auto *scroll = new QScrollArea;
	scroll->setObjectName("uvDensitySettingsScroll");
	scroll->setWidgetResizable(true);
	scroll->setWidget(settings);
	m_tabs->addTab(scroll, Text::tr("&Settings"));
	auto *results = new QWidget;
	auto *resultsLayout = new QVBoxLayout(results);
	m_report = new ModelUvDensityTable(this);
	m_table = new QTableView;
	accessible(m_table, "uvDensityResults", Text::tr("Island density results"),
			   Text::tr("Before and after density for every selected island. Select a row for complete measurements and repair counts."));
	m_table->setModel(m_report);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_table->horizontalHeader()->setResizeContentsPrecision(100);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	resultsLayout->addWidget(m_table, 1);
	m_detail = new QLineEdit;
	m_detail->setReadOnly(true);
	accessible(m_detail, "uvDensityDetails", Text::tr("Selected island details"),
			   Text::tr("Complete measurements; select or copy this text, or move through it with the keyboard."));
	resultsLayout->addWidget(m_detail);
	connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this,
			[this](const QModelIndex &current)
			{
				const auto text = m_report->data(current, Qt::ToolTipRole).toString();
				m_detail->setText(text);
				m_detail->setCursorPosition(0);
				m_detail->setToolTip(text);
				m_detail->setAccessibleDescription(text);
			});
	m_tabs->addTab(results, Text::tr("&Results"));
	auto *view = new QWidget;
	auto *viewLayout = new QVBoxLayout(view);
	m_view = new QComboBox;
	m_view->setMinimumContentsLength(16);
	m_view->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_view->addItems({Text::tr("Preview result"), Text::tr("Original UVs")});
	accessible(m_view, "uvDensityView", Text::tr("UV comparison"),
			   Text::tr("Compare original and previewed UV islands against the same texture."));
	viewLayout->addWidget(m_view);
	m_preview = new ModelUvView;
	accessible(m_preview, "uvDensityPreview", Text::tr("UV density preview"),
			   Text::tr("Inspect island positions and texture coverage before applying. Scaling can create overlaps or move islands beyond "
						"the base tile."));
	m_preview->setMoveEnabled(false);
	viewLayout->addWidget(m_preview, 1);
	connect(m_view, &QComboBox::currentIndexChanged, this, &ModelUvDensityDialog::showPreview);
	m_tabs->addTab(view, Text::tr("&UV preview"));
	m_status = new LiveStatusLabel;
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	accessible(m_status, "uvDensityStatus", Text::tr("Density operation status"), {});
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	m_buttons = buttons;
	buttons->setOrientation(Qt::Vertical);
	buttons->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	buttons->layout()->setSizeConstraint(QLayout::SetNoConstraint);
	m_inspect = buttons->addButton(Text::tr("&Inspect"), QDialogButtonBox::ActionRole);
	m_previewButton = buttons->addButton(Text::tr("&Preview"), QDialogButtonBox::ActionRole);
	m_apply = buttons->addButton(Text::tr("&Apply"), QDialogButtonBox::AcceptRole);
	accessible(m_inspect, "uvDensityInspect", Text::tr("Inspect density"),
			   Text::tr("Measure the chosen islands without changing the model."));
	accessible(m_previewButton, "uvDensityReview", Text::tr("Preview density adjustment"),
			   Text::tr("Prepare and validate the UV result before applying it."));
	accessible(m_apply, "uvDensityApply", Text::tr("Apply reviewed density"),
			   Text::tr("Apply the reviewed UV adjustment as one undo step."));
	connect(m_inspect, &QPushButton::clicked, this, &ModelUvDensityDialog::inspect);
	connect(m_previewButton, &QPushButton::clicked, this, &ModelUvDensityDialog::preview);
	connect(m_apply, &QPushButton::clicked, this, &ModelUvDensityDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelUvDensityDialog::reject);
	layout->addWidget(buttons);
	for (auto *w : {m_width, m_height, m_frame})
		connect(w, &QSpinBox::valueChanged, this, [this] { invalidate(true); });
	connect(m_scope, &QComboBox::currentIndexChanged, this, [this] { invalidate(true); });
	connect(m_target, &QDoubleSpinBox::valueChanged, this, [this] { invalidate(false); });
	connect(m_mode, &QComboBox::currentIndexChanged, this, [this] { invalidate(false); });
	invalidate(true);
	fitDialogToScreen(this, QSize(960, 800));
}
void ModelUvDensityDialog::status(const QString &text)
{
	m_status->setText(text);
	m_status->setToolTip(text);
	m_status->setAccessibleDescription(text);
}
void ModelUvDensityDialog::resizeEvent(QResizeEvent *event)
{
	QDialog::resizeEvent(event);
	if (!m_buttons)
		return;
	int required = 32;
	for (auto *button : m_buttons->buttons())
		required += button->sizeHint().width() + 8;
	m_buttons->setOrientation(width() >= required ? Qt::Horizontal : Qt::Vertical);
}
void ModelUvDensityDialog::invalidate(bool measurement)
{
	if (m_busy)
		return;
	m_ready = false;
	m_result = m_source;
	m_after = {};
	if (measurement)
		m_before = {};
	m_report->setReports(m_before, m_after);
	m_table->resizeColumnsToContents();
	m_detail->clear();
	status(Text::tr("Inspect or preview density."));
	controls();
	showPreview();
}
void ModelUvDensityDialog::controls()
{
	m_tabs->setEnabled(!m_busy);
	m_inspect->setEnabled(!m_busy);
	m_previewButton->setEnabled(!m_busy);
	m_apply->setEnabled(!m_busy && m_ready);
	m_useMeasured->setEnabled(!m_busy && !m_before.islands.isEmpty() && m_before.density >= m_target->minimum() &&
							  m_before.density <= m_target->maximum());
}
void ModelUvDensityDialog::showPreview()
{
	const auto &mesh = m_ready && m_view->currentIndex() == 0 ? m_result : m_source;
	auto selection = m_selection;
	if (!m_before.faces.isEmpty())
		selection = {m_selection.surface, {}, m_before.faces};
	if (selection.surface >= 0 && selection.surface < mesh.surfaces.size())
	{
		// Explicit dimensions also define preview aspect; the material bitmap may
		// have been downsampled, or the user may be targeting a resized texture.
		QSize size(m_width->value(), m_height->value());
		if (size.width() > 1024 || size.height() > 1024)
			size.scale(1024, 1024, Qt::KeepAspectRatio);
		size.setWidth(std::max(1, size.width()));
		size.setHeight(std::max(1, size.height()));
		m_preview->setSource(mesh.surfaces[selection.surface], selection,
							 m_texture.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation));
	}
}
void ModelUvDensityDialog::inspect() { review(false); }
void ModelUvDensityDialog::preview() { review(true); }
void ModelUvDensityDialog::review(bool change)
{
	if (m_busy || m_closing)
		return;
	ModelEdit operation;
	operation.kind = ModelEditKind::SetUvDensity;
	operation.selection = m_selection;
	if (m_scope->currentIndex() == 1 && m_selection.surface >= 0 && m_selection.surface < m_source.surfaces.size())
	{
		operation.selection = {m_selection.surface, {}, {}};
		for (int f = 0; f < m_source.surfaces[m_selection.surface].triangles.size(); ++f)
			operation.selection.faces.insert(f);
	}
	operation.uvDensity = {{m_width->value(), m_height->value()}, m_frame->value(), m_target->value(), m_mode->currentIndex() == 0};
	const auto source = m_source;
	ModelMesh candidate;
	ModelUvDensityReport before, after;
	QString error;
	m_busy = true;
	m_ready = false;
	controls();
	status(Text::tr("Measuring UV islands and validating texture density…"));
	const bool ok = runModelTask(
		this, Text::tr("Texel Density"),
		[source, operation, change, &candidate, &before, &after](QString *failure, const ModelWorkControl &control)
		{
			if (operation.selection.surface < 0 || operation.selection.surface >= source.surfaces.size())
			{
				*failure = Text::tr("Choose an existing surface.");
				return false;
			}
			const auto &surface = source.surfaces[operation.selection.surface];
			ModelUvTopology topology;
			QSet<int> faces;
			if (!buildModelUvTopology(surface, &topology, failure, control) ||
				!expandModelUvIslands(surface, topology, operation.selection.faces, operation.selection.vertices, operation.selection.edges,
									  &faces, failure, control))
				return false;
			if (faces.isEmpty())
			{
				*failure = Text::tr("Select islands or choose All surface islands.");
				return false;
			}
			if (!inspectModelUvDensity(surface, faces, operation.uvDensity.textureSize, operation.uvDensity.referenceFrame, &before,
									   failure, control))
				return false;
			if (!change)
				return true;
			candidate = source;
			return applyModelEdit(&candidate, operation, nullptr, failure, control) &&
				   inspectModelUvDensity(candidate.surfaces[operation.selection.surface], faces, operation.uvDensity.textureSize,
										 operation.uvDensity.referenceFrame, &after, failure, control);
		},
		&error, false, &m_cancel);
	m_busy = false;
	if (m_closing)
	{
		QDialog::reject();
		return;
	}
	m_before = before;
	m_after = ok ? after : ModelUvDensityReport{};
	m_result = ok && change ? std::move(candidate) : m_source;
	m_ready = ok && change;
	if (m_ready)
		m_reviewed = operation;
	m_report->setReports(m_before, m_after);
	m_table->resizeColumnsToContents();
	m_detail->clear();
	if (m_report->rowCount())
		m_table->setCurrentIndex(m_report->index(0, 0));
	status(!ok		? error
		   : change ? Text::tr("Preview ready: %1 → %2 px/unit.").arg(localeNumber(before.density, 8)).arg(localeNumber(after.density, 8))
					: Text::tr("Measured %1 px/unit across %n island(s).", nullptr, before.islands.size()).arg(localeNumber(before.density, 8)));
	controls();
	showPreview();
	m_tabs->setCurrentIndex(1);
}
void ModelUvDensityDialog::accept()
{
	if (m_ready && !m_busy && !m_closing)
		QDialog::accept();
}
void ModelUvDensityDialog::reject()
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
