/* PS2 Widescreen Manager — Fase 2 (toggle beneran).
 *
 * List game PS2 dari /dev_hdd0/PS2ISO/ + Title ID + CRC32 ELF +
 * ketersediaan patch widescreen. Tombol X mengaktifkan/menonaktifkan:
 * ON  = tulis <ISO>.CONFIG di sebelah ISO (patch widescreen + merge
 *       entry database webMAN, pnach menang — GUSUR; .CONFIG manual
 *       yang ditimpa dibackup dulu ke .CONFIG.orig, Fase 3b).
 * OFF = hapus <ISO>.CONFIG (.CONFIG manual dibackup ke .CONFIG.orig).
 * Start = ISO patch (Fase 2t): tulis patch pnach LANGSUNG ke file ISO
 * (burn-to-ISO) dengan backup <ISO>.wsrest; Start lagi di game yang
 * sudah ke-patch = restore. Selalu ada warning + konfirmasi Yes/No
 * dulu — tidak ada tulis ISO diam-diam.
 * Bulatan = keluar (konfirmasi dulu, Fase 2y). Tombol PS / exit request sistem juga keluar.
 */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <sys/systime.h>
#include <sysutil/msg.h>
#include <sysutil/sysutil.h>

#include "configfile.h"
#include "convert.h"
#include "dbmerge.h"
#include "draw.h"
#include "iso_patch.h"
#include "pad.h"
#include "patchfile.h"
#include "scan.h"
#include "state.h"
#include "toggle.h"
#include "video.h"

#define APP_ID    "PS2W00001"
#define APP_TITLE "PS2 Widescreen Manager"
#define APP_DIR   "/dev_hdd0/game/" APP_ID "/USRDIR"
#define PS2ISO_DIR "/dev_hdd0/PS2ISO"
#define PATCHES_DIR APP_DIR "/data/patches"
#define USER_PATCHES_DIR PS2ISO_DIR "/pnach"
#define CONFIGDB_DIR APP_DIR "/data/configdb"
#define STATE_PATH APP_DIR "/state.txt"

/* Layout ikut fork pcsx_rearmed (menu.c menu_run + header_app/footer):
 * bar header 64px + bar footer 52px, margin 64, baris header kolom
 * 64-92, list y0=92, 12 baris x 44px, tanpa cover (list pakai lebar
 * penuh). Kolom (720p): Title | serial w-640 | crc w-480 | database
 * w-320 | activated w-160. database = file .pnach ketemu (jenis apa
 * pun), activated = 2 jenis (Fase 2z): "yes" (.CONFIG), "patched"
 * (ISO patch), "yes+iso" (keduanya), "no". Layar kecil (Fase 3i,
 * h < 720): 8 baris x 60px, kolom Title | serial w-500 | activated
 * w-240 (crc + database sembunyi), tanpa baris header kolom, label
 * prioritas pindah ke baris detail, teks header/footer ke safe area.
 * KOREKSI dari mockup: mockup bilang 17 baris x 27px, itu daftar
 * settings/VMC (SET_VISIBLE); daftar game utama fork = 12 baris x
 * 44px (menu_run: rows=12, y=y0+(i-top)*44) — dan 12x44=528px justru
 * lebih mengisi layar daripada 17x27=459px. Semua ukuran ditulis
 * dalam piksel 720p lalu dikali h/720 supaya proporsinya sama di
 * 720p maupun 1080p (overlay fork tetap 1280x720 dan diskalakan RSX;
 * di sini skalanya dihitung sendiri). Font bitmap tetap 16x30
 * (tidak diskalakan). */
#define ROWS_PER_PAGE 12
#define ROW_H 44

static volatile int exit_requested;
/* XMB buka (Fase 2u, dari SYSUTIL_MENU_OPEN/CLOSE) + pencacah laju
 * loop saat XMB buka (diagnostik: ~60/detik = flip jalan normal,
 * ~1/detik = flip macet). */
static volatile int menu_open;
static int xmb_frames;
static uint64_t xmb_t0;
/* Progress bar scan (Fase 2x): total ISO (dari ws_count_iso_files),
 * persen terakhir yang ditampilkan, dan apakah dialog terbuka. */
static int scan_total;
static int scan_shown;
static int scan_progress_open;
/* Prioritas old value (Fase 2s, toggle Select, SESSION-ONLY):
 * 0 = patch first (komentar // dulu, ELF fallback — default),
 * 1 = elf first. Reset tiap buka app (tidak ada file settingan).
 * Hanya ngaruh di waktu konversi — .CONFIG yang sudah ada di disk
 * tidak disentuh. */
static int old_prio_elf_first;
static FILE *logf;

static void log_line(const char *fmt, ...)
{
    va_list ap;
    if (!logf) return;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fflush(logf);
}

static void log_report(ws_report_t *report)
{
    int i;
    for (i = 0; i < report->count; i++) log_line("  %s\n", report->lines[i]);
}

static void sysutil_cb(uint64_t status, uint64_t param, void *userdata)
{
    (void)param;
    (void)userdata;
    if (status == SYSUTIL_EXIT_GAME) exit_requested = 1;
    else if (status == SYSUTIL_MENU_OPEN) menu_open = 1;
    else if (status == SYSUTIL_MENU_CLOSE) { menu_open = 0; xmb_t0 = 0; }
}

/* Dipanggil scan sesudah tiap ISO: proses exit request sistem (tombol
 * PS) supaya app bisa keluar walau scan belum selesai. Kembalikan 1
 * kalau diminta keluar (scan dibatalkan). Sekalian update progress
 * bar native (Fase 2x): persen + nama ISO terakhir + flip per update
 * (pola IrisMAN). ctx = &res (daftar game, untuk nama ISO). */
static int scan_progress_cb(int done, void *ctx)
{
    sysUtilCheckCallback();
    if (exit_requested) return 1;
    if (scan_progress_open && scan_total > 0) {
        ws_scan_result_t *res = (ws_scan_result_t *)ctx;
        int pct = 100 * done / scan_total;
        if (pct > scan_shown) {
            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0,
                                    (u32)(pct - scan_shown));
            scan_shown = pct;
        }
        if (res && done >= 1 && done <= res->count) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Scanning: %s",
                     res->games[done - 1].iso_name);
            msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, msg);
        }
        {
            unsigned w, h;
            uint32_t *fb = video_frame(&w, &h);
            video_present(fb);
        }
    }
    return 0;
}

#define COL_BG       0xFF101418u
#define COL_BAR      0xFF182238u /* bar header/footer, sama dengan fork */
#define COL_TITLE    0xFFFFFF00u
#define COL_SUB      0xFF9AA0A8u
#define COL_TEXT     0xFFE8E8E8u
#define COL_DIM      0xFF808080u
#define COL_SEL_BG   0xFF2A3440u
#define COL_SEL_TEXT 0xFFFFFF00u
#define COL_YES      0xFF40C040u
#define COL_NO       0xFF808080u
#define COL_AMB      0xFFFFA040u
#define COL_ERR      0xFFFF4040u
#define COL_OFF      0xFFFFC040u

/* Buffer editor patch (Triangle): BSS, bukan stack — 1024 item
 * (~330 KB) + baris editor (~17 KB). Dipakai toggle ON (filter
 * widescreen-only) dan editor. */
static ws_patch_item_t ed_items[WS_MAX_PATCH_ITEMS];
static int ed_checked[WS_MAX_PATCH_ITEMS];
typedef struct {
    int is_header;      /* 1 = header grup, 0 = baris patch */
    int item;           /* indeks ke ed_items (baris patch saja) */
    const char *group;  /* nama grup (header saja) */
} ed_row_t;
static ed_row_t ed_rows[WS_MAX_PATCH_ITEMS + 16];
/* Baris info header pnach di editor (Fase 3l): BSS, bukan stack. */
static char ed_hdr[WS_PATCH_HEADER_MAX_LINES][WS_PATCH_HEADER_LINE_LEN];
/* Buffer picker duplikat (Fase 3l): BSS, bukan stack. */
static char dup_files[16][128];
static char dup_titles[16][256];

