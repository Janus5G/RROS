#ifndef EXPERIMENTALOS_KERNEL_EOSVM_H
#define EXPERIMENTALOS_KERNEL_EOSVM_H

#include <stddef.h>
#include <stdint.h>

/*
 * EOSVM v1: a small deterministic stack machine used as ExperimentalOS's first
 * language runtime, plus its assembler (the build tool for runtime programs).
 * Programs cannot reach files, devices, the network or the kernel: the only
 * side effect is bounded text output. Execution is fuel-limited.
 */
#define OS_EOSVM_VERSION 1u
#define OS_EOSVM_CODE_MAX 1024u
#define OS_EOSVM_STACK_MAX 64u
#define OS_EOSVM_LOCALS 16u
#define OS_EOSVM_OUTPUT_MAX 1024u
#define OS_EOSVM_FUEL_DEFAULT 100000u
#define OS_EOSVM_LABEL_MAX 32u
#define OS_EOSVM_LABEL_LEN 16u

enum os_eosvm_op {
    OS_EVM_HALT = 0, OS_EVM_PUSH, OS_EVM_POP, OS_EVM_DUP, OS_EVM_SWAP, OS_EVM_ADD, OS_EVM_SUB,
    OS_EVM_MUL, OS_EVM_DIV, OS_EVM_MOD, OS_EVM_LT, OS_EVM_EQ, OS_EVM_JMP, OS_EVM_JZ,
    OS_EVM_LOAD, OS_EVM_STORE, OS_EVM_PRINT, OS_EVM_PUTC, OS_EVM_OP_COUNT
};

enum os_eosvm_status {
    OS_EVM_OK = 0, OS_EVM_ERR_ARGUMENT = -1, OS_EVM_ERR_CODE = -2, OS_EVM_ERR_STACK = -3,
    OS_EVM_ERR_DIVIDE = -4, OS_EVM_ERR_FUEL = -5, OS_EVM_ERR_OUTPUT = -6, OS_EVM_ERR_SYNTAX = -7,
    OS_EVM_ERR_LABEL = -8, OS_EVM_ERR_SIZE = -9
};

typedef struct {
    int status;
    uint32_t steps;
    size_t output_len;
    char output[OS_EOSVM_OUTPUT_MAX + 1u];
} os_eosvm_result;

/* Static verification: opcodes, operand bounds, and jump targets on instruction boundaries. */
int os_eosvm_validate(const uint8_t *code, size_t length);
/* Verifies then runs; never reads or writes outside its own stack, locals and output. */
int os_eosvm_run(const uint8_t *code, size_t length, uint32_t fuel, os_eosvm_result *result);
/* Assembles text into code. On failure *error_line is the 1-based offending line (0 if none). */
int os_eosvm_assemble(const char *source, size_t source_length, uint8_t *code, size_t code_cap,
                      size_t *code_length, unsigned *error_line);

#endif
