// Modeller profiles in the Mesh Editor: keys, gestures and transform rules
// from core/model_editor_controls.h, lasting tools and axis locks for 3ds Max
// and MilkShape 3D users, hiding and isolating, and the commands profiles add
// (Border and Element levels, flatten, snap to grid, frame stepping).
#include "app/model_editor_tools.h"

#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QToolBar>
#include <QCoreApplication>
#include <QKeySequence>
#include <QPushButton>
#include <QTabWidget>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTools)
};

int selectedCount(const ModelSelection &selection)
{
	return int(selection.faces.size() + selection.vertices.size() + selection.edges.size() + selection.surfaces.size() +
			   (selection.tag.isEmpty() ? 0 : 1) + (selection.collision.isEmpty() ? 0 : 1));
}
} // namespace

bool ModelEditorTools::held(Qt::KeyboardModifiers current, Qt::KeyboardModifiers wanted) const
{
	return wanted != Qt::NoModifier && (current & wanted) == wanted;
}

void ModelEditorTools::applyControls(const ModelEditorControls &controls)
{
	if (m_modal)
		finishModal(false);
	m_controls = controls;
	m_zoomDragging = m_toolPressed = m_toolDragging = m_subtractDragging = false;
	// Keys: every command takes the profile's keys; dialog commands too.
	const auto keysFor = [](const QStringList &portable)
	{
		QList<QKeySequence> keys;
		for (const auto &key : portable)
		{
			const auto sequence = QKeySequence::fromString(key, QKeySequence::PortableText);
			if (!sequence.isEmpty())
				keys.append(sequence);
		}
		return keys;
	};
	for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it)
	{
		if (modelEditorCommandForId(it.key()))
			it.value()->setShortcuts(keysFor(modelEditorCommandKeys(m_controls, it.key())));
	}
	for (const auto &id : {QStringLiteral("openMesh"), QStringLiteral("saveMesh"), QStringLiteral("undoMesh"), QStringLiteral("redoMesh")})
	{
		if (auto *action = m_editor->findChild<QAction *>(id))
		{
			action->setShortcuts(keysFor(modelEditorCommandKeys(m_controls, id)));
			action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
			if (!m_editor->actions().contains(action))
				m_editor->addAction(action);
		}
	}
	// Navigation: the viewport reads the level camera's routing. Clicks reach
	// it with Shift as its toggle, whatever the profile's extend keys are.
	auto camera = m_controls.navigation.view3D;
	camera.toggleModifiers = Qt::ShiftModifier;
	camera.faceModifiers = Qt::NoModifier;
	camera.perspective = m_viewport->isPerspective();
	m_viewport->setCameraControls(camera, true);
	m_blenderNavigation = m_controls.navigation.view3D.orbitButton == Qt::MiddleButton &&
						  m_controls.navigation.view3D.orbitModifiers == Qt::NoModifier;
	if (auto *navigation = m_actions.value(QStringLiteral("meshBlenderNavigation")))
	{
		const QSignalBlocker blocker(navigation);
		navigation->setChecked(m_blenderNavigation);
	}
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(m_controls.profileId, &profile));
	m_viewport->setControlsHelp(profile.description);
	if (m_controls.transform.style == ModelTransformStyle::Modal)
		setLastingTool(LastingTool::None);
	m_borderMode = m_elementMode = false;
	refreshHeader();
}

void ModelEditorTools::setLastingTool(LastingTool tool)
{
	m_lastingTool = tool;
	if (tool != LastingTool::None)
	{
		m_editor->m_transformTool->setCurrentIndex(tool == LastingTool::Move ? 0 : tool == LastingTool::Rotate ? 1 : 2);
		m_editor->m_moveGizmo->setChecked(true);
	}
	else if (m_controls.transform.style == ModelTransformStyle::ToolMode)
		m_editor->m_moveGizmo->setChecked(false);
	for (const auto &[name, value] : {std::pair{QStringLiteral("meshToolSelect"), LastingTool::None},
									  std::pair{QStringLiteral("meshToolMove"), LastingTool::Move},
									  std::pair{QStringLiteral("meshToolRotate"), LastingTool::Rotate},
									  std::pair{QStringLiteral("meshToolScale"), LastingTool::Scale}})
	{
		if (auto *action = m_actions.value(name))
		{
			const QSignalBlocker blocker(action);
			action->setChecked(value == tool);
		}
	}
	refreshHeader();
}

