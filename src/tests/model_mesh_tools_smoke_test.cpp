// Edit-mode mesh tools and selection operators: geometric topology, quad
// loops and rings, every tool's topology/UV/pose contract and the CLI.
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_geometric_topology.h"
#include "core/model_lod.h"
#include "core/model_mesh_tools.h"
#include "core/model_selection_tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
bool near(double a, double b, double tolerance = 1e-4)
{
	return std::abs(a - b) <= tolerance;
}
// A two-pose mesh; the second pose is the first raised by 8 units.
ModelMesh meshOf(const QVector<ModelVec3> &positions, const QVector<ModelTriangle> &triangles, const QVector<ModelTexCoord> &uvs = {})
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	auto &surface = mesh.surfaces[0];
	surface.triangles = triangles;
	surface.vertexCount = positions.size();
	if (uvs.isEmpty())
	{
		surface.texCoords.clear();
		for (const auto &p : positions)
			surface.texCoords.append({p.x / 64.0f, p.y / 64.0f});
	}
	else
		surface.texCoords = uvs;
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame, frame};
	for (auto &point : surface.frames[1].positions)
		point.z += 8;
	surface.uvSeams.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// A flat grid of quads (two triangles each) from (x0, y0) with `size` cells.
ModelMesh gridMesh(int nx, int ny, double size = 8, double x0 = 0, double y0 = 0)
{
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= ny; ++j)
	{
		for (int i = 0; i <= nx; ++i)
			positions.append({float(x0 + i * size), float(y0 + j * size), 0});
	}
	const auto at = [&](int i, int j) { return j * (nx + 1) + i; };
	for (int j = 0; j < ny; ++j)
	{
		for (int i = 0; i < nx; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	}
	return meshOf(positions, triangles);
}
// A closed, fully shared cube: 8 vertices and 12 outward triangles.
ModelMesh cubeMesh(float h = 8)
{
	const QVector<ModelVec3> p{{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
	const QVector<ModelTriangle> t{{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
								   {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
	auto mesh = meshOf(p, t);
	auto &surface = mesh.surfaces[0];
	for (auto &frame : surface.frames)
	{
		for (int v = 0; v < 8; ++v)
		{
			const auto q = frame.positions[v];
			const float l = std::sqrt(float(h * h * 3));
			frame.normals[v] = {q.x / l, q.y / l, (q.z - (frame.positions[0].z + h)) / l};
		}
	}
	for (int v = 0; v < 8; ++v)
		surface.frames[1].normals[v] = surface.frames[0].normals[v];
	return mesh;
}
bool everyFaceFacesUp(const ModelMesh &mesh)
{
	for (const auto &frame : mesh.surfaces[0].frames)
	{
		for (const auto &t : mesh.surfaces[0].triangles)
		{
			const auto a = frame.positions[t.a], b = frame.positions[t.b], c = frame.positions[t.c];
			const double z = double(b.x - a.x) * (c.y - a.y) - double(b.y - a.y) * (c.x - a.x);
			if (z <= 0)
				return false;
		}
	}
	return true;
}
bool closedManifold(const ModelSurface &surface)
{
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology))
		return false;
	for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
	{
		if (it.value().size() != 2)
			return false;
	}
	return !topology.edgeFaces.isEmpty();
}
bool posesAgree(const ModelSurface &surface)
{
	// Every vertex of the second pose is the first raised by 8 units.
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		const auto a = surface.frames[0].positions[v], b = surface.frames[1].positions[v];
		if (!near(a.x, b.x) || !near(a.y, b.y) || !near(a.z + 8, b.z))
			return false;
	}
	return true;
}
ModelEdit toolEdit(ModelEditKind kind, const ModelSelection &selection)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = selection;
	return edit;
}
QSet<int> allFaces(const ModelMesh &mesh, int surface = 0)
{
	QSet<int> faces;
	for (int face = 0; face < mesh.surfaces[surface].triangles.size(); ++face)
		faces.insert(face);
	return faces;
}
int vertexAt(const ModelSurface &surface, float x, float y, float z = 0)
{
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		const auto p = surface.frames[0].positions[v];
		if (near(p.x, x) && near(p.y, y) && near(p.z, z))
			return v;
	}
	return -1;
}

void checkGeometricTopology()
{
	// Two triangles that share an edge only geometrically (a UV seam).
	auto mesh = meshOf({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}, {0, 0, 0}, {10, 10, 0}}, {{0, 1, 2}, {4, 5, 3}},
					   {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.25f, 0}, {1.25f, 1}});
	ModelGeometricTopology topology;
	expect(buildModelGeometricTopology(mesh.surfaces[0], &topology), "geometric topology builds");
	expect(topology.group(4) == 0 && topology.group(5) == 2, "seam copies share one geometric vertex");
	expect(topology.edgeFaces.value(modelEdge(0, 2)).size() == 2, "a seam edge joins its two faces geometrically");
	expect(topology.copies[0] == QVector<int>({0, 4}), "copies list every index at a position");
	auto grid = gridMesh(4, 4);
	ModelQuadTopology quads;
	expect(buildModelQuadTopology(grid.surfaces[0], 0, 40, &quads), "quad view builds");
	expect(quads.elements.size() == 16 && quads.diagonals.size() == 16, "a triangulated grid pairs into quads");
	// A horizontal interior edge loops across the grid; its ring runs up the column.
	const auto &s = grid.surfaces[0];
	const int a = vertexAt(s, 0, 8), b = vertexAt(s, 8, 8);
	bool closed = true;
	const auto loop = modelEdgeLoop(quads, quads.geometry.geometricEdge(a, b), &closed);
	expect(loop.size() == 4 && !closed, "an edge loop spans the grid");
	QVector<int> elements;
	const auto ring = modelEdgeRing(quads, quads.geometry.geometricEdge(a, b), &closed, &elements);
	expect(ring.size() == 5 && elements.size() == 4 && !closed, "an edge ring crosses the column of quads");
}

