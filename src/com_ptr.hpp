#pragma once

#include <unknwn.h>
#include <type_traits>
#include <utility>

/**
 * A wrapper for a raw COM pointer trying to provide memory safety
 *
 * The com_ptr<T> template provides a smart pointer for a COM interface pointer.
 * It allows to do memory management in a RAII way while retaining (most of) the
 * raw pointer convenience. It tries to act similar to a std::shared_ptr, and
 * strives to make common things work intuitively, and unintuitive things explicit.
 */
template <class T>
class com_ptr {
    T* m_ptr = nullptr;

#ifndef CINTERFACE
    static_assert(std::is_base_of<IUnknown, T>::value, "The COM interface must inherit from IUnknown!");
#endif

    inline void ref()
    {
        if (!m_ptr)
            return;

#ifdef CINTERFACE
        m_ptr->lpVtbl->AddRef(m_ptr);
#else
        m_ptr->AddRef();
#endif
    }

    // You should usually prefer clear()
    inline void release()
    {
        if (!m_ptr)
            return;

#ifdef CINTERFACE
        m_ptr->lpVtbl->Release(m_ptr);
#else
        m_ptr->Release();
#endif
    }

public:
    /**
     * Constructs an empty instance (containing a null pointer)
     *
     * Constructing from a raw pointer is not defined on purpose, because
     * a com_ptr instance casts itself to a raw pointer at the slightest provocation,
     * and stuffing this pointer into a new com_ptr instance by another implicit conversion
     * is paving the way for a double release and great debugging pain.
     *
     * Use com_ptr::ref and com_ptr::take to wrap a dumb pointer.
     */
    inline com_ptr() = default;

    /**
     * Copy constructor: Copy the pointer and create a new reference
     */
    inline com_ptr(const com_ptr<T>& other) : m_ptr(other.m_ptr)
    {
        ref();
    }

    /**
     * Move constructor: Transfer the reference (clearing the old smart pointer)
     */
    inline com_ptr(com_ptr<T>&& other)
    {
        std::swap(m_ptr, other.m_ptr);
    }

    /**
     * Upcasting: Create base class smart pointer from child smart pointer (copy)
     */
    template<class Tsub, class = typename std::enable_if<std::is_base_of<T, Tsub>::value>::type>
    inline com_ptr(const com_ptr<Tsub>& other) : m_ptr(other.m_ptr)
    {
        ref();
    }

    /**
     * Upcasting: Create base class smart pointer from child smart pointer (move)
     */
    template<class Tsub, class = typename std::enable_if<std::is_base_of<T, Tsub>::value>::type>
    com_ptr(com_ptr<Tsub>&& other)
    {
        std::swap(m_ptr, other.m_ptr);
    }

    com_ptr<T>& operator=(com_ptr<T> other)
    {
        std::swap(m_ptr, other.m_ptr);

        return *this;
    }

    /**
     * Upcasting assignment
     */
    template<class Tsub, class = typename std::enable_if<std::is_base_of<T, Tsub>::value>::type>
    com_ptr<T>& operator=(com_ptr<Tsub> other)
    {
        std::swap(m_ptr, other.m_ptr);

        return *this;
    }

    /**
     * Check whether a valid pointer is stored in this smart pointer
     *
     * @returns true if there is a valid pointer, false if there is a null pointer
     */
    explicit operator bool() const
    {
        return m_ptr != nullptr;
    }

    /**
     * Silently convert to the wrapped pointer
     */
    operator T*()
    {
        return m_ptr;
    }

    /**
     * Dereference operator, obtain a reference to the object
     *
     * This is not really useful, but present to behave like a good smart pointer
     */
    T& operator*()
    {
        return *m_ptr;
    }

    T* operator->()
    {
        return m_ptr;
    }

    /**
     * Access the pointer saved in this instance
     */
    T* ptr()
    {
        return m_ptr;
    }

    /**
     * WARNING: Dangerous, may crash horribly if the type is misinterpreted.
     *
     * This is there to call a function like SomeComFunction([in] REFIID uuid, [in] void* dumb)
     * by using SomeComFunction(smart.uuid(), smart.ptr_as_void())
     */
    void* ptr_as_void()
    {
        return reinterpret_cast<void*>(m_ptr);
    }

