// ─────────────────────────────────────────────────────────────────────────────
//  lib/hxpyembed/hxpyembed.c — Embedded CPython FFI shim for hexa-lang.
//
//  Surface (consumed by stdlib/python_ffi.hexa):
//
//      int64_t  hxpy_init(void);
//      int64_t  hxpy_finalize(void);
//      int64_t  hxpy_is_initialized(void);
//      int64_t  hxpy_eval(const char *code);
//      int64_t  hxpy_call_str(const char *mod, const char *fn, const char *arg);
//      void     hxpy_free_str(int64_t p);
//      int64_t  hxpy_tensor_to_py(void *raw_ptr, int64_t numel,
//                                 const char *dtype);
//      int64_t  hxpy_py_to_tensor(void *py_obj, void *out_ptr,
//                                 int64_t max_elems);
//      int64_t  hxpy_py_buf_addr(void *py_obj);
//      int64_t  hxpy_py_buf_len(void *py_obj);
//      int64_t  hxpy_py_buf_contig(void *py_obj);
//      void     hxpy_obj_decref(void *py_obj);
//
//  Refcount discipline: every PyObject* this module returns to hexa as an
//  int handle has +1 held by us; the hexa side MUST call hxpy_obj_decref to
//  release. All error paths use Py_XDECREF to avoid leaks.
//
//  Buffer protocol (PEP 3118): hxpy_tensor_to_py wraps a hexa raw pointer
//  in a memoryview that aliases the same physical bytes — zero-copy.
//  hxpy_py_to_tensor accepts ANY object exposing the buffer protocol
//  (numpy.ndarray, torch.Tensor.numpy(), bytes, bytearray, memoryview):
//    * If buf.buf == out_ptr (round-trip detected) we skip the memcpy.
//    * Otherwise we memcpy up to max_elems*4 bytes (assumes f32 elements).
//
//  Build:
//      cmake -S lib/hxpyembed -B lib/hxpyembed/build
//      cmake --build lib/hxpyembed/build
// ─────────────────────────────────────────────────────────────────────────────

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ── Global state ────────────────────────────────────────────────────────────
static int g_initialized = 0;

// ── hxpy_init / hxpy_finalize / hxpy_is_initialized ────────────────────────
//
// Idempotent. Returns 0 on success. Negative on Python error. We do not
// install signal handlers (Py_InitializeEx with skipinit-of-signals would
// be the right call, but Py_Initialize + clearing PyErr is enough for the
// embedded host — hexa owns the process).
int64_t hxpy_init(void) {
    if (g_initialized) return 0;
    if (Py_IsInitialized()) {
        g_initialized = 1;
        return 0;
    }
    Py_Initialize();
    if (!Py_IsInitialized()) {
        return -1;
    }
    g_initialized = 1;
    return 0;
}

int64_t hxpy_finalize(void) {
    if (!g_initialized) return 0;
    if (Py_IsInitialized()) {
        // Py_FinalizeEx returns 0 ok, -1 on error. We propagate either way.
        int rc = Py_FinalizeEx();
        g_initialized = 0;
        return (int64_t)rc;
    }
    g_initialized = 0;
    return 0;
}

int64_t hxpy_is_initialized(void) {
    return (g_initialized && Py_IsInitialized()) ? 1 : 0;
}

// ── hxpy_eval ──────────────────────────────────────────────────────────────
// Run a multi-line module-level Python statement block. Returns 0 on
// success; -1 if interpreter not initialized; -2 on PyErr (traceback to
// stderr). We always clear the error before returning so the next call
// starts clean.
int64_t hxpy_eval(const char *code) {
    if (!g_initialized || !Py_IsInitialized()) return -1;
    if (code == NULL) return -2;

    PyObject *main_mod = PyImport_AddModule("__main__");  // borrowed
    if (main_mod == NULL) {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
        }
        return -2;
    }
    PyObject *globals = PyModule_GetDict(main_mod);  // borrowed

    PyObject *result = PyRun_String(code, Py_file_input, globals, globals);
    if (result == NULL) {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
        }
        return -2;
    }
    Py_DECREF(result);
    return 0;
}

