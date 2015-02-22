#pragma once

#include <d3d10_1.h>
#include <dxgi1_2.h>

#include "com_ptr.hpp"

class DuplicationSource {
    ID3D10Device   *m_dev;

    int m_desktopWidth;
    int m_desktopHeight;
    int m_desktopX;
    int m_desktopY;

    com_ptr<IDXGIOutputDuplication> m_duplication;

    bool                    m_frameAcquired = false;
    DXGI_OUTDUPL_FRAME_INFO m_duplInfo;
    com_ptr<IDXGIResource>  m_duplDesktopImage;

public:
    void reinit(ID3D10Device *device, int x, int y, int w, int h);
    ID3D10Texture2D *createDesktopTexture();
    ID3D10Texture2D *createCursorTexture();
    void acquireFrame();
    void updateDesktop(ID3D10Texture2D *desktopTex);
    void updateCursor(ID3D10Texture2D *cursorTex, LONG& cursorX, LONG& cursorY, bool& cursorVisible);
    void releaseFrame();
};