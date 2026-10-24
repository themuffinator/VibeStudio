#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_linked_groups.h"
#include "core/map_preview_mesh.h"
#include "core/level_texture_mapping.h"
#include "core/model_transform.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
QString number(double v) { return QString::number(v, 'g', 12); }
QString point(const LevelMapVec3 &p) { return QStringLiteral("( %1 %2 %3 )").arg(number(p.x), number(p.y), number(p.z)); }
QByteArray fixture(const QString &kind, bool naturalAxes = false)
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &document);
	addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/stone"));
	QStringList lines{QStringLiteral("// Outside comment"), QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\""),
					  QStringLiteral("\"custom\" \"keep this\""), QStringLiteral("{")};
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		lines << kind << QStringLiteral("{");
	}
	lines << QStringLiteral("// inside brush") << QStringLiteral("/* block comment retained */");
	int i = 0;
	for (const auto &f : document.brushes.first().faces) {
		const auto p = planeFromPoints(f.p0, f.p1, f.p2);
		QString geometry = QStringLiteral("%1 %2 %3").arg(point(f.p0), point(f.p1), point(f.p2));
		const QString material = QStringLiteral("studio/face%1").arg(i++);
		if (kind == QStringLiteral("brushDef3")) {
			geometry = QStringLiteral("( %1 %2 %3 %4 )").arg(number(p.normalX), number(p.normalY), number(p.normalZ), number(-p.distance));
		}
		if (kind.startsWith(QStringLiteral("brushDef"))) {
			lines << geometry + QStringLiteral(" ( ( 0.125 0.025 0.25 ) ( 0.05 0.25 0.75 ) "
											   ") \"%1\" 2 4 8 // face note %2")
									.arg(material)
									.arg(i);
		} else if (kind == QStringLiteral("valve220")) {
			const QString axes = naturalAxes && std::abs(p.normalX) > 0.9 ? QStringLiteral("[ 0 1 0 7 ] [ 0 0 -1 9 ]")
				: naturalAxes && std::abs(p.normalZ) > 0.9 ? QStringLiteral("[ 1 0 0 7 ] [ 0 1 0 9 ]") : QStringLiteral("[ 1 0 0 7 ] [ 0 0 -1 9 ]");
			lines << geometry + QStringLiteral(" %1 %3 15 0.5 "
											   "2 2 4 8 // face note %2")
									.arg(material)
									.arg(i).arg(axes);
		} else {
			lines << geometry + QStringLiteral(" %1 7 9 15 0.5 2 2 4 8 // face note %2").arg(material).arg(i);
		}
	}
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		lines << QStringLiteral("}");
	}
	lines << QStringLiteral("}") << QStringLiteral("}") << QStringLiteral("// untouched entity")
		  << QStringLiteral("{\n\"classname\" \"info_player_start\"\n\"origin\" "
							"\"128 128 128\"\n}")
		  << QString();
	return lines.join(QLatin1Char('\n')).toUtf8();
}
bool load(const QByteArray &bytes, LevelMapDocument *map, QString *error)
{
	return loadLevelMapBytes({QStringLiteral("components.map"), {}, QStringLiteral("idtech3")}, bytes, map, error);
}
bool sameGeometry(const LevelMapBrush &a, const LevelMapBrush &b)
{
	LevelBrushTopology x, y;
	if (!levelBrushTopology(a, &x) || !levelBrushTopology(b, &y) || x.vertices.size() != y.vertices.size()) {
		return false;
	}
	for (const auto &p : x.vertices) {
		if (std::none_of(y.vertices.cbegin(), y.vertices.cend(), [&](const auto &q) {
				return std::abs(p.x - q.x) < 0.02 && std::abs(p.y - q.y) < 0.02 && std::abs(p.z - q.z) < 0.02;
			})) {
			return false;
		}
	}
	return true;
}

bool topologyEditing(const QByteArray &original)
{
	LevelMapDocument map;
	QString error;
	if (!expect(load(original, &map, &error), "load topology fixture", error)) {
		return false;
	}
	bool ok = true;
	for (bool insertion : {false, true}) {
		auto brush = map.brushes.first();
		LevelBrushEditReport report;
		const bool edited = insertion ? insertLevelBrushVertices(&brush, {{32, 32, 80, true}}, 0, false, &report, &error)
			: removeLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {7}, &report, &error);
		if (!expect(edited, "insert/remove a corner", error)) {
			return false;
		}
		ok &= expect(report.changed && report.verticesBefore == 8 && report.verticesAfter == (insertion ? 9 : 7)
			&& report.facesAfter == (insertion ? 9 : 7) && report.collapsedVertices == 0
			&& report.insertedVertices == (insertion ? 1 : 0) && report.removedVertices == (insertion ? 0 : 1), "topology edit counts");
		ok &= expect(replaceLevelMapBrushGeometry(&map, 0, brush, &error), "commit topology edit", error);
		const auto saved = serializeLevelMap(map);
		LevelMapDocument restored;
		ok &= expect(saved.succeeded() && load(saved.bytes, &restored, &error) && sameGeometry(brush, restored.brushes.first()),
			"topology edit round trips through native parser", error);
		ok &= expect(saved.bytes.contains("// Outside comment") && saved.bytes.contains("/* block comment retained */")
			&& saved.bytes.contains("// untouched entity") && restored.brushes.first().primitiveKind == brush.primitiveKind,
			"topology edits preserve dialect, unrelated entities and comments");
		for (int note = 1; note <= 6; ++note) {
			ok &= expect(saved.bytes.count(QByteArray("// face note ") + QByteArray::number(note)) == 1, "topology edit keeps each face comment once");
		}
		for (const auto &face : restored.brushes.first().faces) {
			ok &= expect(face.contentFlags == 2 && face.surfaceFlags == 4 && face.surfaceValue == 8
				&& face.textureName.startsWith(QStringLiteral("studio/face")), "topology edit keeps surface metadata");
			ok &= expect(face.explicitTextureMatrix ? face.textureMatrix[0] == 0.125 && face.textureMatrix[5] == 0.75
				: face.scaleX == 0.5 && face.scaleY == 2 && face.rotation == 15, "topology edit retains texture parameters");
		}
		ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "topology undo is exact", error);
		ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == saved.bytes, "topology redo is exact", error);
		ok &= expect(undoLevelMapEdit(&map, &error), "return to topology fixture", error);
	}
	return ok;
}

