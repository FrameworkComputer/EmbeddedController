#!/usr/bin/env python3
# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for Stack Analyzer classes and functions."""

# TODO: Clean up the lint errors and remove these disables.
# pylint:disable=invalid-name,missing-function-docstring,no-self-use,too-many-lines

import os
import subprocess
import unittest


try:
    from unittest import mock
except ImportError:
    import mock  # pylint:disable=import-error

import stack_analyzer as sa


class ObjectTest(unittest.TestCase):
    """Tests for classes of basic objects."""

    def testTask(self):
        task_a = sa.Task("a", "a_task", 1234)
        task_b = sa.Task("b", "b_task", 5678, 0x1000)
        self.assertEqual(task_a, task_a)
        self.assertNotEqual(task_a, task_b)
        self.assertNotEqual(task_a, None)

    def testSymbol(self):
        symbol_a = sa.Symbol(0x1234, "F", 32, "a")
        symbol_b = sa.Symbol(0x234, "O", 42, "b")
        self.assertEqual(symbol_a, symbol_a)
        self.assertNotEqual(symbol_a, symbol_b)
        self.assertNotEqual(symbol_a, None)

    def testCallsite(self):
        callsite_a = sa.Callsite(0x1002, 0x3000, False)
        callsite_b = sa.Callsite(0x1002, 0x3000, True)
        self.assertEqual(callsite_a, callsite_a)
        self.assertNotEqual(callsite_a, callsite_b)
        self.assertNotEqual(callsite_a, None)

    def testFunction(self):
        func_a = sa.Function(0x100, "a", 0, [])
        func_b = sa.Function(0x200, "b", 0, [])
        self.assertEqual(func_a, func_a)
        self.assertNotEqual(func_a, func_b)
        self.assertNotEqual(func_a, None)


class ArmAnalyzerTest(unittest.TestCase):
    """Tests for class ArmAnalyzer."""

    def AppendConditionCode(self, opcodes):
        rets = []
        for opcode in opcodes:
            rets.extend(opcode + cc for cc in sa.ArmAnalyzer.CONDITION_CODES)

        return rets

    def testInstructionMatching(self):
        jump_list = self.AppendConditionCode(["b", "bx"])
        jump_list += list(opcode + ".n" for opcode in jump_list) + list(
            opcode + ".w" for opcode in jump_list
        )
        for opcode in jump_list:
            self.assertIsNotNone(sa.ArmAnalyzer.JUMP_OPCODE_RE.match(opcode))

        self.assertIsNone(sa.ArmAnalyzer.JUMP_OPCODE_RE.match("bl"))
        self.assertIsNone(sa.ArmAnalyzer.JUMP_OPCODE_RE.match("blx"))

        cbz_list = ["cbz", "cbnz", "cbz.n", "cbnz.n", "cbz.w", "cbnz.w"]
        for opcode in cbz_list:
            self.assertIsNotNone(
                sa.ArmAnalyzer.CBZ_CBNZ_OPCODE_RE.match(opcode)
            )

        self.assertIsNone(sa.ArmAnalyzer.CBZ_CBNZ_OPCODE_RE.match("cbn"))

        call_list = self.AppendConditionCode(["bl", "blx"])
        call_list += list(opcode + ".n" for opcode in call_list)
        for opcode in call_list:
            self.assertIsNotNone(sa.ArmAnalyzer.CALL_OPCODE_RE.match(opcode))

        self.assertIsNone(sa.ArmAnalyzer.CALL_OPCODE_RE.match("ble"))

        result = sa.ArmAnalyzer.CALL_OPERAND_RE.match("53f90 <get_time+0x18>")
        self.assertIsNotNone(result)
        self.assertEqual(result.group(1), "53f90")
        self.assertEqual(result.group(2), "get_time+0x18")

        result = sa.ArmAnalyzer.CBZ_CBNZ_OPERAND_RE.match("r6, 53f90 <get+0x0>")
        self.assertIsNotNone(result)
        self.assertEqual(result.group(1), "53f90")
        self.assertEqual(result.group(2), "get+0x0")

        self.assertIsNotNone(sa.ArmAnalyzer.PUSH_OPCODE_RE.match("push"))
        self.assertIsNone(sa.ArmAnalyzer.PUSH_OPCODE_RE.match("pushal"))
        self.assertIsNotNone(sa.ArmAnalyzer.STM_OPCODE_RE.match("stmdb"))
        self.assertIsNone(sa.ArmAnalyzer.STM_OPCODE_RE.match("lstm"))
        self.assertIsNotNone(sa.ArmAnalyzer.SUB_OPCODE_RE.match("sub"))
        self.assertIsNotNone(sa.ArmAnalyzer.SUB_OPCODE_RE.match("subs"))
        self.assertIsNotNone(sa.ArmAnalyzer.SUB_OPCODE_RE.match("subw"))
        self.assertIsNotNone(sa.ArmAnalyzer.SUB_OPCODE_RE.match("sub.w"))
        self.assertIsNotNone(sa.ArmAnalyzer.SUB_OPCODE_RE.match("subs.w"))

        result = sa.ArmAnalyzer.SUB_OPERAND_RE.match("sp, sp, #1668   ; 0x684")
        self.assertIsNotNone(result)
        self.assertEqual(result.group(1), "1668")
        result = sa.ArmAnalyzer.SUB_OPERAND_RE.match("sp, #1668")
        self.assertIsNotNone(result)
        self.assertEqual(result.group(1), "1668")
        self.assertIsNone(sa.ArmAnalyzer.SUB_OPERAND_RE.match("sl, #1668"))

    def testAnalyzeFunction(self):
        analyzer = sa.ArmAnalyzer()
        symbol = sa.Symbol(0x10, "F", 0x100, "foo")
        instructions = [
            (0x10, "push", "{r4, r5, r6, r7, lr}"),
            (0x12, "subw", "sp, sp, #16	; 0x10"),
            (0x16, "movs", "lr, r1"),
            (0x18, "beq.n", "26 <foo+0x26>"),
            (0x1A, "bl", "30 <foo+0x30>"),
            (0x1E, "bl", "deadbeef <bar>"),
            (0x22, "blx", "0 <woo>"),
            (0x26, "push", "{r1}"),
            (0x28, "stmdb", "sp!, {r4, r5, r6, r7, r8, r9, lr}"),
            (0x2C, "stmdb", "sp!, {r4}"),
            (0x30, "stmdb", "sp, {r4}"),
            (0x34, "bx.n", "10 <foo>"),
            (0x36, "bx.n", "r3"),
            (0x38, "ldr", "pc, [r10]"),
        ]
        (size, callsites) = analyzer.AnalyzeFunction(symbol, instructions)
        self.assertEqual(size, 72)
        expect_callsites = [
            sa.Callsite(0x1E, 0xDEADBEEF, False),
            sa.Callsite(0x22, 0x0, False),
            sa.Callsite(0x34, 0x10, True),
            sa.Callsite(0x36, None, True),
            sa.Callsite(0x38, None, True),
        ]
        self.assertEqual(callsites, expect_callsites)