void ModelEditorTools::setAxisLock(int axis, bool plane)
{
	m_axisLock = axis >= 0 && axis < 3 ? axis : -1;
	m_planeLock = m_axisLock >= 0 && plane;
	const QString names[3]{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
	if (m_axisLock < 0)
		showBanner(Text::tr("Transforms are free."));
	else if (m_planeLock)
		showBanner(Text::tr("Transforms stay in the plane across %1.").arg(names[m_axisLock]));
	else
		showBanner(Text::tr("Transforms follow %1 only.").arg(names[m_axisLock]));
}

void ModelEditorTools::beginToolDrag(const QPointF &point, bool duplicate)
{
	const ModelModalOperator op = duplicate							  ? ModelModalOperator::Duplicate
								  : m_lastingTool == LastingTool::Rotate ? ModelModalOperator::Rotate
								  : m_lastingTool == LastingTool::Scale	 ? ModelModalOperator::Scale
																		 : ModelModalOperator::Move;
	if (!beginModal(op, m_pressPoint))
		return;
	if (m_axisLock >= 0)
		constrainModal(m_axisLock, m_planeLock);
	m_toolDragging = true;
	moveModal(point, Qt::NoModifier);
}

bool ModelEditorTools::pointerOnSelection(const QPointF &point) const
{
	// MilkShape moves the selection from anywhere in a view; 3ds Max needs
	// the press on it. Without a precise hit, the selection's screen bounds
	// stand in, inflated a little so thin selections stay reachable.
	const auto &mesh = m_editor->m_document.mesh();
	const auto selection = m_editor->m_document.selection();
	if (selectedCount(selection) == 0)
		return false;
	if (m_controls.layout.family == QLatin1String("milkshape"))
		return true;
	QRectF bounds;
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, std::max(0, int(mesh.frames.size()) - 1));
	const auto addPoint = [&](const ModelVec3 &position)
	{
		QPointF screen;
		if (!m_viewport->projectToView(position, &screen))
			return;
		bounds = bounds.isNull() ? QRectF(screen, QSizeF(0.01, 0.01)) : bounds.united(QRectF(screen, QSizeF(0.01, 0.01)));
	};
	const auto addSurface = [&](int index, const QSet<int> &vertices)
	{
		if (index < 0 || index >= mesh.surfaces.size() || mesh.surfaces[index].frames.isEmpty())
			return;
		const auto &positions = mesh.surfaces[index].frames.value(frame).positions;
		for (int vertex : vertices)
		{
			if (vertex >= 0 && vertex < positions.size())
				addPoint(positions[vertex]);
		}
	};
	QSet<int> vertices = selection.vertices;
	for (auto edge : selection.edges)
		vertices << edge.first << edge.second;
	if (selection.surface >= 0 && selection.surface < mesh.surfaces.size())
	{
		const auto &surface = mesh.surfaces[selection.surface];
		for (int face : selection.faces)
		{
			if (face >= 0 && face < surface.triangles.size())
				vertices << surface.triangles[face].a << surface.triangles[face].b << surface.triangles[face].c;
		}
	}
	addSurface(selection.surface, vertices);
	for (int index : selection.surfaces)
	{
		QSet<int> all;
		for (int vertex = 0; vertex < mesh.surfaces.value(index).vertexCount; ++vertex)
			all.insert(vertex);
		addSurface(index, all);
	}
	if (!selection.tag.isEmpty() || !selection.collision.isEmpty())
		return true;
	return !bounds.isNull() && bounds.adjusted(-8, -8, 8, 8).contains(point);
}

