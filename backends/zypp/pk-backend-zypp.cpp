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

#include "config.h"

#include <iterator>
#include <list>
#include <map>
#include <pthread.h>
#include <set>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <pk-backend.h>
#include <pk-shared.h>
#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/pk-enum.h>
#include <pk-backend-spawn.h>

#include <zypp/Digest.h>
#include <zypp/KeyRing.h>
#include <zypp/Package.h>
#include <zypp/Patch.h>
#include <zypp/PathInfo.h>
#include <zypp/Pathname.h>
#include <zypp/Pattern.h>
#include <zypp/PoolQuery.h>
#include <zypp/Product.h>
#include <zypp/RelCompare.h>
#include <zypp/RepoInfo.h>
#include <zypp/RepoInfo.h>
#include <zypp/RepoManager.h>
#include <zypp/Repository.h>
#include <zypp/ResFilters.h>
#include <zypp/ResObject.h>
#include <zypp/ResPool.h>
#include <zypp/ResPoolProxy.h>
#include <zypp/Resolvable.h>
#include <zypp/SrcPackage.h>
#include <zypp/TmpPath.h>
#include <zypp/ZYpp.h>
#include <zypp/ZYppCallbacks.h>
#include <zypp/ZYppFactory.h>
#include <zypp/base/Algorithm.h>
#include <zypp/base/Functional.h>
#include <zypp/base/LogControl.h>
#include <zypp/base/Logger.h>
#include <zypp/base/String.h>
#include <zypp/media/MediaManager.h>
#include <zypp/parser/IniDict.h>
#include <zypp/parser/ParseException.h>
#include <zypp/parser/ProductFileReader.h>
#include <zypp/repo/PackageProvider.h>
#include <zypp/repo/RepoException.h>
#include <zypp/repo/SrcPackageProvider.h>
#include <zypp/sat/Pool.h>
#include <zypp/sat/Solvable.h>
#include <zypp/target/rpm/RpmDb.h>
#include <zypp/target/rpm/RpmException.h>
#include <zypp/target/rpm/RpmHeader.h>
#include <zypp/target/rpm/librpmDb.h>
#include <zypp/ui/Selectable.h>

using namespace std;
using namespace zypp;
using zypp::filesystem::PathInfo;

#undef ZYPP_BASE_LOGGER_LOGGROUP
#define ZYPP_BASE_LOGGER_LOGGROUP "packagekit"

typedef enum {
        INSTALL,
        REMOVE,
        UPDATE
} PerformType;


class ZyppJob {
 public:
	ZyppJob(PkBackendJob *job);
	~ZyppJob();
	zypp::ZYpp::Ptr get_zypp();
};

enum PkgSearchType {
	SEARCH_TYPE_NAME = 0,
	SEARCH_TYPE_DETAILS = 1,
	SEARCH_TYPE_FILE = 2,
	SEARCH_TYPE_RESOLVE = 3
};

// helper function to restore the pool status
// after doing operations on it
class PoolStatusSaver : private base::NonCopyable
{
public:
	PoolStatusSaver() {
		ResPool::instance().proxy().saveState();
	}

	~PoolStatusSaver() {
		ResPool::instance().proxy().restoreState();
	}
};

/** A string to store the last refreshed repo
 * this is needed for gpg-key handling stuff (UGLY HACK)
 * FIXME
 */
gchar * _repoName;
/** Used to show/install only an update to ourself. This way if we find a critical bug
 * in the way we update packages we will install the fix before any other updates.
 */
gboolean _updating_self = FALSE;

/**
 * Build a package_id from the specified resolvable.  The returned
 * gchar * should be freed with g_free ().
 */
static gchar *
zypp_build_package_id_from_resolvable (const sat::Solvable &resolvable)
{
	gchar *package_id;
	const char *arch;

	if (isKind<SrcPackage>(resolvable))
		arch = "source";
	else
		arch = resolvable.arch ().asString ().c_str ();

	string repo = resolvable.repository ().alias();
	if (resolvable.isSystem())
		repo = "installed";
	package_id = pk_package_id_build (resolvable.name ().c_str (),
					  resolvable.edition ().asString ().c_str (),
					  arch, repo.c_str ());
	
	return package_id;
}

namespace ZyppBackend
{
class PkBackendZYppPrivate;
static PkBackendZYppPrivate *priv = 0;

class ZyppBackendReceiver
{
public:
	PkBackendJob *_job;
	gchar *_package_id;
	guint _sub_percentage;

	ZyppBackendReceiver() {
		_job = NULL;
		_package_id = NULL;
		_sub_percentage = 0;
	}

	virtual void clear_package_id () {
		if (_package_id != NULL) {
			g_free (_package_id);
			_package_id = NULL;
		}
	}

	bool zypp_signature_required (const PublicKey &key);
	bool zypp_signature_required (const string &file);
	bool zypp_signature_required (const string &file, const string &id);

	void update_sub_percentage (guint percentage) {
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
		
		if (percentage > 100) {
			MIL << "libzypp is silly" << std::endl;
			return;
		}
		
		_sub_percentage = percentage;
		pk_backend_job_set_item_progress(_job, _package_id, PK_STATUS_ENUM_UNKNOWN, _sub_percentage);
	}
	
	void reset_sub_percentage ()
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

	virtual void start (zypp::Resolvable::constPtr resolvable) {
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

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable) {
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

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description, RpmLevel level) {
		//g_debug ("InstallResolvableReportReceiver::problem()");
		return ABORT;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &reason, RpmLevel level) {
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

	virtual void start (zypp::Resolvable::constPtr resolvable) {
		clear_package_id ();
		_package_id = zypp_build_package_id_from_resolvable (resolvable->satSolvable ());
		if (_package_id != NULL) {
			pk_backend_job_set_status (_job, PK_STATUS_ENUM_REMOVE);
			pk_backend_job_package (_job, PK_INFO_ENUM_REMOVING, _package_id, "");
			reset_sub_percentage ();
		}
	}

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable) {
		update_sub_percentage (value);
		return true;
	}

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description) {
                pk_backend_job_error_code (_job, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, description.c_str ());
		return ABORT;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &reason) {
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
		if (zypp_signature_required(key))
			return KEY_TRUST_AND_IMPORT;
		return KEY_DONT_TRUST;
	}

        virtual bool askUserToAcceptUnsignedFile (const std::string &file, const zypp::KeyContext &keycontext)
        {
                return zypp_signature_required (file);
        }

        virtual bool askUserToAcceptUnknownKey (const std::string &file, const std::string &id, const zypp::KeyContext &keycontext)
        {
                return zypp_signature_required(file, id);
        }

	virtual bool askUserToAcceptVerificationFailed (const std::string &file, const zypp::PublicKey &key,  const zypp::KeyContext &keycontext)
	{
		return zypp_signature_required(key);
	}

};

struct DigestReportReceiver : public zypp::callback::ReceiveReport<zypp::DigestReport>, ZyppBackendReceiver
{
	virtual bool askUserToAcceptNoDigest (const zypp::Pathname &file)
	{
		return zypp_signature_required(file.asString ());
	}

	virtual bool askUserToAcceptUnknownDigest (const zypp::Pathname &file, const std::string &name)
	{
		pk_backend_job_error_code(_job, PK_ERROR_ENUM_GPG_FAILURE, "Repo: %s Digest: %s", file.c_str (), name.c_str ());
		return zypp_signature_required(file.asString ());
	}

	virtual bool askUserToAcceptWrongDigest (const zypp::Pathname &file, const std::string &requested, const std::string &found)
	{
		pk_backend_job_error_code(_job, PK_ERROR_ENUM_GPG_FAILURE, "For repo %s %s is requested but %s was found!",
				file.c_str (), requested.c_str (), found.c_str ());
		return zypp_signature_required(file.asString ());
	}
};

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

class PkBackendZYppPrivate {
 public:
	std::vector<std::string> signatures;
	EventDirector eventDirector;
	PkBackendJob *currentJob;
	
	pthread_mutex_t zypp_mutex;
};

}; // namespace ZyppBackend

using namespace ZyppBackend;

ZyppJob::ZyppJob(PkBackendJob *job)
{
	MIL << "locking zypp" << std::endl;
	pthread_mutex_lock(&priv->zypp_mutex);

	if (priv->currentJob) {
		MIL << "currentjob is already defined - highly impossible" << endl;
	}
	
	pk_backend_job_set_locked(job, true);
	priv->currentJob = job;
	priv->eventDirector.setJob(job);
}

ZyppJob::~ZyppJob()
{
	if (priv->currentJob)
		pk_backend_job_set_locked(priv->currentJob, false);
	priv->currentJob = 0;
	priv->eventDirector.setJob(0);
	MIL << "unlocking zypp" << std::endl;
	pthread_mutex_unlock(&priv->zypp_mutex);
}

/**
 * Initialize Zypp (Factory method)
 */
ZYpp::Ptr
ZyppJob::get_zypp()
{
	static gboolean initialized = FALSE;
	ZYpp::Ptr zypp = NULL;

	try {
		zypp = ZYppFactory::instance ().getZYpp ();

		/* TODO: we need to lifecycle manage this, detect changes
		   in the requested 'root' etc. */
		if (!initialized) {
			filesystem::Pathname pathname("/");
			zypp->initializeTarget (pathname);

			initialized = TRUE;
		}
	} catch (const ZYppFactoryException &ex) {
		pk_backend_job_error_code (priv->currentJob, PK_ERROR_ENUM_FAILED_INITIALIZATION, ex.asUserString().c_str() );
		return NULL;
	} catch (const Exception &ex) {
		pk_backend_job_error_code (priv->currentJob, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
		return NULL;
	}

	return zypp;
}




/**
  * Enable and rotate zypp logging
  */
gboolean
zypp_logging ()
{
	gchar *file = g_strdup ("/var/log/pk_backend_zypp");
	gchar *file_old = g_strdup ("/var/log/pk_backend_zypp-1");

	if (g_file_test (file, G_FILE_TEST_EXISTS)) {
		struct stat buffer;
		g_stat (file, &buffer);
		// if the file is bigger than 10 MB rotate
		if ((guint)buffer.st_size > 10485760) {
			if (g_file_test (file_old, G_FILE_TEST_EXISTS))
				g_remove (file_old);
			g_rename (file, file_old);
		}
	}

	base::LogControl::instance ().logfile(file);

	g_free (file);
	g_free (file_old);

	return TRUE;
}

gboolean
zypp_is_changeable_media (const Url &url)
{
	gboolean is_cd = false;
	try {
		media::MediaManager mm;
		media::MediaAccessId id = mm.open (url);
		is_cd = mm.isChangeable (id);
		mm.close (id);
	} catch (const media::MediaException &e) {
		// TODO: Do anything about this?
	}

	return is_cd;
}

namespace {
	/// Helper finding pattern at end or embedded in name.
	/// E.g '-debug' in 'repo-debug' or 'repo-debug-update'
	inline bool
	name_ends_or_contains( const std::string & name_r, const std::string & pattern_r, const char sepchar_r = '-' )
	{
		if ( ! pattern_r.empty() )
		{
			for ( std::string::size_type pos = name_r.find( pattern_r );
			      pos != std::string::npos;
			      pos = name_r.find( pattern_r, pos + pattern_r.size() ) )
			{
				if ( pos + pattern_r.size() == name_r.size()		// at end
				  || name_r[pos + pattern_r.size()] == sepchar_r )	// embedded
					return true;
			}
		}
		return false;
	}
}

gboolean
zypp_is_development_repo (RepoInfo repo)
{
	return ( name_ends_or_contains( repo.alias(), "-debuginfo" )
	      || name_ends_or_contains( repo.alias(), "-debug" )
	      || name_ends_or_contains( repo.alias(), "-source" )
	      || name_ends_or_contains( repo.alias(), "-development" ) );
}

gboolean
zypp_is_valid_repo (PkBackendJob *job, RepoInfo repo)
{

	if (repo.alias().empty()){
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "Repository has no or invalid repo name defined.\n", repo.alias ().c_str ());
		return FALSE;
	}

	if (!repo.url().isValid()){
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "%s: Repository has no or invalid url defined.\n", repo.alias ().c_str ());
		return FALSE;
	}

	return TRUE;
}

