#include "app/model_editor_tools.h"
#include "app/locale_number.h"

#include "app/model_editor_dialog.h"
#include "app/model_primitive_binding_dialog.h"
#include "app/model_symmetrize_joints_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "core/model_geometric_topology.h"
#include "core/model_file_io.h"
#include "core/model_lod.h"
#include "core/model_targets.h"
#include "core/model_transform_axes.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTools)
};
QString plain(QString text)
{
	return text.remove(QLatin1Char('&'));
}
} // namespace

ModelEditorTools::~ModelEditorTools()
{
	if (m_viewportGuard)
	{
		m_viewport->setOverlayPainter({});
		m_viewport->removeEventFilter(this);
	}
}

ModelEditorTools::ModelEditorTools(ModelEditorDialog *editor) : QObject(editor), m_editor(editor), m_viewport(editor->m_preview)
{
	setObjectName(QStringLiteral("meshEditorTools"));
	m_viewportGuard = m_viewport;
	// The 3D tab gets a container holding the tool shelf and the viewport, so
	// tool shortcuts follow keyboard focus inside the modelling area only.
	// The views live in the editor's pane grid (one view or four); the tab
	// page wraps that grid when there is one, else the viewport itself.
	QWidget *views = m_editor->m_paneGrid ? m_editor->m_paneGrid : static_cast<QWidget *>(m_viewport);
	auto *tabs = qobject_cast<QTabWidget *>(views->parentWidget() ? views->parentWidget()->parentWidget() : nullptr);
	m_viewArea = new QWidget;
	m_viewArea->setObjectName(QStringLiteral("meshModellingArea"));
	auto *row = new QHBoxLayout(m_viewArea);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(0);
	if (tabs)
	{
		const QSignalBlocker blocker(tabs);
		const int page = tabs->indexOf(views);
		const auto label = tabs->tabText(page);
		tabs->removeTab(page);
		buildShelf();
		row->addWidget(m_shelf);
		row->addWidget(views, 1);
		// The tab stack hid the page it no longer owns.
		views->show();
		m_viewport->show();
		tabs->insertTab(page, m_viewArea, label);
		tabs->setCurrentIndex(page);
	}
	else
	{
		buildShelf();
		row->addWidget(m_shelf);
	}
	buildActions();
	buildMenus();
	buildHeader();
	buildOverlays();
	makeToolBarKeyboardReachable(m_shelf);
	m_viewport->setMouseTracking(true);
	m_viewport->installEventFilter(this);
	m_viewport->setOverlayPainter([this](QPainter &painter) { paintOverlay(painter); });
	applyControls(m_controls);
	connect(m_editor->m_selectionMode, &QComboBox::currentIndexChanged, this, [this] { refreshHeader(); });
	// Right-click opens the context menu for the current select mode.
	connect(m_viewport, &ModelViewport::contextMenuRequested, this,
			[this](const QPoint &position)
			{
				if (m_modal || m_editor->m_working)
					return;
				const int mode = selectionMode();
				const auto name = mode == 1 ? QStringLiteral("meshMenuVertex") : mode == 2 ? QStringLiteral("meshMenuEdge") : QStringLiteral("meshMenuFace");
				if (auto *menu = m_menuBar->findChild<QMenu *>(name))
					menu->popup(m_viewport->mapToGlobal(position));
			});
	refreshHeader();
	refreshLastPanel();
	documentChanged();
}

QAction *ModelEditorTools::addAction(const QString &name, const QString &text, const QString &shortcut, std::function<void()> run,
									 const QString &icon)
{
	auto *action = new QAction(text, m_viewArea);
	action->setObjectName(name);
	if (!icon.isEmpty())
		action->setIcon(studioIcon(icon));
	// Modeller commands take their keys from the profile; the literal is the
	// key of actions the profiles do not list.
	QStringList portable = shortcut.isEmpty() ? QStringList{} : shortcut.split(QLatin1Char('|'));
	if (modelEditorCommandForId(name))
		portable = modelEditorCommandKeys(m_controls, name);
	QList<QKeySequence> keys;
	for (const auto &part : portable)
		keys.append(QKeySequence::fromString(part, QKeySequence::PortableText));
	action->setShortcuts(keys);
	scopeShortcuts(action);
	connect(action, &QAction::triggered, this,
			[this, run]()
			{
				if (!m_editor->m_working && !m_modal)
					run();
			});
	m_viewArea->addAction(action);
	m_actions.insert(name, action);
	return action;
}

QAction *ModelEditorTools::action(const QString &objectName) const
{
	return m_actions.value(objectName);
}

QMenu *ModelEditorTools::menu(const QString &title, const QStringList &actions)
{
	auto *result = new QMenu(title, m_menuBar);
	for (const auto &name : actions)
	{
		if (name.isEmpty())
			result->addSeparator();
		else if (auto *entry = m_actions.value(name))
			result->addAction(entry);
	}
	return result;
}

