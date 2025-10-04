/**
 * TocinOS Advanced Shell Enhancement
 * 
 * Features:
 * - Command-line editing with cursor movement
 * - Tab completion
 * - Command history with arrow keys
 * - Environment variables
 * - Command piping and redirection
 * - Job control (background/foreground)
 * - Script execution
 */

#ifndef SHELL_ADVANCED_H
#define SHELL_ADVANCED_H

#include <stdint.h>

// ==================== COMMAND LINE EDITING ====================

#define MAX_LINE_LENGTH 1024
#define MAX_HISTORY_SIZE 50
#define MAX_ENV_VARS 64

typedef struct {
    char buffer[MAX_LINE_LENGTH];
    int length;
    int cursor_pos;
    int history_index;
} cmdline_t;

// Line editing functions
void cmdline_init(cmdline_t *cmd);
void cmdline_insert_char(cmdline_t *cmd, char c);
void cmdline_delete_char(cmdline_t *cmd);
void cmdline_backspace(cmdline_t *cmd);
void cmdline_move_cursor_left(cmdline_t *cmd);
void cmdline_move_cursor_right(cmdline_t *cmd);
void cmdline_move_cursor_home(cmdline_t *cmd);
void cmdline_move_cursor_end(cmdline_t *cmd);
void cmdline_clear(cmdline_t *cmd);
void cmdline_redraw(cmdline_t *cmd);

// ==================== COMMAND HISTORY ====================

typedef struct {
    char commands[MAX_HISTORY_SIZE][MAX_LINE_LENGTH];
    int count;
    int current;
    int max_size;
} history_t;

void history_init(history_t *hist);
void history_add(history_t *hist, const char *cmd);
const char* history_get_prev(history_t *hist);
const char* history_get_next(history_t *hist);
const char* history_get(history_t *hist, int index);
void history_reset_position(history_t *hist);
void history_print(history_t *hist);

// ==================== ENVIRONMENT VARIABLES ====================

typedef struct {
    char name[64];
    char value[256];
} env_var_t;

typedef struct {
    env_var_t vars[MAX_ENV_VARS];
    int count;
} environment_t;

void env_init(environment_t *env);
int env_set(environment_t *env, const char *name, const char *value);
const char* env_get(environment_t *env, const char *name);
int env_unset(environment_t *env, const char *name);
void env_print(environment_t *env);
char* env_expand(environment_t *env, const char *str);

// ==================== TAB COMPLETION ====================

#define MAX_COMPLETIONS 32

typedef struct {
    char candidates[MAX_COMPLETIONS][MAX_LINE_LENGTH];
    int count;
} completion_t;

void completion_init(completion_t *comp);
void completion_add(completion_t *comp, const char *candidate);
int completion_find(completion_t *comp, const char *prefix);
const char* completion_get_common_prefix(completion_t *comp);
void completion_print(completion_t *comp);

// Tab completion for commands
int complete_command(const char *partial, completion_t *comp);
int complete_path(const char *partial, completion_t *comp);

// ==================== COMMAND PARSING ====================

#define MAX_ARGS 32
#define MAX_PIPES 8

typedef enum {
    REDIR_NONE = 0,
    REDIR_INPUT,      // <
    REDIR_OUTPUT,     // >
    REDIR_APPEND,     // >>
    REDIR_ERROR       // 2>
} redir_type_t;

typedef struct {
    char *file;
    redir_type_t type;
} redirection_t;

typedef struct {
    char *args[MAX_ARGS];
    int argc;
    redirection_t input;
    redirection_t output;
    redirection_t error;
    int background;  // Run in background if 1
} command_t;

typedef struct {
    command_t commands[MAX_PIPES];
    int num_commands;
} pipeline_t;

int parse_command_line(const char *line, pipeline_t *pipeline);
void free_pipeline(pipeline_t *pipeline);

// ==================== JOB CONTROL ====================

#define MAX_JOBS 32

typedef enum {
    JOB_RUNNING = 0,
    JOB_STOPPED = 1,
    JOB_DONE = 2
} job_state_t;

typedef struct {
    int job_id;
    int task_id;
    job_state_t state;
    char command[MAX_LINE_LENGTH];
    int background;
} job_t;

typedef struct {
    job_t jobs[MAX_JOBS];
    int count;
    int next_id;
} job_control_t;

void jobs_init(job_control_t *jc);
int jobs_add(job_control_t *jc, int task_id, const char *cmd, int background);
job_t* jobs_get(job_control_t *jc, int job_id);
void jobs_remove(job_control_t *jc, int job_id);
void jobs_print(job_control_t *jc);
int jobs_bring_to_foreground(job_control_t *jc, int job_id);
int jobs_send_to_background(job_control_t *jc, int job_id);

// ==================== SCRIPT EXECUTION ====================

typedef struct {
    const char *filename;
    int line_number;
    int error_code;
} script_context_t;

int script_execute(const char *filename);
int script_execute_line(script_context_t *ctx, const char *line);

// ==================== BUILT-IN COMMANDS ====================

typedef int (*builtin_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    builtin_func_t func;
    const char *help;
} builtin_command_t;

// Built-in command implementations
int builtin_cd(int argc, char **argv);
int builtin_pwd(int argc, char **argv);
int builtin_echo(int argc, char **argv);
int builtin_export(int argc, char **argv);
int builtin_unset(int argc, char **argv);
int builtin_alias(int argc, char **argv);
int builtin_jobs(int argc, char **argv);
int builtin_fg(int argc, char **argv);
int builtin_bg(int argc, char **argv);
int builtin_source(int argc, char **argv);
int builtin_exit(int argc, char **argv);

builtin_command_t* find_builtin(const char *name);

// ==================== ADVANCED SHELL INTERFACE ====================

void shell_advanced_init(void);
void shell_advanced_run(void);
int shell_execute_command(const char *cmdline);
int shell_execute_pipeline(pipeline_t *pipeline);

// ==================== ALIASES ====================

#define MAX_ALIASES 32

typedef struct {
    char name[64];
    char value[256];
} alias_t;

typedef struct {
    alias_t aliases[MAX_ALIASES];
    int count;
} alias_table_t;

void alias_init(alias_table_t *table);
int alias_add(alias_table_t *table, const char *name, const char *value);
const char* alias_get(alias_table_t *table, const char *name);
int alias_remove(alias_table_t *table, const char *name);
void alias_print(alias_table_t *table);

#endif // SHELL_ADVANCED_H
