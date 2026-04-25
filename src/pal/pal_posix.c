#include "pal.h"
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void* pal_mmap_alloc(size_t size) {
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

static void pal_mmap_free(void* ptr, size_t size) {
    munmap(ptr, size);
}

static int pal_file_open(const char* path, int mode) {
    int flags = 0;
    if (mode & PAL_O_RDWR)       flags = O_RDWR;
    else if (mode & PAL_O_WRONLY) flags = O_WRONLY;
    else                         flags = O_RDONLY;
    if (mode & PAL_O_CREAT)  flags |= O_CREAT;
    if (mode & PAL_O_APPEND) flags |= O_APPEND;
    return open(path, flags, 0666);
}

static int      pal_file_close(int fd)           { return close(fd); }
static int64_t  pal_file_read(int fd, void* b, uint64_t n) { return read(fd, b, n); }
static int64_t  pal_file_write(int fd, const void* b, uint64_t n) { return write(fd, b, n); }

static int64_t pal_file_seek(int fd, int64_t offset, int whence) {
    int w = (whence == PAL_SEEK_SET) ? SEEK_SET :
            (whence == PAL_SEEK_CUR) ? SEEK_CUR : SEEK_END;
    return lseek(fd, offset, w);
}

static int pal_file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

static void*  pal_dl_open(const char* p)  { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void   pal_dl_close(void* h)        { dlclose(h); }
static void*  pal_dl_sym(void* h, const char* s) { return dlsym(h, s); }
static char*  pal_dl_error(void)          { return dlerror(); }

static int64_t pal_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

static void pal_exit_fn(int code) { exit(code); }
static int  pal_last_error(void) { return errno; }
static char* pal_error_message(int e) { return strerror(e); }

pal_interface* pal_init(void) {
    static pal_interface pal = {
        .mmap_alloc     = pal_mmap_alloc,
        .mmap_free      = pal_mmap_free,
        .file_open      = pal_file_open,
        .file_close     = pal_file_close,
        .file_read      = pal_file_read,
        .file_write     = pal_file_write,
        .file_seek      = pal_file_seek,
        .file_exists    = pal_file_exists,
        .dl_open        = pal_dl_open,
        .dl_close       = pal_dl_close,
        .dl_sym         = pal_dl_sym,
        .dl_error       = pal_dl_error,
        .current_time_ms = pal_current_time_ms,
        .exit_fn        = pal_exit_fn,
        .last_error     = pal_last_error,
        .error_message  = pal_error_message,
        .state          = NULL,
    };
    return &pal;
}
