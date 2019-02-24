#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

// A noncopyable fixed-size alternative to std::function.
template<typename SIGNATURE, size_t MaxSize = 48>
class FixedFunction;

template<typename R, size_t MaxSize, typename ...Args>
class FixedFunction<R(Args...), MaxSize> {
public:
    // Initializes an unitialized function.
    FixedFunction();
    // Initializes a function from functor.
    template<typename Functor>
    FixedFunction(Functor&& functor);
    // Initializes a function from a function ptr.
    FixedFunction(R (*func)(Args... args));

    // FixedFunction is move-only. The other function becomes uninitialized.
    FixedFunction(FixedFunction&& other);
    FixedFunction& operator=(FixedFunction&& other);
    ~FixedFunction();

    R operator()(Args... args);

    // Returns true if function is initialized.
    operator bool() const;

private:
    // Maximum natural alignment (for SIMD types).
    static size_t const kAlignment = 16;

    // The main function pointer, which will be called in operator(), first argument is always the storage.
    using FuncPtr = R(*)(char*, Args... args);
    // Either calls a move-constructor or a destructor. depending on the first parameter. Must always be non-null,
    // which removes a couple of branches `if (movePtr)`. Making one pointer instead of two reduces the class size
    // while not affecting the perf on modern CPUs with good branch predictors.
    using MovePtr = void(*)(char*, char*);

    alignas(kAlignment) char storage[MaxSize];
    FuncPtr funcPtr = nullptr;
    MovePtr movePtr = defaultMovePtr;

    template<typename Functor>
    static R funcPtrFromFunctor(char* storage, Args... args);
    static R funcPtrFromPtr(char* storage, Args... args);
    template<typename Functor>
    static void movePtrFromFunctor(char* dstStorage, char* srcStorage);
    static void movePtrFromPtr(char* dstStorage, char* srcStorage);

    static void defaultMovePtr(char* dstStorage, char* srcStorage);
};

template<typename R, size_t MaxSize, typename ...Args>
FixedFunction<R(Args...), MaxSize>::FixedFunction()
{
}

template<typename R, size_t MaxSize, typename ...Args>
template<typename Functor>
FixedFunction<R(Args...), MaxSize>::FixedFunction(Functor&& functor)
{
    using RealFunctor = typename std::remove_reference<Functor>::type;
    static_assert(sizeof(RealFunctor) <= sizeof(storage), "Passed functor is too big");
    funcPtr = funcPtrFromFunctor<RealFunctor>;
    movePtr = movePtrFromFunctor<RealFunctor>;
    new(storage) RealFunctor(std::forward<RealFunctor>(functor));
}

template<typename R, size_t MaxSize, typename ...Args>
FixedFunction<R(Args...), MaxSize>::FixedFunction(R (*func)(Args... args))
{
    using FuncType = R(*)(Args...);
    funcPtr = funcPtrFromPtr;
    movePtr = movePtrFromPtr;
    new(storage) FuncType(func);
}

template<typename R, size_t MaxSize, typename ...Args>
FixedFunction<R(Args...), MaxSize>::FixedFunction(FixedFunction&& other)
    : funcPtr(other.funcPtr)
    , movePtr(other.movePtr)
{
    movePtr(storage, other.storage);
    other.funcPtr = nullptr;
}

template<typename R, size_t MaxSize, typename ...Args>
FixedFunction<R(Args...), MaxSize>& FixedFunction<R(Args...), MaxSize>::operator=(FixedFunction&& other)
{
    movePtr(nullptr, storage);
    funcPtr = other.funcPtr;
    movePtr = other.movePtr;
    movePtr(storage, other.storage);
    other.funcPtr = nullptr;
    return *this;
}

template<typename R, size_t MaxSize, typename ...Args>
FixedFunction<R(Args...), MaxSize>::~FixedFunction()
{
    movePtr(nullptr, storage);
}

template<typename R, size_t MaxSize, typename ...Args>
R FixedFunction<R(Args...), MaxSize>::operator()(Args... args)
{
    return funcPtr(storage, args...);
}

template<typename R, size_t MaxSize, typename ...Args>
FixedFunction<R(Args...), MaxSize>::operator bool() const
{
    return funcPtr != nullptr;
}

template<typename R, size_t MaxSize, typename ...Args>
template<typename Functor>
R FixedFunction<R(Args...), MaxSize>::funcPtrFromFunctor(char* storage, Args... args)
{
    return (*((Functor*)storage))(args...);
}

template<typename R, size_t MaxSize, typename ...Args>
R FixedFunction<R(Args...), MaxSize>::funcPtrFromPtr(char* storage, Args... args)
{
    using FuncType = R(*)(Args...);
    FuncType func = *(FuncType*)storage;
    return func(args...);
}

template<typename R, size_t MaxSize, typename ...Args>
template<typename Functor>
void FixedFunction<R(Args...), MaxSize>::movePtrFromFunctor(char* dstStorage, char* srcStorage)
{
    if (dstStorage) {
        new(dstStorage) Functor(std::move(*((Functor*)srcStorage)));
    } else {
        ((Functor*)srcStorage)->~Functor();
    }
}

template<typename R, size_t MaxSize, typename ...Args>
void FixedFunction<R(Args...), MaxSize>::movePtrFromPtr(char* dstStorage, char* srcStorage)
{
    using FuncType = R(*)(Args...);
    if (dstStorage) {
        new(dstStorage) FuncType(*((FuncType*)srcStorage));
    }
}

template<typename R, size_t MaxSize, typename ...Args>
void FixedFunction<R(Args...), MaxSize>::defaultMovePtr(char*, char*)
{
}
