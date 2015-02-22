#pragma once

#include <windows.h>
#include <type_traits>
#include <utility>
#include <functional>

/** @file win32.hpp
 *
 * Convenience functions for accessing Win32 API
 */
namespace win32 {
    /**
     * Waits up to the given number of milliseconds before executing the passed function,
     * at least until the next iteration of the message loop but possibly forever
     *
     * A message loop needs to be running.
     */
    void call_soon(const std::function<void()>& action, unsigned milliseconds);

    class stdcall_thunk_imp {
        void *m_thunk = nullptr;

    public:
        stdcall_thunk_imp(const stdcall_thunk_imp& other) = delete;
        stdcall_thunk_imp& operator=(const stdcall_thunk_imp& other) = delete;

        stdcall_thunk_imp(void *target, uintptr_t value);
        ~stdcall_thunk_imp();

        inline void *thunk() { return m_thunk; }
    };

    template <typename... DummyArgs>
    class stdcall_thunk;

    template <typename TReturn, typename TOrigArg0, typename TTargetArg0, typename... OtherArgs>
    class stdcall_thunk<TReturn(TTargetArg0, OtherArgs...), TReturn(TOrigArg0, OtherArgs...)>
    {
        static_assert(sizeof(TOrigArg0) == sizeof(void*), "The replaced argument must be pointer-sized");
        static_assert(sizeof(TTargetArg0) == sizeof(void*), "The replacement-argument must be pointer-sized");

        typedef TReturn(__stdcall *target_t)(TTargetArg0, OtherArgs...);
        typedef TReturn(__stdcall *source_t)(TOrigArg0, OtherArgs...);

        stdcall_thunk_imp m_impl;

    public:
        inline stdcall_thunk(target_t target, TTargetArg0 arg0)
        : m_impl(reinterpret_cast<void*>(target), reinterpret_cast<uintptr_t>(arg0))
        {}

        inline source_t thunk()
        {
            return reinterpret_cast<source_t>(m_impl.thunk());
        }
    };

    /**
     * Wrapper around a raw win32 window
     */
    class window {
        HWND m_hwnd = 0;
        ATOM m_class = 0;
        enum { DESTROY_NOT_ALLOWED, DESTROY_ALLOWED, DESTROYING } m_destroyState = DESTROY_NOT_ALLOWED;

        stdcall_thunk<LRESULT(window *,UINT,WPARAM,LPARAM), LRESULT(HWND,UINT,WPARAM,LPARAM)> m_wndprocThunk;

    public:
        /**
         * Creates a new window with the specified properties
         *
         * @sa ::CreateWindowEx, WNDCLASSEX
         */
        window(UINT classStyle = 0,
               DWORD style = WS_OVERLAPPEDWINDOW,
               DWORD exStyle = 0,
               HWND  parent = NULL,
               const wchar_t *windowName = nullptr,
               int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int w = CW_USEDEFAULT, int h = CW_USEDEFAULT,
               HICON   icon = 0,
               HCURSOR cursor = LoadCursor(NULL, IDC_ARROW),
               HBRUSH  background = 0,
               HMENU   menu = 0);

        /**
         * Deletes the window.
         *
         * The window will still be on the screen while the overridden destructor is run,
         * and your handleMessage method will not receive the WM_DESTROY message.
         *
         * This is not true if you created the window using make_destroyable(), then your
         * destructor can either be called the "classical" way (before the window is destroyed),
         * or it may be called after the window is long gone, in this case you will have
         * received WM_DESTROY and WM_NCDESTROY messages. Plan accordingly.
         */
        virtual ~window();

        inline HWND hwnd() { return m_hwnd; }

        inline HINSTANCE hinstance() { return (HINSTANCE)GetWindowLongPtr(m_hwnd, GWLP_HINSTANCE); }

        /**
         * Instantiates a win32::window subclass which can either be destroyed using DestroyWindow
         * or deleted using the standard delete operator.
         */
        template <class T, typename... Args>
        static inline T* make_destroyable(Args&&... args)
        {
            static_assert(std::is_base_of<win32::window, T>::value, "make_destroyable only works for win32::window subclasses!");

            T* destroyable = new T(std::forward<Args>(args)...);
            destroyable->m_destroyState = DESTROY_ALLOWED;

            return destroyable;
        }

    protected:
        /**
         * Basically, this is the WndProc for the window.
         *
         * NOTICE: You will NOT receive WM_CREATE or WM_DESTROY messages here, because the window
         * is created before your subclass is constructed and destroyed after the subclass has already
         * been destructed. Use the constructor and destructor to setup your window.
         *
         * If you created the window with make_destroyable<>, you will receive the WM_DESTROY message.
         * After that, the window handle may be invalid, so plan accordingly.
         */
        virtual LRESULT handleMessage(UINT msgid, WPARAM wp, LPARAM lp);

    private:
        static LRESULT CALLBACK WndProc(window *self, UINT msgid, WPARAM wp, LPARAM lp);
    };

    static inline HINSTANCE get_running_instance(void)
    {
        HMODULE ret;
        static int dummy = 42;
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<wchar_t*>(&dummy), &ret))
            return (HINSTANCE)ret;

        return NULL;
    }
};