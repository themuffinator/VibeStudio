// Mesh Editor controls profiles in the real editor: each profile's layout,
// panes and sidebar names, its keys, 3ds Max's add and remove clicks, a
// MilkShape 3D tool drag in an orthographic pane, pane activation, hiding,
// saved overrides and the export menu for the skeletal and idTech 4 formats.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/model_design.h"
#include "core/studio_settings.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMouseEvent>
#include <QTemporaryDir>

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
// A flat 4x4 grid of quads at z = 0.
ModelMesh gridMesh()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	auto &surface = mesh.surfaces[0];
	constexpr int cells = 4;
	constexpr float size = 16;
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= cells; ++j)
	{
		for (int i = 0; i <= cells; ++i)
			positions.append({i * size, j * size, 0});
	}
	const auto at = [](int i, int j) { return j * (cells + 1) + i; };
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
	surface.frames = {frame};
	surface.uvSeams.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
void mouse(QWidget *widget, QEvent::Type type, const QPointF &point, Qt::MouseButton button, Qt::MouseButtons buttons,
		   Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QMouseEvent event(type, point, widget->mapToGlobal(point), button, buttons, modifiers);
	QCoreApplication::sendEvent(widget, &event);
}
void click(QWidget *widget, const QPointF &point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	mouse(widget, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, modifiers);
	mouse(widget, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, modifiers);
}
QString pageTitle(ModelEditorDialog &editor, const char *id)
{
	auto *page = editor.sidebarPage(QString::fromLatin1(id));
	return page ? page->title() : QString();
}
bool onSidebar(StudioSidebar *sidebar, const char *id)
{
	return sidebar && sidebar->pageIds().contains(QString::fromLatin1(id));
}
// Writes the editor to VIBESTUDIO_MODELLER_EVIDENCE when that is set.
bool capture(QWidget &widget, const QString &name)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty())
		return true;
	if (!QDir().mkpath(directory))
		return false;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(directory).filePath(name + ".png"));
}

