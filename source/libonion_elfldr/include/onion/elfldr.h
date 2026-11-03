/* Copyright (C) 2024 John Törnblom / OnionHEN
 *
 * Shared ELF load helpers (inject path, embedded spawn, privilege raise).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Load ELF image into target process; returns entry VA or 0 on failure. */
intptr_t elfldr_load(pid_t pid, uint8_t *elf);

/** Allocate payload_args_t-compatible block in target. Returns VA or 0. */
intptr_t elfldr_payload_args(pid_t pid);

/** Basic ELF bounds/magic validation. Returns 0 when valid. */
int elfldr_sanity_check(uint8_t *elf, size_t elf_size);

/** A buffer for a payload image, mapped so freeing it returns the pages. */
void *elfldr_buf_alloc(size_t size);
void elfldr_buf_free(void *p, size_t size);

/** Read a raw ELF payload from a socket into an elfldr_buf_alloc buffer of
    @elf_size bytes; the caller frees it with elfldr_buf_free. */
int elfldr_read(int fd, uint8_t **elf, size_t *elf_size);

/** Execute an ELF in an already stopped/attached process. */
int elfldr_exec(pid_t pid, int stdio, uint8_t *elf);

/** Spawn a fresh payload process, load @elf into it, and return its pid. */
pid_t elfldr_spawn(int stdio, char *const argv[], uint8_t *elf,
                   size_t elf_size);

/** Set process/thread name through the tracee. */
int elfldr_set_procname(pid_t pid, const char *name);

/**
 * Escape jail and raise privileges for pid.
 * Does not use ptrace — safe for self (getpid()).
 */
int elfldr_raise_privileges(pid_t pid);

#ifdef __cplusplus
}
#endif
