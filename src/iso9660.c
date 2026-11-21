/* Pembaca ISO9660 minimal (read-only). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "iso9660.h"

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Directory record (ISO9660 9.1):
   [0] len_dr, [2..5] extent LE, [10..13] data_len LE, [25] flags,
   [32] name_len, [33..] name. */
typedef struct {
    unsigned extent;
    unsigned size;
    unsigned flags;
    char name[256];
} ws_dirent_t;

static int parse_dirent(const unsigned char *p, ws_dirent_t *d)
{
    unsigned len_dr = p[0];
    unsigned name_len;
    if (len_dr < 34) return -1;
    d->extent = le32(p + 2);
    d->size = le32(p + 10);
    d->flags = p[25];
    name_len = p[32];
    if (name_len > sizeof(d->name) - 1) name_len = sizeof(d->name) - 1;
    memcpy(d->name, p + 33, name_len);
    d->name[name_len] = '\0';
    return 0;
}

/* Bandingkan nama ISO (boleh bawa ";1") dengan nama yang dicari, tanpa
   memperhatikan huruf besar/kecil dan sufiks versi. */
static int name_match(const char *a, const char *b)
{
    char ca[256], cb[256];
    const char *s;
    size_t n;
    strncpy(ca, a, sizeof(ca) - 1); ca[sizeof(ca) - 1] = '\0';
    strncpy(cb, b, sizeof(cb) - 1); cb[sizeof(cb) - 1] = '\0';
    if ((s = strchr(ca, ';')) != NULL) ca[s - ca] = '\0';
    if ((s = strchr(cb, ';')) != NULL) cb[s - cb] = '\0';
    n = strlen(ca);
    return n == strlen(cb) && strncasecmp(ca, cb, n) == 0;
}

static int iso_open_mode(ws_iso_t *iso, const char *path,
                         const char *mode);

int ws_iso_open(ws_iso_t *iso, const char *path)
{
    return iso_open_mode(iso, path, "rb");
}

/* Buka untuk baca + tulis (ISO patch, Fase 2t). Sama seperti
 * ws_iso_open, tapi file dibuka "rb+" supaya write_data bisa
 * menulis. */
int ws_iso_open_rw(ws_iso_t *iso, const char *path)
{
    return iso_open_mode(iso, path, "rb+");
}

static int iso_open_mode(ws_iso_t *iso, const char *path,
                         const char *mode)
{
    unsigned char pvd[WS_ISO_SECTOR];
    ws_dirent_t root;
    /* Ukuran sektor yang dicoba, ala ManaGunZ get_SectorSize. */
    static const unsigned sizes[4] = { 2048, 2352, 2336, 2448 };
    static const unsigned offs[3] = { 0, 16, 24 };
    /* Tanda PVD: type 1 + "CD001" (dicek 6 byte saja, lihat bawah). */
    unsigned char sig[8];
    int i, j, found = 0;
    memset(iso, 0, sizeof(*iso));
    iso->sector_size = WS_ISO_SECTOR;
    iso->data_off = 0;
    iso->f = fopen(path, mode);
    if (!iso->f) return -1;
    /* Cari PVD di sektor 16 untuk tiap (ukuran sektor, offset data).
       Ala ManaGunZ get_SectorSize: langsung coba fseek+fread tiap
       kombinasi, TANPA ftell/ukuran file sebagai gerbang — ftell di
       PS3 terbukti tidak bisa diandalkan (16 game gagal padahal
       PVD-nya ada; webMAN/IrisMAN/ManaGunZ tidak pakai ftell di
       sini). Tanda PVD: type 1 + "CD001" (6 byte, ala IrisMAN —
       tidak mengecek byte versi/byte 7). */
    for (i = 0; i < 4 && !found; i++) {
        for (j = 0; j < 3 && !found; j++) {
            long pos = (long)sizes[i] * 16 + (long)offs[j];
            fseek(iso->f, pos, SEEK_SET);
            if (fread(sig, 1, 8, iso->f) != 8) continue;
            if (sig[0] == 0x01 && memcmp(sig + 1, "CD001", 5) == 0) {
                iso->sector_size = sizes[i];
                iso->data_off = offs[j];
                found = 1;
            }
        }
    }
    if (!found) {
        fclose(iso->f);
        iso->f = NULL;
        return -1;
    }
    /* PVD: 2048 byte data mulai di sektor 16 + data_off; root record
       di offset 156 di dalam data itu. */
    fseek(iso->f, (long)iso->sector_size * 16 + (long)iso->data_off,
          SEEK_SET);
    if (fread(pvd, 1, WS_ISO_SECTOR, iso->f) != WS_ISO_SECTOR) {
        fclose(iso->f);
        iso->f = NULL;
        return -1;
    }
    if (parse_dirent(pvd + 156, &root) != 0) {
        fclose(iso->f);
        iso->f = NULL;
        return -1;
    }
    iso->root_extent = root.extent;
    iso->root_size = root.size;
    return 0;
}

