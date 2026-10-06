#include "app/application_shell.h"
#include "app/audio_editor_dialog.h"
#include "app/audio_placement_dialog.h"
#include "app/studio_theme.h"

#include <QAction>
#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
bool waitFor(const std::function<bool()>& ready)
{
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
		if (ready()) {
			loop.quit();
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(15000);
	if (!ready()) {
		loop.exec();
	}
	return ready();
}
bool finish(AudioEditorDialog* editor)
{
	return expect(waitFor([&] { return !editor->isBusy(); }),
	              "audio worker finishes without blocking GUI events");
}
bool review(AudioEditorDialog* editor, const std::function<void(AudioPlacementDialog*)>& action = {})
{
	auto* command = editor->findChild<QAction*>(QStringLiteral("audioEditorPlace"));
	if (!command || !command->isEnabled()) {
		return expect(false, "placement command is available");
	}
	bool seen = false;
	QTimer submit, deadline;
	submit.setSingleShot(true);
	deadline.setSingleShot(true);
	QObject::connect(&submit, &QTimer::timeout, editor, [&] {
		if (auto* dialog = editor->findChild<AudioPlacementDialog*>()) {
			seen = true;
			if (action) {
				action(dialog);
			} else {
				dialog->findChild<QPushButton*>(QStringLiteral("audioPlacementApply"))->click();
			}
		}
	});
	QObject::connect(&deadline, &QTimer::timeout, editor, [&] {
		if (auto* dialog = editor->findChild<AudioPlacementDialog*>()) {
			dialog->reject();
		}
	});
	submit.start(0);
	deadline.start(5000);
	command->trigger();
	return expect(seen, "placement uses a review dialog");
}
class Expanded final : public QTranslator {
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!QByteArray(context).endsWith("AudioPlacementDialog")) {
			return {};
		}
		const QString text = QString::fromUtf8(source);
		return QStringLiteral("[%1 %2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace

int main(int argc, char** argv)
{
	// Direct Qt methods and QWidget rendering only; no OS capture or input events.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	app.setQuitOnLastWindowClosed(false);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-level-ui-XXXXXX")));
	if (!temporary.isValid()) {
		return 2;
	}
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	StudioSettings().setAudioRecoveryEnabled(false);
	qputenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT",
	        QDir(temporary.path()).filePath(QStringLiteral("recovery")).toUtf8());
	bool ok = true;
	QString error;
	for (int scale : {100, 200}) {
		Expanded expanded;
		if (scale == 200) {
			app.installTranslator(&expanded);
		}
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::HighContrastDark
		                                                     : StudioTheme::HighContrastLight,
		                                        UiDensity::Standard, scale));
		AudioPlacementDialog dialog({LevelMapFormat::QuakeMap, {}}, QStringLiteral("maps/large-map-name.map"),
		                            QStringLiteral("C:/a long example project path/assets.pk3"),
		                            {{}, QStringLiteral("sound/world/hum.wav"), {64, 0, 96, true}});
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
		}
		dialog.show();
		app.processEvents();
		auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("audioPlacementApply"));
		auto* game = dialog.findChild<QComboBox*>(QStringLiteral("audioPlacementGame"));
		auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("audioPlacementMode"));
		auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("audioPlacementTargetName"));
		auto* preview = dialog.findChild<QLabel*>(QStringLiteral("audioPlacementPreview"));
		ok &= expect(!apply->isEnabled(), "ambiguous map requires explicit game selection");
		game->setCurrentIndex(game->findData(QStringLiteral("quake3")));
		ok &= expect(!apply->isEnabled(), "wrong map profile rejected before submission");
		game->setCurrentIndex(game->findData(QStringLiteral("quake2")));
		ok &= expect(apply->isEnabled() && preview->text().contains(QStringLiteral("noise: world/hum.wav")),
		             "Quake II reference is reviewed without sound prefix");
		mode->setCurrentIndex(mode->findData(QStringLiteral("triggered")));
		ok &= expect(!apply->isEnabled(), "inactive speaker cannot be unreachable by default");
		name->setText(QStringLiteral("alarm"));
		app.processEvents();
		ok &= expect(apply->isEnabled() && dialog.request().targetName == QStringLiteral("alarm"),
		             "target makes triggered speaker valid");
		for (auto* label : dialog.findChildren<QLabel*>()) {
			if (!label->accessibleName().isEmpty()) {
				const auto* accessible = QAccessible::queryAccessibleInterface(label);
				ok &= expect(accessible && accessible->text(QAccessible::Description) == label->text(),
				             "placement review exposes current entity properties and delivery details to assistive technology");
			}
		}
		for (auto* widget : dialog.findChildren<QWidget*>()) {
			if (qobject_cast<QComboBox*>(widget) || qobject_cast<QDoubleSpinBox*>(widget) || widget == name ||
			    widget == apply) {
				ok &= expect(!widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus,
				             "placement controls expose names and focus");
			}
		}
		ok &= expect(dialog.rect().contains(apply->mapTo(&dialog, apply->rect().center())),
		             "apply remains visible at 100/200 percent");
		ok &= expect(dialog.findChild<QScrollArea*>()->horizontalScrollBar()->maximum() == 0,
		             "expanded RTL placement form fits without horizontal body scrolling");
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= expect(
			    image.save(QDir(captures).filePath(QStringLiteral("audio-placement-%1.png").arg(scale))),
			    "save placement layout evidence");
			auto* scroll = dialog.findChild<QScrollArea*>();
			scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
			app.processEvents();
			dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(
			                 QStringLiteral("audio-placement-bottom-%1.png").arg(scale))),
			             "save placement preview evidence");
		}
		if (scale == 200) {
			app.removeTranslator(&expanded);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		AudioEditorDialog editor;
		AudioEditorContext current{QStringLiteral("same-package.pk3"), true};
		current.packageToken = QStringLiteral("p1");
		current.levelToken = QStringLiteral("m1");
		current.levelName = QStringLiteral("map.map");
		current.levelTarget = {LevelMapFormat::Quake3Map, QStringLiteral("quake3")};
		current.levelOrigin = {64, 0, 96, true};
		editor.context = [&] { return current; };
		int handoffs = 0;
		editor.handoff = [&](const QByteArray&, const QString&, bool, QString*) {
			++handoffs;
			return true;
		};
		editor.levelHandoff = [&](const QByteArray& bytes, const LevelSoundRequest& request, bool, QString*) {
			++handoffs;
			ok &= expect(request.game == QStringLiteral("quake3") && request.origin.x == 64 &&
			                 validateLevelSoundWav(bytes),
			             "handoff receives matching game delivery and reviewed position");
			return true;
		};
		editor.refreshContext();
		ok &= expect(editor.createNew(44100, 2, 4410), "create editor test clip");
		ok &= finish(&editor);
		editor.show();
		ok &= review(&editor);
		ok &= finish(&editor);
		ok &= expect(handoffs == 1 && editor.hasChanges(), "placement preserves unsaved native project");
		ok &= review(&editor, [&](AudioPlacementDialog* dialog) {
			current.levelToken = QStringLiteral("m2");
			dialog->accept();
		});
		ok &= expect(!editor.isBusy() && handoffs == 1,
		             "map change while reviewing prevents conversion and commit");
		editor.refreshContext();
		ok &= review(&editor);
		current.levelToken = QStringLiteral("m3");
		ok &= finish(&editor);
		ok &= expect(handoffs == 1 && editor.findChild<QLabel*>(QStringLiteral("audioEditorStatus"))
		                                  ->text()
		                                  .contains(QStringLiteral("changed")),
		             "map change during conversion rejects stale entity placement");
		editor.refreshContext();
		editor.findChild<QAction*>(QStringLiteral("audioEditorStage"))->trigger();
		current.packageToken = QStringLiteral("p2");
		ok &= finish(&editor);
		ok &= expect(handoffs == 1, "same-path package revision change rejects ordinary staging");
		editor.refreshContext();
		ok &= review(&editor, [](AudioPlacementDialog* dialog) { dialog->reject(); });
		ok &= expect(!editor.isBusy() && handoffs == 1, "cancelled review has no effects");
		editor.refreshContext();
		ok &= review(&editor);
		editor.cancelWork();
		ok &= finish(&editor);
		ok &= expect(handoffs == 1, "cancelled conversion has no map or package effects");
		current.levelTarget = {LevelMapFormat::DoomWad, {}};
		editor.refreshContext();
		ok &= expect(!editor.findChild<QAction*>(QStringLiteral("audioEditorPlace"))->isEnabled(),
		             "unsupported map keeps placement disabled");
	}
	// Real shell: pending sound bytes must appear in Audio and the map together.
	const QString folder = QDir(temporary.path()).filePath(QStringLiteral("assets"));
	QDir().mkpath(folder);
	AudioClip source{2, 44100, QVector<float>(4410 * 2, 0.125f)};
	const auto original = encodeAudioWav(source);
	QFile input(QDir(folder).filePath(QStringLiteral("fixture.wav")));
	ok &= expect(input.open(QIODevice::WriteOnly) && input.write(original) == original.size(),
	             "write shell fixture");
	input.close();
	auto* shell = new ApplicationShell;
	shell->show();
	shell->openPathFromCommandLine(folder);
	shell->findChild<QAction*>(QStringLiteral("shell.mode.audio"))->trigger();
	auto* list = shell->findChild<QListWidget*>(QStringLiteral("audioEntries"));
	ok &= expect(waitFor([&] {
		             return list && list->count() &&
		                    list->item(0)->data(Qt::UserRole).toString() == QStringLiteral("fixture.wav");
	             }),
	             "shell loads package sound");
	if (list && list->count()) {
		list->setCurrentRow(0);
	}
	shell->findChild<QPushButton*>(QStringLiteral("openAudioEditor"))->click();
	auto* editor = shell->findChild<AudioEditorDialog*>();
	ok &= expect(editor != nullptr, "shell opens editor");
	if (editor) {
		ok &= finish(editor);
		LevelMapCreateRequest create;
		create.starterRoom = false;
		ok &= expect(shell->createLevelDocument(create, &error), "open level beside the audio editor");
		editor->findChild<QLineEdit*>(QStringLiteral("audioEditorPackagePath"))
		    ->setText(QStringLiteral("sound/world/hum.wav"));
		ok &= review(editor);
		ok &= finish(editor);
		const auto& map = shell->levelDocument();
		ok &= expect(map.entities.last().className == QStringLiteral("target_speaker") &&
		                 !map.undoStack.isEmpty(),
		             "real shell records undoable speaker");
		int soundRow = -1;
		ok &= expect(waitFor([&] {
			             for (int row = 0; row < list->count(); ++row) {
				             if (list->item(row)->data(Qt::UserRole).toString() ==
				                 QStringLiteral("sound/world/hum.wav")) {
					             soundRow = row;
					             return true;
				             }
			             }
			             return false;
		             }),
		             "audio browser exposes pending placed sound");
		ok &= expect(!QFileInfo::exists(QDir(folder).filePath(QStringLiteral("sound/world/hum.wav"))),
		             "handoff waits for explicit package save");
		if (soundRow >= 0) {
			list->setCurrentRow(soundRow);
			shell->findChild<QPushButton*>(QStringLiteral("openAudioEditor"))->click();
			ok &= finish(editor);
			ok &= expect(editor->clip().channels == 1 && editor->clip().sampleRate == 22050 &&
			                 editor->clip().frameCount() == 2205,
			             "browser and editor read the same converted pending bytes");
		}
	}
	delete shell;
	return ok ? 0 : 1;
}
