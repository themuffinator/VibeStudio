#include "core/model_mesh_tools_p.h"

#include <QMap>
#include <algorithm>

namespace vibestudio::model_tools
{
namespace
{
struct BevelText
{
	Q_DECLARE_TR_FUNCTIONS(ModelEdgeBevel)
};
using Weights = QMap<int, double>;

Weights mix(const Weights &a, const Weights &b, double t)
{
	Weights result;
	for (auto i = a.cbegin(); i != a.cend(); ++i)
		result[i.key()] += i.value() * (1 - t);
	for (auto i = b.cbegin(); i != b.cend(); ++i)
		result[i.key()] += i.value() * t;
	return result;
}

// Geometry and face-corner attributes deliberately have different provenance.
// A cut has one geometric identity across all poses, but each side keeps its
// own UVs, normals and joint-local offsets. No nearest-vertex rebinding occurs.
struct BevelPoint
{
	Weights weights;
	int owner = -1;
};
struct BevelSide
{
	int first = -1, last = -1;
};
struct EdgeUses
{
	int first = -1, last = -1, count = 0, balance = 0;
};

class BevelBuilder
{
  public:
	BevelBuilder(ModelMesh &mesh, const ModelEdit &edit, ModelSelection &selection, QString *error, const ModelWorkControl &control)
		: m_mesh(mesh), m_edit(edit), m_selection(selection), m_error(error), m_control(control),
		  m_work(control, ModelWorkPhase::Editing, error), m_surface(mesh.surfaces[selection.surface]), m_before(m_surface),
		  m_frame(edit.tool.referenceFrame)
	{
	}

	bool run()
	{
		if (!plan())
			return false;
		if (m_edit.tool.bevelWidth == 0)
			return m_work.check();
		if (!retainFaces() || !bands() || !stitchBoundaries() || !caps() || !validateBoundary() || !compact())
			return false;
		if (!skinning())
			return false;
		m_selection.faces = m_newFaces;
		m_selection.edges.clear();
		m_selection.vertices.clear();
		pruneSeams(&m_surface);
		return m_work.check();
	}

