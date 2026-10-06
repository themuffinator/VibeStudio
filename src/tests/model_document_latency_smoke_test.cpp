#include "app/model_document_work.h"
#include "app/model_recovery_writer.h"
#include "app/studio_theme.h"
#include "core/model_file_io.h"
#include "core/studio_settings.h"
#include "tests/model_scale_test_helpers.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << std::endl;
	}
	return value;
}
} // namespace

int main(int argc, char **argv)
{
	// Exercise production worker services with isolated files and semantic Qt
	// calls. No operating-system input or screen capture is used.
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
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("document-latency-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	const auto mesh = tests::maximumEditableGrid();
	bool ok = true;
	const auto ratio = app.primaryScreen()->devicePixelRatio();
	const auto requestedScale = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble();
	ok &= expect(requestedScale <= 0 || std::abs(ratio - requestedScale) < .01, "requested display scale is active");
	const QJsonObject scene{{"devicePixelRatio", ratio},
							{"theme", "dark"},
							{"vertices", mesh.vertexCount},
							{"triangles", mesh.triangleCount},
							{"frames", mesh.frameCount}};
	std::cout << "SCENE " << QJsonDocument(scene).toJson(QJsonDocument::Compact).constData() << std::endl;
	QString error;
	ModelDocument document;
	const auto measure = [&](const char *name, auto operation)
	{
		std::cout << "BEGIN " << name << std::endl;
		QElapsedTimer elapsed;
		elapsed.start();
		qint64 previous = 0, maximumGap = 0;
		int beats = 0;
		const auto beat = [&]
		{
			const auto now = elapsed.nsecsElapsed();
			maximumGap = std::max(maximumGap, now - previous);
			previous = now;
			++beats;
		};
		QTimer heartbeat;
		heartbeat.setTimerType(Qt::PreciseTimer);
		QObject::connect(&heartbeat, &QTimer::timeout, &app, beat);
		heartbeat.start(5);
		const bool success = operation();
		beat();
		heartbeat.stop();
		const QJsonObject record{{"stage", QString::fromLatin1(name)},
								 {"completedMs", elapsed.nsecsElapsed() / 1e6},
								 {"maxEventGapMs", maximumGap / 1e6},
								 {"heartbeats", beats},
								 {"success", success}};
		std::cout << QJsonDocument(record).toJson(QJsonDocument::Compact).constData() << std::endl;
		ok &= expect(success, qPrintable(QString::fromLatin1(name) + QStringLiteral(": ") + error));
		const int budget = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_DOCUMENT_GAP_MS");
		if (budget > 0)
		{
			ok &= expect(maximumGap / 1e6 <= budget, "configured document event-loop budget");
		}
	};
	const auto job = [&](ModelDocument &target, ModelDocumentJob operation, bool durable = false)
	{
		bool offThread = false;
		const bool success = runModelDocumentWork(
			nullptr, QStringLiteral("Maximum mesh document check"), &target,
			[&](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				offThread = QThread::currentThread() != app.thread();
				return operation(candidate, failure, control);
			},
			&error, durable);
		ok &= expect(offThread, "document operation executes on the production worker");
		return success;
	};
	measure("prepare-document",
			[&]
			{
				return job(document,
						   [&](auto &candidate, auto *failure, const auto &control) { return candidate.setMesh(mesh, failure, control); });
			});
	if (!ok)
	{
		return EXIT_FAILURE;
	}
	const auto revision = document.revisionFingerprint();
	const auto source = path("maximum.mesh.json");
	measure("save-source",
			[&]
			{
				return job(
					document, [&](auto &candidate, auto *failure, const auto &control)
					{ return candidate.save(source, false, failure, control); }, true);
			});
	const auto sourceHash = document.sourceFingerprint();
	ok &= expect(!sourceHash.isEmpty() && !document.isModified() && QFileInfo(source).size() <= modelDocumentMaxSourceBytes,
				 "maximum source publishes within the source limit and becomes clean");
	ModelDocument reopened;
	measure("reopen-source",
			[&]
			{
				return job(reopened,
						   [&](auto &candidate, auto *failure, const auto &control) { return candidate.load(source, failure, control); });
			});
	ok &= expect(reopened.revisionFingerprint() == revision && reopened.sourceFingerprint() == sourceHash && !reopened.isModified(),
				 "source round trip preserves every serialized mesh field");
	if (!ok)
	{
		return EXIT_FAILURE;
	}
	ModelSelection selection;
	selection.edges = document.surfaceTopology(0).allEdges;
	document.setSelection(selection);
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = document.mesh();
	snapshot.selection = selection;
	snapshot.frame = 15;
	snapshot.title = QStringLiteral("Maximum grid recovery");
	snapshot.sourcePath = source;
	snapshot.sourceSha256 = sourceHash;
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const auto recoveryDirectory = path("recovery");
	QString recoveryPath;
	ModelRecoveryWriter writer(recoveryDirectory);
	measure("write-recovery",
			[&]
			{
				QEventLoop loop;
				QTimer deadline;
				deadline.setSingleShot(true);
				QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
				writer.finished = [&](const QString &written, const QString &failure)
				{
					recoveryPath = written;
					error = failure;
					loop.quit();
				};
				writer.checkpoint(id, 1, snapshot);
				deadline.start(120000);
				loop.exec();
				writer.finished = {};
				return !writer.busy() && !recoveryPath.isEmpty() && error.isEmpty();
			});
	ModelDocument recovered;
	measure("restore-recovery",
			[&]
			{
				return job(recovered,
						   [&](auto &candidate, auto *failure, const auto &control)
						   {
							   ModelRecoverySnapshot restored;
							   const auto record = inspectModelRecovery(recoveryPath, &restored, control.cancelled);
							   if (!record.isValid())
							   {
								   *failure = record.error;
								   return false;
							   }
							   return restored.frame == 15 && restored.sourcePath == source && restored.sourceSha256 == sourceHash &&
									  candidate.restoreDraft(restored.mesh, restored.selection, failure, control);
						   });
			});
	ok &= expect(recovered.revisionFingerprint() == revision && recovered.selection() == selection && recovered.path().isEmpty() &&
					 recovered.isModified(),
				 "maximum recovery retains dense edge selection and all poses without rebinding the source");
	const auto objPath = path("maximum-frame.obj");
	measure("export-obj-frame",
			[&]
			{
				return job(
					document,
					[&](auto &candidate, auto *failure, const auto &control)
					{
						// Match the editable-mesh export service used by the editor
						// and `model build`, including destination review.
						const auto target = inspectModelWriteTarget(objPath, control);
						if (!target.isValid())
						{
							*failure = target.error;
							return false;
						}
						const auto bytes = exportEditableModel(candidate.mesh(), QStringLiteral("obj"), 15, failure, control);
						return !bytes.isEmpty() && writeModelFile(target, bytes, failure, control);
					},
					true);
			});
	measure("verify-obj-frame",
			[&]
			{
				return job(document,
						   [&](auto &, auto *failure, const auto &control)
						   {
							   QByteArray bytes;
							   ModelMesh decoded;
							   if (!readModelFile(objPath, &bytes, failure, control) ||
								   !importEditableModel(objPath, bytes, &decoded, failure, nullptr, control))
							   {
								   return false;
							   }
							   if (decoded.vertexCount != mesh.vertexCount || decoded.triangleCount != mesh.triangleCount ||
								   decoded.frameCount != 1)
							   {
								   return false;
							   }
							   for (const auto &surface : decoded.surfaces)
							   {
								   for (const auto &position : surface.frames[0].positions)
								   {
									   if (position.z != 15)
									   {
										   return false;
									   }
								   }
							   }
							   return true;
						   });
			});
	bool forcedCancel = false;
	measure("cancel-source-serialization",
			[&]
			{
				return !job(document,
							[&](auto &candidate, auto *failure, const auto &control)
							{
								ModelWorkControl cancellation;
								cancellation.cancelled = [&] { return forcedCancel || (control.cancelled && control.cancelled()); };
								cancellation.progress = [&](ModelWorkPhase phase, qint64 completed, qint64 total)
								{
									if (control.progress)
									{
										control.progress(phase, completed, total);
									}
									forcedCancel |= phase == ModelWorkPhase::Serializing && completed >= 4096;
								};
								return candidate.save(source, false, failure, cancellation);
							});
			});
	ok &= expect(forcedCancel && !error.isEmpty() && document.revisionFingerprint() == revision &&
					 document.sourceFingerprint() == sourceHash && document.selection() == selection && !document.isModified(),
				 "cancelled maximum source replacement preserves document identity and selection");
	ok &= expect(inspectModelWriteTarget(source).sha256 == sourceHash, "export, recovery and cancellation preserve the saved source bytes");
	const QJsonObject sizes{{"sourceBytes", QFileInfo(source).size()}, {"recoveryBytes", QFileInfo(recoveryPath).size()},
							{"objBytes", QFileInfo(objPath).size()},   {"vertices", mesh.vertexCount},
							{"triangles", mesh.triangleCount},		   {"frames", mesh.frameCount},
							{"selectedEdges", selection.edges.size()}};
	std::cout << "SIZES " << QJsonDocument(sizes).toJson(QJsonDocument::Compact).constData() << std::endl;
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
