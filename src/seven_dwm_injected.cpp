#define CINTERFACE
#define COBJMACROS
#define INITGUID

#include "seven_dwm_injected.hpp"
#include "util.hpp"
#include "com_ptr.hpp"
#include "logger.hpp"
#include "com_ptr.hpp"
#include "win32.hpp"

#include <windows.h>
#include <d3d10_1.h>
#include <dxgi.h>

#include "mhook.h"

#include <atomic>

namespace {
    //////////////////////////////////////
    // Global variables for communication
    //////////////////////////////////////
    std::atomic<HWND>   g_host                { 0 };
    std::atomic<LONG>   g_monitorLeft         { 0 };
    std::atomic<LONG>   g_monitorTop          { 0 };
    std::atomic<LONG>   g_monitorRight        { 0 };
    std::atomic<LONG>   g_monitorBottom       { 0 };
    std::atomic<HANDLE> g_sharedTextureHandle { INVALID_HANDLE_VALUE };

    // The capturing thread will check these in every iteration
    std::atomic<IDXGISwapChain*> g_capturedSwapChain { nullptr };
    std::atomic<ID3D10Resource*> g_captureTarget     { nullptr };

    //////////////////////////////////////////////////
    // D3D Stuff
    //////////////////////////////////////////////////

    // will contain the original IDXGISwapChain::Present function, or a trampoline to call it
    HRESULT (STDCALL *g_truePresent)(IDXGISwapChain* swap, UINT sync_interval, UINT flags);

    // Opens the shared capture target texture
    ID3D10Resource* openCaptureTarget(IDXGISwapChain *swap)
    {
        HRESULT hr;
        com_ptr<ID3D10Device> device;
        ID3D10Resource *target = nullptr;

        HANDLE sharedHandle = g_sharedTextureHandle.load();
        if (sharedHandle == INVALID_HANDLE_VALUE)
            return nullptr;

        hr = IDXGISwapChain_GetDevice(swap, IID_ID3D10Device, device.pptr_as_void_cleared());
        if FAILED(hr) {
            logger << "Failed to retrieve device from swap chain: " << util::hresult_to_utf8(hr) << std::endl;
            return nullptr;
        }

        hr = ID3D10Device_OpenSharedResource(device, sharedHandle, IID_ID3D10Resource, (void**)&target);
        if FAILED(hr) {
            logger << "Failed to open shared texture: " << util::hresult_to_utf8(hr) << std::endl;
            return nullptr;
        }

        return target;
    }

