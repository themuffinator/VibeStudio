#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/package_staging.h"
#include "core/studio_settings.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>

#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

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
bool settle(ModelEditorDialog &editor)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (editor.materialLoading() && elapsed.elapsed() < 20000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	auto *uv = editor.findChild<ModelUvView *>(QStringLiteral("meshUvPreview"));
	return !editor.materialLoading() && uv && tests::settleModelUv(*uv);
}
QByteArray solid(const QColor &color)
{
	QImage image(32, 32, QImage::Format_RGB32);
	image.fill(color);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}
int colorPixels(const QImage &image, bool blue)
{
	int count = 0;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			const auto color = image.pixelColor(x, y);
			const int channel = blue ? color.blue() : color.red(), other = blue ? color.red() : color.blue();
			if (channel > 50 && channel > color.green() * 2 && channel > other * 2)
			{
				++count;
			}
		}
	}
	return count;
}
QImage materialPixels(ModelViewport &viewport, const QImage &image)
{
	// Linux font subpixel antialiasing can add red/blue pixels to the HUD.
	// Check the projected fixture surface, excluding unrelated text overlays.
	QPolygonF projected;
	const auto &positions = viewport.mesh().surfaces.first().frames.first().positions;
	for (qsizetype vertex = 0; vertex < positions.size(); ++vertex) {
		projected << viewport.vertexScreenPosition(0, static_cast<int>(vertex));
	}
	return image.copy(projected.boundingRect().toAlignedRect().intersected(image.rect()));
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
		return QStringLiteral("[%1%2]").arg(original, QString(original.size() / 2, QLatin1Char('~')));
	}
};
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	// Widget APIs and widget render targets only; no physical input or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-materials-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temp(QDir(root).filePath(QStringLiteral("mesh-material-ui-XXXXXX")));
	if (!temp.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	QString error;
	bool ok = tests::putMaterialFile(QDir(temp.path()).filePath(QStringLiteral("assets/models/skin.png")), solid(Qt::red));
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error) && staging.loadBaseArchive(archive, &error);
	const auto original = std::make_shared<PackageArchive>(packagePlannedArchive(staging));
	ok &= staging.addBytes(solid(Qt::blue), QStringLiteral("models/skin.png"), &error, PackageStageConflictResolution::ReplaceExisting);
	const auto replacement = std::make_shared<PackageArchive>(packagePlannedArchive(staging));
	ok &= staging.deleteEntry(QStringLiteral("models/skin.png"), &error);
	const auto deletion = std::make_shared<PackageArchive>(packagePlannedArchive(staging));
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	part.material = QStringLiteral("models/skin.png");
	design.parts << part;
	const auto mesh = buildModelDesignMesh(design);
	for (int scale : {100, 200})
	{
		Expansion expansion;
		if (scale == 200)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app,
						 studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		auto *editor = new ModelEditorDialog;
		ok &= expect(editor->setMesh(mesh, &error), "material fixture loads in modeller");
		editor->setAccessibility(scale == 200, true);
		if (scale == 200)
		{
			editor->setLayoutDirection(Qt::RightToLeft);
			editor->resize(1800, 1200);
		}
		editor->show();
		app.processEvents();
		auto *mode = editor->findChild<QComboBox *>(QStringLiteral("meshRenderMode"));
		auto *status = editor->findChild<QLabel *>(QStringLiteral("meshMaterialStatus"));
		auto *progress = editor->findChild<QProgressBar *>(QStringLiteral("meshMaterialProgress"));
		auto *reload = editor->findChild<QPushButton *>(QStringLiteral("reloadMeshMaterials"));
		auto *cancel = editor->findChild<QPushButton *>(QStringLiteral("cancelMeshMaterials"));
		auto *details = editor->findChild<QPushButton *>(QStringLiteral("meshMaterialDetails"));
		auto *uv = editor->findChild<QLabel *>(QStringLiteral("meshUvPreview"));
		auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
		preview->setShowGrid(false);
		preview->setShowAxes(false);
		preview->setOrbit(0, 80);
		editor->setMaterialSource({original, QStringLiteral("base"), {}});
		mode->setCurrentIndex(3);
		ok &= expect(editor->materialLoading() && progress->isVisible() && cancel->isEnabled() && !status->accessibleName().isEmpty(),
					 "material work exposes loading, progress, accessible status and Cancel");
		ok &= expect(settle(*editor) && colorPixels(uv->pixmap().toImage(), false) > 10000, "loaded material updates the UV background");
		ok &= expect(tests::settleModelViewport(*preview), "material raster finishes");
		QImage image(preview->size(), QImage::Format_RGB32);
		preview->render(&image);
		ok &= expect(colorPixels(materialPixels(*preview, image), false) > 500, "loaded material updates the 3D viewport");
		const auto fingerprint = editor->document().revisionFingerprint();
		editor->setMaterialSource({replacement, QStringLiteral("staged"), {}});
		ok &= expect(settle(*editor) && colorPixels(uv->pixmap().toImage(), true) > 10000,
					 "staged replacement updates UV without reopening mesh");
		ok &= expect(tests::settleModelViewport(*preview), "replacement raster finishes");
		// Progress/status layout can resize the viewport between these renders.
		image = QImage(preview->size(), QImage::Format_RGB32);
		image.fill(Qt::black);
		preview->render(&image);
		const auto replacementPixels = materialPixels(*preview, image);
		ok &= expect(colorPixels(replacementPixels, true) > 500 && colorPixels(replacementPixels, false) < 100, "staged image replaces stale 3D pixels");
		editor->setMaterialSource({deletion, QStringLiteral("deleted"), {}});
		ok &= expect(settle(*editor) && status->text().contains(QStringLiteral("0/1")) && colorPixels(uv->pixmap().toImage(), true) == 0,
					 "deleted image reports a problem and returns to checker");
		editor->setMaterialSource({original, QStringLiteral("undo"), {}});
		ok &= expect(settle(*editor) && colorPixels(uv->pixmap().toImage(), false) > 10000 &&
						 editor->document().revisionFingerprint() == fingerprint && !editor->document().isModified(),
					 "package undo refreshes materials without modifying the mesh document");
		reload->click();
		cancel->click();
		ok &= expect(status->text().contains(QStringLiteral("cancelled")) && !cancel->isEnabled() && !progress->isVisible(),
					 "Cancel exposes a persistent reload action");
		reload->click();
		ok &= expect(settle(*editor) && status->text().contains(QStringLiteral("1/1")), "Reload retries cancelled loading");
		bool detailShown = false;
		QTimer::singleShot(0, editor,
						   [&]
						   {
							   auto *dialog = editor->findChild<QDialog *>(QStringLiteral("meshMaterialDetailsDialog"));
							   if (dialog)
							   {
								   const auto *text = dialog->findChild<QPlainTextEdit *>();
								   detailShown = text && text->isReadOnly() &&
												 text->toPlainText().contains(QStringLiteral("models/skin.png")) &&
												 !text->accessibleName().isEmpty();
								   dialog->accept();
							   }
						   });
		details->click();
		ok &= expect(detailShown, "Details exposes read-only material diagnostics");
		app.processEvents();
		for (auto *button : {reload, cancel, details})
		{
			ok &= expect(button->focusPolicy() != Qt::NoFocus && button->width() >= button->minimumSizeHint().width() &&
							 editor->rect().contains(QRect(button->mapTo(editor, QPoint()), button->size())),
						 "material actions remain visible and focusable at scaled RTL expanded text");
		}
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty())
		{
			QDir().mkpath(evidence);
			ok &= expect(tests::settleModelViewport(*preview), "capture waits for current pixels");
			QImage capture(editor->size(), QImage::Format_RGB32);
			editor->render(&capture);
			ok &= expect(capture.save(QDir(evidence).filePath(QStringLiteral("mesh-materials-%1.png").arg(scale))),
						 "save widget render evidence");
		}
		editor->close();
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		if (scale == 200)
		{
			app.removeTranslator(&expansion);
		}
	}
	StudioSettings::setOverrideFilePath({});
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
