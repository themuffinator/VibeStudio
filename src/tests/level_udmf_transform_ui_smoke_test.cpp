#include "app/application_shell.h"
#include "app/level_rotation_dialog.h"
#include "app/level_placement_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_doom_nodes.h"
#include "core/level_udmf.h"
#include "tests/level_udmf_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace f = vibestudio::tests::udmf;
namespace d = vibestudio::tests::doom;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& error = {}) {
	if (!value) {
		ok = false;
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(10, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
}
void capture(QWidget& widget, const QString& name) {
	QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
	QApplication::processEvents();
	const auto path = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (path.isEmpty()) {
		return;
	}
	QDir().mkpath(path);
	QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	widget.render(&image);
	expect(image.save(QDir(path).filePath(name + ".png")), "widget capture");
}
class Expansion final : public QTranslator {
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		if (QByteArray(context) != "LevelRotationDialog") {
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return text + QString(text.size() / 3, '~');
	}
};
} // namespace
int main(int argc, char** argv) {
	// Semantic Qt controls and QWidget::render only; no native input or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.setRestoreSession(false);
	QString error;
	const auto path = temp.filePath("udmf.wad"), assets = temp.filePath("assets.wad");
	expect(d::write(path, f::fixture()) && d::write(assets, d::wad(d::assets())), "generated fixtures");
	for (int scale : {100, 200}) {
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTextScalePercent(scale);
		settings.setTheme(theme);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		ApplicationShell shell;
		shell.resize(scale == 100 ? 1600 : 2400, scale == 100 ? 1000 : 1600);
		shell.show();
		shell.openPathFromCommandLine(assets);
		shell.openPathFromCommandLine(path);
		if (!expect(until([&] { return shell.levelDocument().doomUdmf != nullptr; }), "shell loads UDMF")) {
			return 1;
		}
		auto* plan = shell.findChild<MapViewport*>("mapViewport");
		auto* undo = shell.findChild<QAction*>("map.undo");
		auto* snap = shell.findChild<QCheckBox*>("levelMapSnap");
		if (!expect(plan && undo && snap, "native edit controls")) {
			return 1;
		}
		snap->setChecked(false);
		plan->selectionSetChanged({{LevelMapSelectionKind::DoomSector, 0}});
		for (const auto& id :
			 {"map.rotatePrecisely", "map.flipHorizontal", "map.flipVertical", "map.resizeSelection", "map.selectConnectedGeometry"}) {
			auto* action = shell.findChild<QAction*>(id);
			expect(action && action->isEnabled(), "UDMF standard transform action enabled", id);
		}
		const auto before = serializeLevelMap(shell.levelDocument()).bytes;
		plan->moveRequested(.375, -.125, 0);
		expect(shell.levelDocument().doomVertices[0].x == .625 && shell.levelDocument().doomVertices[0].y == .125 &&
				   shell.levelDocument().undoStack.size() == 1,
			   "viewport move publishes fractional text once");
		undo->trigger();
		expect(serializeLevelMap(shell.levelDocument()).bytes == before, "viewport move exact undo");
		plan->resizeRequested({-1.125, 2.375, 0, true}, {126.875, 386.375, 0, true});
		expect(shell.levelDocument().doomVertices[0].x == -1.125 && shell.levelDocument().doomVertices[2].y == 386.375 &&
				   shell.levelDocument().undoStack.size() == 1,
			   "viewport resize uses lossless transform service");
		undo->trigger();
		shell.findChild<QAction*>("map.rotateLeft")->trigger();
		expect(shell.levelDocument().doomVertices[0].x == 256.25 && shell.levelDocument().doomVertices[0].y == .25 &&
				   shell.levelDocument().undoStack.size() == 1,
			   "quick turn preserves fractional coordinates");
		undo->trigger();
		shell.findChild<QAction*>("map.flipHorizontal")->trigger();
		expect(shell.levelDocument().doomLinedefs[0].startVertex == 1 && shell.levelDocument().doomLinedefs[0].endVertex == 0 &&
				   shell.levelDocument().doomVertices[0].x == 256.25,
			   "shell mirror preserves line orientation");
		undo->trigger();
		plan->selectionSetChanged({{LevelMapSelectionKind::DoomLinedef, 0}});
		shell.findChild<QAction*>("map.selectConnectedGeometry")->trigger();
		expect(shell.levelDocument().selection.size() == 4 && shell.levelDocument().undoStack.isEmpty(),
			   "connected selection is not a geometry edit");
		QTimer driver;
		driver.setInterval(15);
		int phase = 0;
		QObject::connect(&driver, &QTimer::timeout, &shell, [&] {
			auto* widget = QApplication::activeModalWidget();
			if (!widget || widget->objectName() != "levelRotationDialog" || phase == 2) {
				return;
			}
			auto* dialog = static_cast<LevelRotationDialog*>(widget);
			if (phase == 0) {
				dialog->resize(scale == 100 ? 950 : 1550, scale == 100 ? 800 : 1100);
				dialog->setRequest({2, 22.5, {0, 0, 0, true}, true, false});
				phase = 1;
				return;
			}
			if (!dialog->isReady()) {
				return;
			}
			phase = 2;
			expect(dialog->previewValid(), "UDMF numeric rotation preview", dialog->findChild<QLabel*>("rotationStatus")->text());
			auto* degrees = dialog->findChild<QDoubleSpinBox*>("rotationDegrees");
			expect(degrees && QAccessible::queryAccessibleInterface(degrees)->role() == QAccessible::SpinBox &&
					   !degrees->accessibleDescription().isEmpty() && degrees->focusPolicy() != Qt::NoFocus &&
					   degrees->layoutDirection() == Qt::LeftToRight,
				   "accessible localizable numeric control");
			auto* preview = dialog->findChild<ModelViewport*>();
			expect(preview && preview->hasMesh() && preview->reducedMotion() && tests::settleModelViewport(*preview),
				   "shared Models preview renderer");
			capture(*dialog, QStringLiteral("udmf-rotation-%1").arg(scale));
			expect(dialog->findChild<QScrollArea*>()->horizontalScrollBar()->maximum() == 0, "expanded RTL controls fit horizontally");
			for (auto* button : dialog->findChildren<QPushButton*>()) {
				expect(dialog->rect().contains(QRect(button->mapTo(dialog, QPoint()), button->size())), "scaled buttons fit dialog");
			}
			dialog->accept();
		});
		driver.start();
		QTimer::singleShot(30000, &shell, [&] {
			if (phase != 2) {
				expect(false, "numeric dialog timeout");
				if (auto* dialog = shell.findChild<QDialog*>("levelRotationDialog")) {
					dialog->reject();
				}
			}
		});
		shell.findChild<QAction*>("map.rotatePrecisely")->trigger();
		driver.stop();
		expect(phase == 2 && shell.levelDocument().undoStack.size() == 1 && shell.levelDocument().doomVertices[0].x != .25,
			   "numeric rotation publishes through common worker");
		const auto rotated = serializeLevelMap(shell.levelDocument()).bytes;
		undo->trigger();
		shell.findChild<QAction*>("map.redo")->trigger();
		expect(serializeLevelMap(shell.levelDocument()).bytes == rotated, "numeric rotation exact undo redo");
		const auto saved = temp.filePath(QStringLiteral("transformed-%1.wad").arg(scale));
		expect(shell.saveLevelDocument(saved, false, &error), "transformed WAD save", error);
		LevelMapDocument reopened;
		expect(loadLevelMap({saved, "MAP01", {}}, &reopened, &error) &&
				   reopened.doomUdmf->source == shell.levelDocument().doomUdmf->source && inspectLevelDoomNodes(reopened).needsBuild(),
			   "save and build share transformed source", error);
		// A single vertex rotating around itself must explain why Apply is unavailable.
		LevelMapDocument point;
		loadLevelMapBytes({"point.wad", "MAP01", {}}, f::fixture(), &point, &error);
		selectLevelMapObject(&point, "vertex:0");
		LevelRotationDialog noOp(point);
		noOp.setRequest({2, 22.5, {.25, .25, 0, true}, true, false});
		noOp.show();
		expect(until([&] { return noOp.isReady(); }) && !noOp.previewValid() &&
			   !noOp.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled() &&
			   noOp.findChild<QLabel*>("rotationStatus")->text().contains("unchanged"), "no-op preview gives actionable feedback");
		noOp.reject();
		// UDMF thing placement uses the same preview and numeric surface as
		// brush arrays, including fractional height and accessible controls.
		plan->selectionSetChanged({{LevelMapSelectionKind::DoomThing, 0}});
		for (const auto& id : {"map.duplicateSelection", "map.duplicateWithOffset"}) {
			auto* action = shell.findChild<QAction*>(id);
			expect(action && action->isEnabled(), "UDMF thing duplicate action enabled", id);
		}
		const auto thingsBefore = serializeLevelMap(shell.levelDocument()).bytes;
		shell.findChild<QAction*>("map.duplicateSelection")->trigger();
		expect(shell.levelDocument().doomThings.size() == 2, "shell quick duplicate publishes UDMF thing");
		undo->trigger();
		expect(serializeLevelMap(shell.levelDocument()).bytes == thingsBefore && shell.levelDocument().selection.size() == 1
			&& shell.levelDocument().selection.first().objectId == 0, "shell duplicate undo restores source selection and bytes");
		LevelPlacementDialog placement(shell.levelDocument(), LevelPlacementMode::Duplicate);
		placement.resize(scale == 100 ? 1100 : 1900, scale == 100 ? 750 : 1200);
		placement.setOffset({.375, -.125, .25, true});
		placement.setCopies(3);
		placement.show();
		expect(until([&] { return placement.isReady(); }) && placement.previewValid(), "UDMF array numeric preview",
			placement.findChild<QLabel*>("placementStatus")->text());
		auto* height = placement.findChild<QDoubleSpinBox*>("placementOffset2");
		expect(height && height->isEnabled() && height->decimals() >= 3 && height->value() == .25
			&& height->focusPolicy() != Qt::NoFocus && !height->accessibleDescription().isEmpty()
			&& QAccessible::queryAccessibleInterface(height)->role() == QAccessible::SpinBox, "accessible fractional UDMF height control");
		expect(placement.previewDocument().doomThings.size() == 4 && placement.previewDocument().doomThings.last().z == .75 + .5,
			"UDMF array preview retains fractional height");
		expect(serializeLevelMap(shell.levelDocument()).bytes == thingsBefore, "UDMF preview does not mutate live document");
		placement.reject();
		shell.close();
		if (scale == 200) {
			app.removeTranslator(&expansion);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	return ok ? 0 : 1;
}
