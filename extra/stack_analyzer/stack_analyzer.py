#!/usr/bin/env python2
# Copyright 2017 The Chromium OS Authors. All rights reserved.
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
import ctypes
import re
import subprocess


SECTION_RO = 'RO'
SECTION_RW = 'RW'
# TODO(cheyuw): This should depend on the CPU and build options.
# The size of extra stack frame needed by interrupts. (on cortex-m with FPU)
INTERRUPT_EXTRA_STACK_FRAME = 224


class StackAnalyzerError(Exception):
  """Exception class for stack analyzer utility."""


class TaskInfo(ctypes.Structure):
  """Taskinfo ctypes structure.

  The structure definition is corresponding to the "struct taskinfo"
  in "util/export_taskinfo.so.c".
  """
  _fields_ = [('name', ctypes.c_char_p),
              ('routine', ctypes.c_char_p),
              ('stack_size', ctypes.c_uint32)]


class Task(object):
  """Task information.

  Attributes:
    name: Task name.
    routine_name: Routine function name.
    stack_max_size: Max stack size.
    routine_address: Resolved routine address. None if it hasn't been resolved.
  """

  def __init__(self, name, routine_name, stack_max_size, routine_address=None):
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
    return (self.name == other.name and
            self.routine_name == other.routine_name and
            self.stack_max_size == other.stack_max_size and
            self.routine_address == other.routine_address)


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
    assert symtype in ['O', 'F']
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
    return (self.address == other.address and
            self.symtype == other.symtype and
            self.size == other.size and
            self.name == other.name)


class Callsite(object):
  """Function callsite.

  Attributes:
    address: Address of callsite location.
    target: Callee address.
    is_tail: A bool indicates that it is a tailing call.
    callee: Resolved callee function. None if it hasn't been resolved.
  """

  def __init__(self, address, target, is_tail, callee=None):
    """Constructor.

    Args:
      address: Address of callsite location.
      target: Callee address.
      is_tail: A bool indicates that it is a tailing call. (function jump to
               another function without restoring the stack frame)
      callee: Resolved callee function.
    """
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
    if not (self.address == other.address and
            self.target == other.target and
            self.is_tail == other.is_tail):
      return False

    if self.callee is None:
      return other.callee is None
    else:
      if other.callee is None:
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
    stack_successor: Successor on the max stack usage path. None if it hasn't
                     been analyzed or it's the end.
    cycle_index: Index of the cycle group. None if it hasn't been analyzed.
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
    self.stack_successor = None
    # Node attributes for Tarjan's strongly connected components algorithm.
    # TODO(cheyuw): The SCC node attributes should be moved out from the
    #               Function class.
    self.scc_index = None
    self.scc_lowlink = None
    self.scc_onstack = False
    self.cycle_index = None

  def __eq__(self, other):
    """Function equality.

    Args:
      other: The compared object.

    Returns:
      True if equal, False if not.
    """
    # TODO(cheyuw): Don't compare SCC node attributes here.
    if not (self.address == other.address and
            self.name == other.name and
            self.stack_frame == other.stack_frame and
            self.callsites == other.callsites and
            self.stack_max_usage == other.stack_max_usage and
            self.scc_index == other.scc_index and
            self.scc_lowlink == other.scc_lowlink and
            self.scc_onstack == other.scc_onstack and
            self.cycle_index == other.cycle_index):
      return False

    if self.stack_successor is None:
      return other.stack_successor is None
    else:
      if other.stack_successor is None:
        return False

      # Assume the addresses of functions are unique.
      return self.stack_successor.address == other.stack_successor.address


