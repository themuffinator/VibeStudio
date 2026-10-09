#pragma once

// The shader programs GPU frames draw with, and what each one reads.
//
// Sources live in src/core/shaders; scripts/build_render_shaders.py compiles
// them into render_shader_data.inc (SPIR-V for Vulkan, GLSL text for
// OpenGL). A program's vertex layout and resource bindings are fixed here,
// so frames only name the program. Every program reads one std140 uniform
// block ("Uniforms", Vulkan binding 0) and samplers tex0..texN-1 (Vulkan
// bindings 1..N), all in descriptor set 0.

#include <QtGlobal>

#include <array>

namespace vibestudio {

enum class GpuProgram : int {
	// World-space model and level triangles with flat shading, skins,
	// selection hatching, edges, triangle ids and depth peeling.
	ModelSurface,
	// Antialiased wireframe edges, one instance per segment.
	ModelWire,
	// Copies a same-sized target pixel for pixel, scaled and clamped.
	Composite,
	// Material previews (core/material_render*.cpp), one per engine family,
	// sharing material.vert and the MaterialUniforms block.
	MaterialQuake3,
	MaterialDoom,
	MaterialQuake,
	MaterialDoom3,
	// Material background and fog curve, over the whole image.
	MaterialScreen,
	Count,
};

enum class GpuVertexFormat : quint8 {
	Float1,
	Float2,
	Float3,
	Float4,
	UByte4Normalized,
	UInt1,
	Int1,
};

struct GpuVertexAttribute {
	int location = 0;
	GpuVertexFormat format = GpuVertexFormat::Float1;
	int offset = 0;
};

struct GpuVertexBinding {
	// Zero: the binding is unused.
	int stride = 0;
	bool perInstance = false;
	int attributeCount = 0;
	std::array<GpuVertexAttribute, 8> attributes {};
};

struct GpuProgramInfo {
	const char* name = "";
	std::array<GpuVertexBinding, 2> bindings {};
	int samplerCount = 0;
	// Size of the uniform block in bytes; frames pad shorter data with zeros.
	int uniformBytes = 0;
	// The OpenGL text without a #version line, and the SPIR-V words.
	const char* vertexGlsl = nullptr;
	const char* fragmentGlsl = nullptr;
	const quint32* vertexSpirv = nullptr;
	int vertexSpirvWords = 0;
	const quint32* fragmentSpirv = nullptr;
	int fragmentSpirvWords = 0;
};

int gpuProgramCount();
// Null for an unknown program.
const GpuProgramInfo* gpuProgramInfo(int program);
inline const GpuProgramInfo* gpuProgramInfo(GpuProgram program)
{
	return gpuProgramInfo(static_cast<int>(program));
}

// Bytes per vertex attribute format.
int gpuVertexFormatSize(GpuVertexFormat format);

} // namespace vibestudio
