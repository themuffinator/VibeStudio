#include "app/audio_meter_dialog.h"
#include "app/audio_session_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/audio_media_fixture.h"
#include "tests/fake_audio_stream.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>
#include <QTreeWidget>
#include <iostream>
using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool finish(AudioSessionDialog &dialog)
{
	QElapsedTimer timer;
	timer.start();
	while (dialog.isBusy() && timer.elapsed() < 30000) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	QCoreApplication::processEvents();
	return !dialog.isBusy();
}
class Expanded final : public QTranslator {
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		return QByteArray(context).contains("Audio") ? QString::fromUtf8(source) + " · " + QString::fromUtf8(source)
		                                             : QString();
	}
};
} // namespace
int main(int argc, char **argv)
{
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
	QTemporaryDir temporary(QDir(root).filePath("meter-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	const auto s = test::mediaFixture();
	const auto native = temporary.filePath("song.vssession");
	const auto bytes = encodeAudioSession(s);
	if (!test::writeMediaFixture(native, bytes))
		return EXIT_FAILURE;
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	bool ok = expect(session.openSession(native) && finish(session), "open private session");
	session.setSelection(0, 300);
	auto *action = session.findChild<QAction *>("sessionMeters");
	if (action)
		action->trigger();
	auto *dialog = session.findChild<AudioMeterDialog *>();
	if (!expect(dialog && dialog->isVisible(), "toolbar opens modeless meters"))
		return EXIT_FAILURE;
	dialog->findChild<QPushButton *>("meterAnalyze")->click();
	ok &= expect(finish(session) && dialog->meters().count == s.tracks.size() + 1 &&
	                 dialog->meters().strips[0].post.frames == 300 && !session.hasChanges() &&
	                 encodeAudioSession(session.session()) == bytes,
	             "offline worker meters range without changing document or undo");
	auto *tap = dialog->findChild<QComboBox *>("meterTap");
	tap->setCurrentIndex(1);
	ok &= expect(dialog->meters().strips[0].pre.frames == 300 && !session.hasChanges(),
	             "tap switching retains both readings without rerender or edits");
	dialog->findChild<QPushButton *>("meterReset")->click();
	ok &= expect(dialog->meters().count == 0, "offline reset clears retained report");
	session.setSelection(0, AudioSessionFrameLimit);
	dialog->findChild<QPushButton *>("meterAnalyze")->click();
	dialog->findChild<QPushButton *>("meterCancel")->click();
	ok &= expect(finish(session) && dialog->meters().count == 0 && !session.hasChanges(),
	             "cancel yields no partial range report");
	const auto report = measureAudioSessionMeters(s, 0, 300);
	const QString renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_METER_RENDER_DIR");
	if (!renderRoot.isEmpty())
		ok &= QDir().mkpath(renderRoot);
	Expanded expanded;
	const auto baseFont = app.font();
	struct Layout {
		const char *name;
		bool contrast, rtl;
	};
	for (const auto layout :
	     {Layout{"dark", false, false}, Layout{"contrast", true, false}, Layout{"expanded-rtl", false, true}}) {
		applyStudioTheme(app, studioThemeTokens(layout.rtl        ? StudioTheme::HighContrastDark
		                                        : layout.contrast ? StudioTheme::HighContrastLight
		                                                          : StudioTheme::Dark,
		                                        UiDensity::Comfortable, layout.rtl ? 200 : 100));
		if (layout.rtl)
			app.installTranslator(&expanded);
		app.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		AudioMeterDialog view;
		view.setContext(s, 1, 0, 300);
		view.setReport(report);
		view.show();
		for (bool compact : {false, true}) {
			const QSize requested = compact ? QSize(480, 540) : QSize(1160, 760);
			view.resize(requested);
			QCoreApplication::processEvents();
			ok &= expect(view.width() <= requested.width() && view.height() <= requested.height(),
			             "meter dialog respects the requested compact viewport at expanded text sizes");
			for (const auto &id : {"meterTap", "meterAnalyze", "meterReset", "meterTable"}) {
				auto *control = view.findChild<QWidget *>(id);
				auto *scroll = view.findChild<QScrollArea *>("meterScroll");
				if (scroll->widget()->isAncestorOf(control)) {
					scroll->ensureWidgetVisible(control);
					QCoreApplication::processEvents();
					// Large meter tables scroll internally and need not fit at once,
					// but their rows must retain a readable height within the body.
					ok &= expect(scroll->viewport()->rect().intersects(
					                 QRect(control->mapTo(scroll->viewport(), QPoint{}), control->size())),
					             "scrolling reaches the signal point and meter table");
					if (control->objectName() == "meterTap")
						ok &= expect(scroll->viewport()->rect().contains(
						                 QRect(control->mapTo(scroll->viewport(), QPoint{}), control->size())),
						             "signal selector remains fully visible after scrolling");
				} else
					ok &=
					    expect(control && view.rect().contains(QRect(control->mapTo(&view, QPoint{}), control->size())),
					           "meter controls remain in the scaled/narrow dialog");
				const auto *accessible = QAccessible::queryAccessibleInterface(control);
				ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
				                 control->focusPolicy() != Qt::NoFocus,
				             "native meter controls have names and keyboard focus");
			}
			ok &= expect(view.findChild<QTreeWidget *>("meterTable")->topLevelItemCount() == s.tracks.size() + 1,
			             "every strip plus master has a native meter row");
			const auto table = view.findChild<QTreeWidget *>("meterTable");
			ok &= expect(table->viewport()->height() >= table->fontMetrics().height() * 3,
			             "scaled table retains space for readable meter rows");
			auto *scroll = view.findChild<QScrollArea *>("meterScroll");
			scroll->verticalScrollBar()->setValue(compact ? table->mapTo(scroll->widget(), QPoint{}).y() : 0);
			QCoreApplication::processEvents();
			ok &= expect(scroll->viewport()
			                     ->rect()
			                     .intersected(QRect(table->mapTo(scroll->viewport(), QPoint{}), table->size()))
			                     .height() >= table->fontMetrics().height() * 4,
			             "outer viewport can display the meter header and multiple rows together");
			const auto analyze = view.findChild<QPushButton *>("meterAnalyze");
			const auto reset = view.findChild<QPushButton *>("meterReset");
			ok &= expect(!QRect(analyze->mapTo(&view, QPoint{}), analyze->size())
			                  .intersects(QRect(reset->mapTo(&view, QPoint{}), reset->size())),
			             "expanded action buttons do not overlap");
			for (int signal = 0; signal < 2; ++signal) {
				view.findChild<QComboBox *>("meterTap")->setCurrentIndex(signal);
				QCoreApplication::processEvents();
				if (!renderRoot.isEmpty()) {
					QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
					image.fill(Qt::transparent);
					view.render(&image);
					ok &= image.save(QDir(renderRoot)
					                     .filePath(QString::fromLatin1(layout.name) + (compact ? "-small-" : "-") +
					                               QString::number(signal) + ".png"));
				}
			}
		}
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	app.setFont(baseFont);
	std::cout << (ok ? "Meter UI verification passed\n" : "Meter UI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
