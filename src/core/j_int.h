#ifndef J_INT_H
#define J_INT_H

#include <limits.h>

/*
 * Exact-width integer types for ISO C89.
 *
 * These typedefs require integer types with exactly 8, 16, and 32 bits.
 * C89 does not provide a portable, guaranteed 64-bit integer type.
 */

#if CHAR_BIT != 8 || UCHAR_MAX != 0xFFU
#error "j_int.h requires 8-bit bytes"
#endif

typedef signed char   j_i8;
typedef unsigned char j_u8;

#define J_I8_MIN SCHAR_MIN
#define J_I8_MAX SCHAR_MAX
#define J_U8_MAX UCHAR_MAX

#if USHRT_MAX == 0xFFFFU
typedef signed short   j_i16;
typedef unsigned short j_u16;
#define J_I16_MIN SHRT_MIN
#define J_I16_MAX SHRT_MAX
#define J_U16_MAX USHRT_MAX
#elif UINT_MAX == 0xFFFFU
typedef signed int   j_i16;
typedef unsigned int j_u16;
#define J_I16_MIN INT_MIN
#define J_I16_MAX INT_MAX
#define J_U16_MAX UINT_MAX
#else
#error "j_int.h requires a 16-bit integer type"
#endif

#if UINT_MAX == 0xFFFFFFFFUL
typedef signed int   j_i32;
typedef unsigned int j_u32;
#define J_I32_MIN INT_MIN
#define J_I32_MAX INT_MAX
#define J_U32_MAX UINT_MAX
#elif ULONG_MAX == 0xFFFFFFFFUL
typedef signed long   j_i32;
typedef unsigned long j_u32;
#define J_I32_MIN LONG_MIN
#define J_I32_MAX LONG_MAX
#define J_U32_MAX ULONG_MAX
#else
#error "j_int.h requires a 32-bit integer type"
#endif

#endif /* J_INT_H */
