#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
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
QByteArray source(const ModelEditorDialog &editor)
{
	return QJsonDocument(editableModelJson(editor.document().mesh())).toJson(QJsonDocument::Compact);
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
QImage render(QWidget &widget)
{
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image;
}
bool settlePreview(QApplication &app, ModelEditorDialog &editor, ModelUvView &uv)
{
	QElapsedTimer deadline;
	deadline.start();
	while (deadline.elapsed() < 20000)
	{
		// Material completion hides its progress bar. Qt can still have a parent
		// layout request queued after the UV raster's completion signal returns.
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		app.processEvents(QEventLoop::AllEvents, 10);
		if (!editor.materialLoading() && tests::settleModelUv(uv, qMax(1, 20000 - int(deadline.elapsed()))))
		{
			const auto size = uv.size();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
			app.processEvents(QEventLoop::AllEvents, 10);
			if (!editor.materialLoading() && !uv.isRendering() && uv.size() == size)
			{
				return true;
			}
		}
		QThread::msleep(1);
	}
	return false;
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
	// Semantic widget APIs and QWidget::render only: no input injection or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-mdl-ui-smoke"); skip >= 0) {
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
	QTemporaryDir temporary(QDir(root).filePath("mesh-mdl-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	const auto skinPath = QDir(temporary.path()).filePath("skin.lmp");
	QByteArray lump;
	tests::mdlInteger(lump, 8);
	tests::mdlInteger(lump, 4);
	lump += QByteArray(32, char(175));
	QFile skinFile(skinPath);
	if (!skinFile.open(QIODevice::WriteOnly) || skinFile.write(lump) != lump.size())
	{
		return EXIT_FAILURE;
	}
	skinFile.close();
	const auto mesh = decodeModelMesh("fixture.mdl", tests::groupedMdlFixture().bytes);
	bool ok = true;
	QString error;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		ModelEditorDialog editor;
		editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(mesh, &error), "MDL opens in the ordinary mesh editor");
		editor.resize(scenario == 0 ? 1320 : 2080, scenario == 0 ? 920 : 1360);
		editor.show();
		app.processEvents();
		const auto requiredScale = qEnvironmentVariableIntValue("QT_SCALE_FACTOR");
		ok &= expect(requiredScale < 2 || editor.devicePixelRatioF() >= 1.99, "requested 2x test actually uses a 2x device pixel ratio");
		auto *slot = editor.findChild<QComboBox *>("meshMdlSkin"), *member = editor.findChild<QComboBox *>("meshMdlMember");
		auto *group = editor.findChild<QComboBox *>("meshMdlGroup");
		auto *flags = editor.findChild<QLineEdit *>("meshMdlFlags");
		auto *eye = editor.findChild<QLineEdit *>("meshMdlEye");
		auto *size = editor.findChild<QLineEdit *>("meshMdlSize");
		auto *first = editor.findChild<QSpinBox *>("meshMdlFirst"), *last = editor.findChild<QSpinBox *>("meshMdlLast");
		auto *duration = editor.findChild<QDoubleSpinBox *>("meshMdlPoseDuration");
		auto *preview = editor.findChild<QPushButton *>("previewMeshMdlMember");
		auto *header = editor.findChild<QPushButton *>("applyMeshMdlHeader");
		auto *groupButton = editor.findChild<QPushButton *>("groupMeshMdlFrames");
		auto *timing = editor.findChild<QComboBox *>("meshMdlPlaybackTiming");
		auto *seekTime = editor.findChild<QDoubleSpinBox *>("meshMdlPlaybackTime"),
			 *phase = editor.findChild<QDoubleSpinBox *>("meshMdlSyncPhase");
		auto *seek = editor.findChild<QPushButton *>("seekMeshMdlTiming"),
			 *nativePreview = editor.findChild<QPushButton *>("previewMeshMdlTiming");
		auto *clipTiming = editor.findChild<QPushButton *>("clearMeshMdlTiming");
		auto *nativeStatus = editor.findChild<QLabel *>("meshMdlPlaybackStatus");
		auto *undo = editor.findChild<QAction *>("undoMesh"), *redo = editor.findChild<QAction *>("redoMesh");
		auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
		auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		if (!expect(slot && member && group && flags && eye && size && first && last && duration && preview && header && groupButton &&
						undo && redo && viewport && uv && tabs && timing && seekTime && phase && seek && nativePreview && clipTiming &&
						nativeStatus && editor.findChild<QAction *>("exportMeshMdl"),
					"MDL native controls and export action exist"))
		{
			return EXIT_FAILURE;
		}
		for (int i = 0; i < tabs->count(); ++i)
		{
			if (tabs->widget(i)->isAncestorOf(slot))
			{
				tabs->setCurrentIndex(i);
			}
		}
		const auto original = source(editor);
		ok &= expect(slot->count() == 2 && member->count() == 2 && group->count() == 2 && flags->text() == QString::number(mesh.mdl.flags),
					 "native settings populate without truncating flags or members");
		flags->setText("0xffffffff");
		eye->setText("-1,2,3");
		size->setText("2.5");
		header->click();
		ok &= expect(editor.document().mesh().mdl.flags == 0xffffffffu && editor.document().mesh().mdl.eyePosition.x == -1,
					 "header button applies validated native metadata");
		undo->trigger();
		ok &= expect(source(editor) == original, "native header edit has complete undo");
		redo->trigger();
		undo->trigger();
		first->setValue(0);
		last->setValue(2);
		duration->setValue(.25);
		groupButton->click();
		ok &= expect(editor.document().mesh().mdl.frameGroups.size() == 1 &&
						 editor.document().mesh().mdl.frameGroups[0].intervals == QVector<float>{.25f, .5f, .75f},
					 "native range controls author timed groups");
		undo->trigger();
		ok &= expect(editor.importMdlSkin(skinPath, ModelEditKind::AddMdlSkin, &error) && slot->count() == 3 && slot->currentIndex() == 2,
					 "worker imports an exact indexed skin and selects the new slot");
		ok &= expect(editor.importMdlSkin(skinPath, ModelEditKind::AppendMdlSkinMember, &error) && member->count() == 2 &&
						 member->currentIndex() == 1,
					 "worker appends an animated member and selects it");
		const auto imported = source(editor);
		preview->click();
		ok &= expect(tests::settleModelViewport(*viewport), "selected skin raster completes");
		const auto selected = render(*viewport);
		slot->setCurrentIndex(0);
		member->setCurrentIndex(0);
		preview->click();
		ok &= expect(tests::settleModelViewport(*viewport) && render(*viewport) != selected && source(editor) == imported,
					 "native member preview changes the rendered texture without modifying source or history");
		seekTime->setValue(.23);
		seek->click();
		ok &= expect(viewport->mdlPlaybackActive() && !viewport->isPlaying() && viewport->frame() == 1 &&
						 viewport->mdlPlaybackSample().skinMember == 1 && source(editor) == imported &&
						 !editor.findChild<QDoubleSpinBox *>("meshAnimationRate")->isEnabled(),
					 "native seek uses stored timing and leaves editing source unchanged");
		const auto skinKey = viewport->mdlPlaybackSkins().value(0).cacheKey();
		seekTime->setValue(.26);
		seek->click();
		ok &= expect(viewport->mdlPlaybackActive() && viewport->mdlPlaybackSample().skinMember == 0 &&
						 viewport->mdlPlaybackSkins().value(0).cacheKey() == skinKey,
					 "repeated seek reuses prepared images through paused editor refresh");
		if (auto *timer = viewport->findChild<QTimer *>("modelPlaybackTimer"))
		{
			QSignalBlocker blocked(timer);
			QTabWidget *views = nullptr;
			int previousView = 0;
			for (auto *candidate : editor.findChildren<QTabWidget *>())
			{
				for (int i = 0; i < candidate->count(); ++i)
				{
					if (candidate->widget(i)->isAncestorOf(uv))
					{
						views = candidate;
						previousView = views->currentIndex();
						views->setCurrentIndex(i);
					}
				}
			}
			editor.setAccessibility(scenario > 0, false);
			ok &= expect(nativeStatus->text() == viewport->playbackSummary(), "native status immediately reflects reduced-motion changes");
			viewport->play();
			ok &= expect(viewport->isPlaying() && nativeStatus->text() == viewport->playbackSummary(),
						 "ordinary transport starts native timing and immediately displays playing state");
			ok &= expect(views && settlePreview(app, editor, *uv), "visible native UV preview and material layout finish");
			const auto previousTexture = uv->pixmap().toImage();
			const auto camera = uv->uvToScreen({.5f, .5f});
			const auto cameraOrigin = uv->uvToScreen({0, 0});
			const auto viewSize = uv->size();
			ok &= expect(viewport->seekAnimation(.13) && tests::settleModelUv(*uv), "skin-only native seek completes the UV raster");
			ok &= expect(uv->pixmap().toImage() != previousTexture, "skin-only playback changes UV pixels");
			const auto afterCamera = uv->uvToScreen({.5f, .5f});
			ok &= expect(uv->size() == viewSize, "skin-only comparison keeps a stable viewport size");
			ok &= expect(afterCamera == camera && uv->uvToScreen({0, 0}) == cameraOrigin,
						 "skin-only playback preserves UV camera position and scale");
			ok &= expect(source(editor) == imported, "skin-only playback preserves source");
			if (afterCamera != camera)
			{
				std::cerr << "UV scenario " << scenario << " camera " << camera.x() << ',' << camera.y() << " -> " << afterCamera.x() << ','
						  << afterCamera.y() << " size " << viewSize.width() << 'x' << viewSize.height() << " -> " << uv->width() << 'x'
						  << uv->height() << '\n';
			}
			const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
			if (!evidence.isEmpty())
			{
				ok &= expect(render(editor).save(QDir(evidence).filePath(
								 QString("native-mdl-uv-%1-%2x.png").arg(scenario).arg(editor.devicePixelRatioF()))),
							 "save native UV playback widget render");
			}
			viewport->pause();
			ok &= expect(viewport->mdlPlaybackActive() && viewport->mdlPlaybackSkins().value(0).cacheKey() == skinKey &&
							 nativeStatus->text() == viewport->playbackSummary(),
						 "pause refresh retains native time, status and prepared textures");
			editor.setAccessibility(scenario > 0, true);
			ok &= expect(nativeStatus->text() == viewport->playbackSummary(), "native paused status immediately reflects reduced motion");
			if (views)
			{
				views->setCurrentIndex(previousView);
			}
		}
		timing->setCurrentIndex(1);
		seekTime->setValue(.23);
		nativePreview->click();
		ok &= expect(viewport->mdlPlaybackActive() && !viewport->isPlaying() && viewport->frame() == 0 &&
						 viewport->mdlPlaybackSample().skinMember == 0 && !phase->isEnabled(),
					 "GLQuake preview is discrete and reduced motion prevents automatic play");
		clipTiming->click();
		ok &= expect(!viewport->mdlPlaybackActive() && editor.findChild<QDoubleSpinBox *>("meshAnimationRate")->isEnabled() &&
						 source(editor) == imported,
					 "clip timing restores the FPS controls without changing source");
		timing->setCurrentIndex(0);
		seekTime->setValue(.23);
		seek->click();
		const QList<QWidget *> controls{slot,	 member, group,		  flags,  eye,		size,  first, last,			 duration,
										preview, header, groupButton, timing, seekTime, phase, seek,  nativePreview, clipTiming};
		QScrollArea *inspector = nullptr;
		for (auto *area : editor.findChildren<QScrollArea *>())
		{
			if (area->isAncestorOf(slot))
			{
				inspector = area;
			}
		}
		ok &= expect(inspector, "MDL inspector has a scroll surface");
		if (inspector)
		{
			ok &= expect(inspector->horizontalScrollBar()->maximum() == 0, "MDL controls fit at normal and expanded translated scale");
			for (auto *control : controls)
			{
				const auto *accessible = QAccessible::queryAccessibleInterface(control);
				ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !control->accessibleDescription().isEmpty() &&
								 control->focusPolicy() != Qt::NoFocus,
							 "native controls expose names, descriptions and keyboard focus");
				inspector->ensureWidgetVisible(control);
				app.processEvents();
				const auto bounds = QRect(control->mapTo(inspector->viewport(), QPoint()), control->size());
				ok &= expect(bounds.left() >= 0 && bounds.right() < inspector->viewport()->width() && bounds.top() >= 0 &&
								 bounds.bottom() < inspector->viewport()->height(),
							 "native controls remain reachable without clipping");
			}
			const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
			if (!evidence.isEmpty())
			{
				for (int part = 0; part < 3; ++part)
				{
					inspector->ensureWidgetVisible(part == 0   ? static_cast<QWidget *>(slot)
												   : part == 1 ? static_cast<QWidget *>(flags)
															   : seekTime);
					app.processEvents();
					ok &= expect(render(editor).save(QDir(evidence).filePath(
									 QString("mesh-mdl-%1-%2-%3x.png").arg(scenario).arg(part).arg(editor.devicePixelRatioF()))),
								 "save MDL widget render evidence");
				}
			}
		}
		flags->setText("7");
		header->click();
		ok &= expect(!viewport->mdlPlaybackActive() && viewport->mdlPlaybackSkins().isEmpty(),
					 "document edits retire the prepared native playback cache");
		undo->trigger();
		ok &= expect(source(editor) == imported, "native preview adds no hidden history entries around an ordinary edit");
		ok &= expect(editor.setMesh(mesh, &error), "retire test edits and recovery");
		if (scenario > 0)
		{
			app.removeTranslator(&expansion);
		}
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
