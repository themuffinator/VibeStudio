#include "app/model_editor_dialog.h"
#include "core/model_surfaces.h"

#include "app/model_recovery_dialog.h"
#include "app/model_recovery_writer.h"
#include "app/model_uv_view.h"
#include "app/model_viewport.h"
#include "core/model_tags.h"
#include "core/model_transform_axes.h"
#include "core/studio_settings.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
class ModelComponentView final : public QTableView
{
  public:
	void setWholeSurfaceMode(bool enabled)
	{
		m_wholeSurfaces = enabled;
		updateSurfaceMinimum();
	}

  protected:
	void changeEvent(QEvent *event) override
	{
		QTableView::changeEvent(event);
		if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LanguageChange)
		{
			resizeColumnsToContents();
			updateSurfaceMinimum();
		}
	}

  private:
	void updateSurfaceMinimum()
	{
		if (!m_wholeSurfaces || !model() || model()->columnCount() < 3)
		{
			setMinimumWidth(0);
			return;
		}
		// Keep identity and both counts visible at large text sizes. Long names
		// and material paths still use elision/tooltips and horizontal scrolling.
		const int nameWidth = std::max(horizontalHeader()->sectionSizeHint(0), fontMetrics().horizontalAdvance(QString(24, 'n')));
		setColumnWidth(0, std::min(columnWidth(0), nameWidth));
		setMinimumWidth(columnWidth(0) + columnWidth(1) + columnWidth(2) + 2 * frameWidth() + style()->pixelMetric(QStyle::PM_ScrollBarExtent));
	}
	bool m_wholeSurfaces = false;
};
// Keep signs and exponents in mathematical order while the table itself follows
// the language's layout direction. Display/accessibility data stays plain text.
class ModelComponentDelegate final : public QStyledItemDelegate
{
  public:
	using QStyledItemDelegate::QStyledItemDelegate;

  protected:
	void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
	{
		QStyledItemDelegate::initStyleOption(option, index);
		option->direction = Qt::LeftToRight;
	}
};
class ModelComponentTable final : public QAbstractTableModel
{
  public:
	explicit ModelComponentTable(QObject *parent) : QAbstractTableModel(parent) {}
	bool show(const ModelMesh &mesh, const ModelSurfaceTopology &topology, int surface, int frame, int mode)
	{
		const auto tags = modelTagNames(mesh);
		QStringList boxes;
		for (const auto &box : mesh.collisionBoxes)
		{
			boxes << box.name;
		}
		const bool sameSurface = surface == m_surface && surface >= 0 && surface < m_mesh.surfaces.size() && surface < mesh.surfaces.size();
		const bool reset = mode != m_mode || m_mesh.surfaces.size() != mesh.surfaces.size() || tags != m_tags || boxes != m_boxes ||
			(mode != 5 && (!sameSurface || m_mesh.surfaces.at(surface).vertexCount != mesh.surfaces.at(surface).vertexCount ||
			m_mesh.surfaces.at(surface).triangles.constData() != mesh.surfaces.at(surface).triangles.constData()));
		const bool dataChanged = frame != m_frame || m_mesh.surfaces.constData() != mesh.surfaces.constData() ||
								 m_mesh.tags.constData() != mesh.tags.constData() ||
								 m_mesh.collisionBoxes.constData() != mesh.collisionBoxes.constData();
		if (reset)
		{
			beginResetModel();
		}
		m_mesh = mesh;
		m_surface = surface;
		m_frame = frame;
		m_mode = mode;
		m_tags = tags;
		m_boxes = boxes;
		if (mode == 2)
		{
			m_edges = topology.edges;
			m_edgeUses = topology.faceUses;
		}
		if (reset)
		{
			endResetModel();
		}
		else if (dataChanged && rowCount() > 0)
		{
			Q_EMIT this->dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
		}
		return reset;
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		if (parent.isValid() || m_surface < 0 || m_surface >= m_mesh.surfaces.size())
		{
			return 0;
		}
		const auto &s = m_mesh.surfaces[m_surface];
		return m_mode == 5 ? m_mesh.surfaces.size() : m_mode == 4	 ? m_boxes.size()
			   : m_mode == 3 ? m_tags.size()
			   : m_mode == 2 ? m_edges.size()
			   : m_mode == 1 ? s.vertexCount
							 : s.triangles.size();
	}
	int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_mode == 2 ? 5 : m_mode == 1 ? 6 : 4; }
	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() >= rowCount() || index.column() >= columnCount() ||
			(role != Qt::DisplayRole && role != Qt::ToolTipRole))
		{
			return {};
		}
		if (m_mode == 5)
		{
			const auto &surface = m_mesh.surfaces[index.row()];
			if (index.column() == 0)
				return surface.name;
			if (index.column() == 1)
				return surface.vertexCount;
			if (index.column() == 2)
				return surface.triangles.size();
			return surface.skinPaths.join(QStringLiteral(", "));
		}
		if (m_mode == 4)
		{
			const auto &box = m_mesh.collisionBoxes[index.row()];
			if (index.column() == 0)
			{
				return box.name;
			}
			ModelCollisionBox pose;
			if (!sampleModelCollisionBox(box, m_frame, m_frame, 0, &pose)) return {};
			const float values[]{pose.centre.x, pose.centre.y, pose.centre.z};
			return QString::number(values[index.column() - 1], 'g', 7);
		}
		if (m_mode == 3)
		{
			const auto tag = findModelTag(m_mesh, m_tags[index.row()], m_frame);
			if (!tag)
			{
				return {};
			}
			if (index.column() == 0)
			{
				return tag->name;
			}
			const float values[]{tag->origin.x, tag->origin.y, tag->origin.z};
			return QString::number(values[index.column() - 1], 'g', 7);
		}
		if (index.column() == 0)
		{
			return index.row();
		}
		const auto &surface = m_mesh.surfaces[m_surface];
		if (m_mode == 1)
		{
			const auto p = surface.frames[m_frame].positions[index.row()];
			const auto uv = surface.texCoords[index.row()];
			const float values[] = {p.x, p.y, p.z, uv.u, uv.v};
			return QString::number(values[index.column() - 1], 'g', 7);
		}
		if (m_mode == 2)
		{
			const auto edge = m_edges[index.row()];
			if (index.column() == 1)
			{
				return edge.first;
			}
			if (index.column() == 2)
			{
				return edge.second;
			}
			if (index.column() == 4)
			{
				return m_edgeUses.value(edge);
			}
			const auto a = surface.frames[m_frame].positions[edge.first], b = surface.frames[m_frame].positions[edge.second];
			return QString::number(std::hypot(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z), 'g', 7);
		}
		const auto t = surface.triangles[index.row()];
		const int values[] = {t.a, t.b, t.c};
		return values[index.column() - 1];
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
		{
			return {};
		}
		if (m_mode == 5)
		{
			const QStringList labels{QCoreApplication::translate("VibeStudioModelEditor", "Surface"),
				QCoreApplication::translate("VibeStudioModelEditor", "Vertices"),
				QCoreApplication::translate("VibeStudioModelEditor", "Faces"),
				QCoreApplication::translate("VibeStudioModelEditor", "Materials")};
			return labels.value(section);
		}
		if (section == 0)
		{
			if (m_mode == 4)
			{
				return QCoreApplication::translate("VibeStudioModelEditor", "Box");
			}
			if (m_mode == 3)
			{
				return QCoreApplication::translate("VibeStudioModelEditor", "Tag");
			}
			return m_mode == 2	 ? QCoreApplication::translate("VibeStudioModelEditor", "Edge")
				   : m_mode == 1 ? QCoreApplication::translate("VibeStudioModelEditor", "Vertex")
								 : QCoreApplication::translate("VibeStudioModelEditor", "Face");
		}
		const QStringList axes{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"), QStringLiteral("U"), QStringLiteral("V")};
		if (m_mode == 2 && section == 3)
		{
			return QCoreApplication::translate("VibeStudioModelEditor", "Length");
		}
		if (m_mode == 2 && section == 4)
		{
			return QCoreApplication::translate("VibeStudioModelEditor", "Faces");
		}
		return m_mode == 1 || m_mode >= 3 ? axes.value(section - 1) : QString(QChar('A' + section - 1));
	}

	ModelEdge edge(int row) const { return m_edges.value(row, {-1, -1}); }
	QString tag(int row) const { return m_tags.value(row); }
	int tagRow(const QString &name) const { return m_tags.indexOf(name); }
	QString collision(int row) const { return m_boxes.value(row); }
	int collisionRow(const QString &name) const { return m_boxes.indexOf(name); }
	int edgeRow(ModelEdge value) const
	{
		const auto found = std::lower_bound(m_edges.cbegin(), m_edges.cend(), value);
		return found != m_edges.cend() && *found == value ? int(found - m_edges.cbegin()) : -1;
	}

  private:
	ModelMesh m_mesh;
	int m_surface = -1, m_frame = 0;
	int m_mode = 0;
	QVector<ModelEdge> m_edges;
	QStringList m_tags;
	QStringList m_boxes;
	QHash<ModelEdge, int> m_edgeUses;
};