static void trunc_name(const char *s, char *out, size_t out_size, int max_chars)
{
    size_t n = strlen(s);
    if ((int)n <= max_chars) {
        strncpy(out, s, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    if (max_chars < 4) max_chars = 4;
    n = (size_t)(max_chars - 2);
    if (n > out_size - 1) n = out_size - 1;
    memcpy(out, s, n);
    out[n] = '\0';
    strcat(out, "..");
}

/* Nama file dari path penuh (sesudah '/' terakhir). */
static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* Segmen footer: simbol tombol (atau NONE) + label teks. Simbol
 * digambar (draw_button, warna PS), bukan teks — font bitmap cuma
 * ASCII. y = baris atas teks (simbol dipusatkan ke tengah tinggi
 * font, y+15). Kembalikan x sesudah segmen terakhir. */
typedef struct { ws_btn_t btn; const char *text; } footer_seg_t;

static int draw_footer_segs(uint32_t *fb, unsigned w, unsigned h,
                            int x, int y, const footer_seg_t *segs, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (segs[i].btn == WS_BTN_UPDOWN) {
            draw_button(fb, w, h, x + WS_BTN_SLOT_UPDOWN / 2, y + 15,
                        WS_BTN_UPDOWN);
            x += WS_BTN_SLOT_UPDOWN;
        } else if (segs[i].btn != WS_BTN_NONE) {
            draw_button(fb, w, h, x + WS_BTN_SLOT / 2, y + 15,
                        segs[i].btn);
            x += WS_BTN_SLOT;
        }
        if (segs[i].text[0]) {
            draw_text(fb, w, h, x, y, segs[i].text, COL_DIM);
            x += draw_text_width(segs[i].text) + 20;
        }
    }
    return x;
}

/* Label prioritas old value di pojok kanan footer (Fase 2s, daftar
 * utama saja — footer editor tidak muat di 720p). "Select:" abu
 * (COL_DIM, kayak label tombol lain — jangan dikuningkan), nilainya
 * kuning (COL_TITLE). Rata kanan ke w - margin. */
static void draw_priority_label(uint32_t *fb, unsigned w, unsigned h,
                                int y)
{
    const char *val = old_prio_elf_first ? "old value elf first"
                                         : "old value patch first";
    int right = (int)w - 64 * (int)h / 720;
    int x = right - draw_text_width(val);
    draw_text(fb, w, h, x - draw_text_width("Select: "), y, "Select: ",
              COL_DIM);
    draw_text(fb, w, h, x, y, val, COL_TITLE);
}

/* Format satu baris patch untuk editor: "[X] ADDR TYPE VALUE · comment".
 * line = "patch=1,EE,ADDR,TYPE,VALUE //COMMENT". Kalau formatnya tidak
 * dikenali, tampilkan baris mentahnya (dipotong ke max_chars). */
static void format_patch_row(const ws_patch_item_t *it, int checked,
                             char *out, size_t out_size, int max_chars)
{
    char tmp[256];
    char full[512];
    char *hash, *c1, *c2, *c3, *c4;
    char *addr, *ptype, *value, *comment;
    strncpy(tmp, it->line, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    comment = "";
    hash = strstr(tmp, "//");
    if (hash) {
        comment = hash + 2;
        while (*comment == ' ' || *comment == '\t') comment++;
        *hash = '\0';
        {
            size_t n = strlen(tmp);
            while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '\t'))
                tmp[--n] = '\0';
        }
    }
    addr = ptype = value = "";
    c1 = strchr(tmp, ',');
    if (c1) {
        c2 = strchr(c1 + 1, ',');
        if (c2) {
            c3 = strchr(c2 + 1, ',');
            if (c3) {
                c4 = strchr(c3 + 1, ',');
                if (c4) {
                    *c3 = '\0';
                    *c4 = '\0';
                    addr = c2 + 1;
                    ptype = c3 + 1;
                    value = c4 + 1;
                }
            }
        }
    }
    if (!addr[0] || !ptype[0] || !value[0]) {
        snprintf(full, sizeof(full), "[%c] %s", checked ? 'X' : ' ',
                 it->line);
    } else if (comment[0]) {
        snprintf(full, sizeof(full), "[%c] %s %s %s · %s",
                 checked ? 'X' : ' ', addr, ptype, value, comment);
    } else {
        snprintf(full, sizeof(full), "[%c] %s %s %s",
                 checked ? 'X' : ' ', addr, ptype, value);
    }
    trunc_name(full, out, out_size, max_chars);
}

/* Konversi teks pnach (sudah difilter ke baris yang dipilih) + tulis
 * .CONFIG. Dipakai toggle ON (X) dan editor (Triangle + Bulatan).
 * Kembalikan 0 kalau .CONFIG ditulis, -1 kalau gagal. Mengisi
 * status[] dan meng-update g->has_config + state. */
static int apply_patches(ws_game_t *g, const char *text, size_t text_len,
                         ws_state_t *state, int have_configdb,
                         char *status, size_t status_size)
{
    char cfg_path[1024], orig_path[1088];
    unsigned char *elf = NULL;
    size_t elf_len = 0;
    char elf_err[128];
    unsigned char *out = NULL;
    size_t out_len = 0;
    int n_triples = 0, merged = 0, backed_up = 0;
    /* 12,8 KB — static juga (lihat komentar di main). */
    static ws_report_t report;
    int rc = -1;
    memset(&report, 0, sizeof(report));
    ws_config_path(PS2ISO_DIR, g->iso_name, cfg_path, sizeof(cfg_path));
    snprintf(orig_path, sizeof(orig_path), "%s.orig", cfg_path);
    log_line("apply patches %s\n", g->iso_name);
    /* Single-hook (Fase 3c): game yang sudah ISO-patched tidak boleh
     * ditulis .CONFIG baru — satu jenis hook saja. Restore ISO dulu
     * (Start) baru tulis .CONFIG. */
    if (g->iso_patched) {
        snprintf(status, status_size,
                 "already ISO-patched - restore the ISO first");
        log_line("  %s\n", status);
        return -1;
    }
    if (ws_read_boot_elf(PS2ISO_DIR, g->iso_name, &elf, &elf_len,
                         elf_err, sizeof(elf_err)) != 0) {
        snprintf(status, status_size, "failed to read ELF: %s", elf_err);
        log_line("  %s\n", status);
    } else if (ws_convert(text, text_len, elf, elf_len, g->iso_name,
                          have_configdb ? CONFIGDB_DIR : NULL,
                          0, old_prio_elf_first ? WS_OLD_ELF_FIRST
                                                : WS_OLD_PATCH_FIRST,
                          g->title_id, &out, &out_len,
                          &n_triples, &merged, &report) != 0) {
        int k;
        snprintf(status, status_size, "conversion failed (see wsm.log)");
        for (k = 0; k < report.count; k++)
            if (strncmp(report.lines[k], "error:", 6) == 0) {
                snprintf(status, status_size, "%s", report.lines[k]);
                break;
            }
        log_line("  conversion failed:\n");
        log_report(&report);
    } else if (ws_toggle_backup_custom_config(
                   cfg_path, orig_path,
                   ws_state_has(state, g->iso_name), &backed_up) != 0) {
        /* Backup gagal = JANGAN timpa .CONFIG custom-nya. */
        snprintf(status, status_size, "failed to back up %s.orig",
                 base_name(cfg_path));
        log_line("  %s\n", status);
    } else if (ws_toggle_write_config(cfg_path, out, out_len) != 0) {
        snprintf(status, status_size, "failed to write %s",
                 base_name(cfg_path));
        log_line("  %s\n", status);
    } else {
        g->has_config = 1;
        ws_state_add(state, g->iso_name);
        if (ws_state_save(state, STATE_PATH) != 0)
            log_line("  warning: failed to save state.txt\n");
        if (backed_up)
            log_line("  custom .CONFIG backed up to .CONFIG.orig\n");
        snprintf(status, status_size, "Wrote: %s — %d patch%s%s",
                 base_name(cfg_path), n_triples,
                 merged ? " (database merged in)" : "",
                 backed_up ? " (old copy in .CONFIG.orig)" : "");
        log_line("  %s\n", status);
        log_report(&report);
        rc = 0;
    }
    free(elf);
    free(out);
    return rc;
}

/* Hapus .CONFIG game (OFF). Dipakai toggle OFF (X) dan editor
 * (Bulatan dengan nol centang). Kembalikan 0 kalau sukses. */
static int remove_config_file(ws_game_t *g, ws_state_t *state,
                              char *status, size_t status_size)
{
    char cfg_path[1024], orig_path[1088];
    int app_made = ws_state_has(state, g->iso_name);
    int backed_up = 0;
    ws_config_path(PS2ISO_DIR, g->iso_name, cfg_path, sizeof(cfg_path));
    snprintf(orig_path, sizeof(orig_path), "%s.orig", cfg_path);
    log_line("toggle OFF %s (app_made=%d)\n", g->iso_name, app_made);
    if (ws_toggle_remove_config(cfg_path, orig_path, app_made,
                                &backed_up) != 0) {
        snprintf(status, status_size, "failed to delete %s",
                 base_name(cfg_path));
        log_line("  %s\n", status);
        return -1;
    }
    g->has_config = 0;
    ws_state_remove(state, g->iso_name);
    if (ws_state_save(state, STATE_PATH) != 0)
        log_line("  warning: failed to save state.txt\n");
    if (app_made) {
        /* File kita yang dihapus; kalau ada .CONFIG.orig (file custom
         * yang dulu ditimpa app), kembalikan — itu fix game, jangan
         * hilang cuma karena file app dihapus. File asing yang baru
         * saja di-backup di atas TIDAK direstore (backup-nya baru
         * dibuat sebagai arsip hapusan ini). */
        int restored = ws_toggle_restore_orig(cfg_path, orig_path);
        if (restored == 1) {
            g->has_config = 1;
            snprintf(status, status_size,
                     "Deleted: %s (custom .CONFIG restored from "
                     ".CONFIG.orig)",
                     base_name(cfg_path));
            log_line("  %s\n", status);
            return 0;
        }
        if (restored < 0)
            log_line("  warning: failed to restore .CONFIG.orig\n");
    }
    snprintf(status, status_size, "Deleted: %s%s", base_name(cfg_path),
             backed_up ? " (old copy in .CONFIG.orig)" : "");
    log_line("  %s\n", status);
    return 0;
}

/* Definisi di bawah (dipakai editor, Fase 2v). */
static void iso_patch_action_text(ws_game_t *g, ws_state_t *state,
                                  const char *text,
                                  size_t text_len,
                                  char *status, size_t status_size);

/* Single-hook (Fase 3c): .CONFIG ini bikinan app? Watermark di ekor
 * file ("WSM1") ATAU nama ISO tercatat di state.txt (file lama,
 * sebelum watermark ada). File yang tidak bisa dibaca/di-parse =
 * BUKAN punya app (arah aman: perlakukan sebagai custom). */
static int config_is_ours(const char *cfg_path, ws_state_t *state,
                          const char *iso_name)
{
    int wm = ws_config_has_watermark(cfg_path);
    if (wm == 1) return 1;
    return ws_state_has(state, iso_name);
}