/**
 * Build and return a ResPool that contains all local resolvables
 * and ones found in the enabled repositories.
 */
ResPool
zypp_build_pool (ZYpp::Ptr zypp, gboolean include_local)
{
	static gboolean repos_loaded = FALSE;

	// the target is loaded or unloaded on request
	if (include_local) {
		// FIXME have to wait for fix in zypp (repeated loading of target)
		if (sat::Pool::instance().reposFind( sat::Pool::systemRepoAlias() ).solvablesEmpty ())
		{
			// Add local resolvables
			Target_Ptr target = zypp->target ();
			target->load ();
		}
	} else {
		if (!sat::Pool::instance().reposFind( sat::Pool::systemRepoAlias() ).solvablesEmpty ())
		{
			// Remove local resolvables
			Repository repository = sat::Pool::instance ().reposFind (sat::Pool::systemRepoAlias());
			repository.eraseFromPool ();
		}
	}

	// we only load repositories once.
	if (repos_loaded)
		return zypp->pool();

	// Add resolvables from enabled repos
	RepoManager manager;
	try {
		for (RepoManager::RepoConstIterator it = manager.repoBegin(); it != manager.repoEnd(); ++it) {
			RepoInfo repo (*it);

			// skip disabled repos
			if (repo.enabled () == false)
				continue;
			// skip not cached repos
			if (manager.isCached (repo) == false) {
				g_warning ("%s is not cached! Do a refresh", repo.alias ().c_str ());
				continue;
			}
			//FIXME see above, skip already cached repos
			if (sat::Pool::instance().reposFind( repo.alias ()) == Repository::noRepository)
				manager.loadFromCache (repo);

		}
		repos_loaded = true;
	} catch (const repo::RepoNoAliasException &ex) {
		g_error ("Can't figure an alias to look in cache");
	} catch (const repo::RepoNotCachedException &ex) {
		g_error ("The repo has to be cached at first: %s", ex.asUserString ().c_str ());
	} catch (const Exception &ex) {
		g_error ("TODO: Handle exceptions: %s", ex.asUserString ().c_str ());
	}

	return zypp->pool ();
}

/**
* check and warns the user that a repository may be outdated
*/
void
warn_outdated_repos(PkBackendJob *job, const ResPool & pool)
{
	Repository repoobj;
	ResPool::repository_iterator it;
	for ( it = pool.knownRepositoriesBegin();
		it != pool.knownRepositoriesEnd();
		++it )
	{
		Repository repo(*it);
		if ( repo.maybeOutdated() )
		{
			// warn the user
			pk_backend_job_message (job,
					PK_MESSAGE_ENUM_BROKEN_MIRROR,
					str::form("The repository %s seems to be outdated. You may want to try another mirror.",
					repo.alias().c_str()).c_str() );
		}
	}
}

/**
  * Return the rpmHeader of a package
  */
target::rpm::RpmHeader::constPtr
zypp_get_rpmHeader (const string &name, Edition edition)
{
	target::rpm::librpmDb::db_const_iterator it;
	target::rpm::RpmHeader::constPtr result = new target::rpm::RpmHeader ();

	for (it.findPackage (name, edition); *it; ++it) {
		result = *it;
	}

	return result;
}

/**
  * Return the PkEnumGroup of the given PoolItem.
  */
PkGroupEnum
get_enum_group (const string &group_)
{
	string group(str::toLower(group_));

	if (group.find ("amusements") != string::npos) {
		return PK_GROUP_ENUM_GAMES;
	} else if (group.find ("development") != string::npos) {
		return PK_GROUP_ENUM_PROGRAMMING;
	} else if (group.find ("hardware") != string::npos) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.find ("archiving") != string::npos
		  || group.find("clustering") != string::npos
		  || group.find("system/monitoring") != string::npos
		  || group.find("databases") != string::npos
		  || group.find("system/management") != string::npos) {
		return PK_GROUP_ENUM_ADMIN_TOOLS;
	} else if (group.find ("graphics") != string::npos) {
		return PK_GROUP_ENUM_GRAPHICS;
	} else if (group.find ("multimedia") != string::npos) {
		return PK_GROUP_ENUM_MULTIMEDIA;
	} else if (group.find ("network") != string::npos) {
		return PK_GROUP_ENUM_NETWORK;
	} else if (group.find ("office") != string::npos
		  || group.find("text") != string::npos
		  || group.find("editors") != string::npos) {
		return PK_GROUP_ENUM_OFFICE;
	} else if (group.find ("publishing") != string::npos) {
		return PK_GROUP_ENUM_PUBLISHING;
	} else if (group.find ("security") != string::npos) {
		return PK_GROUP_ENUM_SECURITY;
	} else if (group.find ("telephony") != string::npos) {
		return PK_GROUP_ENUM_COMMUNICATION;
	} else if (group.find ("gnome") != string::npos) {
		return PK_GROUP_ENUM_DESKTOP_GNOME;
	} else if (group.find ("kde") != string::npos) {
		return PK_GROUP_ENUM_DESKTOP_KDE;
	} else if (group.find ("xfce") != string::npos) {
		return PK_GROUP_ENUM_DESKTOP_XFCE;
	} else if (group.find ("gui/other") != string::npos) {
		return PK_GROUP_ENUM_DESKTOP_OTHER;
	} else if (group.find ("localization") != string::npos) {
		return PK_GROUP_ENUM_LOCALIZATION;
	} else if (group.find ("system") != string::npos) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.find ("scientific") != string::npos) {
		return PK_GROUP_ENUM_EDUCATION;
	}

	return PK_GROUP_ENUM_UNKNOWN;
}

/**
 * Returns a list of packages that match the specified package_name.
 */
void
zypp_get_packages_by_name (const gchar *package_name,
			   const ResKind kind,
			   vector<sat::Solvable> &result,
			   gboolean include_local = TRUE)
{
	ui::Selectable::Ptr sel( ui::Selectable::get( kind, package_name ) );
	if ( sel ) {
		if ( ! sel->installedEmpty() ) {
			for_( it, sel->installedBegin(), sel->installedEnd() )
				result.push_back( (*it).satSolvable() );
		}
		if ( ! sel->availableEmpty() ) {
			for_( it, sel->availableBegin(), sel->availableEnd() )
				result.push_back( (*it).satSolvable() );
		}
	}
}

/**
 * Returns a list of packages that owns the specified file.
 */
void
zypp_get_packages_by_file (ZYpp::Ptr zypp,
			   const gchar *search_file,
			   vector<sat::Solvable> &ret)
{
	ResPool pool = zypp_build_pool (zypp, TRUE);

	string file (search_file);

	target::rpm::librpmDb::db_const_iterator it;
	target::rpm::RpmHeader::constPtr result = new target::rpm::RpmHeader ();

	for (it.findByFile (search_file); *it; ++it) {
		for (ResPool::byName_iterator it2 = pool.byNameBegin (it->tag_name ()); it2 != pool.byNameEnd (it->tag_name ()); it2++) {
			if ((*it2)->isSystem ())
				ret.push_back ((*it2)->satSolvable ());
		}
	}

	if (ret.empty ()) {
		Capability cap (search_file);
		sat::WhatProvides prov (cap);

		for(sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); ++it) {
			ret.push_back (*it);
		}
	}
}

/**
 * Returns the Resolvable for the specified package_id.
 * e.g. gnome-packagekit;3.6.1-132.1;x86_64;G:F
*/
sat::Solvable
zypp_get_package_by_id (const gchar *package_id)
{
	MIL << package_id << endl;
	if (!pk_package_id_check(package_id)) {
		// TODO: Do we need to do something more for this error?
		return sat::Solvable::noSolvable;
	}

	gchar **id_parts = pk_package_id_split(package_id);
	const gchar *arch = id_parts[PK_PACKAGE_ID_ARCH];
	if (!arch)
		arch = "noarch";
	bool want_source = !g_strcmp0 (arch, "source");
	
	sat::Solvable package;

	ResPool pool = ResPool::instance();

	// Iterate over the resolvables and mark the one we want to check its dependencies
	for (ResPool::byName_iterator it = pool.byNameBegin (id_parts[PK_PACKAGE_ID_NAME]);
	     it != pool.byNameEnd (id_parts[PK_PACKAGE_ID_NAME]); ++it) {
		
		sat::Solvable pkg = it->satSolvable();
		//MIL << "match " << package_id << " " << pkg << endl;

		if (want_source && !isKind<SrcPackage>(pkg)) {
			//MIL << "not a src package\n";
			continue;
		}

		if (!want_source && (isKind<SrcPackage>(pkg) || g_strcmp0 (pkg.arch().c_str(), arch))) {
			//MIL << "not a matching arch\n";
			continue;
		}

		const string &ver = pkg.edition ().asString();
		if (g_strcmp0 (ver.c_str (), id_parts[PK_PACKAGE_ID_VERSION])) {
			//MIL << "not a matching version\n";
			continue;
		}

		if (!pkg.isSystem()) {
			if (!strncmp(id_parts[PK_PACKAGE_ID_DATA], "installed", 9)) {
				//MIL << "pkg is not installed\n";
				continue;
			}
			if (g_strcmp0(pkg.repository().alias().c_str(), id_parts[PK_PACKAGE_ID_DATA])) {
				//MIL << "repo does not match\n";
				continue;
			}
		} else if (strncmp(id_parts[PK_PACKAGE_ID_DATA], "installed", 9)) {
			//MIL << "pkg installed\n";
			continue;
		}

		MIL << "found " << pkg << endl;
		package = pkg;
		break;
	}

	g_strfreev (id_parts);
	return package;
}

RepoInfo
zypp_get_Repository (PkBackendJob *job, const gchar *alias)
{
	RepoInfo info;

	try {
		RepoManager manager;
		info = manager.getRepositoryInfo (alias);
	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		return RepoInfo ();
	}

	return info;
}