void ModelEditorTools::clickSelect(const QMouseEvent &event, Qt::KeyboardModifiers modifiers)
{
	const auto &selection = m_controls.selection;
	const bool extend = held(modifiers, selection.extendModifiers);
	const bool subtract = held(modifiers, selection.subtractModifiers);
	if (!extend && !subtract)
	{
		forwardClick(event, Qt::NoModifier);
		return;
	}
	// The viewport toggles; keep only the half of the toggle the profile wants.
	const ModelSelection before = m_editor->m_document.selection();
	forwardClick(event, Qt::ShiftModifier);
	const ModelSelection after = m_editor->m_document.selection();
	const int grew = selectedCount(after) - selectedCount(before);
	if ((subtract && grew > 0) || (extend && !selection.extendToggles && grew < 0))
		applySelection(before);
}

void ModelEditorTools::zoomBy(double notches, const QPointF &anchor)
{
	m_forwarding = true;
	QWheelEvent wheel(anchor, m_viewport->mapToGlobal(anchor), QPoint(), QPoint(0, int(std::lround(notches * 120))), Qt::NoButton, Qt::NoModifier,
					  Qt::NoScrollPhase, false);
	QCoreApplication::sendEvent(m_viewport, &wheel);
	m_forwarding = false;
}

// --- Hiding ----------------------------------------------------------------

bool ModelEditorTools::hasHidden() const
{
	for (const auto &faces : m_hiddenFaces)
	{
		if (!faces.isEmpty())
			return true;
	}
	return false;
}

ModelMesh ModelEditorTools::displayMesh(const ModelMesh &mesh) const
{
	if (!hasHidden())
		return mesh;
	ModelMesh display = mesh;
	for (auto it = m_hiddenFaces.cbegin(); it != m_hiddenFaces.cend(); ++it)
	{
		if (it.key() < 0 || it.key() >= display.surfaces.size())
			continue;
		auto &triangles = display.surfaces[it.key()].triangles;
		for (int face : it.value())
		{
			// A collapsed triangle keeps every index stable but covers nothing,
			// so it neither draws nor picks.
			if (face >= 0 && face < triangles.size())
				triangles[face] = {triangles[face].a, triangles[face].a, triangles[face].a};
		}
	}
	return display;
}

ModelSelection ModelEditorTools::withoutHidden(const ModelSelection &selection) const
{
	const auto hidden = m_hiddenFaces.value(selection.surface);
	if (hidden.isEmpty())
		return selection;
	const auto &mesh = m_editor->m_document.mesh();
	if (selection.surface < 0 || selection.surface >= mesh.surfaces.size())
		return selection;
	const auto &surface = mesh.surfaces[selection.surface];
	QSet<int> visibleVertices;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!hidden.contains(face))
			visibleVertices << surface.triangles[face].a << surface.triangles[face].b << surface.triangles[face].c;
	}
	ModelSelection result = selection;
	for (int face : hidden)
		result.faces.remove(face);
	for (auto it = result.vertices.begin(); it != result.vertices.end();)
		it = visibleVertices.contains(*it) ? std::next(it) : result.vertices.erase(it);
	for (auto it = result.edges.begin(); it != result.edges.end();)
		it = visibleVertices.contains(it->first) && visibleVertices.contains(it->second) ? std::next(it) : result.edges.erase(it);
	return result;
}

void ModelEditorTools::applyHidden()
{
	// The document did not change; the displayed copy did.
	m_editor->m_presentedMeshRevision.clear();
	m_editor->m_highlightedRevision.clear();
	m_editor->refresh();
	refreshHeader();
}