bool extrusionChecks()
{
	bool ok = true; QString error;
	for (const auto& dialect : {QStringLiteral("classic"),QStringLiteral("valve220"),QStringLiteral("brushDef"),QStringLiteral("brushDef3")}) {
		LevelMapDocument source;
		if (!load(fixture(dialect,true),&source,&error)) { return false; }
		setLevelMapSelection(&source,{{LevelMapSelectionKind::QuakeBrush,0}});
		const auto bytes = serializeLevelMap(source).bytes;
		LevelBrushTopology before; levelBrushTopology(source.brushes[0],&before);
		for (int face = 0; face < 6; ++face) { for (double distance : {-16.0,16.0}) { for (bool split : {false,true}) {
			auto map = source; LevelMapExtrusionReport report;
			if (!expect(extrudeLevelMapFaces(&map,{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{face}}},distance,split,true,&report,&error),"extrude every face/direction/split/dialect",error)) { return false; }
			ok &= expect(report.changedBrushes == 1 && report.createdBrushes == (split ? 1 : 0) && map.brushes.size() == (split ? 2 : 1) && map.undoStack.size() == 1,"extrusion counts and one undo step");
			const auto serialized = serializeLevelMap(map).bytes; LevelMapDocument reopened;
			ok &= expect(load(serialized,&reopened,&error) && reopened.brushes.size() == map.brushes.size(),"extrusion native reopen",error);
			for (int i = 0; i < map.brushes.size(); ++i) {
				ok &= expect(sameGeometry(map.brushes[i],reopened.brushes[i]) && reopened.brushes[i].faces.size() == 6,"extrusion keeps closed six-plane boxes",error);
				for (const auto& f : reopened.brushes[i].faces) { ok &= expect(f.contentFlags == 2 && f.surfaceFlags == 4 && f.surfaceValue == 8,"extrusion retains native flags"); }
			}
			const auto active = report.faces[0];
			const auto found = std::find_if(map.brushes.cbegin(),map.brushes.cend(),[&](const auto& b) { return b.id == active.object.objectId; });
			const auto from = levelTextureProjection(source.brushes[0].faces[face]);
			const auto to = levelTextureProjection(found->faces[active.components[0]]);
			const auto plane = before.geometry.faces[face].plane;
			for (auto p : before.geometry.faces[face].points) {
				const auto expected = from.at(p); p.x += plane.normalX*distance; p.y += plane.normalY*distance; p.z += plane.normalZ*distance;
				const auto actual = to.at(p);
				ok &= expect(std::abs(actual.x()-expected.x()) < 1e-5 && std::abs(actual.y()-expected.y()) < 1e-5,"extruded cap retains UVs",dialect);
			}
			if (split) {
				const auto a = solveBrushGeometry(reopened.brushes[0].faces), b = solveBrushGeometry(reopened.brushes[1].faces);
				int caps = 0;
				for (int i = 0; i < a.faces.size(); ++i) { for (int j = 0; j < b.faces.size(); ++j) {
					const auto x = a.faces[i].plane, y = b.faces[j].plane;
					if (std::abs(x.normalX+y.normalX)+std::abs(x.normalY+y.normalY)+std::abs(x.normalZ+y.normalZ) > 1e-6 || std::abs(x.distance+y.distance) > 0.001) { continue; }
					++caps; const auto left = levelTextureProjection(reopened.brushes[0].faces[i]), right = levelTextureProjection(reopened.brushes[1].faces[j]);
					for (const auto& p : a.faces[i].points) {
						const auto uv = left.at(p)-right.at(p);
						ok &= expect(std::abs(uv.x()) < 1e-5 && std::abs(uv.y()) < 1e-5,"reversed split caps keep matching UVs after reopen",dialect);
					}
				} }
				ok &= expect(caps == 1,"split sections meet at exactly one reversed cap");
			}
			ok &= expect(serialized.count("// untouched entity") == 1 && serialized.contains("// Outside comment"),"extrusion preserves unrelated source content");
			ok &= expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == bytes,"extrusion undo exact",error);
			ok &= expect(redoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == serialized,"extrusion redo exact",error);
		} } }
		for (double distance : {-64.0,-80.0,40000.0,std::numeric_limits<double>::quiet_NaN()}) {
			auto map = source;
			ok &= expect(!extrudeLevelMapFaces(&map,{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{0}}},distance,false,true,nullptr,&error)
				&& serializeLevelMap(map).bytes == bytes && map.undoStack.isEmpty(),"invalid extrusion leaves map unchanged",error);
		}
	}
	const double root = std::sqrt(0.5);
	ok &= expect(std::abs(snapLevelBrushExtrusionDistance(11.2,{root,root,0,true},{{0,0,0,true}},8)-8/root) < 1e-8,"sloped extrusion snaps a vertex to grid without bending the normal");
	ModelMoveDrag drag;
	ok &= expect(beginModelAxisMoveDrag({0,0,0},{0,0,-1},{float(root),float(root),0},{{0,0,10},{0,0,-1}},&drag),"arbitrary normal drag starts");
	double distance = 0;
	ok &= expect(modelAxisMoveDragDistance(drag,{{8,8,10},{0,0,-1}},&distance) && std::abs(distance-8/root) < 1e-5,"normal drag projects onto slope");
	return ok;
}

