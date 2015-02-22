#pragma once

// The view will send this to the DWM periodically
// Sent by the view
// WPARAM: 0
// LPARAM: 0
#define WM_APP_KEEPALIVE    0x8002

// Informs the injected code of a new shared texture to use as a copy target
// Sent by the view
// WPARAM: 0
// LPARAM: (HANDLE) the shared d3d handle for the texture
#define WM_APP_NEWTEXTURE   0x8003

// Informs the view about a successful injection
// WPARAM: 0
// LPARAM: (HWND) communication window inside the DWM
#define WM_APP_INJECTED     0x8004

// Larger messages are being sent via WM_COPYDATA
// dwData is used as an ID to discriminate specific messages

// lpData contains an UTF8 character string, not necessarily NULL terminated
#define COPYDATA_ID_LOG     1

// lpData contains a RECT structure specifying the desktop coordinates of the screen
#define COPYDATA_ID_NEWSCREEN 2


