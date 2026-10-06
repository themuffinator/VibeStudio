#include "app/audio_automation_editor.h"
#include "app/audio_range_dialog.h"
#include "app/audio_session_dialog.h"
#include "app/audio_session_timeline.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/audio_range_fixture.h"
#include "tests/fake_audio_stream.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
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
bool automationEditing(const QVector<AudioAutomationPoint> &points)
{
	AudioAutomationEditor editor("Gain", -96, 24, 0, 30, 250);
	bool ok = expect(editor.setPoints(points), "editor accepts inherited curve domains");
	for (int i = 0; i < points.size(); ++i) {
		editor.selectPoint(i);
		ok &=
		    expect(editor.setPoint(i, {points[i].frame, points[i].value, points[i].curve}) && editor.points() == points,
		           "opening and applying unchanged controls retains exact inherited curves");
	}
	const auto value = audioAutomationValue(points, 30, 0);
	ok &= expect(editor.addPoint(30, value), "insert evaluated point into a cut smooth curve");
	for (qint64 frame = 0; frame < 260; ++frame)
		ok &= expect(audioAutomationValue(editor.points(), frame, 0) == audioAutomationValue(points, frame, 0),
		             "inserting an evaluated handle retains all sampled curve values");
	editor.findChild<QPushButton *>("automationRemove")->click();
	ok &= expect(validAudioAutomation(editor.points(), -96, 24) && editor.points().size() == points.size(),
	             "removing an inherited handle reauthors a valid adjacent curve");
	editor.setPoints(points);
	editor.selectPoint(1);
	editor.findChild<QComboBox *>("automationCurve")->setCurrentIndex(1);
	ok &= expect(editor.points()[1].curve == AudioAutomationCurve::Step && editor.points()[1].shape.frames == 0 &&
	                 editor.points()[0] == points[0],
	             "explicit outgoing curve changes clear only that retained segment");
	editor.setPoints(points);
	editor.selectPoint(2);
	editor.findChild<QDoubleSpinBox *>("automationValue")->setValue(-3);
	ok &= expect(!editor.points()[1].shape.frames && !editor.points()[2].shape.frames &&
	                 validAudioAutomation(editor.points(), -96, 24),
	             "point value changes explicitly reauthor both adjacent segments");
	return ok;
}
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
	QTemporaryDir temporary(QDir(root).filePath("range-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	const auto source = test::rangeFixture(true);
	const auto path = temporary.filePath("input.vssession");
	const auto original = encodeAudioSession(source);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size())
		return EXIT_FAILURE;
	file.close();
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	bool ok = expect(session.openSession(path) && finish(session), "open range GUI fixture");
	if (!ok)
		return EXIT_FAILURE;
	session.selectRegion("ta", "a");
	session.setSelection(60, 83);
	auto *action = session.findChild<QAction *>("sessionRangeControls");
	bool opened = false;
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioRangeDialog *>();
		if (!dialog)
			return;
		opened = true;
		auto *operation = dialog->findChild<QComboBox *>("rangeOperation");
		operation->setCurrentIndex(operation->findData("ripple-delete"));
		ok &= expect(dialog->edit().trackIds == QStringList{"ta"} && !dialog->edit().masterAutomation,
		             "range scope follows explicitly selected tracks, independently of group links");
		dialog->findChild<QDoubleSpinBox *>("rangeEnd")->setValue(60);
		dialog->accept();
		ok &= expect(dialog->result() != QDialog::Accepted &&
		                 !dialog->findChild<QLabel *>("rangeError")->text().isEmpty(),
		             "empty range remains staged with visible validation");
		dialog->findChild<QDoubleSpinBox *>("rangeEnd")->setValue(83);
		dialog->accept();
	});
	action->trigger();
	ok &= expect(opened && session.hasChanges() && session.session().tracks[0].regions.size() == 2 &&
	                 session.session().tracks[1].regions.size() == 1 &&
	                 session.session().tracks[1].regions[0].position == 40 &&
	                 session.findChild<QDoubleSpinBox *>("sessionRangeStart")->value() == 60 &&
	                 session.findChild<QDoubleSpinBox *>("sessionRangeEnd")->value() == 60,
	             "range toolbar commits one scoped edit and closes the selection gap");
	const auto edited = encodeAudioSession(session.session());
	session.undo();
	ok &= expect(encodeAudioSession(session.session()) == original, "range edit is one undo step");
	session.redo();
	ok &= expect(encodeAudioSession(session.session()) == edited, "redo restores exact clip and envelope identities");
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioRangeDialog *>();
		if (dialog) {
			dialog->findChild<QCheckBox *>("rangeAllTracks")->setChecked(true);
			dialog->reject();
		}
	});
	action->trigger();
	ok &= expect(encodeAudioSession(session.session()) == edited, "cancelled range preview leaves state unchanged");
	auto invalid = test::rangeEdit("repeat", 10, 10);
	ok &= expect(!session.applyRange(invalid, "Invalid") && encodeAudioSession(session.session()) == edited,
	             "failed direct GUI range edit cannot create partial changes");
	const auto saved = temporary.filePath("range.vssession");
	ok &=
	    expect(session.saveSession(saved) && finish(session), "save range changes through existing background writer");
	AudioSessionDialog reopened(nullptr, fakeAudioStreamFactory());
	reopened.setAttribute(Qt::WA_DeleteOnClose, false);
	reopened.setRecoveryEnabled(false);
	ok &= expect(reopened.openSession(saved) && finish(reopened) && encodeAudioSession(reopened.session()) == edited,
	             "GUI reopens exact range edits and inherited automation");
	const auto curves = session.session().tracks[0].gainAutomation;
	ok &= automationEditing(curves);
	const auto renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_RANGE_RENDER_DIR");
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
		const auto capture = [&](QWidget &widget, const QString &suffix) {
			if (renderRoot.isEmpty())
				return;
			widget.repaint();
			QCoreApplication::processEvents();
			QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			widget.render(&image, QPoint{}, QRegion(widget.rect()),
			              QWidget::DrawWindowBackground | QWidget::DrawChildren | QWidget::IgnoreMask);
			ok &= image.save(QDir(renderRoot).filePath(QString::fromLatin1(layout.name) + suffix + ".png"));
		};
		AudioRangeDialog dialog(source, {"ta", "tb"}, 60, 83);
		dialog.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.show();
		auto *operation = dialog.findChild<QComboBox *>("rangeOperation");
		auto *scroll = dialog.findChild<QScrollArea *>();
		for (bool compact : {false, true}) {
			dialog.resize(compact ? QSize(460, 390) : QSize(800, 690));
			for (const auto &op : {"clear", "ripple-delete", "insert-silence", "repeat"}) {
				operation->setCurrentIndex(operation->findData(op));
				QCoreApplication::processEvents();
				for (auto *control : dialog.findChildren<QWidget *>()) {
					if (!control->isVisible() || !control->objectName().startsWith("range") ||
					    qobject_cast<QLabel *>(control))
						continue;
					if (scroll->widget()->isAncestorOf(control)) {
						// QAbstractSpinBox can expose its cursor rectangle to
						// ensureWidgetVisible. Verify the whole control instead.
						const int y = control->mapTo(scroll->widget(), QPoint{}).y();
						scroll->verticalScrollBar()->setValue(y -
						                                      (scroll->viewport()->height() - control->height()) / 2);
						QCoreApplication::processEvents();
						const QRect rect(control->mapTo(scroll->viewport(), QPoint{}), control->size());
						if (!scroll->viewport()->rect().contains(rect))
							std::cerr << layout.name << ' ' << op << ' ' << control->objectName().toStdString()
							          << " rect=" << rect.x() << ',' << rect.y() << ',' << rect.width() << ','
							          << rect.height() << " viewport=" << scroll->viewport()->width() << ','
							          << scroll->viewport()->height() << '\n';
						ok &= expect(scroll->viewport()->rect().contains(rect),
						             "scaled range controls remain reachable without horizontal clipping");
					}
					const auto *accessible = QAccessible::queryAccessibleInterface(control);
					ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
					                 control->focusPolicy() != Qt::NoFocus,
					             "range controls expose accessible names and native keyboard focus");
				}
				scroll->verticalScrollBar()->setValue(compact ? scroll->verticalScrollBar()->maximum() : 0);
				auto *buttons = dialog.findChild<QDialogButtonBox *>();
				ok &= expect(dialog.rect().contains(QRect(buttons->mapTo(&dialog, QPoint{}), buttons->size())),
				             "scaled range commit controls stay in view");
				capture(dialog, QString(compact ? "-small-" : "-") + op);
			}
		}
		AudioAutomationEditor envelope("Gain", -96, 24, 0, 60, 220);
		envelope.setPoints(curves);
		envelope.setLayoutDirection(dialog.layoutDirection());
		envelope.resize(900, 800);
		envelope.show();
		QCoreApplication::processEvents();
		capture(envelope, "-automation");
		AudioSessionTimeline timeline;
		QHash<QString, AudioWaveformData> waveforms;
		for (const auto &media : source.sources)
			waveforms.insert(media.id, AudioWaveformData::build(media.audio.clip));
		timeline.setSession(source, waveforms);
		timeline.setVisibleRange(0, 230);
		timeline.setTimeRange(60, 83);
		timeline.resize(1100, timeline.sizeHint().height());
		timeline.show();
		QCoreApplication::processEvents();
		ok &= expect(timeline.accessibleDescription().contains("60") && timeline.accessibleDescription().contains("83"),
		             "timeline exposes the exclusive selected range in accessible state");
		capture(timeline, "-timeline");
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	std::cout << (ok ? "Range UI verification passed\n" : "Range UI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
