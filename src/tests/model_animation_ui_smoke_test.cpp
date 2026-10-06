#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_animation_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
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
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
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
	// Public widget values/signals and QWidget::render; no injected input or OS capture.
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
	QTemporaryDir temporary(QDir(root).filePath("mesh-animation-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	auto original = tests::animationFixture();
	original.animations[2].name = original.animations[0].name;
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
		ok &= expect(editor.setMesh(original, &error), "open animation UI fixture");
		editor.resize(scenario == 0 ? 1320 : 2080, scenario == 0 ? 920 : 1360);
		editor.show();
		app.processEvents();
		auto *clip = editor.findChild<QComboBox *>("meshAnimationClip");
		auto *frame = editor.findChild<QComboBox *>("meshFrame");
		auto *name = editor.findChild<QLineEdit *>("meshAnimationName");
		auto *prefix = editor.findChild<QLineEdit *>("meshInbetweenPrefix");
		auto *first = editor.findChild<QSpinBox *>("meshAnimationFirst");
		auto *last = editor.findChild<QSpinBox *>("meshAnimationLast");
		auto *rate = editor.findChild<QDoubleSpinBox *>("meshAnimationRate");
		auto *savedRate = editor.findChild<QDoubleSpinBox *>("meshClipRate");
		auto *applyRate = editor.findChild<QPushButton *>("setMeshAnimationRate");
		auto *smooth = editor.findChild<QCheckBox *>("meshAnimationSmooth");
		auto *source = editor.findChild<QSpinBox *>("meshCopyPoseSource");
		auto *count = editor.findChild<QSpinBox *>("meshInbetweenCount");
		auto *add = editor.findChild<QPushButton *>("addMeshAnimation");
		auto *rename = editor.findChild<QPushButton *>("renameMeshAnimation");
		auto *range = editor.findChild<QPushButton *>("setMeshAnimationRange");
		auto *remove = editor.findChild<QPushButton *>("deleteMeshAnimation");
		auto *copy = editor.findChild<QPushButton *>("copyMeshFramePose");
		auto *insert = editor.findChild<QPushButton *>("insertMeshInbetweens");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		if (!expect(clip && frame && name && prefix && first && last && rate && source && count && add && rename && range && remove &&
						copy && insert && tabs && preview && undo && redo && smooth && savedRate && applyRate,
					"animation controls exist"))
		{
			return EXIT_FAILURE;
		}
		for (int i = 0; i < tabs->count(); ++i)
		{
			if (tabs->widget(i)->isAncestorOf(clip))
			{
				tabs->setCurrentIndex(i);
			}
		}
		const QList<QWidget *> controls{clip,  name, prefix, first, last,	rate, smooth, source,
										count, add,	 rename, range, remove, copy, insert, savedRate, applyRate};
		for (auto *control : controls)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !control->accessibleDescription().isEmpty() &&
							 control->focusPolicy() != Qt::NoFocus,
						 "animation controls expose accessible names, descriptions and focus");
		}
		ok &= expect(clip->currentData().toInt() == -1 && !rename->isEnabled() && !range->isEnabled() && !remove->isEnabled() &&
						 !copy->isEnabled(),
					 "all frames is read-only clip selection; self-copy is disabled");
		clip->setCurrentIndex(clip->findData(2));
		ok &= expect(preview->animationIndex() == 2 && frame->currentIndex() == 1 && clip->currentData().toInt() == 2 &&
						 clip->itemText(clip->findData(2)) != clip->itemText(clip->findData(0)),
					 "indices distinguish duplicate imported names");
		preview->stepFrame(1);
		ok &= expect(preview->frame() == 1 && preview->animationIndex() == 2, "single-pose clip stepping stays inside its range");
		clip->setCurrentIndex(clip->findData(0));
		preview->stepFrame(-1);
		ok &= expect(frame->currentIndex() == 1 && preview->animationIndex() == 0, "clip preview wraps backward");
		preview->stepFrame(1);
		ok &=
			expect(frame->currentIndex() == 0 && preview->animationIndex() == 0, "clip preview wraps forward and survives editor refresh");
		rate->setValue(24);
		ok &= expect(smooth->isChecked() && preview->animationInterpolation(), "editor starts with session-only smooth preview");
		smooth->setChecked(false);
		ok &= expect(!preview->animationInterpolation() && !editor.document().isModified() && !editor.document().canUndo(),
					 "stored-frame preview changes no document or history");
		smooth->setChecked(true);
		preview->play();
		ok &= expect(!preview->isPlaying() && preview->framesPerSecond() == 24 && !editor.document().isModified() &&
						 !editor.document().canUndo(),
					 "preview speed is session-only and reduced motion prevents playback");
		editor.setAccessibility(scenario > 0, false);
		preview->play();
		ok &= expect(preview->isPlaying() && !insert->isEnabled() && !copy->isEnabled() && !add->isEnabled() && !rename->isEnabled() &&
						 !remove->isEnabled(),
					 "playback disables pose and clip mutation");
		preview->pause();
		ok &= expect(preview->animationSample().fraction == 0 && smooth->isChecked() && preview->animationInterpolation(),
					 "pause restores exact pose while retaining smooth setting across editor refresh");
		editor.setAccessibility(scenario > 0, true);
		frame->setCurrentIndex(2);
		ok &= expect(clip->currentData().toInt() == -1 && preview->animationIndex() == -1 && !insert->isEnabled(),
					 "jump outside clip restores all frames; last pose cannot insert");
		ok &= expect(!preview->setAnimationIndex(99) && preview->animationIndex() == -1,
					 "invalid clip selection leaves viewport state untouched");
		name->setText("authored");
		first->setValue(0);
		last->setValue(1);
		add->click();
		ok &= expect(editor.document().mesh().animations.size() == 6 && clip->currentData().toInt() == 5 && preview->frame() == 0,
					 "adding a clip selects its range for preview");
		name->setText("renamed");
		rename->click();
		ok &= expect(editor.document().mesh().animations[5].name == "renamed" && preview->animationIndex() == 5,
					 "rename retains selected clip");
		first->setValue(2);
		last->setValue(2);
		range->click();
		ok &= expect(editor.document().mesh().animations[5].firstFrame == 2 && editor.document().mesh().animations[5].frameCount == 1 &&
						 clip->currentData().toInt() == 5 && preview->frame() == 2,
					 "applying disjoint range keeps edited clip selected at its new first pose");
		remove->click();
		ok &= expect(bytes(editor.document().mesh()) == bytes(original) && clip->currentData().toInt() == -1,
					 "deleting clip preserves all poses and original clips");
		clip->setCurrentIndex(clip->findData(0));
		count->setValue(3);
		prefix->setText("transition");
		insert->click();
		ok &= expect(editor.document().mesh().frameCount == 6 && preview->frame() == 1 && clip->currentData().toInt() == 0 &&
						 editor.document().mesh().animations[0].frameCount == 5 &&
						 editor.document().mesh().frames[1].name == "transition_001",
					 "insert selects first generated frame and extends spanning clip");
		const auto inserted = bytes(editor.document().mesh());
		source->setValue(5);
		copy->click();
		ok &= expect(editor.document().mesh().surfaces[1].frames[1].positions[0].z == 16 &&
						 editor.document().mesh().frames[1].name == "transition_001" && preview->frame() == 1 &&
						 preview->animationIndex() == 0,
					 "copy full pose edits displayed frame across all surfaces");
		undo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == inserted && preview->animationIndex() == 0,
					 "undo copy restores destination without losing preview range");
		undo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == bytes(original), "undo insertion restores every original pose");
		redo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == inserted && count->maximum() == 1018 && source->maximum() == 5,
					 "redo refreshes frame count and capacity controls");
		ok &= expect(tests::settleModelViewport(*preview), "generated pose renders");
		app.processEvents();
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
		{
			ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "animation UI uses actual 2x device pixel ratio");
		}
		QScrollArea *inspector = nullptr;
		for (auto *area : editor.findChildren<QScrollArea *>())
		{
			if (area->isAncestorOf(clip))
			{
				inspector = area;
			}
		}
		ok &= expect(inspector, "animation inspector scroll area exists");
		if (inspector)
		{
			if (inspector->horizontalScrollBar()->maximum() != 0)
			{
				std::cerr << "Animation layout scenario=" << scenario << " overflow=" << inspector->horizontalScrollBar()->maximum()
						  << " viewport=" << inspector->viewport()->width() << " minimum=" << inspector->widget()->minimumSizeHint().width()
						  << '\n';
			}
			ok &= expect(inspector->horizontalScrollBar()->maximum() == 0, "animation inspector fits without horizontal overflow");
			for (auto *control : controls)
			{
				// Scroll the complete control into view. ensureWidgetVisible can
				// stop when only a spin box's text caret is already visible.
				const auto center = control->mapTo(inspector->widget(), control->rect().center());
				inspector->verticalScrollBar()->setValue(center.y() - inspector->viewport()->height() / 2);
				app.processEvents();
				const auto bounds = QRect(control->mapTo(inspector->viewport(), QPoint()), control->size());
				if (bounds.left() < 0 || bounds.right() >= inspector->viewport()->width() || bounds.top() < 0 ||
					bounds.bottom() >= inspector->viewport()->height())
				{
					std::cerr << "Animation control scenario=" << scenario << " name=" << control->objectName().toStdString()
							  << " bounds=" << bounds.x() << ',' << bounds.y() << ',' << bounds.width() << ',' << bounds.height()
							  << " viewport=" << inspector->viewport()->width() << ',' << inspector->viewport()->height() << '\n';
				}
				ok &= expect(bounds.left() >= 0 && bounds.right() < inspector->viewport()->width() && bounds.top() >= 0 &&
								 bounds.bottom() < inspector->viewport()->height(),
							 "each animation control remains reachable and fits inspector");
			}
		}
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty() && inspector)
		{
			for (int part = 0; part < 2; ++part)
			{
				inspector->ensureWidgetVisible(part == 0 ? static_cast<QWidget *>(clip) : static_cast<QWidget *>(insert));
				app.processEvents();
				QImage image(editor.size() * editor.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
				image.setDevicePixelRatio(editor.devicePixelRatioF());
				image.fill(Qt::transparent);
				editor.render(&image);
				ok &= expect(image.save(QDir(evidence).filePath(
								 QString("mesh-animation-%1-%2-%3x.png").arg(scenario).arg(part).arg(editor.devicePixelRatioF()))),
							 "save animation widget render");
			}
		}
		ModelEdit cancelled;
		cancelled.kind = ModelEditKind::InsertInbetweens;
		cancelled.frame = 0;
		cancelled.inbetweenCount = 100;
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.applyEdit(cancelled, &error) && bytes(editor.document().mesh()) == inserted,
					 "cancel animation worker without changing the visible document");
		ok &= expect(editor.setMesh(original, &error), "retire animation test recovery");
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
