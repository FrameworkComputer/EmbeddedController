/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console module for Chrome EC */

#include <ctype.h>
#include <string.h>

#include "ec_console.h"
#include "ec_uart.h"

#define MAX_COMMAND_GROUPS 20
#define MAX_ARGS_PER_COMMAND 10

/* Forward declarations for console commands */
static EcError CommandHelp(int argc, char** argv);

static const EcConsoleCommandGroup *group_list[MAX_COMMAND_GROUPS];
static int group_count = 0;

static const EcConsoleCommand console_commands[] = {
  {"help", CommandHelp},
  {"?", CommandHelp},
};
static const EcConsoleCommandGroup console_group = {
  "Console", console_commands,
  sizeof(console_commands) / sizeof(EcConsoleCommand)
};


static void HasInputHandler(void) {
  /* TODO: if we had threads, this would wake the processor thread */
}


EcError EcConsoleInit(void) {
  /* Register the input handler to handle newlines */
  EcUartRegisterHasInputCallback(HasInputHandler, '\n');

  /* Register our internal commands */
  return EcConsoleRegisterCommands(&console_group);
}


/* Command handler - prints help. */
static EcError CommandHelp(int argc, char** argv) {
  const EcConsoleCommand *cmd;
  int c, g;

  EcUartPuts("Known commands:\n");

  for (g = 0; g < group_count; g++) {
    cmd = group_list[g]->commands;
    EcUartPrintf("Group %s:\n", group_list[g]->group_name);
    for (c = group_list[g]->command_count; c > 0; c--, cmd++)
      EcUartPrintf("  %s\n", cmd->name);
  }

  return EC_SUCCESS;
}


EcError EcConsoleRegisterCommands(const EcConsoleCommandGroup* group) {
  if (group_count >= MAX_COMMAND_GROUPS)
    return EC_ERROR_OVERFLOW;  /* No space for a new command group */

  group_list[group_count++] = group;
  return EC_SUCCESS;
}


/* Splits a line of input into words.  Stores the count of words in
 * <argc>.  Stores pointers to the words in <argv>, which must be at
 * least <max_argc> long.  If more than <max_argc> words are found,
 * discards the excess and returns EC_ERROR_OVERFLOW. */
EcError SplitWords(char* input, int max_argc, int* argc, char** argv) {
  char* c;
  int in_word = 0;

  /* Parse input into words */
  for (c = input; c; c++) {
    if (isspace(*c)) {
      if (in_word) {
        /* Ending a word */
        *c = '\0';
        ++*argc;
        in_word = 0;
      }
    } else {
      if (!in_word) {
        /* Starting a new word */
        if (*argc >= max_argc)
          return EC_ERROR_OVERFLOW;  /* More words than we can handle */

        argv[*argc] = c;
        in_word = 1;
      }
    }
  }

  return EC_SUCCESS;
}


/* Finds a command by name.  Returns the command structure, or NULL if
 * no match found. */
const EcConsoleCommand* FindCommand(char* name) {
  const EcConsoleCommand *cmd;
  int c, g;

  /* Find the command in the command groups */
  for (g = 0; g < group_count; g++) {
    cmd = group_list[g]->commands;
    for (c = group_list[g]->command_count; c > 0; c--, cmd++) {
      if (!strcasecmp(name, cmd->name))
        return cmd;
    }
  }

  return NULL;
}


/* Handles a line of input containing a single command.
 *
 * Modifies the input string during parsing. */
EcError ConsoleHandleCommand(char* input) {
  char* argv[MAX_ARGS_PER_COMMAND];
  const EcConsoleCommand *cmd;
  int argc = 0;

  /* Split input into words.  Ignore words past our limit. */
  SplitWords(input, MAX_ARGS_PER_COMMAND, &argc, argv);

  /* If no command, nothing to do */
  if (!argc)
    return EC_SUCCESS;

  cmd = FindCommand(argv[0]);
  if (cmd)
    return cmd->handler(argc, argv);

  EcUartPrintf("Command '%s' not found.\n", argv[0]);
  return EC_ERROR_UNKNOWN;
}


/* TODO: task function */
