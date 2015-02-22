#pragma once

#include <d3d10_1.h>

#include "com_ptr.hpp"

class SevenDwmSource_DwmCommunicator;
class SevenDwmSource {
    ID3D10Device   *m_dev = nullptr;

    int m_desktopWidth  = 0;
    int m_desktopHeight = 0;
    int m_desktopX      = 0;
    int m_desktopY      = 0;

    HCURSOR m_lastCursorSeen = NULL;
    DWORD   m_xHotspot = 0;
    DWORD   m_yHotspot = 0;

    SevenDwmSource_DwmCommunicator *m_communicator;

public:
    SevenDwmSource();
    ~SevenDwmSource();

    void reinit(ID3D10Device *device, int x, int y, int w, int h);
    ID3D10Texture2D *createDesktopTexture();
    ID3D10Texture2D *createCursorTexture();
    void acquireFrame() { /* FIXME: Should we lock the desktop texture? */ }
    void updateDesktop(ID3D10Texture2D *) { /* The injected code will always write the desktop image for us */ }
    void updateCursor(ID3D10Texture2D *cursorTex, LONG& cursorX, LONG& cursorY, bool& cursorVisible);
    void releaseFrame() { /* FIXME: Unlock desktop texture? */ }
};