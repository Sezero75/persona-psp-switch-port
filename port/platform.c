#include <switch.h>
#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "nk_platform.h"
#include "gpu_sdl3vk/ge_gpu.h"

#define GAME_ROOT "sdmc:/switch/persona-runtime"

uint64_t SDL_GetTicksNS(void) {
    return armTicksToNs(armGetSystemTick());
}

/* The software GE writes guest VRAM directly. The presenter never owns a GPU render target. */
int gegpu_sync_guest_fb(const GeGpuFbDescriptor *descriptor) {
    (void)descriptor;
    return GEGPU_SYNC_NO_TARGET;
}
void gegpu_note_guest_flip(void) {}

int nk_fseek64(FILE *file, int64_t offset, int whence) { return fseeko(file, offset, whence); }
int64_t nk_ftell64(FILE *file) { return ftello(file); }
int nk_remove_utf8(const char *path) { return unlink(path); }
int nk_rename_utf8(const char *from, const char *to) { return rename(from, to); }
char nk_platform_path_separator(void) { return '/'; }

int64_t nk_platform_get_file_size(const char *path) {
    struct stat info;
    if (!path || stat(path, &info) != 0 || !S_ISREG(info.st_mode)) return -1;
    return info.st_size;
}

bool nk_platform_file_exists(const char *path) {
    return nk_platform_get_file_size(path) >= 0;
}

static bool ensure_directory(const char *path) {
    struct stat info;
    if (mkdir(path, 0700) == 0) return true;
    return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

bool nk_platform_mkdir_p_private(const char *path) {
    char buffer[1024];
    if (!path || !*path || strlen(path) >= sizeof(buffer)) return false;
    strcpy(buffer, path);
    char *start = strstr(buffer, ":/");
    start = start ? start + 2 : buffer;
    for (char *end = start; ; ++end) {
        if (*end != '/' && *end != '\0') continue;
        char separator = *end;
        *end = '\0';
        if (*start && !ensure_directory(buffer)) return false;
        *end = separator;
        if (!separator) return true;
    }
}

FILE *nk_platform_fopen_private(const char *path, const char *mode) {
    return fopen(path, mode);
}

static bool copy_path(const char *source, char *out, size_t capacity) {
    size_t length = strlen(source);
    if (!out || capacity <= length) return false;
    memcpy(out, source, length + 1);
    return true;
}

bool nk_platform_get_path(NkPathType type, char *out, size_t capacity) {
    static const char *paths[] = {
        [NK_PATH_CONFIG] = GAME_ROOT "/config",
        [NK_PATH_DATA] = GAME_ROOT,
        [NK_PATH_CACHE] = GAME_ROOT "/cache",
        [NK_PATH_LOGS] = GAME_ROOT "/logs",
        [NK_PATH_SAVES] = GAME_ROOT "/memstick/PSP/SAVEDATA",
    };
    if ((unsigned)type >= sizeof(paths)/sizeof(*paths)) return false;
    return nk_platform_mkdir_p_private(paths[type]) && copy_path(paths[type], out, capacity);
}

bool nk_platform_resolve_app_data_dir(char *out, size_t capacity) {
    return copy_path(GAME_ROOT, out, capacity);
}

bool nk_platform_absolute_path(const char *path, char *out, size_t capacity) {
    if (!path || !*path || !out || !capacity) return false;
    if (strstr(path, ":/") || path[0] == '/') return copy_path(path, out, capacity);
    char current[1024];
    if (!getcwd(current, sizeof(current))) return false;
    int length = snprintf(out, capacity, "%s/%s", current, path);
    return length >= 0 && (size_t)length < capacity;
}

/* The bundled ATRAC decoder requires POSIX alignment semantics on newlib. */
int posix_memalign(void **out, size_t alignment, size_t size) {
    if (!out || !alignment || (alignment & (alignment-1)) || alignment % sizeof(void *))
        return EINVAL;
    void *memory = memalign(alignment, size);
    if (!memory) return ENOMEM;
    *out = memory;
    return 0;
}
