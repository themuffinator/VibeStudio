// The Mesh Editor's skeletal export flow: the MD5 animation chooser (clip,
// whole frame rate and review), every clip as md5anims, the Export Report
// that keeps each export's omissions and notes one keyboard-reachable click
// away, and staging an md5anim through the same chooser.
#include "app/model_editor_dialog.h"
#include "app/model_md5_export_dialog.h"
#include "core/model_skeletal_export.h"
#include "core/studio_settings.h"
#include "tests/model_skeletal_export_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
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
bool accessible(const QWidget *widget) { return widget && !widget->accessibleName().isEmpty(); }
// True when `widget` or a child it forwards focus to holds the window's focus.
bool focused(const QWidget *window, const QWidget *widget)
{
	const auto *current = window->focusWidget();
	return current && (current == widget || widget->isAncestorOf(current));
}
// The next visible widget Tab reaches after `widget`.
QWidget *nextTabStop(QWidget *widget)
{
	for (auto *next = widget->nextInFocusChain(); next && next != widget; next = next->nextInFocusChain())
	{
		if (next->isVisible() && next->isEnabled() && (next->focusPolicy() & Qt::TabFocus) && !widget->isAncestorOf(next))
		{
			return next;
		}
	}
	return nullptr;
}

void checkChooser()
{
	auto mesh = exportArmFixture();
	mesh.skeleton.clips[1].framesPerSecond = 12.5;
	ModelMd5ExportDialog chooser(mesh, 1, false);
	chooser.show();
	chooser.activateWindow();
	QApplication::processEvents();
	auto *clip = chooser.findChild<QComboBox *>(QStringLiteral("meshMd5Clip"));
	auto *rate = chooser.findChild<QSpinBox *>(QStringLiteral("meshMd5FrameRate"));
	auto *review = chooser.findChild<QLabel *>(QStringLiteral("meshMd5ExportReview"));
	auto *accept = chooser.findChild<QPushButton *>(QStringLiteral("meshMd5ExportAccept"));
	if (!expect(clip && rate && review && accept, "the chooser has a clip, a rate, a review and a continue button"))
	{
		return;
	}
	expect(accessible(clip) && accessible(rate) && accessible(review) && accessible(accept) && !clip->accessibleDescription().isEmpty() &&
			   !rate->accessibleDescription().isEmpty(),
		   "chooser controls have accessible names and descriptions");
	expect(clip->count() == 2 && clip->currentIndex() == 1 && clip->isVisible() && focused(&chooser, clip), "the preselected clip has keyboard focus");
	expect(rate->value() == 13 && accept->isEnabled() && review->text().contains(QStringLiteral("12.5")) &&
			   review->text().contains(QStringLiteral("faster")),
		   "a fractional rate starts at the nearest whole rate and the review explains the speed change");
	rate->setValue(0);
	expect(!accept->isEnabled() && review->text().contains(QStringLiteral("whole number")), "keeping a fractional rate cannot continue");
	clip->setCurrentIndex(0);
	// The rate is a plural message, "24 frames per second" with the English catalogue.
	expect(accept->isEnabled() && review->text().contains(QStringLiteral("24 frame")) && review->text().contains(QStringLiteral("per second")),
		   "a whole-rate clip keeps its own rate");
	rate->setValue(12);
	auto options = chooser.options();
	expect(options.clip == 0 && options.md5FrameRate == 12, "the chooser returns the clip and the explicit rate");
	// Keyboard path: the clip field leads to the rate field.
	auto *afterClip = nextTabStop(clip);
	expect(afterClip && (afterClip == rate || rate->isAncestorOf(afterClip)), "Tab moves from the clip to the frame rate");

	ModelMd5ExportDialog all(mesh, 0, true);
	all.show();
	all.activateWindow();
	QApplication::processEvents();
	auto *allClip = all.findChild<QComboBox *>(QStringLiteral("meshMd5Clip"));
	auto *allRate = all.findChild<QSpinBox *>(QStringLiteral("meshMd5FrameRate"));
	auto *allAccept = all.findChild<QPushButton *>(QStringLiteral("meshMd5ExportAccept"));
	expect(allClip && !allClip->isVisible() && allRate && focused(&all, allRate) && allAccept && !allAccept->isEnabled(),
		   "every clip at once hides the chooser and waits for a rate for the fractional clip");
	allRate->setValue(24);
	expect(allAccept->isEnabled() && all.options().clip < 0 && all.options().md5FrameRate == 24, "one rate for every clip can continue");
}

