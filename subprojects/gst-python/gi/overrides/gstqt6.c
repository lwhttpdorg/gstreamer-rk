/* -*- Mode: C; ; c-file-style: "k&r"; c-basic-offset: 4 -*- */
/* gst-python
 * Copyright (C) 2024 Martin Rodriguez Reboredo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Martin Rodriguez Reboredo <yakoyoku@gmail.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <Python.h>
#include <pygobject.h>
#include <gst/gst.h>

#include <locale.h>

#define PYGLIB_MODULE_START(symbol, modname)	        \
    static struct PyModuleDef _##symbol##module = {     \
    PyModuleDef_HEAD_INIT,                              \
    modname,                                            \
    NULL,                                               \
    -1,                                                 \
    symbol##_functions,                                 \
    NULL,                                               \
    NULL,                                               \
    NULL,                                               \
    NULL                                                \
};                                                      \
PyMODINIT_FUNC PyInit_##symbol(void);                   \
PyMODINIT_FUNC PyInit_##symbol(void)                    \
{                                                       \
    PyObject *module;                                   \
    module = PyModule_Create(&_##symbol##module);
#define PYGLIB_MODULE_END return module; }

static PyObject *
_gst_qt_element_get_property (PyObject *self, PyObject *args,
    const char *prop, const char *type)
{
  PyTypeObject *gst_element_type;
  PyObject *py_element, *py_mapinfo, *intptr, *ret, *d;
  PyObject *sip_module, *sbk_module;
  PyObject *wrap_module, *quick_module, *item_class, *wrap_instance = NULL;
  GstElement *element;
  void *object = NULL;

  /* Look up Gst.Element parameter */
  gst_element_type = pygobject_lookup_class (gst_element_get_type ());
  if (!PyArg_ParseTuple (args, "O!", gst_element_type, &py_element)) {
    PyErr_BadArgument ();
    return NULL;
  }

  element = GST_ELEMENT (pygobject_get (py_element));
  g_object_get (element, prop, &object, NULL);
  if (!object)
    Py_RETURN_NONE;

  intptr = PyLong_FromSize_t ((size_t) object);
  d = PyDict_Copy (PySys_GetObject ("modules"));
  sip_module = PyDict_GetItemString (d, "PyQt6.sip");
  sbk_module = PyDict_GetItemString (d, "shiboken6.Shiboken");
  if (sip_module) {
    wrap_module = sip_module;
    quick_module = PyDict_GetItemString (d, "PyQt6.QtQuick");
    wrap_instance = PyUnicode_FromString ("wrapinstance");
  } else if (sbk_module) {
    wrap_module = sbk_module;
    quick_module = PyDict_GetItemString (d, "PySide6.QtQuick");
    wrap_instance = PyUnicode_FromString ("wrapInstance");
  }
  Py_DECREF (d);
  if (sip_module && sbk_module) {
    if (PyErr_WarnEx (PyExc_ImportWarning,
        "imported both PyQt6 and PySide6, "
        "which could yield into unexpected results", 1) == -1) {
      Py_XDECREF (wrap_instance);
      return NULL;
    }
  }
  if (!quick_module) {
    Py_XDECREF (wrap_instance);
    return intptr;
  }

  item_class = PyObject_GetAttrString (quick_module, type);
  return PyObject_CallMethodObjArgs (wrap_module, wrap_instance, intptr,
      item_class, NULL);
}

