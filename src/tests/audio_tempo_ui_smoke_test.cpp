#include "app/audio_session_dialog.h"
#include "app/audio_session_timeline.h"
#include "app/audio_tempo_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_stream.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
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
class Expanded final : public QTranslator {
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).contains("AudioTempo"))
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
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("tempo-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	AudioProject source;
	source.clip = {1, 48000, QVector<float>(48000, .1f)};
	source.sourceName = "Music loop";
	source.endFrame = 48000;
	bool ok = expect(session.importSource(source) && finish(session), "prepare GUI audio fixture");
	if (!ok)
		return EXIT_FAILURE;
	const auto original = session.session();
	bool opened = false;
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioTempoDialog *>();
		if (!dialog)
			return;
		opened = true;
		dialog->findChild<QLineEdit *>("tempoPosition")->setText("2.1.0");
		dialog->findChild<QDoubleSpinBox *>("tempoBpm")->setValue(60);
		dialog->findChild<QPushButton *>("tempoSet")->click();
		dialog->findChild<QDoubleSpinBox *>("meterBar")->setValue(3);
		dialog->findChild<QSpinBox *>("meterBeats")->setValue(7);
		auto *unit = dialog->findChild<QComboBox *>("meterUnit");
		unit->setCurrentIndex(unit->findData(8));
		dialog->findChild<QPushButton *>("meterSet")->click();
		const auto keep = dialog->tempoMap();
		dialog->findChild<QLineEdit *>("tempoPosition")->setText("3.8.0");
		dialog->findChild<QPushButton *>("tempoSet")->click();
		ok &= expect(dialog->tempoMap() == keep && !dialog->findChild<QLabel *>("tempoMapStatus")->text().isEmpty(),
		             "invalid GUI beat does not alter draft map");
		dialog->findChild<QDialogButtonBox *>("tempoMapButtons")->button(QDialogButtonBox::Ok)->click();
	});
	session.findChild<QAction *>("sessionTempoMap")->trigger();
	const AudioTempoMap expected{120, 4, 4, {{3840, 60}}, {{3, 7, 8}}};
	{
		AudioTempoDialog atCursor(expected, 48000, 312000);
		ok &= expect(atCursor.findChild<QDoubleSpinBox *>("tempoBpm")->value() == 60 &&
		                 atCursor.findChild<QSpinBox *>("meterBeats")->value() == 7 &&
		                 atCursor.findChild<QComboBox *>("meterUnit")->currentData().toInt() == 8,
		             "new changes start from the tempo and meter effective at the cursor");
		auto *tempos = atCursor.findChild<QTreeWidget *>("tempoChanges");
		tempos->setCurrentItem(tempos->topLevelItem(0));
		ok &= expect(!atCursor.findChild<QPushButton *>("tempoRemove")->isEnabled(), "origin tempo cannot be removed");
		tempos->setCurrentItem(tempos->topLevelItem(1));
		atCursor.findChild<QPushButton *>("tempoRemove")->click();
		auto *meters = atCursor.findChild<QTreeWidget *>("meterChanges");
		meters->setCurrentItem(meters->topLevelItem(1));
		atCursor.findChild<QPushButton *>("meterRemove")->click();
		ok &= expect(atCursor.tempoMap().tempoChanges.isEmpty() && atCursor.tempoMap().meterChanges.isEmpty() &&
		                 atCursor.tempoMap().tempo == 120,
		             "remove commands preserve origin and clear selected changes");
	}
	ok &= expect(opened && session.session().musicalTime == expected &&
	                 session.session().tracks[0].regions[0].position == 0,
	             "tempo dialog commits one validated edit with media unchanged");
	session.undo();
	ok &= expect(session.session().musicalTime == original.musicalTime, "tempo map undo restores origin and lists");
	session.redo();
	ok &= expect(session.session().musicalTime == expected, "tempo map redo restores full map");
	auto *position = session.findChild<QLineEdit *>("sessionMusicalPosition");
	auto *cursor = session.findChild<QDoubleSpinBox *>("sessionCursor");
	position->setText("3.2.0");
	session.findChild<QPushButton *>("sessionGoToPosition")->click();
	ok &= expect(cursor->value() == 312000 && position->text() == "3.2.0" &&
	                 position->accessibleDescription().contains("7/8"),
	             "native musical navigation uses mapped sample clock and accessible meter");
	position->setText("3.8.0");
	session.findChild<QPushButton *>("sessionGoToPosition")->click();
	ok &= expect(cursor->value() == 312000, "invalid navigation keeps exact cursor");
	session.findChild<QComboBox *>("sessionRuler")->setCurrentIndex(1);
	auto *snap = session.findChild<QComboBox *>("sessionSnap");
	snap->setCurrentIndex(snap->findData(-3));
	ok &= expect(session.saveSession(temporary.filePath("timed.vssession")) && finish(session),
	             "save musical session through worker");
	AudioSessionDialog reopened(nullptr, fakeAudioStreamFactory());
	reopened.setAttribute(Qt::WA_DeleteOnClose, false);
	reopened.setRecoveryEnabled(false);
	ok &= expect(reopened.openSession(temporary.filePath("timed.vssession")) && finish(reopened) &&
	                 reopened.session().musicalTime == expected,
	             "GUI native reopen preserves tempo and meter changes");
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioTempoDialog *>();
		if (dialog)
			dialog->reject();
	});
	session.findChild<QAction *>("sessionTempoMap")->trigger();
	ok &= expect(session.session().musicalTime == expected, "cancelled map dialog leaves session unchanged");
	const auto renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_TEMPO_RENDER_DIR");
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
		AudioTempoDialog dialog(expected, 48000, 96000);
		dialog.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.resize(800, 650);
		dialog.show();
		QCoreApplication::processEvents();
		if (layout.rtl)
			ok &= expect(dialog.findChild<QPushButton *>("tempoSet")->text().contains(" · "),
			             "expanded translations reach tempo controls");
		const auto capture = [&](QWidget &widget, const QString &suffix) {
			if (renderRoot.isEmpty())
				return;
			QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			widget.render(&image);
			ok &= image.save(QDir(renderRoot).filePath(QString::fromLatin1(layout.name) + suffix + ".png"));
		};
		for (auto *widget : {static_cast<QWidget *>(dialog.findChild<QLineEdit *>("tempoPosition")),
		                     static_cast<QWidget *>(dialog.findChild<QDoubleSpinBox *>("tempoBpm")),
		                     static_cast<QWidget *>(dialog.findChild<QDoubleSpinBox *>("meterBar")),
		                     static_cast<QWidget *>(dialog.findChild<QSpinBox *>("meterBeats")),
		                     static_cast<QWidget *>(dialog.findChild<QComboBox *>("meterUnit"))})
			ok &= expect(!widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus &&
			                 QAccessible::queryAccessibleInterface(widget),
			             "native tempo controls expose names, roles and keyboard focus");
		capture(dialog, "-tempo");
		dialog.findChild<QTabWidget *>()->setCurrentIndex(1);
		QCoreApplication::processEvents();
		capture(dialog, "-meter");
		dialog.resize(600, 420);
		QCoreApplication::processEvents();
		for (auto *scroll : dialog.findChildren<QScrollArea *>())
			ok &= expect(scroll->horizontalScrollBar()->maximum() == 0,
			             "small expanded tempo forms require no horizontal scrolling");
		for (const char *name : {"meterBar", "meterBeats", "meterUnit", "meterSet", "meterRemove"}) {
			auto *control = dialog.findChild<QWidget *>(QLatin1String(name));
			auto *ancestor = control->parentWidget();
			while (ancestor && !qobject_cast<QScrollArea *>(ancestor))
				ancestor = ancestor->parentWidget();
			auto *scroll = qobject_cast<QScrollArea *>(ancestor);
			if (!expect(scroll != nullptr, "meter control belongs to a scrollable page")) {
				ok = false;
				continue;
			}
			const int y = control->mapTo(scroll->widget(), QPoint()).y();
			scroll->verticalScrollBar()->setValue(y - (scroll->viewport()->height() - control->height()) / 2);
			QCoreApplication::processEvents();
			const QRect rectangle(control->mapTo(scroll->viewport(), QPoint()), control->size());
			ok &= expect(scroll->viewport()->rect().contains(rectangle),
			             "each scaled meter control is completely reachable");
		}
		capture(dialog, "-small");
		AudioSessionTimeline timeline;
		timeline.setSession(session.session(), {});
		timeline.setMusicalRuler(true);
		timeline.setMusicalSnap(AudioMusicalGrid::Beat);
		timeline.setVisibleRange(0, 500000);
		timeline.setCursor(312000);
		timeline.resize(1200, 400);
		timeline.show();
		QCoreApplication::processEvents();
		ok &=
		    expect(timeline.accessibleDescription().contains("3.2.0") && timeline.layoutDirection() == Qt::LeftToRight,
		           "musical timeline retains time direction and accessible cursor");
		capture(timeline, "-timeline");
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	std::cout << (ok ? "Audio tempo UI verification passed\n" : "Audio tempo UI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
