/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2007 Novell, Inc.
 * Copyright (c) 2007 Boyd Timothy <btimothy@gmail.com>
 * Copyright (c) 2007-2008 Stefan Haas <shaas@suse.de>
 * Copyright (c) 2007-2008 Scott Reeves <sreeves@novell.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _ZYPP_EVENTS_H_
#define _ZYPP_EVENTS_H_

#include <stdio.h>
#include <glib.h>
#include <pk-backend.h>
#include <zypp/ZYppCallbacks.h>
#include <zypp/Digest.h>
#include <zypp/KeyRing.h> 

#include "zypp-utils.h"

/*
typedef struct {
	PkBackend *backend;
	guint percentage;
} PercentageData;

static gboolean
emit_sub_percentage (gpointer data)
{
	PercentageData *pd = (PercentageData *)data;
	pk_backend_set_sub_percentage (pd->backend, pd->percentage);
	free (pd);
	return FALSE;
}
*/

namespace ZyppBackend
{

struct ZyppBackendReceiver
{
	PkBackend *_backend;
	gchar *_package_id;
	guint _sub_percentage;

	virtual void initWithBackend (PkBackend *backend)
	{
		_backend = backend;
		_package_id = NULL;
		_sub_percentage = 0;
	}

	virtual void clear_package_id ()
	{
		if (_package_id != NULL) {
			g_free (_package_id);
			_package_id = NULL;
		}
	}

	/**
	 * Build a package_id from the specified zypp::Url.  The returned
	 * gchar * should be freed with g_free ().  Returns NULL if the
	 * URL does not contain information about an RPM.
	 *
	 * Example:
	 *    basename: lynx-2.8.6-63.i586.rpm
	 *    result:   lynx;2.8.6-63;i586;opensuse
	 */
	gchar *
	build_package_id_from_url (const zypp::Url *url)
	{
		gchar *package_id;
		gchar *basename;
		gchar *tmp;

		gchar *arch;
		gchar *edition;
		gchar *name;
		gboolean first_dash_found;

		basename = g_strdup (zypp::Pathname (url->getPathName ()).basename ().c_str());

		tmp = g_strrstr (basename, ".rpm");

		if (tmp == NULL) {
			g_free (basename);
			return NULL;
		}

		// Parse the architecture
		tmp [0] = '\0'; // null-terminate the arch section
		for (tmp--; tmp != basename && tmp [0] != '.'; tmp--) {}
		arch = tmp + 1;

		// Parse the edition
		tmp [0] = '\0'; // null-terminate the edition (version)
		first_dash_found = FALSE;
		for (tmp--; tmp != basename; tmp--) {
			if (tmp [0] == '-') {
				if (first_dash_found == FALSE) {
					first_dash_found = TRUE;
					continue;
				} else {
					break;
				}
			}
		}
		edition = tmp + 1;

		// Parse the name
		tmp [0] = '\0'; // null-terminate the name
		name = basename;

		package_id = pk_package_id_build (name, edition, arch, "opensuse");
		g_free (basename);

		return package_id;
	}

	inline void
	update_sub_percentage (guint percentage)
	{
		// TODO: Figure out this weird bug that libzypp emits a 100
		// at the beginning of installing a package.
		if (_sub_percentage == 0 && percentage == 100)
			return; // can't jump from 0 -> 100 instantly!

		// Only emit a percentage if it's different from the last
		// percentage we emitted and it's divisible by ten.  We
		// don't want to overload dbus/GUI.  Also account for the
		// fact that libzypp may skip over a "divisible by ten"
		// value (i.e., 28, 29, 31, 32).

		// Drop off the least significant digit
		// TODO: Figure out a faster way to drop the least significant digit
		percentage = (percentage / 10) * 10;

		if (percentage <= _sub_percentage)
			return;

		_sub_percentage = percentage;
		//PercentageData *pd = (PercentageData *)malloc (sizeof (PercentageData));
		//pd->backend = _backend;
		//pd->percentage = _sub_percentage;
		//g_idle_add (emit_sub_percentage, pd);
		pk_backend_set_sub_percentage (_backend, _sub_percentage);
	}

