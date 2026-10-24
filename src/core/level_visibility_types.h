#pragma once

namespace vibestudio {
// Session authoring policy, never stored in native map/scene metadata.
// PreserveAndReveal copies explicit attributes and reveals inserted roots.
// FreshInherited creates inherited states, including for recursive copies.
enum class LevelInsertionVisibility { PreserveAndReveal, FreshInherited };
} // namespace vibestudio
