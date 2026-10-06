#pragma once
#include "core/package_content.h"
#include <QStringList>

namespace vibestudio {
struct LevelBuildPakSlot {
	int requested = -1, number = -1;
	QString packagePath, receiptPath, receiptSha256;
	QByteArray layoutSha256;
	QStringList warnings;
	QString error;
	[[nodiscard]] bool ready() const { return number >= 0 && error.isEmpty(); }
};
// -1 selects an existing matching map receipt, otherwise the first loadable free
// slot. Original Quake requires a consecutive prefix; Quake II checks slots 0–9.
// VibeStudio bounds Quake's otherwise unbounded numbering to 0–999.
int levelBuildPakMaximumSlot(const QString& target);
LevelBuildPakSlot planLevelBuildPakSlot(const QString& directory, const QString& target, const QString& mapName, int requested = -1,
										const PackageReadControl& control = {});
// The receipt is a destination hint, never permission to overwrite a package.
// Save only after package publication. Failure leaves the published PAK intact.
bool saveLevelBuildPakReceipt(const LevelBuildPakSlot& reviewed, const QString& target, const QString& mapName,
							  const QString& packageSha256, QString* error);
} // namespace vibestudio
