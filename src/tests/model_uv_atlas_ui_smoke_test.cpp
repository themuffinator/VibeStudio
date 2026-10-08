#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_uv_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor")
		{
			return {};
		}
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
} // namespace
int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	// Uses public values/signals and QWidget::render, never injected input or OS captures.
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
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-atlas-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	auto original = tests::uvSquare();
	original.surfaces[0].uvSeams = {{0, 2}};
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		ModelEditorDialog editor;
		editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open UV atlas fixture");
		editor.resize(scenario == 0 ? 1320 : 2080, scenario == 0 ? 920 : 1360);
		editor.show();
		app.processEvents();
		auto *size = editor.findChild<QSpinBox *>("meshUvAtlasResolution");
		auto *padding = editor.findChild<QSpinBox *>("meshUvAtlasPadding");
		auto *unwrap = editor.findChild<QPushButton *>("unwrapMeshUv");
		auto *pack = editor.findChild<QPushButton *>("packMeshUv");
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
		auto *inspector = editor.findChild<QTabWidget *>("meshInspector");
		ok &= expect(size && padding && unwrap && pack && mode && table && uv && inspector, "atlas controls are exposed");
		if (!size || !padding || !unwrap || !pack || !mode || !table || !uv || !inspector)
		{
			return EXIT_FAILURE;
		}
		editor.showSidebarPage(QStringLiteral("surface"));
		for (auto *tabs : editor.findChildren<QTabWidget *>())
		{
			for (int i = 0; i < tabs->count(); ++i)
			{
				if (tabs->widget(i)->isAncestorOf(uv))
				{
					tabs->setCurrentIndex(i);
				}
			}
		}
		ok &= expect(!unwrap->isEnabled() && !pack->isEnabled(), "atlas actions require a face selection");
		table->selectAll();
		ok &= expect(unwrap->isEnabled() && pack->isEnabled(), "face selection enables atlas operations");
		size->setValue(32);
		ok &= expect(padding->maximum() == 3 && padding->value() == 3, "padding follows resolution constraints");
		size->setValue(256);
		padding->setValue(3);
		const QList<QWidget *> controls{size, padding, unwrap, pack};
		for (QWidget *control : controls)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !control->accessibleDescription().isEmpty() &&
							 control->focusPolicy() != Qt::NoFocus,
						 "atlas controls have accessible names, descriptions and focus");
		}
		unwrap->click();
		const auto mapped = bytes(editor.document().mesh());
		ok &= expect(editor.document().mesh().vertexCount == 6 && editor.document().isModified() &&
						 editor.document().selection().faces.size() == 2,
					 "editor unwrap commits one pose-preserving seam split");
		editor.findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(bytes(editor.document().mesh()) == bytes(original), "editor atlas undo restores original source");
		editor.findChild<QAction *>("redoMesh")->trigger();
		ok &= expect(bytes(editor.document().mesh()) == mapped, "editor atlas redo restores generated UVs");
		pack->click();
		ok &= expect(editor.document().mesh().vertexCount == 6 && editor.document().mesh().frames.size() == 2,
					 "editor packs existing charts without extra geometry splits");
		ok &= expect(tests::settleModelUv(*uv), "atlas result renders in UV view");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
		{
			ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "atlas UI uses actual 2x pixel ratio");
		}
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty())
		{
			for (auto *area : editor.findChildren<QScrollArea *>())
			{
				if (area->isAncestorOf(size))
				{
					area->ensureWidgetVisible(size);
				}
			}
			app.processEvents();
			QImage image(editor.size() * editor.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(editor.devicePixelRatioF());
			image.fill(Qt::transparent);
			editor.render(&image);
			ok &= expect(
				image.save(QDir(evidence).filePath(QStringLiteral("uv-atlas-%1-%2x.png").arg(scenario).arg(editor.devicePixelRatioF()))),
				"save atlas widget render");
		}
		mode->setCurrentIndex(3);
		ok &= expect(!unwrap->isEnabled() && !pack->isEnabled(), "tag mode disables atlas actions");
		// Cancellation through the document worker's public API leaves its value unchanged.
		mode->setCurrentIndex(0);
		table->selectAll();
		const auto before = bytes(editor.document().mesh());
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ModelEdit operation;
		operation.kind = ModelEditKind::UnwrapUv;
		operation.selection = editor.document().selection();
		ok &= expect(!editor.applyEdit(operation, &error) && bytes(editor.document().mesh()) == before,
					 "cancel atlas worker without adopting partial state");
		ok &= expect(editor.setMesh(original, &error), "retire test recovery draft");
		if (scenario > 0)
		{
			app.removeTranslator(&expansion);
		}
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
