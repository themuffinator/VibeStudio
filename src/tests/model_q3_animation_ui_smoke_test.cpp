#include "app/model_assembly_dialog.h"
#include "app/model_q3_animation_dialog.h"
#include "app/studio_theme.h"
#include "tests/model_q3_animation_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QRawFont>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>

#include <cmath>
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
		if (QByteArray(context) != "ModelQ3AnimationDialog" && QByteArray(context) != "ModelAssemblyDialog")
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
	// Own widget APIs, signals and render targets only; no OS input or capture.
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
	QTemporaryDir temporary(QDir(root).filePath("q3-animation-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	const auto modelBytes = QJsonDocument(editableModelJson(tests::q3Model())).toJson();
	if (!tests::q3Write(path("part.mesh.json"), modelBytes))
		return 1;
	const auto base = tests::q3Assembly(path("part.mesh.json"));
	ModelAssemblyResolved resolved;
	QString error;
	if (!resolveModelAssembly(base, {temporary.path(), {}, {}}, &resolved, &error))
		return 1;
	bool ok = true;
	ok &= expect(QRawFont::fromFont(app.font()).supportsCharacter('A'), "owned renderer has a usable text font");
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
		ModelQ3AnimationDialog dialog(base, resolved);
		dialog.resize(scenario ? 1250 : 1050, scenario ? 1000 : 720);
		dialog.show();
		app.processEvents();
		ok &= expect(dialog.width() <= (scenario ? 1250 : 1050), "expanded text does not force the dialog wider than the chosen viewport");
		ok &= expect(dialog.validateDraft(&error), "all imported native slots fit bound models");
		auto *tree = dialog.findChild<QTreeWidget *>("q3Slots");
		auto *first = dialog.findChild<QSpinBox *>("q3First");
		auto *count = dialog.findChild<QSpinBox *>("q3Count");
		auto *loop = dialog.findChild<QSpinBox *>("q3Loop");
		auto *fps = dialog.findChild<QDoubleSpinBox *>("q3Fps");
		auto *reverse = dialog.findChild<QCheckBox *>("q3Reverse");
		auto *sourceClip = dialog.findChild<QComboBox *>("q3SourceClip");
		ok &= expect(tree && first && count && loop && fps && reverse && sourceClip, "native editor controls are discoverable");
		if (!ok)
			return 1;
		tree->setCurrentItem(tree->topLevelItem(15));
		ok &= expect(first->value() == 104 && dialog.findChild<QLabel *>("q3ModelRange")->text().contains("4–8"),
					 "native and adjusted model ranges shown together");
		reverse->setChecked(true);
		loop->setValue(1);
		fps->setValue(15);
		const auto changed = dialog.animation();
		ok &= expect(changed && changed->config.clips[15].reversed && changed->config.clips[15].loopFrames == 1 &&
						 changed->config.clips[15].framesPerSecond == 15,
					 "fields edit selected native slot and retain independent playback settings");
		first->setValue(1000);
		ok &= expect(!dialog.validateDraft(&error) && !error.isEmpty(), "out of model range visible before apply");
		first->setValue(104);
		loop->setValue(6);
		ok &= expect(!dialog.validateDraft(&error), "loop tail cannot exceed count");
		loop->setValue(2);
		dialog.findChild<QPushButton *>("q3UseClip")->click();
		ok &= expect(first->value() == 104 && count->value() == 5 && fps->value() == 15 && reverse->isChecked(),
					 "model clip assignment carries local range and saved rate without changing reverse choice");
		tree->setCurrentItem(tree->topLevelItem(13));
		dialog.findChild<QPushButton *>("q3UseClip")->click();
		ok &= expect(first->value() == 102 && dialog.findChild<QLabel *>("q3Status")->text().contains("TORSO_GESTURE"),
					 "incompatible lower anchor assignment rejected visibly");
		tree->setCurrentItem(tree->topLevelItem(15));
		const auto before = exportModelQ3Animation(dialog.animation()->config);
		ok &= expect(!dialog.setConfigBytes("bad", &error) && exportModelQ3Animation(dialog.animation()->config) == before,
					 "failed import retains authored draft");
		auto precise = dialog.animation()->config;
		precise.headOffset = {1e-20f, -.12345679f, 137.001f};
		const auto preciseBytes = exportModelQ3Animation(precise);
		ok &= expect(dialog.setConfigBytes(preciseBytes, &error) && exportModelQ3Animation(dialog.animation()->config) == preciseBytes &&
						 dialog.setConfigBytes(before, &error),
					 "opening or importing preserves native float precision until an offset field is edited");
		for (const auto &id :
			 {"q3First", "q3Count", "q3Loop", "q3Fps", "q3LowerPart", "q3UpperPart", "q3LowerAnimation", "q3UpperAnimation"})
		{
			auto *widget = dialog.findChild<QWidget *>(QString::fromLatin1(id));
			auto *accessible = QAccessible::queryAccessibleInterface(widget);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && widget->focusPolicy() != Qt::NoFocus,
						 "native fields expose accessible names and focus path");
		}
		fps->setFocus(Qt::OtherFocusReason);
		app.processEvents();
		ok &= expect(fps->hasFocus() && !fps->accessibleDescription().isEmpty(),
					 "numeric playback field accepts owned keyboard focus and has help");
		auto *scroll = dialog.findChild<QScrollArea *>();
		scroll->verticalScrollBar()->setValue(0);
		app.processEvents();
		ok &= expect(render(dialog, QStringLiteral("q3-%1-top").arg(scenario)), "top layout rendered");
		scroll->ensureWidgetVisible(fps);
		app.processEvents();
		ok &= expect(render(dialog, QStringLiteral("q3-%1-clip").arg(scenario)), "clip controls rendered");
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		app.processEvents();
		ok &= expect(render(dialog, QStringLiteral("q3-%1-metadata").arg(scenario)), "native header controls reachable by scrolling");
		dialog.hide();
		ModelAssemblyDialog assembly;
		assembly.setAttribute(Qt::WA_DeleteOnClose, false);
		assembly.setAccessibility(scenario > 0, true);
		ok &= expect(assembly.setAssembly(tests::q3Assembly(path("part.mesh.json"), false), temporary.path(), &error),
					 "ordinary linked assembly opened");
		assembly.show();
		app.processEvents();
		const auto original = modelAssemblyFingerprint(assembly.document().assembly());
		ok &= expect(assembly.applyQ3Animation(dialog.animation(), &error) && assembly.setTime(.099, &error),
					 "native draft applied through normal worker and timeline");
		const auto current = modelAssemblyFingerprint(assembly.document().assembly());
		ok &= expect(assembly.pose().samples[1].frame == 7 && assembly.pose().samples[1].nextFrame == 6 &&
						 std::abs(assembly.pose().samples[1].fraction - .5) < 1e-10,
					 "GUI composed preview uses reversed native millisecond sample");
		assembly.selectPart("lower");
		ok &= expect(assembly.undo(&error) && modelAssemblyFingerprint(assembly.document().assembly()) == original &&
						 assembly.redo(&error) && modelAssemblyFingerprint(assembly.document().assembly()) == current,
					 "native change is one undo/redo step with ready previews");
		ok &= expect(assembly.exportQ3Animation(path("gui.cfg"), scenario > 0, &error) && tests::q3Read(path("gui.cfg")) == before,
					 "GUI native export agrees with shared native bytes");
		auto bad = *dialog.animation();
		bad.config.clips[0].firstFrame = 100;
		ok &= expect(!assembly.applyQ3Animation(bad, &error) && modelAssemblyFingerprint(assembly.document().assembly()) == current &&
						 assembly.previewReady(),
					 "invalid native application preserves document and preview");
		ModelAssemblyAnimation bake;
		ok &= expect(assembly.bakeAnimation({.099, 3, 20, "native"}, &bake, &error),
					 "native animation bakes through normal GUI authoring handoff");
		ok &= expect(render(assembly, QStringLiteral("q3-%1-assembly").arg(scenario)), "native linked pose rendered");
		bool opened = false;
		QTimer::singleShot(50, &assembly, [&] {
			auto *modal = assembly.findChild<QDialog *>("modelQ3AnimationDialog");
			opened = modal != nullptr;
			if (modal)
				modal->reject();
		});
		assembly.findChild<QAction *>("assemblyQ3Animation")->trigger();
		ok &= expect(opened && modelAssemblyFingerprint(assembly.document().assembly()) == current,
					 "native toolbar action opens cancellable review without edits");
		ok &=
			expect(assembly.applyQ3Animation(std::nullopt, &error) && !assembly.document().assembly().q3Animation && assembly.undo(&error),
				   "GUI native removal undo restores configuration");
		assembly.hide();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	ok &= expect(tests::q3Read(path("part.mesh.json")) == modelBytes, "GUI workflow leaves source models intact");
	std::cout << checks << " native animation UI checks\n";
	return ok ? 0 : 1;
}
