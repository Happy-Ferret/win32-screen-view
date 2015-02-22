#include "win32.hpp"
#include "logger.hpp"

#include <cstdio>
#include <cwchar>
#include <cinttypes>

namespace {
    class deferred_action : public win32::window
    {
        std::function<void()> m_action;

    public:
        deferred_action(const std::function<void()>& action, unsigned milliseconds)
          : win32::window(0, 0, 0, HWND_MESSAGE),
            m_action(action)
        {
            SetTimer(hwnd(), 0, (UINT)milliseconds, nullptr);
        }

    protected:
        LRESULT handleMessage(UINT msgid, WPARAM wp, LPARAM lp) override
        {
            if (msgid == WM_TIMER) {
                m_action();

                delete this;

                return TRUE;
            }

            return win32::window::handleMessage(msgid, wp, lp);
        }
    };

    class thunk_heap_t
    {
        HANDLE m_heap = 0;

    public:
        thunk_heap_t()
        {
            m_heap = ::HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0);
        }

        ~thunk_heap_t()
        {
            ::HeapDestroy(m_heap);
        }

        void *alloc(std::size_t size)
        {
            return ::HeapAlloc(m_heap, 0, size);
        }

        template <typename T>
        T *alloc()
        {
            return reinterpret_cast<T*>(alloc(sizeof(T)));
        }

        template <typename T>
        void free(T *ptr)
        {
            ::HeapFree(m_heap, 0, reinterpret_cast<void*>(ptr));
        }
    };
    static thunk_heap_t thunk_heap;
}

#ifdef _M_IX86
namespace {
#   pragma pack(push,1)
    struct stdcallthunk
    {
        uint32_t   m_mov;          // mov dword ptr [esp+0x4], pThis (esp+0x4 is original arg0)
        uint32_t   m_this;         //
        uint8_t    m_jmp;          // jmp
        uint32_t   m_relproc;      // <relative address>
    };
#   pragma pack(pop)
}

win32::stdcall_thunk_imp::stdcall_thunk_imp(void* proc, uintptr_t target)
{
    struct stdcallthunk *thunk = thunk_heap.alloc<struct stdcallthunk>();
    if (!thunk)
        return;

    thunk->m_mov = 0x042444C7;
    thunk->m_this = reinterpret_cast<uint32_t>(target);
    thunk->m_jmp = 0xE9; // jmp
    thunk->m_relproc = reinterpret_cast<uint32_t>((uintptr_t)proc - ((uintptr_t)thunk + sizeof(struct stdcallthunk)));

    // All processors need to see this
    ::FlushInstructionCache(GetCurrentProcess(), thunk, sizeof(struct stdcallthunk));

    m_thunk = reinterpret_cast<void*>(thunk);
}

win32::stdcall_thunk_imp::~stdcall_thunk_imp()
{
    thunk_heap.free(m_thunk);
}

#else
#   error "Thunking supported on i386 only (at least for now...)"
#endif

void win32::call_soon(const std::function< void() >& action, unsigned int milliseconds)
{
    // BAD HACK: We create a window which installs a timer and destroys itself once the timer
    //           has elapsed. And that all because a windows timer can't take a parameter :(
    new deferred_action(action, milliseconds); // will clean up after itself automatically
}


win32::window::window(UINT classStyle,
                      DWORD style,
                      DWORD exStyle,
                      HWND parent,
                      const wchar_t* windowName,
                      int x, int y, int w, int h,
                      HICON icon,
                      HCURSOR cursor,
                      HBRUSH background,
                      HMENU menu)
: m_wndprocThunk(&win32::window::WndProc, this)
{
    wchar_t generatedClassName[32]; // also a fallback for the window name

    std::swprintf(generatedClassName, sizeof(generatedClassName), L"Win32mm_Window_%016" PRIXPTR, (uintptr_t)this);

    WNDCLASSEX wndclass = {
        .cbSize = sizeof(WNDCLASSEX),
        .style = classStyle,
        .lpfnWndProc = &::DefWindowProc, // we cannot set the thunk here, because m_hwnd is not defined yet
        .cbClsExtra = 0,
        .cbWndExtra = 8, // 64bit this pointer
        .hInstance = get_running_instance(),
        .hIcon = icon,
        .hCursor = cursor,
        .hbrBackground = background,
        .lpszMenuName = nullptr,
        .lpszClassName = generatedClassName,
        .hIconSm = 0
    };

    m_class = RegisterClassEx(&wndclass);
    if (!m_class) {
        // TODO: make error value accessible for child classes
        logger << "FAILED: RegisterClassEx: " << GetLastError() << std::endl;
        return;
    }

    m_hwnd = CreateWindowEx(exStyle,
                            MAKEINTATOM(m_class),
                            windowName,
                            style,
                            x,y,w,h,
                            parent,
                            menu,
                            get_running_instance(),
                            nullptr);

    //TODO: make error value accessible for child classes
    if (!m_hwnd)
        logger << "FAILED: CreateWindowEx: " << GetLastError() << std::endl;
    else
        SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_wndprocThunk.thunk()));
}

win32::window::~window()
{
    // If we didn't already destroy it, do that now!
    if (m_destroyState != DESTROYING) {
        m_destroyState = DESTROYING;
        DestroyWindow(m_hwnd);
    }

    if (!UnregisterClass(MAKEINTATOM(m_class), get_running_instance()))
        logger << "FAILED: UnregisterClass: " << GetLastError() << std::endl;
}

LRESULT win32::window::handleMessage(UINT msgid, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(m_hwnd, msgid, wp, lp);
}

LRESULT CALLBACK win32::window::WndProc(win32::window *self, UINT msgid, WPARAM wp, LPARAM lp)
{
    if (msgid == WM_DESTROY) {
        if (self->m_destroyState == DESTROY_NOT_ALLOWED) {
            logger << "Illegally received WM_DESTROY outside of the win32::window destructor. The class is corrupted now." << std::endl;
            std::terminate();
        } else if (self->m_destroyState == DESTROY_ALLOWED) {
            win32::call_soon([=]() {
                self->m_destroyState = DESTROYING;
                delete self;
            }, 0);
        }
    }

    return self->handleMessage(msgid, wp, lp);
}

