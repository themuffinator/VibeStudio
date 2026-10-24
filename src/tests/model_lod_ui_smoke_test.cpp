// The Mesh Editor's Detail Levels…: the dialog's keyboard-reachable, named
// controls, a review on the worker with the per-surface table and findings, a
// preview of a level that leaves the document unchanged, Store in Source as
// one undo step, Export of the reviewed set, a hand-made level without the
// base's frames refused, and Stage in Package staging the stored levels with
// the model. Also under right-to-left layout and 200% text.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_lod_dialog.h"
#include "app/studio_theme.h"
#include "core/model_lod.h"
#include "core/studio_settings.h"
#include "tests/model_targets_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <functional>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
void onNextLodDialog(QObject *context, std::function<void(ModelLodDialog *)> handle)
{
	auto *timer = new QTimer(context);
	timer->setInterval(20);
	QObject::connect(timer, &QTimer::timeout, context,
					 [timer, handle]
					 {
						 if (auto *dialog = dynamic_cast<ModelLodDialog *>(QApplication::activeModalWidget()))
						 {
							 timer->stop();
							 timer->deleteLater();
							 handle(dialog);
						 }
					 });
	timer->start();
}
ModelMesh prop()
{
	auto mesh = targetMesh({targetGridSurface(QStringLiteral("body"), 12, 12), targetGridSurface(QStringLiteral("cap"), 4, 4, 8)});
	mesh.frames.append(mesh.frames[0]);
	mesh.frames[1].name = QStringLiteral("lift");
	for (auto &surface : mesh.surfaces)
	{
		auto pose = surface.frames[0];
		for (auto &p : pose.positions)
			p.z += 2;
		surface.frames.append(pose);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void checkDialog(ModelEditorDialog *editor, const QString &directory, bool full)
{
	QString error;
	const auto mesh = prop();
	expect(editor->setMesh(mesh, &error), "open the prop");
	const auto revision = editor->document().revisionFingerprint();
	bool named = false, reviewed = false, previewKept = false, stored = false, exported = false, refused = false, tabLeaves = false, leftNamed = false;
	int rows = 0, choices = 0;
	const auto base = QDir(directory).filePath(QStringLiteral("lamp.md3"));
	onNextLodDialog(editor, [&](ModelLodDialog *dialog)
					{
						auto *count = dialog->findChild<QSpinBox *>(QStringLiteral("meshLodCount"));
						auto *mode = dialog->findChild<QComboBox *>(QStringLiteral("meshLodMode-1"));
						auto *review = dialog->findChild<QPushButton *>(QStringLiteral("meshLodReview"));
						auto *store = dialog->findChild<QPushButton *>(QStringLiteral("meshLodStore"));
						named = count && mode && review && store && !count->accessibleName().isEmpty() && !mode->accessibleName().isEmpty() &&
								!review->accessibleDescription().isEmpty() && count->focusPolicy() != Qt::NoFocus && count->maximum() == 2 &&
								count->value() == 2 && !store->isEnabled();
						reviewed = dialog->review() && dialog->reviewed() && dialog->levels().size() == 2 && store->isEnabled();
						if (auto *table = dialog->findChild<QTableWidget *>(QStringLiteral("meshLodSurfaces")))
						{
							rows = table->rowCount();
							// Tab leaves the table for the next control.
							table->setFocus(Qt::TabFocusReason);
							QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
							QApplication::sendEvent(table, &tab);
							tabLeaves = !table->tabKeyNavigation() && (!table->hasFocus() || QApplication::focusWidget() == nullptr);
						}
						if (auto *preview = dialog->findChild<QComboBox *>(QStringLiteral("meshLodPreview")))
						{
							choices = preview->count();
							preview->setCurrentIndex(2);
						}
						previewKept = editor->document().revisionFingerprint() == revision;
						if (full)
						{
							// A hand-made level without the base's second frame is refused.
							auto still = mesh;
							still.frames.removeLast();
							for (auto &surface : still.surfaces)
								surface.frames.removeLast();
							updateEditableModelMetadata(&still);
							QString problem;
							const auto stillPath = QDir(directory).filePath(QStringLiteral("still.md3"));
							QFile file(stillPath);
							const auto bytes = exportEditableModel(still, QStringLiteral("md3"), 0, &problem);
							const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
							file.close();
							QVector<ModelLodRecipeLevel> manual{modelLodGeneratedRecipe(1, 0.5).first()};
							manual[0].generated = false;
							manual[0].source = stillPath;
							dialog->setRecipe(manual);
							refused = written && !dialog->review() && !dialog->reviewed() &&
									  dialog->findChild<QPlainTextEdit *>(QStringLiteral("meshLodFindings"))->toPlainText().contains(QStringLiteral("frames"));
							dialog->setRecipe(modelLodGeneratedRecipe(2, 0.5));
							dialog->review();
						}
						stored = dialog->store(&error);
						exported = dialog->exportTo(base, &error);
						// A one-level set written where an older second level sits
						// names the file it leaves, which the game still loads.
						const auto other = QDir(directory).filePath(QStringLiteral("other/lamp.md3"));
						QFile level2(modelLodPath(base, 2));
						const auto levelBytes = level2.open(QIODevice::ReadOnly) ? level2.readAll() : QByteArray();
						level2.close();
						QDir().mkpath(QFileInfo(other).absolutePath());
						QFile left(modelLodPath(other, 2));
						const bool placed = !levelBytes.isEmpty() && left.open(QIODevice::WriteOnly) && left.write(levelBytes) == levelBytes.size();
						left.close();
						dialog->setRecipe(modelLodGeneratedRecipe(1, 0.5));
						leftNamed = placed && dialog->review() && dialog->exportTo(other, &error) && QFileInfo::exists(modelLodPath(other, 1)) &&
									editor->lastExportReport().notes.join(QLatin1Char('\n')).contains(QStringLiteral("lamp_2.md3")) &&
									editor->findChild<QLabel *>(QStringLiteral("meshStatus"))->text().contains(QStringLiteral("stay as they were"));
						dialog->reject();
					});
	expect(editor->reviewDetailLevels(&error), "Detail Levels… opens");
	expect(named, "the dialog's controls are named, described and reachable, starting at the two levels Quake III loads");
	expect(reviewed && rows == 2 && choices == 3, "a review builds both levels and lists each surface");
	expect(tabLeaves, "Tab moves on from the per-surface table instead of between its cells");
	expect(previewKept, "showing a level leaves the document unchanged");
	expect(!full || refused, "a hand-made level without the base's frames is refused by the review");
	expect(stored && editor->document().mesh().detailLevels.size() == 2 && editor->document().canUndo(), "Store in Source keeps the levels in the source");
	expect(exported && QFileInfo::exists(base) && QFileInfo::exists(modelLodPath(base, 1)) && QFileInfo::exists(modelLodPath(base, 2)),
		   "Export writes the reviewed set");
	expect(leftNamed, "a one-level export names the older second level it leaves beside the model");
	// Stage in Package stages the stored levels with the model in one step.
	QVector<ModelStagedFile> staged;
	editor->handoff = [](const QByteArray &, const QString &, bool, const LevelMapVec3 &, bool, QString *) { return true; };
	editor->handoffFiles = [&staged](const QVector<ModelStagedFile> &files, bool, const LevelMapVec3 &, bool, QString *)
	{
		staged = files;
		return true;
	};
	editor->context = [] { return ModelDesignContext{QStringLiteral("package.pk3"), {}, true, false, false}; };
	editor->refreshContext();
	editor->findChild<QLineEdit *>(QStringLiteral("meshVirtualPath"))->setText(QStringLiteral("models/props/lamp.md3"));
	editor->refreshContext();
	editor->findChild<QAction *>(QStringLiteral("stageMesh"))->trigger();
	expect(staged.size() == 3 && staged[0].path == QStringLiteral("models/props/lamp.md3") && staged[2].path == QStringLiteral("models/props/lamp_2.md3") &&
		   staged[1].bytes.startsWith("IDP3"),
		   "Stage in Package stages the model and both stored levels together");
	// A target that loads no detail levels still stages the model, and says why
	// its stored levels stay behind.
	QByteArray single;
	staged.clear();
	editor->handoff = [&single](const QByteArray &bytes, const QString &, bool, const LevelMapVec3 &, bool, QString *)
	{
		single = bytes;
		return true;
	};
	expect(editor->setDeliveryTarget(QStringLiteral("md3"), QStringLiteral("gzdoom")), "choose GZDoom for MD3");
	editor->findChild<QAction *>(QStringLiteral("stageMesh"))->trigger();
	expect(staged.isEmpty() && single.startsWith("IDP3") &&
			   editor->lastExportReport().notes.join(QLatin1Char('\n')).contains(QStringLiteral("loads none beside the model")),
		   "on a target without detail levels the model alone is staged, with a note");
	expect(editor->setDeliveryTarget(QStringLiteral("md3"), QStringLiteral("quake3")), "choose Quake III again");
	editor->handoffFiles = {};
	editor->context = {};
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(editor->document().mesh().detailLevels.isEmpty(), "one undo removes the stored levels");
	// Staging the model alone reviews the detail files the package already
	// holds beside it: an old level without the second frame is named.
	auto still = mesh;
	still.frames.removeLast();
	for (auto &surface : still.surfaces)
		surface.frames.removeLast();
	updateEditableModelMetadata(&still);
	const auto oldLevel = exportEditableModel(still, QStringLiteral("md3"), 0, &error);
	QStringList asked;
	editor->readPackageFiles = [&asked, oldLevel](const QStringList &paths)
	{
		asked = paths;
		return QHash<QString, QByteArray>{{QStringLiteral("Models/Props/Lamp_1.md3"), oldLevel}};
	};
	editor->context = [] { return ModelDesignContext{QStringLiteral("package.pk3"), {}, true, false, false}; };
	editor->refreshContext();
	single.clear();
	editor->findChild<QAction *>(QStringLiteral("stageMesh"))->trigger();
	const auto status = editor->findChild<QLabel *>(QStringLiteral("meshStatus"))->text();
	expect(single.startsWith("IDP3") && asked.contains(QStringLiteral("models/props/lamp_1.md3")) && status.contains(QStringLiteral("wrongly")) &&
			   editor->lastExportReport().notes.join(QLatin1Char('\n')).contains(QStringLiteral("lamp_1.md3")),
		   "the package's old detail level is named in the status and the export report");
	editor->handoff = {};
	editor->readPackageFiles = {};
	editor->context = {};
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-lod-ui-smoke"); skip >= 0)
		return skip;
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("lod-ui-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		// The second pass mirrors the layout and doubles the text.
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		const auto directory = QDir(temporary.path()).filePath(QStringLiteral("pass%1").arg(scenario));
		QDir().mkpath(directory);
		auto *editor = new ModelEditorDialog;
		editor->resize(1400, 900);
		editor->show();
		QApplication::processEvents();
		checkDialog(editor, directory, scenario == 0);
		delete editor;
	}
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "model detail level UI checks passed\n";
	return EXIT_SUCCESS;
}