void checkLoopCut()
{
	auto mesh = gridMesh(3, 1);
	ModelDocument document;
	QString error;
	expect(document.setMesh(mesh, &error), "loop-cut fixture loads");
	const auto &s0 = document.mesh().surfaces[0];
	const int a = vertexAt(s0, 0, 0), b = vertexAt(s0, 0, 8);
	auto edit = toolEdit(ModelEditKind::LoopCut, {0, {}, {}, {modelEdge(a, b)}});
	expect(document.edit(edit, &error), "loop cut across a strip");
	const auto &s = document.mesh().surfaces[0];
	expect(s.triangles.size() == 12 && s.vertexCount == 12, "one cut doubles the strip's quads");
	expect(document.selection().edges.size() == 3, "the new loop is selected");
	expect(vertexAt(s, 0, 4) >= 0 && vertexAt(s, 24, 4) >= 0, "cut points sit halfway up the ring edges");
	expect(everyFaceFacesUp(document.mesh()) && posesAgree(s), "loop cut keeps winding and every pose");
	expect(document.undo(), "loop cut undoes");
	edit.tool.cuts = 3;
	expect(document.edit(edit, &error) && document.mesh().surfaces[0].triangles.size() == 24, "three cuts quadruple the quads");
	// A seam on the ring: both sides get their own coincident points.
	auto seamed = meshOf({{0, 0, 0}, {8, 0, 0}, {8, 8, 0}, {0, 8, 0}, {8, 0, 0}, {16, 0, 0}, {16, 8, 0}, {8, 8, 0}},
						 {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}},
						 {{0, 0}, {0.5f, 0}, {0.5f, 0.5f}, {0, 0.5f}, {0.6f, 0}, {1, 0}, {1, 0.5f}, {0.6f, 0.5f}});
	ModelDocument seam;
	expect(seam.setMesh(seamed, &error), "seamed strip loads");
	auto cut = toolEdit(ModelEditKind::LoopCut, {0, {}, {}, {modelEdge(0, 3)}});
	expect(seam.edit(cut, &error), "a loop cut crosses a UV seam");
	const auto &ss = seam.mesh().surfaces[0];
	expect(ss.vertexCount == 12 && ss.triangles.size() == 8, "each seam side gets its own cut point");
	int atSeam = 0;
	for (int v = 0; v < ss.vertexCount; ++v)
		atSeam += near(ss.frames[0].positions[v].x, 8) && near(ss.frames[0].positions[v].y, 4);
	expect(atSeam == 2, "seam cut points coincide in position");
	// No quads: a lone triangle cannot take a loop.
	auto lone = meshOf({{0, 0, 0}, {8, 0, 0}, {0, 8, 0}}, {{0, 1, 2}});
	ModelDocument single;
	expect(single.setMesh(lone, &error), "lone triangle loads");
	expect(!single.edit(toolEdit(ModelEditKind::LoopCut, {0, {}, {}, {modelEdge(0, 1)}}), &error) && !error.isEmpty(),
		   "loop cut refuses an edge without quads");
}

void checkEdgeTools()
{
	auto mesh = gridMesh(1, 1);
	ModelDocument document;
	QString error;
	expect(document.setMesh(mesh, &error), "square loads");
	expect(document.edit(toolEdit(ModelEditKind::RotateEdges, {0, {}, {}, {modelEdge(0, 3)}}), &error), "rotate the square's diagonal");
	const auto &s = document.mesh().surfaces[0];
	expect(document.selection().edges == QSet<ModelEdge>{modelEdge(1, 2)}, "the other diagonal is selected");
	expect(everyFaceFacesUp(document.mesh()), "rotated faces keep their winding");
	expect(!document.edit(toolEdit(ModelEditKind::RotateEdges, {0, {}, {}, {modelEdge(0, 1)}}), &error), "a border edge cannot rotate");
	Q_UNUSED(s);
	// Poke the first face.
	expect(document.edit(toolEdit(ModelEditKind::PokeFaces, {0, {}, {0}}), &error), "poke a face");
	expect(document.mesh().surfaces[0].triangles.size() == 4 && document.selection().faces.size() == 3, "poke adds a fan");
	expect(everyFaceFacesUp(document.mesh()) && posesAgree(document.mesh().surfaces[0]), "poke keeps winding and poses");
	// Beautify a skinny fan back towards even triangles.
	auto skinny = meshOf({{0, 0, 0}, {40, 0, 0}, {40, 4, 0}, {0, 4, 0}}, {{0, 1, 3}, {1, 2, 3}});
	ModelDocument even;
	expect(even.setMesh(skinny, &error), "skinny quad loads");
	expect(!even.edit(toolEdit(ModelEditKind::BeautifyFaces, {0, {}, {0, 1}}), &error), "an already even pair is left alone");
	auto bad = meshOf({{0, 0, 0}, {40, 0, 0}, {41, 4, 0}, {0, 4, 0}}, {{0, 1, 2}, {0, 2, 3}});
	ModelDocument beautify;
	expect(beautify.setMesh(bad, &error), "uneven quad loads");
	expect(beautify.edit(toolEdit(ModelEditKind::BeautifyFaces, {0, {}, {0, 1}}), &error), "beautify flips a worse diagonal");
	expect(everyFaceFacesUp(beautify.mesh()), "beautified faces keep their winding");
	// Make Face fills a missing corner with consistent winding.
	auto open = meshOf({{0, 0, 0}, {8, 0, 0}, {8, 8, 0}, {0, 8, 0}}, {{0, 1, 2}});
	ModelDocument fill;
	expect(fill.setMesh(open, &error), "open square loads");
	expect(fill.edit(toolEdit(ModelEditKind::MakeFace, {0, {0, 2, 3}, {}}), &error), "make a face from three vertices");
	expect(fill.mesh().surfaces[0].triangles.size() == 2 && everyFaceFacesUp(fill.mesh()), "the new face follows its neighbour's winding");
	expect(!fill.edit(toolEdit(ModelEditKind::MakeFace, {0, {0, 2, 3}, {}}), &error), "make face refuses a duplicate");
	// Extrude a border edge.
	ModelDocument wall;
	expect(wall.setMesh(gridMesh(1, 1), &error), "wall fixture loads");
	auto extrude = toolEdit(ModelEditKind::ExtrudeEdges, {0, {}, {}, {modelEdge(2, 3)}});
	extrude.translation = {0, 0, 8};
	expect(wall.edit(extrude, &error), "extrude a border edge");
	expect(wall.mesh().surfaces[0].triangles.size() == 4 && wall.mesh().surfaces[0].vertexCount == 6, "extrude adds a strip");
	expect(wall.selection().edges.size() == 1, "the new border is selected");
	auto inner = toolEdit(ModelEditKind::ExtrudeEdges, {0, {}, {}, {modelEdge(0, 3)}});
	inner.translation = {0, 0, 8};
	expect(!wall.edit(inner, &error), "extrude edges refuses an interior edge");
}