ModelEditorDialog::ModelEditorDialog(QWidget *parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("modelEditorDialog"));
	setWindowTitle(QCoreApplication::translate("VibeStudioModelEditor", "Mesh Editor"));
	setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Model mesh editor"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(1280, 850);
	auto *layout = new QVBoxLayout(this);
	auto *toolbar = new QToolBar(QCoreApplication::translate("VibeStudioModelEditor", "Mesh document actions"));
	m_toolbar = toolbar;
	toolbar->setObjectName(QStringLiteral("meshDocumentActions"));
	toolbar->setAccessibleName(toolbar->windowTitle());
	toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
	const auto action = [&](const QString &label, const QString &name, auto callback)
	{
		auto *result = toolbar->addAction(label);
		result->setObjectName(name);
		connect(result, &QAction::triggered, this,
				[this, callback]()
				{
					if (!m_working)
					{
						callback();
					}
				});
		return result;
	};
	action(QCoreApplication::translate("VibeStudioModelEditor", "Open / Import…"), QStringLiteral("openMesh"), [this]() { open(); });
	action(QCoreApplication::translate("VibeStudioModelImportRepair", "Repair Import…"), QStringLiteral("repairImportMesh"), [this]() { chooseRepairImport(); });
	auto *saveAction =
		action(QCoreApplication::translate("VibeStudioModelEditor", "Save"), QStringLiteral("saveMesh"), [this]() { save(); });
	saveAction->setShortcut(QKeySequence::Save);
	saveAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(saveAction);
	action(QCoreApplication::translate("VibeStudioModelEditor", "Save As…"), QStringLiteral("saveMeshAs"), [this]() { save(true); });
	action(QCoreApplication::translate("VibeStudioModelEditor", "Recover…"), QStringLiteral("recoverMesh"), [this]() { chooseRecovery(); });
	toolbar->addSeparator();
	m_undo = action(QCoreApplication::translate("VibeStudioModelEditor", "Undo"), QStringLiteral("undoMesh"),
					[this]()
					{
						if (m_document.undo())
						{
							refresh();
						}
					});
	m_redo = action(QCoreApplication::translate("VibeStudioModelEditor", "Redo"), QStringLiteral("redoMesh"),
					[this]()
					{
						if (m_document.redo())
						{
							refresh();
						}
					});
	m_undo->setShortcut(QKeySequence::Undo);
	m_redo->setShortcut(QKeySequence::Redo);
	for (auto *a : {m_undo, m_redo})
	{
		a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		addAction(a);
	}
	toolbar->addSeparator();
	action(QCoreApplication::translate("VibeStudioModelEditor", "Export MDL…"), QStringLiteral("exportMeshMdl"),
		   [this]() { exportModel(QStringLiteral("mdl")); });
	action(QCoreApplication::translate("VibeStudioModelEditor", "Export MD2…"), QStringLiteral("exportMeshMd2"),
		   [this]() { exportModel(QStringLiteral("md2")); });
	action(QCoreApplication::translate("VibeStudioModelEditor", "Export MD3…"), QStringLiteral("exportMeshMd3"),
		   [this]() { exportModel(QStringLiteral("md3")); });
	action(QCoreApplication::translate("VibeStudioModelEditor", "Export OBJ Frame…"), QStringLiteral("exportMeshObj"),
		   [this]() { exportModel(QStringLiteral("obj")); });
	m_stage = action(QCoreApplication::translate("VibeStudioModelEditor", "Stage in Package"), QStringLiteral("stageMesh"),
					 [this]() { stage(false); });
	m_place = action(QCoreApplication::translate("VibeStudioModelEditor", "Stage and Place"), QStringLiteral("placeMesh"),
					 [this]() { stage(true); });
	layout->addWidget(toolbar);
	m_context = new QLabel;
	m_context->setWordWrap(true);
	m_context->setTextFormat(Qt::PlainText);
	m_context->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Model project context"));
	layout->addWidget(m_context);
	auto *splitter = new QSplitter;
	m_editingControls = splitter;
	splitter->setObjectName(QStringLiteral("meshEditingControls"));
	auto *components = new QWidget;
	auto *componentLayout = new QVBoxLayout(components);
	componentLayout->setContentsMargins(0, 0, 0, 0);
	m_surface = new QComboBox;
	m_surface->setObjectName(QStringLiteral("meshSurface"));
	m_surface->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Model surface"));
	m_surface->setAccessibleDescription(QCoreApplication::translate("VibeStudioModelEditor", "Active surface for materials and UV inspection. In Surfaces mode, choosing a surface also adds it to the selection."));
	m_surface->setToolTip(m_surface->accessibleDescription());
	m_selectionMode = new QComboBox;
	m_selectionMode->setObjectName(QStringLiteral("meshSelectionMode"));
	m_selectionMode->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Component selection mode"));
	m_selectionMode->addItems(
		{QCoreApplication::translate("VibeStudioModelEditor", "Faces"), QCoreApplication::translate("VibeStudioModelEditor", "Vertices"),
		 QCoreApplication::translate("VibeStudioModelEditor", "Edges"), QCoreApplication::translate("VibeStudioModelEditor", "Tags"),
		 QCoreApplication::translate("VibeStudioModelEditor", "Collision"), QCoreApplication::translate("VibeStudioModelEditor", "Surfaces")});
	componentLayout->addWidget(m_surface);
	componentLayout->addWidget(m_selectionMode);
	m_table = new ModelComponentView;
	m_table->setObjectName(QStringLiteral("meshComponents"));
	m_table->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Mesh components"));
	m_table->setAccessibleDescription(QCoreApplication::translate(
		"VibeStudioModelEditor", "Select rows to edit faces, vertices, edges, tags, collision boxes, or whole surfaces. Shift extends a range; Control "
								 "adds or removes rows. Tags and Collision modes select one named item."));
	m_components = new ModelComponentTable(this);
	m_table->setModel(m_components);
	m_table->setItemDelegate(new ModelComponentDelegate(m_table));
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->verticalHeader()->hide();
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_table->horizontalHeader()->setResizeContentsPrecision(64);
	// Selection belongs to rows. Keep the header's visual selection separate
	// so repainting labels never traverses every selected component column.
	m_table->horizontalHeader()->setHighlightSections(false);
	m_table->horizontalHeader()->setSelectionModel(new QItemSelectionModel(m_components, m_table->horizontalHeader()));
	m_table->horizontalHeader()->setStretchLastSection(true);
	componentLayout->addWidget(m_table, 1);
	auto *selectAll = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Select All"));
	selectAll->setObjectName(QStringLiteral("selectAllMeshComponents"));
	connect(selectAll, &QPushButton::clicked, m_table, &QTableView::selectAll);
	componentLayout->addWidget(selectAll);
	splitter->addWidget(components);
	auto *centre = new QWidget;
	auto *centreLayout = new QVBoxLayout(centre);
	centreLayout->setContentsMargins(0, 0, 0, 0);
	auto *views = new QToolBar;
	views->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Mesh view controls"));
	m_renderMode = new QComboBox;
	m_renderMode->setObjectName(QStringLiteral("meshRenderMode"));
	m_renderMode->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Mesh rendering"));
	m_renderMode->addItems({QCoreApplication::translate("VibeStudioModelEditor", "Solid"),
							QCoreApplication::translate("VibeStudioModelEditor", "Wireframe"),
							QCoreApplication::translate("VibeStudioModelEditor", "UV Checker"),
							QCoreApplication::translate("VibeStudioModelEditor", "Material")});
	m_renderMode->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
														 "Material mode loads each surface's image from the staged package. "
														 "Missing images use the checker; shader effects are not rendered."));
	views->addWidget(m_renderMode);
	m_viewPreset = new QComboBox;
	m_viewPreset->setObjectName(QStringLiteral("meshViewPreset"));
	m_viewPreset->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "View preset"));
	m_viewPreset->addItems({QCoreApplication::translate("VibeStudioModelEditor", "Orbit"),
							QCoreApplication::translate("VibeStudioModelEditor", "Top (XY)"),
							QCoreApplication::translate("VibeStudioModelEditor", "Front (XZ)"),
							QCoreApplication::translate("VibeStudioModelEditor", "Side (YZ)"),
							QCoreApplication::translate("VibeStudioModelEditor", "Perspective")});
	views->addWidget(m_viewPreset);
	auto *frameView = views->addAction(QCoreApplication::translate("VibeStudioModelEditor", "Frame Model"));
	centreLayout->addWidget(views);
	auto *moveRow = new QWidget;
	auto *transformRows = new QVBoxLayout(moveRow);
	transformRows->setContentsMargins(0, 0, 0, 0);
	auto *moveTools = new QHBoxLayout;
	transformRows->addLayout(moveTools);
	moveTools->setContentsMargins(0, 0, 0, 0);
	m_moveGizmo = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Gizmo"));
	m_moveGizmo->setObjectName(QStringLiteral("meshMoveGizmo"));
	m_moveGizmo->setChecked(true);
	m_moveGizmo->setAccessibleName(m_moveGizmo->text());
	m_moveGizmo->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Show handles for the selected transform tool. Release validates one undo step; Escape cancels. Geometry "
								 "provides the same transforms and pivot controls for keyboard use."));
	moveTools->addWidget(m_moveGizmo);
	m_transformTool = new QComboBox;
	m_transformTool->setObjectName(QStringLiteral("meshTransformTool"));
	m_transformTool->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Viewport transform tool"));
	m_transformTool->addItems({QCoreApplication::translate("VibeStudioModelEditor", "Move"),
							   QCoreApplication::translate("VibeStudioModelEditor", "Rotate"),
							   QCoreApplication::translate("VibeStudioModelEditor", "Scale")});
	m_transformTool->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
															"Move with arrows, rotate with rings, or scale with boxes. In Rotate, drag the Free centre "
															"or empty space inside the dashed circle for trackball rotation. The centre moves in the view "
															"plane in Move, or scales uniformly in Scale. Pivots are set in Geometry."));
	moveTools->addWidget(m_transformTool);
	m_snapTranslation = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Snap"));
	m_snapTranslation->setObjectName(QStringLiteral("meshSnapTranslation"));
	m_snapTranslation->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Snap transforms"));
	m_snapTranslation->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Snap move offsets, rotation angles, and scale factors using each tool's step. The shown step belongs to "
								 "the selected tool. Free rotation snaps its total angle while retaining its axis. Apply Transform "
								 "snaps the numeric XYZ angles separately and uses all three steps; scale snaps relative to 1."));
	moveTools->addWidget(m_snapTranslation);
	m_translationGrid = new QDoubleSpinBox;
	m_translationGrid->setObjectName(QStringLiteral("meshTranslationGrid"));
	m_translationGrid->setDecimals(6);
	m_translationGrid->setLayoutDirection(Qt::LeftToRight);
	m_translationGrid->setRange(0.000001, 1000000);
	m_translationGrid->setValue(1);
	m_translationGrid->setEnabled(false);
	m_translationGrid->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Translation grid step in model units"));
	moveTools->addWidget(m_translationGrid);
	m_rotationGrid = new QDoubleSpinBox;
	m_rotationGrid->setObjectName(QStringLiteral("meshRotationGrid"));
	m_rotationGrid->setDecimals(6);
	m_rotationGrid->setLayoutDirection(Qt::LeftToRight);
	m_rotationGrid->setRange(0.000001, 180);
	m_rotationGrid->setValue(15);
	m_rotationGrid->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Rotation snap step in degrees"));
	m_rotationGrid->setSuffix(QStringLiteral("°"));
	moveTools->addWidget(m_rotationGrid);
	m_scaleGrid = new QDoubleSpinBox;
	m_scaleGrid->setObjectName(QStringLiteral("meshScaleGrid"));
	m_scaleGrid->setDecimals(6);
	m_scaleGrid->setLayoutDirection(Qt::LeftToRight);
	m_scaleGrid->setRange(0.000001, 10000);
	m_scaleGrid->setValue(0.1);
	m_scaleGrid->setSingleStep(0.1);
	m_scaleGrid->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Scale snap step relative to one"));
	m_scaleGrid->setSuffix(QStringLiteral("×"));
	moveTools->addWidget(m_scaleGrid);
	m_xrayVertices = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "X-ray Vertices"));
	m_xrayVertices->setObjectName(QStringLiteral("meshXrayVertices"));
	m_xrayVertices->setAccessibleName(m_xrayVertices->text());
	m_xrayVertices->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
														   "Show and select hidden vertices on the active surface. Hidden unselected "
														   "points use hollow dotted markers. Wireframe always shows through the mesh."));
	auto *overlayOptions = new QHBoxLayout;
	overlayOptions->addWidget(m_xrayVertices);
	m_showTags = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Show Tags"));
	m_showTags->setObjectName(QStringLiteral("meshShowTags"));
	m_showTags->setAccessibleName(m_showTags->text());
	m_showTags->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Show named attachment origins and labelled local axes through the model. Select Tags mode to pick and move or rotate a tag."));
	m_showTags->setChecked(true);
	overlayOptions->addWidget(m_showTags);
	overlayOptions->addStretch();
	transformRows->addLayout(overlayOptions);
	moveTools->addStretch();
	centreLayout->addWidget(moveRow);
	m_materialRow = new QWidget;
	m_materialRow->setObjectName(QStringLiteral("meshMaterialState"));
	auto *materialRow = new QVBoxLayout(m_materialRow);
	materialRow->setContentsMargins(0, 0, 0, 0);
	m_materialStatus = new QLabel;
	m_materialStatus->setObjectName(QStringLiteral("meshMaterialStatus"));
	m_materialStatus->setTextFormat(Qt::PlainText);
	m_materialStatus->setWordWrap(true);
	materialRow->addWidget(m_materialStatus);
	m_materialProgress = new QProgressBar;
	m_materialProgress->setObjectName(QStringLiteral("meshMaterialProgress"));
	m_materialProgress->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Material image loading"));
	materialRow->addWidget(m_materialProgress);
	auto *materialActions = new QHBoxLayout;
	auto *reload = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Reload Images"));
	reload->setObjectName(QStringLiteral("reloadMeshMaterials"));
	reload->setToolTip(QCoreApplication::translate("VibeStudioModelEditor", "Reload material images from the current package snapshot."));
	connect(reload, &QPushButton::clicked, this, [this] { reloadMaterials(); });
	materialActions->addWidget(reload);
	m_materialCancel = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Cancel"));
	m_materialCancel->setObjectName(QStringLiteral("cancelMeshMaterials"));
	connect(m_materialCancel, &QPushButton::clicked, this, [this] { m_materialLoader->cancel(); });
	materialActions->addWidget(m_materialCancel);
	auto *details = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Details…"));
	details->setObjectName(QStringLiteral("meshMaterialDetails"));
	connect(details, &QPushButton::clicked, this, [this] { showMaterialDetails(); });
	materialActions->addWidget(details);
	materialActions->addStretch();
	materialRow->addLayout(materialActions);
	centreLayout->addWidget(m_materialRow);
	auto *viewTabs = new QTabWidget;
	viewTabs->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Model view"));
	m_preview = new ModelViewport;
	m_preview->setObjectName(QStringLiteral("meshPreview"));
	m_preview->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Editable model preview"));
	m_preview->setBackfaceCulling(false);
	m_preview->setControlsHelp(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Click a face, edge, or vertex in the chosen component mode; Control-click toggles it. Vertex picks use the active surface. "
		"Drag the selected transform handles; Escape cancels. Drag elsewhere to orbit. Use the component table and Geometry controls "
		"for keyboard editing."));
	viewTabs->addTab(m_preview, QCoreApplication::translate("VibeStudioModelEditor", "3D"));
	viewTabs->addTab(createUvView(), QCoreApplication::translate("VibeStudioModelEditor", "UV"));
	connect(viewTabs, &QTabWidget::currentChanged, this,
			[this, moveRow, frameView](int index)
			{
				const bool modelView = index == 0;
				moveRow->setVisible(modelView);
				m_viewPreset->setEnabled(modelView);
				frameView->setEnabled(modelView);
				if (!modelView)
				{
					m_preview->pause();
				}
			});
	centreLayout->addWidget(viewTabs, 1);
	auto *timeline = new QToolBar;
	timeline->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Animation transport"));
	m_frame = new QComboBox;
	m_frame->setObjectName(QStringLiteral("meshFrame"));
	m_frame->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Animation frame"));
	timeline->addWidget(m_frame);
	auto *play = timeline->addAction(QCoreApplication::translate("VibeStudioModelEditor", "Play / Pause"));
	connect(play, &QAction::triggered, m_preview, &ModelViewport::togglePlayback);
	centreLayout->addWidget(timeline);
	splitter->addWidget(centre);
	auto *tabs = new QTabWidget;
	tabs->setObjectName(QStringLiteral("meshInspector"));
	tabs->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Mesh properties"));
	const auto page = [&](const QString &label)
	{
		auto *contents = new QWidget;
		auto *form = new QFormLayout(contents);
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		form->setRowWrapPolicy(QFormLayout::WrapAllRows);
		auto *scroll = new QScrollArea;
		scroll->setWidgetResizable(true);
		scroll->setWidget(contents);
		scroll->setFrameShape(QFrame::NoFrame);
		tabs->addTab(scroll, label);
		return form;
	};
	const auto values =
		[&](QFormLayout *form, const QString &label, const QString &name, QDoubleSpinBox **fields, int count, double initial)
	{
		auto *row = new QWidget;
		auto *rowLayout = new QVBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		const QStringList axes = count == 2 ? QStringList{QStringLiteral("U"), QStringLiteral("V")}
											: QStringList{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
		for (int i = 0; i < count; ++i)
		{
			auto *column = new QWidget;
			auto *columnLayout = new QHBoxLayout(column);
			columnLayout->setContentsMargins(0, 0, 0, 0);
			columnLayout->setSpacing(2);
			auto *spin = new QDoubleSpinBox;
			spin->setDecimals(6);
			spin->setLayoutDirection(Qt::LeftToRight);
			spin->setRange(-10000, 10000);
			spin->setValue(initial);
			spin->setObjectName(name + QString::number(i));
			spin->setAccessibleName(label + QLatin1Char(' ') + axes[i]);
			spin->setMinimumWidth(0);
			spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			auto *axis = new QLabel(axes[i]);
			axis->setBuddy(spin);
			columnLayout->addWidget(axis);
			columnLayout->addWidget(spin);
			rowLayout->addWidget(column);
			fields[i] = spin;
		}
		form->addRow(label, row);
		return row;
	};
	const auto button = [&](QFormLayout *form, const QString &label, const QString &name, ModelEditKind kind)
	{
		auto *control = new QPushButton(label);
		control->setObjectName(name);
		control->setProperty("requiresMeshComponents", kind != ModelEditKind::Transform && kind != ModelEditKind::SetMaterial &&
			kind != ModelEditKind::SetMd2SkinSize && kind != ModelEditKind::DuplicateFrame && kind != ModelEditKind::DeleteFrame && kind != ModelEditKind::RenameFrame);
		connect(control, &QPushButton::clicked, this, [this, kind]() { execute(kind); });
		form->addRow(control);
		return control;
	};
	auto *geometry = page(QCoreApplication::translate("VibeStudioModelEditor", "Geometry"));
	m_frameScope = new QComboBox;
	m_frameScope->setObjectName(QStringLiteral("meshFrameScope"));
	m_frameScope->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Transform frame scope"));
	m_frameScope->addItems({QCoreApplication::translate("VibeStudioModelEditor", "All frames"),
							QCoreApplication::translate("VibeStudioModelEditor", "Current frame")});
	geometry->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Apply to"), m_frameScope);
	m_transformSpace = new QComboBox;
	m_transformSpace->setObjectName(QStringLiteral("meshTransformSpace"));
	m_transformSpace->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Transform axes"));
	m_transformSpace->addItems({QCoreApplication::translate("VibeStudioModelEditor", "World"),
		QCoreApplication::translate("VibeStudioModelEditor", "Selection"), QCoreApplication::translate("VibeStudioModelEditor", "Custom")});
	geometry->addRow(m_transformSpace->accessibleName(), m_transformSpace);
	auto *customAxesRow = values(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Custom axes"),
		QStringLiteral("meshAxisRotation"), m_axisRotation, 3, 0);
	customAxesRow->setObjectName(QStringLiteral("meshCustomAxesFields"));
	for (auto *field : m_axisRotation)
	{
		field->setRange(-1000000, 1000000);
		field->setAccessibleDescription(QCoreApplication::translate("VibeStudioModelEditor",
			"Orient the transform axes with XYZ angles in degrees. The pivot remains in model coordinates."));
		field->setToolTip(field->accessibleDescription());
	}
	values(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Offset"), QStringLiteral("meshOffset"), m_translation, 3, 0);
	values(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Rotation"), QStringLiteral("meshRotation"), m_rotation, 3, 0);
	values(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Scale"), QStringLiteral("meshScale"), m_scale, 3, 1);
	m_pivotMode = new QComboBox;
	m_pivotMode->setObjectName(QStringLiteral("meshTransformPivotMode"));
	m_pivotMode->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Transform pivot"));
	m_pivotMode->addItems({QCoreApplication::translate("VibeStudioModelEditor", "Model origin"),
						   QCoreApplication::translate("VibeStudioModelEditor", "Selection centre"),
						   QCoreApplication::translate("VibeStudioModelEditor", "Custom")});
	m_pivotMode->setCurrentIndex(1);
	m_pivotMode->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
														"Use the selected bounds in the displayed pose, the model origin, or a custom "
														"point. An all-frame transform uses that same fixed pivot in every pose."));
	geometry->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Pivot"), m_pivotMode);
	auto *customPivotRow =
		values(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Custom pivot"), QStringLiteral("meshPivot"), m_pivot, 3, 0);
	customPivotRow->setObjectName(QStringLiteral("meshCustomPivotFields"));
	button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Apply Transform"), QStringLiteral("transformMesh"),
		   ModelEditKind::Transform);
	auto *extrude = button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Extrude Faces"), QStringLiteral("extrudeMesh"),
						   ModelEditKind::Extrude);
	extrude->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
													"Extrude selected faces by Offset along the chosen axes in every frame. Existing shared edges "
													"define the region boundary."));
	button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Subdivide Faces"), QStringLiteral("subdivideMesh"),
		   ModelEditKind::Subdivide);
	auto *splitEdges = button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Split Edges"),
							  QStringLiteral("splitMeshEdges"), ModelEditKind::SplitEdges);
	splitEdges->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Split selected edges at their midpoint in every frame. Shared neighbours split together; UV seams stay separate."));
	auto *fillBoundary = button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Fill Boundary Loops"),
							   QStringLiteral("fillMeshBoundaryLoops"), ModelEditKind::FillBoundaryLoops);
	fillBoundary->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Fill selected boundary loops"));
	fillBoundary->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
		"Select at least one boundary edge per hole. Fill each complete loop using the displayed pose, checking every frame. "
		"New faces stay selected; existing UVs and normals are retained for further editing."));
	fillBoundary->setAccessibleDescription(fillBoundary->toolTip());
	m_bridgeTwist = new QSpinBox;
	m_bridgeTwist->setObjectName(QStringLiteral("meshBridgeTwist"));
	m_bridgeTwist->setRange(-1023, 1023);
	m_bridgeTwist->setLayoutDirection(Qt::LeftToRight);
	m_bridgeTwist->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Bridge twist"));
	m_bridgeTwist->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
		"Advance the second boundary's starting vertex from the closest pair in the displayed pose. Zero uses automatic alignment. "
		"Positive and negative steps wrap around the aligned second loop."));
	m_bridgeTwist->setAccessibleDescription(m_bridgeTwist->toolTip());
	geometry->addRow(m_bridgeTwist->accessibleName(), m_bridgeTwist);
	auto *bridgeBoundary = button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Bridge Boundary Loops"),
		QStringLiteral("bridgeMeshBoundaryLoops"), ModelEditKind::BridgeBoundaryLoops);
	bridgeBoundary->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Bridge selected boundary loops"));
	bridgeBoundary->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
		"Select edges from exactly two closed boundaries on one surface. Join unequal or equal loops with triangles chosen in the displayed pose. "
		"Every pose is checked before one undo step. New faces stay selected for UV and normal finishing."));
	bridgeBoundary->setAccessibleDescription(bridgeBoundary->toolTip());
	m_weldDistance = new QDoubleSpinBox;
	m_weldDistance->setObjectName(QStringLiteral("meshWeldDistance"));
	m_weldDistance->setDecimals(6);
	m_weldDistance->setRange(0, 1000000);
	m_weldDistance->setValue(0.001);
	m_weldDistance->setSingleStep(0.01);
	m_weldDistance->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Weld distance in model units"));
	geometry->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Weld Distance"), m_weldDistance);
	m_preserveSeams = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Preserve Seams"));
	m_preserveSeams->setObjectName(QStringLiteral("meshPreserveSeams"));
	m_preserveSeams->setChecked(true);
	m_preserveSeams->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Preserve UV and normal seams when welding"));
	m_preserveSeams->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
															"When enabled, only matching UVs and normals can merge. When disabled, merged "
															"vertices use the lowest vertex index's UVs and normals."));
	geometry->addRow(m_preserveSeams);
	auto *weld = button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Weld by Distance"),
						QStringLiteral("weldMeshVertices"), ModelEditKind::WeldVertices);
	weld->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
												 "Merge selected vertices only when they stay within the distance in every "
												 "frame. Keep the lowest compatible vertex index and remove collapsed faces."));

	button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Duplicate Faces"), QStringLiteral("duplicateMeshFaces"),
		   ModelEditKind::DuplicateFaces);
	button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Delete Faces"), QStringLiteral("deleteMeshFaces"),
		   ModelEditKind::DeleteFaces);
	button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Flip Faces"), QStringLiteral("flipMeshFaces"),
		   ModelEditKind::FlipFaces);
	button(geometry, QCoreApplication::translate("VibeStudioModelEditor", "Recalculate Normals"), QStringLiteral("recalculateMeshNormals"),
		   ModelEditKind::RecalculateNormals);
	auto *surface = page(QCoreApplication::translate("VibeStudioModelEditor", "Surface"));
	addMaterialSlotControls(surface);
	m_material = new QLineEdit;
	m_material->setObjectName(QStringLiteral("meshMaterial"));
	m_material->setLayoutDirection(Qt::LeftToRight);
	m_material->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Surface material path"));
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Material"), m_material);
	m_assignMaterial = button(surface, QCoreApplication::translate("VibeStudioModelEditor", "Assign Material"), QStringLiteral("assignMeshMaterial"),
		   ModelEditKind::SetMaterial);
	auto *show = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Show Material"));
	m_showMaterial = show;
	show->setObjectName("showMeshMaterial");
	show->setAccessibleName(show->text());
	connect(show, &QPushButton::clicked, this,
			[this]()
			{
				if (showMaterial)
				{
					showMaterial(m_material->text());
				}
			});
	surface->addRow(show);
	addSurfaceControls(surface);
	addSkinBindingControls(surface);
	values(surface, QCoreApplication::translate("VibeStudioModelEditor", "UV Scale"), QStringLiteral("meshUvScale"), m_uvScale, 2, 1);
	values(surface, QCoreApplication::translate("VibeStudioModelEditor", "UV Offset"), QStringLiteral("meshUvOffset"), m_uvOffset, 2, 0);
	m_uvRotation = new QDoubleSpinBox;
	m_uvRotation->setObjectName(QStringLiteral("meshUvRotation"));
	m_uvRotation->setRange(-36000, 36000);
	m_uvRotation->setDecimals(4);
	m_uvRotation->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV rotation"));
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "UV Rotation"), m_uvRotation);
	addUvControls(surface);
	button(surface, QCoreApplication::translate("VibeStudioModelEditor", "Transform UVs"), QStringLiteral("transformMeshUvs"),
		   ModelEditKind::TransformUv);
	m_projection = new QComboBox;
	m_projection->addItems({QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YZ")});
	m_projection->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV projection plane"));
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Projection"), m_projection);
	auto *project = button(surface, QCoreApplication::translate("VibeStudioModelEditor", "Project UVs"), QStringLiteral("projectMeshUvs"),
						   ModelEditKind::ProjectUv);
	project->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
													"Project model units onto the chosen plane, then apply UV Scale, "
													"Rotation, and Offset. One tile per 64 units uses a scale of 0.015625."));
	auto *animation = page(QCoreApplication::translate("VibeStudioModelEditor", "Animation"));
	m_frameName = new QLineEdit;
	m_frameName->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Frame name"));
	animation->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Frame name"), m_frameName);
	button(animation, QCoreApplication::translate("VibeStudioModelEditor", "Rename Frame"), QStringLiteral("renameMeshFrame"),
		   ModelEditKind::RenameFrame);
	button(animation, QCoreApplication::translate("VibeStudioModelEditor", "Duplicate Frame"), QStringLiteral("duplicateMeshFrame"),
		   ModelEditKind::DuplicateFrame);
	button(animation, QCoreApplication::translate("VibeStudioModelEditor", "Delete Frame"), QStringLiteral("deleteMeshFrame"),
		   ModelEditKind::DeleteFrame);
	addAnimationControls(animation);
	addTagControls(animation);
	addCollisionControls(page(QCoreApplication::translate("VibeStudioModelEditor", "Collision")));
	addMdlControls(page(QCoreApplication::translate("VibeStudioModelEditor", "Quake MDL")));
	auto *handoffPage = page(QCoreApplication::translate("VibeStudioModelEditor", "Handoff"));
	m_md2Width = new QSpinBox;
	m_md2Height = new QSpinBox;
	m_md2Width->setObjectName(QStringLiteral("meshMd2Width"));
	m_md2Height->setObjectName(QStringLiteral("meshMd2Height"));
	m_md2Width->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "MD2 skin width"));
	m_md2Height->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "MD2 skin height"));
	for (auto *field : {m_md2Width, m_md2Height})
	{
		field->setRange(1, 8192);
		field->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
													  "Match the external PCX skin. Original Quake II export supports at most "
													  "640 by 480 pixels. Apply to save these dimensions with the mesh."));
		field->setAccessibleDescription(field->toolTip());
	}
	handoffPage->addRow(QCoreApplication::translate("VibeStudioModelEditor", "MD2 skin width"), m_md2Width);
	handoffPage->addRow(QCoreApplication::translate("VibeStudioModelEditor", "MD2 skin height"), m_md2Height);
	button(handoffPage, QCoreApplication::translate("VibeStudioModelEditor", "Apply Skin Size"), QStringLiteral("applyMeshMd2Size"),
		   ModelEditKind::SetMd2SkinSize);
	m_virtualPath = new QLineEdit(QStringLiteral("models/props/edited.md3"));
	m_virtualPath->setObjectName(QStringLiteral("meshVirtualPath"));
	m_virtualPath->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Model package path"));
	m_virtualPath->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Use .mdl for Quake, .md2 for Quake II or .md3 for Quake III. Export limits apply when staging. "
								 "The editable source retains authoring metadata. Level placement currently requires MD3."));
	connect(m_virtualPath, &QLineEdit::textChanged, this, [this] { refreshContext(); });
	handoffPage->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Package path"), m_virtualPath);
	m_replace = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Replace existing entry"));
	handoffPage->addRow(m_replace);
	values(handoffPage, QCoreApplication::translate("VibeStudioModelEditor", "Level origin"), QStringLiteral("meshPlacement"), m_placement,
		   3, 0);
	addHealthControls(page(QCoreApplication::translate("VibeStudioModelEditor", "Health")));
	tabs->ensurePolished();
	int inspectorWidth = 300;
	for (auto *scroll : tabs->findChildren<QScrollArea *>())
	{
		// Measure the whole page, including nested group padding and translated
		// labels. Button text alone misses margins added by high-contrast styles.
		scroll->widget()->ensurePolished();
		const int borders = 2 * tabs->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, tabs);
		inspectorWidth = std::max(inspectorWidth,
								  scroll->widget()->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() + borders);
	}
	tabs->setMinimumWidth(inspectorWidth);
	splitter->addWidget(tabs);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({260, 620, 330});
	layout->addWidget(splitter, 1);
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("meshStatus"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Mesh editor status"));
	layout->addWidget(m_status);
	auto *recoveryRow = new QHBoxLayout;
	m_recoveryEnabled = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Keep local recovery copies"));
	m_recoveryEnabled->setObjectName(QStringLiteral("meshRecoveryEnabled"));
	m_recoveryEnabled->setAccessibleName(m_recoveryEnabled->text());
	m_recoveryEnabled->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Checks unsaved edits every five seconds. Copies stay on this device. Turning this off keeps existing copies."));
	m_recoveryEnabled->setChecked(StudioSettings().modelRecoveryEnabled());
	recoveryRow->addWidget(m_recoveryEnabled);
	m_recoveryStatus = new QLabel;
	m_recoveryStatus->setObjectName(QStringLiteral("meshRecoveryStatus"));
	m_recoveryStatus->setTextFormat(Qt::PlainText);
	m_recoveryStatus->setWordWrap(true);
	m_recoveryStatus->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Local mesh recovery status"));
	recoveryRow->addWidget(m_recoveryStatus, 1);
	layout->addLayout(recoveryRow);
	m_materialLoader = new ModelMaterialWorker(this);
	m_materialLoader->started = [this, details]
	{
		m_surfaceImages.clear();
		m_materialAssets = {};
		m_materialStatus->setText(QCoreApplication::translate("VibeStudioModelEditor", "Loading material images…"));
		m_materialStatus->setAccessibleName(m_materialStatus->text());
		details->setEnabled(false);
		m_materialProgress->setAccessibleDescription(m_materialStatus->text());
		m_materialProgress->setRange(0, 0);
		m_materialProgress->show();
		m_materialCancel->setEnabled(true);
		applyMaterialImages();
	};
	m_materialLoader->progress = [this](int done, int total)
	{
		m_materialProgress->setRange(0, total);
		if (total > 0)
		{
			m_materialProgress->setValue(done);
		}
	};
	m_materialLoader->completed = [this, details](const ModelMaterialResult &result)
	{
		m_materialAssets = result.assets;
		m_surfaceImages.clear();
		if (!result.assets.cancelled && result.error.isEmpty())
		{
			for (int s = 0; s < m_document.mesh().surfaces.size(); ++s)
			{
				const auto key = modelPreviewSurfaceMaterialKey(s);
				for (const auto &material : result.assets.materials)
				{
					if (material.key == key && material.ready())
					{
						m_surfaceImages.insert(s, material.image);
						break;
					}
				}
			}
		}
		m_materialStatus->setText(
			!result.error.isEmpty() ? result.error
			: result.assets.cancelled
				? QCoreApplication::translate("VibeStudioModelEditor", "Material loading cancelled. Reload Images to try again.")
				: QCoreApplication::translate("VibeStudioModelEditor", "Material images: %1/%2 ready · %3 problems")
					  .arg(result.assets.readyCount())
					  .arg(result.assets.requestedMaterials)
					  .arg(result.assets.problemCount()));
		m_materialStatus->setAccessibleName(m_materialStatus->text());
		m_materialProgress->hide();
		m_materialCancel->setEnabled(false);
		details->setEnabled(true);
		applyMaterialImages();
	};
	m_recoveryDirectory = modelRecoveryDirectory();
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_recovery = new ModelRecoveryWriter(m_recoveryDirectory, this);
	m_recovery->finished = [this](const QString &path, const QString &error)
	{
		m_recoveryStatus->setText(error.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "Recovery copy updated.")
												  : QCoreApplication::translate("VibeStudioModelEditor", "Recovery failed: %1").arg(error));
		m_recoveryStatus->setToolTip(path);
		if (!error.isEmpty())
		{
			m_recoveryRequestedRevision = 0;
		}
	};
	connect(m_recoveryEnabled, &QCheckBox::toggled, this,
			[this](bool enabled)
			{
				StudioSettings settings;
				settings.setModelRecoveryEnabled(enabled);
				settings.sync();
				m_recoveryRequestedRevision = 0;
				if (enabled)
				{
					checkpointRecovery();
				}
				else
				{
					m_recovery->cancelPending();
					m_recoveryStatus->setText(
						QCoreApplication::translate("VibeStudioModelEditor", "Automatic recovery is off. Existing copies are retained."));
				}
			});
	m_recoveryTimer = new QTimer(this);
	m_recoveryTimer->setInterval(5000);
	connect(m_recoveryTimer, &QTimer::timeout, this, [this] { checkpointRecovery(); });
	m_recoveryTimer->start();
	m_checker = QImage(64, 64, QImage::Format_RGB32);
	m_checker.fill(QColor(215, 215, 215));
	{
		QPainter painter(&m_checker);
		painter.fillRect(0, 0, 32, 32, QColor(60, 60, 60));
		painter.fillRect(32, 32, 32, 32, QColor(60, 60, 60));
		painter.fillRect(2, 2, 8, 8, Qt::white);
	}
	connect(frameView, &QAction::triggered, m_preview, &ModelViewport::frameModel);
	const auto refreshMoveTools = [this, geometry, customPivotRow, customAxesRow]()
	{
		const bool snap = m_snapTranslation->isChecked();
		const int tool = m_transformTool->currentIndex();
		const QList<QDoubleSpinBox *> steps{m_translationGrid, m_rotationGrid, m_scaleGrid};
		for (int i = 0; i < steps.size(); ++i)
		{
			steps[i]->setEnabled(snap);
			steps[i]->setVisible(i == tool);
		}
		m_preview->setTransformGizmo(m_moveGizmo->isChecked(), ModelTransformTool(tool), snap ? m_translationGrid->value() : 0,
									 snap ? m_rotationGrid->value() : 0, snap ? m_scaleGrid->value() : 0);
		m_preview->setTransformPivot(ModelTransformPivot(m_pivotMode->currentIndex()),
									 {float(m_pivot[0]->value()), float(m_pivot[1]->value()), float(m_pivot[2]->value())});
		for (auto *field : m_pivot)
		{
			field->setEnabled(m_pivotMode->currentIndex() == 2);
		}
		geometry->setRowVisible(customPivotRow, m_pivotMode->currentIndex() == 2);
		geometry->setRowVisible(customAxesRow, m_transformSpace->currentIndex() == int(ModelTransformSpace::Custom));
		for (auto *field : m_axisRotation)
			field->setEnabled(m_transformSpace->currentIndex() == int(ModelTransformSpace::Custom));
		refreshTransformAxes();
		m_preview->setXrayVertices(m_xrayVertices->isChecked());
	};
	connect(m_moveGizmo, &QCheckBox::toggled, this, refreshMoveTools);
	connect(m_snapTranslation, &QCheckBox::toggled, this, refreshMoveTools);
	connect(m_translationGrid, &QDoubleSpinBox::valueChanged, this, refreshMoveTools);
	connect(m_rotationGrid, &QDoubleSpinBox::valueChanged, this, refreshMoveTools);
	connect(m_scaleGrid, &QDoubleSpinBox::valueChanged, this, refreshMoveTools);
	connect(m_transformTool, &QComboBox::currentIndexChanged, this, refreshMoveTools);
	connect(m_pivotMode, &QComboBox::currentIndexChanged, this, refreshMoveTools);
	connect(m_transformSpace, &QComboBox::currentIndexChanged, this, refreshMoveTools);
	for (auto *field : m_axisRotation)
		connect(field, &QDoubleSpinBox::valueChanged, this, refreshMoveTools);
	for (auto *field : m_pivot)
	{
		connect(field, &QDoubleSpinBox::valueChanged, this, refreshMoveTools);
	}
	connect(m_frameScope, &QComboBox::currentIndexChanged, this, [this] { m_preview->finishEditTransform(false); });
	connect(m_xrayVertices, &QCheckBox::toggled, this, refreshMoveTools);
	connect(m_showTags, &QCheckBox::toggled, m_preview, &ModelViewport::setShowTags);
	m_preview->setShowTags(m_showTags->isChecked());
	refreshMoveTools();
	connect(m_viewPreset, &QComboBox::currentIndexChanged, this,
			[this](int preset)
			{
				if (m_refreshing || m_working || preset < 0 || preset > 4)
				{
					return;
				}
				auto controls = m_preview->cameraControls();
				controls.perspective = preset == 4;
				m_preview->setCameraControls(controls);
				if (preset < 4)
				{
					const double yaw[]{30, -90, -90, 0}, pitch[]{20, 90, 0, 0};
					m_preview->setOrbit(yaw[preset], pitch[preset]);
				}
				m_preview->frameModel();
			});
	connect(m_preview, &ModelViewport::viewChanged, this,
			[this]()
			{
				int preset = 0;
				const double yaw = std::remainder(m_preview->yaw(), 360.0), pitch = m_preview->pitch();
				if (m_preview->isPerspective())
				{
					preset = 4;
				}
				else if (std::abs(yaw + 90) < 0.001 && std::abs(pitch - 90) < 0.001)
				{
					preset = 1;
				}
				else if (std::abs(yaw + 90) < 0.001 && std::abs(pitch) < 0.001)
				{
					preset = 2;
				}
				else if (std::abs(yaw) < 0.001 && std::abs(pitch) < 0.001)
				{
					preset = 3;
				}
				QSignalBlocker blocker(m_viewPreset);
				m_viewPreset->setCurrentIndex(preset);
			});
	connect(m_renderMode, &QComboBox::currentIndexChanged, this, [this]() { refreshMaterial(); });
	connect(m_surface, &QComboBox::currentIndexChanged, this,
			[this](int index)
			{
				if (m_refreshing || m_working)
				{
					return;
				}
				auto selected = m_selectionMode->currentIndex() == 5 ? m_document.selection() : ModelSelection{};
				selected.surface = index;
				if (m_selectionMode->currentIndex() == 5)
					selected.surfaces.insert(index);
				m_document.setSelection(selected);
				refresh();
			});
	connect(m_selectionMode, &QComboBox::currentIndexChanged, this,
			[this]()
			{
				if (m_refreshing || m_working)
				{
					return;
				}
				ModelSelection selected;
				selected.surface = m_document.selection().surface;
				if (m_selectionMode->currentIndex() == 5)
					selected.surfaces.insert(selected.surface);
				m_document.setSelection(selected);
				refresh();
			});
	connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() { selectFromTable(); });
	connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
		if (m_selectionMode->currentIndex() == 5)
			selectFromTable();
	});
	connect(m_frame, &QComboBox::currentIndexChanged, this,
			[this](int frame)
			{
				if (!m_refreshing && !m_working && frame >= 0)
				{
					// Coalesce pause/frame signals into one inspector refresh.
					m_refreshing = true;
					m_preview->pause();
					m_preview->setFrame(frame);
					m_refreshing = false;
					refresh();
				}
			});
	connect(m_preview, &ModelViewport::frameChanged, this,
			[this](int frame)
			{
				if (m_refreshing || m_working || frame < 0 || frame >= m_frame->count())
				{
					return;
				}
				QSignalBlocker blocker(m_frame);
				m_frame->setCurrentIndex(frame);
				m_frameName->setText(m_document.mesh().frames[frame].name);
				refreshAnimationControls();
				refreshTags();
				refreshCollision();
				refreshIntersections();
				// Avoid rebuilding a potentially large table during playback; the table
				// refreshes on pause and on explicit frame selection.
				if (!m_preview->isPlaying())
				{
					refresh();
				}
				else if (m_transformSpace->currentIndex() == int(ModelTransformSpace::Selection))
				{
					refreshTransformAxes();
				}
			});
	connect(m_preview, &ModelViewport::playbackChanged, this,
			[this](bool playing)
			{
				if (!m_refreshing && !m_working)
				{
					refreshAnimationControls();
					refreshTags();
				}
				if (!playing && !m_refreshing && !m_working)
				{
					refresh();
				}
			});
	connect(m_preview, &ModelViewport::trianglePicked, this,
			[this](int surfaceIndex, int triangle, int pick)
			{
				if (m_working || (m_selectionMode->currentIndex() != 0 && m_selectionMode->currentIndex() != 5) || surfaceIndex < 0 || triangle < 0 ||
					surfaceIndex >= m_document.mesh().surfaces.size())
				{
					return;
				}
				if (m_selectionMode->currentIndex() == 5)
				{
					auto selected = m_document.selection();
					const bool toggle = pick == int(ModelViewportPick::Toggle) || pick == int(ModelViewportPick::FaceToggle);
					if (!toggle)
						selected = {};
					selected.surface = surfaceIndex;
					if (toggle && selected.surfaces.contains(surfaceIndex))
						selected.surfaces.remove(surfaceIndex);
					else
						selected.surfaces.insert(surfaceIndex);
					if (!selected.surfaces.isEmpty() && !selected.surfaces.contains(selected.surface))
						selected.surface = *std::min_element(selected.surfaces.cbegin(), selected.surfaces.cend());
					m_document.setSelection(selected);
					refresh();
					return;
				}
				for (int i = 0; i < surfaceIndex; ++i)
				{
					triangle -= m_document.mesh().surfaces[i].triangles.size();
				}
				const auto &s = m_document.mesh().surfaces[surfaceIndex];
				if (triangle < 0 || triangle >= s.triangles.size())
				{
					return;
				}
				auto selected = m_document.selection();
				const bool toggle = pick == int(ModelViewportPick::Toggle) || pick == int(ModelViewportPick::FaceToggle);
				if (selected.surface != surfaceIndex || !toggle || !selected.collision.isEmpty())
				{
					selected = {surfaceIndex, {}, {}};
				}
				if (toggle && selected.faces.contains(triangle))
				{
					selected.faces.remove(triangle);
				}
				else
				{
					selected.faces.insert(triangle);
				}
				m_document.setSelection(selected);
				refresh();
			});
	connect(m_preview, &ModelViewport::vertexPicked, this,
			[this](int surfaceIndex, int vertex, int pick)
			{
				if (m_working || m_selectionMode->currentIndex() != 1 || surfaceIndex < 0 ||
					surfaceIndex >= m_document.mesh().surfaces.size() || vertex < 0)
				{
					return;
				}
				auto selected = m_document.selection();
				const bool toggle = pick == int(ModelViewportPick::Toggle) || pick == int(ModelViewportPick::FaceToggle);
				if (selected.surface != surfaceIndex || !toggle || !selected.collision.isEmpty())
				{
					selected = {surfaceIndex, {}, {}};
				}
				if (toggle && selected.vertices.contains(vertex))
				{
					selected.vertices.remove(vertex);
				}
				else
				{
					selected.vertices.insert(vertex);
				}
				m_document.setSelection(selected);
				refresh();
			});
	connect(m_preview, &ModelViewport::editTransformActiveChanged, this,
			[this](bool active)
			{
				if (active)
				{
					m_status->setText(QCoreApplication::translate(
						"VibeStudioModelEditor", "Transforming selection. Release to validate and apply; Escape cancels."));
				}
				else if (!m_refreshing && !m_working)
				{
					refreshSelection();
					m_status->setText(m_preview->editTransformValid()
										  ? QCoreApplication::translate("VibeStudioModelEditor", "Transform preview ended.")
										  : QCoreApplication::translate("VibeStudioModelEditor",
																		"Invalid transform cancelled. The source is unchanged."));
				}
			});
	connect(m_preview, &ModelViewport::editTransformPreviewChanged, this,
			[this]
			{
				if (!m_preview->editTransformValid())
				{
					m_status->setText(QCoreApplication::translate(
						"VibeStudioModelEditor", "This drag position is invalid. Move back to a valid position; releasing now cancels."));
					return;
				}
				const auto transform = m_preview->editTransform();
				const int tool = m_transformTool->currentIndex();
				const auto values = tool == 0 ? transform.translation : tool == 1 ? transform.rotation : transform.scale;
				const auto pattern =
					tool == 0	? QCoreApplication::translate("VibeStudioModelEditor",
															  "Move preview: X %1, Y %2, Z %3 model units. Release to apply; Escape cancels.")
					: tool == 1 ? QCoreApplication::translate("VibeStudioModelEditor",
															  "Rotation preview: X %1°, Y %2°, Z %3°. Release to apply; Escape cancels.")
								: QCoreApplication::translate("VibeStudioModelEditor",
															  "Scale preview: X %1×, Y %2×, Z %3×. Release to apply; Escape cancels.");
				m_status->setText(pattern.arg(values.x, 0, 'g', 7).arg(values.y, 0, 'g', 7).arg(values.z, 0, 'g', 7));
			});
	connect(m_preview, &ModelViewport::editTransformRequested, this,
			[this](const ModelTransform &transform)
			{
				if (m_working)
				{
					return;
				}
				ModelEdit edit;
				edit.selection = m_document.selection();
				edit.kind = !edit.selection.collision.isEmpty() ? ModelEditKind::TransformCollisionBox
							: edit.selection.tag.isEmpty()		? ModelEditKind::Transform
																: ModelEditKind::TransformTag;
				edit.translation = transform.translation;
				edit.rotation = transform.rotation;
				edit.scale = transform.scale;
				edit.pivot = transform.pivot;
				configureTransformAxes(edit);
				const auto collision = findModelCollisionBox(m_document.mesh(), edit.selection.collision);
				edit.frame = (collision && collision->framePoses.isEmpty()) || m_frameScope->currentIndex() == 0 ? -1 : m_frame->currentIndex();
				edit.pivotFrame = m_frame->currentIndex();
				QString error;
				if (!applyEdit(edit, &error))
				{
					m_status->setText(error);
				}
			});
	connect(m_preview, &ModelViewport::collisionPicked, this,
			[this](const QString &name, int pick)
			{
				if (m_working || m_selectionMode->currentIndex() != 4)
				{
					return;
				}
				const bool toggle = pick == int(ModelViewportPick::Toggle) || pick == int(ModelViewportPick::FaceToggle);
				if (name.isEmpty() && toggle)
				{
					return;
				}
				ModelSelection selected;
				selected.surface = m_document.selection().surface;
				selected.collision = toggle && m_document.selection().collision == name ? QString{} : name;
				m_preview->pause();
				m_document.setSelection(selected);
				refresh();
			});
	connect(m_preview, &ModelViewport::tagPicked, this,
			[this](const QString &name, int pick)
			{
				if (m_working || m_selectionMode->currentIndex() != 3)
				{
					return;
				}
				const bool toggle = pick == int(ModelViewportPick::Toggle) || pick == int(ModelViewportPick::FaceToggle);
				if (name.isEmpty() && toggle)
				{
					return;
				}
				ModelSelection selected;
				selected.surface = m_document.selection().surface;
				selected.tag = toggle && m_document.selection().tag == name ? QString{} : name;
				m_preview->pause();
				m_document.setSelection(selected);
				refresh();
			});
	connect(m_preview, &ModelViewport::edgePicked, this,
			[this](int surfaceIndex, int a, int b, int pick)
			{
				if (m_working || m_selectionMode->currentIndex() != 2 || surfaceIndex < 0 || a < 0 || b < 0 ||
					surfaceIndex >= m_document.mesh().surfaces.size())
				{
					return;
				}
				auto selected = m_document.selection();
				const bool toggle = pick == int(ModelViewportPick::Toggle) || pick == int(ModelViewportPick::FaceToggle);
				if (selected.surface != surfaceIndex || !toggle || !selected.collision.isEmpty())
				{
					selected = {surfaceIndex, {}, {}};
				}
				const auto edge = modelEdge(a, b);
				if (toggle && selected.edges.contains(edge))
				{
					selected.edges.remove(edge);
				}
				else
				{
					selected.edges.insert(edge);
				}
				m_document.setSelection(selected);
				refresh();
			});
	ModelDesign design;
	design.parts << ModelDesignPart{};
	// Keep construction synchronous; no nested UI events until every control exists.
	m_document.setMesh(buildModelDesignMesh(design));
	refresh(false);
}