/*
 * PK requires a transaction (backend method call) to abort immediately
 * after an error is set. Unfortunately, zypp's 'refresh' methods call
 * these signature methods on errors - and provide no way to signal an
 * abort - instead the refresh continuing on with the next repository.
 * PK (pk_backend_error_timeout_delay_cb) uses this as an excuse to
 * abort the (still running) transaction, and to start another - which
 * leads to multi-threaded use of zypp and hence sudden, random death.
 *
 * To cure this, we throw this custom exception across zypp and catch
 * it outside (hopefully) the only entry point (zypp_refresh_meta_and_cache)
 * that can cause these (zypp_signature_required) methods to be called.
 *
 */
class AbortTransactionException {
 public:
	AbortTransactionException() {}
};

/**
 * helper to refresh a repo's metadata and cache, catching signature
 * exceptions in a safe way.
 */
static gboolean
zypp_refresh_meta_and_cache (RepoManager &manager, RepoInfo &repo, bool force = false)
{
	try {
		if (manager.checkIfToRefreshMetadata (repo, repo.url())    //RepoManager::RefreshIfNeededIgnoreDelay)
		    != RepoManager::REFRESH_NEEDED)
			return TRUE;

		sat::Pool pool = sat::Pool::instance ();
		// Erase old solv file
		pool.reposErase (repo.alias ());
		manager.refreshMetadata (repo, force ?
					 RepoManager::RefreshForced :
					 RepoManager::RefreshIfNeededIgnoreDelay);
		manager.buildCache (repo, force ?
				    RepoManager::BuildForced :
				    RepoManager::BuildIfNeeded);
		manager.loadFromCache (repo);
		return TRUE;
	} catch (const AbortTransactionException &ex) {
		return FALSE;
	}
}


static gboolean
system_and_package_are_x86 (sat::Solvable item)
{
	// i586, i686, ... all should be considered the same arch for our comparison
	return ( item.arch() == Arch_i586 && ZConfig::defaultSystemArchitecture() == Arch_i686 );
}

static gboolean
zypp_package_is_devel (const sat::Solvable &item)
{
	const string &name = item.name();
	const char *cstr = name.c_str();

	return ( g_str_has_suffix (cstr, "-debuginfo") ||
		 g_str_has_suffix (cstr, "-debugsource") ||
		 g_str_has_suffix (cstr, "-devel") );
}

/**
 * should we omit a solvable from a result because of filtering ?
 */
static gboolean
zypp_filter_solvable (PkBitfield filters, const sat::Solvable &item)
{
	// iterate through the given filters
	if (!filters)
		return FALSE;

	for (guint i = 0; i < PK_FILTER_ENUM_LAST; i++) {
		if ((filters & pk_bitfield_value (i)) == 0)
			continue;
		if (i == PK_FILTER_ENUM_INSTALLED && !(item.isSystem ()))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_INSTALLED && item.isSystem ())
			return TRUE;
		if (i == PK_FILTER_ENUM_ARCH) {
			if (item.arch () != ZConfig::defaultSystemArchitecture () &&
			    item.arch () != Arch_noarch &&
			    ! system_and_package_are_x86 (item))
				return TRUE;
		}
		if (i == PK_FILTER_ENUM_NOT_ARCH) {
			if (item.arch () == ZConfig::defaultSystemArchitecture () ||
			    system_and_package_are_x86 (item))
				return TRUE;
		}
		if (i == PK_FILTER_ENUM_SOURCE && !(isKind<SrcPackage>(item)))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_SOURCE && isKind<SrcPackage>(item))
			return TRUE;
		if (i == PK_FILTER_ENUM_DEVELOPMENT && !zypp_package_is_devel (item))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_DEVELOPMENT && zypp_package_is_devel (item))
			return TRUE;

		// FIXME: add more enums - cf. libzif logic and pk-enum.h
		// PK_FILTER_ENUM_SUPPORTED,
		// PK_FILTER_ENUM_NOT_SUPPORTED,
	}

	return FALSE;
}

/**
  * helper to emit pk package signals for a backend for a zypp solvable
  */
static void
zypp_backend_package (PkBackendJob *job, PkInfoEnum info,
		      const sat::Solvable &pkg,
		      const char *opt_summary)
{
	gchar *id = zypp_build_package_id_from_resolvable (pkg);
	pk_backend_job_package (job, info, id, opt_summary);
	g_free (id);
}

/*
 * Emit signals for the packages, -but- if we have an installed package
 * we don't notify the client that the package is also available, since
 * PK doesn't handle re-installs (by some quirk).
 */
void
zypp_emit_filtered_packages_in_list (PkBackendJob *job, PkBitfield filters, const vector<sat::Solvable> &v)
{
	typedef vector<sat::Solvable>::const_iterator sat_it_t;

	vector<sat::Solvable> installed;

	// always emit system installed packages first
	for (sat_it_t it = v.begin (); it != v.end (); ++it) {
		if (!it->isSystem() ||
		    zypp_filter_solvable (filters, *it))
			continue;

		zypp_backend_package (job, PK_INFO_ENUM_INSTALLED, *it,
				      make<ResObject>(*it)->summary().c_str());
		installed.push_back (*it);
	}

	// then available packages later
	for (sat_it_t it = v.begin (); it != v.end (); ++it) {
		gboolean match;

		if (it->isSystem() ||
		    zypp_filter_solvable (filters, *it))
			continue;

		match = FALSE;
		for (sat_it_t i = installed.begin (); !match && i != installed.end (); i++) {
			match = it->sameNVRA (*i) &&
				!(!isKind<SrcPackage>(*it) ^
				  !isKind<SrcPackage>(*i));
		}
		if (!match) {
			zypp_backend_package (job, PK_INFO_ENUM_AVAILABLE, *it,
					      make<ResObject>(*it)->summary().c_str());
		}
	}
}



/**
 * Returns a set of all packages the could be updated
 * (you're able to exclude a single (normally the 'patch' repo)
 */
static void
zypp_get_package_updates (string repo, set<PoolItem> &pks)
{
	ResPool pool = ResPool::instance ();

	ResObject::Kind kind = ResTraits<Package>::kind;
	ResPool::byKind_iterator it = pool.byKindBegin (kind);
	ResPool::byKind_iterator e = pool.byKindEnd (kind);

	getZYpp()->resolver()->doUpdate();
	for (; it != e; ++it)
		if (it->status().isToBeInstalled()) {
			ui::Selectable::constPtr s =
				ui::Selectable::get((*it)->kind(), (*it)->name());
			if (s->hasInstalledObj())
				pks.insert(*it);
		}
}

/**
 * Returns a set of all patches the could be installed
 */
static void
zypp_get_patches (PkBackendJob *job, ZYpp::Ptr zypp, set<PoolItem> &patches)
{
	_updating_self = FALSE;
	
	zypp->resolver ()->setIgnoreAlreadyRecommended (TRUE);
	zypp->resolver ()->resolvePool ();

	for (ResPoolProxy::const_iterator it = zypp->poolProxy ().byKindBegin<Patch>();
			it != zypp->poolProxy ().byKindEnd<Patch>(); it ++) {
		// check if the patch is needed and not set to taboo
		if((*it)->isNeeded() && !((*it)->candidateObj ().isUnwanted())) {
			Patch::constPtr patch = asKind<Patch>((*it)->candidateObj ().resolvable ());
			if (_updating_self) {
				if (patch->restartSuggested ())
					patches.insert ((*it)->candidateObj ());
			}
			else
				patches.insert ((*it)->candidateObj ());

			// check if the patch updates libzypp or packageKit and show only these
			if (!_updating_self && patch->restartSuggested ()) {
				_updating_self = TRUE;
				patches.clear ();
				patches.insert ((*it)->candidateObj ());
			}
		}

	}
}

/**
  * Return the best, most friendly selection of update patches and packages that
  * we can find. Also manages _updating_self to prioritise critical infrastructure
  * updates.
  */
static void
zypp_get_updates (PkBackendJob *job, ZYpp::Ptr zypp, set<PoolItem> &candidates)
{
	typedef set<PoolItem>::iterator pi_it_t;
	zypp_get_patches (job, zypp, candidates);

	if (!_updating_self) {
		// exclude the patch-repository
		string patchRepo;
		if (!candidates.empty ()) {
			patchRepo = candidates.begin ()->resolvable ()->repoInfo ().alias ();
		}

		bool hidePackages = false;
		if (PathInfo("/etc/PackageKit/ZYpp.conf").isExist()) {
			parser::IniDict vendorConf(InputStream("/etc/PackageKit/ZYpp.conf"));
			if (vendorConf.hasSection("Updates")) {
				for ( parser::IniDict::entry_const_iterator eit = vendorConf.entriesBegin("Updates");
				      eit != vendorConf.entriesEnd("Updates");
				      ++eit )
				{
					if ((*eit).first == "HidePackages" &&
					    str::strToTrue((*eit).second))
						hidePackages = true;
				}
			}
		}

		if (!hidePackages)
		{
			set<PoolItem> packages;
			zypp_get_package_updates(patchRepo, packages);

			pi_it_t cb = candidates.begin (), ce = candidates.end (), ci;
			for (ci = cb; ci != ce; ++ci) {
				if (!isKind<Patch>(ci->resolvable()))
				continue;

				Patch::constPtr patch = asKind<Patch>(ci->resolvable());

				// Remove contained packages from list of packages to add
				sat::SolvableSet::const_iterator pki;
				Patch::Contents content(patch->contents());
				for (pki = content.begin(); pki != content.end(); ++pki) {

					pi_it_t pb = packages.begin (), pe = packages.end (), pi;
					for (pi = pb; pi != pe; ++pi) {
						if (pi->satSolvable() == sat::Solvable::noSolvable)
							continue;

						if (pi->satSolvable().identical (*pki)) {
							packages.erase (pi);
							break;
						}
					}
				}
			}

			// merge into the list
			candidates.insert (packages.begin (), packages.end ());
		}
	}
}

/**
  * Sets the restart flag of a patch
  */
static void
zypp_check_restart (PkRestartEnum *restart, Patch::constPtr patch)
{
	if (patch == NULL || restart == NULL)
		return;

	// set the restart flag if a restart is needed
	if (*restart != PK_RESTART_ENUM_SYSTEM &&
	    ( patch->reloginSuggested () ||
	      patch->restartSuggested () ||
	      patch->rebootSuggested ()) ) {
		if (patch->reloginSuggested () || patch->restartSuggested ())
			*restart = PK_RESTART_ENUM_SESSION;
		if (patch->rebootSuggested ())
			*restart = PK_RESTART_ENUM_SYSTEM;
	}
}

/**
  * helper to emit pk package status signals based on a ResPool object
  */