void checkMergeAndDissolve()
{
	QString error;
	ModelDocument document;
	expect(document.setMesh(gridMesh(1, 1), &error), "merge fixture loads");
	auto merge = toolEdit(ModelEditKind::MergeVertices, {0, {0, 1}, {}});
	expect(document.edit(merge, &error), "merge two vertices at their centre");
	const auto &s = document.mesh().surfaces[0];
	expect(s.triangles.size() == 1 && s.vertexCount == 3, "the collapsed face and vertex are removed");
	const int merged = *document.selection().vertices.cbegin();
	expect(near(s.frames[0].positions[merged].x, 4) && near(s.frames[1].positions[merged].z, 8), "merge uses each pose's centre");
	expect(document.undo(), "merge undoes");
	merge.tool.merge = ModelMergeMode::Point;
	merge.tool.point = {4, -4, 0};
	expect(document.edit(merge, &error), "merge at a point");
	const int atPoint = *document.selection().vertices.cbegin();
	expect(near(document.mesh().surfaces[0].frames[0].positions[atPoint].y, -4) &&
			   near(document.mesh().surfaces[0].frames[1].positions[atPoint].z, 8),
		   "point merge follows other poses' motion");
	// Collapse two separate edges of a grid into two points.
	ModelDocument grid;
	expect(grid.setMesh(gridMesh(4, 1), &error), "collapse grid loads");
	const auto &g = grid.mesh().surfaces[0];
	auto collapse = toolEdit(ModelEditKind::MergeVertices,
							 {0, {}, {}, {modelEdge(vertexAt(g, 0, 0), vertexAt(g, 8, 0)), modelEdge(vertexAt(g, 24, 8), vertexAt(g, 32, 8))}});
	collapse.tool.merge = ModelMergeMode::Collapse;
	expect(grid.edit(collapse, &error), "collapse separate edges");
	expect(grid.selection().vertices.size() == 2 && grid.mesh().surfaces[0].vertexCount == 8, "each edge collapses to its own point");
	// Dissolve an interior vertex of a 2x2 grid.
	ModelDocument dissolve;
	expect(dissolve.setMesh(gridMesh(2, 2), &error), "dissolve grid loads");
	const int centre = vertexAt(dissolve.mesh().surfaces[0], 8, 8);
	expect(dissolve.edit(toolEdit(ModelEditKind::DissolveVertices, {0, {centre}, {}}), &error), "dissolve an interior vertex");
	expect(dissolve.mesh().surfaces[0].vertexCount == 8 && dissolve.mesh().surfaces[0].triangles.size() == 6,
		   "the hole is refilled without the vertex");
	expect(everyFaceFacesUp(dissolve.mesh()) && posesAgree(dissolve.mesh().surfaces[0]), "dissolve keeps winding and poses");
	// Dissolve faces: a 2x2 region becomes one polygon.
	ModelDocument faces;
	expect(faces.setMesh(gridMesh(2, 2), &error), "dissolve faces grid loads");
	expect(faces.edit(toolEdit(ModelEditKind::DissolveFaces, {0, {}, allFaces(faces.mesh())}), &error), "dissolve faces");
	expect(faces.mesh().surfaces[0].vertexCount == 8 && faces.mesh().surfaces[0].triangles.size() == 6, "interior vertices go");
}

