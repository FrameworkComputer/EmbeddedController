#!/usr/bin/python2
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file is a utility to quickly flash boards


import os
import subprocess as sp
import sys
import argparse

# example of call this method will make
# make BOARD=nucleo-f072rb CTS_MODULE=gpio -j

ocd_script_dir = '/usr/local/share/openocd/scripts'
th_board = 'stm32l476g-eval'
th_serial_filename = 'th_hla_serial'

def make(module, dut_board, ecDirectory):
    sp.call(['make', '--directory=' + str(ecDirectory),
              'BOARD=stm32l476g-eval', 'CTS_MODULE=' + module, '-j'])

    sp.call(['make', '--directory=' + str(ecDirectory),
              'BOARD=' + dut_board, 'CTS_MODULE=' + module, '-j'])

def openocd_cmd(command_list, board_cfg):
    args = ['openocd', '-s', ocd_script_dir,
            '-f', board_cfg]
    for c in command_list:
        args.append('-c')
        args.append(c)
    args.append('-c')
    args.append('shutdown')
    sp.call(args)

def get_stlink_serial_numbers():
    usb_args = ['lsusb', '-v', '-d', '0x0483:0x374b']
    usb_process = sp.Popen(usb_args, stdout=sp.PIPE, shell=False)
    st_link_info = usb_process.communicate()[0]
    st_serials = []
    for line in st_link_info.split('\n'):
        if 'iSerial' in line:
            st_serials.append(line.split()[2])
    return st_serials

# This function is necessary because the dut might be using an st-link debugger
# params: th_hla_serial is your personal th board's serial
def identify_dut(th_hla_serial):
    stlink_serials = get_stlink_serial_numbers()
    if len(stlink_serials) == 1:
        return None
    # If 2 st-link devices connected, find dut's serial number
    elif len(stlink_serials) == 2:
        dut = [s for s in stlink_serials if th_hla_serial not in s]
        if len(dut) != 1:
            print 'ERROR: Check your TH hla_serial'
            return None
        else:
            return dut[0] # Found your other st-link device serial!
    else:
        print 'ERROR: Please connect TH and your DUT and remove all other st-link devices'
        return None

def update_th_serial(dest_dir):
    serial = get_stlink_serial_numbers()
    if len(serial) != 1:
        print 'Connect your TH and remove other st-link devices'
    else:
        ser = serial[0]
        f = open(os.path.join(dest_dir, th_serial_filename), mode='w')
        f.write(ser)
        f.close()
        return ser

def get_board_config_name(board):
    board_config_locs = {
        'stm32l476g-eval' : 'board/stm32l4discovery.cfg',
        'nucleo-f072rb' : 'board/st_nucleo_f0.cfg'
    }
    return board_config_locs[board]

def flash_boards(dut_board, th_serial_loc):
    th_hla = None
    dut_hla = None
    try:
        th_hla = open(th_serial_loc).read()
    except:
        print 'Your th hla_serial may not have been saved.'
        print 'Connect only your th and run ./cts --th, then try again.'
        print sys.exc_info()[0]
        return
    dut_hla = identify_dut(th_hla)
    th_cfg = get_board_config_name(th_board)
    dut_cfg = get_board_config_name(dut_board)

    if(th_cfg == None or dut_cfg == None):
        print 'Board cfg files not found'
        return

    th_flash_cmds = ['hla_serial ' + th_hla,
                     'reset_config connect_assert_srst',
                     'init',
                     'reset init',
                     'flash write_image erase build/' + th_board + '/ec.bin 0x08000000',
                     'reset halt']

    dut_flash_cmds = ['hla_serial ' + dut_hla,
                      'reset_config connect_assert_srst',
                      'init',
                      'reset init',
                      'flash write_image erase build/' + dut_board + '/ec.bin 0x08000000',
                      'reset halt']

    openocd_cmd(th_flash_cmds, th_cfg)
    openocd_cmd(dut_flash_cmds, dut_cfg)
    openocd_cmd(['hla_serial ' + th_hla, 'init', 'reset init', 'resume'], th_cfg)
    openocd_cmd(['hla_serial ' + dut_hla, 'init', 'reset init', 'resume'], dut_cfg)

def main():
    global ocd_script_dir
    path = os.path.abspath(__file__)
    ec_dir = os.path.join(os.path.dirname(path), '..')
    os.chdir(ec_dir)
    th_serial_dir = os.path.join(ec_dir, 'build', th_board)
    dut_board = 'nucleo-f072rb' #nucleo by default
    module = 'gpio' #gpio by default

    parser = argparse.ArgumentParser(description='Used to build/flash boards')
    parser.add_argument('-d',
                        '--dut',
                        help='Specify DUT you want to build/flash')
    parser.add_argument('-m',
                        '--module',
                        help='Specify module you want to build/flash')
    parser.add_argument('-t',
                        '--th',
                        action='store_true',
                        help='Connect only the th to save its serial')
    parser.add_argument('-b',
                        '--build',
                        action='store_true',
                        help='Build test suite (no flashing)')
    parser.add_argument('-f',
                        '--flash',
                        action='store_true',
                        help='Flash boards with last image built for them')

    args = parser.parse_args()
    args = parser.parse_args()

    if args.th:
        serial = update_th_serial(th_serial_dir)
        if(serial != None):
            print 'Your th hla_serial # has been saved as: ' + serial
        return

    if args.module:
        module = args.module

    if args.dut:
        dut_board = args.dut

    elif args.build:
        make(module, dut_board, ec_dir)

    elif args.flash:
        flash_boards(dut_board, os.path.join(th_serial_dir, th_serial_filename))

    else:
        make(module, dut_board, ec_dir)
        flash_boards(dut_board, os.path.join(th_serial_dir, th_serial_filename))

if __name__ == "__main__":
    main()