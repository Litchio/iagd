// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define UNICODE
#define _UNICODE

// Windows Header Files:
#include <windows.h>
#include <tchar.h>
#include <process.h>
// #include <atlbase.h>  // ATL not needed for MinGW build

#include <codecvt> // wstring_convert
#include <locale>