/* Laporan konversi/merge: daftar baris warning/info/error, formatnya
 * sama dengan tool PC (pnach2config) supaya log bisa dibandingkan. */
#ifndef WS_REPORT_H
#define WS_REPORT_H

#include <stdarg.h>

#define WS_REPORT_MAX_LINES 64
#define WS_REPORT_LINE_LEN 200

typedef struct {
    char lines[WS_REPORT_MAX_LINES][WS_REPORT_LINE_LEN];
    int count;
} ws_report_t;

void ws_report_add(ws_report_t *r, const char *fmt, ...);
void ws_report_addv(ws_report_t *r, const char *fmt, va_list ap);

#endif
