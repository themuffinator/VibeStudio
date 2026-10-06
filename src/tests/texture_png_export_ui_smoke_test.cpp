#include "app/texture_png_export_dialog.h"
#include "app/studio_theme.h"
#include "core/texture_output.h"

#include <QAccessible>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer clock; clock.start();
	while (!ready() && clock.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return ready();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		return QByteArray(context).startsWith("VibeStudioTexture") ? QStringLiteral("[%1 expanded text]").arg(QString::fromUtf8(source)) : QString{};
	}
};
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	bool ok = true; QString error;
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	QImage pixels(128, 64, QImage::Format_ARGB32);
	for (int y = 0; y < pixels.height(); ++y) { for (int x = 0; x < pixels.width(); ++x) { pixels.setPixel(x, y, qRgba(x, y, 190, (x + y) % 256)); } }
	const auto png = encodeTextureOutputPng(pixels, &error);
	ok &= expect(!png.isEmpty() && QImage::fromData(png) == pixels, "PNG retains every RGBA pixel, including transparent RGB");
	int checks = 0;
	ok &= expect(encodeTextureOutputPng(pixels, &error, [&] { return ++checks >= 4; }).isEmpty() && checks >= 4 &&
		error.contains(QStringLiteral("cancelled")), "cancel during PNG writes discards all prepared bytes");
	ok &= expect(encodeTextureOutputPng({}, &error).isEmpty() && !error.isEmpty(), "empty images fail with a diagnostic");
	{
		QImage maximum(4096, 4096, QImage::Format_ARGB32); maximum.fill(qRgba(20, 40, 80, 120));
		const auto bytes = encodeTextureOutputPng(maximum, &error);
		ok &= expect(!bytes.isEmpty() && QImage::fromData(bytes).size() == maximum.size(), "browser PNG supports the full 16-million-pixel decode limit");
		QImage excessive(4097, 4096, QImage::Format_ARGB32);
		ok &= expect(encodeTextureOutputPng(excessive, &error).isEmpty() && !error.isEmpty(), "oversized PNG pixels are rejected before encoding");
		maximum.detach(); quint32 random = 0x741593u;
		for (int y = 0; y < maximum.height(); ++y) {
			auto* row = reinterpret_cast<QRgb*>(maximum.scanLine(y));
			for (int x = 0; x < maximum.width(); ++x) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; row[x] = random; }
		}
		ok &= expect(encodeTextureOutputPng(maximum, &error).isEmpty() && error.contains(QStringLiteral("exceeds 64 MiB")),
			"incompressible maximum-size pixels stop at the encoded byte limit without returning a partial PNG");
	}
	const QString targetPath = temporary.filePath(QStringLiteral("existing.png"));
	ok &= expect(put(targetPath, QByteArray(1024 * 1024, 'x')), "create a destination for cancellation and race checks");
	TextureOutputTarget target; checks = 0;
	ok &= expect(!inspectTextureOutputTarget(targetPath, true, &target, &error, [&] { return ++checks >= 4; }) &&
		error.contains(QStringLiteral("cancelled")) && target.path.isEmpty(), "target fingerprinting checks cancellation between read blocks");
	ok &= expect(inspectTextureOutputTarget(targetPath, true, &target, &error), "capture an existing destination before encoding");
	const QByteArray changed("newer external output"); put(targetPath, changed);
	ok &= expect(!writeTextureOutput(target, png, false, &error) && read(targetPath) == changed, "publication preserves a target modified during encoding");

	int ticks = 0; QTimer heartbeat; heartbeat.setInterval(2); QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; }); heartbeat.start();
	TexturePngExportResult result; bool finished = false;
	QImage large(2048, 4096, QImage::Format_ARGB32); large.fill(qRgba(30, 70, 90, 140));
	const auto output = temporary.filePath(QStringLiteral("snapshot.png"));
	QPointer<QDialog> dialog = runTexturePngExportDialog(nullptr, large, output, false, [&](const auto& value) { result = value; finished = true; });
	auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("cancelTexturePngExport"));
	auto* accessible = QAccessible::queryAccessibleInterface(cancel);
	ok &= expect(cancel && cancel->isEnabled() && cancel->focusPolicy() != Qt::NoFocus && accessible &&
		!accessible->text(QAccessible::Name).isEmpty(), "visible cancellation exposes a focusable accessible button");
	large.fill(Qt::green);
	ok &= expect(until([&] { return finished; }) && result.succeeded && !result.cancelled && ticks > 2,
		"PNG preparation and publication keep the event loop responsive");
	const QImage saved(output);
	ok &= expect(saved.size() == QSize(2048, 4096) && saved.pixelColor(0, 0) == QColor(30, 70, 90, 140),
		"export owns the selected pixel snapshot and accepts images larger than the authoring limit");

	finished = false;
	dialog = runTexturePngExportDialog(nullptr, pixels, targetPath, false, [&](const auto& value) { result = value; finished = true; });
	ok &= expect(until([&] { return finished; }) && !result.succeeded && !result.cancelled && !result.error.isEmpty() &&
		read(targetPath) == changed && dialog && dialog->isVisible(), "unapproved overwrite keeps the original and leaves a visible error");
	if (dialog) { dialog->close(); }
	ExpandedTranslator expanded;
	for (const auto theme : {StudioTheme::Dark, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight}) {
		const int scale = theme == StudioTheme::Dark ? 100 : 200;
		if (scale > 100) { app.installTranslator(&expanded); }
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		finished = false;
		dialog = runTexturePngExportDialog(nullptr, pixels, targetPath, false, [&](const auto& value) { result = value; finished = true; });
		dialog->setLayoutDirection(theme == StudioTheme::HighContrastDark ? Qt::RightToLeft : Qt::LeftToRight);
		ok &= expect(until([&] { return finished; }) && dialog, "export diagnostic is ready for layout verification");
		if (dialog) {
			app.processEvents();
			const auto* close = dialog->findChild<QPushButton*>(QStringLiteral("closeTexturePngExport"));
			ok &= expect(close && close->isVisible() && close->isEnabled() && close->focusPolicy() != Qt::NoFocus &&
				dialog->rect().contains(QRect(close->mapTo(dialog, QPoint()), close->size())), "failed exports retain a visible, unclipped Close action");
			for (auto* label : dialog->findChildren<QLabel*>()) {
				if (!label->isVisible()) { continue; }
				ok &= expect(dialog->rect().contains(QRect(label->mapTo(dialog, QPoint()), label->size())) &&
					label->height() >= label->heightForWidth(label->width()), "scaled and translated status/destination labels remain unclipped");
			}
			const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				QImage capture(dialog->size(), QImage::Format_ARGB32_Premultiplied); capture.fill(Qt::transparent); dialog->render(&capture);
				ok &= expect(capture.save(QDir(captures).filePath(QStringLiteral("texture-png-output-%1.png").arg(static_cast<int>(theme)))), "render the actual PNG export widgets");
			}
			dialog->close();
		}
		if (scale > 100) { app.removeTranslator(&expanded); }
	}

	finished = false;
	dialog = runTexturePngExportDialog(nullptr, pixels, targetPath, true, [&](const auto& value) { result = value; finished = true; });
	dialog->close();
	ok &= expect(until([&] { return finished; }) && result.cancelled && !result.succeeded && read(targetPath) == changed,
		"closing while preparation is queued cancels without replacing the output");
	finished = false;
	dialog = runTexturePngExportDialog(nullptr, pixels, targetPath, true, [&](const auto& value) { result = value; finished = true; });
	ok &= expect(until([&] { return finished; }) && result.succeeded && QImage(targetPath) == pixels, "explicit overwrite publishes the PNG through the guarded path");
	const auto retired = temporary.filePath(QStringLiteral("retired.png"));
	auto* owner = new QWidget;
	runTexturePngExportDialog(owner, pixels, retired, false);
	delete owner; QCoreApplication::processEvents();
	ok &= expect(!QFile::exists(retired), "owner destruction retires queued work before it can write");
	std::cout << "PNG export heartbeat ticks: " << ticks << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
