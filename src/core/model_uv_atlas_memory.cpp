#include "core/model_uv_atlas_memory.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <xatlas.h>

namespace vibestudio::model_atlas_internal
{
struct alignas(std::max_align_t) Allocation
{
	Allocation *previous, *next;
	size_t size;
	AllocationRegion *owner;
};
namespace
{
thread_local AllocationRegion *current = nullptr;
std::once_flag callbacks;
} // namespace
AllocationRegion::AllocationRegion(size_t limit, const ModelWorkControl &control) : m_limit(limit), m_control(control)
{
	std::call_once(callbacks,
				   []
				   {
					   xatlas::SetAlloc(&AllocationRegion::reallocate, &AllocationRegion::release);
					   xatlas::SetPrint(nullptr, false);
				   });
	m_previous = current;
	current = this;
}
AllocationRegion::~AllocationRegion()
{
	// Upstream uses raw allocator-owned blocks, including partially constructed
	// objects. After an exception, walking this region avoids incomplete graphs.
	while (m_first)
	{
		auto *next = m_first->next;
		std::free(m_first);
		m_first = next;
	}
	current = m_previous;
}
void AllocationRegion::release(void *pointer)
{
	if (!pointer)
	{
		return;
	}
	auto *block = static_cast<Allocation *>(pointer) - 1;
	auto *owner = block->owner;
	if (block->previous)
	{
		block->previous->next = block->next;
	}
	else
	{
		owner->m_first = block->next;
	}
	if (block->next)
	{
		block->next->previous = block->previous;
	}
	owner->m_used -= block->size + sizeof(Allocation);
	std::free(block);
}
void *AllocationRegion::reallocate(void *pointer, size_t size)
{
	if (size == 0)
	{
		release(pointer);
		return nullptr;
	}
	auto *region = current;
	if (!region)
	{
		throw std::bad_alloc();
	}
	if ((++region->m_calls & 255) == 0 && region->m_control.cancelled && region->m_control.cancelled())
	{
		throw Cancelled{};
	}
	auto *old = pointer ? static_cast<Allocation *>(pointer) - 1 : nullptr;
	// Count both blocks during realloc; old bytes remain valid until copying ends.
	if (size > region->m_limit || sizeof(Allocation) > region->m_limit - size ||
		region->m_used > region->m_limit - size - sizeof(Allocation))
	{
		throw std::bad_alloc();
	}
	auto *block = static_cast<Allocation *>(std::malloc(sizeof(Allocation) + size));
	if (!block)
	{
		throw std::bad_alloc();
	}
	*block = {nullptr, region->m_first, size, region};
	if (region->m_first)
	{
		region->m_first->previous = block;
	}
	region->m_first = block;
	region->m_used += size + sizeof(Allocation);
	region->m_peak = std::max(region->m_peak, region->m_used);
	if (old)
	{
		std::memcpy(block + 1, pointer, std::min(size, old->size));
		release(pointer);
	}
	return block + 1;
}
} // namespace vibestudio::model_atlas_internal
