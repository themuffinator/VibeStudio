#include "app/studio_theme.h"

#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QProgressBar>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const QString& message)
{
	if (!value) { std::cerr << message.toStdString() << '\n'; }
	return value;
}

double channel(double value)
{
	return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}
double luminance(const QColor& color)
{
	return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}
double contrast(const QColor& a, const QColor& b)
{
	const auto first = luminance(a), second = luminance(b);
	return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

QImage render(QProgressBar& bar, bool text)
{
	bar.setTextVisible(text);
	QImage image(bar.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
	bar.render(&image); return image;
}

bool inspect(QProgressBar& bar, const QString& label, bool capture, double& minimumContrast)
{
	bar.ensurePolished(); bar.resize(480, std::max(36, bar.sizeHint().height()));
	bool ok = expect(bar.height() >= bar.fontMetrics().height() + 4, label + ": progress height must fit scaled text.");
	const auto background = render(bar, false), text = render(bar, true);
	int corePixels[2] = {0, 0}; double minimum[2] = {100.0, 100.0};
	const QRgb normalInk = bar.palette().color(QPalette::Text).rgba();
	const QRgb filledInk = bar.palette().color(QPalette::HighlightedText).rgba();
	// Check opaque foreground strokes against the actual painted background.
	// Antialias fringes intentionally blend with their background; treating the
	// most frequent fringe shade as the foreground gives misleading ratios.
	for (int y = 2; y < text.height() - 2; ++y) {
		for (int x = 2; x < text.width() - 2; ++x) {
			const auto pixel = text.pixel(x, y);
			if (pixel == background.pixel(x, y) || (pixel != normalInk && pixel != filledInk)) { continue; }
			const int half = x < text.width() / 2 ? 0 : 1;
			++corePixels[half];
			minimum[half] = std::min(minimum[half], contrast(text.pixelColor(x, y), background.pixelColor(x, y)));
		}
	}
	for (int half = 0; half < 2; ++half) {
		ok &= expect(corePixels[half] >= 12 && minimum[half] >= 4.5 && minimum[half] < 100,
			label + QStringLiteral(": half %1 has %2 core glyph pixels, minimum contrast %3.").arg(half).arg(corePixels[half]).arg(minimum[half], 0, 'f', 3));
	}
	minimumContrast = std::min({minimumContrast, minimum[0], minimum[1]});
	auto* accessible = QAccessible::queryAccessibleInterface(&bar);
	ok &= expect(accessible && accessible->role() == QAccessible::ProgressBar && accessible->valueInterface()
		&& accessible->valueInterface()->currentValue().toInt() == bar.value()
		&& !accessible->text(QAccessible::Name).isEmpty(), label + ": built-in progress role and value remain available.");
	const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (capture && !captures.isEmpty()) {
		ok &= expect(text.save(QDir(captures).filePath(label + ".png")), "Save progress contrast render.");
	}
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	bool ok = true;
	int cases = 0; double minimumContrast = 100.0;
	for (const auto theme : {StudioTheme::Dark, StudioTheme::Light, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight}) {
		for (int scale : {100, 200}) {
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
				for (bool inverted : {false, true}) {
					QProgressBar bar; bar.setAccessibleName("Synthetic operation progress");
					bar.setLayoutDirection(direction); bar.setInvertedAppearance(inverted);
					bar.setRange(-50, 50); bar.setFormat("HHHH HHHH");
					for (int value : {-50, 0, 50}) {
						bar.setValue(value);
						const auto label = QStringLiteral("progress-%1-%2-%3-%4-%5").arg(themeId(theme)).arg(scale)
							.arg(direction == Qt::LeftToRight ? "ltr" : "rtl").arg(inverted ? "inverted" : "normal").arg(value + 50);
						ok &= inspect(bar, label, direction == Qt::LeftToRight && !inverted && value == 0, minimumContrast);
						++cases;
					}
				}
			}
		}
	}
	std::cout << cases << " rendered progress cases; minimum opaque-stroke contrast " << minimumContrast << ":1.\n";
	return ok ? 0 : 1;
}
