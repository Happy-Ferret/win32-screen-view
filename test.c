#include <stdio.h>
#include <windows.h>

HWND (__cdecl *SV_CreateView)(HWND parent, int x, int y, int w, int h);
void (__cdecl *SV_ChangeScreen)(HWND view, int x, int y, int w, int h);
typedef void (__cdecl *SV_LogHandler_t)(const char *message, void *userdata);
void (__cdecl *SV_SetLogHandler)(SV_LogHandler_t handler, void *userdata);

int x = 0, y = 0, w = 1024, h = 768;
HWND child = NULL;

static BOOL CALLBACK MyEnumMonitorProc(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM data)
{
    (void)monitor;
    (void)hdc;
    (void)data;

    x = rect->left;
    y = rect->top;
    w = rect->right - rect->left;
    h = rect->bottom - rect->top;

    return data ? FALSE : TRUE;
}

static LRESULT CALLBACK MyWindowProc(HWND hwnd, UINT msgid, WPARAM wp, LPARAM lp)
{
    static LPARAM lastMonitorChosen = 0;

    if (msgid == WM_SIZE) {
        RECT cr;
        GetClientRect(hwnd, &cr);

        SetWindowPos(child, HWND_TOP, 0, 0, cr.right-cr.left, cr.bottom-cr.top, 0);
    } else if (msgid == WM_TIMER) {
        // choose another monitor
        EnumDisplayMonitors(NULL, NULL, MyEnumMonitorProc, (lastMonitorChosen = !lastMonitorChosen));

        fprintf(stderr, "changing monitor to %d,%d,%d,%d\n", x, y, w, h);

        SV_ChangeScreen(child, x, y, w, h);
    } else if (msgid == WM_CREATE) {
        SetTimer(hwnd, 0, 5000, NULL);
    } else if (msgid == WM_CLOSE) {
        DestroyWindow(hwnd);
        return TRUE;
    }

    return DefWindowProc(hwnd, msgid, wp, lp);
}

static __cdecl void MyLogHandler(const char *msg, void *userdata)
{
    char *prefix_str = (char*)userdata;

    fprintf(stderr, "%s: %s\n", prefix_str, msg);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    HMODULE win7 = LoadLibrary(L"screenview-x86.dll");
    SV_CreateView = (void*)GetProcAddress(win7, "SV_CreateView");
    SV_ChangeScreen = (void*)GetProcAddress(win7, "SV_ChangeScreen");
    SV_SetLogHandler = (void*)GetProcAddress(win7, "SV_SetLogHandler");

    // register a log handler
    char prefix[] = "DLL LOG";
    SV_SetLogHandler(MyLogHandler, (void*)prefix);

    // register our class if possible, if not, skip it
    WNDCLASSEX wcex;
    memset(&wcex, 0, sizeof(wcex));
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MyWindowProc;
    wcex.hInstance = NULL;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"DesktopViewMainWindow";

    if (!RegisterClassEx(&wcex)) {
        fprintf(stderr, "Failed: RegisterClassEx: %u\n", (unsigned)GetLastError());
        // we just might have been unlucky enough to register the class twice, so don't bail out yet.
        //return 0;
    }

    HWND toplevel = CreateWindow(L"DesktopViewMainWindow",
                          L"Injector display",
                          WS_OVERLAPPEDWINDOW, /* TODO: change to child window */
                          50, 50, 400, 400,
                          NULL, NULL,
                          NULL, NULL);

    // search for monitors
    EnumDisplayMonitors(NULL, NULL, MyEnumMonitorProc, 0);

    child = SV_CreateView(toplevel, x, y, w, h);

    ShowWindow(toplevel, SW_SHOW);
    ShowWindow(child, SW_SHOW);

    MSG msg;
    BOOL ret;
    while ((ret = GetMessage(&msg, NULL, 0, 0))) {
        if (ret == -1)
            break;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}