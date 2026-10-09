// Mesh Editor interaction layer: Blender-style menus, select modes, modal
// operators with typed values and axis constraints, region and loop picking,
// the 3D cursor, Add primitives, Adjust Last Operation, proportional and
// mirrored editing, view alignment and command search.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "core/model_design.h"
#include "core/studio_settings.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>

#include <cmath>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
bool near(double a, double b, double tolerance = 1e-3)
{
	return std::abs(a - b) <= tolerance;
}
// A flat 4x4 grid of quads at z = 0, two poses (the second raised by 8).
ModelMesh gridMesh(int cells = 4, double size = 8, double origin = 0)
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	auto &surface = mesh.surfaces[0];
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= cells; ++j)
	{
		for (int i = 0; i <= cells; ++i)
			positions.append({float(origin + i * size), float(origin + j * size), 0});
	}
	const auto at = [&](int i, int j) { return j * (cells + 1) + i; };
	for (int j = 0; j < cells; ++j)
	{
		for (int i = 0; i < cells; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	}
	surface.triangles = triangles;
	surface.vertexCount = positions.size();
	surface.texCoords.clear();
	for (const auto &p : positions)
		surface.texCoords.append({p.x / 64.0f, p.y / 64.0f});
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame, frame};
	for (auto &p : surface.frames[1].positions)
		p.z += 8;
	surface.uvSeams.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
