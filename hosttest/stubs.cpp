// Host-build stubs for pico-infonesPlus. Implements the small surface of
// platform symbols the InfoNES core links against — Frens helpers, FatFs
// I/O (backed by host stdio), and the audio/messagebox callbacks.
//
// FatFs paths like "/bios/fds-bios.rom" are resolved against the directory
// in the NES_FAT_ROOT environment variable (default ".") so a real BIOS
// file can be dropped next to the harness.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string>

#include "InfoNES_Types.h"
#include "InfoNES_System.h"
#include "FrensHelpers.h"
#include "ff.h"
#include "settings.h"

// --- Settings instance referenced by FDS_AutoInsertEnabled --------------
HostSettings settings = { { /*autoSwapFDS=*/ 1, /*autoInsertDiskA=*/ 1 } };

// --- Frens helpers (only the subset the core links) ---------------------
namespace Frens
{
    // Full 320x240 framebuffer in the same layout the device uses on
    // RP2350. host_main.cpp routes each scanline directly into this
    // buffer via InfoNES_SetLineBuffer().
    WORD framebuffer[SCREENWIDTH * SCREENHEIGHT];

    void *f_malloc(size_t n) { return malloc(n); }
    void  f_free(void *p)    { free(p); }

    // NES_NO_PSRAM=1 runs FDS the way a board without PSRAM does.
    bool isPsramEnabled()     { return getenv("NES_NO_PSRAM") == nullptr; }
    bool isFrameBufferUsed()  { return true; }
    uint GetAvailableMemory() { return 8u * 1024u * 1024u; }

    void getextensionfromfilename(const char *filename, char *extension, size_t extSize)
    {
        if (extSize == 0) return;
        const char *dot = strrchr(filename, '.');
        if (!dot) { extension[0] = 0; return; }
        size_t i = 0;
        const char *p = dot + 1;
        while (*p && i + 1 < extSize) {
            extension[i++] = (char)tolower((unsigned char)*p);
            ++p;
        }
        extension[i] = 0;
    }
}

// --- Host-stdio-backed FatFs --------------------------------------------
// Resolve FatFs paths against $NES_FAT_ROOT (default ".") so paths like
// "/bios/fds-bios.rom" map to "./bios/fds-bios.rom" on disk.
static std::string host_path(const char *fat_path)
{
    const char *root = getenv("NES_FAT_ROOT");
    std::string p = root && *root ? root : ".";
    if (!fat_path || !*fat_path) return p;
    if (fat_path[0] != '/') p += "/";
    p += fat_path;
    return p;
}

extern "C" FRESULT f_stat(const char *path, FILINFO *fno)
{
    struct stat st;
    if (stat(host_path(path).c_str(), &st) != 0) return FR_NO_FILE;
    if (fno) {
        fno->fsize = (FSIZE_t)st.st_size;
        // copy basename
        const char *slash = strrchr(path, '/');
        const char *base  = slash ? slash + 1 : path;
        strncpy(fno->fname, base, sizeof(fno->fname) - 1);
        fno->fname[sizeof(fno->fname) - 1] = 0;
    }
    return FR_OK;
}

extern "C" FRESULT f_open(FIL *fp, const char *path, uint8_t mode)
{
    if (!fp) return FR_INVALID_NAME;
    const char *fmode;
    if (mode & FA_CREATE_ALWAYS)      fmode = "wb";
    else if (mode & FA_WRITE)         fmode = "rb+";
    else                              fmode = "rb";

    std::string hp = host_path(path);
    FILE *f = fopen(hp.c_str(), fmode);
    if (!f && (mode & FA_OPEN_ALWAYS)) f = fopen(hp.c_str(), "wb+");
    if (!f) return FR_NO_FILE;

    fp->fp = f;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        fp->fsize = sz > 0 ? (FSIZE_t)sz : 0;
        fseek(f, 0, SEEK_SET);
    } else {
        fp->fsize = 0;
    }
    return FR_OK;
}

extern "C" FRESULT f_read(FIL *fp, void *buf, UINT btr, UINT *br)
{
    if (!fp || !fp->fp) { if (br) *br = 0; return FR_DISK_ERR; }
    size_t n = fread(buf, 1, btr, (FILE *)fp->fp);
    if (br) *br = (UINT)n;
    return FR_OK;
}

extern "C" FRESULT f_write(FIL *fp, const void *buf, UINT btw, UINT *bw)
{
    if (!fp || !fp->fp) { if (bw) *bw = 0; return FR_DISK_ERR; }
    size_t n = fwrite(buf, 1, btw, (FILE *)fp->fp);
    if (bw) *bw = (UINT)n;
    return FR_OK;
}

extern "C" FRESULT f_close(FIL *fp)
{
    if (!fp || !fp->fp) return FR_OK;
    fclose((FILE *)fp->fp);
    fp->fp = nullptr;
    return FR_OK;
}

extern "C" FRESULT f_unlink(const char *path)
{
    return remove(host_path(path).c_str()) == 0 ? FR_OK : FR_NO_FILE;
}

extern "C" FRESULT f_mkdir(const char *path)
{
    if (mkdir(host_path(path).c_str(), 0755) == 0 || errno == EEXIST) return FR_OK;
    return FR_DENIED;
}

// --- Audio callback no-ops ----------------------------------------------
void InfoNES_SoundInit(void) {}
int  InfoNES_SoundOpen(int /*samples_per_sync*/, int /*sample_rate*/) { return 0; }
void InfoNES_SoundClose(void) {}
int  InfoNES_GetSoundBufferSize() { return 0; }
void InfoNES_SoundOutput(int /*samples*/,
                         BYTE * /*w1*/, BYTE * /*w2*/, BYTE * /*w3*/,
                         BYTE * /*w4*/, BYTE * /*w5*/, BYTE * /*w6*/) {}

// --- Messaging ----------------------------------------------------------
void InfoNES_DebugPrint(const char *msg) { fputs(msg, stdout); fputc('\n', stdout); }

void InfoNES_MessageBox(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

void InfoNES_Error(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fputs("InfoNES_Error: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
