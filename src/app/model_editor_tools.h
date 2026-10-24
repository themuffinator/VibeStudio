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

#include "app/studio_accessibility.h"
#include "core/model_document.h"
#include "core/model_editor_controls.h"
#include "core/model_selection_tools.h"

#include <QHash>
#include <QLineF>
#include <QMap>
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
class QScrollArea;
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

// The navigation gizmo as it is painted and clicked: in the view's trailing
// top corner (the left one in a right-to-left layout), below the heads-up
// tags and the tool banner and clear of Adjust Last Operation, sized from the
// heads-up font. Every axis bubble is a click target at least 24 px across;
// a view too small for that shows no gizmo.
struct ModelNavigationGizmo
{
	// The backdrop; empty while the gizmo is off or has no room.
	QRectF bounds;
	double bubbleRadius = 0;
	struct Bubble
	{
		int axis = 0;
		bool positive = true;
		QPointF at;
		// Larger is farther from the eye.
		double depth = 0;
	};
	// Far to near, as painted; a click takes the nearest bubble it touches.
	QVector<Bubble> bubbles;
};

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
	BevelVertices,
	EdgeSlide,
	BevelEdges,
	Knife,
	Spin,
	SpinDuplicate,
	Rip,
	RipFill
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
	// same entry points the keyboard and mouse use. `pointerOverView` false
	// marks the view's centre standing in for a pointer outside the view.
	// Loop Cut starts on the one selected edge between quads when there is
	// one, else on the edge under the pointer; a pointer that moves onto
	// another edge takes over until it leaves every edge.
	bool beginModal(ModelModalOperator op, const QPointF &pointer, bool pointerOverView = true);
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
	// Knife semantic controls also support keyboard-only authoring and tests.
	bool addKnifePoint(const QPointF &point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	void newKnifePath();
	void undoKnifePoint();
	void setKnifeThrough(bool through);
	void setKnifeSelectedOnly(bool selectedOnly);
	void setKnifeAngleSnap(bool enabled, double degrees = 45);
	void setRipOptions(bool opposite, bool fill);

	// Region selection in viewport coordinates: shift extends, subtract removes.
	bool selectRegion(const QPolygonF &region, bool extend, bool subtract);
	bool selectCircle(const QPointF &centre, double radius, bool subtract);
	// Click gestures: Alt (loop), Ctrl+Alt (ring), Ctrl (shortest path), L (linked).
	bool pickLoop(const QPointF &point, bool ring, bool extend);
	// Select Edge Loops / Rings of Selection: each selected edge (in vertex
	// mode, each edge between selected vertices) grows to its loop or ring
	// through selectModelEdgeLoops, with no pointer involved; seeds without
	// one are named in the banner's details.
	bool selectEdgeLoops(bool ring);
	bool pickPath(const QPointF &point);
	bool pickLinked(const QPointF &point, bool subtract);

	void setCursor3d(const ModelVec3 &point);
	[[nodiscard]] ModelVec3 cursor3d() const;
	bool placeCursor(const QPointF &point);
	// Aligns the view along +/- an axis (0 X, 1 Y, 2 Z), as the gizmo does.
	void alignView(int axis, bool positive);
	[[nodiscard]] ModelNavigationGizmo navigationGizmo();
	void frameSelection();

	[[nodiscard]] QMenuBar *menuBar() const { return m_menuBar; }
	[[nodiscard]] QWidget *lastOperationPanel() const;
	// Keyboard focus (model_editor_keyboard.cpp). Ctrl+Tab and Ctrl+Shift+Tab
	// (Control+Tab on macOS, modelEditorFocusKeys) move to the first control
	// of the next or previous panel in reading order, in every profile, so a
	// view whose profile takes Tab never traps focus. F9 puts focus in Adjust
	// Last Operation; Esc there returns it to the view.
	void focusPanel(bool forward);
	// The panels Ctrl+Tab visits, in reading order: the leading sidebar, the
	// editing header (with the view controls), tool shelf, the views, timeline
	// and trailing sidebar. Anything else in the editor (the status strip and
	// its material images' state included) counts as one more panel after
	// them.
	[[nodiscard]] QList<QWidget *> focusPanels() const;
	[[nodiscard]] QAction *action(const QString &objectName) const;
	void showOperatorSearch();
	// Refreshes the delivery-target summary and the last-operation panel after
	// any document change or target choice.
	void documentChanged();
	// Writes name.md3 and its Quake III detail levels name_1.md3, name_2.md3 for
	// the selected MD3 delivery target. The whole set is prepared first; when a
	// file exists, one confirmation names every file to be replaced, and Cancel
	// writes nothing.
	bool exportDetailLevels(const QString &basePath, int levels, double ratio, QString *error = nullptr);
	// The same for a recipe (core/model_lod.h); hand-made levels resolve
	// against the source's folder.
	bool exportDetailLevels(const QString &basePath, const QVector<ModelLodRecipeLevel> &recipe, QString *error = nullptr);
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
	[[nodiscard]] ModelMesh displayMesh(const ModelMesh &mesh, const ModelEditReport *report = nullptr) const;

  protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

  private:
	// State of the running modal operator; previews and commits share its edit.
	struct Modal
	{
		ModelModalOperator op = ModelModalOperator::None;
		QPointF start, pointer, pivotScreen, lastActual;
		QPointF slideVector{1, 0};
		double slidePixels = 200;
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
		// Loop Cut runs on the selected edge (the seed) while the pointer is
		// over no other edge.
		bool hoverFromSelection = false;
		ModelEdge seedEdge{-1, -1};
		int seedSurface = -1;
		int ripFace = -1;
		bool ripOpposite = false, ripFill = false;
		QVector<QLineF> guides;
		// Vertex snapping: other vertices on screen and the moving selection's points.
		QVector<QPointF> snapScreen;
		QVector<ModelVec3> snapWorld, selectedPoints;
		bool snapped = false;
		QPointF snapPoint;
		ModelKnifeOptions knife;
		QVector<QPolygonF> knifePaths;
		QVector<ModelEdge> knifeSnapEdges;
		QVector<int> knifeSnapVertices;
		QPolygonF knifeProjection;
		QByteArray knifeRevision;
		QPointF knifeHover;
		bool knifeAngle = false;
		double knifeAngleStep = 45;
		ModelEditReport report;
	};
	struct LastOperation
	{
		ModelEdit edit;
		QByteArray revision;
		QString title;
	};
	// A header button that names a command's keys in its tooltip and
	// accessible description, refreshed whenever the profile's keys change.
	struct KeyHint
	{
		QPointer<QToolButton> button;
		QString command, name, detail;
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
	QScrollArea *m_lastScroll = nullptr;
	QToolButton *m_modeButtons[3]{};
	QToolButton *m_proportional = nullptr, *m_xray = nullptr, *m_mirror[3]{};
	QComboBox *m_falloff = nullptr;
	QDoubleSpinBox *m_radius = nullptr;
	QCheckBox *m_connected = nullptr;
	QToolButton *m_budget = nullptr;
	// The delivery targets as last shown, so a change can be announced.
	QString m_budgetState;
	QComboBox *m_snapElement = nullptr;
	QHash<QString, QAction *> m_actions;
	QVector<KeyHint> m_keyHints;
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
	// The joint mirror map last confirmed for Symmetrize on a skinned surface.
	QMap<QString, QString> m_mirrorJoints;
	bool m_keepJointNames = false;
	// The binding last confirmed for Add Primitive on a skinned surface (None
	// for an unskinned new surface), and whether a bound primitive got its own
	// surface.
	ModelNewGeometryBinding m_primitiveBinding = ModelNewGeometryBinding::Joint;
	QString m_primitiveJoint;
	bool m_primitiveNewSurface = false;
	// Document refreshes during an adjustment must not delete its signal sender.
	bool m_adjustingLast = false;
	ModelVec3 m_cursor{};
	bool m_blenderNavigation = true;
	bool m_forwarding = false;
	bool m_swallowRelease = false;
	bool m_showGizmo = true;
	// Where the gizmo was last painted, so overlay moves repaint the view only
	// when the gizmo has to move with them.
	QRectF m_paintedGizmo;
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
	// Keyboard focus and key hints (model_editor_keyboard.cpp).
	void buildFocusActions();
	// A command keyed to Tab or Shift+Tab listens only while the view itself
	// has focus; every other command listens anywhere in its area.
	void scopeShortcuts(QAction *action);
	void refreshKeyHints();
	[[nodiscard]] QString keyboardHelp() const;
	// The native text of a command's keys under the current controls; empty
	// when the profile gives it none.
	[[nodiscard]] QString commandKeys(const QString &command) const;
	// Each tool bar (document actions, tool shelf) is one Tab stop: the arrow
	// keys, Home and End move between its buttons and Enter or Space runs one.
	void makeToolBarKeyboardReachable(QToolBar *bar);
	void refreshToolBarStop(QToolBar *bar, QToolButton *preferred = nullptr);
	void focusLastOperation();
	// Keys of the Adjust Last Operation settings and the tool bar buttons:
	// what a focused control uses itself (Space in a check box, letters in a
	// choice, arrows between buttons) wins over the view's commands, and
	// Esc in a setting returns to the view.
	bool handleControlKey(QObject *watched, QEvent *event);
	void refreshHeader();
	void refreshLastPanel();
	void placeOverlays();
	// The banner over the view, echoed on the status line. A message that
	// answers a key or a click is announced politely; a readout that follows
	// the pointer or typing passes LiveStatus::Update and is not; a refusal
	// passes LiveStatus::Problem and is announced with its reason.
	void showBanner(const QString &text, const QString &details = {}, LiveStatus kind = LiveStatus::Result);
	void paintOverlay(QPainter &painter);
	[[nodiscard]] int selectionMode() const;
	void setSelectionMode(int mode);
	[[nodiscard]] ModelSelection selectionFor(ModelEditKind kind) const;
	void selectOperation(ModelSelectOperation operation, std::function<void(ModelSelectRequest &)> configure = {});
	void runKind(ModelEditKind kind, std::function<void(ModelEdit &)> configure = {});
	// Symmetrize on a skinned surface with off-centre joints: confirm the joint
	// mirror map first; elsewhere the edit's map is cleared. Used by the menu,
	// Repeat Last and F9 replays on another plane. False when the user cancels.
	bool confirmMirrorJoints(ModelEdit &edit);
	// Whether an F9 change to a Symmetrize edit changes which joints sit off
	// the mirror plane (a new axis or merge distance).
	[[nodiscard]] bool mirrorPlaneChanged(const ModelEdit &before, const ModelEdit &after) const;
	// Add Primitive on a skinned surface: choose how the new vertices follow
	// the skeleton first; elsewhere the edit's binding is cleared. Used by the
	// menu and Repeat Last. False when the user cancels.
	bool confirmPrimitiveBinding(ModelEdit &edit);
	void addPrimitive(ModelPrimitive primitive);
	void startModalAtPointer(ModelModalOperator op);
	// The one geometric edge between quads the selection names, as Loop Cut's seed.
	bool loopCutSeed(ModelEdge *edge, int *surface) const;
	void updateModalPreview();
	void restoreDocumentView();
	void showPreview(const ModelMesh &mesh, const ModelSelection &selection, const ModelEditReport *report = nullptr);
	void initialiseKnife(Modal &modal);
	bool initialiseRip(Modal &modal, QString *error);
	void moveKnife(const QPointF &point, Qt::KeyboardModifiers modifiers);
	void updateKnifePreview();
	bool knifeContextUnchanged() const;
	bool handleKnifeKey(QKeyEvent *event);
	void paintKnife(QPainter &painter);
	void remapHiddenAfterEdit(const ModelEdit &edit, const ModelEditReport &report);
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
};
} // namespace vibestudio
