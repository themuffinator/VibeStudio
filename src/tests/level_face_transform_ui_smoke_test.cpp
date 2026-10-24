#include "app/application_shell.h"
#include "app/level_component_gizmo.h"
#include "app/level_component_transform_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_textured_selection.h"
#include "tests/level_type_selection_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

// Faces picked by Select Textured move, rotate and scale in Face mode without
// selecting their brushes: numeric dialog, arrow keys and the manipulator.
using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << error.toStdString() << std::endl; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); } return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override {
		const auto text = QString::fromUtf8(source); return QStringLiteral("[%1%2]").arg(text,QString(text.size()/3,'~'));
	}
};
struct Plane { LevelMapVec3 normal; double distance = 0; };
Plane plane(const LevelMapDocument& map, const LevelSurfaceFace& face)
{
	const auto brush = std::find_if(map.brushes.cbegin(),map.brushes.cend(),[&](const auto& b) { return b.id == face.brushId; });
	LevelBrushTopology topology; Plane result;
	if (brush == map.brushes.cend() || !levelBrushTopology(*brush,&topology) || face.faceIndex < 0 || face.faceIndex >= topology.faces.size()) { return result; }
	const auto& ids = topology.faces[face.faceIndex]; double nx = 0, ny = 0, nz = 0, d = 0;
	for (qsizetype i = 0; i < ids.size(); ++i) {
		const auto& a = topology.vertices[ids[i]]; const auto& b = topology.vertices[ids[(i+1)%ids.size()]];
		nx += (a.y-b.y)*(a.z+b.z); ny += (a.z-b.z)*(a.x+b.x); nz += (a.x-b.x)*(a.y+b.y);
	}
	const double length = std::sqrt(nx*nx+ny*ny+nz*nz); result.normal = {nx/length,ny/length,nz/length,true};
	for (int id : ids) { const auto& p = topology.vertices[id]; d += p.x*result.normal.x+p.y*result.normal.y+p.z*result.normal.z; }
	result.distance = d/static_cast<double>(ids.size()); return result;
}
QPointF label(const LevelComponentGizmo& gizmo, const LevelComponentGizmoFrame& frame, int id)
{
	for (const auto& handle : gizmo.handles(frame)) { if (handle.id == id) { return handle.label; } }
	expect(false,"requested manipulator handle exists",QString::number(id)); return {};
}
}
int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets, semantic events and render targets only.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	const auto source = tests::typeSelectionFixture(), path = temp.filePath("faces.map").toUtf8(); QFile file(QString::fromUtf8(path));
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"fixture")) { return 1; } file.close();
	for (const auto* profile : {"netradiant-custom","trenchbroom","vibestudio-default"}) {
		for (int scale : {100,200}) {
			std::cout << "Face transform UI " << profile << ' ' << scale << '%' << std::endl;
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(QString::fromUtf8(path));
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				const auto trigger = [&](const char* id) { auto* found = action(id); if (expect(found && found->isEnabled(),id)) { found->trigger(); } };
				action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
				auto* view = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				auto* material = shell.findChild<QComboBox*>("levelPaintMaterial");
				if (!expect(view && camera && material && until([&] { return shell.levelDocument().sourcePath == QString::fromUtf8(path) && camera->hasMesh(); }),"map ready")) { return 1; }
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				view->setSelectionSet({}); view->selectionSetChanged({});
				material->setEditText("metal"); trigger("map.selectTextured");
				const QVector<LevelSurfaceFace> seed{{0,0}};
				expect(view->texturedSurfaceFaces() == seed && shell.levelDocument().selection.isEmpty(),"independent face without its parent brush");
				// Vertex mode leaves independent faces out of the component scope.
				trigger("map.vertexTool"); trigger("map.componentScaleTool");
				expect(view->componentGizmo()->handles(view->componentGizmoFrame()).isEmpty() && !action("map.transformComponents")->isEnabled(),
					"faces join component transforms only in face mode");
				trigger("map.faceTool");
				expect(view->componentMode() == MapViewport::ComponentMode::Faces && view->pickedComponentCount() == 0
					&& view->texturedSurfaceFaces() == seed && !shell.statusBar()->currentMessage().isEmpty(),"face mode keeps the pick without handles",shell.statusBar()->currentMessage());
				trigger("map.componentScaleTool");
				expect(!view->componentGizmo()->handles(view->componentGizmoFrame()).isEmpty() && action("map.transformComponents")->isEnabled(),
					"manipulator and numeric dialog are offered for independent faces");
				const auto baseline = bytes(); const auto before = plane(shell.levelDocument(),seed[0]);
				const auto history = shell.levelDocument().undoStack.size();

				// Numeric dialog: move the picked face 16 units along its normal.
				bool seen = false, closed = false, scoped = false, visible = false; QString problem;
				QTimer drive; drive.setInterval(20);
				QObject::connect(&drive,&QTimer::timeout,&shell,[&] {
					auto* dialog = qobject_cast<LevelComponentTransformDialog*>(QApplication::activeModalWidget()); if (!dialog) { return; }
					drive.stop(); seen = true;
					auto* scope = dialog->findChild<QLabel*>("componentTransformScope"); auto* apply = dialog->findChild<QPushButton*>("componentTransformApply");
					auto* accessible = scope ? QAccessible::queryAccessibleInterface(scope) : nullptr;
					scoped = scope && scope->isVisible() && !scope->text().isEmpty() && accessible && !accessible->text(QAccessible::Name).isEmpty();
					visible = apply && apply->isVisible() && dialog->rect().contains(apply->mapTo(dialog,apply->rect().center()));
					dialog->findChild<QComboBox*>("componentOperation")->setCurrentIndex(0);
					dialog->findChild<QDoubleSpinBox*>("componentDeltaX")->setValue(before.normal.x*16);
					dialog->findChild<QDoubleSpinBox*>("componentDeltaY")->setValue(before.normal.y*16);
					dialog->findChild<QDoubleSpinBox*>("componentDeltaZ")->setValue(before.normal.z*16);
					apply->click(); closed = !dialog->isVisible();
					if (!closed) { problem = dialog->findChild<QLabel*>("componentTransformError")->text(); dialog->reject(); }
				});
				QTimer watchdog; watchdog.setSingleShot(true);
				QObject::connect(&watchdog,&QTimer::timeout,&shell,[] { if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modal->reject(); } });
				drive.start(); watchdog.start(20000); trigger("map.transformComponents"); drive.stop(); watchdog.stop();
				expect(seen && closed && scoped && visible,"accessible numeric dialog names its face scope and applies",problem);
				const auto changed = bytes();
				expect(changed != baseline && shell.levelDocument().undoStack.size() == history+1 && shell.levelDocument().selection.isEmpty()
					&& view->texturedSurfaceFaces().size() == 1,"dialog edit is one undo step and keeps the pick",shell.statusBar()->currentMessage());
				const auto moved = plane(shell.levelDocument(),view->texturedSurfaceFaces().value(0));
				expect(std::abs(moved.distance-before.distance-16) < 1e-3,"the pick follows the moved face");
				trigger("map.undo");
				expect(bytes() == baseline && view->texturedSurfaceFaces() == seed,"undo restores the face and its pick");
				trigger("map.redo");
				expect(bytes() == changed && view->texturedSurfaceFaces().size() == 1,"redo restores the moved face and its pick");
				trigger("map.undo"); expect(bytes() == baseline,"back to the original map");

				// Arrow keys nudge independent faces when no handles are picked.
				view->setFocus(Qt::OtherFocusReason);
				QKeyEvent right(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier); QApplication::sendEvent(view,&right);
				expect(until([&] { return bytes() != baseline; }) && shell.levelDocument().undoStack.size() == history+1 && view->texturedSurfaceFaces().size() == 1
					&& shell.levelDocument().selection.isEmpty(),"arrow key nudges the picked face",shell.statusBar()->currentMessage());
				trigger("map.undo"); expect(bytes() == baseline && view->texturedSurfaceFaces() == seed,"nudge undo");

				// Manipulator: rotate the picked face 15 degrees about Z.
				trigger("map.componentRotateTool");
				auto* gizmo = view->componentGizmo(); const auto frame = view->componentGizmoFrame();
				const auto press = label(*gizmo,frame,2); const auto centre = frame.project(gizmo->pivot()); const auto offset = press-centre;
				const double angle = 15*std::acos(-1.0)/180;
				const QPointF end = centre+QPointF(offset.x()*std::cos(angle)-offset.y()*std::sin(angle),offset.x()*std::sin(angle)+offset.y()*std::cos(angle));
				if (expect(view->beginComponentTransform(press) && view->updateComponentTransform(end),"manipulator rotation draft")) {
					auto request = gizmo->request(); auto expected = shell.levelDocument(); LevelSurfaceFaceTransformReport report; QString error;
					request.textureLock = settings.levelComponentUvLock(); request.allowValve220 = settings.levelAllowValve220();
					LevelSurfaceFacePicks picks; picks.faces = seed;
					expect(transformLevelMapSurfaceFaces(&expected,picks,{},request,&report,&error),"shared service oracle",error);
					view->finishComponentTransform(true); expect(until([&] { return !gizmo->active(); }),"manipulator release completes");
					expect(bytes() == serializeLevelMap(expected).bytes && shell.levelDocument().undoStack.size() == history+1
						&& view->texturedSurfaceFaces() == report.faces && shell.levelDocument().selection.isEmpty(),"manipulator equals the shared service in one undo step",shell.statusBar()->currentMessage());
					trigger("map.undo"); expect(bytes() == baseline && view->texturedSurfaceFaces() == seed,"manipulator undo");
				}
				QKeyEvent clear(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(view,&clear);
				expect(view->texturedSurfaceFaces().isEmpty() && !action("map.transformComponents")->isEnabled(),"Escape drops the face picks and their transform scope");
				expect(bytes() == baseline,"workflow leaves the map at its original bytes");
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	return ok ? 0 : 1;
}
