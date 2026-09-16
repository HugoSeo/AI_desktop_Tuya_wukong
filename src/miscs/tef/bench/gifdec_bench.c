/**
 * @file gifdec_bench.c
 * @brief 基准测试专用 GIF 解码器实现。移植自 LVGL 8.3
 *        src/extra/libs/gif/gifdec.c（lecram/gifdec 派生，MIT/公有领域），
 *        改动仅限：去 lv_fs/lv_mem/lv_color 依赖、固定 LV_COLOR_DEPTH==16
 *        路径、分配器带峰值统计。解码/合成算法逐行保持原样，保证 CPU
 *        开销与旧 lv_gif 路径可比。
 */
#if defined(TEF_BENCH) && (TEF_BENCH == 1)

#include "gifdec_bench.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

#define GDB_SEEK_SET 0
#define GDB_SEEK_CUR 1

/* RGB565 打包（等价 LV_COLOR_DEPTH==16 的 lv_color_make().full） */
#define RGB565(r, g, b) \
    ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* ---- 带峰值统计的分配器（8 字节头记录块大小） ---- */
static uint32_t s_cur, s_peak;

static void *gdb_alloc(size_t n)
{
    uint8_t *p = malloc(n + 8);
    if (p == NULL) {
        return NULL;
    }
    *(uint32_t *)p = (uint32_t)n;
    s_cur += (uint32_t)n;
    if (s_cur > s_peak) {
        s_peak = s_cur;
    }
    return p + 8;
}

static void gdb_free(void *p)
{
    if (p == NULL) {
        return;
    }
    uint8_t *raw = (uint8_t *)p - 8;
    s_cur -= *(uint32_t *)raw;
    free(raw);
}

static void *gdb_realloc(void *p, size_t n)
{
    if (p == NULL) {
        return gdb_alloc(n);
    }
    uint8_t *raw = (uint8_t *)p - 8;
    uint32_t old = *(uint32_t *)raw;
    uint8_t *np = realloc(raw, n + 8);
    if (np == NULL) {
        return NULL;
    }
    *(uint32_t *)np = (uint32_t)n;
    s_cur += (uint32_t)n - old;
    if (s_cur > s_peak) {
        s_peak = s_cur;
    }
    return np + 8;
}

void gdb_alloc_stat_reset(void) { s_cur = 0; s_peak = 0; }
uint32_t gdb_alloc_stat_cur(void) { return s_cur; }
uint32_t gdb_alloc_stat_peak(void) { return s_peak; }

/* ---- 以下与 LVGL gifdec.c 逐行对应（data 模式） ---- */

typedef struct Entry {
    uint16_t length;
    uint16_t prefix;
    uint8_t  suffix;
} Entry;

typedef struct Table {
    int bulk;
    int nentries;
    Entry *entries;
} Table;

static void f_gif_read(gdb_GIF *gif, void *buf, size_t len)
{
    memcpy(buf, &gif->data[gif->f_rw_p], len);
    gif->f_rw_p += len;
}

static int f_gif_seek(gdb_GIF *gif, size_t pos, int k)
{
    if (k == GDB_SEEK_CUR) {
        gif->f_rw_p += pos;
    } else {
        gif->f_rw_p = pos;
    }
    return gif->f_rw_p;
}

static uint16_t read_num(gdb_GIF *gif)
{
    uint8_t bytes[2];

    f_gif_read(gif, bytes, 2);
    return bytes[0] + (((uint16_t)bytes[1]) << 8);
}

