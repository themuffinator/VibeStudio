#include "app/application_shell.h"
#include "app/audio_session_dialog.h"
#include "app/audio_take_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_capture.h"
#include "tests/fake_audio_playback.h"
#include "tests/fake_audio_stream.h"
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::test;
namespace
{
bool expect(bool value, const char *text)
{
	if (!value)
		std::cerr << text << '\n';
	return value;
}
bool wait(const std::function<bool()> &condition, int ms = 15000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	QCoreApplication::processEvents();
	return condition();
}
class Expanded final : public QTranslator {
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).contains("AudioTakeDialog"))
			return {};
		return QString::fromUtf8(source) + QStringLiteral(" · ") + QString::fromUtf8(source);
	}
};
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication application(argc, argv);
#ifdef Q_OS_WIN
	application.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("take-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QLatin1String(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	auto fake = std::make_shared<CaptureFixture>();
	QVector<float> signal{.25f, .5f, -.5f, -1, .75f, 1.5f};
	fake->bytes = QByteArray(reinterpret_cast<const char *>(signal.constData()), signal.size() * 4);
	fake->maxRead = 3;
	AudioTakeDialog dialog(48000, {}, {}, 100, nullptr, captureFactory(fake));
	dialog.show();
	auto *capture = dialog.findChild<AudioCapture *>();
	bool ok = expect(wait([&] { return capture->available(); }) && fake->openCount() == 0,
	                 "opening record view only enumerates inputs");
	dialog.findChild<QLineEdit *>("takePath")->setText(path("voice.vstake"));
	dialog.findChild<QComboBox *>("takeInput")->setCurrentIndex(1);
	dialog.findChild<QSpinBox *>("takeInputChannels")->setValue(2);
	dialog.findChild<QLineEdit *>("takeChannels")->setText("2,1");
	dialog.findChild<QSpinBox *>("takeLatency")->setValue(10);
	auto *record = dialog.findChild<QPushButton *>("takeRecord");
	ok &= expect(!record->isEnabled(), "input capture requires arming");
	dialog.findChild<QCheckBox *>("takeArm")->setChecked(true);
	ok &= expect(record->isEnabled() && fake->openCount() == 0, "arming never opens an input");
	record->click(); // Direct control method; no OS mouse/keyboard automation.
	ok &= expect(wait([&] { return fake->consumed(); }), "record control captures simulated channel data");
	dialog.findChild<QPushButton *>("takeStop")->click();
	ok &= expect(wait([&] { return !dialog.busy(); }) && capture->snapshot().take.frames == 3 &&
	                 capture->snapshot().take.complete,
	             "stop publishes a complete reviewable take");
	ok &= expect(capture->snapshot().samplesAboveFullScale == 1 && capture->snapshot().peak[0] == 1.5f,
	             "recording reports headroom without clipping captured samples");
	ok &= expect(dialog.findChild<QDoubleSpinBox *>("takePlace")->value() == 90,
	             "review applies measured placement compensation");
	dialog.findChild<QDoubleSpinBox *>("takeFirst")->setValue(1);
	ok &= expect(dialog.findChild<QDoubleSpinBox *>("takePlace")->value() == 91,
	             "range trim adjusts compensated timeline placement");
	dialog.findChild<QPushButton *>("takeImport")->click();
	ok &= expect(wait([&] { return !dialog.busy(); }) && dialog.result() == QDialog::Accepted &&
	                 dialog.importedAudio().clip.samples == QVector<float>{-1, -.5f, 1.5f, .75f},
	             "reviewed range returns exact selected channel samples");
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	ok &= expect(session.importSource(dialog.importedAudio(), {}, dialog.importPosition()) &&
	                 wait([&] { return !session.isBusy(); }) && session.session().tracks.size() == 1 &&
	                 session.session().tracks[0].regions[0].position == 91,
	             "take handoff enters shared session import and exact placement");
	session.undo();
	ok &= expect(session.session().tracks.isEmpty(), "recorded take import is one undo step");
	session.redo();
	ok &= expect(session.session().sources[0].audio.sourcePath == path("voice.vstake"),
	             "redo retains protected recording provenance");
	ok &= expect(session.saveSession(path("voice.vssession")) && wait([&] { return !session.isBusy(); }),
	             "recorded session saves through native services");
	{
		ApplicationShell shell(nullptr, std::make_unique<FakeAudioPlaybackBackend>());
		shell.openPathFromCommandLine(path("voice.vstake"));
		auto *review = shell.findChild<AudioTakeDialog *>();
		ok &= expect(review && wait([&] { return !review->busy(); }) &&
		                 review->findChild<QPushButton *>("takeImport")->isEnabled(),
		             "normal shell Open routes takes to asynchronous review");
		if (review)
			review->close();
	}
	AudioTakeWriter unfinished;
	AudioTakeMetadata metadata;
	metadata.name = "Interrupted voice take";
	metadata.position = 50;
	QString error;
	ok &= expect(unfinished.open(path("prefix.vstake"), metadata, &error) &&
	                 unfinished.append(std::array<float, 3>{.25f, -.5f, .75f}, &error),
	             "create incomplete take UI fixture");
	unfinished.close();
	AudioTakeDialog recovery(48000, {}, {}, 0, nullptr, captureFactory(std::make_shared<CaptureFixture>()));
	recovery.inspectTake(path("prefix.vstake"));
	ok &=
	    expect(wait([&] { return !recovery.busy(); }) && !recovery.findChild<QPushButton *>("takeImport")->isEnabled(),
	           "incomplete take import requires explicit prefix acceptance");
	recovery.findChild<QCheckBox *>("takeAcceptPrefix")->setChecked(true);
	recovery.findChild<QPushButton *>("takeImport")->click();
	ok &= expect(wait([&] { return !recovery.busy(); }) && recovery.result() == QDialog::Accepted &&
	                 recovery.importedAudio().clip.frameCount() == 3,
	             "reviewed prefix recovery retains only verified frames");
	const auto renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_TAKE_RENDER_DIR");
	if (!renderRoot.isEmpty())
		ok &= expect(QDir().mkpath(renderRoot), "create task render directory");
	struct Layout {
		StudioTheme theme;
		int scale;
		bool rtl;
		const char *name;
	};
	for (const auto &config :
	     {Layout{StudioTheme::Dark, 100, false, "dark"}, Layout{StudioTheme::HighContrastLight, 125, false, "contrast"},
	      Layout{StudioTheme::HighContrastDark, 200, true, "expanded-rtl"}}) {
		Expanded expanded;
		if (config.rtl)
			application.installTranslator(&expanded);
		applyStudioTheme(application, studioThemeTokens(config.theme, UiDensity::Comfortable, config.scale));
		StudioSettings preferences;
		auto accessibility = preferences.accessibilityPreferences();
		accessibility.reducedMotion = config.rtl;
		preferences.setAccessibilityPreferences(accessibility);
		preferences.sync();
		auto layoutInput = std::make_shared<CaptureFixture>();
		layoutInput->bytes = QByteArray(4096 * 4, '\0');
		AudioTakeDialog view(48000, {}, {}, 0, nullptr, captureFactory(layoutInput));
		view.setLayoutDirection(config.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		view.resize(920, 760);
		view.show();
		for (auto *control : view.findChildren<QLineEdit *>())
			if (control->objectName().startsWith("take"))
				ok &= expect(!control->accessibleName().isEmpty() && control->focusPolicy() != Qt::NoFocus,
				             "take text fields expose names and keyboard focus");
		for (int page = 0; page < 2; ++page) {
			if (page) {
				view.findChild<QPushButton *>("takeStop")->click();
				ok &= wait([&] { return !view.busy(); });
				view.inspectTake(path("prefix.vstake"));
				ok &= wait([&] { return !view.busy(); });
			} else {
				ok &= wait([&] { return view.findChild<AudioCapture *>()->available(); });
				view.findChild<QLineEdit *>("takePath")
				    ->setText(QDir(temporary.path()).filePath(QString::fromLatin1(config.name) + ".vstake"));
				view.findChild<QComboBox *>("takeInput")->setCurrentIndex(1);
				view.findChild<QCheckBox *>("takeArm")->setChecked(true);
				view.findChild<QPushButton *>("takeRecord")->click();
				ok &= wait([&] { return view.findChild<AudioCapture *>()->snapshot().storedFrames == 4096; });
			}
			view.findChild<QTabWidget *>()->setCurrentIndex(page);
			QCoreApplication::processEvents();
			for (auto *scroll : view.findChildren<QScrollArea *>())
				if (scroll->isVisible())
					ok &= expect(scroll->horizontalScrollBar()->maximum() == 0,
					             "expanded take layout wraps without horizontal clipping");
			for (auto *label : view.findChildren<QLabel *>())
				if (label->isVisible() && label->wordWrap()) {
					ok &= expect(label->height() >= label->heightForWidth(label->width()),
					             "wrapped take labels reserve all lines without vertical clipping");
					if (label->height() < label->heightForWidth(label->width()))
						std::cerr << config.name << " page=" << page << " label=" << label->text().toStdString()
						          << " width=" << label->width() << " height=" << label->height()
						          << " required=" << label->heightForWidth(label->width())
						          << " hfw=" << label->hasHeightForWidth()
						          << " parent-width=" << label->parentWidget()->width()
						          << " parent-height=" << label->parentWidget()->height() << " parent-hfw="
						          << label->parentWidget()->heightForWidth(label->parentWidget()->width()) << '\n';
				}
			if (!renderRoot.isEmpty()) {
				QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				view.render(&image);
				ok &= expect(image.save(QDir(renderRoot)
				                            .filePath(QString::fromLatin1(config.name) +
				                                      (page ? "-review.png" : "-record.png"))),
				             "direct widget render without OS capture");
				auto *last = view.findChild<QPushButton *>(page ? "takeImport" : "takeRecord");
				for (auto *scroll : view.findChildren<QScrollArea *>())
					if (scroll->isVisible())
						scroll->ensureWidgetVisible(last, 12, 12);
				QCoreApplication::processEvents();
				view.render(&image);
				ok &= expect(image.save(QDir(renderRoot)
				                            .filePath(QString::fromLatin1(config.name) +
				                                      (page ? "-review-controls.png" : "-record-controls.png"))),
				             "render reachable lower take controls without OS capture");
			}
			if (!page && config.rtl)
				ok &= expect(view.findChild<QProgressBar *>()->maximum() == 1,
				             "reduced motion uses a static capture indicator");
		}
		view.resize(600, 400);
		QCoreApplication::processEvents();
		bool scrolls = false;
		for (auto *scroll : view.findChildren<QScrollArea *>())
			scrolls |= scroll->verticalScrollBar()->maximum() > 0;
		ok &= expect(scrolls, "scaled take controls remain reachable through scrolling");
		if (config.rtl)
			application.removeTranslator(&expanded);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
