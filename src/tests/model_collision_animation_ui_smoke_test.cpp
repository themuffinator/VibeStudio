#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/model_recovery.h"
#include "tests/model_collision_animation_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value)
		std::cerr << "FAIL: " << message << ' ' << error.toStdString() << '\n';
	return value;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
bool capture(QWidget &widget, const QString &name)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty())
		return true;
	if (!QDir().mkpath(directory))
		return false;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(directory).filePath(name + ".png"));
}
bool wait(const std::function<bool()> &done)
{
	QEventLoop loop;
	QTimer poll, deadline;
	deadline.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
		if (done())
			loop.quit();
	});
	QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5);
	deadline.start(20000);
	if (!done())
		loop.exec();
	return done();
}
class Expansion final : public QTranslator
{
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).contains("ModelCollision") && QByteArray(context) != "VibeStudioModelEditor")
			return {};
		const auto text = QString::fromUtf8(source);
		return '[' + text + QString(text.size() / 2, '~') + ']';
	}
};
bool scenario(QApplication &app, const QString &directory, int variant)
{
	bool ok = true;
	QString error;
	Expansion expansion;
	if (variant == 2)
		app.installTranslator(&expansion);
	applyStudioTheme(app, studioThemeTokens(variant == 0   ? StudioTheme::Dark
											: variant == 1 ? StudioTheme::HighContrastDark
														   : StudioTheme::HighContrastLight,
											UiDensity::Standard, variant == 2 ? 200 : 100));
	ModelEditorDialog editor;
	editor.resize(variant == 2 ? 2080 : 1600, variant == 2 ? 1360 : 1040);
	editor.setAccessibility(variant != 0, true);
	if (variant == 2)
		editor.setLayoutDirection(Qt::RightToLeft);
	ok &= expect(editor.setMesh(tests::collisionAnimationFixture(), &error), "open mixed collision tracks", error);
	editor.show();
	app.processEvents();
	auto *preview = editor.findChild<ModelViewport *>("meshPreview");
	auto *boxes = editor.findChild<QComboBox *>("meshCollisionBoxes");
	auto *frame = editor.findChild<QComboBox *>("meshFrame");
	auto *scope = editor.findChild<QComboBox *>("meshCollisionPoseScope");
	auto *geometryScope = editor.findChild<QComboBox *>("meshFrameScope");
	auto *centre = editor.findChild<QDoubleSpinBox *>("meshCollisionCentre0");
	auto *apply = editor.findChild<QPushButton *>("updateMeshCollision");
	auto *animate = editor.findChild<QPushButton *>("animateMeshCollision");
	auto *freeze = editor.findChild<QPushButton *>("freezeMeshCollision");
	auto *fit = editor.findChild<QPushButton *>("fitAnimatedMeshCollision");
	auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
	auto *table = editor.findChild<QTableView *>("meshComponents");
	if (!expect(preview && boxes && frame && scope && geometryScope && centre && apply && animate && freeze && fit && tabs && table,
				"animated collision controls exist"))
		return false;
	QScrollArea *scroll = nullptr;
	for (int i = 0; i < tabs->count(); ++i)
		if (tabs->widget(i)->isAncestorOf(boxes))
		{
			tabs->setCurrentIndex(i);
			scroll = tests::pageScroll(tabs->widget(i));
			break;
		}
	boxes->setCurrentIndex(boxes->findData("body"));
	frame->setCurrentIndex(2);
	scope->setCurrentIndex(1);
	ok &= expect(centre->value() == 40 && scope->isEnabled() && geometryScope->currentIndex() == 1 && freeze->isEnabled() &&
					 !animate->isEnabled(),
				 "inspector follows stored frame and shared scope");
	ok &= expect(table->model()->data(table->model()->index(0, 1)).toString().toDouble() == 40, "collision table follows pose");
	ModelCollisionBox posed;
	ok &=
		expect(preview->collisionPose("body", &posed) && posed.centre.x == 40 && posed.rotation.z == 90, "preview uses collision pose two");
	const auto before = bytes(editor.document().mesh());
	centre->setValue(48);
	apply->click();
	ok &= expect(editor.document().mesh().collisionBoxes[0].framePoses[2].centre.x == 48 &&
					 editor.document().mesh().collisionBoxes[0].framePoses[0].centre.x == 0,
				 "Apply Box changes only current collision pose");
	editor.findChild<QAction *>("undoMesh")->trigger();
	ok &= expect(bytes(editor.document().mesh()) == before && centre->value() == 40, "undo restores track and pose inspector");
	editor.findChild<QAction *>("redoMesh")->trigger();
	ok &= expect(centre->value() == 48, "redo refreshes current pose fields");
	editor.findChild<QAction *>("undoMesh")->trigger();
	frame->setCurrentIndex(0);
	if (variant == 0)
	{
		preview->setReducedMotion(false);
		preview->setAnimationIndex(0);
		preview->setFramesPerSecond(1);
		preview->setAnimationInterpolation(true);
		preview->play();
		preview->seekAnimation(.5);
		ok &= expect(preview->collisionPose("body", &posed) && std::abs(posed.centre.x - 10) < .001 && std::abs(posed.rotation.z) < .001,
					 "smooth playback blends collision in sync with mesh");
		ok &= expect(bytes(editor.document().mesh()) == before, "transient collision playback never changes source");
		preview->pause();
		ok &= expect(preview->collisionPose("body", &posed) && posed.centre.x == 0 && centre->value() == 0,
					 "pause returns to exact stored pose");
		preview->setReducedMotion(true);
	}
	frame->setCurrentIndex(1);
	freeze->click();
	ok &= expect(editor.document().mesh().collisionBoxes[0].framePoses.isEmpty() &&
					 editor.document().mesh().collisionBoxes[0].centre.x == 20 && animate->isEnabled() && !freeze->isEnabled() &&
					 !scope->isEnabled(),
				 "freeze current pose updates mode controls");
	animate->click();
	ok &= expect(editor.document().mesh().collisionBoxes[0].framePoses.size() == 3 && scope->currentIndex() == 1 && scope->isEnabled(),
				 "animate restores per-pose workflow with current-frame default");
	geometryScope->setCurrentIndex(0);
	ok &= expect(scope->currentIndex() == 0, "Geometry scope synchronizes collision scope");
	centre->setValue(24);
	apply->click();
	ok &= expect(editor.document().mesh().collisionBoxes[0].framePoses[0].centre.x == 24 &&
					 editor.document().mesh().collisionBoxes[0].framePoses[2].centre.x == 24,
				 "all-frame Apply Box is explicit");
	editor.findChild<QLineEdit *>("meshCollisionName")->setText("fit");
	fit->click();
	ok &= expect(editor.document().mesh().collisionBoxes.size() == 3 &&
					 editor.document().mesh().collisionBoxes[2].framePoses[2].centre.z == 16,
				 "GUI fits per-frame mesh bounds");
	frame->setCurrentIndex(2);
	ok &= expect(editor.exportCollision(directory + QStringLiteral("/animated-%1.map").arg(variant), {"quake2", {}, {}, 2}, false, &error),
				 "GUI writes chosen stored collision pose", error);
	ModelCollisionMap reference;
	exportModelCollisionMap(editor.document().mesh(), {"quake2", {}, {}, 2}, &reference);
	QFile map(directory + QStringLiteral("/animated-%1.map").arg(variant));
	ok &= expect(map.open(QIODevice::ReadOnly) && map.readAll() == reference.bytes, "GUI map uses shared sampled export");
	if (variant == 0)
	{
		LevelMapDocument destination;
		loadLevelMapBytes({directory + "/destination.map", {}, "idtech2"}, "{\n\"classname\" \"worldspawn\"\n}\n", &destination, &error);
		editor.context = [&] { return ModelDesignContext{{}, directory + "/destination.map", false, false, true}; };
		editor.collisionDestination = [&] {
			return ModelCollisionDestination{destination, [&](const LevelMapDocument &candidate, QString *) {
												 destination = candidate;
												 return true;
											 }};
		};
		editor.refreshContext();
		editor.findChild<QComboBox *>("meshCollisionTarget")->setCurrentIndex(1);
		editor.findChild<QPushButton *>("placeMeshCollision")->click();
		ok &= expect(destination.brushes.size() == 3 && undoLevelMapEdit(&destination, &error) && destination.brushes.isEmpty(),
					 "map handoff button carries displayed frame and one undo", error);
	}
	editor.checkpointRecovery();
	ok &= expect(wait([&] { return !editor.recoveryBusy(); }), "animated recovery writer completes");
	ModelRecoverySnapshot recovered;
	ok &= expect(inspectModelRecovery(editor.recoveryPath(), &recovered).isValid() &&
					 bytes(recovered.mesh) == bytes(editor.document().mesh()) && recovered.frame == 2,
				 "GUI recovery retains all collision poses");
	preview->frameModel();
	ok &= expect(tests::settleModelViewport(*preview) && preview->collisionScreenEdges("fit").size() == 12,
				 "animated collision edges publish with raster snapshot");
	const auto edges = preview->collisionScreenEdges("fit");
	if (!edges.isEmpty())
		ok &= expect(preview->collisionAt(edges.front().center()) == "fit", "current-pose collision is pickable");
	for (auto *button : {animate, freeze, fit})
	{
		const auto accessible = QAccessible::queryAccessibleInterface(button);
		ok &= expect(button->focusPolicy() != Qt::NoFocus && accessible && accessible->role() == QAccessible::Button &&
						 !accessible->text(QAccessible::Name).isEmpty() && !button->accessibleDescription().isEmpty(),
					 "new controls expose keyboard focus and accessible metadata");
	}
	if (scroll)
	{
		scroll->ensureWidgetVisible(freeze);
		app.processEvents();
		ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, "expanded/scaled inspector avoids horizontal scroll");
		for (auto *button : {animate, freeze, fit})
			ok &= expect(button->height() >= button->heightForWidth(button->width()), "wrapped action labels fit allocated height");
		ok &= expect(capture(editor, QStringLiteral("collision-animation-%1-controls").arg(variant)), "capture animation controls");
		scroll->verticalScrollBar()->setValue(0);
		app.processEvents();
		ok &= expect(capture(editor, QStringLiteral("collision-animation-%1-inspector").arg(variant)), "capture animated inspector");
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		app.processEvents();
		ok &= expect(capture(editor, QStringLiteral("collision-animation-%1-handoff").arg(variant)), "capture sampled handoff");
	}
	ok &= expect(capture(*preview, QStringLiteral("collision-animation-%1-pose").arg(variant)), "capture current collision render");
	if (variant == 2)
		app.removeTranslator(&expansion);
	return ok;
}
bool maximum(QApplication &app)
{
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelEditorDialog editor;
	editor.resize(1600, 1040);
	editor.setAccessibility(false, true);
	editor.show();
	app.processEvents();
	const auto mesh = tests::collisionMaximumFixture();
	QElapsedTimer elapsed;
	elapsed.start();
	qint64 last = elapsed.nsecsElapsed(), maximumGap = 0;
	int events = 0;
	QTimer tick;
	QObject::connect(&tick, &QTimer::timeout, &editor, [&] {
		const auto now = elapsed.nsecsElapsed();
		maximumGap = std::max(maximumGap, now - last);
		last = now;
		++events;
	});
	tick.start(1);
	QString error;
	bool ok = expect(editor.setMesh(mesh, &error), "load maximum tracks through document worker", error);
	auto *boxes = editor.findChild<QComboBox *>("meshCollisionBoxes");
	boxes->setCurrentIndex(boxes->findData("box_0"));
	editor.findChild<QComboBox *>("meshFrame")->setCurrentIndex(1023);
	editor.findChild<QComboBox *>("meshCollisionPoseScope")->setCurrentIndex(0);
	editor.findChild<QDoubleSpinBox *>("meshCollisionSize0")->setValue(12);
	editor.findChild<QPushButton *>("updateMeshCollision")->click();
	ok &= expect(editor.document().mesh().collisionBoxes[0].framePoses[0].size.x == 12 &&
					 editor.document().mesh().collisionBoxes[0].framePoses[1023].size.x == 12 &&
					 editor.document().mesh().collisionBoxes[1].size.x == 4,
				 "maximum all-pose GUI edit changes only selected track");
	editor.findChild<QAction *>("undoMesh")->trigger();
	const auto *box = findModelCollisionBox(editor.document().mesh(), "box_0");
	ok &= expect(box && box->framePoses[1023].size.x == 4, "maximum GUI edit undoes");
	auto *preview = editor.findChild<ModelViewport *>("meshPreview");
	preview->frameModel();
	ok &= expect(tests::settleModelViewport(*preview) && preview->collisionScreenEdges("box_63").size() == 12,
				 "maximum track preview renders all 64 current boxes");
	app.processEvents();
	maximumGap = std::max(maximumGap, elapsed.nsecsElapsed() - last);
	tick.stop();
	const double gapMs = maximumGap / 1e6;
	const auto budget = qEnvironmentVariable("VIBESTUDIO_MODELLER_MAX_AUTHORING_GAP_MS").toDouble();
	ok &= expect(events >= 2 && (budget <= 0 || gapMs <= budget), "maximum collision authoring respects GUI event-gap budget");
	std::cout << "Maximum GUI tracks: 64 x 1024; elapsed ms=" << elapsed.elapsed() << "; events=" << events << "; max gap ms=" << gapMs
			  << '\n';
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	// Owned widget semantics/rendering only; no keyboard, mouse or OS capture.
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
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("collision-animation-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	StudioSettings().setModelRecoveryEnabled(true);
	bool ok = true;
	for (int variant = 0; variant < 3; ++variant)
		ok &= scenario(app, temporary.path(), variant);
	ok &= maximum(app);
	std::cout << (ok ? "Animated collision GUI checks passed.\n" : "Animated collision GUI checks failed.\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
