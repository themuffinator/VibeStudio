#include "core/compiler_artifact_validation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

QByteArray minimalQuakeBsp()
{
	QByteArray bytes(4 + 15 * 8, '\0');
	qToLittleEndian<qint32>(29, reinterpret_cast<uchar*>(bytes.data()));
	return bytes;
}

QByteArray minimalQuake3Bsp()
{
	QByteArray bytes(8 + 18 * 8, '\0');
	bytes[0] = 'I';
	bytes[1] = 'B';
	bytes[2] = 'S';
	bytes[3] = 'P';
	qToLittleEndian<qint32>(46, reinterpret_cast<uchar*>(bytes.data() + 4));
	return bytes;
}

QByteArray hexen2AlignedQuakeHeader()
{
	QByteArray bytes = minimalQuakeBsp();
	const int modelOffset = bytes.size();
	bytes.resize(bytes.size() + 80);
	qToLittleEndian<quint32>(modelOffset, reinterpret_cast<uchar*>(bytes.data() + 4 + 14 * 8));
	qToLittleEndian<quint32>(80, reinterpret_cast<uchar*>(bytes.data() + 4 + 14 * 8 + 4));
	return bytes;
}

vibestudio::CompilerCommandManifest manifestForOutput(const QString& path, const QString& profileId = QStringLiteral("ericw-qbsp"))
{
	vibestudio::CompilerCommandManifest manifest;
	manifest.profileId = profileId;
	manifest.toolId = profileId.startsWith(QStringLiteral("q3map2")) ? QStringLiteral("q3map2") : profileId;
	manifest.engineFamily = profileId.startsWith(QStringLiteral("q3map2")) ? QStringLiteral("idTech3") : QStringLiteral("idTech2");
	manifest.expectedOutputPaths = {path};
	return manifest;
}

bool containsText(const QStringList& values, const QString& text)
{
	return values.join('\n').contains(text, Qt::CaseInsensitive);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	QDir root(tempDir.path());

	const QString missingPath = root.filePath(QStringLiteral("maps/convert.bsp"));
	if (!root.mkpath(QStringLiteral("maps")) || !writeFile(root.filePath(QStringLiteral("maps/convert-valve.bsp")), minimalQuakeBsp())) {
		return fail("Expected alternate output fixture.");
	}
	vibestudio::CompilerCommandManifest missingManifest = manifestForOutput(missingPath);
	missingManifest.arguments = {QStringLiteral("-convert"), QStringLiteral("valve")};
	const vibestudio::CompilerArtifactValidationReport missingReport = vibestudio::validateCompilerArtifacts(missingManifest);
	if (!missingReport.hasErrors() || !containsText(missingReport.warnings, QStringLiteral("#213"))) {
		return fail("Expected missing conversion output to report an error and #213 naming warning.");
	}

	const QString emptyPath = root.filePath(QStringLiteral("maps/empty.bsp"));
	if (!writeFile(emptyPath, QByteArray())) {
		return fail("Expected empty BSP fixture.");
	}
	const vibestudio::CompilerArtifactValidationReport emptyReport = vibestudio::validateCompilerArtifacts(manifestForOutput(emptyPath));
	if (!emptyReport.hasErrors() || !containsText(emptyReport.errors, QStringLiteral("empty"))) {
		return fail("Expected empty BSP output error.");
	}

	const QString q3Path = root.filePath(QStringLiteral("maps/q3.bsp"));
	if (!writeFile(q3Path, minimalQuake3Bsp())) {
		return fail("Expected Q3 BSP fixture.");
	}
	const vibestudio::CompilerArtifactValidationReport q3Report = vibestudio::validateCompilerArtifacts(manifestForOutput(q3Path, QStringLiteral("q3map2-bsp")));
	if (q3Report.hasErrors()) {
		return fail("Expected minimal Quake III BSP header to validate for q3map2 profile.");
	}
	const vibestudio::CompilerArtifactValidationReport wrongProfileReport = vibestudio::validateCompilerArtifacts(manifestForOutput(q3Path, QStringLiteral("ericw-qbsp")));
	if (!containsText(wrongProfileReport.warnings, QStringLiteral("#278"))) {
		return fail("Expected rough BSP family mismatch warning for ericw profile.");
	}

	const QString hexenPath = root.filePath(QStringLiteral("maps/hexenish.bsp"));
	if (!writeFile(hexenPath, hexen2AlignedQuakeHeader())) {
		return fail("Expected Hexen2-style BSP fixture.");
	}
	const vibestudio::CompilerArtifactValidationReport hexenReport = vibestudio::validateCompilerArtifacts(manifestForOutput(hexenPath));
	if (!containsText(hexenReport.warnings, QStringLiteral("#278"))) {
		return fail("Expected Hexen2-style model layout warning linked to #278.");
	}

	const QString bspxPath = root.filePath(QStringLiteral("maps/light.bsp"));
	if (!writeFile(bspxPath, minimalQuakeBsp())) {
		return fail("Expected BSPX metadata gap fixture.");
	}
	vibestudio::CompilerCommandManifest bspxManifest = manifestForOutput(bspxPath, QStringLiteral("ericw-light"));
	bspxManifest.arguments = {QStringLiteral("-lmshift"), QStringLiteral("3"), QStringLiteral("-world_units_per_luxel"), QStringLiteral("8")};
	const vibestudio::CompilerArtifactValidationReport bspxReport = vibestudio::validateCompilerArtifacts(bspxManifest);
	if (!containsText(bspxReport.warnings, QStringLiteral("#309")) || !containsText(bspxReport.warnings, QStringLiteral("#399")) || !containsText(bspxReport.warnings, QStringLiteral("#415")) || !containsText(bspxReport.warnings, QStringLiteral("#249"))) {
		return fail("Expected missing BSPX lightmap metadata warnings linked to #309/#399/#415/#249.");
	}

	const QString truncatedPath = root.filePath(QStringLiteral("maps/truncated.bsp"));
	if (!writeFile(truncatedPath, QByteArray("IBSP"))) {
		return fail("Expected truncated BSP fixture.");
	}
	const vibestudio::CompilerArtifactValidationReport truncatedReport = vibestudio::validateCompilerArtifacts(manifestForOutput(truncatedPath));
	if (!truncatedReport.hasErrors() || !containsText(truncatedReport.errors, QStringLiteral("too small"))) {
		return fail("Expected truncated BSP header error.");
	}

	return EXIT_SUCCESS;
}
