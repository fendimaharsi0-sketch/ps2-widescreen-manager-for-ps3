/* Pemeriksaan ELF minimal. */
#include <string.h>

#include "elf.h"

int ws_elf_check(const unsigned char *buf, size_t len)
{
    if (!buf || len < 52) return 0;
    if (memcmp(buf, "\177ELF", 4) != 0) return 0;
    if (buf[4] != 1) return 0;  /* ELFCLASS32 */
    return 1;
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const unsigned char *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Cari PT_LOAD yang mencakup `addr` (byte file ATAU BSS). Isi out
 * params. Kembalikan 1 kalau ketemu, 0 kalau alamat tidak tercakup
 * segmen mana pun, -1 kalau ELF malformed. Dipakai bersama oleh
 * ws_elf_word (baca word) dan ws_elf_offset (cari offset file). */
static int elf_find_load(const unsigned char *elf, size_t len,
                         uint32_t addr, uint32_t *p_offset,
                         uint32_t *p_vaddr, uint32_t *p_filesz,
                         uint32_t *p_memsz)
{
    uint32_t e_phoff;
    uint16_t e_phentsize, e_phnum;
    int i;
    if (!elf || len < 52) return -1;
    if (memcmp(elf, "\177ELF", 4) != 0) return -1;
    if (elf[4] != 1) return -1;   /* bukan 32-bit */
    if (elf[5] != 1) return -1;   /* bukan little-endian (ELF PS2 = LSB) */
    e_phoff = le32(elf + 28);
    e_phentsize = le16(elf + 42);
    e_phnum = le16(elf + 44);
    if (e_phentsize < 32 || e_phnum == 0) return -1;
    for (i = 0; i < e_phnum; i++) {
        const unsigned char *ph;
        uint32_t p_type, off, va, fsz, msz;
        uint64_t v;
        size_t ph_off = (size_t)e_phoff + (size_t)i * e_phentsize;
        if (ph_off + 32 > len) break;
        ph = elf + ph_off;
        p_type = le32(ph + 0);
        off = le32(ph + 4);
        va = le32(ph + 8);
        fsz = le32(ph + 16);
        msz = le32(ph + 20);
        if (p_type != 1) continue;  /* PT_LOAD saja */
        v = va;
        if (v <= addr && addr < v + fsz) {
            *p_offset = off;
            *p_vaddr = va;
            *p_filesz = fsz;
            *p_memsz = msz;
            return 1;
        }
        if (v <= addr && addr < v + msz) {
            *p_offset = off;
            *p_vaddr = va;
            *p_filesz = fsz;
            *p_memsz = msz;
            return 1;
        }
    }
    return 0;
}

int ws_elf_word(const unsigned char *elf, size_t len, uint32_t addr,
                uint32_t *out)
{
    uint32_t p_offset, p_vaddr, p_filesz, p_memsz;
    int rc = elf_find_load(elf, len, addr, &p_offset, &p_vaddr,
                           &p_filesz, &p_memsz);
    if (rc != 1) return rc;
    if (addr < p_vaddr + p_filesz) {
        size_t fo = (size_t)p_offset + (addr - p_vaddr);
        if (fo + 4 > len) return 0;
        *out = le32(elf + fo);
        return 1;
    }
    *out = 0;  /* BSS: di-nol-kan loader saat load */
    return 1;
}

/* Offset file (dalam file ELF) untuk range [addr, addr+size) — harus
 * seluruhnya tercakup byte file SATU PT_LOAD (tidak boleh BSS, tidak
 * boleh membentang dua segmen). Kembalikan 1 kalau ketemu (*fo_out
 * diisi), 0 kalau tidak tercakup / BSS / membentang, -1 kalau ELF
 * malformed. Dipakai ISO patch (Fase 2t): alamat BSS tidak punya
 * byte file untuk di-patch, jadi di-skip, bukan ditebak. */
int ws_elf_offset(const unsigned char *elf, size_t len, uint32_t addr,
                  uint32_t size, uint32_t *fo_out)
{
    uint32_t p_offset, p_vaddr, p_filesz, p_memsz;
    int rc = elf_find_load(elf, len, addr, &p_offset, &p_vaddr,
                           &p_filesz, &p_memsz);
    uint64_t end;
    if (rc != 1) return rc;
    if (size == 0 || size > 4) return 0;
    end = (uint64_t)addr + size;
    if (addr < p_vaddr || end > (uint64_t)p_vaddr + p_filesz) return 0;
    {
        size_t fo = (size_t)p_offset + (addr - p_vaddr);
        if (fo + size > len) return 0;
        *fo_out = (uint32_t)fo;
    }
    return 1;
}

int ws_elf_size(const unsigned char *elf, size_t len, uint32_t *size_out)
{
    uint32_t e_phoff, e_shoff;
    uint16_t e_phentsize, e_phnum, e_shentsize, e_shnum;
    uint32_t size = 0;
    int i, n_load = 0;
    if (!elf || len < 52) return 0;
    if (memcmp(elf, "\177ELF", 4) != 0) return 0;
    if (elf[4] != 1) return 0;   /* bukan 32-bit */
    if (elf[5] != 1) return 0;   /* bukan little-endian (ELF PS2 = LSB) */
    if (elf[6] != 1) return 0;   /* bukan ELF version 1 */
    if (le16(elf + 16) != 2 && le16(elf + 16) != 3) return 0; /* EXEC/DYN */
    if (le16(elf + 18) != 8) return 0;   /* bukan EM_MIPS */
    e_phoff = le32(elf + 28);
    e_shoff = le32(elf + 32);
    e_phentsize = le16(elf + 42);
    e_phnum = le16(elf + 44);
    e_shentsize = le16(elf + 46);
    e_shnum = le16(elf + 48);
    if (e_phoff < 52 || e_phentsize < 32 || e_phnum == 0 || e_phnum > 64)
        return 0;
    if ((uint64_t)e_phoff + (uint64_t)e_phnum * e_phentsize > len)
        return 0;
    for (i = 0; i < e_phnum; i++) {
        const unsigned char *ph = elf + e_phoff + (size_t)i * e_phentsize;
        uint32_t p_type = le32(ph + 0);
        uint32_t p_offset = le32(ph + 4);
        uint32_t p_filesz = le32(ph + 16);
        uint32_t p_memsz = le32(ph + 20);
        uint64_t end;
        if (p_type != 1) continue;  /* PT_LOAD saja */
        if (p_filesz == 0 || p_filesz > 32 * 1024 * 1024) return 0;
        if (p_memsz < p_filesz) return 0;
        if (p_offset > 32 * 1024 * 1024) return 0;
        end = (uint64_t)p_offset + p_filesz;
        if (end > size) size = (uint32_t)end;
        n_load++;
    }
    if (n_load == 0) return 0;
    if (e_shoff != 0 && e_shentsize >= 40 && e_shnum > 0 && e_shnum < 256) {
        uint64_t send = (uint64_t)e_shoff + (uint64_t)e_shentsize * e_shnum;
        if (send > size && send < 64 * 1024 * 1024) size = (uint32_t)send;
    }
    if (size < 52) return 0;
    *size_out = size;
    return 1;
}
