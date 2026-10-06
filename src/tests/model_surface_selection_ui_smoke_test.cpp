#include "app/model_editor_dialog.h"
#include "app/model_surface_dialog.h"
#include "app/model_uv_view.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_surfaces_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <cmath>
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
		if (QByteArray(context) != "VibeStudioModelEditor" && QByteArray(context) != "ModelSurfaceDialog")
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
} // namespace
int main(int argc, char **argv)
{
	// Test-owned Qt values and render targets only; no OS input or screen capture.
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
	QTemporaryDir temporary(QDir(root).filePath("surface-selection-ui-XXXXXX"));
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
		ModelEditorDialog editor;
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open multi-surface fixture");
		editor.resize(scenario == 0 ? 1400 : 2200, scenario == 0 ? 960 : 1480);
		editor.show();
		app.processEvents();
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *surface = editor.findChild<QComboBox *>("meshSurface");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		auto *tool = editor.findChild<QComboBox *>("meshTransformTool");
		auto *scope = editor.findChild<QComboBox *>("meshFrameScope");
		auto *frame = editor.findChild<QComboBox *>("meshFrame");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		if (!expect(mode && surface && table && preview && tool && scope && frame && undo && redo, "editor provides all shared controls"))
			return 1;
		mode->setCurrentIndex(5);
		preview->trianglePicked(2, 6, int(ModelViewportPick::Toggle));
		ok &= expect(editor.document().selection().surfaces == QSet<int>{0, 2} && editor.document().selection().surface == 2 &&
						 table->model()->rowCount() == 3 && table->selectionModel()->selectedRows().size() == 2 &&
						 preview->highlightedTriangleCount() == 6 && !editor.document().canUndo(),
					 "surface picking persists across surfaces and highlights all selected faces");
		table->selectionModel()->setCurrentIndex(table->model()->index(0, 0), QItemSelectionModel::NoUpdate);
		ok &= expect(editor.document().selection().surface == 0 && editor.document().selection().surfaces == QSet<int>{0, 2} &&
						 table->currentIndex().row() == 0,
					 "keyboard table focus changes active member without losing selection or focus");
		ok &= expect(!editor.findChild<QPushButton *>("extrudeMesh")->isEnabled() &&
						 !editor.findChild<QAction *>("moveMeshUvs")->isEnabled() &&
						 !editor.findChild<QAction *>("selectMeshUvIslands")->isEnabled(),
					 "component-only geometry and UV actions are unavailable in Surfaces mode");
		const auto selected = editor.document().selection();
		ModelSurfaceDialog review(original, selected);
		auto *sources = review.findChild<QTreeWidget *>("surfaceJoinSources");
		ok &= expect(sources->topLevelItem(0)->checkState(0) == Qt::Checked && sources->topLevelItem(1)->checkState(0) == Qt::Unchecked &&
						 sources->topLevelItem(2)->checkState(0) == Qt::Checked,
					 "surface manager initializes join operands from persistent selection");
		editor.findChild<QComboBox *>("meshViewPreset")->setCurrentIndex(1);
		ok &= expect(settleModelViewport(*preview), "whole-surface selection reaches renderer");
		const auto before = surfaceBytes(editor.document().mesh());
		const auto fixedPoint = preview->vertexScreenPosition(1, 0);
		const auto start = preview->transformGizmoPoints()[3];
		ok &=
			expect(std::isfinite(start.x()) && preview->beginEditTransform(start) && preview->updateEditTransform(start + QPointF(35, -20)),
				   "whole-surface move handle starts and previews");
		ok &= expect(surfaceBytes(editor.document().mesh()) == before && !editor.document().canUndo() &&
						 (preview->vertexScreenPosition(1, 0) - fixedPoint).manhattanLength() < .001,
					 "preview leaves document and unselected surface fixed");
		ok &= expect(settleModelViewport(*preview) && render(editor, QStringLiteral("surface-selection-move-%1").arg(scenario)),
					 "render multi-surface move preview");
		preview->finishEditTransform(false);
		ok &= expect(!editor.document().canUndo() && surfaceBytes(editor.document().mesh()) == before && settleModelViewport(*preview),
					 "cancel restores preview without history");
		tool->setCurrentIndex(1);
		auto rings = preview->rotationGizmoRings();
		if (!expect(rings[2].size() > 36, "shared rotation ring is available"))
			return 1;
		ok &= expect(preview->beginEditTransform(rings[2][12]) && preview->updateEditTransform(rings[2][36]),
					 "rotate both surfaces around one centre");
		const auto point0 = preview->vertexScreenPosition(0, 0), point2 = preview->vertexScreenPosition(2, 5);
		ok &= expect(settleModelViewport(*preview), "rotated preview finishes asynchronously");
		preview->finishEditTransform(true);
		ok &= expect(editor.document().selection() == selected && editor.document().canUndo() && settleModelViewport(*preview) &&
						 (preview->vertexScreenPosition(0, 0) - point0).manhattanLength() < .01 &&
						 (preview->vertexScreenPosition(2, 5) - point2).manhattanLength() < .01,
					 "committed surfaces exactly match preview including unused vertices");
		const auto rotated = surfaceBytes(editor.document().mesh());
		undo->trigger();
		ok &= expect(surfaceBytes(editor.document().mesh()) == before && editor.document().selection() == selected &&
						 !editor.document().canUndo() && mode->currentIndex() == 5 && table->selectionModel()->selectedRows().size() == 2,
					 "one undo restores whole-surface mode and table selection");
		redo->trigger();
		ok &= expect(surfaceBytes(editor.document().mesh()) == rotated && editor.document().selection() == selected,
					 "redo restores both surfaces");
		undo->trigger();
		frame->setCurrentIndex(1);
		scope->setCurrentIndex(1);
		tool->setCurrentIndex(2);
		ok &= expect(settleModelViewport(*preview), "display current pose before scale");
		const auto scaleStart = preview->transformGizmoPoints()[3];
		ok &= expect(preview->beginEditTransform(scaleStart) && preview->updateEditTransform(scaleStart + QPointF(96, 0)),
					 "shared scale preview starts");
		const auto scalePoint = preview->vertexScreenPosition(2, 5);
		preview->finishEditTransform(true);
		ok &= expect(settleModelViewport(*preview) && (preview->vertexScreenPosition(2, 5) - scalePoint).manhattanLength() < .01 &&
						 editor.document().mesh().surfaces[0].frames[0].positions[0].x == 0 &&
						 exactVertex(editor.document().mesh().surfaces[1], 5, original.surfaces[1], 5),
					 "current-frame scale commits displayed common pivot while other poses and surfaces stay fixed");
		undo->trigger();
		scope->setCurrentIndex(0);
		editor.findChild<QDoubleSpinBox *>("meshOffset2")->setValue(2);
		editor.findChild<QPushButton *>("transformMesh")->click();
		ok &=
			expect(editor.document().mesh().surfaces[0].frames[0].positions[0].z == 2 &&
					   editor.document().mesh().surfaces[2].frames[1].positions[5].z == 9.125f && editor.document().selection() == selected,
				   "numeric transform applies to same persistent surface set across all poses");
		undo->trigger();
		ModelEdit pending;
		pending.selection = selected;
		pending.translation = {1, 2, 3};
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.applyEdit(pending, &error) && !editor.document().canUndo() && editor.document().selection() == selected &&
						 surfaceBytes(editor.document().mesh()) == before,
					 "worker cancellation leaves all selected surfaces and history intact");
		surface->setCurrentIndex(1);
		ok &= expect(editor.document().selection().surfaces == QSet<int>{0, 1, 2} && editor.document().selection().surface == 1,
					 "active surface combo adds member while preserving selection");
		preview->trianglePicked(1, 3, int(ModelViewportPick::Toggle));
		ok &= expect(editor.document().selection().surfaces == QSet<int>{0, 2} && editor.document().selection().surface == 0,
					 "toggle active member chooses deterministic surviving active surface");
		table->clearSelection();
		ok &= expect(editor.document().selection().surfaces.isEmpty() && !std::isfinite(preview->transformGizmoPoints()[3].x()),
					 "empty surface selection removes transform handles");
		table->selectAll();
		ok &= expect(editor.document().selection().surfaces.size() == 3, "Select All includes every surface");
		mode->setCurrentIndex(0);
		ok &= expect(editor.document().selection().surfaces.isEmpty() && table->model()->rowCount() == 3 &&
						 editor.findChild<QPushButton *>("extrudeMesh")->isEnabled(),
					 "component mode restores local component authoring");
		preview->trianglePicked(1, 3, int(ModelViewportPick::Replace));
		ok &= expect(editor.document().selection().faces == QSet<int>{0} && editor.document().selection().surface == 1,
					 "face picking remains local after whole-surface mode");
		mode->setCurrentIndex(5);
		preview->trianglePicked(2, 6, int(ModelViewportPick::Toggle));
		table->setFocus(Qt::OtherFocusReason);
		app.processEvents();
		for (auto *widget : QList<QWidget *>{mode, surface, table})
		{
			const auto *accessible = QAccessible::queryAccessibleInterface(widget);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
							 editor.rect().contains(QRect(widget->mapTo(&editor, QPoint()), widget->size())),
						 "selection controls remain accessible and inside expanded RTL bounds");
		}
		ok &= expect(table->hasFocus() && table->selectionMode() == QAbstractItemView::ExtendedSelection,
					 "surface table keeps keyboard focus and extended selection");
		for (int column = 0; column < 3; ++column)
			ok &= expect(table->columnWidth(column) >=
							 table->fontMetrics().horizontalAdvance(table->model()->headerData(column, Qt::Horizontal).toString()),
						 "expanded table headings fit without clipping");
		ok &= expect(table->viewport()->width() >= table->columnWidth(0) + table->columnWidth(1) + table->columnWidth(2),
					 "surface names and both counts remain visible without horizontal scrolling at expanded text sizes");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
			ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "actual 2x widget pixels are used");
		ok &= expect(settleModelViewport(*preview) && settleModelUv(*editor.findChild<ModelUvView *>("meshUvPreview")) &&
						 render(editor, QStringLiteral("surface-selection-editor-%1").arg(scenario)),
					 "render persistent table selection and transform controls");
		ok &= expect(editor.setMesh(original, &error), "retire test recovery draft");
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " surface selection UI checks\n";
	return ok ? 0 : 1;
}
