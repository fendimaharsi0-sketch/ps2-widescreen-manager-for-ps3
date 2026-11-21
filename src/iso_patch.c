/* ISO patch (Fase 2t): tulis patch pnach LANGSUNG ke file ISO.
 * Pola ManaGunZ apply_pnach, minus dua hazard-nya: tulis tepat
 * `size` byte (bukan flat 4), dan translasi tulis per-blok gaya
 * read_data (bukan fseek flat) supaya BIN mentah benar. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "convert.h"
#include "elf.h"
#include "iso9660.h"
#include "iso_patch.h"
#include "report.h"
#include "scan.h"

/* Little-endian eksplisit — PPU PS3 big-endian, jadi jangan memcpy
 * u32 mentah ke file. */
static void put_le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static uint32_t get_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ws_wsrest_path(const char *dir, const char *iso_name,
                    char *out, size_t out_size)
{
    /* "MotoGP 4.iso" -> "<dir>/MotoGP 4.wsrest". */
    char base[256];
    size_t n = strlen(iso_name);
    const char *dot = strrchr(iso_name, '.');
    if (dot && dot != iso_name) n = (size_t)(dot - iso_name);
    if (n > sizeof(base) - 1) n = sizeof(base) - 1;
    memcpy(base, iso_name, n);
    base[n] = '\0';
    snprintf(out, out_size, "%s/%s.wsrest", dir, base);
}

int ws_wsrest_read(const char *path, ws_wsrest_rec_t *recs, int max_recs)
{
    FILE *f = fopen(path, "rb");
    unsigned char raw[WS_WSREST_REC_SIZE];
    size_t got;
    int n = 0;
    if (!f) return -1;
    for (;;) {
        got = fread(raw, 1, sizeof(raw), f);
        if (got == sizeof(raw)) {
            if (n >= max_recs) {
                fclose(f);
                return -2;
            }
            recs[n].offset = get_le32(raw + 0);
            recs[n].size = get_le32(raw + 4);
            if (recs[n].size < 1 || recs[n].size > 4) {
                fclose(f);
                return -2;
            }
            memcpy(recs[n].old_bytes, raw + 8, 4);
            n++;
            continue;
        }
        if (got == 0 && feof(f)) break;  /* ujung file yang bersih */
        /* got 1..11 = record terpotong, atau ferror = gagal baca. */
        fclose(f);
        return -2;
    }
    fclose(f);
    return n;
}

