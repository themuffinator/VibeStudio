// Select Edge Loops and Select Edge Rings of the selection: every selected
// edge grows to its loop or ring through selectModelEdgeLoops, on a grid and
// an open cylinder, with refusals for diagonals and edges outside the
// surface, sorted and repeatable results, extend and cancellation, seeds
// taken from selected edges or vertices, thousands of seeds in seconds, and
// the same results from `model select --select loop|ring --edges|--vertices`.
#include "cli/model_tools.h"
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_selection_tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
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
ModelMesh meshOf(const QVector<ModelVec3> &positions, const QVector<ModelTriangle> &triangles)
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	auto &surface = mesh.surfaces[0];
	surface.triangles = triangles;
	surface.vertexCount = positions.size();
	surface.texCoords.clear();
	for (const auto &p : positions)
		surface.texCoords.append({p.x / 64.0f, p.y / 64.0f + p.z / 64.0f});
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame};
	surface.uvSeams.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
// A flat grid of quads, vertex j * (cells + 1) + i at (8i, 8j).
constexpr int kCells = 4;
int at(int i, int j)
{
	return j * (kCells + 1) + i;
}
ModelMesh gridMesh()
{
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= kCells; ++j)
	{
		for (int i = 0; i <= kCells; ++i)
			positions.append({float(i * 8), float(j * 8), 0});
	}
	for (int j = 0; j < kCells; ++j)
	{
		for (int i = 0; i < kCells; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	}
	return meshOf(positions, triangles);
}
// An open hexagonal cylinder of three rows of quads: ring r, corner k is r * 6 + k.
constexpr int kSides = 6;
ModelMesh cylinderMesh()
{
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int ring = 0; ring <= 3; ++ring)
	{
		for (int k = 0; k < kSides; ++k)
		{
			const double angle = 2 * 3.14159265358979 * k / kSides;
			positions.append({float(16 * std::cos(angle)), float(16 * std::sin(angle)), float(ring * 8)});
		}
	}
	for (int ring = 0; ring < 3; ++ring)
	{
		for (int k = 0; k < kSides; ++k)
		{
			const int a = ring * kSides + k, b = ring * kSides + (k + 1) % kSides, c = (ring + 1) * kSides + (k + 1) % kSides,
					  d = (ring + 1) * kSides + k;
			triangles.append({a, b, c});
			triangles.append({a, c, d});
		}
	}
	return meshOf(positions, triangles);
}
QJsonArray sorted(const QSet<ModelEdge> &edges)
{
	auto list = edges.values();
	std::sort(list.begin(), list.end());
	QJsonArray array;
	for (const auto edge : std::as_const(list))
		array.append(QJsonArray{edge.first, edge.second});
	return array;
}
// What the editor and CLI promise: the join of each seed's own loop or ring.
ModelSelection joinOfSingles(const ModelMesh &mesh, const ModelSelection &current, ModelSelectRequest request, const QVector<ModelEdge> &seeds)
{
	ModelSelection joined{current.surface, {}, {}};
	for (const auto seed : seeds)
	{
		request.edge = seed;
		request.extend = false;
		ModelSelection one;
		if (selectModelComponents(mesh, current, request, &one))
		{
			joined.faces += one.faces;
			joined.vertices += one.vertices;
			joined.edges += one.edges;
		}
	}
	return joined;
}