void ModelEditorTools::buildActions()
{
	using Op = ModelSelectOperation;
	// Select modes.
	addAction(QStringLiteral("meshModeVertex"), Text::tr("Vertex Select Mode"), QStringLiteral("1"), [this] { setSelectionMode(1); },
			  QStringLiteral("vertex-mode"));
	addAction(QStringLiteral("meshModeEdge"), Text::tr("Edge Select Mode"), QStringLiteral("2"), [this] { setSelectionMode(2); },
			  QStringLiteral("edge-mode"));
	addAction(QStringLiteral("meshModeFace"), Text::tr("Face Select Mode"), QStringLiteral("3"), [this] { setSelectionMode(0); },
			  QStringLiteral("face-mode"));
	// Select menu.
	addAction(QStringLiteral("meshSelectAll"), Text::tr("All"), QStringLiteral("A"), [this] { selectOperation(Op::All); });
	addAction(QStringLiteral("meshSelectNone"), Text::tr("None"), QStringLiteral("Alt+A"), [this] { selectOperation(Op::None); });
	addAction(QStringLiteral("meshSelectInvert"), Text::tr("Invert"), QStringLiteral("Ctrl+I"), [this] { selectOperation(Op::Invert); });
	addAction(QStringLiteral("meshSelectBox"), Text::tr("Box Select"), QStringLiteral("B"),
			  [this]
			  {
				  m_region = Region::Box;
				  m_regionArmed = true;
				  showBanner(Text::tr("Box Select: drag a rectangle. Shift extends, Ctrl subtracts, Esc cancels."));
			  },
			  QStringLiteral("select-box"));
	addAction(QStringLiteral("meshSelectCircle"), Text::tr("Circle Select"), QStringLiteral("C"),
			  [this]
			  {
				  m_circleMode = true;
				  m_pointer = pointerInViewport();
				  showBanner(Text::tr("Circle Select: drag to add, Shift+drag to remove, wheel resizes. Enter, Esc or right-click finishes."));
				  m_viewport->update();
			  });
	addAction(QStringLiteral("meshSelectLinked"), Text::tr("Select Linked"), QStringLiteral("Ctrl+L"), [this] { selectOperation(Op::Linked); });
	addAction(QStringLiteral("meshSelectLinkedPick"), Text::tr("Select Linked Under Pointer"), QStringLiteral("L"),
			  [this] { pickLinked(pointerInViewport(), false); });
	addAction(QStringLiteral("meshSelectLinkedSeams"), Text::tr("Select Linked UV Island"), {},
			  [this] { selectOperation(Op::Linked, [](ModelSelectRequest &r) { r.delimitSeams = true; }); });
	addAction(QStringLiteral("meshSelectMore"), Text::tr("More"), QStringLiteral("Ctrl+=|Ctrl+Num++"), [this] { selectOperation(Op::More); });
	addAction(QStringLiteral("meshSelectLess"), Text::tr("Less"), QStringLiteral("Ctrl+-|Ctrl+Num+-"), [this] { selectOperation(Op::Less); });
	addAction(QStringLiteral("meshSelectLoopPick"), Text::tr("Select Loop Under Pointer (Alt+Click)"), {},
			  [this] { pickLoop(pointerInViewport(), false, false); });
	addAction(QStringLiteral("meshSelectRingPick"), Text::tr("Select Ring Under Pointer (Ctrl+Alt+Click)"), {},
			  [this] { pickLoop(pointerInViewport(), true, false); });
	addAction(QStringLiteral("meshSelectSimilarNormal"), Text::tr("Similar Normal"), {},
			  [this] { selectOperation(Op::Similar, [](ModelSelectRequest &r) { r.similarity = ModelSimilarity::Normal; }); });
	addAction(QStringLiteral("meshSelectSimilarArea"), Text::tr("Similar Area"), {},
			  [this] { selectOperation(Op::Similar, [](ModelSelectRequest &r) { r.similarity = ModelSimilarity::Area; }); });
	addAction(QStringLiteral("meshSelectSimilarCoplanar"), Text::tr("Coplanar"), {},
			  [this] { selectOperation(Op::Similar, [](ModelSelectRequest &r) { r.similarity = ModelSimilarity::Coplanar; }); });
	addAction(QStringLiteral("meshSelectSimilarLength"), Text::tr("Similar Length"), {},
			  [this] { selectOperation(Op::Similar, [](ModelSelectRequest &r) { r.similarity = ModelSimilarity::Length; }); });
	addAction(QStringLiteral("meshSelectSimilarDirection"), Text::tr("Similar Direction"), {},
			  [this] { selectOperation(Op::Similar, [](ModelSelectRequest &r) { r.similarity = ModelSimilarity::Direction; }); });
	addAction(QStringLiteral("meshSelectSimilarFaceAngle"), Text::tr("Similar Face Angle"), {},
			  [this] { selectOperation(Op::Similar, [](ModelSelectRequest &r) { r.similarity = ModelSimilarity::FaceAngle; }); });
	addAction(QStringLiteral("meshSelectSimilarSeam"), Text::tr("Same Seam Marking"), {},
			  [this] { selectOperation(Op::Similar, [](ModelSelectRequest &r) { r.similarity = ModelSimilarity::Seam; }); });
	addAction(QStringLiteral("meshSelectSimilarValence"), Text::tr("Same Edge Count"), {},
			  [this] { selectOperation(Op::Similar, [](ModelSelectRequest &r) { r.similarity = ModelSimilarity::Valence; }); });
	addAction(QStringLiteral("meshSelectNonManifold"), Text::tr("Non-Manifold"), QStringLiteral("Shift+Ctrl+Alt+M"),
			  [this] { selectOperation(Op::NonManifold); });
	addAction(QStringLiteral("meshSelectLoose"), Text::tr("Loose Vertices"), {}, [this] { selectOperation(Op::Loose); });
	addAction(QStringLiteral("meshSelectBoundary"), Text::tr("Boundary Loop"), {}, [this] { selectOperation(Op::Boundary); });
	addAction(QStringLiteral("meshSelectSharp"), Text::tr("Sharp Edges"), {},
			  [this] { selectOperation(Op::Sharp, [](ModelSelectRequest &r) { r.threshold = 30; }); });
	addAction(QStringLiteral("meshSelectRandom"), Text::tr("Random"), {},
			  [this]
			  {
				  static quint32 seed = 0;
				  selectOperation(Op::Random, [](ModelSelectRequest &r) { r.seed = seed++; });
			  });
	addAction(QStringLiteral("meshSelectChecker"), Text::tr("Checker Deselect"), {}, [this] { selectOperation(Op::Checker); });
	addAction(QStringLiteral("meshSelectMirror"), Text::tr("Mirror Selection Across X"), QStringLiteral("Shift+Ctrl+M"),
			  [this] { selectOperation(Op::Mirror, [](ModelSelectRequest &r) { r.axis = 0; }); });
	for (int axis = 0; axis < 3; ++axis)
	{
		for (bool positive : {true, false})
		{
			const QString axisName = QStringLiteral("XYZ").mid(axis, 1);
			addAction(QStringLiteral("meshSelectSide%1%2").arg(positive ? QStringLiteral("Positive") : QStringLiteral("Negative"), axisName),
					  Text::tr("Side %1%2").arg(positive ? QStringLiteral("+") : QStringLiteral("-"), axisName), {},
					  [this, axis, positive]
					  {
						  selectOperation(Op::Side,
										  [axis, positive](ModelSelectRequest &r)
										  {
											  r.axis = axis;
											  r.positive = positive;
											  r.threshold = 0;
										  });
					  });
		}
	}
	// Transform.
	addAction(QStringLiteral("meshMove"), Text::tr("Move"), QStringLiteral("G"), [this] { startModalAtPointer(ModelModalOperator::Move); },
			  QStringLiteral("move"));
	addAction(QStringLiteral("meshRotate"), Text::tr("Rotate"), QStringLiteral("R"), [this] { startModalAtPointer(ModelModalOperator::Rotate); },
			  QStringLiteral("rotate"));
	addAction(QStringLiteral("meshScale"), Text::tr("Scale"), QStringLiteral("S"), [this] { startModalAtPointer(ModelModalOperator::Scale); },
			  QStringLiteral("scale"));
	addAction(QStringLiteral("meshShrinkFatten"), Text::tr("Shrink/Fatten"), QStringLiteral("Alt+S"),
			  [this] { startModalAtPointer(ModelModalOperator::ShrinkFatten); }, QStringLiteral("shrink-fatten"));
	addAction(QStringLiteral("meshSmooth"), Text::tr("Smooth Vertices"), {}, [this] { runKind(ModelEditKind::SmoothVertices); },
			  QStringLiteral("smooth"));
	addAction(QStringLiteral("meshExtrude"), Text::tr("Extrude"), QStringLiteral("E"), [this] { startModalAtPointer(ModelModalOperator::Extrude); },
			  QStringLiteral("extrude"));
	addAction(QStringLiteral("meshInset"), Text::tr("Inset Faces"), QStringLiteral("I"), [this] { startModalAtPointer(ModelModalOperator::Inset); },
			  QStringLiteral("inset"));
	addAction(QStringLiteral("meshInsetIndividual"), Text::tr("Inset Faces Individually"), {},
			  [this] { runKind(ModelEditKind::InsetFaces, [](ModelEdit &e) { e.tool.inset = ModelInsetMode::Individual; }); });
	addAction(QStringLiteral("meshDuplicate"), Text::tr("Duplicate"), QStringLiteral("Shift+D"),
			  [this] { startModalAtPointer(ModelModalOperator::Duplicate); }, QStringLiteral("copy"));
	addAction(QStringLiteral("meshLoopCut"), Text::tr("Loop Cut"), QStringLiteral("Ctrl+R"),
			  [this] { startModalAtPointer(ModelModalOperator::LoopCut); }, QStringLiteral("loop-cut"));
	addAction(QStringLiteral("meshEdgeSlide"), Text::tr("Edge Slide"), {},
			  [this] { startModalAtPointer(ModelModalOperator::EdgeSlide); });
	addAction(QStringLiteral("meshKnife"), Text::tr("Knife"), QStringLiteral("K"),
			  [this] { startModalAtPointer(ModelModalOperator::Knife); }, QStringLiteral("loop-cut"));
	// Mesh / vertex / edge / face tools.
	addAction(QStringLiteral("meshMergeCentre"), Text::tr("Merge at Center"), {}, [this] { runKind(ModelEditKind::MergeVertices); },
			  QStringLiteral("merge"));
	addAction(QStringLiteral("meshMergeCursor"), Text::tr("Merge at Cursor"), {},
			  [this]
			  {
				  runKind(ModelEditKind::MergeVertices,
						  [this](ModelEdit &e)
						  {
							  e.tool.merge = ModelMergeMode::Point;
							  e.tool.point = {m_cursor.x, m_cursor.y, m_cursor.z};
						  });
			  });
	addAction(QStringLiteral("meshMergeCollapse"), Text::tr("Collapse"), {},
			  [this] { runKind(ModelEditKind::MergeVertices, [](ModelEdit &e) { e.tool.merge = ModelMergeMode::Collapse; }); });
	addAction(QStringLiteral("meshMergeDistance"), Text::tr("Merge by Distance"), {}, [this] { m_editor->execute(ModelEditKind::WeldVertices); });
	addAction(QStringLiteral("meshDeleteFaces"), Text::tr("Delete Faces"), {}, [this] { m_editor->execute(ModelEditKind::DeleteFaces); },
			  QStringLiteral("trash"));
	addAction(QStringLiteral("meshDissolveVertices"), Text::tr("Dissolve Vertices"), {}, [this] { runKind(ModelEditKind::DissolveVertices); });
	addAction(QStringLiteral("meshDissolveFaces"), Text::tr("Dissolve Faces"), {}, [this] { runKind(ModelEditKind::DissolveFaces); });
	addAction(QStringLiteral("meshMakeFace"), Text::tr("Make Face"), QStringLiteral("F"), [this] { runKind(ModelEditKind::MakeFace); });
	addAction(QStringLiteral("meshFillHoles"), Text::tr("Fill Boundary Loops"), {}, [this] { m_editor->execute(ModelEditKind::FillBoundaryLoops); });
	addAction(QStringLiteral("meshBridge"), Text::tr("Bridge Edge Loops"), {}, [this] { m_editor->execute(ModelEditKind::BridgeBoundaryLoops); });
	addAction(QStringLiteral("meshRotateEdge"), Text::tr("Rotate Edge"), {}, [this] { runKind(ModelEditKind::RotateEdges); },
			  QStringLiteral("rotate"));
	addAction(QStringLiteral("meshExtrudeEdges"), Text::tr("Extrude Edges"), {},
			  [this] { startModalAtPointer(ModelModalOperator::Extrude); });
	addAction(QStringLiteral("meshSpin"), Text::tr("Spin"), {}, [this] { startModalAtPointer(ModelModalOperator::Spin); });
	addAction(QStringLiteral("meshRip"), Text::tr("Rip"), {}, [this] { startModalAtPointer(ModelModalOperator::Rip); });
	addAction(QStringLiteral("meshRipFill"), Text::tr("Rip and Fill"), {}, [this] { startModalAtPointer(ModelModalOperator::RipFill); });
	addAction(QStringLiteral("meshSpinDuplicate"), Text::tr("Spin Duplicates"), {},
			  [this] { startModalAtPointer(ModelModalOperator::SpinDuplicate); });
	addAction(QStringLiteral("meshSubdivide"), Text::tr("Subdivide"), {}, [this] { m_editor->execute(ModelEditKind::Subdivide); });
	addAction(QStringLiteral("meshSplitEdges"), Text::tr("Split Edges"), {}, [this] { m_editor->execute(ModelEditKind::SplitEdges); });
	addAction(QStringLiteral("meshMarkSeam"), Text::tr("Mark Seam"), {}, [this] { m_editor->execute(ModelEditKind::MarkUvSeams); });
	addAction(QStringLiteral("meshClearSeam"), Text::tr("Clear Seam"), {}, [this] { m_editor->execute(ModelEditKind::ClearUvSeams); });
	addAction(QStringLiteral("meshPoke"), Text::tr("Poke Faces"), {}, [this] { runKind(ModelEditKind::PokeFaces); });
	addAction(QStringLiteral("meshBeautify"), Text::tr("Beautify Faces"), {}, [this] { runKind(ModelEditKind::BeautifyFaces); });
	addAction(QStringLiteral("meshFlip"), Text::tr("Flip Normals"), {}, [this] { m_editor->execute(ModelEditKind::FlipFaces); });
	addAction(QStringLiteral("meshRecalculateNormals"), Text::tr("Recalculate Normals"), QStringLiteral("Shift+N"),
			  [this] { m_editor->execute(ModelEditKind::RecalculateNormals); });
	addAction(QStringLiteral("meshShadeFlat"), Text::tr("Shade Flat"), {}, [this] { runKind(ModelEditKind::ShadeFlat); });
	addAction(QStringLiteral("meshShadeSmooth"), Text::tr("Shade Smooth"), {}, [this] { runKind(ModelEditKind::ShadeSmooth); });
	addAction(QStringLiteral("meshAutoSmooth"), Text::tr("Shade Auto Smooth"), {}, [this] { runKind(ModelEditKind::ShadeAutoSmooth); });
	addAction(QStringLiteral("meshBisect"), Text::tr("Bisect Through 3D Cursor"), {},
			  [this]
			  {
				  runKind(ModelEditKind::Bisect,
						  [this](ModelEdit &e)
						  {
							  e.tool.point = {m_cursor.x, m_cursor.y, m_cursor.z};
							  const auto forward = m_viewport->viewForward();
							  // A view-aligned cut: the plane contains the view's up axis.
							  const auto ray = m_viewport->viewRay(QPointF(m_viewport->width() / 2.0, 0));
							  const auto centre = m_viewport->viewRay(QPointF(m_viewport->width() / 2.0, m_viewport->height() / 2.0));
							  const double ux = ray.origin.x - centre.origin.x, uy = ray.origin.y - centre.origin.y, uz = ray.origin.z - centre.origin.z;
							  e.tool.normal = {uy * forward.z - uz * forward.y, uz * forward.x - ux * forward.z, ux * forward.y - uy * forward.x};
							  if (std::hypot(e.tool.normal[0], e.tool.normal[1], e.tool.normal[2]) < 1e-9)
								  e.tool.normal = {1, 0, 0};
						  });
			  });
	for (int axis = 0; axis < 3; ++axis)
	{
		for (bool positive : {true, false})
		{
			const QString axisName = QStringLiteral("XYZ").mid(axis, 1);
			addAction(QStringLiteral("meshSymmetrize%1%2").arg(positive ? QStringLiteral("Positive") : QStringLiteral("Negative"), axisName),
					  positive ? Text::tr("Symmetrize +%1 to -%1").arg(axisName) : Text::tr("Symmetrize -%1 to +%1").arg(axisName), {},
					  [this, axis, positive]
					  {
						  runKind(ModelEditKind::Symmetrize,
								  [axis, positive](ModelEdit &e)
								  {
									  e.tool.axis = axis;
									  e.tool.positiveToNegative = positive;
								  });
					  },
					  QStringLiteral("mirror"));
		}
	}
	addAction(QStringLiteral("meshBevelVertices"), Text::tr("Bevel Vertices"), QStringLiteral("Ctrl+Shift+B"),
			  [this] { startModalAtPointer(ModelModalOperator::BevelVertices); });
	addAction(QStringLiteral("meshBevelEdges"), Text::tr("Bevel Edges"), QStringLiteral("Ctrl+B"),
			  [this] { startModalAtPointer(ModelModalOperator::BevelEdges); });
	addAction(QStringLiteral("meshSolidify"), Text::tr("Solidify"), {},
			  [this] { runKind(ModelEditKind::Solidify, [](ModelEdit &e) { e.tool.solidifyThickness = 2; }); });
	// UV mapping (U), as Blender's UV menu. Projections split UVs only where
	// the mapping changes; Unwrap and Pack use the UV panel's settings.
	const auto projectUvs = [this](ModelUvProjection projection, bool fit)
	{
		runKind(ModelEditKind::ProjectUvMapping,
				[this, projection, fit](ModelEdit &e)
				{
					e.tool.uvProjection = projection;
					e.tool.uvFit = fit;
					e.tool.axis = 2;
					ModelVec3 right, up;
					m_viewport->viewAxes(&right, &up);
					e.tool.uvAxisU = {right.x, right.y, right.z};
					e.tool.uvAxisV = {up.x, up.y, up.z};
				});
	};
	addAction(QStringLiteral("meshUvCube"), Text::tr("Cube Projection"), {}, [projectUvs] { projectUvs(ModelUvProjection::Cube, false); },
			  QStringLiteral("cube"));
	addAction(QStringLiteral("meshUvCylinder"), Text::tr("Cylinder Projection"), {},
			  [projectUvs] { projectUvs(ModelUvProjection::Cylinder, false); });
	addAction(QStringLiteral("meshUvSphere"), Text::tr("Sphere Projection"), {}, [projectUvs] { projectUvs(ModelUvProjection::Sphere, false); });
	addAction(QStringLiteral("meshUvView"), Text::tr("Project From View"), {}, [projectUvs] { projectUvs(ModelUvProjection::View, false); });
	addAction(QStringLiteral("meshUvViewBounds"), Text::tr("Project From View (Bounds)"), {},
			  [projectUvs] { projectUvs(ModelUvProjection::View, true); });
	addAction(QStringLiteral("meshUnwrap"), Text::tr("Unwrap and Pack"), {}, [this] { m_editor->execute(ModelEditKind::UnwrapUv); });
	addAction(QStringLiteral("meshUvDensity"), Text::tr("Texel Density…"), {}, [this] { m_editor->editUvDensity(); });
	addAction(QStringLiteral("meshPackIslands"), Text::tr("Pack Islands"), {}, [this] { m_editor->execute(ModelEditKind::PackUv); });
	addAction(QStringLiteral("meshExportLods"), Text::tr("Export Quake III Detail Levels…"), {},
			  [this]
			  {
				  ModelTargetProfile target;
				  findModelTargetProfile(QStringLiteral("md3"), m_editor->deliveryTarget(QStringLiteral("md3")), &target);
				  const int levels = std::min({2, modelLodMaxLevels, std::max(0, target.limits.detailLevels)});
				  if (levels < 1)
				  {
					  m_editor->m_status->setText(Text::tr("%1 loads no detail levels beside the model.").arg(target.label));
					  return;
				  }
				  const auto path = QFileDialog::getSaveFileName(m_editor, Text::tr("Export Quake III Detail Levels"), {},
																 Text::tr("Quake III model (*.md3)"));
				  if (path.isEmpty())
					  return;
				  QString error;
				  // The levels the source stores, else two halvings.
				  const auto &recipe = m_editor->m_document.mesh().detailLevels;
				  if (recipe.isEmpty())
					  exportDetailLevels(path, levels, 0.5, &error);
				  else
					  exportDetailLevels(path, recipe, &error);
			  },
			  QStringLiteral("export"));
	addAction(QStringLiteral("meshDetailLevels"), Text::tr("Detail Levels…"), {}, [this] { m_editor->reviewDetailLevels(); });
	addAction(QStringLiteral("meshDecimate"), Text::tr("Decimate (Half)"), {}, [this] { runKind(ModelEditKind::Decimate); });
	// The Quake III working limit: RB_CheckOverflow drops a surface of 2,000.
	addAction(QStringLiteral("meshDecimateMd3"), Text::tr("Decimate to MD3 Budget (1,999 Triangles)"), {},
			  [this] { runKind(ModelEditKind::Decimate, [](ModelEdit &e) { e.tool.targetTriangles = 1999; }); });
	addAction(QStringLiteral("meshCheckTargets"), Text::tr("Check Delivery Targets…"), {}, [this] { m_editor->checkDeliveryTargets(); });
	// Add (Shift+A).
	const QVector<QPair<ModelPrimitive, QString>> primitives{
		{ModelPrimitive::Plane, QStringLiteral("Plane")},		  {ModelPrimitive::Cube, QStringLiteral("Cube")},
		{ModelPrimitive::Circle, QStringLiteral("Circle")},		  {ModelPrimitive::Grid, QStringLiteral("Grid")},
		{ModelPrimitive::Cylinder, QStringLiteral("Cylinder")},	  {ModelPrimitive::Cone, QStringLiteral("Cone")},
		{ModelPrimitive::UvSphere, QStringLiteral("UvSphere")},	  {ModelPrimitive::IcoSphere, QStringLiteral("IcoSphere")},
		{ModelPrimitive::Torus, QStringLiteral("Torus")}};
	for (const auto &[primitive, name] : primitives)
		addAction(QStringLiteral("meshAdd") + name, modelPrimitiveName(primitive), {}, [this, primitive] { addPrimitive(primitive); },
				  primitive == ModelPrimitive::Cube ? QStringLiteral("cube") : QString());
	// Cursor and snapping (Shift+S).
	addAction(QStringLiteral("meshCursorToSelected"), Text::tr("Cursor to Selected"), {},
			  [this]
			  {
				  ModelVec3 centre;
				  if (selectionCentre(&centre))
					  setCursor3d(centre);
			  },
			  QStringLiteral("crosshair"));
	addAction(QStringLiteral("meshCursorToOrigin"), Text::tr("Cursor to World Origin"), QStringLiteral("Shift+C"),
			  [this] { setCursor3d({}); });
	addAction(QStringLiteral("meshCursorToGrid"), Text::tr("Cursor to Grid"), {},
			  [this]
			  {
				  const double step = std::max(m_editor->m_translationGrid->value(), 0.000001);
				  setCursor3d({float(std::round(m_cursor.x / step) * step), float(std::round(m_cursor.y / step) * step),
							   float(std::round(m_cursor.z / step) * step)});
			  });
	addAction(QStringLiteral("meshSelectionToCursor"), Text::tr("Selection to Cursor"), {},
			  [this]
			  {
				  ModelVec3 centre;
				  if (!selectionCentre(&centre))
					  return;
				  auto edit = toolEdit(ModelEditKind::Transform);
				  edit.translation = {m_cursor.x - centre.x, m_cursor.y - centre.y, m_cursor.z - centre.z};
				  edit.transformSpace = ModelTransformSpace::World;
				  QString error;
				  if (!runTool(edit, &error))
					  showBanner(error, {}, LiveStatus::Problem);
			  });
	addAction(QStringLiteral("meshPivotCursor"), Text::tr("Pivot at 3D Cursor"), {},
			  [this]
			  {
				  m_editor->m_pivotMode->setCurrentIndex(2);
				  m_editor->m_pivot[0]->setValue(m_cursor.x);
				  m_editor->m_pivot[1]->setValue(m_cursor.y);
				  m_editor->m_pivot[2]->setValue(m_cursor.z);
				  showBanner(Text::tr("Transforms now pivot around the 3D cursor."));
			  });
	addAction(QStringLiteral("meshPivotSelection"), Text::tr("Pivot at Selection Center"), {},
			  [this] { m_editor->m_pivotMode->setCurrentIndex(1); });
	// View.
	addAction(QStringLiteral("meshViewAll"), Text::tr("Frame All"), QStringLiteral("Home"), [this] { m_viewport->frameModel(); },
			  QStringLiteral("frame"));
	addAction(QStringLiteral("meshViewSelected"), Text::tr("Frame Selected"), QStringLiteral("Num+.|Num+Del"), [this] { frameSelection(); });
	addAction(QStringLiteral("meshViewFront"), Text::tr("Front"), QStringLiteral("Num+1"), [this] { alignView(1, false); });
	addAction(QStringLiteral("meshViewBack"), Text::tr("Back"), QStringLiteral("Ctrl+Num+1"), [this] { alignView(1, true); });
	addAction(QStringLiteral("meshViewRight"), Text::tr("Right"), QStringLiteral("Num+3"), [this] { alignView(0, true); });
	addAction(QStringLiteral("meshViewLeft"), Text::tr("Left"), QStringLiteral("Ctrl+Num+3"), [this] { alignView(0, false); });
	addAction(QStringLiteral("meshViewTop"), Text::tr("Top"), QStringLiteral("Num+7"), [this] { alignView(2, true); });
	addAction(QStringLiteral("meshViewBottom"), Text::tr("Bottom"), QStringLiteral("Ctrl+Num+7"), [this] { alignView(2, false); });
	addAction(QStringLiteral("meshViewOpposite"), Text::tr("Opposite View"), QStringLiteral("Num+9"),
			  [this] { m_viewport->setOrbit(m_viewport->yaw() + 180, -m_viewport->pitch()); });
	for (const auto &[name, key, yaw, pitch] :
		 {std::tuple{QStringLiteral("meshOrbitLeft"), QStringLiteral("Num+4"), -15.0, 0.0},
		  std::tuple{QStringLiteral("meshOrbitRight"), QStringLiteral("Num+6"), 15.0, 0.0},
		  std::tuple{QStringLiteral("meshOrbitUp"), QStringLiteral("Num+8"), 0.0, 15.0},
		  std::tuple{QStringLiteral("meshOrbitDown"), QStringLiteral("Num+2"), 0.0, -15.0}})
	{
		const QString text = name == QStringLiteral("meshOrbitLeft")	? Text::tr("Orbit Left")
							 : name == QStringLiteral("meshOrbitRight") ? Text::tr("Orbit Right")
							 : name == QStringLiteral("meshOrbitUp")	? Text::tr("Orbit Up")
																		: Text::tr("Orbit Down");
		addAction(name, text, key, [this, yaw, pitch] { m_viewport->setOrbit(m_viewport->yaw() + yaw, std::clamp(m_viewport->pitch() + pitch, -90.0, 90.0)); });
	}
	addAction(QStringLiteral("meshViewPerspective"), Text::tr("Perspective/Orthographic"), QStringLiteral("Num+5"),
			  [this] { m_editor->m_viewPreset->setCurrentIndex(m_viewport->isPerspective() ? 0 : 4); });
	addAction(QStringLiteral("meshShadingWire"), Text::tr("Wireframe"), {}, [this] { m_editor->m_renderMode->setCurrentIndex(1); });
	addAction(QStringLiteral("meshShadingSolid"), Text::tr("Solid"), {}, [this] { m_editor->m_renderMode->setCurrentIndex(0); });
	addAction(QStringLiteral("meshShadingMaterial"), Text::tr("Material Preview"), {}, [this] { m_editor->m_renderMode->setCurrentIndex(3); });
	addAction(QStringLiteral("meshShadingChecker"), Text::tr("UV Checker"), {}, [this] { m_editor->m_renderMode->setCurrentIndex(2); });
	addAction(QStringLiteral("meshToggleXray"), Text::tr("Toggle X-Ray"), QStringLiteral("Alt+Z"), [this] { m_xray->toggle(); });
	addAction(QStringLiteral("meshToggleProportional"), Text::tr("Proportional Editing"), QStringLiteral("O"),
			  [this] { m_proportional->toggle(); }, QStringLiteral("proportional"));
	addAction(QStringLiteral("meshCycleFalloff"), Text::tr("Next Proportional Falloff"), QStringLiteral("Shift+O"),
			  [this] { m_falloff->setCurrentIndex((m_falloff->currentIndex() + 1) % m_falloff->count()); });
	auto *gizmo = addAction(QStringLiteral("meshToggleNavigationGizmo"), Text::tr("Navigation Gizmo"), {},
							[this]
							{
								m_showGizmo = m_actions.value(QStringLiteral("meshToggleNavigationGizmo"))->isChecked();
								m_viewport->update();
							});
	gizmo->setCheckable(true);
	gizmo->setChecked(true);
	auto *navigation = addAction(QStringLiteral("meshBlenderNavigation"), Text::tr("Blender Navigation (Middle-Drag Orbit)"), {},
								 [this] { setBlenderNavigation(m_actions.value(QStringLiteral("meshBlenderNavigation"))->isChecked()); });
	navigation->setCheckable(true);
	navigation->setChecked(true);
	// Operators about operators.
	addAction(QStringLiteral("meshOperatorSearch"), Text::tr("Search Commands…"), QStringLiteral("F3"), [this] { showOperatorSearch(); },
			  QStringLiteral("search"));
	addAction(QStringLiteral("meshAdjustLast"), Text::tr("Adjust Last Operation"), QStringLiteral("F9"),
			  [this]
			  {
				  const bool show = !m_lastPanel->isVisible() || !m_hasLast;
				  const bool focused = m_lastPanel->isAncestorOf(QApplication::focusWidget());
				  m_lastPanel->setVisible(show);
				  refreshLastPanel();
				  // F9 takes keyboard focus to the settings and back to the view.
				  if (show)
					  focusLastOperation();
				  else if (focused)
					  m_viewport->setFocus(Qt::ShortcutFocusReason);
			  });
	addAction(QStringLiteral("meshRepeatLast"), Text::tr("Repeat Last"), QStringLiteral("Shift+R"),
			  [this]
			  {
				  if (!m_hasLast)
					  return;
				  auto edit = m_last.edit;
				  edit.selection = selectionFor(edit.kind);
				  // The joint map and binding were confirmed for the last target:
				  // confirm them again for this one, as the menu does.
				  if ((edit.kind == ModelEditKind::Symmetrize && !confirmMirrorJoints(edit)) ||
					  (edit.kind == ModelEditKind::AddPrimitive && !confirmPrimitiveBinding(edit)))
					  return;
				  QString error;
				  if (!runTool(edit, &error))
					  showBanner(error, {}, LiveStatus::Problem);
			  },
			  QStringLiteral("repeat"));
	// Context menus opened from the keyboard.
	addAction(QStringLiteral("meshAddMenu"), Text::tr("Add Menu"), QStringLiteral("Shift+A"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuAdd"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshDeleteMenu"), Text::tr("Delete Menu"), QStringLiteral("X|Del"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuDelete"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshMergeMenu"), Text::tr("Merge Menu"), QStringLiteral("M"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuMerge"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshSnapMenu"), Text::tr("Snap Menu"), QStringLiteral("Shift+S"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuSnap"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshVertexMenu"), Text::tr("Vertex Menu"), QStringLiteral("Ctrl+V"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuVertex"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshEdgeMenu"), Text::tr("Edge Menu"), QStringLiteral("Ctrl+E"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuEdge"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshFaceMenu"), Text::tr("Face Menu"), QStringLiteral("Ctrl+F"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuFace"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshNormalsMenu"), Text::tr("Normals Menu"), QStringLiteral("Alt+N"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuNormals"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshSimilarMenu"), Text::tr("Select Similar Menu"), QStringLiteral("Shift+G"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuSimilar"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshShadingMenu"), Text::tr("Shading Menu"), QStringLiteral("Z"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuShading"))->popup(QCursor::pos()); });
	addAction(QStringLiteral("meshUvMenu"), Text::tr("UV Mapping Menu"), QStringLiteral("U"),
			  [this] { m_menuBar->findChild<QMenu *>(QStringLiteral("meshMenuUv"))->popup(QCursor::pos()); });
	buildProfileActions();
	buildFocusActions();
}

void ModelEditorTools::buildMenus()
{
	m_menuBar = new QMenuBar;
	m_menuBar->setObjectName(QStringLiteral("meshMenuBar"));
	m_menuBar->setNativeMenuBar(false);
	m_menuBar->setAccessibleName(Text::tr("Mesh Editor menus"));
	// A menu bar's own policy keeps every menu in view, so at large text sizes
	// it would hold the editor wider than the window (right to left, as wide
	// as its parent). Menus that do not fit move under its overflow button.
	m_menuBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	const auto named = [](QMenu *menu, const QString &name)
	{
		menu->setObjectName(name);
		return menu;
	};
	auto *view = named(menu(Text::tr("&View"), {QStringLiteral("meshViewAll"), QStringLiteral("meshViewSelected"), {}, QStringLiteral("meshViewFront"),
												 QStringLiteral("meshViewBack"), QStringLiteral("meshViewRight"), QStringLiteral("meshViewLeft"),
												 QStringLiteral("meshViewTop"), QStringLiteral("meshViewBottom"), QStringLiteral("meshViewOpposite"),
												 QStringLiteral("meshViewPerspective"), {}, QStringLiteral("meshOrbitLeft"), QStringLiteral("meshOrbitRight"),
												 QStringLiteral("meshOrbitUp"), QStringLiteral("meshOrbitDown"), {}, QStringLiteral("meshToggleXray"),
												 QStringLiteral("meshToggleNavigationGizmo"), QStringLiteral("meshBlenderNavigation"), {},
												 QStringLiteral("meshOperatorSearch"), QStringLiteral("meshAdjustLast"), {},
												 QStringLiteral("meshFocusNextPanel"), QStringLiteral("meshFocusPreviousPanel"), {},
												 QStringLiteral("meshToggleQuadView"), QStringLiteral("meshMaximizeView"),
												 QStringLiteral("meshNextView"), QStringLiteral("meshPreviousView")}),
					   QStringLiteral("meshMenuView"));
	auto *shading = named(menu(Text::tr("Shading"), {QStringLiteral("meshShadingWire"), QStringLiteral("meshShadingSolid"),
													 QStringLiteral("meshShadingMaterial"), QStringLiteral("meshShadingChecker")}),
						  QStringLiteral("meshMenuShading"));
	view->insertMenu(m_actions.value(QStringLiteral("meshToggleXray")), shading);
	// The document's own menu leads, as Blender's image editor leads with Image.
	m_menuBar->addMenu(m_editor->m_modelMenu);
	m_menuBar->addMenu(view);
	auto *similar = named(menu(Text::tr("Select Similar"), {QStringLiteral("meshSelectSimilarNormal"), QStringLiteral("meshSelectSimilarArea"),
															 QStringLiteral("meshSelectSimilarCoplanar"), {}, QStringLiteral("meshSelectSimilarLength"),
															 QStringLiteral("meshSelectSimilarDirection"), QStringLiteral("meshSelectSimilarFaceAngle"),
															 QStringLiteral("meshSelectSimilarSeam"), {}, QStringLiteral("meshSelectSimilarValence")}),
						  QStringLiteral("meshMenuSimilar"));
	auto *side = named(menu(Text::tr("Side of Axis"), {QStringLiteral("meshSelectSidePositiveX"), QStringLiteral("meshSelectSideNegativeX"),
													   QStringLiteral("meshSelectSidePositiveY"), QStringLiteral("meshSelectSideNegativeY"),
													   QStringLiteral("meshSelectSidePositiveZ"), QStringLiteral("meshSelectSideNegativeZ")}),
					   QStringLiteral("meshMenuSide"));
	auto *select = named(menu(Text::tr("&Select"), {QStringLiteral("meshSelectAll"), QStringLiteral("meshSelectNone"), QStringLiteral("meshSelectInvert"),
													 {}, QStringLiteral("meshSelectBox"), QStringLiteral("meshSelectCircle"), {},
													 QStringLiteral("meshSelectMore"), QStringLiteral("meshSelectLess"), {},
													 QStringLiteral("meshSelectLinked"), QStringLiteral("meshSelectLinkedPick"),
													 QStringLiteral("meshSelectLinkedSeams"), {}, QStringLiteral("meshSelectLoopPick"),
													 QStringLiteral("meshSelectRingPick"), QStringLiteral("meshSelectLoop"),
													 QStringLiteral("meshSelectRing"), QStringLiteral("meshSelectBoundary"), {},
													 QStringLiteral("meshSelectSharp"), QStringLiteral("meshSelectNonManifold"),
													 QStringLiteral("meshSelectLoose"), {}, QStringLiteral("meshSelectRandom"),
													 QStringLiteral("meshSelectChecker"), QStringLiteral("meshSelectMirror")}),
						 QStringLiteral("meshMenuSelect"));
	select->insertMenu(m_actions.value(QStringLiteral("meshSelectSharp")), similar);
	select->addMenu(side);
	m_menuBar->addMenu(select);
	auto *add = named(menu(Text::tr("&Add"), {QStringLiteral("meshAddPlane"), QStringLiteral("meshAddCube"), QStringLiteral("meshAddCircle"),
											  QStringLiteral("meshAddGrid"), QStringLiteral("meshAddCylinder"), QStringLiteral("meshAddCone"),
											  QStringLiteral("meshAddUvSphere"), QStringLiteral("meshAddIcoSphere"), QStringLiteral("meshAddTorus")}),
					  QStringLiteral("meshMenuAdd"));
	m_menuBar->addMenu(add);
	auto *snap = named(menu(Text::tr("Snap"), {QStringLiteral("meshCursorToSelected"), QStringLiteral("meshCursorToOrigin"),
											   QStringLiteral("meshCursorToGrid"), {}, QStringLiteral("meshSelectionToCursor"), {},
											   QStringLiteral("meshPivotCursor"), QStringLiteral("meshPivotSelection")}),
					   QStringLiteral("meshMenuSnap"));
	auto *merge = named(menu(Text::tr("Merge"), {QStringLiteral("meshMergeCentre"), QStringLiteral("meshMergeCursor"),
												 QStringLiteral("meshMergeCollapse"), QStringLiteral("meshMergeDistance")}),
						QStringLiteral("meshMenuMerge"));
	auto *remove = named(menu(Text::tr("Delete"), {QStringLiteral("meshDeleteFaces"), QStringLiteral("meshDissolveVertices"),
												   QStringLiteral("meshDissolveFaces"), QStringLiteral("meshMergeCollapse")}),
						 QStringLiteral("meshMenuDelete"));
	auto *normals = named(menu(Text::tr("Normals"), {QStringLiteral("meshFlip"), QStringLiteral("meshRecalculateNormals"), {},
													 QStringLiteral("meshShadeSmooth"), QStringLiteral("meshShadeFlat"), QStringLiteral("meshAutoSmooth")}),
						  QStringLiteral("meshMenuNormals"));
	auto *symmetry = named(menu(Text::tr("Symmetrize"), {QStringLiteral("meshSymmetrizePositiveX"), QStringLiteral("meshSymmetrizeNegativeX"),
														  QStringLiteral("meshSymmetrizePositiveY"), QStringLiteral("meshSymmetrizeNegativeY"),
														  QStringLiteral("meshSymmetrizePositiveZ"), QStringLiteral("meshSymmetrizeNegativeZ")}),
						   QStringLiteral("meshMenuSymmetrize"));
	auto *cleanup = named(menu(Text::tr("Clean Up"), {QStringLiteral("meshDecimate"), QStringLiteral("meshDecimateMd3"),
													  QStringLiteral("meshBeautify"), QStringLiteral("meshMergeDistance"), {},
													  QStringLiteral("meshCheckTargets"), QStringLiteral("meshDetailLevels"), QStringLiteral("meshExportLods")}),
						  QStringLiteral("meshMenuCleanUp"));
	auto *mesh = named(menu(Text::tr("&Mesh"), {QStringLiteral("meshMove"), QStringLiteral("meshRotate"), QStringLiteral("meshScale"),
												 QStringLiteral("meshShrinkFatten"), QStringLiteral("meshSmooth"), {},
												 QStringLiteral("meshDuplicate"), QStringLiteral("meshExtrude"), QStringLiteral("meshKnife"), QStringLiteral("meshBisect"), {},
												 QStringLiteral("meshToggleProportional"), QStringLiteral("meshCycleFalloff"), {},
												 QStringLiteral("meshRepeatLast"), QStringLiteral("meshAdjustLast")}),
					   QStringLiteral("meshMenuMesh"));
	mesh->addSeparator();
	mesh->addMenu(snap);
	mesh->addMenu(merge);
	mesh->addMenu(remove);
	mesh->addMenu(symmetry);
	mesh->addMenu(normals);
	mesh->addMenu(cleanup);
	m_menuBar->addMenu(mesh);
	m_menuBar->addMenu(named(menu(Text::tr("&Vertex"), {QStringLiteral("meshMakeFace"), QStringLiteral("meshBevelVertices"), QStringLiteral("meshMergeCentre"),
														 QStringLiteral("meshRip"), QStringLiteral("meshRipFill"),
														 QStringLiteral("meshMergeCursor"), QStringLiteral("meshDissolveVertices"),
														 QStringLiteral("meshSmooth"), QStringLiteral("meshMergeDistance")}),
							 QStringLiteral("meshMenuVertex")));
	m_menuBar->addMenu(named(menu(Text::tr("&Edge"), {QStringLiteral("meshLoopCut"), QStringLiteral("meshRotateEdge"), QStringLiteral("meshExtrudeEdges"),
													   QStringLiteral("meshRip"), QStringLiteral("meshRipFill"),
													   QStringLiteral("meshSpin"), QStringLiteral("meshEdgeSlide"), QStringLiteral("meshBevelEdges"), QStringLiteral("meshSplitEdges"), QStringLiteral("meshBridge"), QStringLiteral("meshFillHoles"), {},
													   QStringLiteral("meshMarkSeam"), QStringLiteral("meshClearSeam"), {},
													   QStringLiteral("meshSelectLoopPick"), QStringLiteral("meshSelectRingPick"),
													   QStringLiteral("meshSelectLoop"), QStringLiteral("meshSelectRing")}),
							 QStringLiteral("meshMenuEdge")));
	m_menuBar->addMenu(named(menu(Text::tr("&Face"), {QStringLiteral("meshExtrude"), QStringLiteral("meshInset"), QStringLiteral("meshInsetIndividual"),
													   QStringLiteral("meshSpin"), QStringLiteral("meshSpinDuplicate"), QStringLiteral("meshPoke"), QStringLiteral("meshSolidify"), QStringLiteral("meshSubdivide"), QStringLiteral("meshDissolveFaces"),
													   QStringLiteral("meshBeautify"), QStringLiteral("meshMakeFace"), {}, QStringLiteral("meshFlip"),
													   QStringLiteral("meshShadeSmooth"), QStringLiteral("meshShadeFlat"), QStringLiteral("meshAutoSmooth"), {},
													   QStringLiteral("meshDeleteFaces")}),
							 QStringLiteral("meshMenuFace")));
	m_menuBar->addMenu(named(menu(Text::tr("&UV"), {QStringLiteral("meshUnwrap"), {}, QStringLiteral("meshUvCube"), QStringLiteral("meshUvCylinder"),
													 QStringLiteral("meshUvSphere"), {}, QStringLiteral("meshUvView"), QStringLiteral("meshUvViewBounds"), {},
													 QStringLiteral("meshMarkSeam"), QStringLiteral("meshClearSeam"), {}, QStringLiteral("meshUvDensity"), QStringLiteral("meshPackIslands")}),
							 QStringLiteral("meshMenuUv")));
	if (auto *layout = qobject_cast<QBoxLayout *>(m_editor->layout()))
		layout->insertWidget(0, m_menuBar);
}

void ModelEditorTools::buildHeader()
{
	// A tool bar, so a narrow window folds the trailing controls into its
	// overflow menu instead of forcing the editor wider.
	auto *bar = new QToolBar;
	m_header = bar;
	m_header->setObjectName(QStringLiteral("meshToolHeader"));
	m_header->setAccessibleName(Text::tr("Mesh editing header"));
	bar->setFloatable(false);
	bar->setMovable(false);
	struct HeaderRow
	{
		QToolBar *bar;
		void addWidget(QWidget *widget) { bar->addWidget(widget); }
		void addStretch()
		{
			auto *spacer = new QWidget;
			spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
			bar->addWidget(spacer);
		}
	} headerRow{bar};
	auto *row = &headerRow;
	// Undo and Redo lead the header, one click away whatever the profile.
	bar->addAction(m_editor->m_undo);
	bar->addAction(m_editor->m_redo);
	auto *historyDivider = new QFrame;
	historyDivider->setFrameShape(QFrame::VLine);
	row->addWidget(historyDivider);
	// Tooltips and accessible descriptions name the command's keys in the
	// current profile (refreshKeyHints); accessible names stay key-free.
	const auto toolButton = [&](const QString &name, const QString &icon, const QString &title, const QString &detail, const QString &command)
	{
		auto *button = new QToolButton;
		button->setObjectName(name);
		button->setIcon(studioIcon(icon, StudioIconTone::Navigation));
		button->setToolTip(title);
		button->setAccessibleName(title);
		button->setCheckable(true);
		button->setAutoRaise(true);
		button->setFocusPolicy(Qt::TabFocus);
		row->addWidget(button);
		m_keyHints.append({button, command, title, detail});
		return button;
	};
	const QString modes[3]{QStringLiteral("vertex-mode"), QStringLiteral("edge-mode"), QStringLiteral("face-mode")};
	const QString modeNames[3]{Text::tr("Vertex select mode"), Text::tr("Edge select mode"), Text::tr("Face select mode")};
	const QString modeCommands[3]{QStringLiteral("meshModeVertex"), QStringLiteral("meshModeEdge"), QStringLiteral("meshModeFace")};
	for (int i = 0; i < 3; ++i)
	{
		m_modeButtons[i] = toolButton(QStringLiteral("meshSelectMode%1").arg(i), modes[i], modeNames[i], {}, modeCommands[i]);
		connect(m_modeButtons[i], &QToolButton::clicked, this, [this, i] { setSelectionMode(i == 2 ? 0 : i + 1); });
	}
	auto *divider = new QFrame;
	divider->setFrameShape(QFrame::VLine);
	row->addWidget(divider);
	m_xray = toolButton(QStringLiteral("meshXray"), QStringLiteral("eye"), Text::tr("X-ray"),
						Text::tr("Box, circle and lasso selection and vertex picking reach hidden components."), QStringLiteral("meshToggleXray"));
	connect(m_xray, &QToolButton::toggled, this,
			[this](bool on)
			{
				m_editor->m_xrayVertices->setChecked(on);
				m_viewport->setXrayVertices(on);
				showBanner(on ? Text::tr("X-ray on: selection reaches hidden components.") : Text::tr("X-ray off."));
			});
	auto *divider2 = new QFrame;
	divider2->setFrameShape(QFrame::VLine);
	row->addWidget(divider2);
	// Shading, the view preset and Frame Model follow the selection
	// controls, so they stay in view before the proportional settings fold
	// into the overflow menu.
	row->addWidget(m_editor->m_renderMode);
	row->addWidget(m_editor->m_viewPreset);
	if (auto *frame = m_editor->findChild<QAction *>(QStringLiteral("meshFrameModel")))
		bar->addAction(frame);
	auto *viewDivider = new QFrame;
	viewDivider->setFrameShape(QFrame::VLine);
	row->addWidget(viewDivider);
	m_proportional = toolButton(QStringLiteral("meshProportional"), QStringLiteral("proportional"), Text::tr("Proportional editing"),
								Text::tr("Move, rotate and scale nearby vertices with a falloff."), QStringLiteral("meshToggleProportional"));
	m_falloff = new QComboBox;
	m_falloff->setObjectName(QStringLiteral("meshProportionalFalloff"));
	m_falloff->setAccessibleName(Text::tr("Proportional falloff"));
	m_falloff->addItems({Text::tr("Smooth"), Text::tr("Sphere"), Text::tr("Root"), Text::tr("Inverse Square"), Text::tr("Sharp"),
						 Text::tr("Linear"), Text::tr("Constant")});
	row->addWidget(m_falloff);
	m_radius = new QDoubleSpinBox;
	m_radius->setObjectName(QStringLiteral("meshProportionalRadius"));
	m_radius->setAccessibleName(Text::tr("Proportional radius in model units"));
	m_radius->setToolTip(Text::tr("Proportional radius. The mouse wheel or Page Up/Down changes it during a transform."));
	m_radius->setRange(0.01, 100000);
	m_radius->setDecimals(2);
	m_radius->setValue(32);
	m_radius->setLayoutDirection(Qt::LeftToRight);
	row->addWidget(m_radius);
	m_snapElement = new QComboBox;
	m_snapElement->setObjectName(QStringLiteral("meshSnapElement"));
	m_snapElement->setAccessibleName(Text::tr("Snap to"));
	m_snapElement->setToolTip(Text::tr("While snapping (the Snap toggle, or Ctrl held during a move), snap to grid increments or "
									   "make the nearest moving vertex land on the vertex under the pointer."));
	m_snapElement->addItems({Text::tr("Snap: Increment"), Text::tr("Snap: Vertex")});
	m_connected = new QCheckBox(Text::tr("Connected"));
	m_connected->setObjectName(QStringLiteral("meshProportionalConnected"));
	m_connected->setToolTip(Text::tr("Measure the falloff along the mesh instead of straight through space."));
	row->addWidget(m_connected);
	row->addWidget(m_snapElement);
	connect(m_proportional, &QToolButton::toggled, this,
			[this](bool on)
			{
				m_falloff->setEnabled(on);
				m_radius->setEnabled(on);
				m_connected->setEnabled(on);
				showBanner(on ? Text::tr("Proportional editing on (%1, radius %2).").arg(m_falloff->currentText(), localeNumber(m_radius->value()))
							  : Text::tr("Proportional editing off."));
			});
	m_falloff->setEnabled(false);
	m_radius->setEnabled(false);
	m_connected->setEnabled(false);
	auto *divider3 = new QFrame;
	divider3->setFrameShape(QFrame::VLine);
	row->addWidget(divider3);
	auto *symmetry = new QLabel(Text::tr("Symmetry"));
	row->addWidget(symmetry);
	for (int axis = 0; axis < 3; ++axis)
	{
		const QString name = QStringLiteral("XYZ").mid(axis, 1);
		auto *button = new QToolButton;
		button->setObjectName(QStringLiteral("meshMirror%1").arg(name));
		button->setText(name);
		button->setCheckable(true);
		button->setAutoRaise(true);
		button->setToolTip(Text::tr("Mesh symmetry across %1: moving a vertex also moves its mirror counterpart.").arg(name));
		button->setAccessibleName(Text::tr("Mirror editing across %1").arg(name));
		row->addWidget(button);
		m_mirror[axis] = button;
	}
	m_budget = new QToolButton;
	m_budget->setObjectName(QStringLiteral("meshExportBudget"));
	m_budget->setAccessibleName(Text::tr("Delivery targets"));
	m_budget->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_budget->setPopupMode(QToolButton::InstantPopup);
	m_budget->setAutoRaise(true);
	m_budget->setMenu(new QMenu(m_budget));
	row->addWidget(m_budget);
	auto *search = new QToolButton;
	search->setObjectName(QStringLiteral("meshSearchButton"));
	search->setDefaultAction(m_actions.value(QStringLiteral("meshOperatorSearch")));
	search->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	search->setAutoRaise(true);
	row->addWidget(search);
	// The editing header heads the column of views, above material loading
	// and the 3D and UV tabs; the overlay toggles join the gizmo settings on
	// the Tool page.
	row->addStretch();
	auto *tabs = m_viewArea->parentWidget() ? m_viewArea->parentWidget()->parentWidget() : nullptr;
	auto *centre = tabs ? tabs->parentWidget() : nullptr;
	if (auto *layout = centre ? qobject_cast<QBoxLayout *>(centre->layout()) : nullptr)
		layout->insertWidget(0, m_header);
	auto *rows = m_editor->m_moveGizmo->parentWidget() ? qobject_cast<QVBoxLayout *>(m_editor->m_moveGizmo->parentWidget()->layout()) : nullptr;
	auto *transformRow = rows && rows->count() > 0 ? qobject_cast<QBoxLayout *>(rows->itemAt(0)->layout()) : nullptr;
	if (transformRow)
	{
		transformRow->insertWidget(std::max(0, transformRow->count() - 1), m_editor->m_showTags);
		// The header's X-ray button drives the vertex X-ray setting.
		m_editor->m_xrayVertices->hide();
	}
	connect(m_editor->m_xrayVertices, &QCheckBox::toggled, this,
			[this](bool on)
			{
				const QSignalBlocker blocker(m_xray);
				m_xray->setChecked(on);
			});
}

void ModelEditorTools::buildShelf()
{
	m_shelf = new QToolBar;
	m_shelf->setObjectName(QStringLiteral("meshToolShelf"));
	m_shelf->setAccessibleName(Text::tr("Mesh tools"));
	m_shelf->setOrientation(Qt::Vertical);
	m_shelf->setToolButtonStyle(Qt::ToolButtonIconOnly);
	// The glyphs grow with the text scale, as the sidebar tabs do.
	setBaseIconSize(m_shelf, QSize(20, 20));
}

void ModelEditorTools::buildOverlays()
{
	for (const auto &name : {QStringLiteral("meshSelectBox"), QStringLiteral("meshCursorToSelected"), QString(), QStringLiteral("meshMove"),
							 QStringLiteral("meshRotate"), QStringLiteral("meshScale"), QString(), QStringLiteral("meshExtrude"),
							 QStringLiteral("meshInset"), QStringLiteral("meshLoopCut"), QStringLiteral("meshKnife"), QStringLiteral("meshSmooth"),
							 QStringLiteral("meshShrinkFatten"), QStringLiteral("meshMergeCentre"), QString(), QStringLiteral("meshAddCube"),
							 QStringLiteral("meshSymmetrizePositiveX"), QStringLiteral("meshToggleProportional")})
	{
		if (name.isEmpty())
			m_shelf->addSeparator();
		else if (auto *entry = m_actions.value(name))
			m_shelf->addAction(entry);
	}
	m_banner = new QLabel(m_viewport);
	m_banner->setObjectName(QStringLiteral("meshOperatorBanner"));
	m_banner->setAttribute(Qt::WA_TransparentForMouseEvents);
	m_banner->setWordWrap(true);
	m_banner->setTextFormat(Qt::PlainText);
	m_banner->setAlignment(Qt::AlignCenter);
	m_banner->setAutoFillBackground(true);
	m_banner->setMargin(6);
	m_banner->setAccessibleName(Text::tr("Tool status"));
	m_banner->hide();
	m_lastPanel = new QFrame(m_viewport);
	m_lastPanel->setObjectName(QStringLiteral("meshLastOperation"));
	m_lastPanel->setFrameShape(QFrame::StyledPanel);
	m_lastPanel->setAutoFillBackground(true);
	m_lastPanel->setAccessibleName(Text::tr("Adjust last operation"));
	// Keys a setting leaves unused stop here rather than reach the view.
	m_lastPanel->installEventFilter(this);
	auto *layout = new QVBoxLayout(m_lastPanel);
	layout->setContentsMargins(8, 6, 8, 8);
	m_lastTitle = new QLabel;
	m_lastTitle->setObjectName(QStringLiteral("meshLastOperationTitle"));
	auto title = m_lastTitle->font();
	title.setBold(true);
	m_lastTitle->setFont(title);
	m_lastTitle->setWordWrap(true);
	layout->addWidget(m_lastTitle);
	m_lastScroll = new QScrollArea;
	m_lastScroll->setObjectName(QStringLiteral("meshLastOperationScroll"));
	m_lastScroll->setAccessibleName(Text::tr("Operation settings"));
	m_lastScroll->setWidgetResizable(true);
	m_lastScroll->setFrameShape(QFrame::NoFrame);
	layout->addWidget(m_lastScroll);
	m_lastFields = new QWidget;
	m_lastScroll->setWidget(m_lastFields);
	m_lastPanel->hide();
}

void ModelEditorTools::placeOverlays()
{
	const int width = std::min(m_viewport->width() - 24, 640);
	m_banner->setFixedWidth(std::max(160, width));
	m_banner->adjustSize();
	m_banner->move((m_viewport->width() - m_banner->width()) / 2, 8);
	// Newly populated fields must be visible before taking their size hints.
	// Keep long forms inside the viewport, with keyboard-accessible scrolling.
	m_lastFields->ensurePolished();
	const auto margins = m_lastPanel->layout()->contentsMargins();
	const int availableWidth = std::max(1, m_viewport->width() - 16);
	const int panelWidth = std::min(availableWidth, std::max(260, m_lastFields->sizeHint().width() + margins.left() + margins.right() + 24));
	const int contentWidth = std::max(1, panelWidth - margins.left() - margins.right());
	// A long form label must wrap within the scrolling viewport instead of
	// making the whole row wider than a narrow, enlarged-text editor pane.
	const int labelWidth = std::max(1, contentWidth - m_lastScroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent) - 4);
	for (auto *label : m_lastFields->findChildren<QLabel *>())
		label->setMaximumWidth(labelWidth);
	const int fieldsHeight = m_lastFields->hasHeightForWidth() ? m_lastFields->heightForWidth(contentWidth) : m_lastFields->sizeHint().height();
	const int titleHeight = std::max(m_lastTitle->sizeHint().height(), m_lastTitle->heightForWidth(contentWidth));
	const int panelHeight = fieldsHeight + titleHeight + margins.top() + margins.bottom() + m_lastPanel->layout()->spacing() + 8;
	m_lastPanel->setMaximumSize(availableWidth, std::max(1, (m_viewport->height() - 16) * 2 / 3));
	m_lastPanel->resize(panelWidth, std::min(panelHeight, m_lastPanel->maximumHeight()));
	m_lastPanel->layout()->activate();
	// The bottom leading corner, the right one in a right-to-left layout; the
	// navigation gizmo keeps to the trailing top corner.
	const bool rtl = m_viewport->layoutDirection() == Qt::RightToLeft;
	m_lastPanel->move(rtl ? std::max(8, m_viewport->width() - m_lastPanel->width() - 8) : 8,
					  std::max(8, m_viewport->height() - m_lastPanel->height() - 8));
	// The gizmo moves clear of the banner and the panel; a large mesh is not
	// repainted when it stays where it was.
	if (navigationGizmo().bounds != m_paintedGizmo)
		m_viewport->update();
}

void ModelEditorTools::showBanner(const QString &text, const QString &details, LiveStatus kind)
{
	// The banner's accessible name stays "Tool status"; its description is
	// the details (a refusal's reason, a tool's keys), or else what it says.
	// The status line below says it too.
	m_banner->setAccessibleDescription(details.isEmpty() ? text : details);
	m_banner->setToolTip(details);
	m_viewport->setToolTip(details);
	m_editor->m_status->setToolTip(details);
	if (text.isEmpty())
	{
		m_banner->hide();
		return;
	}
	m_banner->setText(text);
	m_banner->show();
	m_banner->raise();
	placeOverlays();
	m_editor->m_status->setText(text);
	// From the status line, which is always shown; the banner comes and goes.
	announceStatus(m_editor->m_status,
				   kind == LiveStatus::Problem && !details.isEmpty() && details != text
					   ? Text::tr("%1: %2", "a refusal and its reason").arg(text, details)
					   : text,
				   kind);
}

void ModelEditorTools::setBlenderNavigation(bool enabled)
{
	// A quick switch between Blender-style navigation and the studio's
	// left-drag orbit, on top of the current profile.
	auto controls = m_controls;
	for (auto *camera : {&controls.navigation.view3D, &controls.navigation.orthographic})
	{
		if (enabled)
		{
			camera->orbitButton = Qt::MiddleButton;
			camera->orbitModifiers = Qt::NoModifier;
			camera->panButtons = {Qt::MiddleButton};
			camera->panModifiers = Qt::ShiftModifier;
			camera->leftPanModifiers = Qt::NoModifier;
		}
		else
		{
			const CameraViewControls studio;
			camera->orbitButton = studio.orbitButton;
			camera->orbitModifiers = studio.orbitModifiers;
			camera->panButtons = studio.panButtons;
			camera->panModifiers = studio.panModifiers;
			camera->leftPanModifiers = studio.leftPanModifiers;
		}
	}
	controls.selection.emptyDragBoxSelects = enabled;
	controls.navigation.emulateMiddleButton = enabled;
	applyControls(controls);
	// The navigation this switches, then keys as the profile gives them: 3ds
	// Max and MilkShape 3D give the modal Move, Rotate and Scale no key, and
	// F3 is not Search Commands there.
	QStringList help{enabled ? Text::tr("Blender navigation: middle-drag orbits, Shift+middle-drag pans and the wheel zooms (Alt+left-drag also "
										"orbits), and dragging from empty space draws a selection box.")
							 : Text::tr("Studio navigation: drag to orbit, Shift or Ctrl+drag pans and the wheel zooms.")};
	const QString move = commandKeys(QStringLiteral("meshMove")), rotate = commandKeys(QStringLiteral("meshRotate")),
				  scale = commandKeys(QStringLiteral("meshScale")), search = commandKeys(QStringLiteral("meshOperatorSearch"));
	if (!move.isEmpty() && !rotate.isEmpty() && !scale.isEmpty())
		help << Text::tr("%1, %2 and %3 move, rotate and scale; X, Y or Z constrains, type a value, Enter confirms and Esc cancels.")
					.arg(move, rotate, scale);
	help << (search.isEmpty() ? Text::tr("View > Search Commands finds every command.") : Text::tr("%1 searches every command.").arg(search));
	help << keyboardHelp();
	m_viewport->setControlsHelp(help.join(QLatin1Char(' ')));
}

int ModelEditorTools::selectionMode() const
{
	return m_editor->m_selectionMode->currentIndex();
}

void ModelEditorTools::refreshHeader()
{
	const int mode = selectionMode();
	for (int i = 0; i < 3; ++i)
	{
		const QSignalBlocker blocker(m_modeButtons[i]);
		m_modeButtons[i]->setChecked((i == 0 && mode == 1) || (i == 1 && mode == 2) || (i == 2 && mode == 0));
	}
}

void ModelEditorTools::setSelectionMode(int mode)
{
	if (mode == selectionMode())
		return;
	// Switching converts the selection as Blender does: faces keep their
	// corners or edges; vertices and edges become the faces they cover.
	auto selection = m_editor->m_document.selection();
	const auto &surface = m_editor->m_document.mesh().surfaces.value(selection.surface);
	ModelSelection converted{selection.surface, {}, {}};
	QSet<int> vertices = selection.vertices;
	for (auto edge : std::as_const(selection.edges))
		vertices << edge.first << edge.second;
	for (int face : std::as_const(selection.faces))
	{
		if (face < surface.triangles.size())
		{
			const auto &t = surface.triangles[face];
			vertices << t.a << t.b << t.c;
		}
	}
	if (mode == 1)
		converted.vertices = vertices;
	else if (mode == 2)
	{
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto &t = surface.triangles[face];
			for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				if (vertices.contains(a) && vertices.contains(b) && (selection.faces.isEmpty() || selection.faces.contains(face)))
					converted.edges.insert(modelEdge(a, b));
			}
		}
		if (!selection.edges.isEmpty())
			converted.edges = selection.edges;
	}
	else if (mode == 0)
	{
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto &t = surface.triangles[face];
			if (vertices.contains(t.a) && vertices.contains(t.b) && vertices.contains(t.c))
				converted.faces.insert(face);
		}
		if (!selection.faces.isEmpty())
			converted.faces = selection.faces;
	}
	if (!selection.tag.isEmpty() || !selection.collision.isEmpty() || !selection.surfaces.isEmpty())
		converted = {selection.surface, {}, {}};
	m_editor->m_document.setSelection(converted);
	{
		const QSignalBlocker blocker(m_editor->m_selectionMode);
		m_editor->m_selectionMode->setCurrentIndex(mode);
	}
	m_editor->m_presentedSelection = {};
	m_editor->refresh();
	if (m_editor->m_selectionMode->currentIndex() != mode)
	{
		m_editor->m_selectionMode->setCurrentIndex(mode);
	}
	refreshHeader();
}