int ws_wsrest_write(const char *path, const ws_wsrest_rec_t *recs,
                    int n_recs)
{
    FILE *f = fopen(path, "wb");
    unsigned char raw[WS_WSREST_REC_SIZE];
    int i;
    if (!f) return -1;
    for (i = 0; i < n_recs; i++) {
        put_le32(raw + 0, recs[i].offset);
        put_le32(raw + 4, recs[i].size);
        memcpy(raw + 8, recs[i].old_bytes, 4);
        if (fwrite(raw, 1, sizeof(raw), f) != sizeof(raw)) {
            fclose(f);
            return -1;
        }
    }
    if (fflush(f) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* Keputusan per baris patch (dipakai preview + apply supaya
 * hitungannya sama): 1 = tulis (fo diisi), 0 = skip (alasan di
 * report). */
static int iso_patch_decision(ws_patch_t *p,
                              const unsigned char *elf, size_t elf_len,
                              uint32_t *fo_out, ws_report_t *report)
{
    if (p->size < 1 || p->size > 4) {
        ws_report_add(report, "info: line %d: unknown type %s, skipped",
                      p->lineno, p->ptype);
        return 0;
    }
    if (strcmp(p->ptype, "extended") == 0 &&
        (p->raw_addr & 0xF0000000ULL) == 0xE0000000ULL) {
        ws_report_add(report,
                      "info: line %d: conditional code (0xE), skipped",
                      p->lineno);
        return 0;
    }
    if (p->addr >= 0x2000000) {
        ws_report_add(report,
                      "warning: line %d: address 0x%08x out of range, skipped",
                      p->lineno, p->addr);
        return 0;
    }
    if (ws_elf_offset(elf, elf_len, p->addr, (uint32_t)p->size, fo_out)
        != 1) {
        ws_report_add(report,
                      "info: line %d: address 0x%08x not in ELF file bytes (BSS/overlay), skipped",
                      p->lineno, p->addr);
        return 0;
    }
    return 1;
}

/* Hitung berapa patch yang akan ditulis / di-skip, tanpa menulis
 * apa-apa (untuk layar konfirmasi). Kembalikan 0 kalau ada yang akan
 * ditulis, 1 kalau sudah ke-patch (semua byte sudah sama), -1 kalau
 * tidak ada yang bisa di-apply / gagal (alasan di report).
 * *n_write = yang akan ditulis, *n_skip = yang di-skip (termasuk
 * yang sudah sama nilainya). */
int ws_iso_patch_preview(const char *dir, const char *iso_name,
                         const char *pnach_text, size_t pnach_len,
                         ws_report_t *report, int *n_write, int *n_skip)
{
    static ws_pnach_t pnach;
    char iso_path[1024];
    char boot[64] = "";
    char err[128];
    ws_iso_t iso;
    unsigned char *elf = NULL;
    size_t elf_len = 0;
    int n_w = 0, n_s = 0, n_same = 0, i;

    *n_write = 0;
    *n_skip = 0;
    memset(&pnach, 0, sizeof(pnach));
    if (ws_parse_pnach(pnach_text, pnach_len, &pnach, report) != 0) {
        ws_report_add(report, "error: cannot parse pnach text");
        return -1;
    }
    if (pnach.n_patches == 0) {
        ws_report_add(report, "error: no active patches in pnach");
        return -1;
    }
    if (ws_boot_name(dir, iso_name, boot, sizeof(boot), err, sizeof(err))
        != 0) {
        ws_report_add(report, "error: boot ELF not found (%s)", err);
        return -1;
    }
    snprintf(iso_path, sizeof(iso_path), "%s/%s", dir, iso_name);
    if (ws_iso_open(&iso, iso_path) != 0) {
        ws_report_add(report, "error: cannot open ISO");
        return -1;
    }
    if (ws_iso_read_file(&iso, boot, &elf, &elf_len) != 0) {
        ws_report_add(report, "error: cannot read boot ELF %s", boot);
        ws_iso_close(&iso);
        return -1;
    }
    ws_iso_close(&iso);
    if (!ws_elf_check(elf, elf_len)) {
        ws_report_add(report, "error: boot ELF is not a 32-bit ELF");
        free(elf);
        return -1;
    }
    for (i = 0; i < pnach.n_patches; i++) {
        ws_patch_t *p = &pnach.patches[i];
        uint32_t fo;
        unsigned char new_bytes[4];
        int k;
        if (!iso_patch_decision(p, elf, elf_len, &fo, report)) {
            n_s++;
            continue;
        }
        for (k = 0; k < p->size; k++)
            new_bytes[k] = (unsigned char)((p->value >> (8 * k)) & 0xFF);
        if (memcmp(elf + fo, new_bytes, (size_t)p->size) == 0) {
            n_same++;
            continue;
        }
        n_w++;
    }
    free(elf);
    *n_write = n_w;
    *n_skip = n_s + n_same;
    if (n_w == 0) {
        if (n_same > 0) {
            ws_report_add(report,
                          "info: already patched (all %d bytes match)",
                          n_same);
            return 1;
        }
        ws_report_add(report, "error: no patch could be applied");
        return -1;
    }
    return 0;
}

int ws_iso_patch_apply(const char *dir, const char *iso_name,
                       const char *pnach_text, size_t pnach_len,
                       ws_report_t *report, int *n_applied, int *n_skipped)
{
    /* BSS, bukan stack: pnach ~12 KB + backup ~3 KB. */
    static ws_pnach_t pnach;
    static ws_wsrest_rec_t recs[WS_WSREST_MAX_RECS];
    char iso_path[1024];
    char boot[64] = "";
    char err[128];
    ws_iso_t iso;
    unsigned extent = 0, elf_size = 0;
    unsigned char *elf = NULL;
    size_t elf_len = 0;
    int n_recs = 0, n_write = 0, n_skip = 0, n_same = 0;
    int i;

    *n_applied = 0;
    *n_skipped = 0;
    memset(&pnach, 0, sizeof(pnach));
    if (ws_parse_pnach(pnach_text, pnach_len, &pnach, report) != 0) {
        ws_report_add(report, "error: cannot parse pnach text");
        return -1;
    }
    if (pnach.n_patches == 0) {
        ws_report_add(report, "error: no active patches in pnach");
        return -1;
    }
    if (ws_boot_name(dir, iso_name, boot, sizeof(boot), err, sizeof(err))
        != 0) {
        ws_report_add(report, "error: boot ELF not found (%s)", err);
        return -1;
    }
    snprintf(iso_path, sizeof(iso_path), "%s/%s", dir, iso_name);
    if (ws_iso_open_rw(&iso, iso_path) != 0) {
        ws_report_add(report, "error: cannot open ISO for writing");
        return -1;
    }
    if (ws_iso_file_offset(&iso, boot, &extent, &elf_size) != 0 ||
        ws_iso_read_file(&iso, boot, &elf, &elf_len) != 0) {
        ws_report_add(report, "error: cannot read boot ELF %s", boot);
        ws_iso_close(&iso);
        return -1;
    }
    if (!ws_elf_check(elf, elf_len)) {
        ws_report_add(report, "error: boot ELF is not a 32-bit ELF");
        free(elf);
        ws_iso_close(&iso);
        return -1;
    }
    for (i = 0; i < pnach.n_patches; i++) {
        ws_patch_t *p = &pnach.patches[i];
        uint32_t fo;
        unsigned char new_bytes[4];
        unsigned char cur_bytes[4];
        int k;
        if (!iso_patch_decision(p, elf, elf_len, &fo, report)) {
            n_skip++;
            continue;
        }
        /* Byte baru: low `size` byte nilai, little-endian. */
        for (k = 0; k < p->size; k++)
            new_bytes[k] = (unsigned char)((p->value >> (8 * k)) & 0xFF);
        memcpy(cur_bytes, elf + fo, (size_t)p->size);
        if (memcmp(cur_bytes, new_bytes, (size_t)p->size) == 0) {
            n_same++;
            continue;  /* sudah ke-patch di alamat ini */
        }
        if (n_recs >= WS_WSREST_MAX_RECS) {
            ws_report_add(report, "error: too many patches (max %d)",
                          WS_WSREST_MAX_RECS);
            goto fail_restore;
        }
        if (ws_iso_write_data(&iso, extent, fo, new_bytes,
                              (size_t)p->size) != 0) {
            ws_report_add(report,
                          "error: failed to write at ELF offset 0x%08x", fo);
            goto fail_restore;
        }
        recs[n_recs].offset = fo;
        recs[n_recs].size = (uint32_t)p->size;
        memset(recs[n_recs].old_bytes, 0, 4);
        memcpy(recs[n_recs].old_bytes, cur_bytes, (size_t)p->size);
        n_recs++;
        n_write++;
        /* Update buffer supaya patch berikutnya yang alamatnya
           tumpang-tindih (satu word, dua patch) baca byte terbaru. */
        memcpy(elf + fo, new_bytes, (size_t)p->size);
    }
    if (n_write == 0) {
        free(elf);
        ws_iso_close(&iso);
        *n_applied = 0;
        *n_skipped = n_skip + n_same;
        if (n_same > 0) {
            ws_report_add(report,
                          "info: already patched (all %d bytes match)",
                          n_same);
            return 1;
        }
        ws_report_add(report, "error: no patch could be applied");
        return -1;
    }
    /* Pastikan tulis ISO sampai ke disk dulu, baru tulis backup. */
    if (fflush(iso.f) != 0) {
        ws_report_add(report, "error: failed to flush ISO");
        goto fail_restore;
    }
    {
        char wsrest_path[1024];
        ws_wsrest_path(dir, iso_name, wsrest_path, sizeof(wsrest_path));
        if (ws_wsrest_write(wsrest_path, recs, n_recs) != 0) {
            ws_report_add(report, "error: failed to write backup %s",
                          wsrest_path);
            goto fail_restore;
        }
    }
    free(elf);
    ws_iso_close(&iso);
    *n_applied = n_write;
    *n_skipped = n_skip + n_same;
    return 0;

fail_restore:
    /* Gagal di tengah: kembalikan semua yang sudah tertulis dari
       backup di memori, urutan terbalik (terakhir dulu) supaya alamat
       yang ke-patch dua kali kembali ke nilai aslinya. ISO tidak
       ditinggal setengah ke-patch; file .wsrest TIDAK ditulis. */
    {
        int j;
        for (j = n_recs - 1; j >= 0; j--)
            ws_iso_write_data(&iso, extent, recs[j].offset,
                              recs[j].old_bytes, recs[j].size);
        fflush(iso.f);
    }
    free(elf);
    ws_iso_close(&iso);
    *n_applied = 0;
    *n_skipped = n_skip + n_same;
    return -1;
}

int ws_iso_patch_restore(const char *dir, const char *iso_name,
                         ws_report_t *report)
{
    static ws_wsrest_rec_t recs[WS_WSREST_MAX_RECS];
    char wsrest_path[1024];
    char iso_path[1024];
    char boot[64] = "";
    char err[128];
    ws_iso_t iso;
    unsigned extent = 0, elf_size = 0;
    int n_recs, i;

    ws_wsrest_path(dir, iso_name, wsrest_path, sizeof(wsrest_path));
    n_recs = ws_wsrest_read(wsrest_path, recs, WS_WSREST_MAX_RECS);
    if (n_recs < 0) {
        ws_report_add(report, "error: no backup file (%s)",
                      n_recs == -1 ? "not found" : "corrupt");
        return -1;
    }
    if (n_recs == 0) {
        ws_report_add(report, "error: backup file is empty");
        return -1;
    }
    if (ws_boot_name(dir, iso_name, boot, sizeof(boot), err, sizeof(err))
        != 0) {
        ws_report_add(report, "error: boot ELF not found (%s)", err);
        return -1;
    }
    snprintf(iso_path, sizeof(iso_path), "%s/%s", dir, iso_name);
    if (ws_iso_open_rw(&iso, iso_path) != 0) {
        ws_report_add(report, "error: cannot open ISO for writing");
        return -1;
    }
    if (ws_iso_file_offset(&iso, boot, &extent, &elf_size) != 0) {
        ws_report_add(report, "error: cannot find boot ELF %s", boot);
        ws_iso_close(&iso);
        return -1;
    }
    /* Restore urutan terbalik (terakhir ditulis dulu) supaya alamat
       yang ke-patch dua kali kembali ke nilai aslinya. */
    for (i = n_recs - 1; i >= 0; i--) {
        if (ws_iso_write_data(&iso, extent, recs[i].offset,
                              recs[i].old_bytes, recs[i].size) != 0) {
            ws_report_add(report,
                          "error: failed to restore at ELF offset 0x%08x",
                          recs[i].offset);
            /* .wsrest TIDAK dihapus — bisa coba lagi. */
            fflush(iso.f);
            ws_iso_close(&iso);
            return -1;
        }
    }
    if (fflush(iso.f) != 0) {
        ws_report_add(report, "error: failed to flush ISO");
        ws_iso_close(&iso);
        return -1;
    }
    ws_iso_close(&iso);
    if (remove(wsrest_path) != 0)
        ws_report_add(report,
                      "warning: restored, but could not delete backup %s",
                      wsrest_path);
    ws_report_add(report, "info: restored %d patch(es) from backup",
                  n_recs);
    return 0;
}