void ModelEditorTools::hideSelection(bool unselected)
{
	const auto &mesh = m_editor->m_document.mesh();
	const auto selection = m_editor->m_document.selection();
	QHash<int, QSet<int>> chosen;
	const auto facesTouching = [&](int index, const QSet<int> &vertices)
	{
		QSet<int> faces;
		const auto &surface = mesh.surfaces[index];
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto &t = surface.triangles[face];
			if (vertices.contains(t.a) || vertices.contains(t.b) || vertices.contains(t.c))
				faces.insert(face);
		}
		return faces;
	};
	if (!selection.surfaces.isEmpty())
	{
		for (int index : selection.surfaces)
		{
			if (index < 0 || index >= mesh.surfaces.size())
				continue;
			for (int face = 0; face < mesh.surfaces[index].triangles.size(); ++face)
				chosen[index].insert(face);
		}
	}
	else if (selection.surface >= 0 && selection.surface < mesh.surfaces.size())
	{
		QSet<int> vertices = selection.vertices;
		for (auto edge : selection.edges)
			vertices << edge.first << edge.second;
		chosen[selection.surface] = selection.faces;
		if (!vertices.isEmpty())
			chosen[selection.surface] += facesTouching(selection.surface, vertices);
	}
	int count = 0;
	for (int index = 0; index < mesh.surfaces.size(); ++index)
	{
		const auto &faces = chosen.value(index);
		for (int face = 0; face < mesh.surfaces[index].triangles.size(); ++face)
		{
			if (faces.contains(face) != unselected)
			{
				if (!m_hiddenFaces[index].contains(face))
					++count;
				m_hiddenFaces[index].insert(face);
				m_hiddenTriangleCounts[index] = mesh.surfaces[index].triangles.size();
			}
		}
	}
	if (count == 0)
	{
		showBanner(unselected ? Text::tr("Everything is selected; nothing to hide.") : Text::tr("Select something to hide first."));
		return;
	}
	applySelection(unselected ? selection : ModelSelection{selection.surface, {}, {}});
	applyHidden();
	showBanner(Text::tr("%1 face(s) hidden. Reveal Hidden shows them again.").arg(count));
}

void ModelEditorTools::revealHidden()
{
	if (!hasHidden())
	{
		showBanner(Text::tr("Nothing is hidden."));
		return;
	}
	// Blender selects what it reveals, so the revealed part can be acted on.
	const auto selection = m_editor->m_document.selection();
	auto revealed = selection;
	if (m_hiddenFaces.contains(selection.surface) && selection.surfaces.isEmpty() && selectionMode() == 0)
		revealed.faces += m_hiddenFaces.value(selection.surface);
	m_hiddenFaces.clear();
	m_hiddenTriangleCounts.clear();
	applyHidden();
	applySelection(revealed);
	showBanner(Text::tr("Revealed every hidden face."));
}

void ModelEditorTools::isolateSelection()
{
	if (hasHidden())
	{
		revealHidden();
		return;
	}
	hideSelection(true);
	frameSelection();
}

// --- Commands profiles add ---------------------------------------------------