ModelSelection ModelEditorTools::selectionFor(ModelEditKind kind) const
{
	auto selection = m_editor->m_document.selection();
	if (selection.surface < 0 || selection.surface >= m_editor->m_document.mesh().surfaces.size())
		return selection;
	const auto &surface = m_editor->m_document.mesh().surfaces[selection.surface];
	const bool needsFaces = kind == ModelEditKind::InsetFaces || kind == ModelEditKind::PokeFaces || kind == ModelEditKind::BeautifyFaces ||
							kind == ModelEditKind::DissolveFaces || kind == ModelEditKind::ShadeFlat || kind == ModelEditKind::ShadeSmooth ||
							kind == ModelEditKind::ShadeAutoSmooth || kind == ModelEditKind::Bisect || kind == ModelEditKind::Symmetrize ||
							kind == ModelEditKind::Decimate || kind == ModelEditKind::Extrude || kind == ModelEditKind::DuplicateFaces ||
							kind == ModelEditKind::DeleteFaces || kind == ModelEditKind::FlipFaces || kind == ModelEditKind::Subdivide ||
							kind == ModelEditKind::Solidify || kind == ModelEditKind::ProjectUvMapping || kind == ModelEditKind::Knife;
	if (needsFaces && selection.faces.isEmpty())
	{
		QSet<int> vertices = selection.vertices;
		for (auto edge : std::as_const(selection.edges))
			vertices << edge.first << edge.second;
		ModelSelection faces{selection.surface, {}, {}};
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto &t = surface.triangles[face];
			if (vertices.contains(t.a) && vertices.contains(t.b) && vertices.contains(t.c))
				faces.faces.insert(face);
		}
		// Whole-surface tools fall back to every face when nothing is selected.
		if (faces.faces.isEmpty() && (kind == ModelEditKind::Symmetrize || kind == ModelEditKind::Decimate ||
									  kind == ModelEditKind::ShadeAutoSmooth || kind == ModelEditKind::Bisect))
		{
			for (int face = 0; face < surface.triangles.size(); ++face)
				faces.faces.insert(face);
		}
		return faces;
	}
	if ((kind == ModelEditKind::RotateEdges || kind == ModelEditKind::LoopCut || kind == ModelEditKind::ExtrudeEdges || kind == ModelEditKind::SlideEdges || kind == ModelEditKind::BevelEdges) && selection.edges.isEmpty())
	{
		ModelSelection edges{selection.surface, {}, {}};
		for (int face : std::as_const(selection.faces))
		{
			const auto &t = surface.triangles[face];
			edges.edges << modelEdge(t.a, t.b) << modelEdge(t.b, t.c) << modelEdge(t.c, t.a);
		}
		if (!selection.vertices.isEmpty())
		{
			for (const auto &t : surface.triangles)
			{
				for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
				{
					if (selection.vertices.contains(a) && selection.vertices.contains(b))
						edges.edges.insert(modelEdge(a, b));
				}
			}
		}
		if (kind == ModelEditKind::BevelEdges)
		{
			ModelQuadTopology topology;
			if (buildModelQuadTopology(surface, std::max(0, m_editor->m_frame->currentIndex()), ModelMeshToolOptions{}.maxAngle, &topology))
			{
				for (auto i = edges.edges.begin(); i != edges.edges.end();)
					if (topology.diagonals.contains(topology.geometry.geometricEdge(i->first, i->second)))
						i = edges.edges.erase(i);
					else
						++i;
			}
		}
		return edges;
	}
	if (kind == ModelEditKind::MakeFace && !selection.faces.isEmpty())
		return {selection.surface, {}, {}};
	return selection;
}