int normalFace(const LevelMapBrush& brush, double x, double y, double z)
{
	const auto geometry = solveBrushGeometry(brush.faces);
	for (int i = 0; i < geometry.faces.size(); ++i) {
		const auto n = geometry.faces[i].plane;
		if (n.normalX*x+n.normalY*y+n.normalZ*z > 0.99) { return i; }
	}
	return -1;
}
double brushVolume(const LevelMapBrush& brush)
{
	const auto geometry = solveBrushGeometry(brush.faces);
	double volume = 0;
	for (const auto& face : geometry.faces) {
		const auto& a = face.points[0];
		for (int i = 1; i+1 < face.points.size(); ++i) {
			const auto& b = face.points[i]; const auto& c = face.points[i+1];
			volume += a.x*(b.y*c.z-b.z*c.y) + a.y*(b.z*c.x-b.x*c.z) + a.z*(b.x*c.y-b.y*c.x);
		}
	}
	return std::abs(volume/6);
}
bool extrusionTransactions()
{
	bool ok = true; QString error; LevelMapDocument source;
	if (!load(fixture(QStringLiteral("classic"),true),&source,&error)) { return false; }
	const auto face = normalFace(source.brushes[0],1,0,0);
	const QVector<LevelMapComponentSet> one{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{face}}};
	ok &= addLevelMapBoxBrush(&source,{128,0,0,true},{192,64,64,true},QStringLiteral("studio/other"),nullptr,&error);
	const int opposite = normalFace(source.brushes[1],-1,0,0);
	const auto bytes = serializeLevelMap(source).bytes;
	for (const auto& second : {LevelMapComponentSet{{LevelMapSelectionKind::QuakeBrush,1},LevelBrushComponent::Face,{-1}},
		LevelMapComponentSet{{LevelMapSelectionKind::QuakeBrush,1},LevelBrushComponent::Face,{normalFace(source.brushes[1],0,0,1)}},
		LevelMapComponentSet{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{face}},
		LevelMapComponentSet{{LevelMapSelectionKind::QuakeBrush,1},LevelBrushComponent::Face,{face,opposite}}}) {
		auto map = source; LevelMapExtrusionReport report; report.changedBrushes = 99;
		ok &= expect(!extrudeLevelMapFaces(&map,{one[0],second},8,false,true,&report,&error) && report.changedBrushes == 0
			&& serializeLevelMap(map).bytes == bytes && map.undoStack.size() == source.undoStack.size(),"invalid multi-brush extrusion is atomic",error);
	}
	const QVector<LevelMapComponentSet> opposed{one[0],{{LevelMapSelectionKind::QuakeBrush,1},LevelBrushComponent::Face,{opposite}}};
	auto map = source;
	ok &= expect(extrudeLevelMapFaces(&map,opposed,8,false,true,nullptr,&error),"opposed faces can move together",error);
	ok &= expect(std::abs(map.brushes[0].maxs.x-72) < 0.001 && std::abs(map.brushes[1].mins.x-136) < 0.001,"opposed faces follow the first face's direction");
	map = source;
	ok &= expect(!extrudeLevelMapFaces(&map,opposed,8,true,true,nullptr,&error) && serializeLevelMap(map).bytes == bytes,"opposed split is refused atomically",error);
	ok &= expect(!extrudeLevelMapFaces(&map,opposed,80,false,true,nullptr,&error) && serializeLevelMap(map).bytes == bytes,"second brush collapse rolls back valid first extrusion",error);

	// Replacement identities may only reuse brushes removed by that transaction.
	ok &= extrudeLevelMapFaces(&map,one,8,true,true,nullptr,&error) && undoLevelMapEdit(&map,&error);
	for (bool duplicate : {false,true}) {
		auto corrupt = map; auto& command = corrupt.redoStack.last();
		command.brushResults.last().id = duplicate ? command.brushResults.first().id : 1;
		ok &= expect(!redoLevelMapEdit(&corrupt,&error) && serializeLevelMap(corrupt).bytes == bytes,"colliding replacement identities are refused before mutation",error);
	}

	// Split pieces retain their owner and inherited scene membership, including
	// the unchanged original ID. Linked copies participate in the same undo step.
	for (bool owner : {false,true}) {
		map = source; setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakeBrush,0}});
		int entity = 0;
		if (owner) { ok &= tieLevelMapSelectionToEntity(&map,QStringLiteral("func_detail"),{},&entity,&error); }
		const auto original = std::find_if(map.brushes.cbegin(),map.brushes.cend(),[entity](const auto& b) { return b.entityId == entity; });
		if (original == map.brushes.cend()) { return false; }
		const int brushId = original->id;
		const QVector<LevelMapComponentSet> grouped{{{LevelMapSelectionKind::QuakeBrush,brushId},LevelBrushComponent::Face,{normalFace(*original,1,0,0)}}};
		QString group,copy;
		ok &= createLevelSceneNode(&map,LevelSceneNodeKind::Group,QStringLiteral("Extrusion"),{},&group,&error);
		ok &= assignLevelSceneObjects(&map,group,{owner ? QStringLiteral("entity:%1").arg(entity) : QStringLiteral("brush:%1").arg(brushId)},&error);
		const auto baseline = map; const auto baselineBytes = serializeLevelMap(map).bytes;
		ok &= setLevelSceneLocked(&map,group,true,&error);
		const auto locked = serializeLevelMap(map).bytes; LevelMapExtrusionReport report; report.createdBrushes = 99;
		ok &= expect(!extrudeLevelMapFaces(&map,grouped,8,true,true,&report,&error) && report.createdBrushes == 0
			&& serializeLevelMap(map).bytes == locked,"extrusion cannot change a locked brush or owner",error);
		map = baseline;
		if (!expect(extrudeLevelMapFaces(&map,grouped,8,true,true,&report,&error),"grouped split",error)) { return false; }
		for (int id : report.brushIds) {
			const auto it = std::find_if(map.brushes.cbegin(),map.brushes.cend(),[id](const auto& b) { return b.id == id; });
			ok &= expect(it != map.brushes.cend() && it->entityId == entity,"split preserves brush entity ownership");
			ok &= expect(levelSceneMembership(map.scene,owner ? QStringLiteral("entity:%1").arg(entity) : QStringLiteral("brush:%1").arg(id)) == group,"split preserves scene membership");
		}
		const auto saved = serializeLevelMap(map).bytes; LevelMapDocument reopened;
		ok &= expect(load(saved,&reopened,&error) && validateLevelScene(reopened,reopened.scene,&error),"grouped extrusion scene survives reopen",error);
		ok &= expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == baselineBytes,"grouped split undo restores exact scene",error);
		if (!expect(createLinkedLevelGroup(&map,group,{0,192,0,true},&copy,&error),"create linked extrusion fixture",error)) { return false; }
		const auto linkedBytes = serializeLevelMap(map).bytes; const auto depth = map.undoStack.size();
		ok &= expect(extrudeLevelMapFaces(&map,grouped,8,true,true,&report,&error) && map.brushes.size() == 5
			&& map.undoStack.size() == depth+1,"linked extrusion creates both slabs as one undo step",error);
		ok &= expect(levelLinkedContent(map,levelSceneNode(map.scene,group)->objects).shape == levelLinkedContent(map,levelSceneNode(map.scene,copy)->objects).shape,"linked split geometry remains equivalent");
		ok &= expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == linkedBytes,"linked extrusion undo is exact",error);
	}

	// Tapered brushes exercise moving planes instead of translating corners.
	map = source; auto wedge = source.brushes[0]; const int top = normalFace(wedge,0,0,1);
	for (auto* p : {&wedge.faces[top].p0,&wedge.faces[top].p1,&wedge.faces[top].p2}) { p->z = 64-p->x*0.5; }
	ok &= replaceLevelMapBrushGeometry(&map,0,wedge,&error); wedge = map.brushes[0];
	LevelBrushTopology topology; ok &= levelBrushTopology(wedge,&topology,&error);
	const auto normal = topology.geometry.faces[top].plane;
	for (double amount : {-8.0,8.0}) { for (bool lock : {false,true}) {
		LevelBrushExtrusion split,whole;
		ok &= expect(prepareLevelBrushExtrusion(wedge,top,amount,true,lock,&split,&error)
			&& prepareLevelBrushExtrusion(wedge,top,amount,false,lock,&whole,&error),"tapered plane extrusion",error);
		if (split.brushes.size() != 2 || whole.brushes.size() != 1) { return false; }
		const double volume = brushVolume(split.brushes[0])+brushVolume(split.brushes[1]);
		ok &= expect(std::abs(volume-brushVolume(amount > 0 ? whole.brushes[0] : wedge)) < 0.01,"split pieces exactly partition the tapered volume");
		const auto from = levelTextureProjection(wedge.faces[top]);
		const auto to = levelTextureProjection(whole.brushes[0].faces[top]);
		for (auto p : topology.geometry.faces[top].points) {
			const auto initial = from.at(p); p.x += normal.normalX*amount; p.y += normal.normalY*amount; p.z += normal.normalZ*amount;
			const auto expected = lock ? initial : from.at(p); const auto actual = to.at(p);
			ok &= expect(std::abs(actual.x()-expected.x()) < 1e-5 && std::abs(actual.y()-expected.y()) < 1e-5,"sloped face respects texture lock");
		}
	} }
	LevelBrushExtrusion refused;
	ok &= expect(!prepareLevelBrushExtrusion(wedge,face,64,false,true,&refused,&error) && refused.brushes.isEmpty(),"extrusion refuses loss of an adjacent sloped face",error);
	return ok;
}

