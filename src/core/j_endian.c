static clip_u32
clip_cal_load_be32(const clip_u8 *p)
{
    clip_u32 v;

    v = ((clip_u32)p[0] << 24);
    v |= ((clip_u32)p[1] << 16);
    v |= ((clip_u32)p[2] << 8);
    v |= (clip_u32)p[3];
    return v;
}

static unsigned int
clip_cal_load_be16(const clip_u8 *p)
{
    unsigned int v;

    v = ((unsigned int)p[0] << 8);
    v |= (unsigned int)p[1];
    return v;
}

static clip_u32
clip_cal_load_be_width(const clip_u8 *p, unsigned int width)
{
    clip_u32 v;
    unsigned int i;

    v = (clip_u32)0;
    for (i = 0U; i < width; ++i)
        v = (v << 8) | (clip_u32)p[i];
    return v;
}

static void
clip_cal_store_be32(clip_u8 *p, clip_u32 v)
{
    p[0] = (clip_u8)(v >> 24);
    p[1] = (clip_u8)(v >> 16);
    p[2] = (clip_u8)(v >> 8);
    p[3] = (clip_u8)v;
}

static void
clip_cal_store_be16(clip_u8 *p, unsigned int v)
{
    p[0] = (clip_u8)((v >> 8) & 0xffU);
    p[1] = (clip_u8)(v & 0xffU);
}

