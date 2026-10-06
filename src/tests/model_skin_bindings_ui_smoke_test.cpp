#include "app/model_editor_dialog.h"
#include "app/model_skin_source_dialog.h"
#include "app/model_viewport.h"
#include "app/model_uv_view.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_skin_binding_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray source(const ModelEditorDialog &editor)
{
	return QJsonDocument(editableModelJson(editor.document().mesh())).toJson(QJsonDocument::Compact);
}
void select(QTableView *table, int row)
{
	table->selectionModel()->setCurrentIndex(table->model()->index(row, 0),
											 QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
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
		if (QByteArray(context) != "VibeStudioModelEditor" && QByteArray(context) != "VibeStudioModelSkinSource")
		{
			return {};
		}
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool settle(ModelEditorDialog &editor)
{
	QElapsedTimer timer;
	timer.start();
	while (editor.materialLoading() && timer.elapsed() < 20000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	auto *view = editor.findChild<ModelViewport *>("meshPreview");
	auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
	return !editor.materialLoading() && view && uv && tests::settleModelViewport(*view) && tests::settleModelUv(*uv);
}
bool capture(QWidget &widget, const QString &name)
{
	const auto folder = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (folder.isEmpty())
	{
		return true;
	}
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image); // Widget-owned render target; no OS capture/input.
	return image.save(QDir(folder).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
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
	{
		return 1;
	}
	QTemporaryDir temp(QDir(root).filePath("skin-bindings-ui-XXXXXX"));
	if (!temp.isValid())
	{
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	auto reader = std::make_shared<tests::SkinReader>();
	reader->add("models/default.skin", "body,models/first\nhead,models/new_head\n");
	reader->add("models/default.skin", tests::skinBindings());
	QImage colour(32, 32, QImage::Format_RGB32);
	colour.fill(Qt::blue);
	reader->add("models/new_body.png", tests::skinPng(colour));
	reader->add("models/new_head.png", tests::skinPng(colour));
	const auto mesh = tests::skinBindingMesh();
	bool ok = true;
	QString error;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelEditorDialog editor;
		ok &= expect(editor.setMesh(mesh, &error), "prepare editable binding fixture");
		editor.setAccessibility(scenario != 0, true);
		editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.resize(scenario ? 1900 : 1300, scenario ? 1250 : 950);
		editor.show();
		app.processEvents();
		auto *file = editor.findChild<QPushButton *>("applyMeshSkinFile");
		auto *package = editor.findChild<QPushButton *>("applyMeshPackageSkin");
		auto *details = editor.findChild<QPushButton *>("meshSkinBindingDetails");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		if (!expect(file && package && details && undo && redo && tabs, "surface binding controls exist"))
		{
			return 1;
		}
		ok &= expect(!package->isEnabled() && !details->isEnabled(), "unavailable package and absent receipt are disabled");
		editor.setMaterialSource({reader, "initial", {}});
		const auto before = source(editor);
		bool pickerSeen = false;
		QTimer::singleShot(0, &editor, [&] {
			auto *picker = dynamic_cast<ModelSkinSourceDialog *>(editor.findChild<QDialog *>("meshSkinSourceDialog"));
			if (!picker)
			{
				ok = false;
				return;
			}
			pickerSeen = true;
			ok &= expect(picker->layoutDirection() == editor.layoutDirection(), "package picker follows application layout direction");
			picker->resize(scenario ? 1600 : 820, scenario ? 800 : 530);
			auto *table = picker->findChild<QTableView *>("meshSkinSourceEntries");
			ok &= expect(table->model()->rowCount() == 2 && !picker->findChild<QComboBox *>("meshSkinSourceOperation"),
						 "skin-only picker needs no unrelated texture operation");
			select(table, 1);
			ok &= expect(picker->reference().entryIndex == 1 && !reader->readOnGui, "exact selected occurrence survives filtering");
			for (auto *widget : QList<QWidget *>{table, picker->findChild<QLineEdit *>("meshSkinSourceFilter"),
												 picker->findChild<QPushButton *>("meshSkinSourceImport")})
			{
				const auto *access = QAccessible::queryAccessibleInterface(widget);
				ok &= expect(access && !access->text(QAccessible::Name).isEmpty() && !widget->accessibleDescription().isEmpty() &&
								 widget->focusPolicy() != Qt::NoFocus,
							 "package picker exposes native accessible names, descriptions and focus");
				ok &= expect(picker->rect().contains(QRect(widget->mapTo(picker, QPoint()), widget->size())),
							 "picker controls fit scaled RTL layouts");
			}
			app.processEvents();
			ok &= capture(*picker, QStringLiteral("skin-bindings-picker-%1").arg(scenario));
			picker->findChild<QPushButton *>("meshSkinSourceImport")->click();
		});
		package->click();
		ok &= expect(pickerSeen && editor.document().mesh().surfaces[0].skinPaths[0] == "models/new_body" && !reader->readOnGui,
					 "package click applies chosen bindings on the worker");
		const auto after = source(editor);
		ok &= expect(editor.importSkinBindingsFromPackage({"", 1}, &error) && source(editor) == after,
					 "reapplying identical assignments succeeds without an extra undo step");
		undo->trigger();
		ok &= expect(source(editor) == before && !editor.document().canUndo(), "GUI import makes one undo step");
		redo->trigger();
		ok &= expect(source(editor) == after, "GUI redo restores all assignments");
		auto *renderMode = editor.findChild<QComboBox *>("meshRenderMode");
		renderMode->setCurrentIndex(3);
		ok &= expect(settle(editor), "new material paths retire old preview requests");
		ok &= expect(editor.findChild<QLabel *>("meshMaterialStatus")->text().contains("2/2"),
					 "both assigned surface images resolve through the shared material service");
		auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
		const auto uvImage = uv->pixmap().toImage();
		int blue = 0;
		for (int y = 0; y < uvImage.height(); y += 4)
		{
			for (int x = 0; x < uvImage.width(); x += 4)
			{
				const auto c = uvImage.pixelColor(x, y);
				blue += c.blue() > 80 && c.blue() > 2 * c.red() && c.blue() > 2 * c.green();
			}
		}
		ok &= expect(blue > 100, "applied shader paths resolve blue image pixels in the UV view");
		bool receipt = false;
		QTimer::singleShot(0, &editor, [&] {
			auto *dialog = editor.findChild<QDialog *>("meshSkinBindingDetailsDialog");
			if (!dialog)
			{
				ok = false;
				return;
			}
			auto *text = dialog->findChild<QPlainTextEdit *>();
			ok &= expect(dialog->layoutDirection() == editor.layoutDirection(), "receipt follows application layout direction");
			receipt = text && text->isReadOnly() && text->toPlainText().contains("tag_mount") && text->toPlainText().contains("other") &&
					  text->toPlainText().contains("1: models/default.skin") && !text->accessibleName().isEmpty();
			dialog->resize(scenario ? 1300 : 760, scenario ? 650 : 480);
			app.processEvents();
			ok &= capture(*dialog, QStringLiteral("skin-bindings-details-%1").arg(scenario));
			dialog->reject();
		});
		details->click();
		ok &= expect(receipt, "import receipt exposes ignored and unused records");
		tabs->setCurrentIndex(1);
		app.processEvents();
		for (auto *button : {file, package, details})
		{
			const auto *access = QAccessible::queryAccessibleInterface(button);
			ok &= expect(access && !access->text(QAccessible::Name).isEmpty() && !button->accessibleDescription().isEmpty() &&
							 button->focusPolicy() != Qt::NoFocus && button->width() >= button->minimumSizeHint().width(),
						 "binding actions retain accessible focus and expanded text width");
		}
		ok &= capture(editor, QStringLiteral("skin-bindings-editor-%1").arg(scenario));
		if (qEnvironmentVariableIntValue("QT_SCALE_FACTOR") >= 2)
		{
			ok &= expect(editor.devicePixelRatioF() >= 1.99, "2x verification uses actual 2x pixels");
		}
		undo->trigger();
		renderMode->setCurrentIndex(0);
		settle(editor);
		reader->lateFailure = true;
		ok &= expect(!editor.importSkinBindingsFromPackage({"", 1}, &error) && source(editor) == before && !editor.document().canUndo(),
					 "late package failure preserves source and history");
		reader->lateFailure = false;
		reader->delay = true;
		QTimer::singleShot(30, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.importSkinBindingsFromPackage({"", 1}, &error) && source(editor) == before && !editor.operationBusy(),
					 "cancellation retires binding worker without changes");
		reader->delay = false;
		QTimer::singleShot(0, &editor, [&] {
			auto *picker = dynamic_cast<ModelSkinSourceDialog *>(editor.findChild<QDialog *>("meshSkinSourceDialog"));
			if (!picker)
			{
				ok = false;
				return;
			}
			select(picker->findChild<QTableView *>("meshSkinSourceEntries"), 1);
			editor.setMaterialSource({reader, "changed", {}});
			picker->findChild<QPushButton *>("meshSkinSourceImport")->click();
		});
		package->click();
		ok &= expect(source(editor) == before && !editor.document().canUndo(), "package revision change invalidates an open picker");
		ok &= expect(editor.setMesh(mesh, &error) && !details->isEnabled(), "opening a model retires the old import receipt and recovery");
		editor.setMaterialSource({});
		if (scenario)
		{
			app.removeTranslator(&expansion);
		}
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? 0 : 1;
}
