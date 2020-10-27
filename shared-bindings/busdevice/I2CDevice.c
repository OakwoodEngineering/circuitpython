/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Mark Komus
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// This file contains all of the Python API definitions for the
// busio.I2C class.

#include "shared-bindings/microcontroller/Pin.h"
#include "shared-bindings/busdevice/I2CDevice.h"
#include "shared-bindings/util.h"
#include "shared-module/busdevice/I2CDevice.h"

#include "lib/utils/buffer_helper.h"
#include "lib/utils/context_manager_helpers.h"
#include "py/runtime.h"
#include "supervisor/shared/translate.h"


//| class I2CDevice:
//|     """
//|     Represents a single I2C device and manages locking the bus and the device
//|     address.
//|     :param ~busio.I2C i2c: The I2C bus the device is on
//|     :param int device_address: The 7 bit device address
//|     :param bool probe: Probe for the device upon object creation, default is true
//|     .. note:: This class is **NOT** built into CircuitPython. See
//|       :ref:`here for install instructions <bus_device_installation>`.
//|     Example:
//|     .. code-block:: python
//|         import busio
//|         from board import *
//|         from adafruit_bus_device.i2c_device import I2CDevice
//|         with busio.I2C(SCL, SDA) as i2c:
//|             device = I2CDevice(i2c, 0x70)
//|             bytes_read = bytearray(4)
//|             with device:
//|                 device.readinto(bytes_read)
//|             # A second transaction
//|             with device:
//|                 device.write(bytes_read)"""
//|     ...
//|
STATIC mp_obj_t busdevice_i2cdevice_make_new(const mp_obj_type_t *type, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    busdevice_i2cdevice_obj_t *self = m_new_obj(busdevice_i2cdevice_obj_t);
    self->base.type = &busdevice_i2cdevice_type;
    enum { ARG_i2c, ARG_device_address, ARG_probe };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_i2c, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_device_address, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_probe, MP_ARG_BOOL, {.u_bool = true} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    busio_i2c_obj_t* i2c = args[ARG_i2c].u_obj;

    common_hal_busdevice_i2cdevice_construct(self, i2c, args[ARG_device_address].u_int, args[ARG_probe].u_bool);
    if (args[ARG_probe].u_bool == true) {
        common_hal_busdevice_i2cdevice___probe_for_device(self);
    }

    return (mp_obj_t)self;
}

STATIC mp_obj_t busdevice_i2cdevice_obj___enter__(mp_obj_t self_in) {
    common_hal_busdevice_i2cdevice_lock(self_in);
    return self_in;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(busdevice_i2cdevice___enter___obj, busdevice_i2cdevice_obj___enter__);

STATIC mp_obj_t busdevice_i2cdevice_obj___exit__(size_t n_args, const mp_obj_t *args) {
    common_hal_busdevice_i2cdevice_unlock(args[0]);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(busdevice_i2cdevice___exit___obj, 4, 4, busdevice_i2cdevice_obj___exit__);

//| def readinto(self, buf, *, start=0, end=None):
//|         """
//|         Read into ``buf`` from the device. The number of bytes read will be the
//|         length of ``buf``.
//|         If ``start`` or ``end`` is provided, then the buffer will be sliced
//|         as if ``buf[start:end]``. This will not cause an allocation like
//|         ``buf[start:end]`` will so it saves memory.
//|         :param bytearray buffer: buffer to write into
//|         :param int start: Index to start writing at
//|         :param int end: Index to write up to but not include; if None, use ``len(buf)``"""
//|         ...
//|
STATIC void readinto(busdevice_i2cdevice_obj_t *self, mp_obj_t buffer, int32_t start, mp_int_t end) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buffer, &bufinfo, MP_BUFFER_WRITE);

    size_t length = bufinfo.len;
    normalize_buffer_bounds(&start, end, &length);
    if (length == 0) {
        mp_raise_ValueError(translate("Buffer must be at least length 1"));
    }

    uint8_t status = common_hal_busdevice_i2cdevice_readinto(self, ((uint8_t*)bufinfo.buf) + start, length);
    if (status != 0) {
        mp_raise_OSError(status);
    }
}

STATIC mp_obj_t busdevice_i2cdevice_readinto(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_buffer, ARG_start, ARG_end };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_buffer,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_start,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_end,        MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = INT_MAX} },
    };

    busdevice_i2cdevice_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    readinto(self, args[ARG_buffer].u_obj, args[ARG_start].u_int, args[ARG_end].u_int);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(busdevice_i2cdevice_readinto_obj, 2, busdevice_i2cdevice_readinto);

