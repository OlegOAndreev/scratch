#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

// Alloc is a simple general-purpose allocator interface with sized delete:
// class Alloc {
//   void* allocate(size_t size);
//   void deallocate(void* ptr, size_t size);
// };

// Helper methods for doing new/delete with allocator.
template<typename T, typename Alloc, typename ...Args>
inline T* allocNew(Alloc& alloc, Args&&... args)
{
    static_assert(!std::is_abstract<T>::value, "T must be a concrete class");
    void* ptr = alloc.allocate(sizeof(T));
    return new(ptr) T(std::forward<Args>(args)...);
}

template<typename T, typename Alloc>
inline void allocDelete(Alloc& alloc, T* t)
{
    static_assert(!std::is_abstract<T>::value, "T must be a concrete class");
    t->~T();
    alloc.deallocate(t, sizeof(T));
}


// The basic Alloc implementation proxying to new/delete.
struct DefaultAlloc {
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
};

inline void* DefaultAlloc::allocate(size_t size)
{
    return operator new(size);
}

inline void DefaultAlloc::deallocate(void* ptr, size_t /*size*/)
{
    operator delete(ptr);
}


// A simple Alloc implementation.
struct SimpleAlloc {
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
};