class StackAnalyzerTest(unittest.TestCase):
    """Tests for class StackAnalyzer."""

    def setUp(self):
        symbols = [
            sa.Symbol(0x1000, "F", 0x15C, "hook_task"),
            sa.Symbol(0x2000, "F", 0x51C, "console_task"),
            sa.Symbol(0x3200, "O", 0x124, "__just_data"),
            sa.Symbol(0x4000, "F", 0x11C, "touchpad_calc"),
            sa.Symbol(0x5000, "F", 0x12C, "touchpad_calc.constprop.42"),
            sa.Symbol(0x12000, "F", 0x13C, "trackpad_range"),
            sa.Symbol(0x13000, "F", 0x200, "inlined_mul"),
            sa.Symbol(0x13100, "F", 0x200, "inlined_mul"),
            sa.Symbol(0x13100, "F", 0x200, "inlined_mul_alias"),
            sa.Symbol(0x20000, "O", 0x0, "__array"),
            sa.Symbol(0x20010, "O", 0x0, "__array_end"),
        ]
        tasklist = [
            sa.Task("HOOKS", "hook_task", 2048, 0x1000),
            sa.Task("CONSOLE", "console_task", 460, 0x2000),
        ]
        # Array at 0x20000 that contains pointers to hook_task and console_task,
        # with stride=8, offset=4
        rodata = (0x20000, [0xDEAD1000, 0x00001000, 0xDEAD2000, 0x00002000])
        options = mock.MagicMock(
            elf_path="./ec.RW.elf",
            export_taskinfo="fake",
            section="RW",
            objdump="objdump",
            addr2line="addr2line",
            annotation=None,
        )
        self.analyzer = sa.StackAnalyzer(options, symbols, rodata, tasklist, {})

    def testParseSymbolText(self):
        symbol_text = (
            "0 g     F .text  e8 Foo\n"
            "0000dead  w    F .text  000000e8 .hidden Bar\n"
            "deadbeef l     O .bss   00000004 .hidden Woooo\n"
            "deadbee g     O .rodata        00000008 __Hooo_ooo\n"
            "deadbee g       .rodata        00000000 __foo_doo_coo_end\n"
        )
        symbols = sa.ParseSymbolText(symbol_text)
        expect_symbols = [
            sa.Symbol(0x0, "F", 0xE8, "Foo"),
            sa.Symbol(0xDEAD, "F", 0xE8, "Bar"),
            sa.Symbol(0xDEADBEEF, "O", 0x4, "Woooo"),
            sa.Symbol(0xDEADBEE, "O", 0x8, "__Hooo_ooo"),
            sa.Symbol(0xDEADBEE, "O", 0x0, "__foo_doo_coo_end"),
        ]
        self.assertEqual(symbols, expect_symbols)

    def testParseRoData(self):
        rodata_text = (
            "\n"
            "Contents of section .rodata:\n"
            " 20000 dead1000 00100000 dead2000 00200000  He..f.He..s.\n"
        )
        rodata = sa.ParseRoDataText(rodata_text)
        expect_rodata = (
            0x20000,
            [0x0010ADDE, 0x00001000, 0x0020ADDE, 0x00002000],
        )
        self.assertEqual(rodata, expect_rodata)

    def testLoadTasklist(self):
        def tasklist_to_taskinfos(pointer, tasklist):
            taskinfos = []
            for task in tasklist:
                taskinfos.append(
                    sa.TaskInfo(
                        name=task.name.encode("utf-8"),
                        routine=task.routine_name.encode("utf-8"),
                        stack_size=task.stack_max_size,
                    )
                )

            TaskInfoArray = sa.TaskInfo * len(taskinfos)
            pointer.contents.contents = TaskInfoArray(*taskinfos)
            return len(taskinfos)

        def ro_taskinfos(pointer):
            return tasklist_to_taskinfos(pointer, expect_ro_tasklist)

        def rw_taskinfos(pointer):
            return tasklist_to_taskinfos(pointer, expect_rw_tasklist)

        expect_ro_tasklist = [
            sa.Task("HOOKS", "hook_task", 2048, 0x1000),
        ]

        expect_rw_tasklist = [
            sa.Task("HOOKS", "hook_task", 2048, 0x1000),
            sa.Task("WOOKS", "hook_task", 4096, 0x1000),
            sa.Task("CONSOLE", "console_task", 460, 0x2000),
        ]

        export_taskinfo = mock.MagicMock(
            get_ro_taskinfos=mock.MagicMock(side_effect=ro_taskinfos),
            get_rw_taskinfos=mock.MagicMock(side_effect=rw_taskinfos),
        )

        tasklist = sa.LoadTasklist("RO", export_taskinfo, self.analyzer.symbols)
        self.assertEqual(tasklist, expect_ro_tasklist)
        tasklist = sa.LoadTasklist("RW", export_taskinfo, self.analyzer.symbols)
        self.assertEqual(tasklist, expect_rw_tasklist)

    def testResolveAnnotation(self):
        self.analyzer.annotation = {}
        (
            add_rules,
            remove_rules,
            invalid_sigtxts,
        ) = self.analyzer.LoadAnnotation()
        self.assertEqual(add_rules, {})
        self.assertEqual(remove_rules, [])
        self.assertEqual(invalid_sigtxts, set())

        self.analyzer.annotation = {"add": None, "remove": None}
        (
            add_rules,
            remove_rules,
            invalid_sigtxts,
        ) = self.analyzer.LoadAnnotation()
        self.assertEqual(add_rules, {})
        self.assertEqual(remove_rules, [])
        self.assertEqual(invalid_sigtxts, set())

        self.analyzer.annotation = {
            "add": None,
            "remove": [
                [["a", "b"], ["0", "[", "2"], "x"],
                [["a", "b[x:3]"], ["0", "1", "2"], "x"],
            ],
        }
        (
            add_rules,
            remove_rules,
            invalid_sigtxts,
        ) = self.analyzer.LoadAnnotation()
        self.assertEqual(add_rules, {})
        self.assertEqual(
            list.sort(remove_rules),
            list.sort(
                [
                    [("a", None, None), ("1", None, None), ("x", None, None)],
                    [("a", None, None), ("0", None, None), ("x", None, None)],
                    [("a", None, None), ("2", None, None), ("x", None, None)],
                    [
                        ("b", os.path.abspath("x"), 3),
                        ("1", None, None),
                        ("x", None, None),
                    ],
                    [
                        ("b", os.path.abspath("x"), 3),
                        ("0", None, None),
                        ("x", None, None),
                    ],
                    [
                        ("b", os.path.abspath("x"), 3),
                        ("2", None, None),
                        ("x", None, None),
                    ],
                ]
            ),
        )
        self.assertEqual(invalid_sigtxts, {"["})

        self.analyzer.annotation = {
            "add": {
                "touchpad_calc": [dict(name="__array", stride=8, offset=4)],
            }
        }
        (
            add_rules,
            remove_rules,
            invalid_sigtxts,
        ) = self.analyzer.LoadAnnotation()
        self.assertEqual(
            add_rules,
            {
                ("touchpad_calc", None, None): set(
                    [("console_task", None, None), ("hook_task", None, None)]
                )
            },
        )

        funcs = {
            0x1000: sa.Function(0x1000, "hook_task", 0, []),
            0x2000: sa.Function(0x2000, "console_task", 0, []),
            0x4000: sa.Function(0x4000, "touchpad_calc", 0, []),
            0x5000: sa.Function(0x5000, "touchpad_calc.constprop.42", 0, []),
            0x13000: sa.Function(0x13000, "inlined_mul", 0, []),
            0x13100: sa.Function(0x13100, "inlined_mul", 0, []),
        }
        funcs[0x1000].callsites = [sa.Callsite(0x1002, None, False, None)]
        # Set address_to_line_cache to fake the results of addr2line.
        self.analyzer.address_to_line_cache = {
            (0x1000, False): [("hook_task", os.path.abspath("a.c"), 10)],
            (0x1002, False): [("toot_calc", os.path.abspath("t.c"), 1234)],
            (0x2000, False): [("console_task", os.path.abspath("b.c"), 20)],
            (0x4000, False): [("toudhpad_calc", os.path.abspath("a.c"), 20)],
            (0x5000, False): [
                ("touchpad_calc.constprop.42", os.path.abspath("b.c"), 40)
            ],
            (0x12000, False): [("trackpad_range", os.path.abspath("t.c"), 10)],
            (0x13000, False): [("inlined_mul", os.path.abspath("x.c"), 12)],
            (0x13100, False): [("inlined_mul", os.path.abspath("x.c"), 12)],
        }
        self.analyzer.annotation = {
            "add": {
                "hook_task.lto.573": ["touchpad_calc.lto.2501[a.c]"],
                "console_task": ["touchpad_calc[b.c]", "inlined_mul_alias"],
                "hook_task[q.c]": ["hook_task"],
                "inlined_mul[x.c]": ["inlined_mul"],
                "toot_calc[t.c:1234]": ["hook_task"],
            },
            "remove": [
                ["touchpad?calc["],
                "touchpad_calc",
                ["touchpad_calc[a.c]"],
                ["task_unk[a.c]"],
                ["touchpad_calc[x/a.c]"],
                ["trackpad_range"],
                ["inlined_mul"],
                ["inlined_mul", "console_task", "touchpad_calc[a.c]"],
                ["inlined_mul", "inlined_mul_alias", "console_task"],
                ["inlined_mul", "inlined_mul_alias", "console_task"],
            ],
        }
        (
            add_rules,
            remove_rules,
            invalid_sigtxts,
        ) = self.analyzer.LoadAnnotation()
        self.assertEqual(invalid_sigtxts, {"touchpad?calc["})

        signature_set = set()
        for src_sig, dst_sigs in add_rules.items():
            signature_set.add(src_sig)
            signature_set.update(dst_sigs)

        for remove_sigs in remove_rules:
            signature_set.update(remove_sigs)

        (signature_map, failed_sigs) = self.analyzer.MapAnnotation(
            funcs, signature_set
        )
        result = self.analyzer.ResolveAnnotation(funcs)
        (add_set, remove_list, eliminated_addrs, failed_sigs) = result

        expect_signature_map = {
            ("hook_task", None, None): {funcs[0x1000]},
            ("touchpad_calc", os.path.abspath("a.c"), None): {funcs[0x4000]},
            ("touchpad_calc", os.path.abspath("b.c"), None): {funcs[0x5000]},
            ("console_task", None, None): {funcs[0x2000]},
            ("inlined_mul_alias", None, None): {funcs[0x13100]},
            ("inlined_mul", os.path.abspath("x.c"), None): {
                funcs[0x13000],
                funcs[0x13100],
            },
            ("inlined_mul", None, None): {funcs[0x13000], funcs[0x13100]},
        }
        self.assertEqual(len(signature_map), len(expect_signature_map))
        for sig, funclist in signature_map.items():
            self.assertEqual(set(funclist), expect_signature_map[sig])

        self.assertEqual(
            add_set,
            {
                (funcs[0x1000], funcs[0x4000]),
                (funcs[0x1000], funcs[0x1000]),
                (funcs[0x2000], funcs[0x5000]),
                (funcs[0x2000], funcs[0x13100]),
                (funcs[0x13000], funcs[0x13000]),
                (funcs[0x13000], funcs[0x13100]),
                (funcs[0x13100], funcs[0x13000]),
                (funcs[0x13100], funcs[0x13100]),
            },
        )
        expect_remove_list = [
            [funcs[0x4000]],
            [funcs[0x13000]],
            [funcs[0x13100]],
            [funcs[0x13000], funcs[0x2000], funcs[0x4000]],
            [funcs[0x13100], funcs[0x2000], funcs[0x4000]],
            [funcs[0x13000], funcs[0x13100], funcs[0x2000]],
            [funcs[0x13100], funcs[0x13100], funcs[0x2000]],
        ]
        self.assertEqual(len(remove_list), len(expect_remove_list))
        for remove_path in remove_list:
            self.assertTrue(remove_path in expect_remove_list)

        self.assertEqual(eliminated_addrs, {0x1002})
        self.assertEqual(
            failed_sigs,
            {
                ("touchpad?calc[", sa.StackAnalyzer.ANNOTATION_ERROR_INVALID),
                ("touchpad_calc", sa.StackAnalyzer.ANNOTATION_ERROR_AMBIGUOUS),
                ("hook_task[q.c]", sa.StackAnalyzer.ANNOTATION_ERROR_NOTFOUND),
                ("task_unk[a.c]", sa.StackAnalyzer.ANNOTATION_ERROR_NOTFOUND),
                (
                    "touchpad_calc[x/a.c]",
                    sa.StackAnalyzer.ANNOTATION_ERROR_NOTFOUND,
                ),
                ("trackpad_range", sa.StackAnalyzer.ANNOTATION_ERROR_NOTFOUND),
            },
        )

    def testPreprocessAnnotation(self):
        funcs = {
            0x1000: sa.Function(0x1000, "hook_task", 0, []),
            0x2000: sa.Function(0x2000, "console_task", 0, []),
            0x4000: sa.Function(0x4000, "touchpad_calc", 0, []),
        }
        funcs[0x1000].callsites = [
            sa.Callsite(0x1002, 0x1000, False, funcs[0x1000])
        ]
        funcs[0x2000].callsites = [
            sa.Callsite(0x2002, 0x1000, False, funcs[0x1000]),
            sa.Callsite(0x2006, None, True, None),
        ]
        add_set = {
            (funcs[0x2000], funcs[0x2000]),
            (funcs[0x2000], funcs[0x4000]),
            (funcs[0x4000], funcs[0x1000]),
            (funcs[0x4000], funcs[0x2000]),
        }
        remove_list = [
            [funcs[0x1000]],
            [funcs[0x2000], funcs[0x2000]],
            [funcs[0x4000], funcs[0x1000]],
            [funcs[0x2000], funcs[0x4000], funcs[0x2000]],
            [funcs[0x4000], funcs[0x1000], funcs[0x4000]],
        ]
        eliminated_addrs = {0x2006}

        remaining_remove_list = self.analyzer.PreprocessAnnotation(
            funcs, add_set, remove_list, eliminated_addrs
        )

        expect_funcs = {
            0x1000: sa.Function(0x1000, "hook_task", 0, []),
            0x2000: sa.Function(0x2000, "console_task", 0, []),
            0x4000: sa.Function(0x4000, "touchpad_calc", 0, []),
        }
        expect_funcs[0x2000].callsites = [
            sa.Callsite(None, 0x4000, False, expect_funcs[0x4000])
        ]
        expect_funcs[0x4000].callsites = [
            sa.Callsite(None, 0x2000, False, expect_funcs[0x2000])
        ]
        self.assertEqual(funcs, expect_funcs)
        self.assertEqual(
            remaining_remove_list,
            [
                [funcs[0x2000], funcs[0x4000], funcs[0x2000]],
            ],
        )

    def testAndesAnalyzeDisassembly(self):
        disasm_text = (
            "\n"
            "build/{BOARD}/RW/ec.RW.elf:     file format elf32-nds32le"
            "\n"
            "Disassembly of section .text:\n"
            "\n"
            "00000900 <wook_task>:\n"
            "   ...\n"
            "00001000 <hook_task>:\n"
            "   1000:   fc 42\tpush25 $r10, #16    ! {$r6~$r10, $fp, $gp, $lp}\n"
            "   1004:   47 70\t\tmovi55 $r0, #1\n"
            "   1006:   b1 13\tbnezs8 100929de <flash_command_write>\n"
            "   1008:   00 01 5c fc\tbne    $r6, $r0, 2af6a\n"
            "00002000 <console_task>:\n"
            "   2000:   fc 00\t\tpush25 $r6, #0    ! {$r6, $fp, $gp, $lp} \n"
            "   2002:   f0 0e fc c5\tjal   1000 <hook_task>\n"
            "   2006:   f0 0e bd 3b\tj  53968 <get_program_memory_addr>\n"
            "   200a:   de ad be ef\tswi.gp $r0, [ + #-11036]\n"
            "00004000 <touchpad_calc>:\n"
            "   4000:   47 70\t\tmovi55 $r0, #1\n"
            "00010000 <look_task>:"
        )
        function_map = self.analyzer.AnalyzeDisassembly(disasm_text)
        func_hook_task = sa.Function(
            0x1000,
            "hook_task",
            48,
            [sa.Callsite(0x1006, 0x100929DE, True, None)],
        )
        expect_funcmap = {
            0x1000: func_hook_task,
            0x2000: sa.Function(
                0x2000,
                "console_task",
                16,
                [
                    sa.Callsite(0x2002, 0x1000, False, func_hook_task),
                    sa.Callsite(0x2006, 0x53968, True, None),
                ],
            ),
            0x4000: sa.Function(0x4000, "touchpad_calc", 0, []),
        }
        self.assertEqual(function_map, expect_funcmap)

    def testArmAnalyzeDisassembly(self):
        disasm_text = (
            "\n"
            "build/{BOARD}/RW/ec.RW.elf:     file format elf32-littlearm"
            "\n"
            "Disassembly of section .text:\n"
            "\n"
            "00000900 <wook_task>:\n"
            "	...\n"
            "00001000 <hook_task>:\n"
            "   1000:	dead beef\tfake\n"
            "   1004:	4770\t\tbx	lr\n"
            "   1006:	b113\tcbz	r3, 100929de <flash_command_write>\n"
            "   1008:	00015cfc\t.word	0x00015cfc\n"
            "00002000 <console_task>:\n"
            "   2000:	b508\t\tpush	{r3, lr} ; malformed comments,; r0, r1 \n"
            "   2002:	f00e fcc5\tbl	1000 <hook_task>\n"
            "   2006:	f00e bd3b\tb.w	53968 <get_program_memory_addr>\n"
            "   200a:	dead beef\tfake\n"
            "00004000 <touchpad_calc>:\n"
            "   4000:	4770\t\tbx	lr\n"
            "00010000 <look_task>:"
        )
        function_map = self.analyzer.AnalyzeDisassembly(disasm_text)
        func_hook_task = sa.Function(
            0x1000,
            "hook_task",
            0,
            [sa.Callsite(0x1006, 0x100929DE, True, None)],
        )
        expect_funcmap = {
            0x1000: func_hook_task,
            0x2000: sa.Function(
                0x2000,
                "console_task",
                8,
                [
                    sa.Callsite(0x2002, 0x1000, False, func_hook_task),
                    sa.Callsite(0x2006, 0x53968, True, None),
                ],
            ),
            0x4000: sa.Function(0x4000, "touchpad_calc", 0, []),
        }
        self.assertEqual(function_map, expect_funcmap)

    def testAnalyzeCallGraph(self):
        funcs = {
            0x1000: sa.Function(0x1000, "hook_task", 0, []),
            0x2000: sa.Function(0x2000, "console_task", 8, []),
            0x3000: sa.Function(0x3000, "task_a", 12, []),
            0x4000: sa.Function(0x4000, "task_b", 96, []),
            0x5000: sa.Function(0x5000, "task_c", 32, []),
            0x6000: sa.Function(0x6000, "task_d", 100, []),
            0x7000: sa.Function(0x7000, "task_e", 24, []),
            0x8000: sa.Function(0x8000, "task_f", 20, []),
            0x9000: sa.Function(0x9000, "task_g", 20, []),
            0x10000: sa.Function(0x10000, "task_x", 16, []),
        }
        funcs[0x1000].callsites = [
            sa.Callsite(0x1002, 0x3000, False, funcs[0x3000]),
            sa.Callsite(0x1006, 0x4000, False, funcs[0x4000]),
        ]
        funcs[0x2000].callsites = [
            sa.Callsite(0x2002, 0x5000, False, funcs[0x5000]),
            sa.Callsite(0x2006, 0x2000, False, funcs[0x2000]),
            sa.Callsite(0x200A, 0x10000, False, funcs[0x10000]),
        ]
        funcs[0x3000].callsites = [
            sa.Callsite(0x3002, 0x4000, False, funcs[0x4000]),
            sa.Callsite(0x3006, 0x1000, False, funcs[0x1000]),
        ]
        funcs[0x4000].callsites = [
            sa.Callsite(0x4002, 0x6000, True, funcs[0x6000]),
            sa.Callsite(0x4006, 0x7000, False, funcs[0x7000]),
            sa.Callsite(0x400A, 0x8000, False, funcs[0x8000]),
        ]
        funcs[0x5000].callsites = [
            sa.Callsite(0x5002, 0x4000, False, funcs[0x4000])
        ]
        funcs[0x7000].callsites = [
            sa.Callsite(0x7002, 0x7000, False, funcs[0x7000])
        ]
        funcs[0x8000].callsites = [
            sa.Callsite(0x8002, 0x9000, False, funcs[0x9000])
        ]
        funcs[0x9000].callsites = [
            sa.Callsite(0x9002, 0x4000, False, funcs[0x4000])
        ]
        funcs[0x10000].callsites = [
            sa.Callsite(0x10002, 0x2000, False, funcs[0x2000])
        ]

        cycles = self.analyzer.AnalyzeCallGraph(
            funcs,
            [
                [funcs[0x2000]] * 2,
                [funcs[0x10000], funcs[0x2000]] * 3,
                [funcs[0x1000], funcs[0x3000], funcs[0x1000]],
            ],
        )

        expect_func_stack = {
            0x1000: (
                268,
                [
                    funcs[0x1000],
                    funcs[0x3000],
                    funcs[0x4000],
                    funcs[0x8000],
                    funcs[0x9000],
                    funcs[0x4000],
                    funcs[0x7000],
                ],
            ),
            0x2000: (
                208,
                [
                    funcs[0x2000],
                    funcs[0x10000],
                    funcs[0x2000],
                    funcs[0x10000],
                    funcs[0x2000],
                    funcs[0x5000],
                    funcs[0x4000],
                    funcs[0x7000],
                ],
            ),
            0x3000: (
                280,
                [
                    funcs[0x3000],
                    funcs[0x1000],
                    funcs[0x3000],
                    funcs[0x4000],
                    funcs[0x8000],
                    funcs[0x9000],
                    funcs[0x4000],
                    funcs[0x7000],
                ],
            ),
            0x4000: (120, [funcs[0x4000], funcs[0x7000]]),
            0x5000: (152, [funcs[0x5000], funcs[0x4000], funcs[0x7000]]),
            0x6000: (100, [funcs[0x6000]]),
            0x7000: (24, [funcs[0x7000]]),
            0x8000: (
                160,
                [funcs[0x8000], funcs[0x9000], funcs[0x4000], funcs[0x7000]],
            ),
            0x9000: (140, [funcs[0x9000], funcs[0x4000], funcs[0x7000]]),
            0x10000: (
                200,
                [
                    funcs[0x10000],
                    funcs[0x2000],
                    funcs[0x10000],
                    funcs[0x2000],
                    funcs[0x5000],
                    funcs[0x4000],
                    funcs[0x7000],
                ],
            ),
        }
        expect_cycles = [
            {funcs[0x4000], funcs[0x8000], funcs[0x9000]},
            {funcs[0x7000]},
        ]
        for func in funcs.values():
            (stack_max_usage, stack_max_path) = expect_func_stack[func.address]
            self.assertEqual(func.stack_max_usage, stack_max_usage)
            self.assertEqual(func.stack_max_path, stack_max_path)

        self.assertEqual(len(cycles), len(expect_cycles))
        for cycle in cycles:
            self.assertTrue(cycle in expect_cycles)

    @mock.patch("subprocess.check_output")
    def testAddressToLine(self, checkoutput_mock):
        checkoutput_mock.return_value = "fake_func\n/test.c:1"
        self.assertEqual(
            self.analyzer.AddressToLine(0x1234), [("fake_func", "/test.c", 1)]
        )
        checkoutput_mock.assert_called_once_with(
            ["addr2line", "-f", "-e", "./ec.RW.elf", "1234"], encoding="utf-8"
        )
        checkoutput_mock.reset_mock()

        checkoutput_mock.return_value = "fake_func\n/a.c:1\nbake_func\n/b.c:2\n"
        self.assertEqual(
            self.analyzer.AddressToLine(0x1234, True),
            [("fake_func", "/a.c", 1), ("bake_func", "/b.c", 2)],
        )
        checkoutput_mock.assert_called_once_with(
            ["addr2line", "-f", "-e", "./ec.RW.elf", "1234", "-i"],
            encoding="utf-8",
        )
        checkoutput_mock.reset_mock()

        checkoutput_mock.return_value = (
            "fake_func\n/test.c:1 (discriminator 128)"
        )
        self.assertEqual(
            self.analyzer.AddressToLine(0x12345), [("fake_func", "/test.c", 1)]
        )
        checkoutput_mock.assert_called_once_with(
            ["addr2line", "-f", "-e", "./ec.RW.elf", "12345"], encoding="utf-8"
        )
        checkoutput_mock.reset_mock()

        checkoutput_mock.return_value = "??\n:?\nbake_func\n/b.c:2\n"
        self.assertEqual(
            self.analyzer.AddressToLine(0x123456),
            [None, ("bake_func", "/b.c", 2)],
        )
        checkoutput_mock.assert_called_once_with(
            ["addr2line", "-f", "-e", "./ec.RW.elf", "123456"], encoding="utf-8"
        )
        checkoutput_mock.reset_mock()

        with self.assertRaisesRegex(
            sa.StackAnalyzerError, "addr2line failed to resolve lines."
        ):
            checkoutput_mock.side_effect = subprocess.CalledProcessError(1, "")
            self.analyzer.AddressToLine(0x5678)

        with self.assertRaisesRegex(
            sa.StackAnalyzerError, "Failed to run addr2line."
        ):
            checkoutput_mock.side_effect = OSError()
            self.analyzer.AddressToLine(0x9012)

    @mock.patch("subprocess.check_output")
    @mock.patch("stack_analyzer.StackAnalyzer.AddressToLine")
    def testAndesAnalyze(self, addrtoline_mock, checkoutput_mock):
        disasm_text = (
            "\n"
            "build/{BOARD}/RW/ec.RW.elf:     file format elf32-nds32le"
            "\n"
            "Disassembly of section .text:\n"
            "\n"
            "00000900 <wook_task>:\n"
            "   ...\n"
            "00001000 <hook_task>:\n"
            "   1000:   fc 00\t\tpush25 $r10, #16    ! {$r6~$r10, $fp, $gp, $lp}\n"
            "   1002:   47 70\t\tmovi55 $r0, #1\n"
            "   1006:   00 01 5c fc\tbne    $r6, $r0, 2af6a\n"
            "00002000 <console_task>:\n"
            "   2000:   fc 00\t\tpush25 $r6, #0    ! {$r6, $fp, $gp, $lp} \n"
            "   2002:   f0 0e fc c5\tjal   1000 <hook_task>\n"
            "   2006:   f0 0e bd 3b\tj  53968 <get_program_memory_addr>\n"
            "   200a:   12 34 56 78\tjral5 $r0\n"
        )

        addrtoline_mock.return_value = [("??", "??", 0)]
        self.analyzer.annotation = {
            "exception_frame_size": 64,
            "remove": [["fake_func"]],
        }

        with mock.patch("builtins.print") as print_mock:
            checkoutput_mock.return_value = disasm_text
            self.analyzer.Analyze()
            print_mock.assert_has_calls(
                [
                    mock.call(
                        "Task: HOOKS, Max size: 96 (32 + 64), Allocated size: 2048"
                    ),
                    mock.call("Call Trace:"),
                    mock.call("    hook_task (32) [??:0] 1000"),
                    mock.call(
                        "Task: CONSOLE, Max size: 112 (48 + 64), Allocated size: 460"
                    ),
                    mock.call("Call Trace:"),
                    mock.call("    console_task (16) [??:0] 2000"),
                    mock.call("        -> ??[??:0] 2002"),
                    mock.call("    hook_task (32) [??:0] 1000"),
                    mock.call("Unresolved indirect callsites:"),
                    mock.call("    In function console_task:"),
                    mock.call("        -> ??[??:0] 200a"),
                    mock.call("Unresolved annotation signatures:"),
                    mock.call("    fake_func: function is not found"),
                ]
            )

        with self.assertRaisesRegex(
            sa.StackAnalyzerError, "Failed to run objdump."
        ):
            checkoutput_mock.side_effect = OSError()
            self.analyzer.Analyze()

        with self.assertRaisesRegex(
            sa.StackAnalyzerError, "objdump failed to disassemble."
        ):
            checkoutput_mock.side_effect = subprocess.CalledProcessError(1, "")
            self.analyzer.Analyze()

    @mock.patch("subprocess.check_output")
    @mock.patch("stack_analyzer.StackAnalyzer.AddressToLine")
    def testArmAnalyze(self, addrtoline_mock, checkoutput_mock):
        disasm_text = (
            "\n"
            "build/{BOARD}/RW/ec.RW.elf:     file format elf32-littlearm"
            "\n"
            "Disassembly of section .text:\n"
            "\n"
            "00000900 <wook_task>:\n"
            "	...\n"
            "00001000 <hook_task>:\n"
            "   1000:	b508\t\tpush	{r3, lr}\n"
            "   1002:	4770\t\tbx	lr\n"
            "   1006:	00015cfc\t.word	0x00015cfc\n"
            "00002000 <console_task>:\n"
            "   2000:	b508\t\tpush	{r3, lr}\n"
            "   2002:	f00e fcc5\tbl	1000 <hook_task>\n"
            "   2006:	f00e bd3b\tb.w	53968 <get_program_memory_addr>\n"
            "   200a:	1234 5678\tb.w  sl\n"
        )

        addrtoline_mock.return_value = [("??", "??", 0)]
        self.analyzer.annotation = {
            "exception_frame_size": 64,
            "remove": [["fake_func"]],
        }

        with mock.patch("builtins.print") as print_mock:
            checkoutput_mock.return_value = disasm_text
            self.analyzer.Analyze()
            print_mock.assert_has_calls(
                [
                    mock.call(
                        "Task: HOOKS, Max size: 72 (8 + 64), Allocated size: 2048"
                    ),
                    mock.call("Call Trace:"),
                    mock.call("    hook_task (8) [??:0] 1000"),
                    mock.call(
                        "Task: CONSOLE, Max size: 80 (16 + 64), Allocated size: 460"
                    ),
                    mock.call("Call Trace:"),
                    mock.call("    console_task (8) [??:0] 2000"),
                    mock.call("        -> ??[??:0] 2002"),
                    mock.call("    hook_task (8) [??:0] 1000"),
                    mock.call("Unresolved indirect callsites:"),
                    mock.call("    In function console_task:"),
                    mock.call("        -> ??[??:0] 200a"),
                    mock.call("Unresolved annotation signatures:"),
                    mock.call("    fake_func: function is not found"),
                ]
            )

        with self.assertRaisesRegex(
            sa.StackAnalyzerError, "Failed to run objdump."
        ):
            checkoutput_mock.side_effect = OSError()
            self.analyzer.Analyze()

        with self.assertRaisesRegex(
            sa.StackAnalyzerError, "objdump failed to disassemble."
        ):
            checkoutput_mock.side_effect = subprocess.CalledProcessError(1, "")
            self.analyzer.Analyze()

    @mock.patch("subprocess.check_output")
    @mock.patch("stack_analyzer.ParseArgs")
    def testMain(
        self, parseargs_mock, checkoutput_mock
    ):  # pylint:disable=no-self-use,invalid-name
        symbol_text = (
            "1000 g     F .text  0000015c .hidden hook_task\n"
            "2000 g     F .text  0000051c .hidden console_task\n"
        )
        rodata_text = (
            "\n"
            "Contents of section .rodata:\n"
            " 20000 dead1000 00100000 dead2000 00200000  He..f.He..s.\n"
        )

        args = mock.MagicMock(
            elf_path="./ec.RW.elf",
            export_taskinfo="fake",
            section="RW",
            objdump="objdump",
            addr2line="addr2line",
            annotation="fake",
        )
        parseargs_mock.return_value = args

        with mock.patch("os.path.exists") as path_mock:
            path_mock.return_value = False
            with mock.patch("builtins.print") as print_mock:
                with mock.patch("builtins.open", mock.mock_open()) as open_mock:
                    sa.main()
                    print_mock.assert_any_call(
                        "Warning: Annotation file fake does not exist."
                    )

        with mock.patch("os.path.exists") as path_mock:
            path_mock.return_value = True
            with mock.patch("builtins.print") as print_mock:
                with mock.patch("builtins.open", mock.mock_open()) as open_mock:
                    open_mock.side_effect = IOError()
                    sa.main()
                    print_mock.assert_called_once_with(
                        "Error: Failed to open annotation file fake."
                    )

            with mock.patch("builtins.print") as print_mock:
                with mock.patch("builtins.open", mock.mock_open()) as open_mock:
                    open_mock.return_value.read.side_effect = ["{", ""]
                    sa.main()
                    open_mock.assert_called_once_with(
                        "fake", "r", encoding="utf-8"
                    )
                    print_mock.assert_called_once_with(
                        "Error: Failed to parse annotation file fake."
                    )

            with mock.patch("builtins.print") as print_mock:
                with mock.patch(
                    "builtins.open", mock.mock_open(read_data="")
                ) as open_mock:
                    sa.main()
                    print_mock.assert_called_once_with(
                        "Error: Invalid annotation file fake."
                    )

        args.annotation = None

        with mock.patch("builtins.print") as print_mock:
            checkoutput_mock.side_effect = [symbol_text, rodata_text]
            sa.main()
            print_mock.assert_called_once_with(
                "Error: Failed to load export_taskinfo."
            )

        with mock.patch("builtins.print") as print_mock:
            checkoutput_mock.side_effect = subprocess.CalledProcessError(1, "")
            sa.main()
            print_mock.assert_called_once_with(
                "Error: objdump failed to dump symbol table or rodata."
            )

        with mock.patch("builtins.print") as print_mock:
            checkoutput_mock.side_effect = OSError()
            sa.main()
            print_mock.assert_called_once_with("Error: Failed to run objdump.")


if __name__ == "__main__":
    unittest.main()