/* Folder baca file .pnach game ini: folder user kalau patch-nya dari
 * user (Fase 3k), folder system kalau dari database bawaan. */
static const char *patch_dir_for(const ws_game_t *g)
{
    return g->user_patch ? USER_PATCHES_DIR : PATCHES_DIR;
}

/* Editor patch (Triangle): pilih patch mana yang aktif untuk satu game.
 * Centang awal = isi .CONFIG yang ada (baca balik — .CONFIG adalah
 * satu-satunya state), atau default widescreen saja kalau belum
 * ada .CONFIG / .CONFIG tidak bisa dibaca. Bulatan = kembali dan
 * langsung tulis .CONFIG dari centangan (atau hapus .CONFIG kalau
 * tidak ada yang dicentang). Start = tulis centangan ke ISO
 * (Fase 2v: preview + warning + backup .wsrest, atau restore kalau
 * game sudah PATCHED). Tidak ada file settingan. */
static void edit_patches(ws_game_t *g, ws_state_t *state, int have_configdb,
                         char *status, size_t status_size)
{
    char *text = NULL;
    size_t text_len = 0;
    int n_items, n_rows = 0, sel = 0, top = 0;
    int i, confirmed = 0;
    uint64_t last_move = 0;
    int repeat = 0;
    /* Pesan hasil Start, tampil di baris detail sampai tombol lain
     * ditekan (status[] sendiri baru tampil di daftar utama sesudah
     * keluar editor). */
    char ed_status[256] = "";
    if (!g->patch_file[0] ||
        ws_patch_read_file(patch_dir_for(g), g->patch_file, &text,
                           &text_len) != 0) {
        snprintf(status, status_size,
                 "patch not found (unexpected — please report)");
        return;
    }
    n_items = ws_patch_list(text, text_len, ed_items, WS_MAX_PATCH_ITEMS);
    if (n_items <= 0) {
        snprintf(status, status_size, "no patches in this file");
        free(text);
        return;
    }
    for (i = 0; i < n_items; i++)
        ed_checked[i] = ed_items[i].ws_kind == 2 && ed_items[i].enabled;
    /* Baca balik .CONFIG (satu-satunya state): kalau game ini sudah
     * ON, centangan mengikuti isi .CONFIG yang ada — pilihan custom
     * user tidak hilang tiap buka editor. Gagal baca/parse = jatuh ke
     * default widescreen-only (mis. .CONFIG buatan webMAN/manual yang
     * strukturnya tidak dikenali). */
    {
        char cfg_path[1024];
        static uint32_t cfg_addrs[64];
        int n_cfg;
        ws_config_path(PS2ISO_DIR, g->iso_name, cfg_path, sizeof(cfg_path));
        n_cfg = ws_config_triple_addrs(cfg_path, cfg_addrs, 64);
        if (n_cfg >= 0) {
            for (i = 0; i < n_items; i++) {
                int j, found = 0;
                if (!ed_items[i].convertible) {
                    ed_checked[i] = 0;
                    continue;
                }
                for (j = 0; j < n_cfg; j++)
                    if (cfg_addrs[j] == ed_items[i].addr) found = 1;
                ed_checked[i] = found;
            }
        }
    }
    /* Bangun baris: header pnach dulu (Fase 3l) — gametitle + comment
     * + catatan // sebelum grup pertama, tempat penjelasan file
     * berada. Baris info: tidak bisa dipilih, seperti header grup. */
    {
        int n_hdr, k;
        n_hdr = ws_patch_header(text, text_len, ed_hdr,
                                WS_PATCH_HEADER_MAX_LINES);
        for (k = 0; k < n_hdr; k++) {
            if (n_rows >= (int)(sizeof(ed_rows) / sizeof(ed_rows[0])))
                break;
            ed_rows[n_rows].is_header = 1;
            ed_rows[n_rows].item = -1;
            ed_rows[n_rows].group = ed_hdr[k];
            n_rows++;
        }
    }
    /* Bangun baris: header grup + baris patch. */
    {
        char cur_group[48] = "";
        for (i = 0; i < n_items; i++) {
            if (strcmp(ed_items[i].group, cur_group) != 0) {
                ed_rows[n_rows].is_header = 1;
                ed_rows[n_rows].item = -1;
                ed_rows[n_rows].group = ed_items[i].group;
                n_rows++;
                strncpy(cur_group, ed_items[i].group,
                        sizeof(cur_group) - 1);
            }
            ed_rows[n_rows].is_header = 0;
            ed_rows[n_rows].item = i;
            ed_rows[n_rows].group = NULL;
            n_rows++;
        }
    }
    sel = 0;
    while (sel < n_rows && ed_rows[sel].is_header) sel++;

    while (!exit_requested) {
        uint32_t *fb, pressed, held;
        unsigned w, h;
        int margin, header_h, footer_h, row_h, list_y0;
        int pages, page, n_checked = 0;
        int row_y;
        /* Layout ringkas 480p (Fase 3i): layar kecil (h < 720) pakai
         * baris lebih tinggi (60 px desain = 40 px di 480p, muat huruf
         * 30 px) dan 8 baris/halaman. video_small() baca scr_h yang
         * sudah di-set video_init — resolusi tidak berubah selama app
         * jalan, jadi nilainya sama tiap iterasi. */
        int small = video_small();
        int rows_pp = small ? 8 : ROWS_PER_PAGE;
        int row_h_base = small ? 60 : ROW_H;
        char line[512], title[256];
        int name_chars;
        uint64_t now = sysGetSystemTime();

        sysUtilCheckCallback();
        /* XMB buka (Fase 2u): sama seperti loop utama — flip saja,
         * jangan gambar / baca pad. */
        if (menu_open) {
            fb = video_frame(&w, &h);
            video_present_unchanged();
            usleep(5000);
            continue;
        }
        pressed = pad_pressed();
        held = pad_held();
        /* Pesan hasil Start hilang begitu tombol lain ditekan. */
        if (pressed) ed_status[0] = '\0';
        if (pressed & PAD_CIRCLE) { confirmed = 1; break; }
        /* Segitiga = tutup selector TANPA tulis (Fase 3a): buat
         * sekadar nengok isi patch / cek mana yang aktif — Bulatan
         * selalu tulis, jadi tanpa ini tidak ada jalan pulang tanpa
         * write. Segitiga buka + tutup page selector. */
        if (pressed & PAD_TRIANGLE) break;
        if (pressed & PAD_CROSS && !ed_rows[sel].is_header) {
            int it = ed_rows[sel].item;
            ed_checked[it] = !ed_checked[it];
        }
        /* Start = ISO patch dari centangan (Fase 2v). Game yang
         * sudah PATCHED -> tawarkan restore (tidak butuh teks).
         * Hasilnya tampil di baris detail (ed_status) + di status
         * daftar utama sesudah keluar editor.
         * Patch BERHASIL -> langsung keluar editor (Fase 2z): kalau
         * tetap di editor, Bulatan sesudahnya menulis .CONFIG dari
         * centangan yang SAMA — eksekusi ganda yang ambigu (ISO
         * sudah ke-patch + .CONFIG aktif). Restore tidak keluar:
         * sesudah restore, .CONFIG = aktivasi tunggal yang jelas. */
        if (pressed & PAD_START) {
            int was_patched = g->iso_patched;
            if (g->iso_patched) {
                iso_patch_action_text(g, state, NULL, 0, status, status_size);
            } else {
                int n_checked = 0;
                for (i = 0; i < n_items; i++)
                    if (ed_checked[i]) n_checked++;
                if (n_checked == 0) {
                    snprintf(status, status_size, "no patches checked");
                } else {
                    char *ftext = NULL;
                    size_t ftext_len = 0;
                    if (ws_patch_filtered_text(text, text_len, ed_items,
                                               ed_checked, n_items,
                                               &ftext, &ftext_len) != 0) {
                        snprintf(status, status_size,
                                 "failed to build patch list");
                    } else {
                        iso_patch_action_text(g, state, ftext, ftext_len,
                                              status, status_size);
                        free(ftext);
                    }
                }
            }
            snprintf(ed_status, sizeof(ed_status), "%s", status);
            /* Patch baru berhasil (0 -> 1): keluar editor tanpa
             * tulis .CONFIG (confirmed tetap 0). */
            if (!was_patched && g->iso_patched)
                break;
        }
        {
            int move = 0;
            int old_sel = sel;
            if (pressed & PAD_UP) move = -1;
            if (pressed & PAD_DOWN) move = 1;
            if (pressed & PAD_L1) {
                int s = sel - rows_pp;
                if (s < 0) s = 0;
                while (s < n_rows && ed_rows[s].is_header) s++;
                if (s < n_rows) sel = s;
            }
            if (pressed & PAD_R1) {
                int s = sel + rows_pp;
                if (s > n_rows - 1) s = n_rows - 1;
                while (s >= 0 && ed_rows[s].is_header) s--;
                if (s >= 0) sel = s;
            }
            if (move == 0 && (held & (PAD_UP | PAD_DOWN))) {
                uint64_t delay = repeat ? 90000 : 400000;
                if (now - last_move >= delay) {
                    move = (held & PAD_UP) ? -1 : 1;
                    repeat = 1;
                }
            } else if (move != 0) {
                repeat = 0;
            }
            if (move != 0) {
                int s;
                last_move = now;
                if (move < 0) {
                    s = sel - 1;
                    while (s >= 0 && ed_rows[s].is_header) s--;
                    if (s >= 0) sel = s;
                } else {
                    s = sel + 1;
                    while (s < n_rows && ed_rows[s].is_header) s++;
                    if (s < n_rows) sel = s;
                }
                (void)old_sel;
            }
        }

        for (i = 0; i < n_items; i++)
            if (ed_checked[i]) n_checked++;

        fb = video_frame(&w, &h);
        draw_fill(fb, w, h, 0, 0, (int)w, (int)h, COL_BG);
        margin = 64 * (int)h / 720;
        header_h = (small ? 96 : 64) * (int)h / 720;
        footer_h = 52 * (int)h / 720;
        row_h = row_h_base * (int)h / 720;
        list_y0 = (small ? 108 : 90) * (int)h / 720;
        pages = (n_rows + rows_pp - 1) / rows_pp;
        page = top / rows_pp + 1;

        /* Bar header: judul game + jumlah patch/centang. Di layar kecil
         * bar lebih tinggi + teks turun ke safe area (teks 30 px, atas
         * 24 px di 480p — di atas overscan CRT ~5%). */
        draw_fill(fb, w, h, 0, 0, (int)w, header_h, COL_BAR);
        draw_fill(fb, w, h, margin, (small ? 36 : 17) * (int)h / 720,
                  4 * (int)h / 720, 30 * (int)h / 720, COL_TITLE);
        trunc_name(g->title, title, sizeof(title),
                   (int)((int)w - margin * 2) / 16 - (small ? 16 : 24));
        snprintf(line, sizeof(line), "Patch — %s", title);
        draw_text(fb, w, h, margin + 16 * (int)h / 720,
                  (small ? 36 : 17) * (int)h / 720, line, COL_TEXT);
        if (small)
            snprintf(line, sizeof(line), "%d checked %d/%d",
                     n_checked, page, pages);
        else
            snprintf(line, sizeof(line),
                     "%d patch · %d checked   [page %d/%d]",
                     n_items, n_checked, page, pages);
        draw_text(fb, w, h,
                  (int)w - margin - draw_text_width(line),
                  (small ? 36 : 17) * (int)h / 720, line, COL_SUB);

        if (sel < top) top = sel;
        if (sel >= top + rows_pp) top = sel - rows_pp + 1;
        name_chars = ((int)w - margin * 2) / 16;
        if (name_chars < 8) name_chars = 8;

        for (i = 0; i < rows_pp && top + i < n_rows; i++) {
            ed_row_t *r = &ed_rows[top + i];
            int y = list_y0 + i * row_h;
            int is_sel = (top + i == sel);
            if (r->is_header) {
                draw_text(fb, w, h, margin, y, r->group, COL_DIM);
            } else {
                if (is_sel)
                    draw_fill(fb, w, h, margin / 2, y - 6 * (int)h / 720,
                              (int)w - margin, row_h - 2 * (int)h / 720,
                              COL_SEL_BG);
                format_patch_row(&ed_items[r->item], ed_checked[r->item],
                                 line, sizeof(line), name_chars);
                draw_text(fb, w, h, margin, y, line,
                          is_sel ? COL_SEL_TEXT : COL_TEXT);
            }
        }

        /* Baris detail + bar footer. Di layar kecil detail naik 12 px
         * (48, bukan 36) supaya tidak menabrak footer. */
        row_y = (int)h - footer_h - (small ? 48 : 36) * (int)h / 720;
        if (ed_status[0]) {
            draw_text(fb, w, h, margin, row_y, ed_status, COL_TITLE);
        } else if (!ed_rows[sel].is_header) {
            snprintf(line, sizeof(line), "%s : %s",
                     ed_items[ed_rows[sel].item].group,
                     ed_items[ed_rows[sel].item].line);
            draw_text(fb, w, h, margin, row_y, line, COL_SUB);
        } else {
            snprintf(line, sizeof(line), "%s", g->iso_name);
            draw_text(fb, w, h, margin, row_y, line, COL_SUB);
        }
        draw_fill(fb, w, h, 0, (int)h - footer_h, (int)w, footer_h,
                  COL_BAR);
        {
            /* Label Start dinamis (Fase 2v): game sudah PATCHED,
             * Start = restore. */
            const char *start_label = "Start: patch ISO";
            if (g->iso_patched) start_label = "Start: restore";
            footer_seg_t segs[] = {
                { WS_BTN_CROSS, "check/uncheck" },
                { WS_BTN_NONE, small ? "" : "L1/R1: page" },
                { WS_BTN_NONE, start_label },
                { WS_BTN_CIRCLE, "back (write)" },
                { WS_BTN_TRIANGLE, "close" },
            };
            /* Teks footer di tengah bar (bukan 41 px dari bawah) di
             * layar kecil — kalau tidak, teks 30 px terpotong 3 px di
             * 480p. Di 720p/1080p posisi lama dipertahankan. */
            int foot_y = small ? (int)h - footer_h + (footer_h - 30) / 2
                               : (int)h - 41 * (int)h / 720;
            draw_footer_segs(fb, w, h, margin,
                             foot_y, segs,
                             sizeof(segs) / sizeof(segs[0]));
        }
        video_present(fb);
        usleep(5000);
    }

    /* Bulatan: tulis .CONFIG dari centangan (atau hapus kalau nol).
     * Segitiga: tutup tanpa tulis (confirmed tetap 0). PS
     * (exit_requested): jangan tulis apa-apa. */
    if (confirmed && !exit_requested) {
        int n_checked = 0;
        for (i = 0; i < n_items; i++)
            if (ed_checked[i]) n_checked++;
        if (n_checked == 0) {
            remove_config_file(g, state, status, status_size);
        } else {
            char *ftext = NULL;
            size_t ftext_len = 0;
            if (ws_patch_filtered_text(text, text_len, ed_items,
                                       ed_checked, n_items,
                                       &ftext, &ftext_len) != 0) {
                snprintf(status, status_size, "failed to build patch list");
            } else {
                apply_patches(g, ftext, ftext_len, state, have_configdb,
                              status, status_size);
                free(ftext);
            }
        }
    }
    free(text);
}

