/* Laporan konversi/merge. */
#include <stdio.h>
#include <string.h>

#include "report.h"

void ws_report_addv(ws_report_t *r, const char *fmt, va_list ap)
{
    if (r->count >= WS_REPORT_MAX_LINES) return;
    vsnprintf(r->lines[r->count], WS_REPORT_LINE_LEN, fmt, ap);
    r->lines[r->count][WS_REPORT_LINE_LEN - 1] = '\0';
    r->count++;
}

void ws_report_add(ws_report_t *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ws_report_addv(r, fmt, ap);
    va_end(ap);
}
