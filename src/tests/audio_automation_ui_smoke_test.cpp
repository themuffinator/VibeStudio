#include "app/audio_automation_editor.h"
#include "app/audio_effects_dialog.h"
#include "app/studio_theme.h"
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
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
	bool ok = true;
	AudioAutomationEditor editor("Gain", -96, 24, 0, 120, 48000);
	QVector<AudioAutomationPoint> points{
	    {0, -12.123456789123, AudioAutomationCurve::Smooth}, {24000, 0, AudioAutomationCurve::Step}, {48000, -6}};
	ok &= expect(editor.setPoints(points) && !editor.addPoint(0, 12) && editor.points() == points,
	             "duplicate addition selects existing point without changing its value");
	ok &= expect(!editor.setPoint(1, {0, 100}) && editor.points() == points,
	             "invalid point edit leaves the model unchanged");
	editor.selectPoint(0);
	editor.findChild<QDoubleSpinBox *>("automationFrame")->setValue(100);
	ok &= expect(editor.points()[0].frame == 100 && editor.points()[0].value == points[0].value,
	             "changing frame preserves original value precision");
	editor.findChild<QComboBox *>("automationCurve")->setCurrentIndex(1);
	ok &=
	    expect(editor.points()[0].curve == AudioAutomationCurve::Step, "native curve selector edits the shared model");
	editor.findChild<QPushButton *>("automationAdd")->click();
	ok &= expect(editor.points().size() == 4 && editor.points()[1].frame == 120,
	             "add at cursor inserts ordered point using the evaluated curve");
	editor.findChild<QPushButton *>("automationRemove")->click();
	ok &= expect(editor.points().size() == 3, "remove selected point");
	AudioSession session;
	session.masterEffects = {makeAudioEffect("reverb", 48000)};
	AudioEffectsDialog effects(session, {});
	effects.setAutomationCursor(12000);
	QTimer::singleShot(0, &effects, [&] {
		auto *dialog = effects.findChild<QDialog *>("effectAutomationDialog");
		if (!dialog) {
			ok = false;
			if (auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				active->reject();
			return;
		}
		auto *parameter = dialog->findChild<QComboBox *>("automationParameter");
		parameter->setCurrentIndex(parameter->findData("mix"));
		dialog->findChild<AudioAutomationEditor *>()->setPoints({{0, .1, AudioAutomationCurve::Smooth}, {24000, .7}});
		dialog->findChild<QCheckBox *>("automationEnabled")->setChecked(false);
		parameter->setCurrentIndex(parameter->findData("roomSize"));
		dialog->findChild<AudioAutomationEditor *>()->setPoints({{0, .25}, {48000, 2}});
		parameter->setCurrentIndex(parameter->findData("mix"));
		ok &=
		    expect(!dialog->findChild<QCheckBox *>("automationEnabled")->isChecked() &&
		               dialog->findChild<AudioAutomationEditor *>()->points()[0].curve == AudioAutomationCurve::Smooth,
		           "parameter switching preserves curve and read/off state");
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	effects.editAutomation();
	const auto change = effects.edit();
	ok &= expect(change.effectAutomation && change.effectAutomation->size() == 2 &&
	                 session.masterEffectAutomation.isEmpty() && editAudioSession(session, change).succeeded(),
	             "effect curves remain staged until the outer effect edit is applied");
	QTimer::singleShot(0, &effects, [&] {
		auto *dialog = effects.findChild<QDialog *>("effectAutomationDialog");
		dialog->findChild<AudioAutomationEditor *>()->setPoints({});
		dialog->reject();
	});
	effects.editAutomation();
	ok &= expect(effects.edit().effectAutomation == change.effectAutomation, "cancel discards nested curve changes");
	ok &= expect(effects.applyPreset(makeAudioEffectPreset("dialogue-clean", 48000)) &&
	                 effects.edit().effectAutomation->isEmpty(),
	             "preset replacement clears old effect targets");
	const auto renderRoot = qEnvironmentVariable("VIBESTUDIO_AUDIO_AUTOMATION_RENDERS");
	if (!renderRoot.isEmpty())
		ok &= QDir().mkpath(renderRoot);
	struct Layout {
		const char *name;
		StudioTheme theme;
		int scale;
		bool rtl;
	};
	for (const auto &layout : {Layout{"dark-100", StudioTheme::Dark, 100, false},
	                           Layout{"high-visibility-125", StudioTheme::HighContrastLight, 125, false},
	                           Layout{"expanded-rtl-200", StudioTheme::HighContrastDark, 200, true}}) {
		Expanded expanded;
		if (layout.rtl)
			app.installTranslator(&expanded);
		applyStudioTheme(app, studioThemeTokens(layout.theme, UiDensity::Comfortable, layout.scale));
		QScrollArea scroll;
		scroll.setWidgetResizable(true);
		scroll.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		auto *widget = new AudioAutomationEditor(
		    QCoreApplication::translate("AudioAutomationEditor", "Gain offset (dB)"), -96, 24, 0, 12000, 48000);
		widget->setPoints(points);
		scroll.setWidget(widget);
		scroll.resize(900, 850);
		scroll.show();
		QCoreApplication::processEvents();
		for (auto *spin : widget->findChildren<QDoubleSpinBox *>())
			ok &= expect(!spin->accessibleName().isEmpty() && spin->focusPolicy() != Qt::NoFocus &&
			                 QAccessible::queryAccessibleInterface(spin),
			             "point fields expose native accessibility and focus");
		ok &= expect(QAccessible::queryAccessibleInterface(widget->findChild<QTableWidget *>("automationPoints")),
		             "point table exposes native row selection");
		const auto capture = [&](const QString &suffix) {
			if (renderRoot.isEmpty())
				return;
			QImage image(scroll.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			scroll.render(&image);
			ok &= image.save(QDir(renderRoot).filePath(QString::fromLatin1(layout.name) + suffix + ".png"));
		};
		capture("");
		scroll.resize(640, 420);
		QCoreApplication::processEvents();
		if (scroll.horizontalScrollBar()->maximum())
			std::cerr << layout.name << " horizontal=" << scroll.horizontalScrollBar()->maximum()
			          << " minimum=" << widget->minimumSizeHint().width() << '\n';
		ok &= expect(scroll.width() == 640 && scroll.horizontalScrollBar()->maximum() == 0,
		             "small expanded curve editor fits without horizontal scrolling");
		scroll.ensureWidgetVisible(widget->findChild<QComboBox *>("automationCurve"));
		QCoreApplication::processEvents();
		capture("-small");
		for (const auto *name : {"automationFrame", "automationValue", "automationCurve", "automationAdd",
		                         "automationRemove", "automationClear"}) {
			auto *control = widget->findChild<QWidget *>(name);
			const int top = control->mapTo(widget, QPoint()).y();
			scroll.verticalScrollBar()->setValue(top - (scroll.viewport()->height() - control->height()) / 2);
			QCoreApplication::processEvents();
			ok &= expect(
			    scroll.viewport()->rect().contains(QRect(control->mapTo(scroll.viewport(), QPoint()), control->size())),
			    "every numeric and point action control is completely reachable in the small editor");
		}
		capture("-small-actions");
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	return ok ? 0 : 1;
}