const ModelDocument &ModelEditorDialog::document() const { return m_document; }
bool ModelEditorDialog::operationBusy() const { return m_working; }
void ModelEditorDialog::cancelOperation()
{
	if (m_cancelWork)
	{
		m_cancelWork();
	}
}
bool ModelEditorDialog::performWork(const QString &title, ModelDocumentJob job, QString *error, bool durableWrite)
{
	if (m_working)
	{
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelEditor", "Wait for the current model operation to finish.");
		}
		return false;
	}
	m_working = true;
	m_uv->finishMove(false);
	m_preview->pause();
	const QPointer<QWidget> previousFocus = focusWidget();
	checkpointRecovery();
	const bool recoveryTimerActive = m_recoveryTimer->isActive();
	m_recoveryTimer->stop();
	QVector<QPair<QAction *, bool>> actionStates;
	for (auto *action : m_toolbar->actions())
	{
		actionStates.append({action, action->isEnabled()});
		action->setEnabled(false);
	}
	m_editingControls->setEnabled(false);
	m_recoveryEnabled->setEnabled(false);
	m_status->setText(title);
	const bool success = runModelDocumentWork(this, title, &m_document, std::move(job), error, durableWrite, &m_cancelWork);
	m_working = false;
	for (const auto &[action, enabled] : actionStates)
	{
		action->setEnabled(enabled);
	}
	m_editingControls->setEnabled(true);
	m_recoveryEnabled->setEnabled(true);
	if (previousFocus && previousFocus->isEnabled() && !m_closeAfterWork)
	{
		previousFocus->setFocus(Qt::OtherFocusReason);
	}
	if (recoveryTimerActive)
	{
		m_recoveryTimer->start();
	}
	refreshContext();
	if (m_materialRefreshPending)
	{
		m_materialRefreshPending = false;
		refreshMaterial();
	}
	if (!success)
	{
		refresh();
	}
	if (m_closeAfterWork)
	{
		m_closeAfterWork = false;
		// Let the calling action finish refreshing before any deferred deletion.
		QTimer::singleShot(0, this, [this] { close(); });
	}
	return success;
}
ModelEditorDialog::~ModelEditorDialog()
{
	delete m_materialLoader;
	m_materialLoader = nullptr;
	m_recoveryTimer->stop();
	if (!m_approvedClose)
	{
		checkpointRecovery();
	}
	m_recovery->finished = {};
	delete m_recovery;
	m_recovery = nullptr;
}
bool ModelEditorDialog::setMesh(const ModelMesh &mesh, QString *error)
{
	const auto sourcePath = mesh.sourcePath;
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Prepare Mesh"),
			[mesh](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{ return candidate.setMesh(mesh, failure, control); }, error))
	{
		return false;
	}
	retireRecovery();
	m_recoverySourcePath = sourcePath;
	m_previewMaterialSlots.clear();
	m_lastSkinBindings.clear();
	m_skinBindingsDetails->setEnabled(false);
	m_recoverySourceHash.clear();
	m_materialLoader->reset();
	m_surfaceImages.clear();
	m_materialAssets = {};
	refresh(false);
	return true;
}
bool ModelEditorDialog::applyEdit(const ModelEdit &edit, QString *error)
{
	const auto before = m_document.revisionFingerprint();
	m_preview->finishEditMove(false);
	m_uv->finishMove(false);
	const auto title = edit.kind == ModelEditKind::UnwrapUv ? QCoreApplication::translate("VibeStudioModelEditor", "Unwrap and Pack UVs")
					   : edit.kind == ModelEditKind::PackUv ? QCoreApplication::translate("VibeStudioModelEditor", "Pack UVs")
					   : edit.kind == ModelEditKind::PackUvAround ? QCoreApplication::translate("ModelUvObstacles", "Pack Around Unselected")
															: QCoreApplication::translate("VibeStudioModelEditor", "Edit Mesh");
	if (!performWork(
			title, [edit](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{ return candidate.edit(edit, failure, control); }, error))
	{
		return false;
	}
	if ((isModelSurfaceEdit(edit.kind) || edit.kind == ModelEditKind::SetMaterialSlots || edit.kind == ModelEditKind::SetMaterial) && before != m_document.revisionFingerprint())
	{
		m_materialLoader->reset();
		m_surfaceImages.clear();
		m_materialAssets = {};
		m_lastSkinBindings.clear();
		m_skinBindingsDetails->setEnabled(false);
	}
	refresh();
	return true;
}
void ModelEditorDialog::setAccessibility(bool highContrast, bool reducedMotion)
{
	m_highContrast = highContrast;
	m_preview->setHighContrast(highContrast);
	m_preview->setReducedMotion(reducedMotion);
}
void ModelEditorDialog::refresh(bool keepView)
{
	if (m_refreshing || m_working || m_document.mesh().surfaces.isEmpty())
	{
		return;
	}
	m_refreshing = true;
	const auto &mesh = m_document.mesh();
	const auto revision = m_document.revisionFingerprint();
	const bool meshChanged = !keepView || m_presentedMeshRevision != revision;
	const bool keepMdlPlayback = keepView && m_preview->mdlPlaybackActive() && m_mdlRevision == m_document.revisionFingerprint();
	const auto nativePlayback = m_preview->mdlPlayback();
	const auto nativeSkins = keepMdlPlayback ? m_preview->mdlPlaybackSkins() : QVector<QImage>{};
	m_md2Width->setValue(mesh.md2SkinSize.width());
	m_md2Height->setValue(mesh.md2SkinSize.height());
	refreshMdlControls();
	const auto selected = m_document.selection();
	const int frame = std::clamp(m_frame->currentIndex(), 0, int(mesh.frames.size()) - 1);
	if (!selected.surfaces.isEmpty())
	{
		m_selectionMode->setCurrentIndex(5);
	}
	else if (!selected.collision.isEmpty())
	{
		m_selectionMode->setCurrentIndex(4);
	}
	else if (!selected.tag.isEmpty())
	{
		m_selectionMode->setCurrentIndex(3);
	}
	else if (!selected.edges.isEmpty())
	{
		m_selectionMode->setCurrentIndex(2);
	}
	else if (!selected.vertices.isEmpty())
	{
		m_selectionMode->setCurrentIndex(1);
	}
	else if (!selected.faces.isEmpty())
	{
		m_selectionMode->setCurrentIndex(0);
	}
	m_table->setSelectionMode((m_selectionMode->currentIndex() == 3 || m_selectionMode->currentIndex() == 4) ? QAbstractItemView::SingleSelection
																   : QAbstractItemView::ExtendedSelection);
	m_surface->setEnabled(m_selectionMode->currentIndex() < 3 || m_selectionMode->currentIndex() == 5);
	if (meshChanged)
	{
		m_surface->clear();
		for (const auto &s : mesh.surfaces)
		{
			m_surface->addItem(s.name);
		}
		m_frame->clear();
		for (int i = 0; i < mesh.frames.size(); ++i)
		{
			m_frame->addItem(QStringLiteral("%1 · %2").arg(i).arg(mesh.frames[i].name));
		}
	}
	m_surface->setCurrentIndex(selected.surface);
	m_frame->setCurrentIndex(frame);
	m_frameName->setText(mesh.frames[frame].name);
	const bool tableReset =
		m_components->show(mesh, m_document.surfaceTopology(selected.surface), selected.surface, frame, m_selectionMode->currentIndex());
	if (tableReset)
	{
		// Size once for the new columns. Pose updates retain the user's widths
		// instead of repeatedly measuring delegate text in header timer events.
		m_table->resizeColumnsToContents();
		static_cast<ModelComponentView *>(m_table)->setWholeSurfaceMode(m_selectionMode->currentIndex() == 5);
	}
	if (tableReset || selected != m_presentedSelection)
	{
		QItemSelection tableSelection;
		const int mode = m_selectionMode->currentIndex(), count = m_components->rowCount();
		const auto addRange = [&](int first, int last)
		{
			tableSelection.append(
				QItemSelectionRange(m_components->index(first, 0), m_components->index(last, m_components->columnCount() - 1)));
		};
		const auto &chosen = mode == 5 ? selected.surfaces : mode == 1 ? selected.vertices : selected.faces;
		const qsizetype selectedCount = mode == 4	? !selected.collision.isEmpty()
										: mode == 3 ? !selected.tag.isEmpty()
										: mode == 2 ? selected.edges.size()
													: chosen.size();
		if (selectedCount > count / 16)
		{
			// Dense selections are cheaper to visit in table order than to sort
			// or binary-search every selected edge, and produce compact ranges.
			int first = -1;
			for (int row = 0; row < count; ++row)
			{
				const bool active = mode == 4	? m_components->collision(row) == selected.collision
									: mode == 3 ? m_components->tag(row) == selected.tag
									: mode == 2 ? selected.edges.contains(m_components->edge(row))
												: chosen.contains(row);
				if (active && first < 0)
				{
					first = row;
				}
				if (!active && first >= 0)
				{
					addRange(first, row - 1);
					first = -1;
				}
			}
			if (first >= 0)
			{
				addRange(first, count - 1);
			}
		}
		else
		{
			QVector<int> rows;
			if (mode == 4 && !selected.collision.isEmpty())
			{
				rows.append(m_components->collisionRow(selected.collision));
			}
			else if (mode == 3 && !selected.tag.isEmpty())
			{
				rows.append(m_components->tagRow(selected.tag));
			}
			else if (mode == 2)
			{
				for (auto edge : selected.edges)
				{
					rows.append(m_components->edgeRow(edge));
				}
			}
			else
			{
				rows = chosen.values();
			}
			std::sort(rows.begin(), rows.end());
			for (qsizetype at = 0; at < rows.size();)
			{
				const int first = rows[at++];
				int last = first;
				while (at < rows.size() && rows[at] == last + 1)
				{
					last = rows[at++];
				}
				if (first >= 0 && last < count)
				{
					addRange(first, last);
				}
			}
		}
		if (m_table->selectionModel()->selection() != tableSelection)
		{
			m_table->selectionModel()->select(tableSelection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		}
		m_presentedSelection = selected;
	}
	refreshMaterialSlots();
	m_skinBindingsFile->setEnabled(!mesh.mdl.enabled && mesh.embeddedSkins.isEmpty());
	m_skinBindingsPackage->setEnabled(m_skinBindingsFile->isEnabled() && m_materialSource.archive && m_materialSource.archive->isOpen());
	if (meshChanged)
	{
		m_preview->setMesh(mesh, keepView);
		m_highlightedRevision.clear();
		m_previewImageKeys.clear();
		m_presentedMeshRevision = revision;
	}
	if (m_preview->frame() != frame || m_preview->animationSample().fraction != 0)
	{
		m_preview->setFrame(frame);
	}
	restoreAnimationPreview(frame, keepView);
	if (keepMdlPlayback)
	{
		m_preview->setMdlPlayback(nativePlayback, nativeSkins);
	}
	refreshMdlPlayback();
	refreshMaterial();
	m_undo->setEnabled(m_document.canUndo());
	m_redo->setEnabled(m_document.canRedo());
	setWindowModified(m_document.isModified());
	setWindowTitle(QCoreApplication::translate("VibeStudioModelEditor", "Mesh Editor — %1[*]")
					   .arg(m_document.path().isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "Unsaved source")
														: QFileInfo(m_document.path()).fileName()));
	m_refreshing = false;
	refreshSelection();
	if (!keepView)
	{
		m_uv->frameAll();
	}
	refreshContext();
}
void ModelEditorDialog::selectFromTable()
{
	if (m_refreshing || m_working)
	{
		return;
	}
	ModelSelection selected;
	selected.surface = m_surface->currentIndex();
	const int mode = m_selectionMode->currentIndex();
	int count = 0;
	const auto ranges = m_table->selectionModel()->selection();
	const bool allEdges =
		mode == 2 && ranges.size() == 1 && ranges.first().top() == 0 && ranges.first().bottom() == m_components->rowCount() - 1;
	for (const auto &range : ranges)
	{
		count += range.height();
	}
	if (mode == 2)
	{
		if (allEdges)
		{
			selected.edges = m_document.surfaceTopology(selected.surface).allEdges;
		}
		else
		{
			selected.edges.reserve(count);
		}
	}
	else if (mode == 1)
	{
		selected.vertices.reserve(count);
	}
	else if (mode == 0)
	{
		selected.faces.reserve(count);
	}
	// QItemSelection already describes compact row ranges. Expanding it to
	// hundreds of thousands of QModelIndex objects makes Select All expensive.
	for (const auto &range : ranges)
	{
		if (allEdges)
		{
			break;
		}
		for (int row = range.top(); row <= range.bottom(); ++row)
		{
			if (mode == 5)
			{
				selected.surfaces.insert(row);
			}
			else if (mode == 4)
			{
				selected.collision = m_components->collision(row);
			}
			else if (mode == 3)
			{
				selected.tag = m_components->tag(row);
			}
			else if (mode == 2)
			{
				selected.edges.insert(m_components->edge(row));
			}
			else if (mode == 1)
			{
				selected.vertices.insert(row);
			}
			else
			{
				selected.faces.insert(row);
			}
		}
	}
	if (mode == 5 && !selected.surfaces.isEmpty())
	{
		const int current = m_table->currentIndex().row();
		if (selected.surfaces.contains(current))
			selected.surface = current;
		else if (!selected.surfaces.contains(selected.surface))
			selected.surface = *std::min_element(selected.surfaces.cbegin(), selected.surfaces.cend());
	}
	m_document.setSelection(selected);
	if (mode == 5)
	{
		refresh();
		return;
	}
	m_presentedSelection = m_document.selection();
	refreshSelection();
}
void ModelEditorDialog::refreshSelection()
{
	++m_recoveryRevision;
	if (!m_document.isModified() && m_recoveryActive)
	{
		retireRecovery();
	}
	const auto &mesh = m_document.mesh();
	const auto selected = m_document.selection();
	if (selected.surface < 0 || selected.surface >= mesh.surfaces.size())
	{
		return;
	}
	if (m_highlightedRevision != m_document.revisionFingerprint() || m_highlightedSelection != selected)
	{
		int base = 0;
		for (int s = 0; s < selected.surface; ++s)
		{
			base += mesh.surfaces[s].triangles.size();
		}
		QVector<int> highlighted;
		highlighted.reserve(selected.faces.size());
		QSet<int> movedVertices = selected.vertices;
		const auto &surface = mesh.surfaces[selected.surface];
		const auto &topology = m_document.surfaceTopology(selected.surface);
		if (selected.faces.isEmpty() && selected.vertices.isEmpty() && !selected.edges.isEmpty() && selected.edges == topology.allEdges)
		{
			movedVertices = topology.edgeVertices;
		}
		else if (!selected.edges.isEmpty() || !selected.faces.isEmpty())
		{
			QBitArray moving(surface.vertexCount);
			for (auto edge : selected.edges)
			{
				moving.setBit(edge.first);
				moving.setBit(edge.second);
			}
			for (int face : selected.faces)
			{
				if (face < 0 || face >= surface.triangles.size())
				{
					continue;
				}
				const auto triangle = surface.triangles[face];
				highlighted.append(base + face);
				moving.setBit(triangle.a);
				moving.setBit(triangle.b);
				moving.setBit(triangle.c);
			}
			movedVertices.reserve(std::min(surface.vertexCount, int(movedVertices.size() + moving.count(true))));
			for (int vertex = 0; vertex < moving.size(); ++vertex)
			{
				if (moving.testBit(vertex))
				{
					movedVertices.insert(vertex);
				}
			}
		}
		if (!selected.surfaces.isEmpty())
		{
			int first = 0;
			for (int s = 0; s < mesh.surfaces.size(); ++s)
			{
				const int count = mesh.surfaces[s].triangles.size();
				if (selected.surfaces.contains(s))
					for (int face = 0; face < count; ++face)
						highlighted.append(first + face);
				first += count;
			}
		}
		m_preview->setHighlightedTriangles(highlighted);
		m_preview->setHighlightedEdges(selected.surface, selected.edges);
		if (!selected.surfaces.isEmpty())
			m_preview->setEditSurfaces(selected.surface, selected.surfaces);
		else
			m_preview->setEditSelection(selected.surface, movedVertices);
		m_highlightedRevision = m_document.revisionFingerprint();
		m_highlightedSelection = selected;
	}
	m_preview->setEdgePicking(m_selectionMode->currentIndex() == 2);
	m_preview->setVertexPicking(m_selectionMode->currentIndex() == 1);
	const bool tagMode = m_selectionMode->currentIndex() == 3;
	const bool collisionMode = m_selectionMode->currentIndex() == 4;
	findChild<QPushButton *>(QStringLiteral("selectAllMeshComponents"))->setEnabled(!tagMode && !collisionMode);
	m_preview->setCollisionPicking(collisionMode);
	if (collisionMode)
	{
		m_showCollision->setChecked(true);
	}
	m_showCollision->setEnabled(!collisionMode);
	m_preview->setTagPicking(tagMode);
	m_preview->setEditTag(selected.tag);
	if (tagMode)
	{
		m_showTags->setChecked(true);
	}
	m_showTags->setEnabled(!tagMode);
	const bool tagTarget = !selected.tag.isEmpty();
	if (auto *tools = qobject_cast<QStandardItemModel *>(m_transformTool->model()))
	{
		tools->item(2)->setEnabled(!tagTarget);
	}
	if (tagTarget && m_transformTool->currentIndex() == 2)
	{
		m_transformTool->setCurrentIndex(0);
	}
	for (auto *field : m_scale)
	{
		field->setEnabled(!tagTarget);
	}
	for (auto *button : findChildren<QPushButton *>())
		if (button->property("requiresMeshComponents").toBool())
			button->setEnabled(m_selectionMode->currentIndex() != 5);
	m_xrayVertices->setEnabled(m_selectionMode->currentIndex() == 1);
	refreshUv();
	refreshTags();
	refreshHealth();
	refreshCollision();
	m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "%1 vertices · %2 triangles · %3 frames · "
																		   "Selected: %4 vertices, %5 faces, %6 edges")
						  .arg(mesh.vertexCount)
						  .arg(mesh.triangleCount)
						  .arg(mesh.frameCount)
						  .arg(selected.vertices.size())
						  .arg(selected.faces.size())
						  .arg(selected.edges.size()));
	if (m_selectionMode->currentIndex() == 5)
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Selected: %1 surfaces · active: %2 · one common transform pivot")
			.arg(selected.surfaces.size()).arg(mesh.surfaces[selected.surface].name));
	}
	if (tagTarget)
	{
		m_status->setText(
			QCoreApplication::translate("VibeStudioModelEditor", "Selected tag: %1 · frame %2 · move and rotate; edit poses in Animation")
				.arg(selected.tag)
				.arg(m_frame->currentIndex()));
	}
	if (!selected.collision.isEmpty())
	{
		const auto box = findModelCollisionBox(m_document.mesh(), selected.collision);
		m_status->setText((box && !box->framePoses.isEmpty()
			? QCoreApplication::translate("VibeStudioModelEditor", "Selected animated collision box: %1 · current/all-frame scope · chosen move/rotate axes · local scale")
			: QCoreApplication::translate("VibeStudioModelEditor", "Selected collision box: %1 · chosen axes for move/rotate · local scale · static across all frames")).arg(selected.collision));
	}
	refreshTransformAxes();
}
void ModelEditorDialog::configureTransformAxes(ModelEdit &edit) const
{
	if (edit.kind != ModelEditKind::Transform && edit.kind != ModelEditKind::TransformTag &&
		edit.kind != ModelEditKind::TransformCollisionBox && edit.kind != ModelEditKind::Extrude && edit.kind != ModelEditKind::DuplicateFaces)
		return;
	edit.transformSpace = ModelTransformSpace(m_transformSpace->currentIndex());
	edit.axesFrame = edit.transformSpace == ModelTransformSpace::Selection ? m_frame->currentIndex() : -1;
	if (edit.transformSpace == ModelTransformSpace::Custom)
		edit.axisRotation = {float(m_axisRotation[0]->value()), float(m_axisRotation[1]->value()), float(m_axisRotation[2]->value())};
}
void ModelEditorDialog::refreshTransformAxes()
{
	ModelEdit edit;
	edit.selection = m_document.selection();
	edit.kind = !edit.selection.collision.isEmpty() ? ModelEditKind::TransformCollisionBox :
		!edit.selection.tag.isEmpty() ? ModelEditKind::TransformTag : ModelEditKind::Transform;
	configureTransformAxes(edit);
	ModelTransformBasis basis;
	QString error;
	const bool available = resolveModelTransformAxes(m_document.mesh(), edit, &basis, &error);
	m_preview->setTransformAxes(basis, m_transformSpace->currentText(), available);
	const QString description = QCoreApplication::translate("VibeStudioModelEditor",
		"Selection axes follow the first usable selected or touching face, the active whole surface, or the selected tag or collision box. "
		"The displayed pose supplies fixed axes for every affected frame. Collision size always uses the box's local axes.");
	m_transformSpace->setToolTip(available ? description : error + QLatin1Char('\n') + description);
	m_transformSpace->setAccessibleDescription(m_transformSpace->toolTip());
	if (!available && m_status && (!edit.selection.faces.isEmpty() || !edit.selection.vertices.isEmpty() ||
		!edit.selection.edges.isEmpty() || !edit.selection.surfaces.isEmpty() || !edit.selection.tag.isEmpty() || !edit.selection.collision.isEmpty()))
		m_status->setText(error);
}
void ModelEditorDialog::setMaterialSource(ModelMaterialSource source)
{
	if (source.revision == m_materialSource.revision && source.paletteId == m_materialSource.paletteId &&
		source.archive == m_materialSource.archive)
	{
		return;
	}
	m_materialSource = std::move(source);
	m_mdlPackageSkin->setEnabled(m_materialSource.archive && m_materialSource.archive->isOpen());
	m_skinBindingsPackage->setEnabled(!m_document.mesh().mdl.enabled && m_document.mesh().embeddedSkins.isEmpty() &&
		m_materialSource.archive && m_materialSource.archive->isOpen());
	m_materialLoader->reset();
	m_surfaceImages.clear();
	m_materialAssets = {};
	if (m_working)
	{
		m_materialRefreshPending = true;
		return;
	}
	refreshMaterial();
}
bool ModelEditorDialog::materialLoading() const { return m_materialLoader->busy(); }
void ModelEditorDialog::reloadMaterials()
{
	if (m_working)
	{
		return;
	}
	m_materialLoader->reset();
	m_surfaceImages.clear();
	m_materialAssets = {};
	refreshMaterial();
}
void ModelEditorDialog::refreshMaterial()
{
	if (m_document.mesh().surfaces.isEmpty())
	{
		return;
	}
	const bool materialMode = m_renderMode->currentIndex() == 3;
	m_materialRow->setVisible(materialMode);
	if (materialMode)
	{
		m_materialLoader->request(m_document.mesh(), m_materialSource, materialPreviewSlots());
	}
	else
	{
		m_materialLoader->reset();
	}
	applyMaterialImages();
}
void ModelEditorDialog::applyMaterialImages()
{
	m_preview->setMdlSkinVisible(m_renderMode->currentIndex() == 3);
	auto images = m_renderMode->currentIndex() == 3 ? m_surfaceImages : QHash<int, QImage>();
	if (m_renderMode->currentIndex() == 3 && !m_mdlPreview.isNull())
	{
		for (int s = 0; s < m_document.mesh().surfaces.size(); ++s)
		{
			images.insert(s, m_mdlPreview);
		}
	}
	QHash<int, qint64> imageKeys{{-1, m_checker.cacheKey()}};
	for (auto it = images.cbegin(); it != images.cend(); ++it)
	{
		imageKeys.insert(it.key(), it->cacheKey());
	}
	if (imageKeys != m_previewImageKeys)
	{
		m_preview->setSkin(m_checker);
		m_preview->setSurfaceSkins(images);
		m_previewImageKeys = std::move(imageKeys);
	}
	m_preview->setRenderMode(m_renderMode->currentIndex() == 0	 ? ModelViewportRenderMode::FlatShaded
							 : m_renderMode->currentIndex() == 1 ? ModelViewportRenderMode::Wireframe
																 : ModelViewportRenderMode::Textured);
	refreshUv();
}
void ModelEditorDialog::showMaterialDetails()
{
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("meshMaterialDetailsDialog"));
	dialog.setWindowTitle(QCoreApplication::translate("VibeStudioModelEditor", "Model Material Details"));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.resize(680, 460);
	auto *layout = new QVBoxLayout(&dialog);
	auto *text = new QPlainTextEdit;
	text->setReadOnly(true);
	text->setAccessibleName(dialog.windowTitle());
	text->setPlainText(m_materialStatus->text() + QStringLiteral("\n\n") + levelPreviewAssetsText(m_materialAssets));
	layout->addWidget(text);
	auto *close = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Close"));
	connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
	layout->addWidget(close);
	dialog.exec();
}
ModelEdit ModelEditorDialog::operation(ModelEditKind kind) const
{
	ModelEdit edit;
	edit.kind = kind == ModelEditKind::Transform && !m_document.selection().tag.isEmpty() ? ModelEditKind::TransformTag : kind;
	if (kind == ModelEditKind::Transform && !m_document.selection().collision.isEmpty())
	{
		edit.kind = ModelEditKind::TransformCollisionBox;
	}
	edit.md2SkinSize = {m_md2Width->value(), m_md2Height->value()};
	edit.selection = m_document.selection();
	edit.frame = m_frameScope->currentIndex() == 0 ? -1 : m_frame->currentIndex();
	if (kind == ModelEditKind::FillBoundaryLoops || kind == ModelEditKind::BridgeBoundaryLoops)
	{
		edit.frame = -1;
		edit.sourceFrame = m_frame->currentIndex();
	}
	if (kind == ModelEditKind::BridgeBoundaryLoops)
		edit.bridgeTwist = m_bridgeTwist->value();
	if (edit.kind == ModelEditKind::TransformCollisionBox)
	{
		const auto box = findModelCollisionBox(m_document.mesh(), edit.selection.collision);
		if (box && box->framePoses.isEmpty()) edit.frame = -1;
	}
	const auto vec = [](QDoubleSpinBox *const *fields)
	{ return ModelVec3{float(fields[0]->value()), float(fields[1]->value()), float(fields[2]->value())}; };
	edit.translation = vec(m_translation);
	edit.rotation = vec(m_rotation);
	edit.scale = vec(m_scale);
	edit.pivot = vec(m_pivot);
	edit.pivotMode = ModelTransformPivot(m_pivotMode->currentIndex());
	edit.pivotFrame = m_frame->currentIndex();
	configureTransformAxes(edit);
	edit.uvScale = {float(m_uvScale[0]->value()), float(m_uvScale[1]->value())};
	edit.uvOffset = {float(m_uvOffset[0]->value()), float(m_uvOffset[1]->value())};
	edit.uvRotation = m_uvRotation->value();
	edit.uvAtlasResolution = m_uvAtlasResolution->value();
	edit.uvAtlasHeight = m_uvAtlasSquare->isChecked() ? 0 : m_uvAtlasHeight->value();
	edit.uvAtlasPadding = m_uvAtlasPadding->value();
	edit.uvPreserveScale = kind == ModelEditKind::PackUvAround && m_uvObstacleScale->currentIndex() == 1;
	edit.uvPivotMode = kind == ModelEditKind::TransformUv || kind == ModelEditKind::ProjectUv
		? ModelUvPivot(m_uvPivotMode->currentIndex()) : ModelUvPivot::Origin;
	edit.uvPivot = {float(m_uvPivot[0]->value()), float(m_uvPivot[1]->value())};
	edit.uvTranslationGrid =
		(kind == ModelEditKind::TransformUv || kind == ModelEditKind::ProjectUv) && m_uvSnap->isChecked() ? m_uvGrid->value() : 0;
	edit.projection = m_projection->currentIndex();
	edit.weldDistance = m_weldDistance->value();
	edit.preserveSeams = m_preserveSeams->isChecked();
	edit.translationGrid = kind == ModelEditKind::Transform && m_snapTranslation->isChecked() ? m_translationGrid->value() : 0;
	edit.rotationGrid = kind == ModelEditKind::Transform && m_snapTranslation->isChecked() ? m_rotationGrid->value() : 0;
	edit.scaleGrid = kind == ModelEditKind::Transform && m_snapTranslation->isChecked() ? m_scaleGrid->value() : 0;
	if (edit.kind == ModelEditKind::TransformTag)
	{
		edit.scale = {1, 1, 1};
		edit.scaleGrid = 0;
	}
	if (kind == ModelEditKind::SetMaterial)
	{
		edit.kind = ModelEditKind::SetMaterialSlots;
		edit.frame = -1;
		edit.materialSlots = m_document.mesh().surfaces[edit.selection.surface].skinPaths;
		const int slot = m_materialSlot->currentIndex();
		if (edit.materialSlots.isEmpty()) edit.materialSlots.append(m_material->text().trimmed());
		else edit.materialSlots[std::clamp(slot, 0, int(edit.materialSlots.size()) - 1)] = m_material->text().trimmed();
	}
	if (kind == ModelEditKind::ProjectUv || kind == ModelEditKind::UnwrapUv || kind == ModelEditKind::DuplicateFrame ||
		kind == ModelEditKind::DeleteFrame || kind == ModelEditKind::RenameFrame)
	{
		edit.frame = m_frame->currentIndex();
		edit.text = m_frameName->text();
	}
	return edit;
}
void ModelEditorDialog::execute(ModelEditKind kind)
{
	QString error;
	if (!applyEdit(operation(kind), &error))
	{
		m_status->setText(error);
	}
}
void ModelEditorDialog::refreshContext()
{
	if (m_working)
	{
		return;
	}
	const auto current = context ? context() : ModelDesignContext{};
	m_context->setText(
		QCoreApplication::translate("VibeStudioModelEditor", "Package: %1 · Level: %2")
			.arg(current.packagePath.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "None") : current.packagePath,
				 current.mapPath.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "None") : current.mapPath));
	m_stage->setEnabled(bool(handoff) && current.canStage);
	if (m_collisionPlace) { m_collisionPlace->setEnabled(bool(collisionDestination) && current.canPlaceCollision && !m_document.mesh().collisionBoxes.isEmpty()); }
	m_place->setEnabled(bool(handoff) && current.canStage && current.canPlace &&
						m_virtualPath->text().endsWith(QStringLiteral(".md3"), Qt::CaseInsensitive));
	for (auto *field : m_placement)
	{
		if (field)
		{
			field->setEnabled(m_place->isEnabled());
		}
	}
}
bool ModelEditorDialog::maybeSave()
{
	if (m_working)
	{
		return false;
	}
	if (!m_document.isModified())
	{
		return true;
	}
	const auto answer =
		QMessageBox::question(this, QCoreApplication::translate("VibeStudioModelEditor", "Unsaved Mesh"),
							  QCoreApplication::translate("VibeStudioModelEditor", "Save the edited mesh source before continuing?"),
							  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save());
}
bool ModelEditorDialog::save(bool saveAs)
{
	if (m_working)
	{
		return false;
	}
	QString path = m_document.path();
	if (saveAs || path.isEmpty())
	{
		path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioModelEditor", "Save Editable Mesh"), path,
											QCoreApplication::translate("VibeStudioModelEditor", "VibeStudio mesh (*.mesh.json)"));
	}
	if (path.isEmpty())
	{
		return false;
	}
	QString error;
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Save Mesh"),
			[path](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{ return candidate.save(path, true, failure, control); }, &error, true))
	{
		m_status->setText(error);
		return false;
	}
	retireRecovery();
	refresh();
	return true;
}
void ModelEditorDialog::open()
{
	const auto path = QFileDialog::getOpenFileName(
		this, QCoreApplication::translate("VibeStudioModelEditor", "Open or Import Model"), {},
		QCoreApplication::translate("VibeStudioModelEditor", "Editable meshes and models (*.mesh.json *.model.json *.obj *.mdl *.md2 *.md3)"));
	if (path.isEmpty())
	{
		return;
	}
	QString error;
	openSource(path, &error);
}
bool ModelEditorDialog::openSource(const QString &path, QString *error)
{
	QString localError;
	if (!error) { error = &localError; }
	error->clear();
	if (m_working || path.isEmpty() || !maybeSave()) { return false; }
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Open or Import Model"),
			[path](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				if (path.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive))
				{
					return candidate.load(path, failure, control);
				}
				QByteArray bytes;
				ModelMesh mesh;
				return readModelFile(path, &bytes, failure, control) &&
					   importEditableModel(path, bytes, &mesh, failure, nullptr, control) && candidate.setMesh(mesh, failure, control);
			},
			error))
	{
		m_status->setText(*error);
		return false;
	}
	retireRecovery();
	m_recoverySourcePath = path;
	m_previewMaterialSlots.clear();
	m_lastSkinBindings.clear();
	m_skinBindingsDetails->setEnabled(false);
	m_recoverySourceHash = m_document.sourceFingerprint();
	m_materialLoader->reset();
	m_surfaceImages.clear();
	m_materialAssets = {};
	refresh(false);
	return true;
}
void ModelEditorDialog::exportModel(const QString &format)
{
	if (m_working)
	{
		return;
	}
	const auto path = QFileDialog::getSaveFileName(
		this, QCoreApplication::translate("VibeStudioModelEditor", "Export Model"), {},
		format == QStringLiteral("mdl")	  ? QCoreApplication::translate("VibeStudioModelEditor", "Quake model (*.mdl)")
		: format == QStringLiteral("md2") ? QCoreApplication::translate("VibeStudioModelEditor", "Quake II model (*.md2)")
		: format == QStringLiteral("md3") ? QCoreApplication::translate("VibeStudioModelEditor", "Quake III model (*.md3)")
										  : QCoreApplication::translate("VibeStudioModelEditor", "Wavefront OBJ (*.obj)"));
	if (path.isEmpty())
	{
		return;
	}
	QString error;
	qint64 written = 0;
	ModelExportReport report;
	const int frame = m_frame->currentIndex();
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Export Model"),
			[path, format, frame, &written, &report](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				// Review the destination before preparation so a competing write during
				// export cannot be silently accepted as the file the user approved.
				const auto target = inspectModelWriteTarget(path, control);
				if (!target.isValid())
				{
					*failure = target.error;
					return false;
				}
				if (modelPathsReferToSameFile(path, candidate.path()))
				{
					*failure =
						QCoreApplication::translate("VibeStudioModelEditor", "Choose an export path separate from the editable source.");
					return false;
				}
				const auto bytes = exportEditableModel(candidate.mesh(), format, frame, failure, control, &report);
				if (bytes.isEmpty() || !writeModelFile(target, bytes, failure, control))
				{
					return false;
				}
				written = bytes.size();
				return true;
			},
			&error, true))
	{
		m_status->setText(error);
		return;
	}
	m_status->setText(
		QCoreApplication::translate("VibeStudioModelEditor", "Exported %1 bytes to %2. The editable source is saved separately.")
			.arg(written)
			.arg(path) +
		QLatin1Char(' ') + report.notes.join(QLatin1Char(' ')) + nativeUvStatus());
}
void ModelEditorDialog::stage(bool place)
{
	if (m_working)
	{
		return;
	}
	refreshContext();
	if (!(place ? m_place : m_stage)->isEnabled())
	{
		return;
	}
	QString error;
	const LevelMapVec3 origin{float(m_placement[0]->value()), float(m_placement[1]->value()), float(m_placement[2]->value())};
	const auto path = m_virtualPath->text();
	const auto package = context ? context().packagePath : QString();
	const bool replace = m_replace->isChecked();
	QByteArray bytes;
	ModelExportReport report;
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Prepare Model Handoff"),
			[path, &bytes, &report](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				bytes = exportEditableModel(candidate.mesh(), QFileInfo(path).suffix(), 0, failure, control, &report);
				return !bytes.isEmpty();
			},
			&error))
	{
		m_status->setText(error);
		return;
	}
	if (context && context().packagePath != package)
	{
		m_status->setText(QCoreApplication::translate(
			"VibeStudioModelEditor", "The active package changed while preparing the model. Review the destination and stage again."));
		return;
	}
	if (!handoff(bytes, path, place, origin, replace, &error))
	{
		m_status->setText(error);
		return;
	}
	m_status->setText((place ? QCoreApplication::translate("VibeStudioModelEditor", "Model staged and placed. Save the "
																					"package and level separately.")
							 : QCoreApplication::translate("VibeStudioModelEditor", "Model staged. Review and save the package plan.")) +
					  nativeUvStatus() + QLatin1Char(' ') + report.notes.join(QLatin1Char(' ')));
}
bool ModelEditorDialog::requestClose(std::function<void()> afterDeferredClose)
{
	if (m_working || m_decidingClose)
	{
		m_afterDeferredClose = std::move(afterDeferredClose);
		close();
		return false;
	}
	return close();
}
void ModelEditorDialog::closeEvent(QCloseEvent *event)
{
	if (m_approvedClose)
	{
		event->accept();
		return;
	}
	if (m_decidingClose)
	{
		event->ignore();
		return;
	}
	if (m_working)
	{
		m_closeAfterWork = true;
		cancelOperation();
		event->ignore();
		return;
	}
	m_preview->finishEditMove(false);
	m_uv->finishMove(false);
	m_decidingClose = true;
	const bool approved = maybeSave();
	m_decidingClose = false;
	if (approved)
	{
		m_approvedClose = true;
		m_recoveryTimer->stop();
		retireRecovery();
		event->accept();
		if (m_afterDeferredClose)
		{
			// The dialog deletes itself after this close event. Queue on the
			// application so deletion cannot silently discard the continuation.
			auto resume = std::move(m_afterDeferredClose);
			m_afterDeferredClose = {};
			QTimer::singleShot(0, QCoreApplication::instance(), std::move(resume));
		}
	}
	else
	{
		m_afterDeferredClose = {};
		m_closeAfterWork = false;
		event->ignore();
	}
}
void ModelEditorDialog::reject() { close(); }

