#ifndef EXPERIMENTALOS_KERNEL_BROWSER_JS_H
#define EXPERIMENTALOS_KERNEL_BROWSER_JS_H

#include <stddef.h>
#include <stdint.h>

/*
 * ESJS v1: a deliberately small, sandboxed JavaScript subset for the browser.
 * Supported: var/let/assignment, int32 numbers, strings (+ concatenation), booleans, if/else, while,
 * blocks, comments, print(x) and document.write(x). Not supported (compatibility limit): functions,
 * objects/arrays, floating point, the DOM, timers, network, eval, modules. Execution is fuel-limited
 * with bounded variables, string pool, nesting depth and output; any unsupported construct is a
 * syntax error and nothing outside the interpreter state is reachable.
 */
#define OS_JS_FUEL_DEFAULT 20000u
#define OS_JS_OUTPUT_MAX 512u
#define OS_JS_VARS 12u
#define OS_JS_NAME_MAX 16u
#define OS_JS_POOL 1024u
#define OS_JS_DEPTH 12u

enum os_js_status {
    OS_JS_OK = 0, OS_JS_ERR_ARGUMENT = -1, OS_JS_ERR_SYNTAX = -2, OS_JS_ERR_FUEL = -3, OS_JS_ERR_OUTPUT = -4,
    OS_JS_ERR_LIMIT = -5, OS_JS_ERR_RUNTIME = -6
};

typedef struct {
    int status;
    uint32_t steps;
    size_t output_len;
    char output[OS_JS_OUTPUT_MAX + 1u];
    char error[40];
} os_js_result;

int os_js_run(const char *source, size_t length, uint32_t fuel, os_js_result *result);

#endif