// ── hxpy_call_str ──────────────────────────────────────────────────────────
// Call `mod.fn(arg)` where arg is a single string and the return is
// stringified. Returns a malloc'd C string on success (caller frees via
// hxpy_free_str), 0 on any error. Refcount-safe: every borrowed/new
// reference is matched.
int64_t hxpy_call_str(const char *mod_name, const char *fn_name,
                      const char *arg) {
    if (!g_initialized || !Py_IsInitialized()) return 0;
    if (mod_name == NULL || fn_name == NULL) return 0;

    PyObject *mod = NULL;
    PyObject *fn = NULL;
    PyObject *arg_obj = NULL;
    PyObject *args_tuple = NULL;
    PyObject *result = NULL;
    PyObject *result_str = NULL;
    char *out = NULL;

    mod = PyImport_ImportModule(mod_name);
    if (mod == NULL) goto fail;

    fn = PyObject_GetAttrString(mod, fn_name);
    if (fn == NULL || !PyCallable_Check(fn)) goto fail;

    if (arg == NULL) {
        args_tuple = PyTuple_New(0);
    } else {
        arg_obj = PyUnicode_FromString(arg);
        if (arg_obj == NULL) goto fail;
        args_tuple = PyTuple_Pack(1, arg_obj);  // INCREFs arg_obj internally
    }
    if (args_tuple == NULL) goto fail;

    result = PyObject_Call(fn, args_tuple, NULL);
    if (result == NULL) goto fail;

    // Stringify the result.
    if (PyUnicode_Check(result)) {
        result_str = result;
        Py_INCREF(result_str);
    } else {
        result_str = PyObject_Str(result);
        if (result_str == NULL) goto fail;
    }

    {
        const char *utf = PyUnicode_AsUTF8(result_str);
        if (utf == NULL) goto fail;
        size_t n = strlen(utf);
        out = (char *)malloc(n + 1);
        if (out == NULL) goto fail;
        memcpy(out, utf, n + 1);
    }

    Py_XDECREF(result_str);
    Py_XDECREF(result);
    Py_XDECREF(args_tuple);
    Py_XDECREF(arg_obj);
    Py_XDECREF(fn);
    Py_XDECREF(mod);
    return (int64_t)(uintptr_t)out;

fail:
    if (PyErr_Occurred()) {
        PyErr_Print();
        PyErr_Clear();
    }
    Py_XDECREF(result_str);
    Py_XDECREF(result);
    Py_XDECREF(args_tuple);
    Py_XDECREF(arg_obj);
    Py_XDECREF(fn);
    Py_XDECREF(mod);
    if (out) { free(out); }
    return 0;
}

void hxpy_free_str(int64_t p) {
    if (p == 0) return;
    free((void *)(uintptr_t)p);
}

// ── Buffer-protocol bridge ─────────────────────────────────────────────────
//
// dtype is a single PEP 3118 format letter:
//   "f" = f32 (4B)  "d" = f64 (8B)  "i" = i32 (4B)
//   "q" = i64 (8B)  "B" = u8  (1B)
//
// We use PyMemoryView_FromMemory on the raw pointer. The resulting
// memoryview aliases the same physical bytes — true zero-copy. The hexa
// side owns the allocation; the memoryview must be released BEFORE the
// hexa-side buffer is freed.
static Py_ssize_t dtype_itemsize(const char *dtype) {
    if (dtype == NULL || dtype[0] == '\0') return 4;  // default f32
    switch (dtype[0]) {
        case 'f': return 4;   // float
        case 'd': return 8;   // double
        case 'i': return 4;   // int32_t
        case 'I': return 4;   // uint32_t
        case 'q': return 8;   // int64_t
        case 'Q': return 8;   // uint64_t
        case 'h': return 2;   // int16_t
        case 'H': return 2;   // uint16_t
        case 'b': return 1;   // int8_t
        case 'B': return 1;   // uint8_t
        default:  return 4;
    }
}