static bool
zypp_backend_pool_item_notify (PkBackendJob  *job,
			       const PoolItem &item,
			       gboolean sanity_check = FALSE)
{
	PkInfoEnum status = PK_INFO_ENUM_UNKNOWN;

	if (item.status ().isToBeUninstalledDueToUpgrade ()) {
		MIL << "updating " << item << endl;
		status = PK_INFO_ENUM_UPDATING;
	} else if (item.status ().isToBeUninstalledDueToObsolete ()) {
		status = PK_INFO_ENUM_OBSOLETING;
	} else if (item.status ().isToBeInstalled ()) {
		MIL << "installing " << item << endl;
		status = PK_INFO_ENUM_INSTALLING;
	} else if (item.status ().isToBeUninstalled ()) {
		status = PK_INFO_ENUM_REMOVING;

		const string &name = item.satSolvable().name();
		if (name == "glibc" || name == "PackageKit" ||
		    name == "rpm" || name == "libzypp") {
			pk_backend_job_error_code (job, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,
					       "The package %s is essential to correct operation and cannot be removed using this tool.",
					       name.c_str());
			return false;
		}
	}

	// FIXME: do we need more heavy lifting here cf. zypper's
	// Summary.cc (readPool) to generate _DOWNGRADING types ?
	if (status != PK_INFO_ENUM_UNKNOWN) {
		const string &summary = item.resolvable ()->summary ();
		zypp_backend_package (job, status, item.resolvable()->satSolvable(), summary.c_str ());
	}
	return true;
}

/**
  * simulate, or perform changes in pool to the system
  */
static gboolean
zypp_perform_execution (PkBackendJob *job, ZYpp::Ptr zypp, PerformType type, gboolean force, PkBitfield transaction_flags)
{
	MIL << force << " " << pk_filter_bitfield_to_string(transaction_flags) << endl;
	gboolean ret = FALSE;
	
	PkBackend *backend = PK_BACKEND(pk_backend_job_get_backend(job));
	
	try {
		if (force)
			zypp->resolver ()->setForceResolve (force);

		// Gather up any dependencies
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
		zypp->resolver ()->setIgnoreAlreadyRecommended (TRUE);
		if (!zypp->resolver ()->resolvePool ()) {
			// Manual intervention required to resolve dependencies
			// TODO: Figure out what we need to do with PackageKit
			// to pull off interactive problem solving.

			ResolverProblemList problems = zypp->resolver ()->problems ();
			gchar * emsg = NULL, * tempmsg = NULL;

			for (ResolverProblemList::iterator it = problems.begin (); it != problems.end (); ++it) {
				if (emsg == NULL) {
					emsg = g_strdup ((*it)->description ().c_str ());
				}
				else {
					tempmsg = emsg;
					emsg = g_strconcat (emsg, "\n", (*it)->description ().c_str (), NULL);
					g_free (tempmsg);
				}
			}

			// reset the status of all touched PoolItems
			ResPool pool = ResPool::instance ();
			for (ResPool::const_iterator it = pool.begin (); it != pool.end (); ++it) {
				if (it->status ().isToBeInstalled ())
					it->statusReset ();
			}

			pk_backend_job_error_code (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, emsg);
			g_free (emsg);

			goto exit;
		}

		switch (type) {
			case INSTALL:
				pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
				break;
			case REMOVE:
				pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
				break;
			case UPDATE:
				pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
				break;
		}

		ResPool pool = ResPool::instance ();
		if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
			ret = TRUE;

			MIL << "simulating" << endl;

			for (ResPool::const_iterator it = pool.begin (); it != pool.end (); ++it) {
				if (type == REMOVE && !(*it)->isSystem ()) {
					it->statusReset ();
					continue;
				}
				// for updates we only care for updates
				if (type == UPDATE && it->status ().isToBeUninstalledDueToUpgrade ())
					continue;

				if (!zypp_backend_pool_item_notify (job, *it, TRUE))
					ret = FALSE;
				it->statusReset ();
			}
			goto exit;
		}


		// look for licenses to confirm

		for (ResPool::const_iterator it = pool.begin (); it != pool.end (); ++it) {
			if (it->status ().isToBeInstalled () && !(it->resolvable()->licenseToConfirm().empty ())) {
				gchar *eula_id = g_strdup ((*it)->name ().c_str ());
				gboolean has_eula = pk_backend_is_eula_valid (backend, eula_id);
				if (!has_eula) {
					gchar *package_id = zypp_build_package_id_from_resolvable (it->satSolvable ());
					pk_backend_job_eula_required (job,
							eula_id,
							package_id,
							(*it)->vendor ().c_str (),
							it->resolvable()->licenseToConfirm().c_str ());
					pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_LICENSE_AGREEMENT, "You've to agree/decline a license");
					g_free (package_id);
					g_free (eula_id);
					goto exit;
				}
				g_free (eula_id);
			}
		}

		// Perform the installation
		gboolean only_download = pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);

		ZYppCommitPolicy policy;
		policy.restrictToMedia (0); // 0 == install all packages regardless to media
		if (only_download)
			policy.downloadMode(DownloadOnly);
		else
			policy.downloadMode (DownloadInHeaps);
		
		policy.syncPoolAfterCommit (true);
		if (!pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED))
			policy.rpmNoSignature(true);

		ZYppCommitResult result = zypp->commit (policy);

		bool worked = result.allDone();
		if (only_download)
			worked = result.noError();

		if ( ! worked )
		{
			std::ostringstream todolist;
			char separator = '\0';

			// process all steps not DONE (ERROR and TODO)
			const sat::Transaction & trans( result.transaction() );
			for_( it, trans.actionBegin(~sat::Transaction::STEP_DONE), trans.actionEnd() )
			{
				if ( separator )
					todolist << separator << it->ident();
				else
					{
					todolist << it->ident();
					separator = '\n';
				}
			}

			pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR,
						   "Transaction could not be completed.\n Theses packages could not be installed: %s",
						   todolist.str().c_str());

			goto exit;
		}

		ret = TRUE;
	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
	} catch (const target::rpm::RpmException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, ex.asUserString().c_str () );
	} catch (const Exception &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
	}

 exit:
	/* reset the various options */
	try {
		zypp->resolver ()->setForceResolve (FALSE);
	} catch (const Exception &ex) { /* we tried */ }

	return ret;
}

/**
  * build array of package_id's seperated by blanks out of the capabilities of a solvable
  */
static GPtrArray *
zypp_build_package_id_capabilities (Capabilities caps, gboolean terminate = TRUE)
{
	GPtrArray *package_ids = g_ptr_array_new();

	sat::WhatProvides provs (caps);

	for (sat::WhatProvides::const_iterator it = provs.begin (); it != provs.end (); ++it) {
		gchar *package_id = zypp_build_package_id_from_resolvable (*it);
		g_ptr_array_add(package_ids, package_id);
	}
	if (terminate)
		g_ptr_array_add(package_ids, NULL);
	return package_ids;
}

/**
  * refresh the enabled repositories
  */
static gboolean
zypp_refresh_cache (PkBackendJob *job, ZYpp::Ptr zypp, gboolean force)
{
	MIL << force << endl;
	// This call is needed as it calls initializeTarget which appears to properly setup the keyring

	if (zypp == NULL)
		return  FALSE;
	filesystem::Pathname pathname("/");
	// This call is needed to refresh system rpmdb status while refresh cache
	zypp->finishTarget ();
	zypp->initializeTarget (pathname);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_job_set_percentage (job, 0);

	RepoManager manager;
	list <RepoInfo> repos;
	try
	{
		repos = list<RepoInfo>(manager.repoBegin(),manager.repoEnd());
	}
	catch ( const Exception &e)
	{
		// FIXME: make sure this dumps out the right sring.
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, e.asUserString().c_str() );
		return FALSE;
	}

	int i = 1;
	int num_of_repos = repos.size ();
	gchar *repo_messages = NULL;

	for (list <RepoInfo>::iterator it = repos.begin(); it != repos.end(); ++it, i++) {
		RepoInfo repo (*it);

		if (!zypp_is_valid_repo (job, repo))
			return FALSE;
		if (pk_backend_job_get_is_error_set (job))
			break;

		// skip disabled repos
		if (repo.enabled () == false)
			continue;

		// do as zypper does
		if (!force && !repo.autorefresh())
			continue;

		// skip changeable meda (DVDs and CDs).  Without doing this,
		// the disc would be required to be physically present.
		if (zypp_is_changeable_media (*repo.baseUrlsBegin ()) == true)
			continue;

		try {
			// Refreshing metadata
			g_free (_repoName);
			_repoName = g_strdup (repo.alias ().c_str ());
			zypp_refresh_meta_and_cache (manager, repo, force);
		} catch (const Exception &ex) {
			if (repo_messages == NULL) {
				repo_messages = g_strdup_printf ("%s: %s%s", repo.alias ().c_str (), ex.asUserString ().c_str (), "\n");
			} else {
				repo_messages = g_strdup_printf ("%s%s: %s%s", repo_messages, repo.alias ().c_str (), ex.asUserString ().c_str (), "\n");
			}
			if (repo_messages == NULL || !g_utf8_validate (repo_messages, -1, NULL))
				repo_messages = g_strdup ("A repository could not be refreshed");
			g_strdelimit (repo_messages, "\\\f\r\t", ' ');
			continue;
		}

		// Update the percentage completed
		pk_backend_job_set_percentage (job, i >= num_of_repos ? 100 : (100 * i) / num_of_repos);
	}
	if (repo_messages != NULL)
		pk_backend_job_message (job, PK_MESSAGE_ENUM_CONNECTION_REFUSED, repo_messages);

	g_free (repo_messages);
	return TRUE;
}

/**
  * helper to simplify returning errors
  */
static void
zypp_backend_finished_error (PkBackendJob  *job, PkErrorEnum err_code,
			     const char *format, ...)
{
	va_list args;
	gchar *buffer;

	/* sadly no _va variant for error code setting */
	va_start (args, format);
	buffer = g_strdup_vprintf (format, args);
	va_end (args);

	pk_backend_job_error_code (job, err_code, "%s", buffer);

	g_free (buffer);

	pk_backend_job_finished (job);
}



/**
 * We do not pretend we're thread safe when all we do is having a huge mutex
 */
gboolean
pk_backend_supports_parallelization (PkBackend *backend)
{
        return FALSE;
}


/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("ZYpp package manager");
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return g_strdup ("Boyd Timothy <btimothy@gmail.com>, "
			 "Scott Reeves <sreeves@novell.com>, "
			 "Stefan Haas <shaas@suse.de>"
			 "ZYpp developers <zypp-devel@opensuse.org>");
}

/**
 * pk_backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_initialize (PkBackend *backend)
{
	/* create private area */
	priv = new PkBackendZYppPrivate;
	priv->currentJob = 0;
	priv->zypp_mutex = PTHREAD_MUTEX_INITIALIZER;
	zypp_logging ();

	g_debug ("zypp_backend_initialize");
	//_updating_self = FALSE;
}

/**
 * pk_backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("zypp_backend_destroy");

	g_free (_repoName);
	delete priv;
}


static bool
zypp_is_no_solvable (const sat::Solvable &solv)
{
	return solv.id() == sat::detail::noSolvableId;
}

/**
  * backend_get_requires_thread:
  */
