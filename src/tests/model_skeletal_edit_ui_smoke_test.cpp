#include "app/model_editor_dialog.h"
#include "app/model_skeletal_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_rig_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return value;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "ModelSkeletalDialog")
			return {};
		const auto original = QString::fromUtf8(text);
		// Breakable padding simulates expansion without introducing a synthetic
		// unbreakable word longer than the viewport.
		return QStringLiteral("[%1 %2]").arg(original, QString("~~ ").repeated(original.size() / 6));
	}
};
bool render(QWidget &widget, const QString &name)
{
	const auto folder = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (folder.isEmpty())
		return true;
	QImage target(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	target.setDevicePixelRatio(widget.devicePixelRatioF());
	target.fill(Qt::transparent);
	widget.render(&target);
	return target.save(QDir(folder).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
} // namespace
int main(int argc, char **argv)
{
	// Only owned QObject/widget APIs and render targets. No system input/capture.
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
	QTemporaryDir temporary(QDir(root).filePath("skeletal-ui-XXXXXX"));
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	const auto mesh = rigFixture();
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		ModelSelection selected;
		selected.vertices = {1};
		ModelSkeletalDialog dialog(mesh, selected, "tip", 2);
		dialog.setLayoutDirection(scenario == 2 ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.resize(760, 680);
		dialog.show();
		QCoreApplication::processEvents();
		auto *operation = dialog.findChild<QComboBox *>("skeletalOperation");
		auto *apply = dialog.findChild<QPushButton *>("applySkeletalEdit");
		auto *name = dialog.findChild<QLineEdit *>("skeletalName");
		auto *scroll = dialog.findChild<QScrollArea *>();
		expect(operation && apply && name && scroll, "dialog exposes authoring controls");
		QStyleOptionComboBox option;
		option.initFrom(operation);
		const auto textRect = operation->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, operation);
		expect(textRect.width() >= operation->fontMetrics().horizontalAdvance(operation->currentText()),
			   "expanded operation name remains fully visible");
		expect(dialog.findChild<QComboBox *>("skeletalClip")->layoutDirection() == Qt::LeftToRight &&
				   dialog.findChild<QComboBox *>("skeletalSourceClip")->layoutDirection() == Qt::LeftToRight &&
				   dialog.findChild<QDoubleSpinBox *>("skeletalRate")->layoutDirection() == Qt::LeftToRight,
			   "technical clip indices and timing retain left-to-right order");
		auto edit = dialog.edit();
		expect(edit.kind == ModelEditKind::TransformSkeletalPose && edit.skeletal.clip == 0 && edit.skeletal.first == 1 &&
				   edit.skeletal.last == 1 && edit.skeletal.joint == "tip" && edit.selection == selected,
			   "dialog starts on current joint and clip-relative frame");
		for (const auto &id :
			 {"skeletalOperation", "skeletalClip", "skeletalJoint", "skeletalFirst", "skeletalLast", "skeletalSpace", "skeletalChildren",
			  "skeletalVector0Axis0", "skeletalVector1Axis2", "skeletalVector2Axis1", "applySkeletalEdit"})
		{
			auto *widget = dialog.findChild<QWidget *>(id);
			auto *interface = widget ? QAccessible::queryAccessibleInterface(widget) : nullptr;
			expect(widget && interface && !widget->accessibleName().isEmpty() && !widget->accessibleDescription().isEmpty() &&
					   (widget->focusPolicy() & Qt::TabFocus),
				   "authoring control has keyboard focus and accessibility metadata");
		}
		dialog.findChild<QDoubleSpinBox *>("skeletalVector0Axis1")->setValue(3);
		dialog.findChild<QComboBox *>("skeletalSpace")->setCurrentIndex(1);
		dialog.findChild<QCheckBox *>("skeletalChildren")->setChecked(false);
		edit = dialog.edit();
		expect(edit.skeletal.local && !edit.skeletal.descendants && edit.skeletal.translation.y == 3, "dialog maps transform options");
		expect(scroll->horizontalScrollBar()->maximum() == 0, "transform form has no horizontal overflow");
		expect(render(dialog, QStringLiteral("skeletal-transform-%1").arg(scenario)), "render transform form");
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(render(dialog, QStringLiteral("skeletal-transform-bottom-%1").arg(scenario)), "render transform controls below fold");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::AddSkeletalClip)));
		QCoreApplication::processEvents();
		expect(!apply->isEnabled(), "new clip needs a name");
		name->setText("idle");
		dialog.findChild<QSpinBox *>("skeletalCount")->setValue(3);
		dialog.findChild<QDoubleSpinBox *>("skeletalRate")->setValue(20);
		dialog.findChild<QCheckBox *>("skeletalLoop")->setChecked(false);
		edit = dialog.edit();
		expect(apply->isEnabled() && edit.skeletal.name == "idle" && edit.skeletal.frameCount == 3 && edit.skeletal.rate == 20 &&
				   !edit.skeletal.loops && edit.skeletal.joint.isEmpty() && !edit.skeletal.local && edit.skeletal.translation.y == 0,
			   "new clip carries only applicable fields");
		scroll->verticalScrollBar()->setValue(0);
		QCoreApplication::processEvents();
		expect(scroll->horizontalScrollBar()->maximum() == 0, "clip form fits translated width");
		expect(render(dialog, QStringLiteral("skeletal-add-%1").arg(scenario)), "render clip form");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::CopySkeletalPose)));
		dialog.findChild<QComboBox *>("skeletalSourceClip")->setCurrentIndex(1);
		dialog.findChild<QSpinBox *>("skeletalSourceFrame")->setValue(1);
		edit = dialog.edit();
		expect(edit.skeletal.sourceClip == 0 && edit.skeletal.sourceFrame == 1 && edit.skeletal.name.isEmpty() &&
				   edit.skeletal.rate == 24 && edit.skeletal.loops,
			   "copy maps source without unrelated settings");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::TransformBindJoint)));
		edit = dialog.edit();
		expect(edit.skeletal.clip == -1 && edit.skeletal.first == 0 && edit.skeletal.last == 0 && edit.skeletal.sourceClip == -1,
			   "bind operation is independent of clip scope");
		dialog.hide();
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	auto staticMesh = mesh;
	staticMesh.skeleton = {};
	ModelSkeletalDialog noSkeleton(staticMesh, {}, {}, 0);
	expect(!noSkeleton.findChild<QPushButton *>("applySkeletalEdit")->isEnabled() &&
			   noSkeleton.findChild<QLabel *>("skeletalSummary")->text().contains("first joint"),
		   "missing skeleton explains the required next action");
	if (const int unavailable = test_support::exitCodeWithoutRenderer("model-skeletal-edit-ui-smoke"); unavailable >= 0)
		return unavailable;
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelEditorDialog editor;
	QString error;
	if (!expect(editor.setMesh(mesh, &error), "editor admits fixture"))
		return 1;
	bool opened = false;
	QTimer::singleShot(0,
					   [&]
					   {
						   auto *dialog = dynamic_cast<ModelSkeletalDialog *>(QApplication::activeModalWidget());
						   if (!dialog)
						   {
							   if (auto *modal = dynamic_cast<QDialog *>(QApplication::activeModalWidget()))
								   modal->reject();
							   return;
						   }
						   opened = true;
						   auto *operation = dialog->findChild<QComboBox *>("skeletalOperation");
						   operation->setCurrentIndex(operation->findData(int(ModelEditKind::AddSkeletalClip)));
						   dialog->findChild<QLineEdit *>("skeletalName")->setText("authored");
						   dialog->findChild<QSpinBox *>("skeletalCount")->setValue(3);
						   dialog->findChild<QPushButton *>("applySkeletalEdit")->click();
					   });
	auto *button = editor.findChild<QPushButton *>("editMeshSkeletalAnimation");
	if (!expect(button, "editor skeleton animation action present"))
		return 1;
	button->click();
	expect(opened && editor.document().mesh().skeleton.clips.size() == 2 && editor.document().mesh().frames.size() == 6 &&
			   editor.document().canUndo(),
		   "editor commits reviewed clip through worker and history");
	ModelEdit edit;
	edit.kind = ModelEditKind::TransformSkeletalPose;
	edit.skeletal.clip = 1;
	edit.skeletal.joint = "root";
	edit.skeletal.first = edit.skeletal.last = 2;
	edit.skeletal.rotation = {0, 0, 90};
	expect(editor.applyEdit(edit, &error), "editor worker authors pose");
	edit = {};
	edit.kind = ModelEditKind::InterpolateSkeletalPoses;
	edit.skeletal.clip = 1;
	edit.skeletal.last = 2;
	expect(editor.applyEdit(edit, &error) &&
			   std::abs(editor.document().mesh().surfaces[0].frames[4].positions[1].x - std::sqrt(32.0)) < .001,
		   "editor worker creates rotational animation");
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