  private:
	bool refuse(const QString &message)
	{
		return fail(m_error, message);
	}
	int group(int index) const
	{
		return m_topology.geometry.group(index);
	}
	P3 originalNormal(int element, int frame) const
	{
		const auto &e = m_topology.elements[element];
		P3 result{0, 0, 0};
		const P3 origin = position(m_before, frame, e.corners[0]);
		for (int i = 1; i + 1 < e.sides(); ++i)
			result = result + cross(position(m_before, frame, e.corners[i]) - origin, position(m_before, frame, e.corners[i + 1]) - origin);
		return result;
	}
	P3 polygonNormal(const QVector<int> &polygon, int frame) const
	{
		P3 result{0, 0, 0};
		const P3 origin = position(m_surface, frame, polygon[0]);
		for (int i = 1; i + 1 < polygon.size(); ++i)
			result = result + cross(position(m_surface, frame, polygon[i]) - origin, position(m_surface, frame, polygon[i + 1]) - origin);
		return result;
	}
	bool plan()
	{
		if (!m_work.check())
			return false;
		if (m_selection.edges.isEmpty())
			return refuse(BevelText::tr("Select edges between two faces to bevel."));
		auto protectedEdges = m_selection.edges;
		protectedEdges.unite(m_before.uvSeams);
		if (!buildModelQuadTopology(m_before, m_frame, m_edit.tool.maxAngle, &m_topology, m_error, m_control, protectedEdges))
			return false;
		const auto indexed = surfaceEdgeSet(m_before);
		for (auto edge : m_selection.edges)
		{
			if (!m_work.step())
				return false;
			if (!indexed.contains(modelEdge(edge.first, edge.second)))
				return refuse(BevelText::tr("Every bevel selector must name an existing edge."));
			const auto geometric = m_topology.geometry.geometricEdge(edge.first, edge.second);
			if (geometric.first == geometric.second || m_topology.sideUses.value(geometric).size() != 2)
				return refuse(BevelText::tr("Bevel edges need two faces. Exclude open borders and nonmanifold edges."));
			m_chosen.insert(geometric);
			m_active << geometric.first << geometric.second;
		}
		m_ordered = QVector<ModelEdge>(m_chosen.cbegin(), m_chosen.cend());
		std::sort(m_ordered.begin(), m_ordered.end());
		// Establish one consistently wound fan at each selected endpoint. A fan
		// that was closed must remain closed after the bevel and its junction cap.
		for (int vertex : m_active)
		{
			if (!m_work.step())
				return false;
			QSet<int> incident;
			for (int face : m_topology.geometry.groupFaces[vertex])
				incident.insert(m_topology.faceElement[face]);
			QHash<int, QSet<int>> neighbours;
			int borders = 0;
			for (int element : incident)
			{
				const auto &e = m_topology.elements[element];
				for (int side = 0; side < e.sides(); ++side)
				{
					const int a = group(e.corners[side]), b = group(e.corners[(side + 1) % e.sides()]);
					if (a != vertex && b != vertex)
						continue;
					const auto uses = m_topology.sideUses.value(modelEdge(a, b));
					if (uses.size() == 1)
						++borders;
					else if (uses.size() == 2)
					{
						const auto other = uses[uses[0].first == element ? 1 : 0];
						const auto &o = m_topology.elements[other.first];
						if (group(o.corners[other.second]) != b || group(o.corners[(other.second + 1) % o.sides()]) != a)
							return refuse(BevelText::tr("Faces around bevel endpoints need consistent winding."));
						neighbours[element].insert(other.first);
					}
					else
						return refuse(BevelText::tr("A bevel endpoint touches nonmanifold geometry. Repair its face fan first."));
				}
			}
			QSet<int> visited;
			QVector<int> pending{*incident.cbegin()};
			while (!pending.isEmpty())
			{
				if (!m_work.step())
					return false;
				const int element = pending.takeLast();
				if (visited.contains(element))
					continue;
				visited.insert(element);
				for (int next : neighbours.value(element))
					pending.append(next);
			}
			if (visited != incident || (borders != 0 && borders != 2))
				return refuse(BevelText::tr("A bevel endpoint has disconnected face fans. Split or repair that vertex first."));
			if (borders == 0)
				m_closed.insert(vertex);
		}
		if (m_edit.tool.bevelWidth == 0)
			return m_work.check();
		qint64 touchedCorners = 0;
		for (const auto &e : m_topology.elements)
		{
			if (!m_work.step())
				return false;
			for (int corner = 0; corner < e.sides(); ++corner)
				if (m_active.contains(group(e.corners[corner])))
					++touchedCorners;
		}
		const qint64 profileVertices = qint64(m_chosen.size()) * 2 * (m_edit.tool.bevelSegments + 1);
		if (!reserveCapacity(m_mesh, m_selection.surface, 4 * touchedCorners + 2 * profileVertices,
							 4 * touchedCorners + 4 * profileVertices, m_error))
			return false;
		m_points.resize(m_before.vertexCount);
		m_indexGeometry.reserve(m_before.vertexCount);
		m_attributes.reserve(m_before.vertexCount);
		for (int v = 0; v < m_before.vertexCount; ++v)
		{
			if (!m_work.step())
				return false;
			m_points[v] = {{{group(v), 1}}, group(v)};
			m_indexGeometry.append(group(v));
			m_attributes.append({{v, 1}});
		}
		// A directed cut v -> n is shared geometrically by both face corners,
		// even when those corners use different indexed vertices at a UV seam.
		for (const auto &e : m_topology.elements)
		{
			if (!m_work.step())
				return false;
			for (int corner = 0; corner < e.sides(); ++corner)
			{
				const int v = group(e.corners[corner]), p = group(e.corners[(corner + e.sides() - 1) % e.sides()]),
						  n = group(e.corners[(corner + 1) % e.sides()]);
				const bool incoming = m_chosen.contains(modelEdge(p, v)), outgoing = m_chosen.contains(modelEdge(v, n));
				if (incoming == outgoing)
					continue;
				const int other = incoming ? n : p;
				const QPair<int, int> key{v, other};
				if (m_cuts.contains(key))
					continue;
				const double edgeLength = length(position(m_before, m_frame, other) - position(m_before, m_frame, v));
				const double fraction = m_edit.tool.bevelWidth / edgeLength;
				if (!std::isfinite(fraction) || fraction >= 1)
					return widthFailure();
				m_cuts.insert(key, addPoint({{v, 1 - fraction}, {other, fraction}}, v));
			}
		}
		return true;
	}
	bool widthFailure()
	{
		return refuse(
			BevelText::tr(
				"The bevel reaches a neighbour, overlaps or folds in a retained pose (%1). Reduce its width or change the selection.")
				.arg(m_stage));
	}
	int addPoint(const Weights &weights, int owner)
	{
		const int index = m_points.size();
		m_points.append({weights, owner});
		return index;
	}
	Weights attributesFor(int geometry, const ModelQuadElement &element) const
	{
		Weights result;
		for (auto i = m_points[geometry].weights.cbegin(); i != m_points[geometry].weights.cend(); ++i)
		{
			for (int corner = 0; corner < element.sides(); ++corner)
				if (group(element.corners[corner]) == i.key())
				{
					result[element.corners[corner]] += i.value();
					break;
				}
		}
		return result;
	}
	// A cap can supply m_attributes[old]; own a copy before appending to that
	// vector, whose allocation may move during the append.
	int append(int geometry, Weights attributes, bool generatedNormal = false)
	{
		if (!m_work.step())
			return -1;
		const int index = m_surface.vertexCount++;
		m_indexGeometry.append(geometry);
		m_attributes.append(attributes);
		double u = 0, v = 0;
		for (auto i = attributes.cbegin(); i != attributes.cend(); ++i)
		{
			u += m_before.texCoords[i.key()].u * i.value();
			v += m_before.texCoords[i.key()].v * i.value();
		}
		m_surface.texCoords.append({float(u), float(v)});
		if (!m_surface.topologyVertices.isEmpty()) m_surface.topologyVertices.append(index);
		for (int frame = 0; frame < m_surface.frames.size(); ++frame)
		{
			if (!m_work.step())
				return -1;
			P3 p{0, 0, 0}, n{0, 0, 0};
			for (auto i = m_points[geometry].weights.cbegin(); i != m_points[geometry].weights.cend(); ++i)
				p = p + position(m_before, frame, i.key()) * i.value();
			for (auto i = attributes.cbegin(); i != attributes.cend(); ++i)
				n = n + point(m_before.frames[frame].normals[i.key()]) * i.value();
			m_surface.frames[frame].positions.append(vec(p));
			m_surface.frames[frame].normals.append(vec(unit(n)));
		}
		if (generatedNormal)
			m_geometricNormals.insert(index);
		return index;
	}
	int cornerIndex(int geometry, int source, const ModelQuadElement &element)
	{
		if (geometry == group(source))
			return source;
		const QPair<int, int> key{geometry, source};
		if (m_cornerCopies.contains(key))
			return m_cornerCopies.value(key);
		const int index = append(geometry, attributesFor(geometry, element));
		m_cornerCopies.insert(key, index);
		return index;
	}
	// Every generated triangle must retain its winding and area in every pose.
	// The expected normal moves with its source faces, rather than staying in
	// world space when an animated asset rotates.
	bool polygon(const QVector<int> &vertices, const std::function<P3(int)> &expected, bool selected)
	{
		QVector<int> cleaned;
		for (int v : vertices)
			if (cleaned.isEmpty() || m_indexGeometry[cleaned.last()] != m_indexGeometry[v])
				cleaned.append(v);
		if (cleaned.size() > 1 && m_indexGeometry[cleaned.first()] == m_indexGeometry[cleaned.last()])
			cleaned.removeLast();
		if (cleaned.size() < 3)
			return widthFailure();
		QVector<ModelTriangle> triangles;
		if (!triangulatePolygon(m_surface, m_frame, cleaned, expected(m_frame), &triangles))
			return widthFailure();
		for (const auto &t : triangles)
		{
			for (int frame = 0; frame < m_surface.frames.size(); ++frame)
			{
				if (!m_work.step())
					return false;
				const P3 n = faceCross(m_surface, frame, t), reference = expected(frame);
				if (collapsedTriangle(m_surface.frames[frame].positions[t.a], m_surface.frames[frame].positions[t.b],
									  m_surface.frames[frame].positions[t.c]) ||
					length(reference) <= 1e-12 || dot(n, reference) <= length(n) * length(reference) * 1e-8)
					return widthFailure();
			}
			if (selected)
				m_newFaces.insert(m_surface.triangles.size());
			m_surface.triangles.append(t);
		}
		return true;
	}
	bool retainFaces()
	{
		m_stage = BevelText::tr("retained faces");
		m_surface.triangles.clear();
		m_surface.uvSeams.clear();
		m_sides.resize(m_topology.elements.size());
		for (int element = 0; element < m_topology.elements.size(); ++element)
		{
			if (!m_work.step())
				return false;
			const auto &e = m_topology.elements[element];
			QVector<int> vertices;
			std::array<QVector<int>, 4> corners;
			bool changed = false;
			for (int corner = 0; corner < e.sides(); ++corner)
			{
				const int source = e.corners[corner], v = group(source), p = group(e.corners[(corner + e.sides() - 1) % e.sides()]),
						  n = group(e.corners[(corner + 1) % e.sides()]);
				const bool incoming = m_chosen.contains(modelEdge(p, v)), outgoing = m_chosen.contains(modelEdge(v, n));
				QVector<int> geometry;
				if (incoming && outgoing)
				{
					const double a = m_edit.tool.bevelWidth / length(position(m_before, m_frame, p) - position(m_before, m_frame, v));
					const double b = m_edit.tool.bevelWidth / length(position(m_before, m_frame, n) - position(m_before, m_frame, v));
					if (!std::isfinite(a + b) || a + b >= 1)
						return widthFailure();
					geometry.append(addPoint({{v, 1 - a - b}, {p, a}, {n, b}}, v));
				}
				else if (incoming || outgoing)
					geometry.append(m_cuts.value({v, incoming ? n : p}));
				else
				{
					const int previousCut = m_cuts.value({v, p}, -1), nextCut = m_cuts.value({v, n}, -1);
					if (previousCut >= 0)
						geometry.append(previousCut);
					if (previousCut < 0 || nextCut < 0)
						geometry.append(v);
					if (nextCut >= 0)
						geometry.append(nextCut);
				}
				for (int point : geometry)
				{
					const int index = cornerIndex(point, source, e);
					if (index < 0)
						return false;
					corners[corner].append(index);
					vertices.append(index);
					changed |= index != source;
				}
			}
			for (int side = 0; side < e.sides(); ++side)
			{
				const int next = (side + 1) % e.sides();
				const BevelSide rail{corners[side].last(), corners[next].first()};
				m_sides[element][side] = rail;
				if (m_before.uvSeams.contains(modelEdge(e.corners[side], e.corners[next])) ||
					m_chosen.contains(modelEdge(group(e.corners[side]), group(e.corners[next]))))
					m_surface.uvSeams.insert(modelEdge(rail.first, rail.last));
				if (changed)
					for (int frame = 0; frame < m_surface.frames.size(); ++frame)
					{
						if (!m_work.step())
							return false;
						if (dot(position(m_surface, frame, rail.last) - position(m_surface, frame, rail.first),
								position(m_before, frame, e.corners[next]) - position(m_before, frame, e.corners[side])) <= 0)
							return widthFailure();
					}
			}
			if (changed)
			{
				if (!polygon(vertices, [&](int frame) { return originalNormal(element, frame); }, false))
					return false;
			}
			else
				for (int face : e.faces)
					if (face >= 0)
					{
						const auto &t = m_before.triangles[face];
						m_surface.triangles.append(t);
						for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
							if (m_before.uvSeams.contains(edge))
								m_surface.uvSeams.insert(edge);
					}
		}
		return true;
	}
	bool bands()
	{
		m_stage = BevelText::tr("bevel bands");
		const int segments = m_edit.tool.bevelSegments;
		const double tile = m_edit.tool.uvTileSize;
		for (auto edge : m_ordered)
		{
			if (!m_work.step())
				return false;
			const auto uses = m_topology.sideUses.value(edge);
			// Follow the first face's winding. The new band must reverse its rail.
			const auto first = m_sides[uses[0].first][uses[0].second], second = m_sides[uses[1].first][uses[1].second];
			const std::array<int, 2> start{first.first, first.last}, finish{second.last, second.first};
			const auto &element = m_topology.elements[uses[0].first];
			const int sourceA = element.corners[uses[0].second], sourceB = element.corners[(uses[0].second + 1) % element.sides()];
			const std::array<int, 2> source{sourceA, sourceB};
			std::array<QVector<int>, 2> profile;
			for (int end = 0; end < 2; ++end)
			{
				const int v = group(source[end]), other = group(source[1 - end]);
				const P3 origin = position(m_before, m_frame, v), along = position(m_before, m_frame, other) - origin;
				const double controlFraction = std::clamp(
					dot((position(m_surface, m_frame, start[end]) + position(m_surface, m_frame, finish[end])) * 0.5 - origin, along) /
						dot(along, along),
					0.0, 1.0);
				const Weights controlGeometry{{v, 1 - controlFraction}, {other, controlFraction}};
				const Weights controlAttributes{{source[end], 1 - controlFraction}, {source[1 - end], controlFraction}};
				const int g0 = m_indexGeometry[start[end]], g1 = m_indexGeometry[finish[end]];
				const auto a0 = m_attributes[start[end]], a1 = m_attributes[finish[end]];
				const auto middleGeometry =
					mix(mix(m_points[g0].weights, m_points[g1].weights, 0.5), controlGeometry, m_edit.tool.bevelRoundness);
				const auto middleAttributes = mix(mix(a0, a1, 0.5), controlAttributes, m_edit.tool.bevelRoundness);
				double distance = 0;
				for (int step = 0; step <= segments; ++step)
				{
					const double t = double(step) / segments;
					int geometry = step == 0 ? g0 : g1;
					Weights attributes = step == 0 ? a0 : a1;
					if (step > 0 && step < segments && g0 != g1)
					{
						geometry =
							addPoint(mix(mix(m_points[g0].weights, middleGeometry, t), mix(middleGeometry, m_points[g1].weights, t), t), v);
						attributes = mix(mix(a0, middleAttributes, t), mix(middleAttributes, a1, t), t);
					}
					const int index = append(geometry, attributes, true);
					if (index < 0)
						return false;
					if (step > 0)
						distance += length(position(m_surface, m_frame, index) - position(m_surface, m_frame, profile[end].last()));
					m_surface.texCoords[index] = {float(end * length(along) / tile), float(distance / tile)};
					profile[end].append(index);
					if (step > 0 && m_indexGeometry[index] != m_indexGeometry[profile[end][step - 1]])
						m_surface.uvSeams.insert(modelEdge(index, profile[end][step - 1]));
				}
			}
			for (int step = 0; step < segments; ++step)
				if (!polygon(
						{profile[1][step], profile[0][step], profile[0][step + 1], profile[1][step + 1]}, [&](int frame)
						{ return unit(originalNormal(uses[0].first, frame)) + unit(originalNormal(uses[1].first, frame)); }, true))
					return false;
			m_surface.uvSeams.insert(modelEdge(profile[0].first(), profile[1].first()));
			m_surface.uvSeams.insert(modelEdge(profile[0].last(), profile[1].last()));
		}
		return true;
	}
	bool collectUses(QHash<ModelEdge, EdgeUses> *uses)
	{
		for (const auto &t : m_surface.triangles)
		{
			if (!m_work.step())
				return false;
			for (auto edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				const int a = m_indexGeometry[edge.first], b = m_indexGeometry[edge.second];
				auto &use = (*uses)[modelEdge(a, b)];
				use.first = edge.first;
				use.last = edge.second;
				++use.count;
				use.balance += a < b ? 1 : -1;
			}
		}
		return true;
	}
	bool stitchBoundaries()
	{
		m_stage = BevelText::tr("junction stitching");
		// Two chamfers can describe the same junction point from opposite
		// directions. Intern their affine provenance (not just one pose's
		// position), then put every indexed copy at the canonical position.
		QHash<QByteArray, int> canonical;
		QVector<int> mapping(m_points.size());
		for (int point = 0; point < m_points.size(); ++point)
		{
			if (!m_work.step())
				return false;
			QByteArray key = QByteArray::number(m_points[point].owner) + ':';
			for (auto i = m_points[point].weights.cbegin(); i != m_points[point].weights.cend(); ++i)
			{
				const qint64 quantized = std::llround(i.value() * 1e12);
				if (quantized != 0)
					key += QByteArray::number(i.key()) + '=' + QByteArray::number(quantized) + ';';
			}
			mapping[point] = canonical.value(key, point);
			canonical.insert(key, mapping[point]);
		}
		for (int index = 0; index < m_surface.vertexCount; ++index)
		{
			if (!m_work.step())
				return false;
			const int old = m_indexGeometry[index], point = mapping[old];
			if (point == old)
				continue;
			m_indexGeometry[index] = point;
			for (int frame = 0; frame < m_surface.frames.size(); ++frame)
			{
				if (!m_work.step())
					return false;
				P3 position{0, 0, 0};
				for (auto i = m_points[point].weights.cbegin(); i != m_points[point].weights.cend(); ++i)
					position = position + model_tools::position(m_before, frame, i.key()) * i.value();
				m_surface.frames[frame].positions[index] = vec(position);
			}
		}
		QHash<ModelEdge, EdgeUses> uses;
		if (!collectUses(&uses))
			return false;
		QHash<int, QSet<int>> ownerPoints;
		for (auto i = uses.cbegin(); i != uses.cend(); ++i)
		{
			if (!m_work.step())
				return false;
			if (i->count == 1 && m_points[i.key().first].owner == m_points[i.key().second].owner)
				ownerPoints[m_points[i.key().first].owner] << i.key().first << i.key().second;
		}
		QHash<ModelEdge, QVector<QPair<double, int>>> splits;
		for (const auto &points : ownerPoints)
			if (points.size() > 4096)
				return refuse(BevelText::tr("A bevel junction exceeds 4,096 boundary points. Reduce its segments or selection."));
		for (auto i = uses.cbegin(); i != uses.cend(); ++i)
		{
			if (!m_work.step())
				return false;
			const auto edge = i.key();
			const int owner = m_points[edge.first].owner;
			if (i->count != 1 || owner != m_points[edge.second].owner)
				continue;
			const auto &a = m_points[edge.first].weights, &b = m_points[edge.second].weights;
			QSet<int> components(a.keyBegin(), a.keyEnd());
			components.unite(QSet<int>(b.keyBegin(), b.keyEnd()));
			int axis = -1;
			double span = 0;
			auto orderedComponents = components.values();
			std::sort(orderedComponents.begin(), orderedComponents.end());
			for (int component : orderedComponents)
				if (std::abs(b.value(component) - a.value(component)) > std::abs(span))
				{
					axis = component;
					span = b.value(component) - a.value(component);
				}
			if (axis < 0)
				continue;
			for (int point : ownerPoints.value(owner))
			{
				if (!m_work.step())
					return false;
				const auto &w = m_points[point].weights;
				const double t = (w.value(axis) - a.value(axis)) / span;
				if (t <= 1e-10 || t >= 1 - 1e-10)
					continue;
				auto all = components;
				all.unite(QSet<int>(w.keyBegin(), w.keyEnd()));
				bool collinear = true;
				for (int component : all)
					collinear &= std::abs(w.value(component) - (a.value(component) * (1 - t) + b.value(component) * t)) < 1e-10;
				if (collinear)
					splits[edge].append({t, point});
			}
		}
		if (splits.isEmpty())
			return true;
		for (auto &points : splits)
			std::sort(points.begin(), points.end());
		const auto triangles = m_surface.triangles;
		const auto selected = m_newFaces;
		m_surface.triangles.clear();
		m_newFaces.clear();
		for (int face = 0; face < triangles.size(); ++face)
		{
			if (!m_work.step())
				return false;
			const auto t = triangles[face];
			QVector<int> boundary;
			bool changed = false;
			for (auto indices : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				boundary.append(indices.first);
				const int a = m_indexGeometry[indices.first], b = m_indexGeometry[indices.second];
				auto points = splits.value(modelEdge(a, b));
				if (points.isEmpty())
					continue;
				changed = true;
				if (a > b)
					std::reverse(points.begin(), points.end());
				const auto sourceA = m_attributes[indices.first], sourceB = m_attributes[indices.second];
				const auto uvA = m_surface.texCoords[indices.first], uvB = m_surface.texCoords[indices.second];
				const bool seam = m_surface.uvSeams.remove(modelEdge(indices.first, indices.second));
				int previous = indices.first;
				for (auto [fraction, geometry] : points)
				{
					if (a > b)
						fraction = 1 - fraction;
					const int vertex = append(geometry, mix(sourceA, sourceB, fraction), m_geometricNormals.contains(indices.first));
					if (vertex < 0)
						return false;
					m_surface.texCoords[vertex] = {float(uvA.u * (1 - fraction) + uvB.u * fraction),
												   float(uvA.v * (1 - fraction) + uvB.v * fraction)};
					boundary.append(vertex);
					if (seam)
						m_surface.uvSeams.insert(modelEdge(previous, vertex));
					previous = vertex;
				}
				if (seam)
					m_surface.uvSeams.insert(modelEdge(previous, indices.second));
			}
			if (changed)
			{
				if (!polygon(boundary, [&](int frame) { return faceCross(m_surface, frame, t); }, selected.contains(face)))
					return false;
			}
			else
			{
				if (selected.contains(face))
					m_newFaces.insert(m_surface.triangles.size());
				m_surface.triangles.append(t);
			}
		}
		return true;
	}
	bool caps()
	{
		m_stage = BevelText::tr("junction caps");
		QHash<ModelEdge, EdgeUses> uses;
		if (!collectUses(&uses))
			return false;
		QMap<int, QMap<int, int>> nextByOwner;
		QHash<int, int> representative;
		for (auto i = uses.cbegin(); i != uses.cend(); ++i)
		{
			if (!m_work.step())
				return false;
			if (i->count != 1)
				continue;
			const int a = m_indexGeometry[i->last], b = m_indexGeometry[i->first], owner = m_points[a].owner;
			if (owner != m_points[b].owner || !m_closed.contains(owner))
				continue;
			auto &next = nextByOwner[owner];
			if (next.contains(a))
				return refuse(BevelText::tr("The bevel junction has an ambiguous boundary. Reduce its width or change the selection."));
			next.insert(a, b);
			representative.insert(a, std::min(representative.value(a, i->last), i->last));
			representative.insert(b, std::min(representative.value(b, i->first), i->first));
		}
		for (auto owner = nextByOwner.begin(); owner != nextByOwner.end(); ++owner)
		{
			auto &next = owner.value();
			while (!next.isEmpty())
			{
				QVector<int> boundary;
				const int first = next.firstKey();
				int current = first;
				do
				{
					if (!m_work.step())
						return false;
					if (!next.contains(current))
						return refuse(BevelText::tr("A bevel junction could not be closed. Change the selection around that vertex."));
					boundary.append(representative.value(current));
					current = next.take(current);
				} while (current != first);
				if (boundary.size() < 3 || boundary.size() > 4096)
					return refuse(
						BevelText::tr("A bevel junction has an unsupported boundary. Reduce its segments or change the selection."));
				const P3 normal = unit(polygonNormal(boundary, m_frame));
				QVector<P3> capNormals;
				for (int frame = 0; frame < m_surface.frames.size(); ++frame)
				{
					if (!m_work.step())
						return false;
					capNormals.append(polygonNormal(boundary, frame));
				}
				const P3 helper = std::abs(normal.x) < 0.9 ? P3{1, 0, 0} : P3{0, 1, 0};
				const P3 u = unit(cross(helper, normal)), v = cross(normal, u), origin = position(m_before, m_frame, owner.key());
				QVector<int> vertices;
				for (int old : boundary)
				{
					const int index = append(m_indexGeometry[old], m_attributes[old], true);
					if (index < 0)
						return false;
					const P3 delta = position(m_surface, m_frame, index) - origin;
					m_surface.texCoords[index] = {float(dot(delta, u) / m_edit.tool.uvTileSize),
												  float(dot(delta, v) / m_edit.tool.uvTileSize)};
					vertices.append(index);
				}
				// A centre fan avoids almost-collinear ears on highly subdivided
				// rounded junctions. Keep ear clipping for non-star-shaped caps.
				Weights centreGeometry, centreAttributes;
				for (int index : vertices)
				{
					for (auto i = m_points[m_indexGeometry[index]].weights.cbegin(); i != m_points[m_indexGeometry[index]].weights.cend();
						 ++i)
						centreGeometry[i.key()] += i.value() / vertices.size();
					for (auto i = m_attributes[index].cbegin(); i != m_attributes[index].cend(); ++i)
						centreAttributes[i.key()] += i.value() / vertices.size();
				}
				const int centre = append(addPoint(centreGeometry, owner.key()), centreAttributes, true);
				if (centre < 0)
					return false;
				const P3 delta = position(m_surface, m_frame, centre) - origin;
				m_surface.texCoords[centre] = {float(dot(delta, u) / m_edit.tool.uvTileSize),
											   float(dot(delta, v) / m_edit.tool.uvTileSize)};
				bool fan = true;
				for (int frame = 0; frame < m_surface.frames.size(); ++frame)
					for (int i = 0; i < vertices.size(); ++i)
					{
						if (!m_work.step())
							return false;
						fan &= dot(faceCross(m_surface, frame, {vertices[i], vertices[(i + 1) % vertices.size()], centre}),
								   capNormals[frame]) > 1e-12;
					}
				if (fan)
				{
					for (int i = 0; i < vertices.size(); ++i)
						if (!polygon(
								{vertices[i], vertices[(i + 1) % vertices.size()], centre}, [&](int frame) { return capNormals[frame]; },
								true))
							return false;
				}
				else if (!polygon(vertices, [&](int frame) { return capNormals[frame]; }, true))
					return false;
				for (int i = 0; i < vertices.size(); ++i)
					m_surface.uvSeams.insert(modelEdge(vertices[i], vertices[(i + 1) % vertices.size()]));
			}
		}
		return true;
	}
	bool validateBoundary()
	{
		QHash<ModelEdge, EdgeUses> uses;
		if (!collectUses(&uses))
			return false;
		for (auto i = uses.cbegin(); i != uses.cend(); ++i)
		{
			if (!m_work.step())
				return false;
			const auto edge = i.key();
			if (edge.first < m_before.vertexCount && edge.second < m_before.vertexCount && !m_active.contains(edge.first) &&
				!m_active.contains(edge.second))
				continue;
			if (i->count > 2 || (i->count == 2 && i->balance != 0) ||
				(i->count == 1 && (m_closed.contains(m_points[edge.first].owner) || m_closed.contains(m_points[edge.second].owner))))
				return refuse(
					BevelText::tr("The bevel would leave a hole or overlapping face boundary. Change its width, segments or selection."));
		}
		// Only band/cap vertices receive geometric normals. Their separate index
		// groups retain a sharp join to the original surface and to each cap.
		auto orderedFaces = m_newFaces.values();
		std::sort(orderedFaces.begin(), orderedFaces.end());
		for (int frame = 0; frame < m_surface.frames.size(); ++frame)
		{
			QHash<int, P3> normals;
			for (int face : orderedFaces)
			{
				if (!m_work.step())
					return false;
				const auto &t = m_surface.triangles[face];
				const int corners[3]{t.a, t.b, t.c};
				for (int corner = 0; corner < 3; ++corner)
					normals[corners[corner]] = normals.value(corners[corner], {0, 0, 0}) + cornerNormal(m_surface, frame, t, corner);
			}
			for (auto i = normals.cbegin(); i != normals.cend(); ++i)
				m_surface.frames[frame].normals[i.key()] = vec(unit(i.value()));
		}
		return true;
	}
	bool compact()
	{
		const auto beforeUsed = usedVertices(m_before), afterUsed = usedVertices(m_surface);
		QVector<int> mapping(m_surface.vertexCount, -1), retained;
		QVector<Weights> attributes;
		QSet<int> geometricNormals;
		QVector<ModelTexCoord> uv;
		QVector<int> identities;
		QHash<int, int> geometryIdentities;
		for (int index = 0; index < m_surface.vertexCount; ++index)
		{
			if (!m_work.step())
				return false;
			if (!afterUsed.contains(index) && (index >= m_before.vertexCount || beforeUsed.contains(index)))
				continue;
			mapping[index] = retained.size();
			if (m_geometricNormals.contains(index))
				geometricNormals.insert(retained.size());
			retained.append(index);
			attributes.append(m_attributes[index]);
			uv.append(m_surface.texCoords[index]);
			if (!m_surface.topologyVertices.isEmpty())
			{
				const int geometry = m_indexGeometry[index];
				if (!geometryIdentities.contains(geometry))
					geometryIdentities.insert(geometry, geometry < m_before.vertexCount ? m_before.topologyVertices[geometry] : index);
				identities.append(geometryIdentities.value(geometry));
			}
		}
		for (auto &frame : m_surface.frames)
		{
			QVector<ModelVec3> positions, normals;
			positions.reserve(retained.size());
			normals.reserve(retained.size());
			for (int index : retained)
			{
				if (!m_work.step())
					return false;
				positions.append(frame.positions[index]);
				normals.append(frame.normals[index]);
			}
			frame.positions = std::move(positions);
			frame.normals = std::move(normals);
		}
		for (auto &t : m_surface.triangles)
		{
			if (!m_work.step())
				return false;
			t = {mapping[t.a], mapping[t.b], mapping[t.c]};
		}
		QSet<ModelEdge> seams;
		for (auto edge : m_surface.uvSeams)
			if (mapping[edge.first] >= 0 && mapping[edge.second] >= 0)
				seams.insert(modelEdge(mapping[edge.first], mapping[edge.second]));
		m_surface.vertexCount = retained.size();
		m_surface.texCoords = std::move(uv);
		m_surface.topologyVertices = std::move(identities);
		compactModelVertexIdentities(&m_surface);
		m_surface.uvSeams = std::move(seams);
		m_attributes = std::move(attributes);
		m_geometricNormals = std::move(geometricNormals);
		return true;
	}
	bool skinning()
	{
		if (m_before.skinning.isEmpty())
			return true;
		ModelSurfaceSkinning result;
		for (int vertex = 0; vertex < m_surface.vertexCount; ++vertex)
		{
			if (!m_work.step())
				return false;
			result.first.append(result.influences.size());
			const auto &attributes = m_attributes[vertex];
			if (attributes.size() == 1 && attributes.first() == 1 && !m_geometricNormals.contains(vertex))
			{
				const int source = attributes.firstKey(), first = m_before.skinning.first[source], count = m_before.skinning.count[source];
				for (int i = first; i < first + count; ++i)
					result.influences.append(m_before.skinning.influences[i]);
			}
			else
			{
				QVector<ModelSkinBlendSource> sources;
				for (auto a = attributes.cbegin(); a != attributes.cend(); ++a)
					sources.append({&m_before.skinning, a.key(), a.value()});
				QVector<ModelJointInfluence> influences;
				switch (blendModelSkinInfluences(sources, &influences, &m_work))
				{
				case ModelSkinBlendStatus::Blended:
					break;
				case ModelSkinBlendStatus::Cancelled:
					return false;
				case ModelSkinBlendStatus::NegativeWeight:
					return refuse(BevelText::tr("Repair negative skin weights before beveling their vertices."));
				case ModelSkinBlendStatus::InfluenceCount:
					return refuse(
						BevelText::tr("A bevel vertex needs 1–64 skin influences. Repair or reduce the source influences first."));
				}
				// Geometric normals are rebuilt from the faces when baked.
				if (m_geometricNormals.contains(vertex))
					for (auto &influence : influences)
						influence.normalOffset = {0, 0, 0};
				result.influences += influences;
			}
			result.count.append(result.influences.size() - result.first.last());
		}
		m_surface.skinning = std::move(result);
		return true;
	}

	ModelMesh &m_mesh;
	const ModelEdit &m_edit;
	ModelSelection &m_selection;
	QString *m_error;
	const ModelWorkControl &m_control;
	ModelWorkProgress m_work;
	ModelSurface &m_surface;
	const ModelSurface m_before;
	int m_frame;
	QString m_stage = BevelText::tr("corner offsets");
	ModelQuadTopology m_topology;
	QSet<ModelEdge> m_chosen;
	QVector<ModelEdge> m_ordered;
	QSet<int> m_active, m_closed, m_newFaces, m_geometricNormals;
	QVector<BevelPoint> m_points;
	QVector<int> m_indexGeometry;
	QVector<Weights> m_attributes;
	QHash<QPair<int, int>, int> m_cuts, m_cornerCopies;
	QVector<std::array<BevelSide, 4>> m_sides;
};
} // namespace

bool bevelEdges(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	return BevelBuilder(*mesh, edit, *selection, error, control).run();
}
} // namespace vibestudio::model_tools
