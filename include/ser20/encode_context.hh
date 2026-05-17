#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

#include "ser20/writer_base.hh"

#ifdef __ARM_NEON
#include "ser20/scanner_neon.hh"
#else
#include "ser20/scanner.hh"
#endif

#ifndef SER20_ALWAYS_INLINE
#if defined(__clang__) || defined(__GNUC__)
#define SER20_ALWAYS_INLINE [[gnu::always_inline]] inline
#else
#define SER20_ALWAYS_INLINE inline
#endif
#endif

namespace ser20 {

#ifdef __ARM_NEON
using ParserScanner = ScannerNeon;
#else
using ParserScanner = Scanner;
#endif


}  // namespace ser20
