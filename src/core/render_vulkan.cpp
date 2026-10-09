// The Vulkan backend: Vulkan 1.0, loaded at run time, rendering offscreen.
//
// The loader is opened with QLibrary (vulkan-1.dll, libvulkan.so.1, the
// macOS loader or MoltenVK) and every entry point comes through
// vkGetInstanceProcAddr / vkGetDeviceProcAddr, so nothing links against
// Vulkan and no window system is needed: the CLI, tests on Qt's offscreen
// platform and the studio all render the same way. Headers are Khronos'
// Vulkan-Headers, pinned in external/graphics/vulkan-headers.
//
// Each frame records one command buffer: uploads, one render pass per
// GpuPass with explicit layout barriers between them, then copies of the
// read-back targets into a host buffer, and waits for its fence. Canonical
// clip space (y down, depth 0..1) is Vulkan's own, so nothing is flipped.
//
// Set VIBESTUDIO_VULKAN_VALIDATION=1 to enable the Khronos validation layer
// when it is installed, and VIBESTUDIO_VULKAN_DEVICE to an index or part of
// a device name to choose a device.

#include "core/render_device_p.h"
#include "core/render_shaders.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include <QCoreApplication>
#include <QLibrary>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace vibestudio::render_detail {

namespace {

constexpr qint64 kCacheBudgetBytes = qint64(512) * 1024 * 1024;
constexpr int kTargetPoolLimit = 32;
constexpr quint64 kTargetIdleFrames = 240;
constexpr VkDeviceSize kArenaAlignment = 256;

// A Vulkan structure with every member zeroed and its type set. Aggregate
// initialisation with only sType leaves GCC warning about every other member.
template<typename T>
T vulkanStruct(VkStructureType type)
{
	T value {};
	value.sType = type;
	return value;
}

#define VIBE_VK_GLOBAL(X)                       \
	X(vkCreateInstance)                         \
	X(vkEnumerateInstanceExtensionProperties)   \
	X(vkEnumerateInstanceLayerProperties)

#define VIBE_VK_INSTANCE(X)                     \
	X(vkDestroyInstance)                        \
	X(vkEnumeratePhysicalDevices)               \
	X(vkGetPhysicalDeviceProperties)            \
	X(vkGetPhysicalDeviceFeatures)              \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceMemoryProperties)      \
	X(vkGetPhysicalDeviceFormatProperties)      \
	X(vkEnumerateDeviceExtensionProperties)     \
	X(vkCreateDevice)                           \
	X(vkGetDeviceProcAddr)

#define VIBE_VK_DEVICE(X)            \
	X(vkDestroyDevice)               \
	X(vkGetDeviceQueue)              \
	X(vkDeviceWaitIdle)              \
	X(vkQueueSubmit)                 \
	X(vkCreateCommandPool)           \
	X(vkDestroyCommandPool)          \
	X(vkAllocateCommandBuffers)      \
	X(vkResetCommandBuffer)          \
	X(vkBeginCommandBuffer)          \
	X(vkEndCommandBuffer)            \
	X(vkCreateFence)                 \
	X(vkDestroyFence)                \
	X(vkWaitForFences)               \
	X(vkResetFences)                 \
	X(vkCreateBuffer)                \
	X(vkDestroyBuffer)               \
	X(vkGetBufferMemoryRequirements) \
	X(vkBindBufferMemory)            \
	X(vkCreateImage)                 \
	X(vkDestroyImage)                \
	X(vkGetImageMemoryRequirements)  \
	X(vkBindImageMemory)             \
	X(vkCreateImageView)             \
	X(vkDestroyImageView)            \
	X(vkAllocateMemory)              \
	X(vkFreeMemory)                  \
	X(vkMapMemory)                   \
	X(vkUnmapMemory)                 \
	X(vkInvalidateMappedMemoryRanges) \
	X(vkCreateSampler)               \
	X(vkDestroySampler)              \
	X(vkCreateRenderPass)            \
	X(vkDestroyRenderPass)           \
	X(vkCreateFramebuffer)           \
	X(vkDestroyFramebuffer)          \
	X(vkCreateShaderModule)          \
	X(vkDestroyShaderModule)         \
	X(vkCreateDescriptorSetLayout)   \
	X(vkDestroyDescriptorSetLayout)  \
	X(vkCreatePipelineLayout)        \
	X(vkDestroyPipelineLayout)       \
	X(vkCreateGraphicsPipelines)     \
	X(vkDestroyPipeline)             \
	X(vkCreateDescriptorPool)        \
	X(vkDestroyDescriptorPool)       \
	X(vkResetDescriptorPool)         \
	X(vkAllocateDescriptorSets)      \
	X(vkUpdateDescriptorSets)        \
	X(vkCmdBeginRenderPass)          \
	X(vkCmdEndRenderPass)            \
	X(vkCmdBindPipeline)             \
	X(vkCmdBindDescriptorSets)       \
	X(vkCmdBindVertexBuffers)        \
	X(vkCmdBindIndexBuffer)          \
	X(vkCmdDraw)                     \
	X(vkCmdDrawIndexed)              \
	X(vkCmdSetViewport)              \
	X(vkCmdSetScissor)               \
	X(vkCmdPipelineBarrier)          \
	X(vkCmdCopyBuffer)               \
	X(vkCmdCopyBufferToImage)        \
	X(vkCmdCopyImageToBuffer)

QString vulkanResult(VkResult result)
{
	switch (result) {
	case VK_SUCCESS:
		return QStringLiteral("VK_SUCCESS");
	case VK_ERROR_OUT_OF_HOST_MEMORY:
		return QStringLiteral("VK_ERROR_OUT_OF_HOST_MEMORY");
	case VK_ERROR_OUT_OF_DEVICE_MEMORY:
		return QStringLiteral("VK_ERROR_OUT_OF_DEVICE_MEMORY");
	case VK_ERROR_INITIALIZATION_FAILED:
		return QStringLiteral("VK_ERROR_INITIALIZATION_FAILED");
	case VK_ERROR_DEVICE_LOST:
		return QStringLiteral("VK_ERROR_DEVICE_LOST");
	case VK_ERROR_MEMORY_MAP_FAILED:
		return QStringLiteral("VK_ERROR_MEMORY_MAP_FAILED");
	case VK_ERROR_LAYER_NOT_PRESENT:
		return QStringLiteral("VK_ERROR_LAYER_NOT_PRESENT");
	case VK_ERROR_EXTENSION_NOT_PRESENT:
		return QStringLiteral("VK_ERROR_EXTENSION_NOT_PRESENT");
	case VK_ERROR_FEATURE_NOT_PRESENT:
		return QStringLiteral("VK_ERROR_FEATURE_NOT_PRESENT");
	case VK_ERROR_INCOMPATIBLE_DRIVER:
		return QStringLiteral("VK_ERROR_INCOMPATIBLE_DRIVER");
	case VK_ERROR_TOO_MANY_OBJECTS:
		return QStringLiteral("VK_ERROR_TOO_MANY_OBJECTS");
	case VK_ERROR_FORMAT_NOT_SUPPORTED:
		return QStringLiteral("VK_ERROR_FORMAT_NOT_SUPPORTED");
	case VK_ERROR_FRAGMENTED_POOL:
		return QStringLiteral("VK_ERROR_FRAGMENTED_POOL");
	case VK_ERROR_OUT_OF_POOL_MEMORY:
		return QStringLiteral("VK_ERROR_OUT_OF_POOL_MEMORY");
	default:
		return QStringLiteral("VkResult %1").arg(int(result));
	}
}

QString vendorName(quint32 vendor)
{
	switch (vendor) {
	case 0x1002:
		return QStringLiteral("AMD");
	case 0x106B:
		return QStringLiteral("Apple");
	case 0x10DE:
		return QStringLiteral("NVIDIA");
	case 0x13B5:
		return QStringLiteral("ARM");
	case 0x5143:
		return QStringLiteral("Qualcomm");
	case 0x8086:
		return QStringLiteral("Intel");
	case 0x10005:
		return QStringLiteral("Mesa");
	default:
		return QStringLiteral("0x%1").arg(vendor, 4, 16, QLatin1Char('0'));
	}
}

QString driverVersionText(const VkPhysicalDeviceProperties& properties)
{
	const quint32 version = properties.driverVersion;
	if (properties.vendorID == 0x10DE) {
		return QStringLiteral("%1.%2.%3.%4").arg(version >> 22).arg((version >> 14) & 0xff).arg((version >> 6) & 0xff).arg(version & 0x3f);
	}
#ifdef Q_OS_WIN
	if (properties.vendorID == 0x8086) {
		return QStringLiteral("%1.%2").arg(version >> 14).arg(version & 0x3fff);
	}
#endif
	return QStringLiteral("%1.%2.%3").arg(VK_API_VERSION_MAJOR(version)).arg(VK_API_VERSION_MINOR(version)).arg(VK_API_VERSION_PATCH(version));
}

QString deviceTypeId(VkPhysicalDeviceType type)
{
	switch (type) {
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
		return QStringLiteral("discrete-gpu");
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
		return QStringLiteral("integrated-gpu");
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
		return QStringLiteral("virtual-gpu");
	case VK_PHYSICAL_DEVICE_TYPE_CPU:
		return QStringLiteral("cpu");
	default:
		return QStringLiteral("other");
	}
}

int deviceTypeScore(VkPhysicalDeviceType type)
{
	switch (type) {
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
		return 4;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
		return 3;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
		return 2;
	case VK_PHYSICAL_DEVICE_TYPE_CPU:
		return 1;
	default:
		return 0;
	}
}

VkBlendFactor blendFactor(GpuBlend factor)
{
	switch (factor) {
	case GpuBlend::Zero:
		return VK_BLEND_FACTOR_ZERO;
	case GpuBlend::One:
		return VK_BLEND_FACTOR_ONE;
	case GpuBlend::SourceColor:
		return VK_BLEND_FACTOR_SRC_COLOR;
	case GpuBlend::OneMinusSourceColor:
		return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case GpuBlend::DestinationColor:
		return VK_BLEND_FACTOR_DST_COLOR;
	case GpuBlend::OneMinusDestinationColor:
		return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case GpuBlend::SourceAlpha:
		return VK_BLEND_FACTOR_SRC_ALPHA;
	case GpuBlend::OneMinusSourceAlpha:
		return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GpuBlend::DestinationAlpha:
		return VK_BLEND_FACTOR_DST_ALPHA;
	case GpuBlend::OneMinusDestinationAlpha:
		return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case GpuBlend::SourceAlphaSaturate:
		return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	}
	return VK_BLEND_FACTOR_ONE;
}