class ArmAnalyzer(object):
  """Disassembly analyzer for ARM architecture.

  Public Methods:
    AnalyzeFunction: Analyze stack frame and callsites of the function.
  """

  GENERAL_PURPOSE_REGISTER_SIZE = 4

  # Possible condition code suffixes.
  CONDITION_CODES = ['', 'eq', 'ne', 'cs', 'hs', 'cc', 'lo', 'mi', 'pl', 'vs',
                     'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le']
  CONDITION_CODES_RE = '({})'.format('|'.join(CONDITION_CODES))

  # Fuzzy regular expressions for instruction and operand parsing.
  # Branch instructions.
  JUMP_OPCODE_RE = re.compile(
      r'^(b{0}|bx{0}|cbz|cbnz)(\.\w)?$'.format(CONDITION_CODES_RE))
  # Call instructions.
  CALL_OPCODE_RE = re.compile(
      r'^(bl{0}|blx{0})(\.\w)?$'.format(CONDITION_CODES_RE))
  # Assume there is no function name containing ">".
  CALL_OPERAND_RE = re.compile(r'^([0-9A-Fa-f]+)\s+<([^>]+)>$')
  # TODO(cheyuw): Handle conditional versions of following
  #               instructions.
  # TODO(cheyuw): Handle other kinds of stm instructions.
  PUSH_OPCODE_RE = re.compile(r'^push$')
  STM_OPCODE_RE = re.compile(r'^stmdb$')
  # Stack subtraction instructions.
  SUB_OPCODE_RE = re.compile(r'^sub(s|w)?(\.\w)?$')
  SUB_OPERAND_RE = re.compile(r'^sp[^#]+#(\d+)')

  def AnalyzeFunction(self, function_symbol, instructions):
    """Analyze function, resolve the size of stack frame and callsites.

    Args:
      function_symbol: Function symbol.
      instructions: Instruction list.

    Returns:
      (stack_frame, callsites): Size of stack frame and callsite list.
    """
    def DetectCallsite(operand_text):
      """Check if the instruction is a callsite.

      Args:
        operand_text: Text of instruction operands.

      Returns:
        target_address: Target address. None if it isn't a callsite.
      """
      result = self.CALL_OPERAND_RE.match(operand_text)
      if result is None:
        return None

      target_address = int(result.group(1), 16)

      if (function_symbol.size > 0 and
          function_symbol.address < target_address <
          (function_symbol.address + function_symbol.size)):
        # Filter out the in-function target (branches and in-function calls,
        # which are actually branches).
        return None

      return target_address

    stack_frame = 0
    callsites = []
    for address, opcode, operand_text in instructions:
      is_jump_opcode = self.JUMP_OPCODE_RE.match(opcode) is not None
      is_call_opcode = self.CALL_OPCODE_RE.match(opcode) is not None
      if is_jump_opcode or is_call_opcode:
        target_address = DetectCallsite(operand_text)
        if target_address is not None:
          # Maybe it's a callsite.
          callsites.append(Callsite(address, target_address, is_jump_opcode))

      elif self.PUSH_OPCODE_RE.match(opcode) is not None:
        # Example: "{r4, r5, r6, r7, lr}"
        stack_frame += (len(operand_text.split(',')) *
                        self.GENERAL_PURPOSE_REGISTER_SIZE)
      elif self.SUB_OPCODE_RE.match(opcode) is not None:
        result = self.SUB_OPERAND_RE.match(operand_text)
        if result is not None:
          stack_frame += int(result.group(1))
        else:
          # Unhandled stack register subtraction.
          assert not operand_text.startswith('sp')

      elif self.STM_OPCODE_RE.match(opcode) is not None:
        if operand_text.startswith('sp!'):
          # Subtract and writeback to stack register.
          # Example: "sp!, {r4, r5, r6, r7, r8, r9, lr}"
          # Get the text of pushed register list.
          unused_sp, unused_sep, parameter_text = operand_text.partition(',')
          stack_frame += (len(parameter_text.split(',')) *
                          self.GENERAL_PURPOSE_REGISTER_SIZE)

    return (stack_frame, callsites)


