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
    char *buf;
    int current_size;
    char *current_pos;
    int max_size;
} diesel_buffer;

typedef struct {
    PyObject_HEAD
    diesel_buffer *internal_buffer;
} Buffer;

void grow_internal_buffer(struct diesel_buffer *internal_buffer, const int size);

struct diesel_buffer *
diesel_buffer_alloc(int startsize)
{
    diesel_buffer *buf;
    if (!(buf = (diesel_buffer *)malloc(sizeof(diesel_buffer))))
        return NULL;
    if (!(buf->buf = (char *)malloc(sizeof(char) * startsize))) {
        free(buf);
        return NULL;
    }
    buf->buf[0] = '\0';
    buf->mtype = UNSET;
    buf->sentinel.term_unset = 1;
    buf->current_size = 0;
    buf->current_pos = buf->buf;
    buf->max_size = startsize;
    return buf;
}

void
grow_internal_buffer(struct diesel_buffer *internal_buffer, const int size)
{
    internal_buffer->max_size += size;
    internal_buffer->buf = (char *)realloc(internal_buffer->buf, internal_buffer->max_size);
    internal_buffer->current_pos = (internal_buffer->buf + internal_buffer->current_size);
}

static PyObject *
Buffer_feed(PyObject *self, PyObject *args, PyObject *kw)
{
    char *s;
    int size;
    Buffer *buf = (Buffer *)self;

    PyArg_ParseTuple(args, "s#", &s, &size);

    if (size + buf->internal_buffer->current_size >= buf->internal_buffer->max_size) {
        grow_internal_buffer(buf->internal_buffer, size);
    }
    memcpy(buf->internal_buffer->current_pos, s, size);
    buf->internal_buffer->current_size += size;
    buf->internal_buffer->current_pos += size;
    return Py_None;
}

static PyObject *
Buffer_set_term(PyObject *self, PyObject *args, PyObject *kw)
{
    PyObject *term;
    PyArg_ParseTuple(args, "O", &term);
    Buffer *buf = (Buffer *)self;

    if (PyString_Check(term)) {
        buf->internal_buffer->mtype = BYTES;
        strcpy(buf->internal_buffer->sentinel.term_bytes, PyString_AsString(term));
    } else if (PyInt_Check(term)) {
        buf->internal_buffer->mtype = INT;
        buf->internal_buffer->sentinel.term_int = PyInt_AsLong(term);
    }
    return Py_None;
}

static PyObject *
Buffer_check(PyObject *self, PyObject *args, PyObject *kw)
{
    diesel_buffer *buf = ((Buffer *)self)->internal_buffer;
    char *match;

    /* XXX beware, broken code to follow!
     *
     * It fails in the following ways:
     *  1) Only matches a byte terminal; will blow up on anything else.
     *  2) Uses strlen to find the length of the terminal (embedded NUL will 
     *     kill it).
     *  3) Doesn't trim the internal *buf and repoint it or *current_pos.
     *
     * BUT ... it does match a byte terminal (and was hacked together over a
     * lunch break).
     */
    match = (char *)memmem(buf->buf, buf->current_size, buf->sentinel.term_bytes, strlen(buf->sentinel.term_bytes));
    if (match) {
        int sz = (match - buf->buf) + strlen(buf->sentinel.term_bytes);
        PyObject *res;
        res = PyString_FromStringAndSize(buf->buf, sz);
        return res;
    } else {
        return Py_None;
    }
}

static void
diesel_buffer_free(diesel_buffer *buf)
{
    free(buf->buf);
    free(buf);
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
    payload = PyString_FromStringAndSize(buf->buf, buf->current_size);
    assert(payload);
    fmtargs = PyTuple_Pack(1, payload);
    fmt = PyString_FromString("Buffer(%r)");
    printf("made fmtargs\n");
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

    diesel_buffer *buf;
    if (!(buf = diesel_buffer_alloc(startsize)))
        return -1;
    ((Buffer *)self)->internal_buffer = buf;
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