VkCompareOp compareOp(GpuCompare compare)
{
	switch (compare) {
	case GpuCompare::Never:
		return VK_COMPARE_OP_NEVER;
	case GpuCompare::Less:
		return VK_COMPARE_OP_LESS;
	case GpuCompare::Equal:
		return VK_COMPARE_OP_EQUAL;
	case GpuCompare::LessOrEqual:
		return VK_COMPARE_OP_LESS_OR_EQUAL;
	case GpuCompare::Greater:
		return VK_COMPARE_OP_GREATER;
	case GpuCompare::NotEqual:
		return VK_COMPARE_OP_NOT_EQUAL;
	case GpuCompare::GreaterOrEqual:
		return VK_COMPARE_OP_GREATER_OR_EQUAL;
	case GpuCompare::Always:
		return VK_COMPARE_OP_ALWAYS;
	}
	return VK_COMPARE_OP_LESS;
}

VkFormat vertexFormat(GpuVertexFormat format)
{
	switch (format) {
	case GpuVertexFormat::Float1:
		return VK_FORMAT_R32_SFLOAT;
	case GpuVertexFormat::Float2:
		return VK_FORMAT_R32G32_SFLOAT;
	case GpuVertexFormat::Float3:
		return VK_FORMAT_R32G32B32_SFLOAT;
	case GpuVertexFormat::Float4:
		return VK_FORMAT_R32G32B32A32_SFLOAT;
	case GpuVertexFormat::UByte4Normalized:
		return VK_FORMAT_R8G8B8A8_UNORM;
	case GpuVertexFormat::UInt1:
		return VK_FORMAT_R32_UINT;
	case GpuVertexFormat::Int1:
		return VK_FORMAT_R32_SINT;
	}
	return VK_FORMAT_R32_SFLOAT;
}

bool integerFormat(VkFormat format)
{
	return format == VK_FORMAT_R32_SINT || format == VK_FORMAT_R32_UINT;
}

bool depthFormat(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_X8_D24_UNORM_PACK32
		|| format == VK_FORMAT_D24_UNORM_S8_UINT;
}

// Formats with a stencil part need both aspects in views and barriers.
VkImageAspectFlags aspectOf(VkFormat format)
{
	if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	return depthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

struct Memory {
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
	void* mapped = nullptr;
	bool coherent = true;
};

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	Memory memory;
	VkDeviceSize capacity = 0;
};

struct Image {
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	Memory memory;
	VkFormat format = VK_FORMAT_UNDEFINED;
	QSize size;
	int levels = 1;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Target {
	Image image;
	GpuFormat format = GpuFormat::Rgba8;
	quint64 lastUsed = 0;
	bool inUse = false;
};

struct CachedTexture {
	Image image;
	qint64 bytes = 0;
	quint64 lastUsed = 0;
};

struct CachedBuffer {
	Buffer buffer;
	qint64 bytes = 0;
	quint64 lastUsed = 0;
};

struct ProgramObjects {
	VkShaderModule vertex = VK_NULL_HANDLE;
	VkShaderModule fragment = VK_NULL_HANDLE;
	VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	bool attempted = false;
	QString error;
};

// What a render pass is: formats by slot (UNDEFINED for unused slots) and
// load operations. Pipelines only care about the formats.
struct PassSignature {
	std::array<VkFormat, kGpuMaxColorTargets> colors {};
	int colorCount = 0;
	VkFormat depth = VK_FORMAT_UNDEFINED;
	std::array<GpuLoad, kGpuMaxColorTargets> loads {};
	GpuLoad depthLoad = GpuLoad::Clear;

	[[nodiscard]] QByteArray formatKey() const
	{
		QByteArray key;
		key.append(char(colorCount));
		for (int i = 0; i < colorCount; ++i) {
			key.append(reinterpret_cast<const char*>(&colors[size_t(i)]), sizeof(VkFormat));
		}
		key.append(reinterpret_cast<const char*>(&depth), sizeof(VkFormat));
		return key;
	}
	[[nodiscard]] QByteArray key() const
	{
		QByteArray result = formatKey();
		for (int i = 0; i < colorCount; ++i) {
			result.append(char(loads[size_t(i)]));
		}
		result.append(char(depthLoad));
		return result;
	}
};

class VulkanDevice final : public DeviceThread {
public:
	VulkanDevice()
		: DeviceThread(RenderBackend::Vulkan)
	{
	}
	~VulkanDevice() override { stop(); }

protected:
	bool startDevice(RenderDeviceInfo* info) override;
	void stopDevice() override;
	GpuFrameResult renderFrame(const GpuFrame& frame, const std::atomic_bool* cancelled) override;
	void releaseOwnerOnDevice(quint64 owner) override;
	[[nodiscard]] bool formatSupportedOnDevice(GpuFormat format) const override;

private:
#define VIBE_VK_MEMBER(name) PFN_##name name = nullptr;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
	VIBE_VK_GLOBAL(VIBE_VK_MEMBER)
	VIBE_VK_INSTANCE(VIBE_VK_MEMBER)
	VIBE_VK_DEVICE(VIBE_VK_MEMBER)
#undef VIBE_VK_MEMBER
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;

	bool loadLibrary(RenderDeviceInfo* info);
	bool selectDevice(RenderDeviceInfo* info);
	VkFormat targetFormat(GpuFormat format) const;
	int memoryType(quint32 typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const;
	VkResult allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, Memory* memory);
	VkResult createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, Buffer* buffer);
	void destroyBuffer(Buffer* buffer);
	VkResult createImage(VkFormat format, QSize size, int levels, VkImageUsageFlags usage, Image* image);
	void destroyImage(Image* image);
	bool ensureProgram(int index, QString* error);
	VkRenderPass renderPass(const PassSignature& signature, VkResult* result);
	VkPipeline pipeline(int program, const GpuState& state, const PassSignature& signature, VkRenderPass pass, VkResult* result);
	VkFramebuffer framebuffer(VkRenderPass pass, const std::vector<VkImageView>& views, QSize size, VkResult* result);
	VkSampler sampler(const GpuSampler& state, bool filterable, VkResult* result);
	Target* acquireTarget(GpuFormat format, QSize size, VkResult* result);
	void releaseTargets();
	void forgetFramebuffers(VkImageView view);
	void evictCaches();
	bool ensureArena(VkDeviceSize size, VkResult* result);
	bool ensureReadback(VkDeviceSize size, VkResult* result);
	bool ensureDescriptorPool(int draws, VkResult* result);
	void transition(VkCommandBuffer commands, Image& image, VkImageLayout layout, VkImageAspectFlags aspect);
	bool submitAndWait(VkResult* result);

	QLibrary m_library;
	VkInstance m_instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
	VkPhysicalDevice m_physical = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties m_properties {};
	VkPhysicalDeviceMemoryProperties m_memory {};
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_queue = VK_NULL_HANDLE;
	quint32 m_queueFamily = 0;
	bool m_independentBlend = false;
	VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
	bool m_rgba16 = false;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	VkCommandBuffer m_commands = VK_NULL_HANDLE;
	VkFence m_fence = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	int m_descriptorCapacity = 0;
	Buffer m_arena;
	Buffer m_readback;
	Image m_white;
	std::vector<ProgramObjects> m_programs;
	std::map<QByteArray, VkRenderPass> m_renderPasses;
	std::map<QByteArray, VkPipeline> m_pipelines;
	struct FramebufferEntry {
		VkRenderPass pass = VK_NULL_HANDLE;
		std::vector<VkImageView> views;
		QSize size;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
	};
	std::vector<FramebufferEntry> m_framebuffers;
	std::map<quint32, VkSampler> m_samplers;
	std::vector<std::unique_ptr<Target>> m_targets;
	std::map<std::pair<quint64, quint64>, CachedTexture> m_textures;
	std::map<std::pair<quint64, quint64>, CachedBuffer> m_buffers;
	quint64 m_frame = 0;
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
	const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
	if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data && data->pMessage) {
		std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
		std::fflush(stderr);
	}
	return VK_FALSE;
}

bool VulkanDevice::loadLibrary(RenderDeviceInfo* info)
{
	QStringList names;
#if defined(Q_OS_WIN)
	names << QStringLiteral("vulkan-1");
#elif defined(Q_OS_MACOS)
	names << QStringLiteral("libvulkan.1.dylib") << QStringLiteral("libvulkan.dylib") << QStringLiteral("libMoltenVK.dylib")
		  << QStringLiteral("/usr/local/lib/libvulkan.1.dylib") << QStringLiteral("/opt/homebrew/lib/libvulkan.1.dylib");
#else
	names << QStringLiteral("libvulkan.so.1") << QStringLiteral("libvulkan.so");
#endif
	QStringList failures;
	for (const QString& name : std::as_const(names)) {
		m_library.setFileName(name);
		if (m_library.load()) {
			vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(m_library.resolve("vkGetInstanceProcAddr"));
			if (vkGetInstanceProcAddr) {
				return true;
			}
			m_library.unload();
		}
		failures << m_library.errorString();
	}
	info->error = QCoreApplication::translate("VibeStudioRendering", "No Vulkan driver is installed. Install your graphics driver's Vulkan support, or choose OpenGL.");
	info->errorDetail = failures.join(QStringLiteral("; "));
	return false;
}