gdb_GIF *gdb_open_gif_data(const void *data)
{
    uint8_t sigver[3];
    uint16_t width, height, depth;
    uint8_t fdsz, bgidx, aspect;
    int i;
    uint8_t *bgcolor;
    int gct_sz;
    gdb_GIF gif_base;
    gdb_GIF *gif = NULL;

    memset(&gif_base, 0, sizeof(gif_base));
    gif_base.data = data;

    f_gif_read(&gif_base, sigver, 3);
    if (memcmp(sigver, "GIF", 3) != 0) {
        return NULL;
    }
    f_gif_read(&gif_base, sigver, 3);
    if (memcmp(sigver, "89a", 3) != 0) {
        return NULL;
    }
    width  = read_num(&gif_base);
    height = read_num(&gif_base);
    f_gif_read(&gif_base, &fdsz, 1);
    if (!(fdsz & 0x80)) {
        return NULL;
    }
    depth = ((fdsz >> 4) & 7) + 1;
    gct_sz = 1 << ((fdsz & 0x07) + 1);
    f_gif_read(&gif_base, &bgidx, 1);
    f_gif_read(&gif_base, &aspect, 1);
    (void)aspect;

    /* LV_COLOR_DEPTH==16: canvas 3B/px + frame 1B/px，单块分配（与 lv_gif 一致） */
    gif = gdb_alloc(sizeof(gdb_GIF) + 4 * width * height);
    if (!gif) {
        return NULL;
    }
    memcpy(gif, &gif_base, sizeof(gdb_GIF));
    gif->width  = width;
    gif->height = height;
    gif->depth  = depth;
    gif->gct.size = gct_sz;
    f_gif_read(gif, gif->gct.colors, 3 * gif->gct.size);
    gif->palette = &gif->gct;
    gif->bgindex = bgidx;
    gif->canvas = (uint8_t *)&gif[1];
    gif->frame = &gif->canvas[3 * width * height];
    if (gif->bgindex) {
        memset(gif->frame, gif->bgindex, gif->width * gif->height);
    }
    bgcolor = &gif->palette->colors[gif->bgindex * 3];
    for (i = 0; i < gif->width * gif->height; i++) {
        uint16_t c = RGB565(*(bgcolor + 0), *(bgcolor + 1), *(bgcolor + 2));
        gif->canvas[i * 3 + 0] = c & 0xff;
        gif->canvas[i * 3 + 1] = (c >> 8) & 0xff;
        gif->canvas[i * 3 + 2] = 0xff;
    }
    gif->anim_start = f_gif_seek(gif, 0, GDB_SEEK_CUR);
    gif->loop_count = -1;
    return gif;
}

static void discard_sub_blocks(gdb_GIF *gif)
{
    uint8_t size;

    do {
        f_gif_read(gif, &size, 1);
        f_gif_seek(gif, size, GDB_SEEK_CUR);
    } while (size);
}

static void read_graphic_control_ext(gdb_GIF *gif)
{
    uint8_t rdit;

    f_gif_seek(gif, 1, GDB_SEEK_CUR);
    f_gif_read(gif, &rdit, 1);
    gif->gce.disposal = (rdit >> 2) & 3;
    gif->gce.input = rdit & 2;
    gif->gce.transparency = rdit & 1;
    gif->gce.delay = read_num(gif);
    f_gif_read(gif, &gif->gce.tindex, 1);
    f_gif_seek(gif, 1, GDB_SEEK_CUR);
}

static void read_application_ext(gdb_GIF *gif)
{
    char app_id[8];
    char app_auth_code[3];
    uint16_t loop_count;

    f_gif_seek(gif, 1, GDB_SEEK_CUR);
    f_gif_read(gif, app_id, 8);
    f_gif_read(gif, app_auth_code, 3);
    if (!strncmp(app_id, "NETSCAPE", sizeof(app_id))) {
        f_gif_seek(gif, 2, GDB_SEEK_CUR);
        loop_count = read_num(gif);
        if (gif->loop_count < 0) {
            gif->loop_count = (loop_count == 0) ? 0 : loop_count + 1;
        }
        f_gif_seek(gif, 1, GDB_SEEK_CUR);
    } else {
        discard_sub_blocks(gif);
    }
}

static void read_ext(gdb_GIF *gif)
{
    uint8_t label;

    f_gif_read(gif, &label, 1);
    switch (label) {
    case 0x01:                                /* plain text：丢弃 */
        f_gif_seek(gif, 13, GDB_SEEK_CUR);
        discard_sub_blocks(gif);
        break;
    case 0xF9:
        read_graphic_control_ext(gif);
        break;
    case 0xFE:                                /* comment：丢弃 */
        discard_sub_blocks(gif);
        break;
    case 0xFF:
        read_application_ext(gif);
        break;
    default:
        break;
    }
}

