#include "app/model_document_work.h"
#include "core/model_design.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFont>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <atomic>
#include <cstdlib>
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
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor")
		{
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 2, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-work-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	ModelDesign design;
	design.parts << ModelDesignPart{};
	ModelDocument document;
	QString error;
	bool ok = expect(document.setMesh(buildModelDesignMesh(design), &error), "prepare worker document");
	const auto before = document.revisionFingerprint();
	ModelEdit edit;
	edit.selection.faces = {0};
	edit.translation = {0, 0, 1};
	std::function<void()> cancel;
	std::atomic_bool offThread = false;
	int heartbeats = 0;
	bool usedCancel = false, timedOut = false;
	int evidenceScale = 100;
	QTimer heartbeat, deadline;
	QObject::connect(&heartbeat, &QTimer::timeout, &app,
					 [&]
					 {
						 ++heartbeats;
						 for (auto *widget : QApplication::topLevelWidgets())
						 {
							 if (widget->objectName() != QStringLiteral("meshOperationDialog") || !widget->isVisible())
							 {
								 continue;
							 }
							 auto *button = widget->findChild<QPushButton *>(QStringLiteral("cancelMeshOperation"));
							 auto *status = widget->findChild<QLabel *>(QStringLiteral("meshOperationStatus"));
							 if (button && button->isEnabled())
							 {
								 usedCancel = status && !status->accessibleName().isEmpty() && status->accessibleName() == status->text() &&
											  widget->accessibleDescription() == status->text() && button->focusPolicy() != Qt::NoFocus;
								 const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
								 if (!evidence.isEmpty())
								 {
									 QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
									 image.fill(Qt::transparent);
									 widget->render(&image);
									 ok &= expect(
										 image.save(QDir(evidence).filePath(QStringLiteral("mesh-operation-%1.png").arg(evidenceScale))),
										 "render operation progress evidence without desktop capture");
								 }
								 button->click();
							 }
						 }
					 });
	deadline.setSingleShot(true);
	QObject::connect(&deadline, &QTimer::timeout, &app,
					 [&]
					 {
						 timedOut = true;
						 if (cancel)
						 {
							 cancel();
						 }
					 });
	const auto *uiThread = app.thread();
	const auto cancelledRun = [&]
	{
		return runModelDocumentWork(
			nullptr, QStringLiteral("Edit mesh"), &document,
			[&](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				offThread = QThread::currentThread() != uiThread;
				if (!candidate.edit(edit, failure))
				{
					return false;
				}
				// Hold a prepared edit until the real progress dialog cancels it. The
				// GUI timer must continue running while this worker owns the candidate.
				while (modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, 0, failure))
				{
					QThread::msleep(2);
				}
				return false;
			},
			&error, false, &cancel);
	};
	Expansion expansion;
	const auto originalFont = app.font();
	const auto originalPalette = app.palette();
	for (int scale : {100, 200})
	{
		evidenceScale = scale;
		if (scale == 200)
		{
			app.installTranslator(&expansion);
			app.setLayoutDirection(Qt::RightToLeft);
			auto font = originalFont;
			font.setPointSizeF(font.pointSizeF() * 2);
			app.setFont(font);
			auto palette = originalPalette;
			palette.setColor(QPalette::Window, Qt::black);
			palette.setColor(QPalette::WindowText, Qt::white);
			palette.setColor(QPalette::Button, Qt::black);
			palette.setColor(QPalette::ButtonText, Qt::white);
			palette.setColor(QPalette::Highlight, Qt::white);
			palette.setColor(QPalette::HighlightedText, Qt::black);
			app.setPalette(palette);
		}
		usedCancel = timedOut = false;
		heartbeats = 0;
		heartbeat.start(5);
		deadline.start(10000);
		const bool accepted = cancelledRun();
		heartbeat.stop();
		deadline.stop();
		ok &= expect(!accepted && !timedOut && usedCancel && offThread && heartbeats > 2 && !cancel &&
						 document.revisionFingerprint() == before && !document.canUndo(),
					 "progress cancellation runs off-thread, services the UI, and discards the prepared edit at both text scales");
	}
	app.removeTranslator(&expansion);
	app.setFont(originalFont);
	app.setPalette(originalPalette);
	app.setLayoutDirection(Qt::LeftToRight);
	ok &= expect(!runModelDocumentWork(
					 nullptr, QStringLiteral("Fail edit"), &document,
					 [&](ModelDocument &candidate, QString *failure, const ModelWorkControl &)
					 {
						 candidate.edit(edit, failure);
						 *failure = QStringLiteral("Fixture failure");
						 return false;
					 },
					 &error) &&
					 error == QStringLiteral("Fixture failure") && document.revisionFingerprint() == before,
				 "failed background edits preserve the caller's document and diagnostic");
	ok &= expect(runModelDocumentWork(
					 nullptr, QStringLiteral("Edit mesh"), &document,
					 [&](ModelDocument &candidate, QString *failure, const ModelWorkControl &) { return candidate.edit(edit, failure); },
					 &error) &&
					 document.isModified() && document.canUndo(),
				 "successful worker edits publish ordinary document history");
	const auto path = QDir(temporary.path()).filePath(QStringLiteral("worker.mesh.json"));
	ok &= expect(runModelDocumentWork(
					 nullptr, QStringLiteral("Save mesh"), &document,
					 [&](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
					 {
						 auto late = control;
						 late.progress = [&](ModelWorkPhase phase, qint64 done, qint64 total)
						 {
							 control.progress(phase, done, total);
							 if (phase == ModelWorkPhase::Committing && done == 1 && total == 1 && cancel)
							 {
								 cancel();
							 }
						 };
						 return candidate.save(path, false, failure, late);
					 },
					 &error, true, &cancel) &&
					 !document.isModified() && document.path() == path && QFileInfo::exists(path),
				 "a committed background save is adopted even if cancellation arrives after publication");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