int vertexAt(const ModelSurface &surface, float x, float y, float z = 0)
{
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		const auto p = surface.frames[0].positions[v];
		if (near(p.x, x) && near(p.y, y) && near(p.z, z))
			return v;
	}
	return -1;
}
QSet<int> allFaces(const ModelMesh &mesh)
{
	QSet<int> faces;
	for (int face = 0; face < mesh.surfaces[0].triangles.size(); ++face)
		faces.insert(face);
	return faces;
}
void key(QWidget *widget, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = {})
{
	QKeyEvent press(QEvent::KeyPress, code, modifiers, text);
	QCoreApplication::sendEvent(widget, &press);
	QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
	QCoreApplication::sendEvent(widget, &release);
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-editor-tools-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("mesh-tools-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	QString error;
	auto *editor = new ModelEditorDialog;
	editor->resize(1500, 950);
	editor->show();
	app.processEvents();
	auto *tools = editor->tools();
	auto *viewport = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	if (!expect(tools && viewport, "the editor creates its interaction layer and keeps the viewport"))
		return EXIT_FAILURE;
	expect(editor->setMesh(gridMesh(), &error), "load a grid");
	const auto &document = editor->document();

	// Menus, shelf and shortcuts.
	auto *menus = editor->findChild<QMenuBar *>(QStringLiteral("meshMenuBar"));
	expect(menus && menus->actions().size() >= 7, "View, Select, Add, Mesh, Vertex, Edge and Face menus exist");
	expect(editor->findChild<QToolBar *>(QStringLiteral("meshToolShelf")) && !editor->findChild<QToolBar *>(QStringLiteral("meshToolShelf"))->actions().isEmpty(),
		   "the tool shelf lists tools");
	expect(tools->action(QStringLiteral("meshMove"))->shortcut() == QKeySequence(Qt::Key_G) &&
			   tools->action(QStringLiteral("meshLoopCut"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_R) &&
			   tools->action(QStringLiteral("meshOperatorSearch"))->shortcut() == QKeySequence(Qt::Key_F3),
		   "Blender's G, Ctrl+R and F3 keys are bound");
	expect(tools->blenderNavigation() && viewport->cameraControls().orbitButton == Qt::MiddleButton,
		   "Blender navigation orbits with the middle button");

	// Top view, framed, for deterministic projections.
	tools->alignView(2, true);
	viewport->frameModel();
	expect(tests::settleModelViewport(*viewport), "viewport settles");

	// Select all, select modes and conversion.
	tools->action(QStringLiteral("meshSelectAll"))->trigger();
	expect(document.selection().faces.size() == 32, "A selects every face");
	editor->findChild<QToolButton *>(QStringLiteral("meshSelectMode0"))->click();
	expect(document.selection().vertices.size() == 25 && document.selection().faces.isEmpty(), "vertex mode converts faces to their corners");
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	expect(document.selection().faces.size() == 32, "face mode converts fully selected corners back to faces");
	tools->action(QStringLiteral("meshSelectNone"))->trigger();
	expect(document.selection().faces.isEmpty(), "Alt+A clears the selection");

	// Modal move with a typed value along Z, driven by keys.
	auto selectFaces = [&](const QSet<int> &faces) { tools->applySelection({0, {}, faces}); };
	selectFaces({0});
	expect(tools->beginModal(ModelModalOperator::Move, QPointF(400, 300)), "G starts a modal move");
	key(viewport, Qt::Key_Z, Qt::NoModifier, QStringLiteral("z"));
	key(viewport, Qt::Key_8, Qt::NoModifier, QStringLiteral("8"));
	expect(tools->modalPreviewValid() && tools->modalText().contains(QStringLiteral("8")), "typed values preview along the constraint");
	key(viewport, Qt::Key_Return);
	expect(tools->modalOperator() == ModelModalOperator::None, "Enter confirms");
	const auto &surface = document.mesh().surfaces[0];
	const auto raised = surface.triangles[0];
	expect(near(surface.frames[0].positions[raised.a].z, 8) && near(surface.frames[1].positions[raised.a].z, 16),
		   "the move applies to every pose");
	const auto moved = document.mesh();
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(document.mesh().surfaces[0].frames[0].positions[raised.a].z == 0, "the move is one undo step");

	// Escape cancels without touching the document.
	const auto before = document.revisionFingerprint();
	expect(tools->beginModal(ModelModalOperator::Rotate, QPointF(400, 300)), "R starts a modal rotation");
	tools->typeModal(QStringLiteral("30"));
	if (!expect(tools->modalPreviewValid(), "rotation previews"))
		std::cerr << tools->modalText().toStdString() << '\n';
	key(viewport, Qt::Key_Escape);
	expect(tools->modalOperator() == ModelModalOperator::None && document.revisionFingerprint() == before, "Escape cancels the preview");

	// Extrude a face along its normal by a typed distance.
	selectFaces({0, 1});
	const int triangles = document.mesh().surfaces[0].triangles.size();
	expect(tools->beginModal(ModelModalOperator::Extrude, QPointF(400, 300)), "E starts an extrusion");
	tools->typeModal(QStringLiteral("4"));
	expect(tools->finishModal(true, &error), "confirm the extrusion");
	expect(document.mesh().surfaces[0].triangles.size() == triangles + 8, "extruding a quad adds its four side walls");
	double top = -1;
	for (const auto &p : document.mesh().surfaces[0].frames[0].positions)
		top = std::max(top, double(p.z));
	expect(near(top, 4), "extrusion follows the face normal");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();

	// Inset with a typed thickness, then adjust it from the last-operation panel.
	selectFaces(allFaces(document.mesh()));
	expect(tools->beginModal(ModelModalOperator::Inset, QPointF(400, 300)), "I starts an inset");
	tools->typeModal(QStringLiteral("2"));
	expect(tools->finishModal(true, &error), "confirm the inset");
	expect(vertexAt(document.mesh().surfaces[0], 2, 2) >= 0, "inner corners sit two units in");
	auto *panel = tools->lastOperationPanel();
	auto spins = panel->findChildren<QDoubleSpinBox *>();
	expect(!spins.isEmpty(), "Adjust Last Operation shows the inset settings");
	if (!spins.isEmpty())
	{
		spins.first()->setValue(4);
		app.processEvents();
		expect(vertexAt(document.mesh().surfaces[0], 4, 4) >= 0 && vertexAt(document.mesh().surfaces[0], 2, 2) < 0,
			   "changing the thickness redoes the inset in place");
	}
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(document.mesh().surfaces[0].vertexCount == 25, "one undo removes the adjusted inset");

	// Loop cut by pointing at an edge.
	viewport->frameModel();
	expect(tests::settleModelViewport(*viewport), "viewport settles before the loop cut");
	QPointF a, b;
	const auto &grid = document.mesh().surfaces[0];
	expect(viewport->projectToView(grid.frames[0].positions[vertexAt(grid, 8, 0)], &a) &&
			   viewport->projectToView(grid.frames[0].positions[vertexAt(grid, 8, 8)], &b),
		   "project an edge");
	const QPointF middle = (a + b) / 2;
	expect(tools->beginModal(ModelModalOperator::LoopCut, middle), "Ctrl+R starts a loop cut");
	tools->moveModal(middle);
	app.processEvents();
	expect(tools->modalPreviewValid(), "the ring under the pointer previews");
	tools->adjustModal(1);
	expect(tools->finishModal(true, &error), "confirm two cuts");
	expect(document.mesh().surfaces[0].triangles.size() == 32 + 16, "two cuts across a four-quad ring add sixteen triangles");
	expect(document.selection().edges.size() == 8, "the new loops are selected");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();

	// Box select every visible face.
	expect(tests::settleModelViewport(*viewport), "viewport settles before box select");
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	expect(tools->selectRegion(QPolygonF(QRectF(viewport->rect())), false, false) && document.selection().faces.size() == 32,
		   "a box around the view selects every visible face");
	expect(tools->selectRegion(QPolygonF(QRectF(QPointF(0, 0), QPointF(viewport->width() / 2.0, viewport->height()))), false, true) &&
			   !document.selection().faces.isEmpty() && document.selection().faces.size() < 32,
		   "Ctrl+drag subtracts a region");

	// Loop picking under the pointer.
	tools->action(QStringLiteral("meshModeEdge"))->trigger();
	expect(tests::settleModelViewport(*viewport), "viewport settles before loop picking");
	expect(tools->pickLoop(middle, false, false) && document.selection().edges.size() == 4, "Alt+click selects an edge loop");
	expect(tests::settleModelViewport(*viewport), "viewport settles before ring picking");
	expect(tools->pickLoop(middle, true, false) && document.selection().edges.size() == 5, "Ctrl+Alt+click selects an edge ring");

	// The 3D cursor and Add.
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	QPointF centre;
	expect(viewport->projectToView({16, 16, 0}, &centre) && tools->placeCursor(centre), "Shift+right-click places the 3D cursor");
	expect(near(tools->cursor3d().x, 16, 0.5) && near(tools->cursor3d().y, 16, 0.5) && near(tools->cursor3d().z, 0, 1e-3),
		   "the cursor lands on the surface under the pointer");
	tools->setCursor3d({100, 0, 0});
	tools->action(QStringLiteral("meshAddCube"))->trigger();
	const auto &withCube = document.mesh().surfaces[0];
	expect(withCube.triangles.size() == 32 + 12 && document.selection().faces.size() == 12, "Add Cube adds a selected cube at the cursor");
	spins = tools->lastOperationPanel()->findChildren<QDoubleSpinBox *>();
	if (expect(spins.size() >= 6, "Adjust Last Operation exposes the primitive's location and size"))
	{
		spins[3]->setValue(64);
		app.processEvents();
		double maxX = -1e9;
		for (const auto &p : document.mesh().surfaces[0].frames[0].positions)
			maxX = std::max(maxX, double(p.x));
		expect(near(maxX, 132), "resizing the last cube rebuilds it");
	}
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();

	// Proportional editing.
	editor->findChild<QToolButton *>(QStringLiteral("meshProportional"))->setChecked(true);
	editor->findChild<QComboBox *>(QStringLiteral("meshProportionalFalloff"))->setCurrentIndex(int(ModelFalloff::Linear));
	editor->findChild<QDoubleSpinBox *>(QStringLiteral("meshProportionalRadius"))->setValue(16);
	tools->action(QStringLiteral("meshModeVertex"))->trigger();
	const auto &g = document.mesh().surfaces[0];
	const int centreVertex = vertexAt(g, 16, 16), neighbour = vertexAt(g, 24, 16);
	tools->applySelection({0, {centreVertex}, {}});
	expect(tools->beginModal(ModelModalOperator::Move, QPointF(400, 300)), "proportional move starts");
	tools->constrainModal(2, false);
	tools->typeModal(QStringLiteral("10"));
	expect(tools->finishModal(true, &error), "confirm the proportional move");
	expect(near(document.mesh().surfaces[0].frames[0].positions[centreVertex].z, 10) &&
			   near(document.mesh().surfaces[0].frames[0].positions[neighbour].z, 5),
		   "neighbours follow the falloff");
	editor->findChild<QToolButton *>(QStringLiteral("meshProportional"))->setChecked(false);
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();

	// Mirror editing on a grid centred on X = 0.
	expect(editor->setMesh(gridMesh(4, 8, -16), &error), "load a centred grid");
	const auto &m = document.mesh().surfaces[0];
	const int right = vertexAt(m, 8, 0), left = vertexAt(m, -8, 0);
	editor->findChild<QToolButton *>(QStringLiteral("meshMirrorX"))->setChecked(true);
	tools->applySelection({0, {right}, {}});
	expect(tools->beginModal(ModelModalOperator::Move, QPointF(400, 300)), "mirrored move starts");
	tools->constrainModal(0, false);
	tools->typeModal(QStringLiteral("2"));
	expect(tools->finishModal(true, &error), "confirm the mirrored move");
	expect(near(document.mesh().surfaces[0].frames[0].positions[right].x, 10) && near(document.mesh().surfaces[0].frames[0].positions[left].x, -10),
		   "the counterpart moves symmetrically");
	editor->findChild<QToolButton *>(QStringLiteral("meshMirrorX"))->setChecked(false);
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(!document.isModified(), "undo returns to the loaded source");

	// Views, framing and the navigation gizmo.
	tools->alignView(1, false);
	expect(editor->findChild<QComboBox *>(QStringLiteral("meshViewPreset"))->currentIndex() == 2, "Numpad 1 looks from the front");
	tools->alignView(2, true);
	expect(editor->findChild<QComboBox *>(QStringLiteral("meshViewPreset"))->currentIndex() == 1, "Numpad 7 looks from the top");
	tools->frameSelection();

	// Export budget, modal vertex bevel and Quake III detail levels.
	{
		auto *budget = editor->findChild<QToolButton *>(QStringLiteral("meshExportBudget"));
		expect(budget && budget->text().contains(QStringLiteral("MD3")), "the header reports the export budget");
		expect(editor->setMesh(gridMesh(), &error), "reload a grid for bevel");
		const auto &b = document.mesh().surfaces[0];
		tools->applySelection({0, {vertexAt(b, 16, 16)}, {}});
		expect(tools->beginModal(ModelModalOperator::BevelVertices, QPointF(400, 300)), "Ctrl+Shift+B starts a vertex bevel");
		tools->typeModal(QStringLiteral("2"));
		expect(tools->finishModal(true, &error), "confirm the bevel");
		expect(vertexAt(document.mesh().surfaces[0], 16, 16) < 0 && vertexAt(document.mesh().surfaces[0], 18, 16) >= 0,
			   "the bevelled vertex becomes a ring of points");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		const auto lodBase = QDir(temporary.path()).filePath(QStringLiteral("prop.md3"));
		expect(tools->exportDetailLevels(lodBase, 2, 0.5, &error) && QFile::exists(QDir(temporary.path()).filePath(QStringLiteral("prop_1.md3"))) &&
				   QFile::exists(QDir(temporary.path()).filePath(QStringLiteral("prop_2.md3"))),
			   "the editor exports the base model and its detail levels");
		tools->applySelection({0, {}, allFaces(document.mesh())});
		tools->action(QStringLiteral("meshSolidify"))->trigger();
		expect(document.mesh().surfaces[0].triangles.size() == 32 + 32 + 32, "Solidify gives the grid a shell");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		expect(!document.isModified(), "the grid returns to its loaded state");
	}

	// Vertex snapping: a moved cube's nearest corner lands on a grid vertex.
	{
		expect(editor->setMesh(gridMesh(), &error), "reload a grid for snapping");
		tools->alignView(2, true);
		viewport->frameModel();
		tools->setCursor3d({40, 40, 0});
		tools->action(QStringLiteral("meshAddCube"))->trigger();
		viewport->frameModel();
		expect(tests::settleModelViewport(*viewport), "viewport settles before snapping");
		QPointF start, target;
		const auto &s = document.mesh().surfaces[0];
		expect(viewport->projectToView({40, 40, 0}, &start) && viewport->projectToView(s.frames[0].positions[vertexAt(s, 16, 16)], &target),
			   "project the cube and a grid vertex");
		tools->setSnapToVertices(true);
		editor->findChild<QCheckBox *>(QStringLiteral("meshSnapTranslation"))->setChecked(true);
		expect(tools->beginModal(ModelModalOperator::Move, start), "move the new cube");
		tools->moveModal(target);
		app.processEvents();
		expect(tools->modalText().contains(QStringLiteral("snapped")), "the move reports a vertex snap");
		expect(tools->finishModal(true, &error), "confirm the snapped move");
		int onTarget = 0;
		for (const auto &p : document.mesh().surfaces[0].frames[0].positions)
			onTarget += near(p.x, 16) && near(p.y, 16) && near(p.z, 0);
		expect(onTarget >= 2, "a cube corner lands exactly on the grid vertex");
		editor->findChild<QCheckBox *>(QStringLiteral("meshSnapTranslation"))->setChecked(false);
		tools->setSnapToVertices(false);
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		expect(!document.isModified(), "snapping test returns to the loaded grid");
	}

	// Command search finds tools by name.
	tools->showOperatorSearch();
	app.processEvents();
	auto *field = editor->findChild<QLineEdit *>(QStringLiteral("meshOperatorSearchField"));
	auto *results = editor->findChild<QListWidget *>(QStringLiteral("meshOperatorSearchResults"));
	if (expect(field && results, "F3 opens command search"))
	{
		field->setText(QStringLiteral("inset"));
		bool found = false;
		for (int row = 0; row < results->count(); ++row)
			found |= results->item(row)->text().contains(QStringLiteral("Inset"));
		expect(found, "searching 'inset' lists Inset Faces");
		field->window()->close();
	}

	// Selection operators through the menus.
	tools->action(QStringLiteral("meshModeFace"))->trigger();
	tools->applySelection({0, {}, {0}});
	tools->action(QStringLiteral("meshSelectLinked"))->trigger();
	expect(document.selection().faces.size() == 32, "Ctrl+L selects linked faces");
	tools->applySelection({0, {}, {0}});
	tools->action(QStringLiteral("meshSelectMore"))->trigger();
	expect(document.selection().faces.size() > 1, "Ctrl+= grows the selection");

	// The editor deletes itself on close.
	editor->close();
	app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
	if (failures == 0)
		std::cout << "mesh editor tools UI checks passed\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
