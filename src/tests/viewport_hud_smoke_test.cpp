#include "app/viewport_hud.h"

#include <QApplication>
#include <QDir>
#include <QImage>
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
bool labels(const QFont& font)
{
	bool ok = true;
	const QFontMetricsF metrics(font);
	for (const auto direction : {Qt::LeftToRight,Qt::RightToLeft}) {
		for (const int width : {240,640}) {
			const QRectF viewport(31,21,width,280);
			const auto hud = layoutViewportHud(viewport,QFontMetricsF(viewportHudFont(font)),
				{QStringLiteral("Front (XZ)"),QStringLiteral("Selection 16384 x 4096 expanded status")},{QStringLiteral("512 selected")},direction);
			for (const auto anchor : {viewport.topLeft() + QPointF(2,2),viewport.topRight() + QPointF(-2,2),
				viewport.bottomLeft() + QPointF(2,-2),viewport.bottomRight() + QPointF(-2,-2),viewport.center(),
				QPointF(viewport.right() - 2,viewport.center().y()),QPointF(viewport.left() + 2,viewport.center().y())}) {
				for (const auto& name : {QStringLiteral("Brush 9999"),QStringLiteral("Entity 123 (expanded translated object name that must fit inside the viewport)")}) {
					const auto label = layoutViewportLabel(viewport,anchor,metrics,name,direction,hud);
					ok &= expect(!label.text.isEmpty() && viewport.adjusted(6,6,-6,-6).contains(label.bounds),"selection label stays readable at every pane edge");
					ok &= expect(!label.bounds.intersects(hud.leading.bounds) && !label.bounds.intersects(hud.trailing.bounds)
						&& !label.bounds.intersects(QRectF(anchor - QPointF(18,18),QSizeF(36,36))),"selection label avoids status and crosshair");
					ok &= expect(metrics.horizontalAdvance(label.text) <= label.bounds.width() - 8,"expanded label fits its text area");
					if (anchor.y() > viewport.bottom() - 3) {
						ok &= expect(label.bounds.top() > viewport.center().y(),"bottom-edge label remains near its selection instead of jumping to the HUD");
					}
				}
			}
			const auto shortLabel = layoutViewportLabel(viewport,viewport.center(),metrics,QStringLiteral("Brush 9"),direction,{});
			if (metrics.horizontalAdvance(QStringLiteral("Brush 9")) + 34 < viewport.width() * 0.5) {
				ok &= expect(direction == Qt::RightToLeft ? shortLabel.bounds.right() < viewport.center().x()
					: shortLabel.bounds.left() > viewport.center().x(),"label preference follows reading direction when that side has space");
			}
		}
	}
	ok &= expect(layoutViewportLabel(QRectF(0,0,10,10),QPointF(5,5),metrics,"label",Qt::LeftToRight).text.isEmpty(),"collapsed pane safely omits label");
	ok &= expect(layoutViewportLabel(QRectF(0,0,500,300),QPointF(-1,100),metrics,"label",Qt::LeftToRight).text.isEmpty(),"offscreen selection does not pin an unrelated label to an edge");
	for (const qreal ratio : {1.0,1.25,1.5,1.75,2.0}) {
		const QRectF viewport(0,0,401,251);
		QImage image(QSize(int(std::ceil(401 * ratio)),int(std::ceil(251 * ratio))),QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio); image.fill(Qt::transparent);
		const QStringList parts {QStringLiteral("Front (XZ)"),QStringLiteral("Grid 16")}, counts {QStringLiteral("2 selected")};
		QImage prepared = image.copy();
		const auto hud = layoutViewportHud(viewport,QFontMetricsF(viewportHudFont(font),&image),parts,counts,Qt::RightToLeft);
		{ QPainter painter(&image); paintViewportHud(painter,viewport,font,parts,counts,Qt::RightToLeft,Qt::white,Qt::black,true); }
		{ QPainter painter(&prepared); paintViewportHud(painter,viewport,font,hud,Qt::RightToLeft,Qt::white,Qt::black,true); }
		ok &= expect(image == prepared,"prepared HUD paints identically to the shared convenience path at every scale");
		image.fill(Qt::transparent);
		const auto label = layoutViewportLabel(viewport,QPointF(399,249),QFontMetricsF(font,&image),QStringLiteral("Brush 9999"),Qt::LeftToRight,hud);
		{ QPainter painter(&image); painter.setFont(font); paintViewportLabel(painter,label,Qt::LeftToRight,Qt::white,Qt::black,true); }
		int ink = 0;
		for (int y = 0; y < image.height(); ++y) {
			for (int x = 0; x < image.width(); ++x) {
				if (!qAlpha(image.pixel(x,y))) { continue; }
				if (qRed(image.pixel(x,y)) > 200 && label.bounds.adjusted(3,3,-3,-3).contains(QPointF(x / ratio,y / ratio))) { ++ink; }
				ok &= expect(label.bounds.adjusted(-1,-1,1,1).contains(QPointF(x / ratio,y / ratio)),"label painting stays inside its layout at fractional scale");
			}
		}
		ok &= expect(ink > 20,"edge label produces visible text");
		// Dense selected outlines must not wash the label out. Compare the
		// actual backing against the theme color, not the scene underneath it.
		for (const bool highContrast : {false,true}) {
			image.fill(QColor(255,215,87));
			{ QPainter painter(&image); painter.setFont(font); paintViewportLabel(painter,label,Qt::LeftToRight,Qt::white,Qt::black,highContrast); }
			const QPoint sample = ((label.bounds.topLeft() + QPointF(2,5)) * ratio).toPoint();
			ok &= expect(image.pixelColor(sample).red() <= (highContrast ? 0 : 26),"selection label has a contrasting backdrop over bright geometry");
		}
	}
	return ok;
}
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	bool ok = true;
	const QStringList counts {QStringLiteral("12 selected · 120 entities · 4000 brushes"), QStringLiteral("80000 triangles")};
	for (int scale : {100, 200, 300}) {
		QFont font = app.font(); font.setPointSizeF(9.0 * scale / 100.0);
		ok &= labels(font);
		const QFontMetricsF metrics(font);
		for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
			for (const auto& title : {QStringLiteral("Front (XZ)"), QStringLiteral("أمامي (XZ)")}) {
				const QStringList parts {title, QStringLiteral("Active editing pane"), QStringLiteral("Grid 1024 units"),
					QStringLiteral("Snap on"), QStringLiteral("Selection 16384 × 4096 expanded status")};
				for (int width : {240, 320, 600, 1600}) {
					const QRectF viewport(31, 21, width, 180);
					const auto layout = layoutViewportHud(viewport, metrics, parts, counts, direction);
					ok &= expect(!layout.leading.lines.isEmpty() && layout.leading.lines.size() <= 2, "view status remains present and bounded to two rows");
					ok &= expect(viewport.contains(layout.leading.bounds), "leading tag stays inside narrow pane");
					if (metrics.horizontalAdvance(title) <= width - 30) {
						ok &= expect(layout.leading.lines.first().startsWith(title), "view identity survives long status in either direction");
					}
					ok &= expect(direction == Qt::LeftToRight ? layout.leading.bounds.left() == viewport.left() + 8
						: layout.leading.bounds.right() == viewport.right() - 8, "status anchors to the logical leading corner");
					for (const auto* tag : {&layout.leading, &layout.trailing}) {
						for (const auto& line : tag->lines) {
							ok &= expect(metrics.horizontalAdvance(line) <= tag->bounds.width() - 14 + 0.1, "visible status text fits its background");
						}
					}
					if (!layout.trailing.lines.isEmpty()) {
						ok &= expect(viewport.contains(layout.trailing.bounds) && !layout.leading.bounds.intersects(layout.trailing.bounds),
							"optional counts never overlap view state or escape the pane");
					}
				}
				const auto wide = layoutViewportHud(QRectF(0, 0, 20000, 180), metrics, parts, counts, direction);
				ok &= expect(wide.leading.lines.size() == 1 && wide.leading.lines.first().contains(parts.last()) && wide.trailing.lines == counts,
					"wide pane retains complete status and statistics");
			}
		}
		const QStringList parts {QStringLiteral("أمامي (XZ)"), QStringLiteral("نشط"), QStringLiteral("Grid 16"),
			QStringLiteral("Selection 128 × 96 expanded text")};
		const auto shortPane = layoutViewportHud(QRectF(0, 0, 240, std::ceil(metrics.height()) + 22), metrics, parts, counts, Qt::RightToLeft);
		ok &= expect(shortPane.leading.lines.size() == 1 && shortPane.trailing.lines.isEmpty(), "short pane avoids a cropped second row");
		ok &= expect(layoutViewportHud(QRectF(0, 0, 10, 10), metrics, parts, counts, Qt::LeftToRight).leading.lines.isEmpty(),
			"collapsed pane omits status without invalid rectangles");
		QImage image(384, 250, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
		QPainter painter(&image);
		const QRectF viewport(32, 25, 320, 200);
		paintViewportHud(painter, viewport, font, parts, counts, Qt::RightToLeft, Qt::white, Qt::black, true);
		painter.end();
		bool painted = false;
		for (int y = 0; y < image.height(); ++y) {
			for (int x = 0; x < image.width(); ++x) {
				if (image.pixelColor(x, y).alpha() == 0) { continue; }
				painted = true;
				ok &= expect(viewport.contains(QPointF(x, y)), "painting stays inside viewport");
			}
		}
		ok &= expect(painted, "status is visibly rendered");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("viewport-status-arabic-%1.png").arg(scale))), "status render saved");
		}
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
