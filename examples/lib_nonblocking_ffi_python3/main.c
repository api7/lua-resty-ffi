#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <string.h>

static PyThreadState *mainThreadState = NULL;

typedef struct {
    char* module;
    char* func;
    char* cfg;
    void* tq;
} state_t;

void *threadFunc( void *p )
{
    state_t *state = p;
    char* module = state->module;
    const char* func = state->func;

    char* last = strrchr(module, '/');
    int dirlen;
    char *dir = NULL;
    if (last != NULL) {
        dirlen = strlen(module) - strlen(last);
        dir = strndup(module, dirlen);
        module = last + 1;
    }

    PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue;

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    if (dir != NULL) {
        PyObject* sys = PyImport_ImportModule( "sys" );
        PyObject* sys_path = PyObject_GetAttrString( sys, "path" );
        PyObject* folder_path = PyUnicode_FromString( dir );
        PyList_Insert( sys_path, 0, folder_path );
    }

    pName = PyUnicode_DecodeFSDefault(module);
    /* Error checking of pName left out */

    pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    if (pModule != NULL) {
        pFunc = PyObject_GetAttrString(pModule, func);
        /* pFunc is a new reference */

        if (pFunc && PyCallable_Check(pFunc)) {
            pArgs = PyTuple_New(3);
            pValue = PyLong_FromLong(666);
            if (!pValue) {
                Py_DECREF(pArgs);
                Py_DECREF(pModule);
                fprintf(stderr, "Cannot convert argument\n");
                goto end;
            }
            /* pValue reference stolen here: */
            PyTuple_SetItem(pArgs, 0, PyLong_FromVoidPtr(state->cfg));
            PyTuple_SetItem(pArgs, 1, pValue);
            PyTuple_SetItem(pArgs, 2, PyLong_FromVoidPtr(state->tq));

            pValue = PyObject_CallObject(pFunc, pArgs);
            Py_DECREF(pArgs);
            if (pValue != NULL) {
                printf("Result of call: %ld\n", PyLong_AsLong(pValue));
                Py_DECREF(pValue);
                goto end;
            } else {
                Py_DECREF(pFunc);
                Py_DECREF(pModule);
                PyErr_Print();
                fprintf(stderr,"Call failed\n");
                goto end;
            }
        } else {
            if (PyErr_Occurred())
                PyErr_Print();
            fprintf(stderr, "Cannot find function \"%s\"\n", func);
        }
        Py_XDECREF(pFunc);
        Py_DECREF(pModule);
    } else {
        PyErr_Print();
        fprintf(stderr, "Failed to load \"%s\"\n", module);
        goto end;
    }

end:
    PyGILState_Release(gstate);
    return NULL;
}

char* nth_strchr(const char* s, int c, int n)
{
    int c_count;
    char* nth_ptr;

    for (c_count=1, nth_ptr = strchr(s, c);
         nth_ptr != NULL && c_count < n && c != 0;
         c_count++)
    {
         nth_ptr = strchr(nth_ptr+1, c);
    }

    return nth_ptr;
}

int lib_nonblocking_ffi_init(char* cfg, int cfg_len, void *tq)
{
    if (mainThreadState == NULL) {
        Py_Initialize();
        mainThreadState = PyEval_SaveThread();
    }

    state_t* state = malloc(sizeof(state_t));
    state->tq = tq;

    char *token, *str, *tofree;
    tofree = str = strdup(cfg);
    token = strsep(&str, ",");
    state->module = strdup(token);
    token = strsep(&str, ",");
    state->func = strdup(token);

    char* pos = nth_strchr(cfg, (int)',', 2);
    state->cfg = pos + 1;

    free(tofree);


    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&thread, &attr, &threadFunc, state);

    //PyEval_RestoreThread(mainThreadState);
    //if (Py_FinalizeEx() < 0) {
    //    return NULL;
    //}

    return 0;
}
