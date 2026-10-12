#pragma once

// Central version definition for the Karst project.
// Bump these when you cut a build; Karst.rc carries the same numbers.

#define KARST_VERSION_MAJOR      0
#define KARST_VERSION_MINOR      1
#define KARST_VERSION_PATCH      0
#define KARST_VERSION_BUILD      0

#define KARST_NAME               "Karst"
#define KARST_NAME_WSTR          L"Karst"

#define _KARST_STR_IMPL(x)       #x
#define _KARST_STR(x)            _KARST_STR_IMPL(x)
#define _KARST_WSTR_IMPL(x)      L##x
#define _KARST_WSTR(x)           _KARST_WSTR_IMPL(x)

// "0.1.0"
#define KARST_VERSION_STR        \
	_KARST_STR(KARST_VERSION_MAJOR) "." \
	_KARST_STR(KARST_VERSION_MINOR) "." \
	_KARST_STR(KARST_VERSION_PATCH)

#define KARST_VERSION_WSTR       _KARST_WSTR(KARST_VERSION_STR)

// "0.1.0.0" - keep in sync with FILEVERSION/PRODUCTVERSION in Karst.rc
#define KARST_VERSION_FULL_STR   KARST_VERSION_STR ".0"
#define KARST_VERSION_FULL_WSTR  _KARST_WSTR(KARST_VERSION_FULL_STR)

// Log banner, e.g. L"Karst 0.1.0 is active."
#define KARST_ACTIVE_MESSAGE     KARST_NAME_WSTR L" " KARST_VERSION_WSTR L" is active."
