#include "app/model_editor_dialog.h"
#include "app/model_material_slots_dialog.h"
#include "app/model_uv_view.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_surfaces_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPolygonF>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextLayout>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "ModelMaterialSlotsDialog" && QByteArray(context) != "VibeStudioModelEditor")
			return {};
		const auto original = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(original, QString(original.size() / 2, '~'));
	}
};
bool render(QWidget &widget, const QString &name)
{
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (evidence.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(evidence).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
bool settle(ModelEditorDialog &editor)
{
	QElapsedTimer timer;
	timer.start();
	while (editor.materialLoading() && timer.elapsed() < 20000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	auto *view = editor.findChild<ModelViewport *>("meshPreview");
	auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
	return !editor.materialLoading() && view && uv && settleModelViewport(*view) && settleModelUv(*uv);
}
int colourPixels(const QImage &image, bool blue)
{
	int count = 0;
	for (int y = 0; y < image.height(); y += 2)
		for (int x = 0; x < image.width(); x += 2)
		{
			const auto c = image.pixelColor(x, y);
			const int a = blue ? c.blue() : c.red(), b = blue ? c.red() : c.blue();
			count += a > 50 && a > 2 * b && a > 2 * c.green();
		}
	return count;
}
} // namespace
int main(int argc, char **argv)
{
	// Test-owned widget APIs and QWidget render targets; no OS capture or input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-material-slots-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("material-slots-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	auto original = surfaceFixture();
	for (auto &surface : original.surfaces)
		surface.skinPaths = {"models/red.png", "models/blue.png", "models/red.png"};
	updateEditableModelMetadata(&original);
	auto reader = std::make_shared<SkinReader>();
	QImage red(32, 32, QImage::Format_RGB32), blue(32, 32, QImage::Format_RGB32);
	red.fill(Qt::red);
	blue.fill(Qt::blue);
	reader->add("models/red.png", skinPng(red));
	reader->add("models/blue.png", skinPng(blue));
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelMaterialSlotsDialog review(original, {0, {}, {1}});
		review.resize(scenario == 0 ? 800 : 1500, scenario == 0 ? 600 : 1160);
		review.show();
		app.processEvents();
		auto *list = review.findChild<QListWidget *>("materialSlotList");
		auto *path = review.findChild<QLineEdit *>("materialSlotPath");
		auto *apply = review.findChild<QPushButton *>("applyMaterialSlots");
		auto *summary = review.findChild<QLabel *>("materialSlotSummary");
		if (!list || !path || !apply || !summary)
			return 1;
		for (auto *widget : QList<QWidget *>{list, path, apply, review.findChild<QPushButton *>("addMaterialSlot"),
											 review.findChild<QPushButton *>("moveMaterialSlotUp")})
		{
			auto *access = QAccessible::queryAccessibleInterface(widget);
			ok &= expect(access && !access->text(QAccessible::Name).isEmpty() && !access->text(QAccessible::Description).isEmpty() &&
							 widget->focusPolicy() != Qt::NoFocus,
						 "slot controls expose accessible names, descriptions and keyboard focus");
		}
		ok &= expect(list->count() == 3 && !apply->isEnabled(), "review retains duplicate bindings and has no implicit edits");
		ok &= expect(list->layoutDirection() == Qt::LeftToRight && path->layoutDirection() == Qt::LeftToRight,
					 "technical indices and paths keep left-to-right order inside RTL review layouts");
		path->setText("../invalid.png");
		ok &= expect(!review.findChild<QPushButton *>("addMaterialSlot")->isEnabled() && !apply->isEnabled(),
					 "unsafe paths cannot enter the buffer");
		path->setText("models/green.png");
		review.findChild<QPushButton *>("addMaterialSlot")->click();
		ok &= expect(list->currentRow() == 3 && apply->isEnabled() && review.edit().materialSlots.last() == "models/green.png",
					 "Add appends to the reviewed list");
		review.findChild<QPushButton *>("moveMaterialSlotUp")->click();
		ok &= expect(list->currentRow() == 2 && review.edit().materialSlots[2] == "models/green.png", "Move Up changes only slot ordering");
		review.findChild<QPushButton *>("moveMaterialSlotDown")->click();
		ok &=
			expect(list->currentRow() == 3 && review.edit().materialSlots[3] == "models/green.png", "Move Down uses the final slot index");
		list->setCurrentRow(1);
		path->setText("models/green.png");
		ok &= expect(!apply->isEnabled(), "a pending path cannot be silently omitted from Apply");
		review.findChild<QPushButton *>("replaceMaterialSlot")->click();
		list->setCurrentRow(0);
		review.findChild<QPushButton *>("removeMaterialSlot")->click();
		ok &= expect(review.edit().materialSlots == QStringList{"models/green.png", "models/red.png", "models/green.png"} &&
						 apply->isEnabled(),
					 "Replace and Remove yield the exact reviewed order");
		list->setFocus(Qt::OtherFocusReason);
		app.processEvents();
		for (auto *widget : QList<QWidget *>{list, path, summary, apply})
			ok &= expect(review.rect().contains(QRect(widget->mapTo(&review, QPoint()), widget->size())),
						 "expanded RTL review controls stay within the dialog");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
			ok &= expect(std::abs(review.devicePixelRatioF() - 2) < .01, "review renders at actual 2x device scale");
		ok &= expect(render(review, QStringLiteral("material-slots-review-%1").arg(scenario)), "save review widget render");
		review.findChild<QPushButton *>("clearMaterialSlots")->click();
		ok &= expect(review.edit().materialSlots.isEmpty() && apply->isEnabled(),
					 "clear is an explicit valid unassignment in the review buffer");
		review.reject();
		ModelEditorDialog editor;
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open animated material fixture");
		editor.setMaterialSource({reader, "slot-colours", "auto"});
		editor.resize(scenario == 0 ? 1400 : 2240, scenario == 0 ? 980 : 1400);
		editor.show();
		app.processEvents();
		auto *previewSlot = editor.findChild<QComboBox *>("meshMaterialSlot");
		auto *surface = editor.findChild<QComboBox *>("meshSurface");
		auto *material = editor.findChild<QLineEdit *>("meshMaterial");
		auto *manage = editor.findChild<QPushButton *>("manageMeshMaterialSlots");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
		auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
		if (!previewSlot || !surface || !material || !manage || !undo || !redo || !uv || !viewport)
			return 1;
		ok &= expect(previewSlot->layoutDirection() == Qt::LeftToRight && material->layoutDirection() == Qt::LeftToRight,
					 "preview indices and path editing retain left-to-right technical order");
		editor.showSidebarPage(QStringLiteral("surface"));
		editor.findChild<QComboBox *>("meshRenderMode")->setCurrentIndex(3);
		ok &= expect(settle(editor) && colourPixels(uv->pixmap().toImage(), false) > 100, "primary red material reaches the UV view");
		const auto before = surfaceBytes(editor.document().mesh());
		previewSlot->setCurrentIndex(1);
		const auto label = previewSlot->currentText();
		QTextLayout layout(label, previewSlot->font());
		QTextOption bidi;
		bidi.setTextDirection(Qt::RightToLeft);
		layout.setTextOption(bidi);
		layout.beginLayout();
		auto line = layout.createLine();
		line.setLineWidth(1000);
		layout.endLayout();
		ok &= expect(line.cursorToX(label.indexOf("1:")) < line.cursorToX(label.indexOf("models/")),
					 "Qt bidi text layout keeps the preview slot ordinal before its technical path");
		ok &=
			expect(settle(editor) && material->text() == "models/blue.png" && colourPixels(uv->pixmap().toImage(), true) > 100 &&
					   surfaceBytes(editor.document().mesh()) == before && !editor.document().canUndo() && !editor.document().isModified(),
				   "alternate preview changes pixels without dirtying source or history");
		QImage image(viewport->size(), QImage::Format_RGB32);
		image.fill(Qt::black);
		viewport->render(&image);
		QPolygonF bounds;
		for (int v = 0; v < original.surfaces[0].vertexCount; ++v)
			bounds << viewport->vertexScreenPosition(0, v);
		ok &= expect(colourPixels(image.copy(bounds.boundingRect().toAlignedRect().intersected(image.rect())), true) > 30,
					 "alternate material reaches the rendered model surface");
		QString shown;
		editor.showMaterial = [&](const QString &value) { shown = value; };
		editor.findChild<QPushButton *>("showMeshMaterial")->click();
		ok &= expect(shown == "models/blue.png", "Show Material hands off the chosen alternate path");
		surface->setCurrentIndex(1);
		ok &= expect(previewSlot->currentIndex() == 0 && material->text() == "models/red.png",
					 "each surface has an independent preview slot");
		surface->setCurrentIndex(0);
		ok &= expect(previewSlot->currentIndex() == 1 && material->text() == "models/blue.png",
					 "returning to a surface restores its preview index");
		ok &= expect(settle(editor) && render(editor, QStringLiteral("material-slots-editor-%1").arg(scenario)),
					 "save alternate preview editor render");
		editor.findChild<QComboBox *>("meshFrameScope")->setCurrentIndex(1);
		material->setText("models/red.png");
		editor.findChild<QPushButton *>("assignMeshMaterial")->click();
		ok &= expect(editor.document().mesh().surfaces[0].skinPaths == QStringList{"models/red.png", "models/red.png", "models/red.png"} &&
						 editor.document().mesh().surfaces[1].skinPaths == original.surfaces[1].skinPaths,
					 "Assign Material updates the selected alternate across all poses");
		undo->trigger();
		ok &= expect(surfaceBytes(editor.document().mesh()) == before && previewSlot->currentIndex() == 1,
					 "undo restores exact ordered slots without discarding view selection");
		bool reviewed = false, responsive = false, locked = false;
		QTimer heartbeat;
		heartbeat.setInterval(0);
		QObject::connect(&heartbeat, &QTimer::timeout, &editor, [&] {
			if (editor.operationBusy())
			{
				responsive = true;
				locked = !manage->isEnabled();
			}
		});
		QTimer::singleShot(0, &editor, [&] {
			auto *dialog = dynamic_cast<ModelMaterialSlotsDialog *>(QApplication::activeModalWidget());
			if (!dialog)
			{
				if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
					modal->reject();
				return;
			}
			reviewed = true;
			dialog->findChild<QListWidget *>("materialSlotList")->setCurrentRow(1);
			dialog->findChild<QPushButton *>("moveMaterialSlotUp")->click();
			dialog->findChild<QPushButton *>("applyMaterialSlots")->click();
		});
		heartbeat.start();
		manage->click();
		heartbeat.stop();
		ok &=
			expect(reviewed && responsive && locked &&
					   editor.document().mesh().surfaces[0].skinPaths == QStringList{"models/blue.png", "models/red.png", "models/red.png"},
				   "closed review dispatches one asynchronous source edit with controls locked");
		undo->trigger();
		ok &= expect(surfaceBytes(editor.document().mesh()) == before && !editor.document().canUndo(), "review has one undo step");
		redo->trigger();
		ModelEdit clear;
		clear.kind = ModelEditKind::SetMaterialSlots;
		ok &=
			expect(editor.applyEdit(clear, &error) && previewSlot->count() == 1 && !previewSlot->isEnabled() && material->text().isEmpty(),
				   "empty slots clear stale preview controls");
		ok &= expect(editor.setMesh(original, &error) && previewSlot->currentIndex() == 0, "new source clears transient preview choices");
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.applyEdit(clear, &error) && surfaceBytes(editor.document().mesh()) == before && !editor.document().canUndo(),
					 "cancelled slot edits preserve the document");
		ModelMesh mdl;
		ok &= expect(importEditableModel("skin.mdl", groupedMdlFixture().bytes, &mdl, &error) && editor.setMesh(mdl, &error) &&
						 !manage->isEnabled() && !previewSlot->isEnabled() &&
						 !editor.findChild<QPushButton *>("assignMeshMaterial")->isEnabled(),
					 "embedded MDL models keep external authoring disabled");
		ok &= expect(previewSlot->currentText().contains("Animation") && manage->accessibleDescription().contains("MDL"),
					 "disabled external controls explain where embedded skin authoring is available");
		ok &= expect(editor.setMesh(original, &error), "retire transient recovery state");
		editor.setMaterialSource({});
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	ok &= expect(!reader->readOnGui, "all archive image reads happen off the GUI thread");
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	return ok ? 0 : 1;
}
