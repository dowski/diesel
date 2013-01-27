#include <Python.h>

enum match_types {UNSET, BYTES, INT, ANY};
typedef int BufAny;
typedef int Unset;

typedef struct diesel_buffer {
    enum match_types mtype;
    union {
        long term_int;
        char term_bytes[32];
        BufAny term_any;
        Unset term_unset;
    } sentinel;
    char *start;
    char *head;
    char *tail;
    int current_size;
    int max_size;
    int alloc_size;
} diesel_buffer;

typedef struct {
    PyObject_HEAD
    diesel_buffer *internal_buffer;
} Buffer;

int grow_internal_buffer(diesel_buffer *internal_buffer, const int size);
void shrink_internal_buffer(diesel_buffer *internal_buffer, const int size);

struct diesel_buffer *
diesel_buffer_alloc(int startsize)
{
    diesel_buffer *buf;
    if (!(buf = malloc(sizeof(diesel_buffer))))
        return NULL;
    if (!(buf->start = malloc(startsize))) {
        free(buf);
        return NULL;
    }
    buf->mtype = UNSET;
    buf->sentinel.term_unset = 1;
    buf->current_size = 0;
    buf->tail = buf->head = buf->start;
    buf->max_size = startsize;
    buf->alloc_size = startsize;
    return buf;
}

int
grow_internal_buffer(diesel_buffer *internal_buffer, const int size)
{
    size_t head_offset, tail_offset, growth;
    char *tmp;
    growth = size < internal_buffer->alloc_size ? internal_buffer->alloc_size : internal_buffer->alloc_size + size;
    internal_buffer->max_size += growth;
    head_offset = internal_buffer->head - internal_buffer->start;
    tail_offset = internal_buffer->tail - internal_buffer->start;
    tmp = realloc(internal_buffer->start, internal_buffer->max_size);
    if (!tmp) {
        return -1;
    }
    internal_buffer->start = tmp;
    internal_buffer->head = (tmp + head_offset);
    internal_buffer->tail = (tmp + tail_offset);
    return 0;
}

void
shrink_internal_buffer(diesel_buffer *dbuf, const int size)
{
    dbuf->head += size;
    dbuf->current_size -= size;
    if (dbuf->current_size == 0) {
        dbuf->head = dbuf->tail = dbuf->start;
    } 
    //memmove(dbuf->head, dbuf->head + size, dbuf->max_size);
    //dbuf->tail = &(dbuf->head[dbuf->current_size]);
}

static void
diesel_buffer_free(diesel_buffer *buf)
{
    free(buf->start);
    free(buf);
}

static PyObject *
diesel_buffer_check(diesel_buffer *buf) {
    int sz = 0, term_sz;
    char *match = NULL;
    PyObject *res = NULL;

    switch (buf->mtype) {
        case BYTES :
            term_sz = (int)buf->sentinel.term_bytes[0];
            match = (char *)memmem(buf->head, buf->current_size, buf->sentinel.term_bytes+1, term_sz);
            if (match) {
                sz = (match - buf->head) + term_sz;
                res = PyString_FromStringAndSize(buf->head, sz);
            } 
            break;
        case INT :
            sz = buf->sentinel.term_int;
            if (buf->current_size >= sz) {
                res = PyString_FromStringAndSize(buf->head, sz);
            }
            break;
        case ANY :
            sz = buf->current_size;
            if (sz > 0) {
                res = PyString_FromStringAndSize(buf->head, sz);
            }
            break;
        case UNSET :
            Py_RETURN_NONE;
    }
    if (res) {
        shrink_internal_buffer(buf, sz);
        buf->mtype = UNSET;
        buf->sentinel.term_unset = 1;
        return res;
    } else {
        Py_RETURN_NONE;
    }
}

static PyObject *
Buffer_feed(PyObject *self, PyObject *args, PyObject *kw)
{
    char *s;
    int size;
    Buffer *buf = (Buffer *)self;

    PyArg_ParseTuple(args, "s#", &s, &size);
    size_t current_use = buf->internal_buffer->tail - buf->internal_buffer->start;

    if (size + current_use > buf->internal_buffer->max_size) {
        grow_internal_buffer(buf->internal_buffer, size);
    }
    if (!(memcpy(buf->internal_buffer->tail, s, size))) {
        return NULL;
    }
    buf->internal_buffer->current_size += size;
    buf->internal_buffer->tail += size;

    return diesel_buffer_check(buf->internal_buffer);
}

static PyObject *
Buffer_pop(PyObject *self, PyObject *args, PyObject *kw)
{
    PyObject *res;
    diesel_buffer *buf = ((Buffer *)self)->internal_buffer;
    size_t sz = buf->current_size;
    if (sz > 0) {
        res = PyString_FromStringAndSize(buf->head, sz);
        shrink_internal_buffer(buf, sz);
        return res;
    } else {
        Py_RETURN_NONE;
    }
}

