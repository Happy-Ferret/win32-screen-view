#define NOMINMAX

#include "seven_dwm_source.hpp"

#include "util.hpp"
#include "logger.hpp"
#include "seven_dwm_injected.hpp"
#include "injection.hpp"
#include "win32.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <windows.h>

namespace util {
    template<>
    inline void raii_free(ICONINFO &ii)
    {
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
    }

    template<>
    inline void raii_free(HDC &hdc)
    {
        DeleteDC(hdc);
    }
}

namespace {
    static const UINT CURSOR_TEX_SIZE = 256;

    void updateCursorShape(ID3D10Texture2D *tex, HCURSOR cursor, DWORD &xHotspot, DWORD &yHotspot)
    {
        util::raii<ICONINFO>   info;
        util::raii<HDC>        hdc;

        struct {
            BITMAPINFOHEADER bi;
            RGBQUAD colors[2];
        } bmi;


        HRESULT hr;

        if (!GetIconInfo(cursor, info))
            return;

        if (!(*hdc = CreateCompatibleDC(NULL)))
            return;

        xHotspot = info->xHotspot;
        yHotspot = info->yHotspot;

        if (!tex)
            return;

        util::zero_out(bmi);
        bmi.bi.biSize = sizeof(bmi.bi);

        if (!info->hbmColor) {
            // monochrome cursor
            if (!GetDIBits(*hdc, info->hbmMask, 0, 0, nullptr, (BITMAPINFO*)&bmi, DIB_RGB_COLORS))
                return;

            UINT w = static_cast<UINT>(bmi.bi.biWidth);
            UINT h = static_cast<UINT>(std::abs(bmi.bi.biHeight)/2);
            bmi.bi.biHeight = -1*std::abs(bmi.bi.biHeight); // force top-down bitmap

            std::unique_ptr<uint8_t[]> bits(new uint8_t[4 * w * h]);

            if (!GetDIBits(*hdc, info->hbmMask, 0, h*2, bits.get(), (BITMAPINFO*)&bmi, DIB_RGB_COLORS))
                return;

            LONG bpl = ((w-1)/32 + 1)*4; // bytes per line

            D3D10_MAPPED_TEXTURE2D map;
            hr = tex->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &map);
            if FAILED(hr) {
                logger << "Failed: ID3D10Texture2D::Map: " << util::hresult_to_utf8(hr) << std::endl;
                return;
            }

            std::memset(map.pData, 0x00, map.RowPitch * CURSOR_TEX_SIZE);

            //FIXME: We don't want to read the desktop image back into the CPU, so we pretend to
            //       apply the AND mask onto a black surface. This is incorrect, but doesn't look too bad.
            uint8_t *and_map = bits.get();
            uint8_t *xor_map = bits.get() + bpl*h;

            for (UINT row = 0; row < std::min(h, CURSOR_TEX_SIZE); ++row)
            {
                uint8_t *and_row = &and_map[row * bpl];
                uint8_t *xor_row = &xor_map[row * bpl];

                for (UINT col = 0; col < std::min(w, CURSOR_TEX_SIZE); ++col) {
                    uint8_t *target = &reinterpret_cast<uint8_t*>(map.pData)[row*map.RowPitch + col*4];

                    uint8_t alpha = util::get_pixel_from_row<1>(and_row, col) ? 0 : 0xFF;
                    uint8_t rgb   = util::get_pixel_from_row<1>(xor_row, col) ? 0xFF : 0;

                    target[0] = rgb;
                    target[1] = rgb;
                    target[2] = rgb;
                    target[3] = alpha;
                }
            }

            tex->Unmap(0);
        } else {
            if (!GetDIBits(*hdc, info->hbmColor, 0, 1, nullptr, (BITMAPINFO *)&bmi, DIB_RGB_COLORS))
                return;

            UINT w = static_cast<UINT>(bmi.bi.biWidth);
            UINT h = static_cast<UINT>(std::abs(bmi.bi.biHeight));

            bmi.bi.biBitCount = 32;
            bmi.bi.biCompression = BI_RGB;
            bmi.bi.biHeight = -std::abs(bmi.bi.biHeight); // force top-down bitmap

            std::unique_ptr<uint8_t[]> bits(new uint8_t[4*w*h]);

            // read the color data
            if (!GetDIBits(*hdc, info->hbmColor, 0, h, bits.get(), (BITMAPINFO *)&bmi, DIB_RGB_COLORS)) {
                logger << "Failed: GetDIBits: " << GetLastError() << std::endl;
                return;
            }

            D3D10_MAPPED_TEXTURE2D map;
            hr = tex->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &map);
            if FAILED(hr) {
                logger << "Failed: ID3D10Texture2D::Map: " << util::hresult_to_utf8(hr) << std::endl;
                return;
            }