	void
	reset_sub_percentage ()
	{
		_sub_percentage = 0;
		pk_backend_set_sub_percentage (_backend, _sub_percentage);
	}
};

struct InstallResolvableReportReceiver : public zypp::callback::ReceiveReport<zypp::target::rpm::InstallResolvableReport>, ZyppBackendReceiver
{
	zypp::Resolvable::constPtr _resolvable;

	virtual void start (zypp::Resolvable::constPtr resolvable)
	{
		clear_package_id ();
		_package_id = zypp_build_package_id_from_resolvable (resolvable->satSolvable ());
		gchar* summary = g_strdup(resolvable->satSolvable ().lookupStrAttribute (zypp::sat::SolvAttr::summary).c_str ());
		//egg_debug ("InstallResolvableReportReceiver::start(): %s", _package_id == NULL ? "unknown" : _package_id);
		if (_package_id != NULL) {
			pk_backend_set_status (_backend, PK_STATUS_ENUM_INSTALL);
			pk_backend_package (_backend, PK_INFO_ENUM_INSTALLING, _package_id, summary);
			reset_sub_percentage ();
		}
		g_free (summary);
	}

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable)
	{
		//egg_debug ("InstallResolvableReportReceiver::progress(), %s:%d", _package_id == NULL ? "unknown" : _package_id, value);
		if (_package_id != NULL)
			update_sub_percentage (value);
		return true;
	}

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description, RpmLevel level)
	{
		//egg_debug ("InstallResolvableReportReceiver::problem()");
		return ABORT;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &reason, RpmLevel level)
	{
		//egg_debug ("InstallResolvableReportReceiver::finish(): %s", _package_id == NULL ? "unknown" : _package_id);
		if (_package_id != NULL) {
			//pk_backend_package (_backend, PK_INFO_ENUM_INSTALLED, _package_id, "TODO: Put the package summary here if possible");
			clear_package_id ();
		}
	}
};

struct RemoveResolvableReportReceiver : public zypp::callback::ReceiveReport<zypp::target::rpm::RemoveResolvableReport>, ZyppBackendReceiver
{
	zypp::Resolvable::constPtr _resolvable;

	virtual void start (zypp::Resolvable::constPtr resolvable)
	{
		clear_package_id ();
		_package_id = zypp_build_package_id_from_resolvable (resolvable->satSolvable ());
		if (_package_id != NULL) {
			pk_backend_set_status (_backend, PK_STATUS_ENUM_REMOVE);
			pk_backend_package (_backend, PK_INFO_ENUM_REMOVING, _package_id, "");
			reset_sub_percentage ();
		}
	}

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable)
	{
		if (_package_id != NULL)
			update_sub_percentage (value);
		return true;
	}

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description)
	{
                pk_backend_error_code (_backend, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, description.c_str ());
		return ABORT;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &reason)
	{
		if (_package_id != NULL) {
			pk_backend_package (_backend, PK_INFO_ENUM_FINISHED, _package_id, "");
			clear_package_id ();
		}
	}
};

struct RepoProgressReportReceiver : public zypp::callback::ReceiveReport<zypp::ProgressReport>, ZyppBackendReceiver
{
	virtual void start (const zypp::ProgressData &data)
	{
		egg_debug ("_____________- RepoProgressReportReceiver::start()___________________");
		reset_sub_percentage ();
	}

	virtual bool progress (const zypp::ProgressData &data)
	{
		//fprintf (stderr, "\n\n----> RepoProgressReportReceiver::progress(), %s:%d\n\n", data.name().c_str(), (int)data.val());
		update_sub_percentage ((int)data.val ());
		return true;
	}

	virtual void finish (const zypp::ProgressData &data)
	{
		//fprintf (stderr, "\n\n----> RepoProgressReportReceiver::finish()\n\n");
	}
};