bool splitChecks()
{
	bool ok = true; QString error;
	for (const auto& dialect : {QStringLiteral("classic"),QStringLiteral("valve220"),QStringLiteral("brushDef"),QStringLiteral("brushDef3")}) {
		LevelMapDocument map;
		if (!load(fixture(dialect),&map,&error)) { return false; }
		const auto original = serializeLevelMap(map).bytes;
		LevelBrushTopology topology; levelBrushTopology(map.brushes[0],&topology);
		for (auto kind : {LevelBrushComponent::Edge,LevelBrushComponent::Face}) {
			const int count = static_cast<int>(kind == LevelBrushComponent::Edge ? topology.edges.size() : topology.faces.size());
			for (int id = 0; id < count; ++id) {
				const auto ids = levelBrushComponentVertices(topology,kind,{id});
				LevelMapVec3 center{0,0,0,true};
				for (int v : ids) { center.x += topology.vertices[v].x/ids.size(); center.y += topology.vertices[v].y/ids.size(); center.z += topology.vertices[v].z/ids.size(); }
				const LevelMapVec3 delta{(center.x-32)*0.5,(center.y-32)*0.5,(center.z-32)*0.5,true};
				const QVector<LevelMapComponentSet> sets{{{LevelMapSelectionKind::QuakeBrush,0},kind,{id}}};
				LevelMapComponentReport report;
				if (!expect(splitLevelMapComponents(&map,sets,delta,false,&report,&error),"split every edge and face in every dialect",error)) { return false; }
				ok &= expect(report.insertedVertices == 1 && report.brushes == 1 && report.collapsedVertices == 0 && map.undoStack.size() == 1,"split inserts exactly one corner and undo step");
				const auto saved = serializeLevelMap(map).bytes; LevelMapDocument reopened;
				ok &= expect(load(saved,&reopened,&error) && sameGeometry(map.brushes[0],reopened.brushes[0]),"split corners persist in native map",error);
				for (const auto& face : reopened.brushes[0].faces) {
					ok &= expect(face.contentFlags == 2 && face.surfaceFlags == 4 && face.surfaceValue == 8,"split face flags survive");
				}
				ok &= expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == original,"split undo restores exact source",error);
				ok &= expect(redoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == saved,"split redo restores exact result",error);
				undoLevelMapEdit(&map,&error);
				LevelBrushEditReport edit; auto brush = map.brushes[0];
				ok &= expect(splitLevelBrushComponents(&brush,kind,{id},{0,0,0,true},0,false,&edit,&error) && !edit.changed,"zero split preview is a no-op",error);
				ok &= expect(!splitLevelBrushComponents(&brush,kind,{id},{-delta.x,-delta.y,-delta.z,true},0,true,&edit,&error)
					&& sameGeometry(brush,map.brushes[0]),"interior split cannot silently discard a requested corner",error);
			}
		}
		auto brush = map.brushes[0];
		for (const auto& ids : {QVector<int>{},QVector<int>{0,0},QVector<int>{-1},QVector<int>{999}}) {
			ok &= expect(!splitLevelBrushComponents(&brush,LevelBrushComponent::Edge,ids,{-16,-16,0,true},0,false,nullptr,&error)
				&& sameGeometry(brush,map.brushes[0]),"invalid split indices are atomic",error);
		}
		ok &= expect(!splitLevelBrushComponents(&brush,LevelBrushComponent::Vertex,{0},{16,16,0,true},0,false,nullptr,&error),"a vertex cannot be split",error);
		ok &= expect(!splitLevelBrushComponents(&brush,LevelBrushComponent::Edge,{0},{16,16,0,false},0,false,nullptr,&error),"invalid split vector refused",error);
		ok &= expect(!splitLevelBrushComponents(&brush,LevelBrushComponent::Edge,{0},{-16,-16,0,true},std::numeric_limits<double>::infinity(),false,nullptr,&error),"non-finite split grid refused",error);
	}
	return ok;
}

