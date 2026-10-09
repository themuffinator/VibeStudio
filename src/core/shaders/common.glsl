// Shared declarations for every VibeStudio shader.
//
// scripts/build_render_shaders.py compiles each stage twice from one source:
// for Vulkan with "#version 450" and VIBE_VULKAN defined, to SPIR-V; and for
// OpenGL as text that the backend prefixes with "#version 330 core" or
// "#version 300 es" at run time. These macros cover the differences.
//
// Clip space is canonical in both: x right, y down (y = -1 is the image's top
// row), z from 0 to 1. VIBE_CLIP() converts z for OpenGL's -1..1 range.

#ifdef VIBE_VULKAN
#define VIBE_UNIFORMS layout(std140, set = 0, binding = 0) uniform Uniforms
#define VIBE_SAMPLER(n) layout(set = 0, binding = 1 + n) uniform sampler2D
#define VIBE_ATTRIBUTE(n) layout(location = n) in
#define VIBE_VARYING_OUT(n) layout(location = n) out
#define VIBE_VARYING_IN(n) layout(location = n) in
#define VIBE_FLAT_OUT(n) layout(location = n) flat out
#define VIBE_FLAT_IN(n) layout(location = n) flat in
#define VIBE_FRAGMENT_OUT(n) layout(location = n) out
#define VIBE_VERTEX_INDEX gl_VertexIndex
#define VIBE_INSTANCE_INDEX gl_InstanceIndex
#define VIBE_CLIP(p) (p)
#else
#define VIBE_UNIFORMS layout(std140) uniform Uniforms
#define VIBE_SAMPLER(n) uniform sampler2D
#define VIBE_ATTRIBUTE(n) layout(location = n) in
#define VIBE_VARYING_OUT(n) out
#define VIBE_VARYING_IN(n) in
#define VIBE_FLAT_OUT(n) flat out
#define VIBE_FLAT_IN(n) flat in
#define VIBE_FRAGMENT_OUT(n) layout(location = n) out
#define VIBE_VERTEX_INDEX gl_VertexID
#define VIBE_INSTANCE_INDEX gl_InstanceID
#define VIBE_CLIP(p) vec4((p).x, (p).y, (p).z * 2.0 - (p).w, (p).w)
#endif

// A clip position outside every clip plane: the triangle or quad drawn with
// it on all corners produces no fragments.
#define VIBE_DISCARDED_POSITION vec4(2.0, 2.0, 2.0, 1.0)