void checkGrid()
{
	const auto grid = gridMesh();
	const ModelSelection current{0, {}, {}};
	ModelSelectRequest request;
	request.operation = ModelSelectOperation::Loop;
	request.mode = ModelSelectionMode::Edges;
	ModelEdgeLoopsResult result;
	QString error;
	// Two rows: two loops of four edges.
	const QVector<ModelEdge> rows{modelEdge(at(0, 1), at(1, 1)), modelEdge(at(0, 3), at(1, 3))};
	expect(selectModelEdgeLoops(grid, current, request, rows, &result, &error) && result.selection.edges.size() == 8 && result.grown.size() == 2 &&
			   result.refused.isEmpty(),
		   "two seeds on two rows select both edge loops");
	expect(result.selection.edges == joinOfSingles(grid, current, request, rows).edges, "the result is the join of each seed's loop");
	// A second seed on a loop already grown adds nothing new.
	expect(selectModelEdgeLoops(grid, current, request, {modelEdge(at(0, 1), at(1, 1)), modelEdge(at(2, 1), at(3, 1))}, &result, &error) &&
			   result.selection.edges.size() == 4 && result.grown.size() == 2,
		   "two seeds on one loop give that loop once");
	// Rings: two columns of horizontal edges across the grid.
	request.operation = ModelSelectOperation::Ring;
	const QVector<ModelEdge> columns{modelEdge(at(0, 1), at(1, 1)), modelEdge(at(2, 2), at(3, 2))};
	expect(selectModelEdgeLoops(grid, current, request, columns, &result, &error) && result.selection.edges.size() == 10 &&
			   result.selection.edges == joinOfSingles(grid, current, request, columns).edges,
		   "two seeds in two columns select both edge rings");
	// A quad diagonal and an edge between unconnected vertices are refused,
	// each with its reason, while the other seed grows.
	request.operation = ModelSelectOperation::Loop;
	const QVector<ModelEdge> mixed{modelEdge(at(0, 0), at(1, 1)), modelEdge(at(0, 1), at(1, 1)), modelEdge(at(0, 0), at(4, 4))};
	expect(selectModelEdgeLoops(grid, current, request, mixed, &result, &error) && result.selection.edges.size() == 4 && result.grown.size() == 1 &&
			   result.refused.size() == 2,
		   "diagonal and unconnected seeds are skipped while the good seed grows");
	const auto lines = modelEdgeLoopRefusalLines(result);
	expect(lines.size() == 2 && lines[0].startsWith(QStringLiteral("Edge %1:%2: ").arg(at(0, 0)).arg(at(1, 1))) &&
			   lines[0].contains(QStringLiteral("inside a quad")) && lines[1].startsWith(QStringLiteral("Edge %1:%2: ").arg(at(0, 0)).arg(at(4, 4))) &&
			   lines[1].contains(QStringLiteral("not part of this surface")),
		   "each refusal names its edge and the reason, in sorted order");
	// Seed order does not matter.
	auto reversed = mixed;
	std::reverse(reversed.begin(), reversed.end());
	ModelEdgeLoopsResult again;
	expect(selectModelEdgeLoops(grid, current, request, reversed, &again, &error) && again.selection.edges == result.selection.edges &&
			   modelEdgeLoopRefusalLines(again) == lines && again.grown == result.grown,
		   "the same seeds in another order give the same result and report");
	// Nothing grows: the call fails and names every refused seed.
	expect(!selectModelEdgeLoops(grid, current, request, {modelEdge(at(0, 0), at(1, 1)), modelEdge(at(1, 1), at(2, 2))}, &result, &error) &&
			   error.contains(QStringLiteral("No selected edge has a loop")) && error.contains(QStringLiteral("Edge %1:%2:").arg(at(1, 1)).arg(at(2, 2))),
		   "only diagonals fail with every reason");
	expect(!selectModelEdgeLoops(grid, current, request, {}, &result, &error) && error.contains(QStringLiteral("Select edges first")),
		   "no seeds asks for a selection");
	// Extend keeps what was selected.
	request.extend = true;
	const ModelSelection face{0, {}, {0}};
	expect(selectModelEdgeLoops(grid, face, request, {rows[0]}, &result, &error) && result.selection.edges.size() == 4 && result.selection.faces == QSet<int>{0},
		   "extend adds the loops to the current selection");
	request.extend = false;
	// Face mode: each seed gives the face loop across it, one per column.
	request.mode = ModelSelectionMode::Faces;
	expect(selectModelEdgeLoops(grid, current, request, columns, &result, &error) && result.selection.faces.size() == 16 &&
			   result.selection.faces == joinOfSingles(grid, current, request, columns).faces,
		   "face mode joins the face loops across each seed");
	// Cancellation stops the work and publishes nothing.
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	request.mode = ModelSelectionMode::Edges;
	result.selection.edges = {modelEdge(0, 1)};
	expect(!selectModelEdgeLoops(grid, current, request, rows, &result, &error, cancelled) && !error.isEmpty() && result.selection.edges.isEmpty(),
		   "cancellation fails without a selection");
	request.operation = ModelSelectOperation::Linked;
	expect(!selectModelEdgeLoops(grid, current, request, rows, &result, &error), "only loops and rings grow from seeds");

	// No seeds given: the selected edges, else the edges between selected
	// vertices, as the editor's vertex mode seeds. Quad diagonals between
	// selected vertices are no edge of the quad view and seed nothing.
	request.operation = ModelSelectOperation::Loop;
	request.mode = ModelSelectionMode::Edges;
	expect(selectModelEdgeLoops(grid, {0, {}, {}, {rows[0]}}, request, {}, &result, &error) && result.seeds == QVector<ModelEdge>{rows[0]} &&
			   result.selection.edges.size() == 4,
		   "without seeds the selected edges grow");
	request.mode = ModelSelectionMode::Vertices;
	const ModelSelection row{0, {at(0, 1), at(1, 1), at(2, 1)}, {}};
	expect(selectModelEdgeLoops(grid, row, request, {}, &result, &error) && result.seeds.size() == 2 && result.refused.isEmpty() &&
			   result.selection.vertices.size() == kCells + 1,
		   "selected vertices seed the edges between them, and the loop's vertices are selected");
	const ModelSelection corner{0, {at(0, 0), at(1, 0), at(1, 1)}, {}};
	expect(selectModelEdgeLoops(grid, corner, request, {}, &result, &error) && result.seeds.size() == 2 && result.refused.isEmpty() &&
			   !result.seeds.contains(modelEdge(at(0, 0), at(1, 1))),
		   "a quad diagonal between selected vertices is left out rather than refused");
	expect(!selectModelEdgeLoops(grid, {0, {at(0, 0), at(1, 1)}, {}}, request, {}, &result, &error) && error.contains(QStringLiteral("Select edges first")),
		   "vertices joined only by a diagonal seed nothing");
	// An explicit diagonal seed is still refused with its reason.
	request.mode = ModelSelectionMode::Edges;
	expect(selectModelEdgeLoops(grid, current, request, {rows[0], modelEdge(at(0, 0), at(1, 1))}, &result, &error) && result.refused.size() == 1,
		   "a diagonal given as a seed is refused");
}

