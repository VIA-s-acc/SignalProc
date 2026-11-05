#pragma once

// GCC at -O3 reports -Wmaybe-uninitialized inside pocketfft's iterators despite -isystem; the CI builds with -Werror.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <pocketfft_hdronly.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
