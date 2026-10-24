#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "core/model_rip.h"

#include <QComboBox>
#include <QCoreApplication>
#include <algorithm>
#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTools)
};
} // namespace

bool ModelEditorTools::initialiseRip(Modal &modal, QString *error)
{
	const auto &mesh = m_editor->m_document.mesh();
	const auto &selection = modal.selection;
	if (selection.surface < 0 || selection.surface >= mesh.surfaces.size() || !selection.faces.isEmpty() ||
		(selection.vertices.isEmpty() && selection.edges.isEmpty()))
	{
		if (error)
			*error = Text::tr("Select vertices or edges to rip. Separate faces to detach a face region.");
		return false;
	}
	const auto &surface = mesh.surfaces[selection.surface];
	ModelGeometricTopology geometry;
	if (!m_editor->performWork(
			Text::tr("Finding rip sides…"), [&](ModelDocument &, QString *failure, const ModelWorkControl &control)
			{ return buildModelGeometricTopology(surface, &geometry, failure, control); }, error))
		return false;
	QSet<int> incident;
	QSet<int> vertices = selection.vertices;
	for (auto edge : selection.edges)
		vertices << edge.first << edge.second;
	for (int vertex : vertices)
	{
		const int group = geometry.group(vertex);
		if (group >= 0)
			for (int face : geometry.groupFaces[group])
				incident.insert(face);
	}
	incident.subtract(m_hiddenFaces.value(selection.surface));
	int hitSurface = -1;
	const int hit = faceAt(modal.start, &hitSurface);
	if (hitSurface == selection.surface && incident.contains(hit))
		modal.ripFace = hit;
	const int frame = std::max(0, m_editor->m_frame->currentIndex());
	if (modal.ripFace < 0)
	{
		double nearest = std::numeric_limits<double>::max();
		auto faces = incident.values();
		std::sort(faces.begin(), faces.end());
		const auto &positions = surface.frames[frame].positions;
		for (int face : faces)
		{
			const auto t = surface.triangles[face];
			const auto a = positions[t.a], b = positions[t.b], c = positions[t.c];
			QPointF screen;
			if (!m_viewport->projectToView({(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3}, &screen))
				continue;
			const auto delta = screen - modal.start;
			const double distance = QPointF::dotProduct(delta, delta);
			if (distance < nearest)
			{
				nearest = distance;
				modal.ripFace = face;
			}
		}
	}
	modal.ripFill = modal.op == ModelModalOperator::RipFill;
	ModelMeshToolOptions options;
	options.referenceFrame = frame;
	options.ripFace = modal.ripFace;
	ModelRipPlan plan;
	return m_editor->performWork(
		Text::tr("Checking rip topology…"), [&](ModelDocument &, QString *failure, const ModelWorkControl &control)
		{ return planModelRip(surface, selection, options, &plan, failure, control); }, error);
}

void ModelEditorTools::setRipOptions(bool opposite, bool fill)
{
	if (!m_modal || m_editor->m_working || (m_modal->op != ModelModalOperator::Rip && m_modal->op != ModelModalOperator::RipFill))
		return;
	m_modal->ripOpposite = opposite;
	m_modal->ripFill = fill;
	updateModalPreview();
}
} // namespace vibestudio