static Table *new_table(int key_size)
{
    int key;
    int init_bulk = MAX(1 << (key_size + 1), 0x100);
    Table *table = gdb_alloc(sizeof(*table) + sizeof(Entry) * init_bulk);

    if (table) {
        table->bulk = init_bulk;
        table->nentries = (1 << key_size) + 2;
        table->entries = (Entry *)&table[1];
        for (key = 0; key < (1 << key_size); key++) {
            table->entries[key] = (Entry) {1, 0xFFF, key};
        }
    }
    return table;
}

static int add_entry(Table **tablep, uint16_t length, uint16_t prefix, uint8_t suffix)
{
    Table *table = *tablep;

    if (table->nentries == table->bulk) {
        table->bulk *= 2;
        table = gdb_realloc(table, sizeof(*table) + sizeof(Entry) * table->bulk);
        if (!table) {
            return -1;
        }
        table->entries = (Entry *)&table[1];
        *tablep = table;
    }
    table->entries[table->nentries] = (Entry) {length, prefix, suffix};
    table->nentries++;
    if ((table->nentries & (table->nentries - 1)) == 0) {
        return 1;
    }
    return 0;
}

static uint16_t get_key(gdb_GIF *gif, int key_size, uint8_t *sub_len, uint8_t *shift, uint8_t *byte)
{
    int bits_read;
    int rpad;
    int frag_size;
    uint16_t key;

    key = 0;
    for (bits_read = 0; bits_read < key_size; bits_read += frag_size) {
        rpad = (*shift + bits_read) % 8;
        if (rpad == 0) {
            if (*sub_len == 0) {
                f_gif_read(gif, sub_len, 1);
                if (*sub_len == 0) {
                    return 0x1000;
                }
            }
            f_gif_read(gif, byte, 1);
            (*sub_len)--;
        }
        frag_size = MIN(key_size - bits_read, 8 - rpad);
        key |= ((uint16_t)((*byte) >> rpad)) << bits_read;
    }
    key &= (1 << key_size) - 1;
    *shift = (*shift + key_size) % 8;
    return key;
}

static int interlaced_line_index(int h, int y)
{
    int p;

    p = (h - 1) / 8 + 1;
    if (y < p) {
        return y * 8;
    }
    y -= p;
    p = (h - 5) / 8 + 1;
    if (y < p) {
        return y * 8 + 4;
    }
    y -= p;
    p = (h - 3) / 4 + 1;
    if (y < p) {
        return y * 4 + 2;
    }
    y -= p;
    return y * 2 + 1;
}

static int read_image_data(gdb_GIF *gif, int interlace)
{
    uint8_t sub_len, shift, byte;
    int init_key_size, key_size, table_is_full = 0;
    int frm_off, frm_size, str_len = 0, i, p, x, y;
    uint16_t key, clear, stop;
    int ret;
    Table *table;
    Entry entry = {0};
    size_t start, end;

    f_gif_read(gif, &byte, 1);
    key_size = (int)byte;
    start = f_gif_seek(gif, 0, GDB_SEEK_CUR);
    discard_sub_blocks(gif);
    end = f_gif_seek(gif, 0, GDB_SEEK_CUR);
    f_gif_seek(gif, start, GDB_SEEK_SET);
    clear = 1 << key_size;
    stop = clear + 1;
    table = new_table(key_size);
    key_size++;
    init_key_size = key_size;
    sub_len = shift = 0;
    key = get_key(gif, key_size, &sub_len, &shift, &byte);
    frm_off = 0;
    ret = 0;
    frm_size = gif->fw * gif->fh;
    while (frm_off < frm_size) {
        if (key == clear) {
            key_size = init_key_size;
            table->nentries = (1 << (key_size - 1)) + 2;
            table_is_full = 0;
        } else if (!table_is_full) {
            ret = add_entry(&table, str_len + 1, key, entry.suffix);
            if (ret == -1) {
                gdb_free(table);
                return -1;
            }
            if (table->nentries == 0x1000) {
                ret = 0;
                table_is_full = 1;
            }
        }
        key = get_key(gif, key_size, &sub_len, &shift, &byte);
        if (key == clear) {
            continue;
        }
        if (key == stop || key == 0x1000) {
            break;
        }
        if (ret == 1) {
            key_size++;
        }
        entry = table->entries[key];
        str_len = entry.length;
        for (i = 0; i < str_len; i++) {
            p = frm_off + entry.length - 1;
            x = p % gif->fw;
            y = p / gif->fw;
            if (interlace) {
                y = interlaced_line_index((int)gif->fh, y);
            }
            gif->frame[(gif->fy + y) * gif->width + gif->fx + x] = entry.suffix;
            if (entry.prefix == 0xFFF) {
                break;
            }
            entry = table->entries[entry.prefix];
        }
        frm_off += str_len;
        if (key < table->nentries - 1 && !table_is_full) {
            table->entries[table->nentries - 1].suffix = entry.suffix;
        }
    }
    gdb_free(table);
    if (key == stop) {
        f_gif_read(gif, &sub_len, 1);
    }
    f_gif_seek(gif, end, GDB_SEEK_SET);
    return 0;
}