static PyObject *
_gst_qt_element_set_property (PyObject *self, PyObject *args,
    const char *prop, const char *type)
{
  PyTypeObject *gst_element_type;
  PyObject *py_element, *py_obj, *intptr;
  PyObject *sip_module = NULL, *sbk_module = NULL;
  PyObject *quick_class = NULL, *unwrap_instance = NULL;
  GstElement *element;
  void *object = NULL;

  /* Look up Gst.Element and passed QtQuick type parameters */
  gst_element_type = pygobject_lookup_class (gst_element_get_type ());
  if (!PyArg_ParseTuple (args, "O!O", gst_element_type, &py_element, &py_obj)) {
    PyErr_BadArgument ();
    return NULL;
  }
  element = GST_ELEMENT (pygobject_get (py_element));

  if (py_obj == Py_None) {
    g_object_set (element, prop, NULL, NULL);
    Py_RETURN_NONE;
  } else if (PyLong_Check (py_obj)) {
    object = (void *) PyLong_AsSize_t (py_obj);
    g_object_set (element, prop, object, NULL);
    Py_RETURN_NONE;
  }

  PyObject *mro = Py_TYPE (py_obj)->tp_mro;
  for (Py_ssize_t pos = 0; pos < PyTuple_GET_SIZE (mro); pos++) {
    PyObject *cls = PyTuple_GET_ITEM (mro, pos);
    PyObject *name = PyObject_GetAttrString (cls, "__name__");
    PyObject *mod = PyObject_GetAttrString (cls, "__module__");
    if (!mod)
      continue;
    if (PyUnicode_CompareWithASCIIString (mod, "PyQt6.sip") == 0) {
      sip_module = PyImport_Import (mod);
      unwrap_instance = PyUnicode_FromString ("unwrapinstance");
    } else if (PyUnicode_CompareWithASCIIString (mod, "Shiboken") == 0 ||
        PyUnicode_CompareWithASCIIString (mod, "shiboken6.Shiboken") == 0) {
      sbk_module = PyImport_ImportModule ("shiboken6.Shiboken");
      unwrap_instance = PyUnicode_FromString ("getCppPointer");
    }
    if (PyUnicode_CompareWithASCIIString (mod, "PyQt6.QtQuick") == 0 ||
        PyUnicode_CompareWithASCIIString (mod, "PySide6.QtQuick") == 0 &&
        PyUnicode_CompareWithASCIIString (name, type) == 0) {
      quick_class = cls;
    }
  }

  if (!sip_module && !sbk_module || !quick_class) {
    if (!PyErr_Occurred ()) {
      PyErr_Format (PyExc_TypeError,
          "passed %s argument is neither a Qt %s nor an integer", prop, type);
    }
    Py_XDECREF (unwrap_instance);
    return NULL;
  }

  PyObject *ptr;
  if (sip_module) {
    intptr = PyObject_CallMethodOneArg (sip_module, unwrap_instance, py_obj);
  } else if (sbk_module) {
    PyObject *tuple =
        PyObject_CallMethodOneArg (sbk_module, unwrap_instance, py_obj);
    intptr = PyTuple_GET_ITEM (tuple, 0);
    Py_INCREF (intptr);
    Py_DECREF (tuple);
  }
  Py_XDECREF (sip_module);
  Py_XDECREF (sbk_module);
  Py_DECREF (unwrap_instance);

  if (!intptr)
    return NULL;
  object = (void *) PyLong_AsSize_t (intptr);
  Py_DECREF (intptr);

  g_object_set (element, prop, object, NULL);

  Py_RETURN_NONE;
}

static PyObject *
_gst_qt_element_get_root_object (PyObject *self, PyObject *args)
{
  return _gst_qt_element_get_property (self, args, "root-object", "QQuickItem");
}

static PyObject *
_gst_qt_element_set_root_object (PyObject *self, PyObject *args)
{
  return _gst_qt_element_set_property (self, args, "root-object", "QQuickItem");
}

static PyObject *
_gst_qt_element_get_widget (PyObject *self, PyObject *args)
{
  return _gst_qt_element_get_property (self, args, "widget", "QQuickItem");
}

static PyObject *
_gst_qt_element_set_widget (PyObject *self, PyObject *args)
{
  return _gst_qt_element_set_property (self, args, "widget", "QQuickItem");
}

static PyObject *
_gst_qt_element_get_window (PyObject *self, PyObject *args)
{
  return _gst_qt_element_get_property (self, args, "window", "QQuickWindow");
}

static PyObject *
_gst_qt_element_set_window (PyObject *self, PyObject *args)
{
  return _gst_qt_element_set_property (self, args, "window", "QQuickWindow");
}

static PyMethodDef _gi_gstqt6_functions[] = {
  {"get_root_object", (PyCFunction) _gst_qt_element_get_root_object,
      METH_VARARGS, NULL},
  {"set_root_object", (PyCFunction) _gst_qt_element_set_root_object,
      METH_VARARGS, NULL},
  {"get_widget", (PyCFunction) _gst_qt_element_get_widget, METH_VARARGS,
      NULL},
  {"set_widget", (PyCFunction) _gst_qt_element_set_widget, METH_VARARGS,
      NULL},
  {"get_window", (PyCFunction) _gst_qt_element_get_window, METH_VARARGS,
      NULL},
  {"set_window", (PyCFunction) _gst_qt_element_set_window, METH_VARARGS,
      NULL},
  {NULL, NULL, 0, NULL}
};

PYGLIB_MODULE_START (_gi_gstqt6, "_gi_gstqt6")
{
  /* gst should have been initialized already */

  /* Initialize debugging category */
  // GST_DEBUG_CATEGORY_INIT (pygstqt6_debug, "pygstqt6", 0,
  //     "GStreamer Qt python bindings");

  pygobject_init (3, 0, 0);
}

PYGLIB_MODULE_END;
