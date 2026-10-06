#include "app/application_shell.h"
#include "app/audio_analysis_dialog.h"
#include "app/audio_editor_dialog.h"
#include "app/audio_markers_dialog.h"
#include "app/audio_recovery.h"
#include "app/audio_recovery_dialog.h"
#include "app/audio_waveform_view.h"
#include "app/studio_theme.h"
#include "vibestudio_config.h"
#include "tests/fake_audio_playback.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QUuid>

#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
bool waitForEditButton(QPushButton* button)
{
	if (!button) { return false; }
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (button->isEnabled()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(15000);
	if (!button->isEnabled()) { loop.exec(); }
	return expect(button->isEnabled(), "Edit Sound enables after the selected audio preview validates");
}
bool verifyNamedLabels(QWidget* root)
{
	bool ok = true;
	for (auto* label : root->findChildren<QLabel*>()) {
		if (label->accessibleName().isEmpty() || label->objectName() == QStringLiteral("audioPlaybackPosition")) {
			// Playback exposes its changing frame through the adjacent accessible
			// spin box; its clock description announces transport state only.
			continue;
		}
		const auto* accessible = QAccessible::queryAccessibleInterface(label);
		const bool exposed = accessible && accessible->text(QAccessible::Name) == label->accessibleName() &&
		                     accessible->text(QAccessible::Description) == label->text();
		if (!exposed) {
			std::cerr << "Accessible label lost its current text: " << label->accessibleName().toStdString() << '\n';
		}
		ok &= expect(exposed, "named labels expose current content and clear obsolete descriptions through Qt accessibility");
	}
	return ok;
}
bool finish(AudioEditorDialog* editor, int timeoutMs = 15000)
{
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (!editor->isBusy()) {
			loop.quit();
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(timeoutMs);
	if (editor->isBusy()) {
		loop.exec();
	}
	return expect(!editor->isBusy(), "audio worker must finish without blocking the event loop");
}
bool finishRecovery(AudioEditorDialog* editor)
{
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (!editor->recoveryBusy()) {
			loop.quit();
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(15000);
	if (editor->recoveryBusy()) {
		loop.exec();
	}
	return expect(!editor->recoveryBusy(), "recovery writer must settle without blocking the UI");
}
bool finishInventory(AudioRecoveryDialog* dialog)
{
	QApplication::processEvents();
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (!dialog->busy()) {
			loop.quit();
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(15000);
	if (dialog->busy()) {
		loop.exec();
	}
	return expect(!dialog->busy(), "recovery inventory completes asynchronously");
}
bool verifyOptionsLayout(QDialog* dialog)
{
	auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("audioOptionsScroll"));
	auto* buttons = dialog->findChild<QDialogButtonBox*>();
	if (!expect(scroll && buttons, "audio settings have a scroll body and a separate commit row")) {
		return false;
	}
	const auto settle = [&]() {
		for (int pass = 0; pass < 4; ++pass) {
			QApplication::processEvents();
			dialog->layout()->activate();
			scroll->widget()->layout()->activate();
		}
	};
	const auto check = [&]() {
		bool ok = verifyNamedLabels(dialog);
		ok &= expect(dialog->rect().contains(buttons->geometry()) &&
		                     !buttons->geometry().intersects(scroll->geometry()),
		                 "audio settings buttons stay inside the dialog and "
		                 "outside the scrolling text");
		for (auto* label : scroll->findChildren<QLabel*>()) {
			if (label->wordWrap()) {
				ok &= expect(label->height() >= label->heightForWidth(label->width()) &&
				                 label->parentWidget()->rect().contains(label->geometry()),
				             "every translated summary line fits inside the scrollable content");
				scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
				const QPoint bottom = label->mapTo(scroll->viewport(), label->rect().bottomLeft());
				ok &= expect(bottom.y() >= 0 && bottom.y() < scroll->viewport()->height(),
				             "the end of the audio summary is reachable above the pinned "
				             "buttons");
			}
		}
		scroll->verticalScrollBar()->setValue(0);
		return ok;
	};
	settle();
	bool ok = check();
	const QSize original = dialog->size();
	dialog->resize(original.width(), 160);
	settle();
	ok &= check();
	ok &= expect(scroll->verticalScrollBar()->maximum() > 0,
	             "short settings dialogs expose overflow by scrolling");
	dialog->resize(original);
	settle();
	return check() && ok;
}
class ExpandedTranslator : public QTranslator {
  public:
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "vibestudio::AudioEditorDialog" &&
		    QByteArray(context) != "AudioEditorDialog" &&
		    QByteArray(context) != "vibestudio::AudioWaveformView" &&
		    QByteArray(context) != "AudioWaveformView" &&
		    QByteArray(context) != "vibestudio::AudioAnalysisDialog" &&
		    QByteArray(context) != "AudioAnalysisDialog" &&
		    QByteArray(context) != "vibestudio::AudioChannelMapDialog" &&
		    QByteArray(context) != "AudioChannelMapDialog" &&
		    QByteArray(context) != "VibeStudioAudioAnalysis" &&
		    QByteArray(context) != "vibestudio::AudioRecoveryDialog" &&
		    QByteArray(context) != "AudioRecoveryDialog" &&
		    QByteArray(context) != "vibestudio::AudioMarkersDialog" &&
		    QByteArray(context) != "AudioMarkersDialog" && QByteArray(context) != "VibeStudioAudio") {
			return {};
		}
		const QString text = QString::fromUtf8(source);
		return text + QStringLiteral(" · ") + text;
	}
};
QListWidgetItem* audioEntry(ApplicationShell* shell, const QString& path, bool wait = false)
{
	auto* list = shell->findChild<QListWidget*>(QStringLiteral("audioEntries"));
	if (!list) {
		return nullptr;
	}
	const auto find = [&]() -> QListWidgetItem* {
		for (int row = 0; row < list->count(); ++row) {
			if (list->item(row)->data(Qt::UserRole).toString() == path) {
				return list->item(row);
			}
		}
		return nullptr;
	};
	if (wait && !find()) {
		QEventLoop loop;
		QTimer poll, timeout;
		timeout.setSingleShot(true);
		QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
			if (find()) {
				loop.quit();
			}
		});
		QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
		poll.start(10);
		timeout.start(15000);
		loop.exec();
	}
	return find();
}
} // namespace

