// Copyright (c) REAN Studios. MIT License.
//
// ReanCore: the engine-independent part of the REAN SDK (plain C++17, no
// Unreal headers). Inside Unreal it is the "ReanCore" module; outside it is
// built with CMake (see ReanCore/CMakeLists.txt) and unit-tested.
#pragma once

// REAN_CORE_SHARED is 1 when ReanCore is its own shared library (Unreal
// modular builds: the editor, development builds with -modular). The Unreal
// module sets it from ReanCore.Build.cs; CMake builds leave it undefined.
#if defined(REAN_CORE_SHARED) && REAN_CORE_SHARED
#  if defined(_WIN32)
#    if defined(REAN_CORE_BUILDING) && REAN_CORE_BUILDING
#      define REAN_CORE_API __declspec(dllexport)
#    else
#      define REAN_CORE_API __declspec(dllimport)
#    endif
#  else
#    define REAN_CORE_API __attribute__((visibility("default")))
#  endif
#else
#  define REAN_CORE_API
#endif

// Exported classes hold std:: members; MSVC warns about that (C4251) even
// though both sides are built with the same compiler and runtime.
#if defined(_MSC_VER)
#  define REAN_CORE_WARNINGS_PUSH __pragma(warning(push)) __pragma(warning(disable : 4251 4275))
#  define REAN_CORE_WARNINGS_POP __pragma(warning(pop))
#else
#  define REAN_CORE_WARNINGS_PUSH
#  define REAN_CORE_WARNINGS_POP
#endif

#define REAN_CORE_VERSION "0.1.0"
