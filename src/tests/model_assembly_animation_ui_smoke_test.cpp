#include "app/model_assembly_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/model_assembly_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
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
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) != "ModelAssemblyDialog" && QByteArray(context) != "VibeStudioModelEditor" &&
			QByteArray(context) != "VibeStudioModelAssemblyAnimation")
			return {};
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
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
} // namespace
int main(int argc, char **argv)
{
	// All events and renders belong to these test widgets; no OS input or capture.
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
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("assembly-animation-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	QFile source(path("part.mesh.json"));
	if (!source.open(QIODevice::WriteOnly) || source.write(QJsonDocument(editableModelJson(tests::assemblyModel())).toJson()) <= 0)
		return 1;
	source.close();
	bool ok = true;
	QString error;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelAssemblyDialog assembly;
		assembly.setAttribute(Qt::WA_DeleteOnClose, false);
		assembly.setAccessibility(scenario > 0, true);
		ok &= expect(assembly.setAssembly(tests::assemblyRecipe(path("part.mesh.json")), temporary.path(), &error), "open linked assembly");
		assembly.show();
		const auto recipeHash = modelAssemblyFingerprint(assembly.document().assembly());
		const ModelAssemblyAnimationOptions options{.25, 5, 2.5, QStringLiteral("composed")};
		ModelAssemblyAnimation animation;
		bool busy = false, locked = false;
		QTimer::singleShot(0, &assembly, [&] {
			busy = assembly.operationBusy();
			locked = !assembly.findChild<QWidget *>("assemblyToolbar")->isEnabled();
		});
		ok &= expect(assembly.bakeAnimation(options, &animation, &error) && busy && locked && !assembly.operationBusy(),
					 "worker publishes busy state and locks actions until the full bake completes");
		const auto baked = QJsonDocument(editableModelJson(animation.mesh)).toJson();
		QTimer::singleShot(0, &assembly, [&] { assembly.cancelOperation(); });
		ok &= expect(!assembly.bakeAnimation(options, &animation, &error) &&
						 QJsonDocument(editableModelJson(animation.mesh)).toJson() == baked &&
						 modelAssemblyFingerprint(assembly.document().assembly()) == recipeHash && assembly.previewReady(),
					 "cancelled GUI bake retains caller result, recipe and preview");
		ok &= expect(assembly.exportAnimation(options, path("gui.mesh.json"), scenario > 0, &error),
					 "GUI animated export uses guarded shared writer");
		ok &= expect(!assembly.exportAnimation(options, path("part.mesh.json"), true, &error), "GUI protects linked model input");
		ModelEditorDialog editor;
		editor.setAccessibility(scenario > 0, true);
		int handoffs = 0;
		assembly.editBakedPose = [&](const ModelMesh &mesh, QString *failure) {
			++handoffs;
			return editor.setMesh(mesh, failure);
		};
		bool reviewed = false;
		QTimer::singleShot(80, &assembly, [&] {
			auto *dialog = assembly.findChild<QDialog *>("assemblyAnimationOptions");
			if (!dialog)
			{
				ok &= expect(false, "animation review opens");
				return;
			}
			dialog->resize(scenario == 0 ? 740 : 1250, scenario == 0 ? 800 : 1350);
			auto *start = dialog->findChild<QDoubleSpinBox *>("assemblyAnimationStart");
			auto *frames = dialog->findChild<QSpinBox *>("assemblyAnimationFrames");
			auto *fps = dialog->findChild<QDoubleSpinBox *>("assemblyAnimationFps");
			auto *name = dialog->findChild<QLineEdit *>("assemblyAnimationName");
			if (!start || !frames || !fps || !name)
			{
				ok &= expect(false, "review has settings");
				dialog->reject();
				return;
			}
			start->setValue(.25);
			frames->setValue(5);
			fps->setValue(2.5);
			name->setText("composed");
			fps->setFocus(Qt::OtherFocusReason);
			app.processEvents();
			auto *accessible = QAccessible::queryAccessibleInterface(fps);
			ok &= expect(accessible && accessible->role() == QAccessible::SpinBox && !accessible->text(QAccessible::Name).isEmpty() &&
							 !fps->accessibleDescription().isEmpty() && fps->hasFocus(),
						 "sampling field exposes role, name, help and keyboard focus");
			auto *notes = dialog->findChild<QPlainTextEdit *>("assemblyAnimationSummary");
			ok &= expect(notes && notes->isReadOnly() && notes->focusPolicy() != Qt::NoFocus &&
							 notes->horizontalScrollBar()->maximum() == 0 && !notes->accessibleDescription().isEmpty(),
						 "long translated review notes wrap without horizontal clipping and remain keyboard readable");
			auto *accept = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
			start->setValue(1000000);
			ok &= expect(!accept->isEnabled(), "invalid last-sample time disables publication");
			start->setValue(.25);
			app.processEvents();
			ok &= expect(accept->isEnabled() && render(*dialog, QStringLiteral("assembly-animation-options-%1").arg(scenario)),
						 "render reviewed sample range and omissions");
			reviewed = true;
			dialog->accept();
		});
		assembly.findChild<QAction *>("assemblyBakeAnimation")->trigger();
		ok &= expect(reviewed && handoffs == 1 && QJsonDocument(editableModelJson(editor.document().mesh())).toJson() == baked,
					 "reviewed bake opens exact full sequence in normal mesh authoring");
		editor.resize(scenario == 0 ? 1400 : 2250, scenario == 0 ? 1000 : 1500);
		editor.show();
		app.processEvents();
		auto *clip = editor.findChild<QComboBox *>("meshAnimationClip");
		auto *rate = editor.findChild<QDoubleSpinBox *>("meshAnimationRate");
		auto *saved = editor.findChild<QDoubleSpinBox *>("meshClipRate");
		auto *apply = editor.findChild<QPushButton *>("setMeshAnimationRate");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		if (!expect(clip && rate && saved && apply && preview, "mesh clip timing controls exist"))
			return 1;
		clip->setCurrentIndex(clip->findData(0));
		ok &= expect(preview->framesPerSecond() == 2.5 && saved->value() == 2.5 && rate->value() == 2.5 && !preview->isPlaying(),
					 "saved fractional rate enters reduced-motion preview without autoplay");
		ok &= expect(preview->seekAnimation(.8) && preview->frame() == 2, "preview timebase matches bake sample interval");
		rate->setValue(7.5);
		ok &= expect(preview->seekAnimation(.4) && preview->frame() == 3 && preview->framesPerSecond() == 7.5 &&
						 editor.document().mesh().animations[0].framesPerSecond == 2.5,
					 "session FPS override survives frame refresh without editing saved timing");
		clip->setCurrentIndex(clip->findData(-1));
		clip->setCurrentIndex(clip->findData(0));
		ok &= expect(preview->framesPerSecond() == 2.5, "reselecting clip restores authored rate");
		for (auto *tabs : editor.findChildren<QTabWidget *>())
			for (int i = 0; i < tabs->count(); ++i)
				if (tabs->widget(i)->isAncestorOf(saved))
					tabs->setCurrentIndex(i);
		for (auto *parent = saved->parentWidget(); parent; parent = parent->parentWidget())
			if (auto *scroll = qobject_cast<QScrollArea *>(parent))
				scroll->ensureWidgetVisible(saved);
		saved->setValue(23.976);
		apply->click();
		ok &= expect(editor.document().mesh().animations[0].framesPerSecond == 23.976 && preview->framesPerSecond() == 23.976,
					 "Apply Clip FPS updates durable metadata and preview together");
		editor.findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(editor.document().mesh().animations[0].framesPerSecond == 2.5 && preview->framesPerSecond() == 2.5,
					 "undo restores timing in source and viewport");
		editor.findChild<QAction *>("redoMesh")->trigger();
		saved->setFocus(Qt::OtherFocusReason);
		app.processEvents();
		ok &= expect(tests::settleModelViewport(*preview) && render(editor, QStringLiteral("assembly-animation-editor-%1").arg(scenario)),
					 "render baked animation with saved timing controls");
		ok &= expect(modelAssemblyFingerprint(assembly.document().assembly()) == recipeHash,
					 "mesh handoff and timing edits preserve assembly");
		editor.hide();
		assembly.hide();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " assembly animation GUI checks\n";
	return ok ? 0 : 1;
}
