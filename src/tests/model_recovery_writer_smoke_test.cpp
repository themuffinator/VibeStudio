#include "app/model_recovery_writer.h"
#include "core/model_design.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

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
bool settle(ModelRecoveryWriter &writer)
{
	QEventLoop loop;
	QTimer deadline, check;
	deadline.setSingleShot(true);
	QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(&check, &QTimer::timeout, &loop,
					 [&]
					 {
						 if (!writer.busy())
						 {
							 loop.quit();
						 }
					 });
	deadline.start(20000);
	check.start(5);
	if (writer.busy())
	{
		loop.exec();
	}
	return !writer.busy();
}
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-recovery-worker-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto directory = QDir(temporary.path()).filePath(QStringLiteral("copies"));
	ModelDesign design;
	design.parts << ModelDesignPart{};
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = buildModelDesignMesh(design);
	snapshot.title = QStringLiteral("First revision");
	bool ok = true;
	const auto id = freshId();
	QString error;
	{
		ModelRecoveryWriter writer(directory);
		writer.finished = [&](const QString &, const QString &failure) { error = failure; };
		writer.checkpoint(id, 1, snapshot);
		snapshot.title = QStringLiteral("Intermediate revision");
		writer.checkpoint(id, 2, snapshot);
		snapshot.title = QStringLiteral("Latest revision");
		writer.checkpoint(id, 3, snapshot);
		ok &= expect(settle(writer) && error.isEmpty(), "background checkpoints finish");
		auto scan = listModelRecoveries(directory);
		ok &= expect(scan.records.size() == 1 && scan.records[0].title == snapshot.title, "pending edits coalesce to the latest revision");
		writer.cancelPending();
		ok &= expect(listModelRecoveries(directory).records.size() == 1, "disabling recovery preserves committed copies");
		const auto retired = freshId();
		writer.checkpoint(retired, 4, snapshot);
		writer.retire(retired);
		writer.checkpoint(retired, 5, snapshot);
		ok &= expect(settle(writer) && listModelRecoveries(directory).records.size() == 1,
					 "retiring an active ID prevents late completion and requeue from resurrecting it");
		writer.retire(id);
		ok &= expect(listModelRecoveries(directory).records.isEmpty(), "retiring a committed copy removes only that copy");
	}
	const auto preserved = freshId();
	{
		ModelRecoveryWriter writer(directory);
		writer.checkpoint(preserved, 1, snapshot);
		snapshot.title = QStringLiteral("Last accepted edit");
		writer.checkpoint(preserved, 2, snapshot);
	}
	auto scan = listModelRecoveries(directory);
	ModelRecoverySnapshot restored;
	ok &= expect(scan.records.size() == 1 && inspectModelRecovery(scan.records[0].path, &restored).isValid() &&
					 restored.title == snapshot.title,
				 "unexpected destruction preserves the last accepted pending snapshot");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
