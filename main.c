#define _POSIX_C_SOURCE 200809L

#include "builtins.h"
#include "variables.h"
#include "parameters.h"
#include "chain.h"
#include "executor.h"
#include "expansion.h"
#include "input.h"
#include "history.h"
#include "jobs.h"
#include "lexer.h"
#include "parser.h"
#include "signals.h"
#include "terminal.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fnmatch.h>

typedef struct {
    int parent_active;
    int condition_true;
    int branch_taken;
    int awaiting_then;
    int in_else;
} ScriptIfFrame;

typedef struct {
    int parent_active;
    int condition_true;
    int awaiting_do;
    int repeating;
    int until;
    int for_loop;
    char *for_name;
    char **for_values;
    size_t for_count;
    size_t for_index;
    off_t body_start;
    int body_ran;
    int last_body_status;
    int flow; /* 0: normal, 1: break, 2: continue. */
    off_t start;
} ScriptLoopFrame;

static void free_for_loop(ScriptLoopFrame *frame)
{
    free(frame->for_name);
    for (size_t i = 0; i < frame->for_count; i++) {
        free(frame->for_values[i]);
    }
    free(frame->for_values);
}

typedef struct {
    int parent_active;
    int matched;
    int branch_active;
    int awaiting_pattern;
    char *value;
} ScriptCaseFrame;

typedef enum { CONTROL_IF, CONTROL_LOOP, CONTROL_CASE } ControlKind;

static char *case_separator(char *text, char separator)
{
    int in_brackets = 0;
    for (char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == '\\' && cursor[1] != '\0') {
            cursor++;
        } else if (*cursor == '[' && !in_brackets) {
            in_brackets = 1;
        } else if (*cursor == ']' && in_brackets) {
            in_brackets = 0;
        } else if (!in_brackets && *cursor == separator) {
            return cursor;
        }
    }
    return NULL;
}

static int case_pattern_matches(char *patterns, const char *value)
{
    char *part = patterns;
    for (;;) {
        char *end = case_separator(part, '|');
        if (end != NULL) *end = '\0';
        while (isspace((unsigned char)*part)) part++;
        char *tail = part + strlen(part);
        while (tail > part && isspace((unsigned char)tail[-1])) *--tail = '\0';
        int matches = *part != '\0' && fnmatch(part, value, 0) == 0;
        if (matches || end == NULL) return matches;
        part = end + 1;
    }
}

