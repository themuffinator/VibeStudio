#include "app/application_shell.h"
#include "app/brush_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
class ExpandedLabels final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).contains("Brush")) {
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Direct widget APIs and QWidget::render only. No OS capture/input injection.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	std::cerr << "Brush UI application initialized" << std::endl;
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
	LevelMapDocument map;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	ok &= expect(createLevelMap(create, &map, &error) &&
					 addLevelMapBoxBrush(&map, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/stone"), nullptr, &error),
				 "create brush", error);
	for (int scale : {100, 200}) {
		std::cerr << "Brush UI scale " << scale << std::endl;
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		StudioSettings settings;
		settings.setTheme(theme);
		settings.setReducedMotion(true);
		ExpandedLabels translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		BrushEditorDialog dialog(map.brushes.first(), 8);
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1550, 1050);
		}
		std::cerr << "Brush UI dialog constructed" << std::endl;
		dialog.show();
		app.processEvents();
		auto *view = dialog.findChild<BrushComponentView *>();
		auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("brushComponents"));
		auto *preview = dialog.findChild<ModelViewport *>();
		auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("brushComponentMode"));
		if (!expect(view && table && preview && mode, "component controls present")) {
			return EXIT_FAILURE;
		}
		ok &=
			expect(view->focusPolicy() == Qt::StrongFocus && QAccessible::queryAccessibleInterface(view)->role() == QAccessible::Graphic &&
					   !table->accessibleDescription().isEmpty(),
				   "accessible view and numeric alternative");
		ok &=
			expect(table->rowCount() == 8 && table->layoutDirection() == Qt::LeftToRight && preview->hasMesh() && preview->reducedMotion(),
				   "initial topology and shared preview");
		view->frameBrush();
		ok &= expect(view->componentAt(view->componentPosition(7)) == 7, "top projection picks frontmost corner");
		dialog.selectComponents(LevelBrushComponent::Vertex, {7});
		ok &= expect(table->selectionModel()->selectedRows().size() == 1 && view->selection() == QVector<int>{7},
					 "table and canvas selection synchronized");
		ok &= expect(dialog.moveSelected({16, 8, 12, true}) && dialog.brush().faceCount > 6, "GUI reshapes and splits faces", error);
		const int bentFaces = dialog.brush().faceCount;
		dialog.undo();
		ok &= expect(dialog.brush().faceCount == 6, "local undo restores original topology");
		dialog.redo();
		ok &= expect(dialog.brush().faceCount == bentFaces, "local redo restores result");
		dialog.undo();
		dialog.selectComponents(LevelBrushComponent::Vertex, {7});
		table->item(7, 1)->setText(QStringLiteral("80"));
		ok &= expect(dialog.brush().maxs.x == 80, "editable coordinate applies to vertex");
		table->item(7, 1)->setText(QStringLiteral("nan"));
		ok &= expect(dialog.brush().maxs.x == 80 && !dialog.findChild<QLabel *>(QStringLiteral("brushComponentStatus"))->text().isEmpty(),
					 "invalid coordinate rejected visibly");
		dialog.undo();
		mode->setCurrentIndex(1);
		ok &= expect(table->rowCount() == 12, "edge mode exposes all edges");
		dialog.selectComponents(LevelBrushComponent::Edge, {0, 1});
		ok &= expect(dialog.moveSelected({8, 0, 0, true}), "edge multiselection moves as one draft edit");
		dialog.undo();
		dialog.selectComponents(LevelBrushComponent::Face, {0});
		ok &= expect(table->rowCount() == 6 && preview->highlightedTriangleCount() == 2, "face mode highlights shared surface triangles");
		ok &= expect(dialog.moveSelected({8, 4, 0, true}), "face displacement rebuilds neighbors");
		dialog.undo();
		dialog.selectComponents(LevelBrushComponent::Vertex, {0});
		ok &= expect(!dialog.moveSelected({48, 48, 48, true}), "collapse disabled by default");
		dialog.findChild<QCheckBox *>(QStringLiteral("brushAllowCollapse"))->setChecked(true);
		ok &= expect(dialog.moveSelected({48, 48, 48, true}), "explicit collapse choice applies");
		dialog.undo();
		dialog.selectComponents(LevelBrushComponent::Vertex, {7});
		dialog.moveSelected({16, 8, 12, true});
		const auto captureRoot = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		std::cerr << "Brush UI controls verified" << std::endl;
		if (!captureRoot.isEmpty()) {
			QDir().mkpath(captureRoot);
			auto *tabs = dialog.findChild<QTabWidget *>();
			auto *scroll = dialog.findChild<QScrollArea *>();
			for (int page = 0; page < 2; ++page) {
				tabs->setCurrentIndex(page);
				app.processEvents();
				view->frameBrush();
				preview->frameModel();
				for (int bottom = 0; bottom < 2; ++bottom) {
					scroll->verticalScrollBar()->setValue(bottom ? scroll->verticalScrollBar()->maximum() : 0);
					app.processEvents();
					if (page == 1) {
						ok &= expect(tests::settleModelViewport(*preview), "surface render completes before capture");
					}
					QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
					image.fill(Qt::transparent);
					dialog.render(&image);
					ok &= expect(
						image.save(
							QDir(captureRoot).filePath(QStringLiteral("brush-editor-%1-tab%2-%3.png").arg(scale).arg(page).arg(bottom))),
						"widget render saved");
				}
			}
		}
		dialog.setApplyHandler([](const LevelMapBrush &, QString *commitError) {
			*commitError = QStringLiteral("Map changed during this draft.");
			return false;
		});
		dialog.accept();
		ok &= expect(dialog.isVisible() && dialog.brush().faceCount == bentFaces &&
						 dialog.findChild<QLabel *>(QStringLiteral("brushComponentStatus"))->text().contains(QStringLiteral("Map changed")),
					 "failed commit preserves draft and reports conflict");
		dialog.setApplyHandler({});
		dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
		ok &= expect(dialog.result() == QDialog::Accepted, "Apply accepts draft");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ApplicationShell shell;
	std::cerr << "Brush UI shell constructed" << std::endl;
	create.starterRoom = true;
	ok &= expect(shell.createLevelDocument(create, &error), "shell creates room", error);
	std::cerr << "Brush UI room created" << std::endl;
	auto brush = shell.levelDocument().brushes.first();
	const int id = brush.id;
	ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {7}, {8, 4, 4, true}, 0, false, nullptr, &error) &&
					 shell.applyLevelBrushGeometry(brush, id, &error),
				 "shell commits geometry through shared service", error);
	std::cerr << "Brush UI geometry committed" << std::endl;
	ok &= expect(shell.levelDocument().undoStack.size() == 1, "one map undo per dialog commit");
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	ok &= expect(shell.levelDocument().brushes.first().faceCount == 6, "shell undo restores room brush");
	shell.findChild<QAction *>(QStringLiteral("map.redo"))->trigger();
	ok &= expect(shell.levelDocument().brushes.first().faceCount > 6, "shell redo restores bent brush");
	const auto output = QDir(temp.path()).filePath(QStringLiteral("room.map"));
	ok &= expect(shell.saveLevelDocument(output, false, &error), "shell saves component edits", error);
	std::cerr << "Brush UI map saved" << std::endl;
	LevelMapDocument loaded;
	ok &= expect(loadLevelMap({output, {}, {}}, &loaded, &error) &&
					 std::any_of(loaded.brushes.cbegin(), loaded.brushes.cend(),
								 [&](const auto &b) { return b.boundsSolved && b.faceCount == brush.faceCount && b.faceCount > 6; }),
				 "saved shell brush reopens with edited topology", error);
	ok &= expect(shell.close(), "saved shell closes without unsaved prompt");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