int main(int argc, char** argv)
{
	// Direct widget methods only: no keyboard/mouse injection, OS capture, or
	// playback into the user's audio device. Images come from QWidget::render.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",
	        QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-ui-XXXXXX")));
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	qputenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT",
	        QDir(temporary.path()).filePath(QStringLiteral("recovery")).toUtf8());
	AudioClip clip{2, 22050, {}};
	clip.samples.resize(22050 * 2);
	for (int frame = 0; frame < 22050; ++frame) {
		clip.samples[frame * 2] = float(std::sin(frame * 0.09) * 0.6);
		clip.samples[frame * 2 + 1] = float(std::cos(frame * 0.06) * 0.35);
	}
	const QByteArray source = encodeAudioWav(clip);
	const auto decoded = decodeAudioClip(QStringLiteral("fixture.wav"), source);
	bool ok = true;
	QString error;
	const QString assets = QDir(temporary.path()).filePath(QStringLiteral("assets"));
	QDir().mkpath(assets);
	const QString input = QDir(assets).filePath(QStringLiteral("fixture.wav"));
	ok &= expect(saveAudioWav(clip, input, false, {}, &error), "save independent generated audio fixture");
	for (int scale : {100, 200, 125}) {
		const StudioTheme theme = scale == 100   ? StudioTheme::Dark
		                          : scale == 200 ? StudioTheme::HighContrastDark
		                                         : StudioTheme::HighContrastLight;
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		ExpandedTranslator expanded;
		if (scale == 200) {
			app.installTranslator(&expanded);
		}
		auto playbackBackend = std::make_unique<FakeAudioPlaybackBackend>();
		[[maybe_unused]] auto* playback = playbackBackend.get();
		auto* editor = new AudioEditorDialog(nullptr, std::move(playbackBackend));
		PackageArchive archive;
		PackageStagingModel staging;
		ok &= expect(archive.load(assets, &error) && staging.loadBaseArchive(archive, &error),
		             "load editor package context");
		QString targetPackage = assets;
		editor->context = [&]() { return AudioEditorContext{targetPackage, true}; };
		int handoffs = 0;
		editor->handoff = [&](const QByteArray& wav, const QString& path, bool replace, QString* problem) {
			++handoffs;
			return stageAudioWav(wav, path, &staging, replace, problem);
		};
		if (scale == 200) {
			editor->setLayoutDirection(Qt::RightToLeft);
			editor->resize(1500, 1200);
		}
		editor->show();
		ok &= expect(editor->openFile(input), "start asynchronous file open");
		ok &= finish(editor);
		ok &= expect(editor->clip().samples == decoded.clip.samples && !editor->hasChanges(),
		             "open preserves all decoded samples");
		ok &= verifyNamedLabels(editor);
		auto* waveform = editor->findChild<AudioWaveformView*>();
		waveform->setHighContrast(scale != 100);
		ok &=
		    expect(waveform->visibleEnd() == editor->clip().frameCount() &&
		               waveform->focusPolicy() != Qt::NoFocus && !waveform->accessibleDescription().isEmpty(),
		           "waveform is accessible and focusable");
		const auto* accessible = QAccessible::queryAccessibleInterface(waveform);
		ok &= expect(accessible && accessible->role() == QAccessible::Graphic,
		             "waveform exposes its accessible graphic role");
		auto* first = editor->findChild<QSpinBox*>(QStringLiteral("audioSelectionStart"));
		auto* end = editor->findChild<QSpinBox*>(QStringLiteral("audioSelectionEnd"));
		first->setValue(2205);
		end->setValue(11025);
		ok &= expect(waveform->selectionStart() == 2205 && waveform->selectionEnd() == 11025,
		             "exact frame fields update the selected waveform range");
		editor->findChild<QAction*>(QStringLiteral("audioZoomSelection"))->trigger();
		ok &= expect(waveform->visibleStart() == 2205 && waveform->visibleEnd() == 11025,
		             "fit-selection frames the exact selected samples");
		auto* pan = editor->findChild<QScrollBar*>(QStringLiteral("audioWaveformPan"));
		pan->setValue(3000);
		ok &= expect(waveform->visibleStart() == 3000 && waveform->visibleEnd() == 11820 &&
		                 first->value() == 2205 && end->value() == 11025,
		             "scrollbar pans without modifying selection or samples");
		waveform->setVisibleRange(1, 5);
		ok &= expect(waveform->frameForX(waveform->xForFrame(3)) == 3,
		             "sample zoom hit positions round-trip exact sub-millisecond frames");
		waveform->moveCursor(2);
		waveform->moveCursor(3, true);
		ok &= expect(first->value() == 2 && end->value() == 3 && !editor->hasChanges(),
		             "waveform cursor commands select a single exact frame without "
		             "dirtying audio");
		waveform->moveCursor(1, true);
		ok &= expect(first->value() == 1 && end->value() == 2,
		             "reversing extension retains the original selection anchor");
		waveform->panFrames(-10000);
		ok &= expect(waveform->visibleStart() == 0 && waveform->visibleEnd() == 4,
		             "panning clamps at the beginning while preserving scale");
		waveform->zoomToFit();
		editor->setSelection(2205, 11025);
		auto* markersAction = editor->findChild<QAction*>(QStringLiteral("audioEditorMarkers"));
		auto* selectLoop = editor->findChild<QAction*>(QStringLiteral("audioEditorSelectLoop"));
		ok &= expect(markersAction && markersAction->isEnabled() && selectLoop && !selectLoop->isEnabled(),
		             "a loaded sound exposes marker authoring and no absent-loop command");
		markersAction->trigger();
		auto* markerDialog = editor->findChild<AudioMarkersDialog*>();
		ok &= expect(markerDialog != nullptr, "marker action opens the pending marker editor");
		if (markerDialog) {
			markerDialog->findChild<QPushButton*>(QStringLiteral("audioMarkersAdd"))->click();
			markerDialog->findChild<QPushButton*>(QStringLiteral("audioMarkersUseSelection"))->click();
			markerDialog->reject();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			ok &= expect(editor->clip().markers.empty() && !editor->hasChanges(),
			             "cancelling cue and loop authoring leaves samples, metadata "
			             "and saved revision intact");
		}
		markersAction->trigger();
		markerDialog = editor->findChild<AudioMarkersDialog*>();
		if (markerDialog) {
			markerDialog->findChild<QPushButton*>(QStringLiteral("audioMarkersUseSelection"))->click();
			auto* add = markerDialog->findChild<QPushButton*>(QStringLiteral("audioMarkersAdd"));
			auto* table = markerDialog->findChild<QTableWidget*>(QStringLiteral("audioMarkersCues"));
			add->click();
			table->item(0, 0)->setText(QString::fromUtf8("风 · Wind start"));
			table->item(0, 1)->setData(Qt::EditRole, 3000);
			add->click();
			table->item(1, 0)->setText(QStringLiteral("Tail"));
			table->item(1, 1)->setData(Qt::EditRole, 17000);
			auto* loopEnd = markerDialog->findChild<QSpinBox*>(QStringLiteral("audioMarkersEnd"));
			loopEnd->setValue(100);
			markerDialog->accept();
			ok &= expect(markerDialog->isVisible() &&
			                 !markerDialog->findChild<QLabel*>(QStringLiteral("audioMarkersStatus"))
			                      ->text()
			                      .isEmpty() &&
			                 !editor->hasChanges(),
			             "invalid loop boundaries keep the pending editor open with a "
			             "validation explanation");
			ok &= verifyNamedLabels(markerDialog);
			auto* loopEnabled = markerDialog->findChild<QCheckBox*>(QStringLiteral("audioMarkersLoop"));
			loopEnabled->setChecked(false);
			ok &= verifyNamedLabels(markerDialog);
			ok &= expect(markerDialog->findChild<QLabel*>(QStringLiteral("audioMarkersStatus"))->text().isEmpty(),
			             "disabling the loop clears its obsolete validation error");
			loopEnabled->setChecked(true);
			markerDialog->accept();
			loopEnd->setValue(11025);
			ok &= verifyNamedLabels(markerDialog);
			app.processEvents();
			auto* body = markerDialog->findChild<QScrollArea*>(QStringLiteral("audioMarkersBody"));
			auto* buttons = markerDialog->findChild<QDialogButtonBox*>();
			const QSize originalSize = markerDialog->size();
			markerDialog->resize(originalSize.width(), 180);
			app.processEvents();
			ok &= expect(body->verticalScrollBar()->maximum() > 0 &&
			                 markerDialog->rect().contains(buttons->geometry()) &&
			                 !body->geometry().intersects(buttons->geometry()),
			             "short marker dialogs scroll while OK and Cancel remain visible");
			markerDialog->resize(originalSize);
			app.processEvents();
			ok &= expect(!table->accessibleName().isEmpty() && table->focusPolicy() != Qt::NoFocus &&
			                 markerDialog->layoutDirection() == editor->layoutDirection(),
			             "cue table exposes named keyboard editing and inherits RTL "
			             "direction");
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				QImage image(markerDialog->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				markerDialog->render(&image);
				ok &= expect(
				    image.save(QDir(captures).filePath(QStringLiteral("audio-markers-%1.png").arg(scale))),
				    "render marker authoring controls directly");
			}
			const float* samples = editor->clip().samples.constData();
			AudioMarkers expected = markerDialog->markers();
			table->editItem(table->item(1, 0));
			app.processEvents();
			auto* cueEditor = table->findChild<QLineEdit*>();
			ok &= expect(cueEditor && !cueEditor->accessibleName().isEmpty(),
			             "editing a cue creates one named field");
			if (cueEditor) {
				cueEditor->setText(QString::fromUtf8("Tail entrée"));
				expected.cues[1].name = QString::fromUtf8("Tail entrée");
			}
			markerDialog->accept();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			ok &= expect(editor->hasChanges() && editor->clip().markers == expected &&
			                 editor->clip().samples.constData() == samples && selectLoop->isEnabled() &&
			                 waveform->accessibleSummary().contains(QStringLiteral("2205")),
			             "accepted marker changes share samples, become undoable and update "
			             "waveform accessibility");
			ok &= expect(editor->clip().markers.cues[1].name == QString::fromUtf8("Tail entrée"),
			             "OK commits the active cue cell before saving pending metadata");
			selectLoop->trigger();
			ok &= expect(first->value() == 2205 && end->value() == 11025 &&
			                 waveform->visibleStart() == 2205 && waveform->visibleEnd() == 11025,
			             "select-loop prepares exact loop audition through normal "
			             "selection transport");
			if (!captures.isEmpty()) {
				waveform->zoomToFit();
				QImage image(waveform->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				waveform->render(&image);
				ok &= expect(image.save(QDir(captures).filePath(
				                 QStringLiteral("audio-marker-waveform-%1.png").arg(scale))),
				             "render cues and loop bracket without screen capture");
			}
			editor->undo();
			ok &= expect(editor->clip().markers.empty() && !editor->hasChanges() && !selectLoop->isEnabled(),
			             "marker undo restores clean state and command availability");
			editor->redo();
			ok &= expect(editor->clip().markers == expected,
			             "marker redo restores exact IDs, names and boundaries");
			editor->undo();
		}
		QTimer::singleShot(0, [&]() {
			auto* dialog = editor->findChild<QDialog*>(QStringLiteral("audioResampleDialog"));
			ok &= expect(dialog != nullptr, "resample command opens its settings dialog");
			if (!dialog) {
				return;
			}
			auto* buttons = dialog->findChild<QDialogButtonBox*>();
			auto* rate = dialog->findChild<QSpinBox*>(QStringLiteral("audioResampleRate"));
			auto* presets = dialog->findChild<QComboBox*>();
			ok &= expect(!buttons->button(QDialogButtonBox::Ok)->isEnabled() &&
			                 !rate->accessibleName().isEmpty(),
			             "resample controls are named and an unchanged rate cannot "
			             "create an undo step");
			presets->setCurrentIndex(presets->findData(44100));
			ok &= verifyOptionsLayout(dialog);
			ok &= expect(rate->value() == 44100 && buttons->button(QDialogButtonBox::Ok)->isEnabled(),
			             "sample rate presets update the conversion input");
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				dialog->layout()->activate();
				QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				dialog->render(&image);
				ok &= expect(
				    image.save(QDir(captures).filePath(QStringLiteral("audio-resample-%1.png").arg(scale))),
				    "render resampling controls at each tested scale and "
				    "language direction");
			}
			buttons->button(QDialogButtonBox::Ok)->click();
		});
		editor->findChild<QAction*>(QStringLiteral("audioEditorResample"))->trigger();
		ok &= finish(editor);
		ok &= expect(editor->clip().sampleRate == 44100 && editor->clip().frameCount() == 44100 &&
		                 first->value() == 4410 && end->value() == 22050 && editor->hasChanges(),
		             "resampling updates rate, duration, selection, and dirty "
		             "state together");
		const AudioClip resampled = editor->clip();
		editor->undo();
		ok &= expect(editor->clip().samples == decoded.clip.samples && editor->clip().sampleRate == 22050 &&
		                 first->value() == 2205 && end->value() == 11025 && !editor->hasChanges(),
		             "resample undo restores samples, sample rate, exact "
		             "selection, and saved revision");
		editor->redo();
		ok &= expect(editor->clip().samples == resampled.samples && editor->clip().sampleRate == 44100,
		             "resample redo restores the converted result without recomputing");
		editor->undo();
		editor->resample(48000);
		editor->cancelWork();
		ok &= finish(editor);
		ok &= expect(editor->clip().sampleRate == 22050 && editor->clip().samples == decoded.clip.samples &&
		                 !editor->hasChanges(),
		             "cancelled conversion cannot replace the document");
		const auto expectedAnalysis = analyzeAudioClip(editor->clip(), first->value(), end->value());
		editor->findChild<QAction*>(QStringLiteral("audioEditorAnalyze"))->trigger();
		ok &= finish(editor);
		auto* analysisDialog = editor->findChild<AudioAnalysisDialog*>(QStringLiteral("audioAnalysisDialog"));
		ok &= expect(analysisDialog &&
		                 audioAnalysisJson(analysisDialog->analysis()) ==
		                     audioAnalysisJson(expectedAnalysis.analysis) &&
		                 !editor->hasChanges() && editor->clip().samples == decoded.clip.samples,
		             "analysis measures the exact editor selection without "
		             "modifying document or history");
		if (analysisDialog) {
			app.processEvents();
			auto* table = analysisDialog->findChild<QTableWidget*>(QStringLiteral("audioAnalysisChannels"));
			ok &=
			    expect(table && table->rowCount() == editor->clip().channels && table->columnCount() == 13 &&
			               table->editTriggers() == QAbstractItemView::NoEditTriggers &&
			               !table->accessibleName().isEmpty() && table->focusPolicy() != Qt::NoFocus &&
			               analysisDialog->layoutDirection() == editor->layoutDirection(),
			           "analysis exposes named, focusable, read-only channel measurements "
			           "in the editor's language direction");
			auto* body = analysisDialog->findChild<QScrollArea*>(QStringLiteral("audioAnalysisBody"));
			ok &= expect(body && body->widget()->width() <= body->viewport()->width() &&
			                 body->horizontalScrollBar()->maximum() == 0,
			             "expanded analysis summaries fit the report viewport in both language directions");
			const auto* truePeak = analysisDialog->findChild<QLabel*>(QStringLiteral("audioAnalysisTruePeak"));
			const auto* loudness = analysisDialog->findChild<QLabel*>(QStringLiteral("audioAnalysisLoudness"));
			ok &= expect(truePeak && loudness && !truePeak->accessibleName().isEmpty() && !loudness->accessibleName().isEmpty() &&
			                 table->item(0, 12)->data(Qt::AccessibleTextRole).toString().contains(QLatin1Char('-')),
			             "true peak and programme loudness have accessible summaries and signed per-channel values");
			ok &= expect(
			    table->item(0, 1)->text().startsWith(QChar(0x2066)) &&
			        !table->item(0, 1)->flags().testFlag(Qt::ItemIsEditable) &&
			        !table->item(0, 1)->data(Qt::AccessibleTextRole).toString().contains(QChar(0x2066)),
			    "numeric direction preserves leading signs in RTL while "
			    "accessibility exposes plain values");
			for (auto* label : body->widget()->findChildren<QLabel*>()) {
				if (!label->isVisibleTo(analysisDialog)) {
					continue;
				}
				ok &= expect(label->height() >= label->heightForWidth(label->width()),
				             "expanded analysis report labels have enough height to wrap");
			}
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				QImage image(analysisDialog->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				analysisDialog->render(&image);
				ok &= expect(
				    image.save(QDir(captures).filePath(QStringLiteral("audio-analysis-%1.png").arg(scale))),
				    "render analysis report at each scale, contrast, and "
				    "language direction");
			}
			auto* definitions =
			    analysisDialog->findChild<QToolButton*>(QStringLiteral("audioAnalysisDefinitions"));
			ok &= expect(definitions && definitions->isCheckable() && !definitions->isChecked() &&
			                 !definitions->accessibleName().isEmpty() && !definitions->icon().isNull() &&
			                 definitions->toolTip() == definitions->accessibleName(),
			             "measurement definitions support progressive disclosure "
			             "with an accessible checked state");
			if (definitions) {
				definitions->setChecked(true);
			}
			app.processEvents();
			ok &= expect(body->widget()->width() <= body->viewport()->width() &&
			                 body->horizontalScrollBar()->maximum() == 0,
			             "expanded measurement definitions do not force horizontal scrolling of report text");
			ok &= verifyNamedLabels(analysisDialog);
			analysisDialog->resize(analysisDialog->width(), 180);
			app.processEvents();
			auto* buttons = analysisDialog->findChild<QDialogButtonBox*>();
			ok &= expect(body->verticalScrollBar()->maximum() > 0 &&
			                 analysisDialog->rect().contains(buttons->geometry()) &&
			                 !body->geometry().intersects(buttons->geometry()),
			             "short analysis windows keep the close action visible while "
			             "report contents scroll");
			analysisDialog->close();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		}
		{
			QPointer<AudioChannelMapDialog> mapping = new AudioChannelMapDialog(6, editor);
			mapping->show(); app.processEvents();
			auto* buttons = mapping->findChild<QDialogButtonBox*>();
			auto* preset = mapping->findChild<QComboBox*>(QStringLiteral("audioChannelLayoutPreset"));
			auto* role = mapping->findChild<QComboBox*>(QStringLiteral("audioChannelRole2"));
			auto* enable = mapping->findChild<QCheckBox*>(QStringLiteral("audioMeasureLoudness"));
			if (!expect(buttons && preset && role && enable, "speaker review controls exist")) { return EXIT_FAILURE; }
			ok &= expect(buttons && preset && role && enable && !buttons->button(QDialogButtonBox::Ok)->isEnabled(),
			             "surround analysis does not silently infer speaker roles from channel count");
			preset->setCurrentIndex(1); app.processEvents();
			const auto chosen = mapping->options();
			ok &= expect(chosen.channelMap.size() == 6 && chosen.channelMap[3] == AudioChannelRole::LowFrequency &&
			                 buttons->button(QDialogButtonBox::Ok)->isEnabled(),
			             "an explicit preset fills the displayed source-order roles");
			role->setCurrentIndex(role->findData(int(AudioChannelRole::Left)));
			ok &= expect(!buttons->button(QDialogButtonBox::Ok)->isEnabled() && preset->currentIndex() == 0,
			             "duplicate speaker positions block analysis and custom changes clear the preset label");
			enable->setChecked(false);
			ok &= expect(buttons->button(QDialogButtonBox::Ok)->isEnabled() && mapping->options().channelMap.isEmpty() &&
			                 !mapping->options().measureLoudness && !role->isEnabled(),
			             "unknown layouts can still use sample and true-peak analysis");
			enable->setChecked(true); preset->setCurrentIndex(1); app.processEvents();
			auto* scroll = mapping->findChild<QScrollArea*>();
			ok &= expect(scroll && scroll->horizontalScrollBar()->maximum() == 0 &&
			                 mapping->layoutDirection() == editor->layoutDirection() && !role->accessibleName().isEmpty() &&
			                 role->focusPolicy() != Qt::NoFocus && verifyNamedLabels(mapping),
			             "speaker review remains accessible and fits scaled, expanded and RTL layouts");
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				QImage image(mapping->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); mapping->render(&image);
				ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("audio-channel-map-%1.png").arg(scale))),
				             "render the reviewed speaker layout controls");
			}
			mapping->resize(mapping->width(), 180); app.processEvents();
			ok &= expect(scroll->verticalScrollBar()->maximum() > 0 && mapping->rect().contains(buttons->geometry()) &&
			                 !scroll->geometry().intersects(buttons->geometry()),
			             "short speaker-review windows keep Analyze and Cancel reachable");
			mapping->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			ok &= expect(!mapping && !editor->isBusy() && !editor->hasChanges(), "cancelling role review leaves the sound and history intact");
		}
		{
			// Exercise the real editor handoff separately from the layout probes.
			auto surroundEditor = std::make_unique<AudioEditorDialog>(nullptr, std::make_unique<FakeAudioPlaybackBackend>());
			const bool previousRecovery = StudioSettings().audioRecoveryEnabled();
			surroundEditor->setRecoveryEnabled(false);
			AudioClip surround{6, 48000, {}};
			surround.samples.resize(48000 * 6);
			for (qint64 frame = 0; frame < surround.frameCount(); ++frame) {
				for (int channel = 0; channel < 6; ++channel) {
					surround.samples[frame * 6 + channel] = float(.1 * std::sin(frame * .13));
				}
			}
			const auto bytes = encodeAudioWav(surround);
			ok &= expect(surroundEditor->loadSource(QStringLiteral("surround.wav"), {}, [bytes](QString*) { return bytes; }),
			             "open an independent six-channel source for the analysis handoff");
			ok &= finish(surroundEditor.get());
			surroundEditor->setSelection(4800, 28800);
			surroundEditor->reviewAnalysisChannels();
			QPointer<AudioChannelMapDialog> pending = surroundEditor->findChild<AudioChannelMapDialog*>();
			if (!expect(pending && !surroundEditor->isBusy(), "role review precedes the analysis worker")) { return EXIT_FAILURE; }
			surroundEditor->reviewAnalysisChannels();
			ok &= expect(surroundEditor->findChildren<AudioChannelMapDialog*>().size() == 1,
			             "repeated analysis requests reuse the current role review");
			pending->findChild<QComboBox*>(QStringLiteral("audioChannelLayoutPreset"))->setCurrentIndex(1);
			const auto expected = analyzeAudioClip(surroundEditor->clip(), 4800, 28800, {}, pending->options());
			pending->accept();
			ok &= finish(surroundEditor.get());
			auto* report = surroundEditor->findChild<AudioAnalysisDialog*>();
			ok &= expect(report && expected.succeeded() && expected.analysis.integratedLufs &&
			                 audioAnalysisJson(report->analysis()) == audioAnalysisJson(expected.analysis) && !surroundEditor->hasChanges(),
			             "accepted speaker roles reach the shared worker for the exact current selection without an edit");
			if (report) { report->close(); }
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			surroundEditor->reviewAnalysisChannels();
			pending = surroundEditor->findChild<AudioChannelMapDialog*>();
			if (!expect(pending, "speaker roles are reviewed again for a later analysis")) { return EXIT_FAILURE; }
			pending->findChild<QCheckBox*>(QStringLiteral("audioMeasureLoudness"))->setChecked(false);
			pending->accept();
			ok &= finish(surroundEditor.get());
			report = surroundEditor->findChild<AudioAnalysisDialog*>();
			ok &= expect(report && report->analysis().loudnessStatus == QStringLiteral("disabled") && report->analysis().truePeak,
			             "opting out in the editor reports not-requested loudness while retaining true peak");
			if (report) { report->close(); }
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			surroundEditor->reviewAnalysisChannels();
			pending = surroundEditor->findChild<AudioChannelMapDialog*>();
			if (!expect(pending, "prepare a review before a document change")) { return EXIT_FAILURE; }
			pending->findChild<QComboBox*>(QStringLiteral("audioChannelLayoutPreset"))->setCurrentIndex(1);
			surroundEditor->applyEdit(QStringLiteral("gain"), -3);
			ok &= finish(surroundEditor.get());
			pending->accept();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			ok &= expect(!surroundEditor->isBusy() && !surroundEditor->findChild<AudioAnalysisDialog*>() && surroundEditor->hasChanges(),
			             "a changed document rejects stale speaker review without running an analysis or losing the edit");
			StudioSettings restored;
			restored.setAudioRecoveryEnabled(previousRecovery);
			restored.sync();
		}
		editor->analyze();
		editor->cancelWork();
		ok &= finish(editor);
		ok &= expect(!editor->findChild<AudioAnalysisDialog*>() && !editor->hasChanges(),
		             "cancelled analysis cannot display a partial report or create an edit");
		QTimer::singleShot(0, [&]() {
			auto* dialog = editor->findChild<QDialog*>(QStringLiteral("audioWavExportDialog"));
			ok &= expect(dialog != nullptr, "export command opens precision controls");
			if (!dialog) {
				return;
			}
			auto* precision = dialog->findChild<QComboBox*>(QStringLiteral("audioWavPrecision"));
			auto* dither = dialog->findChild<QCheckBox*>();
			dither->setChecked(true);
			precision->setCurrentIndex(precision->findData(int(AudioWavFormat::Float32)));
			ok &= verifyOptionsLayout(dialog);
			ok &=
			    expect(!dither->isEnabled() && !dither->isChecked() && !precision->accessibleName().isEmpty(),
			           "float export visibly disables incompatible dither");
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				dialog->layout()->activate();
				QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				dialog->render(&image);
				ok &= expect(
				    image.save(QDir(captures).filePath(QStringLiteral("audio-export-%1.png").arg(scale))),
				    "render export controls at each tested scale and language "
				    "direction");
			}
			auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("audioDeliveryPreset"));
			preset->setCurrentIndex(preset->findData(int(AudioDeliveryPreset::Doom)));
			ok &= verifyOptionsLayout(dialog);
			ok &= expect(
			    !precision->isEnabled() && precision->currentData().toInt() == int(AudioWavFormat::Pcm8) &&
			        dither->isEnabled() &&
			        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),
			    "Doom delivery displays its fixed precision and allows "
			    "integer dither");
			if (!captures.isEmpty()) {
				QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				dialog->render(&image);
				ok &= expect(image.save(QDir(captures).filePath(
				                 QStringLiteral("audio-doom-export-%1.png").arg(scale))),
				             "render game delivery controls without screen capture");
			}
			preset->setCurrentIndex(preset->findData(int(AudioDeliveryPreset::Wav)));
			ok &= expect(precision->isEnabled() &&
			                 precision->currentData().toInt() == int(AudioWavFormat::Float32) &&
			                 !dither->isEnabled(),
			             "returning to custom WAV restores the chosen precision");
			dialog->reject();
		});
		editor->findChild<QAction*>(QStringLiteral("audioEditorExport"))->trigger();
		const QString floatOutput =
		    QDir(temporary.path()).filePath(QStringLiteral("float-%1.wav").arg(scale));
		ok &= expect(editor->exportTo(floatOutput, false, {AudioWavFormat::Float32}),
		             "start float export through editor");
		ok &= finish(editor);
		QFile floatFile(floatOutput);
		ok &= expect(floatFile.open(QIODevice::ReadOnly) &&
		                 decodeAudioClip(floatOutput, floatFile.readAll()).clip.samples ==
		                     decoded.clip.samples &&
		                 !editor->hasChanges(),
		             "editor float export retains samples and document state");
		const QString dmxOutput = QDir(temporary.path()).filePath(QStringLiteral("doom-%1.dmx").arg(scale));
		ok &= expect(editor->exportDeliveryTo(dmxOutput, false, {AudioDeliveryPreset::Doom, {}}),
		             "start cancellable game delivery preparation");
		editor->cancelWork();
		ok &= finish(editor);
		ok &= expect(!QFileInfo::exists(dmxOutput) && editor->clip().samples == decoded.clip.samples &&
		                 !editor->hasChanges(),
		             "cancelled delivery creates no output and leaves the document intact");
		ok &= expect(editor->exportDeliveryTo(dmxOutput, false, {AudioDeliveryPreset::Doom, {}}),
		             "prepare and commit game delivery through the editor");
		ok &= finish(editor);
		QFile dmxFile(dmxOutput);
		ok &= expect(dmxFile.open(QIODevice::ReadOnly) && isGeneratedAudioDmx(dmxFile.readAll()) &&
		                 editor->clip().sampleRate == 22050 && editor->clip().channels == 2 &&
		                 !editor->hasChanges(),
		             "Doom delivery does not convert or dirty the working document");
		ok &=
		    expect(first->height() >= first->fontMetrics().height() + 8 && !first->accessibleName().isEmpty(),
		           "scaled frame controls expose readable text and names");
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
		// Capture prepared bytes through the backend boundary; no audio device is
		// opened. Decoder fixtures independently establish float WAV correctness.
		editor->findChild<QSlider*>(QStringLiteral("audioPlaybackVolume"))->setValue(0);
		int auditions = 0;
		editor->beforePlayback = [&]() { ++auditions; };
		editor->findChild<QAction*>(QStringLiteral("audioEditorPlay"))->trigger();
		ok &= finish(editor);
		auto audition = decodeAudioClip(QStringLiteral("audition.wav"), playback->bytes);
		ok &= expect(auditions == 1 && audition.succeeded() &&
		                 audition.clip.samples == decoded.clip.samples.mid(2205 * 2, (11025 - 2205) * 2),
		             "audition buffer contains the selected float samples without quantization");
		editor->stopPlayback();
		ok &= expect(waveform->playheadFrame() >= 2205 && waveform->playheadFrame() <= 11025,
		             "transport preparation must not reset the selection playhead to zero");
		ok &= expect(playback->bytes.isEmpty() && playback->state == AudioPlaybackBackend::State::Stopped,
		             "shared stop detaches edited playback");
		editor->setSelection(3, 3);
		editor->findChild<QAction*>(QStringLiteral("audioEditorPlay"))->trigger();
		ok &= finish(editor);
		audition = decodeAudioClip(QStringLiteral("audition.wav"), playback->bytes);
		ok &= expect(audition.succeeded() && audition.clip.samples == decoded.clip.samples.mid(3 * 2) &&
		                 waveform->playheadFrame() == 3,
		             "cursor playback starts at an exact frame without rounding its offset down to milliseconds");
		editor->stopPlayback();
		editor->setSelection(2205, 11025);