class StackAnalyzer(object):
  """Class to analyze stack usage.

  Public Methods:
    Analyze: Run the stack analysis.
  """

  def __init__(self, options, symbols, tasklist):
    """Constructor.

    Args:
      options: Namespace from argparse.parse_args().
      symbols: Symbol list.
      tasklist: Task list.
    """
    self.options = options
    self.symbols = symbols
    self.tasklist = tasklist

  def AddressToLine(self, address):
    """Convert address to line.

    Args:
      address: Target address.

    Returns:
      line: The corresponding line.

    Raises:
      StackAnalyzerError: If addr2line is failed.
    """
    try:
      line_text = subprocess.check_output([self.options.addr2line,
                                           '-e',
                                           self.options.elf_path,
                                           '{:x}'.format(address)])
    except subprocess.CalledProcessError:
      raise StackAnalyzerError('addr2line failed to resolve lines.')
    except OSError:
      raise StackAnalyzerError('Failed to run addr2line.')

    return line_text.strip()

  def AnalyzeDisassembly(self, disasm_text):
    """Parse the disassembly text, analyze, and build a map of all functions.

    Args:
      disasm_text: Disassembly text.

    Returns:
      function_map: Dict of functions.
    """
    # TODO(cheyuw): Select analyzer based on architecture.
    analyzer = ArmAnalyzer()

    # Example: "08028c8c <motion_lid_calc>:"
    function_signature_regex = re.compile(
        r'^(?P<address>[0-9A-Fa-f]+)\s+<(?P<name>[^>]+)>:$')
    # Example: "44d94:	f893 0068 	ldrb.w	r0, [r3, #104]	; 0x68"
    # Assume there is always a "\t" after the hex data.
    disasm_regex = re.compile(r'^(?P<address>[0-9A-Fa-f]+):\s+[0-9A-Fa-f ]+'
                              r'\t\s*(?P<opcode>\S+)(\s+(?P<operand>[^;]*))?')

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

      address = int(result.group('address'), 16)
      symbol = symbol_map.get(address)

      # Check if the function exists and matches.
      if symbol is None or symbol.symtype != 'F':
        return None

      return symbol

    def ParseInstruction(line, function_end):
      """Parse the line of instruction.

      Args:
        line: Text of disassembly.
        function_end: End address of the current function. None if unknown.

      Returns:
        (address, opcode, operand_text): The instruction address, opcode,
                                         and the text of operands. None if it
                                         isn't an instruction line.
      """
      result = disasm_regex.match(line)
      if result is None:
        return None

      address = int(result.group('address'), 16)
      # Check if it's out of bound.
      if function_end is not None and address >= function_end:
        return None

      opcode = result.group('opcode').strip()
      operand_text = result.group('operand')
      if operand_text is None:
        operand_text = ''
      else:
        operand_text = operand_text.strip()

      return (address, opcode, operand_text)

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
    disasm_lines = [line.strip() for line in disasm_text.splitlines()]
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
            function_end = function_symbol.address + function_symbol.size
          else:
            function_end = None

      else:
        # Step 2: Parse the function body.

        instruction = ParseInstruction(line, function_end)
        if instruction is not None:
          instructions.append(instruction)

        if instruction is None or line_index + 1 == len(disasm_lines):
          # Either the invalid instruction or EOF indicates the end of the
          # function, finalize the function analysis.

          # Assume there is no empty function.
          assert len(instructions) > 0

          (stack_frame, callsites) = analyzer.AnalyzeFunction(function_symbol,
                                                              instructions)
          # Assume the function addresses are unique in the disassembly.
          assert function_symbol.address not in function_map
          function_map[function_symbol.address] = Function(
              function_symbol.address,
              function_symbol.name,
              stack_frame,
              callsites)

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
        # Remain the callee as None if we can't resolve it.
        callsite.callee = function_map.get(callsite.target)

    return function_map

  def AnalyzeCallGraph(self, function_map):
    """Analyze call graph.

    It will update the max stack size and path for each function.

    Args:
      function_map: Function map.

    Returns:
      SCC groups of the call graph.
    """
    def BuildSCC(function):
      """Tarjan's strongly connected components algorithm.

      It also calculates the max stack size and path for the function.
      For cycle, we only count the stack size following the traversal order.

      Args:
        function: Current function.
      """
      function.scc_index = scc_index_counter[0]
      function.scc_lowlink = function.scc_index
      scc_index_counter[0] += 1
      scc_stack.append(function)
      function.scc_onstack = True

      # Max stack usage is at least equal to the stack frame.
      max_stack_usage = function.stack_frame
      max_callee = None
      self_loop = False
      for callsite in function.callsites:
        callee = callsite.callee
        if callee is None:
          continue

        if callee.scc_lowlink is None:
          # Unvisited descendant.
          BuildSCC(callee)
          function.scc_lowlink = min(function.scc_lowlink, callee.scc_lowlink)
        elif callee.scc_onstack:
          # Reaches a parent node or self.
          function.scc_lowlink = min(function.scc_lowlink, callee.scc_index)
          if callee is function:
            self_loop = True

        # If the callee is a parent or itself, stack_max_usage will be None.
        callee_stack_usage = callee.stack_max_usage
        if callee_stack_usage is not None:
          if callsite.is_tail:
            # For tailing call, since the callee reuses the stack frame of the
            # caller, choose which one is larger directly.
            stack_usage = max(function.stack_frame, callee_stack_usage)
          else:
            stack_usage = function.stack_frame + callee_stack_usage

          if stack_usage > max_stack_usage:
            max_stack_usage = stack_usage
            max_callee = callee

      if function.scc_lowlink == function.scc_index:
        # Group the functions to a new cycle group.
        group_index = len(cycle_groups)
        group = []
        while scc_stack[-1] is not function:
          scc_func = scc_stack.pop()
          scc_func.scc_onstack = False
          scc_func.cycle_index = group_index
          group.append(scc_func)

        scc_stack.pop()
        function.scc_onstack = False
        function.cycle_index = group_index

        # If the function is in any cycle (include self loop), add itself to
        # the cycle group. Otherwise its cycle group is empty.
        if len(group) > 0 or self_loop:
          # The function is in a cycle.
          group.append(function)

        cycle_groups.append(group)

      # Update stack analysis result.
      function.stack_max_usage = max_stack_usage
      function.stack_successor = max_callee

    cycle_groups = []
    scc_index_counter = [0]
    scc_stack = []
    for function in function_map.values():
      if function.scc_lowlink is None:
        BuildSCC(function)

    return cycle_groups

  def Analyze(self):
    """Run the stack analysis."""
    # Analyze disassembly.
    try:
      disasm_text = subprocess.check_output([self.options.objdump,
                                             '-d',
                                             self.options.elf_path])
    except subprocess.CalledProcessError:
      raise StackAnalyzerError('objdump failed to disassemble.')
    except OSError:
      raise StackAnalyzerError('Failed to run objdump.')

    function_map = self.AnalyzeDisassembly(disasm_text)
    cycle_groups = self.AnalyzeCallGraph(function_map)

    # Print the results of task-aware stack analysis.
    for task in self.tasklist:
      routine_func = function_map[task.routine_address]
      print('Task: {}, Max size: {} ({} + {}), Allocated size: {}'.format(
          task.name,
          routine_func.stack_max_usage + INTERRUPT_EXTRA_STACK_FRAME,
          routine_func.stack_max_usage,
          INTERRUPT_EXTRA_STACK_FRAME,
          task.stack_max_size))

      print('Call Trace:')
      curr_func = routine_func
      while curr_func is not None:
        line = self.AddressToLine(curr_func.address)
        output = '\t{} ({}) {:x} [{}]'.format(curr_func.name,
                                              curr_func.stack_frame,
                                              curr_func.address,
                                              line)
        if len(cycle_groups[curr_func.cycle_index]) > 0:
          # If its cycle group isn't empty, it is in a cycle.
          output += ' [cycle]'

        print(output)
        curr_func = curr_func.stack_successor


