/**
 * Standard Integer Types for TocinOS
 * 
 * Provides fixed-width integer types for freestanding environment
 */

#ifndef STDINT_H
#define STDINT_H

// Signed integer types
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

// Unsigned integer types
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

// Pointer-sized integers
#ifdef __x86_64__
typedef int64_t            intptr_t;
typedef uint64_t           uintptr_t;
typedef int64_t            ptrdiff_t;
typedef uint64_t           size_t;
#else
typedef int32_t            intptr_t;
typedef uint32_t           uintptr_t;
typedef int32_t            ptrdiff_t;
typedef uint32_t           size_t;
#endif

// Limits
#define INT8_MIN           (-128)
#define INT8_MAX           127
#define UINT8_MAX          255
#define INT16_MIN          (-32768)
#define INT16_MAX          32767
#define UINT16_MAX         65535
#define INT32_MIN          (-2147483648)
#define INT32_MAX          2147483647
#define UINT32_MAX         4294967295U
#define INT64_MIN          (-9223372036854775808LL)
#define INT64_MAX          9223372036854775807LL
#define UINT64_MAX         18446744073709551615ULL

#endif // STDINT_H
