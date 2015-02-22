#include "duplication_source.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <cstdlib>

namespace {
    static const UINT CURSOR_TEX_SIZE = 256;
}

void
DuplicationSource::reinit(ID3D10Device *device, int x, int y, int w, int h)
{
    logger << "(Re)initializing duplication source dev="<<device<<" x="<<x<<" y="<<y<<" w="<<w<<" h="<<h << std::endl;

    m_duplication.clear();
    m_duplDesktopImage.clear();
    m_frameAcquired = false;

    m_desktopWidth = w;
    m_desktopHeight = h;
    m_desktopX = x;
    m_desktopY = y;

    m_dev  = device;

    // find the matching output
    com_ptr<IDXGIDevice>  dev;
    com_ptr<IDXGIAdapter> adp;

    device->QueryInterface(dev.uuid(), dev.pptr_as_void_cleared());
    dev->GetAdapter(adp.pptr_cleared());

    com_ptr<IDXGIOutput> output;
    for (UINT i = 0; SUCCEEDED(adp->EnumOutputs(i, output.pptr_cleared())); ++i) {
        com_ptr<IDXGIOutput1> output1 = output.query<IDXGIOutput1>();

        if (!output1)
            continue;

        DXGI_OUTPUT_DESC desc;
        output1->GetDesc(&desc);

        if (desc.AttachedToDesktop
            && desc.DesktopCoordinates.left == x
            && desc.DesktopCoordinates.top  == y
            && desc.DesktopCoordinates.right == x + w
            && desc.DesktopCoordinates.bottom == y + h)
        {
            logger << "Attempting to duplicate display " << i << std::endl;

            HRESULT hr = output1->DuplicateOutput(device, m_duplication.pptr_cleared());
            if FAILED(hr)
                logger << "Attempted to duplicate display " << i << " but: " << util::hresult_to_utf8(hr) << std::endl;

            return;
        }
    }

    logger << "WARNING: Couldn't find display: x="<<x<<" y="<<y<<" w="<<w<<" h="<<h<<std::endl;
}