int64_t hxpy_tensor_to_py(void *raw_ptr, int64_t numel, const char *dtype) {
    if (!g_initialized || !Py_IsInitialized()) return 0;
    if (raw_ptr == NULL || numel <= 0) return 0;

    Py_ssize_t itemsz = dtype_itemsize(dtype);
    Py_ssize_t nbytes = (Py_ssize_t)numel * itemsz;

    // Build a writable memoryview directly over the raw bytes (zero-copy).
    PyObject *mv = PyMemoryView_FromMemory((char *)raw_ptr, nbytes,
                                           PyBUF_WRITE);
    if (mv == NULL) {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
        }
        return 0;
    }

    // Cast the memoryview to the requested dtype so the Python side sees
    // floats/ints rather than raw bytes. memoryview.cast returns a new ref.
    PyObject *fmt = PyUnicode_FromString(dtype && dtype[0] ? dtype : "f");
    if (fmt == NULL) {
        Py_DECREF(mv);
        return 0;
    }

    PyObject *cast = PyObject_CallMethod(mv, "cast", "O", fmt);
    Py_DECREF(fmt);
    if (cast == NULL) {
        // Cast failed; return the byte-view rather than NULL — still
        // zero-copy, just bytes-typed. Many callers (numpy.frombuffer)
        // can take that fine.
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
        }
        return (int64_t)(uintptr_t)mv;
    }
    // Cast succeeded — drop the byte view, keep the typed view.
    Py_DECREF(mv);
    return (int64_t)(uintptr_t)cast;
}

// hxpy_py_to_tensor — pull the buffer-protocol bytes out of py_obj.
//
// If `out_ptr` matches the underlying buf.buf, we know the hexa side is
// asking us "is this round-trip the same buffer?" and we skip the memcpy
// (true zero-copy). Otherwise we memcpy min(buf.len, max_elems * itemsz).
//
// We assume 4-byte items unless py_obj is a memoryview with a known
// itemsize. Returns numel on success, -1 on error.
int64_t hxpy_py_to_tensor(void *py_obj, void *out_ptr, int64_t max_elems) {
    if (!g_initialized || !Py_IsInitialized()) return -1;
    if (py_obj == NULL) return -1;

    PyObject *obj = (PyObject *)py_obj;
    Py_buffer buf;
    memset(&buf, 0, sizeof(buf));

    if (PyObject_GetBuffer(obj, &buf, PyBUF_SIMPLE) != 0) {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
        }
        return -1;
    }

    Py_ssize_t itemsz = (buf.itemsize > 0) ? buf.itemsize : 4;
    Py_ssize_t numel = buf.len / itemsz;

    if (out_ptr != NULL && buf.buf != out_ptr) {
        // Genuine copy path.
        Py_ssize_t cap_bytes = (Py_ssize_t)max_elems * itemsz;
        Py_ssize_t n = (buf.len < cap_bytes) ? buf.len : cap_bytes;
        memcpy(out_ptr, buf.buf, (size_t)n);
        numel = n / itemsz;
    }
    // else: same address → zero-copy round-trip; nothing to do.

    PyBuffer_Release(&buf);
    return (int64_t)numel;
}

int64_t hxpy_py_buf_addr(void *py_obj) {
    if (py_obj == NULL) return 0;
    PyObject *obj = (PyObject *)py_obj;
    Py_buffer buf;
    memset(&buf, 0, sizeof(buf));
    if (PyObject_GetBuffer(obj, &buf, PyBUF_SIMPLE) != 0) {
        if (PyErr_Occurred()) PyErr_Clear();
        return 0;
    }
    int64_t addr = (int64_t)(uintptr_t)buf.buf;
    PyBuffer_Release(&buf);
    return addr;
}

int64_t hxpy_py_buf_len(void *py_obj) {
    if (py_obj == NULL) return 0;
    PyObject *obj = (PyObject *)py_obj;
    Py_buffer buf;
    memset(&buf, 0, sizeof(buf));
    if (PyObject_GetBuffer(obj, &buf, PyBUF_SIMPLE) != 0) {
        if (PyErr_Occurred()) PyErr_Clear();
        return 0;
    }
    int64_t n = (int64_t)buf.len;
    PyBuffer_Release(&buf);
    return n;
}

int64_t hxpy_py_buf_contig(void *py_obj) {
    if (py_obj == NULL) return 0;
    PyObject *obj = (PyObject *)py_obj;
    Py_buffer buf;
    memset(&buf, 0, sizeof(buf));
    // Ask explicitly for C-contiguous; if the request fails the object is
    // not C-contig (or doesn't expose the buffer protocol at all).
    if (PyObject_GetBuffer(obj, &buf, PyBUF_C_CONTIGUOUS) != 0) {
        if (PyErr_Occurred()) PyErr_Clear();
        return 0;
    }
    PyBuffer_Release(&buf);
    return 1;
}

void hxpy_obj_decref(void *py_obj) {
    if (py_obj == NULL) return;
    if (!Py_IsInitialized()) return;
    PyObject *obj = (PyObject *)py_obj;
    Py_DECREF(obj);
}