void checkInset()
{
	QString error;
	ModelDocument document;
	expect(document.setMesh(gridMesh(2, 2), &error), "inset grid loads");
	auto inset = toolEdit(ModelEditKind::InsetFaces, {0, {}, allFaces(document.mesh())});
	inset.tool.insetThickness = 2;
	expect(document.edit(inset, &error), "inset a region");
	const auto &s = document.mesh().surfaces[0];
	expect(s.triangles.size() == 8 + 16 && s.vertexCount == 9 + 8, "region inset adds a rim");
	expect(vertexAt(s, 2, 2) >= 0 && vertexAt(s, 14, 14) >= 0, "inner corners sit one thickness inside");
	expect(everyFaceFacesUp(document.mesh()) && posesAgree(s), "inset keeps winding and poses");
	expect(document.undo(), "inset undoes");
	inset.tool.insetThickness = 20;
	expect(!document.edit(inset, &error), "too thick an inset is refused");
	inset.tool.insetThickness = 1;
	inset.tool.inset = ModelInsetMode::Individual;
	inset.selection.faces = {0};
	expect(document.edit(inset, &error), "inset one face individually");
	expect(document.mesh().surfaces[0].triangles.size() == 8 + 6 && document.mesh().surfaces[0].vertexCount == 12, "individual inset");
	inset.tool.insetDepth = 3;
	inset.tool.inset = ModelInsetMode::Region;
	inset.selection.faces = {2, 3};
	expect(document.edit(inset, &error), "inset with depth");
	bool raised = false;
	for (const auto &p : document.mesh().surfaces[0].frames[0].positions)
		raised |= near(p.z, 3);
	expect(raised, "inset depth lifts the inner region");
}

void checkShape()
{
	QString error;
	ModelDocument cube;
	expect(cube.setMesh(cubeMesh(), &error), "cube loads");
	auto fatten = toolEdit(ModelEditKind::ShrinkFatten, {0, {}, allFaces(cube.mesh())});
	fatten.tool.offset = 2;
	expect(cube.edit(fatten, &error), "fatten a cube");
	const auto &p = cube.mesh().surfaces[0].frames[0].positions;
	expect(near(p[6].x, 10, 1e-3) && near(p[6].y, 10, 1e-3) && near(p[6].z, 10, 1e-3), "even thickness offsets faces by the distance");
	// Smooth a raised point back into the grid.
	auto bumped = gridMesh(2, 2);
	const int middle = vertexAt(bumped.surfaces[0], 8, 8);
	for (auto &frame : bumped.surfaces[0].frames)
		frame.positions[middle].z += 5;
	ModelDocument smooth;
	expect(smooth.setMesh(bumped, &error), "bumped grid loads");
	auto relax = toolEdit(ModelEditKind::SmoothVertices, {0, {middle}, {}});
	relax.tool.smoothFactor = 1;
	expect(smooth.edit(relax, &error), "smooth a vertex");
	expect(near(smooth.mesh().surfaces[0].frames[0].positions[middle].z, 0) && near(smooth.mesh().surfaces[0].frames[1].positions[middle].z, 8),
		   "smoothing moves to the neighbours' average in every pose");
	// Proportional editing with a linear falloff.
	ModelDocument proportional;
	expect(proportional.setMesh(gridMesh(4, 4), &error), "proportional grid loads");
	const auto &g = proportional.mesh().surfaces[0];
	const int centre = vertexAt(g, 16, 16), neighbour = vertexAt(g, 24, 16), far = vertexAt(g, 32, 16);
	auto lift = toolEdit(ModelEditKind::WeightedTransform, {0, {centre}, {}});
	lift.translation = {0, 0, 10};
	lift.pivotMode = ModelTransformPivot::SelectionCentre;
	lift.tool.proportionalRadius = 16;
	lift.tool.falloff = ModelFalloff::Linear;
	expect(proportional.edit(lift, &error), "proportional move");
	const auto &moved = proportional.mesh().surfaces[0].frames[0].positions;
	expect(near(moved[centre].z, 10) && near(moved[neighbour].z, 5) && near(moved[far].z, 0), "falloff weights the move by distance");
	// Mirror editing moves the counterpart across X.
	ModelDocument mirror;
	expect(mirror.setMesh(gridMesh(2, 1, 8, -8, 0), &error), "mirror grid loads");
	const auto &m = mirror.mesh().surfaces[0];
	const int right = vertexAt(m, 8, 8), left = vertexAt(m, -8, 8);
	auto mirrored = toolEdit(ModelEditKind::WeightedTransform, {0, {right}, {}});
	mirrored.translation = {2, 0, 1};
	mirrored.pivotMode = ModelTransformPivot::SelectionCentre;
	mirrored.tool.mirrorAxes = 1;
	expect(mirror.edit(mirrored, &error), "mirrored move");
	const auto &mm = mirror.mesh().surfaces[0].frames[0].positions;
	expect(near(mm[right].x, 10) && near(mm[left].x, -10) && near(mm[left].z, 1), "the mirror counterpart moves symmetrically");
}

void checkShading()
{
	QString error;
	ModelDocument cube;
	expect(cube.setMesh(cubeMesh(), &error), "shading cube loads");
	expect(cube.edit(toolEdit(ModelEditKind::ShadeAutoSmooth, {0, {}, allFaces(cube.mesh())}), &error), "auto smooth a cube");
	const auto &s = cube.mesh().surfaces[0];
	expect(s.vertexCount == 24, "each cube side gets its own corners");
	bool axisNormals = true;
	for (const auto &n : s.frames[0].normals)
		axisNormals &= near(std::abs(n.x) + std::abs(n.y) + std::abs(n.z), 1);
	expect(axisNormals && closedManifold(s), "hard edges keep side normals and the cube stays closed");
	expect(cube.undo(), "auto smooth undoes");
	expect(cube.edit(toolEdit(ModelEditKind::ShadeFlat, {0, {}, allFaces(cube.mesh())}), &error), "shade flat");
	expect(cube.mesh().surfaces[0].vertexCount == 36, "flat shading gives every face private corners");
	expect(cube.edit(toolEdit(ModelEditKind::ShadeSmooth, {0, {}, allFaces(cube.mesh())}), &error), "shade smooth");
	expect(cube.mesh().surfaces[0].vertexCount == 8, "smooth shading welds same-UV copies");
	const auto n = cube.mesh().surfaces[0].frames[0].normals[6];
	expect(near(n.x, n.y) && near(n.y, n.z) && n.x > 0.5f, "smooth corners face diagonally");
}

