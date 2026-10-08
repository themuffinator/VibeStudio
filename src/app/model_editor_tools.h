#pragma once

// Blender-style interaction layer for the Mesh Editor.
//
// Adds header menus (View, Select, Add, Mesh, Vertex, Edge, Face), a header
// row (select modes, proportional editing, mesh symmetry, X-ray), a tool
// shelf, keyboard-driven modal operators (grab, rotate, scale, extrude, inset,
// shrink/fatten, duplicate, loop cut) with axis constraints, typed values and
// snapping, box/circle/lasso and loop/ring/path picking, a 3D cursor, a
// navigation gizmo, operator search (F3) and an Adjust Last Operation panel
// (F9). Every edit runs through the editor's ordinary document service as one
// undo step, and previews apply the same core edit to a copy of the mesh, so a
// preview never differs from the committed result.
//
// Interaction design follows Blender's documented edit mode
// (https://docs.blender.org/manual/en/latest/modeling/meshes/index.html);
// no Blender code is used.

#include "core/model_document.h"
#include "core/model_editor_controls.h"
#include "core/model_selection_tools.h"

#include <QHash>
#include <QLineF>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QFrame;
class QKeyEvent;
class QLabel;
class QMenu;
class QMenuBar;
class QMouseEvent;
class QPainter;
class QToolBar;
class QToolButton;
class QWidget;

namespace vibestudio
{
class ModelEditorDialog;
class ModelViewport;

enum class ModelModalOperator
{
	None,
	Move,
	Rotate,
	Scale,
	Extrude,
	Inset,
	ShrinkFatten,
	Duplicate,
	LoopCut,
	BevelVertices
};

class ModelEditorTools final : public QObject
{
  public:
	explicit ModelEditorTools(ModelEditorDialog *editor);
	~ModelEditorTools() override;

	// Replaces the document selection and refreshes every view (no undo step).
	void applySelection(const ModelSelection &selection);
	// Selection operators on the document selection (no undo step).
	bool select(ModelSelectRequest request, QString *error = nullptr);
	// One mesh tool through the document worker; remembered for F9 and Shift+R.
	bool runTool(const ModelEdit &edit, QString *error = nullptr);
	// A tool edit pre-filled from the editor: selection (converted to what the
	// tool needs), displayed pose, frame scope, pivot and transform axes.
	[[nodiscard]] ModelEdit toolEdit(ModelEditKind kind) const;

