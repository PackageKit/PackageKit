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
	//pk_backend_set_sub_percentage (pd->backend, pd->percentage);
	free (pd);
	return FALSE;
}
*/

namespace ZyppBackend
{

class ZyppBackendReceiver
{
public:
	PkBackendJob *_job;
	gchar *_package_id;
	guint _sub_percentage;

	ZyppBackendReceiver()
	{
		_job = NULL;
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

	inline void
	update_sub_percentage (guint percentage)
	{
		// Only emit a percentage if it's different from the last
		// percentage we emitted and it's divisible by ten.  We
		// don't want to overload dbus/GUI.  Also account for the
		// fact that libzypp may skip over a "divisible by ten"
		// value (i.e., 28, 29, 31, 32).

		MIL << percentage << " " << _sub_percentage << std::endl;
		if (percentage <= _sub_percentage)
			return;

		if (!_package_id) {
			MIL << "percentage without package" << std::endl;
			return;
		}
		
		_sub_percentage = percentage;
		pk_backend_job_set_item_progress(_job, _package_id, PK_STATUS_ENUM_UNKNOWN, _sub_percentage);
	}

	void
	reset_sub_percentage ()
	{
		_sub_percentage = 0;
		//pk_backend_set_sub_percentage (_backend, _sub_percentage);
	}

protected:
	~ZyppBackendReceiver() {} // or a public virtual one
};

struct InstallResolvableReportReceiver : public zypp::callback::ReceiveReport<zypp::target::rpm::InstallResolvableReport>, ZyppBackendReceiver
{
	zypp::Resolvable::constPtr _resolvable;
	bool preparing;
	int last_value;

	virtual void start (zypp::Resolvable::constPtr resolvable)
	{
		clear_package_id ();
		_package_id = zypp_build_package_id_from_resolvable (resolvable->satSolvable ());
		MIL << resolvable << " " << _package_id << std::endl;
		gchar* summary = g_strdup(zypp::asKind<zypp::ResObject>(resolvable)->summary().c_str ());
		if (_package_id != NULL) {
			pk_backend_job_set_status (_job, PK_STATUS_ENUM_INSTALL);
			pk_backend_job_package (_job, PK_INFO_ENUM_INSTALLING, _package_id, summary);
			reset_sub_percentage ();
		}
		// first we prepare then we install
		preparing = true;
		last_value = 0;
		g_free (summary);
	}

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable)
	{
		// we need to have extra logic here as progress is reported twice
		// and PackageKit does not like percentages going back
		if (preparing && value < last_value) 
			preparing = false;
		last_value = value;
		MIL << preparing << " " << value << " " << _package_id << std::endl;
		int perc = 0;
		if (preparing)
			perc = value * 30 / 100;
		else
			perc = 30 + value * 70 / 100;
		update_sub_percentage (perc);
		return true;
	}

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description, RpmLevel level)
	{
		//g_debug ("InstallResolvableReportReceiver::problem()");
		return ABORT;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &reason, RpmLevel level)
	{
		MIL << reason << " " << _package_id << " " << resolvable << std::endl;
		if (_package_id != NULL) {
			//pk_backend_job_package (_backend, PK_INFO_ENUM_INSTALLED, _package_id, "TODO: Put the package summary here if possible");
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
			pk_backend_job_set_status (_job, PK_STATUS_ENUM_REMOVE);
			pk_backend_job_package (_job, PK_INFO_ENUM_REMOVING, _package_id, "");
			reset_sub_percentage ();
		}
	}

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable)
	{
		update_sub_percentage (value);
		return true;
	}

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description)
	{
                pk_backend_job_error_code (_job, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, description.c_str ());
		return ABORT;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &reason)
	{
		if (_package_id != NULL) {
			pk_backend_job_package (_job, PK_INFO_ENUM_FINISHED, _package_id, "");
			clear_package_id ();
		}
	}
};