void checkCuts()
{
	QString error;
	ModelDocument bisect;
	expect(bisect.setMesh(gridMesh(2, 2), &error), "bisect grid loads");
	auto cut = toolEdit(ModelEditKind::Bisect, {0, {}, allFaces(bisect.mesh())});
	cut.tool.point = {5, 0, 0};
	cut.tool.normal = {1, 0, 0};
	cut.tool.keep = ModelBisectKeep::Front;
	expect(bisect.edit(cut, &error), "bisect a grid");
	double lowest = 1e9;
	for (const auto &p : bisect.mesh().surfaces[0].frames[0].positions)
		lowest = std::min(lowest, double(p.x));
	expect(near(lowest, 5) && !bisect.selection().edges.isEmpty(), "the back half is removed along the cut");
	expect(everyFaceFacesUp(bisect.mesh()) && posesAgree(bisect.mesh().surfaces[0]), "bisect keeps winding and poses");
	// Symmetrize: the -X half becomes a mirror of the +X half.
	auto lopsided = gridMesh(4, 2, 4, -8, 0);
	for (auto &frame : lopsided.surfaces[0].frames)
	{
		for (auto &p : frame.positions)
		{
			if (p.x < -0.5f)
				p.y += 1;
		}
	}
	ModelDocument symmetric;
	expect(symmetric.setMesh(lopsided, &error), "lopsided grid loads");
	auto symmetrize = toolEdit(ModelEditKind::Symmetrize, {0, {}, allFaces(symmetric.mesh())});
	symmetrize.tool.axis = 0;
	expect(symmetric.edit(symmetrize, &error), "symmetrize across X");
	const auto &s = symmetric.mesh().surfaces[0];
	bool mirrored = true;
	for (int v = 0; v < s.vertexCount; ++v)
	{
		const auto p = s.frames[0].positions[v];
		mirrored &= vertexAt(s, -p.x, p.y, p.z) >= 0;
	}
	expect(mirrored && s.triangles.size() == 16, "every vertex has a mirror partner");
	expect(everyFaceFacesUp(symmetric.mesh()) && posesAgree(s), "mirrored faces keep the original winding");
}

void checkPrimitives()
{
	QString error;
	for (auto primitive : {ModelPrimitive::Plane, ModelPrimitive::Cube, ModelPrimitive::Circle, ModelPrimitive::Grid, ModelPrimitive::Cylinder,
						   ModelPrimitive::Cone, ModelPrimitive::UvSphere, ModelPrimitive::IcoSphere, ModelPrimitive::Torus})
	{
		ModelDocument document;
		expect(document.setMesh(gridMesh(1, 1), &error), "primitive host loads");
		auto add = toolEdit(ModelEditKind::AddPrimitive, {0, {}, {}});
		add.tool.primitive = primitive;
		add.tool.point = {100, 0, 0};
		add.tool.size = primitive == ModelPrimitive::Torus ? std::array<double, 3>{48, 48, 16} : std::array<double, 3>{32, 32, 16};
		add.tool.segments = 12;
		add.tool.rings = primitive == ModelPrimitive::IcoSphere ? 2 : 6;
		add.tool.axis = 2;
		const bool added = document.edit(add, &error);
		if (!expect(added, "add a primitive"))
		{
			std::cerr << modelPrimitiveName(primitive).toStdString() << ": " << error.toStdString() << '\n';
			continue;
		}
		const auto &s = document.mesh().surfaces[0];
		expect(document.selection().faces.size() == s.triangles.size() - 2, "the primitive's faces are selected");
		// Closed primitives are manifold once the host square is removed.
		const bool closedShape = primitive != ModelPrimitive::Plane && primitive != ModelPrimitive::Circle && primitive != ModelPrimitive::Grid;
		if (closedShape)
		{
			ModelSurface copy = s;
			copy.triangles.remove(0, 2);
			if (!expect(closedManifold(copy), "closed primitives are watertight"))
				std::cerr << "  " << modelPrimitiveName(primitive).toStdString() << '\n';
			// Outward winding: every face normal points away from the centre.
			bool outward = true;
			for (const auto &t : std::as_const(copy.triangles))
			{
				const auto a = copy.frames[0].positions[t.a], b = copy.frames[0].positions[t.b], c = copy.frames[0].positions[t.c];
				const double nx = double(b.y - a.y) * (c.z - a.z) - double(b.z - a.z) * (c.y - a.y);
				const double ny = double(b.z - a.z) * (c.x - a.x) - double(b.x - a.x) * (c.z - a.z);
				const double nz = double(b.x - a.x) * (c.y - a.y) - double(b.y - a.y) * (c.x - a.x);
				const double cx = (a.x + b.x + c.x) / 3.0 - 100, cy = (a.y + b.y + c.y) / 3.0, cz = (a.z + b.z + c.z) / 3.0;
				double ox = cx, oy = cy, oz = cz;
				if (primitive == ModelPrimitive::Torus)
				{
					const double r = std::sqrt(cx * cx + cy * cy), ring = 24 - 8;
					ox = cx - cx / r * ring;
					oy = cy - cy / r * ring;
				}
				outward &= nx * ox + ny * oy + nz * oz > 0;
			}
			if (!expect(outward, "primitive faces wind outwards"))
				std::cerr << "  " << modelPrimitiveName(primitive).toStdString() << '\n';
		}
	}
}

