// Mirror panes: a viewport that shows what another viewport shows (mesh,
// frame, shading, skins, selection and overlays) through its own camera. The
// modeller's four-view layout keeps one interactive viewport and three
// mirrors, and swaps a mirror in when the user clicks it.
#include "app/model_viewport.h"

namespace vibestudio {

void ModelViewport::setViewLabel(const QString& label)
{
	if (label == m_viewLabel) {
		return;
	}
	m_viewLabel = label;
	update();
}

void ModelViewport::mirrorDisplayFrom(const ModelViewport& source)
{
	if (&source == this) {
		return;
	}
	// Mesh copies share their arrays until either side changes, so equal
	// buffers mean an unchanged mesh and the costly rebuild can be skipped.
	const bool meshChanged = m_hasMesh != source.m_hasMesh || m_mesh.surfaces.constData() != source.m_mesh.surfaces.constData()
		|| m_mesh.frames.constData() != source.m_mesh.frames.constData() || m_mesh.surfaces.size() != source.m_mesh.surfaces.size();
	const bool hadMesh = m_hasMesh;
	if (meshChanged) {
		m_mesh = source.m_mesh;
		m_hasMesh = source.m_hasMesh;
		m_frameTotal = source.m_frameTotal;
		m_editPivotCache = {};
		rebuildMeshTriangles();
	}
	m_skin = source.m_skin;
	m_skinHasAlpha = source.m_skinHasAlpha;
	m_surfaceSkins = source.m_surfaceSkins;
	m_surfaceSkinAlpha = source.m_surfaceSkinAlpha;
	m_renderMode = source.m_renderMode;
	m_backfaceCulling = source.m_backfaceCulling;
	m_showGrid = source.m_showGrid;
	m_showAxes = source.m_showAxes;
	m_showEdges = source.m_showEdges;
	m_highContrast = source.m_highContrast;
	m_reducedMotion = source.m_reducedMotion;
	m_frame = source.m_frame;
	m_rangeFirst = source.m_rangeFirst;
	m_rangeCount = source.m_rangeCount;
	m_animation = source.m_animation;
	m_animationIndex = source.m_animationIndex;
	m_fps = source.m_fps;
	m_interpolateAnimation = source.m_interpolateAnimation;
	m_frameBlend = source.m_frameBlend;
	m_blendFrame = source.m_blendFrame;
	m_nativeMdl = source.m_nativeMdl;
	m_mdlSkinVisible = source.m_mdlSkinVisible;
	m_mdlPlayback = source.m_mdlPlayback;
	m_mdlSample = source.m_mdlSample;
	m_mdlPlaybackSkins = source.m_mdlPlaybackSkins;
	m_highlighted = source.m_highlighted;
	m_highlightCount = source.m_highlightCount;
	m_edgeSelectionSurface = source.m_edgeSelectionSurface;
	m_selectedEdges = source.m_selectedEdges;
	m_editSurface = source.m_editSurface;
	m_editTag = source.m_editTag;
	m_showTags = source.m_showTags;
	m_editVertices = source.m_editVertices;
	m_editSurfaces = source.m_editSurfaces;
	m_xrayVertices = source.m_xrayVertices;
	m_vertexPicking = source.m_vertexPicking;
	m_edgePicking = source.m_edgePicking;
	m_tagPicking = source.m_tagPicking;
	m_showCollision = source.m_showCollision;
	m_collisionPicking = source.m_collisionPicking;
	m_editCollision = source.m_editCollision;
	rebuildFillBrushes();
	if (meshChanged && !hadMesh && m_hasMesh) {
		m_center = source.m_center;
		m_radius = source.m_radius;
		frameModel();
	}
	invalidateProjection();
	// Keep the last image until the new one is ready, so syncing never flickers.
	invalidateRaster(false);
	update();
}

} // namespace vibestudio