static void
backend_get_requires_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;

	PkBitfield _filters;
	gchar **package_ids;
	gboolean recursive;
	g_variant_get(params, "(t^a&sb)",
		      &_filters,
		      &package_ids,
		      &recursive);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	pk_backend_job_set_percentage (job, 10);

	PoolStatusSaver saver;
	ResPool pool = zypp_build_pool (zypp, true);
	for (uint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);

		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						     "Package couldn't be found");
			return;
		}

		PoolItem package = PoolItem(solvable);

		// get-requires only works for installed packages. It's meaningless for stuff in the repo
		// same with yum backend
		if (!solvable.isSystem ())
			continue;
		// set Package as to be uninstalled
		package.status ().setToBeUninstalled (ResStatus::USER);

		// solver run
		ResPool pool = ResPool::instance ();
		Resolver solver(pool);

		solver.setForceResolve (true);
		solver.setIgnoreAlreadyRecommended (TRUE);

		if (!solver.resolvePool ()) {
			string problem = "Resolution failed: ";
			list<ResolverProblem_Ptr> problems = solver.problems ();
			for (list<ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); ++it){
				problem += (*it)->description ();
			}
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				problem.c_str());
			return;
		}

		// look for packages which would be uninstalled
		bool error = false;
		for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package);
				it != pool.byKindEnd (ResKind::package); ++it) {

			if (!error && !zypp_filter_solvable (_filters, it->resolvable()->satSolvable()))
				error = !zypp_backend_pool_item_notify (job, *it);
		}

		solver.setForceResolve (false);
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_get_requires:
  */
void
pk_backend_get_requires(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_job_thread_create (job, backend_get_requires_thread, NULL, NULL);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_ADMIN_TOOLS,
		PK_GROUP_ENUM_COMMUNICATION,
		PK_GROUP_ENUM_DESKTOP_GNOME,
		PK_GROUP_ENUM_DESKTOP_KDE,
		PK_GROUP_ENUM_DESKTOP_OTHER,
		PK_GROUP_ENUM_DESKTOP_XFCE,
		PK_GROUP_ENUM_EDUCATION,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_GRAPHICS,
		PK_GROUP_ENUM_LOCALIZATION,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_NETWORK,
		PK_GROUP_ENUM_OFFICE,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SECURITY,
		PK_GROUP_ENUM_SYSTEM,
		-1);
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
				       PK_FILTER_ENUM_ARCH,
				       PK_FILTER_ENUM_NEWEST,
				       PK_FILTER_ENUM_SOURCE,
				       -1);
}

/*
 * This method is a bit of a travesty of the complexity of
 * solving dependencies. We try to give a simple answer to
 * "what packages are required for these packages" - but,
 * clearly often there is no simple answer.
 */
static void
backend_get_depends_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBitfield _filters;
	gchar **package_ids;
	gboolean recursive;
	g_variant_get (params, "(t^a&sb)",
		       &_filters,
		       &package_ids,
		       &recursive);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	
	MIL << package_ids[0] << " " << pk_filter_bitfield_to_string (_filters) << endl;

	try
	{
		sat::Solvable solvable = zypp_get_package_by_id(package_ids[0]);
		
		pk_backend_job_set_percentage (job, 20);

		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				"Did not find the specified package.");
			return;
		}

		// Gather up any dependencies
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
		pk_backend_job_set_percentage (job, 60);

		// get dependencies
		Capabilities req = solvable[Dep::REQUIRES];

		// which package each capability
		map<string, sat::Solvable> caps;
		// packages already providing a capability
		vector<string> pkg_names;

		for (Capabilities::const_iterator cap = req.begin (); cap != req.end (); ++cap) {
			g_debug ("get_depends - capability '%s'", cap->asString().c_str());

			if (caps.find (cap->asString ()) != caps.end()) {
				g_debug ("Interesting ! already have capability '%s'", cap->asString().c_str());
				continue;
			}

			// Look for packages providing each capability
			bool have_preference = false;
			sat::Solvable preferred;

			sat::WhatProvides prov_list (*cap);
			for (sat::WhatProvides::const_iterator provider = prov_list.begin ();
			     provider != prov_list.end (); provider++) {

				g_debug ("provider: '%s'", provider->asString().c_str());

				// filter out caps like "rpmlib(PayloadFilesHavePrefix) <= 4.0-1" (bnc#372429)
				if (zypp_is_no_solvable (*provider))
					continue;

				// Is this capability provided by a package we already have listed ?
				if (find (pkg_names.begin (), pkg_names.end(),
					       provider->name ()) != pkg_names.end()) {
					preferred = *provider;
					have_preference = true;
					break;
				}

				// Something is better than nothing
				if (!have_preference) {
					preferred = *provider;
					have_preference = true;

				// Prefer system packages
				} else if (provider->isSystem()) {
					preferred = *provider;
					break;

				} // else keep our first love
			}

			if (have_preference &&
			    find (pkg_names.begin (), pkg_names.end(),
				       preferred.name ()) == pkg_names.end()) {
				caps[cap->asString()] = preferred;
				pkg_names.push_back (preferred.name ());
			}
		}

		// print dependencies
		for (map<string, sat::Solvable>::iterator it = caps.begin ();
		     it != caps.end();
		     ++it) {
			
			// backup sanity check for no-solvables
			if (! it->second.name ().c_str() ||
			    it->second.name ().c_str()[0] == '\0')
				continue;
			
			PoolItem item(it->second);
			PkInfoEnum info = it->second.isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;

			g_debug ("add dep - '%s' '%s' %d [%s]", it->second.name().c_str(),
				 info == PK_INFO_ENUM_INSTALLED ? "installed" : "available",
				 it->second.isSystem(),
				 zypp_filter_solvable (_filters, it->second) ? "don't add" : "add" );

			if (!zypp_filter_solvable (_filters, it->second)) {
				zypp_backend_package (job, info, it->second,
						      item->summary ().c_str());
			}
		}

		pk_backend_job_set_percentage (job, 100);
	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_job_thread_create (job, backend_get_depends_thread, NULL, NULL);
}

static void
backend_get_details_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;

	gchar **package_ids;
	g_variant_get (params, "(^a&s)",
		       &package_ids);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		MIL << package_ids[i] << endl;

		sat::Solvable solv = zypp_get_package_by_id( package_ids[i] );

		ResObject::constPtr obj = make<ResObject>( solv );
		if (obj == NULL) {
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, 
						     "couldn't find package");
			return;
		}

		try {
			Package::constPtr pkg = make<Package>( solv );	// or NULL if not a Package
			Patch::constPtr patch = make<Patch>( solv );	// or NULL if not a Patch

			ByteCount size;
			if ( patch ) {
				Patch::Contents contents( patch->contents() );
				for_( it, contents.begin(), contents.end() ) {
					size += make<ResObject>(*it)->downloadSize();
				}
			}
			else {
				size = obj->isSystem() ? obj->installSize() : obj->downloadSize();
			}

			pk_backend_job_details (job,
				package_ids[i],				// package_id
				(pkg ? pkg->license().c_str() : "" ),	// license is Package attribute
				get_enum_group(pkg ? pkg->group() : ""),// PkGroupEnum
				obj->description().c_str(),		// description is common attibute
				(pkg ? pkg->url().c_str() : "" ),	// url is Package attribute
				(gulong)size);
		} catch (const Exception &ex) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
			return;
		}
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_details_thread, NULL, NULL);
}

static void
backend_get_distro_upgrades_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	vector<parser::ProductFileData> result;
	if (!parser::ProductFileReader::scanDir (functor::getAll (back_inserter (result)), "/etc/products.d")) {
		zypp_backend_finished_error (job, PK_ERROR_ENUM_INTERNAL_ERROR, 
					     "Could not parse /etc/products.d");
		return;
	}

	for (vector<parser::ProductFileData>::iterator it = result.begin (); it != result.end (); ++it) {
		vector<parser::ProductFileData::Upgrade> upgrades = it->upgrades();
		for (vector<parser::ProductFileData::Upgrade>::iterator it2 = upgrades.begin (); it2 != upgrades.end (); it2++) {
			if (it2->notify ()){
				PkDistroUpgradeEnum status = PK_DISTRO_UPGRADE_ENUM_UNKNOWN;
				if (it2->status () == "stable") {
					status = PK_DISTRO_UPGRADE_ENUM_STABLE;
				} else if (it2->status () == "unstable") {
					status = PK_DISTRO_UPGRADE_ENUM_UNSTABLE;
				}
				pk_backend_job_distro_upgrade (job,
							   status,
							   it2->name ().c_str (),
							   it2->summary ().c_str ());
			}
		}
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_job_thread_create (job, backend_get_distro_upgrades_thread, NULL, NULL);
}

static void
backend_refresh_cache_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean force;
	g_variant_get (params, "(b)",
		       &force);

	MIL << force << endl;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	zypp_refresh_cache (job, zypp, force);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_refresh_cache
 */
void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	pk_backend_job_thread_create (job, backend_refresh_cache_thread, NULL, NULL);
}

/* If a critical self update (see qualifying steps below) is available then only show/install that update first.
 1. there is a patch available with the <restart_suggested> tag set
 2. The patch contains the package "PackageKit" or "gnome-packagekit
*/
/*static gboolean
check_for_self_update (PkBackend *backend, set<PoolItem> *candidates)
{
	set<PoolItem>::iterator cb = candidates->begin (), ce = candidates->end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		ResObject::constPtr res = ci->resolvable();
		if (isKind<Patch>(res)) {
			Patch::constPtr patch = asKind<Patch>(res);
			//g_debug ("restart_suggested is %d",(int)patch->restartSuggested());
			if (patch->restartSuggested ()) {
				if (!strcmp (PACKAGEKIT_RPM_NAME, res->satSolvable ().name ().c_str ()) ||
						!strcmp (GNOME_PACKAGKEKIT_RPM_NAME, res->satSolvable ().name ().c_str ())) {
					g_free (update_self_patch_name);
					update_self_patch_name = zypp_build_package_id_from_resolvable (res->satSolvable ());
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}*/

static void
backend_get_updates_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBitfield _filters;
	g_variant_get (params, "(t)",
		       &_filters);

	MIL << pk_filter_bitfield_to_string(_filters) << endl;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	typedef set<PoolItem>::iterator pi_it_t;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	ResPool pool = zypp_build_pool (zypp, TRUE);
	pk_backend_job_set_percentage (job, 40);

	// check if the repositories may be dead (feature #301904)
	warn_outdated_repos (job, pool);

	set<PoolItem> candidates;
	zypp_get_updates (job, zypp, candidates);

	pk_backend_job_set_percentage (job, 80);

	pi_it_t cb = candidates.begin (), ce = candidates.end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		ResObject::constPtr res = ci->resolvable();

		// Emit the package
		PkInfoEnum infoEnum = PK_INFO_ENUM_ENHANCEMENT;
		if (isKind<Patch>(res)) {
			Patch::constPtr patch = asKind<Patch>(res);
			if (patch->category () == "recommended") {
				infoEnum = PK_INFO_ENUM_IMPORTANT;
			} else if (patch->category () == "optional") {
				infoEnum = PK_INFO_ENUM_LOW;
			} else if (patch->category () == "security") {
				infoEnum = PK_INFO_ENUM_SECURITY;
			} else if (patch->category () == "distupgrade") {
				continue;
			} else {
				infoEnum = PK_INFO_ENUM_NORMAL;
			}
		}

		if (!zypp_filter_solvable (_filters, res->satSolvable())) {
			// some package descriptions generate markup parse failures
			// causing the update to show empty package lines, comment for now
			// res->summary ().c_str ());
			// Test if this still happens!
			zypp_backend_package (job, infoEnum, res->satSolvable (),
					      res->summary ().c_str ());
		}
	}

	pk_backend_job_set_percentage (job, 100);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_get_updates
 */