void checkDecimate()
{
	QString error;
	ModelDocument document;
	expect(document.setMesh(gridMesh(8, 8), &error), "decimate grid loads");
	auto reduce = toolEdit(ModelEditKind::Decimate, {0, {}, allFaces(document.mesh())});
	reduce.tool.ratio = 0.25;
	expect(document.edit(reduce, &error), "decimate a flat grid");
	const auto &s = document.mesh().surfaces[0];
	expect(s.triangles.size() <= 40 && s.triangles.size() >= 30, "decimation removes most interior detail");
	expect(everyFaceFacesUp(document.mesh()) && posesAgree(s), "decimation keeps winding and every pose");
	double minX = 1e9, maxX = -1e9;
	for (const auto &p : s.frames[0].positions)
	{
		minX = std::min(minX, double(p.x));
		maxX = std::max(maxX, double(p.x));
	}
	expect(near(minX, 0) && near(maxX, 64), "boundaries are preserved");
	ModelDocument budget;
	expect(budget.setMesh(gridMesh(8, 8), &error), "budget grid loads");
	reduce.tool.ratio = 0.5;
	reduce.tool.targetTriangles = 100;
	expect(budget.edit(reduce, &error) && budget.mesh().surfaces[0].triangles.size() <= 100, "an explicit triangle budget wins");
}

void checkSelection()
{
	QString error;
	auto grid = gridMesh(4, 4);
	const auto &s = grid.surfaces[0];
	ModelSelection current{0, {}, {0}};
	ModelSelection result;
	ModelSelectRequest request;
	request.operation = ModelSelectOperation::All;
	expect(selectModelComponents(grid, current, request, &result, &error) && result.faces.size() == 32, "select all faces");
	request.operation = ModelSelectOperation::Invert;
	expect(selectModelComponents(grid, current, request, &result, &error) && result.faces.size() == 31, "invert");
	request.operation = ModelSelectOperation::More;
	expect(selectModelComponents(grid, current, request, &result, &error) && result.faces.size() > 1, "select more");
	const auto grown = result;
	request.operation = ModelSelectOperation::Less;
	expect(selectModelComponents(grid, grown, request, &result, &error) && result.faces.size() < grown.faces.size(), "select less");
	request.operation = ModelSelectOperation::Loop;
	request.mode = ModelSelectionMode::Edges;
	request.edge = modelEdge(vertexAt(s, 0, 8), vertexAt(s, 8, 8));
	expect(selectModelComponents(grid, current, request, &result, &error) && result.edges.size() == 4, "edge loop");
	request.operation = ModelSelectOperation::Ring;
	expect(selectModelComponents(grid, current, request, &result, &error) && result.edges.size() == 5, "edge ring");
	request.mode = ModelSelectionMode::Faces;
	request.operation = ModelSelectOperation::Loop;
	expect(selectModelComponents(grid, current, request, &result, &error) && result.faces.size() == 8, "face loop");
	request.mode = ModelSelectionMode::Vertices;
	request.operation = ModelSelectOperation::ShortestPath;
	request.from = vertexAt(s, 0, 0);
	request.to = vertexAt(s, 32, 0);
	expect(selectModelComponents(grid, current, request, &result, &error) && result.vertices.size() == 5, "shortest path along the border");
	request.operation = ModelSelectOperation::NonManifold;
	request.mode = ModelSelectionMode::Edges;
	expect(selectModelComponents(grid, current, request, &result, &error) && result.edges.size() == 16, "open borders are non-manifold");
	request.operation = ModelSelectOperation::Boundary;
	request.mode = ModelSelectionMode::Faces;
	ModelSelection block{0, {}, {0, 1, 2, 3}};
	expect(selectModelComponents(grid, block, request, &result, &error) && result.edges.size() == 6, "boundary loop of a region");
	request.operation = ModelSelectOperation::Random;
	request.seed = 7;
	ModelSelection first, second;
	expect(selectModelComponents(grid, current, request, &first, &error) && selectModelComponents(grid, current, request, &second, &error) &&
			   first == second && !first.faces.isEmpty() && first.faces.size() < 32,
		   "random selection is repeatable");
	request.operation = ModelSelectOperation::Checker;
	ModelSelection everything{0, {}, allFaces(grid)};
	expect(selectModelComponents(grid, everything, request, &result, &error) && result.faces.size() == 16, "checker deselect halves");
	request.operation = ModelSelectOperation::Side;
	request.axis = 0;
	request.threshold = 15;
	expect(selectModelComponents(grid, current, request, &result, &error) && !result.faces.isEmpty(), "select one side");
	// Cube: sharp edges, similar normals and facing.
	auto cube = cubeMesh();
	request.operation = ModelSelectOperation::Sharp;
	request.mode = ModelSelectionMode::Edges;
	request.threshold = 30;
	expect(selectModelComponents(cube, current, request, &result, &error) && result.edges.size() == 12, "a cube has twelve sharp edges");
	request.operation = ModelSelectOperation::Similar;
	request.mode = ModelSelectionMode::Faces;
	request.similarity = ModelSimilarity::Normal;
	request.threshold = 5;
	expect(selectModelComponents(cube, current, request, &result, &error) && result.faces.size() == 2, "similar normals pick one side");
	request.operation = ModelSelectOperation::Facing;
	request.axis = 2;
	request.positive = true;
	request.threshold = 10;
	expect(selectModelComponents(cube, current, request, &result, &error) && result.faces.size() == 2, "faces facing +Z");
	// Linked across a seam, and delimited by it.
	auto seamed = meshOf({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}, {0, 0, 0}, {10, 10, 0}}, {{0, 1, 2}, {4, 5, 3}},
						 {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.25f, 0}, {1.25f, 1}});
	seamed.surfaces[0].uvSeams.insert(modelEdge(0, 2));
	request = {};
	request.operation = ModelSelectOperation::Linked;
	request.mode = ModelSelectionMode::Faces;
	ModelSelection one{0, {}, {0}};
	expect(selectModelComponents(seamed, one, request, &result, &error) && result.faces.size() == 2, "linked crosses UV seams");
	request.delimitSeams = true;
	expect(selectModelComponents(seamed, one, request, &result, &error) && result.faces.size() == 1, "delimited linked stops at seams");
	// Mirror selection.
	auto symmetric = gridMesh(2, 1, 8, -8, 0);
	request = {};
	request.operation = ModelSelectOperation::Mirror;
	request.mode = ModelSelectionMode::Vertices;
	ModelSelection rightSide{0, {vertexAt(symmetric.surfaces[0], 8, 0)}, {}};
	expect(selectModelComponents(symmetric, rightSide, request, &result, &error) &&
			   result.vertices == QSet<int>{vertexAt(symmetric.surfaces[0], -8, 0)},
		   "mirror selection finds the counterpart");
}

