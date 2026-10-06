#include "app/audio_media_dialog.h"
#include "app/audio_session_dialog.h"
#include "app/audio_waveform_view.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/audio_media_fixture.h"
#include "tests/fake_audio_stream.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
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
template <typename Dialog> bool finish(Dialog &dialog)
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
	QTemporaryDir temporary(QDir(root).filePath("media-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	const auto path = [&](const char *name) { return temporary.filePath(QLatin1String(name)); };
	auto original = test::mediaFixture();
	original.sources[0].audio.sourcePath = path("missing.wav");
	original.sources[1].audio.sourcePath = path("unused.wav");
	const auto originalBytes = encodeAudioSession(original);
	auto replacement = original.sources[0].audio;
	for (auto &value : replacement.clip.samples)
		value *= .25f;
	const auto candidateBytes = encodeAudioProject(replacement);
	bool ok = expect(test::writeMediaFixture(path("song.vssession"), originalBytes) &&
	                     test::writeMediaFixture(path("replacement.vsaudio"), candidateBytes) &&
	                     test::writeMediaFixture(path("unused.wav"), "protected source"),
	                 "write private media UI fixtures");
	if (!ok)
		return EXIT_FAILURE;
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	ok &= expect(session.openSession(path("song.vssession")) && finish(session), "open session asynchronously");
	session.selectRegion("ta", "a");
	auto *mediaAction = session.findChild<QAction *>("sessionMedia");
	bool opened = false;
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioMediaDialog *>();
		if (!dialog)
			return;
		opened = true;
		ok &= expect(finish(*dialog) && dialog->edit().sourceIds == QStringList{"media"},
		             "media selection follows the focused clip");
		dialog->findChild<QLineEdit *>("mediaName")->setText("Reviewed source");
		dialog->accept();
		ok &= expect(dialog->result() != QDialog::Accepted, "unreviewed request cannot be applied");
		dialog->review();
		ok &= expect(finish(*dialog) && dialog->isReviewed(),
		             "rename review is asynchronous and stages a valid proposal");
		dialog->accept();
	});
	if (mediaAction)
		mediaAction->trigger();
	ok &= expect(opened && finish(session) && session.session().sources[0].audio.sourceName == "Reviewed source",
	             "toolbar adopts a reviewed media edit");
	session.undo();
	ok &= expect(encodeAudioSession(session.session()) == originalBytes, "media rename is one undo step");
	AudioMediaDialog review(original, "media");
	ok &= expect(finish(review), "source inventory completes");
	auto *operation = review.findChild<QComboBox *>("mediaOperation");
	operation->setCurrentIndex(operation->findData("replace"));
	review.findChild<QLineEdit *>("mediaPath")->setText(path("replacement.vsaudio"));
	review.review();
	ok &= expect(finish(review) && review.isReviewed() && review.candidate().succeeded(),
	             "replacement review retains a decoded file and digest");
	const auto candidate = review.candidate();
	review.findChild<QCheckBox *>("mediaResample")->setChecked(true);
	ok &= expect(!review.isReviewed(), "changing a review option invalidates approval");
	review.review();
	review.cancelReview();
	ok &= expect(finish(review) && !review.isReviewed(), "cancelled worker cannot publish a prepared change");
	review.findChild<QCheckBox *>("mediaResample")->setChecked(false);
	review.review();
	ok &= expect(finish(review) && review.isReviewed(), "review can restart after cancellation");
	ok &= expect(session.applyMedia(review.edit(), review.candidate()) && finish(session),
	             "parent applies reviewed replacement on its session worker");
	const auto changed = encodeAudioSession(session.session());
	ok &= expect(session.session().sources[0].id != "media" &&
	                 session.session().sources[0].audio.clip.samples == replacement.clip.samples &&
	                 session.retainedWaveformBytes() > 0,
	             "replacement has fresh waveform identity and retained undo data");
	session.undo();
	ok &= expect(encodeAudioSession(session.session()) == originalBytes,
	             "undo restores the original source and all clip references");
	session.redo();
	ok &= expect(encodeAudioSession(session.session()) == changed, "redo restores the same replacement identity");
	ok &= expect(session.saveSession(path("saved.vssession")) && finish(session), "save reviewed media edit");
	AudioSessionDialog reopened(nullptr, fakeAudioStreamFactory());
	reopened.setAttribute(Qt::WA_DeleteOnClose, false);
	reopened.setRecoveryEnabled(false);
	ok &= expect(reopened.openSession(path("saved.vssession")) && finish(reopened) &&
	                 encodeAudioSession(reopened.session()) == changed,
	             "GUI reload retains exact replacement session");
	ok &= test::writeMediaFixture(path("replacement.vsaudio"), encodeAudioProject(original.sources[0].audio));
	ok &= expect(session.applyMedia({"replace", {session.session().sources[0].id}}, candidate) && finish(session) &&
	                 encodeAudioSession(session.session()) == changed,
	             "parent rejects a stale reviewed file without creating an undo step");
	ok &= test::writeMediaFixture(path("replacement.vsaudio"), candidateBytes);
	ok &= expect(session.applyMedia({"prune"}) && finish(session) && session.session().sources.size() == 1,
	             "GUI prunes unused embedded sources");
	session.setSelection(0, 300);
	ok &= expect(session.exportMix(path("unused.wav"), true) && finish(session) &&
	                 test::readMediaFixture(path("unused.wav")) == "protected source",
	             "pruned source still held by undo cannot be overwritten by mixdown");
	session.undo();
	ok &= expect(encodeAudioSession(session.session()) == changed, "undo restores pruned source");
	const auto renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_MEDIA_RENDER_DIR");
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
		AudioMediaDialog dialog(original, "media");
		dialog.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.show();
		ok &= finish(dialog);
		auto *choice = dialog.findChild<QComboBox *>("mediaOperation");
		auto *scroll = dialog.findChild<QScrollArea *>();
		auto *tabs = dialog.findChild<QTabWidget *>();
		for (const auto &op : {"rename", "relink", "replace", "remove", "prune"}) {
			choice->setCurrentIndex(choice->findData(op));
			dialog.findChild<QLineEdit *>("mediaPath")->setText(path("replacement.vsaudio"));
			if (QString(op) == "remove")
				dialog.findChild<QTreeWidget *>("mediaSources")
				    ->setCurrentItem(dialog.findChild<QTreeWidget *>("mediaSources")->topLevelItem(1));
			else
				dialog.findChild<QTreeWidget *>("mediaSources")
				    ->setCurrentItem(dialog.findChild<QTreeWidget *>("mediaSources")->topLevelItem(0));
			if (QString(op) == "relink") {
				ok &= test::writeMediaFixture(path("identical.vsaudio"), encodeAudioProject(original.sources[0].audio));
				dialog.findChild<QLineEdit *>("mediaPath")->setText(path("identical.vsaudio"));
			}
			dialog.review();
			ok &= expect(finish(dialog) && dialog.isReviewed(), "each media operation has a reviewable UI proposal");
			for (bool compact : {false, true}) {
				dialog.resize(compact ? QSize(460, 420) : QSize(880, 940));
				QCoreApplication::processEvents();
				for (const auto &id :
				     {"mediaSources", "mediaOperation", "mediaName", "mediaPath", "mediaResample", "mediaReview"}) {
					auto *control = dialog.findChild<QWidget *>(id);
					if (!control->isEnabled())
						continue;
					const int y = control->mapTo(scroll->widget(), QPoint{}).y();
					scroll->verticalScrollBar()->setValue(y - (scroll->viewport()->height() - control->height()) / 2);
					QCoreApplication::processEvents();
					const QRect rect(control->mapTo(scroll->viewport(), QPoint{}), control->size());
					if (!scroll->viewport()->rect().contains(rect))
						std::cerr << layout.name << ' ' << op << ' ' << compact << ' ' << id << " rect=" << rect.x()
						          << ',' << rect.y() << ',' << rect.width() << ',' << rect.height()
						          << " viewport=" << scroll->viewport()->width() << ',' << scroll->viewport()->height()
						          << '\n';
					ok &= expect(scroll->viewport()->rect().contains(rect),
					             "media controls remain reachable at narrow widths and expanded translations");
					const auto *accessible = QAccessible::queryAccessibleInterface(control);
					ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
					                 control->focusPolicy() != Qt::NoFocus,
					             "media controls provide accessible names and native keyboard focus");
				}
				auto *buttons = dialog.findChild<QDialogButtonBox *>();
				ok &= expect(dialog.rect().contains(QRect(buttons->mapTo(&dialog, QPoint{}), buttons->size())),
				             "apply and cancel stay outside the scrolling controls");
				for (int view = 0; view < ((QString(op) == "replace" || QString(op) == "relink") ? 2 : 1); ++view) {
					tabs->setCurrentIndex(view);
					scroll->verticalScrollBar()->setValue(compact ? scroll->verticalScrollBar()->maximum() : 0);
					QCoreApplication::processEvents();
					if (!renderRoot.isEmpty()) {
						QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
						image.fill(Qt::transparent);
						dialog.render(&image, QPoint{}, QRegion(dialog.rect()),
						              QWidget::DrawWindowBackground | QWidget::DrawChildren | QWidget::IgnoreMask);
						ok &= image.save(QDir(renderRoot)
						                     .filePath(QString::fromLatin1(layout.name) + (compact ? "-small-" : "-") +
						                               op + '-' + QString::number(view) + ".png"));
					}
				}
			}
		}
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	std::cout << (ok ? "Media UI verification passed\n" : "Media UI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