ID3D10Texture2D *
DuplicationSource::createDesktopTexture()
{
    if (!m_duplication || !m_dev)
        return nullptr;

    DXGI_OUTDUPL_DESC dpldesc;
    HRESULT hr;
    ID3D10Texture2D *texture = nullptr;

    m_duplication->GetDesc(&dpldesc);

    D3D10_TEXTURE2D_DESC texdsc = {
        .Width = dpldesc.ModeDesc.Width,
        .Height = dpldesc.ModeDesc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc = {
            .Count = 1,
            .Quality = 0
        },
        .Usage = D3D10_USAGE_DEFAULT,
        .BindFlags = D3D10_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = 0,
        .MiscFlags = 0
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

    return texture;
}

ID3D10Texture2D *
DuplicationSource::createCursorTexture()
{
    if (!m_dev)
        return nullptr;

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
DuplicationSource::acquireFrame()
{
    HRESULT hr;

    if (!m_duplication || !m_dev)
        return;

    hr = m_duplication->AcquireNextFrame(100, &m_duplInfo, m_duplDesktopImage.pptr_cleared());

    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        return; // This can happen if the screen is idle or whatever, it's not really fatal enough to log

    if (!(m_frameAcquired = SUCCEEDED(hr)))
        logger << "Failed: AcquireNextFrame: " << util::hresult_to_utf8(hr) << std::endl;

    // if the access has been lost, we might get away with just recreating it again
    if (FAILED(hr) && hr == DXGI_ERROR_ACCESS_LOST) {
        logger << "Recreating the IDXGIOutputDuplication interface because of DXGI_ERROR_ACCESS_LOST=" << hr << std::endl;
        reinit(m_dev, m_desktopX, m_desktopY, m_desktopWidth, m_desktopHeight);
    }
}

void
DuplicationSource::updateDesktop(ID3D10Texture2D *desktopTex)
{
    if (!desktopTex || !m_frameAcquired || !m_dev || !m_duplDesktopImage)
        return;

    if (!m_duplInfo.LastPresentTime.QuadPart || !m_duplDesktopImage)
        return;

    auto d3dresource = m_duplDesktopImage.query<ID3D10Texture2D>();
    m_dev->CopyResource(desktopTex, d3dresource);
}

void
DuplicationSource::updateCursor(ID3D10Texture2D *cursorTex, LONG& cursorX, LONG& cursorY, bool& cursorVisible)
{
    HRESULT hr;

    if (!cursorTex || !m_frameAcquired)
        return;

    if (!m_duplInfo.LastMouseUpdateTime.QuadPart)
        return;

    if ((cursorVisible = m_duplInfo.PointerPosition.Visible)) {
        cursorX = m_duplInfo.PointerPosition.Position.x;
        cursorY = m_duplInfo.PointerPosition.Position.y;
    }

    if (!m_duplInfo.PointerShapeBufferSize)
        return;

    std::unique_ptr<uint8_t[]> buffer(new uint8_t[m_duplInfo.PointerShapeBufferSize]);

    DXGI_OUTDUPL_POINTER_SHAPE_INFO pointer;
    UINT dummy;
    hr = m_duplication->GetFramePointerShape(m_duplInfo.PointerShapeBufferSize, reinterpret_cast<void*>(buffer.get()), &dummy, &pointer);
    if FAILED(hr) {
        logger << "Failed: GetFramePointerShape: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    // we can now update the pointer shape
    D3D10_MAPPED_TEXTURE2D info;
    hr = cursorTex->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &info);
    if FAILED(hr) {
        logger << "Failed: ID3D10Texture2D::Map: " << util::hresult_to_utf8(hr) << std::endl;
        return;
    }

    // first, make it black and transparent
    memset(info.pData, 0, 4 * CURSOR_TEX_SIZE * CURSOR_TEX_SIZE);

    // then, fill it with the new cursor
    if (pointer.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
        for (UINT row = 0; row < std::min(pointer.Height, CURSOR_TEX_SIZE); ++row) {
            memcpy((char*)info.pData + row*info.RowPitch, buffer.get() + row*pointer.Pitch, std::min(pointer.Width, CURSOR_TEX_SIZE)*4);
        }
    } else if (pointer.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) {
        //FIXME: We don't want to read the desktop image back into the CPU, so we apply the mask
        // onto a black background. This is not correct.
        //FIXME: I haven't found a way yet to trigger this codepath at runtime.
        for (UINT row = 0; row < std::min(pointer.Height, CURSOR_TEX_SIZE); ++row) {
            for (UINT col = 0; col < std::min(pointer.Width, CURSOR_TEX_SIZE); ++col) {
                uint8_t *target = &reinterpret_cast<uint8_t*>(info.pData)[row*info.RowPitch + col*4];
                uint8_t *source = &buffer[row*pointer.Pitch + col*4];

                // the mask value doesn't matter because
                //  mask==0     => Use source RGB values
                //  mask==0xFF  => Use source RGB XOR target RGB = source RGB if the target is black
                target[0] = source[0];
                target[1] = source[1];
                target[2] = source[2];
                target[3] = 0xFF;
            }
        }
    } else {
        //FIXME: We don't want to read the desktop image back into the CPU, so we pretend to
        //       apply the AND mask onto a black surface. This is incorrect, but doesn't look too bad.

        uint8_t *and_map = buffer.get();
        uint8_t *xor_map = and_map + pointer.Pitch*pointer.Height/2;

        for (UINT row = 0; row < std::min(pointer.Height/2, CURSOR_TEX_SIZE); ++row)
        {
            uint8_t *and_row = &and_map[row * pointer.Pitch];
            uint8_t *xor_row = &xor_map[row * pointer.Pitch];

            for (UINT col = 0; col < std::min(pointer.Width, CURSOR_TEX_SIZE); ++col) {
                uint8_t *target = &reinterpret_cast<uint8_t*>(info.pData)[row*info.RowPitch + col*4];

                uint8_t alpha = util::get_pixel_from_row<1>(and_row, col) ? 0 : 0xFF;
                uint8_t rgb   = util::get_pixel_from_row<1>(xor_row, col) ? 0xFF : 0;

                target[0] = rgb;
                target[1] = rgb;
                target[2] = rgb;
                target[3] = alpha;
            }
        }
    }

    cursorTex->Unmap(0);
}

void
DuplicationSource::releaseFrame()
{
    if (m_frameAcquired && m_duplication)
        m_duplication->ReleaseFrame();

    m_frameAcquired = false;
}