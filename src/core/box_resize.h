#pragma once

#include <array>

namespace vibestudio {

using BoxResizePoint = std::array<double,3>;
struct ResizeBox {
	BoxResizePoint mins{}, maxs{};
	bool operator==(const ResizeBox&) const = default;
};
struct BoxResizeRay {
	BoxResizePoint origin{}, direction{};
	bool forwardOnly = false;
};
struct BoxResizeDrag {
	ResizeBox source;
	BoxResizePoint anchor{}, normal{};
	double pressCoordinate = 0, grid = 0;
	int handle = -1;
};

// Six face centres: X min/max, Y min/max, Z min/max. Collapsed axes have no
// handle. Bounds and unchanged coordinates retain double source precision.
bool validResizeBox(const ResizeBox& box);
bool boxResizeHandle(const ResizeBox& box, int handle, BoxResizePoint* point);
// A camera-facing plane contains the selected world axis. Degenerate/grazing
// or backward rays fail without changing output. Grid zero disables snapping.
bool beginBoxResize(const ResizeBox& box, int handle, BoxResizePoint viewDirection,
	BoxResizeRay ray, double grid, BoxResizeDrag* drag);
bool updateBoxResize(const BoxResizeDrag& drag, BoxResizeRay ray, ResizeBox* box);
// Affine geometry preview; point-owned visuals use the transformed origin's
// translation instead, preserving the size of entity models and thing markers.
BoxResizePoint resizeBoxPoint(const ResizeBox& from, const ResizeBox& to, BoxResizePoint point);

} // namespace vibestudio
