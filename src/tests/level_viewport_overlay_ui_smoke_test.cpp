#include "app/viewport_hud.h"
#include "tests/map_viewport_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QPainter>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
}
int main(int argc, char** argv)
{
	// Render the actual widget to Qt images through semantic calls only.
	qputenv("QT_QPA_PLATFORM","offscreen"); qputenv("QT_SCALE_FACTOR","1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	LevelMapDocument source; source.format = LevelMapFormat::QuakeMap;
	LevelMapEntity entity; entity.id = 37; entity.className = QStringLiteral("light_with_an_expanded_translated_description");
	entity.origin = {0,0,0,true}; source.entities.append(entity);
	const auto labelText = QStringLiteral("Entity 37 (%1)").arg(entity.className);
	MapViewport view; view.resize(641,401); view.setHighContrast(true); view.setShowGrid(false);
	view.setShowLabels(false); view.setShowThings(false); view.setShowVertices(false); view.setReducedMotion(true);
	view.setDocument(source); view.setSelection(LevelMapSelectionKind::Entity,37);
	bool ok = true; int sample = 0;
	for (const auto direction : {Qt::LeftToRight,Qt::RightToLeft}) {
		view.setLayoutDirection(direction);
		for (const int scale : {100,200,300}) {
			QFont font = app.font(); font.setPointSizeF(9.0 * scale / 100); view.setFont(font);
			for (const qreal ratio : {1.0,1.25,1.75,2.0}) {
				for (const auto anchor : {QPointF(2,2),QPointF(639,2),QPointF(2,399),QPointF(639,399),QPointF(639,200)}) {
					ok &= view.restoreNavigationState({0,QPointF(view.width() * 0.5 - anchor.x(),anchor.y() - view.height() * 0.5),1});
					QImage actual(QSize(int(std::ceil(view.width() * ratio)),int(std::ceil(view.height() * ratio))),QImage::Format_ARGB32_Premultiplied);
					actual.setDevicePixelRatio(ratio);
					if (!tests::settleMapViewport(view,&actual)) { return 1; }
					const auto tags = view.hudTags();
					const auto hud = layoutViewportHud(view.rect(),QFontMetricsF(viewportHudFont(font),&actual),
						tags.at(0).split(QStringLiteral("  %1  ").arg(QChar(0x00b7))),{tags.at(1)},direction);
					const auto label = layoutViewportLabel(view.rect(),anchor,QFontMetricsF(font,&actual),labelText,direction,hud);
					ok &= expect(!label.text.isEmpty(),"actual edge selection has a visible label layout");
					QImage reference(actual.size(),actual.format()); reference.setDevicePixelRatio(ratio); reference.fill(Qt::black);
					{ QPainter painter(&reference); painter.setRenderHint(QPainter::Antialiasing); painter.setFont(font);
						paintViewportLabel(painter,label,direction,Qt::white,Qt::black,true); }
					const QRect region(QPoint(int(std::ceil(label.bounds.left() * ratio)),int(std::ceil(label.bounds.top() * ratio))),
						QPoint(int(std::floor(label.bounds.right() * ratio)) - 1,int(std::floor(label.bounds.bottom() * ratio)) - 1));
					ok &= expect(actual.copy(region) == reference.copy(region),"actual primary label is completely painted inside its uncluttered layout");
					ok &= expect(view.accessibleDescription().contains(entity.className),"elision preserves full selection identity for assistive technology");
					if (scale == 200 && ratio == 1.25 && anchor == QPointF(639,399)
						&& !qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_DIR")) {
						const QDir output(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR")); QDir().mkpath(output.path());
						ok &= actual.save(output.filePath(QStringLiteral("level-edge-label-%1.png").arg(int(direction))));
					}
					if (sample++ == 0) {
						const auto unchanged = actual;
						view.setPaintStatisticsEnabled(true); view.render(&actual);
						const auto measured = view.paintStatistics();
						ok &= expect(measured.totalNs > 0 && measured.overlayNs >= measured.selectionMarkersNs && actual == unchanged,
							"opt-in paint statistics leave output unchanged and measure nested work");
						view.setPaintStatisticsEnabled(false);
					}
				}
			}
		}
	}
	ok &= expect(view.displayDocument().entities.first().origin.x == 0 && view.displayDocument().entities.first().origin.y == 0
		&& source.selection.isEmpty() && source.undoStack.isEmpty() && view.displayDocument().undoStack.isEmpty(),
		"navigation and overlay paint leave source geometry and edit state intact");
	view.clearDocument();
	ok &= expect(!view.hasDocument() && view.selectionSet().isEmpty(),"close releases overlay and selection state");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
