#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/patch_editor_dialog.h"
#include "app/studio_theme.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
class ExpandedLabels final : public QTranslator {
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "PatchEditorDialog" && !QByteArray(context).contains("PatchControlView")) {
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace

int main(int argc, char** argv)
{
	// Direct widget actions and QWidget render only; no input injection or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	bool ok = true;
	QString error;
	LevelMapPatch patch;
	LevelPatchCreateRequest request;
	request.columns = 5;
	ok &= expect(createLevelPatch(request, &patch, &error), "create test patch", error);
	for (int scale : {100, 200}) {
		StudioSettings settings;
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.setReducedMotion(true);
		std::cerr << "Patch UI scale " << scale << '\n';
		ExpandedLabels translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		applyStudioTheme(app,
		                 studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		PatchEditorDialog dialog(patch, true, 8);
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1500, 1000);
		}
		dialog.show();
		app.processEvents();
		auto* view = dialog.findChild<PatchControlView*>();
		auto* points = dialog.findChild<QTableWidget*>(QStringLiteral("patchPoints"));
		auto* preview = dialog.findChild<ModelViewport*>(QStringLiteral("patchSurfacePreview"));
		auto* shape = dialog.findChild<QComboBox*>(QStringLiteral("patchShape"));
		auto* generate = dialog.findChild<QPushButton*>(QStringLiteral("patchGenerate"));
		if (!expect(view && points && preview && shape && generate, "editor controls are discoverable")) {
			return EXIT_FAILURE;
		}
		view->framePatch();
		ok &= expect(view->focusPolicy() == Qt::StrongFocus && !view->accessibleName().isEmpty() &&
		                 QAccessible::queryAccessibleInterface(view)->role() == QAccessible::Graphic,
		             "custom view exposes focus and graphical role");
		ok &= expect(points->rowCount() == 15 && points->columnCount() == 5 && !points->accessibleDescription().isEmpty(),
		             "numeric table exposes every point and UV coordinate");
		ok &= expect(view->pointAt(view->pointPosition(7)) == 7, "control picking follows projected geometry");
		dialog.selectPoints({6, 7});
		ok &= expect(points->selectionModel()->selectedRows().size() == 2 && view->selectedPoints().size() == 2,
		             "table and canvas selection stay synchronized");
		ok &= expect(dialog.moveSelected({0, 0, 24, true}) && dialog.patch().controlPoints.at(6).z == 24 &&
		                 dialog.patch().controlPoints.at(7).z == 24,
		             "multi-point movement changes selected points only");
		dialog.undo();
		ok &= expect(dialog.patch().controlPoints.at(7).z == 0, "local undo restores point positions");
		dialog.redo();
		ok &= expect(dialog.patch().controlPoints.at(7).z == 24, "local redo restores edit");
		points->item(7, 3)->setText(QStringLiteral("2.5"));
		ok &= expect(dialog.patch().controlU.at(7) == 2.5, "table edits patch UVs");
		points->item(7, 0)->setText(QStringLiteral("nan"));
		ok &= expect(dialog.patch().controlPoints.at(7).x == 0 &&
		                 dialog.findChild<QLabel*>(QStringLiteral("patchStatus"))->text().contains(QStringLiteral("finite")),
		             "bad numeric input is rejected visibly");
		auto* split = dialog.findChild<QAction*>(QStringLiteral("patchSplitColumns"));
		split->trigger();
		ok &= expect(dialog.patch().width == 9 && view->selectedPoints().isEmpty(), "topology edits clear ambiguous component IDs");
		dialog.undo();
		shape->setCurrentIndex(shape->findData(QStringLiteral("cylinder")));
		generate->click();
		ok &= expect(dialog.patch().width == 9 && dialog.patch().height == 3 && preview->hasMesh() && preview->hasSkin(),
		             "shape controls regenerate live checker preview");
		auto* material = dialog.findChild<QComboBox*>(QStringLiteral("patchTexture"));
		material->setEditText(QStringLiteral("textures/textures/studio/nested"));
		QMetaObject::invokeMethod(material->lineEdit(), "editingFinished", Qt::DirectConnection);
		generate->click();
		ok &= expect(dialog.patch().textureName == QStringLiteral("textures/studio/nested"),
		             "regenerating a shape must not remove the material prefix twice");
		material->setEditText(QStringLiteral("textures/studio/checker"));
		QMetaObject::invokeMethod(material->lineEdit(), "editingFinished", Qt::DirectConnection);
		auto* projection = dialog.findChild<QComboBox*>(QStringLiteral("patchViewPlane"));
		projection->setCurrentIndex(projection->findData(QStringLiteral("xz")));
		view->framePatch();
		const QString captureRoot = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captureRoot.isEmpty()) {
			QDir().mkpath(captureRoot);
			auto* tabs = dialog.findChild<QTabWidget*>();
			auto* scroll = dialog.findChild<QScrollArea*>();
			for (int page = 0; page < 2; ++page) {
				tabs->setCurrentIndex(page);
				app.processEvents();
				for (int bottom = 0; bottom < 2; ++bottom) {
					scroll->verticalScrollBar()->setValue(bottom ? scroll->verticalScrollBar()->maximum() : 0);
					app.processEvents();
					QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
					image.fill(Qt::transparent);
					dialog.render(&image);
					ok &= expect(
					    image.save(
					        QDir(captureRoot).filePath(QStringLiteral("patch-editor-%1-tab%2-%3.png").arg(scale).arg(page).arg(bottom))),
					    "widget render written");
				}
			}
		}
		dialog.findChild<QPushButton*>(QStringLiteral("patchApply"))->click();
		ok &= expect(dialog.result() == QDialog::Accepted, "valid edited patch is reviewable and accepted");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ApplicationShell shell;
	std::cerr << "Patch shell integration" << '\n';
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	ok &= expect(shell.createLevelDocument(create, &error) && shell.applyLevelPatch(patch, -1, &error),
	             "actual shell creates and adopts patch", error);
	ok &= expect(shell.findChild<QAction*>(QStringLiteral("map.addPatch"))->isEnabled() &&
	                 shell.findChild<QAction*>(QStringLiteral("map.editPatch"))->isEnabled(),
	             "commands reflect compatible map and patch selection");
	auto updated = shell.levelDocument().patches.first();
	updated.controlPoints[7].z += 48;
	ok &= expect(shell.applyLevelPatch(updated, 0, &error) && shell.levelDocument().undoStack.size() == 2,
	             "shell commits component edit as one map undo step", error);
	const QString saved = QDir(temp.path()).filePath(QStringLiteral("patch.map"));
	ok &= expect(shell.saveLevelDocument(saved, false, &error), "actual shell saves authored patch", error);
	LevelMapDocument reopened;
	ok &= expect(loadLevelMap({saved, {}, {}}, &reopened, &error) && reopened.patches.size() == 1 &&
	                 reopened.patches.first().controlPoints.at(7).z == 48,
	             "shell save round trip retains edited geometry", error);
	ok &= expect(shell.close(), "saved shell closes without unsaved prompt");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
