/* Harness: baca boot ELF dari satu ISO pakai kode app sendiri,
 * cetak nama boot + ukuran ELF + CRC32. Ground truth dicetak oleh
 * make_realistic_iso.py. */
#include <stdio.h>
#include <stdlib.h>

#include "crc32.h"
#include "iso9660.h"
#include "scan.h"

int main(int argc, char **argv)
{
    unsigned char *elf = NULL;
    size_t elf_len = 0;
    char err[128] = "";
    char boot[64] = "";
    ws_iso_t iso;
    unsigned char *cnf = NULL;
    size_t cnf_len = 0;

    if (argc < 2) {
        fprintf(stderr, "pakai: %s <file.iso>\n", argv[0]);
        return 2;
    }
    /* Jalur 1: ws_read_boot_elf (dipakai scan). */
    if (ws_read_boot_elf(".", argv[1], &elf, &elf_len, err, sizeof(err)) != 0) {
        printf("read_boot_elf GAGAL: %s\n", err);
        return 1;
    }
    printf("boot ELF dibaca: %zu byte, CRC game (XOR-fold) %08X, CRC32 IEEE %08X\n",
           elf_len, ws_ps2crc(elf, elf_len), ws_crc32(elf, elf_len));
    printf("ELF magic: %02X %02X %02X %02X, class %d\n",
           elf[0], elf[1], elf[2], elf[3], elf[4]);
    free(elf);

    /* Jalur 2: buka manual — SYSTEM.CNF + list root (cek fallback). */
    if (ws_iso_open(&iso, argv[1]) != 0) {
        printf("ws_iso_open GAGAL\n");
        return 1;
    }
    if (ws_iso_read_file(&iso, "SYSTEM.CNF", &cnf, &cnf_len) == 0) {
        printf("SYSTEM.CNF: %zu byte: %.*s\n", cnf_len, (int)cnf_len, cnf);
        free(cnf);
    } else {
        printf("SYSTEM.CNF TIDAK ketemu\n");
    }
    {
        static char names[256][64];
        int n = ws_iso_list_dir(&iso, "/", names, 256);
        int i, serial = 0;
        printf("root dir: %d entri (cap 256)\n", n);
        for (i = 0; i < n; i++)
            if (names[i][0] == 'S' && names[i][4] == '_') serial++;
        printf("entri bentuk serial di root: %d\n", serial);
        (void)boot;
    }
    ws_iso_close(&iso);
    return 0;
}
