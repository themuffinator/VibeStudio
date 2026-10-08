#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/model_design.h"
#include "core/model_recovery.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << " " << error.toStdString() << '\n';
	}
	return value;
}
bool wait(const std::function<bool()> &done)
{
	QEventLoop loop;
	QTimer poll, deadline;
	deadline.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop,
					 [&]
					 {
						 if (done())
						 {
							 loop.quit();
						 }
					 });
	QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5);
	deadline.start(15000);
	if (!done())
	{
		loop.exec();
	}
	return done();
}
bool capture(QWidget &widget, const QString &name)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty())
	{
		return true;
	}
	if (!QDir().mkpath(directory))
	{
		return false;
	}
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(directory).filePath(name + ".png"));
}
class Expansion final : public QTranslator
{
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelCollisionEditor" && QByteArray(context) != "VibeStudioModelEditor" &&
			!QByteArray(context).endsWith("ModelViewport"))
		{
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return '[' + text + QString(text.size() / 3, '~') + ']';
	}
};
ModelMesh fixture()
{
	ModelDesign design;
	ModelDesignPart part;
	part.name = "prop";
	part.primitive = "box";
	design.parts << part;
	return buildModelDesignMesh(design);
}
bool manipulate(ModelEditorDialog &editor, int variant)
{
	auto *preview = editor.findChild<ModelViewport *>("meshPreview");
	auto *table = editor.findChild<QTableView *>("meshComponents");
	auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
	auto *tool = editor.findChild<QComboBox *>("meshTransformTool");
	auto *show = editor.findChild<QCheckBox *>("meshShowCollision");
	bool ok = expect(mode->currentIndex() == 4 && !show->isEnabled() && show->isChecked() && table->model()->rowCount() == 1 &&
						 table->selectionModel()->selectedRows().size() == 1,
					 "collision selection synchronizes table, inspector and required visibility");
	editor.findChild<QComboBox *>("meshViewPreset")->setCurrentIndex(1);
	editor.findChild<QCheckBox *>("meshMoveGizmo")->setChecked(true);
	editor.findChild<QComboBox *>("meshTransformPivotMode")->setCurrentIndex(1);
	editor.findChild<QCheckBox *>("meshSnapTranslation")->setChecked(true);
	editor.findChild<QDoubleSpinBox *>("meshScaleGrid")->setValue(.25);
	editor.findChild<QDoubleSpinBox *>("meshRotationGrid")->setValue(15);
	if (!tests::settleModelViewport(*preview))
	{
		return false;
	}
	const auto edges = preview->collisionScreenEdges("body");
	if (edges.isEmpty())
	{
		return false;
	}
	const auto edge = edges.front().center();
	ok &= expect(preview->collisionAt(edge) == "body" && preview->collisionAt({-10, -10}).isEmpty() &&
					 preview->collisionAt(edge, std::numeric_limits<double>::quiet_NaN()).isEmpty(),
				 "edge picking uses completed visible collision overlay");
	preview->collisionPicked("body", int(ModelViewportPick::Toggle));
	ok &= expect(editor.document().selection().collision.isEmpty() && table->selectionModel()->selectedRows().isEmpty(),
				 "toggle clears box selection");
	table->selectionModel()->setCurrentIndex(table->model()->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
	ok &= expect(editor.document().selection().collision == "body", "keyboard-accessible row selection reaches collision inspector");
	const auto nameRect = table->visualRect(table->model()->index(0, 0));
	ok &= expect(nameRect.left() >= 0 && nameRect.right() < table->viewport()->width(),
				 "collision names stay visible in the component pane at expanded RTL scale");
	preview->setOrbit(30, 20);
	ok &= expect(preview->collisionAt(edge).isEmpty(), "camera change cannot pick against an obsolete collision snapshot");
	preview->setOrbit(-90, 90);
	const auto before = editor.document().revisionFingerprint();
	const auto original = editor.document().mesh().collisionBoxes[0];
	for (int kind = 0; kind < 3; ++kind)
	{
		tool->setCurrentIndex(kind);
		if (!tests::settleModelViewport(*preview))
		{
			return false;
		}
		const auto handles = preview->transformGizmoPoints();
		QPointF start = handles[0], end = handles[0] + (handles[0] - handles[3]) * .65;
		if (kind == 1)
		{
			const auto rings = preview->rotationGizmoRings();
			if (rings[2].size() != 97)
			{
				return false;
			}
			start = rings[2][12];
			end = rings[2][36];
		}
		if (!expect(preview->beginEditTransform(start) && preview->updateEditTransform(end), "collision gesture starts and updates"))
		{
			return false;
		}
		ModelCollisionBox pose;
		ok &= expect(preview->collisionPose("body", &pose) && editor.document().revisionFingerprint() == before &&
						 preview->collisionAt(edge).isEmpty(),
					 "preview is isolated from history and picking");
		if (kind == 2)
		{
			ok &= expect(std::abs(pose.size.x - original.size.x * 1.75) < .001 && pose.size.y == original.size.y && pose.rotation.z == 35 &&
							 std::abs(handles[0].y() - handles[3].y()) > 10,
						 "rotated local X handle resizes only box X with snapped scale");
		}
		if (kind == 1)
		{
			ok &= expect(std::abs(pose.rotation.z - 125) < .001, "world Z rotation composes with authored rotation");
		}
		ok &= expect(tests::settleModelViewport(*preview) &&
						 capture(*preview, QStringLiteral("collision-gizmo-%1-%2").arg(variant).arg(kind)),
					 "capture collision transform preview");
		const auto shown = preview->collisionScreenEdges("body");
		preview->finishEditTransform(true);
		ok &= expect(tests::settleModelViewport(*preview) && preview->collisionScreenEdges("body") == shown &&
						 editor.document().revisionFingerprint() != before && editor.document().selection().collision == "body",
					 "committed collision matches preview and preserves selection");
		editor.findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(editor.document().revisionFingerprint() == before, "gesture creates one undo step");
	}
	if (!tests::settleModelViewport(*preview))
	{
		return false;
	}
	auto handles = preview->transformGizmoPoints();
	ok &= expect(preview->beginEditTransform(handles[3]) && preview->updateEditTransform(handles[3] + QPointF(96, 0)),
				 "uniform collision scale preview");
	preview->finishEditTransform(false);
	ok &= expect(editor.document().revisionFingerprint() == before, "cancel keeps collision source untouched");
	if (!tests::settleModelViewport(*preview))
	{
		return false;
	}
	handles = preview->transformGizmoPoints();
	ok &= expect(preview->beginEditTransform(handles[3]) && !preview->updateEditTransform(handles[3] + QPointF(-960, 0)) &&
					 !preview->editTransformValid(),
				 "sub-unit collision dimensions reject the preview");
	preview->finishEditTransform(true);
	ok &= expect(editor.document().revisionFingerprint() == before, "invalid release cannot commit last valid preview");
	editor.findChild<QComboBox *>("meshFrameScope")->setCurrentIndex(1);
	editor.findChild<QDoubleSpinBox *>("meshScale0")->setValue(1.37);
	editor.findChild<QPushButton *>("transformMesh")->click();
	ok &= expect(editor.document().mesh().collisionBoxes[0].size.x == original.size.x * 1.25f,
				 "numeric geometry transform uses local snap even with current-frame scope");
	editor.findChild<QAction *>("undoMesh")->trigger();
	editor.findChild<QDoubleSpinBox *>("meshScale0")->setValue(1);
	editor.findChild<QComboBox *>("meshFrameScope")->setCurrentIndex(0);
	if (!tests::settleModelViewport(*preview))
	{
		return false;
	}
	handles = preview->transformGizmoPoints();
	ok &= expect(preview->beginEditTransform(handles[3]) && preview->updateEditTransform(handles[3] + QPointF(96, 0)),
				 "selection change fixture");
	mode->setCurrentIndex(0);
	ok &= expect(!preview->editingMove() && editor.document().revisionFingerprint() == before &&
					 editor.document().selection().collision.isEmpty(),
				 "changing component mode cancels the preview and releases collision selection");
	mode->setCurrentIndex(4);
	table->selectionModel()->setCurrentIndex(table->model()->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("collision-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	StudioSettings().setModelRecoveryEnabled(true);
	bool ok = true;
	QString error;
	for (int variant = 0; variant < 3; ++variant)
	{
		Expansion expansion;
		if (variant == 2)
		{
			app.installTranslator(&expansion);
		}
		const auto theme = variant == 0 ? StudioTheme::Dark : variant == 1 ? StudioTheme::HighContrastDark : StudioTheme::HighContrastLight;
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, variant == 2 ? 200 : 100));
		ModelEditorDialog editor;
		editor.resize(1700, 1100);
		editor.setAccessibility(variant != 0, true);
		if (variant == 2)
		{
			editor.setLayoutDirection(Qt::RightToLeft);
		}
		ok &= expect(editor.setMesh(fixture(), &error), "load authoring fixture", error);
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		auto *boxes = editor.findChild<QComboBox *>("meshCollisionBoxes");
		auto *name = editor.findChild<QLineEdit *>("meshCollisionName");
		auto *add = editor.findChild<QPushButton *>("addMeshCollision");
		auto *fit = editor.findChild<QPushButton *>("fitMeshCollision");
		auto *target = editor.findChild<QComboBox *>("meshCollisionTarget");
		auto *material = editor.findChild<QLineEdit *>("meshCollisionMaterial");
		auto *show = editor.findChild<QCheckBox *>("meshShowCollision");
		auto *place = editor.findChild<QPushButton *>("placeMeshCollision");
		if (!preview || !tabs || !boxes || !name || !add || !fit || !target || !material || !show || !place)
		{
			return EXIT_FAILURE;
		}
		QScrollArea *scroll = nullptr;
		for (int i = 0; i < tabs->count(); ++i)
		{
			if (tabs->widget(i)->isAncestorOf(boxes))
			{
				tabs->setCurrentIndex(i);
				scroll = tests::pageScroll(tabs->widget(i));
				break;
			}
		}
		editor.show();
		app.processEvents();
		const auto *accessibleBoxes = QAccessible::queryAccessibleInterface(boxes);
		const auto *accessibleAdd = QAccessible::queryAccessibleInterface(add);
		ok &=
			expect(scroll && boxes->focusPolicy() != Qt::NoFocus && accessibleBoxes &&
					   !accessibleBoxes->text(QAccessible::Name).isEmpty() && accessibleAdd && accessibleAdd->role() == QAccessible::Button,
				   "standard accessible collision controls");
		name->setText("body");
		fit->click();
		ok &= expect(editor.document().mesh().collisionBoxes.size() == 1 && editor.document().selection().collision == "body" &&
						 boxes->currentData() == "body",
					 "fit selects authored collision in document and inspector");
		if (editor.document().mesh().collisionBoxes.isEmpty())
		{
			return EXIT_FAILURE;
		}
		preview->frameModel();
		ok &= expect(wait([&] { return !preview->isRendering() && preview->collisionScreenEdges("body").size() == 12; }),
					 "completed mesh snapshot includes twelve collision edges");
		editor.findChild<QDoubleSpinBox *>("meshCollisionRotation2")->setValue(35);
		editor.findChild<QPushButton *>("updateMeshCollision")->click();
		ok &= expect(editor.document().mesh().collisionBoxes[0].rotation.z == 35, "numeric rotation edits box without render geometry");
		if (!manipulate(editor, variant))
		{
			return EXIT_FAILURE;
		}
		name->setText("extra");
		editor.findChild<QPushButton *>("duplicateMeshCollision")->click();
		ok &= expect(editor.document().mesh().collisionBoxes.size() == 2 && boxes->currentData() == "extra", "duplicate selected volume");
		ok &= expect(tests::settleModelViewport(*preview), "coincident collision snapshot settles");
		const auto overlapping = preview->collisionScreenEdges("body");
		ok &= expect(!overlapping.isEmpty() && preview->collisionAt(overlapping.front().center()) == "extra",
					 "coincident edge picks prefer selected box");
		editor.findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(editor.document().mesh().collisionBoxes.size() == 1 && boxes->currentData() == "body",
					 "undo restores collision selection");
		editor.findChild<QAction *>("redoMesh")->trigger();
		ok &= expect(editor.document().mesh().collisionBoxes.size() == 2 && boxes->currentData() == "extra", "redo restores selected copy");
		editor.findChild<QDoubleSpinBox *>("meshCollisionCentre0")->setValue(72);
		editor.findChild<QPushButton *>("updateMeshCollision")->click();
		ok &= expect(findModelCollisionBox(editor.document().mesh(), "extra")->centre.x == 72, "copy can be positioned separately");
		show->setChecked(false);
		ok &= expect(preview->collisionScreenEdges("body").isEmpty(), "collision visibility toggle");
		show->setChecked(true);
		target->setCurrentIndex(2);
		ok &= expect(material->isEnabled(), "Quake III exposes required shader");
		material->setText("common/playerclip");
		editor.checkpointRecovery();
		ok &= expect(wait([&] { return !editor.recoveryBusy(); }), "checkpoint completes asynchronously");
		ModelRecoverySnapshot recovered;
		ok &= expect(inspectModelRecovery(editor.recoveryPath(), &recovered).isValid() && recovered.selection.collision == "extra" &&
						 recovered.mesh.collisionBoxes.size() == 2,
					 "UI recovery retains collision selection");
		auto *selectionMode = editor.findChild<QComboBox *>("meshSelectionMode");
		for (int mode = 0; mode < 3; ++mode)
		{
			boxes->setCurrentIndex(boxes->findData("extra"));
			selectionMode->setCurrentIndex(mode);
			if (mode == 0)
			{
				preview->trianglePicked(0, 0, int(ModelViewportPick::Toggle));
			}
			else if (mode == 1)
			{
				preview->vertexPicked(0, 0, int(ModelViewportPick::Toggle));
			}
			else
			{
				const auto triangle = editor.document().mesh().surfaces[0].triangles[0];
				preview->edgePicked(0, triangle.a, triangle.b, int(ModelViewportPick::Toggle));
			}
			const auto selected = editor.document().selection();
			ok &= expect(selected.collision.isEmpty() && (mode == 0	  ? selected.faces.size()
														  : mode == 1 ? selected.vertices.size()
																	  : selected.edges.size()) == 1,
						 "toggle mesh picking exits exclusive collision selection");
		}
		selectionMode->setCurrentIndex(0);
		boxes->setCurrentIndex(boxes->findData("extra"));
		if (variant == 0)
		{
			LevelMapDocument map;
			ok &= expect(loadLevelMapBytes({path("level.map"), {}, "idtech3"}, "{\n\"classname\" \"worldspawn\"\n}\n", &map, &error),
						 "level fixture", error);
			editor.context = [&] { return ModelDesignContext{{}, path("level.map"), false, false, true}; };
			int published = 0;
			editor.collisionDestination = [&]
			{
				return ModelCollisionDestination{map, [&](const LevelMapDocument &candidate, QString *)
												 {
													 map = candidate;
													 ++published;
													 return true;
												 }};
			};
			editor.refreshContext();
			ok &= expect(place->isEnabled(), "collision placement needs a level but no writable package");
			ok &= expect(editor.placeCollision({"quake3", "common/playerclip", {128, 0, 0}}, &error) && published == 1 &&
							 map.brushes.size() == 2 && undoLevelMapEdit(&map, &error) && map.brushes.isEmpty(),
						 "GUI placement publishes shared undoable candidate", error);
			editor.collisionDestination = [&]
			{
				return ModelCollisionDestination{map, [](const LevelMapDocument &, QString *failure)
												 {
													 *failure = "stale level context";
													 return false;
												 }};
			};
			ok &= expect(!editor.placeCollision({"quake3", "common/playerclip", {}}, &error) && map.brushes.isEmpty() &&
							 error == "stale level context",
						 "stale destination does not publish");
			ok &= expect(editor.exportCollision(path("collision.map"), {"quake2", {}, {}}, false, &error),
						 "GUI collision export through guarded writer", error);
			ok &= expect(!editor.exportCollision(path("collision.map"), {"quake2", {}, {}}, false, &error),
						 "unapproved derivative overwrite refused");
		}
		preview->frameModel();
		ok &= expect(wait([&] { return !preview->isRendering() && preview->collisionScreenEdges("body").size() == 12; }),
					 "preview settles after authored edits");
		ok &= expect(preview->accessibleDescription().contains("collision boxes") && !preview->isPlaying(),
					 "collision summary and reduced motion");
		if (scroll)
		{
			scroll->verticalScrollBar()->setValue(0);
			app.processEvents();
			ok &= expect(scroll->horizontalScrollBar()->maximum() == 0 &&
							 add->width() >= add->fontMetrics().horizontalAdvance(add->text()) + 12,
						 "scaled expanded collision controls fit without horizontal clipping");
			for (const auto *button : scroll->findChildren<QPushButton *>())
			{
				ok &= expect(button->hasHeightForWidth() ? button->height() >= button->heightForWidth(button->width())
					: button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 12,
							 "all collision action labels fit at the configured text scale");
			}
			ok &= expect(capture(editor, QStringLiteral("collision-editor-%1-top").arg(variant)), "capture collision controls");
			scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
			app.processEvents();
			ok &= expect(capture(editor, QStringLiteral("collision-editor-%1-handoff").arg(variant)), "capture collision handoff");
		}
		ok &= expect(capture(*preview, QStringLiteral("collision-preview-%1").arg(variant)), "capture viewport render target");
		if (variant == 2)
		{
			app.removeTranslator(&expansion);
		}
	}
	std::cout << (ok ? "Collision GUI checks passed.\n" : "Collision GUI checks failed.\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
