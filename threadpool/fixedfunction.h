#pragma once

#include <type_traits>
#include <utility>

// A move-only alternative to std::function. Allows specifying the size of the object and whether
// to allocate on heap if the passed lambda is larger than MaxSize.
template<typename SIGNATURE, size_t MaxSize = 48, bool AllocOnOverflow = true>
class FixedFunction;

template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
class FixedFunction<R(Args...), MaxSize, AllocOnOverflow> {
public:
    // Initializes an unitialized function.
    FixedFunction();
    // Initializes a function from functor.
    template<typename Functor>
    FixedFunction(Functor&& functor);
    // Initializes a function from a function ptr.
    FixedFunction(R (&func)(Args... args));

    // FixedFunction is move-only. The other function becomes uninitialized.
    FixedFunction(FixedFunction&& other);
    FixedFunction& operator=(FixedFunction&& other);

    ~FixedFunction();

    // Calls the stored function pointer/functor object.
    R operator()(Args... args);

    // Returns true if function has not been initialized, false otherwise.
    bool empty() const;

private:
    // Maximum natural alignment (for SIMD types).
    static size_t const kAlignment = 16;

    // The main function pointer, which will be called in operator(), first argument is always
    // the storage.
    using FuncPtr = R(*)(char*, Args... args);
    // Either calls a move-constructor or a destructor. depending on the first parameter.
    // Must always be non-null, which removes a couple of branches `if (movePtr)`.
    // Making one pointer instead of two reduces the class size while not affecting the perf
    // too much on modern CPUs with good branch predictors.
    using MovePtr = void(*)(char*, char*);

    // Storage can contain one of:
    //  1) function pointer (if initialized from function pointer)
    //  2) moved functor object/lambda (if initialized from functor object/lambda
    //     and size <= MaxSize)
    //  3) pointer to the moved functor object/lambda.
    alignas(kAlignment) char storage[MaxSize];
    FuncPtr funcPtr;
    MovePtr movePtr;
};


namespace detail {
    template<typename Functor, typename R, typename ...Args>
    R funcPtrFromFunctor(char* storage, Args... args);
    // A version of funcPtrFromFunctor which stores the pointer in first storage bytes.
    template<typename Functor, typename R, typename ...Args>
    R funcPtrFromFunctorOverflow(char* storage, Args... args);
    template<typename R, typename ...Args>
    R funcPtrFromPtr(char* storage, Args... args);
    template<typename Functor>
    void movePtrFromFunctor(char* dstStorage, char* srcStorage);
    // A version of movePtrFromFunctor which stores the pointer in first storage bytes.
    template<typename Functor>
    void movePtrFromFunctorOverflow(char* dstStorage, char* srcStorage);
    template<typename R, typename ...Args>
    void movePtrFromPtr(char* dstStorage, char* srcStorage);

    void defaultMovePtr(char* dstStorage, char* srcStorage);
} // namespace detail


template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
FixedFunction<R(Args...), MaxSize, AllocOnOverflow>::FixedFunction()
    : funcPtr(nullptr)
    , movePtr(detail::defaultMovePtr)
{
}

