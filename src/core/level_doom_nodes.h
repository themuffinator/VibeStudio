#pragma once
#include "core/level_map.h"
#include <QJsonObject>

namespace vibestudio {
enum class LevelDoomNodeState { NotApplicable, Missing, Invalid, Present, Unsupported, Stale, Cancelled };
struct LevelDoomNodeReport {
	LevelDoomNodeState state = LevelDoomNodeState::NotApplicable;
	QString format, message;
	QStringList warnings;
	quint32 segs = 0, subsectors = 0, nodes = 0;
	[[nodiscard]] bool needsBuild() const {
		return state == LevelDoomNodeState::Missing || state == LevelDoomNodeState::Invalid || state == LevelDoomNodeState::Stale;
	}
};
// Structural validation, not a proof that externally retained nodes were built
// from these inputs. Studio edits invalidate their derived payloads on save.
// Loading caches the immutable source report; edit state overrides that cache.
LevelDoomNodeReport inspectLevelDoomNodes(const LevelMapDocument& document, const std::function<bool()>& isCancelled = {});
QString levelDoomNodeStateId(LevelDoomNodeState state);
QJsonObject levelDoomNodeReportJson(const LevelDoomNodeReport& report);
struct LevelDoomWadNodeReport {
	QByteArray sourceHash;
	QMap<QString, LevelDoomNodeReport> maps;
	QStringList errors, warnings;
	bool cancelled = false;
};
// Reads and indexes one bounded WAD snapshot, then inspects requested groups.
// An empty map list inspects all maps. Duplicate/missing map labels refuse.
LevelDoomWadNodeReport inspectLevelDoomWadNodes(const QString& path, const QStringList& maps = {},
												const std::function<bool()>& isCancelled = {});
bool isDoomNodeProduct(const QString& name);
// The same conventional/custom marker recognition used by the map loader.
bool isLevelDoomMapMarkerAt(const QVector<LevelMapWadSourceLump>& lumps, int index);
// Invalidates selected-map runtime records and removes its separately labelled
// GL cache. Other maps/resources remain unchanged. Ambiguous GL labels refuse.
bool invalidateLevelDoomNodeProducts(QVector<LevelMapWadSourceLump>* lumps, int mapMarker, QStringList* invalidated, QString* error);
} // namespace vibestudio
