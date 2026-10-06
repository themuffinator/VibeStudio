#include "app/audio_routing_dialog.h"
#include "app/audio_session_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_stream.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
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
bool wait(const std::function<bool()> &condition)
{
	QElapsedTimer clock;
	clock.start();
	while (!condition() && clock.elapsed() < 15000) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	QCoreApplication::processEvents();
	return condition();
}
class Expanded final : public QTranslator {
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).endsWith("AudioRoutingDialog"))
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
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("routing-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	AudioProject source;
	source.sourceName = "Dialogue";
	source.clip = {2, 48000, {.25f, -.5f, .5f, .25f}};
	source.endFrame = 2;
	bool ok = expect(session.importSource(source) && wait([&] { return !session.isBusy(); }),
	                 "import session routing fixture");
	const auto track = session.session().tracks[0].id;
	AudioSessionEdit add;
	add.operation = "add-bus";
	add.name = "Dialogue bus";
	ok &= expect(session.applyEdit(add, "Add bus"), "shared bus edit enters session history");
	const auto bus = session.session().tracks.last().id;
	ok &= expect(!session.openTake(), "recorded takes cannot be imported onto a bus");
	session.selectRegion(track, {});
	bool reviewed = false;
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioRoutingDialog *>();
		if (!dialog)
			return;
		auto *output = dialog->findChild<QComboBox *>("routingOutput");
		output->setCurrentIndex(output->findData(bus));
		dialog->findChild<QCheckBox *>("routingLeft")->setChecked(true);
		dialog->findChild<QPushButton *>("routingAddSend")->click();
		dialog->findChild<QComboBox *>("routingSendTap")->setCurrentIndex(1);
		dialog->findChild<QDoubleSpinBox *>("routingSendGain")->setValue(-6);
		dialog->findChild<QCheckBox *>("routingSendEnabled")->setChecked(true);
		reviewed = dialog->edit().routing.sends.size() == 1;
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	session.findChild<QAction *>("sessionRouting")->trigger();
	ok &= expect(reviewed && session.session().tracks[0].routing.outputId == bus &&
	                 session.session().tracks[0].routing.invertLeft,
	             "actual session action applies reviewed output, polarity and send together");
	session.undo();
	ok &= expect(session.session().tracks[0].routing == AudioTrackRouting{}, "routing changes undo atomically");
	session.redo();
	ok &= expect(session.session().tracks[0].routing.sends[0].preFader, "redo restores the complete send state");
	const auto saved = QDir(temporary.path()).filePath("routed.vssession");
	ok &= expect(session.saveSession(saved) && wait([&] { return !session.isBusy(); }),
	             "routed session saves asynchronously");
	AudioSession read;
	QString error;
	ok &= expect(readAudioSession(saved, &read, nullptr, &error) &&
	                 audioSessionSummary(read) == audioSessionSummary(session.session()),
	             "native reload retains exact UI routing state");
	AudioSessionEdit another;
	another.operation = "add-bus";
	another.name = "Return";
	const auto added = editAudioSession(read, another);
	read = added.session;
	read.tracks.last().routing.outputId = bus;
	AudioRoutingDialog invalid(read, bus);
	auto *destination = invalid.findChild<QComboBox *>("routingOutput");
	destination->setCurrentIndex(destination->findData(read.tracks.last().id));
	ok &= expect(!invalid.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled() &&
	                 !invalid.findChild<QLabel *>("routingStatus")->text().isEmpty(),
	             "draft cycle exposes validation and disables Apply");
	invalid.reject();
	ok &= expect(session.session().tracks.size() == 2, "cancelled invalid routing never mutates the parent session");
	const auto renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_ROUTING_RENDER_DIR");
	if (!renderRoot.isEmpty())
		ok &= QDir().mkpath(renderRoot);
	struct Layout {
		StudioTheme theme;
		int scale;
		bool rtl;
		const char *name;
	};
	for (const auto &layout :
	     {Layout{StudioTheme::Dark, 100, false, "dark"}, Layout{StudioTheme::HighContrastLight, 125, false, "contrast"},
	      Layout{StudioTheme::HighContrastDark, 200, true, "expanded-rtl"}}) {
		Expanded expanded;
		if (layout.rtl)
			app.installTranslator(&expanded);
		applyStudioTheme(app, studioThemeTokens(layout.theme, UiDensity::Comfortable, layout.scale));
		AudioRoutingDialog dialog(session.session(), track);
		dialog.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.resize(920, 760);
		dialog.show();
		QCoreApplication::processEvents();
		if (layout.rtl)
			ok &= expect(dialog.findChild<QPushButton *>("routingAddSend")->text().contains(" · "),
			             "expanded translator reaches the actual namespaced routing context");
		for (auto *combo : dialog.findChildren<QComboBox *>())
			ok &= expect(!combo->accessibleName().isEmpty() && combo->focusPolicy() != Qt::NoFocus,
			             "routing selectors expose names and focus");
		for (auto *scroll : dialog.findChildren<QScrollArea *>())
			ok &= expect(scroll->horizontalScrollBar()->maximum() == 0,
			             "expanded routing form wraps without horizontal clipping");
		ok &= expect(dialog.findChild<QListWidget *>()->horizontalScrollBar()->maximum() == 0,
		             "expanded send summaries wrap to the list viewport");
		if (!renderRoot.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= image.save(QDir(renderRoot).filePath(QString::fromLatin1(layout.name) + ".png"));
			auto *scroll = dialog.findChild<QScrollArea *>();
			scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
			QCoreApplication::processEvents();
			dialog.render(&image);
			ok &= image.save(QDir(renderRoot).filePath(QString::fromLatin1(layout.name) + "-send.png"));
		}
		dialog.resize(600, 400);
		QCoreApplication::processEvents();
		ok &=
		    expect(dialog.width() == 600 && dialog.findChild<QScrollArea *>()->horizontalScrollBar()->maximum() == 0 &&
		               dialog.findChild<QListWidget *>()->horizontalScrollBar()->maximum() == 0,
		           "small expanded routing view retains the requested width without horizontal scrolling");
		ok &= expect(dialog.findChild<QScrollArea *>()->verticalScrollBar()->maximum() > 0,
		             "small routing view keeps every control reachable by scrolling");
		if (!renderRoot.isEmpty()) {
			QImage small(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			small.fill(Qt::transparent);
			dialog.render(&small);
			ok &= small.save(QDir(renderRoot).filePath(QString::fromLatin1(layout.name) + "-small.png"));
		}
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