static int read_image(gdb_GIF *gif)
{
    uint8_t fisrz;
    int interlace;

    gif->fx = read_num(gif);
    gif->fy = read_num(gif);
    gif->fw = read_num(gif);
    gif->fh = read_num(gif);
    f_gif_read(gif, &fisrz, 1);
    interlace = fisrz & 0x40;
    if (fisrz & 0x80) {
        gif->lct.size = 1 << ((fisrz & 0x07) + 1);
        f_gif_read(gif, gif->lct.colors, 3 * gif->lct.size);
        gif->palette = &gif->lct;
    } else {
        gif->palette = &gif->gct;
    }
    return read_image_data(gif, interlace);
}

static void render_frame_rect(gdb_GIF *gif, uint8_t *buffer)
{
    int i, j, k;
    uint8_t index, *color;

    i = gif->fy * gif->width + gif->fx;
    for (j = 0; j < gif->fh; j++) {
        for (k = 0; k < gif->fw; k++) {
            index = gif->frame[(gif->fy + j) * gif->width + gif->fx + k];
            color = &gif->palette->colors[index * 3];
            if (!gif->gce.transparency || index != gif->gce.tindex) {
                uint16_t c = RGB565(*(color + 0), *(color + 1), *(color + 2));
                buffer[(i + k) * 3 + 0] = c & 0xff;
                buffer[(i + k) * 3 + 1] = (c >> 8) & 0xff;
                buffer[(i + k) * 3 + 2] = 0xff;
            }
        }
        i += gif->width;
    }
}

static void dispose(gdb_GIF *gif)
{
    int i, j, k;
    uint8_t *bgcolor;

    switch (gif->gce.disposal) {
    case 2: {
        bgcolor = &gif->palette->colors[gif->bgindex * 3];
        uint8_t opa = gif->gce.transparency ? 0x00 : 0xff;

        i = gif->fy * gif->width + gif->fx;
        for (j = 0; j < gif->fh; j++) {
            for (k = 0; k < gif->fw; k++) {
                uint16_t c = RGB565(*(bgcolor + 0), *(bgcolor + 1), *(bgcolor + 2));
                gif->canvas[(i + k) * 3 + 0] = c & 0xff;
                gif->canvas[(i + k) * 3 + 1] = (c >> 8) & 0xff;
                gif->canvas[(i + k) * 3 + 2] = opa;
            }
            i += gif->width;
        }
    } break;
    case 3:
        break;
    default:
        render_frame_rect(gif, gif->canvas);
    }
}

int gdb_get_frame(gdb_GIF *gif)
{
    char sep;

    dispose(gif);
    f_gif_read(gif, &sep, 1);
    while (sep != ',') {
        if (sep == ';') {
            f_gif_seek(gif, gif->anim_start, GDB_SEEK_SET);
            if (gif->loop_count == 1 || gif->loop_count < 0) {
                return 0;
            } else if (gif->loop_count > 1) {
                gif->loop_count--;
            }
        } else if (sep == '!') {
            read_ext(gif);
        } else {
            return -1;
        }
        f_gif_read(gif, &sep, 1);
    }
    if (read_image(gif) == -1) {
        return -1;
    }
    return 1;
}

void gdb_render_frame(gdb_GIF *gif, uint8_t *buffer)
{
    render_frame_rect(gif, buffer);
}

void gdb_close_gif(gdb_GIF *gif)
{
    gdb_free(gif);
}

#endif /* TEF_BENCH */
