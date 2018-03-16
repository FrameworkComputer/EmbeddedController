Stack Size Analysis Tool for EC Firmware
========================================

This tool does static analysis on EC firmwares to get the maximum stack usage of
each function and task. The maximum stack usage of a function includes the stack
used by itself and the functions it calls.

Usage
-----

Make sure the firmware of your target board has been built.

In `src/platform/ec`, run
```
make BOARD=${BOARD} SECTION=${SECTION} ANNOTATION=${ANNOTATION} analyzestack
```
The `${SECTION}` can be `RO` or `RW`. The `${ANNOTATION}` is a optional
annotation file, see the example_annotation.yaml, by default,
board/$BOARD/analyzestack.yaml is used.

Output
------

For each task, it will output the result like below,
```
Task: PD_C1, Max size: 1156 (932 + 224), Allocated size: 640
Call Trace:
    pd_task (160) [common/usb_pd_protocol.c:1644] 1008a6e8
        -> pd_task[common/usb_pd_protocol.c:1808] 1008ac8a
           - handle_request[common/usb_pd_protocol.c:1191]
             - handle_data_request[common/usb_pd_protocol.c:798]
        -> pd_task[common/usb_pd_protocol.c:2672] 1008c222
        -> [annotation]
    pd_send_request_msg.lto_priv.263 (56) [common/usb_pd_protocol.c:653] 1009a0b4
        -> pd_send_request_msg.lto_priv.263[common/usb_pd_protocol.c:712] 1009a22e0
```
The `pd_task` uses 160 bytes on the stack and calls `pd_send_request_msg.lto_priv.263`.

The callsites to the next function will be shown like below,
```
-> pd_task[common/usb_pd_protocol.c:1808] 1008ac8a
   - handle_request[common/usb_pd_protocol.c:1191]
     - handle_data_request[common/usb_pd_protocol.c:798]
-> pd_task[common/usb_pd_protocol.c:2672] 1008c222
-> [annotation]
```
This means one callsite to the next function is at `usb_pd_protocol.c:798`,
but it is inlined to the current function and you can follow the trace:
`usb_pd_protocol.c:1808 -> usb_pd_protocol.c:1191 -> usb_pd_protocol.c:798` to
find the callsite. The second callsite is at `usb_pd_protocol.c:2672`. And the
third one is added by annotation.

The unresolved indirect callsites have the similar format to the above.

Annotating Indirect Call
------------------------

To annotate an indirect call like this,
```
Unresolved indirect callsites:
    pd_transmit
        -> pd_transmit[common/usb_pd_protocol.c:407] 802c9c8
           - tcpm_transmit[driver/tcpm/tcpm.h:142]
```
It is an indirect call in the `tcpm_transmit`, which is inlined to the `pd_transmit`.

You can add a annotation like the below to eliminate it.
```
add:
  tcpm_transmit[driver/tcpm/tcpm.h:142]:
  - anx74xx_tcpm_transmit
```
The source `tcpm_transmit[driver/tcpm/tcpm.h:142]` must be a full signature (function_name[path:line number]).
So the resolver can know which indirect call you want to annotate and eliminate (even if it is inlined).

Annotating arrays (hooks, console commands, host commands)
----------------------------------------------------------

When a callsite calls a number of functions based on values from an constant
array (in `.rodata` section), one can use the following syntax:

```
  hook_task[common/hooks.c:197]:
    - { name: __deferred_funcs, stride: 4, offset: 0 }
    - { name: __hooks_second, stride: 8, offset: 0 }
    - { name: __hooks_tick, stride: 8, offset: 0 }
```

Where `name` is the symbol name for the start of the array (the end of the array
is `<name>_end`), stride is the array element size, and offset is the offset of
the function pointer in the structure. For example, above, `__deferred_funcs` is
a simple array of function pointers, while `__hooks_tick` is an array of
`struct hook_data` (size 8, pointer at offset 0):

```
struct hook_data {
        /* Hook processing routine. */
        void (*routine)(void);
        /* Priority; low numbers = higher priority. */
        int priority;
};
```
