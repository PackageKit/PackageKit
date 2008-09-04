/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <gmodule.h>

#include <Python.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-enum.h>

#include "egg-debug.h"
#include "pk-backend-internal.h"
#include "pk-backend-python.h"
#include "pk-marshal.h"
#include "pk-enum.h"
#include "pk-time.h"
#include "pk-inhibit.h"

#define PK_BACKEND_PYTHON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_PYTHON, PkBackendPythonPrivate))

struct PkBackendPythonPrivate
{
	PkBackend		*backend;
	PyObject		*pModule;
	PyObject		*pInstance;
};

G_DEFINE_TYPE (PkBackendPython, pk_backend_python, G_TYPE_OBJECT)
static gpointer pk_backend_python_object = NULL;

static PyObject *pk_backend_python_repo_detail_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_status_changed_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_percentage_changed_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_sub_percentage_changed_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_package_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_details_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_files_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_update_detail_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_finished_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_allow_cancel_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_error_code_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_require_restart_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_message_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_repo_signature_required_cb (PyObject *self, PyObject *args);
static PyObject *pk_backend_python_eula_required_cb (PyObject *self, PyObject *args);

static PyMethodDef PkBackendPythonMethods[] = {
	{"repo_detail", pk_backend_python_repo_detail_cb, METH_VARARGS, ""},
	{"status_changed", pk_backend_python_status_changed_cb, METH_VARARGS, ""},
	{"percentage_changed", pk_backend_python_percentage_changed_cb, METH_VARARGS, ""},
	{"sub_percentage_changed", pk_backend_python_sub_percentage_changed_cb, METH_VARARGS, ""},
	{"package", pk_backend_python_package_cb, METH_VARARGS, ""},
	{"details", pk_backend_python_details_cb, METH_VARARGS, ""},
	{"files", pk_backend_python_files_cb, METH_VARARGS, ""},
	{"update_detail", pk_backend_python_update_detail_cb, METH_VARARGS, ""},
	{"finished", pk_backend_python_finished_cb, METH_VARARGS, ""},
	{"allow_cancel", pk_backend_python_allow_cancel_cb, METH_VARARGS, ""},
	{"error_code", pk_backend_python_error_code_cb, METH_VARARGS, ""},
	{"require_restart", pk_backend_python_require_restart_cb, METH_VARARGS, ""},
	{"message", pk_backend_python_message_cb, METH_VARARGS, ""},
	{"repo_signature_required", pk_backend_python_repo_signature_required_cb, METH_VARARGS, ""},
	{"eula_required", pk_backend_python_eula_required_cb, METH_VARARGS, ""},
	{NULL, NULL, 0, NULL}
};

/**
 * pk_backend_python_repo_detail_cb:
 **/
static PyObject *
pk_backend_python_repo_detail_cb (PyObject *self, PyObject *args)
{
	const gchar *repo_id;
	const gchar *description;
	gboolean enabled;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "ssi", &repo_id, &description, &enabled);

	pk_backend_repo_detail (python->priv->backend, repo_id, description, enabled);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_status_changed_cb:
 **/
static PyObject *
pk_backend_python_status_changed_cb (PyObject *self, PyObject *args)
{
	const gchar *status_text;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "s", &status_text);

	pk_backend_set_status (python->priv->backend, pk_status_enum_from_text (status_text));
	return Py_BuildValue("");
}

/**
 * pk_backend_python_percentage_changed_cb:
 **/
static PyObject *
pk_backend_python_percentage_changed_cb (PyObject *self, PyObject *args)
{
	guint percentage;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "i", &percentage);

	pk_backend_set_percentage (python->priv->backend, percentage);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_sub_percentage_changed_cb:
 **/
static PyObject *
pk_backend_python_sub_percentage_changed_cb (PyObject *self, PyObject *args)
{
	guint sub_percentage;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "i", &sub_percentage);

	pk_backend_set_sub_percentage (python->priv->backend, sub_percentage);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_package_cb:
 **/
static PyObject *
pk_backend_python_package_cb (PyObject *self, PyObject *args)
{
	const gchar *info_text;
	const gchar *package_id;
	const gchar *summary;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "sss", &info_text, &package_id, &summary);

	pk_backend_package (python->priv->backend, pk_info_enum_from_text (info_text), package_id, summary);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_details_cb:
 **/