void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	pk_backend_job_thread_create (job, backend_get_updates_thread, NULL, NULL);
}

static void
backend_install_files_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	RepoManager manager;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	PkBitfield transaction_flags;
	gchar **full_paths;
	g_variant_get (params, "(t^a&s)",
		       &transaction_flags,
		       &full_paths);
	
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	// create a temporary directory
	filesystem::TmpDir tmpDir;
	if (tmpDir == NULL) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
			"Could not create a temporary directory");
		return;
	}

	for (guint i = 0; full_paths[i]; i++) {

		// check if file is really a rpm
		Pathname rpmPath (full_paths[i]);
		target::rpm::RpmHeader::constPtr rpmHeader = target::rpm::RpmHeader::readPackage (rpmPath, target::rpm::RpmHeader::NOSIGNATURE);

		if (rpmHeader == NULL) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
				"%s is not valid rpm-File", full_paths[i]);
			return;
		}

		// copy the rpm into tmpdir
		string tempDest = tmpDir.path ().asString () + "/" + rpmHeader->tag_name () + ".rpm";
		if (filesystem::copy (full_paths[i], tempDest) != 0) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
				"Could not copy the rpm-file into the temp-dir");
			return;
		}
	}

	// create a plaindir-repo and cache it
	RepoInfo tmpRepo;

	try {
		tmpRepo.setType(repo::RepoType::RPMPLAINDIR);
		string url = "dir://" + tmpDir.path ().asString ();
		tmpRepo.addBaseUrl(Url::parseUrl(url));
		tmpRepo.setEnabled (true);
		tmpRepo.setAutorefresh (true);
		tmpRepo.setAlias ("PK_TMP_DIR");
		tmpRepo.setName ("PK_TMP_DIR");

		// add Repo to pool
		manager.addRepository (tmpRepo);

		if (!zypp_refresh_meta_and_cache (manager, tmpRepo)) {
			zypp_backend_finished_error (
			  job, PK_ERROR_ENUM_INTERNAL_ERROR, "Can't refresh repositories");
			return;
		}
		zypp_build_pool (zypp, true);

	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
		return;
	}

	Repository repo = ResPool::instance().reposFind("PK_TMP_DIR");

	for_(it, repo.solvablesBegin(), repo.solvablesEnd()){
		MIL << "Setting " << *it << " for installation" << endl;
		PoolItem(*it).status().setToBeInstalled(ResStatus::USER);
	}

	if (!zypp_perform_execution (job, zypp, INSTALL, FALSE, transaction_flags)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED, "Could not install the rpm-file.");
	}

	// remove tmp-dir and the tmp-repo
	try {
		manager.removeRepository (tmpRepo);
	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_install_files
  */
void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
	pk_backend_job_thread_create (job, backend_install_files_thread, NULL, NULL);
}

static void
backend_get_update_detail_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	gchar **package_ids;
	g_variant_get (params, "(^a&s)",
		&package_ids);

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	if (package_ids == NULL) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		MIL << package_ids[i] << " " << solvable << endl;

		Capabilities obs = solvable.obsoletes ();

		GPtrArray *obsoletes = zypp_build_package_id_capabilities (obs, FALSE);

		PkRestartEnum restart = PK_RESTART_ENUM_NONE;

		GPtrArray *bugzilla = g_ptr_array_new();
		GPtrArray *cve = g_ptr_array_new();
		GPtrArray *vendor_urls = g_ptr_array_new();

		if (isKind<Patch>(solvable)) {
			Patch::constPtr patch = make<Patch>(solvable); // may use asKind<Patch> if libzypp-11.6.4 is asserted
			zypp_check_restart (&restart, patch);

			// Building links like "http://www.distro-update.org/page?moo;Bugfix release for kernel;http://www.test.de/bgz;test domain"
			for (Patch::ReferenceIterator it = patch->referencesBegin (); it != patch->referencesEnd (); it ++) {
				if (it.type () == "bugzilla") {
				    g_ptr_array_add(bugzilla, g_strconcat (it.href ().c_str (), (gchar *)NULL));
				} else {
				    g_ptr_array_add(cve, g_strconcat (it.href ().c_str (), (gchar *)NULL));
				}
			}

			sat::SolvableSet content = patch->contents ();

			for (sat::SolvableSet::const_iterator it = content.begin (); it != content.end (); ++it) {
				GPtrArray *nobs = zypp_build_package_id_capabilities (it->obsoletes ());
				int i;
				for (i = 0; nobs->pdata[i]; i++)
				    g_ptr_array_add(obsoletes, nobs->pdata[i]);
			}
		}
		g_ptr_array_add(bugzilla, NULL);
		g_ptr_array_add(cve, NULL);
		g_ptr_array_add(obsoletes, NULL);
		g_ptr_array_add(vendor_urls, NULL);

		pk_backend_job_update_detail (job,
					  package_ids[i],
					  NULL,		// updates TODO with Resolver.installs
					  (gchar **)obsoletes->pdata,
					  (gchar **)vendor_urls->pdata,
					  (gchar **)bugzilla->pdata,	// bugzilla
					  (gchar **)cve->pdata,		// cve
					  restart,	// restart -flag
					  make<ResObject>(solvable)->description().c_str (),	// update-text
					  NULL,		// ChangeLog text
					  PK_UPDATE_STATE_ENUM_UNKNOWN,		// state of the update
					  NULL, // date that the update was issued
					  NULL);	// date that the update was updated

		g_ptr_array_unref(obsoletes);
		g_ptr_array_unref(vendor_urls);
		g_ptr_array_unref(bugzilla);
		g_ptr_array_unref(cve);
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_get_update_detail
  */
void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_update_detail_thread, NULL, NULL);
}

static void
backend_install_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	PoolStatusSaver saver;

	PkBitfield transaction_flags = 0;
	gchar **package_ids;
	
	g_variant_get(params, "(t^a&s)",
		      &transaction_flags,
		      &package_ids);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	// refresh the repos before installing packages
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);
	
	try
	{
		ResPool pool = zypp_build_pool (zypp, TRUE);
		pk_backend_job_set_percentage (job, 10);
		vector<PoolItem> *items = new vector<PoolItem> ();

		guint to_install = 0;
		for (guint i = 0; package_ids[i]; i++) {
			MIL << package_ids[i] << endl;
			sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
			
			to_install++;
			PoolItem item(solvable);
			// set status to ToBeInstalled
			item.status ().setToBeInstalled (ResStatus::USER);
			items->push_back (item);
		
		}
			
		pk_backend_job_set_percentage (job, 40);

		if (!to_install) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED,
				"The packages are already all installed");
			return;
		}

		// Todo: ideally we should call pk_backend_job_package (...
		// PK_INFO_ENUM_DOWNLOADING | INSTALLING) for each package.
		if (!zypp_perform_execution (job, zypp, INSTALL, FALSE, transaction_flags)) {
			// reset the status of the marked packages
			for (vector<PoolItem>::iterator it = items->begin (); it != items->end (); ++it) {
				it->statusReset ();
			}
			delete (items);
			pk_backend_job_finished (job);
			return;
		}
		delete (items);

		pk_backend_job_set_percentage (job, 100);

	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	// For now, don't let the user cancel the install once it's started
	pk_backend_job_set_allow_cancel (job, FALSE);
	pk_backend_job_thread_create (job, backend_install_packages_thread, NULL, NULL);
}


static void
backend_install_signature_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	const gchar *key_id;
	const gchar *package_id;

	g_variant_get(params, "(&s&s)",
		&key_id,
		&package_id);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_SIG_CHECK);
	priv->signatures.push_back ((string)(key_id));

	pk_backend_job_finished (job);
}

/**
 * pk_backend_install_signature:
 */
void
pk_backend_install_signature (PkBackend *backend, PkBackendJob *job, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id)
{
	pk_backend_job_thread_create (job, backend_install_signature_thread, NULL, NULL);
}

static void
backend_remove_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	PoolStatusSaver saver;
	PkBitfield transaction_flags = 0;
	gboolean autoremove = false;
	gboolean allow_deps = false;
	gchar **package_ids;
	vector<PoolItem> *items = new vector<PoolItem> ();

	g_variant_get(params, "(t^a&sbb)",
		      &transaction_flags,
		      &package_ids,
		      &allow_deps,
		      &autoremove);
	
	pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
	pk_backend_job_set_percentage (job, 0);

	Target_Ptr target;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	zypp->resolver()->setCleandepsOnRemove(autoremove);

	target = zypp->target ();

	// Load all the local system "resolvables" (packages)
	target->load ();
	pk_backend_job_set_percentage (job, 10);

	for (guint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		
		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						     "couldn't find package");
			return;
		}
		PoolItem item(solvable);
		if (solvable.isSystem ()) {
			item.status ().setToBeUninstalled (ResStatus::USER);
			items->push_back (item);
		} else {
			item.status ().resetTransact (ResStatus::USER);
		}
	}

	pk_backend_job_set_percentage (job, 40);

	try
	{
		if (!zypp_perform_execution (job, zypp, REMOVE, TRUE, transaction_flags)) {
			//reset the status of the marked packages
			for (vector<PoolItem>::iterator it = items->begin (); it != items->end (); ++it) {
				it->statusReset();
			}
			delete (items);
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_TRANSACTION_ERROR,
				"Couldn't remove the package");
			return;
		}

		delete (items);
		pk_backend_job_set_percentage (job, 100);

	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags,
			    gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_job_thread_create (job, backend_remove_packages_thread, NULL, NULL);
}

static void
backend_resolve_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	gchar **search;
	PkBitfield _filters;
	
	g_variant_get(params, "(t^a&s)",
		      &_filters,
		      &search);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	zypp_build_pool (zypp, TRUE);

	for (uint i = 0; search[i]; i++) {
		MIL << search[i] << " " << pk_filter_bitfield_to_string(_filters) << endl;
		vector<sat::Solvable> v;
		
		/* build a list of packages with this name */
		zypp_get_packages_by_name (search[i], ResKind::package, v);

		/* add source packages */
		if (!pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_SOURCE)) {
			vector<sat::Solvable> src;
			zypp_get_packages_by_name (search[i], ResKind::srcpackage, src);
			v.insert (v.end (), src.begin (), src.end ());
		}

		/* include patches too */
		vector<sat::Solvable> v2;
		zypp_get_packages_by_name (search[i], ResKind::patch, v2);
		v.insert (v.end (), v2.begin (), v2.end ());

		/* include patterns too */
		zypp_get_packages_by_name (search[i], ResKind::pattern, v2);
		v.insert (v.end (), v2.begin (), v2.end ());

		sat::Solvable newest;
		vector<sat::Solvable> pkgs;

		/* Filter the list of packages with this name to 'pkgs' */
		for (vector<sat::Solvable>::iterator it = v.begin (); it != v.end (); ++it) {

			MIL << "found " << *it << endl;

			if (zypp_filter_solvable (_filters, *it) ||
			    zypp_is_no_solvable(*it))
				continue;
			
			if (zypp_is_no_solvable(newest)) {
				newest = *it;
			} else if (it->edition() > newest.edition() || Arch::compare(it->arch(), newest.arch()) > 0) {
				newest = *it;
			}
			MIL << "emit " << *it << endl;
			pkgs.push_back (*it);
		}
		
		if (!zypp_is_no_solvable(newest)) {
			
			/* 'newest' filter support */
			if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NEWEST)) {
				pkgs.clear();
				MIL << "emit just newest " << newest << endl;
				pkgs.push_back (newest);
			} else if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_NEWEST)) {
				pkgs.erase (find (pkgs.begin (), pkgs.end(), newest));
			}
		}
		
		zypp_emit_filtered_packages_in_list (job, _filters, pkgs);
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_resolve_thread, NULL, NULL);
}

