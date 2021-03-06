/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include "py/nlr.h"
#include "py/obj.h"
#include "py/objtype.h"
#include "py/objint.h"
#include "py/runtime0.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "py/stream.h" // for mp_obj_print

mp_obj_type_t *mp_obj_get_type(mp_const_obj_t o_in) {
    if (MP_OBJ_IS_SMALL_INT(o_in)) {
        return (mp_obj_t)&mp_type_int;
    } else if (MP_OBJ_IS_QSTR(o_in)) {
        return (mp_obj_t)&mp_type_str;
    } else {
        const mp_obj_base_t *o = o_in;
        return (mp_obj_t)o->type;
    }
}

const char *mp_obj_get_type_str(mp_const_obj_t o_in) {
    return qstr_str(mp_obj_get_type(o_in)->name);
}

void mp_obj_print_helper(const mp_print_t *print, mp_obj_t o_in, mp_print_kind_t kind) {
    // There can be data structures nested too deep, or just recursive
    MP_STACK_CHECK();
#ifndef NDEBUG
    if (o_in == NULL) {
        mp_print_str(print, "(nil)");
        return;
    }
#endif
    mp_obj_type_t *type = mp_obj_get_type(o_in);
    if (type->print != NULL) {
        type->print((mp_print_t*)print, o_in, kind);
    } else {
        mp_printf(print, "<%q>", type->name);
    }
}

void mp_obj_print(mp_obj_t o_in, mp_print_kind_t kind) {
#if MICROPY_PY_IO
    mp_obj_print_helper(&mp_sys_stdout_print, o_in, kind);
#else
    mp_obj_print_helper(&mp_plat_print, o_in, kind);
#endif
}

// helper function to print an exception with traceback
void mp_obj_print_exception(const mp_print_t *print, mp_obj_t exc) {
    if (mp_obj_is_exception_instance(exc)) {
        mp_uint_t n, *values;
        mp_obj_exception_get_traceback(exc, &n, &values);
        if (n > 0) {
            assert(n % 3 == 0);
            mp_print_str(print, "Traceback (most recent call last):\n");
            for (int i = n - 3; i >= 0; i -= 3) {
#if MICROPY_ENABLE_SOURCE_LINE
                mp_printf(print, "  File \"%q\", line %d", values[i], (int)values[i + 1]);
#else
                mp_printf(print, "  File \"%q\"", values[i]);
#endif
                // the block name can be NULL if it's unknown
                qstr block = values[i + 2];
                if (block == MP_QSTR_NULL) {
                    mp_print_str(print, "\n");
                } else {
                    mp_printf(print, ", in %q\n", block);
                }
            }
        }
    }
    mp_obj_print_helper(print, exc, PRINT_EXC);
    mp_print_str(print, "\n");
}

bool mp_obj_is_true(mp_obj_t arg) {
    if (arg == mp_const_false) {
        return 0;
    } else if (arg == mp_const_true) {
        return 1;
    } else if (arg == mp_const_none) {
        return 0;
    } else if (MP_OBJ_IS_SMALL_INT(arg)) {
        if (MP_OBJ_SMALL_INT_VALUE(arg) == 0) {
            return 0;
        } else {
            return 1;
        }
    } else {
        mp_obj_type_t *type = mp_obj_get_type(arg);
        if (type->unary_op != NULL) {
            mp_obj_t result = type->unary_op(MP_UNARY_OP_BOOL, arg);
            if (result != MP_OBJ_NULL) {
                return result == mp_const_true;
            }
        }

        mp_obj_t len = mp_obj_len_maybe(arg);
        if (len != MP_OBJ_NULL) {
            // obj has a length, truth determined if len != 0
            return len != MP_OBJ_NEW_SMALL_INT(0);
        } else {
            // any other obj is true per Python semantics
            return 1;
        }
    }
}

bool mp_obj_is_callable(mp_obj_t o_in) {
    mp_call_fun_t call = mp_obj_get_type(o_in)->call;
    if (call != mp_obj_instance_call) {
        return call != NULL;
    }
    return mp_obj_instance_is_callable(o_in);
}