void checkEditor(const QString &directory)
{
	QString error;
	auto *editor = new ModelEditorDialog;
	editor->resize(1400, 900);
	editor->show();
	QApplication::processEvents();
	if (!expect(editor->setMesh(exportArmFixture(), &error), "the editor opens the arm"))
	{
		std::cerr << error.toStdString() << '\n';
		delete editor;
		return;
	}
	auto *other = editor->findChild<QAction *>(QStringLiteral("exportMeshOtherFormat"));
	bool menuHasAll = false;
	for (auto *entry : other ? other->menu()->actions() : QList<QAction *>{})
		menuHasAll |= entry->objectName() == QStringLiteral("exportMeshMd5AnimAll") && !entry->toolTip().isEmpty();
	expect(menuHasAll, "Export Other Format offers every clip as md5anims");
	expect(editor->findChild<QPushButton *>(QStringLiteral("exportMeshMd5AnimAll")) != nullptr, "the Skeleton page offers every clip too");
	auto *button = editor->findChild<QPushButton *>(QStringLiteral("meshExportReport"));
	expect(button && !button->isVisible(), "the Export Report button waits for a report");

	// One clip at an explicit rate.
	const auto path = QDir(directory).filePath(QStringLiteral("wave.md5anim"));
	ModelExportOptions options;
	options.clip = 1;
	options.md5FrameRate = 15;
	expect(editor->exportToPath(path, QStringLiteral("md5anim"), options, &error) && QFileInfo::exists(path), "the editor writes the chosen clip");
	QFile written(path);
	const auto decoded = written.open(QIODevice::ReadOnly) ? decodeModelMesh(path, written.readAll()) : ModelMesh{};
	expect(decoded.error.isEmpty() && decoded.skeleton.clips.size() == 1 && decoded.skeleton.clips[0].frames.size() == 4 &&
			   std::abs(decoded.skeleton.clips[0].framesPerSecond - 15) < 1e-9,
		   "the written md5anim is the chosen clip at the chosen rate");
	const auto &report = editor->lastExportReport();
	auto *status = editor->findChild<QLabel *>(QStringLiteral("meshStatus"));
	expect(report.clip == 1 && report.frameRate == 15 && status && status->text().contains(QStringLiteral("Export Report")),
		   "the status summarises the export and points at the report");
	expect(button && button->isVisible() && button->isEnabled() && accessible(button) && button->focusPolicy() != Qt::NoFocus,
		   "the Export Report button appears, named and keyboard reachable");

	// The md5mesh leaves the hand tag out, which the report counts.
	const auto meshPath = QDir(directory).filePath(QStringLiteral("arm.md5mesh"));
	expect(editor->exportToPath(meshPath, QStringLiteral("md5mesh"), ModelExportOptions{}, &error) && !report.omissions.isEmpty() &&
			   button->text().contains(QStringLiteral("left out")) && status->text().contains(QStringLiteral("tag_hand")),
		   "omissions lead the status and label the report button");
	QString shown;
	QTimer::singleShot(0, editor, [&]
					   {
						   if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
						   {
							   if (auto *text = dialog->findChild<QPlainTextEdit *>(QStringLiteral("meshExportReportText")))
								   shown = text->toPlainText();
							   dialog->reject();
						   }
					   });
	button->click();
	expect(shown.contains(QStringLiteral("Left out or changed")) && shown.contains(QStringLiteral("tag_hand")) && shown.contains(meshPath),
		   "the report lists the omissions and the written file");

	// Every clip at once, refusing to replace files unless asked.
	const auto folder = QDir(directory).filePath(QStringLiteral("anims"));
	expect(editor->exportMd5Clips(folder, ModelExportOptions{}, false, &error) && QFileInfo::exists(QDir(folder).filePath(QStringLiteral("idle.md5anim"))) &&
			   QFileInfo::exists(QDir(folder).filePath(QStringLiteral("wave.md5anim"))) &&
			   status->text().contains(QStringLiteral("2 md5anim")),
		   "every clip is written as its own md5anim");
	error.clear();
	expect(!editor->exportMd5Clips(folder, ModelExportOptions{}, false, &error) && error.contains(QStringLiteral("already exists")),
		   "existing clip files are not replaced without asking");
	expect(editor->exportMd5Clips(folder, ModelExportOptions{}, true, &error), "replacing them is explicit");

	// Staging an md5anim confirms its clip and rate in the same chooser,
	// starting from the clip the package path names.
	QByteArray staged;
	editor->context = []
	{
		ModelDesignContext context;
		context.packagePath = QStringLiteral("fixture.pk3");
		context.canStage = true;
		return context;
	};
	editor->handoff = [&staged](const QByteArray &bytes, const QString &, bool, const LevelMapVec3 &, bool, QString *)
	{
		staged = bytes;
		return true;
	};
	auto *packagePath = editor->findChild<QLineEdit *>(QStringLiteral("meshVirtualPath"));
	auto *stage = editor->findChild<QAction *>(QStringLiteral("stageMesh"));
	if (!expect(packagePath && stage, "the Export page has a package path and Stage in Package"))
	{
		delete editor;
		return;
	}
	packagePath->setText(QStringLiteral("models/test/wave.md5anim"));
	editor->refreshContext();
	int preselected = -1;
	bool chooserShown = false;
	auto *poll = new QTimer(editor);
	poll->setInterval(20);
	QObject::connect(poll, &QTimer::timeout, editor,
					 [&, poll]
					 {
						 auto *chooser = qobject_cast<QDialog *>(QApplication::activeModalWidget());
						 if (!chooser || !chooser->findChild<QComboBox *>(QStringLiteral("meshMd5Clip")))
							 return;
						 poll->stop();
						 chooserShown = true;
						 if (auto *clip = chooser->findChild<QComboBox *>(QStringLiteral("meshMd5Clip")))
							 preselected = clip->currentData().toInt();
						 if (auto *accept = chooser->findChild<QPushButton *>(QStringLiteral("meshMd5ExportAccept")))
							 accept->click();
					 });
	poll->start();
	stage->trigger();
	poll->stop();
	const auto stagedAnim = decodeModelMesh(QStringLiteral("rt/staged.md5anim"), staged);
	expect(chooserShown && preselected == 1 && stagedAnim.error.isEmpty() && stagedAnim.skeleton.clips.size() == 1 &&
			   stagedAnim.skeleton.clips[0].frames.size() == 4,
		   "staging wave.md5anim preselects the clip it names and stages that clip");
	delete editor;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	checkChooser();
	// The editor draws in 3D: skip its checks where no renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-skeletal-export-ui-smoke"); skip >= 0)
	{
		return failures > 0 ? EXIT_FAILURE : skip;
	}
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("skeletal-export-ui-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	checkEditor(temporary.path());
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "model skeletal export UI checks passed\n";
	return EXIT_SUCCESS;
}