/* Layar konfirmasi Yes/No (dipakai ISO patch/restore, Fase 2t). Judul
 * di bar header, baris-baris teks di tengah, footer X = ya /
 * Bulatan = tidak. Kembalikan 1 kalau X ditekan, 0 kalau Bulatan /
 * PS (tidak ada yang ditulis kalau 0). */
static int confirm_screen(const char *title, const char *lines[],
                          int n_lines, const char *yes_label)
{
    int result = 0;
    while (!exit_requested) {
        uint32_t *fb, pressed;
        unsigned w, h;
        int margin, header_h, footer_h;
        int small;
        int i;
        char line[512];

        sysUtilCheckCallback();
        pressed = pad_pressed();
        if (pressed & PAD_CROSS) { result = 1; break; }
        if (pressed & PAD_CIRCLE) { result = 0; break; }

        fb = video_frame(&w, &h);
        draw_fill(fb, w, h, 0, 0, (int)w, (int)h, COL_BG);
        margin = 64 * (int)h / 720;
        /* Layar kecil (Fase 3i): header lebih tinggi + teks ke safe
         * area, footer di tengah bar — sama seperti daftar utama. */
        small = video_small();
        header_h = (small ? 96 : 64) * (int)h / 720;
        footer_h = 52 * (int)h / 720;
        draw_fill(fb, w, h, 0, 0, (int)w, header_h, COL_BAR);
        draw_fill(fb, w, h, margin, (small ? 36 : 17) * (int)h / 720,
                  4 * (int)h / 720, 30 * (int)h / 720, COL_TITLE);
        trunc_name(title, line, sizeof(line),
                   (int)((int)w - margin * 2) / 16 - 24);
        draw_text(fb, w, h, margin + 16 * (int)h / 720,
                  (small ? 36 : 17) * (int)h / 720, line, COL_TEXT);
        for (i = 0; i < n_lines; i++) {
            int y = (110 + i * 40) * (int)h / 720;
            draw_text(fb, w, h, margin, y, lines[i], COL_TEXT);
        }
        draw_fill(fb, w, h, 0, (int)h - footer_h, (int)w, footer_h,
                  COL_BAR);
        {
            footer_seg_t segs[2];
            int foot_y;
            segs[0].btn = WS_BTN_CROSS;
            segs[0].text = yes_label;
            segs[1].btn = WS_BTN_CIRCLE;
            segs[1].text = "cancel";
            foot_y = small ? (int)h - footer_h + (footer_h - 30) / 2
                           : (int)h - 41 * (int)h / 720;
            draw_footer_segs(fb, w, h, margin,
                             foot_y, segs, 2);
        }
        video_present(fb);
        usleep(5000);
    }
    return result;
}

/* Aksi tombol Start (Fase 2t): ISO patch atau restore, dari teks pnach
 * yang disiapkan pemanggil — daftar utama: isi file .pnach utuh;
 * editor (Fase 2v): teks hasil filter centangan. Game yang sudah
 * ke-patch (.wsrest ada) -> tawarkan restore (teks tidak dipakai).
 * Game belum ke-patch -> preview, konfirmasi warning, lalu patch.
 * Mengisi status[] dan meng-update g->iso_patched. */