static PyObject *
pk_backend_python_details_cb (PyObject *self, PyObject *args)
{
	const gchar *package_id;
	const gchar *license;
	const gchar *group_text;
	const gchar *detail;
	const gchar *url;
	guint64 size;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "sssssi", &package_id, &license, &group_text, &detail, &url, &size);

	pk_backend_details (python->priv->backend, package_id,
			    license, pk_group_enum_from_text (group_text),
			    detail, url, size);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_files_cb:
 **/
static PyObject *
pk_backend_python_files_cb (PyObject *self, PyObject *args)
{
	const gchar *package_id;
	const gchar *file_list;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "ss", &package_id, &file_list);

	pk_backend_files (python->priv->backend, package_id, file_list);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_update_detail_cb:
 **/
static PyObject *
pk_backend_python_update_detail_cb (PyObject *self, PyObject *args)
{
	const gchar *package_id;
	const gchar *updates;
	const gchar *obsoletes;
	const gchar *vendor_url;
	const gchar *bugzilla_url;
	const gchar *cve_url;
	const gchar *restart_text;
	const gchar *update_text;
	const gchar *changelog;
	const gchar *state;
	const gchar *issued;
	const gchar *updated;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "ssssssssssss", &package_id, &updates, &obsoletes,
			  &vendor_url, &bugzilla_url, &cve_url, &restart_text,
			  &update_text, &changelog, &state, &issued, &updated);

	pk_backend_update_detail (python->priv->backend, package_id, updates,
				  obsoletes, vendor_url, bugzilla_url, cve_url,
				  pk_restart_enum_from_text (restart_text),
				  update_text, changelog,
				  pk_update_state_enum_from_text (state),
				  issued, updated);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_finished_cb:
 **/
static PyObject *
pk_backend_python_finished_cb (PyObject *self, PyObject *args)
{
	const gchar *exit_text;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "s", &exit_text);

	egg_debug ("deleting python %p, exit %s", python, exit_text);
	pk_backend_finished (python->priv->backend);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_allow_cancel_cb:
 **/
static PyObject *
pk_backend_python_allow_cancel_cb (PyObject *self, PyObject *args)
{
	gboolean allow_cancel;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "i", &allow_cancel);

	pk_backend_set_allow_cancel (python->priv->backend, allow_cancel);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_error_code_cb:
 **/
static PyObject *
pk_backend_python_error_code_cb (PyObject *self, PyObject *args)
{
	const gchar *error_text;
	const gchar *details;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "ss", &error_text, &details);

	pk_backend_error_code (python->priv->backend, pk_error_enum_from_text (error_text), details);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_require_restart_cb:
 **/
static PyObject *
pk_backend_python_require_restart_cb (PyObject *self, PyObject *args)
{
	const gchar *type_text;
	const gchar *details;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "ss", &type_text, &details);

	pk_backend_require_restart (python->priv->backend, pk_restart_enum_from_text (type_text), details);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_message_cb:
 **/
static PyObject *
pk_backend_python_message_cb (PyObject *self, PyObject *args)
{
	const gchar *message_text;
	const gchar *details;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "ss", &message_text, &details);

	pk_backend_message (python->priv->backend, pk_message_enum_from_text (message_text), details);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_repo_signature_required_cb:
 **/
static PyObject *
pk_backend_python_repo_signature_required_cb (PyObject *self, PyObject *args)
{
	const gchar *package_id;
	const gchar *repository_name;
	const gchar *key_url;
	const gchar *key_userid;
	const gchar *key_id;
	const gchar *key_fingerprint;
	const gchar *key_timestamp;
	const gchar *type_text;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "ssssssss", &package_id, &repository_name,
			  &key_url, &key_userid, &key_id, &key_fingerprint,
			  &key_timestamp, &type_text);

	pk_backend_repo_signature_required (python->priv->backend, package_id, repository_name,
					    key_url, key_userid, key_id, key_fingerprint,
					    key_timestamp, PK_SIGTYPE_ENUM_GPG);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_eula_required_cb:
 **/
static PyObject *
pk_backend_python_eula_required_cb (PyObject *self, PyObject *args)
{
	const gchar *eula_id;
	const gchar *package_id;
	const gchar *vendor_name;
	const gchar *license_agreement;
	PkBackendPython *python = PK_BACKEND_PYTHON (pk_backend_python_object);

	egg_debug ("got signal");
	PyArg_ParseTuple (args, "ssss", &eula_id, &package_id, &vendor_name, &license_agreement);

	pk_backend_eula_required (python->priv->backend, eula_id, package_id,
				  vendor_name, license_agreement);
	return Py_BuildValue("");
}