void ws_iso_close(ws_iso_t *iso)
{
    if (iso->f) fclose(iso->f);
    iso->f = NULL;
}

/* Baca `len` byte DATA mulai dari offset data `off` di dalam file /
 * direktori yang bermula di `extent`. Data tersusun per blok 2048 byte;
 * tiap blok tinggal di sektor `extent + i` pada offset
 * `sektor*sector_size + data_off + (posisi di dalam blok)`. Untuk ISO
 * polos (2048/0) ini satu fread berurutan; untuk BIN mentah, celah
 * antar sektor (sektor 2352 -> 304 byte, dst.) dilewati. Kembalikan
 * 0 kalau semua byte terbaca, -1 kalau tidak. */
static int read_data(ws_iso_t *iso, unsigned extent, unsigned off,
                     unsigned char *buf, size_t len)
{
    size_t done = 0;
    if (len == 0) return 0;
    if (iso->sector_size == WS_ISO_SECTOR && iso->data_off == 0) {
        fseek(iso->f, (long)extent * WS_ISO_SECTOR + (long)off, SEEK_SET);
        return fread(buf, 1, len, iso->f) == len ? 0 : -1;
    }
    while (done < len) {
        unsigned pos = off + (unsigned)done;
        unsigned blk = pos / WS_ISO_SECTOR;      /* blok data 2048 */
        unsigned in_blk = pos % WS_ISO_SECTOR;   /* posisi di blok */
        unsigned chunk = WS_ISO_SECTOR - in_blk;
        long fpos;
        if (chunk > len - done) chunk = (unsigned)(len - done);
        fpos = ((long)(extent + blk)) * (long)iso->sector_size +
               (long)iso->data_off + (long)in_blk;
        fseek(iso->f, fpos, SEEK_SET);
        if (fread(buf + done, 1, chunk, iso->f) != chunk) return -1;
        done += chunk;
    }
    return 0;
}

/* Cari satu entri bernama `name` di direktori (extent, size). */
static int find_in_dir(ws_iso_t *iso, unsigned extent, unsigned size,
                       const char *name, ws_dirent_t *out)
{
    unsigned char *buf;
    unsigned off = 0;
    int found = -1;
    if (size == 0) return -1;
    buf = (unsigned char *)malloc(size);
    if (!buf) return -1;
    if (read_data(iso, extent, 0, buf, size) != 0) {
        free(buf);
        return -1;
    }
    while (off < size) {
        unsigned len_dr = buf[off];
        ws_dirent_t d;
        if (len_dr == 0) {
            /* Padding sampai batas sektor berikutnya. */
            off = (off / WS_ISO_SECTOR + 1) * WS_ISO_SECTOR;
            continue;
        }
        if (off + len_dr > size) break;
        if (parse_dirent(buf + off, &d) == 0 &&
            d.name[0] != '\0' && d.name[0] != 0 &&
            strcmp(d.name, ".") != 0 && strcmp(d.name, "..") != 0 &&
            name_match(d.name, name)) {
            *out = d;
            found = 0;
            break;
        }
        off += len_dr;
    }
    free(buf);
    return found;
}