ModelEdit ModelEditorTools::toolEdit(ModelEditKind kind) const
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = selectionFor(kind);
	const int frame = std::max(0, m_editor->m_frame->currentIndex());
	edit.pivotFrame = frame;
	if (isModelMeshToolEdit(kind))
		edit.tool.referenceFrame = frame;
	const bool shaped = isModelMeshShapeEdit(kind) || kind == ModelEditKind::Transform;
	edit.frame = shaped && m_editor->m_frameScope->currentIndex() == 1 ? frame : -1;
	if (kind == ModelEditKind::Transform || kind == ModelEditKind::WeightedTransform)
	{
		edit.pivotMode = ModelTransformPivot(m_editor->m_pivotMode->currentIndex());
		edit.pivot = {float(m_editor->m_pivot[0]->value()), float(m_editor->m_pivot[1]->value()), float(m_editor->m_pivot[2]->value())};
	}
	if (kind == ModelEditKind::Transform)
		m_editor->configureTransformAxes(edit);
	if (kind == ModelEditKind::InsetFaces)
		edit.tool.insetThickness = std::max(0.5, double(m_editor->m_document.mesh().boundingRadius()) / 32);
	if (kind == ModelEditKind::SmoothVertices)
		edit.tool.pinBoundary = true;
	return edit;
}

