#include "app/model_editor_dialog.h"
#include "app/model_rig_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_rig_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
		if (QByteArray(context) != "ModelRigDialog")
			return {};
		const auto original = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(original, QString(original.size() / 2, '~'));
	}
};
bool render(QWidget &widget, const QString &name)
{
	const auto folder = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (folder.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(folder).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
} // namespace
int main(int argc, char **argv)
{
	// Owned QObject methods and QWidget render targets only; no OS input/capture.
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
	QTemporaryDir temporary(QDir(root).filePath("rig-ui-XXXXXX"));
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	const auto mesh = rigFixture();
	ModelSelection selection;
	selection.vertices = {1};
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		ModelRigDialog dialog(mesh, selection, "tip");
		dialog.setLayoutDirection(scenario == 2 ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.show();
		QCoreApplication::processEvents();
		auto *operation = dialog.findChild<QComboBox *>("rigOperation");
		auto *joint = dialog.findChild<QComboBox *>("rigJoint");
		auto *parent = dialog.findChild<QComboBox *>("rigParent");
		auto *name = dialog.findChild<QLineEdit *>("rigName");
		auto *weight = dialog.findChild<QDoubleSpinBox *>("rigWeight");
		auto *apply = dialog.findChild<QPushButton *>("applyRigEdit");
		if (!expect(operation && joint && parent && name && weight && apply, "rig controls present"))
			return 1;
		for (QWidget *widget : QList<QWidget *>{operation, joint, parent, name, weight, apply})
		{
			const auto *accessible = QAccessible::queryAccessibleInterface(widget);
			expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !accessible->text(QAccessible::Description).isEmpty(),
				   "rig controls expose accessible names and descriptions");
			expect(widget->focusPolicy() != Qt::NoFocus, "rig controls accept keyboard focus");
		}
		expect(!apply->isEnabled(), "incomplete joint name cannot be applied");
		name->setText("child");
		expect(apply->isEnabled(), "entering a joint name enables the authoring action");
		parent->setCurrentIndex(parent->findData("tip"));
		dialog.findChild<QDoubleSpinBox *>("rigOrigin0")->setValue(9);
		auto edit = dialog.edit();
		expect(edit.kind == ModelEditKind::AddJoint && edit.rig.parent == "tip" && edit.rig.origin.x == 9,
			   "add controls produce shared joint operation");
		expect(render(dialog, QStringLiteral("rig-add-%1").arg(scenario)), "render add controls");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::SetJointWeight)));
		weight->setValue(0.25);
		QCoreApplication::processEvents();
		edit = dialog.edit();
		expect(edit.rig.joint == "tip" && edit.rig.weight == 0.25 && edit.selection == selection && edit.rig.parent.isEmpty(),
			   "weight controls retain selection and omit inactive options");
		expect(name->isHidden() && parent->isHidden() && !weight->isHidden(), "irrelevant operation controls hidden");
		expect(dialog.findChild<QScrollArea *>()->horizontalScrollBar()->maximum() == 0,
			   "no horizontal overflow at expanded 200 percent text");
		expect(render(dialog, QStringLiteral("rig-weight-%1").arg(scenario)), "render weight controls");
		apply->click();
		expect(dialog.result() == QDialog::Accepted, "apply accepts reviewed operation");
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	// Exercise the actual editor button, modal review and worker adoption.
	if (const int unavailable = test_support::exitCodeWithoutRenderer("model-rig-ui-smoke"); unavailable >= 0)
		return unavailable;
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelEditorDialog editor;
	QString error;
	if (!expect(editor.setMesh(mesh, &error), "editor admits rig fixture"))
		return 1;
	bool opened = false;
	QTimer::singleShot(0, [&] {
		auto *dialog = dynamic_cast<ModelRigDialog *>(QApplication::activeModalWidget());
		if (!dialog)
		{
			if (auto *modal = dynamic_cast<QDialog *>(QApplication::activeModalWidget()))
				modal->reject();
			return;
		}
		opened = true;
		dialog->findChild<QLineEdit *>("rigName")->setText("unused");
		dialog->findChild<QPushButton *>("applyRigEdit")->click();
	});
	const auto *button = editor.findChild<QPushButton *>("editMeshRig");
	if (!expect(button, "editor skeleton action present"))
		return 1;
	const_cast<QPushButton *>(button)->click();
	expect(opened && editor.document().mesh().skeleton.joints.size() == 3 && editor.document().canUndo(),
		   "editor commits reviewed rig through undoable worker");
	ModelEdit edit;
	edit.kind = ModelEditKind::SetJointWeight;
	edit.rig.joint = "tip";
	edit.rig.weight = 0.5;
	edit.selection = selection;
	expect(editor.applyEdit(edit, &error) && std::abs(editor.document().mesh().surfaces[0].frames[2].positions[1].y - 2) < 0.0001,
		   "editor worker updates deformation for half weight");
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
