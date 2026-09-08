#pragma once

// Function declarations and lambda attributes require different syntax.
#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define HOSQ_ALWAYS_INLINE inline HOSQ_ALWAYS_INLINE_ATTR
#elif defined(_MSC_VER)
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_ALWAYS_INLINE __forceinline
#else
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_ALWAYS_INLINE inline
#endif