bool ModelEditorTools::runTool(const ModelEdit &operation, QString *error)
{
	auto edit = operation;
	if (edit.kind == ModelEditKind::Knife) edit.tool.knife.hiddenFaces = m_hiddenFaces;
	const auto before = m_editor->m_document.revisionFingerprint();
	QString failure;
	ModelEditReport report;
	if (!m_editor->applyEdit(edit, &failure, &report))
	{
		if (error)
			*error = failure;
		if (edit.kind == ModelEditKind::BevelEdges || edit.kind == ModelEditKind::Knife || edit.kind == ModelEditKind::Spin || edit.kind == ModelEditKind::Rip)
			showBanner(edit.kind == ModelEditKind::Rip ? Text::tr("Rip refused") : edit.kind == ModelEditKind::Spin ? Text::tr("Spin refused") : edit.kind == ModelEditKind::Knife ? Text::tr("Knife refused") : Text::tr("Bevel refused"), failure,
					   LiveStatus::Problem);
		else
			showBanner(failure, {}, LiveStatus::Problem);
		return false;
	}
	remapHiddenAfterEdit(edit, report);
	const auto after = m_editor->m_document.revisionFingerprint();
	if (after != before)
	{
		m_last = {edit, after, {}};
		m_hasLast = true;
	}
	// Interpolated, mirrored, rebound, merged or bound skin weights are named.
	showBanner(modelSkinProvenanceSummary(report.skin));
	refreshLastPanel();
	return true;
}

void ModelEditorTools::runKind(ModelEditKind kind, std::function<void(ModelEdit &)> configure)
{
	auto edit = toolEdit(kind);
	if (configure)
		configure(edit);
	if (kind == ModelEditKind::Symmetrize && !confirmMirrorJoints(edit))
		return;
	QString error;
	runTool(edit, &error);
}

bool ModelEditorTools::confirmMirrorJoints(ModelEdit &edit)
{
	const auto &mesh = m_editor->m_document.mesh();
	const int surface = edit.selection.surface;
	// The same tolerance as the edit's own check for joints on the plane.
	const double tolerance = std::max(edit.tool.mergeThreshold, 1e-4);
	if (mesh.skeleton.isEmpty() || surface < 0 || surface >= mesh.surfaces.size() || mesh.surfaces[surface].skinning.isEmpty() ||
		modelOffCentreJoints(mesh, surface, edit.tool.axis, tolerance).isEmpty())
	{
		// Every joint keeps its name across this plane: a map confirmed for
		// another plane or surface must not travel with a repeat or F9 replay.
		edit.tool.mirrorJoints.clear();
		edit.tool.keepJointNames = false;
		return true;
	}
	ModelSymmetrizeJointsDialog dialog(mesh, surface, edit.tool.axis, m_mirrorJoints, m_keepJointNames, tolerance, m_editor);
	if (dialog.exec() != QDialog::Accepted)
	{
		showBanner(Text::tr("Symmetrize cancelled. No change was made."));
		return false;
	}
	m_mirrorJoints = dialog.mirrorJoints();
	m_keepJointNames = dialog.keepJointNames();
	edit.tool.mirrorJoints = m_mirrorJoints;
	edit.tool.keepJointNames = m_keepJointNames;
	return true;
}

void ModelEditorTools::addPrimitive(ModelPrimitive primitive)
{
	auto edit = toolEdit(ModelEditKind::AddPrimitive);
	edit.selection = {std::max(0, m_editor->m_document.selection().surface), {}, {}};
	edit.tool.primitive = primitive;
	edit.tool.point = {m_cursor.x, m_cursor.y, m_cursor.z};
	// idTech scale: a 32-unit primitive (a player is 56 units tall).
	const double size = 32;
	edit.tool.size = {size, size, primitive == ModelPrimitive::Torus ? size / 4 : size};
	if (primitive == ModelPrimitive::Torus)
		edit.tool.size = {size * 1.5, size * 1.5, size / 2};
	edit.tool.segments = primitive == ModelPrimitive::Grid ? 4 : primitive == ModelPrimitive::UvSphere || primitive == ModelPrimitive::Torus ? 16 : 12;
	edit.tool.rings = primitive == ModelPrimitive::IcoSphere ? 2 : primitive == ModelPrimitive::Grid ? 4 : 8;
	edit.tool.axis = 2;
	if (!confirmPrimitiveBinding(edit))
		return;
	QString error;
	if (runTool(edit, &error))
		setSelectionMode(0);
}

bool ModelEditorTools::mirrorPlaneChanged(const ModelEdit &before, const ModelEdit &after) const
{
	if (before.tool.axis != after.tool.axis)
		return true;
	const auto &mesh = m_editor->m_document.mesh();
	const int surface = after.selection.surface;
	return modelOffCentreJoints(mesh, surface, after.tool.axis, std::max(before.tool.mergeThreshold, 1e-4)) !=
		   modelOffCentreJoints(mesh, surface, after.tool.axis, std::max(after.tool.mergeThreshold, 1e-4));
}

bool ModelEditorTools::confirmPrimitiveBinding(ModelEdit &edit)
{
	const auto &mesh = m_editor->m_document.mesh();
	const int surface = edit.selection.surface;
	if (mesh.skeleton.isEmpty() || surface < 0 || surface >= mesh.surfaces.size() || mesh.surfaces[surface].skinning.isEmpty())
	{
		// An unskinned target takes plain geometry: a binding or new surface
		// confirmed for an earlier target must not travel with a repeat.
		edit.tool.newGeometryBinding = ModelNewGeometryBinding::None;
		edit.tool.bindJoint.clear();
		edit.tool.newSurface.clear();
		return true;
	}
	const auto selected = m_editor->m_document.selection().joint;
	// The dialog proposes an unused name for a new surface every time.
	ModelPrimitiveBindingDialog dialog(mesh, surface, selected.isEmpty() ? m_primitiveJoint : selected, m_primitiveBinding,
		m_primitiveNewSurface, m_editor);
	if (dialog.exec() != QDialog::Accepted)
	{
		showBanner(Text::tr("Add Primitive cancelled. No change was made."));
		return false;
	}
	m_primitiveBinding = dialog.binding();
	m_primitiveNewSurface = !dialog.newSurface().isEmpty() && dialog.binding() != ModelNewGeometryBinding::None;
	if (!dialog.joint().isEmpty())
		m_primitiveJoint = dialog.joint();
	edit.tool.newGeometryBinding = dialog.binding();
	edit.tool.bindJoint = dialog.joint();
	edit.tool.newSurface = dialog.newSurface();
	return true;
}

bool ModelEditorTools::select(ModelSelectRequest request, QString *error)
{
	const int mode = selectionMode();
	if (mode > 2)
	{
		const auto message = Text::tr("Selection operators work in vertex, edge and face modes.");
		if (error)
			*error = message;
		showBanner(message, {}, LiveStatus::Problem);
		return false;
	}
	request.mode = mode == 0 ? ModelSelectionMode::Faces : mode == 1 ? ModelSelectionMode::Vertices : ModelSelectionMode::Edges;
	request.frame = std::max(0, m_editor->m_frame->currentIndex());
	ModelSelection result;
	QString failure;
	if (!selectModelComponents(m_editor->m_document.mesh(), m_editor->m_document.selection(), request, &result, &failure))
	{
		if (error)
			*error = failure;
		showBanner(failure, {}, LiveStatus::Problem);
		return false;
	}
	applySelection(result);
	showBanner({});
	return true;
}

void ModelEditorTools::selectOperation(ModelSelectOperation operation, std::function<void(ModelSelectRequest &)> configure)
{
	ModelSelectRequest request;
	request.operation = operation;
	if (configure)
		configure(request);
	select(request);
}

void ModelEditorTools::applySelection(const ModelSelection &selection)
{
	const int mode = selectionMode();
	m_editor->m_document.setSelection(withoutHidden(selection));
	m_editor->refresh();
	// An empty or edge result must not silently change the chosen mode.
	if (selection.faces.isEmpty() && selection.vertices.isEmpty() && selection.edges.isEmpty() && selectionMode() != mode)
		m_editor->m_selectionMode->setCurrentIndex(mode);
	refreshHeader();
}

bool ModelEditorTools::selectionCentre(ModelVec3 *centre) const
{
	const auto &mesh = m_editor->m_document.mesh();
	const auto selection = m_editor->m_document.selection();
	if (selection.surface < 0 || selection.surface >= mesh.surfaces.size())
		return false;
	const auto &surface = mesh.surfaces[selection.surface];
	QSet<int> vertices = selection.vertices;
	for (auto edge : selection.edges)
		vertices << edge.first << edge.second;
	for (int face : selection.faces)
	{
		if (face < surface.triangles.size())
		{
			const auto &t = surface.triangles[face];
			vertices << t.a << t.b << t.c;
		}
	}
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, int(surface.frames.size()) - 1);
	if (vertices.isEmpty() || frame < 0)
		return false;
	double low[3]{INFINITY, INFINITY, INFINITY}, high[3]{-INFINITY, -INFINITY, -INFINITY};
	for (int v : vertices)
	{
		if (v < 0 || v >= surface.vertexCount)
			continue;
		const auto p = surface.frames[frame].positions[v];
		const double values[3]{p.x, p.y, p.z};
		for (int axis = 0; axis < 3; ++axis)
		{
			low[axis] = std::min(low[axis], values[axis]);
			high[axis] = std::max(high[axis], values[axis]);
		}
	}
	if (!std::isfinite(low[0]))
		return false;
	*centre = {float((low[0] + high[0]) / 2), float((low[1] + high[1]) / 2), float((low[2] + high[2]) / 2)};
	return true;
}

void ModelEditorTools::frameSelection()
{
	const auto &mesh = m_editor->m_document.mesh();
	const auto selection = m_editor->m_document.selection();
	ModelVec3 centre;
	if (!selectionCentre(&centre))
	{
		m_viewport->frameModel();
		return;
	}
	const auto &surface = mesh.surfaces[selection.surface];
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, int(surface.frames.size()) - 1);
	QSet<int> vertices = selection.vertices;
	for (auto edge : selection.edges)
		vertices << edge.first << edge.second;
	for (int face : selection.faces)
	{
		const auto &t = surface.triangles[face];
		vertices << t.a << t.b << t.c;
	}
	ModelVec3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
	for (int v : vertices)
	{
		const auto p = surface.frames[frame].positions[v];
		low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
		high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
	}
	m_viewport->frameBounds(low, high);
}

void ModelEditorTools::alignView(int axis, bool positive)
{
	// Orbit yaw/pitch put the eye on the chosen side, looking back at the model.
	if (axis == 2)
		m_viewport->setOrbit(-90, positive ? 90 : -90);
	else if (axis == 0)
		m_viewport->setOrbit(positive ? 0 : 180, 0);
	else
		m_viewport->setOrbit(positive ? 90 : -90, 0);
}

void ModelEditorTools::setCursor3d(const ModelVec3 &point)
{
	m_cursor = point;
	m_viewport->update();
	m_editor->m_status->setText(Text::tr("3D cursor at %1.").arg(localeNumberList({point.x, point.y, point.z})));
}

ModelVec3 ModelEditorTools::cursor3d() const
{
	return m_cursor;
}

bool ModelEditorTools::placeCursor(const QPointF &point)
{
	const auto ray = m_viewport->viewRay(point);
	const auto hit = m_viewport->hitAt(point);
	const auto &mesh = m_editor->m_document.mesh();
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, int(mesh.frames.size()) - 1);
	const auto dot = [](ModelVec3 a, ModelVec3 b) { return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z; };
	const auto sub = [](ModelVec3 a, ModelVec3 b) { return ModelVec3{a.x - b.x, a.y - b.y, a.z - b.z}; };
	ModelVec3 planePoint = m_cursor, normal = m_viewport->viewForward();
	if (hit.valid && hit.surface >= 0 && hit.surface < mesh.surfaces.size())
	{
		int face = hit.triangle;
		for (int s = 0; s < hit.surface; ++s)
			face -= mesh.surfaces[s].triangles.size();
		const auto &surface = mesh.surfaces[hit.surface];
		if (face >= 0 && face < surface.triangles.size() && frame >= 0)
		{
			const auto &t = surface.triangles[face];
			const auto &positions = surface.frames[frame].positions;
			const auto a = positions[t.a], e1 = sub(positions[t.b], a), e2 = sub(positions[t.c], a);
			normal = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
			planePoint = a;
		}
	}
	const double denominator = dot(ray.direction, normal);
	if (std::abs(denominator) < 1e-12)
		return false;
	const double t = dot(sub(planePoint, ray.origin), normal) / denominator;
	setCursor3d({float(ray.origin.x + ray.direction.x * t), float(ray.origin.y + ray.direction.y * t),
				 float(ray.origin.z + ray.direction.z * t)});
	return true;
}

QPointF ModelEditorTools::pointerInViewport() const
{
	const QPointF local = m_viewport->mapFromGlobal(QCursor::pos());
	if (m_viewport->rect().contains(local.toPoint()))
		return local;
	return QPointF(m_viewport->width() / 2.0, m_viewport->height() / 2.0);
}

ModelEdge ModelEditorTools::edgeNear(const QPointF &point, int *surface)
{
	const auto hit = m_viewport->edgeAt(point, 24);
	if (!hit.valid)
		return {-1, -1};
	if (surface)
		*surface = hit.surface;
	return modelEdge(hit.a, hit.b);
}

int ModelEditorTools::faceAt(const QPointF &point, int *surface)
{
	const auto hit = m_viewport->hitAt(point);
	if (!hit.valid)
		return -1;
	int face = hit.triangle;
	for (int s = 0; s < hit.surface; ++s)
		face -= m_editor->m_document.mesh().surfaces[s].triangles.size();
	if (surface)
		*surface = hit.surface;
	return face;
}

bool ModelEditorTools::pickLoop(const QPointF &point, bool ring, bool extend)
{
	int surface = -1;
	const auto edge = edgeNear(point, &surface);
	if (edge.first < 0 || selectionMode() > 2)
		return false;
	auto current = m_editor->m_document.selection();
	if (surface != current.surface)
		current = {surface, {}, {}};
	ModelSelectRequest request;
	request.operation = ring ? ModelSelectOperation::Ring : ModelSelectOperation::Loop;
	request.edge = edge;
	request.extend = extend;
	const int mode = selectionMode();
	request.mode = mode == 0 ? ModelSelectionMode::Faces : mode == 1 ? ModelSelectionMode::Vertices : ModelSelectionMode::Edges;
	request.frame = std::max(0, m_editor->m_frame->currentIndex());
	ModelSelection result;
	QString error;
	if (!selectModelComponents(m_editor->m_document.mesh(), current, request, &result, &error))
	{
		showBanner(error, {}, LiveStatus::Problem);
		return false;
	}
	applySelection(result);
	return true;
}