/**
 * pk_backend_python_import:
 **/
static gboolean
pk_backend_python_import (PkBackendPython *python, const char *name)
{
	PyObject *pName;

	egg_debug ("importing module %s", name);
	pName = PyString_FromString (name);
	python->priv->pModule = PyImport_Import (pName);
	Py_DECREF (pName);

	if (python->priv->pModule == NULL) {
		PyErr_Print ();
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_backend_python_get_instance:
 **/
static gboolean
pk_backend_python_get_instance (PkBackendPython *python)
{
	PyObject *pDict;
	PyObject *pClass;

	/* pDict is a borrowed reference */
	pDict = PyModule_GetDict (python->priv->pModule);

	/* Build the name of a callable class */
	pClass = PyDict_GetItemString (pDict, "PackageKitBackend");

	/* Create an instance of the class */
	if (!PyCallable_Check (pClass)) {
		return FALSE;
	}

	python->priv->pInstance = PyObject_CallObject (pClass, NULL);
	return TRUE;
}

/**
 * pk_backend_python_startup:
 **/
gboolean
pk_backend_python_startup (PkBackendPython *python, const gchar *filename)
{
	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);

	/* import the file */
	pk_backend_python_import (python, filename);
	if (python->priv->pModule == NULL) {
		egg_warning ("Failed to load");
		return FALSE;
	}

	/* get an instance */
	pk_backend_python_get_instance (python);
	if (python->priv->pInstance == NULL) {
		egg_warning ("Failed to get instance");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_backend_python_check_method:
 **/
gboolean
pk_backend_python_check_method (PkBackendPython *python, const gchar *method_name)
{
	gboolean ret = TRUE;
	PyObject *pFunc;

	pFunc = PyObject_GetAttrString (python->priv->pInstance, method_name);
	if (pFunc == NULL) {
		egg_warning ("function NULL");
		return FALSE;
	}
	ret = PyCallable_Check (pFunc);
	if (!ret) {
		egg_warning ("not callable");
	}

	if (pFunc != NULL) {
		Py_DECREF (pFunc);
	}
	return ret;
}

/**
 * pk_backend_python_cancel:
 **/
gboolean
pk_backend_python_cancel (PkBackendPython *python)
{
	const gchar *method = "cancel";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, NULL);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_get_updates:
 **/
gboolean
pk_backend_python_get_updates (PkBackendPython *python)
{
	PkBitfield filters;
	const gchar *method = "get_updates";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}

	filters = pk_backend_get_uint (python->priv->backend, "filters");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)",
			     pk_filter_enums_to_text (filters));
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_get_repo_list:
 **/
gboolean
pk_backend_python_get_repo_list (PkBackendPython *python)
{
	PkBitfield filters;
	const gchar *method = "get_repo_list";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}

	filters = pk_backend_get_uint (python->priv->backend, "filters");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)",
			     pk_filter_enums_to_text (filters));
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_refresh_cache:
 **/
gboolean
pk_backend_python_refresh_cache (PkBackendPython *python)
{
	gboolean force;
	const gchar *method = "refresh_cache";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}

	force = pk_backend_get_bool (python->priv->backend, "force");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(i)", force);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_update_system:
 **/
gboolean
pk_backend_python_update_system (PkBackendPython *python)
{
	const gchar *method = "update_system";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, NULL);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_repo_enable:
 **/
gboolean
pk_backend_python_repo_enable (PkBackendPython *python)
{
	const gchar *rid;
	gboolean enabled;
	const gchar *method = "repo_enable";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}

	rid = pk_backend_get_string (python->priv->backend, "rid");
	enabled = pk_backend_get_bool (python->priv->backend, "enabled");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(si)",
			     rid, enabled);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_repo_set_data:
 **/
gboolean
pk_backend_python_repo_set_data (PkBackendPython *python)
{
	const gchar *rid;
	const gchar *parameter;
	const gchar *value;
	const gchar *method = "repo_set_data";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	rid = pk_backend_get_string (python->priv->backend, "rid");
	parameter = pk_backend_get_string (python->priv->backend, "parameter");
	value = pk_backend_get_string (python->priv->backend, "value");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(sss)",
			     rid, parameter, value);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_resolve:
 **/
