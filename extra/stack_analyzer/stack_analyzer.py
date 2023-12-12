#!/usr/bin/env python3
# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Statically analyze stack usage of EC firmware.

  Example:
    extra/stack_analyzer/stack_analyzer.py \
        --export_taskinfo ./build/elm/util/export_taskinfo.so \
        --section RW \
        ./build/elm/RW/ec.RW.elf

"""

from __future__ import print_function

import argparse
import collections
import ctypes
import os
import re
import subprocess

import yaml  # pylint:disable=import-error


SECTION_RO = "RO"
SECTION_RW = "RW"
# Default size of extra stack frame needed by exception context switch.
# This value is for cortex-m with FPU enabled.
DEFAULT_EXCEPTION_FRAME_SIZE = 224


class StackAnalyzerError(Exception):
    """Exception class for stack analyzer utility."""


class TaskInfo(ctypes.Structure):
    """Taskinfo ctypes structure.

    The structure definition is corresponding to the "struct taskinfo"
    in "util/export_taskinfo.so.c".
    """

    _fields_ = [
        ("name", ctypes.c_char_p),
        ("routine", ctypes.c_char_p),
        ("stack_size", ctypes.c_uint32),
    ]


class Task(object):
    """Task information.

    Attributes:
      name: Task name.
      routine_name: Routine function name.
      stack_max_size: Max stack size.
      routine_address: Resolved routine address. None if it hasn't been resolved.
    """

    def __init__(
        self, name, routine_name, stack_max_size, routine_address=None
    ):
        """Constructor.

        Args:
          name: Task name.
          routine_name: Routine function name.
          stack_max_size: Max stack size.
          routine_address: Resolved routine address.
        """
        self.name = name
        self.routine_name = routine_name
        self.stack_max_size = stack_max_size
        self.routine_address = routine_address

    def __eq__(self, other):
        """Task equality.

        Args:
          other: The compared object.

        Returns:
          True if equal, False if not.
        """
        if not isinstance(other, Task):
            return False

        return (
            self.name == other.name
            and self.routine_name == other.routine_name
            and self.stack_max_size == other.stack_max_size
            and self.routine_address == other.routine_address
        )


class Symbol(object):
    """Symbol information.

    Attributes:
      address: Symbol address.
      symtype: Symbol type, 'O' (data, object) or 'F' (function).
      size: Symbol size.
      name: Symbol name.
    """

    def __init__(self, address, symtype, size, name):
        """Constructor.

        Args:
          address: Symbol address.
          symtype: Symbol type.
          size: Symbol size.
          name: Symbol name.
        """
        assert symtype in ["O", "F"]
        self.address = address
        self.symtype = symtype
        self.size = size
        self.name = name

    def __eq__(self, other):
        """Symbol equality.

        Args:
          other: The compared object.

        Returns:
          True if equal, False if not.
        """
        if not isinstance(other, Symbol):
            return False

        return (
            self.address == other.address
            and self.symtype == other.symtype
            and self.size == other.size
            and self.name == other.name
        )


class Callsite(object):
    """Function callsite.

    Attributes:
      address: Address of callsite location. None if it is unknown.
      target: Callee address. None if it is unknown.
      is_tail: A bool indicates that it is a tailing call.
      callee: Resolved callee function. None if it hasn't been resolved.
    """

    def __init__(self, address, target, is_tail, callee=None):
        """Constructor.

        Args:
          address: Address of callsite location. None if it is unknown.
          target: Callee address. None if it is unknown.
          is_tail: A bool indicates that it is a tailing call. (function jump to
                   another function without restoring the stack frame)
          callee: Resolved callee function.
        """
        # It makes no sense that both address and target are unknown.
        assert not (address is None and target is None)
        self.address = address
        self.target = target
        self.is_tail = is_tail
        self.callee = callee

    def __eq__(self, other):
        """Callsite equality.

        Args:
          other: The compared object.

        Returns:
          True if equal, False if not.
        """
        if not isinstance(other, Callsite):
            return False

        if not (
            self.address == other.address
            and self.target == other.target
            and self.is_tail == other.is_tail
        ):
            return False

        if self.callee is None:
            return other.callee is None
        elif other.callee is None:
            return False

        # Assume the addresses of functions are unique.
        return self.callee.address == other.callee.address


class Function(object):
    """Function.

    Attributes:
      address: Address of function.
      name: Name of function from its symbol.
      stack_frame: Size of stack frame.
      callsites: Callsite list.
      stack_max_usage: Max stack usage. None if it hasn't been analyzed.
      stack_max_path: Max stack usage path. None if it hasn't been analyzed.
    """

    def __init__(self, address, name, stack_frame, callsites):
        """Constructor.

        Args:
          address: Address of function.
          name: Name of function from its symbol.
          stack_frame: Size of stack frame.
          callsites: Callsite list.
        """
        self.address = address
        self.name = name
        self.stack_frame = stack_frame
        self.callsites = callsites
        self.stack_max_usage = None
        self.stack_max_path = None

    def __eq__(self, other):
        """Function equality.

        Args:
          other: The compared object.

        Returns:
          True if equal, False if not.
        """
        if not isinstance(other, Function):
            return False

        if not (
            self.address == other.address
            and self.name == other.name
            and self.stack_frame == other.stack_frame
            and self.callsites == other.callsites
            and self.stack_max_usage == other.stack_max_usage
        ):
            return False

        if self.stack_max_path is None:
            return other.stack_max_path is None
        elif other.stack_max_path is None:
            return False

        if len(self.stack_max_path) != len(other.stack_max_path):
            return False

        for self_func, other_func in zip(
            self.stack_max_path, other.stack_max_path
        ):
            # Assume the addresses of functions are unique.
            if self_func.address != other_func.address:
                return False

        return True

    def __hash__(self):
        return id(self)


class AndesAnalyzer(object):
    """Disassembly analyzer for Andes architecture.

    Public Methods:
      AnalyzeFunction: Analyze stack frame and callsites of the function.
    """

    GENERAL_PURPOSE_REGISTER_SIZE = 4

    # Possible condition code suffixes.
    CONDITION_CODES = [
        "eq",
        "eqz",
        "gez",
        "gtz",
        "lez",
        "ltz",
        "ne",
        "nez",
        "eqc",
        "nec",
        "nezs",
        "nes",
        "eqs",
    ]
    CONDITION_CODES_RE = "({})".format("|".join(CONDITION_CODES))

    IMM_ADDRESS_RE = r"([0-9A-Fa-f]+)\s+<([^>]+)>"
    # Branch instructions.
    JUMP_OPCODE_RE = re.compile(
        r"^(b{0}|j|jr|jr.|jrnez)(\d?|\d\d)$".format(CONDITION_CODES_RE)
    )
    # Call instructions.
    CALL_OPCODE_RE = re.compile(
        r"^(jal|jral|jral.|jralnez|beqzal|bltzal|bgezal)(\d)?$"
    )
    CALL_OPERAND_RE = re.compile(r"^{}$".format(IMM_ADDRESS_RE))
    # Ignore lp register because it's for return.
    INDIRECT_CALL_OPERAND_RE = re.compile(
        r"^\$r\d{1,}$|\$fp$|\$gp$|\$ta$|\$sp$|\$pc$"
    )
    # TODO: Handle other kinds of store instructions.
    PUSH_OPCODE_RE = re.compile(r"^push(\d{1,})$")
    PUSH_OPERAND_RE = re.compile(r"^\$r\d{1,}, \#\d{1,}    \! \{([^\]]+)\}")
    SMW_OPCODE_RE = re.compile(r"^smw(\.\w\w|\.\w\w\w)$")
    SMW_OPERAND_RE = re.compile(
        r"^(\$r\d{1,}|\$\wp), \[\$\wp\], "
        r"(\$r\d{1,}|\$\wp), \#\d\w\d    \! \{([^\]]+)\}"
    )
    OPERANDGROUP_RE = re.compile(r"^\$r\d{1,}\~\$r\d{1,}")

    LWI_OPCODE_RE = re.compile(r"^lwi(\.\w\w)$")
    LWI_PC_OPERAND_RE = re.compile(r"^\$pc, \[([^\]]+)\]")
    # Example: "34280:  3f c8 0f ec   addi.gp $fp, #0xfec"
    # Assume there is always a "\t" after the hex data.
    DISASM_REGEX_RE = re.compile(
        r"^(?P<address>[0-9A-Fa-f]+):\s+"
        r"(?P<words>[0-9A-Fa-f ]+)"
        r"\t\s*(?P<opcode>\S+)(\s+(?P<operand>[^;]*))?"
    )

    def ParseInstruction(self, line, function_end):
        """Parse the line of instruction.

        Args:
          line: Text of disassembly.
          function_end: End address of the current function. None if unknown.

        Returns:
          (address, words, opcode, operand_text): The instruction address, words,
                                            opcode, and the text of operands.
                                            None if it isn't an instruction line.
        """
        result = self.DISASM_REGEX_RE.match(line)
        if result is None:
            return None

        address = int(result.group("address"), 16)
        # Check if it's out of bound.
        if function_end is not None and address >= function_end:
            return None

        opcode = result.group("opcode").strip()
        operand_text = result.group("operand")
        words = result.group("words")
        if operand_text is None:
            operand_text = ""
        else:
            operand_text = operand_text.strip()

        return (address, words, opcode, operand_text)

    def AnalyzeFunction(self, function_symbol, instructions):
        stack_frame = 0
        callsites = []
        for address, words, opcode, operand_text in instructions:
            is_jump_opcode = self.JUMP_OPCODE_RE.match(opcode) is not None
            is_call_opcode = self.CALL_OPCODE_RE.match(opcode) is not None

            if is_jump_opcode or is_call_opcode:
                is_tail = is_jump_opcode

                result = self.CALL_OPERAND_RE.match(operand_text)

                if result is None:
                    if (
                        self.INDIRECT_CALL_OPERAND_RE.match(operand_text)
                        is not None
                    ):
                        # Found an indirect call.
                        callsites.append(Callsite(address, None, is_tail))

                else:
                    target_address = int(result.group(1), 16)
                    # Filter out the in-function target (branches and in-function calls,
                    # which are actually branches).
                    if not (
                        function_symbol.size > 0
                        and function_symbol.address
                        < target_address
                        < (function_symbol.address + function_symbol.size)
                    ):
                        # Maybe it is a callsite.
                        callsites.append(
                            Callsite(address, target_address, is_tail)
                        )

            elif self.LWI_OPCODE_RE.match(opcode) is not None:
                result = self.LWI_PC_OPERAND_RE.match(operand_text)
                if result is not None:
                    # Ignore "lwi $pc, [$sp], xx" because it's usually a return.
                    if result.group(1) != "$sp":
                        # Found an indirect call.
                        callsites.append(Callsite(address, None, True))

            elif self.PUSH_OPCODE_RE.match(opcode) is not None:
                # Example: fc 20    push25 $r8, #0    ! {$r6~$r8, $fp, $gp, $lp}
                if self.PUSH_OPERAND_RE.match(operand_text) is not None:
                    # capture fc 20
                    imm5u = int(words.split(" ")[1], 16)
                    # sp = sp - (imm5u << 3)
                    imm8u = (imm5u << 3) & 0xFF
                    stack_frame += imm8u

                    result = self.PUSH_OPERAND_RE.match(operand_text)
                    operandgroup_text = result.group(1)
                    # capture $rx~$ry
                    if (
                        self.OPERANDGROUP_RE.match(operandgroup_text)
                        is not None
                    ):
                        # capture number & transfer string to integer
                        oprandgrouphead = operandgroup_text.split(",")[0]
                        rx = int(
                            "".join(
                                filter(
                                    str.isdigit, oprandgrouphead.split("~")[0]
                                )
                            )
                        )
                        ry = int(
                            "".join(
                                filter(
                                    str.isdigit, oprandgrouphead.split("~")[1]
                                )
                            )
                        )

                        stack_frame += (
                            len(operandgroup_text.split(",")) + ry - rx
                        ) * self.GENERAL_PURPOSE_REGISTER_SIZE
                    else:
                        stack_frame += (
                            len(operandgroup_text.split(","))
                            * self.GENERAL_PURPOSE_REGISTER_SIZE
                        )

            elif self.SMW_OPCODE_RE.match(opcode) is not None:
                # Example: smw.adm $r6, [$sp], $r10, #0x2    ! {$r6~$r10, $lp}
                if self.SMW_OPERAND_RE.match(operand_text) is not None:
                    result = self.SMW_OPERAND_RE.match(operand_text)
                    operandgroup_text = result.group(3)
                    # capture $rx~$ry
                    if (
                        self.OPERANDGROUP_RE.match(operandgroup_text)
                        is not None
                    ):
                        # capture number & transfer string to integer
                        oprandgrouphead = operandgroup_text.split(",")[0]
                        rx = int(
                            "".join(
                                filter(
                                    str.isdigit, oprandgrouphead.split("~")[0]
                                )
                            )
                        )
                        ry = int(
                            "".join(
                                filter(
                                    str.isdigit, oprandgrouphead.split("~")[1]
                                )
                            )
                        )

                        stack_frame += (
                            len(operandgroup_text.split(",")) + ry - rx
                        ) * self.GENERAL_PURPOSE_REGISTER_SIZE
                    else:
                        stack_frame += (
                            len(operandgroup_text.split(","))
                            * self.GENERAL_PURPOSE_REGISTER_SIZE
                        )

        return (stack_frame, callsites)


class ArmAnalyzer(object):
    """Disassembly analyzer for ARM architecture.

    Public Methods:
      AnalyzeFunction: Analyze stack frame and callsites of the function.
    """

    GENERAL_PURPOSE_REGISTER_SIZE = 4

    # Possible condition code suffixes.
    CONDITION_CODES = [
        "",
        "eq",
        "ne",
        "cs",
        "hs",
        "cc",
        "lo",
        "mi",
        "pl",
        "vs",
        "vc",
        "hi",
        "ls",
        "ge",
        "lt",
        "gt",
        "le",
    ]
    CONDITION_CODES_RE = "({})".format("|".join(CONDITION_CODES))
    # Assume there is no function name containing ">".
    IMM_ADDRESS_RE = r"([0-9A-Fa-f]+)\s+<([^>]+)>"

    # Fuzzy regular expressions for instruction and operand parsing.
    # Branch instructions.
    JUMP_OPCODE_RE = re.compile(
        r"^(b{0}|bx{0})(\.\w)?$".format(CONDITION_CODES_RE)
    )
    # Call instructions.
    CALL_OPCODE_RE = re.compile(
        r"^(bl{0}|blx{0})(\.\w)?$".format(CONDITION_CODES_RE)
    )
    CALL_OPERAND_RE = re.compile(r"^{}$".format(IMM_ADDRESS_RE))
    CBZ_CBNZ_OPCODE_RE = re.compile(r"^(cbz|cbnz)(\.\w)?$")
    # Example: "r0, 1009bcbe <host_cmd_motion_sense+0x1d2>"
    CBZ_CBNZ_OPERAND_RE = re.compile(r"^[^,]+,\s+{}$".format(IMM_ADDRESS_RE))
    # Ignore lr register because it's for return.
    INDIRECT_CALL_OPERAND_RE = re.compile(r"^r\d+|sb|sl|fp|ip|sp|pc$")
    # TODO(cheyuw): Handle conditional versions of following
    #               instructions.
    # TODO(cheyuw): Handle other kinds of pc modifying instructions (e.g. mov pc).
    LDR_OPCODE_RE = re.compile(r"^ldr(\.\w)?$")
    # Example: "pc, [sp], #4"
    LDR_PC_OPERAND_RE = re.compile(r"^pc, \[([^\]]+)\]")
    # TODO(cheyuw): Handle other kinds of stm instructions.
    PUSH_OPCODE_RE = re.compile(r"^push$")
    STM_OPCODE_RE = re.compile(r"^stmdb$")
    # Stack subtraction instructions.
    SUB_OPCODE_RE = re.compile(r"^sub(s|w)?(\.\w)?$")
    SUB_OPERAND_RE = re.compile(r"^sp[^#]+#(\d+)")
    # Example: "44d94:  f893 0068   ldrb.w  r0, [r3, #104]  ; 0x68"
    # Assume there is always a "\t" after the hex data.
    DISASM_REGEX_RE = re.compile(
        r"^(?P<address>[0-9A-Fa-f]+):\s+[0-9A-Fa-f ]+"
        r"\t\s*(?P<opcode>\S+)(\s+(?P<operand>[^;]*))?"
    )

    def ParseInstruction(self, line, function_end):
        """Parse the line of instruction.

        Args:
          line: Text of disassembly.
          function_end: End address of the current function. None if unknown.

        Returns:
          (address, opcode, operand_text): The instruction address, opcode,
                                           and the text of operands. None if it
                                           isn't an instruction line.
        """
        result = self.DISASM_REGEX_RE.match(line)
        if result is None:
            return None

        address = int(result.group("address"), 16)
        # Check if it's out of bound.
        if function_end is not None and address >= function_end:
            return None

        opcode = result.group("opcode").strip()
        operand_text = result.group("operand")
        if operand_text is None:
            operand_text = ""
        else:
            operand_text = operand_text.strip()

        return (address, opcode, operand_text)

    def AnalyzeFunction(self, function_symbol, instructions):
        """Analyze function, resolve the size of stack frame and callsites.

        Args:
          function_symbol: Function symbol.
          instructions: Instruction list.

        Returns:
          (stack_frame, callsites): Size of stack frame, callsite list.
        """
        stack_frame = 0
        callsites = []
        for address, opcode, operand_text in instructions:
            is_jump_opcode = self.JUMP_OPCODE_RE.match(opcode) is not None
            is_call_opcode = self.CALL_OPCODE_RE.match(opcode) is not None
            is_cbz_cbnz_opcode = (
                self.CBZ_CBNZ_OPCODE_RE.match(opcode) is not None
            )
            if is_jump_opcode or is_call_opcode or is_cbz_cbnz_opcode:
                is_tail = is_jump_opcode or is_cbz_cbnz_opcode

                if is_cbz_cbnz_opcode:
                    result = self.CBZ_CBNZ_OPERAND_RE.match(operand_text)
                else:
                    result = self.CALL_OPERAND_RE.match(operand_text)

                if result is None:
                    # Failed to match immediate address, maybe it is an indirect call.
                    # CBZ and CBNZ can't be indirect calls.
                    if (
                        not is_cbz_cbnz_opcode
                        and self.INDIRECT_CALL_OPERAND_RE.match(operand_text)
                        is not None
                    ):
                        # Found an indirect call.
                        callsites.append(Callsite(address, None, is_tail))

                else:
                    target_address = int(result.group(1), 16)
                    # Filter out the in-function target (branches and in-function calls,
                    # which are actually branches).
                    if not (
                        function_symbol.size > 0
                        and function_symbol.address
                        < target_address
                        < (function_symbol.address + function_symbol.size)
                    ):
                        # Maybe it is a callsite.
                        callsites.append(
                            Callsite(address, target_address, is_tail)
                        )

            elif self.LDR_OPCODE_RE.match(opcode) is not None:
                result = self.LDR_PC_OPERAND_RE.match(operand_text)
                if result is not None:
                    # Ignore "ldr pc, [sp], xx" because it's usually a return.
                    if result.group(1) != "sp":
                        # Found an indirect call.
                        callsites.append(Callsite(address, None, True))

            elif self.PUSH_OPCODE_RE.match(opcode) is not None:
                # Example: "{r4, r5, r6, r7, lr}"
                stack_frame += (
                    len(operand_text.split(","))
                    * self.GENERAL_PURPOSE_REGISTER_SIZE
                )
            elif self.SUB_OPCODE_RE.match(opcode) is not None:
                result = self.SUB_OPERAND_RE.match(operand_text)
                if result is not None:
                    stack_frame += int(result.group(1))
                else:
                    # Unhandled stack register subtraction.
                    assert not operand_text.startswith("sp")

            elif self.STM_OPCODE_RE.match(opcode) is not None:
                if operand_text.startswith("sp!"):
                    # Subtract and writeback to stack register.
                    # Example: "sp!, {r4, r5, r6, r7, r8, r9, lr}"
                    # Get the text of pushed register list.
                    (
                        unused_sp,
                        unused_sep,
                        parameter_text,
                    ) = operand_text.partition(",")
                    stack_frame += (
                        len(parameter_text.split(","))
                        * self.GENERAL_PURPOSE_REGISTER_SIZE
                    )

        return (stack_frame, callsites)


class RiscvAnalyzer(object):
    """Disassembly analyzer for RISC-V architecture.

    Public Methods:
      AnalyzeFunction: Analyze stack frame and callsites of the function.
    """

    # Possible condition code suffixes.
    CONDITION_CODES = [
        "eqz",
        "nez",
        "lez",
        "gez",
        "ltz",
        "gtz",
        "gt",
        "le",
        "gtu",
        "leu",
        "eq",
        "ne",
        "ge",
        "lt",
        "ltu",
        "geu",
    ]
    CONDITION_CODES_RE = "({})".format("|".join(CONDITION_CODES))
    # Branch instructions.
    JUMP_OPCODE_RE = re.compile(r"^(b{0}|j|jr)$".format(CONDITION_CODES_RE))
    # Call instructions.
    CALL_OPCODE_RE = re.compile(r"^(jal|jalr)$")
    # Example: "j		8009b318 <set_state_prl_hr>" or
    #          "jal	ra,800a4394 <power_get_signals>" or
    #          "bltu	t0,t1,80080300 <data_loop>"
    JUMP_ADDRESS_RE = r"((\w(\w|\d\d),){0,2})([0-9A-Fa-f]+)\s+<([^>]+)>"
    CALL_OPERAND_RE = re.compile(r"^{}$".format(JUMP_ADDRESS_RE))
    # Capture address, Example:  800a4394
    CAPTURE_ADDRESS = re.compile(r"[0-9A-Fa-f]{8}")
    # Indirect jump, Example: jalr	a5
    INDIRECT_CALL_OPERAND_RE = re.compile(r"^t\d+|s\d+|a\d+$")
    # Example:  addi
    ADDI_OPCODE_RE = re.compile(r"^addi$")
    # Allocate stack instructions.
    ADDI_OPERAND_RE = re.compile(r"^(sp,sp,-\d+)$")
    # Example: "800804b6:	1101                	addi	sp,sp,-32"
    DISASM_REGEX_RE = re.compile(
        r"^(?P<address>[0-9A-Fa-f]+):\s+[0-9A-Fa-f ]+"
        r"\t\s*(?P<opcode>\S+)(\s+(?P<operand>[^;]*))?"
    )

    def ParseInstruction(self, line, function_end):
        """Parse the line of instruction.

        Args:
          line: Text of disassembly.
          function_end: End address of the current function. None if unknown.

        Returns:
          (address, opcode, operand_text):  The instruction address, opcode,
                                            and the text of operands. None if it
                                            isn't an instruction line.
        """
        result = self.DISASM_REGEX_RE.match(line)
        if result is None:
            return None

        address = int(result.group("address"), 16)
        # Check if it's out of bound.
        if function_end is not None and address >= function_end:
            return None

        opcode = result.group("opcode").strip()
        operand_text = result.group("operand")
        if operand_text is None:
            operand_text = ""
        else:
            operand_text = operand_text.strip()

        return (address, opcode, operand_text)

    def AnalyzeFunction(self, function_symbol, instructions):
        stack_frame = 0
        callsites = []
        for address, opcode, operand_text in instructions:
            is_jump_opcode = self.JUMP_OPCODE_RE.match(opcode) is not None
            is_call_opcode = self.CALL_OPCODE_RE.match(opcode) is not None

            if is_jump_opcode or is_call_opcode:
                is_tail = is_jump_opcode

                result = self.CALL_OPERAND_RE.match(operand_text)
                if result is None:
                    if (
                        self.INDIRECT_CALL_OPERAND_RE.match(operand_text)
                        is not None
                    ):
                        # Found an indirect call.
                        callsites.append(Callsite(address, None, is_tail))

                else:
                    # Capture address form operand_text and then convert to string
                    address_str = "".join(
                        self.CAPTURE_ADDRESS.findall(operand_text)
                    )
                    # String to integer
                    target_address = int(address_str, 16)
                    # Filter out the in-function target (branches and in-function calls,
                    # which are actually branches).
                    if not (
                        function_symbol.size > 0
                        and function_symbol.address
                        < target_address
                        < (function_symbol.address + function_symbol.size)
                    ):
                        # Maybe it is a callsite.
                        callsites.append(
                            Callsite(address, target_address, is_tail)
                        )

            elif self.ADDI_OPCODE_RE.match(opcode) is not None:
                # Example: sp,sp,-32
                if self.ADDI_OPERAND_RE.match(operand_text) is not None:
                    stack_frame += abs(int(operand_text.split(",")[2]))

        return (stack_frame, callsites)


class StackAnalyzer(object):
    """Class to analyze stack usage.

    Public Methods:
      Analyze: Run the stack analysis.
    """

    C_FUNCTION_NAME = r"_A-Za-z0-9"

    # Assume there is no ":" in the path.
    # Example: "driver/accel_kionix.c:321 (discriminator 3)"
    ADDRTOLINE_RE = re.compile(
        r"^(?P<path>[^:]+):(?P<linenum>\d+)(\s+\(discriminator\s+\d+\))?$"
    )
    # To eliminate the suffix appended by compilers, try to extract the
    # C function name from the prefix of symbol name.
    # Example: "SHA256_transform.constprop.28"
    FUNCTION_PREFIX_NAME_RE = re.compile(
        r"^(?P<name>[{0}]+)([^{0}].*)?$".format(C_FUNCTION_NAME)
    )

    # Errors of annotation resolving.
    ANNOTATION_ERROR_INVALID = "invalid signature"
    ANNOTATION_ERROR_NOTFOUND = "function is not found"
    ANNOTATION_ERROR_AMBIGUOUS = "signature is ambiguous"

    def __init__(self, options, symbols, rodata, tasklist, annotation):
        """Constructor.

        Args:
          options: Namespace from argparse.parse_args().
          symbols: Symbol list.
          rodata: Content of .rodata section (offset, data)
          tasklist: Task list.
          annotation: Annotation config.
        """
        self.options = options
        self.symbols = symbols
        self.rodata_offset = rodata[0]
        self.rodata = rodata[1]
        self.tasklist = tasklist
        self.annotation = annotation
        self.address_to_line_cache = {}

    def AddressToLine(self, address, resolve_inline=False):
        """Convert address to line.

        Args:
          address: Target address.
          resolve_inline: Output the stack of inlining.

        Returns:
          lines: List of the corresponding lines.

        Raises:
          StackAnalyzerError: If addr2line is failed.
        """
        cache_key = (address, resolve_inline)
        if cache_key in self.address_to_line_cache:
            return self.address_to_line_cache[cache_key]

        try:
            args = [
                self.options.addr2line,
                "-f",
                "-e",
                self.options.elf_path,
                "{:x}".format(address),
            ]
            if resolve_inline:
                args.append("-i")

            line_text = subprocess.check_output(args, encoding="utf-8")
        except subprocess.CalledProcessError:
            raise StackAnalyzerError("addr2line failed to resolve lines.")
        except OSError:
            raise StackAnalyzerError("Failed to run addr2line.")

        lines = [line.strip() for line in line_text.splitlines()]
        # Assume the output has at least one pair like "function\nlocation\n", and
        # they always show up in pairs.
        # Example: "handle_request\n
        #           common/usb_pd_protocol.c:1191\n"
        assert len(lines) >= 2 and len(lines) % 2 == 0

        line_infos = []
        for index in range(0, len(lines), 2):
            (function_name, line_text) = lines[index : index + 2]
            if line_text in ["??:0", ":?"]:
                line_infos.append(None)
            else:
                result = self.ADDRTOLINE_RE.match(line_text)
                # Assume the output is always well-formed.
                assert result is not None
                line_infos.append(
                    (
                        function_name.strip(),
                        os.path.realpath(result.group("path").strip()),
                        int(result.group("linenum")),
                    )
                )

        self.address_to_line_cache[cache_key] = line_infos
        return line_infos

    def AnalyzeDisassembly(self, disasm_text):
        """Parse the disassembly text, analyze, and build a map of all functions.

        Args:
          disasm_text: Disassembly text.

        Returns:
          function_map: Dict of functions.
        """
        disasm_lines = [line.strip() for line in disasm_text.splitlines()]

        if "nds" in disasm_lines[1]:
            analyzer = AndesAnalyzer()
        elif "arm" in disasm_lines[1]:
            analyzer = ArmAnalyzer()
        elif "riscv" in disasm_lines[1]:
            analyzer = RiscvAnalyzer()
        else:
            raise StackAnalyzerError("Unsupported architecture.")

        # Example: "08028c8c <motion_lid_calc>:"
        function_signature_regex = re.compile(
            r"^(?P<address>[0-9A-Fa-f]+)\s+<(?P<name>[^>]+)>:$"
        )

        def DetectFunctionHead(line):
            """Check if the line is a function head.

            Args:
              line: Text of disassembly.

            Returns:
              symbol: Function symbol. None if it isn't a function head.
            """
            result = function_signature_regex.match(line)
            if result is None:
                return None

            address = int(result.group("address"), 16)
            symbol = symbol_map.get(address)

            # Check if the function exists and matches.
            if symbol is None or symbol.symtype != "F":
                return None

            return symbol

        # Build symbol map, indexed by symbol address.
        symbol_map = {}
        for symbol in self.symbols:
            # If there are multiple symbols with same address, keeping any of them is
            # good enough.
            symbol_map[symbol.address] = symbol

        # Parse the disassembly text. We update the variable "line" to next line
        # when needed. There are two steps of parser:
        #
        # Step 1: Searching for the function head. Once reach the function head,
        # move to the next line, which is the first line of function body.
        #
        # Step 2: Parsing each instruction line of function body. Once reach a
        # non-instruction line, stop parsing and analyze the parsed instructions.
        #
        # Finally turn back to the step 1 without updating the line, because the
        # current non-instruction line can be another function head.
        function_map = {}
        # The following three variables are the states of the parsing processing.
        # They will be initialized properly during the state changes.
        function_symbol = None
        function_end = None
        instructions = []

        # Remove heading and tailing spaces for each line.
        line_index = 0
        while line_index < len(disasm_lines):
            # Get the current line.
            line = disasm_lines[line_index]

            if function_symbol is None:
                # Step 1: Search for the function head.

                function_symbol = DetectFunctionHead(line)
                if function_symbol is not None:
                    # Assume there is no empty function. If the function head is followed
                    # by EOF, it is an empty function.
                    assert line_index + 1 < len(disasm_lines)

                    # Found the function head, initialize and turn to the step 2.
                    instructions = []
                    # If symbol size exists, use it as a hint of function size.
                    if function_symbol.size > 0:
                        function_end = (
                            function_symbol.address + function_symbol.size
                        )
                    else:
                        function_end = None

            else:
                # Step 2: Parse the function body.

                instruction = analyzer.ParseInstruction(line, function_end)
                if instruction is not None:
                    instructions.append(instruction)

                if instruction is None or line_index + 1 == len(disasm_lines):
                    # Either the invalid instruction or EOF indicates the end of the
                    # function, finalize the function analysis.

                    # Assume there is no empty function.
                    assert len(instructions) > 0

                    (stack_frame, callsites) = analyzer.AnalyzeFunction(
                        function_symbol, instructions
                    )
                    # Assume the function addresses are unique in the disassembly.
                    assert function_symbol.address not in function_map
                    function_map[function_symbol.address] = Function(
                        function_symbol.address,
                        function_symbol.name,
                        stack_frame,
                        callsites,
                    )

                    # Initialize and turn back to the step 1.
                    function_symbol = None

                    # If the current line isn't an instruction, it can be another function
                    # head, skip moving to the next line.
                    if instruction is None:
                        continue

            # Move to the next line.
            line_index += 1

        # Resolve callees of functions.
        for function in function_map.values():
            for callsite in function.callsites:
                if callsite.target is not None:
                    # Remain the callee as None if we can't resolve it.
                    callsite.callee = function_map.get(callsite.target)

        return function_map

    def MapAnnotation(self, function_map, signature_set):
        """Map annotation signatures to functions.

        Args:
          function_map: Function map.
          signature_set: Set of annotation signatures.

        Returns:
          Map of signatures to functions, map of signatures which can't be resolved.
        """
        # Build the symbol map indexed by symbol name. If there are multiple symbols
        # with the same name, add them into a set. (e.g. symbols of static function
        # with the same name)
        symbol_map = collections.defaultdict(set)
        for symbol in self.symbols:
            if symbol.symtype == "F":
                # Function symbol.
                result = self.FUNCTION_PREFIX_NAME_RE.match(symbol.name)
                if result is not None:
                    function = function_map.get(symbol.address)
                    # Ignore the symbol not in disassembly.
                    if function is not None:
                        # If there are multiple symbol with the same name and point to the
                        # same function, the set will deduplicate them.
                        symbol_map[result.group("name").strip()].add(function)

        # Build the signature map indexed by annotation signature.
        signature_map = {}
        sig_error_map = {}
        symbol_path_map = {}
        for sig in signature_set:
            (name, path, _) = sig

            functions = symbol_map.get(name)
            if functions is None:
                sig_error_map[sig] = self.ANNOTATION_ERROR_NOTFOUND
                continue

            if name not in symbol_path_map:
                # Lazy symbol path resolving. Since the addr2line isn't fast, only
                # resolve needed symbol paths.
                group_map = collections.defaultdict(list)
                for function in functions:
                    line_info = self.AddressToLine(function.address)[0]
                    if line_info is None:
                        continue

                    (_, symbol_path, _) = line_info

                    # Group the functions with the same symbol signature (symbol name +
                    # symbol path). Assume they are the same copies and do the same
                    # annotation operations of them because we don't know which copy is
                    # indicated by the users.
                    group_map[symbol_path].append(function)

                symbol_path_map[name] = group_map

            # Symbol matching.
            function_group = None
            group_map = symbol_path_map[name]
            if len(group_map) > 0:
                if path is None:
                    if len(group_map) > 1:
                        # There is ambiguity but the path isn't specified.
                        sig_error_map[sig] = self.ANNOTATION_ERROR_AMBIGUOUS
                        continue

                    # No path signature but all symbol signatures of functions are same.
                    # Assume they are the same functions, so there is no ambiguity.
                    (function_group,) = group_map.values()
                else:
                    function_group = group_map.get(path)

            if function_group is None:
                sig_error_map[sig] = self.ANNOTATION_ERROR_NOTFOUND
                continue

            # The function_group is a list of all the same functions (according to
            # our assumption) which should be annotated together.
            signature_map[sig] = function_group

        return (signature_map, sig_error_map)

    def LoadAnnotation(self):
        """Load annotation rules.

        Returns:
          Map of add rules, set of remove rules, set of text signatures which can't
          be parsed.
        """
        # Assume there is no ":" in the path.
        # Example: "get_range.lto.2501[driver/accel_kionix.c:327]"
        annotation_signature_regex = re.compile(
            r"^(?P<name>[^\[]+)(\[(?P<path>[^:]+)(:(?P<linenum>\d+))?\])?$"
        )

        def NormalizeSignature(signature_text):
            """Parse and normalize the annotation signature.

            Args:
              signature_text: Text of the annotation signature.

            Returns:
              (function name, path, line number) of the signature. The path and line
              number can be None if not exist. None if failed to parse.
            """
            result = annotation_signature_regex.match(signature_text.strip())
            if result is None:
                return None

            name_result = self.FUNCTION_PREFIX_NAME_RE.match(
                result.group("name").strip()
            )
            if name_result is None:
                return None

            path = result.group("path")
            if path is not None:
                path = os.path.realpath(path.strip())

            linenum = result.group("linenum")
            if linenum is not None:
                linenum = int(linenum.strip())

            return (name_result.group("name").strip(), path, linenum)

        def ExpandArray(dic):
            """Parse and expand a symbol array

            Args:
              dic: Dictionary for the array annotation

            Returns:
              array of (symbol name, None, None).
            """
            # TODO(drinkcat): This function is quite inefficient, as it goes through
            # the symbol table multiple times.

            begin_name = dic["name"]
            end_name = dic["name"] + "_end"
            offset = dic["offset"] if "offset" in dic else 0
            stride = dic["stride"]

            begin_address = None
            end_address = None

            for symbol in self.symbols:
                if symbol.name == begin_name:
                    begin_address = symbol.address
                if symbol.name == end_name:
                    end_address = symbol.address

            if not begin_address or not end_address:
                return None

            output = []
            # TODO(drinkcat): This is inefficient as we go from address to symbol
            # object then to symbol name, and later on we'll go back from symbol name
            # to symbol object.
            for addr in range(begin_address + offset, end_address, stride):
                # TODO(drinkcat): Not all architectures need to drop the first bit.
                val = self.rodata[(addr - self.rodata_offset) // 4] & 0xFFFFFFFE
                name = None
                for symbol in self.symbols:
                    if symbol.address == val:
                        result = self.FUNCTION_PREFIX_NAME_RE.match(symbol.name)
                        name = result.group("name")
                        break

                if not name:
                    raise StackAnalyzerError(
                        "Cannot find function for address %s." % hex(val)
                    )

                output.append((name, None, None))

            return output

        add_rules = collections.defaultdict(set)
        remove_rules = list()
        invalid_sigtxts = set()

        if "add" in self.annotation and self.annotation["add"] is not None:
            for src_sigtxt, dst_sigtxts in self.annotation["add"].items():
                src_sig = NormalizeSignature(src_sigtxt)
                if src_sig is None:
                    invalid_sigtxts.add(src_sigtxt)
                    continue

                for dst_sigtxt in dst_sigtxts:
                    if isinstance(dst_sigtxt, dict):
                        dst_sig = ExpandArray(dst_sigtxt)
                        if dst_sig is None:
                            invalid_sigtxts.add(str(dst_sigtxt))
                        else:
                            add_rules[src_sig].update(dst_sig)
                    else:
                        dst_sig = NormalizeSignature(dst_sigtxt)
                        if dst_sig is None:
                            invalid_sigtxts.add(dst_sigtxt)
                        else:
                            add_rules[src_sig].add(dst_sig)

        if (
            "remove" in self.annotation
            and self.annotation["remove"] is not None
        ):
            for sigtxt_path in self.annotation["remove"]:
                if isinstance(sigtxt_path, str):
                    # The path has only one vertex.
                    sigtxt_path = [sigtxt_path]

                if len(sigtxt_path) == 0:
                    continue

                # Generate multiple remove paths from all the combinations of the
                # signatures of each vertex.
                sig_paths = [[]]
                broken_flag = False
                for sigtxt_node in sigtxt_path:
                    if isinstance(sigtxt_node, str):
                        # The vertex has only one signature.
                        sigtxt_set = {sigtxt_node}
                    elif isinstance(sigtxt_node, list):
                        # The vertex has multiple signatures.
                        sigtxt_set = set(sigtxt_node)
                    else:
                        # Assume the format of annotation is verified. There should be no
                        # invalid case.
                        assert False

                    sig_set = set()
                    for sigtxt in sigtxt_set:
                        sig = NormalizeSignature(sigtxt)
                        if sig is None:
                            invalid_sigtxts.add(sigtxt)
                            broken_flag = True
                        elif not broken_flag:
                            sig_set.add(sig)

                    if broken_flag:
                        continue

                    # Append each signature of the current node to the all previous
                    # remove paths.
                    sig_paths = [
                        path + [sig] for path in sig_paths for sig in sig_set
                    ]

                if not broken_flag:
                    # All signatures are normalized. The remove path has no error.
                    remove_rules.extend(sig_paths)

        return (add_rules, remove_rules, invalid_sigtxts)

    def ResolveAnnotation(self, function_map):
        """Resolve annotation.

        Args:
          function_map: Function map.

        Returns:
          Set of added call edges, list of remove paths, set of eliminated
          callsite addresses, set of annotation signatures which can't be resolved.
        """

        def StringifySignature(signature):
            """Stringify the tupled signature.

            Args:
              signature: Tupled signature.

            Returns:
              Signature string.
            """
            (name, path, linenum) = signature
            bracket_text = ""
            if path is not None:
                path = os.path.relpath(path)
                if linenum is None:
                    bracket_text = "[{}]".format(path)
                else:
                    bracket_text = "[{}:{}]".format(path, linenum)

            return name + bracket_text

        (add_rules, remove_rules, invalid_sigtxts) = self.LoadAnnotation()

        signature_set = set()
        for src_sig, dst_sigs in add_rules.items():
            signature_set.add(src_sig)
            signature_set.update(dst_sigs)

        for remove_sigs in remove_rules:
            signature_set.update(remove_sigs)

        # Map signatures to functions.
        (signature_map, sig_error_map) = self.MapAnnotation(
            function_map, signature_set
        )

        # Build the indirect callsite map indexed by callsite signature.
        indirect_map = collections.defaultdict(set)
        for function in function_map.values():
            for callsite in function.callsites:
                if callsite.target is not None:
                    continue

                # Found an indirect callsite.
                line_info = self.AddressToLine(callsite.address)[0]
                if line_info is None:
                    continue

                (name, path, linenum) = line_info
                result = self.FUNCTION_PREFIX_NAME_RE.match(name)
                if result is None:
                    continue

                indirect_map[(result.group("name").strip(), path, linenum)].add(
                    (function, callsite.address)
                )

        # Generate the annotation sets.
        add_set = set()
        remove_list = list()
        eliminated_addrs = set()

        for src_sig, dst_sigs in add_rules.items():
            src_funcs = set(signature_map.get(src_sig, []))
            # Try to match the source signature to the indirect callsites. Even if it
            # can't be found in disassembly.
            indirect_calls = indirect_map.get(src_sig)
            if indirect_calls is not None:
                for function, callsite_address in indirect_calls:
                    # Add the caller of the indirect callsite to the source functions.
                    src_funcs.add(function)
                    # Assume each callsite can be represented by a unique address.
                    eliminated_addrs.add(callsite_address)

                if src_sig in sig_error_map:
                    # Assume the error is always the not found error. Since the signature
                    # found in indirect callsite map must be a full signature, it can't
                    # happen the ambiguous error.
                    assert (
                        sig_error_map[src_sig] == self.ANNOTATION_ERROR_NOTFOUND
                    )
                    # Found in inline stack, remove the not found error.
                    del sig_error_map[src_sig]

            for dst_sig in dst_sigs:
                dst_funcs = signature_map.get(dst_sig)
                if dst_funcs is None:
                    continue

                # Duplicate the call edge for all the same source and destination
                # functions.
                for src_func in src_funcs:
                    for dst_func in dst_funcs:
                        add_set.add((src_func, dst_func))

        for remove_sigs in remove_rules:
            # Since each signature can be mapped to multiple functions, generate
            # multiple remove paths from all the combinations of these functions.
            remove_paths = [[]]
            skip_flag = False
            for remove_sig in remove_sigs:
                # Transform each signature to the corresponding functions.
                remove_funcs = signature_map.get(remove_sig)
                if remove_funcs is None:
                    # There is an unresolved signature in the remove path. Ignore the
                    # whole broken remove path.
                    skip_flag = True
                    break
                else:
                    # Append each function of the current signature to the all previous
                    # remove paths.
                    remove_paths = [
                        p + [f] for p in remove_paths for f in remove_funcs
                    ]

            if skip_flag:
                # Ignore the broken remove path.
                continue

            for remove_path in remove_paths:
                # Deduplicate the remove paths.
                if remove_path not in remove_list:
                    remove_list.append(remove_path)

        # Format the error messages.
        failed_sigtxts = set()
        for sigtxt in invalid_sigtxts:
            failed_sigtxts.add((sigtxt, self.ANNOTATION_ERROR_INVALID))

        for sig, error in sig_error_map.items():
            failed_sigtxts.add((StringifySignature(sig), error))

        return (add_set, remove_list, eliminated_addrs, failed_sigtxts)

    def PreprocessAnnotation(
        self, function_map, add_set, remove_list, eliminated_addrs
    ):
        """Preprocess the annotation and callgraph.

        Add the missing call edges, and delete simple remove paths (the paths have
        one or two vertices) from the function_map.

        Eliminate the annotated indirect callsites.

        Return the remaining remove list.

        Args:
          function_map: Function map.
          add_set: Set of missing call edges.
          remove_list: List of remove paths.
          eliminated_addrs: Set of eliminated callsite addresses.

        Returns:
          List of remaining remove paths.
        """

        def CheckEdge(path):
            """Check if all edges of the path are on the callgraph.

            Args:
              path: Path.

            Returns:
              True or False.
            """
            for index in range(len(path) - 1):
                if (path[index], path[index + 1]) not in edge_set:
                    return False

            return True

        for src_func, dst_func in add_set:
            # TODO(cheyuw): Support tailing call annotation.
            src_func.callsites.append(
                Callsite(None, dst_func.address, False, dst_func)
            )

        # Delete simple remove paths.
        remove_simple = set(tuple(p) for p in remove_list if len(p) <= 2)
        edge_set = set()
        for function in function_map.values():
            cleaned_callsites = []
            for callsite in function.callsites:
                if (callsite.callee,) in remove_simple or (
                    function,
                    callsite.callee,
                ) in remove_simple:
                    continue

                if (
                    callsite.target is None
                    and callsite.address in eliminated_addrs
                ):
                    continue

                cleaned_callsites.append(callsite)
                if callsite.callee is not None:
                    edge_set.add((function, callsite.callee))

            function.callsites = cleaned_callsites

        return [p for p in remove_list if len(p) >= 3 and CheckEdge(p)]

    def AnalyzeCallGraph(self, function_map, remove_list):
        """Analyze callgraph.

        It will update the max stack size and path for each function.

        Args:
          function_map: Function map.
          remove_list: List of remove paths.

        Returns:
          List of function cycles.
        """

        def Traverse(curr_state):
            """Traverse the callgraph and calculate the max stack usages of functions.

            Args:
              curr_state: Current state.

            Returns:
              SCC lowest link.
            """
            scc_index = scc_index_counter[0]
            scc_index_counter[0] += 1
            scc_index_map[curr_state] = scc_index
            scc_lowlink = scc_index
            scc_stack.append(curr_state)
            # Push the current state in the stack. We can use a set to maintain this
            # because the stacked states are unique; otherwise we will find a cycle
            # first.
            stacked_states.add(curr_state)

            (curr_address, curr_positions) = curr_state
            curr_func = function_map[curr_address]

            invalid_flag = False
            new_positions = list(curr_positions)
            for index, position in enumerate(curr_positions):
                remove_path = remove_list[index]

                # The position of each remove path in the state is the length of the
                # longest matching path between the prefix of the remove path and the
                # suffix of the current traversing path. We maintain this length when
                # appending the next callee to the traversing path. And it can be used
                # to check if the remove path appears in the traversing path.

                # TODO(cheyuw): Implement KMP algorithm to match remove paths
                #               efficiently.
                if remove_path[position] is curr_func:
                    # Matches the current function, extend the length.
                    new_positions[index] = position + 1
                    if new_positions[index] == len(remove_path):
                        # The length of the longest matching path is equal to the length of
                        # the remove path, which means the suffix of the current traversing
                        # path matches the remove path.
                        invalid_flag = True
                        break

                else:
                    # We can't get the new longest matching path by extending the previous
                    # one directly. Fallback to search the new longest matching path.

                    # If we can't find any matching path in the following search, reset
                    # the matching length to 0.
                    new_positions[index] = 0

                    # We want to find the new longest matching prefix of remove path with
                    # the suffix of the current traversing path. Because the new longest
                    # matching path won't be longer than the prevous one now, and part of
                    # the suffix matches the prefix of remove path, we can get the needed
                    # suffix from the previous matching prefix of the invalid path.
                    suffix = remove_path[:position] + [curr_func]
                    for offset in range(1, len(suffix)):
                        length = position - offset
                        if remove_path[:length] == suffix[offset:]:
                            new_positions[index] = length
                            break

            new_positions = tuple(new_positions)

            # If the current suffix is invalid, set the max stack usage to 0.
            max_stack_usage = 0
            max_callee_state = None
            self_loop = False

            if not invalid_flag:
                # Max stack usage is at least equal to the stack frame.
                max_stack_usage = curr_func.stack_frame
                for callsite in curr_func.callsites:
                    callee = callsite.callee
                    if callee is None:
                        continue

                    callee_state = (callee.address, new_positions)
                    if callee_state not in scc_index_map:
                        # Unvisited state.
                        scc_lowlink = min(scc_lowlink, Traverse(callee_state))
                    elif callee_state in stacked_states:
                        # The state is shown in the stack. There is a cycle.
                        sub_stack_usage = 0
                        scc_lowlink = min(
                            scc_lowlink, scc_index_map[callee_state]
                        )
                        if callee_state == curr_state:
                            self_loop = True

                    done_result = done_states.get(callee_state)
                    if done_result is not None:
                        # Already done this state and use its result. If the state reaches a
                        # cycle, reusing the result will cause inaccuracy (the stack usage
                        # of cycle depends on where the entrance is). But it's fine since we
                        # can't get accurate stack usage under this situation, and we rely
                        # on user-provided annotations to break the cycle, after which the
                        # result will be accurate again.
                        (sub_stack_usage, _) = done_result

                        if callsite.is_tail:
                            # For tailing call, since the callee reuses the stack frame of the
                            # caller, choose the larger one directly.
                            stack_usage = max(
                                curr_func.stack_frame, sub_stack_usage
                            )
                        else:
                            stack_usage = (
                                curr_func.stack_frame + sub_stack_usage
                            )

                        if stack_usage > max_stack_usage:
                            max_stack_usage = stack_usage
                            max_callee_state = callee_state

            if scc_lowlink == scc_index:
                group = []
                while scc_stack[-1] != curr_state:
                    scc_state = scc_stack.pop()
                    stacked_states.remove(scc_state)
                    group.append(scc_state)

                scc_stack.pop()
                stacked_states.remove(curr_state)

                # If the cycle is not empty, record it.
                if len(group) > 0 or self_loop:
                    group.append(curr_state)
                    cycle_groups.append(group)

            # Store the done result.
            done_states[curr_state] = (max_stack_usage, max_callee_state)

            if curr_positions == initial_positions:
                # If the current state is initial state, we traversed the callgraph by
                # using the current function as start point. Update the stack usage of
                # the function.
                # If the function matches a single vertex remove path, this will set its
                # max stack usage to 0, which is not expected (we still calculate its
                # max stack usage, but prevent any function from calling it). However,
                # all the single vertex remove paths have been preprocessed and removed.
                curr_func.stack_max_usage = max_stack_usage

                # Reconstruct the max stack path by traversing the state transitions.
                max_stack_path = [curr_func]
                callee_state = max_callee_state
                while callee_state is not None:
                    # The first element of state tuple is function address.
                    max_stack_path.append(function_map[callee_state[0]])
                    done_result = done_states.get(callee_state)
                    # All of the descendants should be done.
                    assert done_result is not None
                    (_, callee_state) = done_result

                curr_func.stack_max_path = max_stack_path

            return scc_lowlink

        # The state is the concatenation of the current function address and the
        # state of matching position.
        initial_positions = (0,) * len(remove_list)
        done_states = {}
        stacked_states = set()
        scc_index_counter = [0]
        scc_index_map = {}
        scc_stack = []
        cycle_groups = []
        for function in function_map.values():
            if function.stack_max_usage is None:
                Traverse((function.address, initial_positions))

        cycle_functions = []
        for group in cycle_groups:
            cycle = set(function_map[state[0]] for state in group)
            if cycle not in cycle_functions:
                cycle_functions.append(cycle)

        return cycle_functions

    def Analyze(self):
        """Run the stack analysis.

        Raises:
          StackAnalyzerError: If disassembly fails.
        """

        def OutputInlineStack(address, prefix=""):
            """Output beautiful inline stack.

            Args:
              address: Address.
              prefix: Prefix of each line.

            Returns:
              Key for sorting, output text
            """
            line_infos = self.AddressToLine(address, True)

            if line_infos[0] is None:
                order_key = (None, None)
            else:
                (_, path, linenum) = line_infos[0]
                order_key = (linenum, path)

            line_texts = []
            for line_info in reversed(line_infos):
                if line_info is None:
                    (function_name, path, linenum) = ("??", "??", 0)
                else:
                    (function_name, path, linenum) = line_info

                line_texts.append(
                    "{}[{}:{}]".format(
                        function_name, os.path.relpath(path), linenum
                    )
                )

            output = "{}-> {} {:x}\n".format(prefix, line_texts[0], address)
            for depth, line_text in enumerate(line_texts[1:]):
                output += "{}   {}- {}\n".format(
                    prefix, "  " * depth, line_text
                )

            # Remove the last newline character.
            return (order_key, output.rstrip("\n"))

        # Analyze disassembly.
        try:
            disasm_text = subprocess.check_output(
                [self.options.objdump, "-d", self.options.elf_path],
                encoding="utf-8",
            )
        except subprocess.CalledProcessError:
            raise StackAnalyzerError("objdump failed to disassemble.")
        except OSError:
            raise StackAnalyzerError("Failed to run objdump.")

        function_map = self.AnalyzeDisassembly(disasm_text)
        result = self.ResolveAnnotation(function_map)
        (add_set, remove_list, eliminated_addrs, failed_sigtxts) = result
        remove_list = self.PreprocessAnnotation(
            function_map, add_set, remove_list, eliminated_addrs
        )
        cycle_functions = self.AnalyzeCallGraph(function_map, remove_list)

        # Print the results of task-aware stack analysis.
        extra_stack_frame = self.annotation.get(
            "exception_frame_size", DEFAULT_EXCEPTION_FRAME_SIZE
        )
        for task in self.tasklist:
            routine_func = function_map[task.routine_address]
            print(
                "Task: {}, Max size: {} ({} + {}), Allocated size: {}".format(
                    task.name,
                    routine_func.stack_max_usage + extra_stack_frame,
                    routine_func.stack_max_usage,
                    extra_stack_frame,
                    task.stack_max_size,
                )
            )

            print("Call Trace:")
            max_stack_path = routine_func.stack_max_path
            # Assume the routine function is resolved.
            assert max_stack_path is not None
            for depth, curr_func in enumerate(max_stack_path):
                line_info = self.AddressToLine(curr_func.address)[0]
                if line_info is None:
                    (path, linenum) = ("??", 0)
                else:
                    (_, path, linenum) = line_info

                print(
                    "    {} ({}) [{}:{}] {:x}".format(
                        curr_func.name,
                        curr_func.stack_frame,
                        os.path.relpath(path),
                        linenum,
                        curr_func.address,
                    )
                )

                if depth + 1 < len(max_stack_path):
                    succ_func = max_stack_path[depth + 1]
                    text_list = []
                    for callsite in curr_func.callsites:
                        if callsite.callee is succ_func:
                            indent_prefix = "        "
                            if callsite.address is None:
                                order_text = (
                                    None,
                                    "{}-> [annotation]".format(indent_prefix),
                                )
                            else:
                                order_text = OutputInlineStack(
                                    callsite.address, indent_prefix
                                )

                            text_list.append(order_text)

                    for _, text in sorted(text_list, key=lambda item: item[0]):
                        print(text)

        print("Unresolved indirect callsites:")
        for function in function_map.values():
            indirect_callsites = []
            for callsite in function.callsites:
                if callsite.target is None:
                    indirect_callsites.append(callsite.address)

            if len(indirect_callsites) > 0:
                print("    In function {}:".format(function.name))
                text_list = []
                for address in indirect_callsites:
                    text_list.append(OutputInlineStack(address, "        "))

                for _, text in sorted(text_list, key=lambda item: item[0]):
                    print(text)

        print("Unresolved annotation signatures:")
        for sigtxt, error in failed_sigtxts:
            print("    {}: {}".format(sigtxt, error))

        if len(cycle_functions) > 0:
            print("There are cycles in the following function sets:")
            for functions in cycle_functions:
                print(
                    "[{}]".format(
                        ", ".join(function.name for function in functions)
                    )
                )


def ParseArgs():
    """Parse commandline arguments.

    Returns:
      options: Namespace from argparse.parse_args().
    """
    parser = argparse.ArgumentParser(description="EC firmware stack analyzer.")
    parser.add_argument("elf_path", help="the path of EC firmware ELF")
    parser.add_argument(
        "--export_taskinfo",
        required=True,
        help="the path of export_taskinfo.so utility",
    )
    parser.add_argument(
        "--section",
        required=True,
        help="the section.",
        choices=[SECTION_RO, SECTION_RW],
    )
    parser.add_argument(
        "--objdump", default="objdump", help="the path of objdump"
    )
    parser.add_argument(
        "--addr2line", default="addr2line", help="the path of addr2line"
    )
    parser.add_argument(
        "--annotation", default=None, help="the path of annotation file"
    )

    # TODO(cheyuw): Add an option for dumping stack usage of all functions.

    return parser.parse_args()


def ParseSymbolText(symbol_text):
    """Parse the content of the symbol text.

    Args:
      symbol_text: Text of the symbols.

    Returns:
      symbols: Symbol list.
    """
    # Example: "10093064 g     F .text  0000015c .hidden hook_task"
    symbol_regex = re.compile(
        r"^(?P<address>[0-9A-Fa-f]+)\s+[lwg]\s+"
        r"((?P<type>[OF])\s+)?\S+\s+"
        r"(?P<size>[0-9A-Fa-f]+)\s+"
        r"(\S+\s+)?(?P<name>\S+)$"
    )

    symbols = []
    for line in symbol_text.splitlines():
        line = line.strip()
        result = symbol_regex.match(line)
        if result is not None:
            address = int(result.group("address"), 16)
            symtype = result.group("type")
            if symtype is None:
                symtype = "O"

            size = int(result.group("size"), 16)
            name = result.group("name")
            symbols.append(Symbol(address, symtype, size, name))

    return symbols


def ParseRoDataText(rodata_text):
    """Parse the content of rodata

    Args:
      symbol_text: Text of the rodata dump.

    Returns:
      symbols: Symbol list.
    """
    # Examples: 8018ab0 00040048 00010000 10020000 4b8e0108  ...H........K...
    #           100a7294 00000000 00000000 01000000           ............

    base_offset = None
    offset = None
    rodata = []
    for line in rodata_text.splitlines():
        line = line.strip()
        space = line.find(" ")
        if space < 0:
            continue
        try:
            address = int(line[0:space], 16)
        except ValueError:
            continue

        if not base_offset:
            base_offset = address
            offset = address
        elif address != offset:
            raise StackAnalyzerError("objdump of rodata not contiguous.")

        for i in range(0, 4):
            num = line[(space + 1 + i * 9) : (space + 9 + i * 9)]
            if len(num.strip()) > 0:
                val = int(num, 16)
            else:
                val = 0
            # TODO(drinkcat): Not all platforms are necessarily big-endian
            rodata.append(
                (val & 0x000000FF) << 24
                | (val & 0x0000FF00) << 8
                | (val & 0x00FF0000) >> 8
                | (val & 0xFF000000) >> 24
            )

        offset = offset + 4 * 4

    return (base_offset, rodata)


def LoadTasklist(section, export_taskinfo, symbols):
    """Load the task information.

    Args:
      section: Section (RO | RW).
      export_taskinfo: Handle of export_taskinfo.so.
      symbols: Symbol list.

    Returns:
      tasklist: Task list.
    """

    TaskInfoPointer = ctypes.POINTER(TaskInfo)
    taskinfos = TaskInfoPointer()
    if section == SECTION_RO:
        get_taskinfos_func = export_taskinfo.get_ro_taskinfos
    else:
        get_taskinfos_func = export_taskinfo.get_rw_taskinfos

    taskinfo_num = get_taskinfos_func(ctypes.pointer(taskinfos))

    tasklist = []
    for index in range(taskinfo_num):
        taskinfo = taskinfos[index]
        tasklist.append(
            Task(
                taskinfo.name.decode("utf-8"),
                taskinfo.routine.decode("utf-8"),
                taskinfo.stack_size,
            )
        )

    # Resolve routine address for each task. It's more efficient to resolve all
    # routine addresses of tasks together.
    routine_map = dict((task.routine_name, None) for task in tasklist)

    for symbol in symbols:
        # Resolve task routine address.
        if symbol.name in routine_map:
            # Assume the symbol of routine is unique.
            assert routine_map[symbol.name] is None
            routine_map[symbol.name] = symbol.address

    for task in tasklist:
        address = routine_map[task.routine_name]
        # Assume we have resolved all routine addresses.
        assert address is not None
        task.routine_address = address

    return tasklist


def main():
    """Main function."""
    try:
        options = ParseArgs()

        # Load annotation config.
        if options.annotation is None:
            annotation = {}
        elif not os.path.exists(options.annotation):
            print(
                "Warning: Annotation file {} does not exist.".format(
                    options.annotation
                )
            )
            annotation = {}
        else:
            try:
                with open(options.annotation, "r") as annotation_file:
                    annotation = yaml.safe_load(annotation_file)

            except yaml.YAMLError:
                raise StackAnalyzerError(
                    "Failed to parse annotation file {}.".format(
                        options.annotation
                    )
                )
            except IOError:
                raise StackAnalyzerError(
                    "Failed to open annotation file {}.".format(
                        options.annotation
                    )
                )

            # TODO(cheyuw): Do complete annotation format verification.
            if not isinstance(annotation, dict):
                raise StackAnalyzerError(
                    "Invalid annotation file {}.".format(options.annotation)
                )

        # Generate and parse the symbols.
        try:
            symbol_text = subprocess.check_output(
                [options.objdump, "-t", options.elf_path], encoding="utf-8"
            )
            rodata_text = subprocess.check_output(
                [options.objdump, "-s", "-j", ".rodata", options.elf_path],
                encoding="utf-8",
            )
        except subprocess.CalledProcessError:
            raise StackAnalyzerError(
                "objdump failed to dump symbol table or rodata."
            )
        except OSError:
            raise StackAnalyzerError("Failed to run objdump.")

        symbols = ParseSymbolText(symbol_text)
        rodata = ParseRoDataText(rodata_text)

        # Load the tasklist.
        try:
            export_taskinfo = ctypes.CDLL(options.export_taskinfo)
        except OSError:
            raise StackAnalyzerError("Failed to load export_taskinfo.")

        tasklist = LoadTasklist(options.section, export_taskinfo, symbols)

        analyzer = StackAnalyzer(options, symbols, rodata, tasklist, annotation)
        analyzer.Analyze()
    except StackAnalyzerError as e:
        print("Error: {}".format(e))


if __name__ == "__main__":
    main()