struct RepoProgressReportReceiver : public zypp::callback::ReceiveReport<zypp::ProgressReport>, ZyppBackendReceiver
{
	virtual void start (const zypp::ProgressData &data)
	{
		g_debug ("_____________- RepoProgressReportReceiver::start()___________________");
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
		g_debug ("______________________ RepoReportReceiver::start()________________________");
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

struct DownloadProgressReportReceiver : public zypp::callback::ReceiveReport<zypp::repo::DownloadResolvableReport>, ZyppBackendReceiver
{
	virtual void start (zypp::Resolvable::constPtr resolvable, const zypp::Url &file)
	{
		MIL << resolvable << " " << file << std::endl;
		clear_package_id ();
		_package_id = zypp_build_package_id_from_resolvable (resolvable->satSolvable ());
		gchar* summary = g_strdup(zypp::asKind<zypp::ResObject>(resolvable)->summary().c_str ());

		fprintf (stderr, "DownloadProgressReportReceiver::start():%s --%s\n",
			 g_strdup (file.asString().c_str()),	_package_id);
		if (_package_id != NULL) {
			pk_backend_job_set_status (_job, PK_STATUS_ENUM_DOWNLOAD); 
			pk_backend_job_package (_job, PK_INFO_ENUM_DOWNLOADING, _package_id, summary);
			reset_sub_percentage ();
		}
		g_free(summary);
	}

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable)
	{
		MIL << resolvable << " " << value << " " << _package_id << std::endl;
		update_sub_percentage (value);
		//pk_backend_job_set_speed (_job, static_cast<guint>(dbps_current));
		return true;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &konreason)
	{
		MIL << resolvable << " " << error << " " << _package_id << std::endl;
		update_sub_percentage (100);
		clear_package_id ();
	}
};

struct MediaChangeReportReceiver : public zypp::callback::ReceiveReport<zypp::media::MediaChangeReport>, ZyppBackendReceiver
{
	virtual Action requestMedia (zypp::Url &url, unsigned mediaNr, const std::string &label, zypp::media::MediaChangeReport::Error error, const std::string &description, const std::vector<std::string> & devices, unsigned int &dev_current)
	{
		pk_backend_job_error_code (_job, PK_ERROR_ENUM_REPO_NOT_AVAILABLE, description.c_str ());
		// We've to abort here, because there is currently no feasible way to inform the user to insert/change media
		return ABORT;
	}
};

struct ProgressReportReceiver : public zypp::callback::ReceiveReport<zypp::ProgressReport>, ZyppBackendReceiver
{
        virtual void start (const zypp::ProgressData &progress)
        {
		MIL << std::endl;
                reset_sub_percentage ();
        }

        virtual bool progress (const zypp::ProgressData &progress)
        {
		MIL << progress.val() << std::endl;
                update_sub_percentage ((int)progress.val ());
		return true;
        }

        virtual void finish (const zypp::ProgressData &progress)
        {
		MIL << progress.val() << std::endl;
                update_sub_percentage ((int)progress.val ());
        }
};

// These last two are called -only- from zypp_refresh_meta_and_cache
// *if this is not true* - we will get un-caught Abort exceptions.

struct KeyRingReportReceiver : public zypp::callback::ReceiveReport<zypp::KeyRingReport>, ZyppBackendReceiver
{
	virtual zypp::KeyRingReport::KeyTrust askUserToAcceptKey (const zypp::PublicKey &key, const zypp::KeyContext &keycontext)
	{
		if (zypp_signature_required(_job, key))
			return KEY_TRUST_AND_IMPORT;
		return KEY_DONT_TRUST;
	}

        virtual bool askUserToAcceptUnsignedFile (const std::string &file, const zypp::KeyContext &keycontext)
        {
                return zypp_signature_required (_job, file);
        }

        virtual bool askUserToAcceptUnknownKey (const std::string &file, const std::string &id, const zypp::KeyContext &keycontext)
        {
                return zypp_signature_required(_job, file, id);
        }

	virtual bool askUserToAcceptVerificationFailed (const std::string &file, const zypp::PublicKey &key,  const zypp::KeyContext &keycontext)
	{
		return zypp_signature_required(_job, key);
	}

};

struct DigestReportReceiver : public zypp::callback::ReceiveReport<zypp::DigestReport>, ZyppBackendReceiver
{
	virtual bool askUserToAcceptNoDigest (const zypp::Pathname &file)
	{
		return zypp_signature_required(_job, file.asString ());
	}

	virtual bool askUserToAcceptUnknownDigest (const zypp::Pathname &file, const std::string &name)
	{
		pk_backend_job_error_code(_job, PK_ERROR_ENUM_GPG_FAILURE, "Repo: %s Digest: %s", file.c_str (), name.c_str ());
		return zypp_signature_required(_job, file.asString ());
	}

	virtual bool askUserToAcceptWrongDigest (const zypp::Pathname &file, const std::string &requested, const std::string &found)
	{
		pk_backend_job_error_code(_job, PK_ERROR_ENUM_GPG_FAILURE, "For repo %s %s is requested but %s was found!",
				file.c_str (), requested.c_str (), found.c_str ());
		return zypp_signature_required(_job, file.asString ());
	}
};

}; // namespace ZyppBackend

class EventDirector
{
	private:
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
		EventDirector ()
		{
			_repoReport.connect ();
			_repoProgressReport.connect ();
			_installResolvableReport.connect ();
			_removeResolvableReport.connect ();
			_downloadProgressReport.connect ();
                        _keyRingReport.connect ();
			_digestReport.connect ();
                        _mediaChangeReport.connect ();
                        _progressReport.connect ();
		}

		void setJob(PkBackendJob *job)
		{
			_repoReport._job = job;
			_repoProgressReport._job = job;
			_installResolvableReport._job = job;
			_removeResolvableReport._job = job;
			_downloadProgressReport._job = job;
                        _keyRingReport._job = job;
			_digestReport._job = job;
                        _mediaChangeReport._job = job;
                        _progressReport._job = job;	
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