mp_int_t mp_obj_hash(mp_obj_t o_in) {
    if (o_in == mp_const_false) {
        return 0; // needs to hash to same as the integer 0, since False==0
    } else if (o_in == mp_const_true) {
        return 1; // needs to hash to same as the integer 1, since True==1
    } else if (MP_OBJ_IS_SMALL_INT(o_in)) {
        return MP_OBJ_SMALL_INT_VALUE(o_in);
    } else if (MP_OBJ_IS_TYPE(o_in, &mp_type_int)) {
        return mp_obj_int_hash(o_in);
    } else if (MP_OBJ_IS_STR(o_in) || MP_OBJ_IS_TYPE(o_in, &mp_type_bytes)) {
        return mp_obj_str_get_hash(o_in);
    } else if (MP_OBJ_IS_TYPE(o_in, &mp_type_NoneType)) {
        return (mp_int_t)o_in;
    } else if (MP_OBJ_IS_FUN(o_in)) {
        return (mp_int_t)o_in;
    } else if (MP_OBJ_IS_TYPE(o_in, &mp_type_tuple)) {
        return mp_obj_tuple_hash(o_in);
    } else if (MP_OBJ_IS_TYPE(o_in, &mp_type_type)) {
        return (mp_int_t)o_in;
    } else if (mp_obj_is_instance_type(mp_obj_get_type(o_in))) {
        // if a valid __hash__ method exists, use it
        mp_obj_t method[2];
        mp_load_method_maybe(o_in, MP_QSTR___hash__, method);
        if (method[0] != MP_OBJ_NULL) {
            mp_obj_t hash_val = mp_call_method_n_kw(0, 0, method);
            if (MP_OBJ_IS_INT(hash_val)) {
                return mp_obj_int_get_truncated(hash_val);
            }
            goto error;
        }

        mp_load_method_maybe(o_in, MP_QSTR___eq__, method);
        if (method[0] == MP_OBJ_NULL) {
            // https://docs.python.org/3/reference/datamodel.html#object.__hash__
            // "User-defined classes have __eq__() and __hash__() methods by default;
            // with them, all objects compare unequal (except with themselves) and 
            // x.__hash__() returns an appropriate value such that x == y implies
            // both that x is y and hash(x) == hash(y)."
            return (mp_int_t)o_in;
        }
        // "A class that overrides __eq__() and does not define __hash__() will have its __hash__() implicitly set to None.
        // When the __hash__() method of a class is None, instances of the class will raise an appropriate TypeError"
    }

    // TODO hash classes

error:
    if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "unhashable type"));
    } else {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
            "unhashable type: '%s'", mp_obj_get_type_str(o_in)));
    }
}

// This function implements the '==' operator (and so the inverse of '!=').
//
// From the Python language reference:
// (https://docs.python.org/3/reference/expressions.html#not-in)
// "The objects need not have the same type. If both are numbers, they are converted
// to a common type. Otherwise, the == and != operators always consider objects of
// different types to be unequal."
//
// This means that False==0 and True==1 are true expressions.
//
// Furthermore, from the v3.4.2 code for object.c: "Practical amendments: If rich
// comparison returns NotImplemented, == and != are decided by comparing the object
// pointer."
bool mp_obj_equal(mp_obj_t o1, mp_obj_t o2) {
    if (o1 == o2) {
        return true;
    }
    if (o1 == mp_const_none || o2 == mp_const_none) {
        return false;
    }

    // fast path for small ints
    if (MP_OBJ_IS_SMALL_INT(o1)) {
        if (MP_OBJ_IS_SMALL_INT(o2)) {
            // both SMALL_INT, and not equal if we get here
            return false;
        } else {
            mp_obj_t temp = o2; o2 = o1; o1 = temp;
            // o2 is now the SMALL_INT, o1 is not
            // fall through to generic op
        }
    }

    // fast path for strings
    if (MP_OBJ_IS_STR(o1)) {
        if (MP_OBJ_IS_STR(o2)) {
            // both strings, use special function
            return mp_obj_str_equal(o1, o2);
        } else {
            // a string is never equal to anything else
            return false;
        }
    } else if (MP_OBJ_IS_STR(o2)) {
        // o1 is not a string (else caught above), so the objects are not equal
        return false;
    }

    // generic type, call binary_op(MP_BINARY_OP_EQUAL)
    mp_obj_type_t *type = mp_obj_get_type(o1);
    if (type->binary_op != NULL) {
        mp_obj_t r = type->binary_op(MP_BINARY_OP_EQUAL, o1, o2);
        if (r != MP_OBJ_NULL) {
            return r == mp_const_true ? true : false;
        }
    }

    // equality not implemented, and objects are not the same object, so
    // they are defined as not equal
    return false;
}