int VulkanDevice::memoryType(quint32 typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const
{
	int fallback = -1;
	for (quint32 index = 0; index < m_memory.memoryTypeCount; ++index) {
		if ((typeBits & (1u << index)) == 0) {
			continue;
		}
		const VkMemoryPropertyFlags flags = m_memory.memoryTypes[index].propertyFlags;
		if ((flags & required) != required) {
			continue;
		}
		if ((flags & preferred) == preferred) {
			return int(index);
		}
		if (fallback < 0) {
			fallback = int(index);
		}
	}
	return fallback;
}

VkResult VulkanDevice::allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, Memory* memory)
{
	const int type = memoryType(requirements.memoryTypeBits, required, preferred);
	if (type < 0) {
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	VkMemoryAllocateInfo allocation = vulkanStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = quint32(type);
	const VkResult result = vkAllocateMemory(m_device, &allocation, nullptr, &memory->memory);
	if (result != VK_SUCCESS) {
		return result;
	}
	memory->size = requirements.size;
	const VkMemoryPropertyFlags flags = m_memory.memoryTypes[type].propertyFlags;
	memory->coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
	if ((required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
		const VkResult mapped = vkMapMemory(m_device, memory->memory, 0, VK_WHOLE_SIZE, 0, &memory->mapped);
		if (mapped != VK_SUCCESS) {
			vkFreeMemory(m_device, memory->memory, nullptr);
			memory->memory = VK_NULL_HANDLE;
			return mapped;
		}
	}
	return VK_SUCCESS;
}

VkResult VulkanDevice::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, Buffer* buffer)
{
	VkBufferCreateInfo create = vulkanStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
	create.size = std::max<VkDeviceSize>(size, 16);
	create.usage = usage;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(m_device, &create, nullptr, &buffer->buffer);
	if (result != VK_SUCCESS) {
		return result;
	}
	VkMemoryRequirements requirements {};
	vkGetBufferMemoryRequirements(m_device, buffer->buffer, &requirements);
	result = hostVisible
		? allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			  usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer->memory)
		: allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &buffer->memory);
	if (result == VK_SUCCESS) {
		result = vkBindBufferMemory(m_device, buffer->buffer, buffer->memory.memory, 0);
	}
	if (result != VK_SUCCESS) {
		destroyBuffer(buffer);
		return result;
	}
	buffer->capacity = create.size;
	return VK_SUCCESS;
}

void VulkanDevice::destroyBuffer(Buffer* buffer)
{
	if (buffer->buffer) {
		vkDestroyBuffer(m_device, buffer->buffer, nullptr);
	}
	if (buffer->memory.memory) {
		vkFreeMemory(m_device, buffer->memory.memory, nullptr);
	}
	*buffer = {};
}

VkResult VulkanDevice::createImage(VkFormat format, QSize size, int levels, VkImageUsageFlags usage, Image* image)
{
	VkImageCreateInfo create = vulkanStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = format;
	create.extent = {quint32(size.width()), quint32(size.height()), 1};
	create.mipLevels = quint32(levels);
	create.arrayLayers = 1;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = VK_IMAGE_TILING_OPTIMAL;
	create.usage = usage;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult result = vkCreateImage(m_device, &create, nullptr, &image->image);
	if (result != VK_SUCCESS) {
		return result;
	}
	VkMemoryRequirements requirements {};
	vkGetImageMemoryRequirements(m_device, image->image, &requirements);
	result = allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &image->memory);
	if (result == VK_SUCCESS) {
		result = vkBindImageMemory(m_device, image->image, image->memory.memory, 0);
	}
	if (result == VK_SUCCESS) {
		VkImageViewCreateInfo view = vulkanStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
		view.image = image->image;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = format;
		view.subresourceRange = {aspectOf(format), 0, quint32(levels), 0, 1};
		result = vkCreateImageView(m_device, &view, nullptr, &image->view);
	}
	if (result != VK_SUCCESS) {
		destroyImage(image);
		return result;
	}
	image->format = format;
	image->size = size;
	image->levels = levels;
	image->layout = VK_IMAGE_LAYOUT_UNDEFINED;
	return VK_SUCCESS;
}

void VulkanDevice::destroyImage(Image* image)
{
	if (image->view) {
		forgetFramebuffers(image->view);
		vkDestroyImageView(m_device, image->view, nullptr);
	}
	if (image->image) {
		vkDestroyImage(m_device, image->image, nullptr);
	}
	if (image->memory.memory) {
		vkFreeMemory(m_device, image->memory.memory, nullptr);
	}
	*image = {};
}

bool VulkanDevice::selectDevice(RenderDeviceInfo* info)
{
	quint32 count = 0;
	vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
	std::vector<VkPhysicalDevice> devices(count);
	if (count) {
		vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
	}
	const QString wanted = qEnvironmentVariable("VIBESTUDIO_VULKAN_DEVICE").trimmed();
	bool wantedIsIndex = false;
	const int wantedIndex = wanted.toInt(&wantedIsIndex);
	int bestScore = -1;
	QStringList rejected;
	for (quint32 index = 0; index < count; ++index) {
		VkPhysicalDevice candidate = devices[index];
		VkPhysicalDeviceProperties properties {};
		vkGetPhysicalDeviceProperties(candidate, &properties);
		const QString name = QString::fromUtf8(properties.deviceName);
		quint32 families = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
		std::vector<VkQueueFamilyProperties> familyProperties(families);
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, familyProperties.data());
		int graphics = -1;
		for (quint32 family = 0; family < families; ++family) {
			if ((familyProperties[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
				graphics = int(family);
				break;
			}
		}
		const auto supports = [&](VkFormat format, VkFormatFeatureFlags features) {
			VkFormatProperties formatProperties {};
			vkGetPhysicalDeviceFormatProperties(candidate, format, &formatProperties);
			return (formatProperties.optimalTilingFeatures & features) == features;
		};
		const bool formats = supports(VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
			&& supports(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
			&& supports(VK_FORMAT_R32_SINT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
			&& supports(VK_FORMAT_R32_SFLOAT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
		if (graphics < 0 || !formats || properties.apiVersion < VK_API_VERSION_1_0) {
			rejected << name;
			continue;
		}
		int score = deviceTypeScore(properties.deviceType);
		if (!wanted.isEmpty() && ((wantedIsIndex && int(index) == wantedIndex) || (!wantedIsIndex && name.contains(wanted, Qt::CaseInsensitive)))) {
			score += 100;
		}
		if (score > bestScore) {
			bestScore = score;
			m_physical = candidate;
			m_properties = properties;
			m_queueFamily = quint32(graphics);
		}
	}
	if (!m_physical) {
		info->error = count == 0
			? QCoreApplication::translate("VibeStudioRendering", "Vulkan is installed, but no graphics device offers it.")
			: QCoreApplication::translate("VibeStudioRendering", "No Vulkan device here supports what VibeStudio's 3D views need.");
		info->errorDetail = rejected.isEmpty() ? QStringLiteral("no physical devices") : QStringLiteral("rejected: %1").arg(rejected.join(QStringLiteral(", ")));
		return false;
	}
	vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memory);
	const auto supports = [&](VkFormat format, VkFormatFeatureFlags features) {
		VkFormatProperties formatProperties {};
		vkGetPhysicalDeviceFormatProperties(m_physical, format, &formatProperties);
		return (formatProperties.optimalTilingFeatures & features) == features;
	};
	for (VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT}) {
		if (supports(format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
			m_depthFormat = format;
			break;
		}
	}
	m_rgba16 = supports(VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
	VkPhysicalDeviceFeatures features {};
	vkGetPhysicalDeviceFeatures(m_physical, &features);
	m_independentBlend = features.independentBlend == VK_TRUE;
	info->deviceName = QString::fromUtf8(m_properties.deviceName);
	info->vendor = vendorName(m_properties.vendorID);
	info->driverVersion = driverVersionText(m_properties);
	info->apiVersion = QStringLiteral("%1.%2.%3")
						   .arg(VK_API_VERSION_MAJOR(m_properties.apiVersion))
						   .arg(VK_API_VERSION_MINOR(m_properties.apiVersion))
						   .arg(VK_API_VERSION_PATCH(m_properties.apiVersion));
	info->deviceType = deviceTypeId(m_properties.deviceType);
	const QString lower = info->deviceName.toLower();
	info->softwareImplementation = m_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU || lower.contains(QLatin1String("llvmpipe"))
		|| lower.contains(QLatin1String("lavapipe")) || lower.contains(QLatin1String("swiftshader"));
	info->maxTextureSize = int(m_properties.limits.maxImageDimension2D);
	if (m_depthFormat == VK_FORMAT_UNDEFINED) {
		info->error = QCoreApplication::translate("VibeStudioRendering", "This Vulkan device has no depth buffer format VibeStudio can use.");
		info->errorDetail = info->deviceName;
		return false;
	}
	return true;
}

bool VulkanDevice::startDevice(RenderDeviceInfo* info)
{
	if (!loadLibrary(info)) {
		return false;
	}
	const auto fail = [&](const QString& message, const QString& detail) {
		info->error = message;
		info->errorDetail = detail;
		stopDevice();
		return false;
	};
#define VIBE_VK_LOAD_GLOBAL(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name));
	VIBE_VK_GLOBAL(VIBE_VK_LOAD_GLOBAL)
#undef VIBE_VK_LOAD_GLOBAL
	if (!vkCreateInstance || !vkEnumerateInstanceExtensionProperties || !vkEnumerateInstanceLayerProperties) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan loader on this computer is incomplete."),
			QStringLiteral("global entry points missing"));
	}
	quint32 extensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	std::vector<VkExtensionProperties> extensions(extensionCount);
	if (extensionCount) {
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
	}
	const auto hasExtension = [&](const char* name) {
		return std::any_of(extensions.cbegin(), extensions.cend(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
	};
	std::vector<const char*> enabledExtensions;
	std::vector<const char*> enabledLayers;
	VkInstanceCreateFlags flags = 0;
	if (hasExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
		enabledExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		// The portability subset such devices expose depends on it under
		// Vulkan 1.0.
		if (hasExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
			enabledExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
		}
	}
	const bool validation = qEnvironmentVariableIntValue("VIBESTUDIO_VULKAN_VALIDATION") != 0;
	if (validation) {
		quint32 layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> layers(layerCount);
		if (layerCount) {
			vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
		}
		if (std::any_of(layers.cbegin(), layers.cend(), [](const VkLayerProperties& l) { return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; })) {
			enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
		}
		if (hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
			enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
	}
	VkApplicationInfo application = vulkanStruct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
	application.pApplicationName = "VibeStudio";
	application.pEngineName = "VibeStudio";
	application.apiVersion = VK_API_VERSION_1_0;
	VkInstanceCreateInfo create = vulkanStruct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
	create.flags = flags;
	create.pApplicationInfo = &application;
	create.enabledExtensionCount = quint32(enabledExtensions.size());
	create.ppEnabledExtensionNames = enabledExtensions.data();
	create.enabledLayerCount = quint32(enabledLayers.size());
	create.ppEnabledLayerNames = enabledLayers.data();
	VkResult result = vkCreateInstance(&create, nullptr, &m_instance);
	if (result != VK_SUCCESS) {
		return fail(result == VK_ERROR_INCOMPATIBLE_DRIVER
				? QCoreApplication::translate("VibeStudioRendering", "No Vulkan driver is installed. Install your graphics driver's Vulkan support, or choose OpenGL.")
				: QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not start."),
			QStringLiteral("vkCreateInstance: %1").arg(vulkanResult(result)));
	}
#define VIBE_VK_LOAD_INSTANCE(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(m_instance, #name));
	VIBE_VK_INSTANCE(VIBE_VK_LOAD_INSTANCE)
#undef VIBE_VK_LOAD_INSTANCE
	if (!vkCreateDevice || !vkGetDeviceProcAddr || !vkEnumeratePhysicalDevices) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan loader on this computer is incomplete."),
			QStringLiteral("instance entry points missing"));
	}
	if (validation && std::find_if(enabledExtensions.cbegin(), enabledExtensions.cend(), [](const char* name) {
			return std::strcmp(name, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
		}) != enabledExtensions.cend()) {
		vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
		vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
		if (vkCreateDebugUtilsMessengerEXT) {
			VkDebugUtilsMessengerCreateInfoEXT messenger = vulkanStruct<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
			messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			messenger.pfnUserCallback = debugMessage;
			vkCreateDebugUtilsMessengerEXT(m_instance, &messenger, nullptr, &m_messenger);
		}
	}
	if (!selectDevice(info)) {
		const QString message = info->error;
		const QString detail = info->errorDetail;
		return fail(message, detail);
	}
	quint32 deviceExtensionCount = 0;
	vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &deviceExtensionCount, nullptr);
	std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
	if (deviceExtensionCount) {
		vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &deviceExtensionCount, deviceExtensions.data());
	}
	std::vector<const char*> enabledDeviceExtensions;
	// MoltenVK and other portability implementations require this when they
	// offer it.
	if (std::any_of(deviceExtensions.cbegin(), deviceExtensions.cend(), [](const VkExtensionProperties& e) {
			return std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0;
		})) {
		enabledDeviceExtensions.push_back("VK_KHR_portability_subset");
	}
	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queue = vulkanStruct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
	queue.queueFamilyIndex = m_queueFamily;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	VkPhysicalDeviceFeatures features {};
	features.independentBlend = m_independentBlend ? VK_TRUE : VK_FALSE;
	VkDeviceCreateInfo deviceCreate = vulkanStruct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
	deviceCreate.queueCreateInfoCount = 1;
	deviceCreate.pQueueCreateInfos = &queue;
	deviceCreate.enabledExtensionCount = quint32(enabledDeviceExtensions.size());
	deviceCreate.ppEnabledExtensionNames = enabledDeviceExtensions.data();
	deviceCreate.pEnabledFeatures = &features;
	result = vkCreateDevice(m_physical, &deviceCreate, nullptr, &m_device);
	if (result != VK_SUCCESS) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not open %1.").arg(info->deviceName),
			QStringLiteral("vkCreateDevice: %1").arg(vulkanResult(result)));
	}
#define VIBE_VK_LOAD_DEVICE(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(m_device, #name));
	VIBE_VK_DEVICE(VIBE_VK_LOAD_DEVICE)
#undef VIBE_VK_LOAD_DEVICE
	bool complete = true;
#define VIBE_VK_CHECK(name) complete = complete && name != nullptr;
	VIBE_VK_DEVICE(VIBE_VK_CHECK)
#undef VIBE_VK_CHECK
	if (!complete) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver on this computer is incomplete."),
			QStringLiteral("device entry points missing"));
	}
	vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
	VkCommandPoolCreateInfo pool = vulkanStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
	pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool.queueFamilyIndex = m_queueFamily;
	result = vkCreateCommandPool(m_device, &pool, nullptr, &m_commandPool);
	if (result == VK_SUCCESS) {
		VkCommandBufferAllocateInfo allocation = vulkanStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
		allocation.commandPool = m_commandPool;
		allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocation.commandBufferCount = 1;
		result = vkAllocateCommandBuffers(m_device, &allocation, &m_commands);
	}
	if (result == VK_SUCCESS) {
		VkFenceCreateInfo fence = vulkanStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
		result = vkCreateFence(m_device, &fence, nullptr, &m_fence);
	}
	if (result != VK_SUCCESS) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not prepare its command buffers."),
			QStringLiteral("command setup: %1").arg(vulkanResult(result)));
	}
	// A 1x1 white texture for sampler slots a draw leaves empty.
	result = createImage(VK_FORMAT_R8G8B8A8_UNORM, QSize(1, 1), 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &m_white);
	if (result == VK_SUCCESS && ensureArena(256, &result)) {
		const quint32 white = 0xffffffffu;
		std::memcpy(m_arena.memory.mapped, &white, 4);
		VkCommandBufferBeginInfo begin = vulkanStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(m_commands, &begin);
		transition(m_commands, m_white, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		VkBufferImageCopy copy {};
		copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy.imageExtent = {1, 1, 1};
		vkCmdCopyBufferToImage(m_commands, m_arena.buffer, m_white.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		transition(m_commands, m_white, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		vkEndCommandBuffer(m_commands);
		submitAndWait(&result);
	}
	if (result != VK_SUCCESS) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver failed while VibeStudio set up its renderer."),
			QStringLiteral("setup upload: %1").arg(vulkanResult(result)));
	}
	m_programs.assign(size_t(gpuProgramCount()), ProgramObjects {});
	return true;
}

