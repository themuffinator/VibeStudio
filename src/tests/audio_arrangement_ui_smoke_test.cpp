#include "app/audio_arrangement_dialog.h"
#include "app/audio_session_dialog.h"
#include "app/audio_session_timeline.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/audio_arrangement_fixture.h"
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
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
		if (!QByteArray(context).contains("AudioArrangement"))
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
	QTemporaryDir temporary(QDir(root).filePath("arrangement-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	const auto source = test::arrangementFixture(false);
	const auto path = temporary.filePath("input.vssession");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(encodeAudioSession(source)) <= 0)
		return EXIT_FAILURE;
	file.close();
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	bool ok = expect(session.openSession(path) && finish(session), "load arranging GUI fixture");
	if (!ok)
		return EXIT_FAILURE;
	auto *tree = session.findChild<QTreeWidget *>("sessionTracks");
	auto *timeline = session.findChild<AudioSessionTimeline *>();
	auto *links = session.findChild<QCheckBox *>("sessionLinkedGroups");
	auto *selectionAction = session.findChild<QAction *>("sessionSelectionControls");
	session.selectRegions({"b", "a"}, "b");
	ok &= expect(tree->selectionMode() == QAbstractItemView::ExtendedSelection && tree->selectedItems().size() == 2 &&
	                 tree->currentItem()->data(0, Qt::UserRole + 1) == "b" && !session.hasChanges(),
	             "native multi-selection retains primary focus without dirtying the session");
	bool opened = false;
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioArrangementDialog *>();
		if (!dialog)
			return;
		opened = true;
		auto *operation = dialog->findChild<QComboBox *>("arrangementOperation");
		operation->setCurrentIndex(operation->findData("group"));
		dialog->accept();
		ok &= expect(dialog->result() != QDialog::Accepted &&
		                 !dialog->findChild<QLabel *>("arrangementError")->text().isEmpty(),
		             "invalid group name leaves the staged dialog open with visible validation");
		dialog->findChild<QLineEdit *>("arrangementGroupName")->setText("Linked percussion");
		dialog->accept();
	});
	selectionAction->trigger();
	ok &= expect(opened && session.session().groups.size() == 1 &&
	                 session.selectedRegions(false) == QStringList({"a", "b"}) && tree->selectedItems().size() == 2 &&
	                 tree->currentItem()->data(0, Qt::UserRole + 1) == "b",
	             "native dialog commits one grouped edit and refresh preserves multi-selection");
	const auto grouped = encodeAudioSession(session.session());
	session.undo();
	ok &= expect(session.session().groups.isEmpty(), "group undo restores independent clips");
	session.redo();
	ok &= expect(encodeAudioSession(session.session()) == grouped, "group redo retains exact identities");
	session.selectRegion("ta", "a");
	ok &= expect(session.selectedRegions(true) == QStringList({"a", "b"}) && tree->selectedItems().size() == 1 &&
	                 tree->topLevelItem(0)->child(0)->text(3) == "Linked percussion",
	             "linked targets and named groups remain visible independently of explicit selection");
	links->setChecked(false);
	timeline->moveRequested("ta", "a", 30);
	ok &= expect(session.session().tracks[0].regions[0].position == 30 &&
	                 session.session().tracks[1].regions[0].position == 40,
	             "link toggle off moves only the explicit clip");
	session.undo();
	links->setChecked(true);
	timeline->selectionRequested("tc", "c", Qt::ControlModifier);
	ok &= expect(session.selectedRegions(false) == QStringList({"a", "c"}) && session.selectedRegions(true).size() == 3,
	             "timeline additive selection uses native group expansion");
	timeline->selectionRequested("tc", "c", Qt::ControlModifier);
	ok &= expect(session.selectedRegions(false) == QStringList({"a"}),
	             "timeline toggle removes the requested explicit clip");
	timeline->moveRequested("ta", "a", 40);
	ok &= expect(session.session().tracks[0].regions[0].position == 40 &&
	                 session.session().tracks[1].regions[0].position == 60,
	             "timeline movement applies a shared delta to linked clips");
	const auto beforeInvalid = encodeAudioSession(session.session());
	timeline->moveRequested("ta", "a", -1);
	ok &= expect(encodeAudioSession(session.session()) == beforeInvalid,
	             "failed grouped movement leaves every clip unchanged");
	session.findChild<QAction *>("sessionDuplicate")->trigger();
	ok &= expect(session.session().groups.size() == 2 && session.selectedRegions(false).size() == 2 &&
	                 session.session().tracks[0].regions[1].position == 140 &&
	                 session.session().tracks[1].regions[1].position == 160,
	             "toolbar duplicate appends the selection span and selects its copies");
	const auto unsplit = renderAudioSession(session.session());
	session.findChild<QDoubleSpinBox *>("sessionCursor")->setValue(180);
	session.findChild<QAction *>("sessionSplit")->trigger();
	ok &= expect(session.session().groups.size() == 3 && session.selectedRegions(false).size() == 2 &&
	                 renderAudioSession(session.session()).clip.samples == unsplit.clip.samples,
	             "toolbar split retains sound, envelopes and independent linked halves");
	const auto split = encodeAudioSession(session.session());
	session.undo();
	ok &= expect(session.session().groups.size() == 2, "grouped split is one undo state");
	session.redo();
	ok &= expect(encodeAudioSession(session.session()) == split, "grouped split redo retains exact metadata");
	session.selectRegions({session.session().tracks[0].regions.last().id});
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioArrangementDialog *>();
		if (!dialog)
			return;
		dialog->findChild<QDoubleSpinBox *>("arrangementOffset")->setValue(50);
		dialog->reject();
	});
	selectionAction->trigger();
	ok &= expect(encodeAudioSession(session.session()) == split, "cancelled selection dialog makes no change");
	const auto saved = temporary.filePath("grouped.vssession");
	ok &= expect(session.saveSession(saved) && finish(session), "save grouped arrangement through existing worker");
	AudioSessionDialog reopened(nullptr, fakeAudioStreamFactory());
	reopened.setAttribute(Qt::WA_DeleteOnClose, false);
	reopened.setRecoveryEnabled(false);
	ok &= expect(reopened.openSession(saved) && finish(reopened) && encodeAudioSession(reopened.session()) == split,
	             "GUI reopening restores linked groups and inherited fades");
	const auto renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_ARRANGEMENT_RENDER_DIR");
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
		AudioArrangementDialog dialog(session.session(), {"a"}, true, 80);
		dialog.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.resize(780, 600);
		dialog.show();
		QCoreApplication::processEvents();
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
		auto *operation = dialog.findChild<QComboBox *>("arrangementOperation");
		if (layout.rtl)
			ok &= expect(operation->itemText(0).contains(" · "), "expanded translations reach selection controls");
		for (const auto &name : {"move", "fades", "group"}) {
			operation->setCurrentIndex(operation->findData(name));
			QCoreApplication::processEvents();
			capture(dialog, QStringLiteral("-") + name);
		}
		for (auto *widget : {static_cast<QWidget *>(operation),
		                     static_cast<QWidget *>(dialog.findChild<QLineEdit *>("arrangementGroupName")),
		                     static_cast<QWidget *>(dialog.findChild<QCheckBox *>("arrangementLinkedGroups"))}) {
			const auto *accessible = QAccessible::queryAccessibleInterface(widget);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
			                 widget->focusPolicy() != Qt::NoFocus,
			             "native editing controls expose names and keyboard focus");
		}
		dialog.resize(400, 300);
		auto *scroll = dialog.findChild<QScrollArea *>();
		for (const auto &op : {"move", "fades", "group"}) {
			operation->setCurrentIndex(operation->findData(op));
			QCoreApplication::processEvents();
			for (auto *control : dialog.findChildren<QWidget *>()) {
				if (!control->isVisible() || !control->objectName().startsWith("arrangement") ||
				    qobject_cast<QLabel *>(control) || !scroll->widget()->isAncestorOf(control))
					continue;
				const int y = control->mapTo(scroll->widget(), QPoint{}).y();
				scroll->verticalScrollBar()->setValue(y - (scroll->viewport()->height() - control->height()) / 2);
				QCoreApplication::processEvents();
				const QRect rect(control->mapTo(scroll->viewport(), QPoint{}), control->size());
				ok &= expect(scroll->viewport()->rect().contains(rect),
				             "each scaled selection control is fully reachable without horizontal clipping");
			}
			scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
			const auto *buttons = dialog.findChild<QDialogButtonBox *>();
			ok &= expect(buttons->isVisible() &&
			                 dialog.rect().contains(QRect(buttons->mapTo(&dialog, QPoint{}), buttons->size())),
			             "compact scaled dialog keeps commit controls reachable");
			capture(dialog, QStringLiteral("-small-") + op);
		}
		AudioSessionTimeline preview;
		QHash<QString, AudioWaveformData> waveforms;
		for (const auto &media : session.session().sources)
			waveforms.insert(media.id, AudioWaveformData::build(media.audio.clip));
		preview.setSession(session.session(), waveforms);
		preview.setSelected("ta", "a");
		preview.setSelection({"a", "b", "c"});
		preview.setVisibleRange(0, 300);
		preview.setCursor(180);
		preview.resize(1100, preview.sizeHint().height());
		preview.show();
		QCoreApplication::processEvents();
		capture(preview, "-timeline");
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	std::cout << (ok ? "Arrangement UI verification passed\n" : "Arrangement UI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