mp_int_t mp_obj_get_int(mp_const_obj_t arg) {
    // This function essentially performs implicit type conversion to int
    // Note that Python does NOT provide implicit type conversion from
    // float to int in the core expression language, try some_list[1.0].
    if (arg == mp_const_false) {
        return 0;
    } else if (arg == mp_const_true) {
        return 1;
    } else if (MP_OBJ_IS_SMALL_INT(arg)) {
        return MP_OBJ_SMALL_INT_VALUE(arg);
    } else if (MP_OBJ_IS_TYPE(arg, &mp_type_int)) {
        return mp_obj_int_get_checked(arg);
    } else {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError,
                "can't convert to int"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "can't convert %s to int", mp_obj_get_type_str(arg)));
        }
    }
}

// returns false if arg is not of integral type
// returns true and sets *value if it is of integral type
// can throw OverflowError if arg is of integral type, but doesn't fit in a mp_int_t
bool mp_obj_get_int_maybe(mp_const_obj_t arg, mp_int_t *value) {
    if (arg == mp_const_false) {
        *value = 0;
    } else if (arg == mp_const_true) {
        *value = 1;
    } else if (MP_OBJ_IS_SMALL_INT(arg)) {
        *value = MP_OBJ_SMALL_INT_VALUE(arg);
    } else if (MP_OBJ_IS_TYPE(arg, &mp_type_int)) {
        *value = mp_obj_int_get_checked(arg);
    } else {
        return false;
    }
    return true;
}

#if MICROPY_PY_BUILTINS_FLOAT
mp_float_t mp_obj_get_float(mp_obj_t arg) {
    if (arg == mp_const_false) {
        return 0;
    } else if (arg == mp_const_true) {
        return 1;
    } else if (MP_OBJ_IS_SMALL_INT(arg)) {
        return MP_OBJ_SMALL_INT_VALUE(arg);
    } else if (MP_OBJ_IS_TYPE(arg, &mp_type_int)) {
        return mp_obj_int_as_float(arg);
    } else if (MP_OBJ_IS_TYPE(arg, &mp_type_float)) {
        return mp_obj_float_get(arg);
    } else {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError,
                "can't convert to float"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "can't convert %s to float", mp_obj_get_type_str(arg)));
        }
    }
}

#if MICROPY_PY_BUILTINS_COMPLEX
void mp_obj_get_complex(mp_obj_t arg, mp_float_t *real, mp_float_t *imag) {
    if (arg == mp_const_false) {
        *real = 0;
        *imag = 0;
    } else if (arg == mp_const_true) {
        *real = 1;
        *imag = 0;
    } else if (MP_OBJ_IS_SMALL_INT(arg)) {
        *real = MP_OBJ_SMALL_INT_VALUE(arg);
        *imag = 0;
    } else if (MP_OBJ_IS_TYPE(arg, &mp_type_int)) {
        *real = mp_obj_int_as_float(arg);
        *imag = 0;
    } else if (MP_OBJ_IS_TYPE(arg, &mp_type_float)) {
        *real = mp_obj_float_get(arg);
        *imag = 0;
    } else if (MP_OBJ_IS_TYPE(arg, &mp_type_complex)) {
        mp_obj_complex_get(arg, real, imag);
    } else {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError,
                "can't convert to complex"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "can't convert %s to complex", mp_obj_get_type_str(arg)));
        }
    }
}
#endif
#endif

void mp_obj_get_array(mp_obj_t o, mp_uint_t *len, mp_obj_t **items) {
    if (MP_OBJ_IS_TYPE(o, &mp_type_tuple)) {
        mp_obj_tuple_get(o, len, items);
    } else if (MP_OBJ_IS_TYPE(o, &mp_type_list)) {
        mp_obj_list_get(o, len, items);
    } else {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError,
                "expected tuple/list"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "object '%s' is not a tuple or list", mp_obj_get_type_str(o)));
        }
    }
}

void mp_obj_get_array_fixed_n(mp_obj_t o, mp_uint_t len, mp_obj_t **items) {
    mp_uint_t seq_len;
    mp_obj_get_array(o, &seq_len, items);
    if (seq_len != len) {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError,
                "tuple/list has wrong length"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
                "requested length %d but object has length %d", len, seq_len));
        }
    }
}

