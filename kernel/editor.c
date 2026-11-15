#include "editor.h"

static int char_ok(char c)
{
    return c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7e);
}

static void journal(os_editor *ed, int insert, char ch, size_t pos)
{
    if (ed->undo_count == OS_EDITOR_UNDO_MAX) {
        for (size_t i = 1; i < OS_EDITOR_UNDO_MAX; i++) ed->undo[i - 1u] = ed->undo[i];
        ed->undo_count--;
    }
    ed->undo[ed->undo_count].insert = (uint8_t)insert;
    ed->undo[ed->undo_count].ch = (uint8_t)ch;
    ed->undo[ed->undo_count].pos = (uint16_t)pos;
    ed->undo_count++;
}

static void raw_insert(os_editor *ed, size_t pos, char c)
{
    for (size_t i = ed->length; i > pos; i--) ed->text[i] = ed->text[i - 1u];
    ed->text[pos] = c;
    ed->length++;
}

static char raw_remove(os_editor *ed, size_t pos)
{
    char c = ed->text[pos];
    for (size_t i = pos; i + 1u < ed->length; i++) ed->text[i] = ed->text[i + 1u];
    ed->length--;
    return c;
}

void os_editor_init(os_editor *ed)
{
    if (!ed) return;
    ed->length = 0; ed->cursor = 0; ed->dirty = 0; ed->undo_count = 0;
    ed->text[0] = 0;
}

int os_editor_insert(os_editor *ed, const char *text, size_t length)
{
    if (!ed || (!text && length)) return OS_ED_ERR_ARGUMENT;
    if (length > OS_EDITOR_CAPACITY || ed->length + length > OS_EDITOR_CAPACITY - 1u) return OS_ED_ERR_FULL;
    for (size_t i = 0; i < length; i++) if (!char_ok(text[i])) return OS_ED_ERR_CHARACTER;
    for (size_t i = 0; i < length; i++) {
        raw_insert(ed, ed->cursor, text[i]);
        journal(ed, 1, text[i], ed->cursor);
        ed->cursor++;
    }
    ed->text[ed->length] = 0;
    if (length) ed->dirty = 1;
    return OS_ED_OK;
}

int os_editor_backspace(os_editor *ed)
{
    char c;
    if (!ed) return OS_ED_ERR_ARGUMENT;
    if (ed->cursor == 0) return OS_ED_ERR_BOUNDS;
    ed->cursor--;
    c = raw_remove(ed, ed->cursor);
    ed->text[ed->length] = 0;
    journal(ed, 0, c, ed->cursor);
    ed->dirty = 1;
    return OS_ED_OK;
}

int os_editor_delete(os_editor *ed)
{
    char c;
    if (!ed) return OS_ED_ERR_ARGUMENT;
    if (ed->cursor >= ed->length) return OS_ED_ERR_BOUNDS;
    c = raw_remove(ed, ed->cursor);
    ed->text[ed->length] = 0;
    journal(ed, 0, c, ed->cursor);
    ed->dirty = 1;
    return OS_ED_OK;
}

int os_editor_move(os_editor *ed, int delta)
{
    if (!ed) return OS_ED_ERR_ARGUMENT;
    if (delta < 0 ? (size_t)(-(long)delta) > ed->cursor : (size_t)delta > ed->length - ed->cursor) return OS_ED_ERR_BOUNDS;
    ed->cursor = (size_t)((long)ed->cursor + delta);
    return OS_ED_OK;
}

static size_t line_start(const os_editor *ed, size_t pos)
{
    while (pos > 0 && ed->text[pos - 1u] != '\n') pos--;
    return pos;
}

static size_t line_end(const os_editor *ed, size_t pos)
{
    while (pos < ed->length && ed->text[pos] != '\n') pos++;
    return pos;
}

int os_editor_home(os_editor *ed) { if (!ed) return OS_ED_ERR_ARGUMENT; ed->cursor = line_start(ed, ed->cursor); return OS_ED_OK; }
int os_editor_end(os_editor *ed) { if (!ed) return OS_ED_ERR_ARGUMENT; ed->cursor = line_end(ed, ed->cursor); return OS_ED_OK; }