struct RepoReportReceiver : public zypp::callback::ReceiveReport<zypp::repo::RepoReport>, ZyppBackendReceiver
{
	virtual void start (const zypp::ProgressData &data, const zypp::RepoInfo)
	{
		egg_debug ("______________________ RepoReportReceiver::start()________________________");
		reset_sub_percentage ();
	}

	virtual bool progress (const zypp::ProgressData &data)
	{
		//fprintf (stderr, "\n\n----> RepoReportReceiver::progress(), %s:%d\n", data.name().c_str(), (int)data.val());
		update_sub_percentage ((int)data.val ());
		return true;
	}

	virtual void finish (zypp::Repository source, const std::string &task, zypp::repo::RepoReport::Error error, const std::string &reason)
	{
		//fprintf (stderr, "\n\n----> RepoReportReceiver::finish()\n");
	}
};

struct DownloadProgressReportReceiver : public zypp::callback::ReceiveReport<zypp::media::DownloadProgressReport>, ZyppBackendReceiver
{
	virtual void start (const zypp::Url &file, zypp::Pathname localfile)
	{
		clear_package_id ();
		_package_id = build_package_id_from_url (&file);

		//egg_debug ("DownloadProgressReportReceiver::start():%s --%s\n",
		//		g_strdup (file.asString().c_str()),	g_strdup (localfile.asString().c_str()) );
		if (_package_id != NULL) {
			gchar* summary = g_strdup (file.asString().c_str());
			pk_backend_set_status (_backend, PK_STATUS_ENUM_DOWNLOAD);
			pk_backend_package (_backend, PK_INFO_ENUM_DOWNLOADING, _package_id, summary);
			g_free (summary);
			reset_sub_percentage ();
		}
	}

	virtual bool progress (int value, const zypp::Url &file, double dbps_avg, double dbps_current)
	{
		//fprintf (stderr, "\n\n----> DownloadProgressReportReceiver::progress(), %s:%d\n\n", _package_id == NULL ? "unknown" : _package_id, value);
		if (_package_id != NULL)
			update_sub_percentage (value);
		return true;
	}

	virtual void finish (const zypp::Url & file, Error error, const std::string &konreason)
	{
		//fprintf (stderr, "\n\n----> DownloadProgressReportReceiver::finish(): %s\n", _package_id == NULL ? "unknown" : _package_id);
		clear_package_id ();
	}
};

struct KeyRingReportReceiver : public zypp::callback::ReceiveReport<zypp::KeyRingReport>, ZyppBackendReceiver
{
	virtual zypp::KeyRingReport::KeyTrust askUserToAcceptKey (const zypp::PublicKey &key, const zypp::KeyContext &keycontext)
	{
		if (zypp_signature_required(_backend, key))
			return KEY_TRUST_AND_IMPORT;
		return KEY_DONT_TRUST;
	}

        virtual bool askUserToAcceptUnsignedFile (const std::string &file, const zypp::KeyContext &keycontext)
        {
                gboolean ok = zypp_signature_required(_backend, file);

                return ok;
        }

        virtual bool askUserToAcceptUnknownKey (const std::string &file, const std::string &id, const zypp::KeyContext &keycontext)
        {
                gboolean ok = zypp_signature_required(_backend, file, id);

                return ok;
        }

	virtual bool askUserToAcceptVerificationFailed (const std::string &file, const zypp::PublicKey &key,  const zypp::KeyContext &keycontext)
	{
		gboolean ok = zypp_signature_required(_backend, key);

		return ok;
	}

};

struct DigestReportReceiver : public zypp::callback::ReceiveReport<zypp::DigestReport>, ZyppBackendReceiver
{
	virtual bool askUserToAcceptNoDigest (const zypp::Pathname &file)
	{
		gboolean ok = zypp_signature_required(_backend, file.asString ());

		return ok;
	}