// is_slice determines whether the index is a slice index
mp_uint_t mp_get_index(const mp_obj_type_t *type, mp_uint_t len, mp_obj_t index, bool is_slice) {
    mp_int_t i;
    if (MP_OBJ_IS_SMALL_INT(index)) {
        i = MP_OBJ_SMALL_INT_VALUE(index);
    } else if (!mp_obj_get_int_maybe(index, &i)) {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError,
                "indices must be integers"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "%q indices must be integers, not %s",
                type->name, mp_obj_get_type_str(index)));
        }
    }

    if (i < 0) {
        i += len;
    }
    if (is_slice) {
        if (i < 0) {
            i = 0;
        } else if ((mp_uint_t)i > len) {
            i = len;
        }
    } else {
        if (i < 0 || (mp_uint_t)i >= len) {
            if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_IndexError, "index out of range"));
            } else {
                nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_IndexError,
                    "%q index out of range", type->name));
            }
        }
    }
    return i;
}

mp_obj_t mp_obj_id(mp_obj_t o_in) {
    mp_int_t id = (mp_int_t)o_in;
    if (!MP_OBJ_IS_OBJ(o_in)) {
        return mp_obj_new_int(id);
    } else if (id >= 0) {
        // Many OSes and CPUs have affinity for putting "user" memories
        // into low half of address space, and "system" into upper half.
        // We're going to take advantage of that and return small int
        // (signed) for such "user" addresses.
        return MP_OBJ_NEW_SMALL_INT(id);
    } else {
        // If that didn't work, well, let's return long int, just as
        // a (big) positve value, so it will never clash with the range
        // of small int returned in previous case.
        return mp_obj_new_int_from_uint((mp_uint_t)id);
    }
}

// will raise a TypeError if object has no length
mp_obj_t mp_obj_len(mp_obj_t o_in) {
    mp_obj_t len = mp_obj_len_maybe(o_in);
    if (len == MP_OBJ_NULL) {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError,
                "object has no len"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "object of type '%s' has no len()", mp_obj_get_type_str(o_in)));
        }
    } else {
        return len;
    }
}

// may return MP_OBJ_NULL
mp_obj_t mp_obj_len_maybe(mp_obj_t o_in) {
    if (
#if !MICROPY_PY_BUILTINS_STR_UNICODE
        // It's simple - unicode is slow, non-unicode is fast
        MP_OBJ_IS_STR(o_in) ||
#endif
        MP_OBJ_IS_TYPE(o_in, &mp_type_bytes)) {
        return MP_OBJ_NEW_SMALL_INT(mp_obj_str_get_len(o_in));
    } else {
        mp_obj_type_t *type = mp_obj_get_type(o_in);
        if (type->unary_op != NULL) {
            return type->unary_op(MP_UNARY_OP_LEN, o_in);
        } else {
            return MP_OBJ_NULL;
        }
    }
}

mp_obj_t mp_obj_subscr(mp_obj_t base, mp_obj_t index, mp_obj_t value) {
    mp_obj_type_t *type = mp_obj_get_type(base);
    if (type->subscr != NULL) {
        mp_obj_t ret = type->subscr(base, index, value);
        if (ret != MP_OBJ_NULL) {
            return ret;
        }
        // TODO: call base classes here?
    }
    if (value == MP_OBJ_NULL) {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError,
                "object does not support item deletion"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "'%s' object does not support item deletion", mp_obj_get_type_str(base)));
        }
    } else if (value == MP_OBJ_SENTINEL) {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "object is not subscriptable"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "'%s' object is not subscriptable", mp_obj_get_type_str(base)));
        }
    } else {
        if (MICROPY_ERROR_REPORTING == MICROPY_ERROR_REPORTING_TERSE) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError,
                "object does not support item assignment"));
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_TypeError,
                "'%s' object does not support item assignment", mp_obj_get_type_str(base)));
        }
    }
}

// Return input argument. Useful as .getiter for objects which are
// their own iterators, etc.
mp_obj_t mp_identity(mp_obj_t self) {
    return self;
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_identity_obj, mp_identity);

bool mp_get_buffer(mp_obj_t obj, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    mp_obj_type_t *type = mp_obj_get_type(obj);
    if (type->buffer_p.get_buffer == NULL) {
        return false;
    }
    int ret = type->buffer_p.get_buffer(obj, bufinfo, flags);
    if (ret != 0) {
        return false;
    }
    return true;
}

void mp_get_buffer_raise(mp_obj_t obj, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    if (!mp_get_buffer(obj, bufinfo, flags)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "object with buffer protocol required"));
    }
}
