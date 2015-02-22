#pragma once

//////////////////////////////////////////////////////////////////////
// Installing a log handler - C API
//////////////////////////////////////////////////////////////////////
extern "C" {

/**
 * Prototype for a function processing a log message
 *
 * @param message  The log message (UTF-8)
 * @param userdata Opaque pointer that was passed in SV_SetLogHandler
 */
typedef void (__cdecl *SV_LogHandler_t)(const char *message, void *userdata);

/**
 * Installs the given function to be called whenever a new log message
 * is going to be processed.
 *
 * @a handler  A function taking a log message. It may be called from any thread!
 * @a userdata Opaque pointer passed into the handler function
 */
__cdecl __declspec(dllexport) void SV_SetLogHandler(SV_LogHandler_t handler, void *userdata);

} // extern "C"

#ifdef __cplusplus

/////////////////////////////////////////////////////////////////////
// Using the logger - C++ API
/////////////////////////////////////////////////////////////////////

#include <ostream>

std::ostream *get_logger();

#define logger (*get_logger())

#endif /* __cplusplus */