static void iso_patch_action_text(ws_game_t *g, ws_state_t *state,
                                  const char *text,
                                  size_t text_len,
                                  char *status, size_t status_size)
{
    static ws_report_t report;
    char wsrest_path[1024];
    memset(&report, 0, sizeof(report));
    ws_wsrest_path(PS2ISO_DIR, g->iso_name, wsrest_path,
                   sizeof(wsrest_path));
    log_line("iso patch %s (patched=%d)\n", g->iso_name, g->iso_patched);
    if (g->iso_patched) {
        /* ---- Restore: kembalikan byte asli dari .wsrest ---- */
        const char *lines[3];
        char l0[256], l2[256];
        char title[256];
        snprintf(l0, sizeof(l0),
                 "This writes the original bytes back into the ISO file.");
        snprintf(l2, sizeof(l2), "Backup: %s", base_name(wsrest_path));
        lines[0] = l0;
        lines[1] = "The ISO returns to its state before patching.";
        lines[2] = l2;
        snprintf(title, sizeof(title), "Restore ISO - %.200s", g->title);
        if (!confirm_screen(title, lines, 3, "yes - restore")) {
            snprintf(status, status_size, "Restore cancelled");
            return;
        }
        if (ws_iso_patch_restore(PS2ISO_DIR, g->iso_name, &report) != 0) {
            int k;
            snprintf(status, status_size, "restore failed (see wsm.log)");
            for (k = 0; k < report.count; k++)
                if (strncmp(report.lines[k], "error:", 6) == 0) {
                    snprintf(status, status_size, "%s", report.lines[k]);
                    break;
                }
            log_line("  restore failed:\n");
            log_report(&report);
        } else {
            g->iso_patched = 0;
            snprintf(status, status_size, "Restored: %.200s (backup deleted)",
                     g->iso_name);
            log_line("  %s\n", status);
            log_report(&report);
        }
        return;
    }
    /* ---- Patch: butuh teks pnach ---- */
    if (!text || text_len == 0) {
        snprintf(status, status_size, "no patch file for this game");
        return;
    }
    {
        int n_write = 0, n_skip = 0;
        int prc = ws_iso_patch_preview(PS2ISO_DIR, g->iso_name, text,
                                       text_len, &report, &n_write,
                                       &n_skip);
        if (prc == 1) {
            snprintf(status, status_size,
                     "already patched (ISO already has these values)");
            log_line("  %s\n", status);
            return;
        }
        if (prc != 0) {
            int k;
            snprintf(status, status_size,
                     "cannot patch this ISO (see wsm.log)");
            for (k = 0; k < report.count; k++)
                if (strncmp(report.lines[k], "error:", 6) == 0) {
                    snprintf(status, status_size, "%s", report.lines[k]);
                    break;
                }
            log_line("  preview failed:\n");
            log_report(&report);
            return;
        }
        /* ---- Warning + konfirmasi ---- */
        {
            const char *lines[5];
            char l0[256], l3[256], l4[256];
            char title[256];
            snprintf(l0, sizeof(l0),
                     "This writes the widescreen patch into the ISO file itself.");
            snprintf(l3, sizeof(l3), "Patch: %d - Skipped: %d",
                     n_write, n_skip);
            snprintf(l4, sizeof(l4), "Backup: %s",
                     base_name(wsrest_path));
            lines[0] = l0;
            lines[1] = "In most cases it can be reverted from this app (restore).";
            lines[2] = "But unexpected things can happen.";
            lines[3] = l3;
            lines[4] = l4;
            snprintf(title, sizeof(title), "Patch ISO - %.200s", g->title);
            if (!confirm_screen(title, lines, 5, "yes - patch")) {
                snprintf(status, status_size, "Patch cancelled");
                return;
            }
        }
        {
            int n_applied = 0, n_skipped = 0;
            /* Single-hook (Fase 3c): .CONFIG buatan app dihapus dulu
             * (otomatis — file kita, bisa dibuat ulang); kalau ada
             * .CONFIG.orig, file custom di baliknya kembali hidup
             * (lihat remove_config_file). .CONFIG custom dibiarkan —
             * ISO patch diijinkan berdampingan dengan fix komunitas.
             * Kalau hapus gagal, batalkan patch — jangan bikin ganda. */
            if (g->has_config) {
                char cfg_path[1024];
                ws_config_path(PS2ISO_DIR, g->iso_name, cfg_path,
                               sizeof(cfg_path));
                if (config_is_ours(cfg_path, state, g->iso_name)) {
                    if (!ws_file_exists(cfg_path)) {
                        /* Catatan scan basi: file sudah tidak ada. */
                        g->has_config = 0;
                        log_line("  app .CONFIG already gone\n");
                    } else {
                        char del_status[256];
                        if (remove_config_file(g, state, del_status,
                                               sizeof(del_status)) != 0) {
                            snprintf(status, status_size,
                                     "patch aborted: cannot remove app "
                                     ".CONFIG (see wsm.log)");
                            return;
                        }
                        log_line("  app .CONFIG removed before ISO "
                                 "patch: %s\n",
                                 del_status);
                    }
                } else {
                    log_line("  custom .CONFIG kept (supplemental)\n");
                }
            }
            int arc = ws_iso_patch_apply(PS2ISO_DIR, g->iso_name, text,
                                         text_len, &report, &n_applied,
                                         &n_skipped);
            if (arc == 1) {
                snprintf(status, status_size,
                         "already patched (ISO already has these values)");
                log_line("  %s\n", status);
            } else if (arc != 0) {
                int k;
                snprintf(status, status_size, "patch failed (see wsm.log)");
                for (k = 0; k < report.count; k++)
                    if (strncmp(report.lines[k], "error:", 6) == 0) {
                        snprintf(status, status_size, "%s", report.lines[k]);
                        break;
                    }
                log_line("  patch failed:\n");
                log_report(&report);
            } else {
                g->iso_patched = 1;
                snprintf(status, status_size,
                         "Patched ISO: %d patch(es) - backup %s%s",
                         n_applied, base_name(wsrest_path),
                         g->has_config ? " (custom .CONFIG kept)" : "");
                log_line("  %s\n", status);
                log_report(&report);
            }
        }
    }
}

/* Daftar utama (Fase 2t): baca file .pnach game, lalu jalankan aksi
 * Start di atas. Restore tidak butuh file — langsung ke _text. */
static void iso_patch_action(ws_game_t *g, ws_state_t *state,
                             char *status, size_t status_size)
{
    char *text = NULL;
    size_t text_len = 0;
    if (g->iso_patched) {
        iso_patch_action_text(g, state, NULL, 0, status, status_size);
        return;
    }
    if (!g->patch_file[0] ||
        ws_patch_read_file(patch_dir_for(g), g->patch_file, &text,
                           &text_len) != 0) {
        snprintf(status, status_size, "no patch file for this game");
        return;
    }
    iso_patch_action_text(g, state, text, text_len, status, status_size);
    free(text);
}

/* Pilih satu file dari kandidat duplikat (Fase 3l): game yang
 * lookup-nya ambiguous (2+ file, app tidak bisa memilih sendiri).
 * Baris = nama file (serial + CRC-nya kelihatan); baris detail =
 * gametitle file yang dipilih (di sanalah penjelasannya, mis.
 * "(English patch CRC)"). X = pilih, Bulatan = batal. Pilihan
 * session-only — scan berikutnya game ini ambiguous lagi.
 * Kembalikan 1 kalau user memilih + file berhasil dimuat (Fase 3m:
 * pemanggil langsung buka editor), 0 kalau batal / gagal. */
