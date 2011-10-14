/* keyboard.h -
 *
 * (Chromium license) */

#ifndef __CROS_EC_KEYBOARD_H
#define __CROS_EC_KEYBOARD_H

/* Register the board-specific keyboard matrix translation function.
 * The callback function accepts col/row and returns the scan code.
 */
EcError CrKeyboardMatrixRegister(
    int8_t col_num, int8_t row_num,
    EcError (*callback)(
        int8_t column, int8_t row, int8_t pressed,
        uint8_t *scan_code, int32_t* len));

#endif  /* __CROS_EC_KEYBOARD_H */