	// Modal operators. The pointer is in viewport coordinates; tests drive the
	// same entry points the keyboard and mouse use.
	bool beginModal(ModelModalOperator op, const QPointF &pointer);
	void moveModal(const QPointF &pointer, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	// Axis 0-2: the first press constrains to the world axis, the second to the
	// selection's axis and the third clears it. A plane constraint excludes it.
	void constrainModal(int axis, bool plane);
	// Digits, '.', '-' (sign) and '\b' (backspace) edit the typed value.
	void typeModal(const QString &text);
	void adjustModal(int steps);
	bool finishModal(bool commit, QString *error = nullptr);
	[[nodiscard]] ModelModalOperator modalOperator() const;
	[[nodiscard]] QString modalText() const;
	[[nodiscard]] bool modalPreviewValid() const;
	[[nodiscard]] const ModelMesh *modalPreview() const;

	// Region selection in viewport coordinates: shift extends, subtract removes.
	bool selectRegion(const QPolygonF &region, bool extend, bool subtract);
	bool selectCircle(const QPointF &centre, double radius, bool subtract);
	// Click gestures: Alt (loop), Ctrl+Alt (ring), Ctrl (shortest path), L (linked).
	bool pickLoop(const QPointF &point, bool ring, bool extend);
	bool pickPath(const QPointF &point);
	bool pickLinked(const QPointF &point, bool subtract);

	void setCursor3d(const ModelVec3 &point);
	[[nodiscard]] ModelVec3 cursor3d() const;
	bool placeCursor(const QPointF &point);
	// Aligns the view along +/- an axis (0 X, 1 Y, 2 Z), as the gizmo does.
	void alignView(int axis, bool positive);
	void frameSelection();

	[[nodiscard]] QMenuBar *menuBar() const { return m_menuBar; }
	[[nodiscard]] QWidget *lastOperationPanel() const;
	[[nodiscard]] QAction *action(const QString &objectName) const;
	void showOperatorSearch();
	// Refreshes the export budget and the last-operation panel after any document change.
	void documentChanged();
	// Writes name.md3 and its Quake III detail levels name_1.md3, name_2.md3.
	bool exportDetailLevels(const QString &basePath, int levels, double ratio, QString *error = nullptr);
	[[nodiscard]] QString budgetSummary() const;
	// Searchable commands, as the operator search lists them.
	[[nodiscard]] QList<QAction *> searchableActions() const;
	[[nodiscard]] bool blenderNavigation() const { return m_blenderNavigation; }
	// Snap target while snapping: increments (grid steps) or vertices.
	void setSnapToVertices(bool vertices);
	void setBlenderNavigation(bool enabled);

	// Modeller profiles (model_editor_profiles.cpp): every key, gesture and
	// transform rule comes from these controls.
	void applyControls(const ModelEditorControls &controls);
	[[nodiscard]] const ModelEditorControls &controls() const { return m_controls; }
	// The lasting tool of tool-mode profiles (3ds Max's Q/W/E/R, MilkShape's
	// F1-F4); None selects. Modal profiles keep None.
	enum class LastingTool
	{
		None,
		Move,
		Rotate,
		Scale
	};
	void setLastingTool(LastingTool tool);
	[[nodiscard]] LastingTool lastingTool() const { return m_lastingTool; }
	// A lasting axis constraint for tool drags (0-2), or -1 for none; with
	// `plane` the constraint excludes the axis.
	void setAxisLock(int axis, bool plane);
	[[nodiscard]] int axisLock() const { return m_axisLock; }
	// Hiding: hidden faces are not drawn, picked or selected until revealed.
	void hideSelection(bool unselected);
	void revealHidden();
	void isolateSelection();
	[[nodiscard]] bool hasHidden() const;
	[[nodiscard]] const QHash<int, QSet<int>> &hiddenFaces() const { return m_hiddenFaces; }
	// The display copy of a mesh with hidden faces collapsed, so face indices
	// stay stable while nothing hidden draws or picks.
	[[nodiscard]] ModelMesh displayMesh(const ModelMesh &mesh) const;

  protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

  private:
	// State of the running modal operator; previews and commits share its edit.
	struct Modal
	{
		ModelModalOperator op = ModelModalOperator::None;
		QPointF start, pointer, pivotScreen, lastActual;
		ModelVec3 pivot{}, normal{};
		int axis = -1;
		bool plane = false, local = false;
		ModelTransformBasis basis;
		QString typed;
		double angle = 0, lastAngle = 0, startDistance = 1;
		Qt::KeyboardModifiers modifiers;
		ModelSelection selection;
		ModelEdit edit;
		bool editReady = false, valid = false, edges = false, individual = false;
		QString error;
		ModelMesh preview;
		ModelSelection previewSelection;
		bool previewShown = false, pending = false;
		ModelEdge hoverEdge{-1, -1};
		int hoverSurface = -1, cuts = 1;
		QVector<QLineF> guides;
		// Vertex snapping: other vertices on screen and the moving selection's points.
		QVector<QPointF> snapScreen;
		QVector<ModelVec3> snapWorld, selectedPoints;
		bool snapped = false;
		QPointF snapPoint;
	};
	struct LastOperation
	{
		ModelEdit edit;
		QByteArray revision;
		QString title;
	};
	enum class Region
	{
		None,
		Box,
		Circle,
		Lasso
	};
	ModelEditorDialog *m_editor = nullptr;
	ModelViewport *m_viewport = nullptr;
	// The dialog destroys its widgets before this child object.
	QPointer<QObject> m_viewportGuard;
	QWidget *m_viewArea = nullptr;
	QMenuBar *m_menuBar = nullptr;
	QToolBar *m_shelf = nullptr;
	QWidget *m_header = nullptr;
	QLabel *m_banner = nullptr;
	QFrame *m_lastPanel = nullptr;
	QLabel *m_lastTitle = nullptr;
	QWidget *m_lastFields = nullptr;
	QToolButton *m_modeButtons[3]{};
	QToolButton *m_proportional = nullptr, *m_xray = nullptr, *m_mirror[3]{};
	QComboBox *m_falloff = nullptr;
	QDoubleSpinBox *m_radius = nullptr;
	QCheckBox *m_connected = nullptr;
	QToolButton *m_budget = nullptr;
	QComboBox *m_snapElement = nullptr;
	QHash<QString, QAction *> m_actions;
	std::unique_ptr<Modal> m_modal;
	ModelEditorControls m_controls = modelEditorControlsForProfile(defaultModelEditorProfileId());
	LastingTool m_lastingTool = LastingTool::None;
	int m_axisLock = -1;
	bool m_planeLock = false;
	// A drag zoom (Blender Ctrl+middle, 3ds Max Ctrl+Alt+middle, MilkShape
	// Shift+left) in progress, and a tool drag started on the selection.
	bool m_zoomDragging = false, m_toolPressed = false, m_toolDragging = false, m_subtractDragging = false;
	// 3ds Max Border and Element levels: clicks pick a whole boundary loop or piece.
	bool m_borderMode = false, m_elementMode = false;
	// The edit mode Tab returns to from object (surface) mode.
	int m_lastEditMode = 1;
	QPointF m_zoomLast;
	QHash<int, QSet<int>> m_hiddenFaces;
	// Each hidden surface's face count when it was hidden.
	QHash<int, int> m_hiddenTriangleCounts;
	LastOperation m_last;
	bool m_hasLast = false;
	ModelVec3 m_cursor{};
	bool m_blenderNavigation = true;
	bool m_forwarding = false;
	bool m_swallowRelease = false;
	bool m_showGizmo = true;
	// Region selection gesture state.
	Region m_region = Region::None;
	bool m_regionArmed = false, m_regionDragging = false, m_circleMode = false;
	double m_circleRadius = 32;
	QPointF m_pressPoint, m_pointer;
	Qt::KeyboardModifiers m_pressModifiers;
	Qt::MouseButton m_pressButton = Qt::NoButton;
	QPolygonF m_lasso;
	bool m_loopCutHover = false;

	void buildActions();
	void buildProfileActions();
	void buildMenus();
	void buildHeader();
	void buildShelf();
	void buildOverlays();
	QAction *addAction(const QString &name, const QString &text, const QString &shortcut, std::function<void()> run,
					   const QString &icon = {});
	QMenu *menu(const QString &title, const QStringList &actions);
	void refreshHeader();
	void refreshLastPanel();
	void placeOverlays();
	void showBanner(const QString &text);
	void paintOverlay(QPainter &painter);
	[[nodiscard]] int selectionMode() const;
	void setSelectionMode(int mode);
	[[nodiscard]] ModelSelection selectionFor(ModelEditKind kind) const;
	void selectOperation(ModelSelectOperation operation, std::function<void(ModelSelectRequest &)> configure = {});
	void runKind(ModelEditKind kind, std::function<void(ModelEdit &)> configure = {});
	void addPrimitive(ModelPrimitive primitive);
	void startModalAtPointer(ModelModalOperator op);
	void updateModalPreview();
	void restoreDocumentView();
	void showPreview(const ModelMesh &mesh, const ModelSelection &selection);
	[[nodiscard]] QPointF pointerInViewport() const;
	[[nodiscard]] bool selectionCentre(ModelVec3 *centre) const;
	[[nodiscard]] double worldPerPixel(const ModelVec3 &at);
	[[nodiscard]] ModelEdge edgeNear(const QPointF &point, int *surface = nullptr);
	[[nodiscard]] int faceAt(const QPointF &point, int *surface = nullptr);
	bool handleKey(QKeyEvent *event);
	bool handleModalKey(QKeyEvent *event);
	bool handleMouse(QMouseEvent *event);
	void forwardClick(const QMouseEvent &event, Qt::KeyboardModifiers modifiers);
	// A click with the profile's extend and subtract rules applied.
	void clickSelect(const QMouseEvent &event, Qt::KeyboardModifiers modifiers);
	[[nodiscard]] bool pointerOnSelection(const QPointF &point) const;
	[[nodiscard]] bool held(Qt::KeyboardModifiers current, Qt::KeyboardModifiers wanted) const;
	void zoomBy(double notches, const QPointF &anchor);
	void beginToolDrag(const QPointF &point, bool duplicate);
	void applyHidden();
	[[nodiscard]] ModelSelection withoutHidden(const ModelSelection &selection) const;
	[[nodiscard]] int gizmoAxisAt(const QPointF &point, bool *positive);
	[[nodiscard]] QPointF gizmoCentre() const;
};
} // namespace vibestudio
