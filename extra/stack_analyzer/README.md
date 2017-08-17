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
annotation file, see the example_annotation.yaml.

Output
------

For each task, it will output the result like below,
```
Task: PD_C1, Max size: 1156 (932 + 224), Allocated size: 640
Call Trace:
    pd_task (160) [common/usb_pd_protocol.c:1644] 1008a6e8
        -> pd_task [common/usb_pd_protocol.c:1808] 1008ac8a
           - handle_request [common/usb_pd_protocol.c:1191]
             - handle_data_request [common/usb_pd_protocol.c:798]
        -> pd_task [common/usb_pd_protocol.c:2672] 1008c222
        -> [annotation]
    pd_send_request_msg.lto_priv.263 (56) [common/usb_pd_protocol.c:653] 1009a0b4
        -> pd_send_request_msg.lto_priv.263 [common/usb_pd_protocol.c:712] 1009a22e0
```
The `pd_task` uses 160 bytes on the stack and calls `pd_send_request_msg.lto_priv.263`.

The callsites to the next function will be shown like below,
```
-> pd_task [common/usb_pd_protocol.c:1808] 1008ac8a
   - handle_request [common/usb_pd_protocol.c:1191]
     - handle_data_request [common/usb_pd_protocol.c:798]
-> pd_task [common/usb_pd_protocol.c:2672] 1008c222
```
This means one callsite to the next function is at `usb_pd_protocol.c:798`,
but it is inlined to the current function and you can follow the trace:
`usb_pd_protocol.c:1808 -> usb_pd_protocol.c:1191 -> usb_pd_protocol.c:798` to
find the callsite. The second callsite is at `usb_pd_protocol.c:2672`. And the
third one is added by annotation.
