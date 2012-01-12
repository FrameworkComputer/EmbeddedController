#
# Board specific files build

# the IC is TI Stellaris LM4
CHIP:=lm4

board-y=board.o
board-$(CONFIG_TEMP_SENSOR)+=board_temp_sensor.o
