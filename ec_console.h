/* ec_console.h - Debug console for Chrome EC
 *
 * (Chromium license) */

#ifndef __CROS_EC_CONSOLE_H
#define __CROS_EC_CONSOLE_H

#include "ec_common.h"

/* Console command */
typedef struct EcConsoleCommand {
  /* Command name.  Case-insensitive. */
  const char* name;
  /* Handler for the command.  argv[0] will be the command name. */
  EcError (*handler)(int argc, char** argv);
} EcConsoleCommand;


/* Console command group */
typedef struct EcConsoleCommandGroup {
  const char* group_name;            /* Name of the command group */
  const EcConsoleCommand* commands;  /* List of commands */
  int command_count;                 /* Number of commands in list */
} EcConsoleCommandGroup;


/* Initializes the console module. */
EcError ConsoleInit(void);


/* Registers a group of console commands. */
EcError ConsoleRegisterCommands(const EcConsoleCommandGroup* group);

#endif  /* __CROS_EC_CONSOLE_H */
