#include "core/text_document.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject cli(const QString& binary, const QStringList& args, int expected, bool& ok)
{
	QProcess process;
	process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("--json")} + args);
	ok &= expect(process.waitForFinished(30000) && process.exitCode() == expected, "unexpected text CLI exit code");
	const QJsonDocument result = QJsonDocument::fromJson(process.readAllStandardOutput());
	ok &= expect(result.isObject(), "text CLI must return JSON");
	return result.object().value(QStringLiteral("textDocument")).toObject();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QByteArray encoded;
	QString error;
	const QByteArray bom = QByteArray::fromHex("efbbbf");
	const QString unicode = QStringLiteral("café\u00a0雪\u2028soft\u2029paragraph\r\nlast 🌍");
	const QVector<QByteArray> fixtures {
		{}, "plain", "one\n", "one\r\ntwo\rthree\nlast", bom, bom + "text\r\n", bom + bom + "twice",
		unicode.toUtf8(),
		QByteArray::fromHex("fffe6100a0003cd80ddf0d000a0062000d006300"),
		QByteArray::fromHex("feff006100a0d83cdf0d000d000a0062000d0063"),
	};
	for (const auto& bytes : fixtures) {
		const auto source = decodeTextFile(bytes);
		ok &= expect(source.editable() && encodeTextFile(source, source.text, &encoded, &error) && encoded == bytes, "a no-edit round trip must preserve every byte");
		ok &= expect(encodeTextFile(source, source.text + QStringLiteral("!"), &encoded, &error)
			&& decodeTextFile(encoded).text == source.text + QStringLiteral("!"), "edited Unicode text must round trip without loss");
	}
	for (const QByteArray& bytes : {QByteArray::fromHex("c080"), QByteArray::fromHex("e282"), QByteArray::fromHex("fffe4100ff"),
		QByteArray::fromHex("feffd800"), QByteArray::fromHex("fffe0000"), QByteArray::fromHex("0000feff"), QByteArray("binary\0text", 11)}) {
		const auto invalid = decodeTextFile(bytes);
		ok &= expect(!invalid.editable() && !invalid.error.isEmpty() && !encodeTextFile(invalid, QStringLiteral("text"), &encoded), "invalid encoding and binary input must not be writable");
	}
	const auto mixed = decodeTextFile(bom + "alpha\r\nbeta\ngamma\rlast");
	const auto repeated = decodeTextFile(bom + "a\r\na\na\ra");
	QString editedText;
	ok &= expect(encodeTextFileEdits(repeated, {{2, 1, QStringLiteral("b\nc")}}, &encoded, &editedText)
		&& encoded == bom + "a\r\nb\r\nc\na\ra" && editedText == QStringLiteral("a\nb\nc\na\na"),
		"exact-range edits retain original separators even among identical lines; inserted lines use the preferred ending");
	ok &= expect(encodeTextFileEdits(repeated, {{2, 2, {}}}, &encoded) && encoded == bom + "a\r\na\ra",
		"deleting a known range removes exactly its separator");
	ok &= expect(!encodeTextFileEdits(repeated, {{2, 2, {}}, {3, 1, {}}}, &encoded)
		&& !encodeTextFileEdits(repeated, {{1, std::numeric_limits<qsizetype>::max(), {}}}, &encoded)
		&& !encodeTextFileEdits(repeated, {{-1, 1, {}}}, &encoded), "overlapping, negative and overflowing exact ranges must be rejected");
	const auto astral = decodeTextFile(QStringLiteral("🌍x").toUtf8());
	ok &= expect(!encodeTextFileEdits(astral, {{1, 1, {}}}, &encoded)
		&& !encodeTextFileEdits(astral, {{0, 1, {}}}, &encoded)
		&& !encodeTextFileEdits(astral, {{0, 2, QString(QChar(0xd800))}}, &encoded)
		&& !encodeTextFileEdits(astral, {{0, 2, QString(QChar(0))}}, &encoded), "exact edits cannot split surrogates or introduce invalid text");
	ok &= expect(encodeTextFile(mixed, QStringLiteral("edited\nbeta\ngamma\nlast"), &encoded) && encoded == bom + "edited\r\nbeta\ngamma\rlast", "changed lines preserve mixed separators");
	ok &= expect(encodeTextFile(mixed, QStringLiteral("added\nalpha\nbeta\ngamma\nlast"), &encoded)
		&& encoded == bom + "added\r\nalpha\r\nbeta\ngamma\rlast", "insertions must not shift unchanged line endings");
	ok &= expect(encodeTextFile(mixed, QStringLiteral("alpha\ngamma\nlast\n"), &encoded)
		&& encoded == bom + "alpha\r\ngamma\rlast\r\n", "deleted lines and a new final newline preserve corresponding separators");
	const auto majority = decodeTextFile("a\nb\r\nc\r\nlast");
	ok &= expect(majority.preferredLineEnding == TextLineEnding::CrLf, "new lines use the most common original separator");
	ok &= expect(encodeTextFile(majority, QStringLiteral("a\nb\nc\nlast\nadded\n"), &encoded)
		&& encoded == "a\nb\r\nc\r\nlast\r\nadded\r\n", "new trailing lines must use the prevailing separator");
	const auto unicodeSeparator = decodeTextFile(QStringLiteral("a\u2029b\u00a0c\u2028d").toUtf8());
	ok &= expect(encodeTextFile(unicodeSeparator, QStringLiteral("A\nb\u00a0c\u2028d"), &encoded)
		&& encoded == QStringLiteral("A\u2029b\u00a0c\u2028d").toUtf8(), "Unicode paragraph, soft line, and non-breaking-space characters survive edits");
	const auto little = decodeTextFile(QByteArray::fromHex("fffe61000d000a006200"));
	ok &= expect(encodeTextFile(little, QStringLiteral("A\nb"), &encoded) && encoded == QByteArray::fromHex("fffe41000d000a006200"), "UTF-16 LE remains little endian with BOM");
	const auto big = decodeTextFile(QByteArray::fromHex("feff0061000d000a0062"));
	ok &= expect(encodeTextFile(big, QStringLiteral("A\nb"), &encoded) && encoded == QByteArray::fromHex("feff0041000d000a0062"), "UTF-16 BE remains big endian with BOM");
	ok &= expect(!encodeTextFile(mixed, QString(QChar(0xd800)), &encoded)
		&& !encodeTextFile(mixed, QString(QChar(0)), &encoded)
		&& !encodeTextFile(mixed, QStringLiteral("a\r\nb"), &encoded), "invalid editor text must be rejected");
	ok &= expect(!decodeTextFile(QByteArray(textDocumentByteLimit + 1, 'x')).editable()
		&& !encodeTextFile(mixed, QString(textDocumentByteLimit, QChar(0x00e9)), &encoded), "input and encoded-output byte bounds are enforced");
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.qc"));
	const QByteArray original = bom + "alpha\r\nbeta";
	ok &= expect(writeFile(path, original), "write source fixture");
	auto source = readTextFile(path);
	const auto preview = saveTextFile(source, QStringLiteral("new\nbeta"));
	ok &= expect(preview.succeeded && preview.dryRun && preview.changed && preview.bytesWritten == 0
		&& readFile(path) == original, "save defaults to a non-writing preview");
	QFile file(path);
	ok &= expect(file.open(QIODevice::ReadWrite), "open timestamp fixture");
	const auto timestamp = QDateTime::fromSecsSinceEpoch(1600000000).toUTC();
	ok &= expect(file.setFileTime(timestamp, QFileDevice::FileModificationTime), "set timestamp fixture");
	file.close();
	const auto unchanged = saveTextFile(source, source.text, false);
	ok &= expect(unchanged.succeeded && !unchanged.changed && QFileInfo(path).lastModified() == timestamp, "saving unchanged text must not touch the file");
	ok &= expect(writeFile(path, bom + "other\r\nbeta"), "write same-sized external change");
	ok &= expect(file.open(QIODevice::ReadWrite) && file.setFileTime(timestamp, QFileDevice::FileModificationTime), "restore external edit timestamp");
	file.close();
	const auto blocked = saveTextFile(source, QStringLiteral("new\nbeta"), false);
	ok &= expect(!blocked.succeeded && blocked.conflict && readFile(path) == bom + "other\r\nbeta", "source hashes must catch same-size and same-time external edits");
	ok &= expect(saveTextFile(source, QStringLiteral("new\nbeta"), false, blocked.currentSha256).succeeded
		&& readFile(path) == bom + "new\r\nbeta", "a separately reviewed current hash permits an overwrite");
	ok &= expect(!saveTextFile(source, QStringLiteral("newer\nbeta"), false, blocked.currentSha256).succeeded, "reviewed overwrite hash cannot be reused after another change");
	source = readTextFile(path);
	source.resolvedPath += QStringLiteral(".different");
	ok &= expect(!saveTextFile(source, QStringLiteral("newer\nbeta"), false).succeeded, "a changed resolved path blocks saving even with matching bytes");
	source = readTextFile(path);
	ok &= expect(QFile::remove(path) && !saveTextFile(source, source.text, false).succeeded && !QFile::exists(path), "a missing source is not silently recreated");
	ok &= expect(saveTextFile(source, source.text, false).canRecreate
		&& saveTextFile(source, source.text, false, {}, true).succeeded && readFile(path) == source.originalBytes, "an explicitly reviewed deleted source can be recreated from its snapshot");
	ok &= expect(!saveTextFile(source, QStringLiteral("replace"), false, {}, true).succeeded
		&& readFile(path) == source.originalBytes, "recreation must never overwrite a file that reappeared during review");
	if (app.arguments().size() > 1) {
		const QString binary = app.arguments()[1];
		const QString input = temp.filePath(QStringLiteral("input.qc"));
		ok &= expect(writeFile(path, original) && writeFile(input, "new\nbeta"), "write CLI fixtures");
		const auto info = cli(binary, {QStringLiteral("code"), QStringLiteral("text-info"), path}, 0, ok);
		ok &= expect(info.value(QStringLiteral("byteOrderMark")).toBool() && info.value(QStringLiteral("lineEndings")).toObject().value(QStringLiteral("CRLF")).toInt() == 1, "CLI reports source format");
		const QStringList command {QStringLiteral("code"), QStringLiteral("text-save"), path, QStringLiteral("--input"), input};
		const auto dry = cli(binary, command, 0, ok).value(QStringLiteral("save")).toObject();
		ok &= expect(dry.value(QStringLiteral("dryRun")).toBool() && readFile(path) == original, "CLI saves default to preview");
		cli(binary, command + QStringList {QStringLiteral("--write")}, 2, ok);
		cli(binary, command + QStringList {QStringLiteral("--expected-sha256"), QStringLiteral("invalid")}, 2, ok);
		cli(binary, command + QStringList {QStringLiteral("--write"), QStringLiteral("--expected-sha256"), QString(64, QLatin1Char('0'))}, 4, ok);
		const auto saved = cli(binary, command + QStringList {QStringLiteral("--write"), QStringLiteral("--expected-sha256"), info.value(QStringLiteral("sha256")).toString()}, 0, ok).value(QStringLiteral("save")).toObject();
		ok &= expect(saved.value(QStringLiteral("succeeded")).toBool() && readFile(path) == bom + "new\r\nbeta", "CLI uses the same format-preserving save service");
		cli(binary, {QStringLiteral("code"), QStringLiteral("text-info"), path + QStringLiteral(".missing")}, 3, ok);
		const QString created = temp.filePath(QStringLiteral("created.qc"));
		const QStringList create {QStringLiteral("code"), QStringLiteral("text-create"), created};
		cli(binary, create, 0, ok);
		ok &= expect(!QFile::exists(created), "create preview must not create even an empty file");
		cli(binary, create + QStringList {QStringLiteral("--write")}, 0, ok);
		ok &= expect(QFile::exists(created) && QFileInfo(created).size() == 0, "CLI creates an empty UTF-8 document");
		const QStringList copy {QStringLiteral("code"), QStringLiteral("text-save-as"), path, QStringLiteral("--output"), created};
		const auto reviewed = cli(binary, copy, 0, ok);
		cli(binary, copy + QStringList {QStringLiteral("--write")}, 2, ok);
		cli(binary, copy + QStringList {QStringLiteral("--write"), QStringLiteral("--expected-sha256"), QString(64, QLatin1Char('0'))}, 4, ok);
		cli(binary, copy + QStringList {QStringLiteral("--write"), QStringLiteral("--expected-sha256"), reviewed.value(QStringLiteral("destinationSha256")).toString()}, 0, ok);
		ok &= expect(readFile(created) == readFile(path), "CLI Save As preserves source bytes and reviews the destination hash");
	}
	const QString destination = temp.filePath(QStringLiteral("copy.cfg"));
	const auto target = inspectTextWriteTarget(destination);
	ok &= expect(target.isValid() && !target.existed && saveTextFileAs(little, QStringLiteral("A\nb"), target).succeeded
		&& !QFile::exists(destination), "Save As defaults to a non-writing preview");
	ok &= expect(saveTextFileAs(little, QStringLiteral("A\nb"), target, false).succeeded
		&& readFile(destination) == QByteArray::fromHex("fffe41000d000a006200"), "a pathless source saves its original format to a new destination");
	ok &= expect(!saveTextFileAs(little, little.text, target, false).succeeded, "a destination that appeared after inspection must not be overwritten");
	const auto existing = inspectTextWriteTarget(destination);
	ok &= expect(saveTextFileAs(little, little.text, existing, false).succeeded, "a reviewed destination may be replaced");
	ok &= expect(!saveTextFileAs(little, little.text, existing, false).succeeded, "reviewed destination hashes cannot be reused after changes");
	const auto removed = inspectTextWriteTarget(destination);
	ok &= expect(QFile::remove(destination) && !saveTextFileAs(little, little.text, removed, false).succeeded, "a removed destination is a conflict, not permission to recreate it");
	ok &= expect(!inspectTextWriteTarget({}).isValid() && !inspectTextWriteTarget(temp.path()).isValid()
		&& !inspectTextWriteTarget(temp.filePath(QStringLiteral("missing/child.qc"))).isValid(), "Save As rejects empty paths, directories, and missing parents");
	ok &= expect(saveTextFileAs(decodeTextFile({}), {}, inspectTextWriteTarget(destination), false).succeeded && QFile::exists(destination), "a new empty document must still create its file");
	return ok ? 0 : 1;
}
