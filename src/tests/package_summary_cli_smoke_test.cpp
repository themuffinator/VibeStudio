#include "package_summary_test_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (argc < 2 || !temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); bool ok = true;
	const auto inspect = [&](const QString& path, bool json) {
		QProcess process; process.setWorkingDirectory(root.path());
		QStringList args{"--cli", "--settings-file", root.filePath("settings.ini"), "package", "info", path};
		if (json) { args << "--json"; }
		process.start(QString::fromLocal8Bit(argv[1]), args);
		ok &= process.waitForFinished(15000) && process.exitCode() == 0 && process.exitStatus() == QProcess::NormalExit;
		return process.readAllStandardOutput();
	};
	constexpr auto maximum = std::numeric_limits<quint64>::max();
	const QString path = root.filePath("declared.zip");
	ok &= tests::summaryZip(path, {{"a.bin", maximum}});
	const auto exact = QJsonDocument::fromJson(inspect(path, true)).object().value("package").toObject().value("summary").toObject();
	ok &= exact.value("totalSizeBytesExact").toString() == QString::number(maximum) && !exact.value("totalSizeOverflow").toBool();
	ok &= tests::summaryZip(path, {{"a.bin", maximum}, {"b.bin", 1}});
	const auto oversized = QJsonDocument::fromJson(inspect(path, true)).object().value("package").toObject().value("summary").toObject();
	ok &= oversized.value("totalSizeOverflow").toBool() && oversized.value("totalSizeBytes").isNull()
		&& oversized.value("totalSizeBytesExact").isNull() && inspect(path, false).contains("exceeds the supported range");
	const auto preview = [&](const QString& entry, bool json, bool succeeds) {
		QProcess process; process.setWorkingDirectory(root.path());
		QStringList args{"--cli", "--settings-file", root.filePath("settings.ini"), "package", "preview", path, entry};
		if (json) { args << "--json"; }
		process.start(QString::fromLocal8Bit(argv[1]), args);
		ok &= process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == succeeds;
		return process.readAllStandardOutput();
	};
	for (const quint64 total : {(quint64(1) << 53) + 1, quint64(std::numeric_limits<qint64>::max()),
		quint64(std::numeric_limits<qint64>::max()) + 1, maximum}) {
		ok &= tests::summaryZip(path, {{"a.txt", total}});
		const auto report = QJsonDocument::fromJson(preview("a.txt", true, false)).object().value("preview").toObject();
		ok &= report.value("totalBytesKnown").toBool() && report.value("totalBytesExact").toString() == QString::number(total)
			&& report.value("totalBytes").toDouble() > 0 && report.value("bytesReadExact").toString() == QStringLiteral("0")
			&& report.value("bytesRead").toDouble() == 0 && !report.value("error").toString().isEmpty()
			&& report.value("body").toString().isEmpty() && preview("a.txt", false, false).contains(QString::number(total).toUtf8());
	}
	ok &= tests::summaryZip(path, {{"a.txt", 1}});
	const auto small = QJsonDocument::fromJson(preview("a.txt", true, true)).object().value("preview").toObject();
	ok &= small.value("totalBytesKnown").toBool() && small.value("totalBytesExact").toString() == QStringLiteral("1")
		&& small.value("bytesReadExact").toString() == QStringLiteral("1") && small.value("body").toString() == QStringLiteral("x");
	const auto missing = QJsonDocument::fromJson(preview("missing.txt", true, false)).object().value("preview").toObject();
	ok &= !missing.value("totalBytesKnown").toBool() && missing.value("totalBytesExact").isNull()
		&& missing.value("bytesReadExact").toString() == QStringLiteral("0");
	if (!ok) { std::cerr << "CLI totals and previews must distinguish exact unsigned sizes, overflow and unknown metadata without reporting a wrapped number.\n"; }
	return ok ? 0 : 1;
}