            // make everything black and transparent
            memset(map.pData, 0x00, map.RowPitch * CURSOR_TEX_SIZE);

            for (UINT y = 0; y < std::min(h, CURSOR_TEX_SIZE); y++) {
                uint8_t *src_row = bits.get() + y*w*4;
                uint8_t *dst_row = reinterpret_cast<uint8_t*>(map.pData) + y*map.RowPitch;

                memcpy(dst_row, src_row, std::min(w, CURSOR_TEX_SIZE)*4);
            }

            // mask
            if (GetDIBits(*hdc, info->hbmMask, 0, h, bits.get(), (BITMAPINFO *)&bmi, DIB_RGB_COLORS))
            {
                for (UINT y = 0; y < std::min(h, CURSOR_TEX_SIZE); y++)
                {
                    for (UINT x = 0; x < std::min(w, CURSOR_TEX_SIZE); x++)
                    {
                        uint8_t *target = reinterpret_cast<uint8_t*>(map.pData) + y*map.RowPitch + x*4;
                        uint8_t *source = bits.get() + (x + y * w)*4;

                        target[3] = 255 - source[0];
                    }
                }
            }

            tex->Unmap(0);
        }
    }
}

class SevenDwmSource_DwmCommunicator : public win32::window
{
    static const UINT_PTR KEEPALIVE_TIMER_ID = 42;

    HWND m_dwmWindow = NULL;

    HANDLE          m_textureForDwm = INVALID_HANDLE_VALUE;
    RECT            m_screenForDwm  { 0,0,0,0 };
    wchar_t         m_ownDllPath[MAX_PATH] = {};
    wchar_t        *m_ownDllBaseName = nullptr;

public:
    SevenDwmSource_DwmCommunicator(const SevenDwmSource_DwmCommunicator* window) = delete;

    SevenDwmSource_DwmCommunicator()
      : win32::window(0, 0, 0, NULL, L"SevenDwmSource DWM Communicator")
    {
        // find out our own base name, for injecting it into the DWM
        if (GetModuleFileName(win32::get_running_instance(), m_ownDllPath, MAX_PATH)) {
            m_ownDllBaseName = wcsrchr(m_ownDllPath, L'\\') + 1; // acutally recommended by MSDN somewhere
        }

        // Install the keepalive timer
        SetTimer(hwnd(), KEEPALIVE_TIMER_ID, 1000, nullptr);
    }

    void sendNewTexture(HANDLE sharedTexture)
    {
        m_textureForDwm = sharedTexture;

        sendTexture();
    }

    void sendNewScreen(int x, int y, int w, int h)
    {
        m_screenForDwm.left   = x;
        m_screenForDwm.top    = y;
        m_screenForDwm.right  = x + w;
        m_screenForDwm.bottom = y + h;

        sendScreen();
    }

private:
    void sendTexture()
    {
        if (m_dwmWindow)
            PostMessage(m_dwmWindow, WM_APP_NEWTEXTURE, 0, (LPARAM)m_textureForDwm);
    }