static int pick_dup_patch(ws_game_t *g, ws_patch_index_t *pindex,
                          char *status, size_t status_size)
{
    int total, n, sel = 0, top = 0, i, picked = 0;
    uint64_t last_move = 0;
    int repeat = 0;
    total = ws_patch_candidates(pindex, g->crc, g->serial_norm,
                                dup_files, 16);
    if (total <= 0) {
        snprintf(status, status_size, "no patch file for this game");
        return 0;
    }
    n = total > 16 ? 16 : total;
    for (i = 0; i < n; i++) {
        char *text = NULL;
        size_t text_len = 0;
        char title[256] = "";
        if (ws_patch_read_file(PATCHES_DIR, dup_files[i], &text,
                               &text_len) == 0) {
            ws_gametitle(text, text_len, title, sizeof(title));
            free(text);
        }
        /* Salin manual (bukan snprintf "%s"): snprintf di sini
         * memicu -Wformat-truncation karena compiler tidak bisa
         * membuktikan panjang sumbernya. */
        {
            const char *src = title[0] ? title : dup_files[i];
            size_t sl = strlen(src);
            if (sl > sizeof(dup_titles[i]) - 1)
                sl = sizeof(dup_titles[i]) - 1;
            memcpy(dup_titles[i], src, sl);
            dup_titles[i][sl] = '\0';
        }
    }
    while (!exit_requested) {
        uint32_t *fb, pressed, held;
        unsigned w, h;
        int margin, header_h, footer_h, row_h, list_y0;
        int small, rows_pp, row_h_base;
        char line[512];
        int name_chars;
        uint64_t now = sysGetSystemTime();

        sysUtilCheckCallback();
        if (menu_open) {
            fb = video_frame(&w, &h);
            video_present_unchanged();
            usleep(5000);
            continue;
        }
        pressed = pad_pressed();
        held = pad_held();
        if (pressed & PAD_CIRCLE) break;
        if (pressed & PAD_CROSS) { picked = 1; break; }
        {
            int move = 0;
            if (pressed & PAD_UP) move = -1;
            if (pressed & PAD_DOWN) move = 1;
            if (move == 0 && (held & (PAD_UP | PAD_DOWN))) {
                uint64_t delay = repeat ? 90000 : 400000;
                if (now - last_move >= delay) {
                    move = (held & PAD_UP) ? -1 : 1;
                    repeat = 1;
                }
            } else if (move != 0) {
                repeat = 0;
            }
            if (move != 0) {
                last_move = now;
                sel += move;
                if (sel < 0) sel = n - 1;
                if (sel > n - 1) sel = 0;
            }
        }

        small = video_small();
        rows_pp = small ? 8 : ROWS_PER_PAGE;
        row_h_base = small ? 60 : ROW_H;
        fb = video_frame(&w, &h);
        draw_fill(fb, w, h, 0, 0, (int)w, (int)h, COL_BG);
        margin = 64 * (int)h / 720;
        header_h = (small ? 96 : 64) * (int)h / 720;
        footer_h = 52 * (int)h / 720;
        row_h = row_h_base * (int)h / 720;
        list_y0 = (small ? 108 : 90) * (int)h / 720;
        name_chars = ((int)w - margin * 2) / 16;
        if (name_chars < 8) name_chars = 8;

        draw_fill(fb, w, h, 0, 0, (int)w, header_h, COL_BAR);
        draw_fill(fb, w, h, margin, (small ? 36 : 17) * (int)h / 720,
                  4 * (int)h / 720, 30 * (int)h / 720, COL_TITLE);
        draw_text(fb, w, h, margin + 16 * (int)h / 720,
                  (small ? 36 : 17) * (int)h / 720,
                  "Choose patch file", COL_TEXT);
        snprintf(line, sizeof(line), "%d file%s", total,
                 total == 1 ? "" : "s");
        draw_text(fb, w, h,
                  (int)w - margin - draw_text_width(line),
                  (small ? 36 : 17) * (int)h / 720, line, COL_SUB);

        if (sel < top) top = sel;
        if (sel >= top + rows_pp) top = sel - rows_pp + 1;
        for (i = 0; i < rows_pp && top + i < n; i++) {
            int y = list_y0 + i * row_h;
            int is_sel = (top + i == sel);
            char name[512];
            if (is_sel)
                draw_fill(fb, w, h, margin / 2, y - 6 * (int)h / 720,
                          (int)w - margin, row_h - 2 * (int)h / 720,
                          COL_SEL_BG);
            trunc_name(dup_files[top + i], name, sizeof(name), name_chars);
            draw_text(fb, w, h, margin, y, name,
                      is_sel ? COL_SEL_TEXT : COL_TEXT);
        }

        /* Baris detail: gametitle file yang dipilih — penjelasnya. */
        {
            int row_y = (int)h - footer_h -
                        (small ? 48 : 36) * (int)h / 720;
            trunc_name(dup_titles[sel], line, sizeof(line), name_chars);
            draw_text(fb, w, h, margin, row_y, line, COL_SUB);
        }
        draw_fill(fb, w, h, 0, (int)h - footer_h, (int)w, footer_h,
                  COL_BAR);
        {
            footer_seg_t segs[] = {
                { WS_BTN_CROSS, "choose" },
                { WS_BTN_CIRCLE, "cancel" },
            };
            int foot_y = small ? (int)h - footer_h + (footer_h - 30) / 2
                               : (int)h - 41 * (int)h / 720;
            draw_footer_segs(fb, w, h, margin,
                             foot_y, segs,
                             sizeof(segs) / sizeof(segs[0]));
        }
        video_present(fb);
        usleep(5000);
    }
    if (picked && !exit_requested) {
        if (ws_game_load_patch(g, PATCHES_DIR, dup_files[sel]) == 0) {
            snprintf(status, status_size, "patch: %s", dup_files[sel]);
            log_line("  dup pick: %s -> %s\n", g->iso_name,
                     dup_files[sel]);
            return 1;
        }
        snprintf(status, status_size, "patch not readable");
    }
    return 0;
}