def ParseArgs():
  """Parse commandline arguments.

  Returns:
    options: Namespace from argparse.parse_args().
  """
  parser = argparse.ArgumentParser(description="EC firmware stack analyzer.")
  parser.add_argument('elf_path', help="the path of EC firmware ELF")
  parser.add_argument('--export_taskinfo', required=True,
                      help="the path of export_taskinfo.so utility")
  parser.add_argument('--section', required=True, help='the section.',
                      choices=[SECTION_RO, SECTION_RW])
  parser.add_argument('--objdump', default='objdump',
                      help='the path of objdump')
  parser.add_argument('--addr2line', default='addr2line',
                      help='the path of addr2line')

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
  symbol_regex = re.compile(r'^(?P<address>[0-9A-Fa-f]+)\s+[lwg]\s+'
                            r'((?P<type>[OF])\s+)?\S+\s+'
                            r'(?P<size>[0-9A-Fa-f]+)\s+'
                            r'(\S+\s+)?(?P<name>\S+)$')

  symbols = []
  for line in symbol_text.splitlines():
    line = line.strip()
    result = symbol_regex.match(line)
    if result is not None:
      address = int(result.group('address'), 16)
      symtype = result.group('type')
      if symtype is None:
        symtype = 'O'

      size = int(result.group('size'), 16)
      name = result.group('name')
      symbols.append(Symbol(address, symtype, size, name))

  return symbols


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
    tasklist.append(Task(taskinfo.name, taskinfo.routine, taskinfo.stack_size))

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

    # Generate and parse the symbols.
    try:
      symbol_text = subprocess.check_output([options.objdump,
                                             '-t',
                                             options.elf_path])
    except subprocess.CalledProcessError:
      raise StackAnalyzerError('objdump failed to dump symbol table.')
    except OSError:
      raise StackAnalyzerError('Failed to run objdump.')

    symbols = ParseSymbolText(symbol_text)

    # Load the tasklist.
    try:
      export_taskinfo = ctypes.CDLL(options.export_taskinfo)
    except OSError:
      raise StackAnalyzerError('Failed to load export_taskinfo.')

    tasklist = LoadTasklist(options.section, export_taskinfo, symbols)

    analyzer = StackAnalyzer(options, symbols, tasklist)
    analyzer.Analyze()
  except StackAnalyzerError as e:
    print('Error: {}'.format(e))


if __name__ == '__main__':
  main()