static int list_dir_raw(ws_iso_t *iso, unsigned extent, unsigned size,
                        char names[][64], int max_names)
{
    unsigned char *buf;
    unsigned off = 0;
    int n = 0;
    if (size == 0) return -1;
    buf = (unsigned char *)malloc(size);
    if (!buf) return -1;
    if (read_data(iso, extent, 0, buf, size) != 0) {
        free(buf);
        return -1;
    }
    while (off < size && n < max_names) {
        unsigned len_dr = buf[off];
        ws_dirent_t d;
        if (len_dr == 0) {
            off = (off / WS_ISO_SECTOR + 1) * WS_ISO_SECTOR;
            continue;
        }
        if (off + len_dr > size) break;
        if (parse_dirent(buf + off, &d) == 0 &&
            d.name[0] != '\0' && d.name[0] != 0 && d.name[0] != '\x01' &&
            strcmp(d.name, ".") != 0 && strcmp(d.name, "..") != 0) {
            char *semi = strchr(d.name, ';');
            size_t ln = semi ? (size_t)(semi - d.name) : strlen(d.name);
            if (ln > 63) ln = 63;
            memcpy(names[n], d.name, ln);
            names[n][ln] = '\0';
            n++;
        }
        off += len_dr;
    }
    free(buf);
    return n;
}

/* Resolusi path: komponen dipisah '/', masing-masing dicari di direktori
   sebelumnya (komponen direktori harus punya flag 2). */
static int resolve(ws_iso_t *iso, const char *path, ws_dirent_t *out)
{
    char copy[512];
    char *save = NULL, *comp;
    unsigned extent = iso->root_extent, size = iso->root_size;
    ws_dirent_t d;
    strncpy(copy, path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    comp = strtok_r(copy, "/", &save);
    if (!comp) return -1;
    for (;;) {
        if (find_in_dir(iso, extent, size, comp, &d) != 0) return -1;
        comp = strtok_r(NULL, "/", &save);
        if (!comp) {
            *out = d;
            return 0;
        }
        if (!(d.flags & 2)) return -1;  /* bukan direktori */
        extent = d.extent;
        size = d.size;
    }
}

int ws_iso_read_file(ws_iso_t *iso, const char *path,
                     unsigned char **out, size_t *out_len)
{
    ws_dirent_t d;
    unsigned char *buf;
    *out = NULL;
    *out_len = 0;
    if (resolve(iso, path, &d) != 0) return -1;
    if (d.flags & 2) return -1;  /* direktori, bukan file */
    buf = (unsigned char *)malloc(d.size ? d.size : 1);
    if (!buf) return -1;
    if (d.size && read_data(iso, d.extent, 0, buf, d.size) != 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_len = d.size;
    return 0;
}

int ws_iso_file_offset(ws_iso_t *iso, const char *path,
                       unsigned *extent_out, unsigned *size_out)
{
    ws_dirent_t d;
    if (resolve(iso, path, &d) != 0) return -1;
    if (d.flags & 2) return -1;  /* direktori, bukan file */
    *extent_out = d.extent;
    *size_out = d.size;
    return 0;
}

int ws_iso_write_data(ws_iso_t *iso, unsigned extent, unsigned off,
                      const unsigned char *buf, size_t len)
{
    size_t done = 0;
    if (len == 0) return 0;
    if (iso->sector_size == WS_ISO_SECTOR && iso->data_off == 0) {
        fseek(iso->f, (long)extent * WS_ISO_SECTOR + (long)off, SEEK_SET);
        return fwrite(buf, 1, len, iso->f) == len ? 0 : -1;
    }
    while (done < len) {
        unsigned pos = off + (unsigned)done;
        unsigned blk = pos / WS_ISO_SECTOR;
        unsigned in_blk = pos % WS_ISO_SECTOR;
        unsigned chunk = WS_ISO_SECTOR - in_blk;
        long fpos;
        if (chunk > len - done) chunk = (unsigned)(len - done);
        fpos = ((long)(extent + blk)) * (long)iso->sector_size +
               (long)iso->data_off + (long)in_blk;
        fseek(iso->f, fpos, SEEK_SET);
        if (fwrite(buf + done, 1, chunk, iso->f) != chunk) return -1;
        done += chunk;
    }
    return 0;
}

int ws_iso_list_dir(ws_iso_t *iso, const char *path,
                    char names[][64], int max_names)
{
    ws_dirent_t d;
    unsigned extent = iso->root_extent, size = iso->root_size;
    if (path && path[0] && strcmp(path, "/") != 0) {
        if (resolve(iso, path, &d) != 0) return -1;
        if (!(d.flags & 2)) return -1;
        extent = d.extent;
        size = d.size;
    }
    return list_dir_raw(iso, extent, size, names, max_names);
}