void VulkanDevice::stopDevice()
{
	if (m_device) {
		vkDeviceWaitIdle(m_device);
		for (FramebufferEntry& entry : m_framebuffers) {
			vkDestroyFramebuffer(m_device, entry.framebuffer, nullptr);
		}
		m_framebuffers.clear();
		for (auto& [key, pipeline] : m_pipelines) {
			vkDestroyPipeline(m_device, pipeline, nullptr);
		}
		m_pipelines.clear();
		for (auto& [key, pass] : m_renderPasses) {
			vkDestroyRenderPass(m_device, pass, nullptr);
		}
		m_renderPasses.clear();
		for (ProgramObjects& program : m_programs) {
			if (program.pipelineLayout) {
				vkDestroyPipelineLayout(m_device, program.pipelineLayout, nullptr);
			}
			if (program.setLayout) {
				vkDestroyDescriptorSetLayout(m_device, program.setLayout, nullptr);
			}
			if (program.vertex) {
				vkDestroyShaderModule(m_device, program.vertex, nullptr);
			}
			if (program.fragment) {
				vkDestroyShaderModule(m_device, program.fragment, nullptr);
			}
		}
		m_programs.clear();
		for (auto& [key, sampler] : m_samplers) {
			vkDestroySampler(m_device, sampler, nullptr);
		}
		m_samplers.clear();
		for (auto& target : m_targets) {
			destroyImage(&target->image);
		}
		m_targets.clear();
		for (auto& [key, texture] : m_textures) {
			destroyImage(&texture.image);
		}
		m_textures.clear();
		for (auto& [key, buffer] : m_buffers) {
			destroyBuffer(&buffer.buffer);
		}
		m_buffers.clear();
		destroyImage(&m_white);
		destroyBuffer(&m_arena);
		destroyBuffer(&m_readback);
		if (m_descriptorPool) {
			vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
		}
		if (m_fence) {
			vkDestroyFence(m_device, m_fence, nullptr);
			m_fence = VK_NULL_HANDLE;
		}
		if (m_commandPool) {
			vkDestroyCommandPool(m_device, m_commandPool, nullptr);
			m_commandPool = VK_NULL_HANDLE;
			m_commands = VK_NULL_HANDLE;
		}
		vkDestroyDevice(m_device, nullptr);
		m_device = VK_NULL_HANDLE;
	}
	if (m_instance) {
		if (m_messenger && vkDestroyDebugUtilsMessengerEXT) {
			vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
			m_messenger = VK_NULL_HANDLE;
		}
		if (vkDestroyInstance) {
			vkDestroyInstance(m_instance, nullptr);
		}
		m_instance = VK_NULL_HANDLE;
	}
	m_physical = VK_NULL_HANDLE;
}

bool VulkanDevice::formatSupportedOnDevice(GpuFormat format) const
{
	return format != GpuFormat::Rgba16 || m_rgba16;
}

VkFormat VulkanDevice::targetFormat(GpuFormat format) const
{
	switch (format) {
	case GpuFormat::Rgba8:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case GpuFormat::Rgba16:
		return m_rgba16 ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
	case GpuFormat::R32Int:
		return VK_FORMAT_R32_SINT;
	case GpuFormat::R32Float:
		return VK_FORMAT_R32_SFLOAT;
	case GpuFormat::Depth32:
		return m_depthFormat;
	}
	return VK_FORMAT_B8G8R8A8_UNORM;
}

