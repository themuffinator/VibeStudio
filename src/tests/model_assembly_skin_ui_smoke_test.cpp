#include "app/model_assembly_dialog.h"
#include "app/model_skin_source_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/model_assembly_test_helpers.h"
#include "tests/model_skin_binding_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRawFont>
#include <QScrollArea>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <array>
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
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
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
		if (QByteArray(context) != "ModelAssemblyDialog" && QByteArray(context) != "ModelSkinSourceDialog")
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
	// Owned widget APIs/signals and widget render targets; no OS input or capture.
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
	QTemporaryDir temporary(QDir(root).filePath("assembly-skin-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return temporary.filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	const auto model = QJsonDocument(editableModelJson(tests::skinBindingMesh())).toJson();
	const QByteArray skin("body,textures/red.png\nhead,textures/blue.png\n");
	const QByteArray alternate("body,textures/blue.png\nhead,textures/red.png\n");
	if (!write(path("part.mesh.json"), model) || !write(path("default.skin"), skin))
		return 1;
	auto base = tests::assemblyRecipe(path("part.mesh.json"));
	base.parts[1].tag = "tag_mount";
	base.parts[1].translation = {0, 112, 0};
	auto package = std::make_shared<tests::SkinReader>();
	package->add("skins/default.skin", skin);
	package->add("skins/default.skin", alternate);
	for (const auto &[name, color] : std::array<std::pair<QString, QColor>, 2>{
			 {{"textures/red.png", QColor(240, 70, 40)}, {"textures/blue.png", QColor(30, 140, 240)}}})
	{
		QImage texture(8, 8, QImage::Format_ARGB32);
		texture.fill(color);
		package->add(name, tests::skinPng(texture));
	}
	bool ok = expect(QRawFont::fromFont(app.font()).supportsCharacter('A'), "rendering has a usable text font");
	QString error;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelAssemblyDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.setAccessibility(scenario != 0, true);
		editor.resize(scenario ? 1400 : 1180, scenario ? 1000 : 760);
		editor.show();
		app.processEvents();
		editor.findChild<QCheckBox *>("assemblyRecoveryEnabled")->setChecked(false);
		editor.setMaterialSource({package, QStringLiteral("skin-fixture"), {}});
		ok &= expect(editor.setAssembly(base, temporary.path(), &error) && editor.previewReady(),
					 "ordinary assembly remains usable before linking skin");
		auto *kind = editor.findChild<QComboBox *>("assemblySkinKind");
		auto *source = editor.findChild<QLineEdit *>("assemblySkinSource");
		auto *index = editor.findChild<QSpinBox *>("assemblySkinIndex");
		auto *choose = editor.findChild<QPushButton *>("assemblyBrowseSkin");
		auto *apply = editor.findChild<QPushButton *>("assemblyApplyPart");
		auto *scroll = editor.findChild<QScrollArea *>("assemblyInspectorScroll");
		auto *details = editor.findChild<QPlainTextEdit *>("assemblyDependencyDetails");
		ok &= expect(kind && source && index && choose && apply && scroll && details, "linked skin controls have stable identities");
		if (!ok)
			return 1;
		ok &= expect(kind->currentIndex() == 0 && !source->isEnabled() && !index->isEnabled() && !choose->isEnabled(),
					 "ordinary mode disables irrelevant fields");
		kind->setCurrentIndex(1);
		source->setText(path("default.skin"));
		ok &= expect(source->isEnabled() && !index->isEnabled() && choose->isEnabled(), "file mode exposes only file inputs");
		for (QWidget *control : std::array<QWidget *, 4>{kind, source, index, choose})
		{
			const auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !control->accessibleDescription().isEmpty(),
						 "skin controls expose accessible names and descriptions");
			ok &= expect(control->focusPolicy() != Qt::NoFocus, "skin controls participate in normal focus navigation");
		}
		apply->click();
		ok &= expect(editor.document().assembly().parts[0].skin && editor.previewReady() &&
						 editor.pose().mesh.surfaces[0].skinPaths.first() == "textures/red.png",
					 "Apply Part resolves linked skin and replaces only the selected part materials");
		ok &= expect(editor.undo(&error) && !editor.document().assembly().parts[0].skin && !editor.document().canUndo() &&
						 editor.redo(&error),
					 "one-step GUI undo and redo retain linked skin selection");
		ok &= expect(details->toPlainText().contains("SHA-256") && details->toPlainText().contains("textures/red.png"),
					 "dependency details disclose input digest and material mapping");
		kind->setCurrentIndex(2);
		source->setText("skins/default.skin");
		index->setValue(1);
		package->readOnGui = false;
		apply->click();
		ok &= expect(editor.document().assembly().parts[0].skin->entryIndex == 1 &&
						 editor.pose().mesh.surfaces[0].skinPaths.first() == "textures/blue.png" && !package->readOnGui,
					 "exact package occurrence loads on the assembly worker");
		const auto before = modelAssemblyFingerprint(editor.document().assembly());
		auto changed = editor.document().assembly().parts[0];
		changed.skin->entryIndex = 0;
		package->delay = true;
		bool cancelledBusy = false;
		QTimer::singleShot(20, &editor, [&] {
			cancelledBusy = editor.operationBusy();
			editor.cancelOperation();
		});
		ok &= expect(!editor.applyPart("root", changed, &error) && cancelledBusy &&
						 modelAssemblyFingerprint(editor.document().assembly()) == before && editor.previewReady(),
					 "cancelling package skin read preserves recipe, history and preview");
		package->delay = false;
		changed.skin->source = "skins/missing.skin";
		ok &= expect(editor.applyPart("root", changed, &error) && !editor.previewReady() &&
						 !editor.document().assembly().parts[0].skin->source.isEmpty(),
					 "missing skin leaves a visible repairable reference and no stale preview");
		ok &=
			expect(editor.undo(&error) && editor.previewReady() && editor.pose().mesh.surfaces[0].skinPaths.first() == "textures/blue.png",
				   "undo repairs missing skin without touching model source");
		kind->setCurrentIndex(0);
		apply->click();
		ok &= expect(!editor.document().assembly().parts[0].skin && editor.pose().mesh.surfaces[0].skinPaths.first() == "models/old_body" &&
						 editor.undo(&error),
					 "Model materials removes the override and undo restores it");
		bool pickerCancelled = false;
		QTimer::singleShot(0, &editor, [&] {
			auto *picker = dynamic_cast<ModelSkinSourceDialog *>(QApplication::activeModalWidget());
			if (!picker)
				return;
			picker->resize(scenario ? 1100 : 800, scenario ? 700 : 500);
			app.processEvents();
			ok &= expect(render(*picker, QStringLiteral("skins-%1-picker").arg(scenario)), "capture metadata-only package skin picker");
			pickerCancelled = true;
			picker->reject();
		});
		choose->click();
		ok &= expect(pickerCancelled && modelAssemblyFingerprint(editor.document().assembly()) == before && index->value() == 1,
					 "cancelled package picker retains exact current skin");
		ModelAssemblyAnimation animation;
		ModelAssemblyAnimationOptions options;
		options.frameCount = 3;
		options.framesPerSecond = 4;
		ok &=
			expect(editor.bakeAnimation(options, &animation, &error) && animation.mesh.surfaces[0].skinPaths.first() == "textures/blue.png",
				   "GUI animation bake matches live skin assignments");
		auto *renderMode = editor.findChild<QComboBox *>("assemblyRenderMode");
		renderMode->setCurrentIndex(2);
		ModelMaterialWorker *worker = nullptr;
		for (auto *child : editor.children())
			if (auto *materialWorker = dynamic_cast<ModelMaterialWorker *>(child))
				worker = materialWorker;
		QElapsedTimer wait;
		wait.start();
		while (worker && worker->busy() && wait.elapsed() < 5000)
		{
			app.processEvents();
			QThread::msleep(1);
		}
		ok &= expect(worker && !worker->busy(), "linked material preview worker completes");
		ok &= expect(tests::settleModelViewport(*editor.findChild<ModelViewport *>("assemblyPreview")),
					 "material viewport finishes rendering");
		ok &=
			expect(editor.findChild<ModelViewport *>("assemblyPreview")->hasSkin(), "resolved material images reach the visible viewport");
		scroll->ensureWidgetVisible(kind);
		// Restore only the offscreen Qt window's activation after the modal picker.
		editor.activateWindow();
		app.processEvents();
		source->setFocus(Qt::OtherFocusReason);
		app.processEvents();
		ok &= expect(source->hasFocus() && source->layoutDirection() == Qt::LeftToRight && index->layoutDirection() == Qt::LeftToRight,
					 "skin path and index retain logical numeric direction and focus under RTL");
		ok &= expect(editor.width() <= (scenario ? 1400 : 1180) && render(editor, QStringLiteral("skins-%1-controls").arg(scenario)),
					 "skin inspector fits and renders at theme/text/device scale");
		editor.findChild<QAction *>("assemblyDetails")->trigger();
		app.processEvents();
		ok &=
			expect(tests::settleModelViewport(*editor.findChild<ModelViewport *>("assemblyPreview")), "details resize finishes rendering");
		ok &= expect(details->isVisible() && render(editor, QStringLiteral("skins-%1-details").arg(scenario)),
					 "skin receipts remain inspectable beside model preview");
		const auto saved = temporary.filePath(QStringLiteral("gui-%1.assembly.json").arg(scenario));
		ok &= expect(editor.saveSource(saved, false, &error) && editor.openSource(saved, &error) &&
						 editor.document().assembly().parts[0].skin->entryIndex == 1 && editor.previewReady(),
					 "GUI save/reopen retains exact package skin reference");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << checks << " assembly skin UI checks\n";
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	return ok ? 0 : 1;
}
