#ifndef EXPERIMENTALOS_KERNEL_EDITOR_H
#define EXPERIMENTALOS_KERNEL_EDITOR_H

#include <stddef.h>
#include <stdint.h>
#include "storage.h"

/* Native text editor core: bounded buffer, cursor, undo journal, search, and file persistence. */
#define OS_EDITOR_CAPACITY 4096u
#define OS_EDITOR_UNDO_MAX 128u

enum os_editor_status {
    OS_ED_OK = 0, OS_ED_ERR_ARGUMENT = -1, OS_ED_ERR_FULL = -2, OS_ED_ERR_BOUNDS = -3,
    OS_ED_ERR_CHARACTER = -4, OS_ED_ERR_STORAGE = -5, OS_ED_ERR_EMPTY = -6, OS_ED_ERR_NOT_FOUND = -7
};

typedef struct { uint8_t insert; uint8_t ch; uint16_t pos; } os_editor_edit;

typedef struct {
    char text[OS_EDITOR_CAPACITY];
    size_t length;
    size_t cursor;
    int dirty;
    os_editor_edit undo[OS_EDITOR_UNDO_MAX];
    size_t undo_count;
} os_editor;

void os_editor_init(os_editor *ed);
/* Only printable ASCII, tab and newline are accepted; anything else is rejected atomically. */
int os_editor_insert(os_editor *ed, const char *text, size_t length);
int os_editor_backspace(os_editor *ed);
int os_editor_delete(os_editor *ed);
int os_editor_move(os_editor *ed, int delta);
int os_editor_move_line(os_editor *ed, int delta);
int os_editor_home(os_editor *ed);
int os_editor_end(os_editor *ed);
int os_editor_undo(os_editor *ed);
/* Moves the cursor to the next occurrence at or after the cursor. */
int os_editor_find(os_editor *ed, const char *needle);
size_t os_editor_line_count(const os_editor *ed);
void os_editor_position(const os_editor *ed, size_t *line, size_t *column);
int os_editor_save(os_editor *ed, os_storage_fs *fs, const char *path);
int os_editor_load(os_editor *ed, os_storage_fs *fs, const char *path);

#endif
