#include "core/level_build_pak_deployment.h"
#include "core/package_storage.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>

namespace vibestudio {
namespace {
QString text(const char* source) { return QCoreApplication::translate("LevelBuildPakDeployment", source); }
bool stopped(const PackageReadControl& control) { return control.isCancelled && control.isCancelled(); }
QString packageName(int slot) { return QStringLiteral("pak%1.pak").arg(slot); }
QString receiptName(int slot) { return QStringLiteral(".vibestudio-pak%1.json").arg(slot); }
bool readReceipt(const QString& path, int slot, QJsonObject* value, QString* hash, QString* error, const PackageReadControl& control) {
	if (!safePackageStoragePath(path, error)) {
		return false;
	}
	if (!QFileInfo::exists(path)) {
		return true;
	}
	if (!QFileInfo(path).isFile() || QFileInfo(path).size() > 65536) {
		*error = text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment", "Invalid or oversized PAK deployment receipt: %1")).arg(path);
		return false;
	}
	const auto identity = capturePackageFileIdentity(path, error, control, 32);
	if (!identity) {
		return false;
	}
	PackageContentDevice device(identity, control);
	if (!device.open()) {
		*error = device.errorString();
		return false;
	}
	const auto bytes = device.readAll();
	if (device.failed()) {
		*error = device.errorString();
		return false;
	}
	*value = QJsonDocument::fromJson(bytes).object();
	const auto target = value->value("target").toString();
	if (value->value("schemaVersion").toInt() != 1 || value->value("kind") != QStringLiteral("vibestudio-level-pak") ||
		value->value("slot").toInt(-1) != slot || levelBuildPakMaximumSlot(target) < slot ||
		!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,64}$")).match(value->value("mapName").toString()).hasMatch() ||
		!QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(value->value("packageSha256").toString()).hasMatch()) {
		*error = text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment", "Unrecognized PAK deployment receipt: %1")).arg(path);
		return false;
	}
	*hash = QString::fromLatin1(identity->sha256.toHex());
	return true;
}
} // namespace
int levelBuildPakMaximumSlot(const QString& target) {
	return target == QStringLiteral("quake") ? 999 : target == QStringLiteral("quake2") ? 9 : -1;
}
LevelBuildPakSlot planLevelBuildPakSlot(const QString& directory, const QString& target, const QString& mapName, int requested,
										const PackageReadControl& control) {
	LevelBuildPakSlot plan;
	plan.requested = requested;
	const auto fail = [&](const QString& error) {
		plan.error = error;
		return plan;
	};
	const int maximum = levelBuildPakMaximumSlot(target);
	if (maximum < 0 || requested < -1 || requested > maximum) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment", "Choose Automatic or a PAK slot between 0 and %1 for this target."))
						.arg(maximum));
	}
	if (!safePackageStoragePath(directory, &plan.error)) {
		return plan;
	}
	if (QFileInfo::exists(directory) && !QFileInfo(directory).isDir()) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment", "The game folder is occupied by a file.")));
	}
	QMap<int, QFileInfo> packages, receipts;
	const QRegularExpression packagePattern(QStringLiteral("^pak([0-9]+)\\.pak$"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpression receiptPattern(QStringLiteral("^\\.vibestudio-pak([0-9]+)\\.json$"),
											QRegularExpression::CaseInsensitiveOption);
	for (const auto& entry :
		 QDir(directory).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
		if (stopped(control)) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment", "PAK slot review cancelled.")));
		}
		const auto p = packagePattern.match(entry.fileName()), r = receiptPattern.match(entry.fileName());
		if (!p.hasMatch() && !r.hasMatch()) {
			continue;
		}
		bool valid = false;
		const int slot = (p.hasMatch() ? p : r).captured(1).toInt(&valid);
		if (!valid || slot > maximum) {
			continue;
		}
		const auto expected = p.hasMatch() ? packageName(slot) : receiptName(slot);
		if (entry.fileName() != expected || !entry.isFile() || !safePackageStoragePath(entry.absoluteFilePath(), &plan.error)) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment", "A PAK slot has an unsafe path or nonportable filename: %1"))
							.arg(entry.absoluteFilePath()));
		}
		(p.hasMatch() ? packages : receipts).insert(slot, entry);
	}
	QMap<int, QString> hashes;
	QMap<int, QJsonObject> records;
	for (auto it = receipts.cbegin(); it != receipts.cend(); ++it) {
		if (requested >= 0 && it.key() != requested) {
			continue;
		}
		QJsonObject record;
		QString hash;
		if (!readReceipt(it->absoluteFilePath(), it.key(), &record, &hash, &plan.error, control)) {
			return plan;
		}
		hashes.insert(it.key(), hash);
		records.insert(it.key(), record);
		if (requested < 0 && record.value("target").toString() == target && record.value("mapName").toString() == mapName) {
			if (plan.number >= 0) {
				return fail(text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment",
												   "Several PAK slots were deployed for this map. Choose a slot explicitly.")));
			}
			plan.number = it.key();
		}
	}
	if (requested >= 0) {
		plan.number = requested;
	}
	if (plan.number < 0) {
		for (int slot = 0; slot <= maximum; ++slot) {
			// A receipt for another map reserves its slot even if its PAK was removed.
			if (!packages.contains(slot) && !receipts.contains(slot)) {
				plan.number = slot;
				break;
			}
		}
	}
	if (plan.number < 0) {
		return fail(text(QT_TRANSLATE_NOOP(
			"LevelBuildPakDeployment", "No free PAK slot is available. Choose another game folder or review an explicit replacement.")));
	}
	// Interface facts from id Software's GPL-2.0-or-later releases, reviewed
	// 2026-10-05: Quake WinQuake/common.c COM_AddGameDirectory stops at a gap;
	// Quake-2 qcommon/files.c FS_AddGameDirectory checks 0..9 and skips gaps.
	// Public source links and compatible-license review: docs/CREDITS.md.
	if (target == QStringLiteral("quake")) {
		for (int slot = 0; slot < plan.number; ++slot) {
			if (!packages.contains(slot)) {
				return fail(
					text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment",
										   "Quake stops loading at missing pak%1.pak. Choose that slot or a different game folder."))
						.arg(slot));
			}
		}
	}
	plan.packagePath = QDir(directory).filePath(packageName(plan.number));
	plan.receiptPath = QDir(directory).filePath(receiptName(plan.number));
	plan.receiptSha256 = hashes.value(plan.number);
	const auto record = records.value(plan.number);
	if (requested < 0 && !record.isEmpty() && packages.contains(plan.number)) {
		const auto identity = capturePackageFileIdentity(plan.packagePath, &plan.error, control);
		if (!identity) {
			return plan;
		}
		if (QString::fromLatin1(identity->sha256.toHex()) != record.value("packageSha256").toString()) {
			return fail(text(QT_TRANSLATE_NOOP(
				"LevelBuildPakDeployment",
				"The remembered PAK changed outside this deployment. Choose its slot explicitly to review replacement.")));
		}
	}
	if (requested >= 0 && !record.isEmpty() &&
		(record.value("target").toString() != target || record.value("mapName").toString() != mapName)) {
		plan.warnings << text(QT_TRANSLATE_NOOP(
			"LevelBuildPakDeployment", "This slot was previously assigned to another map. Replacing it removes that deployed package."));
	}
	QByteArray layout;
	for (auto it = packages.cbegin(); it != packages.cend(); ++it) {
		if (it.key() == plan.number) {
			continue;
		}
		layout += it->fileName().toUtf8() + ':' + QByteArray::number(it->size()) + ':' +
				  QByteArray::number(it->lastModified().toMSecsSinceEpoch()) + '\n';
	}
	plan.layoutSha256 = QCryptographicHash::hash(layout, QCryptographicHash::Sha256);
	if (packages.size() > (packages.contains(plan.number) ? 1 : 0)) {
		plan.warnings << text(
			QT_TRANSLATE_NOOP("LevelBuildPakDeployment", "Other numbered PAKs can override this map or its assets. Higher loaded slots "
														 "take precedence; source-port search rules may differ."));
	}
	return plan;
}
bool saveLevelBuildPakReceipt(const LevelBuildPakSlot& reviewed, const QString& target, const QString& mapName,
							  const QString& packageSha256, QString* error) {
	QJsonObject previous;
	QString hash;
	if (!readReceipt(reviewed.receiptPath, reviewed.number, &previous, &hash, error, {}) || hash != reviewed.receiptSha256) {
		if (error->isEmpty()) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildPakDeployment", "The PAK deployment receipt changed during publication."));
		}
		return false;
	}
	QSaveFile output(reviewed.receiptPath);
	const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1},
												 {"kind", "vibestudio-level-pak"},
												 {"slot", reviewed.number},
												 {"target", target},
												 {"mapName", mapName},
												 {"packageSha256", packageSha256}})
						   .toJson();
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
		*error = output.errorString();
		return false;
	}
	return true;
}
} // namespace vibestudio
