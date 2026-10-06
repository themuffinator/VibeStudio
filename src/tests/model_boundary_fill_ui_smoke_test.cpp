#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_boundary_fill_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
bool expect(bool condition, const char *message)
{
	if (!condition)
		std::cerr << "FAIL: " << message << '\n';
	return condition;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
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
		if (QByteArray(context) != "VibeStudioModelEditor")
			return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Only test-owned Qt widgets/signals/render targets; no OS input or capture.
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
	QTemporaryDir temporary(QDir(root).filePath("boundary-fill-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	const auto original = tests::boundaryChangingConcavity();
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
		ok &= expect(editor.setMesh(original, &error), "open changing-concavity animated fixture");
		editor.resize(scenario == 0 ? 1320 : 2080, scenario == 0 ? 920 : 1360);
		editor.show();
		app.processEvents();
		auto *fill = editor.findChild<QPushButton *>("fillMeshBoundaryLoops");
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *frame = editor.findChild<QComboBox *>("meshFrame");
		auto *scope = editor.findChild<QComboBox *>("meshFrameScope");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		if (!fill || !mode || !frame || !scope || !table || !tabs || !preview || !undo || !redo)
			return 1;
		for (int i = 0; i < tabs->count(); ++i)
		{
			if (tabs->widget(i)->isAncestorOf(fill))
				tabs->setCurrentIndex(i);
		}
		auto *accessible = QAccessible::queryAccessibleInterface(fill);
		ok &= expect(accessible && accessible->role() == QAccessible::Button && !accessible->text(QAccessible::Name).isEmpty() &&
						 !accessible->text(QAccessible::Description).isEmpty() && fill->focusPolicy() != Qt::NoFocus,
					 "fill action exposes an accessible button role, name, guidance and keyboard focus");
		mode->setCurrentIndex(2);
		int edgeRow = -1;
		for (int row = 0; row < table->model()->rowCount(); ++row)
		{
			if (table->model()->index(row, 1).data().toInt() == 4 && table->model()->index(row, 2).data().toInt() == 5)
				edgeRow = row;
		}
		if (edgeRow < 0)
			return 1;
		table->selectionModel()->select(table->model()->index(edgeRow, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		ok &= expect(editor.document().selection().edges == QSet<ModelEdge>{{4, 5}}, "component table selects one hole seed edge");
		fill->click();
		ok &= expect(bytes(editor.document().mesh()) == bytes(original) && !editor.document().canUndo(),
					 "invalid reference-pose triangulation fails without a document/history change");
		frame->setCurrentIndex(1);
		scope->setCurrentIndex(1);
		bool responsive = false, locked = false;
		QTimer heartbeat;
		heartbeat.setInterval(0);
		QObject::connect(&heartbeat, &QTimer::timeout, &editor, [&] {
			if (editor.operationBusy())
			{
				responsive = true;
				locked = !fill->isEnabled();
			}
		});
		heartbeat.start();
		fill->click();
		heartbeat.stop();
		ok &= expect(responsive && locked && !editor.operationBusy(),
					 "fill runs through the responsive worker with mutation controls locked");
		ok &= expect(editor.document().mesh().triangleCount == 12 && editor.document().selection().faces == QSet<int>{10, 11} &&
						 mode->currentIndex() == 0 && table->selectionModel()->selectedRows().size() == 2,
					 "displayed pose chooses a valid all-frame cap despite current-frame transform scope, selecting new faces");
		const auto filled = bytes(editor.document().mesh());
		ok &= expect(tests::settleModelViewport(*preview), "filled geometry and face selection reach the viewport");
		app.processEvents();
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
		{
			ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "fill UI uses the actual 2x device pixel ratio");
		}
		for (auto *area : editor.findChildren<QScrollArea *>())
		{
			if (!area->isAncestorOf(fill))
				continue;
			area->ensureWidgetVisible(fill);
			app.processEvents();
			const auto bounds = QRect(fill->mapTo(area->viewport(), QPoint()), fill->size());
			ok &= expect(area->horizontalScrollBar()->maximum() == 0 && bounds.left() >= 0 && bounds.right() < area->viewport()->width(),
						 "expanded fill control fits the scaled/RTL inspector without horizontal overflow");
		}
		fill->setFocus(Qt::OtherFocusReason);
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty())
		{
			QImage image(editor.size() * editor.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(editor.devicePixelRatioF());
			image.fill(Qt::transparent);
			editor.render(&image);
			ok &= expect(
				image.save(QDir(evidence).filePath(QString("boundary-fill-%1-%2x.png").arg(scenario).arg(editor.devicePixelRatioF()))),
				"save filled geometry widget render");
		}
		undo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == bytes(original) && editor.document().selection().edges == QSet<ModelEdge>{{4, 5}},
					 "UI undo restores source and boundary selection");
		redo->trigger();
		ok &= expect(bytes(editor.document().mesh()) == filled && editor.document().selection().faces == QSet<int>{10, 11},
					 "UI redo restores new faces for finishing");
		ModelEdit project;
		project.kind = ModelEditKind::ProjectUv;
		project.selection = editor.document().selection();
		project.frame = 1;
		ok &= expect(editor.applyEdit(project, &error) && editor.document().selection().faces == QSet<int>{10, 11},
					 "cap selection hands directly to shared UV projection workflow");
		ok &= expect(editor.setMesh(original, &error), "replace fixture and retire recovery after finishing");
		ModelEdit cancel;
		cancel.kind = ModelEditKind::FillBoundaryLoops;
		cancel.selection.edges = {{4, 5}};
		cancel.sourceFrame = 1;
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &=
			expect(!editor.applyEdit(cancel, &error) && bytes(editor.document().mesh()) == bytes(original) && !editor.document().canUndo(),
				   "cancelled fill leaves no geometry, history or recovery mutation");
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	return ok ? 0 : 1;
}
