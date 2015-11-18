/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

%module ftdi_spi_tpm
typedef unsigned uint32_t;
typedef unsigned char uint8_t;

%{
typedef struct swig_string_data
{
  int size;
  char *data;
} swig_string_data;

extern int FtdiSpiInit(uint32_t freq, int enable_debug);
extern void FtdiStop(void);
extern swig_string_data FtdiSendCommandAndWait(char *tpm_command,
					       int command_size);
%}

%typemap(in) (char *tpm_command, int command_size)
{
        if(!PyString_Check($input))
        {
                PyErr_SetString(PyExc_ValueError, "String value required");
                return NULL;
        }

        $1 = PyString_AsString($input);
        $2 = PyString_Size($input);
}

%typemap(out) swig_string_data
{
        $result = PyString_FromStringAndSize($1.data, $1.size);
        free($1.data);
}

typedef struct swig_string_data
{
  int size;
  char *data;
} swig_string_data;

extern int FtdiSpiInit(uint32_t freq, int enable_debug);
extern void FtdiStop(void);
extern swig_string_data FtdiSendCommandAndWait(char *tpm_command,
					       int command_size);