	virtual bool askUserToAccepUnknownDigest (const zypp::Pathname &file, const std::string &name)
	{
		pk_backend_error_code(_backend, PK_ERROR_ENUM_GPG_FAILURE, "Repo: %s Digest: %s", file.c_str (), name.c_str ());
		gboolean ok = zypp_signature_required(_backend, file.asString ());

		return ok;
	}

	virtual bool askUserToAcceptWrongDigest (const zypp::Pathname &file, const std::string &requested, const std::string &found)
	{
		pk_backend_error_code(_backend, PK_ERROR_ENUM_GPG_FAILURE, "For repo %s %s is requested but %s was found!",
				file.c_str (), requested.c_str (), found.c_str ());
		gboolean ok = zypp_signature_required(_backend, file.asString ());

		return ok;
	}
};

struct MediaChangeReportReceiver : public zypp::callback::ReceiveReport<zypp::media::MediaChangeReport>, ZyppBackendReceiver
{
	virtual Action requestMedia (zypp::Url &url, unsigned mediaNr, const std::string &label, zypp::media::MediaChangeReport::Error error, const std::string &description, const std::vector<std::string> & devices, unsigned int &dev_current)
	{
		pk_backend_error_code (_backend, PK_ERROR_ENUM_REPO_NOT_AVAILABLE, description.c_str ());
		// We've to abort here, because there is currently no feasible way to inform the user to insert/change media
		return ABORT;
	}
};

struct ProgressReportReceiver : public zypp::callback::ReceiveReport<zypp::ProgressReport>, ZyppBackendReceiver
{
        virtual void start (const zypp::ProgressData &progress)
        {
                reset_sub_percentage ();
        }

        virtual bool progress (const zypp::ProgressData &progress)
        {
                update_sub_percentage ((int)progress.val ());
		return true;
        }

        virtual void finish (const zypp::ProgressData &progress)
        {
                update_sub_percentage ((int)progress.val ());
        }
};

}; // namespace ZyppBackend

class EventDirector
{
	private:
		EventDirector () {}

		ZyppBackend::RepoReportReceiver _repoReport;
		ZyppBackend::RepoProgressReportReceiver _repoProgressReport;
		ZyppBackend::InstallResolvableReportReceiver _installResolvableReport;
		ZyppBackend::RemoveResolvableReportReceiver _removeResolvableReport;
		ZyppBackend::DownloadProgressReportReceiver _downloadProgressReport;
                ZyppBackend::KeyRingReportReceiver _keyRingReport;
		ZyppBackend::DigestReportReceiver _digestReport;
                ZyppBackend::MediaChangeReportReceiver _mediaChangeReport;
                ZyppBackend::ProgressReportReceiver _progressReport;

	public:
		EventDirector (PkBackend *backend)
		{
			_repoReport.initWithBackend (backend);
			_repoReport.connect ();

			_repoProgressReport.initWithBackend (backend);
			_repoProgressReport.connect ();

			_installResolvableReport.initWithBackend (backend);
			_installResolvableReport.connect ();

			_removeResolvableReport.initWithBackend (backend);
			_removeResolvableReport.connect ();

                        _downloadProgressReport.initWithBackend (backend);
			_downloadProgressReport.connect ();

                        _keyRingReport.initWithBackend (backend);
                        _keyRingReport.connect ();

			_digestReport.initWithBackend (backend);
			_digestReport.connect ();

                        _mediaChangeReport.initWithBackend (backend);
                        _mediaChangeReport.connect ();

                        _progressReport.initWithBackend (backend);
                        _progressReport.connect ();
		}

		~EventDirector ()
		{
			_repoReport.disconnect ();
			_repoProgressReport.disconnect ();
			_installResolvableReport.disconnect ();
			_removeResolvableReport.disconnect ();
			_downloadProgressReport.disconnect ();
                        _keyRingReport.disconnect ();
			_digestReport.disconnect ();
                        _mediaChangeReport.disconnect ();
                        _progressReport.disconnect ();
		}
};


#endif // _ZYPP_EVENTS_H_