bool topologyRefusalsAndTransactions()
{
	LevelMapDocument map;
	QString error;
	if (!load(fixture(QStringLiteral("classic")), &map, &error)) {
		return false;
	}
	bool ok = true;
	const auto original = map.brushes.first();
	LevelBrushTopology topology;
	levelBrushTopology(original, &topology);
	for (int edge = 0; edge < topology.edges.size(); ++edge) {
		auto brush = original;
		LevelBrushEditReport report;
		ok &= expect(removeLevelBrushComponents(&brush, LevelBrushComponent::Edge, {edge}, &report, &error)
			&& report.verticesAfter == 6 && report.removedVertices == 2, "remove any box edge", error);
	}
	for (int face = 0; face < topology.faces.size(); ++face) {
		auto brush = original;
		ok &= expect(!removeLevelBrushComponents(&brush, LevelBrushComponent::Face, {face}, nullptr, &error)
			&& sameGeometry(brush, original), "removing a box face cannot flatten the brush", error);
	}
	for (const auto &ids : {QVector<int>{}, QVector<int>{99}, QVector<int>{-1}, QVector<int>{0, 1, 2, 3, 4}}) {
		auto brush = original;
		ok &= expect(!removeLevelBrushComponents(&brush, LevelBrushComponent::Vertex, ids, nullptr, &error)
			&& sameGeometry(brush, original), "invalid removal is atomic", error);
	}
	const double nan = std::numeric_limits<double>::quiet_NaN();
	for (const auto &points : {QVector<LevelMapVec3>{}, QVector<LevelMapVec3>{{32, 32, 32, true}},
		QVector<LevelMapVec3>{{32, 32, 64, true}}, QVector<LevelMapVec3>{{0, 0, 64, true}},
		QVector<LevelMapVec3>{{32, 32, 80, true}, {32, 32, 80, true}}, QVector<LevelMapVec3>{{nan, 0, 0, true}},
		QVector<LevelMapVec3>{{40000, 0, 0, true}}, QVector<LevelMapVec3>{{32, 32, 80, false}}}) {
		auto brush = original;
		ok &= expect(!insertLevelBrushVertices(&brush, points, 0, true, nullptr, &error) && sameGeometry(brush, original),
			"redundant, invalid or duplicate insertion is atomic even with collapse", error);
	}
	auto brush = original;
	LevelBrushEditReport edit;
	ok &= expect(insertLevelBrushVertices(&brush, {{31.9, 31.9, 79.9, true}}, 8, false, &edit, &error)
		&& std::abs(brush.maxs.z - 80) < 0.001 && edit.insertedVertices == 1, "inserted coordinates snap to grid", error);
	brush = original;
	ok &= expect(insertLevelBrushVertices(&brush, {{-16, 32, 32, true}, {80, 32, 32, true}}, 0, false, &edit, &error)
		&& edit.verticesAfter == 10 && edit.insertedVertices == 2, "multiple exterior insertions are one edit", error);
	brush = original;
	ok &= expect(!insertLevelBrushVertices(&brush, {{64, 64, 80, true}}, 0, false, nullptr, &error) && sameGeometry(brush, original),
		"insertion requires explicit permission to remove an old corner", error);
	ok &= expect(insertLevelBrushVertices(&brush, {{64, 64, 80, true}}, 0, true, &edit, &error)
		&& edit.verticesAfter == 8 && edit.collapsedVertices == 1 && edit.insertedVertices == 1,
		"insertion can explicitly replace an old corner", error);
	brush = original;
	ok &= expect(insertLevelBrushVertices(&brush, {{32, -16, 0, true}}, 0, false, &edit, &error) && edit.verticesAfter == 9,
		"inserted edge corner survives supporting-plane reconstruction", error);
	brush = original;
	ok &= expect(insertLevelBrushVertices(&brush, {{32, 32, 80, true}}, 0, false, &edit, &error), "prepare non-box face removal", error);
	LevelBrushTopology peaked;
	levelBrushTopology(brush, &peaked);
	const auto triangular = std::find_if(peaked.faces.cbegin(), peaked.faces.cend(), [](const auto &face) { return face.size() == 3; });
	if (expect(triangular != peaked.faces.cend(), "inserted peak has triangular faces")) {
		ok &= expect(removeLevelBrushComponents(&brush, LevelBrushComponent::Face, {static_cast<int>(triangular - peaked.faces.cbegin())}, &edit, &error)
			&& edit.removedVertices == 3 && edit.verticesAfter == 6, "remove face vertices when the remaining solid is valid", error);
	} else {
		ok = false;
	}
	ok &= expect(addLevelMapBoxBrush(&map, {128, 0, 0, true}, {192, 64, 64, true}, QStringLiteral("studio/stone"), nullptr, &error),
		"add second brush for atomic removal", error);
	const auto baseline = serializeLevelMap(map).bytes;
	const auto history = map.undoStack.size();
	QVector<LevelMapComponentSet> sets{{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Vertex, {7}},
		{{LevelMapSelectionKind::QuakeBrush, 1}, LevelBrushComponent::Face, {0}}};
	ok &= expect(!removeLevelMapComponents(&map, sets, nullptr, &error) && serializeLevelMap(map).bytes == baseline
		&& map.undoStack.size() == history, "invalid second brush rolls back the first", error);
	sets[1].kind = LevelBrushComponent::Vertex;
	sets[1].components = {0};
	LevelMapComponentReport report;
	ok &= expect(removeLevelMapComponents(&map, sets, &report, &error) && report.brushes == 2 && report.removedVertices == 2
		&& map.undoStack.size() == history + 1, "multiple removals commit one undo", error);
	ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == baseline, "atomic topology undo restores both brushes", error);
	const QVector<LevelMapComponentSet> splitSets{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Edge,{0}},
		{{LevelMapSelectionKind::QuakeBrush,1},LevelBrushComponent::Edge,{0}}};
	auto badSplit = splitSets; badSplit[1].components = {999};
	ok &= expect(!splitLevelMapComponents(&map,badSplit,{-16,-16,0,true},false,&report,&error)
		&& serializeLevelMap(map).bytes == baseline && map.undoStack.size() == history && report.insertedVertices == 0,"invalid second split rolls back first and report",error);
	ok &= expect(splitLevelMapComponents(&map,splitSets,{-16,-16,0,true},false,&report,&error) && report.insertedVertices == 2
		&& report.brushes == 2 && map.undoStack.size() == history+1,"multi-brush split is one undo transaction",error);
	ok &= expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == baseline,"multi-brush split undo is exact",error);
	QString group;
	ok &= expect(createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Protected"), {}, &group, &error)
		&& assignLevelSceneObjects(&map, group, {QStringLiteral("brush:1")}, &error)
		&& setLevelSceneLocked(&map, group, true, &error), "protect second brush", error);
	const auto locked = serializeLevelMap(map).bytes;
	const auto lockedHistory = map.undoStack.size();
	report.insertedVertices = 999;
	ok &= expect(!splitLevelMapComponents(&map,splitSets,{-16,-16,0,true},false,&report,&error)
		&& serializeLevelMap(map).bytes == locked && map.undoStack.size() == lockedHistory && report.insertedVertices == 0,"scene lock refuses entire split and clears report",error);
	ok &= expect(!removeLevelMapComponents(&map, sets, nullptr, &error) && serializeLevelMap(map).bytes == locked
		&& map.undoStack.size() == lockedHistory, "locked brush refuses all removals", error);
	brush = map.brushes[1];
	ok &= expect(insertLevelBrushVertices(&brush, {{160, 32, 80, true}}, 0, false, nullptr, &error)
		&& !replaceLevelMapBrushGeometry(&map, 1, brush, &error) && serializeLevelMap(map).bytes == locked,
		"locked brush refuses topology draft insertion", error);
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	ok &= topologyRefusalsAndTransactions();
	ok &= splitChecks();
	ok &= extrusionChecks();
	ok &= extrusionTransactions();
	for (const auto &kind :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument map;
		const auto original = fixture(kind);
		ok &= topologyEditing(original);
		ok &= expect(load(original, &map, &error), "load dialect fixture", error);
		if (map.brushes.size() != 1) {
			return EXIT_FAILURE;
		}
		LevelBrushTopology topology;
		auto brush = map.brushes.first();
		ok &= expect(levelBrushTopology(brush, &topology, &error) && topology.vertices.size() == 8 && topology.edges.size() == 12 &&
						 topology.faces.size() == 6,
					 "cube components", error);
		LevelBrushEditReport edit;
		ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {7}, {16, 8, 12, true}, 0, false, &edit, &error),
					 "bend corner through convex hull", error);
		ok &= expect(edit.changed && edit.facesAfter > 6 && edit.verticesAfter == 8 && edit.collapsedVertices == 0,
					 "bent faces split without losing vertices");
		ok &= expect(replaceLevelMapBrushGeometry(&map, 0, brush, &error), "commit authored brush", error);
		const auto saved = serializeLevelMap(map);
		LevelMapDocument reloaded;
		ok &= expect(saved.succeeded() && load(saved.bytes, &reloaded, &error) && reloaded.brushes.size() == 1 &&
						 sameGeometry(brush, reloaded.brushes.first()),
					 "saved hull matches live geometry", error);
		ok &= expect(reloaded.brushes.first().primitiveKind == kind, "primitive dialect retained");
		ok &= expect(saved.bytes.contains("// Outside comment") && saved.bytes.contains("/* block comment retained */") &&
						 saved.bytes.contains("\"custom\" \"keep this\"") && saved.bytes.contains("// untouched entity"),
					 "surrounding source and comments survive");
		for (int note = 1; note <= 6; ++note) {
			ok &= expect(saved.bytes.count(QByteArray("// face note ") + QByteArray::number(note)) == 1, "face comments retained once");
		}
		for (const auto &face : reloaded.brushes.first().faces) {
			ok &= expect(face.contentFlags == 2 && face.surfaceFlags == 4 && face.surfaceValue == 8 &&
							 face.textureName.startsWith(QStringLiteral("studio/face")),
						 "source face flags and material retained");
			if (face.explicitTextureMatrix) {
				ok &= expect(face.textureMatrix[0] == 0.125 && face.textureMatrix[5] == 0.75, "matrix survives new plane");
			} else {
				ok &= expect(face.scaleX == 0.5 && face.scaleY == 2 && face.rotation == 15, "texture parameters survive new plane");
			}
		}
		ok &=
			expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "undo restores exact original text", error);
		ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == saved.bytes, "redo restores authored text", error);
		const auto depth = map.undoStack.size();
		ok &= expect(replaceLevelMapBrushGeometry(&map, 0, map.brushes.first(), &error) && map.undoStack.size() == depth,
					 "no-op commit adds no undo", error);
		ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "select edited brush", error);
		ok &= expect(setLevelMapBrushFaceProperty(&map, 0, 0, QStringLiteral("texture"), QStringLiteral("studio/new"), &error),
					 "material editor works after topology edit", error);
		ok &= expect(duplicateLevelMapSelection(&map, 128, 0, 0, &error) && map.brushes.size() == 2, "duplicate edited brush", error);
		const auto copy = serializeLevelMap(map);
		ok &= expect(load(copy.bytes, &reloaded, &error) && reloaded.brushes.size() == 2 && reloaded.brushes.last().boundsSolved,
					 "copied hull reloads", error);
		const auto recovery = writeLevelMapRecovery(map, temp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		LevelMapDocument recovered;
		ok &= expect(!recovery.isEmpty() && restoreLevelMapRecovery(recovery, &recovered, &error) &&
						 serializeLevelMap(recovered).bytes == copy.bytes,
					 "recovery preserves authored hull and copy", error);
		ok &= expect(buildLevelMapPreviewMesh(map).triangles > 12, "shared model preview triangulates authored brush");
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		const auto clipboard = levelMapSelectionText(map, &error);
		LevelMapDocument pasted;
		LevelMapCreateRequest empty;
		empty.starterRoom = false;
		createLevelMap(empty, &pasted);
		ok &= expect(!clipboard.isEmpty() && pasteLevelMapText(&pasted, clipboard, &error) && pasted.brushes.size() == 1 &&
						 sameGeometry(pasted.brushes.first(), map.brushes.first()),
					 "clipboard preserves authored hull", error);
		const auto beforeDelete = serializeLevelMap(map).bytes;
		ok &= expect(deleteLevelMapSelection(&map, &error) && map.brushes.size() == 1 && undoLevelMapEdit(&map, &error) &&
						 serializeLevelMap(map).bytes == beforeDelete,
					 "delete and undo preserve component source replacement", error);
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		int clipped = 0;
		ok &= expect(clipLevelMapSelection(&map, {32, -128, -128, true}, {32, -128, 128, true}, {32, 128, 128, true},
										   LevelMapClipKeep::Both, &clipped, &error) &&
						 clipped == 1,
					 "CSG clip accepts authored face bindings", error);
		ok &= expect(load(serializeLevelMap(map).bytes, &reloaded, &error) && reloaded.brushes.size() == 3,
					 "clipped authored pieces reload", error);
	}
	LevelMapDocument map;
	ok &= expect(load(fixture(QStringLiteral("classic")), &map, &error), "load operations fixture", error);
	const auto original = map.brushes.first();
	LevelBrushTopology base;
	levelBrushTopology(original, &base);
	for (int edge = 0; edge < base.edges.size(); ++edge) {
		auto brush = original;
		ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Edge, {edge}, {8, 12, 16, true}, 0, false, nullptr, &error),
					 "every cube edge can be reshaped", error);
	}
	for (int face = 0; face < base.faces.size(); ++face) {
		auto brush = original;
		ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Face, {face}, {8, 12, 16, true}, 0, false, nullptr, &error),
					 "every cube face can be moved", error);
	}
	for (int vertex = 0; vertex < 8; ++vertex) {
		for (int axis = 0; axis < 3; ++axis) {
			for (double step : {-16.0, -0.25, 0.125, 16.0}) {
				auto brush = original;
				LevelMapVec3 d{0, 0, 0, true};
				if (axis == 0) {
					d.x = step;
				} else if (axis == 1) {
					d.y = step;
				} else {
					d.z = step;
				}
				const bool moved = moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {vertex}, d, 0, false, nullptr, &error);
				ok &= expect(moved, "axis and fractional corner edits",
							 QStringLiteral("vertex %1 axis %2 step %3: %4").arg(vertex).arg(axis).arg(step).arg(error));
			}
		}
	}
	auto brush = original;
	LevelBrushEditReport edit;
	ok &= expect(!moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {0}, {48, 48, 48, true}, 0, false, &edit, &error) &&
					 sameGeometry(brush, original),
				 "interior vertex collapse requires explicit choice", error);
	ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {0}, {48, 48, 48, true}, 0, true, &edit, &error) &&
					 edit.collapsedVertices == 1 && edit.verticesAfter == 7,
				 "explicit collapse reports disappeared corner", error);
	brush = original;
	ok &= expect(!moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {0, 2, 4, 6}, {0, 0, 64, true}, 0, true, nullptr, &error) &&
					 sameGeometry(brush, original),
				 "flattening rejected atomically", error);
	for (double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 1e9}) {
		ok &= expect(!moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {0}, {invalid, 0, 0, true}, 0, true, nullptr, &error),
					 "invalid numeric movement rejected");
	}
	ok &= expect(!moveLevelBrushComponents(&brush, LevelBrushComponent::Edge, {99}, {1, 0, 0, true}, 0, false, nullptr, &error),
				 "unknown component rejected");
	if (argc > 1) {
		const QString source = QDir(temp.path()).filePath(QStringLiteral("input.map"));
		QFile file(source);
		if (!file.open(QIODevice::WriteOnly)) {
			return EXIT_FAILURE;
		}
		file.write(fixture(QStringLiteral("classic")));
		file.close();
		const QString output = QDir(temp.path()).filePath(QStringLiteral("output.map"));
		const auto cli = [&](const QStringList &args, int expected, bool json = true) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
																	   QDir(temp.path()).filePath(QStringLiteral("settings.ini"))} +
														   args + (json ? QStringList{QStringLiteral("--json")} : QStringList{}));
			const bool complete = process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			ok &= expect(complete && process.exitCode() == expected, "CLI command exit", QString::fromUtf8(bytes));
			if (!json && args.contains(QStringLiteral("--quiet"))) {
				ok &= expect(bytes.isEmpty(), "quiet inspection suppresses narration");
			}
			return QJsonDocument::fromJson(bytes).object();
		};
		const auto inspected =
			cli({QStringLiteral("map"), QStringLiteral("brush-components"), source, QStringLiteral("--brush"), QStringLiteral("0")}, 0);
		ok &= expect(inspected.value(QStringLiteral("brush")).toObject().value(QStringLiteral("vertices")).toArray().size() == 8,
					 "CLI exposes solved vertex IDs");
		cli({QStringLiteral("map"), QStringLiteral("brush-components"), source, QStringLiteral("--brush"), QStringLiteral("0"),
		     QStringLiteral("--quiet")}, 0, false);
		const QStringList args{QStringLiteral("map"),
							   QStringLiteral("move-components"),
							   source,
							   QStringLiteral("--brush"),
							   QStringLiteral("0"),
							   QStringLiteral("--kind"),
							   QStringLiteral("vertex"),
							   QStringLiteral("--component"),
							   QStringLiteral("7"),
							   QStringLiteral("--delta"),
							   QStringLiteral("16,8,12"),
							   QStringLiteral("--output"),
							   output};
		cli(args + QStringList{QStringLiteral("--dry-run")}, 0);
		ok &= expect(!QFileInfo::exists(output), "CLI dry-run creates no output");
		cli(args, 0);
		LevelMapDocument loaded;
		ok &= expect(loadLevelMap({output, {}, {}}, &loaded, &error) && loaded.brushes.first().faceCount > 6,
					 "CLI movement persists split faces", error);
		cli(args, 4); // Existing output is protected (validation failure).
		const QString insertedPath = QDir(temp.path()).filePath(QStringLiteral("inserted.map"));
		const QString removedPath = QDir(temp.path()).filePath(QStringLiteral("removed.map"));
		const QString rejectedPath = QDir(temp.path()).filePath(QStringLiteral("rejected.map"));
		const QStringList insertArgs{QStringLiteral("map"), QStringLiteral("insert-vertices"), source,
			QStringLiteral("--brush"), QStringLiteral("0"), QStringLiteral("--position"), QStringLiteral("32,32,80"),
			QStringLiteral("--output"), insertedPath};
		const QStringList removeArgs{QStringLiteral("map"), QStringLiteral("remove-components"), source,
			QStringLiteral("--brush"), QStringLiteral("0"), QStringLiteral("--kind"), QStringLiteral("vertex"),
			QStringLiteral("--component"), QStringLiteral("7"), QStringLiteral("--output"), removedPath};
		cli(insertArgs + QStringList{QStringLiteral("--dry-run")}, 0);
		cli(removeArgs + QStringList{QStringLiteral("--dry-run")}, 0);
		ok &= expect(!QFileInfo::exists(insertedPath) && !QFileInfo::exists(removedPath), "topology dry runs create no output");
		cli(insertArgs, 0);
		cli(removeArgs, 0);
		LevelBrushTopology savedTopology;
		ok &= expect(loadLevelMap({insertedPath, {}, {}}, &loaded, &error) && levelBrushTopology(loaded.brushes.first(), &savedTopology)
			&& savedTopology.vertices.size() == 9, "CLI inserted vertex persists", error);
		ok &= expect(loadLevelMap({removedPath, {}, {}}, &loaded, &error) && levelBrushTopology(loaded.brushes.first(), &savedTopology)
			&& savedTopology.vertices.size() == 7, "CLI removed vertex persists", error);
		cli(insertArgs, 4);
		cli(removeArgs, 4);
		for (const auto &extra : {QStringList{QStringLiteral("--typo")}, QStringList{QStringLiteral("--brush"), QStringLiteral("0")},
			QStringList{QStringLiteral("--delta"), QStringLiteral("1,0,0")}, QStringList{QStringLiteral("--grid")},
			QStringList{QStringLiteral("--dry-run=true")}, QStringList{QStringLiteral("extra.map")}}) {
			auto invalid = insertArgs;
			invalid[invalid.size() - 1] = rejectedPath;
			cli(invalid + extra, 2);
			ok &= expect(!QFileInfo::exists(rejectedPath), "invalid topology option writes nothing");
		}
		for (const auto &position : {QStringLiteral("32,32,32"), QStringLiteral("32,32,64"), QStringLiteral("nan,0,0")}) {
			auto invalid = insertArgs;
			invalid[6] = position;
			invalid[invalid.size() - 1] = rejectedPath;
			cli(invalid, 2);
			ok &= expect(!QFileInfo::exists(rejectedPath), "invalid topology result writes nothing");
		}
		cli(removeArgs + QStringList{QStringLiteral("--allow-collapse")}, 2);
		const auto splitPath = temp.filePath(QStringLiteral("split.map"));
		const QStringList splitArgs{QStringLiteral("map"),QStringLiteral("split-components"),source,QStringLiteral("--brush"),QStringLiteral("0"),
			QStringLiteral("--kind"),QStringLiteral("edge"),QStringLiteral("--component"),QStringLiteral("0"),QStringLiteral("--delta"),QStringLiteral("-16,-16,0"),QStringLiteral("--output"),splitPath};
		cli(splitArgs+QStringList{QStringLiteral("--dry-run")},0);
		ok &= expect(!QFileInfo::exists(splitPath),"split dry run creates no output");
		const auto splitResult = cli(splitArgs,0);
		ok &= expect(splitResult.value(QStringLiteral("insertedVertices")).toInt() == 1,"split CLI reports inserted corner");
		ok &= expect(loadLevelMap({splitPath,{},{}},&loaded,&error) && levelBrushTopology(loaded.brushes[0],&savedTopology)
			&& savedTopology.vertices.size() == 9,"split CLI persists ninth corner",error);
		cli(splitArgs,4);
		cli(splitArgs+QStringList{QStringLiteral("--typo")},2);
		cli(splitArgs+QStringList{QStringLiteral("--delta"),QStringLiteral("0,0,0")},2);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
