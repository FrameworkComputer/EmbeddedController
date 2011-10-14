/* ec_common.h - Common includes for Chrome EC
 *
 * (Chromium license) */

#ifndef __EC_COMMON_H
#define __EC_COMMON_H

#include <stdint.h>

typedef int EcError;  /* Functions which return error return one of these */
/* YJLOU: why not "typedef enum EcErrorListe EcError"? */

/* List of common EcError codes that can be returned */
enum EcErrorList {
  /* Success - no error */
  EC_SUCCESS = 0,
  /* Unknown error */
  EC_ERROR_UNKNOWN = 1,
  /* Function not implemented yet */
  EC_ERROR_UNIMPLEMENTED = 2,
  /* Overflow error; too much input provided. */
  EC_ERROR_OVERFLOW = 3,

  /* Module-internal error codes may use this range.   */
  EC_ERROR_INTERNAL_FIRST = 0x10000,
  EC_ERROR_INTERNAL_LAST =  0x1FFFF
};

#endif  /* __EC_COMMON_H */
