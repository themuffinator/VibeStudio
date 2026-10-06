#include "app/audio_effects_dialog.h"
#include "app/audio_session_dialog.h"
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
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *text)
{
	if (!value)
		std::cerr << text << '\n';
	return value;
}
bool wait(const std::function<bool()> &ready)
{
	QElapsedTimer clock;
	clock.start();
	while (!ready() && clock.elapsed() < 15000) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	QCoreApplication::processEvents();
	return ready();
}
class Expanded final : public QTranslator {
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).contains("AudioEffect"))
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
	QTemporaryDir temporary(QDir(root).filePath("effects-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	AudioProject source;
	source.sourceName = "Dialogue";
	source.clip = {2, 48000, QVector<float>(512, .125f)};
	source.endFrame = 256;
	if (!expect(session.importSource(source) && wait([&] { return !session.isBusy(); }), "prepare effects UI fixture"))
		return EXIT_FAILURE;
	const auto track = session.session().tracks[0].id;
	session.selectRegion(track, {});
	bool applied = false;
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioEffectsDialog *>();
		if (!dialog)
			return;
		auto *type = dialog->findChild<QComboBox *>("effectsType");
		for (const auto &name : {QString("peak-eq"), QString("compressor"), QString("delay")}) {
			type->setCurrentIndex(type->findData(name));
			dialog->findChild<QPushButton *>("effectsAdd")->click();
			if (name == "peak-eq")
				dialog->findChild<QDoubleSpinBox *>("effectParameter_gainDb")->setValue(6);
		}
		dialog->findChild<QPushButton *>("effectsUp")->click();
		dialog->findChild<QDoubleSpinBox *>("effectParameter_mix")->setValue(.5);
		dialog->findChild<QCheckBox *>("effectsEnabled")->setChecked(false);
		dialog->findChild<QDoubleSpinBox *>("effectsTail")->setValue(.01);
		applied = dialog->edit().effects.size() == 3;
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	session.findChild<QAction *>("sessionEffects")->trigger();
	bool ok = expect(applied && session.session().tracks[0].effects.size() == 3 &&
	                     session.session().tracks[0].effects[1].type == "delay" &&
	                     !session.session().tracks[0].effects[1].enabled &&
	                     session.session().tracks[0].effects[0].parameters.value("gainDb") == 6 &&
	                     session.session().effectTailSeconds == .01,
	                 "actual session action stages insert order, parameters, bypass and tail together");
	session.undo();
	ok &= expect(session.session().tracks[0].effects.isEmpty() && session.session().effectTailSeconds == 2,
	             "entire effects draft undoes in one step");
	session.redo();
	ok &= expect(session.session().tracks[0].effects.size() == 3, "redo restores chain identities and settings");
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioEffectsDialog *>();
		if (!dialog)
			return;
		auto *type = dialog->findChild<QComboBox *>("effectsType");
		type->setCurrentIndex(type->findData("lookahead-limiter"));
		dialog->findChild<QPushButton *>("effectsAdd")->click();
		dialog->findChild<QDoubleSpinBox *>("effectParameter_ceilingDb")->setValue(-3);
		dialog->findChild<QDoubleSpinBox *>("effectParameter_lookaheadMs")->setValue(3.333);
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	session.findChild<QAction *>("sessionMasterEffects")->trigger();
	ok &= expect(session.session().masterEffects.size() == 1 &&
	                 session.session().masterEffects[0].parameters.value("ceilingDb") == -3,
	             "master inserts use a separate chain through the same editor");
	ok &= expect(audioEffectLatencyFrames(session.session().masterEffects, 48000) == 160,
	             "GUI structural lookahead rounds to fixed whole frames");
	session.undo();
	ok &= expect(session.session().masterEffects.isEmpty(), "undo removes lookahead and its latency together");
	session.redo();
	AudioEffectsDialog latent(session.session(), {});
	auto *lookahead = latent.findChild<QDoubleSpinBox *>("effectParameter_lookaheadMs");
	ok &= expect(lookahead && !lookahead->toolTip().isEmpty() && !lookahead->accessibleDescription().isEmpty() &&
	                 latent.findChild<QLabel *>("effectsStatus")->text().contains("160"),
	             "latency and structural-parameter behavior are visible and accessible");
	QTimer::singleShot(0, &latent, [&] {
		auto *dialog = latent.findChild<QDialog *>("effectAutomationDialog");
		if (!dialog)
			return;
		auto *parameters = dialog->findChild<QComboBox *>("automationParameter");
		ok &= expect(parameters && parameters->count() == 3 && parameters->findData("lookaheadMs") < 0 &&
		                 parameters->findData("ceilingDb") >= 0,
		             "automation editor excludes structural latency control");
		dialog->reject();
	});
	latent.findChild<QPushButton *>("effectsAutomation")->click();
	const auto path = QDir(temporary.path()).filePath("effects.vssession");
	ok &= expect(session.saveSession(path) && wait([&] { return !session.isBusy(); }),
	             "save effect session asynchronously");
	AudioSession reloaded;
	QString error;
	ok &= expect(readAudioSession(path, &reloaded, nullptr, &error) &&
	                 audioSessionSummary(reloaded) == audioSessionSummary(session.session()),
	             "native reload preserves complete GUI effect state");
	AudioEffectsDialog missing(session.session(), "missing");
	ok &= expect(!missing.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),
	             "missing track cannot accidentally edit master");
	const auto prior = session.session();
	QTimer::singleShot(0, &session, [&] {
		auto *dialog = session.findChild<AudioEffectsDialog *>();
		if (!dialog)
			return;
		auto *factory = dialog->findChild<QComboBox *>("effectsFactoryPreset");
		factory->setCurrentIndex(factory->findData("large-hall"));
		dialog->findChild<QPushButton *>("effectsLoadFactoryPreset")->click();
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	session.findChild<QAction *>("sessionEffects")->trigger();
	ok &= expect(
	    session.session().tracks[0].effects.size() == 1 && session.session().tracks[0].effects[0].type == "reverb" &&
	        session.session().effectTailSeconds == 12 && session.session().masterEffects == prior.masterEffects,
	    "actual factory button replaces only selected chain with one staged edit");
	session.undo();
	ok &= expect(audioSessionSummary(session.session()) == audioSessionSummary(prior),
	             "preset chain and global tail undo together");
	AudioEffectsDialog presets(session.session(), track);
	ok &= presets.applyPreset(makeAudioEffectPreset("large-hall", 48000));
	const auto presetPath = QDir(temporary.path()).filePath("room.vsfx");
	presets.findChild<QLineEdit *>("effectsPresetName")->setText("Studio Hall");
	ok &= expect(presets.savePresetFile(presetPath) && presets.busy() &&
	                 !presets.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled() &&
	                 wait([&] { return !presets.busy(); }),
	             "preset save has a visible busy state and completes asynchronously");
	AudioEffectPreset savedPreset;
	ok &= expect(readAudioEffectPreset(presetPath, &savedPreset, nullptr, &error) && savedPreset.name == "Studio Hall",
	             "GUI save writes exact staged preset name");
	ok &= presets.applyPreset(makeAudioEffectPreset("wide-chorus", 48000));
	ok &= expect(presets.loadPresetFile(presetPath) && wait([&] { return !presets.busy(); }) &&
	                 presets.edit().effects[0].type == "reverb" &&
	                 presets.edit().effects[0].id != savedPreset.effects[0].id,
	             "asynchronous file load installs fresh effect identities in the draft");
	const auto draftBefore = presets.edit().effects;
	const auto invalidPath = QDir(temporary.path()).filePath("invalid.vsfx");
	QFile invalid(invalidPath);
	ok &= invalid.open(QIODevice::WriteOnly);
	invalid.write("{}");
	invalid.close();
	ok &= expect(presets.loadPresetFile(invalidPath) && wait([&] { return !presets.busy(); }) &&
	                 presets.edit().effects == draftBefore,
	             "failed preset load preserves staged effects");
	ok &= presets.applyPreset(makeAudioEffectPreset("small-room", 48000));
	QFile external(presetPath);
	ok &= external.open(QIODevice::Append);
	external.write(" ");
	external.close();
	ok &= external.open(QIODevice::ReadOnly);
	const auto changed = external.readAll();
	external.close();
	ok &= presets.savePresetFile(presetPath, true) && wait([&] { return !presets.busy(); });
	ok &= external.open(QIODevice::ReadOnly);
	ok &= expect(external.readAll() == changed &&
	                 presets.findChild<QLabel *>("effectsStatus")->text().contains("changed"),
	             "GUI guarded save preserves external edits");
	external.close();
	presets.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
	AudioEffectsDialog cancelled(session.session(), track);
	const auto beforeCancel = cancelled.edit().effects;
	ok &= cancelled.loadPresetFile(presetPath);
	cancelled.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
	ok &= expect(wait([&] { return !cancelled.busy(); }) && cancelled.result() == QDialog::Rejected &&
	                 cancelled.edit().effects == beforeCancel,
	             "closing a busy preset reader cancels without applying a late result");
	ok &= expect(audioSessionSummary(session.session()) == audioSessionSummary(prior),
	             "preset file operations and draft cancellation leave parent session unchanged");
	const auto renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_EFFECTS_RENDER_DIR");
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
		auto visual = session.session();
		visual.tracks[0].effects.clear();
		for (const auto &type : {"reverb", "chorus", "flanger", "phaser", "tremolo", "lookahead-limiter"})
			visual.tracks[0].effects.append(makeAudioEffect(type, visual.sampleRate));
		AudioEffectsDialog dialog(visual, track);
		dialog.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.resize(920, 820);
		dialog.show();
		QCoreApplication::processEvents();
		if (layout.rtl)
			ok &= expect(dialog.findChild<QPushButton *>("effectsAdd")->text().contains(" · "),
			             "expanded translator reaches the effects UI");
		auto *list = dialog.findChild<QListWidget *>("effectsChain");
		dialog.findChild<QToolButton *>("effectsPresets")->setChecked(true);
		list->setCurrentRow(0);
		QCoreApplication::processEvents();
		list->doItemsLayout();
		list->scrollToItem(list->currentItem(), QAbstractItemView::PositionAtBottom);
		for (int settle = 0; settle < 3; ++settle)
			QCoreApplication::processEvents();
		for (auto *box : dialog.findChildren<QDoubleSpinBox *>()) {
			const auto *accessible = QAccessible::queryAccessibleInterface(box);
			ok &= expect(!box->accessibleName().isEmpty() && box->focusPolicy() != Qt::NoFocus && accessible,
			             "native parameter controls expose accessible names, roles and focus");
		}
		auto *scroll = dialog.findChild<QScrollArea *>();
		if (scroll->horizontalScrollBar()->maximum() || list->horizontalScrollBar()->maximum())
			std::cerr << layout.name << " horizontal body=" << scroll->horizontalScrollBar()->maximum()
			          << " list=" << list->horizontalScrollBar()->maximum() << '\n';
		ok &= expect(scroll->horizontalScrollBar()->maximum() == 0 && list->horizontalScrollBar()->maximum() == 0,
		             "expanded effect forms fit without horizontal scrolling");
		const auto capture = [&](const QString &suffix) {
			if (renderRoot.isEmpty())
				return;
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= image.save(QDir(renderRoot).filePath(QString::fromLatin1(layout.name) + suffix + ".png"));
		};
		capture("");
		for (auto *widget : {static_cast<QWidget *>(dialog.findChild<QLineEdit *>("effectsPresetName")),
		                     static_cast<QWidget *>(dialog.findChild<QComboBox *>("effectsFactoryPreset"))})
			ok &= expect(!widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus &&
			                 QAccessible::queryAccessibleInterface(widget),
			             "preset controls expose native accessibility and focus");
		const auto reachParameters = [&] {
			auto controls = dialog.findChildren<QDoubleSpinBox *>();
			auto *tail = dialog.findChild<QDoubleSpinBox *>("effectsTail");
			controls.removeAll(tail);
			controls.append(tail);
			for (auto *box : controls) {
				for (int settle = 0; settle < 3; ++settle) {
					// Qt's ensureWidgetVisible follows the spin box's line-edit focus
					// proxy, which excludes its styled frame. Exercise the actual
					// scrollbar range and verify the complete control instead.
					const int top = box->mapTo(scroll->widget(), QPoint()).y();
					scroll->verticalScrollBar()->setValue(top - (scroll->viewport()->height() - box->height()) / 2);
					QCoreApplication::processEvents();
				}
				const QRect rectangle(box->mapTo(scroll->viewport(), QPoint()), box->size());
				const bool visible = scroll->viewport()->rect().contains(rectangle);
				if (!visible)
					std::cerr << layout.name << " width=" << dialog.width()
					          << " control=" << box->objectName().toStdString() << " rect=" << rectangle.x() << ','
					          << rectangle.y() << ',' << rectangle.width() << ',' << rectangle.height()
					          << " viewport=" << scroll->viewport()->width() << ',' << scroll->viewport()->height()
					          << " scroll=" << scroll->verticalScrollBar()->value() << '/'
					          << scroll->verticalScrollBar()->maximum() << '\n';
				ok &= expect(visible, "every parameter and the saved tail can be scrolled completely into view");
			}
		};
		reachParameters();
		capture("-parameters");
		dialog.resize(600, 400);
		QCoreApplication::processEvents();
		ok &= expect(dialog.width() == 600 && scroll->horizontalScrollBar()->maximum() == 0 &&
		                 list->horizontalScrollBar()->maximum() == 0 && scroll->verticalScrollBar()->maximum() > 0,
		             "small scaled effects editor keeps all controls reachable");
		reachParameters();
		capture("-small");
		for (int selected = 1; selected < list->count(); ++selected) {
			list->setCurrentRow(selected);
			QCoreApplication::processEvents();
			for (auto *box : dialog.findChildren<QDoubleSpinBox *>())
				ok &= expect(!box->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(box),
				             "all modulation parameters expose native accessibility");
			reachParameters();
			if (selected == 3)
				capture("-phaser-small");
			if (selected == 5) {
				capture("-lookahead-small");
				dialog.resize(920, 820);
				QCoreApplication::processEvents();
				reachParameters();
				capture("-lookahead");
			}
		}
		dialog.findChild<QToolButton *>("effectsPresets")->setChecked(false);
		QCoreApplication::processEvents();
		ok &= expect(!dialog.findChild<QLineEdit *>("effectsPresetName")->isVisible(),
		             "preset disclosure hides its optional controls");
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
