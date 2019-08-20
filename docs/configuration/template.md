# EC Feature Configuration Template

*Short description of the EC feature and the capabilities provided*

## Config options

In [config.h], search for options that start with `CONFIG_<feature>*` and
evaluate whether each option is appropriate to add to `baseboard.h` or
`board.h`.

*Note - Avoid documenting `CONFIG_` options in the markdown as `config.h`
contains the authoritative definition.*

## Feature Parameters

*Detail `CONFIG_*` options that must be assigned to a value for this EC feature
to compile and operate.*

## GPIOs and Alternate Pins

*Document any hard-coded GPIO enumeration names required by the EC feature.*

*For pins that require an alternate function, note the module required by the EC
feature.*

## Data Structures

*Document any data structures that must be defined in the board.c or baseboard.c
files in order for the EC feature to compile and operate.*

*Document any functions that must be implemented in the board.c and baseboard.c
files.*

## Tasks

*Document any EC tasks that must be enabled by the feature.*

## Testing and Debugging

*Provide any tips for testing and debugging the EC feature.*

### Console Commands

*Document an EC console commands related to the feature.*

## Example

*Optional - provide code snippets from a working board to walk the user through
all code that must be created to enable this feature.*

[config.h]: ../new_board_checklist.md#config_h
