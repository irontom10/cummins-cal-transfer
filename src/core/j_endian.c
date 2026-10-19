#include "j_endian.h"

j_u16
j_load_be16(const j_u8 *p)
{
    return (j_u16)(((j_u16)p[0] << 8) | (j_u16)p[1]);
}

j_u32
j_load_be32(const j_u8 *p)
{
    return ((j_u32)p[0] << 24) |
           ((j_u32)p[1] << 16) |
           ((j_u32)p[2] << 8)  |
           (j_u32)p[3];
}

j_u16
j_load_le16(const j_u8 *p)
{
    return (j_u16)((j_u16)p[0] | ((j_u16)p[1] << 8));
}

j_u32
j_load_le32(const j_u8 *p)
{
    return (j_u32)p[0]         |
           ((j_u32)p[1] << 8)  |
           ((j_u32)p[2] << 16) |
           ((j_u32)p[3] << 24);
}

j_u32
j_load_be_width(const j_u8 *p, unsigned int width)
{
    j_u32 value;
    unsigned int i;

    value = (j_u32)0;
    for (i = 0U; i < width; ++i)
        value = (value << 8) | (j_u32)p[i];
    return value;
}

j_u32
j_load_le_width(const j_u8 *p, unsigned int width)
{
    j_u32 value;
    unsigned int i;

    value = (j_u32)0;
    for (i = 0U; i < width; ++i)
        value |= (j_u32)p[i] << (i * 8U);
    return value;
}

void
j_store_be16(j_u8 *p, j_u16 value)
{
    p[0] = (j_u8)(value >> 8);
    p[1] = (j_u8)value;
}

void
j_store_be32(j_u8 *p, j_u32 value)
{
    p[0] = (j_u8)(value >> 24);
    p[1] = (j_u8)(value >> 16);
    p[2] = (j_u8)(value >> 8);
    p[3] = (j_u8)value;
}

void
j_store_le16(j_u8 *p, j_u16 value)
{
    p[0] = (j_u8)value;
    p[1] = (j_u8)(value >> 8);
}

void
j_store_le32(j_u8 *p, j_u32 value)
{
    p[0] = (j_u8)value;
    p[1] = (j_u8)(value >> 8);
    p[2] = (j_u8)(value >> 16);
    p[3] = (j_u8)(value >> 24);
}
