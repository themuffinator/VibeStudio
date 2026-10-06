#pragma once

#include "core/model_work.h"

#include <cstddef>

namespace vibestudio::model_atlas_internal
{
struct Cancelled
{
};
struct Allocation;
class AllocationRegion
{
  public:
	AllocationRegion(size_t limit, const ModelWorkControl &control);
	~AllocationRegion();
	AllocationRegion(const AllocationRegion &) = delete;
	AllocationRegion &operator=(const AllocationRegion &) = delete;
	[[nodiscard]] size_t peakBytes() const { return m_peak; }
	static void *reallocate(void *pointer, size_t size);
	static void release(void *pointer);

  private:
	AllocationRegion *m_previous = nullptr;
	Allocation *m_first = nullptr;
	size_t m_limit, m_used = 0, m_peak = 0, m_calls = 0;
	const ModelWorkControl &m_control;
};
} // namespace vibestudio::model_atlas_internal
