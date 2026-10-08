#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_boundary_bridge_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool condition, const char *message)
{
	++checks;
	if (!condition)
		std::cerr << "FAIL: " << message << '\n';
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
			return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool render(QWidget &widget, int scenario, const QString &phase)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(directory).filePath(QString("boundary-bridge-%1-%2-%3x.png").arg(phase).arg(scenario).arg(widget.devicePixelRatioF())));
}
} // namespace

int main(int argc, char **argv)
{
	// Test-owned Qt signals/properties and render targets only; no OS input/capture.
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
	QTemporaryDir temporary(QDir(root).filePath("boundary-bridge-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	const auto original = tests::bridgeDisks();
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0 ? StudioTheme::Dark : scenario == 1 ? StudioTheme::HighContrastLight : StudioTheme::HighContrastDark,
			UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelEditorDialog editor;
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open animated boundary-pair fixture");
		editor.resize(scenario ? 2250 : 1400, scenario ? 1500 : 1040);
		editor.show();
		app.processEvents();
		auto *bridge = editor.findChild<QPushButton *>("bridgeMeshBoundaryLoops");
		auto *twist = editor.findChild<QSpinBox *>("meshBridgeTwist");
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *frame = editor.findChild<QComboBox *>("meshFrame");
		auto *scope = editor.findChild<QComboBox *>("meshFrameScope");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		if (!expect(bridge && twist && mode && frame && scope && table && tabs && preview && undo && redo, "bridge controls exist"))
			return 1;
		for (int i = 0; i < tabs->count(); ++i)
			if (tabs->widget(i)->isAncestorOf(bridge))
				tabs->setCurrentIndex(i);
		for (auto *control : {static_cast<QWidget *>(bridge), static_cast<QWidget *>(twist)})
		{
			auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && accessible->role() == (control == bridge ? QAccessible::Button : QAccessible::SpinBox) &&
				!accessible->text(QAccessible::Name).isEmpty() && !accessible->text(QAccessible::Description).isEmpty() && control->focusPolicy() != Qt::NoFocus,
				"bridge controls expose semantic roles, names, help and focus");
		}
		const auto select = [&](const QSet<ModelEdge> &edges) {
			mode->setCurrentIndex(2);
			table->selectionModel()->clearSelection();
			for (int row = 0; row < table->model()->rowCount(); ++row)
			{
				const auto edge = ModelEdge{table->model()->index(row, 1).data().toInt(), table->model()->index(row, 2).data().toInt()};
				if (edges.contains(edge))
					table->selectionModel()->select(table->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
			}
			return editor.document().selection().edges == edges;
		};
		ok &= expect(select({{0, 1}}), "table selects an explicit first boundary seed");
		bridge->click();
		ok &= expect(bytes(editor.document().mesh()) == bytes(original) && !editor.document().canUndo(), "one-loop refusal leaves document and history intact");
		ok &= expect(select({{0, 1}, {4, 5}}), "table selects two boundary seeds");
		frame->setCurrentIndex(1);
		scope->setCurrentIndex(1);
		twist->setValue(1);
		for (auto *area : editor.findChildren<QScrollArea *>())
		{
			if (!area->isAncestorOf(bridge))
				continue;
			const auto center = bridge->mapTo(area->widget(), bridge->rect().center());
			area->verticalScrollBar()->setValue(center.y() - area->viewport()->height() / 2);
			app.processEvents();
			for (auto *control : {static_cast<QWidget *>(bridge), static_cast<QWidget *>(twist)})
			{
				const auto bounds = QRect(control->mapTo(area->viewport(), QPoint()), control->size());
				ok &= expect(area->horizontalScrollBar()->maximum() == 0 && area->viewport()->rect().contains(bounds),
					"bridge action and twist fit together at expanded text and RTL");
			}
		}
		// The refused bridge ran a worker; offscreen, that can leave no active window.
		editor.activateWindow();
		app.processEvents();
		twist->setFocus(Qt::OtherFocusReason);
		ok &= expect(twist->hasFocus() && twist->layoutDirection() == Qt::LeftToRight, "twist has visible focus and stable numeric direction");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
			ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "actual 2x display scaling is active");
		ok &= expect(tests::settleModelViewport(*preview) && render(editor, scenario, "before"), "render disjoint boundaries before authoring");
		ModelEdit operation;
		operation.kind = ModelEditKind::BridgeBoundaryLoops;
		operation.selection.edges = {{0, 1}, {4, 5}};
		operation.sourceFrame = 1;
		operation.bridgeTwist = 1;
		auto expected = original;
		ModelSelection expectedSelection;
		ok &= expect(applyModelEdit(&expected, operation, &expectedSelection, &error), "shared service accepts the reviewed alignment");
		bool responsive = false, locked = false;
		QTimer::singleShot(0, &editor, [&] { responsive = editor.operationBusy(); locked = !bridge->isEnabled(); });
		bridge->click();
		ok &= expect(responsive && locked && !editor.operationBusy(), "bridge worker exposes busy state and locks mutation controls");
		ok &= expect(bytes(editor.document().mesh()) == bytes(expected) && editor.document().selection() == expectedSelection && mode->currentIndex() == 0 &&
			table->selectionModel()->selectedRows().size() == 8, "GUI bridges every pose with exact core twist and selects the new faces");
		bridge->setFocus(Qt::OtherFocusReason);
		ok &= expect(tests::settleModelViewport(*preview) && render(editor, scenario, "after"), "render connected geometry and finishing selection");
		undo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == bytes(original) && editor.document().selection().edges == operation.selection.edges && !editor.document().canUndo(),
			"GUI undo restores original boundaries in one step");
		redo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == bytes(expected), "GUI redo restores exact bridge");
		ModelEdit uv;
		uv.kind = ModelEditKind::UnwrapUv;
		uv.selection = editor.document().selection();
		uv.frame = 1;
		ok &= expect(editor.applyEdit(uv, &error) && editor.document().selection().faces == expectedSelection.faces,
			"new faces hand directly to atlas unwrapping through the normal document worker");
		ok &= expect(editor.setMesh(tests::collapsingBridge(), &error), "open later-pose collapse fixture");
		ok &= expect(!editor.applyEdit(operation, &error) && bytes(editor.document().mesh()) == bytes(tests::collapsingBridge()) && !editor.document().canUndo(),
			"failed bridge retains source without a history entry");
		ok &= expect(editor.setMesh(original, &error), "restore ordinary source for cancellation");
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.applyEdit(operation, &error) && bytes(editor.document().mesh()) == bytes(original) && !editor.document().canUndo(),
			"cancelled bridge publishes no geometry or history");
		editor.hide();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " boundary bridge GUI checks\n";
	return ok ? 0 : 1;
}
