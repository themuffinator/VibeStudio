#include "app/model_editor_dialog.h"
#include "app/model_uv_view.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_uv_islands_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
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
		if (QByteArray(context) != "VibeStudioModelEditor" && QByteArray(context) != "ModelUvTransform")
			return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
void reveal(QWidget *control)
{
	for (auto *parent = control->parentWidget(); parent; parent = parent->parentWidget())
		if (auto *scroll = qobject_cast<QScrollArea *>(parent))
			scroll->ensureWidgetVisible(control);
}
bool render(QWidget &widget, const QString &name)
{
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (evidence.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(evidence).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
} // namespace
int main(int argc, char **argv)
{
	// Test-owned Qt state and render targets; no OS input or screen capture.
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
	QTemporaryDir temporary(QDir(root).filePath("uv-islands-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	const auto original = uvIslandsFixture();
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelEditorDialog editor;
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open island transform fixture");
		editor.resize(scenario == 0 ? 1400 : 2200, scenario == 0 ? 960 : 1480);
		editor.show();
		app.processEvents();
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
		auto *pivot = editor.findChild<QComboBox *>("meshUvPivotMode");
		auto *transform = editor.findChild<QPushButton *>("transformMeshUvs");
		auto *project = editor.findChild<QPushButton *>("projectMeshUvs");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		if (!expect(mode && table && uv && pivot && transform && project && undo && redo, "UV workflow controls exist"))
			return 1;
		for (auto *tabs : editor.findChildren<QTabWidget *>())
			for (int index = 0; index < tabs->count(); ++index)
				if (tabs->widget(index)->isAncestorOf(uv))
					tabs->setCurrentIndex(index);
		editor.showSidebarPage(QStringLiteral("surface"));
		ok &= expect(settleModelUv(*uv), "initial UV analysis and render finish");
		pivot->setCurrentIndex(int(ModelUvPivot::IndividualIslands));
		ok &= expect(pivot->count() == 4 && !editor.findChild<QDoubleSpinBox *>("meshUvPivot0")->isEnabled() &&
						 !editor.findChild<QDoubleSpinBox *>("meshUvPivot1")->isEnabled() && !pivot->accessibleDescription().isEmpty(),
					 "individual mode exposes help and disables irrelevant custom coordinates");
		uv->componentPicked(0, 0, -1, false);
		ok &= expect(settleModelUv(*uv), "first partial-face pick reaches the renderer before the next pick");
		uv->componentPicked(0, 2, -1, true);
		transform->click();
		ok &= expect(!editor.document().canUndo() && uvIslandBytes(editor.document().mesh()) == uvIslandBytes(original),
					 "partial charts are rejected without changes or history");
		editor.findChild<QAction *>("selectMeshUvIslands")->trigger();
		ok &= expect(editor.document().selection().faces == QSet<int>{0, 1, 2, 3} && table->selectionModel()->selectedRows().size() == 4 &&
						 !editor.document().canUndo(),
					 "Select Islands expands partial faces and retains table selection without an edit");
		const auto selected = editor.document().selection();
		ModelEdit edit;
		edit.kind = ModelEditKind::TransformUv;
		edit.selection = selected;
		edit.uvPivotMode = ModelUvPivot::IndividualIslands;
		edit.uvScale = {2, .5f};
		edit.uvRotation = 90;
		edit.uvOffset = {.0625f, -.0625f};
		edit.uvTranslationGrid = .125;
		auto expected = original;
		ok &= expect(applyModelEdit(&expected, edit, nullptr, &error), "prepare shared service result for GUI comparison");
		editor.findChild<QDoubleSpinBox *>("meshUvScale0")->setValue(2);
		editor.findChild<QDoubleSpinBox *>("meshUvScale1")->setValue(.5);
		editor.findChild<QDoubleSpinBox *>("meshUvRotation")->setValue(90);
		// Exact grid values here; the core/CLI fixture covers half-step ties.
		editor.findChild<QDoubleSpinBox *>("meshUvOffset0")->setValue(.125);
		editor.findChild<QDoubleSpinBox *>("meshUvOffset1")->setValue(-.125);
		editor.findChild<QCheckBox *>("meshUvSnap")->setChecked(true);
		reveal(pivot);
		pivot->setFocus(Qt::OtherFocusReason);
		app.processEvents();
		ok &= expect(pivot->hasFocus() && pivot->focusPolicy() != Qt::NoFocus &&
						 QAccessible::queryAccessibleInterface(pivot)->role() == QAccessible::ComboBox,
					 "pivot is named, keyboard focusable and exposes a native combo role");
		editor.findChild<QAction *>("frameMeshUvSelection")->trigger();
		ok &= expect(settleModelUv(*uv) && render(editor, QStringLiteral("uv-islands-selected-%1").arg(scenario)),
					 "render selected islands and pivot controls");
		bool busy = false, locked = false;
		QTimer::singleShot(0, &editor, [&] {
			busy = editor.operationBusy();
			locked = !transform->isEnabled() && !pivot->isEnabled();
		});
		transform->click();
		ok &= expect(busy && locked && !editor.operationBusy(), "worker exposes busy state and locks mutation controls until completion");
		ok &= expect(uvIslandBytes(editor.document().mesh()) == uvIslandBytes(expected) && editor.document().selection() == selected &&
						 settleModelUv(*uv) && uv->topology()->islands.size() == 3,
					 "numeric controls produce exact shared result and refresh UV analysis");
		editor.findChild<QAction *>("frameMeshUvSelection")->trigger();
		ok &= expect(settleModelUv(*uv) && render(editor, QStringLiteral("uv-islands-transformed-%1").arg(scenario)), "render independently transformed charts");
		undo->trigger();
		ok &= expect(uvIslandBytes(editor.document().mesh()) == uvIslandBytes(original) && editor.document().selection() == selected &&
						 !editor.document().canUndo(),
					 "one undo restores source and selection");
		redo->trigger();
		ok &= expect(uvIslandBytes(editor.document().mesh()) == uvIslandBytes(expected), "redo restores transformed islands");
		undo->trigger();
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.applyEdit(edit, &error) && !editor.document().canUndo() && editor.document().selection() == selected &&
						 uvIslandBytes(editor.document().mesh()) == uvIslandBytes(original),
					 "cancellation retains source, selection and history");
		editor.findChild<QComboBox *>("meshFrame")->setCurrentIndex(1);
		editor.findChild<QDoubleSpinBox *>("meshUvScale1")->setValue(2);
		editor.findChild<QDoubleSpinBox *>("meshUvRotation")->setValue(0);
		editor.findChild<QDoubleSpinBox *>("meshUvOffset0")->setValue(0);
		editor.findChild<QDoubleSpinBox *>("meshUvOffset1")->setValue(0);
		editor.findChild<QCheckBox *>("meshUvSnap")->setChecked(false);
		project->click();
		ok &= expect(uvNear(editor.document().mesh().surfaces[0].texCoords[0], {-8, 13}) &&
						 uvSamePoses(original.surfaces[0], editor.document().mesh().surfaces[0]),
					 "GUI projection uses displayed pose and preserves all geometry");
		undo->trigger();
		mode->setCurrentIndex(5);
		ok &= expect(!transform->isEnabled() && !project->isEnabled(), "whole-surface mode keeps component-only UV transforms disabled");
		mode->setCurrentIndex(0);
		ok &= expect(transform->isEnabled() && project->isEnabled(), "returning to components restores UV controls");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == QStringLiteral("2"))
		{
			ok &= expect(settleModelUv(*uv), "last mode change finishes before the high-DPI image check");
			const auto pixels = uv->pixmap();
			ok &= expect(editor.devicePixelRatioF() >= 2 && uv->devicePixelRatioF() >= 2 &&
				pixels.width() > uv->width() && pixels.height() > uv->height() &&
				qint64(pixels.width()) * pixels.height() <= 4 * 1024 * 1024,
				"actual 2x display retains high-DPI UV pixels within the renderer's allocation cap");
		}
		editor.hide();
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " UV island GUI checks\n";
	return ok ? 0 : 1;
}