// A large grid in one flat surface, `cells` quads a side.
ModelMesh largeGrid(int cells)
{
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	const auto index = [cells](int i, int j) { return j * (cells + 1) + i; };
	for (int j = 0; j <= cells; ++j)
	{
		for (int i = 0; i <= cells; ++i)
			positions.append({float(i * 8), float(j * 8), 0});
	}
	for (int j = 0; j < cells; ++j)
	{
		for (int i = 0; i < cells; ++i)
		{
			triangles.append({index(i, j), index(i + 1, j), index(i + 1, j + 1)});
			triangles.append({index(i, j), index(i + 1, j + 1), index(i, j + 1)});
		}
	}
	return meshOf(positions, triangles);
}

// Thousands of seeds share one topology build: before, every seed rebuilt the
// surface's topology, and selecting all of a 40x40 grid took over 30 s in
// edge mode and two minutes in vertex mode.
void checkScale()
{
	constexpr int cells = 48;
	const auto grid = largeGrid(cells);
	const auto &surface = grid.surfaces[0];
	ModelSelection all{0, {}, {}};
	for (const auto &t : surface.triangles)
		all.edges << modelEdge(t.a, t.b) << modelEdge(t.b, t.c) << modelEdge(t.c, t.a);
	ModelSelectRequest request;
	request.operation = ModelSelectOperation::Loop;
	request.mode = ModelSelectionMode::Edges;
	ModelEdgeLoopsResult result;
	QString error;
	QElapsedTimer timer;
	timer.start();
	const bool edges = selectModelEdgeLoops(grid, all, request, {}, &result, &error);
	const auto edgeTime = timer.elapsed();
	const int sides = 2 * cells * (cells + 1);
	expect(edges && result.selection.edges.size() == sides && result.refused.size() == cells * cells, "every side of a large grid grows; every diagonal is refused");
	request.mode = ModelSelectionMode::Vertices;
	for (int v = 0; v < surface.vertexCount; ++v)
		all.vertices.insert(v);
	all.edges.clear();
	timer.restart();
	const bool vertices = selectModelEdgeLoops(grid, all, request, {}, &result, &error);
	const auto vertexTime = timer.elapsed();
	expect(vertices && result.seeds.size() == sides && result.refused.isEmpty() && result.selection.vertices.size() == surface.vertexCount,
		   "every vertex of a large grid seeds its sides and leaves the diagonals out");
	request.operation = ModelSelectOperation::Ring;
	timer.restart();
	const bool rings = selectModelEdgeLoops(grid, all, request, {}, &result, &error);
	const auto ringTime = timer.elapsed();
	expect(rings && result.selection.vertices.size() == surface.vertexCount, "every ring of a large grid joins");
	std::cout << "edge loops of " << surface.triangles.size() << " triangles: edges " << edgeTime << " ms, vertices " << vertexTime << " ms, rings "
			  << ringTime << " ms\n";
	// A generous bound for a loaded debug machine; the old per-seed rebuild
	// took minutes on a smaller grid.
	expect(edgeTime < 8000 && vertexTime < 8000 && ringTime < 8000, "thousands of seeds finish in seconds");
}

