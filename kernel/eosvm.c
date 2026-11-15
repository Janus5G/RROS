#include "eosvm.h"

static const struct { const char *name; int op; int operand; } mnemonics[] = {
    {"halt", OS_EVM_HALT, 0}, {"push", OS_EVM_PUSH, 4}, {"pop", OS_EVM_POP, 0},
    {"dup", OS_EVM_DUP, 0}, {"swap", OS_EVM_SWAP, 0}, {"add", OS_EVM_ADD, 0},
    {"sub", OS_EVM_SUB, 0}, {"mul", OS_EVM_MUL, 0}, {"div", OS_EVM_DIV, 0},
    {"mod", OS_EVM_MOD, 0}, {"lt", OS_EVM_LT, 0}, {"eq", OS_EVM_EQ, 0},
    {"jmp", OS_EVM_JMP, 2}, {"jz", OS_EVM_JZ, 2}, {"load", OS_EVM_LOAD, 1},
    {"store", OS_EVM_STORE, 1}, {"print", OS_EVM_PRINT, 0}, {"putc", OS_EVM_PUTC, 0},
};

static int operand_size(int op)
{
    for (size_t i = 0; i < sizeof mnemonics / sizeof mnemonics[0]; i++)
        if (mnemonics[i].op == op) return mnemonics[i].operand;
    return -1;
}

int os_eosvm_validate(const uint8_t *code, size_t length)
{
    static uint8_t boundary[OS_EOSVM_CODE_MAX + 1u];
    if (!code || length == 0 || length > OS_EOSVM_CODE_MAX) return OS_EVM_ERR_CODE;
    for (size_t i = 0; i <= length; i++) boundary[i] = 0;
    for (size_t pc = 0; pc < length;) {
        int size = operand_size(code[pc]);
        if (size < 0 || pc + 1u + (size_t)size > length) return OS_EVM_ERR_CODE;
        boundary[pc] = 1;
        if (code[pc] == OS_EVM_LOAD || code[pc] == OS_EVM_STORE) {
            if (code[pc + 1u] >= OS_EOSVM_LOCALS) return OS_EVM_ERR_CODE;
        }
        pc += 1u + (size_t)size;
    }
    for (size_t pc = 0; pc < length;) {
        int size = operand_size(code[pc]);
        if (code[pc] == OS_EVM_JMP || code[pc] == OS_EVM_JZ) {
            size_t target = (size_t)code[pc + 1u] | ((size_t)code[pc + 2u] << 8);
            if (target >= length || !boundary[target]) return OS_EVM_ERR_CODE;
        }
        pc += 1u + (size_t)size;
    }
    return OS_EVM_OK;
}

typedef struct { os_eosvm_result *r; } out_ctx;

static int out_char(os_eosvm_result *r, char c)
{
    if (r->output_len >= OS_EOSVM_OUTPUT_MAX) return 0;
    r->output[r->output_len++] = c;
    r->output[r->output_len] = 0;
    return 1;
}

static int out_int(os_eosvm_result *r, int32_t v)
{
    char tmp[12];
    int n = 0;
    uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    if (v < 0 && !out_char(r, '-')) return 0;
    do { tmp[n++] = (char)('0' + (u % 10u)); u /= 10u; } while (u);
    while (n) if (!out_char(r, tmp[--n])) return 0;
    return out_char(r, '\n');
}