int main(void)
{
    /* PERHATIAN: res + state BESAR (352 KB + 128 KB). Dulu keduanya
     * lokal di main() — frame main() jadi 497 KB dan app mati sebelum
     * main() jalan (stack thread utama PS3 tidak sebesar itu; crash di
     * prologue, sebelum fopen log — makanya wsm.log tidak terbentuk).
     * Sekarang di BSS (static), frame main() tinggal ~3 KB.
     * Indeks patch (~140 KB, nama file saja) juga di BSS — isi file
     * .pnach TIDAK pernah dimuat utuh (pelajaran cheat fork). */
    static ws_patch_index_t pindex;
    static ws_user_index_t uindex;
    static ws_state_t state;
    static ws_scan_result_t res;
    int have_pindex, have_uindex, have_configdb;
    int sel = 0, scroll = 0;
    uint64_t last_move = 0;
    int repeat = 0;
    char status[256] = "";
    int status_show = 0;

    logf = fopen(APP_DIR "/wsm.log", "w");
    log_line("PS2 Widescreen Manager phase 2 started\n");

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    pad_init();
    {
        int vi = video_init();
        if (vi != 0) {
            log_line("video_init failed (step %d)\n", vi);
            if (logf) fclose(logf);
            return 1;
        }
    }
    memset(&pindex, 0, sizeof(pindex));
    have_pindex = ws_patch_index_load(&pindex, PATCHES_DIR) == 0;
    log_line("patches/ %s (%d file, %s)\n",
             have_pindex ? "index loaded" : "NOT found",
             have_pindex ? (int)pindex.count : 0, PATCHES_DIR);
    /* Pnach user (Fase 3k): folder tidak ada = tidak ada pnach user,
     * bukan error — app jalan normal dengan database system saja. */
    memset(&uindex, 0, sizeof(uindex));
    have_uindex = ws_user_index_load(&uindex, USER_PATCHES_DIR) == 0;
    log_line("user pnach/ %s (%d file, %s)\n",
             have_uindex ? "index loaded" : "NOT found",
             have_uindex ? (int)uindex.count : 0, USER_PATCHES_DIR);
    /* configdb per-file: tidak ada yang dimuat di startup — entry
       dibaca satu-per-game saat toggle ON. Di sini cuma dicek ada. */
    {
        DIR *d = opendir(CONFIGDB_DIR);
        have_configdb = d != NULL;
        if (d) closedir(d);
    }
    log_line("configdb/ %s (%s)\n",
             have_configdb ? "found" : "NOT found", CONFIGDB_DIR);
    ws_state_load(&state, STATE_PATH);
    log_line("state: %d game ON oleh app\n", state.count);

    {
        int scan_rc;
        /* Progress bar native (Fase 2x): total dihitung dulu (readdir
         * saja, tanpa baca isi file), dialog dibuka sebelum scan dan
         * ditutup sesudahnya — di SEMUA jalur (sukses, gagal buka
         * direktori, batal tombol PS). Tanpa ISO = tanpa dialog. */
        scan_total = ws_count_iso_files(PS2ISO_DIR);
        scan_shown = 0;
        scan_progress_open = 0;
        if (scan_total > 0 &&
            msgDialogOpen2(MSG_DIALOG_SINGLE_PROGRESSBAR |
                           MSG_DIALOG_MUTE_ON,
                           "PS2 Widescreen Manager", NULL, NULL, NULL)
                == 0) {
            msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0,
                                      "Scanning games...");
            msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX0);
            scan_progress_open = 1;
        }
        scan_rc = ws_scan_dir(PS2ISO_DIR, &pindex, PATCHES_DIR,
                              have_uindex ? &uindex : NULL,
                              have_uindex ? USER_PATCHES_DIR : NULL,
                              &res, scan_progress_cb, &res);
        if (scan_progress_open) msgDialogAbort();
        if (scan_rc == 1) {
            /* Dibatalkan tombol PS di tengah scan — keluar langsung. */
            log_line("scan aborted (PS button), %d ISO read\n",
                     res.count);
            ws_patch_index_free(&pindex);
            ws_user_index_free(&uindex);
            sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
            if (logf) fclose(logf);
            return 0;
        }
        if (scan_rc != 0) {
            memset(&res, 0, sizeof(res));
            log_line("failed to open %s for scan\n", PS2ISO_DIR);
        } else {
        int i;
        log_line("scan: %d ISO\n", res.count);
        for (i = 0; i < res.count; i++) {
            ws_game_t *g = &res.games[i];
            char errbuf[160] = "";
            char dbs[160] = "";
            if (g->avail == WS_AVAIL_ERROR)
                snprintf(errbuf, sizeof(errbuf), "  ERR: %s", g->err);
            /* db user = nama file-nya ikut dicatat (diagnostik:
             * file user mana yang menang harus kelihatan di log).
             * Fase 3m: ambiguous = "duplicate", bukan "not found" —
             * file-nya ADA, cuma app tidak bisa memilih sendiri. */
            if (g->avail == WS_AVAIL_AMBIGUOUS)
                snprintf(dbs, sizeof(dbs), "duplicate (%d)",
                         g->dup_count);
            else if (!g->patch_file[0])
                snprintf(dbs, sizeof(dbs), "not found");
            else if (g->user_patch)
                snprintf(dbs, sizeof(dbs), "user:%s", g->patch_file);
            else if (g->serial_fallback)
                snprintf(dbs, sizeof(dbs), "serial");
            else
                snprintf(dbs, sizeof(dbs), "found");
            /* %zu JANGAN dipakai: newlib PSL1GHT menggantung (hang) di
             * %zu — Fase 2c hang di baris log pertama gara-gara ini.
             * Pakai %lu + cast, C89, aman. */
            log_line("  %s | boot %s (%lu bytes, entry %08X) | %s | CRC %08X | db %s | act %s | iso %s | %d lines (%d ws16, %d conv)%s\n",
                     g->iso_name,
                     g->boot[0] ? g->boot : "-",
                     (unsigned long)g->elf_len, g->elf_entry,
                     g->title_id, g->crc,
                     dbs,
                     g->has_config ? "yes" : "no",
                     g->iso_patched ? "patched" : "-",
                     g->n_lines, g->ws_lines, g->n_conv,
                     errbuf);
        }
        }
    }

    while (!exit_requested) {
        uint32_t *fb, pressed, held;
        unsigned w, h;
        int rows_per_page, i;
        int small;
        int row_h_base;
        char line[512], name[256];
        int name_chars;
        uint64_t now = sysGetSystemTime();

        /* Layout ringkas 480p (Fase 3i) — lihat edit_patches(). */
        small = video_small();
        rows_per_page = small ? 8 : ROWS_PER_PAGE;
        row_h_base = small ? 60 : ROW_H;

        sysUtilCheckCallback();
        /* XMB buka (Fase 2u): jangan baca pad / gambar ulang / flush.
         * Flip buffer belakang apa adanya (isinya frame 2 iterasi
         * lalu — sama saja, layar statis saat idle) supaya queue
         * flip tetap bergerak; XMB tidak berebut PPU dengan kita.
         * Pelajaran 2r: pause total bikin XMB tidak muncul. */
        if (menu_open) {
            fb = video_frame(&w, &h);
            video_present_unchanged();
            if (xmb_t0 == 0) { xmb_t0 = now; xmb_frames = 0; }
            xmb_frames++;
            if (now - xmb_t0 >= 1000000) {
                log_line("  xmb open: %d loops/s\n", xmb_frames);
                xmb_frames = 0;
                xmb_t0 = now;
            }
            usleep(5000);
            continue;
        }
        pressed = pad_pressed();
        held = pad_held();
        /* Bulatan = keluar ke XMB, tapi tanya dulu (Fase 2y): tanpa
         * konfirmasi, kepencet sedikit langsung keluar padahal belum
         * niat. Pola sama dengan konfirmasi ISO patch: X = ya,
         * Bulatan = batal. PS tetap keluar langsung (sistem). */
        if (pressed & PAD_CIRCLE) {
            const char *lines[2];
            lines[0] = "Exit to the XMB?";
            lines[1] = "The app will close.";
            if (confirm_screen("Exit to XMB", lines, 2, "yes - exit"))
                break;
        }
        /* Select: toggle prioritas old value (Fase 2s, session-only).
         * Daftar utama saja — editor tidak menampilkan labelnya
         * (tidak muat di 720p), tapi pakai prioritas yang sama. */
        if (pressed & PAD_SELECT)
            old_prio_elf_first = !old_prio_elf_first;
        /* Start: ISO patch / restore (Fase 2t). Shortcut deliberate —
         * bukan tombol yang kepencet tidak sengaja. Warning +
         * konfirmasi Yes/No muncul dulu; tidak ada tulis diam-diam. */
        if (pressed & PAD_START && res.count > 0) {
            ws_game_t *g = &res.games[sel];
            status[0] = '\0';
            if (g->avail == WS_AVAIL_ERROR) {
                snprintf(status, sizeof(status), "ISO/ELF error: %s",
                         g->err);
            } else if (g->avail == WS_AVAIL_AMBIGUOUS) {
                /* Fase 3l/3m: Start di game duplikat = pilih dulu,
                 * sesudah pilih langsung masuk editor. */
                if (pick_dup_patch(g, &pindex, status, sizeof(status)))
                    edit_patches(g, &state, have_configdb, status,
                                 sizeof(status));
            } else {
                iso_patch_action(g, &state, status, sizeof(status));
            }
            status_show = 1;
        }
        if (pressed & PAD_CROSS && res.count > 0) {
            ws_game_t *g = &res.games[sel];
            status[0] = '\0';
            if (g->has_config) {
                /* ---- Toggle OFF: hapus .CONFIG (backup kalau manual).
                 * OFF tidak di-gate avail/widescreen (Fase 2w): mematikan
                 * cuma menghapus file .CONFIG game ini — tidak ada patch
                 * yang bisa salah ke-apply. Sebelumnya OFF cuma bisa
                 * lewat YES && has_config, jadi game tanpa patch
                 * widescreen (mis. NFS Most Wanted, cuma post-processing)
                 * yang ON lewat editor cuma bisa OFF lewat editor
                 * (buang semua centang) — kikuk. */
                remove_config_file(g, &state, status, sizeof(status));
            } else if (g->avail == WS_AVAIL_YES) {
                /* ---- Toggle ON: konversi + tulis .CONFIG ---- */
                /* Default = widescreen-only: konverter sendiri tidak
                 * peduli grup, jadi teks yang diberikan ke ws_convert
                 * hanya berisi baris widescreen (bukan semua
                 * patch di file). Isi .pnach dibaca per-file di
                 * sini (satu file kecil), bukan dari blob — lalu
                 * di-free sesudah konversi. */
                char *text = NULL;
                size_t text_len = 0;
                if (!g->patch_file[0] ||
                    ws_patch_read_file(patch_dir_for(g), g->patch_file, &text,
                                       &text_len) != 0) {
                    snprintf(status, sizeof(status),
                             "patch not found (unexpected — please report)");
                    log_line("  %s\n", status);
                } else {
                    int n_items = ws_patch_list(text, text_len, ed_items,
                                                WS_MAX_PATCH_ITEMS);
                    int i;
                    char *ftext = NULL;
                    size_t ftext_len = 0;
                    for (i = 0; i < n_items; i++)
                        ed_checked[i] =
                            ed_items[i].ws_kind == 2 && ed_items[i].enabled;
                    if (ws_patch_filtered_text(text, text_len, ed_items,
                                               ed_checked, n_items,
                                               &ftext, &ftext_len) != 0) {
                        snprintf(status, sizeof(status),
                                 "failed to build patch list");
                    } else {
                        apply_patches(g, ftext, ftext_len, &state,
                                      have_configdb, status, sizeof(status));
                        free(ftext);
                    }
                    free(text);
                }
            } else if (g->avail == WS_AVAIL_AMBIGUOUS) {
                /* Fase 3l/3m: X di game duplikat = pilih dulu,
                 * sesudah pilih langsung masuk editor. */
                if (pick_dup_patch(g, &pindex, status, sizeof(status)))
                    edit_patches(g, &state, have_configdb, status,
                                 sizeof(status));
            } else if (g->avail == WS_AVAIL_ERROR) {
                snprintf(status, sizeof(status), "ISO/ELF error: %s",
                         g->err);
            } else if (!g->patch_file[0]) {
                snprintf(status, sizeof(status),
                         "no patch file for this game");
            } else {
                snprintf(status, sizeof(status),
                         "no 16:9 widescreen patch - open the editor to pick other patches");
            }
            status_show = 1;
        }
        if (pressed & PAD_TRIANGLE && res.count > 0) {
            /* Editor patch: pilih patch mana yang aktif untuk game ini.
             * Butuh file .pnach (bukan cuma widescreen) — game tanpa
             * file patch tidak bisa diedit. */
            ws_game_t *g = &res.games[sel];
            status[0] = '\0';
            if (g->avail == WS_AVAIL_ERROR) {
                snprintf(status, sizeof(status), "ISO/ELF error: %s",
                         g->err);
            } else if (g->avail == WS_AVAIL_AMBIGUOUS) {
                /* Fase 3l/3m: Segitiga di game duplikat = pilih dulu,
                 * sesudah pilih langsung masuk editor. */
                if (pick_dup_patch(g, &pindex, status, sizeof(status)))
                    edit_patches(g, &state, have_configdb, status,
                                 sizeof(status));
            } else if (!g->patch_file[0]) {
                snprintf(status, sizeof(status),
                         "no patch file for this game");
            } else {
                edit_patches(g, &state, have_configdb, status,
                             sizeof(status));
            }
            status_show = 1;
        }
        if (res.count > 0) {
            int move = 0;
            int old_sel = sel;
            if (pressed & PAD_UP) move = -1;
            if (pressed & PAD_DOWN) move = 1;
            /* L1/R1: lompat satu halaman (pola fork menu.c). */
            if (pressed & PAD_L1)
                sel = sel > rows_per_page ? sel - rows_per_page : 0;
            if (pressed & PAD_R1)
                sel = sel + rows_per_page < res.count
                          ? sel + rows_per_page : res.count - 1;
            if (move == 0 && (held & (PAD_UP | PAD_DOWN))) {
                /* Repeat saat ditahan: jeda awal 400 ms, lalu 90 ms. */
                uint64_t delay = repeat ? 90000 : 400000;
                if (now - last_move >= delay) {
                    move = (held & PAD_UP) ? -1 : 1;
                    repeat = 1;
                }
            } else if (move != 0) {
                repeat = 0;
            }
            if (move != 0) {
                last_move = now;
                sel += move;
                if (sel < 0) sel = 0;
                if (sel > res.count - 1) sel = res.count - 1;
                if (sel != old_sel) status_show = 0;
            }
        }

        fb = video_frame(&w, &h);
        draw_fill(fb, w, h, 0, 0, (int)w, (int)h, COL_BG);
        /* Layout ala fork (menu_run/header_app/footer). Semua ukuran
         * dalam piksel 720p, dikali h/720 — 720p=1x, 1080p=1.5x. */
        {
            int margin = 64 * (int)h / 720;
            int header_h = (small ? 96 : 64) * (int)h / 720;
            int footer_h = 52 * (int)h / 720;
            int row_h = row_h_base * (int)h / 720;
            int list_y0 = (small ? 108 : 92) * (int)h / 720;
            int detail_y = (int)h - footer_h - (small ? 48 : 36) * (int)h / 720;
            int col_y = 68 * (int)h / 720;
            int serial_x = (int)w - (small ? 500 : 640) * (int)h / 720;
            int crc_x = (int)w - 480 * (int)h / 720;
            int db_x = (int)w - 320 * (int)h / 720;
            int act_x = (int)w - (small ? 240 : 160) * (int)h / 720;
            int foot_y = small ? (int)h - footer_h + (footer_h - 30) / 2
                               : (int)h - 41 * (int)h / 720;
            int pages = res.count ? (res.count + rows_per_page - 1) / rows_per_page : 1;
            int page = res.count ? scroll / rows_per_page + 1 : 1;

            /* Bar header: aksen 4px + judul, kanan jumlah game + halaman.
             * Layar kecil: bar lebih tinggi + teks ke safe area, teks kanan
             * dipendekkan ("39 games 1/5") supaya tidak menabrak judul. */
            draw_fill(fb, w, h, 0, 0, (int)w, header_h, COL_BAR);
            draw_fill(fb, w, h, margin, (small ? 36 : 17) * (int)h / 720,
                      4 * (int)h / 720, 30 * (int)h / 720, COL_TITLE);
            draw_text(fb, w, h, margin + 16 * (int)h / 720,
                      (small ? 36 : 17) * (int)h / 720, APP_TITLE, COL_TEXT);
            if (small)
                snprintf(line, sizeof(line), "%d game%s %d/%d",
                         res.count, res.count == 1 ? "" : "s", page, pages);
            else
                snprintf(line, sizeof(line), "%d game%s   L1/R1: page %d/%d",
                         res.count, res.count == 1 ? "" : "s", page, pages);
            if (!have_pindex && !small)
                strncat(line, "   [patches folder not found]",
                        sizeof(line) - strlen(line) - 1);
            draw_text(fb, w, h,
                      (int)w - margin - draw_text_width(line),
                      (small ? 36 : 17) * (int)h / 720, line, COL_SUB);

            if (sel < scroll) scroll = sel;
            if (sel >= scroll + rows_per_page) scroll = sel - rows_per_page + 1;
            name_chars = (serial_x - margin - 16 * (int)h / 720) / 16;
            if (name_chars < 8) name_chars = 8;

            /* Baris header kolom (keterangan di atas list — Fase 2p):
             * database = file .pnach ketemu (jenis apa pun), activated
             * = 2 jenis (Fase 2z): "yes" (.CONFIG), "patched" (ISO),
             * "yes+iso" (keduanya), "no". Layar kecil: TIDAK digambar —
             * label 30 px tidak muat di bar 19 px tanpa menabrak baris
             * list pertama; kolomnya sudah jelas dari isinya (judul /
             * serial / yes+iso). */
            if (!small) {
                draw_fill(fb, w, h, 0, header_h, (int)w, 28 * (int)h / 720,
                          COL_SEL_BG);
                draw_text(fb, w, h, margin, col_y, "Title", COL_TITLE);
                draw_text(fb, w, h, serial_x, col_y, "serial", COL_TITLE);
                draw_text(fb, w, h, crc_x, col_y, "crc", COL_TITLE);
                draw_text(fb, w, h, db_x, col_y, "database", COL_TITLE);
                draw_text(fb, w, h, act_x, col_y, "activated", COL_TITLE);
            }

            if (res.count == 0) {
                draw_text(fb, w, h, margin, list_y0,
                          "No .iso files in /dev_hdd0/PS2ISO/", COL_TEXT);
            }
            for (i = 0; i < rows_per_page && scroll + i < res.count; i++) {
                ws_game_t *g = &res.games[scroll + i];
                int y = list_y0 + i * row_h;
                int is_sel = (scroll + i == sel);
                if (is_sel)
                    draw_fill(fb, w, h, margin / 2, y - 6 * (int)h / 720,
                              (int)w - margin, row_h - 2 * (int)h / 720,
                              COL_SEL_BG);
                trunc_name(g->title, name, sizeof(name), name_chars);
                draw_text(fb, w, h, margin, y, name,
                          is_sel ? COL_SEL_TEXT : COL_TEXT);
                draw_text(fb, w, h, serial_x, y,
                          g->title_id[0] ? g->title_id : "-",
                          is_sel ? COL_SEL_TEXT : COL_TEXT);
                /* Layar kecil: kolom crc + database disembunyikan
                 * (info-nya tetap ada di baris detail editor / log) —
                 * serial + activated saja yang muat tanpa tabrakan. */
                if (!small) {
                    if (g->avail != WS_AVAIL_ERROR) {
                        snprintf(line, sizeof(line), "%08X", g->crc);
                        draw_text(fb, w, h, crc_x, y, line,
                                  is_sel ? COL_SEL_TEXT : COL_TEXT);
                    }
                    /* Kolom database (Fase 3m): ambiguous = "duplicate"
                     * (amber), bukan "not found" — file-nya ada, cuma
                     * belum dipilih. */
                    {
                        const char *db_txt;
                        uint32_t db_col;
                        if (g->avail == WS_AVAIL_AMBIGUOUS) {
                            db_txt = "duplicate";
                            db_col = COL_AMB;
                        } else if (g->patch_file[0]) {
                            db_txt = g->user_patch ? "user" : "found";
                            db_col = COL_YES;
                        } else {
                            db_txt = "not found";
                            db_col = COL_NO;
                        }
                        draw_text(fb, w, h, db_x, y, db_txt, db_col);
                    }
                }
                /* Status aktif 2 jenis (Fase 2z): .CONFIG saja =
                 * "yes", ISO patch saja = "patched", keduanya =
                 * "yes+iso", tidak ada = "no". */
                {
                    const char *act;
                    if (g->has_config && g->iso_patched) act = "yes+iso";
                    else if (g->iso_patched) act = "patched";
                    else if (g->has_config) act = "yes";
                    else act = "no";
                    draw_text(fb, w, h, act_x, y, act,
                              (g->has_config || g->iso_patched) ? COL_YES
                                                                : COL_NO);
                }
            }

            /* Baris detail / status + bar footer. Layar kecil: detail
             * counts dipendekkan jadi "%d lines" supaya label prioritas
             * muat di baris yang sama (rata kanan); pesan error/status
             * tetap penuh tanpa label (prioritas tidak relevan di situ).
             * " - PATCHED" tidak ditambah di layar kecil — kolom
             * activated + label Start: restore sudah menunjukkannya. */
            if (status_show && status[0]) {
                draw_text(fb, w, h, margin, detail_y, status, COL_TITLE);
            } else if (res.count > 0) {
                ws_game_t *g = &res.games[sel];
                int detail_counts = 0;
                if (g->avail == WS_AVAIL_ERROR)
                    snprintf(line, sizeof(line), "%s : %s", g->iso_name, g->err);
                else if (g->avail == WS_AVAIL_AMBIGUOUS)
                    snprintf(line, sizeof(line),
                             "%s : %d duplicate patch files - press X to choose",
                             g->iso_name, g->dup_count);
                else if (!g->patch_file[0])
                    snprintf(line, sizeof(line),
                             "%s : no patch file (database not found)",
                             g->iso_name);
                else if (small) {
                    snprintf(line, sizeof(line), "%d lines", g->n_lines);
                    detail_counts = 1;
                } else if (g->ws_lines == 0 && g->n_conv > 0)
                    snprintf(line, sizeof(line),
                             "%d patch lines - 0 widescreen 16:9 - %d convertible - open the editor",
                             g->n_lines, g->n_conv);
                else
                    snprintf(line, sizeof(line),
                             "%d patch lines - %d widescreen 16:9 - %d convertible",
                             g->n_lines, g->ws_lines, g->n_conv);
                /* State PATCHED di baris detail; aksi Start-nya ada
                 * di footer (label dinamis: patch / restore). */
                if (g->iso_patched && !small)
                    strncat(line, " - PATCHED",
                            sizeof(line) - strlen(line) - 1);
                draw_text(fb, w, h, margin, detail_y, line, COL_SUB);
                if (small && detail_counts)
                    draw_priority_label(fb, w, h, detail_y);
            }
            draw_fill(fb, w, h, 0, (int)h - footer_h, (int)w, footer_h,
                      COL_BAR);
            {
                /* Label Start dinamis (Fase 2t): game yang sudah
                 * PATCHED, Start = restore, bukan patch. Segitiga
                 * kembali ke footer (Fase 3a): "edit" = buka patch
                 * selector — sempat hilang di 2t, dan footer tanpa
                 * label itu tidak menjelaskan Triangle. Segmen ▲▼
                 * DIHAPUS di Fase 3f: jarak glyph-nya ke segmen
                 * berikut cuma 4 px (nempel, apalagi di CRT 480p),
                 * dan jarak 2 spasi tidak muat — footer berakhir
                 * x=764 vs label prioritas mulai x=768 (cuma 4 px
                 * sisa); 3 spasi malah menabrak label. Jadi sesuai
                 * fallback Fendi ("kalau gak bisa ... dihapus saja"),
                 * segmennya dibuang dari kedua footer. */
                const char *start_label = "Start: patch ISO";
                if (res.count > 0 && res.games[sel].iso_patched)
                    start_label = "Start: restore";
                footer_seg_t segs[] = {
                    { WS_BTN_CROSS, "on/off" },
                    { WS_BTN_NONE, start_label },
                    { WS_BTN_CIRCLE, "exit" },
                    { WS_BTN_TRIANGLE, "edit" },
                };
                int fx = draw_footer_segs(fb, w, h, margin,
                                          foot_y,
                                          segs, sizeof(segs) / sizeof(segs[0]));
                if (!have_configdb && !small)
                    draw_text(fb, w, h, fx,
                              foot_y,
                              "[configdb folder not found - no merge]",
                              COL_DIM);
                /* Layar kecil: label prioritas pindah ke baris detail
                 * (di atas) — footer tidak muat label 448 px lagi. */
                if (!small)
                    draw_priority_label(fb, w, h, foot_y);
            }
        }
        video_present(fb);
        usleep(5000);
    }

    ws_patch_index_free(&pindex);
    ws_user_index_free(&uindex);
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    if (logf) fclose(logf);
    return 0;
}