void checkCylinder()
{
	const auto cylinder = cylinderMesh();
	const ModelSelection current{0, {}, {}};
	ModelSelectRequest request;
	request.operation = ModelSelectOperation::Loop;
	request.mode = ModelSelectionMode::Edges;
	ModelEdgeLoopsResult result;
	QString error;
	// Two inner rings of the cylinder: two closed loops of six edges.
	const QVector<ModelEdge> around{modelEdge(kSides, kSides + 1), modelEdge(2 * kSides + 3, 2 * kSides + 4)};
	expect(selectModelEdgeLoops(cylinder, current, request, around, &result, &error) && result.selection.edges.size() == 2 * kSides,
		   "two seeds around a cylinder close two loops");
	// Rings: the upright edges of the bottom and top rows of quads.
	request.operation = ModelSelectOperation::Ring;
	const QVector<ModelEdge> upright{modelEdge(0, kSides), modelEdge(2 * kSides + 1, 3 * kSides + 1)};
	expect(selectModelEdgeLoops(cylinder, current, request, upright, &result, &error) && result.selection.edges.size() == 2 * kSides &&
			   result.selection.edges == joinOfSingles(cylinder, current, request, upright).edges,
		   "two upright seeds select two rings around the cylinder");
	// A diagonal of a side quad has no ring.
	expect(selectModelEdgeLoops(cylinder, current, request, {upright[0], modelEdge(0, kSides + 1)}, &result, &error) && result.refused.size() == 1 &&
			   result.refused[0].edge == modelEdge(0, kSides + 1),
		   "a side quad's diagonal is refused");
}