bool ModelEditorTools::selectEdgeLoops(bool ring)
{
	const int mode = selectionMode();
	const auto &mesh = m_editor->m_document.mesh();
	const auto current = m_editor->m_document.selection();
	if (mode > 2 || current.surface < 0 || current.surface >= mesh.surfaces.size())
	{
		showBanner(Text::tr("Selection operators work in vertex, edge and face modes."));
		return false;
	}
	ModelSelectRequest request;
	request.operation = ring ? ModelSelectOperation::Ring : ModelSelectOperation::Loop;
	request.mode = mode == 0 ? ModelSelectionMode::Faces : mode == 1 ? ModelSelectionMode::Vertices : ModelSelectionMode::Edges;
	request.frame = std::max(0, m_editor->m_frame->currentIndex());
	request.extend = true;
	// The service seeds from the selected edges, or in vertex mode the edges
	// between selected vertices, as `model select` does. A large selection
	// runs on the worker, with progress and Cancel.
	ModelEdgeLoopsResult loops;
	QString error;
	if (!m_editor->performWork(
			ring ? Text::tr("Selecting edge rings…") : Text::tr("Selecting edge loops…"),
			[current, request, &loops](ModelDocument &candidate, QString *problem, const ModelWorkControl &control)
			{ return selectModelEdgeLoops(candidate.mesh(), current, request, {}, &loops, problem, control); },
			&error))
	{
		showBanner(error, {}, LiveStatus::Problem);
		return false;
	}
	applySelection(loops.selection);
	if (loops.refused.isEmpty())
		showBanner({});
	else
		showBanner(ring ? Text::tr("Kept %n selected edge(s) without a ring.", nullptr, int(loops.refused.size()))
						: Text::tr("Kept %n selected edge(s) without a loop.", nullptr, int(loops.refused.size())),
				   modelEdgeLoopRefusalLines(loops).join(QLatin1Char('\n')));
	return true;
}

bool ModelEditorTools::pickPath(const QPointF &point)
{
	const int mode = selectionMode();
	if (mode > 2)
		return false;
	auto current = m_editor->m_document.selection();
	ModelSelectRequest request;
	request.operation = ModelSelectOperation::ShortestPath;
	request.extend = true;
	request.frame = std::max(0, m_editor->m_frame->currentIndex());
	int surface = -1;
	if (mode == 0)
	{
		request.mode = ModelSelectionMode::Faces;
		request.to = faceAt(point, &surface);
		if (request.to < 0 || current.faces.isEmpty() || surface != current.surface)
			return false;
		request.from = *std::min_element(current.faces.cbegin(), current.faces.cend());
	}
	else
	{
		request.mode = mode == 1 ? ModelSelectionMode::Vertices : ModelSelectionMode::Edges;
		const auto vertex = m_viewport->vertexAt(point, 16);
		int target = -1;
		if (vertex.valid && vertex.surface == current.surface)
			target = vertex.vertex;
		else
		{
			const auto edge = edgeNear(point, &surface);
			if (edge.first >= 0 && surface == current.surface)
				target = edge.second;
		}
		QSet<int> from = current.vertices;
		for (auto e : current.edges)
			from << e.first << e.second;
		if (target < 0 || from.isEmpty())
			return false;
		request.from = *std::min_element(from.cbegin(), from.cend());
		request.to = target;
	}
	ModelSelection result;
	QString error;
	if (!selectModelComponents(m_editor->m_document.mesh(), current, request, &result, &error))
	{
		showBanner(error, {}, LiveStatus::Problem);
		return false;
	}
	applySelection(result);
	return true;
}

bool ModelEditorTools::pickLinked(const QPointF &point, bool subtract)
{
	const int mode = selectionMode();
	int surface = -1;
	const int face = faceAt(point, &surface);
	if (face < 0 || mode > 2)
		return false;
	ModelSelection seed{surface, {}, {face}};
	ModelSelectRequest request;
	request.operation = ModelSelectOperation::Linked;
	request.mode = mode == 0 ? ModelSelectionMode::Faces : mode == 1 ? ModelSelectionMode::Vertices : ModelSelectionMode::Edges;
	request.frame = std::max(0, m_editor->m_frame->currentIndex());
	ModelSelection linked;
	QString error;
	// Seed through faces, then convert to the current mode.
	ModelSelectRequest faces = request;
	faces.mode = ModelSelectionMode::Faces;
	if (!selectModelComponents(m_editor->m_document.mesh(), seed, faces, &linked, &error))
		return false;
	if (request.mode != ModelSelectionMode::Faces)
	{
		const auto &s = m_editor->m_document.mesh().surfaces[surface];
		ModelSelection converted{surface, {}, {}};
		for (int f : std::as_const(linked.faces))
		{
			const auto &t = s.triangles[f];
			if (request.mode == ModelSelectionMode::Vertices)
				converted.vertices << t.a << t.b << t.c;
			else
				converted.edges << modelEdge(t.a, t.b) << modelEdge(t.b, t.c) << modelEdge(t.c, t.a);
		}
		linked = converted;
	}
	auto current = m_editor->m_document.selection();
	if (current.surface != surface)
		current = {surface, {}, {}};
	if (subtract)
	{
		current.faces -= linked.faces;
		current.vertices -= linked.vertices;
		current.edges -= linked.edges;
	}
	else
	{
		current.faces += linked.faces;
		current.vertices += linked.vertices;
		current.edges += linked.edges;
	}
	applySelection(current);
	return true;
}

bool ModelEditorTools::selectRegion(const QPolygonF &region, bool extend, bool subtract)
{
	const int mode = selectionMode();
	if (mode > 2 || region.size() < 3)
		return false;
	const auto &mesh = m_editor->m_document.mesh();
	auto current = m_editor->m_document.selection();
	const int surfaceIndex = std::clamp(current.surface, 0, int(mesh.surfaces.size()) - 1);
	const auto &surface = mesh.surfaces[surfaceIndex];
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, int(surface.frames.size()) - 1);
	const bool xray = m_xray->isChecked() || m_editor->m_renderMode->currentIndex() == 1;
	const auto projection = m_viewport->editVertexProjection();
	const bool projected = projection && projection->surface == surfaceIndex && projection->positions.size() == surface.vertexCount;
	QVector<QPointF> screen(surface.vertexCount);
	QVector<bool> visible(surface.vertexCount, true), valid(surface.vertexCount, false);
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		if (projected)
		{
			screen[v] = projection->positions[v];
			valid[v] = std::isfinite(screen[v].x()) && std::isfinite(screen[v].y());
			visible[v] = xray || projection->visible.testBit(v);
		}
		else
			valid[v] = m_viewport->projectToView(surface.frames[frame].positions[v], &screen[v]);
	}
	const auto inside = [&](int v) { return valid[v] && visible[v] && region.containsPoint(screen[v], Qt::OddEvenFill); };
	ModelSelection picked{surfaceIndex, {}, {}};
	if (mode == 1)
	{
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			if (inside(v))
				picked.vertices.insert(v);
		}
	}
	else if (mode == 2)
	{
		for (auto edge : m_editor->m_document.surfaceTopology(surfaceIndex).edges)
		{
			if (inside(edge.first) && inside(edge.second))
				picked.edges.insert(edge);
		}
	}
	else
	{
		int base = 0;
		for (int s = 0; s < surfaceIndex; ++s)
			base += mesh.surfaces[s].triangles.size();
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto &t = surface.triangles[face];
			if (!valid[t.a] || !valid[t.b] || !valid[t.c])
				continue;
			const QPointF centre = (screen[t.a] + screen[t.b] + screen[t.c]) / 3.0;
			if (!region.containsPoint(centre, Qt::OddEvenFill))
				continue;
			if (!xray)
			{
				// Visible faces own the pixel under their centre.
				const auto hit = m_viewport->hitAt(centre);
				if (!hit.valid || hit.triangle != base + face)
					continue;
			}
			picked.faces.insert(face);
		}
	}
	if (current.surface != surfaceIndex || (!extend && !subtract) || !current.tag.isEmpty() || !current.collision.isEmpty() ||
		!current.surfaces.isEmpty())
		current = {surfaceIndex, {}, {}};
	if (subtract)
	{
		current.faces -= picked.faces;
		current.vertices -= picked.vertices;
		current.edges -= picked.edges;
	}
	else
	{
		current.faces += picked.faces;
		current.vertices += picked.vertices;
		current.edges += picked.edges;
	}
	applySelection(current);
	return true;
}

bool ModelEditorTools::selectCircle(const QPointF &centre, double radius, bool subtract)
{
	QPolygonF circle;
	for (int i = 0; i < 32; ++i)
	{
		const double angle = 2 * std::numbers::pi * i / 32;
		circle << centre + QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
	}
	return selectRegion(circle, true, subtract);
}

QWidget *ModelEditorTools::lastOperationPanel() const
{
	return m_lastPanel;
}

