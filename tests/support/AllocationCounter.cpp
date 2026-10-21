// SPDX-License-Identifier: MIT
#include "AllocationCounter.hpp"

#include <atomic>
#include <new>

#include <cstdlib>

namespace
{
constinit std::atomic<std::uint64_t> allocations{0};
constinit std::atomic<std::uint64_t> bytes{0};

void* allocate(std::size_t size)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    bytes.fetch_add(size, std::memory_order_relaxed);
    return std::malloc(size == 0 ? 1 : size);
}

void* allocate_aligned(std::size_t size, std::align_val_t align)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    bytes.fetch_add(size, std::memory_order_relaxed);
#ifdef _MSC_VER
    return _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(align));
#else
    const std::size_t a = static_cast<std::size_t>(align);
    return std::aligned_alloc(a, ((size == 0 ? 1 : size) + a - 1) / a * a);
#endif
}

void release_aligned(void* p) noexcept
{
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* allocate_or_throw(std::size_t size)
{
    if (void* p = allocate(size))
    {
        return p;
    }
    throw std::bad_alloc{};
}

void* allocate_aligned_or_throw(std::size_t size, std::align_val_t align)
{
    if (void* p = allocate_aligned(size, align))
    {
        return p;
    }
    throw std::bad_alloc{};
}
} // namespace

namespace test_support
{
std::uint64_t allocation_count() noexcept
{
    return allocations.load(std::memory_order_relaxed);
}

std::uint64_t allocated_bytes() noexcept
{
    return bytes.load(std::memory_order_relaxed);
}
} // namespace test_support

// NOLINTBEGIN(misc-new-delete-overloads): the full set of replaceable forms
void* operator new(std::size_t size)
{
    return allocate_or_throw(size);
}
void* operator new[](std::size_t size)
{
    return allocate_or_throw(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size);
}
void* operator new(std::size_t size, std::align_val_t align)
{
    return allocate_aligned_or_throw(size, align);
}
void* operator new[](std::size_t size, std::align_val_t align)
{
    return allocate_aligned_or_throw(size, align);
}
void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept
{
    return allocate_aligned(size, align);
}
void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept
{
    return allocate_aligned(size, align);
}

void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    release_aligned(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    release_aligned(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    release_aligned(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    release_aligned(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
    release_aligned(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
    release_aligned(p);
}
// NOLINTEND(misc-new-delete-overloads)