void checkCli(const QString &executable)
{
	QTemporaryDir directory;
	if (!expect(directory.isValid(), "temporary directory"))
		return;
	const auto source = directory.filePath(QStringLiteral("grid.mesh.json"));
	const auto grid = gridMesh();
	{
		ModelDocument document;
		QString error;
		expect(document.setMesh(grid, &error) && document.save(source, false, &error), "write a CLI source");
	}
	const auto edges = [](const QVector<ModelEdge> &list)
	{
		QStringList text;
		for (const auto edge : list)
			text << QStringLiteral("%1:%2").arg(edge.first).arg(edge.second);
		return text.join(QLatin1Char(','));
	};
	const auto select = [&](const QString &operation, const QStringList &extra)
	{
		return cli::runModelSelect(QStringList{QStringLiteral("vibestudio"), QStringLiteral("--cli"), QStringLiteral("model"),
											   QStringLiteral("select"), source, QStringLiteral("--select"), operation, QStringLiteral("--mode"),
											   QStringLiteral("edges")} +
								   extra);
	};
	const QVector<ModelEdge> rows{modelEdge(at(0, 1), at(1, 1)), modelEdge(at(0, 3), at(1, 3))};
	ModelSelectRequest request;
	request.operation = ModelSelectOperation::Loop;
	request.mode = ModelSelectionMode::Edges;
	ModelEdgeLoopsResult core;
	selectModelEdgeLoops(grid, {0, {}, {}}, request, rows, &core);
	auto result = select(QStringLiteral("loop"), {QStringLiteral("--edges"), edges(rows)});
	expect(result.exitCode == 0 && result.payload.value(QStringLiteral("selection")).toObject().value(QStringLiteral("edges")).toArray() ==
										   sorted(core.selection.edges) &&
			   result.payload.value(QStringLiteral("seeds")).toArray().size() == 2 && result.payload.value(QStringLiteral("refused")).toArray().isEmpty(),
		   "CLI loop grows every --edges seed exactly as the core does");
	request.operation = ModelSelectOperation::Ring;
	selectModelEdgeLoops(grid, {0, {}, {}}, request, rows, &core);
	result = select(QStringLiteral("ring"), {QStringLiteral("--edges"), edges(rows)});
	expect(result.exitCode == 0 &&
			   result.payload.value(QStringLiteral("selection")).toObject().value(QStringLiteral("edges")).toArray() == sorted(core.selection.edges),
		   "CLI ring grows every --edges seed exactly as the core does");
	// A refused seed is reported in JSON and text, and the rest still grow.
	const auto diagonal = modelEdge(at(0, 0), at(1, 1));
	result = select(QStringLiteral("loop"), {QStringLiteral("--edges"), edges({rows[0], diagonal})});
	const auto refused = result.payload.value(QStringLiteral("refused")).toArray();
	expect(result.exitCode == 0 && refused.size() == 1 &&
			   refused[0].toObject().value(QStringLiteral("edge")).toArray() == QJsonArray{diagonal.first, diagonal.second} &&
			   refused[0].toObject().value(QStringLiteral("reason")).toString().contains(QStringLiteral("inside a quad")) &&
			   result.lines.join(QLatin1Char('\n')).contains(QStringLiteral("Edge %1:%2:").arg(diagonal.first).arg(diagonal.second)),
		   "CLI lists a refused seed with its reason");
	result = select(QStringLiteral("loop"), {QStringLiteral("--edges"), edges({diagonal})});
	expect(result.exitCode == 4 && result.error.contains(QStringLiteral("No selected edge has a loop")), "CLI exits 4 when no seed grows");
	// --vertices seeds as the editor's vertex mode does: the edges between
	// them, without quad diagonals.
	const auto vertices = cli::runModelSelect({QStringLiteral("vibestudio"), QStringLiteral("--cli"), QStringLiteral("model"), QStringLiteral("select"),
											   source, QStringLiteral("--select"), QStringLiteral("loop"), QStringLiteral("--mode"),
											   QStringLiteral("vertices"), QStringLiteral("--vertices"),
											   QStringLiteral("%1,%2,%3").arg(at(0, 0)).arg(at(1, 0)).arg(at(1, 1))});
	ModelSelectRequest vertexRequest;
	vertexRequest.operation = ModelSelectOperation::Loop;
	vertexRequest.mode = ModelSelectionMode::Vertices;
	selectModelEdgeLoops(grid, {0, {at(0, 0), at(1, 0), at(1, 1)}, {}}, vertexRequest, {}, &core);
	expect(vertices.exitCode == 0 && vertices.payload.value(QStringLiteral("seeds")).toArray().size() == 2 &&
			   vertices.payload.value(QStringLiteral("refused")).toArray().isEmpty() &&
			   vertices.payload.value(QStringLiteral("selection")).toObject().value(QStringLiteral("vertices")).toArray().size() == core.selection.vertices.size(),
		   "CLI --vertices seeds the edges between them, as the editor does");
	// --extend keeps the given selection, which is what the editor shows.
	request.operation = ModelSelectOperation::Loop;
	ModelSelectRequest extended = request;
	extended.extend = true;
	selectModelEdgeLoops(grid, {0, {}, {}, {rows[0], diagonal}}, extended, {}, &core);
	result = select(QStringLiteral("loop"), {QStringLiteral("--edges"), edges({rows[0], diagonal}), QStringLiteral("--extend")});
	expect(result.exitCode == 0 &&
			   result.payload.value(QStringLiteral("selection")).toObject().value(QStringLiteral("edges")).toArray() == sorted(core.selection.edges) &&
			   core.selection.edges.contains(diagonal),
		   "CLI --extend keeps a refused seed selected, as the editor does");
	result = select(QStringLiteral("loop"), {});
	expect(result.exitCode == 4 && result.error.contains(QStringLiteral("--edge")) && result.error.contains(QStringLiteral("--edges")),
		   "CLI loop without a seed names the options that give one");
	// --edge keeps its meaning: one seed, the selection only for --extend.
	result = select(QStringLiteral("loop"), {QStringLiteral("--edge"), edges({rows[0]}), QStringLiteral("--edges"), edges({rows[1]})});
	expect(result.exitCode == 0 && result.payload.value(QStringLiteral("selection")).toObject().value(QStringLiteral("edges")).toArray().size() == 4 &&
			   !result.payload.contains(QStringLiteral("seeds")),
		   "CLI --edge still seeds one loop");
	if (!executable.isEmpty())
	{
		QProcess process;
		process.start(executable, {QStringLiteral("--cli"), QStringLiteral("model"), QStringLiteral("select"), source, QStringLiteral("--select"),
								   QStringLiteral("loop"), QStringLiteral("--mode"), QStringLiteral("edges"), QStringLiteral("--edges"), edges(rows),
								   QStringLiteral("--json")});
		const bool finished = process.waitForFinished(60000);
		const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		expect(finished && process.exitCode() == 0 && json.value(QStringLiteral("selection")).toObject().value(QStringLiteral("edges")).toArray().size() == 8,
			   "the application dispatches multi-seed loop selection");
	}
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	checkGrid();
	checkCylinder();
	checkScale();
	checkCli(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
	if (failures == 0)
		std::cout << "model edge loops smoke test passed\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