static PyObject *
Buffer_set_term(PyObject *self, PyObject *args, PyObject *kw)
{
    PyObject *term;
    PyArg_ParseTuple(args, "O", &term);
    Buffer *buf = (Buffer *)self;

    if (PyString_Check(term)) {
        Py_ssize_t sz;
        char *term_val = NULL;
        buf->internal_buffer->mtype = BYTES;
        PyString_AsStringAndSize(term, &term_val, &sz);
        buf->internal_buffer->sentinel.term_bytes[0] = (char)sz;
        memcpy(buf->internal_buffer->sentinel.term_bytes+1, term_val, sz);
    } else if (PyInt_Check(term)) {
        buf->internal_buffer->mtype = INT;
        buf->internal_buffer->sentinel.term_int = PyInt_AsLong(term);
    } else {
        // XXX kind of a hack - treat any other PyObject as BufAny ...
        buf->internal_buffer->mtype = ANY;
        buf->internal_buffer->sentinel.term_any = 1;
    }
    Py_RETURN_NONE;
}

static PyObject *
Buffer_clear_term(PyObject *self, PyObject *args, PyObject *kw)
{
    diesel_buffer *buf = ((Buffer *)self)->internal_buffer;
    buf->mtype = UNSET;
    buf->sentinel.term_unset = 1;
    Py_RETURN_NONE;
}

static PyObject *
Buffer_check(PyObject *self, PyObject *args, PyObject *kw)
{
    diesel_buffer *buf = ((Buffer *)self)->internal_buffer;
    return diesel_buffer_check(buf);
}

static void
Buffer_dealloc(Buffer *self)
{

    diesel_buffer_free(self->internal_buffer);
    self->ob_type->tp_free((PyObject *)self);
}

static PyObject *
Buffer_repr(PyObject *self)
{
    PyObject *payload, *res, *fmt, *fmtargs;
    diesel_buffer *buf;
    buf = ((Buffer *)self)->internal_buffer;
    payload = PyString_FromStringAndSize(buf->head, buf->current_size);
    assert(payload);
    fmtargs = PyTuple_Pack(1, payload);
    fmt = PyString_FromString("Buffer(%r)");
    res = PyString_Format(fmt, fmtargs);
    Py_DECREF(fmtargs);
    Py_DECREF(fmt);
    Py_DECREF(payload);
    return res;
}

//static PyObject *
//Buffer_new(PyTypeObject *type, PyObject *args, PyObject *kw)
//{
//    Buffer *self;
//
//    self->internal_buffer = NULL;
//
//    return (PyObject *)self;
//}

static int
Buffer_init(PyObject *self, PyObject *args, PyObject *kw)
{
    int startsize;
    PyArg_ParseTuple(args, "i", &startsize);

    diesel_buffer *ib;
    Buffer *buf = (Buffer *)self;
    if (!buf->internal_buffer) {
        if (!(ib = diesel_buffer_alloc(startsize))) {
            return -1;
        }
        buf->internal_buffer = ib;
    }
    return 0;
}

static PyMethodDef Buffer_methods[] = {
    {"feed", (PyCFunction)Buffer_feed, METH_VARARGS,
        "Feed new data into the buffer"
    },
    {"set_term", (PyCFunction)Buffer_set_term, METH_VARARGS,
        "Set a new terminal for the buffer"
    },
    {"check", (PyCFunction)Buffer_check, METH_NOARGS,
        "Check to see if the current terminal is present in the buffer"
    },
    {"clear_term", (PyCFunction)Buffer_clear_term, METH_NOARGS,
        "Clear the current sentinel"
    },
    {"pop", (PyCFunction)Buffer_pop, METH_NOARGS,
        "Clear and return the full contents of the buffer"
    },
    {NULL}
};

static PyTypeObject diesel_Buffer = {
    PyObject_HEAD_INIT(NULL)
    0, //ob size
    "diesel.cbuf.Buffer", //tp name
    sizeof(Buffer),
    0,                         /*tp_itemsize*/
    (destructor)Buffer_dealloc,                         /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    Buffer_repr,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    "A buffer",
    0,                     /* tp_traverse */
    0,                     /* tp_clear */
    0,                     /* tp_richcompare */
    0,                     /* tp_weaklistoffset */
    0,                     /* tp_iter */
    0,                     /* tp_iternext */
    Buffer_methods,             /* tp_methods */
    0,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Buffer_init,      /* tp_init */
    0,
    0,//Buffer_new,
};

static PyMethodDef cbuf_methods[] = {
    {NULL}
};

#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC
initcbuf(void)
{
    PyObject *m;

    diesel_Buffer.tp_new = PyType_GenericNew;
    if (PyType_Ready(&diesel_Buffer) < 0) {
        return;
    }

    m = Py_InitModule3("diesel.cbuf", cbuf_methods, "Contains the Buffer.");

    Py_INCREF(&diesel_Buffer);
    PyModule_AddObject(m, "Buffer", (PyObject *)&diesel_Buffer);
}

