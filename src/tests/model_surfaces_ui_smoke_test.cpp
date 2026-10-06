#include "app/model_editor_dialog.h"
#include "app/model_surface_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_surfaces_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"
#include "app/model_uv_view.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <cmath>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
		std::cerr << "FAIL: " << message << '\n';
	return condition;
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
		if (QByteArray(context) != "ModelSurfaceDialog" && QByteArray(context) != "VibeStudioModelEditor")
			return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
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
bool settleMaterials(ModelEditorDialog &editor)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (editor.materialLoading() && elapsed.elapsed() < 20000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	auto *view = editor.findChild<ModelViewport *>("meshPreview");
	auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
	return !editor.materialLoading() && view && uv && settleModelViewport(*view) && settleModelUv(*uv);
}
int colouredPixels(const QImage &image, bool green)
{
	int count = 0;
	for (int y = 0; y < image.height(); y += 4)
		for (int x = 0; x < image.width(); x += 4)
		{
			const auto c = image.pixelColor(x, y);
			count += green ? c.green() > 80 && c.green() > 2 * c.red() && c.green() > 2 * c.blue()
						   : c.red() > 80 && c.red() > 2 * c.green() && c.red() > 2 * c.blue();
		}
	return count;
}
} // namespace
int main(int argc, char **argv)
{
	// Test-owned Qt widgets and render targets only; no physical input/capture.
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
	QTemporaryDir temporary(QDir(root).filePath("surface-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	const auto original = surfaceFixture();
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
		auto mismatch = original;
		mismatch.surfaces[1].skinPaths[1] = "models/alternate.tga";
		ModelSurfaceDialog review(mismatch, {0, {}, {1}});
		review.resize(scenario == 0 ? 760 : 1500, scenario == 0 ? 580 : 1160);
		review.show();
		app.processEvents();
		auto *operation = review.findChild<QComboBox *>("surfaceOperation");
		auto *name = review.findChild<QLineEdit *>("surfaceName");
		auto *target = review.findChild<QComboBox *>("surfaceTarget");
		auto *sources = review.findChild<QTreeWidget *>("surfaceJoinSources");
		auto *adopt = review.findChild<QCheckBox *>("surfaceAdoptMaterials");
		auto *apply = review.findChild<QPushButton *>("applySurfaceOperation");
		auto *summary = review.findChild<QLabel *>("surfaceSummary");
		if (!operation || !name || !target || !sources || !adopt || !apply || !summary)
			return 1;
		for (auto *widget : QList<QWidget *>{operation, name, target, sources, adopt, apply})
		{
			auto *access = QAccessible::queryAccessibleInterface(widget);
			ok &= expect(access && !access->text(QAccessible::Name).isEmpty() && !access->text(QAccessible::Description).isEmpty() &&
							 widget->focusPolicy() != Qt::NoFocus,
						 "surface controls expose accessible names, descriptions and keyboard focus");
		}
		name->setText("part1");
		ok &= expect(!apply->isEnabled(), "review prevents duplicate names before worker dispatch");
		name->setText("renamed");
		ok &= expect(apply->isEnabled() && review.edit().text == "renamed", "review returns the requested rename");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::MoveFacesToSurface)));
		target->setCurrentIndex(1);
		ok &= expect(!apply->isEnabled() && !adopt->isChecked(), "different alternate materials block a move by default");
		adopt->setChecked(true);
		ok &= expect(apply->isEnabled() && review.edit().adoptTargetMaterials && review.edit().targetSurface == 1,
					 "material adoption is explicit and included in reviewed move");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::JoinSurfaces)));
		target->setCurrentIndex(1);
		sources->topLevelItem(1)->setCheckState(0, Qt::Checked);
		ok &= expect(!adopt->isChecked() && !apply->isEnabled(), "changing operation resets material replacement consent");
		adopt->setChecked(true);
		ok &= expect(apply->isEnabled() && review.edit().surfaces == QSet<int>{0, 1}, "join checklist and target reach the common service");
		sources->setFocus(Qt::OtherFocusReason);
		app.processEvents();
		for (int column = 0; column < 2; ++column)
			ok &= expect(sources->columnWidth(column) >=
							 sources->header()->fontMetrics().horizontalAdvance(sources->headerItem()->text(column)),
						 "surface identities and triangle headings remain readable at expanded text sizes");
		ok &= expect(sources->columnWidth(0) >= sources->fontMetrics().horizontalAdvance(sources->topLevelItem(0)->text(0)) + 16,
					 "surface names fit beside their checkboxes at 200 percent text");
		for (auto *widget : QList<QWidget *>{operation, target, sources, adopt, summary, apply})
		{
			const QRect bounds(widget->mapTo(&review, QPoint()), widget->size());
			ok &= expect(review.rect().contains(bounds), "expanded RTL/high-contrast controls fit review bounds");
		}
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
			ok &= expect(std::abs(review.devicePixelRatioF() - 2) < .01, "surface review uses actual 2x rendering");
		ok &= expect(render(review, QStringLiteral("surface-review-%1").arg(scenario)), "save surface review widget render");
		review.hide();
		ModelEditorDialog editor;
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open shared editor fixture");
		editor.resize(scenario == 0 ? 1320 : 2080, scenario == 0 ? 920 : 1360);
		editor.show();
		app.processEvents();
		auto *manage = editor.findChild<QPushButton *>("manageMeshSurfaces");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *surface = editor.findChild<QComboBox *>("meshSurface");
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *scope = editor.findChild<QComboBox *>("meshFrameScope");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		if (!manage || !table || !surface || !mode || !scope || !undo || !redo || !preview)
			return 1;
		surface->setCurrentIndex(1);
		mode->setCurrentIndex(0);
		scope->setCurrentIndex(1);
		table->selectionModel()->select(table->model()->index(1, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		bool sawDialog = false, responsive = false, locked = false;
		QTimer heartbeat;
		heartbeat.setInterval(0);
		QObject::connect(&heartbeat, &QTimer::timeout, &editor, [&] {
			if (editor.operationBusy())
			{
				responsive = true;
				locked = !manage->isEnabled();
			}
		});
		QTimer::singleShot(0, &editor, [&] {
			auto *dialog = dynamic_cast<ModelSurfaceDialog *>(QApplication::activeModalWidget());
			if (!dialog)
			{
				if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
					modal->reject();
				return;
			}
			sawDialog = true;
			auto *choice = dialog->findChild<QComboBox *>("surfaceOperation");
			choice->setCurrentIndex(choice->findData(int(ModelEditKind::SeparateFaces)));
			dialog->findChild<QLineEdit *>("surfaceName")->setText("panel");
			dialog->findChild<QPushButton *>("applySurfaceOperation")->click();
		});
		heartbeat.start();
		manage->click();
		heartbeat.stop();
		ok &= expect(sawDialog && responsive && locked && !editor.operationBusy(),
					 "manager review closes before cancellable worker locks editor mutation controls");
		ok &= expect(editor.document().mesh().surfaces.size() == 4 && editor.document().selection() == ModelSelection{3, {}, {0}} &&
						 surface->currentIndex() == 3 && table->selectionModel()->selectedRows().size() == 1 &&
						 exactFace(original.surfaces[1], 1, editor.document().mesh().surfaces[3], 0),
					 "UI separates all poses despite current-frame transform scope and selects the new surface");
		ok &= expect(settleModelViewport(*preview), "surface partition reaches rendered viewport");
		ok &= expect(render(editor, QStringLiteral("surface-editor-%1").arg(scenario)), "save edited surface widget render");
		const auto edited = surfaceBytes(editor.document().mesh());
		undo->trigger();
		ok &= expect(surfaceBytes(editor.document().mesh()) == surfaceBytes(original) && surface->currentIndex() == 1,
					 "UI undo restores original surface order and selection");
		redo->trigger();
		ok &=
			expect(surfaceBytes(editor.document().mesh()) == edited && surface->currentIndex() == 3, "UI redo restores resulting surface");
		ModelEdit uv;
		uv.kind = ModelEditKind::ProjectUv;
		uv.selection = editor.document().selection();
		ok &= expect(editor.applyEdit(uv, &error) && editor.document().selection().surface == 3,
					 "new face selection hands directly to UV authoring");
		ok &= expect(editor.setMesh(original, &error), "reset source after history workflow");
		ModelEdit cancelled;
		cancelled.kind = ModelEditKind::JoinSurfaces;
		cancelled.targetSurface = 1;
		cancelled.surfaces = {0, 1, 2};
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.applyEdit(cancelled, &error) && surfaceBytes(editor.document().mesh()) == surfaceBytes(original) &&
						 !editor.document().canUndo(),
					 "cancelled surface assembly does not mutate geometry or history");
		if (scenario == 0)
		{
			auto colours = original;
			auto reader = std::make_shared<SkinReader>();
			for (int i = 0; i < 3; ++i)
			{
				QImage image(32, 32, QImage::Format_RGB32);
				image.fill(i == 0 ? Qt::red : i == 1 ? Qt::green : Qt::blue);
				const auto path = QStringLiteral("models/colour%1.png").arg(i);
				reader->add(path, skinPng(image));
				colours.surfaces[i].skinPaths = {path};
			}
			updateEditableModelMetadata(&colours);
			ok &= expect(editor.setMesh(colours, &error), "open distinct material fixture");
			editor.setMaterialSource({reader, "surface-colours", {}});
			editor.findChild<QComboBox *>("meshRenderMode")->setCurrentIndex(3);
			auto *uvView = editor.findChild<ModelUvView *>("meshUvPreview");
			ok &= expect(settleMaterials(editor) && colouredPixels(uvView->pixmap().toImage(), false) > 100,
						 "initial source resolves red material pixels");
			ModelEdit remove;
			remove.kind = ModelEditKind::DeleteSurface;
			ok &= expect(editor.applyEdit(remove, &error) && settleMaterials(editor) && editor.document().selection().surface == 0 &&
							 colouredPixels(uvView->pixmap().toImage(), true) > 100 && !reader->readOnGui,
						 "deletion reloads UV material pixels for renumbered surfaces on the worker");
			undo->trigger();
			ok &= expect(settleMaterials(editor) && colouredPixels(uvView->pixmap().toImage(), false) > 100,
						 "undo resolves restored surface material identities");
			ok &= expect(editor.setMesh(original, &error), "retire material fixture recovery");
			editor.setMaterialSource({});
		}
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	return ok ? 0 : 1;
}