void checkBevelAndSolidify()
{
	QString error;
	ModelDocument cube;
	expect(cube.setMesh(cubeMesh(), &error), "bevel cube loads");
	auto bevel = toolEdit(ModelEditKind::BevelVertices, {0, {6}, {}});
	bevel.tool.bevelWidth = 2;
	expect(cube.edit(bevel, &error), "bevel one corner");
	const auto &s = cube.mesh().surfaces[0];
	expect(vertexAt(s, 8, 8, 8) < 0 && vertexAt(s, 6, 8, 8) >= 0 && vertexAt(s, 8, 6, 8) >= 0 && vertexAt(s, 8, 8, 6) >= 0,
		   "the corner becomes points two units along its edges");
	expect(closedManifold(s) && posesAgree(s), "a bevelled cube stays closed in every pose");
	expect(!cube.selection().faces.isEmpty(), "the bevel faces are selected");
	ModelDocument all;
	expect(all.setMesh(cubeMesh(), &error), "second bevel cube loads");
	bevel.selection = {0, {0, 1, 2, 3, 4, 5, 6, 7}, {}};
	expect(all.edit(bevel, &error), "bevel every corner");
	expect(closedManifold(all.mesh().surfaces[0]), "bevelling every corner keeps the cube closed");
	bool corners = false;
	for (const auto &p : all.mesh().surfaces[0].frames[0].positions)
		corners |= std::abs(p.x) == 8 && std::abs(p.y) == 8 && std::abs(p.z) == 8;
	expect(!corners, "no original corner remains");
	bevel.tool.bevelWidth = 20;
	ModelDocument wide;
	expect(wide.setMesh(cubeMesh(), &error) && !wide.edit(bevel, &error), "a bevel wider than an edge is refused");

	ModelDocument sheet;
	expect(sheet.setMesh(gridMesh(2, 2), &error), "solidify sheet loads");
	auto solid = toolEdit(ModelEditKind::Solidify, {0, {}, allFaces(sheet.mesh())});
	solid.tool.solidifyThickness = 2;
	expect(sheet.edit(solid, &error), "solidify a sheet");
	const auto &t = sheet.mesh().surfaces[0];
	expect(t.triangles.size() == 8 + 8 + 16, "a shell adds flipped back faces and a rim");
	expect(closedManifold(t) && posesAgree(t), "the shell is closed in every pose");
	double lowest = 1e9;
	for (const auto &p : t.frames[0].positions)
		lowest = std::min(lowest, double(p.z));
	expect(near(lowest, -2), "the back faces sit one thickness behind");
}

void checkLods()
{
	QString error;
	QVector<ModelLodLevel> levels;
	expect(buildModelLods(gridMesh(8, 8), 2, 0.5, &levels, &error) && levels.size() == 2, "build two detail levels");
	if (levels.size() == 2)
		expect(levels[0].triangles <= 64 && levels[1].triangles <= 32 && levels[1].triangles < levels[0].triangles,
			   "each level halves the previous one");
	expect(modelLodPath(QStringLiteral("models/prop.md3"), 2) == QStringLiteral("models/prop_2.md3") &&
			   modelLodPath(QStringLiteral("prop.md3"), 1) == QStringLiteral("prop_1.md3"),
		   "Quake III level names");
	expect(!buildModelLods(gridMesh(2, 2), 4, 0.5, &levels, &error), "at most three levels");
}

