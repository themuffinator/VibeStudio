#pragma once
#include <QString>
#include <QVector>

namespace vibestudio {
enum class LevelEntityConnectionMode { SourcesToLast, RadiantTarget, RadiantKillTarget };
struct LevelEntityConnectionRequest {
	LevelEntityConnectionMode mode = LevelEntityConnectionMode::RadiantTarget;
	// Empty/auto/custom uses the same map evidence as built-in entity definitions.
	QString game;
};
struct LevelEntityConnectionReport {
	QString game;
	QString property;
	QString targetName;
	int targetEntity = -1;
	QVector<int> sourceEntities;
	int changedEntities = 0;
};
} // namespace vibestudio