    // Copies the back buffer into the given target
    void copyBackBuffer(IDXGISwapChain *swap, ID3D10Resource *target)
    {
        HRESULT hr;
        com_ptr<ID3D10Device>   device;
        com_ptr<ID3D10Resource> backBuffer;

        DXGI_SWAP_CHAIN_DESC swpdsc;

        hr = IDXGISwapChain_GetDevice(swap, IID_ID3D10Device, device.pptr_as_void_cleared());
        if FAILED(hr) {
            logger << "Failed to retrieve device from swap chain: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        hr = IDXGISwapChain_GetBuffer(swap, 0, IID_ID3D10Resource, backBuffer.pptr_as_void_cleared());
        if FAILED(hr) {
            logger << "Failed to retrieve back buffer from swap chain: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        hr = IDXGISwapChain_GetDesc(swap, &swpdsc);
        if FAILED(hr) {
            logger << "Failed to retrieve description of swap chain: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        if (swpdsc.SampleDesc.Count > 1) {
            ID3D10Device_ResolveSubresource(device, target, 0, backBuffer, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
        } else {
            ID3D10Device_CopyResource(device, target, backBuffer);
        }
    }

    void trySetupCapturing(IDXGISwapChain *swap)
    {
        HRESULT hr;
        com_ptr<IDXGIOutput> output;
        DXGI_OUTPUT_DESC desc;

        hr = IDXGISwapChain_GetContainingOutput(swap, output.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to retrieve output from swap chain: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        LONG left = g_monitorLeft.load();
        LONG top = g_monitorTop.load();
        LONG right = g_monitorRight.load();
        LONG bottom = g_monitorBottom.load();

        hr = IDXGIOutput_GetDesc(output, &desc);
        if FAILED(hr) {
            logger << "Failed to retrieve description from output: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        if (!desc.AttachedToDesktop)
            return;

        if (desc.DesktopCoordinates.left == left && desc.DesktopCoordinates.top == top && desc.DesktopCoordinates.bottom == bottom && desc.DesktopCoordinates.right == right) {
            // This is our swap chain!
            g_capturedSwapChain.store(swap);
        }
    }

    // Our own overridden IDXGISwapChain::Present method
    HRESULT STDCALL OverriddenPresent(IDXGISwapChain *swap, UINT sync_interval, UINT flags)
    {
        IDXGISwapChain *capturedChain = g_capturedSwapChain.load();

        // We limit the number of screenshots to 20fps
        static DWORD lastShot = 0;

        if (capturedChain == swap) {
            // We're supposed to record this one.
            ID3D10Resource *captureTarget = g_captureTarget.load();

            if (!captureTarget) {
                captureTarget = openCaptureTarget(swap);
                g_captureTarget.store(captureTarget);
            }

            if (captureTarget) {
                DWORD currentTicks = GetTickCount();
                if (currentTicks - lastShot > 50) {
                    lastShot = currentTicks;
                    copyBackBuffer(swap, captureTarget);
                }
            }
        } else if (!capturedChain) {
            // we might be able to initiate capturing on this swap chain
            trySetupCapturing(swap);
        }

        return g_truePresent(swap, sync_interval, flags);
    }

    //////////////////////////////////////////////////
    // Communication window to the host
    //////////////////////////////////////////////////
    class Communicator : win32::window
    {
        static const UINT_PTR CHECK_KEEPALIVE_TIMER_ID = 42;

        DWORD m_lastKeepAlive;

    public:
        DwmCommunicator(const Communicator* window) = delete;

        Communicator() : win32::window(0, 0, 0, HWND_MESSAGE), m_lastKeepAlive(GetTickCount())
        {
            SetTimer(hwnd(), CHECK_KEEPALIVE_TIMER_ID, 1000, nullptr);

            PostMessage(g_host.load(), WM_APP_INJECTED, 0, (LPARAM)hwnd());
        }

        ~Communicator()
        {}

    private:
        LRESULT onCopydata(COPYDATASTRUCT *data)
        {
            if (data->dwData == COPYDATA_ID_NEWSCREEN) {
                RECT *screen = reinterpret_cast<RECT*>(data->lpData);

                g_monitorLeft.store(screen->left);
                g_monitorTop.store(screen->top);
                g_monitorRight.store(screen->right);
                g_monitorBottom.store(screen->bottom);

                g_capturedSwapChain.store(nullptr);
            }

            return TRUE;
        }

        LRESULT onKeepAlive()
        {
            m_lastKeepAlive = GetTickCount();

            return TRUE;
        }

        LRESULT onCheckKeepAlive()
        {
            if ((GetTickCount() - m_lastKeepAlive) > 2000)
                PostQuitMessage(-1);

            return TRUE;
        }

        LRESULT onNewTexture(HANDLE tex)
        {
            g_sharedTextureHandle.store(tex);
            ID3D10Resource *res = g_captureTarget.exchange(nullptr);
            if (res)
                ID3D10Resource_Release(res);

            return TRUE;
        }

    protected:
        LRESULT handleMessage(UINT msgid, WPARAM wp, LPARAM lp)
        {
            if (msgid == WM_COPYDATA) {
                COPYDATASTRUCT *data = reinterpret_cast<COPYDATASTRUCT*>(lp);

                return onCopydata(data);
            } else if (msgid == WM_APP_NEWTEXTURE) {
                return onNewTexture((HANDLE)lp);
            } else if (msgid == WM_APP_KEEPALIVE) {
                return onKeepAlive();
            } else if (msgid == WM_TIMER && wp == (WPARAM)CHECK_KEEPALIVE_TIMER_ID) {
                return onCheckKeepAlive();
            }

            return win32::window::handleMessage(msgid, wp, lp);
        }
    };

    //////////////////////////////////////
    // Logger function
    //////////////////////////////////////
    static void __cdecl SendMessageLogHandler(const char *message, void *userdata)
    {
        HWND host = (HWND)userdata;

        COPYDATASTRUCT copy = {
            .dwData = COPYDATA_ID_LOG,
            .cbData = static_cast<DWORD>(std::strlen(message)),
            .lpData = reinterpret_cast<void*>(const_cast<char*>(message))
        };

        SendMessageTimeout(host, WM_COPYDATA, 0, (LPARAM)&copy, SMTO_ABORTIFHUNG, 500, nullptr);
    }

    ///////////////////////////////////////////
    // Hooking
    ///////////////////////////////////////////
    static bool DoTheHook()
    {
        HRESULT hr;

        // find d3d10 dll to inject our code
        util::dll_func<HRESULT (IDXGIAdapter *,
                                D3D10_DRIVER_TYPE,
                                HMODULE,
                                UINT,
                                D3D10_FEATURE_LEVEL1,
                                UINT,
                                DXGI_SWAP_CHAIN_DESC *,
                                IDXGISwapChain **,
                                ID3D10Device1 **)> d3dCreator { L"d3d10_1.dll", "D3D10CreateDeviceAndSwapChain1" };
        if (!d3dCreator) {
            logger << "No D3D10.1 available in the DWM :(" << std::endl;
            return false;
        }

        // need dummy window to create a dummy swap chain
        win32::window temporary;

        DXGI_SWAP_CHAIN_DESC swapDesc;
        ZeroMemory(&swapDesc, sizeof(swapDesc));
        swapDesc.BufferCount       = 2;
        swapDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapDesc.BufferDesc.Width  = 2;
        swapDesc.BufferDesc.Height = 2;
        swapDesc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.OutputWindow      = temporary.hwnd();
        swapDesc.SampleDesc.Count  = 1;
        swapDesc.Windowed          = TRUE;

        com_ptr<ID3D10Device1>  dev;
        com_ptr<IDXGISwapChain> swap;

        hr = d3dCreator(nullptr,
                        D3D10_DRIVER_TYPE_NULL,
                        nullptr,
                        0,
                        D3D10_FEATURE_LEVEL_9_1,
                        D3D10_1_SDK_VERSION,
                        &swapDesc,
                        swap.pptr_cleared(),
                        dev.pptr_cleared());
        if FAILED(hr) {
            logger << "FAILED: D3D10CreateDeviceAndSwapChain1: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        // now hook the present function
        // MHook uses a critical section which implies a memory barrier
        // so it's actually safe to access the global variable here
        g_truePresent = swap->lpVtbl->Present;
        return Mhook_SetHook((void**)&g_truePresent, (void*)OverriddenPresent);
    }

    static bool UndoTheHook()
    {
        // Unhook ourselves. This _has_ to succeed, otherwise we're doomed
        // because we really want to destroy ourselves, and we can't unload
        // the dll while we're still executing it.
        return Mhook_Unhook((void**)&g_truePresent);
    }
}

extern "C" __stdcall __declspec(dllexport)
DWORD _SV_DWM_EntryPoint(void *param)
{
    HWND host = (HWND)param;
    g_host.store(host);

    SV_SetLogHandler(SendMessageLogHandler, param);

    logger << "Thread has been injected!" << std::endl;

    // Setup the communication window
    Communicator comm;

    // Hook IDXGISwapChain::Present
    if (!DoTheHook())
        return -1;

    // Run our message loop
    BOOL ret;
    MSG  msg;
    while ((ret = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (ret < 0)
            break;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    logger << "Bye Bye DWM!" << std::endl;

    // Unhook IDXGISwapChain::Present
    if (UndoTheHook()) {
        // Freeing the allocated shared texture object is in order
        ID3D10Resource *res = g_captureTarget.load();

        if (res)
            ID3D10Resource_Release(res);

        FreeLibraryAndExitThread(win32::get_running_instance(), 0);
    }

    logger << "Unhook failed, guess we're staying..." << std::endl;
    Sleep(INFINITE);

    return 0;
}