void checkCli(const QString &executable)
{
	QTemporaryDir directory;
	if (!expect(directory.isValid(), "temporary directory"))
		return;
	const auto source = directory.filePath(QStringLiteral("grid.mesh.json"));
	{
		ModelDocument document;
		QString error;
		expect(document.setMesh(gridMesh(3, 1), &error) && document.save(source, false, &error), "write a CLI source");
	}
	const auto run = [&](const QStringList &args, QJsonObject *json)
	{
		QProcess process;
		process.start(executable, QStringList{QStringLiteral("--cli")} + args + QStringList{QStringLiteral("--json")});
		process.waitForFinished(60000);
		if (json)
			*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		return process.exitCode();
	};
	QJsonObject json;
	const auto output = directory.filePath(QStringLiteral("cut.mesh.json"));
	expect(run({QStringLiteral("model"), QStringLiteral("tool"), source, QStringLiteral("--tool"), QStringLiteral("loop-cut"),
				QStringLiteral("--edges"), QStringLiteral("0:4"), QStringLiteral("--output"), output},
			   &json) == 0,
		   "CLI loop cut succeeds");
	expect(json.value(QStringLiteral("surface")).toObject().value(QStringLiteral("triangles")).toInt() == 12 && QFile::exists(output),
		   "CLI loop cut writes the result");
	expect(run({QStringLiteral("model"), QStringLiteral("tool"), source, QStringLiteral("--tool"), QStringLiteral("inset"),
				QStringLiteral("--faces"), QStringLiteral("all"), QStringLiteral("--thickness"), QStringLiteral("1"), QStringLiteral("--output"),
				directory.filePath(QStringLiteral("inset.mesh.json")), QStringLiteral("--dry-run")},
			   &json) == 0 &&
			   !json.value(QStringLiteral("written")).toBool(),
		   "CLI inset dry run writes nothing");
	expect(run({QStringLiteral("model"), QStringLiteral("tool"), source, QStringLiteral("--tool"), QStringLiteral("inset"),
				QStringLiteral("--edges"), QStringLiteral("0:1"), QStringLiteral("--output"), directory.filePath(QStringLiteral("x.mesh.json"))},
			   nullptr) == 2,
		   "CLI refuses options a tool does not take");
	expect(run({QStringLiteral("model"), QStringLiteral("tool"), source, QStringLiteral("--tool"), QStringLiteral("add"),
				QStringLiteral("--primitive"), QStringLiteral("cylinder"), QStringLiteral("--at"), QStringLiteral("0,0,32"),
				QStringLiteral("--segments"), QStringLiteral("8"), QStringLiteral("--output"), directory.filePath(QStringLiteral("add.mesh.json"))},
			   &json) == 0 &&
			   json.value(QStringLiteral("selection")).toObject().value(QStringLiteral("faces")).toArray().size() == 32,
		   "CLI adds a primitive and reports its faces");
	expect(run({QStringLiteral("model"), QStringLiteral("select"), source, QStringLiteral("--select"), QStringLiteral("ring"),
				QStringLiteral("--mode"), QStringLiteral("edges"), QStringLiteral("--edge"), QStringLiteral("0:4")},
			   &json) == 0 &&
			   json.value(QStringLiteral("selection")).toObject().value(QStringLiteral("edges")).toArray().size() == 4,
		   "CLI ring selection");
	const auto lodBase = directory.filePath(QStringLiteral("lod/prop.md3"));
	QDir(directory.path()).mkpath(QStringLiteral("lod"));
	expect(run({QStringLiteral("model"), QStringLiteral("lod"), source, QStringLiteral("--output"), lodBase, QStringLiteral("--levels"),
				QStringLiteral("2")},
			   &json) == 0 &&
			   json.value(QStringLiteral("outputs")).toArray().size() == 3 && QFile::exists(directory.filePath(QStringLiteral("lod/prop_2.md3"))),
		   "CLI writes the base MD3 and two detail levels");
	expect(run({QStringLiteral("model"), QStringLiteral("lod"), source, QStringLiteral("--output"), lodBase}, nullptr) == 1,
		   "CLI refuses to replace detail levels without --overwrite");
	expect(run({QStringLiteral("model"), QStringLiteral("tool"), source, QStringLiteral("--tool"), QStringLiteral("solidify"),
				QStringLiteral("--faces"), QStringLiteral("all"), QStringLiteral("--thickness"), QStringLiteral("2"), QStringLiteral("--output"),
				directory.filePath(QStringLiteral("solid.mesh.json"))},
			   &json) == 0 &&
			   json.value(QStringLiteral("surface")).toObject().value(QStringLiteral("triangles")).toInt() == 6 + 6 + 16,
		   "CLI solidify");
	expect(run({QStringLiteral("model"), QStringLiteral("tool"), source, QStringLiteral("--tool"), QStringLiteral("bevel-vertices"),
				QStringLiteral("--vertices"), QStringLiteral("5"), QStringLiteral("--width"), QStringLiteral("1"), QStringLiteral("--output"),
				directory.filePath(QStringLiteral("bevel.mesh.json"))},
			   &json) == 0,
		   "CLI bevel vertices");
	expect(run({QStringLiteral("model"), QStringLiteral("select"), source, QStringLiteral("--select"), QStringLiteral("loop")}, nullptr) == 4,
		   "CLI loop needs a seed edge");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	checkGeometricTopology();
	checkLoopCut();
	checkEdgeTools();
	checkMergeAndDissolve();
	checkInset();
	checkShape();
	checkShading();
	checkCuts();
	checkPrimitives();
	checkDecimate();
	checkSelection();
	checkBevelAndSolidify();
	checkLods();
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]));
	if (failures == 0)
		std::cout << "model mesh tools smoke test passed\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