int os_eosvm_run(const uint8_t *code, size_t length, uint32_t fuel, os_eosvm_result *result)
{
    int32_t stack[OS_EOSVM_STACK_MAX];
    int32_t locals[OS_EOSVM_LOCALS];
    size_t sp = 0, pc = 0;
    int status;
    if (!result) return OS_EVM_ERR_ARGUMENT;
    result->status = OS_EVM_OK; result->steps = 0; result->output_len = 0; result->output[0] = 0;
    for (size_t i = 0; i < OS_EOSVM_LOCALS; i++) locals[i] = 0;
    if (fuel == 0) { result->status = OS_EVM_ERR_ARGUMENT; return result->status; }
    status = os_eosvm_validate(code, length);
    if (status) { result->status = status; return status; }
#define FAIL(s) do { result->status = (s); return (s); } while (0)
#define NEED(n) do { if (sp < (n)) FAIL(OS_EVM_ERR_STACK); } while (0)
#define ROOM() do { if (sp >= OS_EOSVM_STACK_MAX) FAIL(OS_EVM_ERR_STACK); } while (0)
    for (;;) {
        uint8_t op;
        if (pc >= length) FAIL(OS_EVM_ERR_CODE);
        if (result->steps >= fuel) FAIL(OS_EVM_ERR_FUEL);
        result->steps++;
        op = code[pc];
        switch (op) {
        case OS_EVM_HALT: return OS_EVM_OK;
        case OS_EVM_PUSH:
            ROOM();
            stack[sp++] = (int32_t)((uint32_t)code[pc + 1u] | ((uint32_t)code[pc + 2u] << 8) |
                                    ((uint32_t)code[pc + 3u] << 16) | ((uint32_t)code[pc + 4u] << 24));
            pc += 5u; break;
        case OS_EVM_POP: NEED(1); sp--; pc++; break;
        case OS_EVM_DUP: NEED(1); ROOM(); stack[sp] = stack[sp - 1u]; sp++; pc++; break;
        case OS_EVM_SWAP: { NEED(2); int32_t t = stack[sp - 1u]; stack[sp - 1u] = stack[sp - 2u]; stack[sp - 2u] = t; pc++; break; }
        case OS_EVM_ADD: NEED(2); stack[sp - 2u] = (int32_t)((uint32_t)stack[sp - 2u] + (uint32_t)stack[sp - 1u]); sp--; pc++; break;
        case OS_EVM_SUB: NEED(2); stack[sp - 2u] = (int32_t)((uint32_t)stack[sp - 2u] - (uint32_t)stack[sp - 1u]); sp--; pc++; break;
        case OS_EVM_MUL: NEED(2); stack[sp - 2u] = (int32_t)((uint32_t)stack[sp - 2u] * (uint32_t)stack[sp - 1u]); sp--; pc++; break;
        case OS_EVM_DIV:
        case OS_EVM_MOD:
            NEED(2);
            if (stack[sp - 1u] == 0 || (stack[sp - 2u] == INT32_MIN && stack[sp - 1u] == -1)) FAIL(OS_EVM_ERR_DIVIDE);
            stack[sp - 2u] = op == OS_EVM_DIV ? stack[sp - 2u] / stack[sp - 1u] : stack[sp - 2u] % stack[sp - 1u];
            sp--; pc++; break;
        case OS_EVM_LT: NEED(2); stack[sp - 2u] = stack[sp - 2u] < stack[sp - 1u]; sp--; pc++; break;
        case OS_EVM_EQ: NEED(2); stack[sp - 2u] = stack[sp - 2u] == stack[sp - 1u]; sp--; pc++; break;
        case OS_EVM_JMP: pc = (size_t)code[pc + 1u] | ((size_t)code[pc + 2u] << 8); break;
        case OS_EVM_JZ:
            NEED(1);
            if (stack[--sp] == 0) pc = (size_t)code[pc + 1u] | ((size_t)code[pc + 2u] << 8);
            else pc += 3u;
            break;
        case OS_EVM_LOAD: ROOM(); stack[sp++] = locals[code[pc + 1u]]; pc += 2u; break;
        case OS_EVM_STORE: NEED(1); locals[code[pc + 1u]] = stack[--sp]; pc += 2u; break;
        case OS_EVM_PRINT: NEED(1); if (!out_int(result, stack[--sp])) FAIL(OS_EVM_ERR_OUTPUT); pc++; break;
        case OS_EVM_PUTC:
            NEED(1);
            if (!out_char(result, (char)(stack[--sp] & 0x7f))) FAIL(OS_EVM_ERR_OUTPUT);
            pc++; break;
        default: FAIL(OS_EVM_ERR_CODE);
        }
    }
#undef FAIL
#undef NEED
#undef ROOM
}

typedef struct { char name[OS_EOSVM_LABEL_LEN]; size_t address; } label_t;
typedef struct { size_t patch; char name[OS_EOSVM_LABEL_LEN]; unsigned line; } fixup_t;

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static int is_ident(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int parse_int(const char *s, size_t n, int64_t *out)
{
    size_t i = 0;
    int neg = 0;
    int64_t v = 0;
    if (i < n && s[i] == '-') { neg = 1; i++; }
    if (i >= n) return 0;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (s[i] - '0');
        if (v > 2147483648LL) return 0;
    }
    if (neg) v = -v;
    if (v < INT32_MIN || v > INT32_MAX) return 0;
    *out = v;
    return 1;
}