static void
backend_find_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	const gchar *search;
	PkRoleEnum role;

	PkBitfield _filters;
	gchar **values;
	g_variant_get(params, "(t^a&s)",
		&_filters,
		&values);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	// refresh the repos before searching
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	search = values[0];  //Fixme - support the possible multiple values (logical OR search)
	role = pk_backend_job_get_role(job);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	vector<sat::Solvable> v;

	PoolQuery q;
	q.addString( search ); // may be called multiple times (OR'ed)
	q.setCaseSensitive( true );
	q.setMatchSubstring();

	switch (role) {
	case PK_ROLE_ENUM_SEARCH_NAME:
		zypp_build_pool (zypp, TRUE); // seems to be necessary?
		q.addKind( ResKind::package );
		q.addKind( ResKind::srcpackage );
		q.addAttribute( sat::SolvAttr::name );
		// Note: The query result is NOT sorted packages first, then srcpackage.
		// If that's necessary you need to sort the vector accordongly or use
		// two separate queries.
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		zypp_build_pool (zypp, TRUE); // seems to be necessary?
		q.addKind( ResKind::package );
		//q.addKind( ResKind::srcpackage );
		q.addAttribute( sat::SolvAttr::name );
		q.addAttribute( sat::SolvAttr::description );
		// Note: Don't know if zypp_get_packages_by_details intentionally
		// did not search in srcpackages.
		break;
	case PK_ROLE_ENUM_SEARCH_FILE: {
		zypp_build_pool (zypp, TRUE);
		q.addKind( ResKind::package );
		q.addAttribute( sat::SolvAttr::name );
		q.addAttribute( sat::SolvAttr::description );
		q.addAttribute( sat::SolvAttr::filelist );
		q.setFilesMatchFullPath(true);
		q.setMatchExact();
		break;
	}
	default:
		break;
	};

	if ( ! q.empty() ) {
		copy( q.begin(), q.end(), back_inserter( v ) );
	}
	zypp_emit_filtered_packages_in_list (job, _filters, v);

	pk_backend_job_finished (job);
}

/**
 * pk_backend_search_name:
 */
void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_find_packages_thread, NULL, NULL);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_find_packages_thread, NULL, NULL);
}

static void
backend_search_group_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	const gchar *group;

	gchar **search;
	PkBitfield _filters;
	g_variant_get(params, "(t^a&s)",
		&_filters,
		&search);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	group = search[0];  //Fixme - add support for possible multiple values.

	if (group == NULL) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_GROUP_NOT_FOUND, "Group is invalid.");
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	ResPool pool = zypp_build_pool (zypp, true);

	pk_backend_job_set_percentage (job, 30);

	vector<sat::Solvable> v;
	PkGroupEnum pkGroup = pk_group_enum_from_string (group);

	sat::LookupAttr look (sat::SolvAttr::group);

	for (sat::LookupAttr::iterator it = look.begin (); it != look.end (); ++it) {
		PkGroupEnum rpmGroup = get_enum_group (it.asString ());
		if (pkGroup == rpmGroup)
			v.push_back (it.inSolvable ());
	}

	pk_backend_job_set_percentage (job, 70);

	zypp_emit_filtered_packages_in_list (job, _filters, v);

	pk_backend_job_set_percentage (job, 100);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_search_group:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_search_group_thread, NULL, NULL);
}

/**
 * pk_backend_search_file:
 */
void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_find_packages_thread, NULL, NULL);
}

/**
 * backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	MIL << endl;

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	list <RepoInfo> repos;
	try
	{
		repos = list<RepoInfo>(manager.repoBegin(),manager.repoEnd());
	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	for (list <RepoInfo>::iterator it = repos.begin(); it != repos.end(); ++it) {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && zypp_is_development_repo (*it))
			continue;
		// RepoInfo::alias - Unique identifier for this source.
		// RepoInfo::name - Short label or description of the
		// repository, to be used on the user interface
		pk_backend_job_repo_detail (job,
					it->alias().c_str(),
					it->name().c_str(),
					it->enabled());
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, PkBackendJob *job, const gchar *rid, gboolean enabled)
{
	MIL << endl;
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	RepoInfo repo;

	try {
		repo = manager.getRepositoryInfo (rid);
		if (!zypp_is_valid_repo (job, repo)){
			pk_backend_job_finished (job);
			return;
		}
		repo.setEnabled (enabled);
		manager.modifyRepository (rid, repo);
		if (!enabled) {
			Repository repository = sat::Pool::instance ().reposFind (repo.alias ());
			repository.eraseFromPool ();
		}

	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

static void
backend_get_files_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;

	gchar **package_ids;
	g_variant_get(params, "(^a&s)",
		      &package_ids);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	for (uint i = 0; package_ids[i]; i++) {
		pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		
		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"couldn't find package");
			return;
		}

		string temp;
		if (solvable.isSystem ()){
			try {
				target::rpm::RpmHeader::constPtr rpmHeader = zypp_get_rpmHeader (solvable.name (), solvable.edition ());
				list<string> files = rpmHeader->tag_filenames ();

				for (list<string>::iterator it = files.begin (); it != files.end (); ++it) {
					temp.append (*it);
					temp.append (";");
				}

			} catch (const target::rpm::RpmException &ex) {
				zypp_backend_finished_error (job, PK_ERROR_ENUM_REPO_NOT_FOUND,
							     "Couldn't open rpm-database");
				return;
			}
		} else {
			temp = "Only available for installed packages";
		}

		pk_backend_job_files (job, package_ids[i], temp.c_str ());	// file_list
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_get_files:
  */
void
pk_backend_get_files(PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_files_thread, NULL, NULL);
}

static void
backend_get_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;

	PkBitfield _filters;
	g_variant_get (params, "(t)",
		       &_filters);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	vector<sat::Solvable> v;

	zypp_build_pool (zypp, TRUE);
	ResPool pool = ResPool::instance ();
	for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package); it != pool.byKindEnd (ResKind::package); ++it) {
		v.push_back (it->satSolvable ());
	}

	zypp_emit_filtered_packages_in_list (job, _filters, v);

	pk_backend_job_finished (job);
}
/**
  * pk_backend_get_packages:
  */
void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filter)
{
	pk_backend_job_thread_create (job, backend_get_packages_thread, NULL, NULL);
}

static void
backend_update_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	PoolStatusSaver saver;

	PkBitfield transaction_flags = 0;
	gchar **package_ids;
	g_variant_get(params, "(t^a&s)",
		      &transaction_flags,
		      &package_ids);
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	ResPool pool = zypp_build_pool (zypp, TRUE);
	PkRestartEnum restart = PK_RESTART_ENUM_NONE;

	for (guint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		PoolItem item(solvable);
		item.status ().setToBeInstalled (ResStatus::USER);
		Patch::constPtr patch = asKind<Patch>(item.resolvable ());
		zypp_check_restart (&restart, patch);
		if (restart != PK_RESTART_ENUM_NONE){
			pk_backend_job_require_restart (job, restart, package_ids[i]);
			restart = PK_RESTART_ENUM_NONE;
		}
	}

	zypp_perform_execution (job, zypp, UPDATE, FALSE, transaction_flags);

	pk_backend_job_finished (job);
}

/**
  * pk_backend_update_packages
  */
void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_update_packages_thread, NULL, NULL);
}

static void
backend_repo_set_data_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	const gchar *repo_id;
	const gchar *parameter;
	const gchar *value;

	g_variant_get(params, "(&s&s&s)",
		      &repo_id,
		      &parameter,
		      &value);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
		
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	RepoInfo repo;

	try {
		pk_backend_job_set_status(job, PK_STATUS_ENUM_SETUP);
		if (g_ascii_strcasecmp (parameter, "add") != 0) {
			repo = manager.getRepositoryInfo (repo_id);
			if (!zypp_is_valid_repo (job, repo)){
				pk_backend_job_finished (job);
				return;
			}
		}
		// add a new repo
		if (g_ascii_strcasecmp (parameter, "add") == 0) {
			repo.setAlias (repo_id);
			repo.setBaseUrl (Url(value));
			repo.setAutorefresh (TRUE);
			repo.setEnabled (TRUE);

			manager.addRepository (repo);

		// remove a repo
		} else if (g_ascii_strcasecmp (parameter, "remove") == 0) {
			manager.removeRepository (repo);
		// set autorefresh of a repo true/false
		} else if (g_ascii_strcasecmp (parameter, "refresh") == 0) {

			if (g_ascii_strcasecmp (value, "true") == 0) {
				repo.setAutorefresh (TRUE);
			} else if (g_ascii_strcasecmp (value, "false") == 0) {
				repo.setAutorefresh (FALSE);
			} else {
				pk_backend_job_message (job, PK_MESSAGE_ENUM_PARAMETER_INVALID, "Autorefresh a repo: Enter true or false");
			}

			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "keep") == 0) {

			if (g_ascii_strcasecmp (value, "true") == 0) {
				repo.setKeepPackages (TRUE);
			} else if (g_ascii_strcasecmp (value, "false") == 0) {
				repo.setKeepPackages (FALSE);
			} else {
				pk_backend_job_message (job, PK_MESSAGE_ENUM_PARAMETER_INVALID, "Keep downloaded packages: Enter true or false");
			}

			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "url") == 0) {
			repo.setBaseUrl (Url(value));
			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "name") == 0) {
			repo.setName(value);
			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "prio") == 0) {
			gint prio = 0;
			gint length = strlen (value);

			if (length > 2) {
				pk_backend_job_message (job, PK_MESSAGE_ENUM_PRIORITY_INVALID, "Priorities has to be between 1 (highest) and 99");
			} else {
				for (gint i = 0; i < length; i++) {
					gint tmp = g_ascii_digit_value (value[i]);

					if (tmp == -1) {
						pk_backend_job_message (job, PK_MESSAGE_ENUM_PRIORITY_INVALID, "Priorities has to be a number between 1 (highest) and 99");
						prio = 0;
						break;
					} else {
						if (length == 2 && i == 0) {
							prio = tmp * 10;
						} else {
							prio = prio + tmp;
						}
					}
				}

				if (prio != 0) {
					repo.setPriority (prio);
					manager.modifyRepository (repo_id, repo);
				}
			}

		} else {
			pk_backend_job_error_code (job, PK_ERROR_ENUM_NOT_SUPPORTED, "Valid parameters for set_repo_data are remove/add/refresh/prio/keep/url/name");
		}

	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't find the specified repository");
	} catch (const repo::RepoAlreadyExistsException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "This repo already exists");
	} catch (const repo::RepoUnknownTypeException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Type of the repo can't be determined");
	} catch (const repo::RepoException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Can't access the given URL");
	} catch (const Exception &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asString ().c_str ());
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_repo_set_data
  */
