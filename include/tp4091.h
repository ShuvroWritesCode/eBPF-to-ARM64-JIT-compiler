#ifndef TP4091_H
#define TP4091_H

#include "jit.h"
#include <stdint.h>

// TP4091 Command Types
#define TP4091_CMD_LOAD 0x01
#define TP4091_CMD_ACTIVATE 0x02
#define TP4091_CMD_EXECUTE 0x03
#define TP4091_CMD_UNLOAD 0x04

// Maximum number of programs
#define MAX_PROGRAMS 16

// Program states
#define PROGRAM_STATE_EMPTY 0
#define PROGRAM_STATE_LOADED 1
#define PROGRAM_STATE_ACTIVE 2

// Program entry
typedef struct {
  uint32_t program_id;
  uint32_t state;
  uint32_t namespace_id;
  jit_func_t jit_func;
  size_t code_size;
} tp4091_program_t;

// Command structures
typedef struct {
  uint32_t program_id;
  uint32_t code_size;
  uint8_t *code;
} tp4091_load_cmd_t;

typedef struct {
  uint32_t program_id;
  uint32_t namespace_id;
} tp4091_activate_cmd_t;

typedef struct {
  uint32_t program_id;
  uint64_t lba;
  uint32_t block_count;
  void *buffer;
} tp4091_execute_cmd_t;

// Program manager functions
int tp4091_init(void);
void tp4091_cleanup(void);

int tp4091_load(const tp4091_load_cmd_t *cmd);
int tp4091_activate(const tp4091_activate_cmd_t *cmd);
int tp4091_execute(const tp4091_execute_cmd_t *cmd, uint64_t *result);
int tp4091_unload(uint32_t program_id);

// Get program by ID
// Get program by ID
tp4091_program_t *tp4091_get_program(uint32_t program_id);

// Get currently executing program (context for helpers)
tp4091_program_t *tp4091_get_current_program(void);

#endif // TP4091_H
