#include "app/audio_session_dialog.h"
#include "app/audio_stem_export_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_stream.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
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
bool finish(AudioSessionDialog &dialog)
{
	QElapsedTimer clock;
	clock.start();
	while (dialog.isBusy() && clock.elapsed() < 30000) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	QCoreApplication::processEvents();
	return !dialog.isBusy();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
class Expanded final : public QTranslator {
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).contains("Audio"))
			return {};
		return QString::fromUtf8(source) + " · " + QString::fromUtf8(source);
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
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("stems-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setRecoveryEnabled(false);
	AudioProject source;
	source.sourceName = "Dialogue";
	source.clip = {2, 48000, {.25f, -.5f, .5f, .25f}};
	source.endFrame = 2;
	bool ok = expect(session.importSource(source) && finish(session), "import GUI stem fixture");
	session.setSelection(0, 20001);
	const auto before = encodeAudioSession(session.session());
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioStemExportDialog *>();
		if (!dialog) {
			ok = false;
			return;
		}
		dialog->findChild<QLineEdit *>("stemDirectory")->setText(temporary.path());
		dialog->findChild<QLineEdit *>("stemPrefix")->setText("gui");
		auto *format = dialog->findChild<QComboBox *>("stemFormat");
		format->setCurrentIndex(format->findData(int(AudioWavFormat::Pcm24)));
		dialog->findChild<QCheckBox *>("stemDither")->setChecked(true);
		dialog->findChild<QLineEdit *>("stemSeed")->setText("18446744073709551616");
		ok &= expect(!dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->isEnabled(),
		             "overflowing seed disables export");
		dialog->findChild<QLineEdit *>("stemSeed")->setText("123");
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
	});
	session.findChild<QAction *>("sessionExportStems")->trigger();
	ok &= expect(finish(session) && session.deliveryReport()["completed"].toInt() == 2 &&
	                 encodeAudioSession(session.session()) == before,
	             "actual GUI action exports asynchronously without changing session history");
	const auto delivery = session.deliveryReport()["delivery"].toObject();
	AudioStemExportRequest equivalent;
	equivalent.directory = temporary.path();
	equivalent.prefix = "core";
	equivalent.stripIds = {session.session().tracks[0].id};
	equivalent.includeMaster = true;
	equivalent.end = 20001;
	equivalent.format = AudioWavFormat::Pcm24;
	equivalent.dither = true;
	equivalent.ditherSeed = 123;
	const auto expected = writeAudioSessionStems(session.session(), equivalent);
	for (int i = 0; i < 2; ++i)
		ok &= expect(read(expected.plan.files[i].path) ==
		                 read(temporary.filePath(delivery["files"].toArray()[i].toObject()["file"].toString())),
		             "GUI WAV bytes equal shared core delivery including dither");
	bool inspected = false;
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog)
			return;
		const auto *text = dialog->findChild<QPlainTextEdit *>();
		inspected = text && text->toPlainText().contains("sha256");
		dialog->reject();
	});
	session.findChild<QAction *>("sessionDeliveryReport")->trigger();
	ok &= expect(inspected, "delivery details expose file hashes through the actual action");
	equivalent.prefix = "cancel";
	equivalent.end = 1000000;
	equivalent.progress = [&](int index, int, qint64, qint64) {
		if (index == 1)
			QMetaObject::invokeMethod(&session, &AudioSessionDialog::cancelWork, Qt::QueuedConnection);
	};
	ok &= expect(session.exportStems(equivalent) && finish(session) &&
	                 session.deliveryReport()["completed"].toInt() == 1 &&
	                 session.deliveryReport()["delivery"].toObject()["status"] == "cancelled",
	             "GUI cancellation reports committed files rather than discarding partial delivery details");
	const auto renders = qEnvironmentVariable("VIBESTUDIO_AUDIO_STEMS_RENDERS");
	if (!renders.isEmpty())
		ok &= QDir().mkpath(renders);
	struct Layout {
		const char *name;
		StudioTheme theme;
		int scale;
		bool rtl;
	};
	for (auto layout : {Layout{"dark-100", StudioTheme::Dark, 100, false},
	                    Layout{"high-visibility-125", StudioTheme::HighContrastLight, 125, false},
	                    Layout{"expanded-rtl-200", StudioTheme::HighContrastDark, 200, true}}) {
		Expanded expanded;
		if (layout.rtl)
			app.installTranslator(&expanded);
		applyStudioTheme(app, studioThemeTokens(layout.theme, UiDensity::Comfortable, layout.scale));
		AudioStemExportDialog dialog(session.session(), 0, 20001);
		dialog.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.findChild<QLineEdit *>("stemDirectory")->setText(temporary.path());
		dialog.show();
		app.processEvents();
		auto *scroll = dialog.findChild<QScrollArea *>();
		for (const auto *name :
		     {"stemStrips", "stemMaster", "stemSolo", "stemTap", "stemFirst", "stemEnd", "stemFormat", "stemDither",
		      "stemSeed", "stemPrefix", "stemDirectory", "stemOverwrite", "stemPlan"}) {
			auto *widget = dialog.findChild<QWidget *>(name);
			ok &= expect(widget && !widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus &&
			                 QAccessible::queryAccessibleInterface(widget),
			             "native export controls expose focus and accessibility metadata");
		}
		const auto capture = [&](const QString &suffix) {
			if (renders.isEmpty())
				return;
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= image.save(QDir(renders).filePath(QString::fromLatin1(layout.name) + suffix + ".png"));
		};
		capture("");
		dialog.resize(640, 420);
		app.processEvents();
		ok &= expect(dialog.width() == 640 && scroll->horizontalScrollBar()->maximum() == 0,
		             "small expanded export dialog has no horizontal overflow");
		capture("-small");
		for (const auto *name : {"stemTap", "stemFirst", "stemEnd", "stemFormat", "stemDither", "stemSeed",
		                         "stemPrefix", "stemDirectory", "stemOverwrite"}) {
			auto *widget = dialog.findChild<QWidget *>(name);
			scroll->verticalScrollBar()->setValue(widget->mapTo(scroll->widget(), QPoint()).y() -
			                                      (scroll->viewport()->height() - widget->height()) / 2);
			app.processEvents();
			ok &= expect(
			    scroll->viewport()->rect().contains(QRect(widget->mapTo(scroll->viewport(), QPoint()), widget->size())),
			    "every export field fits within reachable scroll area");
		}
		capture("-small-output");
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	return ok ? 0 : 1;
}