void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	pk_backend_job_thread_create (job, backend_repo_set_data_thread, NULL, NULL);
}

static void
backend_what_provides_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	
	gchar **values;
	PkBitfield _filters;
	PkProvidesEnum provides;
	g_variant_get(params, "(tu^a&s)",
		      &_filters,
		      &provides,
		      &values);
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	const gchar *search = values[0]; //Fixme - support possible multi1ple search values (logical OR)
	ResPool pool = zypp_build_pool (zypp, true);

	if((provides == PK_PROVIDES_ENUM_HARDWARE_DRIVER) || g_ascii_strcasecmp("drivers_for_attached_hardware", search) == 0) {
		// solver run
		Resolver solver(pool);
		solver.setIgnoreAlreadyRecommended (TRUE);

		if (!solver.resolvePool ()) {
			list<ResolverProblem_Ptr> problems = solver.problems ();
			for (list<ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); ++it){
				g_warning("Solver problem (This should never happen): '%s'", (*it)->description ().c_str ());
			}
			solver.setIgnoreAlreadyRecommended (FALSE);
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Resolution failed");
			return;
		}

		// look for packages which would be installed
		for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package);
				it != pool.byKindEnd (ResKind::package); ++it) {
			PkInfoEnum status = PK_INFO_ENUM_UNKNOWN;

			gboolean hit = FALSE;

			if (it->status ().isToBeInstalled ()) {
				status = PK_INFO_ENUM_AVAILABLE;
				hit = TRUE;
			}

			if (hit && !zypp_filter_solvable (_filters, it->resolvable()->satSolvable())) {
				zypp_backend_package (job, status, it->resolvable()->satSolvable(),
						      it->resolvable ()->summary ().c_str ());
			}
			it->statusReset ();
		}
		solver.setIgnoreAlreadyRecommended (FALSE);
	} else {
		Capability cap (search);
		sat::WhatProvides prov (cap);

		for (sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); ++it) {
			if (zypp_filter_solvable (_filters, *it))
				continue;

			PkInfoEnum info = it->isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
			zypp_backend_package (job, info, *it,  make<ResObject>(*it)->summary().c_str ());
		}
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_what_provides
  */
void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, PkProvidesEnum provide, gchar **values)
{
	pk_backend_job_thread_create (job, backend_what_provides_thread, NULL, NULL);
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = {
				"application/x-rpm",
				NULL };
	return g_strdupv ((gchar **) mime_types);
}

static void
backend_download_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	gchar **package_ids;
	gulong size = 0;
	const gchar *tmpDir;

	g_variant_get(params, "(^a&ss)",
		      &package_ids,
		      &tmpDir);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	try
	{
		ResPool pool = zypp_build_pool (zypp, FALSE);

		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
		for (guint i = 0; package_ids[i]; i++) {
			sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);

			if (zypp_is_no_solvable(solvable)) {
				zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
							     "couldn't find package");
				return;
			}

			PoolItem item(solvable);
			size += 2 * make<ResObject>(solvable)->downloadSize();

			filesystem::Pathname repo_dir = solvable.repository().info().packagesPath();
			struct statfs stat;
			statfs(repo_dir.c_str(), &stat);
			if (size > stat.f_bavail * 4) {
				pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_SPACE_ON_DEVICE,
					"Insufficient space in download directory '%s'.", repo_dir.c_str());
				pk_backend_job_finished (job);
				return;
			}

			repo::RepoMediaAccess access;
			repo::DeltaCandidates deltas;
			ManagedFile tmp_file;
			if (isKind<SrcPackage>(solvable)) {
				SrcPackage::constPtr package = asKind<SrcPackage>(item.resolvable());
				repo::SrcPackageProvider pkgProvider(access);
				tmp_file = pkgProvider.provideSrcPackage(package);
			} else {
				Package::constPtr package = asKind<Package>(item.resolvable());
				repo::PackageProvider pkgProvider(access, package, deltas);
				tmp_file = pkgProvider.providePackage();
			}
			string target = tmpDir;
			// be sure it ends with /
			target += "/";
			target += tmp_file->basename();
			filesystem::hardlinkCopy(tmp_file, target);
			pk_backend_job_files (job, package_ids[i], target.c_str());
			pk_backend_job_package (job, PK_INFO_ENUM_DOWNLOADING, package_ids[i], item->summary ().c_str());
		}
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
	pk_backend_job_thread_create (job, backend_download_packages_thread, NULL, NULL);
}

/**
 * pk_backend_start_job:
 */
void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	gchar *locale;
	gchar *proxy_http;
	gchar *proxy_https;
	gchar *proxy_ftp;
	gchar *uri;
	gchar *proxy_socks;
	gchar *no_proxy;
	gchar *pac;

	locale = pk_backend_job_get_locale(job);
	if (!pk_strzero (locale)) {
		setlocale(LC_ALL, locale);
	}

	/* http_proxy */
	proxy_http = pk_backend_job_get_proxy_http (job);
	if (!pk_strzero (proxy_http)) {
		uri = pk_backend_spawn_convert_uri (proxy_http);
		g_setenv ("http_proxy", uri, TRUE);
		g_free (uri);
	}

	/* https_proxy */
	proxy_https = pk_backend_job_get_proxy_https (job);
	if (!pk_strzero (proxy_https)) {
		uri = pk_backend_spawn_convert_uri (proxy_https);
		g_setenv ("https_proxy", uri, TRUE);
		g_free (uri);
	}

	/* ftp_proxy */
	proxy_ftp = pk_backend_job_get_proxy_ftp (job);
	if (!pk_strzero (proxy_ftp)) {
		uri = pk_backend_spawn_convert_uri (proxy_ftp);
		g_setenv ("ftp_proxy", uri, TRUE);
		g_free (uri);
	}

	/* socks_proxy */
	proxy_socks = pk_backend_job_get_proxy_socks (job);
	if (!pk_strzero (proxy_socks)) {
		uri = pk_backend_spawn_convert_uri (proxy_socks);
		g_setenv ("socks_proxy", uri, TRUE);
		g_free (uri);
	}

	/* no_proxy */
	no_proxy = pk_backend_job_get_no_proxy (job);
	if (!pk_strzero (no_proxy)) {
		g_setenv ("no_proxy", no_proxy, TRUE);
	}

	/* pac */
	pac = pk_backend_job_get_pac (job);
	if (!pk_strzero (pac)) {
		uri = pk_backend_spawn_convert_uri (pac);
		g_setenv ("pac", uri, TRUE);
		g_free (uri);
	}

	g_free (locale);
	g_free (proxy_http);
	g_free (proxy_https);
	g_free (proxy_ftp);
	g_free (proxy_socks);
	g_free (no_proxy);
	g_free (pac);
}

/**
 * pk_backend_stop_job:
 */
void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
	/* unset proxy info for this transaction */
	g_unsetenv ("http_proxy");
	g_unsetenv ("ftp_proxy");
	g_unsetenv ("https_proxy");
	g_unsetenv ("no_proxy");
	g_unsetenv ("socks_proxy");
	g_unsetenv ("pac");
}


/**
  * Ask the User if it is OK to import an GPG-Key for a repo
  */
bool
ZyppBackend::ZyppBackendReceiver::zypp_signature_required (const PublicKey &key)
{
	bool ok = false;

	if (find (priv->signatures.begin (), priv->signatures.end (), key.id ()) == priv->signatures.end ()) {
		RepoInfo info = zypp_get_Repository (_job, _repoName);
		if (info.type () == repo::RepoType::NONE)
			pk_backend_job_error_code (_job, PK_ERROR_ENUM_INTERNAL_ERROR,
						   "Repository unknown");
		else {
			pk_backend_job_repo_signature_required (_job,
								"dummy;0.0.1;i386;data",
								_repoName,
								info.baseUrlsBegin ()->asString ().c_str (),
								key.name ().c_str (),
								key.id ().c_str (),
								key.fingerprint ().c_str (),
								key.created ().asString ().c_str (),
								PK_SIGTYPE_ENUM_GPG);
			pk_backend_job_error_code (_job, PK_ERROR_ENUM_GPG_FAILURE,
						   "Signature verification for Repository %s failed", _repoName);
		}
		throw AbortTransactionException();
	} else
		ok = true;
	
	return ok;
}

/**
  * Ask the User if it is OK to refresh the Repo while we don't know the key
  */
bool
ZyppBackend::ZyppBackendReceiver::zypp_signature_required (const string &file, const string &id)
{
	bool ok = false;

	if (find (priv->signatures.begin (), priv->signatures.end (), id) == priv->signatures.end ()) {
		RepoInfo info = zypp_get_Repository (_job, _repoName);
		if (info.type () == repo::RepoType::NONE)
			pk_backend_job_error_code (_job, PK_ERROR_ENUM_INTERNAL_ERROR,
					       "Repository unknown");
		else {
			pk_backend_job_repo_signature_required (_job,
				"dummy;0.0.1;i386;data",
				_repoName,
				info.baseUrlsBegin ()->asString ().c_str (),
				id.c_str (),
				id.c_str (),
				"UNKNOWN",
				"UNKNOWN",
				PK_SIGTYPE_ENUM_GPG);
			pk_backend_job_error_code (_job, PK_ERROR_ENUM_GPG_FAILURE,
					       "Signature verification for Repository %s failed", _repoName);
		}
		throw AbortTransactionException();
	} else
		ok = true;

	return ok;
}

/**
  * Ask the User if it is OK to refresh the Repo while we don't know the key, only its id which was never seen before
  */
bool
ZyppBackend::ZyppBackendReceiver::zypp_signature_required (const string &file)
{
	bool ok = false;

	if (find (priv->signatures.begin (), priv->signatures.end (), file) == priv->signatures.end ()) {
		RepoInfo info = zypp_get_Repository (_job, _repoName);
		if (info.type () == repo::RepoType::NONE)
			pk_backend_job_error_code (_job, PK_ERROR_ENUM_INTERNAL_ERROR,
					       "Repository unknown");
		else {
			pk_backend_job_repo_signature_required (_job,
				"dummy;0.0.1;i386;data",
				_repoName,
				info.baseUrlsBegin ()->asString ().c_str (),
				"UNKNOWN",
				file.c_str (),
				"UNKNOWN",
				"UNKNOWN",
				PK_SIGTYPE_ENUM_GPG);
			pk_backend_job_error_code (_job, PK_ERROR_ENUM_GPG_FAILURE,
					       "Signature verification for Repository %s failed", _repoName);
		}
		throw AbortTransactionException();
	} else
		ok = true;

	return ok;
}
