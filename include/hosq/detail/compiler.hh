#pragma once

// Function declarations and lambda attributes require different syntax.
#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define HOSQ_ALWAYS_INLINE inline HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_COLD_NOINLINE __attribute__((cold, noinline))
#elif defined(_MSC_VER)
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_ALWAYS_INLINE __forceinline
#define HOSQ_COLD_NOINLINE __declspec(noinline)
#else
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_ALWAYS_INLINE inline
#define HOSQ_COLD_NOINLINE
#endif