//|    def write(self, buf, *, start=0, end=None):
//|        """
//|        Write the bytes from ``buffer`` to the device, then transmit a stop
//|        bit.
//|        If ``start`` or ``end`` is provided, then the buffer will be sliced
//|        as if ``buffer[start:end]``. This will not cause an allocation like
//|        ``buffer[start:end]`` will so it saves memory.
//|        :param bytearray buffer: buffer containing the bytes to write
//|        :param int start: Index to start writing from
//|        :param int end: Index to read up to but not include; if None, use ``len(buf)``
//|        """
//|        ...
//|
STATIC void write(busdevice_i2cdevice_obj_t *self, mp_obj_t buffer, int32_t start, mp_int_t end) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buffer, &bufinfo, MP_BUFFER_READ);

    size_t length = bufinfo.len;
    normalize_buffer_bounds(&start, end, &length);
    if (length == 0) {
        mp_raise_ValueError(translate("Buffer must be at least length 1"));
    }

    uint8_t status = common_hal_busdevice_i2cdevice_write(self, ((uint8_t*)bufinfo.buf) + start, length);
    if (status != 0) {
        mp_raise_OSError(status);
    }
}

STATIC mp_obj_t busdevice_i2cdevice_write(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_buffer, ARG_start, ARG_end };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_buffer,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_start,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_end,        MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = INT_MAX} },
    };
    busdevice_i2cdevice_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    write(self, args[ARG_buffer].u_obj, args[ARG_start].u_int, args[ARG_end].u_int);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(busdevice_i2cdevice_write_obj, 2, busdevice_i2cdevice_write);


//| def write_then_readinto(self, out_buffer, in_buffer, *, out_start=0, out_end=None, in_start=0, in_end=None):
//|         """
//|         Write the bytes from ``out_buffer`` to the device, then immediately
//|         reads into ``in_buffer`` from the device. The number of bytes read
//|         will be the length of ``in_buffer``.
//|         If ``out_start`` or ``out_end`` is provided, then the output buffer
//|         will be sliced as if ``out_buffer[out_start:out_end]``. This will
//|         not cause an allocation like ``buffer[out_start:out_end]`` will so
//|         it saves memory.
//|         If ``in_start`` or ``in_end`` is provided, then the input buffer
//|         will be sliced as if ``in_buffer[in_start:in_end]``. This will not
//|         cause an allocation like ``in_buffer[in_start:in_end]`` will so
//|         it saves memory.
//|         :param bytearray out_buffer: buffer containing the bytes to write
//|         :param bytearray in_buffer: buffer containing the bytes to read into
//|         :param int out_start: Index to start writing from
//|         :param int out_end: Index to read up to but not include; if None, use ``len(out_buffer)``
//|         :param int in_start: Index to start writing at
//|         :param int in_end: Index to write up to but not include; if None, use ``len(in_buffer)``
//|         """
//|         ...
//|
STATIC mp_obj_t busdevice_i2cdevice_write_then_readinto(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_out_buffer, ARG_in_buffer, ARG_out_start, ARG_out_end, ARG_in_start, ARG_in_end };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_out_buffer,    MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_in_buffer,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_out_start,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_out_end,       MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = INT_MAX} },
        { MP_QSTR_in_start,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_in_end,        MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = INT_MAX} },
    };
    busdevice_i2cdevice_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    write(self, args[ARG_out_buffer].u_obj, args[ARG_out_start].u_int, args[ARG_out_end].u_int);

    readinto(self, args[ARG_in_buffer].u_obj, args[ARG_in_start].u_int, args[ARG_in_end].u_int);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(busdevice_i2cdevice_write_then_readinto_obj, 3, busdevice_i2cdevice_write_then_readinto);

//| def __probe_for_device(self):
//|     """
//|     Try to read a byte from an address,
//|     if you get an OSError it means the device is not there
//|     or that the device does not support these means of probing
//|     """
//|     ...
//|
STATIC mp_obj_t busdevice_i2cdevice___probe_for_device(mp_obj_t self_in) {
    busdevice_i2cdevice_obj_t *self = self_in;
    common_hal_busdevice_i2cdevice___probe_for_device(self);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(busdevice_i2cdevice___probe_for_device_obj, busdevice_i2cdevice___probe_for_device);

STATIC const mp_rom_map_elem_t busdevice_i2cdevice_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&busdevice_i2cdevice___enter___obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&busdevice_i2cdevice___exit___obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&busdevice_i2cdevice_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&busdevice_i2cdevice_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_then_readinto), MP_ROM_PTR(&busdevice_i2cdevice_write_then_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR___probe_for_device), MP_ROM_PTR(&busdevice_i2cdevice___probe_for_device_obj) },
};


STATIC MP_DEFINE_CONST_DICT(busdevice_i2cdevice_locals_dict, busdevice_i2cdevice_locals_dict_table);

const mp_obj_type_t busdevice_i2cdevice_type = {
   { &mp_type_type },
   .name = MP_QSTR_I2CDevice,
   .make_new = busdevice_i2cdevice_make_new,
   .locals_dict = (mp_obj_dict_t*)&busdevice_i2cdevice_locals_dict,
};
