#include "app/application_shell.h"
#include "app/model_editor_dialog.h"
#include "app/model_preview_worker.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"
#include "core/model_obj.h"
#include "core/package_draft.h"
#include "core/studio_settings.h"
#include "tests/model_skin_source_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
bool waitFor(const std::function<bool()> &condition, int timeout = 15000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < timeout)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return condition();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor" && QByteArray(context) != "VibeStudioModelObj")
		{
			return {};
		}
		const auto original = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(original, QString(original.size() / 2, '~'));
	}
};
const QByteArray fixture("v -50 -20 0\nv -10 -20 0\nv -10 20 0\nv -50 20 0\nv 10 -20 0\nv 50 -20 0\nv 50 20 0\nv 10 20 0\n"
						 "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\ng left\nusemtl models/red.png\nf 1/1/1 2/2/1 3/3/1 4/4/1\n"
						 "g right\nusemtl models/blue.png\nf 5/1/1 6/2/1 7/3/1 8/4/1\n");
bool capture(QWidget &widget, const QString &name)
{
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (evidence.isEmpty())
	{
		return true;
	}
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(evidence).filePath(name + ".png"));
}
bool bothMaterials(ModelViewport &viewport)
{
	viewport.setRenderMode(ModelViewportRenderMode::Textured);
	viewport.setBackfaceCulling(false);
	viewport.setShowGrid(false);
	viewport.setShowAxes(false);
	viewport.setOrbit(0, 80);
	QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	viewport.frameModel();
	if (!tests::settleModelViewport(viewport))
	{
		return false;
	}
	QImage image(viewport.size(), QImage::Format_RGB32);
	viewport.render(&image);
	int red = 0, blue = 0;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			const auto c = image.pixelColor(x, y);
			red += c.red() > 70 && c.red() > 2 * c.green() && c.red() > 2 * c.blue();
			blue += c.blue() > 70 && c.blue() > 2 * c.green() && c.blue() > 2 * c.red();
		}
	}
	return red > 500 && blue > 500;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-obj-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-obj-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	bool ok = true;
	QString error;
	QDir().mkpath(path("assets/models"));
	QImage red(16, 16, QImage::Format_RGB32), blue(16, 16, QImage::Format_RGB32);
	red.fill(Qt::red);
	blue.fill(Qt::blue);
	ok &= expect(write(path("assets/models/fixture.obj"), fixture) && write(path("bad.obj"), "mtllib material.mtl\n" + fixture) &&
					 red.save(path("assets/models/red.png")) && blue.save(path("assets/models/blue.png")),
				 "prepare independently generated model and materials");
	auto archive = std::make_shared<PackageArchive>();
	ok &= expect(archive->load(path("assets"), &error), "open OBJ package folder");
	{
		ModelPreviewWorker worker;
		QVector<ModelPreviewResult> results;
		worker.completed = [&](const auto &result) { results.append(result); };
		auto slow = std::make_shared<tests::SkinReader>();
		slow->add("slow.obj", fixture);
		slow->delay = true;
		worker.request({slow, "slow", "quake"}, "slow.obj", "retired");
		ok &= expect(waitFor([&] { return slow->entered.load(); }), "preview starts on the worker");
		worker.request({archive, "live", "quake"}, "models/fixture.obj", "latest");
		ok &= expect(waitFor([&] { return !worker.busy(); }) && results.size() == 1 && results[0].key == "latest" &&
						 results[0].mesh.geometryAvailable && results[0].assets.readyCount() == 2 && !slow->readOnGui,
					 "latest immutable package request wins, with per-surface material images and no GUI I/O");
		results.clear();
		slow->entered = false;
		worker.request({slow, "slow", "quake"}, "slow.obj", "cancelled");
		ok &= expect(waitFor([&] { return slow->entered.load(); }), "second worker read starts");
		worker.cancel();
		ok &= expect(waitFor([&] { return !worker.busy(); }) && results.size() == 1 && results[0].cancelled &&
						 results[0].mesh.surfaces.isEmpty(),
					 "cancel publishes one empty result and discards late completion");
		results.clear();
		auto broken = std::make_shared<tests::SkinReader>();
		broken->add("broken.obj", fixture);
		broken->lateFailure = true;
		worker.request({broken, "broken", "quake"}, "broken.obj", "broken");
		ok &= expect(waitFor([&] { return !worker.busy(); }) && results.size() == 1 && !results[0].error.isEmpty() &&
						 !results[0].mesh.geometryAvailable,
					 "late package verification failure cannot publish geometry");
		slow->entered = false;
		worker.request({slow, "closing", "quake"}, "slow.obj", "closing");
		ok &= expect(waitFor([&] { return slow->entered.load(); }), "active worker is safe to destroy");
	}
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		ModelEditorDialog editor;
		editor.setMaterialSource({archive, "folder", "quake"});
		editor.setAccessibility(scenario != 0, true);
		editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.resize(scenario ? 1700 : 1450, scenario ? 1150 : 900);
		editor.show();
		ok &= expect(editor.openSource(path("assets/models/fixture.obj"), &error),
					 "editor Open imports OBJ through its cancellable document worker");
		auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
		if (auto *mode = editor.findChild<QComboBox *>("meshRenderMode"))
		{
			mode->setCurrentIndex(3);
		}
		auto *surface = editor.findChild<QComboBox *>("meshSurface");
		ok &= expect(viewport && surface && surface->count() == 2 && !surface->accessibleName().isEmpty() &&
						 surface->focusPolicy() != Qt::NoFocus && editor.document().mesh().triangleCount == 4,
					 "imported surfaces expose ordinary accessible mesh controls");
		if (viewport)
		{
			viewport->setOrbit(0, 80);
			ok &= expect(waitFor([&] { return !editor.materialLoading(); }) && tests::settleModelViewport(*viewport),
						 "imported package materials settle on the render worker");
		}
		const auto revision = editor.document().revisionFingerprint();
		ok &= expect(!editor.openSource(path("bad.obj"), &error) && error.contains("material") &&
						 editor.document().revisionFingerprint() == revision,
					 "failed OBJ import leaves the current document intact with actionable diagnostics");
		if (viewport)
		{
			ok &= expect(bothMaterials(*viewport), "editor renders both OBJ material assignments after failed import");
		}
		ok &=
			expect(capture(editor, QStringLiteral("obj-editor-%1").arg(scenario)), "render imported mesh and error using QWidget::render");
		ModelEdit edit;
		edit.kind = ModelEditKind::Transform;
		edit.selection.faces = {0};
		edit.translation = {0, 0, 2};
		ok &= expect(editor.applyEdit(edit, &error), "imported geometry supports the ordinary undo transaction");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		if (undo)
		{
			undo->trigger();
		}
		ok &= expect(undo && editor.document().revisionFingerprint() == revision, "editor undo restores exact imported geometry");
		editor.close();
		if (scenario)
		{
			app.removeTranslator(&expansion);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		PackageStagingModel plan;
		ok &= expect(
			plan.loadBaseArchive(*archive, &error) && plan.addBytes(fixture, "models/staged.obj", &error) &&
				plan.addBytes(exportEditableModel(decodeModelObj("fixture.obj", fixture), "md3", 0, &error), "models/native.md3", &error) &&
				PackageDraft::save(path("models.vibepackage"), &plan, false, &error),
			"save staged OBJ fixture for the shell handoff");
		ApplicationShell shell;
		shell.resize(1450, 960);
		shell.show();
		shell.openPathFromCommandLine(path("models.vibepackage"));
		if (auto *mode = shell.findChild<QAction *>("shell.mode.models"))
		{
			mode->trigger();
		}
		auto *entries = shell.findChild<QListWidget *>("modelEntries");
		auto *viewport = shell.findChild<ModelViewport *>("modelViewport");
		auto *open = shell.findChild<QPushButton *>("openModelEditor");
		auto *cancel = shell.findChild<QPushButton *>("cancelModelPreview");
		ok &= expect(entries && viewport && open && cancel && !cancel->accessibleName().isEmpty() && cancel->focusPolicy() != Qt::NoFocus,
					 "shell exposes OBJ browser and accessible cancellation");
		if (entries && viewport && open && cancel)
		{
			QListWidgetItem *staged = nullptr;
			ok &= expect(waitFor(
							 [&]
							 {
								 for (int i = 0; i < entries->count(); ++i)
								 {
									 if (entries->item(i)->data(Qt::UserRole).toString() == "models/staged.obj")
									 {
										 staged = entries->item(i);
										 return true;
									 }
								 }
								 return false;
							 }),
						 "saved staged OBJ appears in Models");
			if (staged)
			{
				QListWidgetItem *native = nullptr;
				for (int i = 0; i < entries->count(); ++i)
				{
					if (entries->item(i)->data(Qt::UserRole).toString() == "models/native.md3")
					{
						native = entries->item(i);
					}
				}
				entries->setCurrentItem(staged);
				if (native)
				{
					entries->setCurrentItem(native);
				}
				ok &= expect(native && waitFor([&] { return viewport->mesh().format == ModelMeshFormat::Quake3Md3 && viewport->hasMesh(); }),
							 "native selection retires pending OBJ work");
				ok &= expect(waitFor([&] { return !shell.findChild<QPushButton *>("cancelModelPreview")->isVisible(); }),
							 "native selection clears OBJ loading controls");
				entries->setCurrentItem(staged);
				cancel->click();
				ok &= expect(!viewport->hasMesh(), "shell Cancel Preview discards the pending OBJ");
				entries->itemClicked(staged);
				ok &= expect(waitFor([&] { return viewport->mesh().sourcePath == "models/staged.obj" && viewport->hasMesh(); }) &&
								 tests::settleModelViewport(*viewport),
							 "shell asynchronously decodes the staged OBJ");
				ok &= expect(viewport->mesh().triangleCount == 4 && viewport->hasSkin(), "shell retains material-separated OBJ geometry");
				ok &= expect(bothMaterials(*viewport), "browser renders independently resolved red and blue surfaces");
				auto *details = shell.findChild<QTreeWidget *>("modelDetails");
				int resolvedSkins = 0;
				if (details)
				{
					for (QTreeWidgetItemIterator it(details); *it; ++it)
					{
						if (!(*it)->data(0, Qt::UserRole + 7).toString().isEmpty() && !(*it)->icon(0).isNull())
						{
							++resolvedSkins;
						}
					}
				}
				ok &= expect(details && details->findItems("Warnings*", Qt::MatchWildcard | Qt::MatchRecursive).isEmpty() && resolvedSkins == 2,
							 "browser inspector marks both resolved skins without a false missing-skin warning");
				ok &= expect(capture(shell, "obj-browser"), "render asynchronous OBJ package browser evidence");
				open->click();
				app.processEvents();
				auto *editor = static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>("modelEditorDialog"));
				ok &= expect(editor && editor->document().mesh().triangleCount == 4 && editor->document().mesh().surfaces.size() == 2,
							 "Edit Mesh adopts the selected staged OBJ");
				if (editor)
				{
					if (auto *mode = editor->findChild<QComboBox *>("meshRenderMode"))
					{
						mode->setCurrentIndex(3);
					}
					ok &= expect(waitFor([&] { return !editor->materialLoading(); }), "staged OBJ editor retains package material context");
					if (auto *preview = editor->findChild<ModelViewport *>("meshPreview"))
					{
						ok &= expect(bothMaterials(*preview), "staged editor renders both surface textures");
					}
					ok &= expect(capture(*editor, "obj-staged-editor"), "render staged OBJ handoff evidence");
					editor->close();
				}
			}
		}
		shell.close();
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	std::cout << "OBJ worker retirement, cancellation, verification, editor import and staged shell handoff verified.\n";
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