bool ModelEditorDialog::recoveryBusy() const { return m_recovery && m_recovery->busy(); }
QString ModelEditorDialog::recoveryPath() const { return modelRecoveryPath(m_recoveryDirectory, m_recoveryId); }
void ModelEditorDialog::checkpointRecovery()
{
	if (!m_document.isModified())
	{
		if (m_recoveryActive)
		{
			retireRecovery();
		}
		return;
	}
	if (!m_recovery || !m_recoveryEnabled->isChecked() || m_recoveryRequestedRevision == m_recoveryRevision)
	{
		return;
	}
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = m_document.mesh();
	snapshot.selection = m_document.selection();
	snapshot.frame = std::max(0, m_frame->currentIndex());
	snapshot.sourcePath = m_document.path().isEmpty() ? m_recoverySourcePath : m_document.path();
	snapshot.sourceSha256 = m_document.path().isEmpty() ? m_recoverySourceHash : m_document.sourceFingerprint();
	snapshot.title = snapshot.sourcePath.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "Unsaved mesh")
												   : QFileInfo(snapshot.sourcePath).fileName();
	m_recoveryActive = true;
	m_recoveryRequestedRevision = m_recoveryRevision;
	m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioModelEditor", "Updating local recovery copy…"));
	m_recovery->checkpoint(m_recoveryId, m_recoveryRevision, std::move(snapshot));
}
void ModelEditorDialog::retireRecovery(const QString &preservedId)
{
	if (m_recoveryActive)
	{
		if (m_recoveryId == preservedId)
		{
			m_recovery->cancelPending();
		}
		else
		{
			m_recovery->retire(m_recoveryId);
		}
	}
	m_recoveryActive = false;
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_recoveryRequestedRevision = 0;
	m_recoveryStatus->clear();
	m_recoveryStatus->setToolTip({});
}
void ModelEditorDialog::chooseRecovery()
{
	if (!maybeSave())
	{
		return;
	}
	m_recoveryTimer->stop();
	auto restored = chooseModelRecovery(this, m_recoveryDirectory);
	m_recoveryTimer->start();
	if (!restored)
	{
		return;
	}
	retireRecovery(restored->record.id);
	m_document = std::move(restored->document);
	m_previewMaterialSlots.clear();
	m_lastSkinBindings.clear();
	m_skinBindingsDetails->setEnabled(false);
	m_recoverySourcePath = restored->record.sourcePath;
	m_recoverySourceHash = restored->record.sourceSha256;
	m_materialLoader->reset();
	m_surfaceImages.clear();
	m_materialAssets = {};
	{
		QSignalBlocker blocker(m_frame);
		m_frame->clear();
		for (const auto &frame : m_document.mesh().frames)
		{
			m_frame->addItem(frame.name);
		}
		m_frame->setCurrentIndex(restored->record.frame);
		QSignalBlocker modeBlocker(m_selectionMode);
		m_selectionMode->setCurrentIndex(!m_document.selection().surfaces.isEmpty() ? 5 : !m_document.selection().collision.isEmpty() ? 4
										 : !m_document.selection().tag.isEmpty()	 ? 3
										 : !m_document.selection().edges.isEmpty()	 ? 2
																					 : (m_document.selection().vertices.isEmpty() ? 0 : 1));
	}
	refresh(false);
	checkpointRecovery();
	m_status->setText(
		QCoreApplication::translate("VibeStudioModelEditor", "Recovered an unsaved draft. Save it to an editable mesh source."));
}

} // namespace vibestudio