    /**
     * A pointer to the pointer saved in this instance.
     *
     * Allows you to call a function like SomeComFunc([in] int numElems, [in] T* const * array)
     * by using SomeComFunc(1, smart.pptr())
     */
    T* const * pptr()
    {
        return &m_ptr;
    }


    /**
     * Possibly DANGEROUS convenience: Pass a smart com_ptr as out parameter to a com function
     *
     * *smart.pptr_cleared() = dumb;
     *
     * is equivalent to
     *
     * smart = com_ptr::take(dumb);
     *
     * DANGEROUS: Do NOT mess with the pointer saved in the com_ptr template.
     * This function is only present to so that you can call SomeComFunc([out] T** dumb)
     * by using SomeComFunc(smart.pptr_cleared());
     *
     * Doing ONE assignment to *pptr_cleared() is perfectly ok, doing anything else is verboten.
     */
    T** pptr_cleared()
    {
        clear();

        return &m_ptr;
    }

    /**
     * A pointer to the pointer saved in this instance, passed as void*
     *
     * Allows you to call a function like SomeConFunc([in] REFIID uuid, [in] int numElems, [in] T* const* array)
     * by using SomeComFunc(smart.uuid(), 1, smart.pptr())
     */
    void* const * pptr_as_void()
    {
        return reinterpret_cast<void**>(pptr());
    }

    /**
     * Possibly DANGEROUS convenience: Pass a smart com_ptr as out parameter to a com function expecting void**
     *
     * Use it like pptr_cleared() to call SomeComFunc([in] REFIID uuid, [out] void** dumb)
     * with SomeComFunc(smart.uuid(), smart.pptr_as_void_cleared())
     *
     * See also: com_ptr::pptr_cleared()
     */
    void** pptr_as_void_cleared()
    {
        return reinterpret_cast<void**>(pptr_cleared());
    }

    /**
     * Retrieves a pointer to the supported interface T2 on the object wrapped by this com_ptr instance
     * as a new com_ptr smart pointer template.
     *
     * If the interface cannot be instanced, the returned com_ptr will carry a null pointer
     */
    template<typename T2> com_ptr<T2> query()
    {
        if (!m_ptr)
            return com_ptr<T2>::take(nullptr);

        T2 *newptr = nullptr;;
#ifdef CINTERFACE
        m_ptr->lpVtbl->QueryInterface(m_ptr, com_ptr<T2>::uuid(), reinterpret_cast<void**>(&newptr));
#else
        m_ptr->QueryInterface(com_ptr<T2>::uuid(), reinterpret_cast<void**>(&newptr));
#endif

        return com_ptr<T2>::take(newptr);
    }

    /**
     * Retrieves a pointer to the supported interface T2 on the object wrapped by this com_ptr instance
     * as a new com_ptr smart pointer template.
     *
     * If the interface could be retrieved, out& will countain a pointer to it and true will be returned.
     * Otherwise, false will be returned and out& will be left untouched.
     */
    template<typename T2> bool query(com_ptr<T2>& out)
    {
        com_ptr<T2> tmp = query<T2>();

        if (tmp) {
            out = std::move(tmp);

            return true;
        }

        return false;
    }

    static REFIID uuid()
    {
        return __uuidof(T);
    }

    /**
     * Fill this smart pointer with a new pointer, adopting the original reference.
     *
     * If the smart pointer is already occupied, the previous object will be released.
     */
    /*void take(T* dumb)
    {
        clear();
        m_ptr = dumb;
    }*/

    /**
     * Wrap a dumb pointer into a com_ptr, adopting the original reference
     */
    static com_ptr<T> take(T *dumb)
    {
        com_ptr<T> smart;

        smart.m_ptr = dumb;

        return smart;
    }

    /*void ref(T *dumb)
    {
        clear();
        m_ptr = dumb;
    }*/

    /**
     * Wrap a dumb pointer into a com_ptr, creating a new reference
     */
    static com_ptr<T> ref(T *dumb)
    {
        if (dumb) {
#ifdef CINTERFACE
            dumb->lpVtbl->AddRef(dumb);
#else
            dumb->AddRef();
#endif

            return com_ptr::take(dumb);
        }

        return com_ptr<T>();
    }

    /**
     * Releases the interface saved into this com_ptr smart pointer template
     * and sets the pointer to NULL
     */
    void clear() {
        release();

        m_ptr = nullptr;
    }

    ~com_ptr() {
        clear();
    }
};