#else
		ok &= expect(!editor->findChild<QAction*>(QStringLiteral("audioEditorPlay"))->isEnabled(),
		             "no-playback build still enables editing with disabled transport");
#endif
		editor->applyEdit(QStringLiteral("trim"));
		ok &= expect(editor->isBusy() &&
		                 !editor->findChild<QAction*>(QStringLiteral("audioEditorExport"))->isEnabled(),
		             "pending edit blocks racing export");
		ok &= finish(editor);
		ok &= expect(editor->clip().frameCount() == 8820 && editor->hasChanges(),
		             "trim commits edited PCM and dirty state");
		editor->undo();
		ok &= expect(editor->clip().samples == decoded.clip.samples && !editor->hasChanges() &&
		                 first->value() == 2205 && end->value() == 11025,
		             "undo restores samples, frame range, and saved revision");
		editor->redo();
		ok &= expect(editor->clip().frameCount() == 8820 && editor->hasChanges(), "redo restores edit");
		editor->applyEdit(QStringLiteral("reverse"));
		editor->cancelWork();
		ok &= finish(editor);
		ok &= expect(editor->clip().samples ==
		                 applyAudioEdit(decoded.clip, {QStringLiteral("trim"), 2205, 11025}).clip.samples,
		             "cancelled worker cannot commit partial data");
		const QString saved = QDir(temporary.path()).filePath(QStringLiteral("edited-%1.wav").arg(scale));
		ok &= expect(editor->exportTo(saved), "start export");
		ok &= finish(editor);
		ok &= expect(editor->hasChanges() && QFileInfo::exists(saved),
		             "quantized export does not mark the lossless document saved");
		ok &= finishRecovery(editor);
		const QString checkpoint = editor->recoveryPath();
		AudioProject recovered;
		ok &= expect(readAudioProject(checkpoint, &recovered, nullptr, &error) &&
		                 recovered.clip.samples == editor->clip().samples,
		             "unsaved edit has an exact recovery checkpoint");
		editor->findChild<QAction*>(QStringLiteral("audioEditorRecover"))->trigger();
		auto* manager = editor->findChild<AudioRecoveryDialog*>();
		ok &= expect(manager != nullptr, "Recoveries opens an inspectable manager");
		if (manager) {
			ok &= finishInventory(manager);
			auto* records = manager->findChild<QTableWidget*>(QStringLiteral("audioRecoveryRecords"));
			auto* restoreButton = manager->findChild<QPushButton*>(QStringLiteral("audioRecoveryRestore"));
			auto* discardButton = manager->findChild<QPushButton*>(QStringLiteral("audioRecoveryDiscard"));
			ok &= expect(records && records->rowCount() >= 1 && restoreButton && discardButton &&
			                 restoreButton->isEnabled() &&
			                 records->editTriggers() == QAbstractItemView::NoEditTriggers &&
			                 !records->accessibleName().isEmpty() && records->focusPolicy() != Qt::NoFocus &&
			                 manager->layoutDirection() == editor->layoutDirection(),
			             "manager exposes verified read-only rows and named "
			             "focusable restore controls in inherited "
			             "layout direction");
			QTimer::singleShot(0, manager, []() {
				if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
					if (auto* button = box->button(QMessageBox::Discard)) {
						button->click();
					}
				}
			});
			discardButton->click();
			ok &= finishInventory(manager);
			ok &= expect(QFileInfo::exists(checkpoint) &&
			                 !manager->findChild<QLabel*>(QStringLiteral("audioRecoveryInventoryStatus"))
			                      ->text()
			                      .isEmpty(),
			             "confirmed discard cannot delete a live editor's copy and "
			             "shows the reason");
			ok &= verifyNamedLabels(manager);
			const QString capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (scale == 200) {
				manager->resize(1450, 1100);
			}
			QApplication::processEvents();
			if (!capture.isEmpty()) {
				QImage image(manager->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				manager->render(&image);
				ok &= expect(
				    image.save(QDir(capture).filePath(QStringLiteral("audio-recoveries-%1.png").arg(scale))),
				    "render recovery manager without screen capture");
			}
			manager->resize(700, 400);
			QApplication::processEvents();
			auto* body = manager->findChild<QScrollArea*>(QStringLiteral("audioRecoveryBody"));
			auto* buttons = manager->findChild<QDialogButtonBox*>();
			ok &= expect(body && buttons && manager->rect().contains(buttons->geometry()) &&
			                 !body->geometry().intersects(buttons->geometry()) &&
			                 body->verticalScrollBar()->maximum() > 0,
			             "recovery controls remain scrollable with Close outside the "
			             "body in a short window");
			delete manager;
		}
		const QString projectPath =
		    QDir(temporary.path()).filePath(QStringLiteral("edited-%1.vsaudio").arg(scale));
		ok &= expect(editor->saveProjectTo(projectPath), "start lossless project save");
		ok &= finish(editor);
		ok &= finishRecovery(editor);
		ok &= expect(!editor->hasChanges() && editor->projectPath() == projectPath &&
		                 !QFileInfo::exists(checkpoint),
		             "native save establishes a clean revision and retires its recovery");
		editor->undo();
		ok &= expect(editor->hasChanges(), "undo past saved revision is dirty");
		editor->redo();
		ok &= expect(!editor->hasChanges(), "redo to saved revision is clean");
		editor->applyEdit(QStringLiteral("reverse"));
		ok &= finish(editor);
		// Staging must recheck the context after an asynchronous preparation.
		auto* path = editor->findChild<QLineEdit*>(QStringLiteral("audioEditorPackagePath"));
		path->setText(QStringLiteral("sound/edited.wav"));
		auto* stage = editor->findChild<QAction*>(QStringLiteral("audioEditorStage"));
		stage->trigger();
		targetPackage += QStringLiteral("-changed");
		ok &= finish(editor);
		ok &= expect(handoffs == 0, "package switch during preparation prevents stale handoff");
		targetPackage = assets;
		editor->refreshContext();
		stage->trigger();
		ok &= finish(editor);
		ok &= expect(handoffs == 1 && staging.operations().size() == 1 && editor->hasChanges(),
		             "staging does not mark an unsaved project clean");
		AudioProject external;
		ok &= expect(readAudioProject(projectPath, &external, nullptr, &error),
		             "read independent external-edit fixture");
		external.clip.samples[0] = 0.123456789f;
		ok &= expect(writeAudioProject(external, {projectPath, true, false, {}, {}}).succeeded,
		             "simulate another audio editor saving");
		ok &= expect(editor->saveProjectTo(projectPath), "attempt conflicted native save");
		ok &= finish(editor);
		ok &= expect(editor->hasChanges() && editor->findChild<QLabel*>(QStringLiteral("audioEditorStatus"))
		                                         ->text()
		                                         .contains(QStringLiteral("changed")),
		             "external change blocks save without clearing edits");
		const QString separate =
		    QDir(temporary.path()).filePath(QStringLiteral("separate-%1.vsaudio").arg(scale));
		ok &= expect(editor->saveProjectTo(separate), "save conflicted work to a separate project");
		ok &= finish(editor);
		ok &= verifyNamedLabels(editor);
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			app.processEvents();
			QImage image(editor->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			editor->render(&image);
			ok &=
			    expect(image.save(QDir(captures).filePath(QStringLiteral("audio-editor-%1.png").arg(scale))),
			           "render layout evidence without desktop capture");
			waveform->setVisibleRange(15, 30);
			QImage detail(waveform->size(), QImage::Format_ARGB32_Premultiplied);
			detail.fill(Qt::transparent);
			waveform->render(&detail);
			ok &= expect(
			    detail.save(QDir(captures).filePath(QStringLiteral("audio-samples-%1.png").arg(scale))),
			    "render exact sample detail evidence");
			waveform->zoomToFit();
		}
		const AudioClip preserved = editor->clip();
		if (scale == 200) {
			editor->resize(720, 540);
			app.processEvents();
			ok &= expect(editor->height() == 540 && editor->findChild<QScrollArea*>(),
			             "200% controls remain reachable by scrolling on a smaller window");
			auto* operationStatus = editor->findChild<QLabel*>(QStringLiteral("audioEditorStatus"));
			ok &= expect(operationStatus && operationStatus->height() <= operationStatus->fontMetrics().height() * 3 &&
			                 operationStatus->accessibleDescription() == operationStatus->text(),
			             "expanded save confirmation stays compact and fully accessible in a short window");
			if (!captures.isEmpty()) {
				QImage small(editor->size(), QImage::Format_ARGB32_Premultiplied);
				small.fill(Qt::transparent);
				editor->render(&small);
				ok &= expect(small.save(QDir(captures).filePath(QStringLiteral("audio-small-200.png"))),
				             "render small-window scrolling evidence");
			}
		}
		editor->loadSource(QStringLiteral("bad.ogg"), {}, [](QString*) { return QByteArray("OggS"); });
		ok &= finish(editor);
		ok &= expect(editor->clip().samples == preserved.samples &&
		                 editor->findChild<QLabel*>(QStringLiteral("audioEditorStatus"))
		                     ->text()
		                     .contains(QStringLiteral("Ogg")),
		             "failed open retains the previous document and reports the codec "
		             "limitation");
		delete editor;
		app.removeTranslator(&expanded);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	if (qEnvironmentVariableIsSet("VIBESTUDIO_AUDIO_BENCHMARK")) {
		// Optional near-limit workload: measure actual worker responsiveness and
		// full/detail painting without touching a real audio asset or user input.
		AudioClip longClip{2, 44100, {}};
		longClip.samples.resize(AudioSampleLimit);
		for (qint64 frame = 0; frame < longClip.frameCount(); ++frame) {
			longClip.samples[frame * 2] = float(frame % 1024 - 512) / 1024;
			longClip.samples[frame * 2 + 1] = float(frame % 257 - 128) / 256;
		}
		const QString longPath = QDir(temporary.path()).filePath(QStringLiteral("near-limit.wav"));
		ok &= expect(saveAudioWav(longClip, longPath, false, {}, &error),
		             "create independent near-limit waveform fixture");
		auto* largeEditor = new AudioEditorDialog;
		largeEditor->resize(1240, 900);
		largeEditor->show();
		app.processEvents();
		int pulses = 0;
		QTimer heartbeat;
		QObject::connect(&heartbeat, &QTimer::timeout, &app, [&]() { ++pulses; });
		heartbeat.start(5);
		QElapsedTimer timer;
		timer.start();
		largeEditor->openFile(longPath);
		ok &= finish(largeEditor, 60000);
		const qint64 loadMs = timer.elapsed();
		heartbeat.stop();
		ok &= expect(pulses > 1 && largeEditor->clip().samples == longClip.samples,
		             "near-limit decode/cache work keeps the GUI event loop "
		             "responsive and preserves every sample");
		auto* waveform = largeEditor->findChild<AudioWaveformView*>();
		QImage view(waveform->size(), QImage::Format_ARGB32_Premultiplied);
		view.fill(Qt::transparent);
		timer.restart();
		waveform->render(&view);
		const qint64 fullUs = timer.nsecsElapsed() / 1000;
		waveform->setVisibleRange(longClip.frameCount() / 2, longClip.frameCount() / 2 + 32);
		view.fill(Qt::transparent);
		timer.restart();
		waveform->render(&view);
		const qint64 detailUs = timer.nsecsElapsed() / 1000;
		std::cout << "Audio waveform workload: samples=" << longClip.samples.size() << " loadMs=" << loadMs
		          << " guiHeartbeats=" << pulses << " fullRenderUs=" << fullUs
		          << " detailRenderUs=" << detailUs << '\n';
		const QString captureRoot = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captureRoot.isEmpty()) {
			ok &= expect(view.save(QDir(captureRoot).filePath(QStringLiteral("audio-near-limit-detail.png"))),
			             "retain near-limit sample detail evidence");
		}
		pulses = 0;
		heartbeat.start(5);
		timer.restart();
		largeEditor->analyze();
		ok &= finish(largeEditor, 60000);
		const qint64 analysisMs = timer.elapsed();
		heartbeat.stop();
		auto* report = largeEditor->findChild<AudioAnalysisDialog*>();
		ok &= expect(report && report->analysis().endFrame == longClip.frameCount() && pulses > 1 &&
		                 !largeEditor->hasChanges(),
		             "near-limit analysis scans the full sound while the GUI timer "
		             "continues and saved state stays intact");
		if (report) {
			report->close();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		}
		std::cout << "Audio analysis workload: samples=" << longClip.samples.size()
		          << " analysisMs=" << analysisMs << " guiHeartbeats=" << pulses << '\n';
		timer.restart();
		largeEditor->analyze();
		QTimer::singleShot(25, largeEditor, &AudioEditorDialog::cancelWork);
		ok &= finish(largeEditor, 60000);
		std::cout << "Audio analysis cancellation workload: elapsedMs=" << timer.elapsed() << '\n';
		ok &= expect(!largeEditor->findChild<AudioAnalysisDialog*>() &&
		                 largeEditor->clip().samples == longClip.samples,
		             "cancelling a running near-limit analysis leaves samples "
		             "intact and exposes no partial report");
		delete largeEditor;
	}
	{
		auto* author = new AudioEditorDialog;
		ok &= expect(author->openFile(input), "open source for clipboard authoring");
		ok &= finish(author);
		author->setSelection(100, 200);
		author->copySelection();
		ok &= finish(author);
		ok &= expect(!author->hasChanges() && author->clip().samples == decoded.clip.samples,
		             "copy is non-mutating");
		author->setSelection(0, author->clip().frameCount());
		author->copySelection(true);
		ok &= finish(author);
		ok &= expect(author->clip().samples.isEmpty() && author->hasChanges() &&
		                 !author->findChild<QAction*>(QStringLiteral("audioEditorExport"))->isEnabled() &&
		                 author->findChild<QAction*>(QStringLiteral("audioEditorSave"))->isEnabled(),
		             "cut all leaves a saveable empty project");
		const QString empty = QDir(temporary.path()).filePath(QStringLiteral("empty.vsaudio"));
		ok &= expect(author->saveProjectTo(empty), "save an empty project");
		ok &= finish(author);
		author->pasteSelection();
		ok &= finish(author);
		ok &= expect(author->clip().samples == decoded.clip.samples && author->hasChanges() &&
		                 author->findChild<QAction*>(QStringLiteral("audioEditorUndo"))
		                     ->text()
		                     .contains(QStringLiteral("Paste")),
		             "paste restores the exact cut samples and describes the undo step");
		author->undo();
		ok &= expect(author->clip().samples.isEmpty() && !author->hasChanges(),
		             "undo to the saved empty project is clean");
		author->redo();
		author->setSelection(2205, 2205);
		author->insertSilence(100);
		ok &= finish(author);
		ok &= expect(author->clip().frameCount() == 22150 && author->clip().samples[4410] == 0 &&
		                 author->clip().samples[4610] == decoded.clip.samples[4410],
		             "silence insertion shifts the following audio");
		author->undo();
		author->setSelection(0, 0);
		author->pasteSelection(true);
		ok &= finish(author);
		ok &= expect(author->clip().samples[1] == decoded.clip.samples[1] * 2 &&
		                 author->clip().frameCount() == 22050,
		             "clipboard mixing adds samples without changing duration when it fits");
		auto* newSound = new AudioEditorDialog;
		ok &= expect(newSound->createNew(22050, 2), "create a new empty sound");
		ok &= finish(newSound);
		newSound->pasteSelection();
		ok &= finish(newSound);
		ok &= expect(newSound->clip().samples == decoded.clip.samples,
		             "shared audio clipboard works across editor windows");
		auto* mismatch = new AudioEditorDialog;
		ok &= expect(mismatch->createNew(48000, 1), "create a different audio format");
		ok &= finish(mismatch);
		mismatch->pasteSelection();
		ok &= finish(mismatch);
		ok &= expect(mismatch->clip().frameCount() == 0 &&
		                 mismatch->findChild<QLabel*>(QStringLiteral("audioEditorStatus"))
		                     ->text()
		                     .contains(QStringLiteral("same sample rate")),
		             "format mismatch reports an actionable error without changing the "
		             "document");
		delete mismatch;
		delete newSound;
		delete author;
	}
	PackageArchive shellArchive;
	PackageStagingModel shellStaging;
	ok &= expect(shellArchive.load(assets, &error) && shellStaging.loadBaseArchive(shellArchive, &error),
	             "prepare shell package");
	PackageWriteRequest packageWrite;
	packageWrite.destinationPath = QDir(temporary.path()).filePath(QStringLiteral("audio.pk3"));
	ok &= expect(shellStaging.writeArchive(packageWrite).succeeded(), "write shell package fixture");
	auto* shell = new ApplicationShell;
	shell->openPathFromCommandLine(packageWrite.destinationPath);
	if (auto* audioMode = shell->findChild<QAction*>(QStringLiteral("shell.mode.audio"))) {
		audioMode->trigger();
	}
	auto* list = shell->findChild<QListWidget*>(QStringLiteral("audioEntries"));
	if (list) {
		QEventLoop loop;
		QTimer poll, timeout;
		timeout.setSingleShot(true);
		QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
			if (list->count() &&
			    list->item(0)->data(Qt::UserRole).toString() == QStringLiteral("fixture.wav")) {
				loop.quit();
			}
		});
		QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
		poll.start(10);
		timeout.start(15000);
		loop.exec();
	}
	ok &= expect(list && list->count() > 0 &&
	                 list->item(0)->data(Qt::UserRole).toString() == QStringLiteral("fixture.wav"),
	             "Audio browser sees the generated package sound");
	if (list) {
		list->setCurrentRow(0);
	}
	auto* button = shell->findChild<QPushButton*>(QStringLiteral("openAudioEditor"));
	ok &= expect(button != nullptr, "Audio page exposes editor entry point");
	if (waitForEditButton(button)) {
		button->click();
	}
	auto* editor = shell->findChild<AudioEditorDialog*>();
	ok &= expect(editor != nullptr, "Audio page opens the editor with its package selection");
	if (editor) {
		ok &= finish(editor);
		ok &= expect(editor->clip().frameCount() == 22050, "shell package snapshot loads complete audio");
		editor->applyEdit(QStringLiteral("mono"));
		ok &= finish(editor);
		editor->findChild<QLineEdit*>(QStringLiteral("audioEditorPackagePath"))
		    ->setText(QStringLiteral("sound/shell.wav"));
		editor->findChild<QAction*>(QStringLiteral("audioEditorStage"))->trigger();
		ok &= finish(editor);
		ok &= expect(editor->hasChanges() && editor->findChild<QLabel*>(QStringLiteral("audioEditorStatus"))
		                                         ->text()
		                                         .contains(QStringLiteral("sound/shell.wav")),
		             "shell handoff stages generated bytes and reports the pending package "
		             "save");
		ok &= expect(audioEntry(shell, QStringLiteral("sound/shell.wav")) != nullptr,
		             "newly staged audio appears in the Audio browser before package save");
		const QString shellProject = QDir(temporary.path()).filePath(QStringLiteral("shell-audio.vsaudio"));
		ok &= expect(editor->saveProjectTo(shellProject), "save an audio project from the integrated shell");
		ok &= finish(editor);
		shell->openPathFromCommandLine(shellProject);
		ok &= finish(editor);
		ok &= expect(editor->projectPath() == shellProject && editor->clip().channels == 1,
		             "command-line/drop routing reopens the native audio project "
		             "in its editor");
		const auto replacement = renderAudioDelivery(clip, {AudioDeliveryPreset::Quake3, {}});
		ok &= expect(editor->handoff(replacement.bytes, QStringLiteral("sound/shell.wav"), true, &error),
		             "replace a pending sound through the real shell handoff");
		if (auto* stagedItem = audioEntry(shell, QStringLiteral("sound/shell.wav"))) {
			list->setCurrentItem(stagedItem);
			if (waitForEditButton(button)) { button->click(); }
			ok &= finish(editor);
			ok &=
			    expect(editor->clip().samples ==
			               decodeAudioClip(QStringLiteral("sound/shell.wav"), replacement.bytes).clip.samples,
			           "opening a replaced Audio browser entry uses current staged bytes");
		}
	}
	delete shell;
	app.processEvents();
	// Doom package integration uses generated fixtures, never game content.
	const QString emptyWadPath = QDir(temporary.path()).filePath(QStringLiteral("empty.wad"));
	QFile emptyWad(emptyWadPath);
	ok &= expect(emptyWad.open(QIODevice::WriteOnly) &&
	                 emptyWad.write(QByteArray::fromHex("50574144000000000c000000")) == 12,
	             "write an independent empty Doom WAD fixture");
	emptyWad.close();
	PackageArchive doomArchive;
	PackageStagingModel doomStaging;
	ok &= expect(doomArchive.load(emptyWadPath, &error) && doomStaging.loadBaseArchive(doomArchive, &error) &&
	                 stageAudioDelivery(renderAudioDelivery(clip, {AudioDeliveryPreset::Doom, {}}).bytes,
	                                    QStringLiteral("DSFIXTUR"), &doomStaging, false, &error),
	             "prepare generated Doom sound package");
	PackageWriteRequest doomWrite;
	doomWrite.destinationPath = QDir(temporary.path()).filePath(QStringLiteral("sounds.wad"));
	doomWrite.format = PackageArchiveFormat::Wad;
	ok &= expect(doomStaging.writeArchive(doomWrite).succeeded(), "write Doom shell fixture");
	auto* doomShell = new ApplicationShell;
	doomShell->openPathFromCommandLine(doomWrite.destinationPath);
	doomShell->findChild<QAction*>(QStringLiteral("shell.mode.audio"))->trigger();
	auto* doomItem = audioEntry(doomShell, QStringLiteral("DSFIXTUR"), true);
	ok &= expect(doomItem != nullptr, "Doom resource WAD reaches the Audio browser");
	if (doomItem) {
		doomShell->findChild<QListWidget*>(QStringLiteral("audioEntries"))->setCurrentItem(doomItem);
		auto* editButton = doomShell->findChild<QPushButton*>(QStringLiteral("openAudioEditor"));
		if (waitForEditButton(editButton)) { editButton->click(); }
		auto* doomEditor = doomShell->findChild<AudioEditorDialog*>();
		ok &= expect(doomEditor != nullptr, "Doom browser entry opens the integrated audio editor");
		if (doomEditor) {
			ok &= finish(doomEditor);
			ok &= expect(doomEditor->context().canStage && doomEditor->context().doomWad &&
			                 doomEditor->findChild<QComboBox*>(QStringLiteral("audioStagePreset"))
			                         ->currentData()
			                         .toInt() == int(AudioDeliveryPreset::Doom),
			             "Doom package context selects its compatible delivery preset");
			doomEditor->applyEdit(QStringLiteral("gain"), -3);
			ok &= finish(doomEditor);
			doomEditor->findChild<QLineEdit*>(QStringLiteral("audioEditorPackagePath"))
			    ->setText(QStringLiteral("DSNEW"));
			doomEditor->findChild<QAction*>(QStringLiteral("audioEditorStage"))->trigger();
			ok &= finish(doomEditor);
			ok &=
			    expect(doomEditor->hasChanges() && audioEntry(doomShell, QStringLiteral("DSNEW")) != nullptr,
			           "Doom handoff appears immediately as pending audio without "
			           "saving the editable project");
			PackageArchive untouched;
			QByteArray absent;
			ok &= expect(untouched.load(doomWrite.destinationPath, &error) &&
			                 !untouched.readEntryBytes(QStringLiteral("DSNEW"), &absent, &error),
			             "Doom staging does not write through to the opened source WAD");
		}
	}
	delete doomShell;
	app.processEvents();
	// Owner destruction (as in an interrupted session) preserves its latest
	// accepted edit. Restoration is a draft and has no writable source identity.
	auto* interrupted = new AudioEditorDialog;
	ok &= expect(interrupted->openFile(input), "open recovery source");
	ok &= finish(interrupted);
	interrupted->setSelection(100, 8000);
	interrupted->applyEdit(QStringLiteral("reverse"));
	ok &= finish(interrupted);
	const AudioClip latest = interrupted->clip();
	const QString recovery = interrupted->recoveryPath();
	delete interrupted;
	auto* restored = new AudioEditorDialog;
	restored->show();
	ok &= expect(restored->openProject(recovery, true, QByteArray(32, 'x')),
	             "start stale reviewed recovery read");
	ok &= finish(restored);
	ok &= expect(restored->clip().channels == 0 && !restored->hasChanges(),
	             "a stale recovery digest cannot replace the editor document");
	restored->findChild<QAction*>(QStringLiteral("audioEditorRecover"))->trigger();
	auto* recoveryManager = restored->findChild<AudioRecoveryDialog*>();
	ok &= expect(recoveryManager != nullptr, "restore uses the recovery manager");
	if (recoveryManager) {
		ok &= finishInventory(recoveryManager);
		auto* records = recoveryManager->findChild<QTableWidget*>(QStringLiteral("audioRecoveryRecords"));
		const auto inventory = listAudioRecoveries(audioRecoveryDirectory());
		for (qsizetype row = 0; row < inventory.records.size(); ++row) {
			if (inventory.records[row].path == recovery) {
				records->selectRow(int(row));
			}
		}
		recoveryManager->findChild<QPushButton*>(QStringLiteral("audioRecoveryRestore"))->click();
	}
	ok &= finish(restored);
	ok &= expect(restored->hasChanges() && restored->projectPath().isEmpty() &&
	                 restored->clip().samples == latest.samples &&
	                 restored->findChild<QSpinBox*>(QStringLiteral("audioSelectionStart"))->value() == 100,
	             "recovery restores exact samples and selection as an unsaved draft");
	const QByteArray recoveryBeforeSave = readFile(recovery);
	ok &= expect(restored->saveProjectTo(recovery, true),
	             "start a guarded save to the restored checkpoint path");
	ok &= finish(restored);
	ok &= expect(restored->hasChanges() && restored->projectPath().isEmpty() &&
	                 readFile(recovery) == recoveryBeforeSave,
	             "restored drafts cannot overwrite their original recovery "
	             "checkpoint even with explicit overwrite");
	const QString restoredPath = QDir(temporary.path()).filePath(QStringLiteral("restored.vsaudio"));
	ok &= expect(restored->saveProjectTo(restoredPath), "save restored draft to an explicit destination");
	ok &= finish(restored);
	ok &= finishRecovery(restored);
	ok &= expect(!restored->hasChanges() && QFileInfo::exists(recovery),
	             "recovery source remains intact after restoring and saving a copy");
	// Test actual save/cancel transitions using dialog button methods, without
	// sending keyboard or mouse events to the computer.
	restored->applyEdit(QStringLiteral("gain"), -2);
	ok &= finish(restored);
	QTimer answer;
	QMessageBox::StandardButton response = QMessageBox::Cancel;
	QObject::connect(&answer, &QTimer::timeout, &app, [&]() {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			if (auto* chosen = box->button(response)) {
				chosen->click();
			}
		}
	});
	answer.start(5);
	ok &= expect(!restored->openFile(input) && restored->hasChanges(),
	             "cancelled Open keeps the edited document");
	const AudioClip beforeNew = restored->clip();
	ok &= expect(!restored->createNew(8000, 1) && restored->clip().samples == beforeNew.samples &&
	                 restored->hasChanges(),
	             "cancelled New keeps the current edited document");
	ok &= expect(!restored->close() && restored->hasChanges(), "cancelled Close keeps the edited document");
	response = QMessageBox::Save;
	const AudioClip beforeClose = restored->clip();
	QPointer<AudioEditorDialog> closing = restored;
	ok &= expect(!restored->close(), "Save and Close waits for the atomic save to finish");
	answer.stop();
	{
		QEventLoop loop;
		QTimer poll, timeout;
		timeout.setSingleShot(true);
		QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
			if (!closing) {
				loop.quit();
			}
		});
		QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
		poll.start(10);
		timeout.start(15000);
		loop.exec();
	}
	AudioProject afterClose;
	ok &= expect(!closing && readAudioProject(restoredPath, &afterClose, nullptr, &error) &&
	                 afterClose.clip.samples == beforeClose.samples,
	             "Save and Close commits the latest exact samples before "
	             "destroying the editor");
	if (closing) {
		delete closing;
	}
	{
		auto* markerEditor = new AudioEditorDialog;
		ok &= expect(markerEditor->createNew(22050, 1, 20), "create a marker persistence document");
		ok &= finish(markerEditor);
		const AudioMarkers authored{{{1, 3, QString::fromUtf8("Loop entrée")}}, AudioLoop{4, 16}};
		ok &= expect(markerEditor->setMarkers(authored), "set typed markers through the editor service");
		const QString markerProject = QDir(temporary.path()).filePath(QStringLiteral("markers.vsaudio"));
		ok &= expect(markerEditor->saveProjectTo(markerProject),
		             "save marker-bearing native project asynchronously");
		ok &= finish(markerEditor);
		ok &= expect(!markerEditor->hasChanges(), "successful marker save establishes a clean revision");
		auto* reopened = new AudioEditorDialog;
		ok &=
		    expect(reopened->openProject(markerProject), "reopen native marker document through the editor");
		ok &= finish(reopened);
		ok &= expect(reopened->clip().markers == authored && !reopened->hasChanges(),
		             "GUI save and reopen preserves every typed marker");
		delete reopened;
		ok &= expect(!markerEditor->setMarkers({{}, AudioLoop{4, 21}}) &&
		                 markerEditor->clip().markers == authored && !markerEditor->hasChanges(),
		             "invalid programmatic markers cannot dirty or replace a saved "
		             "document");
		markerEditor->setMarkers({});
		ok &= expect(markerEditor->hasChanges(), "clearing saved markers is a real edit");
		markerEditor->undo();
		ok &= expect(!markerEditor->hasChanges() && markerEditor->clip().markers == authored,
		             "undo to a marker-bearing saved revision clears the dirty flag");
		ok &= finishRecovery(markerEditor);
		delete markerEditor;
	}
	const QString writerRoot = QDir(temporary.path()).filePath(QStringLiteral("writer-race"));
	const QString retiredId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const QString retainedId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	{
		AudioRecoveryWriter writer(writerRoot);
		writer.checkpoint(retiredId, 1, afterClose);
		writer.checkpoint(retiredId, 2, afterClose);
		writer.retire(retiredId);
		writer.checkpoint(retainedId, 3, afterClose);
	}
	ok &= expect(!QFileInfo::exists(audioRecoveryPath(writerRoot, retiredId)) &&
	                 QFileInfo::exists(audioRecoveryPath(writerRoot, retainedId)),
	             "retired queued/in-flight copies cannot return; shutdown "
	             "flushes the newer document");
	StudioSettings::setOverrideFilePath({});
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
