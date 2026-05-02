#include "core/compiler_artifact_validation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>

namespace vibestudio {

namespace {

constexpr qsizetype kQ1HeaderLumps = 15;
constexpr qsizetype kQ2HeaderLumps = 19;
constexpr qsizetype kQ3HeaderLumps = 18;
constexpr qsizetype kLumpBytes = 8;
constexpr qsizetype kQ1HeaderBytes = 4 + kQ1HeaderLumps * kLumpBytes;
constexpr qsizetype kQ2HeaderBytes = 8 + kQ2HeaderLumps * kLumpBytes;
constexpr qsizetype kQ3HeaderBytes = 8 + kQ3HeaderLumps * kLumpBytes;
constexpr qsizetype kQ1ModelLump = 14;
constexpr qsizetype kQ1FaceLump = 7;
constexpr qsizetype kQ1TexinfoLump = 6;
constexpr qsizetype kQ1SurfedgesLump = 13;
constexpr qsizetype kQ2FaceLump = 6;
constexpr qsizetype kQ2TexinfoLump = 5;
constexpr qsizetype kQ2SurfedgesLump = 12;

struct BspLump {
	quint32 offset = 0;
	quint32 length = 0;
};

enum class BspFamily {
	Unknown,
	QuakeBsp29,
	HalfLifeBsp30,
	QuakeBsp2,
	QuakeBsp2Rmq,
	Quake2Ibsp,
	QbismIbsp,
	Quake3Ibsp,
	QuakeLiveIbsp,
};

struct BspHeaderInfo {
	BspFamily family = BspFamily::Unknown;
	QString displayName;
	qsizetype headerBytes = 0;
	QVector<BspLump> lumps;
	bool usesQ1Layout = false;
	bool usesQ2Layout = false;
	bool usesQ3Layout = false;
};

QString validationText(const char* source)
{
	return QCoreApplication::translate("VibeStudioCompilerArtifactValidation", source);
}

QString nativePath(const QString& path)
{
	return QDir::toNativeSeparators(QDir::cleanPath(path));
}

void addFinding(CompilerArtifactValidationReport* report, const QString& level, const QString& path, const QString& message)
{
	if (!report || message.trimmed().isEmpty()) {
		return;
	}
	CompilerArtifactValidationFinding finding;
	finding.level = level;
	finding.path = QDir::cleanPath(path);
	finding.message = message.trimmed();
	report->findings.push_back(finding);
	if (level == QStringLiteral("error")) {
		report->errors.push_back(finding.message);
	} else {
		report->warnings.push_back(finding.message);
	}
}

void addWarning(CompilerArtifactValidationReport* report, const QString& path, const QString& message)
{
	addFinding(report, QStringLiteral("warning"), path, message);
}

void addError(CompilerArtifactValidationReport* report, const QString& path, const QString& message)
{
	addFinding(report, QStringLiteral("error"), path, message);
}

bool containsArgumentText(const QStringList& arguments, const QStringList& needles)
{
	const QString haystack = arguments.join(' ').toLower();
	for (const QString& needle : needles) {
		if (haystack.contains(needle.toLower())) {
			return true;
		}
	}
	return false;
}

quint32 readU32(const QByteArray& bytes, qsizetype offset)
{
	if (offset < 0 || offset + 4 > bytes.size()) {
		return 0;
	}
	return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

qint32 readI32(const QByteArray& bytes, qsizetype offset)
{
	return static_cast<qint32>(readU32(bytes, offset));
}

qint16 readI16(const QByteArray& bytes, qsizetype offset)
{
	if (offset < 0 || offset + 2 > bytes.size()) {
		return 0;
	}
	return qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

QString readAsciiName(const QByteArray& bytes, qsizetype offset, qsizetype length)
{
	QByteArray name;
	for (qsizetype i = 0; i < length && offset + i < bytes.size(); ++i) {
		const char ch = bytes.at(offset + i);
		if (ch == '\0') {
			break;
		}
		name.append(ch);
	}
	return QString::fromLatin1(name).trimmed();
}

QVector<BspLump> readLumps(const QByteArray& bytes, qsizetype offset, qsizetype count)
{
	QVector<BspLump> lumps;
	for (qsizetype i = 0; i < count; ++i) {
		const qsizetype lumpOffset = offset + i * kLumpBytes;
		lumps.push_back({readU32(bytes, lumpOffset), readU32(bytes, lumpOffset + 4)});
	}
	return lumps;
}

BspHeaderInfo inspectBspHeader(const QByteArray& bytes)
{
	BspHeaderInfo info;
	if (bytes.size() < 4) {
		return info;
	}

	const QByteArray ident = bytes.left(4);
	const qint32 firstWord = readI32(bytes, 0);
	if (ident == "IBSP" && bytes.size() >= 8) {
		const qint32 version = readI32(bytes, 4);
		const qsizetype headerBytes = (version == 46 || version == 47) ? kQ3HeaderBytes : kQ2HeaderBytes;
		if (bytes.size() < headerBytes) {
			return info;
		}
		info.family = version == 46 ? BspFamily::Quake3Ibsp : version == 47 ? BspFamily::QuakeLiveIbsp : BspFamily::Quake2Ibsp;
		info.displayName = version == 46 ? validationText("Quake III IBSP v46") : version == 47 ? validationText("Quake Live IBSP v47") : validationText("Quake II IBSP v%1").arg(version);
		info.headerBytes = headerBytes;
		info.lumps = readLumps(bytes, 8, version == 46 || version == 47 ? kQ3HeaderLumps : kQ2HeaderLumps);
		info.usesQ2Layout = version != 46 && version != 47;
		info.usesQ3Layout = version == 46 || version == 47;
		return info;
	}
	if (ident == "QBSP" && bytes.size() >= kQ2HeaderBytes) {
		info.family = BspFamily::QbismIbsp;
		info.displayName = validationText("Qbism IBSP v%1").arg(readI32(bytes, 4));
		info.headerBytes = kQ2HeaderBytes;
		info.lumps = readLumps(bytes, 8, kQ2HeaderLumps);
		info.usesQ2Layout = true;
		return info;
	}
	if (ident == "BSP2" && bytes.size() >= kQ1HeaderBytes) {
		info.family = BspFamily::QuakeBsp2;
		info.displayName = validationText("Quake BSP2");
		info.headerBytes = kQ1HeaderBytes;
		info.lumps = readLumps(bytes, 4, kQ1HeaderLumps);
		info.usesQ1Layout = true;
		return info;
	}
	if (ident == "2PSB" && bytes.size() >= kQ1HeaderBytes) {
		info.family = BspFamily::QuakeBsp2Rmq;
		info.displayName = validationText("Quake BSP2-RMQ");
		info.headerBytes = kQ1HeaderBytes;
		info.lumps = readLumps(bytes, 4, kQ1HeaderLumps);
		info.usesQ1Layout = true;
		return info;
	}
	if ((firstWord == 29 || firstWord == 30) && bytes.size() >= kQ1HeaderBytes) {
		info.family = firstWord == 30 ? BspFamily::HalfLifeBsp30 : BspFamily::QuakeBsp29;
		info.displayName = firstWord == 30 ? validationText("Half-Life BSP v30") : validationText("Quake BSP v29");
		info.headerBytes = kQ1HeaderBytes;
		info.lumps = readLumps(bytes, 4, kQ1HeaderLumps);
		info.usesQ1Layout = true;
		return info;
	}
	return info;
}

qsizetype alignedBspxOffset(const BspHeaderInfo& info)
{
	quint64 end = static_cast<quint64>(info.headerBytes);
	for (const BspLump& lump : info.lumps) {
		end = std::max<quint64>(end, static_cast<quint64>(lump.offset) + lump.length);
	}
	return static_cast<qsizetype>((end + 3u) & ~3u);
}

bool lumpLengthDivisible(const BspLump& lump, quint32 recordSize)
{
	return recordSize == 0 || lump.length == 0 || lump.length % recordSize == 0;
}

void validateLumpBounds(const QString& path, const QByteArray& bytes, const BspHeaderInfo& info, CompilerArtifactValidationReport* report)
{
	for (qsizetype i = 0; i < info.lumps.size(); ++i) {
		const BspLump& lump = info.lumps.at(i);
		if (lump.length == 0) {
			continue;
		}
		const quint64 end = static_cast<quint64>(lump.offset) + lump.length;
		if (lump.offset < static_cast<quint32>(info.headerBytes) || end > static_cast<quint64>(bytes.size()) || end < lump.offset) {
			addError(report, path, validationText("Post-run artifact validation: BSP lump %1 points outside %2; the output is truncated or corrupt (#278/#344).").arg(i).arg(nativePath(path)));
		}
	}
}

void validateRecordSize(const QString& path, const BspHeaderInfo& info, qsizetype lumpIndex, quint32 recordSize, const QString& label, CompilerArtifactValidationReport* report)
{
	if (lumpIndex >= info.lumps.size()) {
		return;
	}
	if (!lumpLengthDivisible(info.lumps.at(lumpIndex), recordSize)) {
		addError(report, path, validationText("Post-run artifact validation: BSP %1 lump has a non-record-aligned size; this can indicate crash-prone output (#344).").arg(label));
	}
}

void validateFaceReferences(const QString& path, const QByteArray& bytes, const BspHeaderInfo& info, qsizetype faceLumpIndex, qsizetype texinfoLumpIndex, qsizetype surfedgeLumpIndex, quint32 faceSize, quint32 texinfoSize, bool bsp2Face, CompilerArtifactValidationReport* report)
{
	if (faceLumpIndex >= info.lumps.size() || texinfoLumpIndex >= info.lumps.size() || surfedgeLumpIndex >= info.lumps.size()) {
		return;
	}
	const BspLump faceLump = info.lumps.at(faceLumpIndex);
	if (faceLump.length == 0 || !lumpLengthDivisible(faceLump, faceSize)) {
		return;
	}
	const quint32 texinfoCount = info.lumps.at(texinfoLumpIndex).length / texinfoSize;
	const quint32 surfedgeCount = info.lumps.at(surfedgeLumpIndex).length / 4;
	const quint32 faceCount = faceLump.length / faceSize;
	for (quint32 i = 0; i < faceCount; ++i) {
		const qsizetype offset = static_cast<qsizetype>(faceLump.offset) + static_cast<qsizetype>(i * faceSize);
		const qint32 firstedge = bsp2Face ? readI32(bytes, offset + 12) : readI32(bytes, offset + 4);
		const qint32 numedges = bsp2Face ? readI32(bytes, offset + 16) : readI16(bytes, offset + 8);
		const qint32 texinfo = bsp2Face ? readI32(bytes, offset + 20) : readI16(bytes, offset + 10);
		if (numedges < 0 || firstedge < 0 || static_cast<quint64>(firstedge) + static_cast<quint64>(std::max(0, numedges)) > surfedgeCount) {
			addError(report, path, validationText("Post-run artifact validation: BSP face %1 references surfedges outside the surfedge lump; this is a crash-risk sanity failure (#344).").arg(i));
			return;
		}
		if (texinfo < 0 || static_cast<quint32>(texinfo) >= texinfoCount) {
			addError(report, path, validationText("Post-run artifact validation: BSP face %1 references missing texture info; surface extents may be unsafe (#344).").arg(i));
			return;
		}
	}
}

QStringList parseBspxLumps(const QByteArray& bytes, const BspHeaderInfo& info, bool expectedBspx, const QString& path, CompilerArtifactValidationReport* report)
{
	QStringList names;
	const qsizetype offset = alignedBspxOffset(info);
	if (offset + 8 > bytes.size()) {
		return names;
	}
	if (bytes.mid(offset, 4) != "BSPX") {
		if (expectedBspx) {
			addWarning(report, path, validationText("Post-run artifact validation: BSPX metadata was requested, but the aligned extension header is absent or invalid (#309/#399/#415/#249)."));
		}
		return names;
	}
	const quint32 count = readU32(bytes, offset + 4);
	if (count > 4096 || offset + 8 + static_cast<qsizetype>(count) * 32 > bytes.size()) {
		addWarning(report, path, validationText("Post-run artifact validation: BSPX lump table is outside the file; lightmap metadata cannot be trusted (#309/#399/#415)."));
		return names;
	}
	for (quint32 i = 0; i < count; ++i) {
		const qsizetype entryOffset = offset + 8 + static_cast<qsizetype>(i) * 32;
		const QString name = readAsciiName(bytes, entryOffset, 24);
		const quint32 lumpOffset = readU32(bytes, entryOffset + 24);
		const quint32 lumpLength = readU32(bytes, entryOffset + 28);
		if (static_cast<quint64>(lumpOffset) + lumpLength > static_cast<quint64>(bytes.size())) {
			addWarning(report, path, validationText("Post-run artifact validation: BSPX lump %1 points outside the file; metadata validation is incomplete (#309/#399/#415).").arg(name.isEmpty() ? QString::number(i) : name));
			continue;
		}
		if (!name.isEmpty()) {
			names.push_back(name);
		}
	}
	return names;
}

QStringList expectedBspxLumps(const CompilerCommandManifest& manifest)
{
	QStringList expected;
	if (manifest.profileId.compare(QStringLiteral("ericw-light"), Qt::CaseInsensitive) != 0) {
		return expected;
	}
	const QStringList arguments = manifest.arguments;
	if (containsArgumentText(arguments, {QStringLiteral("-bspxlit"), QStringLiteral("-bspxonly"), QStringLiteral("-bspx ")})) {
		expected << QStringLiteral("RGBLIGHTING");
	}
	if (containsArgumentText(arguments, {QStringLiteral("-bspxlux"), QStringLiteral("-bspxonly"), QStringLiteral("-bspx ")})) {
		expected << QStringLiteral("LIGHTINGDIR");
	}
	if (containsArgumentText(arguments, {QStringLiteral("-bspxhdr")})) {
		expected << QStringLiteral("LIGHTING_E5BGR9");
	}
	if (containsArgumentText(arguments, {QStringLiteral("-wrnormals")})) {
		expected << QStringLiteral("FACENORMALS");
	}
	if (containsArgumentText(arguments, {QStringLiteral("-world_units_per_luxel")})) {
		expected << QStringLiteral("DECOUPLED_LM");
	}
	if (containsArgumentText(arguments, {QStringLiteral("-lmshift"), QStringLiteral("-lightmap_scale")})) {
		expected << QStringLiteral("LMSHIFT");
	}
	if (containsArgumentText(arguments, {QStringLiteral("-lightgrid")})) {
		expected << QStringLiteral("LIGHTGRID_OCTREE");
	}
	expected.removeDuplicates();
	return expected;
}

bool outputLooksLikeRequestedFamily(const CompilerCommandManifest& manifest, BspFamily family)
{
	const bool idTech3Profile = manifest.engineFamily.compare(QStringLiteral("idTech3"), Qt::CaseInsensitive) == 0
		|| manifest.profileId.startsWith(QStringLiteral("q3map2-"), Qt::CaseInsensitive);
	if (idTech3Profile) {
		return family == BspFamily::Quake3Ibsp || family == BspFamily::QuakeLiveIbsp;
	}
	const bool ericwProfile = manifest.toolId.startsWith(QStringLiteral("ericw-"), Qt::CaseInsensitive);
	if (!ericwProfile) {
		return true;
	}
	if (containsArgumentText(manifest.arguments, {QStringLiteral("q2bsp"), QStringLiteral("quake2")})) {
		return family == BspFamily::Quake2Ibsp || family == BspFamily::QbismIbsp;
	}
	return family == BspFamily::QuakeBsp29 || family == BspFamily::HalfLifeBsp30 || family == BspFamily::QuakeBsp2 || family == BspFamily::QuakeBsp2Rmq;
}

void validateHeaderFamily(const QString& path, const CompilerCommandManifest& manifest, const BspHeaderInfo& info, CompilerArtifactValidationReport* report)
{
	if (info.family == BspFamily::Unknown) {
		addError(report, path, validationText("Post-run artifact validation: %1 does not begin with a recognized Quake-family BSP header; expected output may be corrupt (#278).").arg(nativePath(path)));
		return;
	}
	if (!outputLooksLikeRequestedFamily(manifest, info.family)) {
		addWarning(report, path, validationText("Post-run artifact validation: %1 header looks like %2, which does not match the selected compiler profile; check conversion and destination expectations (#213/#278).").arg(nativePath(path), info.displayName));
	}
}

void validateHexen2Heuristic(const QString& path, const CompilerCommandManifest& manifest, const BspHeaderInfo& info, CompilerArtifactValidationReport* report)
{
	if (!info.usesQ1Layout || kQ1ModelLump >= info.lumps.size()) {
		return;
	}
	if (containsArgumentText(manifest.arguments, {QStringLiteral("hexen2"), QStringLiteral("h2bsp")})) {
		return;
	}
	const quint32 modelLength = info.lumps.at(kQ1ModelLump).length;
	if (modelLength == 0) {
		return;
	}
	const bool q1Aligned = modelLength % 64 == 0;
	const bool h2Aligned = modelLength % 80 == 0;
	if (!q1Aligned && h2Aligned) {
		addWarning(report, path, validationText("Post-run artifact validation: model data is aligned like Hexen II, not Quake, although the profile did not request Hexen II; inspect for the corrupt-BSP misidentification risk tracked as ericw-tools #278."));
	} else if (q1Aligned && h2Aligned) {
		addWarning(report, path, validationText("Post-run artifact validation: model data is ambiguous between Quake and Hexen II layouts; VibeStudio only performs a lightweight #278 header check, so use a BSP inspector before release if an engine reports Hexen II."));
	}
}

QStringList likelyAlternateOutputs(const QString& expectedPath)
{
	QStringList candidates;
	const QFileInfo expectedInfo(expectedPath);
	const QDir dir(expectedInfo.absolutePath());
	const QString base = expectedInfo.completeBaseName();
	const QString suffix = expectedInfo.suffix().isEmpty() ? QStringLiteral("bsp") : expectedInfo.suffix();
	for (const QString& stem : {QStringLiteral("%1-valve").arg(base), QStringLiteral("%1_valve").arg(base), QStringLiteral("%1-bsp2").arg(base)}) {
		const QString candidate = dir.filePath(QStringLiteral("%1.%2").arg(stem, suffix));
		if (QFileInfo(candidate).isFile()) {
			candidates.push_back(QDir::cleanPath(candidate));
		}
	}
	return candidates;
}

void validateBspArtifact(const QString& path, const CompilerCommandManifest& manifest, const QByteArray& bytes, CompilerArtifactValidationReport* report)
{
	const BspHeaderInfo info = inspectBspHeader(bytes);
	validateHeaderFamily(path, manifest, info, report);
	if (info.family == BspFamily::Unknown) {
		return;
	}
	validateLumpBounds(path, bytes, info, report);
	if (info.usesQ1Layout) {
		const bool bsp29 = info.family == BspFamily::QuakeBsp29 || info.family == BspFamily::HalfLifeBsp30;
		validateRecordSize(path, info, 1, 20, validationText("planes"), report);
		validateRecordSize(path, info, 3, 12, validationText("vertices"), report);
		validateRecordSize(path, info, 5, info.family == BspFamily::QuakeBsp2Rmq ? 32 : 24, validationText("nodes"), report);
		validateRecordSize(path, info, kQ1TexinfoLump, 40, validationText("texture info"), report);
		validateRecordSize(path, info, kQ1FaceLump, bsp29 ? 20 : 32, validationText("faces"), report);
		validateRecordSize(path, info, 9, bsp29 ? 8 : 12, validationText("clipnodes"), report);
		validateRecordSize(path, info, kQ1SurfedgesLump, 4, validationText("surfedges"), report);
		validateFaceReferences(path, bytes, info, kQ1FaceLump, kQ1TexinfoLump, kQ1SurfedgesLump, bsp29 ? 20 : 32, 40, !bsp29, report);
		validateHexen2Heuristic(path, manifest, info, report);
	}
	if (info.usesQ2Layout) {
		validateRecordSize(path, info, kQ2TexinfoLump, 76, validationText("texture info"), report);
		validateRecordSize(path, info, kQ2FaceLump, 20, validationText("faces"), report);
		validateRecordSize(path, info, kQ2SurfedgesLump, 4, validationText("surfedges"), report);
		validateFaceReferences(path, bytes, info, kQ2FaceLump, kQ2TexinfoLump, kQ2SurfedgesLump, 20, 76, false, report);
	}

	const QStringList expectedLumps = expectedBspxLumps(manifest);
	const QStringList actualLumps = parseBspxLumps(bytes, info, !expectedLumps.isEmpty(), path, report);
	for (const QString& expected : expectedLumps) {
		if (!actualLumps.contains(expected, Qt::CaseInsensitive)) {
			addWarning(report, path, validationText("Post-run artifact validation: expected BSPX lump %1 was not found; this preserves the validation gap around lightmap metadata compatibility (#309/#399/#415/#249).").arg(expected));
		}
	}
}

} // namespace

bool CompilerArtifactValidationReport::hasWarnings() const
{
	return !warnings.isEmpty();
}

bool CompilerArtifactValidationReport::hasErrors() const
{
	return !errors.isEmpty();
}

CompilerArtifactValidationReport validateCompilerArtifacts(const CompilerCommandManifest& manifest)
{
	CompilerArtifactValidationReport report;
	for (const QString& outputPath : manifest.expectedOutputPaths) {
		const QString path = QDir::cleanPath(outputPath);
		const QFileInfo info(path);
		if (!info.exists()) {
			addError(&report, path, validationText("Post-run artifact validation: expected compiler output is missing after a successful process exit: %1").arg(nativePath(path)));
			if (containsArgumentText(manifest.arguments, {QStringLiteral("-convert"), QStringLiteral("valve")})) {
				const QStringList alternates = likelyAlternateOutputs(path);
				const QString suffix = alternates.isEmpty() ? QString() : validationText(" Alternate output found: %1").arg(nativePath(alternates.first()));
				addWarning(&report, path, validationText("Post-run artifact validation: conversion output naming did not match VibeStudio's expected destination; check ericw-tools #213.%1").arg(suffix));
			}
			continue;
		}
		if (!info.isFile()) {
			addError(&report, path, validationText("Post-run artifact validation: expected compiler output is not a file: %1").arg(nativePath(path)));
			continue;
		}
		if (info.size() == 0) {
			addError(&report, path, validationText("Post-run artifact validation: expected compiler output is empty after a successful process exit: %1").arg(nativePath(path)));
			continue;
		}

		if (info.suffix().compare(QStringLiteral("bsp"), Qt::CaseInsensitive) != 0) {
			continue;
		}

		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			addWarning(&report, path, validationText("Post-run artifact validation: unable to read BSP output for header checks: %1").arg(nativePath(path)));
			continue;
		}
		const QByteArray bytes = file.readAll();
		if (bytes.size() < kQ1HeaderBytes) {
			addError(&report, path, validationText("Post-run artifact validation: BSP output is too small to contain a complete header: %1 (#278/#344).").arg(nativePath(path)));
			continue;
		}
		validateBspArtifact(path, manifest, bytes, &report);
	}
	report.warnings.removeDuplicates();
	report.errors.removeDuplicates();
	return report;
}

} // namespace vibestudio
