#pragma once

#include <windows.h>

/*
 * Allows to view the image currently presented on a monitor in a window
 */

#define DECLSPEC __attribute__((dllimport)) __attribute__((cdecl))


/*
 * Setup a handler for logging messages. By default, these are printed to stderr.
 *
 * Each message should be presented on a new line, i.e. a line break must be emitted
 * after printing the message.
 */
typedef void (DECLSPEC *SV_LogHandler_t)(const char *message, void *userdata);
void DECLSPEC SV_SetLogHandler(SV_LogHandler_t handler, void *userdata);

/*
 * Creates a window displaying the contents of the given screen.
 *
 * Note that the given rectangle must exactly match the desktop rectangle for a monitor.
 *
 * The window and all associated resources can be freed by calling DestroyWindow on it.
 *
 * If the view couldn't be created, 0 will be returned.
 */
HWND DECLSPEC SV_CreateView(HWND parent, int x, int y, int w, int h);

/*
 * Changes the screen displayed by the given view to the screen indicated by the
 * given coordinates
 */
void DECLSPEC SV_ChangeScreen(HWND view, int x, int y, int w, int h);