int os_eosvm_assemble(const char *source, size_t source_length, uint8_t *code, size_t code_cap,
                      size_t *code_length, unsigned *error_line)
{
    label_t labels[OS_EOSVM_LABEL_MAX];
    fixup_t fixups[OS_EOSVM_LABEL_MAX * 4u];
    size_t label_count = 0, fixup_count = 0, len = 0, pos = 0;
    unsigned line = 0;
    if (error_line) *error_line = 0;
    if (!source || !code || !code_length || code_cap == 0) return OS_EVM_ERR_ARGUMENT;
    *code_length = 0;
#define ERR(s) do { if (error_line) *error_line = line; return (s); } while (0)
    while (pos <= source_length) {
        size_t start = pos, end, i;
        char word[OS_EOSVM_LABEL_LEN + 1u];
        size_t wl = 0;
        while (pos < source_length && source[pos] != '\n') pos++;
        end = pos;
        pos++;
        line++;
        for (i = start; i < end; i++) if (source[i] == ';') { end = i; break; }
        for (i = start; i < end; i++) if ((unsigned char)source[i] >= 0x80 || (source[i] != '\t' && source[i] != '\r' && (unsigned char)source[i] < 0x20)) ERR(OS_EVM_ERR_SYNTAX);
        i = start;
        while (i < end && is_space(source[i])) i++;
        if (i >= end) continue;
        while (i < end && is_ident(source[i])) {
            if (wl >= OS_EOSVM_LABEL_LEN - 1u) ERR(OS_EVM_ERR_SYNTAX);
            word[wl++] = source[i++];
        }
        word[wl] = 0;
        if (wl == 0) ERR(OS_EVM_ERR_SYNTAX);
        if (i < end && source[i] == ':') {
            size_t k;
            for (k = 0; k < label_count; k++) {
                size_t j = 0;
                while (labels[k].name[j] && labels[k].name[j] == word[j]) j++;
                if (labels[k].name[j] == word[j]) ERR(OS_EVM_ERR_LABEL);
            }
            if (label_count >= OS_EOSVM_LABEL_MAX) ERR(OS_EVM_ERR_LABEL);
            for (k = 0; k <= wl; k++) labels[label_count].name[k] = word[k];
            labels[label_count++].address = len;
            i++;
            while (i < end && is_space(source[i])) i++;
            if (i >= end) continue;
            wl = 0;
            while (i < end && is_ident(source[i])) {
                if (wl >= OS_EOSVM_LABEL_LEN - 1u) ERR(OS_EVM_ERR_SYNTAX);
                word[wl++] = source[i++];
            }
            word[wl] = 0;
            if (wl == 0) ERR(OS_EVM_ERR_SYNTAX);
        }
        {
            int op = -1, operand = 0;
            size_t m, aw = 0;
            char arg[OS_EOSVM_LABEL_LEN + 12u];
            for (m = 0; m < sizeof mnemonics / sizeof mnemonics[0]; m++) {
                size_t j = 0;
                while (mnemonics[m].name[j] && mnemonics[m].name[j] == lower(word[j])) j++;
                if (!mnemonics[m].name[j] && !word[j]) { op = mnemonics[m].op; operand = mnemonics[m].operand; break; }
            }
            if (op < 0) ERR(OS_EVM_ERR_SYNTAX);
            while (i < end && is_space(source[i])) i++;
            while (i < end && !is_space(source[i])) {
                if (aw >= sizeof arg - 1u) ERR(OS_EVM_ERR_SYNTAX);
                arg[aw++] = source[i++];
            }
            arg[aw] = 0;
            while (i < end && is_space(source[i])) i++;
            if (i < end) ERR(OS_EVM_ERR_SYNTAX);
            if ((operand == 0) != (aw == 0)) ERR(OS_EVM_ERR_SYNTAX);
            if (len + 1u + (size_t)operand > code_cap || len + 1u + (size_t)operand > OS_EOSVM_CODE_MAX) ERR(OS_EVM_ERR_SIZE);
            code[len++] = (uint8_t)op;
            if (operand == 4) {
                int64_t v;
                if (!parse_int(arg, aw, &v)) ERR(OS_EVM_ERR_SYNTAX);
                for (int b = 0; b < 4; b++) code[len++] = (uint8_t)(((uint32_t)v >> (8 * b)) & 0xffu);
            } else if (operand == 1) {
                int64_t v;
                if (!parse_int(arg, aw, &v) || v < 0 || v >= (int64_t)OS_EOSVM_LOCALS) ERR(OS_EVM_ERR_SYNTAX);
                code[len++] = (uint8_t)v;
            } else if (operand == 2) {
                if (aw >= OS_EOSVM_LABEL_LEN || fixup_count >= sizeof fixups / sizeof fixups[0]) ERR(OS_EVM_ERR_LABEL);
                for (size_t k = 0; k < aw; k++) if (!is_ident(arg[k])) ERR(OS_EVM_ERR_SYNTAX);
                for (size_t k = 0; k <= aw; k++) fixups[fixup_count].name[k] = arg[k];
                fixups[fixup_count].patch = len;
                fixups[fixup_count++].line = line;
                code[len++] = 0; code[len++] = 0;
            }
        }
    }
    for (size_t f = 0; f < fixup_count; f++) {
        int found = 0;
        for (size_t k = 0; k < label_count && !found; k++) {
            size_t j = 0;
            while (labels[k].name[j] && labels[k].name[j] == fixups[f].name[j]) j++;
            if (labels[k].name[j] == fixups[f].name[j]) {
                code[fixups[f].patch] = (uint8_t)(labels[k].address & 0xffu);
                code[fixups[f].patch + 1u] = (uint8_t)(labels[k].address >> 8);
                found = 1;
            }
        }
        if (!found) { line = fixups[f].line; ERR(OS_EVM_ERR_LABEL); }
    }
#undef ERR
    if (len == 0) return OS_EVM_ERR_SYNTAX;
    {
        int v = os_eosvm_validate(code, len);
        if (v) return v;
    }
    *code_length = len;
    return OS_EVM_OK;
}
