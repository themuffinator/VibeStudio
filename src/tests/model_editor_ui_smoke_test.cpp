#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/model_design.h"
#include "core/studio_settings.h"
#include "tests/model_uv_view_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

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
		const auto original = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(original, QString(original.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Exercise widgets and signals directly. No user keyboard/mouse control or
	// desktop screenshots; render the application's own widgets to QImage.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QElapsedTimer elapsed;
	elapsed.start();
	const auto timing = [&](const char *stage)
	{
		if (qEnvironmentVariableIsSet("VIBESTUDIO_MODELLER_TIMING"))
		{
			std::cout << "TIMING " << stage << ' ' << elapsed.elapsed() << " ms" << std::endl;
		}
	};
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-ui-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	bool ok = true;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	QString error;
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	const auto plane = buildModelDesignMesh(design);
	{
		auto busyMesh = plane;
		busyMesh.surfaces[0].triangles.fill(plane.surfaces[0].triangles[0], modelDocumentMaxTriangles);
		updateEditableModelMetadata(&busyMesh);
		auto *editor = new ModelEditorDialog;
		const auto before = editor->document().revisionFingerprint();
		bool checked = false;
		QTimer::singleShot(
			0, editor,
			[&]
			{
				checked = true;
				ok &= expect(editor->operationBusy() && !editor->findChild<QWidget *>(QStringLiteral("meshEditingControls"))->isEnabled() &&
								 !editor->findChild<QAction *>(QStringLiteral("saveMesh"))->isEnabled(),
							 "document work disables editing controls and shortcuts while servicing UI events");
				ModelEdit nested;
				QString failure;
				ok &= expect(!editor->applyEdit(nested, &failure) && !failure.isEmpty(), "reentrant document edits are rejected");
				editor->cancelOperation();
			});
		ok &= expect(!editor->setMesh(busyMesh, &error) && checked && !editor->operationBusy() &&
						 editor->document().revisionFingerprint() == before &&
						 editor->findChild<QWidget *>(QStringLiteral("meshEditingControls"))->isEnabled(),
					 "cancelled preparation restores controls and preserves the visible document");
		QPointer<ModelEditorDialog> lifetime(editor);
		bool closeDeferred = false;
		int resumed = 0;
		QTimer::singleShot(0, editor, [&] { closeDeferred = !editor->requestClose([&] { ++resumed; }) && editor->operationBusy(); });
		ok &= expect(!editor->setMesh(busyMesh, &error) && closeDeferred && lifetime,
					 "closing during document work cancels without destroying the active editor");
		app.processEvents();
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		app.processEvents();
		ok &= expect(lifetime.isNull() && resumed == 1, "deferred editor close resumes its caller once after the worker returns");
	}
	{
		auto *editor = new ModelEditorDialog;
		ok &= expect(editor->setMesh(plane, &error), "prepare a dirty deferred-close fixture");
		ModelEdit move;
		move.selection.faces = {0};
		move.translation.z = 1;
		ok &= expect(editor->applyEdit(move, &error), "deferred-close fixture has unsaved changes");
		editor->show();
		QPointer<ModelEditorDialog> lifetime(editor);
		int resumed = 0;
		QTimer::singleShot(0, editor, [&] { editor->requestClose([&] { ++resumed; }); });
		auto busyMesh = plane;
		busyMesh.surfaces[0].triangles.fill(plane.surfaces[0].triangles[0], modelDocumentMaxTriangles);
		updateEditableModelMetadata(&busyMesh);
		ok &= expect(!editor->setMesh(busyMesh, &error), "deferred close cancels a replacement candidate");
		bool answered = false;
		QTimer answer;
		answer.setInterval(0);
		QObject::connect(&answer, &QTimer::timeout, &app,
						 [&]
						 {
							 if (auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
							 {
								 answered = true;
								 answer.stop();
								 prompt->button(QMessageBox::Cancel)->click();
							 }
						 });
		answer.start();
		app.processEvents();
		answer.stop();
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		ok &= expect(answered && lifetime && editor->isVisible() && editor->document().isModified() && resumed == 0,
					 "cancelling the unsaved prompt keeps the editor and caller open");
		if (lifetime)
		{
			editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
			editor->close();
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		app.processEvents();
		ok &= expect(!lifetime && resumed == 0, "a cancelled continuation cannot fire on a later independent close");
	}
	for (int scale : {100, 200})
	{
		timing(scale == 100 ? "editor-100-start" : "editor-200-start");
		Expansion expansion;
		if (scale == 200)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app,
						 studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		auto *editor = new ModelEditorDialog;
		ok &= expect(editor->setMesh(plane, &error), "authoring source loads");
		ok &= expect(editor->findChild<QAction *>(QStringLiteral("openMesh"))->text().contains(QChar(0x2026)),
					 "localized action punctuation retains its Unicode ellipsis");
		editor->setAccessibility(scale == 200, true);
		if (scale == 200)
		{
			editor->setLayoutDirection(Qt::RightToLeft);
			editor->resize(1700, 1100);
		}
		editor->show();
		app.processEvents();
		auto *table = editor->findChild<QTableView *>(QStringLiteral("meshComponents"));
		auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
		ok &= expect(table && preview && table->model()->rowCount() == 2 && !table->accessibleName().isEmpty() &&
						 table->focusPolicy() != Qt::NoFocus,
					 "mesh components are accessible and focusable without "
					 "creating one widget per face");
		table->selectAll();
		ok &= expect(editor->document().selection().faces.size() == 2 && preview->highlightedTriangleCount() == 2,
					 "table selection drives document and preview");
		editor->findChild<QDoubleSpinBox *>(QStringLiteral("meshOffset2"))->setValue(16);
		editor->findChild<QPushButton *>(QStringLiteral("extrudeMesh"))->click();
		ok &= expect(editor->document().mesh().triangleCount == 10 && editor->document().isModified() &&
						 table->selectionModel()->selectedRows().size() == 2,
					 "extrusion updates geometry and selects the cap");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		ok &= expect(editor->document().mesh().triangleCount == 2 && !editor->document().isModified() &&
						 editor->document().selection().faces.size() == 2,
					 "UI undo restores original selection and clean state");
		preview->trianglePicked(0, 0, int(ModelViewportPick::Replace));
		ok &= expect(editor->document().selection().faces == QSet<int>{0} && table->selectionModel()->selectedRows().size() == 1,
					 "viewport face selection appears in the keyboard-accessible table");
		editor->findChild<QDoubleSpinBox *>(QStringLiteral("meshUvOffset0"))->setValue(0.25);
		editor->findChild<QPushButton *>(QStringLiteral("transformMeshUvs"))->click();
		ok &= expect(editor->document().mesh().vertexCount == 6 &&
						 tests::settleModelUv(*editor->findChild<ModelUvView *>(QStringLiteral("meshUvPreview"))),
					 "UV edits split a seam and update the layout preview");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		editor->findChild<QPushButton *>(QStringLiteral("duplicateMeshFrame"))->click();
		ok &= expect(editor->document().mesh().frameCount == 2 && editor->findChild<QComboBox *>(QStringLiteral("meshFrame"))->count() == 2,
					 "frame duplication updates authoring and transport");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		auto *mode = editor->findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
		mode->setCurrentIndex(2);
		ok &=
			expect(table->model()->rowCount() == 5 && table->model()->columnCount() == 5 && editor->document().selection().faces.isEmpty(),
				   "Edges mode exposes indexed endpoints, length and incident-face count");
		int sharedRow = -1;
		for (int row = 0; row < table->model()->rowCount(); ++row)
		{
			if (table->model()->index(row, 4).data().toInt() == 2)
			{
				sharedRow = row;
			}
		}
		ok &= expect(sharedRow >= 0, "component table identifies the shared diagonal");
		if (sharedRow >= 0)
		{
			const int a = table->model()->index(sharedRow, 1).data().toInt(), b = table->model()->index(sharedRow, 2).data().toInt();
			preview->edgePicked(0, a, b, int(ModelViewportPick::Replace));
			ok &= expect(editor->document().selection().edges == QSet<ModelEdge>{{a, b}} && preview->highlightedEdgeCount() == 1 &&
							 table->selectionModel()->selectedRows().size() == 1,
						 "viewport edge selection synchronizes the document, table and preview");
			preview->edgePicked(0, a, b, int(ModelViewportPick::Toggle));
			ok &= expect(editor->document().selection().edges.isEmpty(), "edge toggle removes the selected pair");
			// Select through the model so an offscreen RTL header does not need to
			// resolve a viewport coordinate before the next layout event.
			table->selectionModel()->select(table->model()->index(sharedRow, 0),
											QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
			ok &= expect(editor->document().selection().edges == QSet<ModelEdge>{{a, b}},
						 "table row selects the intended indexed edge before splitting");
			editor->findChild<QPushButton *>(QStringLiteral("splitMeshEdges"))->click();
			if (editor->document().mesh().triangleCount != 4 || editor->document().selection().edges.size() != 2 ||
				preview->highlightedEdgeCount() != 2)
			{
				std::cerr << "Split UI scale=" << scale << " triangles=" << editor->document().mesh().triangleCount
						  << " edges=" << editor->document().selection().edges.size() << " highlight=" << preview->highlightedEdgeCount()
						  << " row=" << sharedRow << " endpoints=" << a << ':' << b << '\n';
				for (const auto *label : editor->findChildren<QLabel *>())
				{
					if (label->objectName().contains(QStringLiteral("Status")))
					{
						std::cerr << label->objectName().toStdString() << ':' << label->text().toStdString() << '\n';
					}
				}
			}
			ok &= expect(editor->document().mesh().triangleCount == 4 && editor->document().selection().edges.size() == 2 &&
							 preview->highlightedEdgeCount() == 2,
						 "Split Edges control creates conforming topology and selects its child edges");
			const auto topologyEvidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
			if (!topologyEvidence.isEmpty())
			{
				auto *inspector = editor->findChild<QTabWidget *>(QStringLiteral("meshInspector"));
				auto *geometry = qobject_cast<QScrollArea *>(inspector->widget(0));
				inspector->setCurrentIndex(0);
				geometry->ensureWidgetVisible(editor->findChild<QPushButton *>(QStringLiteral("weldMeshVertices")));
				app.processEvents();
				QImage evidenceImage(editor->size(), QImage::Format_ARGB32_Premultiplied);
				evidenceImage.fill(Qt::transparent);
				ok &= expect(tests::settleModelViewport(*preview), "edge selection preview completes before topology evidence");
				editor->render(&evidenceImage);
				ok &= expect(evidenceImage.save(QDir(topologyEvidence).filePath(QStringLiteral("mesh-topology-%1.png").arg(scale))),
							 "save topology control and selected-edge evidence");
			}
			editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
			ok &= expect(editor->document().mesh().triangleCount == 2 && editor->document().selection().edges == QSet<ModelEdge>{{a, b}},
						 "UI undo restores edge identity");
		}
		mode->setCurrentIndex(0);
		preview->trianglePicked(0, 0, int(ModelViewportPick::Replace));
		editor->findChild<QPushButton *>(QStringLiteral("transformMeshUvs"))->click();
		mode->setCurrentIndex(1);
		table->selectAll();
		auto *seams = editor->findChild<QCheckBox *>(QStringLiteral("meshPreserveSeams"));
		auto *distance = editor->findChild<QDoubleSpinBox *>(QStringLiteral("meshWeldDistance"));
		ok &= expect(seams->isChecked() && !seams->accessibleName().isEmpty() && !distance->accessibleName().isEmpty(),
					 "weld controls expose seam protection and accessible names");
		seams->setChecked(false);
		distance->setValue(0);
		editor->findChild<QPushButton *>(QStringLiteral("weldMeshVertices"))->click();
		ok &= expect(editor->document().mesh().vertexCount == 4,
					 "Weld by Distance joins explicitly approved UV seams through document services");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		mode->setCurrentIndex(0);
		int handoffs = 0;
		editor->context = [&]() { return ModelDesignContext{temporary.path(), QStringLiteral("map.map"), true, true}; };
		editor->handoff = [&](const QByteArray &bytes, const QString &, bool place, const LevelMapVec3 &, bool, QString *)
		{
			++handoffs;
			const auto mesh = decodeModelMesh(QStringLiteral("staged.md3"), bytes);
			return place && mesh.triangleCount == 2;
		};
		editor->refreshContext();
		editor->findChild<QAction *>(QStringLiteral("placeMesh"))->trigger();
		ok &= expect(handoffs == 1, "mesh document reaches the normal package/map handoff callback");
		auto *skinWidth = editor->findChild<QSpinBox *>(QStringLiteral("meshMd2Width"));
		auto *skinHeight = editor->findChild<QSpinBox *>(QStringLiteral("meshMd2Height"));
		ok &= expect(skinWidth && skinHeight && !skinWidth->accessibleName().isEmpty() && !skinHeight->accessibleDescription().isEmpty() &&
						 editor->findChild<QAction *>(QStringLiteral("exportMeshMd2")),
					 "MD2 export and accessible skin settings are available");
		skinWidth->setValue(320);
		skinHeight->setValue(200);
		editor->findChild<QPushButton *>(QStringLiteral("applyMeshMd2Size"))->click();
		ok &= expect(editor->document().mesh().md2SkinSize == QSize(320, 200), "skin size uses document edit history");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		ok &= expect(skinWidth->value() == 256 && skinHeight->value() == 256, "undo refreshes MD2 skin controls");
		editor->findChild<QAction *>(QStringLiteral("redoMesh"))->trigger();
		ModelEdit pcx;
		pcx.kind = ModelEditKind::SetMaterial;
		pcx.text = QStringLiteral("models/skin.pcx");
		ok &= expect(editor->applyEdit(pcx, &error), "assign external MD2 skin");
		editor->findChild<QLineEdit *>(QStringLiteral("meshVirtualPath"))->setText(QStringLiteral("models/panel.md2"));
		editor->handoff = [&](const QByteArray &bytes, const QString &path, bool place, const LevelMapVec3 &, bool, QString *)
		{
			++handoffs;
			const auto mesh = decodeModelMesh(path, bytes);
			return !place && mesh.error.isEmpty() && mesh.md2SkinSize == QSize(320, 200) && mesh.triangleCount == 2;
		};
		ok &= expect(!editor->findChild<QAction *>(QStringLiteral("placeMesh"))->isEnabled(),
					 "MD2 package path disables unsupported automatic placement");
		editor->findChild<QAction *>(QStringLiteral("stageMesh"))->trigger();
		ok &= expect(handoffs == 2 && editor->findChild<QLabel *>(QStringLiteral("meshStatus"))->text().contains(QStringLiteral("162")),
					 "worker prepares MD2 handoff and reports quantization");
		auto *tabs = editor->findChild<QTabWidget *>(QStringLiteral("meshInspector"));
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		for (int tab = 0; tab < tabs->count(); ++tab)
		{
			tabs->setCurrentIndex(tab);
			app.processEvents();
			auto *scroll = qobject_cast<QScrollArea *>(tabs->widget(tab));
			ok &= expect(scroll && scroll->widgetResizable(), "all inspector pages support scalable scrolling");
			if (!evidence.isEmpty())
			{
				QDir().mkpath(evidence);
				QImage image(editor->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				ok &= expect(tests::settleModelViewport(*preview), "mesh preview worker completes before evidence capture");
				editor->render(&image);
				ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("mesh-%1-tab-%2.png").arg(scale).arg(tab))),
							 "render mesh UI evidence");
			}
		}
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		ok &= expect(!editor->document().isModified(), "UI test returns to clean source after MD2 settings and material edits");
		editor->close();
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		if (scale == 200)
		{
			app.removeTranslator(&expansion);
		}
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	{
		timing("material-preview-start");
		ModelDesign materials;
		ModelDesignPart left, right;
		left.name = QStringLiteral("left");
		right.name = QStringLiteral("right");
		left.primitive = right.primitive = QStringLiteral("plane");
		left.origin.x = -40;
		right.origin.x = 40;
		materials.parts = {left, right};
		ModelViewport viewport;
		viewport.resize(600, 400);
		viewport.setMesh(buildModelDesignMesh(materials));
		viewport.setBackfaceCulling(false);
		viewport.setShowGrid(false);
		viewport.setShowAxes(false);
		viewport.setOrbit(0, 80);
		QImage red(16, 8, QImage::Format_RGB32), green(8, 16, QImage::Format_RGB32);
		red.fill(Qt::red);
		green.fill(Qt::green);
		viewport.setSurfaceSkins({{0, red}, {1, green}});
		viewport.setRenderMode(ModelViewportRenderMode::Textured);
		viewport.show();
		app.processEvents();
		ok &= expect(tests::settleModelViewport(viewport), "material preview completes on its render worker");
		QImage image(viewport.size(), QImage::Format_RGB32);
		viewport.render(&image);
		int redPixels = 0, greenPixels = 0;
		for (int y = 0; y < image.height(); ++y)
		{
			for (int x = 0; x < image.width(); ++x)
			{
				const auto color = image.pixelColor(x, y);
				if (color.red() > 50 && color.red() > color.green() * 2 && color.red() > color.blue() * 2)
				{
					++redPixels;
				}
				if (color.green() > 50 && color.green() > color.red() * 2 && color.green() > color.blue() * 2)
				{
					++greenPixels;
				}
			}
		}
		ok &= expect(viewport.hasSkin() && redPixels > 500 && greenPixels > 500,
					 "viewport uses each surface's material image and dimensions without a shared fallback");
		viewport.clearSkin();
		ok &= expect(!viewport.hasSkin(), "clearing skins removes per-surface overrides too");
	}
	StudioSettings::setOverrideFilePath(QString());
	timing("complete");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