// One pass over every profile. Variant 0 is the dark theme at 100%; variant 1
// is high-contrast light at 200% text, right to left.
bool run(QApplication &app, int variant, const QString &settingsPath)
{
	StudioSettings::setOverrideFilePath(settingsPath);
	applyStudioTheme(app, studioThemeTokens(variant == 0 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard,
										   variant == 0 ? 100 : 200));
	ModelEditorDialog *shown = nullptr;
	const auto shot = [&](QWidget &widget, const char *name)
	{
		if (qEnvironmentVariableIsEmpty("VIBESTUDIO_MODELLER_EVIDENCE"))
			return true;
		// Evidence shows finished renders in every pane.
		for (auto *pane : shown ? shown->panes() : QVector<ModelViewport *>{})
		{
			if (pane && pane->isVisible())
				tests::settleModelViewport(*pane);
		}
		return capture(widget, QStringLiteral("profiles-%1-%2").arg(variant).arg(QString::fromLatin1(name)));
	};
	QString error;
	auto *editor = new ModelEditorDialog;
	shown = editor;
	editor->resize(1600, 1000);
	if (variant == 1)
		editor->setLayoutDirection(Qt::RightToLeft);
	editor->show();
	app.processEvents();
	auto *tools = editor->tools();
	auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	if (!expect(tools && preview, "the editor has its interaction layer and viewport"))
		return false;
	expect(editor->setMesh(gridMesh(), &error), "load a grid");
	const auto &document = editor->document();
	// The studio's own profile: one view and the Blender-style keys.
	expect(editor->profileId() == QStringLiteral("studio") && editor->viewLayout() == ModelViewLayout::Single,
		   "a fresh editor uses the studio profile with one view");
	expect(onSidebar(editor->leadingSidebar(), "outliner") && onSidebar(editor->trailingSidebar(), "item") &&
			   pageTitle(*editor, "surface") == QStringLiteral("Surface"),
		   "the outliner leads and the property pages trail");
	expect(tools->action(QStringLiteral("meshHide"))->shortcut() == QKeySequence(Qt::Key_H) &&
			   editor->findChild<QAction *>(QStringLiteral("redoMesh"))->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Y)),
		   "H hides and Ctrl+Y also redoes");

	app.processEvents();
	expect(shot(*editor, "studio"), "capture the studio layout");
	// Blender: its own names and Blender's redo only.
	editor->applyProfile(QStringLiteral("blender"));
	expect(StudioSettings().modelEditorProfileId() == QStringLiteral("blender"), "choosing a profile saves it");
	expect(pageTitle(*editor, "surface") == QStringLiteral("Material") && pageTitle(*editor, "skeleton") == QStringLiteral("Armature") &&
			   pageTitle(*editor, "collision") == QStringLiteral("Physics"),
		   "Blender names the pages Material, Armature and Physics");
	const auto redo = editor->findChild<QAction *>(QStringLiteral("redoMesh"))->shortcuts();
	expect(redo.contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)) && !redo.contains(QKeySequence(Qt::CTRL | Qt::Key_Y)),
		   "Blender redoes with Ctrl+Shift+Z only");

	app.processEvents();
	expect(shot(*editor, "blender"), "capture the blender layout");
	// Hiding faces keeps them out of the selection until they are revealed.
	tools->applySelection({0, {}, {0, 1}});
	tools->action(QStringLiteral("meshHide"))->trigger();
	expect(tools->hasHidden() && tools->hiddenFaces().value(0) == QSet<int>({0, 1}) && document.selection().faces.isEmpty(),
		   "Hide Selected hides the faces and clears them from the selection");
	tools->action(QStringLiteral("meshSelectAll"))->trigger();
	expect(document.selection().faces.size() == 30 && !document.selection().faces.contains(0),
		   "Select All skips hidden faces");
	tools->action(QStringLiteral("meshReveal"))->trigger();
	expect(!tools->hasHidden(), "Reveal Hidden shows them again");

	// 3ds Max: four views, Max's names, tool keys and Ctrl/Alt clicks.
	editor->applyProfile(QStringLiteral("3ds-max"));
	app.processEvents();
	const auto panes = editor->panes();
	expect(editor->viewLayout() == ModelViewLayout::FourViews && panes.size() == 4 && !panes.contains(nullptr),
		   "3ds Max starts with four views");
	expect(editor->paneView(0) == ModelPaneView::Top && editor->paneView(1) == ModelPaneView::Front &&
			   editor->paneView(2) == ModelPaneView::Left && editor->paneView(3) == ModelPaneView::Perspective &&
			   editor->activePane() == 3 && panes.value(3) == preview,
		   "the panes are Top, Front, Left and Perspective, and the 3D pane takes input");
	expect(pageTitle(*editor, "item") == QStringLiteral("Modify") && pageTitle(*editor, "add") == QStringLiteral("Create") &&
			   onSidebar(editor->trailingSidebar(), "add") && !onSidebar(editor->leadingSidebar(), "add"),
		   "Create and Modify sit in the command panel");
	expect(tools->action(QStringLiteral("meshToolMove"))->shortcut() == QKeySequence(Qt::Key_W) &&
			   tools->action(QStringLiteral("meshModeVertex"))->shortcut() == QKeySequence(Qt::Key_1) &&
			   editor->controls().transform.style == ModelTransformStyle::ToolMode,
		   "W picks the Move tool and 1 picks vertices");

	app.processEvents();
	expect(shot(*editor, "max"), "capture the max layout");
	// Clicking the Top pane makes it the interactive one.
	click(panes[0], QPointF(panes[0]->width() / 2.0, panes[0]->height() / 2.0));
	app.processEvents();
	expect(editor->activePane() == 0 && editor->panes().value(0) == preview && !preview->isPerspective(),
		   "clicking a pane activates it with its own view");
	preview->frameModel();
	expect(tests::settleModelViewport(*preview), "the Top pane settles");
	tools->action(QStringLiteral("meshModeVertex"))->trigger();
	tools->applySelection({0, {}, {}});
	const int a = 6, b = 8;
	// Picks wait for the view to show the current state, as a user's would.
	const auto clickVertex = [&](int vertex, Qt::KeyboardModifiers modifiers)
	{
		tests::settleModelViewport(*preview);
		click(preview, preview->vertexScreenPosition(0, vertex), modifiers);
	};
	clickVertex(a, Qt::NoModifier);
	expect(document.selection().vertices == QSet<int>{a}, "a click selects one vertex");
	clickVertex(b, Qt::ControlModifier);
	expect(document.selection().vertices == QSet<int>({a, b}), "Ctrl+click adds, as in 3ds Max");
	clickVertex(a, Qt::AltModifier);
	expect(document.selection().vertices == QSet<int>{b}, "Alt+click removes, as in 3ds Max");

	// MilkShape 3D: its panes, tab names and a Move tool drag in the Top pane.
	editor->applyProfile(QStringLiteral("milkshape-3d"));
	app.processEvents();
	expect(editor->paneView(0) == ModelPaneView::Front && editor->paneView(1) == ModelPaneView::Top &&
			   editor->paneView(2) == ModelPaneView::Right && editor->paneView(3) == ModelPaneView::Perspective,
		   "MilkShape 3D shows Front, Top, Right and 3D");
	expect(pageTitle(*editor, "tool") == QStringLiteral("Model") && pageTitle(*editor, "outliner") == QStringLiteral("Groups") &&
			   pageTitle(*editor, "skeleton") == QStringLiteral("Joints") && editor->leadingSidebar()->pageIds().isEmpty(),
		   "MilkShape 3D's tabs all sit on one side");
	expect(editor->findChild<QAction *>(QStringLiteral("redoMesh"))->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_R)) &&
			   tools->action(QStringLiteral("meshToolMove"))->shortcut() == QKeySequence(Qt::Key_F2),
		   "Ctrl+R redoes and F2 picks the Move tool");
	app.processEvents();
	expect(shot(*editor, "milkshape"), "capture the milkshape layout");
	editor->activatePane(1);
	app.processEvents();
	preview->frameModel();
	expect(editor->activePane() == 1 && tests::settleModelViewport(*preview), "the Top pane takes input");
	tools->action(QStringLiteral("meshToolMove"))->trigger();
	expect(tools->lastingTool() == ModelEditorTools::LastingTool::Move, "the Move tool stays on");
	const auto before = document.revisionFingerprint();
	const float startX = document.mesh().surfaces[0].frames[0].positions[b].x;
	tests::settleModelViewport(*preview);
	const QPointF from = preview->vertexScreenPosition(0, b);
	const QPointF to = from + QPointF(60, 0);
	mouse(preview, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
	for (int step = 1; step <= 6; ++step)
		mouse(preview, QEvent::MouseMove, from + (to - from) * (step / 6.0), Qt::NoButton, Qt::LeftButton);
	mouse(preview, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
	app.processEvents();
	const float movedX = document.mesh().surfaces[0].frames[0].positions[b].x;
	expect(document.revisionFingerprint() != before && std::abs(movedX - startX) > 1.0f,
		   "dragging with the Move tool moves the selected vertex");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(document.revisionFingerprint() == before, "the tool drag is one undo step");

	// Saved overrides apply on top of the profile.
	QJsonObject overrides{{QStringLiteral("keys"), QJsonObject{{QStringLiteral("meshHide"), QJsonArray{QStringLiteral("Ctrl+J")}}}}};
	expect(StudioSettings().setModelEditorControlOverrides(QStringLiteral("blender"), overrides, &error), "save a key override");
	editor->applyProfile(QStringLiteral("blender"), false);
	expect(tools->action(QStringLiteral("meshHide"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_J) &&
			   editor->viewLayout() == ModelViewLayout::Single,
		   "the override replaces Blender's H and Blender returns to one view");

	// The profile chooser explains each profile, gaps included.
	auto *chooser = editor->findChild<QComboBox *>(QStringLiteral("meshControlsProfile"));
	const int maxIndex = chooser ? chooser->findData(QStringLiteral("3ds-max")) : -1;
	expect(maxIndex >= 0 && chooser->itemData(maxIndex, Qt::ToolTipRole).toString().contains(QStringLiteral("Quickslice")),
		   "the 3ds Max tooltip says what it leaves out");

	// The skeletal and idTech 4 exports share one menu.
	auto *other = editor->findChild<QAction *>(QStringLiteral("exportMeshOtherFormat"));
	QStringList names;
	if (other && other->menu())
	{
		for (auto *entry : other->menu()->actions())
			names << entry->objectName();
	}
	expect(names == QStringList({QStringLiteral("exportMeshMd5"), QStringLiteral("exportMeshMd5Anim"), QStringLiteral("exportMeshIqm"),
								 QStringLiteral("exportMeshAse")}),
		   "Export Other Format offers MD5 mesh, MD5 animation, IQM and ASE");

	// Closing can ask whether to save the edited grid, a modal question that
	// waits forever offscreen; destroying the editor skips it.
	delete editor;
	return true;
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
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-profiles-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("mesh-profiles-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	for (int variant = 0; variant < 2; ++variant)
	{
		if (!run(app, variant, QDir(temporary.path()).filePath(QStringLiteral("settings-%1.ini").arg(variant))))
			return EXIT_FAILURE;
	}
	if (failures)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "Model profiles UI smoke passed.\n";
	return EXIT_SUCCESS;
}