int os_editor_move_line(os_editor *ed, int delta)
{
    size_t col, pos;
    if (!ed) return OS_ED_ERR_ARGUMENT;
    pos = ed->cursor;
    col = pos - line_start(ed, pos);
    while (delta > 0) {
        size_t e = line_end(ed, pos);
        if (e >= ed->length) return OS_ED_ERR_BOUNDS;
        pos = e + 1u;
        delta--;
    }
    while (delta < 0) {
        size_t s = line_start(ed, pos);
        if (s == 0) return OS_ED_ERR_BOUNDS;
        pos = line_start(ed, s - 1u);
        delta++;
    }
    {
        size_t s = line_start(ed, pos), e = line_end(ed, pos);
        ed->cursor = s + col > e ? e : s + col;
    }
    return OS_ED_OK;
}

int os_editor_undo(os_editor *ed)
{
    os_editor_edit e;
    if (!ed) return OS_ED_ERR_ARGUMENT;
    if (ed->undo_count == 0) return OS_ED_ERR_EMPTY;
    e = ed->undo[--ed->undo_count];
    if (e.insert) {
        raw_remove(ed, e.pos);
        ed->cursor = e.pos;
    } else {
        raw_insert(ed, e.pos, (char)e.ch);
        ed->cursor = (size_t)e.pos + 1u;
    }
    ed->text[ed->length] = 0;
    ed->dirty = 1;
    return OS_ED_OK;
}

int os_editor_find(os_editor *ed, const char *needle)
{
    size_t n = 0;
    if (!ed || !needle) return OS_ED_ERR_ARGUMENT;
    while (needle[n]) n++;
    if (n == 0) return OS_ED_ERR_ARGUMENT;
    for (size_t i = ed->cursor; i + n <= ed->length; i++) {
        size_t j = 0;
        while (j < n && ed->text[i + j] == needle[j]) j++;
        if (j == n) { ed->cursor = i; return OS_ED_OK; }
    }
    return OS_ED_ERR_NOT_FOUND;
}

size_t os_editor_line_count(const os_editor *ed)
{
    size_t n = 1;
    if (!ed) return 0;
    for (size_t i = 0; i < ed->length; i++) if (ed->text[i] == '\n') n++;
    return n;
}

void os_editor_position(const os_editor *ed, size_t *line, size_t *column)
{
    size_t l = 1;
    if (!ed) return;
    for (size_t i = 0; i < ed->cursor; i++) if (ed->text[i] == '\n') l++;
    if (line) *line = l;
    if (column) *column = ed->cursor - line_start(ed, ed->cursor) + 1u;
}

int os_editor_save(os_editor *ed, os_storage_fs *fs, const char *path)
{
    if (!ed || !fs || !path) return OS_ED_ERR_ARGUMENT;
    if (!os_storage_write(fs, path, ed->text, ed->length)) return OS_ED_ERR_STORAGE;
    ed->dirty = 0;
    return OS_ED_OK;
}

int os_editor_load(os_editor *ed, os_storage_fs *fs, const char *path)
{
    usize size = 0, got = 0;
    static char staging[OS_EDITOR_CAPACITY];
    if (!ed || !fs || !path) return OS_ED_ERR_ARGUMENT;
    if (!os_storage_exists(fs, path, &size)) return OS_ED_ERR_STORAGE;
    if (size > OS_EDITOR_CAPACITY - 1u) return OS_ED_ERR_FULL;
    if (size && !os_storage_read(fs, path, 0, staging, size, &got)) return OS_ED_ERR_STORAGE;
    if (got != size) return OS_ED_ERR_STORAGE;
    for (usize i = 0; i < size; i++) if (!char_ok(staging[i])) return OS_ED_ERR_CHARACTER;
    os_editor_init(ed);
    for (usize i = 0; i < size; i++) ed->text[i] = staging[i];
    ed->length = size;
    ed->text[size] = 0;
    return OS_ED_OK;
}