template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
template<typename Functor>
FixedFunction<R(Args...), MaxSize, AllocOnOverflow>::FixedFunction(Functor&& functor)
{
    using RealFunctor = typename std::remove_reference<Functor>::type;
#if __cplusplus >= 201703
    if constexpr (sizeof(RealFunctor) <= sizeof(storage)) {
#else
    // We basically do not care which of the two versions is used, any compiler worth its salt
    // will optimize this if out. The constexpr version is used to silence the compiler warnings.
    if (sizeof(RealFunctor) <= sizeof(storage)) {
#endif
        funcPtr = detail::funcPtrFromFunctor<RealFunctor, R, Args...>;
        movePtr = detail::movePtrFromFunctor<RealFunctor>;
        new(storage) RealFunctor(std::forward<RealFunctor>(functor));
    } else {
        static_assert(AllocOnOverflow, "Passed functor is too big");
        static_assert(sizeof(char*) <= sizeof(storage), "MaxSize should be at least the size"
                                                        " of function pointer");
        funcPtr = detail::funcPtrFromFunctorOverflow<RealFunctor, R, Args...>;
        movePtr = detail::movePtrFromFunctorOverflow<RealFunctor>;
        // Really-really hope that realStorage is aligned correctly here.
        // NOTE: We could rewrite this by over-allocating new char[sizeof(RealFunctor) + 15],
        // and either storing both the pointer to allocated memory block and the aligned pointer
        // or aligning the pointer on each access. On the other hand all the sane allocators
        // (including tcmalloc and jemalloc) will provide the 16-byte alignment for types larger
        // than 16 bytes anyway.
        char* realStorage = new char[sizeof(RealFunctor)];
        new(realStorage) RealFunctor(std::forward<RealFunctor>(functor));
        new(storage) char*(realStorage);
    }
}

template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
FixedFunction<R(Args...), MaxSize, AllocOnOverflow>::FixedFunction(R (&func)(Args... args))
    : FixedFunction(&func)
{
}

template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
FixedFunction<R(Args...), MaxSize, AllocOnOverflow>::FixedFunction(FixedFunction&& other)
    : funcPtr(other.funcPtr)
    , movePtr(other.movePtr)
{
    movePtr(storage, other.storage);
    other.funcPtr = nullptr;
    other.movePtr = detail::defaultMovePtr;
}

template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
FixedFunction<R(Args...), MaxSize, AllocOnOverflow>&
    FixedFunction<R(Args...), MaxSize, AllocOnOverflow>::operator=(FixedFunction&& other)
{
    movePtr(nullptr, storage);
    funcPtr = other.funcPtr;
    movePtr = other.movePtr;
    movePtr(storage, other.storage);
    other.funcPtr = nullptr;
    other.movePtr = detail::defaultMovePtr;
    return *this;
}

template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
FixedFunction<R(Args...), MaxSize, AllocOnOverflow>::~FixedFunction()
{
    movePtr(nullptr, storage);
}

template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
R FixedFunction<R(Args...), MaxSize, AllocOnOverflow>::operator()(Args... args)
{
    return funcPtr(storage, std::forward<Args>(args)...);
}

template<typename R, size_t MaxSize, bool AllocOnOverflow, typename ...Args>
bool FixedFunction<R(Args...), MaxSize, AllocOnOverflow>::empty() const
{
    return funcPtr == nullptr;
}

namespace detail {

template<typename Functor, typename R, typename ...Args>
R funcPtrFromFunctor(char* storage, Args... args)
{
    return (*((Functor*)storage))(std::forward<Args>(args)...);
}

template<typename Functor, typename R, typename ...Args>
R funcPtrFromFunctorOverflow(char* storage, Args... args)
{
    char* realStorage = *(char**)storage;
    return (*((Functor*)realStorage))(std::forward<Args>(args)...);
}

template<typename R, typename ...Args>
R funcPtrFromPtr(char* storage, Args... args)
{
    using FuncType = R(*)(Args...);
    FuncType func = *(FuncType*)storage;
    return func(std::forward<Args>(args)...);
}

template<typename Functor>
void movePtrFromFunctor(char* dstStorage, char* srcStorage)
{
    if (dstStorage) {
        new(dstStorage) Functor(std::move(*((Functor*)srcStorage)));
    } else {
        ((Functor*)srcStorage)->~Functor();
    }
}

template<typename Functor>
void movePtrFromFunctorOverflow(char* dstStorage, char* srcStorage)
{
    if (dstStorage) {
        *(char**)dstStorage = *(char**)srcStorage;
        *(char**)srcStorage = nullptr;
    } else {
        char* realSrcStorage = *(char**)srcStorage;
        ((Functor*)realSrcStorage)->~Functor();
        delete[] realSrcStorage;
    }
}

template<typename R, typename ...Args>
void movePtrFromPtr(char* dstStorage, char* srcStorage)
{
    using FuncType = R(*)(Args...);
    if (dstStorage) {
        *(FuncType*)dstStorage = *(FuncType*)srcStorage;
    }
}

void defaultMovePtr(char*, char*)
{
}

} // namespace detail
