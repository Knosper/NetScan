#ifndef PLATFORM_HPP
#define PLATFORM_HPP

// Ensure _WIN32_WINNT is defined before any Windows or Windows-dependent
// headers (e.g. cpp-httplib) are included.  The definition must appear in
// this header which is included first in every translation unit that pulls
// in such headers.
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10
#endif
#ifndef WINVER
#define WINVER 0x0A00 // Windows 10
#endif
#endif

#endif
