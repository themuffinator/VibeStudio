#include "core/model_file_io.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>

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
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-io-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	const auto nested = QDir(temporary.path()).filePath(QStringLiteral("nested/output.mesh.json"));
	const auto fresh = inspectModelWriteTarget(nested);
	ok &= expect(fresh.isValid() && !fresh.existed && !QFileInfo::exists(QFileInfo(nested).absolutePath()),
				 "reviewing a new destination creates nothing");
	const QByteArray first(700000, 'a'), second(700000, 'b');
	ok &= expect(writeModelFile(fresh, first, &error) && read(nested) == first,
				 "new output publishes complete bytes with missing directories");
	ok &= expect(!writeModelFile(fresh, second, &error) && read(nested) == first,
				 "a stale absent-file review cannot replace an existing output");
	const auto existing = inspectModelWriteTarget(nested);
	ok &= expect(existing.isValid() && existing.existed && existing.sha256.size() == 32,
				 "existing output review records its content fingerprint");
	bool cancelled = false;
	ModelWorkControl cancel;
	cancel.cancelled = [&] { return cancelled; };
	cancel.progress = [&](ModelWorkPhase phase, qint64 done, qint64)
	{
		if (phase == ModelWorkPhase::Writing && done > 0)
		{
			cancelled = true;
		}
	};
	ok &= expect(!writeModelFile(existing, second, &error, cancel) && read(nested) == first && error.contains(QStringLiteral("cancelled")),
				 "cancelling a partial replacement keeps the complete original");
	const auto cancelledNewPath = QDir(temporary.path()).filePath(QStringLiteral("cancelled.md3"));
	cancelled = false;
	ok &= expect(!writeModelFile(inspectModelWriteTarget(cancelledNewPath), second, &error, cancel) && !QFileInfo::exists(cancelledNewPath),
				 "cancelling a new output leaves no partial destination");
	const QByteArray external("External edit");
	bool changed = false;
	ModelWorkControl change;
	change.progress = [&](ModelWorkPhase phase, qint64 done, qint64)
	{
		if (!changed && phase == ModelWorkPhase::Writing && done > 0)
		{
			changed = write(nested, external);
		}
	};
	ok &= expect(!writeModelFile(existing, second, &error, change) && changed && read(nested) == external,
				 "an external edit during staging is detected before replacement");
	const auto appeared = QDir(temporary.path()).filePath(QStringLiteral("appeared.obj"));
	const auto absent = inspectModelWriteTarget(appeared);
	changed = false;
	change.progress = [&](ModelWorkPhase phase, qint64, qint64)
	{
		if (!changed && phase == ModelWorkPhase::Committing)
		{
			changed = write(appeared, external);
		}
	};
	ok &= expect(!writeModelFile(absent, second, &error, change) && changed && read(appeared) == external,
				 "a destination created by another writer before publication is protected");
	{
		QLockFile lock(existing.resolvedPath + QStringLiteral(".vibestudio-model.lock"));
		ok &= expect(lock.tryLock(0), "reserve a competing writer fixture");
		ok &= expect(!writeModelFile(inspectModelWriteTarget(nested), second, &error) && read(nested) == external,
					 "cooperating concurrent writers fail without modifying the destination");
	}
	bool lateCancellation = false;
	ModelWorkControl late;
	late.cancelled = [&] { return lateCancellation; };
	late.progress = [&](ModelWorkPhase phase, qint64 done, qint64 total)
	{
		if (phase == ModelWorkPhase::Committing && done == total)
		{
			lateCancellation = true;
		}
	};
	ok &= expect(writeModelFile(inspectModelWriteTarget(nested), second, &error, late) && lateCancellation && read(nested) == second,
				 "cancellation after successful publication cannot report the committed save as failed");
	QByteArray output("unchanged");
	ModelWorkControl cancelRead;
	cancelled = false;
	cancelRead.cancelled = [&] { return cancelled; };
	cancelRead.progress = [&](ModelWorkPhase phase, qint64 done, qint64)
	{
		if (phase == ModelWorkPhase::Reading && done > 0)
		{
			cancelled = true;
		}
	};
	ok &= expect(!readModelFile(nested, &output, &error, cancelRead) && output == QByteArray("unchanged"),
				 "cancelled reads preserve their caller's output");
	ok &= expect(!inspectModelWriteTarget(temporary.path()).isValid(), "directory destinations are refused");
	ok &= expect(modelPathsReferToSameFile(nested, QDir(QFileInfo(nested).absolutePath()).filePath(QStringLiteral("./output.mesh.json"))),
				 "source protection resolves alternate spellings");
	const auto leftovers =
		QDir(temporary.path()).entryList({QStringLiteral("*.lock"), QStringLiteral(".vibestudio-model-*")}, QDir::Files | QDir::Hidden);
	const auto nestedLeftovers =
		QDir(QFileInfo(nested).absolutePath())
			.entryList({QStringLiteral("*.lock"), QStringLiteral(".vibestudio-model-*")}, QDir::Files | QDir::Hidden);
	ok &= expect(leftovers.isEmpty() && nestedLeftovers.isEmpty(), "writes clean up only their own staging files and locks");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