namespace
{
struct LastField
{
	QString label;
	double low = 0, high = 1;
	int decimals = 2;
	std::function<double(const ModelEdit &)> get;
	std::function<void(ModelEdit &, double)> set;
	bool toggle = false;
	QStringList choices;
	// Changing this field changes which other fields apply.
	bool rebuild = false;
};
QVector<LastField> lastFields(const ModelEdit &edit)
{
	using E = ModelEdit;
	const auto kind = edit.kind;
	const auto vector3 = [](const QString &label, ModelVec3 E::*member, double low, double high)
	{
		QVector<LastField> fields;
		const char *axes[3]{"X", "Y", "Z"};
		for (int axis = 0; axis < 3; ++axis)
		{
			fields.append({QStringLiteral("%1 %2").arg(label, QLatin1String(axes[axis])), low, high, 3,
						   [member, axis](const E &e) { return double(axis == 0 ? (e.*member).x : axis == 1 ? (e.*member).y : (e.*member).z); },
						   [member, axis](E &e, double v) { (axis == 0 ? (e.*member).x : axis == 1 ? (e.*member).y : (e.*member).z) = float(v); }});
		}
		return fields;
	};
	const auto array3 = [](const QString &label, std::array<double, 3> ModelMeshToolOptions::*member, double low, double high)
	{
		QVector<LastField> fields;
		const char *axes[3]{"X", "Y", "Z"};
		for (int axis = 0; axis < 3; ++axis)
			fields.append({QStringLiteral("%1 %2").arg(label, QLatin1String(axes[axis])), low, high, 3,
						   [member, axis](const E &e) { return (e.tool.*member)[axis]; }, [member, axis](E &e, double v) { (e.tool.*member)[axis] = v; }});
		return fields;
	};
	switch (kind)
	{
	case ModelEditKind::Transform:
	case ModelEditKind::WeightedTransform:
		return vector3(Text::tr("Move"), &E::translation, -100000, 100000) + vector3(Text::tr("Rotate"), &E::rotation, -36000, 36000) +
			   vector3(Text::tr("Scale"), &E::scale, 0.0001, 10000);
	case ModelEditKind::Extrude:
	case ModelEditKind::DuplicateFaces:
	case ModelEditKind::ExtrudeEdges:
		return vector3(Text::tr("Offset"), &E::translation, -100000, 100000);
	case ModelEditKind::Spin:
		return QVector<LastField>{
			{Text::tr("Angle"), -360, 360, 3, [](const E &e) { return e.tool.spinAngle; }, [](E &e, double v) { e.tool.spinAngle = v; }},
			{Text::tr("Steps"), 1, 256, 0, [](const E &e) { return double(e.tool.spinSteps); }, [](E &e, double v) { e.tool.spinSteps = int(v); }},
			{Text::tr("Duplicate selection"), 0, 1, 0, [](const E &e) { return double(e.tool.spinDuplicate); }, [](E &e, double v) { e.tool.spinDuplicate = v > .5; }, true},
			{Text::tr("Close full turn"), 0, 1, 0, [](const E &e) { return double(e.tool.spinClose); }, [](E &e, double v) { e.tool.spinClose = v > .5; }, true},
			{Text::tr("Units per texture repeat"), .001, 100000, 3, [](const E &e) { return e.tool.uvTileSize; }, [](E &e, double v) { e.tool.uvTileSize = v; }}}
			+ array3(Text::tr("Centre"), &ModelMeshToolOptions::point, -100000, 100000)
			+ array3(Text::tr("Axis"), &ModelMeshToolOptions::normal, -1, 1);
	case ModelEditKind::Rip:
		return vector3(Text::tr("Offset"), &E::translation, -100000, 100000) + QVector<LastField>{
			{Text::tr("Side face"), 0, 131071, 0, [](const E &e) { return double(e.tool.ripFace); }, [](E &e, double v) { e.tool.ripFace = int(v); }},
			{Text::tr("Opposite side"), 0, 1, 0, [](const E &e) { return double(e.tool.ripOpposite); }, [](E &e, double v) { e.tool.ripOpposite = v > .5; }, true},
			{Text::tr("Fill gap"), 0, 1, 0, [](const E &e) { return double(e.tool.ripFill); }, [](E &e, double v) { e.tool.ripFill = v > .5; }, true},
			{Text::tr("Units per texture repeat"), .001, 1000000, 3, [](const E &e) { return e.tool.uvTileSize; }, [](E &e, double v) { e.tool.uvTileSize = v; }},
			{Text::tr("Quad pairing angle"), 0, 180, 1, [](const E &e) { return e.tool.maxAngle; }, [](E &e, double v) { e.tool.maxAngle = v; }}};
	case ModelEditKind::InsetFaces:
		return {{Text::tr("Thickness"), 0, 100000, 3, [](const E &e) { return e.tool.insetThickness; }, [](E &e, double v) { e.tool.insetThickness = v; }},
				{Text::tr("Depth"), -100000, 100000, 3, [](const E &e) { return e.tool.insetDepth; }, [](E &e, double v) { e.tool.insetDepth = v; }},
				{Text::tr("Individual"), 0, 1, 0, [](const E &e) { return double(e.tool.inset == ModelInsetMode::Individual); },
				 [](E &e, double v) { e.tool.inset = v > 0.5 ? ModelInsetMode::Individual : ModelInsetMode::Region; }, true},
				{Text::tr("Even thickness"), 0, 1, 0, [](const E &e) { return double(e.tool.evenThickness); },
				 [](E &e, double v) { e.tool.evenThickness = v > 0.5; }, true}};
	case ModelEditKind::ShrinkFatten:
		return {{Text::tr("Distance"), -100000, 100000, 3, [](const E &e) { return e.tool.offset; }, [](E &e, double v) { e.tool.offset = v; }},
				{Text::tr("Even thickness"), 0, 1, 0, [](const E &e) { return double(e.tool.evenThickness); },
				 [](E &e, double v) { e.tool.evenThickness = v > 0.5; }, true}};
	case ModelEditKind::PokeFaces:
		return {{Text::tr("Centre offset"), -100000, 100000, 3, [](const E &e) { return e.tool.offset; }, [](E &e, double v) { e.tool.offset = v; }}};
	case ModelEditKind::SmoothVertices:
		return {{Text::tr("Smoothing"), 0, 1, 3, [](const E &e) { return e.tool.smoothFactor; }, [](E &e, double v) { e.tool.smoothFactor = v; }},
				{Text::tr("Repeat"), 1, 100, 0, [](const E &e) { return double(e.tool.iterations); }, [](E &e, double v) { e.tool.iterations = int(v); }},
				{Text::tr("Keep borders"), 0, 1, 0, [](const E &e) { return double(e.tool.pinBoundary); },
				 [](E &e, double v) { e.tool.pinBoundary = v > 0.5; }, true}};
	case ModelEditKind::LoopCut:
		return {{Text::tr("Number of cuts"), 1, 64, 0, [](const E &e) { return double(e.tool.cuts); }, [](E &e, double v) { e.tool.cuts = int(v); }},
				{Text::tr("Slide (one cut)"), -1, 1, 3, [](const E &e) { return e.tool.slide; }, [](E &e, double v) { e.tool.slide = v; }}};
	case ModelEditKind::Knife:
		return {{Text::tr("Cut through"), 0, 1, 0, [](const E &e) { return double(e.tool.knife.through); },
			[](E &e, double v) { e.tool.knife.through = v > .5; }, true},
			{Text::tr("Selected faces only"), 0, 1, 0, [](const E &e) { return double(e.tool.knife.selectedOnly); },
			[](E &e, double v) { e.tool.knife.selectedOnly = v > .5; }, true}};
	case ModelEditKind::SlideEdges:
		return {{Text::tr("Mode"), 0, 1, 0, [](const E &e) { return double(e.tool.slideByDistance); },
				 [](E &e, double v) {
					 const double amount = e.tool.slideByDistance ? e.tool.offset : e.tool.slide;
					 e.tool.slideByDistance = v > 0.5;
					 if (e.tool.slideByDistance) e.tool.offset = amount;
					 else e.tool.slide = std::clamp(amount, -.9999, .9999);
				 }, false, {Text::tr("Fraction of rail"), Text::tr("Distance in reference pose")}, true},
				{edit.tool.slideByDistance ? Text::tr("Distance") : Text::tr("Fraction"),
				 edit.tool.slideByDistance ? -1000000.0 : -.9999, edit.tool.slideByDistance ? 1000000.0 : .9999, 4,
				 [](const E &e) { return e.tool.slideByDistance ? e.tool.offset : e.tool.slide; },
				 [](E &e, double v) { if (e.tool.slideByDistance) e.tool.offset = v; else e.tool.slide = v; }},
				{Text::tr("Quad pairing angle"), 0, 180, 1, [](const E &e) { return e.tool.maxAngle; }, [](E &e, double v) { e.tool.maxAngle = v; }}};
	case ModelEditKind::MergeVertices:
		return {{Text::tr("Merge"), 0, 2, 0, [](const E &e) { return double(int(e.tool.merge)); }, [](E &e, double v) { e.tool.merge = ModelMergeMode(int(v)); },
				 false, {Text::tr("At Center"), Text::tr("At Cursor"), Text::tr("Collapse")}}};
	case ModelEditKind::BeautifyFaces:
		return {{Text::tr("Max angle"), 0, 180, 1, [](const E &e) { return e.tool.maxAngle; }, [](E &e, double v) { e.tool.maxAngle = v; }}};
	case ModelEditKind::ShadeAutoSmooth:
		return {{Text::tr("Angle"), 0, 180, 1, [](const E &e) { return e.tool.smoothAngle; }, [](E &e, double v) { e.tool.smoothAngle = v; }}};
	case ModelEditKind::Symmetrize:
		return {{Text::tr("Axis"), 0, 2, 0, [](const E &e) { return double(e.tool.axis); }, [](E &e, double v) { e.tool.axis = int(v); }, false,
				 {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")}},
				{Text::tr("Positive to negative"), 0, 1, 0, [](const E &e) { return double(e.tool.positiveToNegative); },
				 [](E &e, double v) { e.tool.positiveToNegative = v > 0.5; }, true},
				{Text::tr("Merge distance"), 0, 1000, 4, [](const E &e) { return e.tool.mergeThreshold; }, [](E &e, double v) { e.tool.mergeThreshold = v; }}};
	case ModelEditKind::Bisect:
		return array3(Text::tr("Plane point"), &ModelMeshToolOptions::point, -100000, 100000) +
			   array3(Text::tr("Plane normal"), &ModelMeshToolOptions::normal, -1, 1) +
			   QVector<LastField>{{Text::tr("Keep"), 0, 2, 0, [](const E &e) { return double(int(e.tool.keep)); },
								   [](E &e, double v) { e.tool.keep = ModelBisectKeep(int(v)); }, false,
								   {Text::tr("Both halves"), Text::tr("Front half"), Text::tr("Back half")}}};
	case ModelEditKind::AddPrimitive:
		return array3(Text::tr("Location"), &ModelMeshToolOptions::point, -100000, 100000) +
			   array3(Text::tr("Size"), &ModelMeshToolOptions::size, 0.01, 100000) +
			   QVector<LastField>{{Text::tr("Segments"), 3, 256, 0, [](const E &e) { return double(e.tool.segments); },
								   [](E &e, double v) { e.tool.segments = int(v); }},
								  {Text::tr("Rings / subdivisions"), 1, 256, 0, [](const E &e) { return double(e.tool.rings); },
								   [](E &e, double v) { e.tool.rings = int(v); }},
								  {Text::tr("Up axis"), 0, 2, 0, [](const E &e) { return double(e.tool.axis); }, [](E &e, double v) { e.tool.axis = int(v); },
								   false, {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")}}};
	case ModelEditKind::BevelVertices:
		return {{Text::tr("Width"), 0.000001, 100000, 3, [](const E &e) { return e.tool.bevelWidth; }, [](E &e, double v) { e.tool.bevelWidth = v; }}};
	case ModelEditKind::BevelEdges:
		return {{Text::tr("Width"), 0, 1000000, 4, [](const E &e) { return e.tool.bevelWidth; }, [](E &e, double v) { e.tool.bevelWidth = v; }},
				{Text::tr("Segments"), 1, 32, 0, [](const E &e) { return double(e.tool.bevelSegments); }, [](E &e, double v) { e.tool.bevelSegments = int(v); }},
				{Text::tr("Roundness"), 0, 1, 3, [](const E &e) { return e.tool.bevelRoundness; }, [](E &e, double v) { e.tool.bevelRoundness = v; }},
				{Text::tr("Units per texture repeat"), .001, 1000000, 3, [](const E &e) { return e.tool.uvTileSize; }, [](E &e, double v) { e.tool.uvTileSize = v; }},
				{Text::tr("Quad pairing angle"), 0, 180, 1, [](const E &e) { return e.tool.maxAngle; }, [](E &e, double v) { e.tool.maxAngle = v; }}};
	case ModelEditKind::Solidify:
		return {{Text::tr("Thickness"), -100000, 100000, 3, [](const E &e) { return e.tool.solidifyThickness; },
				 [](E &e, double v) { e.tool.solidifyThickness = v; }},
				{Text::tr("Even thickness"), 0, 1, 0, [](const E &e) { return double(e.tool.evenThickness); },
				 [](E &e, double v) { e.tool.evenThickness = v > 0.5; }, true}};
	case ModelEditKind::ProjectUvMapping:
	{
		QVector<LastField> fields{{Text::tr("Projection"), 0, 3, 0, [](const E &e) { return double(int(e.tool.uvProjection)); },
								   [](E &e, double v) { e.tool.uvProjection = ModelUvProjection(int(v)); }, false,
								   {Text::tr("Cube"), Text::tr("From View"), Text::tr("Cylinder"), Text::tr("Sphere")}, true}};
		if (edit.tool.uvProjection == ModelUvProjection::Cube || edit.tool.uvProjection == ModelUvProjection::View)
			fields.append({Text::tr("Units per texture repeat"), 0.001, 100000, 3, [](const E &e) { return e.tool.uvTileSize; },
						   [](E &e, double v) { e.tool.uvTileSize = v; }});
		else
			fields.append({Text::tr("Axis"), 0, 2, 0, [](const E &e) { return double(e.tool.axis); }, [](E &e, double v) { e.tool.axis = int(v); },
						   false, {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")}});
		fields.append({Text::tr("Scale to bounds"), 0, 1, 0, [](const E &e) { return double(e.tool.uvFit); },
					   [](E &e, double v) { e.tool.uvFit = v > 0.5; }, true});
		return fields;
	}
	case ModelEditKind::Decimate:
		return {{Text::tr("Ratio"), 0, 1, 3, [](const E &e) { return e.tool.ratio; }, [](E &e, double v) { e.tool.ratio = v; }},
				{Text::tr("Triangle budget (0 uses the ratio)"), 0, 131072, 0, [](const E &e) { return double(e.tool.targetTriangles); },
				 [](E &e, double v) { e.tool.targetTriangles = int(v); }},
				{Text::tr("Keep borders"), 0, 1, 0, [](const E &e) { return double(e.tool.preserveBoundary); },
				 [](E &e, double v) { e.tool.preserveBoundary = v > 0.5; }, true}};
	default:
		return {};
	}
}
QString kindTitle(ModelEditKind kind)
{
	switch (kind)
	{
	case ModelEditKind::Transform:
		return Text::tr("Transform");
	case ModelEditKind::WeightedTransform:
		return Text::tr("Proportional Transform");
	case ModelEditKind::Extrude:
		return Text::tr("Extrude Faces");
	case ModelEditKind::ExtrudeEdges:
		return Text::tr("Extrude Edges");
	case ModelEditKind::DuplicateFaces:
		return Text::tr("Duplicate");
	case ModelEditKind::InsetFaces:
		return Text::tr("Inset Faces");
	case ModelEditKind::ShrinkFatten:
		return Text::tr("Shrink/Fatten");
	case ModelEditKind::PokeFaces:
		return Text::tr("Poke Faces");
	case ModelEditKind::SmoothVertices:
		return Text::tr("Smooth Vertices");
	case ModelEditKind::LoopCut:
		return Text::tr("Loop Cut");
	case ModelEditKind::SlideEdges:
		return Text::tr("Edge Slide");
	case ModelEditKind::MergeVertices:
		return Text::tr("Merge");
	case ModelEditKind::BeautifyFaces:
		return Text::tr("Beautify Faces");
	case ModelEditKind::ShadeAutoSmooth:
		return Text::tr("Shade Auto Smooth");
	case ModelEditKind::Symmetrize:
		return Text::tr("Symmetrize");
	case ModelEditKind::Bisect:
		return Text::tr("Bisect");
	case ModelEditKind::AddPrimitive:
		return Text::tr("Add Primitive");
	case ModelEditKind::Decimate:
		return Text::tr("Decimate");
	case ModelEditKind::RotateEdges:
		return Text::tr("Rotate Edge");
	case ModelEditKind::DissolveVertices:
		return Text::tr("Dissolve Vertices");
	case ModelEditKind::DissolveFaces:
		return Text::tr("Dissolve Faces");
	case ModelEditKind::MakeFace:
		return Text::tr("Make Face");
	case ModelEditKind::ShadeFlat:
		return Text::tr("Shade Flat");
	case ModelEditKind::ShadeSmooth:
		return Text::tr("Shade Smooth");
	case ModelEditKind::BevelVertices:
		return Text::tr("Bevel Vertices");
	case ModelEditKind::BevelEdges:
		return Text::tr("Bevel Edges");
	case ModelEditKind::Knife:
		return Text::tr("Knife");
	case ModelEditKind::Spin:
		return Text::tr("Spin");
	case ModelEditKind::Rip:
		return Text::tr("Rip");
	case ModelEditKind::Solidify:
		return Text::tr("Solidify");
	case ModelEditKind::ProjectUvMapping:
		return Text::tr("UV Projection");
	default:
		return Text::tr("Mesh Edit");
	}
}
} // namespace

void ModelEditorTools::refreshLastPanel()
{
	if (!m_lastPanel || m_adjustingLast)
		return;
	// A rebuild keeps keyboard focus on the same field rather than losing it
	// with the deleted one.
	int focusedField = -1;
	for (auto *widget = QApplication::focusWidget(); widget && m_lastFields && m_lastFields->isAncestorOf(widget); widget = widget->parentWidget())
	{
		if (widget->property("lastOperationField").isValid())
		{
			focusedField = widget->property("lastOperationField").toInt();
			break;
		}
	}
	delete m_lastFields;
	m_lastFields = new QWidget;
	auto *form = new QFormLayout(m_lastFields);
	form->setContentsMargins(0, 4, 0, 0);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	if (!m_hasLast)
	{
		m_lastTitle->setText(Text::tr("No operation to adjust yet"));
		// The profile's key, or the menu where the profile gives it none.
		const QString keys = commandKeys(QStringLiteral("meshAdjustLast"));
		auto *hint = new QLabel(keys.isEmpty() ? Text::tr("Run a tool, then choose View > Adjust Last Operation to change its settings.")
											   : Text::tr("Run a tool, then press %1 to change its settings.").arg(keys));
		hint->setObjectName(QStringLiteral("meshLastOperationHint"));
		form->addRow(hint);
		m_lastScroll->setWidget(m_lastFields);
		placeOverlays();
		if (focusedField >= 0)
			m_viewport->setFocus(Qt::OtherFocusReason);
		return;
	}
	const bool current = m_last.revision == m_editor->m_document.revisionFingerprint();
	const auto title = current ? Text::tr("Adjust Last Operation — %1").arg(kindTitle(m_last.edit.kind))
							   : Text::tr("%1 (the mesh has changed since)").arg(kindTitle(m_last.edit.kind));
	m_lastTitle->setText(current ? kindTitle(m_last.edit.kind) : title);
	m_lastTitle->setAccessibleName(title);
	m_lastTitle->setToolTip(title);
	const auto fields = lastFields(m_last.edit);
	if (fields.isEmpty())
		form->addRow(new QLabel(Text::tr("This operation has no settings.")));
	for (int index = 0; index < fields.size(); ++index)
	{
		const auto &field = fields[index];
		QWidget *editor = nullptr;
		const auto apply = [this, index](double value)
		{
			if (m_adjustingLast || m_editor->m_working || !m_hasLast || m_last.revision != m_editor->m_document.revisionFingerprint())
				return;
			const QScopedValueRollback<bool> adjusting(m_adjustingLast, true);
			auto edit = m_last.edit;
			const auto changed = lastFields(edit)[index];
			changed.set(edit, value);
			// The joint mirror map was confirmed for the last plane. A new axis,
			// or a merge distance that moves joints on or off the plane, needs it
			// confirmed again; cancelling keeps the last result.
			if (edit.kind == ModelEditKind::Symmetrize && mirrorPlaneChanged(m_last.edit, edit) && !confirmMirrorJoints(edit))
			{
				QMetaObject::invokeMethod(this, &ModelEditorTools::refreshLastPanel, Qt::QueuedConnection);
				return;
			}
			QString error;
			QByteArray base;
			ModelEditReport report;
			const int discarded = m_editor->m_document.discardedHistorySteps();
			// Undo and replay inside the worker's candidate. Failure or cancellation
			// leaves the live document and both history stacks untouched.
			const bool applied = m_editor->performWork(Text::tr("Adjust Last Operation"),
				[edit, &base, &report](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
				{
					if (!candidate.undo())
						return false;
					base = candidate.revisionFingerprint();
					return candidate.edit(edit, failure, control, &report);
				}, &error);
			if (applied)
			{
				remapHiddenAfterEdit(edit, report);
				m_last.edit = edit;
				m_last.revision = m_editor->m_document.revisionFingerprint();
				// A zero/no-op replay removed this history entry. Never let another
				// adjustment undo the preceding, unrelated operation instead.
				m_hasLast = m_last.revision != base;
			}
			m_editor->m_presentedMeshRevision.clear();
			m_editor->refresh();
			m_editor->noteHistoryTrim(discarded);
			if (!error.isEmpty() && (edit.kind == ModelEditKind::BevelEdges || edit.kind == ModelEditKind::Knife || edit.kind == ModelEditKind::Spin || edit.kind == ModelEditKind::Rip))
				showBanner(edit.kind == ModelEditKind::Rip ? Text::tr("Rip refused") : edit.kind == ModelEditKind::Spin ? Text::tr("Spin refused") : edit.kind == ModelEditKind::Knife ? Text::tr("Knife refused") : Text::tr("Bevel refused"), error,
						   LiveStatus::Problem);
			else
				showBanner(applied ? modelSkinProvenanceSummary(report.skin) : error, {}, applied ? LiveStatus::Update : LiveStatus::Problem);
			// Rebuilt after this signal returns, since it deletes the sender.
			if (changed.rebuild || !applied || !m_hasLast)
				QMetaObject::invokeMethod(this, &ModelEditorTools::refreshLastPanel, Qt::QueuedConnection);
		};
		if (field.toggle)
		{
			auto *box = new QCheckBox;
			box->setChecked(field.get(m_last.edit) > 0.5);
			connect(box, &QCheckBox::toggled, this, [apply](bool on) { apply(on ? 1 : 0); });
			editor = box;
		}
		else if (!field.choices.isEmpty())
		{
			auto *combo = new QComboBox;
			combo->addItems(field.choices);
			combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
			combo->setMinimumContentsLength(10);
			combo->setToolTip(field.choices.value(int(field.get(m_last.edit))));
			combo->setCurrentIndex(int(field.get(m_last.edit)));
			connect(combo, &QComboBox::currentIndexChanged, this, [apply](int value) { apply(value); });
			editor = combo;
		}
		else
		{
			auto *spin = new QDoubleSpinBox;
			spin->setRange(field.low, field.high);
			spin->setDecimals(field.decimals);
			spin->setValue(field.get(m_last.edit));
			spin->setKeyboardTracking(false);
			spin->setLayoutDirection(Qt::LeftToRight);
			connect(spin, &QDoubleSpinBox::valueChanged, this, apply);
			editor = spin;
		}
		editor->setEnabled(current);
		editor->setAccessibleName(field.label);
		editor->setProperty("lastOperationField", index);
		// Esc in a field returns keyboard focus to the view.
		editor->installEventFilter(this);
		form->addRow(field.label, editor);
		if (auto *label = qobject_cast<QLabel *>(form->labelForField(editor)))
			label->setWordWrap(true);
	}
	m_lastScroll->setWidget(m_lastFields);
	placeOverlays();
	if (focusedField >= 0)
	{
		QWidget *target = m_viewport;
		for (auto *widget : m_lastFields->findChildren<QWidget *>())
		{
			if (widget->property("lastOperationField").isValid() && widget->property("lastOperationField").toInt() == focusedField && widget->isEnabled())
			{
				target = widget;
				break;
			}
		}
		target->setFocus(Qt::OtherFocusReason);
	}
}

QList<QAction *> ModelEditorTools::searchableActions() const
{
	QList<QAction *> result;
	for (auto *entry : m_actions)
	{
		if (!entry->objectName().endsWith(QStringLiteral("Menu")))
			result.append(entry);
	}
	for (auto *entry : std::as_const(m_editor->m_documentActions))
	{
		// A submenu is searched through its entries.
		const auto entries = entry->menu() ? entry->menu()->actions() : QList<QAction *>{entry};
		for (auto *item : entries)
		{
			if (!item->isSeparator() && !item->text().isEmpty())
				result.append(item);
		}
	}
	// The Model menu's recovery preference, which has no other place now.
	if (m_editor->m_recoveryEnabled)
		result.append(m_editor->m_recoveryEnabled);
	std::sort(result.begin(), result.end(), [](QAction *a, QAction *b) { return plain(a->text()).localeAwareCompare(plain(b->text())) < 0; });
	return result;
}

void ModelEditorTools::showOperatorSearch()
{
	auto *popup = new QFrame(m_editor, Qt::Popup);
	popup->setObjectName(QStringLiteral("meshOperatorSearch"));
	popup->setAttribute(Qt::WA_DeleteOnClose);
	popup->setAccessibleName(Text::tr("Search commands"));
	auto *layout = new QVBoxLayout(popup);
	auto *field = new QLineEdit;
	field->setObjectName(QStringLiteral("meshOperatorSearchField"));
	field->setPlaceholderText(Text::tr("Search mesh commands…"));
	field->setAccessibleName(Text::tr("Command name"));
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("meshOperatorSearchResults"));
	list->setAccessibleName(Text::tr("Matching commands"));
	layout->addWidget(field);
	layout->addWidget(list);
	const auto actions = searchableActions();
	const auto fill = [list, actions](const QString &query)
	{
		list->clear();
		for (auto *entry : actions)
		{
			const auto name = plain(entry->text());
			if (!query.isEmpty() && !name.contains(query, Qt::CaseInsensitive))
				continue;
			const auto shortcut = entry->shortcut().toString(QKeySequence::NativeText);
			auto *item = new QListWidgetItem(entry->icon(), shortcut.isEmpty() ? name : QStringLiteral("%1\t%2").arg(name, shortcut), list);
			item->setData(Qt::UserRole, QVariant::fromValue(static_cast<QObject *>(entry)));
			item->setFlags(entry->isEnabled() ? item->flags() : item->flags() & ~Qt::ItemIsEnabled);
		}
		if (list->count() > 0)
			list->setCurrentRow(0);
	};
	fill({});
	const auto trigger = [popup, list]()
	{
		auto *item = list->currentItem();
		auto *entry = item ? qobject_cast<QAction *>(item->data(Qt::UserRole).value<QObject *>()) : nullptr;
		popup->close();
		if (entry && entry->isEnabled())
			QMetaObject::invokeMethod(entry, &QAction::trigger, Qt::QueuedConnection);
	};
	connect(field, &QLineEdit::textChanged, popup, fill);
	connect(field, &QLineEdit::returnPressed, popup, trigger);
	connect(list, &QListWidget::itemActivated, popup, [trigger](QListWidgetItem *) { trigger(); });
	struct Keys final : QObject
	{
		QListWidget *list;
		explicit Keys(QListWidget *l, QObject *parent) : QObject(parent), list(l) {}
		bool eventFilter(QObject *, QEvent *event) override
		{
			if (event->type() != QEvent::KeyPress)
				return false;
			const int key = static_cast<QKeyEvent *>(event)->key();
			if (key == Qt::Key_Down || key == Qt::Key_Up)
			{
				list->setCurrentRow(std::clamp(list->currentRow() + (key == Qt::Key_Down ? 1 : -1), 0, std::max(0, list->count() - 1)));
				return true;
			}
			return false;
		}
	};
	field->installEventFilter(new Keys(list, popup));
	popup->resize(420, 360);
	const QPoint centre = m_viewport->mapToGlobal(QPoint(m_viewport->width() / 2, m_viewport->height() / 3));
	popup->move(centre - QPoint(popup->width() / 2, 0));
	popup->show();
	field->setFocus(Qt::PopupFocusReason);
}
namespace
{
enum class TargetState
{
	Ready,
	// Only an upper bound exceeds a limit; Check Delivery Targets counts exactly.
	Check,
	Over,
};
struct TargetSummary
{
	ModelTargetProfile profile;
	ModelTargetReport report;
	TargetState state = TargetState::Ready;
};
// The selected delivery target of each format the header summarises, checked
// by counts only so the summary stays live; exports run the full preflight.
QVector<TargetSummary> targetSummaries(const ModelEditorDialog &editor, const ModelMesh &mesh)
{
	QStringList formats{QStringLiteral("md3"), QStringLiteral("md2"), QStringLiteral("mdl")};
	// The skeletal formats for skeletal models: IQM, and the MD5 mesh and
	// animation files (Q2PRO's MD5 replacement limits, for one).
	if (!mesh.skeleton.isEmpty())
		formats << QStringLiteral("iqm") << QStringLiteral("md5mesh") << QStringLiteral("md5anim");
	QVector<TargetSummary> result;
	for (const auto &format : std::as_const(formats))
	{
		TargetSummary summary;
		// MD3's frame scan (coordinate range and 1/64-unit precision) is quick
		// on most models, so the summary runs it whole and stays exact.
		qint64 positions = 0;
		for (const auto &surface : mesh.surfaces)
			positions += qint64(surface.vertexCount) * mesh.frames.size();
		const bool scan = format == QStringLiteral("md3") && positions <= modelTargetLiveScanPositions;
		const auto depth = scan ? ModelTargetDepth::Full : ModelTargetDepth::Counts;
		if (!findModelTargetProfile(format, editor.deliveryTarget(format), &summary.profile) ||
			!assessModelTarget(mesh, summary.profile, &summary.report, nullptr, {}, depth))
			continue;
		// Unchecked limits or an upper bound over its limit leave the state open.
		const bool bounded = !summary.report.unchecked.isEmpty() ||
							 std::any_of(summary.report.measures.cbegin(), summary.report.measures.cend(),
										 [](const ModelTargetMeasure &measure) { return measure.upperBound && measure.value > measure.maximum; });
		summary.state = !summary.report.ready() ? TargetState::Over : bounded ? TargetState::Check : TargetState::Ready;
		result.append(std::move(summary));
	}
	return result;
}
QString stateWord(TargetState state)
{
	return state == TargetState::Ready ? Text::tr("ready") : state == TargetState::Check ? Text::tr("check") : Text::tr("over");
}
// The format's name in the summary: MD5 mesh and MD5 animation by name.
QString formatLabel(const QString &format)
{
	return format == QStringLiteral("md5mesh")	 ? Text::tr("MD5 Mesh")
		   : format == QStringLiteral("md5anim") ? Text::tr("MD5 Animation")
												 : format.toUpper();
}
// The header chip's parts: one per format, with the MD5 mesh and animation
// files as one "MD5" part showing the worse of the two.
QStringList chipParts(const QVector<TargetSummary> &summaries)
{
	QStringList parts;
	int md5 = -1;
	TargetState md5State = TargetState::Ready;
	for (const auto &summary : summaries)
	{
		if (summary.profile.format.startsWith(QStringLiteral("md5")))
		{
			md5State = std::max(md5State, summary.state);
			if (md5 < 0)
			{
				md5 = int(parts.size());
				parts << QString();
			}
			continue;
		}
		parts << Text::tr("%1 %2").arg(summary.profile.format.toUpper(), stateWord(summary.state));
	}
	if (md5 >= 0)
		parts[md5] = Text::tr("MD5 %1").arg(stateWord(md5State));
	return parts;
}
} // namespace

void ModelEditorTools::setSnapToVertices(bool vertices)
{
	m_snapElement->setCurrentIndex(vertices ? 1 : 0);
}

QString ModelEditorTools::budgetSummary() const
{
	return chipParts(targetSummaries(*m_editor, m_editor->m_document.mesh())).join(QStringLiteral(" · "));
}

void ModelEditorTools::documentChanged()
{
	// Hidden faces are kept by index; a surface whose faces changed shows
	// again rather than hiding the wrong ones.
	if (hasHidden())
	{
		const auto &surfaces = m_editor->m_document.mesh().surfaces;
		bool pruned = false;
		for (auto it = m_hiddenFaces.begin(); it != m_hiddenFaces.end();)
		{
			if (it.key() < 0 || it.key() >= surfaces.size() || surfaces[it.key()].triangles.size() != m_hiddenTriangleCounts.value(it.key(), -1))
			{
				it = m_hiddenFaces.erase(it);
				pruned = true;
			}
			else
				++it;
		}
		if (pruned)
		{
			m_editor->m_presentedMeshRevision.clear();
			QMetaObject::invokeMethod(this, [this] { m_editor->refresh(); }, Qt::QueuedConnection);
		}
	}
	if (!m_budget)
		return;
	const auto &mesh = m_editor->m_document.mesh();
	bool all = true;
	auto *menu = m_budget->menu();
	// The target submenus are children of the menu; clear() keeps them.
	qDeleteAll(menu->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly));
	menu->clear();
	QStringList parts, lines;
	const auto summaries = targetSummaries(*m_editor, mesh);
	parts = chipParts(summaries);
	for (const auto &summary : summaries)
	{
		const auto format = formatLabel(summary.profile.format);
		all &= summary.state == TargetState::Ready;
		const int blockers = int(summary.report.issues.size());
		const auto headroom = modelTargetMeasureLines(summary.report).join(QStringLiteral("; "));
		QStringList names;
		for (const auto &limit : summary.report.unchecked)
			names << modelTargetLimitName(limit);
		const auto unchecked = names.join(QStringLiteral(", "));
		const auto line = summary.state == TargetState::Over
							  ? Text::tr("%1 · %2: %n blocker(s)", nullptr, blockers).arg(format, summary.profile.label)
						  : summary.state == TargetState::Check && !summary.report.unchecked.isEmpty()
							  ? Text::tr("%1 · %2: check, the model is too large to check %3 as you edit; Check Delivery Targets checks it")
									.arg(format, summary.profile.label, unchecked)
						  : summary.state == TargetState::Check
							  ? Text::tr("%1 · %2: check, a count before sharing is over its limit: %3").arg(format, summary.profile.label, headroom)
						  : headroom.isEmpty() ? Text::tr("%1 · %2: ready").arg(format, summary.profile.label)
											   : Text::tr("%1 · %2: ready, largest %3").arg(format, summary.profile.label, headroom);
		lines << line;
		auto *entry = menu->addAction(studioIcon(summary.state == TargetState::Ready ? QStringLiteral("success") : QStringLiteral("warning")), line);
		entry->setEnabled(false);
		for (const auto &issue : summary.report.issues)
		{
			auto *item = menu->addAction(QStringLiteral("    ") + issue.message);
			// An issue about one surface selects what it names there, so the
			// fix starts there: its faces or vertices, else the surface.
			item->setEnabled(issue.surface >= 0 && issue.surface < mesh.surfaces.size());
			if (!item->isEnabled())
				continue;
			const auto &name = mesh.surfaces.at(issue.surface).name;
			item->setToolTip(!issue.faces.isEmpty()	   ? Text::tr("Select the %n face(s) concerned on surface \"%1\". %2", nullptr, int(issue.faces.size())).arg(name, issue.fix)
							 : !issue.vertices.isEmpty() ? Text::tr("Select the %n vertex(es) concerned on surface \"%1\". %2", nullptr, int(issue.vertices.size())).arg(name, issue.fix)
														 : Text::tr("Select surface \"%1\". %2").arg(name, issue.fix));
			item->setStatusTip(item->toolTip());
			// Queued: selecting refreshes the editor, which rebuilds this menu.
			connect(item, &QAction::triggered, this, [this, issue] { m_editor->selectTargetIssue(issue); }, Qt::QueuedConnection);
		}
		// Prepare for Target, where it has something to do: blockers, or joint
		// influences past what the format keeps.
		const bool crowded = std::any_of(summary.report.measures.cbegin(), summary.report.measures.cend(), [](const ModelTargetMeasure &measure)
										 { return measure.limit == QStringLiteral("influences") && measure.value > measure.maximum; });
		// No fix applies to an md5anim's limits (joints and frames).
		if ((summary.state != TargetState::Ready || crowded) && summary.profile.format != QStringLiteral("md5anim"))
		{
			auto *prepare = menu->addAction(studioIcon(QStringLiteral("apply")), QStringLiteral("    ") + Text::tr("Prepare for %1 · %2…").arg(format, summary.profile.label));
			prepare->setObjectName(QStringLiteral("meshPrepareTarget-%1").arg(summary.profile.format));
			prepare->setToolTip(Text::tr("Review the splits, joins, reductions and moves that fit the model to %1, then apply them as one undo step.")
									.arg(summary.profile.label));
			prepare->setStatusTip(prepare->toolTip());
			connect(prepare, &QAction::triggered, this, [this, id = summary.profile.format] { m_editor->prepareForTarget(id); }, Qt::QueuedConnection);
		}
	}
	menu->addSeparator();
	// One exclusive choice of delivery target per format; the MD5 files are
	// written from skeletal models only.
	QStringList targetFormats{QStringLiteral("md3"), QStringLiteral("md2"), QStringLiteral("mdl"), QStringLiteral("iqm")};
	if (!mesh.skeleton.isEmpty())
		targetFormats << QStringLiteral("md5mesh") << QStringLiteral("md5anim");
	for (const auto &format : std::as_const(targetFormats))
	{
		const auto label = formatLabel(format);
		// The submenu says where the choice comes from unless it was made here.
		const auto origin = m_editor->deliveryTargetOrigin(format);
		auto *choices = menu->addMenu(origin == ModelDeliveryOrigin::Explicit ? Text::tr("%1 Delivery Target").arg(label)
																			   : Text::tr("%1 Delivery Target (%2)").arg(label, modelDeliveryOriginName(origin)));
		choices->menuAction()->setObjectName(QStringLiteral("meshTargetMenu-%1").arg(format));
		auto *group = new QActionGroup(choices);
		const auto selected = m_editor->deliveryTarget(format);
		for (const auto &profile : modelTargetProfiles(format))
		{
			auto *choice = choices->addAction(profile.isDefault ? Text::tr("%1 (default)").arg(profile.label) : profile.label);
			choice->setObjectName(QStringLiteral("meshTarget-%1-%2").arg(format, profile.id));
			choice->setCheckable(true);
			choice->setChecked(profile.id == selected);
			choice->setToolTip(profile.description);
			choice->setStatusTip(profile.description);
			group->addAction(choice);
			// Queued: choosing rebuilds this menu, which deletes the action.
			connect(
				choice, &QAction::triggered, this, [this, format, id = profile.id] { m_editor->setDeliveryTarget(format, id); },
				Qt::QueuedConnection);
		}
	}
	auto *save = menu->addAction(Text::tr("Save Targets with the Project"));
	save->setObjectName(QStringLiteral("meshSaveDeliveryTargets"));
	save->setEnabled(bool(m_editor->saveDeliveryPreferences) && !m_editor->m_deliveryContext.projectRoot.isEmpty());
	save->setToolTip(save->isEnabled() ? Text::tr("Save each format's target and the strict-export setting with %1; exports, stages and model build use them there.")
											 .arg(m_editor->m_deliveryContext.projectRoot)
									   : Text::tr("Open a project to save delivery targets with it."));
	save->setStatusTip(save->toolTip());
	connect(save, &QAction::triggered, this, [this] { m_editor->saveDeliveryTargetsToProject(); }, Qt::QueuedConnection);
	menu->addSeparator();
	menu->addAction(m_actions.value(QStringLiteral("meshCheckTargets")));
	menu->addAction(m_actions.value(QStringLiteral("meshDecimateMd3")));
	menu->addAction(m_actions.value(QStringLiteral("meshDetailLevels")));
	menu->addAction(m_actions.value(QStringLiteral("meshExportLods")));
	m_budget->setIcon(studioIcon(all ? QStringLiteral("success") : QStringLiteral("warning")));
	m_budget->setText(parts.join(QStringLiteral(" · ")));
	m_budget->setToolTip(Text::tr("Delivery targets: the selected engine for each format, checked as you edit (counts, and MD3's frame scan "
								  "unless the model is very large). Open to choose targets or check every limit."));
	// A screen reader hears each format's line from the menu, and hears when
	// an edit or a new target moves a format over its limits or back.
	m_budget->setAccessibleDescription(lines.join(QStringLiteral("; ")));
	const auto budget = m_budget->text();
	if (!m_budgetState.isEmpty() && budget != m_budgetState)
		announceStatus(m_budget, Text::tr("Delivery targets: %1").arg(budget), LiveStatus::Result);
	m_budgetState = budget;
	if (m_hasLast && m_lastPanel && m_lastPanel->isVisible())
		refreshLastPanel();
}

bool ModelEditorTools::exportDetailLevels(const QString &basePath, int levels, double ratio, QString *error)
{
	// More levels than any target loads keep the target's own refusal.
	return exportDetailLevels(basePath, modelLodGeneratedRecipe(std::clamp(levels, 0, modelLodMaxLevels + 1), ratio), error);
}

bool ModelEditorTools::exportDetailLevels(const QString &basePath, const QVector<ModelLodRecipeLevel> &recipe, QString *error)
{
	QString failure;
	const auto finish = [&](const QString &message)
	{
		if (error)
			*error = message;
		showBanner(message, {}, LiveStatus::Problem);
		return false;
	};
	// Prepare the whole set first: every file's bytes, the delivery target's
	// preflight and every destination's review.
	ModelLodSet set;
	const auto target = m_editor->deliveryTarget(QStringLiteral("md3"));
	const QStringList inputs{m_editor->m_recoverySourcePath};
	const auto revision = m_editor->m_document.revisionFingerprint();
	const bool strict = m_editor->strictExport();
	const auto baseDirectory = m_editor->m_document.path().isEmpty() ? QString() : QFileInfo(m_editor->m_document.path()).absolutePath();
	if (!m_editor->performWork(
			Text::tr("Prepare Detail Levels"),
			[basePath, recipe, baseDirectory, target, inputs, strict, &set](ModelDocument &candidate, QString *problem, const ModelWorkControl &control)
			{
				return prepareModelLodSet(candidate.mesh(), recipe, baseDirectory, basePath, target, inputs + QStringList{candidate.path()}, strict, &set,
										  problem, control);
			},
			&failure))
		return finish(failure);
	bool overwrite = false;
	const auto existing = set.existing();
	if (!existing.isEmpty())
	{
		// Hand-made detail files can sit beside the base name; never replace
		// one without asking, and name every file that would be replaced.
		QMessageBox confirm(QMessageBox::Question, Text::tr("Replace Detail Levels"),
							Text::tr("Files of this detail set that already exist and would be replaced (%n): %1. Replace them?", nullptr,
									 int(existing.size()))
								.arg(existing.join(QStringLiteral(", "))),
							QMessageBox::Yes | QMessageBox::Cancel, m_editor);
		confirm.setObjectName(QStringLiteral("meshLodReplaceConfirm"));
		confirm.setDefaultButton(QMessageBox::Cancel);
		confirm.button(QMessageBox::Yes)->setText(Text::tr("Replace"));
		confirm.setAccessibleName(confirm.windowTitle());
		if (confirm.exec() != QMessageBox::Yes)
		{
			m_editor->m_status->setText(Text::tr("Nothing was written; the existing detail files are unchanged."));
			if (error)
				*error = m_editor->m_status->text();
			return false;
		}
		if (revision != m_editor->m_document.revisionFingerprint())
			return finish(Text::tr("The model changed while confirming. Export the detail levels again."));
		overwrite = true;
	}
	if (!m_editor->performWork(
			Text::tr("Export Detail Levels"),
			[&set, overwrite](ModelDocument &, QString *problem, const ModelWorkControl &control) { return publishModelLodSet(&set, overwrite, problem, control); },
			&failure, true))
		return finish(failure);
	// One report for the set, as model lod prints it: each file's decimation
	// and export notes, prefixed with its name.
	QStringList written;
	ModelExportReport combined;
	for (const auto &file : std::as_const(set.files))
	{
		written << file.path;
		const auto name = QFileInfo(file.path).fileName();
		for (const auto &note : file.notes + file.exportNotes)
		{
			combined.notes << Text::tr("%1: %2").arg(name, note);
			if (file.exportOmissions.contains(note))
				combined.omissions << combined.notes.last();
		}
	}
	// Detail files past the set (or MDC files) stay and still load with the
	// model: they come first in the report, and the status says so.
	combined.notes = modelLodSiblingLines(set.stale) + combined.notes;
	combined.target.format = QStringLiteral("md3");
	combined.target.target = set.target;
	combined.target.label = set.targetLabel;
	m_editor->presentExportReport(Text::tr("Exported %n model file(s) for %1: %2.", nullptr, int(written.size()))
										  .arg(set.targetLabel, written.join(QStringLiteral(", "))) +
									  (set.stale.isEmpty() ? QString() : QLatin1Char(' ') + modelLodSiblingSummary(set.stale)),
								  combined);
	return true;
}
} // namespace vibestudio