    void sendScreen()
    {
        if (!m_dwmWindow)
            return;

        COPYDATASTRUCT copy = {
            .dwData = COPYDATA_ID_NEWSCREEN,
            .cbData = sizeof(m_screenForDwm),
            .lpData = reinterpret_cast<void*>(&m_screenForDwm)
        };

        SendMessage(m_dwmWindow, WM_COPYDATA, (WPARAM)hwnd(), (LPARAM)&copy);
    }

    LRESULT onCopydata(COPYDATASTRUCT *data)
    {
        if (data->dwData != COPYDATA_ID_LOG)
            return FALSE;

        // Ensure null termination by copying the string first
        std::string str(reinterpret_cast<char*>(data->lpData), static_cast<std::string::size_type>(data->cbData));

        logger << "FROM DWM: " << str << std::endl;

        return TRUE;
    }

    LRESULT onInjected(HWND dwmWindow)
    {
        m_dwmWindow = (HWND)dwmWindow;

        sendTexture();
        sendScreen();

        return TRUE;
    }

    LRESULT onKeepAlive()
    {
        // Check whether we are (still) injected into the dwm
        uint32_t dwm = injection::process_id_for_name(L"dwm.exe");

        if (!dwm) { // uups, no dwm present!?
            m_dwmWindow = NULL;
        } else {
            if (!injection::is_dll_loaded(dwm, m_ownDllBaseName)) {
                logger << "Now injecting into DWM" << std::endl;
                m_dwmWindow = NULL;

                // inject ourselves into the dwm
                std::ptrdiff_t load_library_offset = injection::get_function_offset(L"kernel32.dll", "LoadLibraryW");
                std::ptrdiff_t our_entry_point_offset = injection::get_function_offset(m_ownDllBaseName, "_SV_DWM_EntryPoint@4");

                if (!load_library_offset || !our_entry_point_offset) {
                    logger << "FATAL: Entry point not found, can't inject :(" << std::endl;
                    return TRUE;
                }

                // load the dll
                if (!injection::call_remote_func(dwm, L"kernel32.dll", load_library_offset, (void*)m_ownDllPath, MAX_PATH*sizeof(wchar_t))) {
                    logger << "FATAL: LoadLibraryW could not be executed :(" << std::endl;
                    return TRUE;
                }

                // and kickoff our own function
                injection::call_remote_func(dwm, m_ownDllBaseName, our_entry_point_offset, (void*)hwnd(), 0, nullptr, 0);
            }
        }

        // If we are connected to the DWM, we message it
        if (m_dwmWindow)
            PostMessage(m_dwmWindow, WM_APP_KEEPALIVE, 0, 0);

        return TRUE;
    }

protected:
    virtual LRESULT handleMessage(UINT msgid, WPARAM wp, LPARAM lp) override
    {
        if (msgid == WM_COPYDATA) {
            COPYDATASTRUCT *data = reinterpret_cast<COPYDATASTRUCT*>(lp);

            return onCopydata(data);
        } else if (msgid == WM_APP_INJECTED) {
            return onInjected((HWND)lp);
        } else if (msgid == WM_TIMER && wp == (WPARAM)KEEPALIVE_TIMER_ID) {
            return onKeepAlive();
        }

        return win32::window::handleMessage(msgid, wp, lp);
    }
}; // class SevenDwmSource_DwmCommunicator

SevenDwmSource::SevenDwmSource() : m_communicator(new SevenDwmSource_DwmCommunicator)
{
}

SevenDwmSource::~SevenDwmSource()
{
    delete m_communicator;
}

void
SevenDwmSource::reinit(ID3D10Device *device, int x, int y, int w, int h)
{
    logger << "(Re)initializing dwm source dev="<<device<<" x="<<x<<" y="<<y<<" w="<<w<<" h="<<h << std::endl;

    m_desktopWidth = w;
    m_desktopHeight = h;
    m_desktopX = x;
    m_desktopY = y;

    m_dev  = device;

    m_lastCursorSeen = NULL;
    m_xHotspot = 0;
    m_yHotspot = 0;

    m_communicator->sendNewScreen(x, y, w, h);
}

