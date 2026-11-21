/* Tes reader BIN mentah (sektor 2352): buka .bin, baca SYSTEM.CNF +
 * boot ELF, bandingkan CRC game (XOR-fold) dengan ground truth yang
 * diberikan sebagai argumen. Dipakai Fase 2f (17 game ERROR).
 *   ./test_bin <file.bin> <crc-hex-yang-diharapkan>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "elf.h"
#include "iso9660.h"

int main(int argc, char **argv)
{
    ws_iso_t iso;
    unsigned char *cnf = NULL, *elf = NULL;
    size_t cnf_len = 0, elf_len = 0;
    uint32_t want, got;
    int fail = 0;

    if (argc < 3) {
        fprintf(stderr, "pakai: %s <file.bin> <crc-hex>\n", argv[0]);
        return 2;
    }
    want = (uint32_t)strtoul(argv[2], NULL, 16);

    if (ws_iso_open(&iso, argv[1]) != 0) {
        printf("[FAIL] buka BIN %s\n", argv[1]);
        return 1;
    }
    printf("sektor: %u byte, data_off: %u\n", iso.sector_size, iso.data_off);
    if (iso.sector_size != 2352 || iso.data_off != 24) {
        printf("[FAIL] deteksi sektor salah (mau 2352/+24)\n");
        fail = 1;
    }
    if (ws_iso_read_file(&iso, "SYSTEM.CNF", &cnf, &cnf_len) != 0 ||
        cnf_len == 0 || strstr((char *)cnf, "BOOT2") == NULL) {
        printf("[FAIL] baca SYSTEM.CNF dari BIN\n");
        fail = 1;
    } else {
        printf("SYSTEM.CNF: %zu byte, BOOT2 ketemu\n", cnf_len);
    }
    if (ws_iso_read_file(&iso, "SLUS_211.59", &elf, &elf_len) != 0) {
        printf("[FAIL] baca SLUS_211.59 dari BIN\n");
        fail = 1;
    } else {
        got = ws_ps2crc(elf, elf_len);
        printf("boot ELF dibaca: %zu byte, CRC game %08X (mau %08X)\n",
               elf_len, got, want);
        if (!ws_elf_check(elf, elf_len)) {
            printf("[FAIL] ELF tidak valid\n");
            fail = 1;
        }
        if (got != want) {
            printf("[FAIL] CRC game BIN != ground truth\n");
            fail = 1;
        }
    }
    free(cnf);
    free(elf);
    ws_iso_close(&iso);
    printf("%s\n", fail ? "GAGAL" : "BIN OK");
    return fail ? 1 : 0;
}
