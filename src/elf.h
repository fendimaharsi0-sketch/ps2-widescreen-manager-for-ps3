/* Pemeriksaan ELF (Fase 1: hanya validasi magic + class 32-bit).
 * Pembacaan segmen (PT_LOAD, BSS) untuk old value datang di Fase 2. */
#ifndef WS_ELF_H
#define WS_ELF_H

#include <stddef.h>
#include <stdint.h>

/* 1 kalau buf adalah ELF 32-bit, 0 kalau bukan. */
int ws_elf_check(const unsigned char *buf, size_t len);

/* Baca word 32-bit little-endian di alamat EE `addr` dari ELF di memori
 * (port dari readElfWord pnach2config.cpp — header + program header saja,
 * tanpa file I/O).
 * Kembalikan 1 kalau ketemu (*out diisi), 0 kalau alamat tidak tercakup
 * segment PT_LOAD mana pun, -1 kalau ELF-nya malformed (bukan ELF,
 * bukan 32-bit, bukan little-endian, tidak ada program header).
 * Alamat di wilayah BSS (p_filesz..p_memsz) menghasilkan 0 — nilai
 * load-nya memang 0, bukan tebakan. */
int ws_elf_word(const unsigned char *elf, size_t len, uint32_t addr,
                uint32_t *out);

/* Offset file (dalam file ELF) untuk range [addr, addr+size) — harus
 * seluruhnya tercakup byte file SATU PT_LOAD. Kembalikan 1 kalau
 * ketemu (*fo_out diisi), 0 kalau tidak tercakup / BSS / membentang
 * dua segmen, -1 kalau ELF malformed. Dipakai ISO patch (Fase 2t). */
int ws_elf_offset(const unsigned char *elf, size_t len, uint32_t addr,
                  uint32_t size, uint32_t *fo_out);

/* Hitung ukuran file ELF dari program header (maks p_offset+p_filesz
 * semua segmen, dan tabel section header kalau ada). Kembalikan 1 kalau
 * header + program header valid (ada >= 1 PT_LOAD, semua nilai masuk
 * akal), 0 kalau tidak. Dipakai scan ELF langsung (Fase 2g): ukuran ini
 * yang menentukan berapa byte ELF yang dibaca dari file mentah. */
int ws_elf_size(const unsigned char *elf, size_t len, uint32_t *size_out);

#endif