void ModelEditorTools::buildProfileActions()
{
	addAction(QStringLiteral("meshToggleEditMode"), Text::tr("Toggle Object/Edit Mode"), {},
			  [this]
			  {
				  const int mode = selectionMode();
				  if (mode == 5)
					  setSelectionMode(m_lastEditMode >= 0 && m_lastEditMode <= 2 ? m_lastEditMode : 1);
				  else
				  {
					  m_lastEditMode = mode >= 0 && mode <= 2 ? mode : 1;
					  setSelectionMode(5);
				  }
			  });
	addAction(QStringLiteral("meshModeBorder"), Text::tr("Border Select Mode"), {},
			  [this]
			  {
				  setSelectionMode(2);
				  m_borderMode = true;
				  m_elementMode = false;
				  showBanner(Text::tr("Border: click an open edge to select its whole boundary loop."));
			  });
	addAction(QStringLiteral("meshModeElement"), Text::tr("Element Select Mode"), {},
			  [this]
			  {
				  setSelectionMode(0);
				  m_elementMode = true;
				  m_borderMode = false;
				  showBanner(Text::tr("Element: click a face to select its whole connected piece."));
			  });
	addAction(QStringLiteral("meshModeObject"), Text::tr("Object (Surface) Select Mode"), {}, [this] { setSelectionMode(5); });
	addAction(QStringLiteral("meshModeTag"), Text::tr("Tag and Joint Mode"), {}, [this] { setSelectionMode(3); });
	addAction(QStringLiteral("meshSelectLoop"), Text::tr("Select Edge Loops of Selection"), {},
			  [this]
			  {
				  const auto selection = m_editor->m_document.selection();
				  if (selection.edges.isEmpty())
				  {
					  showBanner(Text::tr("Select edges first; each grows to its loop."));
					  return;
				  }
				  auto result = selection;
				  for (auto edge : selection.edges)
				  {
					  ModelSelectRequest request;
					  request.operation = ModelSelectOperation::Loop;
					  request.mode = ModelSelectionMode::Edges;
					  request.edge = edge;
					  request.extend = true;
					  ModelSelection grown;
					  if (selectModelComponents(m_editor->m_document.mesh(), result, request, &grown))
						  result = grown;
				  }
				  applySelection(result);
			  });
	addAction(QStringLiteral("meshSelectRing"), Text::tr("Select Edge Rings of Selection"), {},
			  [this]
			  {
				  const auto selection = m_editor->m_document.selection();
				  if (selection.edges.isEmpty())
				  {
					  showBanner(Text::tr("Select edges first; each grows to its ring."));
					  return;
				  }
				  auto result = selection;
				  for (auto edge : selection.edges)
				  {
					  ModelSelectRequest request;
					  request.operation = ModelSelectOperation::Ring;
					  request.mode = ModelSelectionMode::Edges;
					  request.edge = edge;
					  request.extend = true;
					  ModelSelection grown;
					  if (selectModelComponents(m_editor->m_document.mesh(), result, request, &grown))
						  result = grown;
				  }
				  applySelection(result);
			  });
	addAction(QStringLiteral("meshHide"), Text::tr("Hide Selected"), {}, [this] { hideSelection(false); });
	addAction(QStringLiteral("meshHideUnselected"), Text::tr("Hide Unselected"), {}, [this] { hideSelection(true); });
	addAction(QStringLiteral("meshReveal"), Text::tr("Reveal Hidden"), {}, [this] { revealHidden(); });
	addAction(QStringLiteral("meshIsolate"), Text::tr("Isolate Selection"), {}, [this] { isolateSelection(); });
	for (const auto &[name, text, tool] :
		 {std::tuple{QStringLiteral("meshToolSelect"), Text::tr("Select Tool"), LastingTool::None},
		  std::tuple{QStringLiteral("meshToolMove"), Text::tr("Move Tool"), LastingTool::Move},
		  std::tuple{QStringLiteral("meshToolRotate"), Text::tr("Rotate Tool"), LastingTool::Rotate},
		  std::tuple{QStringLiteral("meshToolScale"), Text::tr("Scale Tool"), LastingTool::Scale}})
	{
		auto *action = addAction(name, text, {}, [this, tool] { setLastingTool(tool); });
		action->setCheckable(true);
		action->setChecked(tool == LastingTool::None);
	}
	for (int axis = 0; axis < 3; ++axis)
	{
		const QString names[3]{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
		addAction(QStringLiteral("meshAxis%1").arg(names[axis]), Text::tr("Constrain to %1").arg(names[axis]), {},
				  [this, axis] { setAxisLock(m_axisLock == axis && !m_planeLock ? -1 : axis, false); });
	}
	addAction(QStringLiteral("meshAxisFree"), Text::tr("Free or Planar Constraint"), {},
			  [this]
			  {
				  // 3ds Max's F8 cycles the XY, YZ and ZX planes.
				  if (!m_planeLock)
					  setAxisLock(2, true);
				  else if (m_axisLock == 2)
					  setAxisLock(0, true);
				  else if (m_axisLock == 0)
					  setAxisLock(1, true);
				  else
					  setAxisLock(-1, false);
			  });
	addAction(QStringLiteral("meshToggleSnap"), Text::tr("Toggle Snapping"), {}, [this] { m_editor->m_snapTranslation->toggle(); });
	for (int axis = 0; axis < 3; ++axis)
	{
		const QString names[3]{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
		addAction(QStringLiteral("meshFlatten%1").arg(names[axis]), Text::tr("Flatten %1").arg(names[axis]), {},
				  [this, axis]
				  {
					  // MilkShape's Flatten gives the selection one coordinate:
					  // a zero scale along the axis about the selection's centre.
					  ModelVec3 centre{};
					  if (!selectionCentre(&centre))
					  {
						  showBanner(Text::tr("Select the vertices to flatten first."));
						  return;
					  }
					  auto edit = toolEdit(ModelEditKind::Transform);
					  edit.pivotMode = ModelTransformPivot::Custom;
					  edit.pivot = centre;
					  edit.transformSpace = ModelTransformSpace::World;
					  edit.scale = {axis == 0 ? 0.0f : 1.0f, axis == 1 ? 0.0f : 1.0f, axis == 2 ? 0.0f : 1.0f};
					  QString error;
					  if (!runTool(edit, &error))
						  showBanner(error);
				  });
	}
	addAction(QStringLiteral("meshSnapSelectionToGrid"), Text::tr("Snap Selection to Grid"), {},
			  [this]
			  {
				  // Each selected vertex moves to its nearest grid point.
				  const auto &mesh = m_editor->m_document.mesh();
				  const auto selection = m_editor->m_document.selection();
				  const double grid = std::max(0.000001, m_editor->m_translationGrid->value());
				  if (selection.surface < 0 || selection.surface >= mesh.surfaces.size())
					  return;
				  const auto &surface = mesh.surfaces[selection.surface];
				  QSet<int> vertices = selection.vertices;
				  for (auto edge : selection.edges)
					  vertices << edge.first << edge.second;
				  for (int face : selection.faces)
				  {
					  if (face >= 0 && face < surface.triangles.size())
						  vertices << surface.triangles[face].a << surface.triangles[face].b << surface.triangles[face].c;
				  }
				  const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, std::max(0, int(surface.frames.size()) - 1));
				  if (vertices.isEmpty() || surface.frames.isEmpty())
				  {
					  showBanner(Text::tr("Select the vertices to snap first."));
					  return;
				  }
				  // One undoable edit per vertex would flood history; snap through
				  // a single weighted transform per distinct offset instead.
				  QHash<QString, QSet<int>> groups;
				  QHash<QString, ModelVec3> offsets;
				  for (int vertex : vertices)
				  {
					  if (vertex < 0 || vertex >= surface.frames[frame].positions.size())
						  continue;
					  const auto p = surface.frames[frame].positions[vertex];
					  const ModelVec3 snapped{float(std::round(p.x / grid) * grid), float(std::round(p.y / grid) * grid),
											  float(std::round(p.z / grid) * grid)};
					  const ModelVec3 offset{snapped.x - p.x, snapped.y - p.y, snapped.z - p.z};
					  const QString key = QStringLiteral("%1 %2 %3").arg(offset.x, 0, 'g', 9).arg(offset.y, 0, 'g', 9).arg(offset.z, 0, 'g', 9);
					  groups[key].insert(vertex);
					  offsets.insert(key, offset);
				  }
				  int moved = 0;
				  for (auto it = groups.cbegin(); it != groups.cend(); ++it)
				  {
					  const auto offset = offsets.value(it.key());
					  if (std::abs(offset.x) + std::abs(offset.y) + std::abs(offset.z) < 1e-9)
						  continue;
					  auto edit = toolEdit(ModelEditKind::Transform);
					  edit.selection = {selection.surface, it.value(), {}};
					  edit.transformSpace = ModelTransformSpace::World;
					  edit.translation = offset;
					  QString error;
					  if (!runTool(edit, &error))
					  {
						  showBanner(error);
						  break;
					  }
					  moved += it.value().size();
				  }
				  applySelection(selection);
				  showBanner(Text::tr("%1 vertices snapped to the %2-unit grid.").arg(moved).arg(grid));
			  });
	addAction(QStringLiteral("meshShowUvEditor"), Text::tr("Texture Coordinate Editor"), {},
			  [this]
			  {
				  if (auto *tabs = m_editor->findChild<QTabWidget *>(QStringLiteral("meshViewTabs")))
					  tabs->setCurrentIndex(tabs->currentIndex() == 1 ? 0 : 1);
			  });
	addAction(QStringLiteral("meshAssignMaterial"), Text::tr("Assign Material to Selection"), {},
			  [this]
			  {
				  // The surface's material is assigned in the Surface panel.
				  m_editor->showSidebarPage(QStringLiteral("surface"));
			  });
	addAction(QStringLiteral("meshViewUser"), Text::tr("User View"), {}, [this] { m_editor->m_viewPreset->setCurrentIndex(0); });
	addAction(QStringLiteral("meshToggleQuadView"), Text::tr("Toggle Four Views"), {}, [this] { m_editor->toggleFourViews(); });
	addAction(QStringLiteral("meshMaximizeView"), Text::tr("Maximise View"), {}, [this] { m_editor->toggleMaximisedView(); });
	addAction(QStringLiteral("meshToggleSidebars"), Text::tr("Toggle Sidebars"), {}, [this] { m_editor->toggleSidebars(); });
	addAction(QStringLiteral("meshToggleToolShelf"), Text::tr("Toggle Tool Shelf"), {},
			  [this]
			  {
				  if (m_shelf)
					  m_shelf->setVisible(!m_shelf->isVisible());
			  });
	addAction(QStringLiteral("meshToggleWireframe"), Text::tr("Toggle Wireframe"), {},
			  [this] { m_editor->m_renderMode->setCurrentIndex(m_editor->m_renderMode->currentIndex() == 1 ? 0 : 1); });
	addAction(QStringLiteral("meshToggleGrid"), Text::tr("Toggle Grid"), {}, [this] { m_viewport->setShowGrid(!m_viewport->showGrid()); });
	addAction(QStringLiteral("meshToggleEdges"), Text::tr("Toggle Edged Faces"), {}, [this] { m_viewport->setShowEdges(!m_viewport->showEdges()); });
	addAction(QStringLiteral("meshPlay"), Text::tr("Play or Pause"), {}, [this] { m_viewport->togglePlayback(); });
	const auto stepFrame = [this](int target)
	{
		auto *frames = m_editor->m_frame;
		if (frames->count() == 0)
			return;
		m_viewport->pause();
		frames->setCurrentIndex(std::clamp(target, 0, frames->count() - 1));
	};
	addAction(QStringLiteral("meshFrameNext"), Text::tr("Next Frame"), {},
			  [this, stepFrame] { stepFrame((m_editor->m_frame->currentIndex() + 1) % std::max(1, m_editor->m_frame->count())); });
	addAction(QStringLiteral("meshFramePrevious"), Text::tr("Previous Frame"), {},
			  [this, stepFrame]
			  {
				  const int count = std::max(1, m_editor->m_frame->count());
				  stepFrame((m_editor->m_frame->currentIndex() + count - 1) % count);
			  });
	addAction(QStringLiteral("meshFrameFirst"), Text::tr("First Frame"), {}, [stepFrame] { stepFrame(0); });
	addAction(QStringLiteral("meshFrameLast"), Text::tr("Last Frame"), {}, [this, stepFrame] { stepFrame(m_editor->m_frame->count() - 1); });
	addAction(QStringLiteral("meshCopyPoseToFrame"), Text::tr("Copy Pose to Current Frame"), {},
			  [this]
			  {
				  if (m_editor->m_copyFramePose && m_editor->m_copyFramePose->isEnabled())
					  m_editor->m_copyFramePose->click();
				  else
					  showBanner(Text::tr("Choose a different source frame in Animation to copy its pose here."));
			  });
	addAction(QStringLiteral("meshTypeIn"), Text::tr("Numeric Transform"), {}, [this] { m_editor->showSidebarPage(QStringLiteral("item")); });
}

} // namespace vibestudio