bool VulkanDevice::ensureProgram(int index, QString* error)
{
	ProgramObjects& program = m_programs[size_t(index)];
	if (program.attempted) {
		*error = program.error;
		return program.pipelineLayout != VK_NULL_HANDLE;
	}
	program.attempted = true;
	const GpuProgramInfo* info = gpuProgramInfo(index);
	VkResult result = VK_SUCCESS;
	for (int stage = 0; stage < 2 && result == VK_SUCCESS; ++stage) {
		VkShaderModuleCreateInfo create = vulkanStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
		create.codeSize = size_t(stage == 0 ? info->vertexSpirvWords : info->fragmentSpirvWords) * 4;
		create.pCode = stage == 0 ? info->vertexSpirv : info->fragmentSpirv;
		result = vkCreateShaderModule(m_device, &create, nullptr, stage == 0 ? &program.vertex : &program.fragment);
	}
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	if (info->uniformBytes > 0) {
		bindings.push_back({0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
	}
	for (int slot = 0; slot < info->samplerCount; ++slot) {
		bindings.push_back({quint32(1 + slot), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
	}
	if (result == VK_SUCCESS) {
		VkDescriptorSetLayoutCreateInfo create = vulkanStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
		create.bindingCount = quint32(bindings.size());
		create.pBindings = bindings.data();
		result = vkCreateDescriptorSetLayout(m_device, &create, nullptr, &program.setLayout);
	}
	if (result == VK_SUCCESS) {
		VkPipelineLayoutCreateInfo create = vulkanStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
		create.setLayoutCount = 1;
		create.pSetLayouts = &program.setLayout;
		result = vkCreatePipelineLayout(m_device, &create, nullptr, &program.pipelineLayout);
	}
	if (result != VK_SUCCESS) {
		program.error = QStringLiteral("%1: %2").arg(QLatin1String(info->name), vulkanResult(result));
		*error = program.error;
		return false;
	}
	return true;
}

VkRenderPass VulkanDevice::renderPass(const PassSignature& signature, VkResult* result)
{
	const QByteArray key = signature.key();
	const auto found = m_renderPasses.find(key);
	if (found != m_renderPasses.end()) {
		return found->second;
	}
	std::vector<VkAttachmentDescription> attachments;
	std::vector<VkAttachmentReference> colors;
	const auto loadOp = [](GpuLoad load) {
		return load == GpuLoad::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : load == GpuLoad::Load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	};
	for (int slot = 0; slot < signature.colorCount; ++slot) {
		if (signature.colors[size_t(slot)] == VK_FORMAT_UNDEFINED) {
			colors.push_back({VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED});
			continue;
		}
		VkAttachmentDescription attachment {};
		attachment.format = signature.colors[size_t(slot)];
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = loadOp(signature.loads[size_t(slot)]);
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colors.push_back({quint32(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
		attachments.push_back(attachment);
	}
	VkAttachmentReference depth {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
	if (signature.depth != VK_FORMAT_UNDEFINED) {
		VkAttachmentDescription attachment {};
		attachment.format = signature.depth;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = loadOp(signature.depthLoad);
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth = {quint32(attachments.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		attachments.push_back(attachment);
	}
	VkSubpassDescription subpass {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = quint32(colors.size());
	subpass.pColorAttachments = colors.data();
	subpass.pDepthStencilAttachment = signature.depth != VK_FORMAT_UNDEFINED ? &depth : nullptr;
	VkRenderPassCreateInfo create = vulkanStruct<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
	create.attachmentCount = quint32(attachments.size());
	create.pAttachments = attachments.data();
	create.subpassCount = 1;
	create.pSubpasses = &subpass;
	VkRenderPass pass = VK_NULL_HANDLE;
	*result = vkCreateRenderPass(m_device, &create, nullptr, &pass);
	if (*result != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}
	m_renderPasses.emplace(key, pass);
	return pass;
}

VkPipeline VulkanDevice::pipeline(int program, const GpuState& state, const PassSignature& signature, VkRenderPass pass, VkResult* result)
{
	QByteArray key = signature.formatKey();
	key.append(reinterpret_cast<const char*>(&program), sizeof(program));
	key.append(reinterpret_cast<const char*>(&state), sizeof(state));
	const auto found = m_pipelines.find(key);
	if (found != m_pipelines.end()) {
		return found->second;
	}
	const GpuProgramInfo* info = gpuProgramInfo(program);
	const ProgramObjects& objects = m_programs[size_t(program)];
	VkPipelineShaderStageCreateInfo stages[2] {};
	stages[0] = vulkanStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = objects.vertex;
	stages[0].pName = "main";
	stages[1] = vulkanStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = objects.fragment;
	stages[1].pName = "main";
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
	for (int slot = 0; slot < 2; ++slot) {
		const GpuVertexBinding& binding = info->bindings[size_t(slot)];
		if (binding.stride <= 0) {
			continue;
		}
		bindings.push_back({quint32(slot), quint32(binding.stride), binding.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
		for (int a = 0; a < binding.attributeCount; ++a) {
			const GpuVertexAttribute& attribute = binding.attributes[size_t(a)];
			attributes.push_back({quint32(attribute.location), quint32(slot), vertexFormat(attribute.format), quint32(attribute.offset)});
		}
	}
	VkPipelineVertexInputStateCreateInfo vertexInput = vulkanStruct<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
	vertexInput.vertexBindingDescriptionCount = quint32(bindings.size());
	vertexInput.pVertexBindingDescriptions = bindings.data();
	vertexInput.vertexAttributeDescriptionCount = quint32(attributes.size());
	vertexInput.pVertexAttributeDescriptions = attributes.data();
	VkPipelineInputAssemblyStateCreateInfo assembly = vulkanStruct<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewport = vulkanStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo raster = vulkanStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = state.cull == GpuCull::Back ? VK_CULL_MODE_BACK_BIT : state.cull == GpuCull::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisample = vulkanStruct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineDepthStencilStateCreateInfo depth = vulkanStruct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
	const bool hasDepth = signature.depth != VK_FORMAT_UNDEFINED;
	depth.depthTestEnable = state.depthTest && hasDepth ? VK_TRUE : VK_FALSE;
	depth.depthWriteEnable = state.depthWrite && hasDepth ? VK_TRUE : VK_FALSE;
	depth.depthCompareOp = compareOp(state.depthCompare);
	std::vector<VkPipelineColorBlendAttachmentState> blends;
	bool mixed = false;
	for (int slot = 0; slot < signature.colorCount; ++slot) {
		VkPipelineColorBlendAttachmentState blend {};
		const VkFormat format = signature.colors[size_t(slot)];
		const bool blended = state.blend && format != VK_FORMAT_UNDEFINED && !integerFormat(format);
		mixed = mixed || (state.blend && integerFormat(format));
		blend.blendEnable = blended ? VK_TRUE : VK_FALSE;
		blend.srcColorBlendFactor = blendFactor(state.sourceColor);
		blend.dstColorBlendFactor = blendFactor(state.destinationColor);
		blend.colorBlendOp = VK_BLEND_OP_ADD;
		blend.srcAlphaBlendFactor = blendFactor(state.sourceAlpha);
		blend.dstAlphaBlendFactor = blendFactor(state.destinationAlpha);
		blend.alphaBlendOp = VK_BLEND_OP_ADD;
		blend.colorWriteMask = VkColorComponentFlags(state.colorMask & 0xF);
		blends.push_back(blend);
	}
	if (mixed && !m_independentBlend) {
		// Without independent blending every attachment must share one state.
		for (auto& blend : blends) {
			blend.blendEnable = VK_FALSE;
		}
	}
	VkPipelineColorBlendStateCreateInfo colorBlend = vulkanStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
	colorBlend.attachmentCount = quint32(blends.size());
	colorBlend.pAttachments = blends.data();
	const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic = vulkanStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;
	VkGraphicsPipelineCreateInfo create = vulkanStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
	create.stageCount = 2;
	create.pStages = stages;
	create.pVertexInputState = &vertexInput;
	create.pInputAssemblyState = &assembly;
	create.pViewportState = &viewport;
	create.pRasterizationState = &raster;
	create.pMultisampleState = &multisample;
	create.pDepthStencilState = &depth;
	create.pColorBlendState = &colorBlend;
	create.pDynamicState = &dynamic;
	create.layout = objects.pipelineLayout;
	create.renderPass = pass;
	create.subpass = 0;
	VkPipeline pipeline = VK_NULL_HANDLE;
	*result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline);
	if (*result != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}
	m_pipelines.emplace(key, pipeline);
	return pipeline;
}

VkFramebuffer VulkanDevice::framebuffer(VkRenderPass pass, const std::vector<VkImageView>& views, QSize size, VkResult* result)
{
	for (const FramebufferEntry& entry : m_framebuffers) {
		if (entry.pass == pass && entry.views == views && entry.size == size) {
			return entry.framebuffer;
		}
	}
	VkFramebufferCreateInfo create = vulkanStruct<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
	create.renderPass = pass;
	create.attachmentCount = quint32(views.size());
	create.pAttachments = views.data();
	create.width = quint32(size.width());
	create.height = quint32(size.height());
	create.layers = 1;
	FramebufferEntry entry;
	entry.pass = pass;
	entry.views = views;
	entry.size = size;
	*result = vkCreateFramebuffer(m_device, &create, nullptr, &entry.framebuffer);
	if (*result != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}
	m_framebuffers.push_back(entry);
	return entry.framebuffer;
}

void VulkanDevice::forgetFramebuffers(VkImageView view)
{
	for (auto it = m_framebuffers.begin(); it != m_framebuffers.end();) {
		if (std::find(it->views.cbegin(), it->views.cend(), view) != it->views.cend()) {
			vkDestroyFramebuffer(m_device, it->framebuffer, nullptr);
			it = m_framebuffers.erase(it);
		} else {
			++it;
		}
	}
}

VkSampler VulkanDevice::sampler(const GpuSampler& state, bool filterable, VkResult* result)
{
	GpuSampler effective = state;
	if (!filterable) {
		effective.minFilter = GpuFilter::Nearest;
		effective.magFilter = GpuFilter::Nearest;
		effective.mipmap = GpuMipmap::None;
	}
	const quint32 key = quint32(effective.minFilter) | quint32(effective.magFilter) << 2 | quint32(effective.mipmap) << 4
		| quint32(effective.wrapU) << 6 | quint32(effective.wrapV) << 8;
	const auto found = m_samplers.find(key);
	if (found != m_samplers.end()) {
		return found->second;
	}
	VkSamplerCreateInfo create = vulkanStruct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
	create.magFilter = effective.magFilter == GpuFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	create.minFilter = effective.minFilter == GpuFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	create.mipmapMode = effective.mipmap == GpuMipmap::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	create.addressModeU = effective.wrapU == GpuWrap::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	create.addressModeV = effective.wrapV == GpuWrap::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	create.minLod = 0.0f;
	// No mipmapping: clamp sampling to the base level.
	create.maxLod = effective.mipmap == GpuMipmap::None ? 0.0f : VK_LOD_CLAMP_NONE;
	create.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	VkSampler sampler = VK_NULL_HANDLE;
	*result = vkCreateSampler(m_device, &create, nullptr, &sampler);
	if (*result != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}
	m_samplers.emplace(key, sampler);
	return sampler;
}

Target* VulkanDevice::acquireTarget(GpuFormat format, QSize size, VkResult* result)
{
	const VkFormat vkFormat = targetFormat(format);
	for (auto& target : m_targets) {
		if (!target->inUse && target->image.format == vkFormat && target->image.size == size) {
			target->inUse = true;
			target->lastUsed = m_frame;
			// Contents never carry over between frames.
			target->image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
			return target.get();
		}
	}
	auto target = std::make_unique<Target>();
	target->format = format;
	const bool depth = format == GpuFormat::Depth32;
	const VkImageUsageFlags usage = depth ? VkImageUsageFlags(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
		: VkImageUsageFlags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	*result = createImage(vkFormat, size, 1, usage, &target->image);
	if (*result != VK_SUCCESS) {
		return nullptr;
	}
	target->inUse = true;
	target->lastUsed = m_frame;
	m_targets.push_back(std::move(target));
	return m_targets.back().get();
}

void VulkanDevice::releaseTargets()
{
	for (auto& target : m_targets) {
		target->inUse = false;
	}
	std::sort(m_targets.begin(), m_targets.end(), [](const auto& a, const auto& b) { return a->lastUsed > b->lastUsed; });
	while (!m_targets.empty() && (int(m_targets.size()) > kTargetPoolLimit || m_frame - m_targets.back()->lastUsed > kTargetIdleFrames)) {
		destroyImage(&m_targets.back()->image);
		m_targets.pop_back();
	}
}

void VulkanDevice::evictCaches()
{
	qint64 total = 0;
	for (const auto& [key, texture] : m_textures) {
		total += texture.bytes;
	}
	for (const auto& [key, buffer] : m_buffers) {
		total += buffer.bytes;
	}
	while (total > kCacheBudgetBytes) {
		auto oldestTexture = m_textures.end();
		auto oldestBuffer = m_buffers.end();
		for (auto it = m_textures.begin(); it != m_textures.end(); ++it) {
			if (it->second.lastUsed < m_frame && (oldestTexture == m_textures.end() || it->second.lastUsed < oldestTexture->second.lastUsed)) {
				oldestTexture = it;
			}
		}
		for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
			if (it->second.lastUsed < m_frame && (oldestBuffer == m_buffers.end() || it->second.lastUsed < oldestBuffer->second.lastUsed)) {
				oldestBuffer = it;
			}
		}
		if (oldestTexture == m_textures.end() && oldestBuffer == m_buffers.end()) {
			break;
		}
		if (oldestBuffer == m_buffers.end() || (oldestTexture != m_textures.end() && oldestTexture->second.lastUsed <= oldestBuffer->second.lastUsed)) {
			total -= oldestTexture->second.bytes;
			destroyImage(&oldestTexture->second.image);
			m_textures.erase(oldestTexture);
		} else {
			total -= oldestBuffer->second.bytes;
			destroyBuffer(&oldestBuffer->second.buffer);
			m_buffers.erase(oldestBuffer);
		}
	}
}

void VulkanDevice::releaseOwnerOnDevice(quint64 owner)
{
	for (auto it = m_textures.begin(); it != m_textures.end();) {
		if (it->first.first == owner) {
			destroyImage(&it->second.image);
			it = m_textures.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = m_buffers.begin(); it != m_buffers.end();) {
		if (it->first.first == owner) {
			destroyBuffer(&it->second.buffer);
			it = m_buffers.erase(it);
		} else {
			++it;
		}
	}
}

bool VulkanDevice::ensureArena(VkDeviceSize size, VkResult* result)
{
	if (m_arena.buffer && m_arena.capacity >= size) {
		return true;
	}
	destroyBuffer(&m_arena);
	const VkDeviceSize capacity = std::max<VkDeviceSize>(size + size / 2, 4 * 1024 * 1024);
	*result = createBuffer(capacity,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true,
		&m_arena);
	return *result == VK_SUCCESS;
}

bool VulkanDevice::ensureReadback(VkDeviceSize size, VkResult* result)
{
	if (m_readback.buffer && m_readback.capacity >= size) {
		return true;
	}
	destroyBuffer(&m_readback);
	*result = createBuffer(std::max<VkDeviceSize>(size, 64 * 1024), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &m_readback);
	return *result == VK_SUCCESS;
}

bool VulkanDevice::ensureDescriptorPool(int draws, VkResult* result)
{
	const int needed = std::max(draws, 16);
	if (m_descriptorPool && m_descriptorCapacity >= needed) {
		*result = vkResetDescriptorPool(m_device, m_descriptorPool, 0);
		return *result == VK_SUCCESS;
	}
	if (m_descriptorPool) {
		vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
		m_descriptorPool = VK_NULL_HANDLE;
	}
	const int capacity = needed + needed / 2;
	const VkDescriptorPoolSize sizes[] = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, quint32(capacity)},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, quint32(capacity * kGpuMaxTextures)},
	};
	VkDescriptorPoolCreateInfo create = vulkanStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
	create.maxSets = quint32(capacity);
	create.poolSizeCount = 2;
	create.pPoolSizes = sizes;
	*result = vkCreateDescriptorPool(m_device, &create, nullptr, &m_descriptorPool);
	m_descriptorCapacity = *result == VK_SUCCESS ? capacity : 0;
	return *result == VK_SUCCESS;
}

void VulkanDevice::transition(VkCommandBuffer commands, Image& image, VkImageLayout layout, VkImageAspectFlags aspect)
{
	if (image.layout == layout && layout != VK_IMAGE_LAYOUT_UNDEFINED) {
		// Still order this use after earlier writes to the same image.
		VkMemoryBarrier memory = vulkanStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
		memory.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		memory.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory, 0, nullptr, 0, nullptr);
		return;
	}
	VkImageMemoryBarrier barrier = vulkanStruct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
	barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.oldLayout = image.layout;
	barrier.newLayout = layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.image;
	barrier.subresourceRange = {aspect, 0, quint32(image.levels), 0, 1};
	vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	image.layout = layout;
}

bool VulkanDevice::submitAndWait(VkResult* result)
{
	VkSubmitInfo submit = vulkanStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &m_commands;
	*result = vkResetFences(m_device, 1, &m_fence);
	if (*result == VK_SUCCESS) {
		*result = vkQueueSubmit(m_queue, 1, &submit, m_fence);
	}
	if (*result == VK_SUCCESS) {
		*result = vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
	}
	return *result == VK_SUCCESS;
}

GpuFrameResult VulkanDevice::renderFrame(const GpuFrame& frame, const std::atomic_bool* cancelled)
{
	GpuFrameResult result;
	VkResult status = VK_SUCCESS;
	++m_frame;
	std::vector<Target*> targets(size_t(frame.targets.size()), nullptr);
	std::vector<Image> transientImages;
	bool recording = false;
	const auto cleanUp = [&]() {
		if (recording) {
			vkEndCommandBuffer(m_commands);
			vkResetCommandBuffer(m_commands, 0);
		}
		for (Image& image : transientImages) {
			destroyImage(&image);
		}
		releaseTargets();
		evictCaches();
	};
	const auto fail = [&](const QString& message, const QString& detail) {
		cleanUp();
		if (status == VK_ERROR_DEVICE_LOST) {
			markLost(detail);
		}
		result.success = false;
		result.error = message;
		result.errorDetail = detail;
		result.readbacks.clear();
		return result;
	};

	// Lay out this frame's host data in the arena: transient buffers, the
	// staging copies of new uploads, and every draw's uniforms.
	struct Placement {
		VkDeviceSize offset = 0;
		bool staged = false;
	};
	VkDeviceSize arenaSize = 0;
	const auto place = [&](VkDeviceSize bytes, VkDeviceSize alignment) {
		arenaSize = (arenaSize + alignment - 1) / alignment * alignment;
		const VkDeviceSize offset = arenaSize;
		arenaSize += std::max<VkDeviceSize>(bytes, 4);
		return offset;
	};
	std::vector<Placement> bufferPlacements(size_t(frame.buffers.size()));
	std::vector<CachedBuffer*> cachedBuffers(size_t(frame.buffers.size()), nullptr);
	for (int index = 0; index < frame.buffers.size(); ++index) {
		const GpuBufferData& data = frame.buffers.at(index);
		if (data.cacheKey != 0) {
			auto found = m_buffers.find({frame.owner, data.cacheKey});
			if (found != m_buffers.end() && found->second.bytes == data.bytes.size()) {
				found->second.lastUsed = m_frame;
				cachedBuffers[size_t(index)] = &found->second;
				continue;
			}
			if (found != m_buffers.end()) {
				destroyBuffer(&found->second.buffer);
				m_buffers.erase(found);
			}
			bufferPlacements[size_t(index)].staged = true;
		}
		bufferPlacements[size_t(index)].offset = place(VkDeviceSize(data.bytes.size()), kArenaAlignment);
	}
	std::vector<CachedTexture*> cachedTextures(size_t(frame.textures.size()), nullptr);
	std::vector<Image*> textureImages(size_t(frame.textures.size()), nullptr);
	std::vector<std::vector<VkDeviceSize>> levelOffsets(size_t(frame.textures.size()));
	std::vector<std::vector<QImage>> levelImages(size_t(frame.textures.size()));
	for (int index = 0; index < frame.textures.size(); ++index) {
		const GpuTextureData& data = frame.textures.at(index);
		if (data.cacheKey != 0) {
			auto found = m_textures.find({frame.owner, data.cacheKey});
			if (found != m_textures.end()) {
				found->second.lastUsed = m_frame;
				cachedTextures[size_t(index)] = &found->second;
				textureImages[size_t(index)] = &found->second.image;
				continue;
			}
		}
		for (const QImage& level : data.levels) {
			const QImage converted = textureLevelImage(level);
			levelOffsets[size_t(index)].push_back(place(VkDeviceSize(converted.width()) * converted.height() * 4, kArenaAlignment));
			levelImages[size_t(index)].push_back(converted);
		}
	}
	const VkDeviceSize uniformAlignment = std::max<VkDeviceSize>(m_properties.limits.minUniformBufferOffsetAlignment, 16);
	std::vector<std::vector<VkDeviceSize>> uniformOffsets(size_t(frame.passes.size()));
	std::vector<std::vector<QByteArray>> uniformData(size_t(frame.passes.size()));
	int drawCount = 0;
	for (int passIndex = 0; passIndex < frame.passes.size(); ++passIndex) {
		const GpuPass& pass = frame.passes.at(passIndex);
		for (const GpuDraw& draw : pass.draws) {
			const GpuProgramInfo* program = gpuProgramInfo(draw.program);
			++drawCount;
			if (program->uniformBytes > 0) {
				uniformData[size_t(passIndex)].push_back(paddedUniforms(draw, program->uniformBytes));
				uniformOffsets[size_t(passIndex)].push_back(place(VkDeviceSize(program->uniformBytes), uniformAlignment));
			} else {
				uniformData[size_t(passIndex)].push_back({});
				uniformOffsets[size_t(passIndex)].push_back(0);
			}
		}
	}
	if (!ensureArena(arenaSize, &status)) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The graphics device ran out of memory drawing this view."),
			QStringLiteral("arena of %1 bytes: %2").arg(arenaSize).arg(vulkanResult(status)));
	}
	auto* arena = static_cast<char*>(m_arena.memory.mapped);
	for (int index = 0; index < frame.buffers.size(); ++index) {
		if (!cachedBuffers[size_t(index)] && !frame.buffers.at(index).bytes.isEmpty()) {
			std::memcpy(arena + bufferPlacements[size_t(index)].offset, frame.buffers.at(index).bytes.constData(), size_t(frame.buffers.at(index).bytes.size()));
		}
	}
	for (int index = 0; index < frame.textures.size(); ++index) {
		for (size_t level = 0; level < levelImages[size_t(index)].size(); ++level) {
			const QImage& image = levelImages[size_t(index)][level];
			char* destination = arena + levelOffsets[size_t(index)][level];
			for (int y = 0; y < image.height(); ++y) {
				std::memcpy(destination + qsizetype(y) * image.width() * 4, image.constScanLine(y), size_t(image.width()) * 4);
			}
		}
	}
	for (int passIndex = 0; passIndex < frame.passes.size(); ++passIndex) {
		for (size_t drawIndex = 0; drawIndex < uniformData[size_t(passIndex)].size(); ++drawIndex) {
			const QByteArray& bytes = uniformData[size_t(passIndex)][drawIndex];
			if (!bytes.isEmpty()) {
				std::memcpy(arena + uniformOffsets[size_t(passIndex)][drawIndex], bytes.constData(), size_t(bytes.size()));
			}
		}
	}

	// Device objects for new uploads.
	for (int index = 0; index < frame.buffers.size(); ++index) {
		if (!bufferPlacements[size_t(index)].staged) {
			continue;
		}
		const GpuBufferData& data = frame.buffers.at(index);
		CachedBuffer cached;
		status = createBuffer(VkDeviceSize(data.bytes.size()),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, &cached.buffer);
		if (status != VK_SUCCESS) {
			return fail(QCoreApplication::translate("VibeStudioRendering", "The graphics device ran out of memory drawing this view."),
				QStringLiteral("vertex buffer: %1").arg(vulkanResult(status)));
		}
		cached.bytes = data.bytes.size();
		cached.lastUsed = m_frame;
		auto& stored = m_buffers[{frame.owner, data.cacheKey}];
		stored = cached;
		cachedBuffers[size_t(index)] = &stored;
	}
	for (int index = 0; index < frame.textures.size(); ++index) {
		if (textureImages[size_t(index)]) {
			continue;
		}
		const GpuTextureData& data = frame.textures.at(index);
		Image image;
		status = createImage(VK_FORMAT_R8G8B8A8_UNORM, data.levels.first().size(), int(data.levels.size()),
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &image);
		if (status != VK_SUCCESS) {
			return fail(QCoreApplication::translate("VibeStudioRendering", "The graphics device ran out of memory drawing this view."),
				QStringLiteral("texture: %1").arg(vulkanResult(status)));
		}
		if (data.cacheKey != 0) {
			CachedTexture cached;
			cached.image = image;
			cached.lastUsed = m_frame;
			for (const QImage& level : levelImages[size_t(index)]) {
				cached.bytes += qint64(level.width()) * level.height() * 4;
			}
			auto& stored = m_textures[{frame.owner, data.cacheKey}];
			stored = cached;
			cachedTextures[size_t(index)] = &stored;
			textureImages[size_t(index)] = &stored.image;
		} else {
			transientImages.push_back(image);
		}
	}
	// Transient images are stored by value; point at them now the vector is final.
	{
		size_t transient = 0;
		for (int index = 0; index < frame.textures.size(); ++index) {
			if (!textureImages[size_t(index)]) {
				textureImages[size_t(index)] = &transientImages[transient++];
			}
		}
	}
	for (int index = 0; index < frame.targets.size(); ++index) {
		targets[size_t(index)] = acquireTarget(frame.targets.at(index).format, targetSize(frame, index), &status);
		if (!targets[size_t(index)]) {
			return fail(QCoreApplication::translate("VibeStudioRendering", "The graphics device ran out of memory drawing this view."),
				QStringLiteral("target %1: %2").arg(index).arg(vulkanResult(status)));
		}
	}
	if (!ensureDescriptorPool(drawCount, &status)) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The graphics device ran out of memory drawing this view."),
			QStringLiteral("descriptor pool: %1").arg(vulkanResult(status)));
	}
	for (int index = 0; index < frame.passes.size(); ++index) {
		for (const GpuDraw& draw : frame.passes.at(index).draws) {
			QString error;
			if (!ensureProgram(draw.program, &error)) {
				return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not build one of VibeStudio's shaders."), error);
			}
		}
	}

	VkCommandBufferBeginInfo begin = vulkanStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	status = vkBeginCommandBuffer(m_commands, &begin);
	if (status != VK_SUCCESS) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not record this view."),
			QStringLiteral("vkBeginCommandBuffer: %1").arg(vulkanResult(status)));
	}
	recording = true;

	// Uploads.
	for (int index = 0; index < frame.buffers.size(); ++index) {
		if (bufferPlacements[size_t(index)].staged && !frame.buffers.at(index).bytes.isEmpty()) {
			VkBufferCopy copy {bufferPlacements[size_t(index)].offset, 0, VkDeviceSize(frame.buffers.at(index).bytes.size())};
			vkCmdCopyBuffer(m_commands, m_arena.buffer, cachedBuffers[size_t(index)]->buffer.buffer, 1, &copy);
		}
	}
	for (int index = 0; index < frame.textures.size(); ++index) {
		if (levelImages[size_t(index)].empty()) {
			continue;
		}
		Image& image = *textureImages[size_t(index)];
		transition(m_commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		std::vector<VkBufferImageCopy> copies;
		for (size_t level = 0; level < levelImages[size_t(index)].size(); ++level) {
			const QImage& source = levelImages[size_t(index)][level];
			VkBufferImageCopy copy {};
			copy.bufferOffset = levelOffsets[size_t(index)][level];
			copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, quint32(level), 0, 1};
			copy.imageExtent = {quint32(source.width()), quint32(source.height()), 1};
			copies.push_back(copy);
		}
		vkCmdCopyBufferToImage(m_commands, m_arena.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, quint32(copies.size()), copies.data());
		transition(m_commands, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
	}
	{
		VkMemoryBarrier memory = vulkanStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
		memory.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		memory.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		vkCmdPipelineBarrier(m_commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory, 0, nullptr, 0, nullptr);
	}

	for (int passIndex = 0; passIndex < frame.passes.size(); ++passIndex) {
		if (cancelled && cancelled->load()) {
			cleanUp();
			result.cancelled = true;
			return result;
		}
		const GpuPass& pass = frame.passes.at(passIndex);
		PassSignature signature;
		QSize passSize;
		std::vector<VkImageView> views;
		std::vector<VkClearValue> clears;
		for (int slot = 0; slot < kGpuMaxColorTargets; ++slot) {
			if (pass.colors[size_t(slot)] >= 0) {
				signature.colorCount = slot + 1;
			}
		}
		for (int slot = 0; slot < signature.colorCount; ++slot) {
			const int target = pass.colors[size_t(slot)];
			if (target < 0) {
				signature.colors[size_t(slot)] = VK_FORMAT_UNDEFINED;
				continue;
			}
			Image& image = targets[size_t(target)]->image;
			signature.colors[size_t(slot)] = image.format;
			signature.loads[size_t(slot)] = pass.colorLoad[size_t(slot)];
			if (pass.colorLoad[size_t(slot)] != GpuLoad::Load) {
				image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}
			transition(m_commands, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			views.push_back(image.view);
			VkClearValue clear {};
			const GpuClear& value = pass.clear[size_t(slot)];
			if (integerFormat(image.format)) {
				clear.color.int32[0] = value.integer;
			} else {
				std::memcpy(clear.color.float32, value.color.data(), sizeof(float) * 4);
			}
			clears.push_back(clear);
			passSize = image.size;
		}
		if (pass.depth >= 0) {
			Image& image = targets[size_t(pass.depth)]->image;
			signature.depth = image.format;
			signature.depthLoad = pass.depthLoad;
			if (pass.depthLoad != GpuLoad::Load) {
				image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}
			transition(m_commands, image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspectOf(image.format));
			views.push_back(image.view);
			VkClearValue clear {};
			clear.depthStencil = {pass.clearDepth, 0};
			clears.push_back(clear);
			passSize = image.size;
		}
		// Targets this pass samples become shader-readable first.
		for (const GpuDraw& draw : pass.draws) {
			for (const GpuTextureRef& ref : draw.textures) {
				if (ref.kind == GpuTextureRef::Kind::Target) {
					transition(m_commands, targets[size_t(ref.index)]->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
				}
			}
		}
		const VkRenderPass vkPass = renderPass(signature, &status);
		const VkFramebuffer vkFramebuffer = vkPass ? framebuffer(vkPass, views, passSize, &status) : VK_NULL_HANDLE;
		if (!vkFramebuffer) {
			return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver cannot render this view's images."),
				QStringLiteral("pass %1: %2").arg(passIndex).arg(vulkanResult(status)));
		}
		VkRenderPassBeginInfo beginPass = vulkanStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
		beginPass.renderPass = vkPass;
		beginPass.framebuffer = vkFramebuffer;
		beginPass.renderArea = {{0, 0}, {quint32(passSize.width()), quint32(passSize.height())}};
		beginPass.clearValueCount = quint32(clears.size());
		beginPass.pClearValues = clears.data();
		vkCmdBeginRenderPass(m_commands, &beginPass, VK_SUBPASS_CONTENTS_INLINE);
		const VkViewport viewport {0.0f, 0.0f, float(passSize.width()), float(passSize.height()), 0.0f, 1.0f};
		vkCmdSetViewport(m_commands, 0, 1, &viewport);
		for (int drawIndex = 0; drawIndex < pass.draws.size(); ++drawIndex) {
			const GpuDraw& draw = pass.draws.at(drawIndex);
			if (draw.count <= 0) {
				continue;
			}
			const GpuProgramInfo* program = gpuProgramInfo(draw.program);
			const ProgramObjects& objects = m_programs[size_t(draw.program)];
			const VkPipeline vkPipeline = pipeline(draw.program, draw.state, signature, vkPass, &status);
			if (!vkPipeline) {
				vkCmdEndRenderPass(m_commands);
				return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not build one of VibeStudio's shaders."),
					QStringLiteral("pipeline %1: %2").arg(QLatin1String(program->name), vulkanResult(status)));
			}
			vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
			const QRect scissor = draw.scissor.isEmpty() ? QRect(QPoint(), passSize) : draw.scissor;
			const VkRect2D rect {{scissor.x(), scissor.y()}, {quint32(scissor.width()), quint32(scissor.height())}};
			vkCmdSetScissor(m_commands, 0, 1, &rect);
			VkDescriptorSet set = VK_NULL_HANDLE;
			VkDescriptorSetAllocateInfo allocation = vulkanStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
			allocation.descriptorPool = m_descriptorPool;
			allocation.descriptorSetCount = 1;
			allocation.pSetLayouts = &objects.setLayout;
			status = vkAllocateDescriptorSets(m_device, &allocation, &set);
			if (status != VK_SUCCESS) {
				vkCmdEndRenderPass(m_commands);
				return fail(QCoreApplication::translate("VibeStudioRendering", "The graphics device ran out of memory drawing this view."),
					QStringLiteral("descriptor set: %1").arg(vulkanResult(status)));
			}
			std::vector<VkWriteDescriptorSet> writes;
			VkDescriptorBufferInfo uniformInfo {m_arena.buffer, 0, VkDeviceSize(std::max(program->uniformBytes, 16))};
			std::array<VkDescriptorImageInfo, kGpuMaxTextures> imageInfo {};
			if (program->uniformBytes > 0) {
				VkWriteDescriptorSet write = vulkanStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
				write.dstSet = set;
				write.dstBinding = 0;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				write.pBufferInfo = &uniformInfo;
				writes.push_back(write);
			}
			for (int slot = 0; slot < program->samplerCount; ++slot) {
				const GpuTextureRef& ref = draw.textures[size_t(slot)];
				VkImageView view = m_white.view;
				bool filterable = true;
				if (ref.kind == GpuTextureRef::Kind::Texture) {
					view = textureImages[size_t(ref.index)]->view;
				} else if (ref.kind == GpuTextureRef::Kind::Target) {
					view = targets[size_t(ref.index)]->image.view;
					const GpuFormat format = frame.targets.at(ref.index).format;
					filterable = format == GpuFormat::Rgba8 || format == GpuFormat::Rgba16;
				}
				const VkSampler vkSampler = sampler(ref.sampler, filterable, &status);
				if (!vkSampler) {
					vkCmdEndRenderPass(m_commands);
					return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not record this view."),
						QStringLiteral("sampler: %1").arg(vulkanResult(status)));
				}
				imageInfo[size_t(slot)] = {vkSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
				VkWriteDescriptorSet write = vulkanStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
				write.dstSet = set;
				write.dstBinding = quint32(1 + slot);
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				write.pImageInfo = &imageInfo[size_t(slot)];
				writes.push_back(write);
			}
			if (!writes.empty()) {
				vkUpdateDescriptorSets(m_device, quint32(writes.size()), writes.data(), 0, nullptr);
			}
			const quint32 dynamicOffset = quint32(uniformOffsets[size_t(passIndex)][size_t(drawIndex)]);
			vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, objects.pipelineLayout, 0, 1, &set, program->uniformBytes > 0 ? 1 : 0,
				&dynamicOffset);
			for (int slot = 0; slot < 2; ++slot) {
				const GpuVertexBinding& binding = program->bindings[size_t(slot)];
				if (binding.stride <= 0) {
					continue;
				}
				const GpuBufferRef& ref = draw.vertexBuffers[size_t(slot)];
				VkBuffer buffer = m_arena.buffer;
				VkDeviceSize offset = VkDeviceSize(ref.offset);
				if (cachedBuffers[size_t(ref.buffer)]) {
					buffer = cachedBuffers[size_t(ref.buffer)]->buffer.buffer;
				} else {
					offset += bufferPlacements[size_t(ref.buffer)].offset;
				}
				vkCmdBindVertexBuffers(m_commands, quint32(slot), 1, &buffer, &offset);
			}
			if (draw.indexBuffer.buffer >= 0) {
				const int index = draw.indexBuffer.buffer;
				VkBuffer buffer = m_arena.buffer;
				VkDeviceSize offset = VkDeviceSize(draw.indexBuffer.offset);
				if (cachedBuffers[size_t(index)]) {
					buffer = cachedBuffers[size_t(index)]->buffer.buffer;
				} else {
					offset += bufferPlacements[size_t(index)].offset;
				}
				vkCmdBindIndexBuffer(m_commands, buffer, offset, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(m_commands, quint32(draw.count), quint32(draw.instances), 0, 0, 0);
			} else {
				vkCmdDraw(m_commands, quint32(draw.count), quint32(draw.instances), 0, 0);
			}
		}
		vkCmdEndRenderPass(m_commands);
	}

	// Read-backs.
	VkDeviceSize readbackSize = 0;
	std::vector<VkDeviceSize> readbackOffsets;
	for (int target : frame.readbacks) {
		const QSize size = targetSize(frame, target);
		readbackSize = (readbackSize + kArenaAlignment - 1) / kArenaAlignment * kArenaAlignment;
		readbackOffsets.push_back(readbackSize);
		const GpuFormat format = targets[size_t(target)]->image.format == VK_FORMAT_R16G16B16A16_UNORM ? GpuFormat::Rgba16 : frame.targets.at(target).format;
		readbackSize += VkDeviceSize(size.width()) * size.height() * (format == GpuFormat::Rgba16 ? 8 : 4);
	}
	if (!frame.readbacks.isEmpty() && !ensureReadback(readbackSize, &status)) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The graphics device ran out of memory drawing this view."),
			QStringLiteral("readback: %1").arg(vulkanResult(status)));
	}
	for (int i = 0; i < frame.readbacks.size(); ++i) {
		const int target = frame.readbacks.at(i);
		Image& image = targets[size_t(target)]->image;
		const bool depth = frame.targets.at(target).format == GpuFormat::Depth32;
		if (depth && image.format != VK_FORMAT_D32_SFLOAT) {
			return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not return this view's image."),
				QStringLiteral("depth read-back needs D32_SFLOAT"));
		}
		transition(m_commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, aspectOf(image.format));
		VkBufferImageCopy copy {};
		copy.bufferOffset = readbackOffsets[size_t(i)];
		copy.imageSubresource = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 0, 1};
		copy.imageExtent = {quint32(image.size.width()), quint32(image.size.height()), 1};
		vkCmdCopyImageToBuffer(m_commands, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback.buffer, 1, &copy);
	}
	if (!frame.readbacks.isEmpty()) {
		VkMemoryBarrier memory = vulkanStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
		memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		vkCmdPipelineBarrier(m_commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory, 0, nullptr, 0, nullptr);
	}
	status = vkEndCommandBuffer(m_commands);
	recording = false;
	if (status != VK_SUCCESS) {
		return fail(QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver could not record this view."),
			QStringLiteral("vkEndCommandBuffer: %1").arg(vulkanResult(status)));
	}
	if (!submitAndWait(&status)) {
		return fail(status == VK_ERROR_DEVICE_LOST ? QCoreApplication::translate("VibeStudioRendering", "The graphics device stopped responding.")
												   : QCoreApplication::translate("VibeStudioRendering", "The Vulkan driver failed while drawing this view."),
			QStringLiteral("submit: %1").arg(vulkanResult(status)));
	}
	vkResetCommandBuffer(m_commands, 0);
	if (!m_readback.memory.coherent && !frame.readbacks.isEmpty()) {
		VkMappedMemoryRange range = vulkanStruct<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
		range.memory = m_readback.memory.memory;
		range.offset = 0;
		range.size = VK_WHOLE_SIZE;
		vkInvalidateMappedMemoryRanges(m_device, 1, &range);
	}
	for (int i = 0; i < frame.readbacks.size(); ++i) {
		const int target = frame.readbacks.at(i);
		const QSize size = targetSize(frame, target);
		GpuReadback readback;
		readback.target = target;
		readback.size = size;
		readback.format = targets[size_t(target)]->image.format == VK_FORMAT_R16G16B16A16_UNORM ? GpuFormat::Rgba16
			: frame.targets.at(target).format == GpuFormat::Rgba16							 ? GpuFormat::Rgba8
																							 : frame.targets.at(target).format;
		const qsizetype bytes = qsizetype(size.width()) * size.height() * (readback.format == GpuFormat::Rgba16 ? 8 : 4);
		readback.bytes = QByteArray(static_cast<const char*>(m_readback.memory.mapped) + readbackOffsets[size_t(i)], bytes);
		result.readbacks.append(readback);
	}
	cleanUp();
	result.success = true;
	return result;
}

} // namespace

std::shared_ptr<DeviceThread> createVulkanDevice()
{
	return std::make_shared<VulkanDevice>();
}

} // namespace vibestudio::render_detail