static char *skip_space(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

static int script_keyword(const char *text, const char *keyword)
{
    size_t length = strlen(keyword);
    if (strncmp(text, keyword, length) != 0) {
        return 0;
    }
    text += length;
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return *text == '\0' || *text == '#';
}

static int script_branch_active(const ScriptIfFrame *frame)
{
    return frame->parent_active && !frame->awaiting_then
        && (frame->in_else ? !frame->branch_taken : frame->condition_true);
}

static int script_loop_active(const ScriptLoopFrame *frame)
{
    return frame->parent_active && !frame->awaiting_do
        && frame->condition_true && frame->flow == 0;
}

static int script_loop_flow_pending(const ScriptLoopFrame *loops,
                                    size_t loop_depth)
{
    for (size_t i = 0; i < loop_depth; i++) {
        if (loops[i].flow != 0) {
            return 1;
        }
    }
    return 0;
}

static int script_context_active(const ScriptIfFrame *ifs, size_t if_depth,
                                 const ScriptLoopFrame *loops, size_t loop_depth,
                                 const ScriptCaseFrame *cases, size_t case_depth,
                                 const ControlKind *order, size_t depth)
{
    if (script_loop_flow_pending(loops, loop_depth)) {
        return 0;
    }
    if (depth == 0) {
        return 1;
    }
    if (order[depth - 1] == CONTROL_IF) {
        return script_branch_active(&ifs[if_depth - 1]);
    }
    if (order[depth - 1] == CONTROL_LOOP) {
        return script_loop_active(&loops[loop_depth - 1]);
    }
    return cases[case_depth - 1].parent_active
        && cases[case_depth - 1].branch_active;
}

static int script_flow_level(char *text, size_t keyword_length,
                             size_t *level)
{
    char *argument = skip_space(text + keyword_length);
    if (*argument == '\0' || *argument == '#') {
        *level = 1;
        return 0;
    }
    size_t parsed = 0;
    if (!isdigit((unsigned char)*argument)) {
        return -1;
    }
    do {
        parsed = parsed * 10 + (size_t)(*argument - '0');
        if (parsed > 32) {
            return -1;
        }
        argument++;
    } while (isdigit((unsigned char)*argument));
    argument = skip_space(argument);
    if (parsed == 0 || (*argument != '\0' && *argument != '#')) {
        return -1;
    }
    *level = parsed;
    return 0;
}

/* Expand a standalone eval line once, then return text for a second parse. */
static int expand_eval_line(const char *line, int last_status, char **result,
                            char *error, size_t error_size,
                            int *required_failure)
{
    *result = NULL;
    *required_failure = 0;
    while (isspace((unsigned char)*line)) line++;
    if (strncmp(line, "eval", 4) != 0
        || (line[4] != '\0' && !isspace((unsigned char)line[4]))) {
        return 0;
    }

    TokenList words = {0};
    if (lex_line(line, &words, error, error_size) == -1) return -1;
    for (size_t i = 0; i < words.count; i++) {
        if (words.items[i].type != TOKEN_WORD) {
            snprintf(error, error_size,
                     "eval requires a standalone command line");
            free_tokens(&words);
            return -1;
        }
    }
    if (expand_pipeline_fields(&words, last_status, error, error_size,
                               required_failure) == -1) {
        free_tokens(&words);
        return -1;
    }

    size_t length = 0;
    for (size_t i = 1; i < words.count; i++) {
        if (words.items[i].expansion_empty) continue;
        size_t field_length = strlen(words.items[i].text);
        size_t separator = length > 0;
        if (field_length > MSH_MAX_LINE - length
            || separator > MSH_MAX_LINE - length - field_length) {
            snprintf(error, error_size, "eval command is too long");
            free_tokens(&words);
            return -1;
        }
        length += field_length + separator;
    }
    char *text = malloc(length + 1);
    if (text == NULL) {
        snprintf(error, error_size, "out of memory during eval");
        free_tokens(&words);
        return -1;
    }
    size_t used = 0;
    for (size_t i = 1; i < words.count; i++) {
        if (words.items[i].expansion_empty) continue;
        if (used > 0) text[used++] = ' ';
        size_t field_length = strlen(words.items[i].text);
        memcpy(text + used, words.items[i].text, field_length);
        used += field_length;
    }
    text[used] = '\0';
    free_tokens(&words);
    *result = text;
    return 1;
}

int main(int argc, char **argv)
{
    int script_mode = argc >= 2;
    int input_fd = STDIN_FILENO;
    struct {
        int input_fd;
        int script_mode;
        size_t control_depth;
    } source_stack[16];
    size_t source_depth = 0;

    if (script_mode) {
        input_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
        if (input_fd == -1) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }

        struct stat info;
        if (fstat(input_fd, &info) == -1) {
            perror("msh: inspect script");
            close(input_fd);
            return EXIT_FAILURE;
        }
        if (S_ISDIR(info.st_mode)) {
            fprintf(stderr, "msh: script is a directory: %s\n", argv[1]);
            close(input_fd);
            return EXIT_FAILURE;
        }
    }

    if (terminal_init_mode(!script_mode) == -1) {
        if (script_mode) {
            close(input_fd);
        }
        return EXIT_FAILURE;
    }

    if (signals_init() == -1) {
        terminal_reclaim();
        if (script_mode) {
            close(input_fd);
        }
        return EXIT_FAILURE;
    }

    const char *parameter_zero = script_mode ? argv[1] : argv[0];
    size_t parameter_count = script_mode ? (size_t)(argc - 2) : 0;
    char **parameter_arguments = script_mode ? argv + 2 : NULL;

    if (parameters_set(parameter_zero, parameter_count,
                       parameter_arguments) == -1) {
        perror("msh: initialize parameters");
        signals_close();
        terminal_reclaim();
        if (script_mode) {
            close(input_fd);
        }
        return EXIT_FAILURE;
    }

    char *line = NULL;
    size_t capacity = 0;
    int interactive = terminal_is_interactive();
    int status = EXIT_SUCCESS;
    int exit_warned = 0;
    ScriptIfFrame if_stack[32];
    size_t if_depth = 0;
    ScriptLoopFrame loop_stack[32];
    size_t loop_depth = 0;
    ScriptCaseFrame case_stack[32];
    size_t case_depth = 0;
    ControlKind control_order[32];
    size_t control_depth = 0;

    for (;;) {
        jobs_notify();

        if (interactive && !script_mode) {
            fputs("msh> ", stdout);
            fflush(stdout);
        }

        off_t line_start = script_mode ? lseek(input_fd, 0, SEEK_CUR) : -1;
        int input_result = input_read_command_line_fd(input_fd, &line, &capacity);

        if (input_result == -3) {
            status = 130;
            exit_warned = 0;
            continue;
        }

        if (input_result == -2) {
            status = 2;
            exit_warned = 0;
            continue;
        }

        if (input_result == -1) {
            status = EXIT_FAILURE;
            break;
        }

        if (input_result == 0) {
            if (source_depth > 0) {
                if (control_depth != source_stack[source_depth - 1].control_depth) {
                    fprintf(stderr, "msh: unfinished control block in sourced file\n");
                    status = 2;
                    break;
                }
                close(input_fd);
                source_depth--;
                input_fd = source_stack[source_depth].input_fd;
                script_mode = source_stack[source_depth].script_mode;
                continue;
            }
            if (script_mode && control_depth > 0) {
                fprintf(stderr, "msh: unfinished control block in script\n");
                status = 2;
                break;
            }
            if (interactive && jobs_have_active() && !exit_warned) {
                fprintf(stderr,
                        "\nmsh: active jobs remain; "
                        "repeat exit or Ctrl+D to terminate them\n");
                exit_warned = 1;
                continue;
            }

            if (interactive) {
                putchar('\n');
            }
            break;
        }

        if (interactive && !script_mode && history_add(line) == -1) {
            perror("msh: history");
        }

        char *work_line = line;
        int if_condition = 0;
        int loop_condition = 0;
        if (script_mode) {
            char *text = skip_space(line);
            if (control_depth > 0
                && control_order[control_depth - 1] == CONTROL_CASE
                && case_stack[case_depth - 1].awaiting_pattern
                && !script_keyword(text, "esac")) {
                if (*text == '\0' || *text == '#') continue;
                char *close = case_separator(text, ')');
                if (close == NULL || close == text
                    || (*skip_space(close + 1) != '\0'
                        && *skip_space(close + 1) != '#')) {
                    fprintf(stderr, "msh: expected case pattern followed by )\n");
                    status = 2;
                    break;
                }
                ScriptCaseFrame *frame = &case_stack[case_depth - 1];
                frame->awaiting_pattern = 0;
                frame->branch_active = 0;
                if (frame->parent_active && !frame->matched) {
                    char *patterns = strndup(text, (size_t)(close - text));
                    if (patterns == NULL) {
                        perror("msh: case pattern");
                        status = 1;
                        break;
                    }
                    frame->branch_active = case_pattern_matches(patterns,
                                                                 frame->value);
                    frame->matched = frame->branch_active;
                    free(patterns);
                    if (frame->branch_active) status = 0;
                }
                continue;
            }
            if (strncmp(text, "case", 4) == 0
                && isspace((unsigned char)text[4])) {
                if (control_depth == 32
                    || (control_depth > 0
                        && ((control_order[control_depth - 1] == CONTROL_IF
                             && if_stack[if_depth - 1].awaiting_then)
                            || (control_order[control_depth - 1] == CONTROL_LOOP
                                && loop_stack[loop_depth - 1].awaiting_do)))) {
                    fprintf(stderr, "msh: invalid or too deeply nested case\n");
                    status = 2;
                    break;
                }
                TokenList words = {0};
                char error[256];
                if (lex_line(text, &words, error, sizeof(error)) == -1) {
                    fprintf(stderr, "msh: %s\n", error);
                    status = 2;
                    break;
                }
                if (words.count != 3 || words.items[1].type != TOKEN_WORD
                    || words.items[2].type != TOKEN_WORD
                    || strcmp(words.items[2].raw_text, "in") != 0) {
                    fprintf(stderr, "msh: expected case WORD in\n");
                    free_tokens(&words);
                    status = 2;
                    break;
                }
                int active = script_context_active(if_stack, if_depth,
                    loop_stack, loop_depth, case_stack, case_depth,
                    control_order, control_depth);
                if (active && expand_tokens(&words, status, error,
                                            sizeof(error)) == -1) {
                    fprintf(stderr, "msh: %s\n", error);
                    free_tokens(&words);
                    status = 1;
                    break;
                }
                char *value = strdup(active ? words.items[1].text : "");
                free_tokens(&words);
                if (value == NULL) {
                    perror("msh: case value");
                    status = 1;
                    break;
                }
                case_stack[case_depth++] = (ScriptCaseFrame) {
                    .parent_active = active, .awaiting_pattern = 1,
                    .value = value
                };
                control_order[control_depth++] = CONTROL_CASE;
                continue;
            }
            if (script_keyword(text, ";;")) {
                if (control_depth == 0
                    || control_order[control_depth - 1] != CONTROL_CASE
                    || case_stack[case_depth - 1].awaiting_pattern) {
                    fprintf(stderr, "msh: unexpected ;;\n");
                    status = 2;
                    break;
                }
                case_stack[case_depth - 1].branch_active = 0;
                case_stack[case_depth - 1].awaiting_pattern = 1;
                continue;
            }
            if (script_keyword(text, "esac")) {
                if (control_depth == 0
                    || control_order[control_depth - 1] != CONTROL_CASE) {
                    fprintf(stderr, "msh: unexpected esac\n");
                    status = 2;
                    break;
                }
                ScriptCaseFrame *frame = &case_stack[case_depth - 1];
                if (frame->parent_active && !frame->matched) status = 0;
                free(frame->value);
                case_depth--;
                control_depth--;
                continue;
            }
            if (strncmp(text, "if", 2) == 0
                && (text[2] == '\0'
                    || isspace((unsigned char)text[2]))) {
                if ((control_depth > 0
                     && ((control_order[control_depth - 1] == CONTROL_IF
                          && if_stack[if_depth - 1].awaiting_then)
                         || (control_order[control_depth - 1] == CONTROL_LOOP
                             && loop_stack[loop_depth - 1].awaiting_do)))
                    || control_depth == 32) {
                    fprintf(stderr, "msh: invalid or too deeply nested if\n");
                    status = 2;
                    break;
                }
                char *condition = skip_space(text + 2);
                if (*condition == '\0' || *condition == '#') {
                    fprintf(stderr, "msh: if requires a condition\n");
                    status = 2;
                    break;
                }
                int active = script_context_active(
                    if_stack, if_depth, loop_stack, loop_depth,
                    case_stack, case_depth,
                    control_order, control_depth);
                if_stack[if_depth++] = (ScriptIfFrame) {
                    .parent_active = active,
                    .awaiting_then = 1
                };
                control_order[control_depth++] = CONTROL_IF;
                if (!active) {
                    continue;
                }
                work_line = condition;
                if_condition = 1;
            } else if (strncmp(text, "elif", 4) == 0
                       && (text[4] == '\0'
                           || isspace((unsigned char)text[4]))) {
                if (control_depth == 0
                    || control_order[control_depth - 1] != CONTROL_IF
                    || if_stack[if_depth - 1].awaiting_then
                    || if_stack[if_depth - 1].in_else) {
                    fprintf(stderr, "msh: unexpected elif\n");
                    status = 2;
                    break;
                }
                char *condition = skip_space(text + 4);
                if (*condition == '\0' || *condition == '#') {
                    fprintf(stderr, "msh: elif requires a condition\n");
                    status = 2;
                    break;
                }
                ScriptIfFrame *frame = &if_stack[if_depth - 1];
                frame->condition_true = 0;
                frame->awaiting_then = 1;
                if (!frame->parent_active || frame->branch_taken
                    || script_loop_flow_pending(loop_stack, loop_depth)) {
                    continue;
                }
                work_line = condition;
                if_condition = 1;
            } else if (script_keyword(text, "then")) {
                if (control_depth == 0
                    || control_order[control_depth - 1] != CONTROL_IF
                    || !if_stack[if_depth - 1].awaiting_then) {
                    fprintf(stderr, "msh: unexpected then\n");
                    status = 2;
                    break;
                }
                if_stack[if_depth - 1].awaiting_then = 0;
                continue;
            } else if (script_keyword(text, "else")) {
                if (control_depth == 0
                    || control_order[control_depth - 1] != CONTROL_IF
                    || if_stack[if_depth - 1].awaiting_then
                    || if_stack[if_depth - 1].in_else) {
                    fprintf(stderr, "msh: unexpected else\n");
                    status = 2;
                    break;
                }
                if_stack[if_depth - 1].in_else = 1;
                if (script_branch_active(&if_stack[if_depth - 1])
                    && !script_loop_flow_pending(loop_stack, loop_depth)) {
                    status = 0;
                }
                continue;
            } else if (script_keyword(text, "fi")) {
                if (control_depth == 0
                    || control_order[control_depth - 1] != CONTROL_IF
                    || if_stack[if_depth - 1].awaiting_then) {
                    fprintf(stderr, "msh: unexpected fi\n");
                    status = 2;
                    break;
                }
                ScriptIfFrame frame = if_stack[--if_depth];
                control_depth--;
                if (frame.parent_active && !frame.branch_taken
                    && !script_loop_flow_pending(loop_stack, loop_depth)
                    && !frame.in_else) {
                    status = 0;
                }
                continue;
            } else if (strncmp(text, "for", 3) == 0
                       && isspace((unsigned char)text[3])) {
                if (line_start == -1 || control_depth == 32
                    || (control_depth > 0
                        && ((control_order[control_depth - 1] == CONTROL_IF
                             && if_stack[if_depth - 1].awaiting_then)
                            || (control_order[control_depth - 1] == CONTROL_LOOP
                                && loop_stack[loop_depth - 1].awaiting_do)))) {
                    fprintf(stderr, "msh: invalid for loop\n");
                    status = 2;
                    break;
                }
                TokenList words = {0};
                char error[256];
                if (lex_line(text, &words, error, sizeof(error)) == -1) {
                    fprintf(stderr, "msh: %s\n", error);
                    status = 2;
                    break;
                }
                int implicit_arguments = words.count == 2;
                if (words.count < 2 || words.items[1].type != TOKEN_WORD
                    || !variables_valid_name(words.items[1].text)
                    || strcmp(words.items[1].raw_text, words.items[1].text) != 0
                    || (!implicit_arguments
                        && (words.items[2].type != TOKEN_WORD
                            || strcmp(words.items[2].raw_text, "in") != 0))) {
                    fprintf(stderr, "msh: expected for NAME [in WORDS]\n");
                    free_tokens(&words);
                    status = 2;
                    break;
                }
                int valid = 1;
                for (size_t i = 3; i < words.count; i++) {
                    if (words.items[i].type != TOKEN_WORD) valid = 0;
                }
                if (!valid) {
                    fprintf(stderr, "msh: invalid for word list\n");
                    free_tokens(&words);
                    status = 2;
                    break;
                }
                int active = script_context_active(if_stack, if_depth,
                    loop_stack, loop_depth, case_stack, case_depth,
                    control_order, control_depth);
                if (active) {
                    int required = 0;
                    if (expand_pipeline_fields(&words, status, error,
                                               sizeof(error), &required) == -1) {
                        fprintf(stderr, "msh: %s\n", error);
                        free_tokens(&words);
                        status = required ? 1 : 2;
                        break;
                    }
                }
                ScriptLoopFrame frame = { .parent_active = active,
                    .awaiting_do = 1, .for_loop = 1, .start = line_start };
                frame.for_name = strdup(words.items[1].text);
                if (frame.for_name == NULL) valid = 0;
                size_t value_count = implicit_arguments ? parameters_count()
                    : words.count - 3;
                if (active && value_count > 0) {
                    frame.for_values = calloc(value_count, sizeof(char *));
                    if (frame.for_values == NULL) valid = 0;
                    if (valid) {
                        for (size_t i = 0; i < value_count; i++) {
                            const char *value = implicit_arguments
                                ? parameters_get(i + 1) : words.items[i + 3].text;
                            frame.for_values[frame.for_count] = strdup(value);
                            if (frame.for_values[frame.for_count] == NULL) {
                                valid = 0;
                                break;
                            }
                            frame.for_count++;
                        }
                    }
                }
                free_tokens(&words);
                if (!valid) {
                    free_for_loop(&frame);
                    fprintf(stderr, "msh: out of memory\n");
                    status = 1;
                    break;
                }
                loop_stack[loop_depth++] = frame;
                control_order[control_depth++] = CONTROL_LOOP;
                continue;
            } else if ((strncmp(text, "while", 5) == 0
                        && (text[5] == '\0'
                            || isspace((unsigned char)text[5])))
                       || (strncmp(text, "until", 5) == 0
                           && (text[5] == '\0'
                               || isspace((unsigned char)text[5])))) {
                char *condition = skip_space(text + 5);
                if (*condition == '\0' || *condition == '#') {
                    fprintf(stderr, "msh: loop requires a condition\n");
                    status = 2;
                    break;
                }
                if (line_start == -1) {
                    fprintf(stderr, "msh: loops require a seekable script file\n");
                    status = 2;
                    break;
                }
                int until = text[0] == 'u';
                ScriptLoopFrame *frame;
                if (control_depth > 0
                    && control_order[control_depth - 1] == CONTROL_LOOP
                    && loop_stack[loop_depth - 1].repeating
                    && loop_stack[loop_depth - 1].start == line_start) {
                    frame = &loop_stack[loop_depth - 1];
                    frame->repeating = 0;
                    frame->condition_true = 0;
                } else {
                    if ((control_depth > 0
                         && ((control_order[control_depth - 1] == CONTROL_IF
                              && if_stack[if_depth - 1].awaiting_then)
                             || (control_order[control_depth - 1]
                                 == CONTROL_LOOP
                                 && loop_stack[loop_depth - 1].awaiting_do)))
                        || control_depth == 32) {
                        fprintf(stderr, "msh: invalid or too deeply nested loop\n");
                        status = 2;
                        break;
                    }
                    int active = script_context_active(
                        if_stack, if_depth, loop_stack, loop_depth,
                        case_stack, case_depth,
                        control_order, control_depth);
                    loop_stack[loop_depth++] = (ScriptLoopFrame) {
                        .parent_active = active,
                        .awaiting_do = 1,
                        .until = until,
                        .start = line_start
                    };
                    control_order[control_depth++] = CONTROL_LOOP;
                    frame = &loop_stack[loop_depth - 1];
                }
                if (frame->until != until || !frame->parent_active) {
                    if (frame->until != until) {
                        fprintf(stderr, "msh: loop keyword changed\n");
                        status = 2;
                        break;
                    }
                    continue;
                }
                work_line = condition;
                loop_condition = 1;
            } else if (script_keyword(text, "do")) {
                if (control_depth == 0
                    || control_order[control_depth - 1] != CONTROL_LOOP
                    || !loop_stack[loop_depth - 1].awaiting_do) {
                    fprintf(stderr, "msh: unexpected do\n");
                    status = 2;
                    break;
                }
                ScriptLoopFrame *frame = &loop_stack[loop_depth - 1];
                frame->awaiting_do = 0;
                if (frame->for_loop) {
                    frame->body_start = lseek(input_fd, 0, SEEK_CUR);
                    frame->condition_true = frame->for_count > 0;
                    if (frame->parent_active && frame->condition_true
                        && variables_set(frame->for_name,
                                         frame->for_values[0], 0) == -1) {
                        perror("msh: for assignment");
                        status = 1;
                        break;
                    }
                }
                if (script_loop_active(frame)) {
                    status = 0;
                }
                continue;
            } else if (script_keyword(text, "done")) {
                if (control_depth == 0
                    || control_order[control_depth - 1] != CONTROL_LOOP
                    || loop_stack[loop_depth - 1].awaiting_do) {
                    fprintf(stderr, "msh: unexpected done\n");
                    status = 2;
                    break;
                }
                ScriptLoopFrame *frame = &loop_stack[loop_depth - 1];
                if (frame->for_loop) {
                    if (frame->flow != 1 && frame->parent_active
                        && frame->condition_true) {
                        frame->last_body_status = frame->flow == 2 ? 0 : status;
                        frame->body_ran = 1;
                        frame->flow = 0;
                        if (++frame->for_index < frame->for_count) {
                            if (variables_set(frame->for_name,
                                frame->for_values[frame->for_index], 0) == -1
                                || lseek(input_fd, frame->body_start,
                                         SEEK_SET) == -1) {
                                perror("msh: advance for loop");
                                status = 1;
                                break;
                            }
                            status = 0;
                            continue;
                        }
                    }
                    if (frame->parent_active) {
                        status = frame->flow == 1 ? 0
                            : frame->body_ran ? frame->last_body_status : 0;
                    }
                    free_for_loop(frame);
                    loop_depth--;
                    control_depth--;
                    continue;
                }
                if (frame->flow != 0) {
                    int flow = frame->flow;
                    if (flow == 2) {
                        frame->flow = 0;
                        frame->last_body_status = 0;
                        frame->body_ran = 1;
                        frame->awaiting_do = 1;
                        frame->repeating = 1;
                        if (lseek(input_fd, frame->start, SEEK_SET) == -1) {
                            perror("msh: rewind loop");
                            status = 1;
                            break;
                        }
                    } else {
                        loop_depth--;
                        control_depth--;
                    }
                    status = 0;
                    continue;
                }
                if (script_loop_active(frame)) {
                    frame->last_body_status = status;
                    frame->body_ran = 1;
                    frame->awaiting_do = 1;
                    frame->repeating = 1;
                    if (lseek(input_fd, frame->start, SEEK_SET) == -1) {
                        perror("msh: rewind loop");
                        status = 1;
                        break;
                    }
                    continue;
                }
                if (frame->parent_active) {
                    status = frame->body_ran ? frame->last_body_status : 0;
                }
                loop_depth--;
                control_depth--;
                continue;
            } else if ((strncmp(text, "break", 5) == 0
                        && (text[5] == '\0'
                            || isspace((unsigned char)text[5])))
                       || (strncmp(text, "continue", 8) == 0
                           && (text[8] == '\0'
                               || isspace((unsigned char)text[8])))) {
                if (!script_context_active(if_stack, if_depth, loop_stack,
                                           loop_depth, case_stack, case_depth,
                                           control_order,
                                           control_depth)) {
                    continue;
                }
                int continuing = text[0] == 'c';
                size_t level;
                if (script_flow_level(text, continuing ? 8 : 5,
                                      &level) == -1
                    || level > loop_depth) {
                    fprintf(stderr, "msh: %s: invalid loop level\n",
                            continuing ? "continue" : "break");
                    status = 2;
                    break;
                }
                size_t target = loop_depth - level;
                for (size_t i = target; i < loop_depth; i++) {
                    loop_stack[i].flow = i == target && continuing ? 2 : 1;
                }
                status = 0;
                continue;
            } else if (control_depth > 0) {
                if (control_order[control_depth - 1] == CONTROL_IF
                    && if_stack[if_depth - 1].awaiting_then) {
                    if (*text == '\0' || *text == '#') {
                        continue;
                    }
                    fprintf(stderr, "msh: expected then\n");
                    status = 2;
                    break;
                }
                if (control_order[control_depth - 1] == CONTROL_LOOP
                    && loop_stack[loop_depth - 1].awaiting_do) {
                    if (*text == '\0' || *text == '#') {
                        continue;
                    }
                    fprintf(stderr, "msh: expected do\n");
                    status = 2;
                    break;
                }
                if (!script_context_active(if_stack, if_depth, loop_stack,
                                           loop_depth, case_stack, case_depth,
                                           control_order,
                                           control_depth)) {
                    continue;
                }
            }
        }

        char *evaluated_line = NULL;
        char eval_error[128];
        int eval_failed = 0;
        int eval_required = 0;
        for (int depth = 0; depth < 8; depth++) {
            char *next = NULL;
            int required = 0;
            int eval_result = expand_eval_line(work_line, status, &next,
                eval_error, sizeof(eval_error), &required);
            if (eval_result == 0) break;
            if (eval_result == -1) {
                eval_failed = 1;
                eval_required = required;
                break;
            }
            free(evaluated_line);
            evaluated_line = next;
            work_line = next;
            if (*work_line == '\0') {
                status = 0;
                break;
            }
            if (depth == 7) {
                snprintf(eval_error, sizeof(eval_error),
                         "eval nesting is too deep");
                eval_failed = 1;
            }
        }
        if (eval_failed) {
            fprintf(stderr, "msh: %s\n", eval_error);
            free(evaluated_line);
            status = eval_required ? 1 : 2;
            if (if_condition || loop_condition || (script_mode && eval_required))
                break;
            continue;
        }

        TokenList tokens;
        CommandList list;
        char error[128];

        if (lex_line(work_line, &tokens, error, sizeof(error)) == -1) {
            fprintf(stderr, "msh: %s\n", error);
            status = 2;
            exit_warned = 0;
            free(evaluated_line);
            if (if_condition || loop_condition) {
                break;
            }
            continue;
        }

        /* Validate the whole list before executing any part of it. */
        if (parse_command_list(&tokens, &list,
                               error, sizeof(error)) == -1) {
            fprintf(stderr, "msh: %s\n", error);
            free_tokens(&tokens);
            free(evaluated_line);
            status = 2;
            exit_warned = 0;
            if (if_condition || loop_condition) {
                break;
            }
            continue;
        }

        int should_exit = 0;
        int source_started = 0;
        int source_invalid = 0;

        for (size_t i = 0; i < list.count; i++) {
            CommandListItem item = list.items[i];

            /*
             * Launch a background logical chain before evaluating
             * any of its conditions or expanding any of its words.
             */
            size_t chain_end = command_list_chain_end(&list, i);
            const CommandListItem *last_item = &list.items[chain_end - 1];
            const Token *chain_last = &tokens.items[
                last_item->token_start + last_item->token_count - 1
            ];

            if (chain_end > i + 1 && chain_last->type == TOKEN_AMP) {
                const Token *chain_first = &tokens.items[item.token_start];
                size_t text_end = chain_last->position
                    + strlen(chain_last->raw_text);

                char *chain_text = strndup(
                    work_line + chain_first->position,
                    text_end - chain_first->position
                );

                if (chain_text == NULL) {
                    perror("msh: chain command text");
                    status = EXIT_FAILURE;
                    exit_warned = 0;
                    break;
                }

                status = execute_background_chain(
                    &tokens, &list, i, chain_end, status, chain_text
                );
                free(chain_text);
                i = chain_end - 1;
                exit_warned = 0;
                continue;
            }

            if ((item.condition == LIST_ON_SUCCESS && status != 0)
                || (item.condition == LIST_ON_FAILURE && status == 0)) {
                continue;
            }

            /* Expansion owns this copy and may later resize it. */
            TokenList part = {0};
            if (tokens_copy_range(&tokens, item.token_start,
                                  item.token_count, &part) == -1) {
                perror("msh: copy command tokens");
                status = EXIT_FAILURE;
                exit_warned = 0;
                break;
            }

            const Token *first = &part.items[0];
            const Token *last = &part.items[part.count - 1];
            size_t text_end = last->position + strlen(last->raw_text);

            char *command_text = strndup(
                work_line + first->position, text_end - first->position
            );

            if (command_text == NULL) {
                perror("msh: command text");
                free_tokens(&part);
                status = EXIT_FAILURE;
                exit_warned = 0;
                break;
            }

            /* Earlier commands may have changed status or variables. */
            int required_failure = 0;
            if (expand_pipeline_fields(&part, status,
                                      error, sizeof(error),
                                      &required_failure) == -1) {
                fprintf(stderr, "msh: %s\n", error);
                free(command_text);
                free_tokens(&part);
                status = EXIT_FAILURE;
                should_exit = !interactive && required_failure;
                exit_warned = 0;
                break;
            }

            Pipeline pipeline;

            if (parse_pipeline_assignments(&part, &pipeline,
                               error, sizeof(error)) == -1) {
                fprintf(stderr, "msh: %s\n", error);
                free(command_text);
                free_tokens(&part);
                status = 2;
                exit_warned = 0;
                break;
            }
            free_tokens(&part);

            int unsupported_assignment = 0;
            for (size_t stage = 0; stage < pipeline.command_count; stage++) {
                const Command *command = &pipeline.commands[stage];
                if (command->assignment_count > 0
                    && command->argc > 0 && is_builtin(command)
                    && (pipeline.command_count != 1 || pipeline.background)
                        && strcmp(command->argv[0], "pwd") != 0
                        && strcmp(command->argv[0], ":") != 0) {
                    unsupported_assignment = 1;
                }
            }

            if (unsupported_assignment) {
                fprintf(stderr,
                        "msh: assignments require a standalone foreground "
                        "assignment-only command or an external "
                        "command/pipeline stage\n");
                free_pipeline(&pipeline);
                free(command_text);
                status = 2;
                exit_warned = 0;
                continue;
            }

            if (pipeline.command_count == 1 && !pipeline.background) {
                const Command *command = &pipeline.commands[0];
                if (command->argc > 0 && strcmp(command->argv[0], ".") == 0) {
                    if (list.count != 1 || command->argc != 2
                        || command->assignment_count != 0
                        || command->redirection_count != 0
                        || control_depth != 0 || source_depth == 16) {
                        fprintf(stderr, "msh: .: expected a standalone file at script top level\n");
                        status = 2;
                        source_invalid = 1;
                    } else {
                        int fd = open(command->argv[1], O_RDONLY | O_CLOEXEC);
                        if (fd == -1) {
                            perror("msh: .");
                            status = 1;
                        } else {
                            struct stat info;
                            if (fstat(fd, &info) == -1 || S_ISDIR(info.st_mode)) {
                                fprintf(stderr, "msh: .: cannot read script: %s\n",
                                        command->argv[1]);
                                close(fd);
                                status = 1;
                            } else {
                                source_stack[source_depth].input_fd = input_fd;
                                source_stack[source_depth].script_mode = script_mode;
                                source_stack[source_depth].control_depth = control_depth;
                                source_depth++;
                                input_fd = fd;
                                script_mode = 1;
                                status = 0;
                                source_started = 1;
                            }
                        }
                    }
                } else {
                BuiltinResult result =
                    run_builtin_redirected(command, status);

                if (result.handled) {
                    status = result.status;
                    should_exit = result.should_exit;
                } else {
                    status = execute_pipeline(&pipeline, command_text, status);
                }
                }
            } else {
                status = execute_pipeline(&pipeline, command_text, status);
            }

            free_pipeline(&pipeline);
            free(command_text);

            if (should_exit || source_started || source_invalid) {
                break;
            }

            /* A command between exit attempts cancels confirmation. */
            exit_warned = 0;
        }

        free_command_list(&list);
        free_tokens(&tokens);
        free(evaluated_line);

        if (source_started) continue;

        if (if_condition) {
            if_stack[if_depth - 1].condition_true = status == 0;
            if_stack[if_depth - 1].branch_taken |= status == 0;
        }
        if (loop_condition) {
            ScriptLoopFrame *frame = &loop_stack[loop_depth - 1];
            frame->condition_true = frame->until ? status != 0 : status == 0;
        }

        if (should_exit) {
            if (interactive && jobs_have_active() && !exit_warned) {
                fprintf(stderr,
                        "msh: active jobs remain; "
                        "repeat exit or Ctrl+D to terminate them\n");
                exit_warned = 1;
                continue;
            }

            break;
        }
    }

    for (size_t i = 0; i < case_depth; i++) free(case_stack[i].value);
    while (source_depth > 0) {
        close(input_fd);
        input_fd = source_stack[--source_depth].input_fd;
        script_mode = source_stack[source_depth].script_mode;
    }
    for (size_t i = 0; i < loop_depth; i++) {
        if (loop_stack[i].for_loop) free_for_loop(&loop_stack[i]);
    }
    jobs_shutdown();
    variables_clear();
    parameters_clear();
    history_clear();
    signals_close();

    if (terminal_reclaim() == -1) {
        status = EXIT_FAILURE;
    }

    if (script_mode) {
        close(input_fd);
    }

    free(line);
    return status;
}
