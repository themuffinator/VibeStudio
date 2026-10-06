#include "core/package_copy.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

using namespace vibestudio;

// A bounded fixture probe for external time/memory measurement. The caller
// supplies a synthetic single-file archive and an owned output parent.
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (argc != 3) { return 2; }
	QElapsedTimer timer; timer.start();
	PackageArchive archive; QString error;
	if (!archive.load(QString::fromLocal8Bit(argv[1]), &error)) { std::cerr << error.toStdString(); return 1; }
	PackageCopyRequest request; request.parentDirectory = QString::fromLocal8Bit(argv[2]);
	const auto entries = archive.entries();
	for (qsizetype index = 0; index < entries.size(); ++index) {
		if (entries.at(index).kind == PackageEntryKind::File) { request.entryIndexes << index; }
	}
	if (request.entryIndexes.size() != 1) { return 2; }
	const auto copied = copyPackageEntries(archive, request);
	if (!copied.succeeded() || copied.paths.size() != 1) { std::cerr << copied.error.toStdString(); return 1; }
	const auto copyMs = timer.elapsed();
	QFile output(copied.paths.first()); if (!output.open(QIODevice::ReadOnly)) { return 1; }
	QCryptographicHash hash(QCryptographicHash::Sha256); qint64 bytes = 0;
	while (!output.atEnd()) {
		const auto chunk = output.read(65536);
		if (chunk.isEmpty() && output.error() != QFile::NoError) { return 1; }
		bytes += chunk.size(); hash.addData(chunk);
	}
	output.close();
	QJsonObject result{{QStringLiteral("bytes"), bytes}, {QStringLiteral("sha256"), QString::fromLatin1(hash.result().toHex())},
		{QStringLiteral("copyMs"), copyMs}, {QStringLiteral("verificationMs"), timer.elapsed() - copyMs}};
	std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
	return 0;
}
