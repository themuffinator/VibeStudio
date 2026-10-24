#include "core/level_component_selection.h"
#include <QCoreApplication>
#include <QMap>
#include <algorithm>

namespace vibestudio {
namespace {
using Lists = QVector<QVector<int>>;
bool fail(QString* error, const char* message) {
	if (error) { *error = QCoreApplication::translate("LevelComponentSelection",message); } return false;
}
bool validKind(LevelBrushComponent kind) { return kind >= LevelBrushComponent::Vertex && kind <= LevelBrushComponent::Face; }
int count(const LevelComponentSelectionTopology& t, LevelBrushComponent kind) {
	return kind == LevelBrushComponent::Vertex ? t.vertexCount : int(kind == LevelBrushComponent::Edge ? t.edges.size() : t.faces.size());
}
quint64 edgeKey(int a, int b) { return (quint64(std::min(a,b)) << 32) | quint32(std::max(a,b)); }
struct Incidence {
	Lists vertexEdges, vertexFaces, edgeFaces, faceEdges;
};
bool incidence(const LevelComponentSelectionTopology& t, Incidence* out, QString* error) {
	if (t.object.objectId < 0 || t.vertexCount < 1 || t.vertexCount > 16384) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Invalid component topology or object ID.")); }
	if (t.object.kind == LevelMapSelectionKind::QuakePatch) {
		return (t.patchWidth >= 2 && t.patchHeight >= 2 && qint64(t.patchWidth)*t.patchHeight == t.vertexCount)
			|| fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Patch control grid dimensions do not match its points."));
	}
	if (t.object.kind != LevelMapSelectionKind::QuakeBrush || t.vertexCount > kLevelBrushComponentMaxVertices
		|| t.faces.size() < 4 || t.faces.size() > kLevelBrushComponentMaxFaces || t.edges.size() > 3*t.vertexCount-6) {
		return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Invalid solved brush topology."));
	}
	out->vertexEdges.resize(t.vertexCount); out->vertexFaces.resize(t.vertexCount);
	out->edgeFaces.resize(t.edges.size()); out->faceEdges.resize(t.faces.size());
	QHash<quint64,int> edges;
	for (int i = 0; i < t.edges.size(); ++i) {
		const auto e = t.edges[i];
		if (e[0] < 0 || e[1] < 0 || e[0] >= t.vertexCount || e[1] >= t.vertexCount || e[0] == e[1] || edges.contains(edgeKey(e[0],e[1]))) {
			return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Invalid or duplicate solved brush edge."));
		}
		edges.insert(edgeKey(e[0],e[1]),i); out->vertexEdges[e[0]] << i; out->vertexEdges[e[1]] << i;
	}
	for (int f = 0; f < t.faces.size(); ++f) {
		const auto& face = t.faces[f]; QSet<int> used;
		if (face.size() < 3 || face.size() > t.vertexCount) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Invalid solved brush face.")); }
		for (int v : face) {
			if (v < 0 || v >= t.vertexCount || used.contains(v)) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Invalid or duplicate face corner.")); }
			used.insert(v); out->vertexFaces[v] << f;
		}
		for (int i = 0; i < face.size(); ++i) {
			const auto found = edges.constFind(edgeKey(face[i],face[(i+1)%face.size()]));
			if (found == edges.cend()) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","A face boundary is missing its solved edge.")); }
			out->faceEdges[f] << found.value(); out->edgeFaces[found.value()] << f;
		}
	}
	if (std::any_of(out->edgeFaces.cbegin(),out->edgeFaces.cend(),[](const auto& faces) { return faces.size() != 2; })
		|| std::any_of(out->vertexEdges.cbegin(),out->vertexEdges.cend(),[](const auto& edgesAtVertex) { return edgesAtVertex.size() < 3; })) {
		return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Component selection requires a closed solved brush."));
	}
	return true;
}
Lists adjacency(const LevelComponentSelectionTopology& t, const Incidence& in, LevelBrushComponent kind) {
	Lists result(count(t,kind));
	if (t.object.kind == LevelMapSelectionKind::QuakePatch) {
		for (int v = 0; v < t.vertexCount; ++v) {
			const int x = v%t.patchWidth, y = v/t.patchWidth;
			if (x) { result[v] << v-1; } if (x+1 < t.patchWidth) { result[v] << v+1; }
			if (y) { result[v] << v-t.patchWidth; } if (y+1 < t.patchHeight) { result[v] << v+t.patchWidth; }
		}
	} else if (kind == LevelBrushComponent::Vertex) {
		for (const auto& e : t.edges) { result[e[0]] << e[1]; result[e[1]] << e[0]; }
	} else {
		const auto& shared = kind == LevelBrushComponent::Edge ? in.vertexEdges : in.edgeFaces;
		for (const auto& incident : shared) {
			for (int a : incident) { for (int b : incident) { if (a != b) { result[a] << b; } } }
		}
		for (auto& neighbours : result) { std::sort(neighbours.begin(),neighbours.end()); neighbours.erase(std::unique(neighbours.begin(),neighbours.end()),neighbours.end()); }
	}
	return result;
}
QSet<int> converted(const LevelComponentSelectionTopology& t, const Incidence& in, const QSet<int>& picked,
	LevelBrushComponent source, LevelBrushComponent target, bool touching) {
	if (source == target || t.object.kind == LevelMapSelectionKind::QuakePatch) { return picked; }
	QSet<int> result;
	for (int id = 0; id < count(t,target); ++id) {
		QVector<int> incident;
		if (target == LevelBrushComponent::Vertex) { incident = source == LevelBrushComponent::Edge ? in.vertexEdges[id] : in.vertexFaces[id]; }
		else if (target == LevelBrushComponent::Edge) {
			incident = source == LevelBrushComponent::Vertex ? QVector<int>{t.edges[id][0],t.edges[id][1]} : in.edgeFaces[id];
		} else { incident = source == LevelBrushComponent::Vertex ? t.faces[id] : in.faceEdges[id]; }
		const auto selected = [&](int component) { return picked.contains(component); };
		if (source < target && !touching ? std::all_of(incident.cbegin(),incident.cend(),selected) : std::any_of(incident.cbegin(),incident.cend(),selected)) { result.insert(id); }
	}
	return result;
}
QSet<int> extended(const LevelComponentSelectionTopology& t, const Incidence& in, const QSet<int>& picked,
	LevelBrushComponent kind, const LevelComponentSelectionRequest& request) {
	const auto graph = adjacency(t,in,kind); auto result = picked;
	if (request.operation == LevelComponentSelectionOperation::Shrink) {
		for (int step = 0; step < request.steps && !result.isEmpty(); ++step) {
			auto remaining = result;
			for (int id : result) {
				if ((t.object.kind == LevelMapSelectionKind::QuakePatch && graph[id].size() < 4)
					|| std::any_of(graph[id].cbegin(),graph[id].cend(),[&](int neighbour) { return !result.contains(neighbour); })) { remaining.remove(id); }
			}
			if (remaining == result) { break; } result = std::move(remaining);
		}
	} else {
		auto frontier = picked;
		const int steps = request.operation == LevelComponentSelectionOperation::Connected ? int(graph.size()) : request.steps;
		for (int step = 0; step < steps && !frontier.isEmpty(); ++step) {
			QSet<int> next;
			for (int id : frontier) { for (int neighbour : graph[id]) { if (!result.contains(neighbour)) { next.insert(neighbour); } } }
			result.unite(next); frontier = std::move(next);
		}
	}
	return result;
}
} // namespace
LevelComponentSelectionTopology levelComponentSelectionTopology(const LevelMapSelectionRef& object, const LevelBrushTopology& topology) {
	return {object,int(topology.vertices.size()),topology.edges,topology.faces,0,0};
}
LevelComponentSelectionTopology levelComponentSelectionTopology(const LevelMapPatch& patch) {
	return {{LevelMapSelectionKind::QuakePatch,patch.id},int(patch.controlPoints.size()),{},{},patch.width,patch.height};
}
bool queryLevelComponentSelection(const QVector<LevelComponentSelectionTopology>& topology, const QVector<LevelMapComponentSet>& input,
	const LevelComponentSelectionRequest& request, QVector<LevelMapComponentSet>* output, QString* error) {
	if (error) { error->clear(); }
	if (!output || !validKind(request.target) || request.operation < LevelComponentSelectionOperation::Convert
		|| request.operation > LevelComponentSelectionOperation::Connected || request.steps < 1 || request.steps > 256
		|| ((request.operation == LevelComponentSelectionOperation::Convert || request.operation == LevelComponentSelectionOperation::Connected) && request.steps != 1)
		|| (request.operation != LevelComponentSelectionOperation::Convert && request.touching)) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Invalid component selection operation or step count (1–256).")); }
	if (input.size() > 256 || topology.size() > 256) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Select components on at most 256 objects per query.")); }
	QMap<QString,LevelMapComponentSet> merged;
	for (const auto& set : input) {
		if (!validKind(set.kind) || set.components.size() > 16384) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Invalid component kind or too many component IDs.")); }
		const auto key = levelMapSelectionRefId(set.object);
		if (merged.contains(key) && merged[key].kind != set.kind) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Use one component kind per object in a selection query.")); }
		if (!merged.contains(key)) { merged.insert(key,set); }
		else { merged[key].components += set.components; }
		if (merged[key].components.size() > 16384) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Too many component IDs for one object.")); }
	}
	QVector<LevelMapComponentSet> candidate;
	for (const auto& set : merged) {
		const auto found = std::find_if(topology.cbegin(),topology.cend(),[&](const auto& t) { return t.object == set.object; });
		if (found == topology.cend()) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","A selected object is missing or unavailable in this component context.")); }
		Incidence in; if (!incidence(*found,&in,error)) { return false; }
		if (found->object.kind == LevelMapSelectionKind::QuakePatch && set.kind != LevelBrushComponent::Vertex) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Patches select control points with kind vertex.")); }
		QSet<int> picked;
		for (int id : set.components) { if (id < 0 || id >= count(*found,set.kind)) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","A component ID is stale or outside the solved topology.")); } picked.insert(id); }
		const auto target = request.operation == LevelComponentSelectionOperation::Convert && found->object.kind != LevelMapSelectionKind::QuakePatch ? request.target : set.kind;
		const auto selected = request.operation == LevelComponentSelectionOperation::Convert ? converted(*found,in,picked,set.kind,target,request.touching) : extended(*found,in,picked,set.kind,request);
		if (!selected.isEmpty()) {
			QVector<int> ids(selected.cbegin(),selected.cend()); std::sort(ids.begin(),ids.end()); candidate << LevelMapComponentSet{set.object,target,ids};
		}
	}
	std::sort(candidate.begin(),candidate.end(),[](const auto& a,const auto& b) { return a.object.kind != b.object.kind ? a.object.kind < b.object.kind : a.object.objectId < b.object.objectId; });
	*output = std::move(candidate); return true;
}
bool selectLevelMapComponents(const LevelMapDocument& document, const QVector<LevelMapComponentSet>& input,
	const LevelComponentSelectionRequest& request, QVector<LevelMapComponentSet>* output, QString* error) {
	if (input.size() > 256 || (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map)) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Component selection requires a Quake-family map and at most 256 objects.")); }
	QVector<LevelComponentSelectionTopology> topology; QSet<QString> seen;
	for (const auto& set : input) {
		const auto key = levelMapSelectionRefId(set.object); if (seen.contains(key)) { continue; } seen.insert(key);
		if (set.object.kind == LevelMapSelectionKind::QuakeBrush) {
			const auto brush = std::find_if(document.brushes.cbegin(),document.brushes.cend(),[&](const auto& b) { return b.id == set.object.objectId; });
			LevelBrushTopology solved;
			if (brush == document.brushes.cend()) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Selected brush does not exist.")); }
			if (!levelBrushTopology(*brush,&solved,error)) { return false; }
			topology << levelComponentSelectionTopology(set.object,solved);
		} else if (set.object.kind == LevelMapSelectionKind::QuakePatch) {
			const auto patch = std::find_if(document.patches.cbegin(),document.patches.cend(),[&](const auto& p) { return p.id == set.object.objectId; });
			if (patch == document.patches.cend()) { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Selected patch does not exist.")); }
			topology << levelComponentSelectionTopology(*patch);
		} else { return fail(error,QT_TRANSLATE_NOOP("LevelComponentSelection","Select brush components or patch control points.")); }
	}
	return queryLevelComponentSelection(topology,input,request,output,error);
}
} // namespace vibestudio
