#include "util.hpp"
#include "renderer.hpp"
#include "duplication_source.hpp"
#include "seven_dwm_source.hpp"
#include "logger.hpp"
#include "win32.hpp"

#define WM_APP_RESIZE    (WM_APP + 1)
#define WM_APP_QUIT      (WM_APP + 2)
#define WM_APP_SETSCREEN (WM_APP + 3)

template <class TSource>
class RenderThread {
    DWORD   m_threadId = static_cast<DWORD>(-1);
    HANDLE  m_threadHandle = NULL;

    // these are accessed by the created thread using the Interlocked* function family
    volatile LONG m_hwnd = 0;
    volatile LONG m_w = 0;
    volatile LONG m_h = 0;
    volatile LONG m_x = 0;
    volatile LONG m_y = 0;

public:
    RenderThread(HWND hwnd, int x, int y, int w, int h)
    {
        InterlockedExchange(&m_hwnd, reinterpret_cast<LONG>(hwnd));
        InterlockedExchange(&m_x, x);
        InterlockedExchange(&m_y, y);
        InterlockedExchange(&m_w, w);
        InterlockedExchange(&m_h, h);

        m_threadHandle = CreateThread(nullptr, 0, &RenderThread::threadProc, reinterpret_cast<void*>(this), 0, &m_threadId);
        if (!m_threadHandle) {
            logger << "FAILED: CreateThread: " << GetLastError() << std::endl;
        }
    }

    ~RenderThread()
    {
        PostThreadMessage(m_threadId, WM_APP_QUIT, 0, 0);

        WaitForSingleObject(m_threadHandle, INFINITE);
        CloseHandle(m_threadHandle);
    }

    void sendResize()
    {
        PostThreadMessage(m_threadId, WM_APP_RESIZE, 0, 0);
    }

    void sendNewScreen(int x, int y, int w, int h)
    {
        InterlockedExchange(&m_x, x);
        InterlockedExchange(&m_y, y);
        InterlockedExchange(&m_w, w);
        InterlockedExchange(&m_h, h);

        PostThreadMessage(m_threadId, WM_APP_SETSCREEN, 0, 0);

        logger << "Posted WM_APP_SETSCREEN x="<<x<<" y="<<y<<" w="<<w<<" h="<<h << std::endl;
    }

private:
    static CALLBACK DWORD threadProc(void *param)
    {
        RenderThread *owner = static_cast<RenderThread*>(param);

        Renderer<TSource> renderer(
            reinterpret_cast<HWND>(InterlockedExchangeAdd(&owner->m_hwnd, 0)),
            static_cast<int>(InterlockedExchangeAdd(&owner->m_x, 0)),
            static_cast<int>(InterlockedExchangeAdd(&owner->m_y, 0)),
            static_cast<int>(InterlockedExchangeAdd(&owner->m_w, 0)),
            static_cast<int>(InterlockedExchangeAdd(&owner->m_h, 0)));

        MSG msg;
        memset(&msg, 0, sizeof(msg));

        uint64_t last = util::milliseconds_now();

        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_APP_QUIT) {
                    PostQuitMessage(0);
                } else if (msg.message == WM_APP_RESIZE) {
                    RECT cr;
                    GetClientRect(reinterpret_cast<HWND>(InterlockedExchangeAdd(&owner->m_hwnd, 0)), &cr);

                    renderer.resize(cr);
                } else if (msg.message == WM_APP_SETSCREEN) {
                    renderer.reset(
                        static_cast<int>(InterlockedExchangeAdd(&owner->m_x, 0)),
                        static_cast<int>(InterlockedExchangeAdd(&owner->m_y, 0)),
                        static_cast<int>(InterlockedExchangeAdd(&owner->m_w, 0)),
                        static_cast<int>(InterlockedExchangeAdd(&owner->m_h, 0))
                    );
                } else {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            } else {
                renderer.render();

                // As a safety net against broken vsync, we cap the FPS at 100
                uint64_t previous = last;
                uint64_t now = last = util::milliseconds_now();
                if ((now - previous) < 10)
                    Sleep(10 - static_cast<DWORD>(now - previous));

                //TODO: display fps?
                //logger << "FPS: " << 1000.0f/static_cast<float>(now - previous) << std::endl;
            }
        }

        return 0;
    }
};

namespace ViewWindow {
    template<typename TSource>
    inline HWND create(HWND parent, int x, int y, int w, int h);

    // Manages probably everything about the view.
    // Will delete itself once the window is destroyed.
    template<class TSource>
    class Impl : public win32::window {
        RenderThread<TSource> m_renderer;

    public:
        Impl(HWND parent, int x, int y, int w, int h) :
        win32::window(CS_HREDRAW | CS_VREDRAW,
                      WS_CHILD,
                      0,
                      parent,
                      L"ScreenView View Window",
                      0, 0, 10, 10),
        m_renderer(hwnd(), x, y, w, h)
        {};
        ~Impl()
        {
        };

    protected:
        LRESULT handleMessage(UINT msgid, WPARAM wp, LPARAM lp) override
        {
            if (msgid == WM_SIZE) {
                m_renderer.sendResize();
            } else if (msgid == WM_APP_SETSCREEN) {
                int *xywh = reinterpret_cast<int*>(wp);

                m_renderer.sendNewScreen(xywh[0], xywh[1], xywh[2], xywh[3]);
            }

            return win32::window::handleMessage(msgid, wp, lp);
        }
    };

    template<typename TSource>
    inline HWND create(HWND parent, int x, int y, int w, int h)
    {
        return win32::window::make_destroyable<Impl<TSource>>(parent, x, y, w, h)->hwnd();
    }

    inline void setScreen(HWND view, int x, int y, int w, int h)
    {
        int xywh[4] = { x, y, w, h };

        SendMessage(view, WM_APP_SETSCREEN, reinterpret_cast<WPARAM>(&xywh), 0);
    }
};

//////////////////////////////////////////////////////////////////////////////
// Exported API
//////////////////////////////////////////////////////////////////////////////
EXPORT HWND SV_CreateView(HWND parent, int x, int y, int w, int h)
{
    if (util::check_windows_version(6, 2))
        return ViewWindow::create<DuplicationSource>(parent, x, y, w, h);
    else if (util::check_windows_version<std::equal_to<DWORD>>(6, 1))
        return ViewWindow::create<SevenDwmSource>(parent, x, y, w, h);
    else
        return NULL;
}

EXPORT void SV_ChangeScreen(HWND view, int x, int y, int w, int h)
{
    ViewWindow::setScreen(view, x, y, w, h);
}