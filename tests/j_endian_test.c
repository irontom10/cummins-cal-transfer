#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "j_endian.h"

int
main(void)
{
    j_u8 buf[6];
    unsigned int i;
    const j_u8 b[4] = { 0x12U, 0x34U, 0x56U, 0x78U };

    assert(sizeof(j_u8) * CHAR_BIT == 8U);
    assert(sizeof(j_u16) * CHAR_BIT == 16U);
    assert(sizeof(j_u32) * CHAR_BIT == 32U);
    assert(sizeof(j_i8) * CHAR_BIT == 8U);
    assert(sizeof(j_i16) * CHAR_BIT == 16U);
    assert(sizeof(j_i32) * CHAR_BIT == 32U);
    assert(J_U16_MAX == 0xFFFFU);
    assert(J_U32_MAX == 0xFFFFFFFFUL);

    assert(j_load_be16(b) == 0x1234U);
    assert(j_load_be32(b) == 0x12345678UL);
    assert(j_load_le16(b) == 0x3412U);
    assert(j_load_le32(b) == 0x78563412UL);

    assert(j_load_be_width(b, 1U) == 0x12U);
    assert(j_load_be_width(b, 2U) == 0x1234U);
    assert(j_load_be_width(b, 3U) == 0x123456U);
    assert(j_load_be_width(b, 4U) == 0x12345678UL);
    assert(j_load_le_width(b, 1U) == 0x12U);
    assert(j_load_le_width(b, 2U) == 0x3412U);
    assert(j_load_le_width(b, 3U) == 0x563412UL);
    assert(j_load_le_width(b, 4U) == 0x78563412UL);

    for (i = 0U; i < sizeof(buf); ++i)
        buf[i] = 0xAAU;

    /* Deliberately unaligned addresses; no pointer casts inside helpers. */
    j_store_be16(buf + 1, (j_u16)0xABCDU);
    assert(buf[0] == 0xAAU && buf[1] == 0xABU);
    assert(buf[2] == 0xCDU && buf[3] == 0xAAU);
    assert(j_load_be16(buf + 1) == 0xABCDU);

    j_store_be32(buf + 1, (j_u32)0x89ABCDEFUL);
    assert(buf[1] == 0x89U && buf[2] == 0xABU);
    assert(buf[3] == 0xCDU && buf[4] == 0xEFU);
    assert(j_load_be32(buf + 1) == 0x89ABCDEFUL);

    j_store_le16(buf + 1, (j_u16)0xABCDU);
    assert(buf[1] == 0xCDU && buf[2] == 0xABU);
    assert(j_load_le16(buf + 1) == 0xABCDU);

    j_store_le32(buf + 1, (j_u32)0x89ABCDEFUL);
    assert(buf[1] == 0xEFU && buf[2] == 0xCDU);
    assert(buf[3] == 0xABU && buf[4] == 0x89U);
    assert(j_load_le32(buf + 1) == 0x89ABCDEFUL);

    puts("j_int/j_endian tests passed");
    return 0;
}
