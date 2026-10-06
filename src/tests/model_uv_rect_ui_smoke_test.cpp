#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/package_archive.h"
#include "core/studio_settings.h"
#include "tests/model_uv_rect_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value) std::cerr << "FAIL: " << message << '\n';
	return value;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor") return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool settle(ModelEditorDialog &editor, ModelUvView &uv)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (editor.materialLoading() && elapsed.elapsed() < 20000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return !editor.materialLoading() && tests::settleModelUv(uv);
}
bool render(QWidget &widget, int scenario, const QString &phase)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty()) return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(directory).filePath(QString("rectangular-atlas-%1-%2-%3x.png").arg(phase).arg(scenario).arg(widget.devicePixelRatioF())));
}
} // namespace
int main(int argc, char **argv)
{
	// Test-owned widget signals/properties and render targets; no OS input/capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root)) return 1;
	QTemporaryDir temporary(QDir(root).filePath("rectangular-atlas-ui-XXXXXX"));
	if (!temporary.isValid()) return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	const auto assets = QDir(temporary.path()).filePath("assets");
	if (!QDir().mkpath(QDir(assets).filePath("models"))) return 1;
	QImage texture(512, 128, QImage::Format_RGB32);
	for (int y = 0; y < texture.height(); ++y)
		for (int x = 0; x < texture.width(); ++x)
			texture.setPixelColor(x, y, ((x / 16 + y / 16) % 2) ? QColor(100, 135, 160) : QColor(45, 65, 85));
	if (!texture.save(QDir(assets).filePath("models/atlas.png"))) return 1;
	auto package = std::make_shared<PackageArchive>();
	QString error;
	if (!package->load(assets, &error)) return 1;
	bool ok = true;
	const auto original = tests::atlasPanels();
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario) app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0 ? StudioTheme::Dark : scenario == 1 ? StudioTheme::HighContrastLight : StudioTheme::HighContrastDark,
			UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelEditorDialog editor;
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open animated rectangular atlas fixture");
		editor.resize(scenario ? 2250 : 1400, scenario ? 1500 : 1040);
		editor.setMaterialSource({package, QStringLiteral("rectangular-atlas"), {}});
		editor.show();
		app.processEvents();
		auto *width = editor.findChild<QSpinBox *>("meshUvAtlasResolution");
		auto *height = editor.findChild<QSpinBox *>("meshUvAtlasHeight");
		auto *square = editor.findChild<QCheckBox *>("meshUvAtlasSquare");
		auto *padding = editor.findChild<QSpinBox *>("meshUvAtlasPadding");
		auto *unwrap = editor.findChild<QPushButton *>("unwrapMeshUv");
		auto *pack = editor.findChild<QPushButton *>("packMeshUv");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
		auto *renderMode = editor.findChild<QComboBox *>("meshRenderMode");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		if (!expect(width && height && square && padding && unwrap && pack && table && uv && renderMode && undo && redo, "rectangular controls exist")) return 1;
		renderMode->setCurrentIndex(3);
		for (auto *tabs : editor.findChildren<QTabWidget *>())
			for (int i = 0; i < tabs->count(); ++i)
				if (tabs->widget(i)->isAncestorOf(uv) || tabs->widget(i)->isAncestorOf(width)) tabs->setCurrentIndex(i);
		ok &= expect(!unwrap->isEnabled() && square->isChecked() && !height->isEnabled(), "square default and selection requirements are explicit");
		width->setValue(32);
		ok &= expect(height->value() == 32 && padding->maximum() == 3, "square height and padding track width");
		width->setValue(512);
		square->setChecked(false);
		height->setValue(32);
		ok &= expect(height->isEnabled() && padding->maximum() == 3, "short rectangular axis limits padding");
		height->setValue(128);
		padding->setValue(2);
		table->selectAll();
		const QList<QWidget *> controls{width, height, square, padding, unwrap, pack};
		for (auto *control : controls)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !accessible->text(QAccessible::Description).isEmpty() &&
				control->focusPolicy() != Qt::NoFocus, "atlas controls expose accessible names, help and focus");
		}
		for (auto *scroll : editor.findChildren<QScrollArea *>())
		{
			if (!scroll->isAncestorOf(width)) continue;
			const int first = width->mapTo(scroll->widget(), QPoint()).y(), last = pack->mapTo(scroll->widget(), pack->rect().bottomLeft()).y();
			scroll->verticalScrollBar()->setValue((first + last - scroll->viewport()->height()) / 2);
			app.processEvents();
			for (auto *control : controls)
				ok &= expect(scroll->horizontalScrollBar()->maximum() == 0 && scroll->viewport()->rect().contains(QRect(control->mapTo(scroll->viewport(), QPoint()), control->size())),
					"atlas dimensions and actions fit together with expanded text and RTL");
		}
		height->setFocus(Qt::OtherFocusReason);
		ok &= expect(height->hasFocus() && height->layoutDirection() == Qt::LeftToRight, "height is focusable and keeps numeric direction in RTL");
		ok &= expect(settle(editor, *uv), "rectangular package texture reaches UV view");
		const auto origin = uv->uvToScreen({0, 0}), across = uv->uvToScreen({1, 0}), down = uv->uvToScreen({0, 1});
		ok &= expect(std::abs(std::abs((across.x() - origin.x()) / (down.y() - origin.y())) - 4) < .001, "UV view reflects the actual 512x128 material aspect");
		ok &= expect(render(editor, scenario, "before"), "render rectangular atlas controls and texture");
		ModelEdit edit;
		edit.kind = ModelEditKind::UnwrapUv;
		edit.selection = editor.document().selection();
		edit.uvAtlasResolution = 512; edit.uvAtlasHeight = 128; edit.uvAtlasPadding = 2;
		auto expected = original;
		ok &= expect(applyModelEdit(&expected, edit, nullptr, &error), "core accepts reviewed rectangular settings");
		bool responsive = false, locked = false;
		QTimer::singleShot(0, &editor, [&] { responsive = editor.operationBusy(); locked = !unwrap->isEnabled() && !height->isEnabled(); });
		unwrap->click();
		ok &= expect(responsive && locked && !editor.operationBusy() && height->isEnabled(), "worker exposes busy state and restores rectangular controls");
		ok &= expect(bytes(editor.document().mesh()) == bytes(expected) && editor.document().selection().faces == edit.selection.faces, "GUI and core agree on rectangular charts and selection");
		unwrap->setFocus(Qt::OtherFocusReason);
		ok &= expect(settle(editor, *uv) && render(editor, scenario, "after"), "render chart layout against rectangular texture");
		undo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == bytes(original) && !editor.document().canUndo(), "one undo restores complete source");
		redo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == bytes(expected), "redo restores exact rectangular layout");
		edit.kind = ModelEditKind::PackUv;
		ok &= expect(applyModelEdit(&expected, edit, nullptr, &error), "core packs existing rectangular UVs");
		pack->click();
		ok &= expect(bytes(editor.document().mesh()) == bytes(expected), "GUI repack preserves the core pixel-space result");
		const auto beforeCancel = bytes(editor.document().mesh());
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.applyEdit(edit, &error) && bytes(editor.document().mesh()) == beforeCancel, "cancel publishes no partial atlas");
		square->setChecked(true);
		ok &= expect(height->value() == width->value() && !height->isEnabled(), "return to square mode is explicit");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2") ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "actual 2x display scaling");
		editor.setMesh(original, &error);
		editor.hide();
		if (scenario) app.removeTranslator(&expansion);
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok) std::cerr << error.toStdString() << '\n';
	std::cout << checks << " rectangular atlas GUI checks\n";
	return ok ? 0 : 1;
}
