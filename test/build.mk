#
# on-board test binaries build
#

test-list=hello pingpong timer_calib timer_dos mutex
#disable: powerdemo

pingpong-y=pingpong.o
powerdemo-y=powerdemo.o
timer_calib-y=timer_calib.o
timer_dos-y=timer_dos.o
mutex-y=mutex.o
