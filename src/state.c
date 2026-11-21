/* State toggle. */
#include <stdio.h>
#include <string.h>

#include "state.h"

int ws_state_load(ws_state_t *st, const char *path)
{
    FILE *f;
    char line[512];
    memset(st, 0, sizeof(*st));
    f = fopen(path, "r");
    if (!f) return 0;  /* belum ada = kosong, bukan error */
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (!n || st->count >= WS_MAX_GAMES) continue;
        if (ws_state_has(st, line)) continue;
        strncpy(st->names[st->count], line, 255);
        st->names[st->count][255] = '\0';
        st->count++;
    }
    fclose(f);
    return 0;
}

int ws_state_save(ws_state_t *st, const char *path)
{
    FILE *f;
    int i;
    f = fopen(path, "w");
    if (!f) return -1;
    for (i = 0; i < st->count; i++) fprintf(f, "%s\n", st->names[i]);
    if (fclose(f) != 0) return -1;
    return 0;
}

int ws_state_has(ws_state_t *st, const char *iso_name)
{
    int i;
    for (i = 0; i < st->count; i++)
        if (strcmp(st->names[i], iso_name) == 0) return 1;
    return 0;
}

void ws_state_add(ws_state_t *st, const char *iso_name)
{
    if (ws_state_has(st, iso_name)) return;
    if (st->count >= WS_MAX_GAMES) return;
    strncpy(st->names[st->count], iso_name, 255);
    st->names[st->count][255] = '\0';
    st->count++;
}

void ws_state_remove(ws_state_t *st, const char *iso_name)
{
    int i, j;
    for (i = 0; i < st->count; i++) {
        if (strcmp(st->names[i], iso_name) != 0) continue;
        for (j = i + 1; j < st->count; j++)
            strcpy(st->names[j - 1], st->names[j]);
        st->count--;
        return;
    }
}
