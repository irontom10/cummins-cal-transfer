
#ifndef J_INT_H
#define J_INT_H

#include <limits.h>

/*
 * Fixed-width integer types.
 * ISO C89 compatible.
 */

/* 8-bit integers */
#if CHAR_BIT == 8 && UCHAR_MAX == 0xFFU
typedef signed char   j_i8;
typedef unsigned char j_u8;
#else
#error "No suitable 8-bit integer type"
#endif

/* 16-bit integers */
#if USHRT_MAX == 0xFFFFU
typedef signed short   j_i16;
typedef unsigned short j_u16;
#elif UINT_MAX == 0xFFFFU
typedef signed int     j_i16;
typedef unsigned int   j_u16;
#else
#error "No suitable 16-bit integer type"
#endif

/* 32-bit integers */
#if UINT_MAX == 0xFFFFFFFFUL
typedef signed int   j_i32;
typedef unsigned int j_u32;
#elif ULONG_MAX == 0xFFFFFFFFUL
typedef signed long   j_i32;
typedef unsigned long j_u32;
#else
#error "No suitable 32-bit integer type"
#endif

/* Integer limits */
#define J_U8_MAX   0xFFU
#define J_U16_MAX  0xFFFFU
#define J_U32_MAX  0xFFFFFFFFUL

#endif /* J_INT_H */

