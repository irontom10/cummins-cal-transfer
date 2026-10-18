#ifndef J_ENDIAN_H
#define J_ENDIAN_H

#include "j_int.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Byte-order conversion without alignment or host-endianness assumptions.
 * p must point to at least 2 or 4 accessible bytes, as appropriate.
 */
j_u16 j_load_be16(const j_u8 *p);
j_u32 j_load_be32(const j_u8 *p);
j_u16 j_load_le16(const j_u8 *p);
j_u32 j_load_le32(const j_u8 *p);

/*
 * width must be between 1 and 4 inclusive; p must contain width bytes.
 * Results are zero-extended into a 32-bit unsigned integer.
 */
j_u32 j_load_be_width(const j_u8 *p, unsigned int width);
j_u32 j_load_le_width(const j_u8 *p, unsigned int width);

/* p must point to at least 2 or 4 writable bytes, as appropriate. */
void j_store_be16(j_u8 *p, j_u16 value);
void j_store_be32(j_u8 *p, j_u32 value);
void j_store_le16(j_u8 *p, j_u16 value);
void j_store_le32(j_u8 *p, j_u32 value);

#ifdef __cplusplus
}
#endif

#endif /* J_ENDIAN_H */
