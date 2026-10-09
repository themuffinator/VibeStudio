#include "core/render_shaders.h"

#include "core/render_shader_data.inc"

#include <initializer_list>

namespace vibestudio {

namespace {

template <std::size_t N>
constexpr int words(const quint32 (&)[N])
{
	return static_cast<int>(N);
}

GpuVertexBinding binding(int stride, bool perInstance, std::initializer_list<GpuVertexAttribute> attributes)
{
	GpuVertexBinding result;
	result.stride = stride;
	result.perInstance = perInstance;
	for (const GpuVertexAttribute& attribute : attributes) {
		result.attributes[static_cast<std::size_t>(result.attributeCount++)] = attribute;
	}
	return result;
}

const std::array<GpuProgramInfo, static_cast<std::size_t>(GpuProgram::Count)>& programs()
{
	using namespace render_shader_data;
	static const std::array<GpuProgramInfo, static_cast<std::size_t>(GpuProgram::Count)> table = [] {
		std::array<GpuProgramInfo, static_cast<std::size_t>(GpuProgram::Count)> result {};

		// Position, texture coordinates, face plane, surface colour, corner
		// info; then the per-corner selection flags in a second buffer, so a
		// selection change uploads four bytes a corner, not the geometry.
		GpuProgramInfo& surface = result[static_cast<std::size_t>(GpuProgram::ModelSurface)];
		surface.name = "model-surface";
		surface.bindings[0] = binding(44, false,
			{{0, GpuVertexFormat::Float3, 0}, {1, GpuVertexFormat::Float2, 12}, {2, GpuVertexFormat::Float4, 20},
				{3, GpuVertexFormat::UByte4Normalized, 36}, {4, GpuVertexFormat::UInt1, 40}});
		surface.bindings[1] = binding(4, false, {{5, GpuVertexFormat::UInt1, 0}});
		surface.samplerCount = 3;
		surface.uniformBytes = 208;
		surface.vertexGlsl = model_surface_vert_glsl;
		surface.fragmentGlsl = model_surface_frag_glsl;
		surface.vertexSpirv = model_surface_vert_spirv;
		surface.vertexSpirvWords = words(model_surface_vert_spirv);
		surface.fragmentSpirv = model_surface_frag_spirv;
		surface.fragmentSpirvWords = words(model_surface_frag_spirv);

		// Six vertices an instance; the segment comes from the instance.
		GpuProgramInfo& wire = result[static_cast<std::size_t>(GpuProgram::ModelWire)];
		wire.name = "model-wire";
		wire.bindings[1] = binding(28, true,
			{{0, GpuVertexFormat::Float3, 0}, {1, GpuVertexFormat::Float3, 12}, {2, GpuVertexFormat::UInt1, 24}});
		wire.uniformBytes = 128;
		wire.vertexGlsl = model_wire_vert_glsl;
		wire.fragmentGlsl = model_wire_frag_glsl;
		wire.vertexSpirv = model_wire_vert_spirv;
		wire.vertexSpirvWords = words(model_wire_vert_spirv);
		wire.fragmentSpirv = model_wire_frag_spirv;
		wire.fragmentSpirvWords = words(model_wire_frag_spirv);

		GpuProgramInfo& composite = result[static_cast<std::size_t>(GpuProgram::Composite)];
		composite.name = "composite";
		composite.samplerCount = 1;
		composite.uniformBytes = 32;
		composite.vertexGlsl = fullscreen_vert_glsl;
		composite.fragmentGlsl = composite_frag_glsl;
		composite.vertexSpirv = fullscreen_vert_spirv;
		composite.vertexSpirvWords = words(fullscreen_vert_spirv);
		composite.fragmentSpirv = composite_frag_spirv;
		composite.fragmentSpirvWords = words(composite_frag_spirv);

		// Material previews: position, normal, tangent, bitangent, stage and
		// lightmap coordinates, colour; eight samplers and one uniform block.
		const GpuVertexBinding materialVertices = binding(80, false,
			{{0, GpuVertexFormat::Float3, 0}, {1, GpuVertexFormat::Float3, 12}, {2, GpuVertexFormat::Float3, 24},
				{3, GpuVertexFormat::Float3, 36}, {4, GpuVertexFormat::Float4, 48}, {5, GpuVertexFormat::Float4, 64}});
		const auto material = [&](GpuProgram program, const char* name, const char* fragmentGlsl, const quint32* fragmentSpirv, int fragmentWords) {
			GpuProgramInfo& info = result[static_cast<std::size_t>(program)];
			info.name = name;
			info.bindings[0] = materialVertices;
			info.samplerCount = 8;
			info.uniformBytes = 752;
			info.vertexGlsl = material_vert_glsl;
			info.fragmentGlsl = fragmentGlsl;
			info.vertexSpirv = material_vert_spirv;
			info.vertexSpirvWords = words(material_vert_spirv);
			info.fragmentSpirv = fragmentSpirv;
			info.fragmentSpirvWords = fragmentWords;
		};
		material(GpuProgram::MaterialQuake3, "material-quake3", material_quake3_frag_glsl, material_quake3_frag_spirv, words(material_quake3_frag_spirv));
		material(GpuProgram::MaterialDoom, "material-doom", material_doom_frag_glsl, material_doom_frag_spirv, words(material_doom_frag_spirv));
		material(GpuProgram::MaterialQuake, "material-quake", material_quake_frag_glsl, material_quake_frag_spirv, words(material_quake_frag_spirv));
		material(GpuProgram::MaterialDoom3, "material-doom3", material_doom3_frag_glsl, material_doom3_frag_spirv, words(material_doom3_frag_spirv));

		GpuProgramInfo& screen = result[static_cast<std::size_t>(GpuProgram::MaterialScreen)];
		screen.name = "material-screen";
		screen.samplerCount = 8;
		screen.uniformBytes = 752;
		screen.vertexGlsl = fullscreen_vert_glsl;
		screen.fragmentGlsl = material_screen_frag_glsl;
		screen.vertexSpirv = fullscreen_vert_spirv;
		screen.vertexSpirvWords = words(fullscreen_vert_spirv);
		screen.fragmentSpirv = material_screen_frag_spirv;
		screen.fragmentSpirvWords = words(material_screen_frag_spirv);
		return result;
	}();
	return table;
}

} // namespace

int gpuProgramCount()
{
	return static_cast<int>(GpuProgram::Count);
}

const GpuProgramInfo* gpuProgramInfo(int program)
{
	if (program < 0 || program >= gpuProgramCount()) {
		return nullptr;
	}
	return &programs()[static_cast<std::size_t>(program)];
}

int gpuVertexFormatSize(GpuVertexFormat format)
{
	switch (format) {
	case GpuVertexFormat::Float1:
	case GpuVertexFormat::UByte4Normalized:
	case GpuVertexFormat::UInt1:
	case GpuVertexFormat::Int1:
		return 4;
	case GpuVertexFormat::Float2:
		return 8;
	case GpuVertexFormat::Float3:
		return 12;
	case GpuVertexFormat::Float4:
		return 16;
	}
	return 4;
}

} // namespace vibestudio
