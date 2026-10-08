#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/model_recovery.h"
#include "core/model_tags.h"
#include "core/studio_settings.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>
#include <cmath>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
bool near(ModelVec3 a, ModelVec3 b) { return std::abs(a.x - b.x) < 0.0001 && std::abs(a.y - b.y) < 0.0001 && std::abs(a.z - b.z) < 0.0001; }
ModelMesh fixture()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	mesh.surfaces[0].frames << mesh.surfaces[0].frames[0];
	for (auto &point : mesh.surfaces[0].frames[1].positions)
	{
		point.z += 8;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor")
		{
			return {};
		}
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
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
	// Public value APIs and signals only. Images come from QWidget::render;
	// this test never injects user input or captures an OS window or desktop.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-tags-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	const auto original = fixture();
	const auto geometry = editableModelJson(original).value("surfaces");
	{
		auto mesh = original;
		for (auto &triangle : mesh.surfaces[0].triangles)
		{
			std::swap(triangle.b, triangle.c);
		}
		ModelEdit add;
		add.kind = ModelEditKind::AddTag;
		add.text = "tag_probe";
		ok &= expect(applyModelEdit(&mesh, add, nullptr, &error), "prepare authored tag on a native-normal winding fixture");
		ModelViewport viewport;
		viewport.resize(600, 600);
		viewport.setMesh(mesh);
		viewport.setOrbit(-90, 90);
		viewport.setBackfaceCulling(true);
		viewport.setEditSelection(0, {});
		viewport.setTagPicking(true);
		viewport.setEditTag("tag_probe");
		viewport.setMoveGizmo(true);
		viewport.show();
		app.processEvents();
		ok &= expect(tests::settleModelViewport(viewport), "native-normal fixture settles before tag movement");
		const auto sample =
			(viewport.vertexScreenPosition(0, 0) + viewport.vertexScreenPosition(0, 1) + viewport.vertexScreenPosition(0, 2)) / 3;
		ok &= expect(viewport.hitAt(sample).valid, "imported normals preserve facing despite opposite stored winding");
		const auto handles = viewport.transformGizmoPoints();
		ok &=
			expect(viewport.beginEditTransform(handles[0]) && viewport.updateEditTransform(handles[0] + (handles[0] - handles[3]) * 0.6) &&
					   tests::settleModelViewport(viewport) && viewport.hitAt(sample).valid,
				   "tag preview preserves imported mesh normals and visible-face culling");
		viewport.finishEditTransform(false);
	}
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		const int scale = scenario == 0 ? 100 : 200;
		Expansion expansion;
		if (scenario > 0)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scale));
		auto *editor = new ModelEditorDialog;
		ok &= expect(editor->setMesh(original, &error), "prepare animated model for attachment authoring");
		if (scenario > 0)
		{
			editor->resize(1900, 1200);
		}
		if (scenario == 1)
		{
			editor->setLayoutDirection(Qt::RightToLeft);
		}
		editor->setAccessibility(scenario > 0, true);
		editor->show();
		app.processEvents();
		auto *preview = editor->findChild<ModelViewport *>("meshPreview");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == QStringLiteral("2"))
		{
			ok &= expect(std::abs(preview->devicePixelRatioF() - 2) < 0.01, "tag viewport uses the requested 2x display pixel ratio");
		}
		auto *mode = editor->findChild<QComboBox *>("meshSelectionMode");
		auto *table = editor->findChild<QTableView *>("meshComponents");
		auto *inspector = editor->findChild<QTabWidget *>("meshInspector");
		auto *name = editor->findChild<QLineEdit *>("meshTagName");
		auto *scope = editor->findChild<QComboBox *>("meshTagFrameScope");
		auto *frame = editor->findChild<QComboBox *>("meshFrame");
		auto *tool = editor->findChild<QComboBox *>("meshTransformTool");
		auto *originX = editor->findChild<QDoubleSpinBox *>("meshTagOrigin0");
		auto *undo = editor->findChild<QAction *>("undoMesh");
		auto *redo = editor->findChild<QAction *>("redoMesh");
		const auto press = [&](const char *object) { editor->findChild<QPushButton *>(QLatin1String(object))->click(); };
		const auto tag = [&](int pose) { return *findModelTag(editor->document().mesh(), "tag_weapon", pose); };
		ok &= expect(preview && mode && table && inspector && name && scope && frame && tool && originX && undo && redo,
					 "tag authoring exposes the connected controls");
		editor->showSidebarPage(QStringLiteral("animation"));
		table->selectAll();
		press("meshTagUseSelectionCentre");
		ok &= expect(originX->value() == 0, "new tag origin can use selected geometry bounds");
		name->setText("tag_weapon");
		originX->setValue(1);
		editor->findChild<QDoubleSpinBox *>("meshTagOrigin1")->setValue(2);
		editor->findChild<QDoubleSpinBox *>("meshTagOrigin2")->setValue(3);
		press("addMeshTag");
		ok &= expect(editor->document().mesh().tags.size() == 2 && editor->document().selection().tag == "tag_weapon" &&
						 mode->currentIndex() == 3 && table->model()->rowCount() == 1 &&
						 table->selectionModel()->selectedRows().size() == 1 && near(tag(0).origin, {1, 2, 3}) &&
						 near(tag(1).origin, {1, 2, 3}),
					 "creation populates every pose, selects the tag and switches the component table");
		ok &= expect(!name->accessibleName().isEmpty() && !scope->accessibleName().isEmpty() && !originX->accessibleName().isEmpty() &&
						 name->focusPolicy() != Qt::NoFocus && originX->focusPolicy() != Qt::NoFocus &&
						 originX->layoutDirection() == Qt::LeftToRight && !editor->findChild<QDoubleSpinBox *>("meshScale0")->isEnabled() &&
						 !qobject_cast<QStandardItemModel *>(tool->model())->item(2)->isEnabled(),
					 "tag controls have focus and accessibility metadata; numeric direction and rigid transform limits are explicit");
		undo->trigger();
		ok &= expect(mode->currentIndex() == 0 && editor->document().selection().faces.size() == 2 &&
						 editor->document().mesh().tags.isEmpty(),
					 "undo returns to the prior component selection");
		redo->trigger();
		frame->setCurrentIndex(1);
		scope->setCurrentIndex(1);
		originX->setValue(11);
		press("setMeshTagOrigin");
		ok &= expect(editor->findChild<QComboBox *>("meshFrameScope")->currentIndex() == 1 && near(tag(0).origin, {1, 2, 3}) &&
						 near(tag(1).origin, {11, 2, 3}),
					 "absolute pose editing shares current-frame scope with Geometry");
		editor->findChild<QComboBox *>("meshViewPreset")->setCurrentIndex(1);
		editor->findChild<QCheckBox *>("meshSnapTranslation")->setChecked(true);
		editor->findChild<QDoubleSpinBox *>("meshTranslationGrid")->setValue(2);
		ok &= expect(tests::settleModelViewport(*preview), "tag viewport settles");
		const auto at = preview->tagScreenPosition("tag_weapon");
		ok &= expect(preview->tagAt(at) == "tag_weapon" && preview->accessibleSummary().contains("tag_weapon"),
					 "tag origin is independently pickable and described");
		preview->tagPicked("tag_weapon", int(ModelViewportPick::Toggle));
		ok &= expect(editor->document().selection().tag.isEmpty() && table->selectionModel()->selectedRows().isEmpty(),
					 "toggle clears the named attachment selection");
		table->selectionModel()->select(table->model()->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		ok &= expect(editor->document().selection().tag == "tag_weapon", "component row selection restores the named attachment");
		ok &= expect(tests::settleModelViewport(*preview), "tag reselection settles");
		const auto beforeMove = editableModelJson(editor->document().mesh());
		const auto vertex = preview->vertexScreenPosition(0, 0);
		auto handles = preview->transformGizmoPoints();
		ok &= expect(preview->beginEditTransform(handles[0]) && preview->updateEditTransform(handles[0] + (handles[0] - handles[3]) * 0.65),
					 "begin snapped attachment movement");
		const auto movement = preview->editTransform().translation;
		ModelTag shown;
		ok &= expect(preview->tagPose("tag_weapon", &shown) && movement.x != 0 && std::fmod(movement.x, 2) == 0 &&
						 near(shown.origin, {11 + movement.x, 2, 3}) && editableModelJson(editor->document().mesh()) == beforeMove &&
						 (preview->vertexScreenPosition(0, 0) - vertex).manhattanLength() < 0.001,
					 "attachment preview does not transform mesh vertices or history");
		preview->finishEditTransform(false);
		ok &= expect(editableModelJson(editor->document().mesh()) == beforeMove && tests::settleModelViewport(*preview),
					 "cancelling the tag preview retains the document");
		handles = preview->transformGizmoPoints();
		ok &= expect(preview->beginEditTransform(handles[0]) && preview->updateEditTransform(handles[0] + (handles[0] - handles[3]) * 0.65),
					 "repeat tag movement for commit");
		const auto delta = preview->editTransform().translation;
		preview->finishEditTransform(true);
		ok &= expect(near(tag(0).origin, {1, 2, 3}) && near(tag(1).origin, {11 + delta.x, 2, 3}) &&
						 editableModelJson(editor->document().mesh()).value("surfaces") == geometry,
					 "committed tag movement affects only its chosen pose and preserves every mesh surface");
		undo->trigger();
		ok &= expect(editableModelJson(editor->document().mesh()) == beforeMove && editor->document().selection().tag == "tag_weapon",
					 "one undo restores the full tag pose and selection");
		tool->setCurrentIndex(1);
		ok &= expect(tests::settleModelViewport(*preview), "rotation handles settle at the tag origin");
		const auto rings = preview->rotationGizmoRings();
		ok &= expect(preview->beginEditTransform(rings[2][12]) && preview->updateEditTransform(rings[2][36]),
					 "quarter-turn tag rotation previews");
		preview->finishEditTransform(true);
		ok &= expect(near(modelTagPoint(tag(1), {1, 0, 0}), {11, 3, 3}) && near(modelTagPoint(tag(0), {1, 0, 0}), {2, 2, 3}) &&
						 editableModelJson(editor->document().mesh()).value("surfaces") == geometry,
					 "tag rotation preserves origin and rotates the MD3 local basis only in the selected pose");
		press("resetMeshTagOrientation");
		ok &= expect(near(modelTagPoint(tag(1), {1, 0, 0}), {12, 2, 3}), "explicit orientation reset aligns local axes");
		undo->trigger();
		undo->trigger();
		scope->setCurrentIndex(0);
		editor->findChild<QDoubleSpinBox *>("meshRotation2")->setValue(90);
		press("transformMesh");
		ok &= expect(near(tag(0).origin, {11, -8, 3}) && near(tag(1).origin, {11, 2, 3}),
					 "numeric all-frame tag rotation uses one displayed-pose pivot");
		undo->trigger();
		editor->findChild<QDoubleSpinBox *>("meshRotation2")->setValue(0);
		scope->setCurrentIndex(1);
		editor->findChild<QSpinBox *>("meshTagCopyFrame")->setValue(0);
		press("copyMeshTagPose");
		ok &= expect(near(tag(1).origin, tag(0).origin), "copy pose uses its explicit source and shared destination scope");
		undo->trigger();
		name->setText("tag_head");
		press("duplicateMeshTag");
		ok &= expect(editor->document().mesh().tagCount == 2 && findModelTag(editor->document().mesh(), "tag_head", 1) &&
						 table->model()->rowCount() == 2,
					 "duplication retains all poses with a new identity");
		name->setText("tag_muzzle");
		press("renameMeshTag");
		ok &= expect(editor->document().selection().tag == "tag_muzzle" && !findModelTag(editor->document().mesh(), "tag_head", 0),
					 "rename follows the selected identity across every pose");
		press("deleteMeshTag");
		ok &= expect(editor->document().mesh().tagCount == 1 && editor->document().selection().tag.isEmpty(),
					 "deleting an attachment clears its selection");
		undo->trigger();
		ok &= expect(editor->document().selection().tag == "tag_muzzle", "undo restores deleted attachment selection");
		editor->checkpointRecovery();
		QElapsedTimer waiting;
		waiting.start();
		while (editor->recoveryBusy() && waiting.elapsed() < 20000)
		{
			app.processEvents();
			QThread::msleep(1);
		}
		ModelRecoverySnapshot recovered;
		ok &= expect(!editor->recoveryBusy() && inspectModelRecovery(editor->recoveryPath(), &recovered).isValid() &&
						 recovered.selection.tag == "tag_muzzle" &&
						 editableModelJson(recovered.mesh) == editableModelJson(editor->document().mesh()),
					 "GUI recovery records the selected tag and full editable document");
		QByteArray staged;
		bool placed = false;
		editor->context = []
		{
			ModelDesignContext context;
			context.packagePath = "fixture.pk3";
			context.mapPath = "fixture.map";
			context.canStage = true;
			context.canPlace = true;
			return context;
		};
		editor->handoff = [&](const QByteArray &bytes, const QString &path, bool place, const LevelMapVec3 &, bool, QString *)
		{
			staged = bytes;
			placed = place;
			return path.endsWith(".md3");
		};
		editor->refreshContext();
		editor->findChild<QAction *>("placeMesh")->trigger();
		ModelMesh native;
		ok &= expect(placed && importEditableModel("staged.md3", staged, &native, &error) && native.tags.size() == 4 &&
						 findModelTag(native, "tag_muzzle", 1),
					 "MD3 package and level handoff retains authored attachments");
		tool->setCurrentIndex(0);
		editor->findChild<QComboBox *>("meshViewPreset")->setCurrentIndex(0);
		ok &= expect(tests::settleModelViewport(*preview), "attachment evidence view settles");
		auto *scroll = tests::pageScroll(inspector->currentWidget());
		scroll->ensureWidgetVisible(name, 0, 60);
		app.processEvents();
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty())
		{
			QImage image(editor->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			editor->render(&image);
			ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("mesh-tags-%1.png").arg(scenario))),
						 "save attachment authoring visual evidence");
			scroll->ensureWidgetVisible(editor->findChild<QPushButton *>("copyMeshTagPose"), 0, 20);
			app.processEvents();
			editor->render(&image);
			ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("mesh-tag-pose-%1.png").arg(scenario))),
						 "save attachment pose control evidence");
		}
		editor->setAccessibility(scenario > 0, false);
		preview->play();
		ok &= expect(preview->isPlaying() && !editor->findChild<QWidget *>("meshTagControls")->isEnabled(),
					 "playback disables tag pose editing");
		preview->pause();
		ok &= expect(editor->findChild<QWidget *>("meshTagControls")->isEnabled(), "pause restores tag authoring controls");
		ok &= expect(editor->setMesh(original, &error), "retire attachment recovery and history after test");
		editor->close();
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		if (scenario > 0)
		{
			app.removeTranslator(&expansion);
		}
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
