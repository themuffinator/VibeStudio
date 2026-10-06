#include "tests/model_uv_render_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests::uvRender;
namespace
{
bool benchmark(const QString &name, ModelSurface surface, int ratio, bool selected)
{
	ModelUvRenderRequest request;
	request.surface = surface;
	request.logicalSize = {1024, 768};
	request.pixelRatio = ratio;
	request.background = QColor(35, 40, 50);
	request.foreground = Qt::white;
	request.accent = QColor(255, 170, 60);
	if (selected)
		for (int i = 0; i < surface.triangles.size(); ++i)
			request.selection.faces.insert(i);
	QElapsedTimer timer;
	timer.start();
	auto topology = std::make_shared<ModelUvTopology>();
	QString error;
	if (!buildModelUvTopology(surface, topology.get(), &error))
		return false;
	const double topologyMs = timer.nsecsElapsed() / 1e6;
	request.topology = topology;
	timer.restart();
	ModelUvRenderResult result;
	const bool success = renderModelUv(request, &result, &error);
	const double renderMs = timer.nsecsElapsed() / 1e6;
	std::cout << QJsonDocument(QJsonObject{{"fixture", name},
										   {"ratio", ratio},
										   {"selected", selected},
										   {"vertices", surface.vertexCount},
										   {"faces", surface.triangles.size()},
										   {"edges", topology->edges.size()},
										   {"topologyMs", topologyMs},
										   {"renderMs", renderMs},
										   {"success", success},
										   {"error", error}})
					 .toJson(QJsonDocument::Compact)
					 .constData()
			  << std::endl;
	const int budget = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_UV_RENDER_MS");
	return expect(success && result.topology->edges.size() == (name == QStringLiteral("grid") ? 195585 : 131069) &&
					  result.image.size() == QSize(1024 * ratio, 768 * ratio),
				  "maximum workload preserves complete topology and requested physical size") &&
		   expect(budget <= 0 || renderMs <= budget, "maximum UV rendering stays within the explicit throughput budget") &&
		   save(result.image, QStringLiteral("uv-render-%1-%2x-%3").arg(name).arg(ratio).arg(selected ? "selected" : "ordinary"));
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	ok &= contourChecks();
	ok &= renderChecks();
	ok &= tileChecks();
	for (int ratio : {1, 2})
		for (bool selected : {false, true})
		{
			ok &= benchmark(QStringLiteral("grid"), grid(256, 256), ratio, selected);
			ok &= benchmark(QStringLiteral("strip"), grid(32768, 2), ratio, selected);
		}
	std::cout << "UV rendering checks: " << checks << '\n';
	return ok ? 0 : 1;
}