gboolean
pk_backend_python_resolve (PkBackendPython *python)
{
	PkBitfield filters;
	gchar **packages;
	const gchar *method = "resolve";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	packages = pk_backend_get_strv (python->priv->backend, "package_ids");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(ss)",
			     pk_filter_enums_to_text (filters), packages);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_rollback:
 **/
gboolean
pk_backend_python_rollback (PkBackendPython *python)
{
	const gchar *transaction_id;
	const gchar *method = "rollback";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	transaction_id = pk_backend_get_string (python->priv->backend, "transaction_id");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)", transaction_id);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_search_name:
 **/
gboolean
pk_backend_python_search_name (PkBackendPython *python)
{
	PkBitfield filters;
	const gchar *search;
	const gchar *method = "search_name";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	search = pk_backend_get_string (python->priv->backend, "search");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(ss)",
			     pk_filter_enums_to_text (filters), search);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_search_details:
 **/
gboolean
pk_backend_python_search_details (PkBackendPython *python)
{
	PkBitfield filters;
	const gchar *search;
	const gchar *method = "search_details";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	search = pk_backend_get_string (python->priv->backend, "search");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(ss)",
			     pk_filter_enums_to_text (filters), search);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_search_group:
 **/
gboolean
pk_backend_python_search_group (PkBackendPython *python)
{
	PkBitfield filters;
	const gchar *search;
	const gchar *method = "search_group";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	search = pk_backend_get_string (python->priv->backend, "search");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(ss)",
			     pk_filter_enums_to_text (filters), search);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_search_file:
 **/
gboolean
pk_backend_python_search_file (PkBackendPython *python)
{
	PkBitfield filters;
	const gchar *search;
	const gchar *method = "search_file";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	search = pk_backend_get_string (python->priv->backend, "search");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(ss)",
			     pk_filter_enums_to_text (filters), search);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_get_depends:
 **/
gboolean
pk_backend_python_get_depends (PkBackendPython *python)
{
	PkBitfield filters;
	gchar **package_ids;
	gboolean recursive;
	const gchar *method = "get_depends";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	recursive = pk_backend_get_bool (python->priv->backend, "recursive");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(sssss)",
			     pk_filter_enums_to_text (filters), package_ids, recursive);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_get_requires:
 **/
gboolean
pk_backend_python_get_requires (PkBackendPython *python)
{
	PkBitfield filters;
	gchar **package_ids;
	gboolean recursive;
	const gchar *method = "get_requires";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	recursive = pk_backend_get_bool (python->priv->backend, "recursive");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(sss)",
			     pk_filter_enums_to_text (filters), package_ids, recursive);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_get_packages:
 **/
gboolean
pk_backend_python_get_packages (PkBackendPython *python)
{
	PkBitfield filters;
	const gchar *method = "get_packages";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)",
			     pk_filter_enums_to_text (filters));
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_download_packages:
 **/
gboolean
pk_backend_python_download_packages (PkBackendPython *python)
{
	gchar **package_ids;
	const gchar *directory;
	const gchar *method = "download_packages";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	directory = pk_backend_get_string (python->priv->backend, "directory");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(ss)", package_ids, directory);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}


/**
 * pk_backend_python_get_update_detail:
 **/
gboolean
pk_backend_python_get_update_detail (PkBackendPython *python)
{
	gchar **package_ids;
	const gchar *method = "get_update_detail";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)", package_ids);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_get_details:
 **/
gboolean
pk_backend_python_get_details (PkBackendPython *python)
{
	gchar **package_ids;
	const gchar *method = "get_details";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)", package_ids);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_get_files:
 **/
gboolean
pk_backend_python_get_files (PkBackendPython *python)
{
	gchar **package_ids;
	const gchar *method = "get_files";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)", package_ids);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_remove_packages:
 **/
gboolean
pk_backend_python_remove_packages (PkBackendPython *python)
{
	gchar **package_ids;
	gboolean allow_deps;
	gboolean autoremove;
	const gchar *method = "remove_packages";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	allow_deps = pk_backend_get_bool (python->priv->backend, "allowdeps");
	autoremove = pk_backend_get_bool (python->priv->backend, "autoremove");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(sii)",
			     package_ids, allow_deps, autoremove);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_install_packages:
 **/
gboolean
pk_backend_python_install_packages (PkBackendPython *python)
{
	gchar **package_ids;
	const gchar *method = "install_packages";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)",
			     package_ids);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_update_packages:
 **/
