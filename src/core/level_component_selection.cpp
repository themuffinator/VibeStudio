#include "core/level_component_selection.h"
#include <QHash>
#include <algorithm>
#include <cmath>

namespace vibestudio {
QSet<int> selectLevelComponentHandles(const QSet<int>& current,const QSet<int>& hits,LevelComponentPick operation,bool groupToggle)
{
	if (operation == LevelComponentPick::Replace) { return hits; }
	auto result = current;
	if (operation == LevelComponentPick::Add) { return result.unite(hits); }
	if (operation == LevelComponentPick::Subtract) { return result.subtract(hits); }
	const bool removeGroup = groupToggle && std::all_of(hits.cbegin(),hits.cend(),[&](int id) { return current.contains(id); });
	for (int id : hits) {
		if (groupToggle ? removeGroup : current.contains(id)) { result.remove(id); } else { result.insert(id); }
	}
	return result;
}
std::optional<LevelComponentPick> levelComponentPickOperation(Qt::KeyboardModifiers keys,Qt::KeyboardModifiers toggle,
	Qt::KeyboardModifiers add,bool band,bool bandToggles)
{
	if (toggle == Qt::NoModifier) { toggle = Qt::ControlModifier; }
	if (add == Qt::NoModifier || add == toggle) { add = toggle == Qt::ShiftModifier ? Qt::ControlModifier : Qt::ShiftModifier; }
	if (keys == toggle) { return band && bandToggles ? LevelComponentPick::Add : LevelComponentPick::Toggle; }
	if (keys == add) { return LevelComponentPick::Add; }
	// A custom toggle may already contain the additive modifier. Its exact
	// configured role wins over our combined-key subtraction alternative.
	if (keys == (toggle|add)) { return LevelComponentPick::Subtract; }
	if (keys == Qt::NoModifier) { return band && bandToggles ? LevelComponentPick::Toggle : LevelComponentPick::Replace; }
	return {};
}
QString levelComponentGeometryKey(const QVector<LevelMapVec3>& vertices)
{
	if (vertices.isEmpty()) { return {}; }
	QStringList parts;
	for (const auto& p : vertices) {
		if (!p.valid || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
			|| std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)}) > 32768) { return {}; }
		parts << QStringLiteral("%1,%2,%3").arg(std::llround(p.x*1e6)).arg(std::llround(p.y*1e6)).arg(std::llround(p.z*1e6));
	}
	std::sort(parts.begin(),parts.end()); return parts.join(';');
}
LevelComponentCycleResult cycleLevelComponentHits(const QVector<LevelComponentCycleHit>& hits,
	const QSet<int>& picked, int steps)
{
	LevelComponentCycleResult result; result.picked = picked;
	if (!steps || hits.size() > 16384) { return result; }
	struct Group { QVector<int> handles; double depth = 0, distance = 0; int first = -1; bool selected = false; };
	QVector<Group> groups; groups.reserve(hits.size());
	QHash<QString,int> grouped; grouped.reserve(hits.size()); QSet<int> seen; seen.reserve(hits.size());
	for (const auto& hit : hits) {
		if (hit.handle < 0 || !std::isfinite(hit.depth) || !std::isfinite(hit.distanceSquared) || hit.distanceSquared < 0 || seen.contains(hit.handle)) { continue; }
		seen.insert(hit.handle);
		const auto key = hit.geometryKey.isEmpty() ? QStringLiteral("handle:%1").arg(hit.handle) : QStringLiteral("geometry:")+hit.geometryKey;
		auto entry = grouped.constFind(key);
		const int index = entry == grouped.cend() ? int(groups.size()) : entry.value();
		if (index == groups.size()) { grouped.insert(key,index); groups.append(Group{}); }
		auto& group = groups[index];
		if (group.handles.isEmpty()) { group.depth = hit.depth; group.distance = hit.distanceSquared; group.first = hit.handle; }
		else { group.depth = std::max(group.depth,hit.depth); group.distance = std::min(group.distance,hit.distanceSquared); group.first = std::min(group.first,hit.handle); }
		group.handles.append(hit.handle); group.selected = group.selected || picked.contains(hit.handle);
	}
	std::sort(groups.begin(),groups.end(),[](const Group& a,const Group& b) {
		if (a.depth != b.depth) { return a.depth > b.depth; }
		if (a.distance != b.distance) { return a.distance < b.distance; }
		return a.first < b.first;
	});
	result.count = int(groups.size()); if (!result.count) { return result; }
	result.picked.subtract(seen);
	int current = -1, selectedGroups = 0;
	for (int i = 0; i < groups.size(); ++i) {
		if (groups[i].selected) { current = i; ++selectedGroups; }
	}
	if (selectedGroups != 1) { current = steps > 0 ? -1 : 0; }
	const int next = int(((qint64(current)+steps)%result.count+result.count)%result.count);
	result.ordinal = next+1; result.handle = groups[next].first;
	for (int handle : groups[next].handles) { result.picked.insert(handle); } return result;
}
void LevelComponentCycleCursor::aim(const QPointF& position)
{
	if (!std::isfinite(position.x()) || !std::isfinite(position.y())) { reset(); return; }
	if (!point || std::hypot(position.x()-point->x(),position.y()-point->y()) > 4) { remainder = 0; clearResult(); }
	point = position;
}
int LevelComponentCycleCursor::wheelSteps(int delta)
{
	remainder = std::clamp(remainder-double(delta)/120.0,-256.0,256.0);
	const int steps = int(remainder); remainder -= steps; return steps;
}
} // namespace vibestudio