ID3D10Texture2D *
SevenDwmSource::createDesktopTexture()
{
    HRESULT hr;
    ID3D10Texture2D *texture = nullptr;

    D3D10_TEXTURE2D_DESC texdsc = {
        .Width = static_cast<UINT>(m_desktopWidth),
        .Height = static_cast<UINT>(m_desktopHeight),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc = {
            .Count = 1,
            .Quality = 0
        },
        .Usage = D3D10_USAGE_DEFAULT,
        .BindFlags = D3D10_BIND_RENDER_TARGET|D3D10_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = 0,
        .MiscFlags = D3D10_RESOURCE_MISC_SHARED
    };

    // initially, the texture is black and transparent
    D3D10_SUBRESOURCE_DATA texdata = {
        .pSysMem = std::calloc(texdsc.Width * texdsc.Height, 4),
        .SysMemPitch = 4*texdsc.Width,
        .SysMemSlicePitch = 0
    };

    hr = m_dev->CreateTexture2D(&texdsc, &texdata, &texture);

    // free the allocated memory
    std::free(const_cast<void*>(texdata.pSysMem));

    if FAILED(hr)
        logger << "Failed:CreateTexture2D: " << util::hresult_to_utf8(hr) << std::endl;

    // Pass texture handle to the injected side
    com_ptr<IDXGIResource> res;
    hr = texture->QueryInterface(res.uuid(), res.pptr_as_void_cleared());
    if FAILED(hr) {
        logger << "Failed: QueryInterface<IDXGIResource>: " << util::hresult_to_utf8(hr) << std::endl;
        return texture;
    }

    HANDLE hshared;
    hr = res->GetSharedHandle(&hshared);
    if FAILED(hr) {
        logger << "Failed: GetSharedHandle: " << util::hresult_to_utf8(hr) << std::endl;
        return texture;
    }

    m_communicator->sendNewTexture(hshared);

    return texture;
}

ID3D10Texture2D *
SevenDwmSource::createCursorTexture()
{
    HRESULT hr;
    ID3D10Texture2D *texture = nullptr;

    D3D10_TEXTURE2D_DESC texdsc = {
        .Width = CURSOR_TEX_SIZE,
        .Height = CURSOR_TEX_SIZE,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc = {
            .Count = 1,
            .Quality = 0
        },
        .Usage = D3D10_USAGE_DYNAMIC,
        .BindFlags = D3D10_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = D3D10_CPU_ACCESS_WRITE,
        .MiscFlags = 0
    };

    // initially, the texture is black and transparent
    D3D10_SUBRESOURCE_DATA texdata = {
        .pSysMem = std::calloc(texdsc.Width * texdsc.Height, 4),
        .SysMemPitch = 4*texdsc.Width,
        .SysMemSlicePitch = 0
    };

    hr = m_dev->CreateTexture2D(&texdsc, &texdata, &texture);

    std::free(const_cast<void*>(texdata.pSysMem));

    if FAILED(hr)
        logger << "Failed:CreateTexture2D: " << util::hresult_to_utf8(hr) << std::endl;

    return texture;
}

void
SevenDwmSource::updateCursor(ID3D10Texture2D *cursorTex, LONG& cursorX, LONG& cursorY, bool& cursorVisible)
{
    CURSORINFO cursorinfo;
    POINT      position;

    cursorinfo.cbSize = sizeof(cursorinfo);

    if (!GetCursorPos(&position) || !GetCursorInfo(&cursorinfo))
        return;

    if (cursorinfo.hCursor != m_lastCursorSeen) {
        updateCursorShape(cursorTex, (m_lastCursorSeen = cursorinfo.hCursor), m_xHotspot, m_yHotspot);
    }

    cursorVisible = cursorinfo.flags == CURSOR_SHOWING;
    cursorX       = position.x - m_desktopX - m_xHotspot;
    cursorY       = position.y - m_desktopY - m_yHotspot;
    //FIXME: do we need to release info.hCursor?
}