gboolean
pk_backend_python_update_packages (PkBackendPython *python)
{
	gchar **package_ids;
	const gchar *method = "update_packages";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	package_ids = pk_backend_get_strv (python->priv->backend, "package_ids");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(s)",
			     package_ids);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_install_files:
 **/
gboolean
pk_backend_python_install_files (PkBackendPython *python)
{
	gboolean trusted;
	gchar **full_paths;
	const gchar *method = "install_files";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	trusted = pk_backend_get_bool (python->priv->backend, "trusted");
	full_paths = pk_backend_get_strv (python->priv->backend, "paths");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(bs)",
			     trusted, full_paths);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_service_pack:
 **/
gboolean
pk_backend_python_service_pack (PkBackendPython *python)
{
	const gchar *location;
	gboolean enabled;
	const gchar *method = "service_pack";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	enabled = pk_backend_get_bool (python->priv->backend, "enabled");
	location = pk_backend_get_string (python->priv->backend, "location");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(is)",
			     enabled, location);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_what_provides:
 **/
gboolean
pk_backend_python_what_provides (PkBackendPython *python)
{
	PkBitfield filters;
	PkProvidesEnum provides;
	const gchar *search;
	const gchar *method = "what_provides";

	g_return_val_if_fail (PK_IS_BACKEND_PYTHON (python), FALSE);
	g_return_val_if_fail (python->priv->pInstance != NULL, FALSE);

	if (!pk_backend_python_check_method (python, method)) {
		pk_backend_not_implemented_yet (python->priv->backend, method);
		return FALSE;
	}
	filters = pk_backend_get_uint (python->priv->backend, "filters");
	provides = pk_backend_get_uint (python->priv->backend, "provides");
	search = pk_backend_get_string (python->priv->backend, "search");
	PyObject_CallMethod (python->priv->pInstance, (gchar*) method, "(sss)",
			     pk_filter_enums_to_text (filters),
			     pk_provides_enum_to_text (provides), search);
	pk_backend_finished (python->priv->backend);

	return TRUE;
}

/**
 * pk_backend_python_finalize:
 **/
static void
pk_backend_python_finalize (GObject *object)
{
	PkBackendPython *python;
	g_return_if_fail (PK_IS_BACKEND_PYTHON (object));

	python = PK_BACKEND_PYTHON (object);

	g_object_unref (python->priv->backend);
	Py_Finalize();

	G_OBJECT_CLASS (pk_backend_python_parent_class)->finalize (object);
}

/**
 * pk_backend_python_class_init:
 **/
static void
pk_backend_python_class_init (PkBackendPythonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_python_finalize;
	g_type_class_add_private (klass, sizeof (PkBackendPythonPrivate));
}

/**
 * pk_backend_python_init:
 **/
static void
pk_backend_python_init (PkBackendPython *python)
{
	const gchar *path;

	python->priv = PK_BACKEND_PYTHON_GET_PRIVATE (python);
	python->priv->pModule = NULL;
	python->priv->pInstance = NULL;
	python->priv->backend = pk_backend_new ();

	setenv ("PYTHONPATH", "/home/hughsie/Code/PackageKit/backends/yum3/helpers", 1);
	path = getenv ("PYTHONPATH");
	egg_debug ("PYTHONPATH=%s", path);

	Py_Initialize ();
	Py_InitModule ("PackageKitBaseBackend", PkBackendPythonMethods);
}

/**
 * pk_backend_python_new:
 **/
PkBackendPython *
pk_backend_python_new (void)
{
	if (pk_backend_python_object != NULL) {
		g_object_ref (pk_backend_python_object);
	} else {
		pk_backend_python_object = g_object_new (PK_TYPE_BACKEND_PYTHON, NULL);
		g_object_add_weak_pointer (pk_backend_python_object, &pk_backend_python_object);
	}
	return PK_BACKEND_PYTHON (pk_backend_python_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_backend_test_python (EggTest *test)
{
	if (!egg_test_start (test, "PkBackendPython"))
		return;

	egg_test_end (test);
}
#endif

