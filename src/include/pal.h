#ifndef SCHEME_PAL_H
#define SCHEME_PAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    void*  (*mmap_alloc)(size_t size);
    void   (*mmap_free)(void* ptr, size_t size);

    int    (*file_open)(const char* path, int mode);
    int    (*file_close)(int fd);
    int64_t (*file_read)(int fd, void* buf, uint64_t nbytes);
    int64_t (*file_write)(int fd, const void* buf, uint64_t nbytes);
    int64_t (*file_seek)(int fd, int64_t offset, int whence);
    int    (*file_exists)(const char* path);

    void*  (*dl_open)(const char* path);
    void   (*dl_close)(void* handle);
    void*  (*dl_sym)(void* handle, const char* symbol);
    char*  (*dl_error)(void);

    int64_t (*current_time_ms)(void);
    void   (*exit_fn)(int code);

    int    (*last_error)(void);
    char*  (*error_message)(int err);

    void*  state;
} pal_interface;

#define PAL_O_RDONLY 0
#define PAL_O_WRONLY 1
#define PAL_O_RDWR   2
#define PAL_O_CREAT  4
#define PAL_O_APPEND 8

#define PAL_SEEK_SET 0
#define PAL_SEEK_CUR 1
#define PAL_SEEK_END 2

pal_interface* pal_init(void);

#endif
