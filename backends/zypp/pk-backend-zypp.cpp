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
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/pk-enum.h>

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
using zypp::sat::detail::SolvableIterator;
using zypp::sat::Solvable;

#undef ZYPP_BASE_LOGGER_LOGGROUP
#define ZYPP_BASE_LOGGER_LOGGROUP "packagekit"



struct problem {
  
  string kind;
  std::list<string> solutions;
  string selected;
};

struct backend_job_private {

  ResolverProblemList::iterator it;
  ProblemSolutionList* sol_it;
  std::list<std::string> to_install;
  std::list<std::string> to_remove;
  
  std::list<struct problem> problems;
  
  struct msg_proc_helper *msg_proc_helper;
  
  PkBackendJob *job;
  gint input_id;
  int input;
  int ouput;
  
  int output;
  
};

typedef enum {
        INSTALL,
        REMOVE,
        UPDATE,
        UPGRADE_SYSTEM
} PerformType;

typedef enum {
        NEWER_VERSION,
        OLDER_VERSION,
        EQUAL_VERSION
} VersionRelation;

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

/** Details about the kind of content returned by zypp_get_patches. */
enum class SelfUpdate {
  kNo,				///< applicable patches (no ZYPP stack update)
  kYes,				///< a ZYPP stack update (must be applied first)
  kYesAndShaddowsSecurity	///< a ZYPP stack update is shadowing applicable security patches
};

/// \class PoolStatusSaver
/// \brief Helper to restore the pool status after doing operations on it.
///
/// \note It's important that a PoolStatusSaver is instantiated \b after
/// the pool is built/refreshed. Otherwise you lose all the locks applied
/// during refresh. (bnc#804054)
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

/* We need to track the number of packages to download in global scope */
guint _dl_count = 0;
guint _dl_progress = 0;
guint _dl_status = 0;

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

	void update_sub_percentage (guint percentage, PkStatusEnum status) {
		// Only emit a percentage if it's different from the last
		// percentage we emitted and it's divisible by ten.  We
		// don't want to overload dbus/GUI.  Also account for the
		// fact that libzypp may skip over a "divisible by ten"
		// value (i.e., 28, 29, 31, 32).

		//MIL << percentage << " " << _sub_percentage << std::endl;
		if (percentage == _sub_percentage)
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
		pk_backend_job_set_item_progress(_job, _package_id, status, _sub_percentage);
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

	virtual void start (zypp::Resolvable::constPtr resolvable) {
		clear_package_id ();

		/* This is the first package we see coming as INSTALLING - resetting counter and modus */
		if (_dl_status != PK_INFO_ENUM_INSTALLING) {
			_dl_progress = 0;
			_dl_status = PK_INFO_ENUM_INSTALLING;
		}
		_package_id = zypp_build_package_id_from_resolvable (resolvable->satSolvable ());
		MIL << resolvable << " " << _package_id << std::endl;
		gchar* summary = g_strdup(zypp::asKind<zypp::ResObject>(resolvable)->summary().c_str ());
		if (_package_id != NULL) {
			pk_backend_job_set_status (_job, PK_STATUS_ENUM_INSTALL);
			pk_backend_job_package (_job, PK_INFO_ENUM_INSTALLING, _package_id, summary);
			reset_sub_percentage ();
		}
		g_free (summary);
	}

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable) {
		// we need to have extra logic here as progress is reported twice
		// and PackageKit does not like percentages going back
		//MIL << value << " " << _package_id << std::endl;
		update_sub_percentage (value, PK_STATUS_ENUM_INSTALL);
		return true;
	}

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description, RpmLevel level) {
		pk_backend_job_error_code (_job, PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL, "%s", description.c_str ());
		
                return ABORT;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &reason, RpmLevel level) {
		MIL << reason << " " << _package_id << " " << resolvable << std::endl;
		pk_backend_job_set_percentage(_job, (double)++_dl_progress / _dl_count * 100);
		if (_package_id != NULL) {
			//pk_backend_job_package (_backend, PK_INFO_ENUM_INSTALLED, _package_id, "TODO: Put the package summary here if possible");
			update_sub_percentage (100, PK_STATUS_ENUM_INSTALL);
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
		update_sub_percentage (value, PK_STATUS_ENUM_REMOVE);
		return true;
	}

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description) {
                pk_backend_job_error_code (_job, PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, "%s", description.c_str ());
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
		update_sub_percentage ((int)data.val (), PK_STATUS_ENUM_UNKNOWN);
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
		update_sub_percentage ((int)data.val (), PK_STATUS_ENUM_UNKNOWN);
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
		/* This is the first package we see coming as INSTALLING - resetting counter and modus */
		if (_dl_status != PK_INFO_ENUM_DOWNLOADING) {
			_dl_progress = 0;
			_dl_status = PK_INFO_ENUM_DOWNLOADING;
		}
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
		//MIL << resolvable << " " << value << " " << _package_id << std::endl;
		update_sub_percentage (value, PK_STATUS_ENUM_DOWNLOAD);
		//pk_backend_job_set_speed (_job, static_cast<guint>(dbps_current));
		return true;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &konreason)
	{
		MIL << resolvable << " " << error << " " << _package_id << std::endl;
		update_sub_percentage (100, PK_STATUS_ENUM_DOWNLOAD);
		pk_backend_job_set_percentage(_job, (double)++_dl_progress / _dl_count * 100);
		clear_package_id ();
	}
};

struct MediaChangeReportReceiver : public zypp::callback::ReceiveReport<zypp::media::MediaChangeReport>, ZyppBackendReceiver
{
	virtual Action requestMedia (zypp::Url &url, unsigned mediaNr, const std::string &label, zypp::media::MediaChangeReport::Error error, const std::string &description, const std::vector<std::string> & devices, unsigned int &dev_current)
	{
		pk_backend_job_error_code (_job, PK_ERROR_ENUM_REPO_NOT_AVAILABLE, "%s", description.c_str ());
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
                update_sub_percentage ((int)progress.val (), PK_STATUS_ENUM_UNKNOWN);
		return true;
        }

        virtual void finish (const zypp::ProgressData &progress)
        {
		MIL << progress.val() << std::endl;
                update_sub_percentage ((int)progress.val (), PK_STATUS_ENUM_UNKNOWN);
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
#if 0
        bool isBonsoleInit;
        ResolverProblemList::iterator it;
        ProblemSolutionList* sol_it;
        char *sender;
        
        
        std::list<std::string> to_install;
        std::list<std::string> to_remove;
        
        std::list<struct problem> problems;
        bool first_run;
        
#endif
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
		pk_backend_job_error_code (priv->currentJob, PK_ERROR_ENUM_FAILED_INITIALIZATION, "%s", ex.asUserString().c_str() );
		return NULL;
	} catch (const Exception &ex) {
		pk_backend_job_error_code (priv->currentJob, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", ex.asUserString().c_str() );
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
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "%s: Repository has no or invalid repo name defined.\n", repo.alias ().c_str ());
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
 * Return the package is from a local file or not.
 */
bool
zypp_package_is_local (const gchar *package_id)
{
	MIL << package_id << endl;
	bool ret = false;

	if (!pk_package_id_check (package_id))
		return false;

	gchar **id_parts = pk_package_id_split (package_id);
	if (!strncmp (id_parts[PK_PACKAGE_ID_DATA], "local", 5))
		ret = true;

	g_strfreev (id_parts);
	return ret;
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
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, "%s", ex.asUserString().c_str() );
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
		manager.refreshMetadata (repo, force ?
					 RepoManager::RefreshForced :
					 RepoManager::RefreshIfNeededIgnoreDelay);
		manager.buildCache (repo, force ?
				    RepoManager::BuildForced :
				    RepoManager::BuildIfNeeded);
		try
		{
			manager.loadFromCache (repo);
		}
		catch (const Exception &exp)
		{
			// cachefile has old fomat (or is corrupted): rebuild it
			manager.cleanCache (repo);
			manager.buildCache (repo, force ?
					    RepoManager::BuildForced :
					    RepoManager::BuildIfNeeded);
			manager.loadFromCache (repo);
		}
		return TRUE;
	} catch (const AbortTransactionException &ex) {
		return FALSE;
	}
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

static gboolean
zypp_package_provides_application (const sat::Solvable &item)
{
	Capabilities provides = item.provides();
	for_(capit, provides.begin(), provides.end())
	{
		if (g_str_has_prefix (((Capability) *capit).c_str(), "application("))
			return TRUE;
	}
	return FALSE;

}

static gboolean
zypp_package_is_cached (const sat::Solvable &item)
{
	if ( isKind<Package>( item ) )
	{
		Package::Ptr pkg( make<Package>( item ) );
		return pkg->isCached();
	}
	return FALSE;
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
			    item.arch () != "noarch")
				return TRUE;
		}
		if (i == PK_FILTER_ENUM_NOT_ARCH) {
			if (item.arch () == ZConfig::defaultSystemArchitecture () ||
			    item.arch () == "noarch")
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

		if (i == PK_FILTER_ENUM_APPLICATION && !zypp_package_provides_application (item))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_APPLICATION && zypp_package_provides_application (item))
			return TRUE;

		if (i == PK_FILTER_ENUM_DOWNLOADED && !zypp_package_is_cached (item))
			return TRUE;
		if (i == PK_FILTER_ENUM_NOT_DOWNLOADED && zypp_package_is_cached (item))
			return TRUE;
		if (i == PK_FILTER_ENUM_NEWEST) {
			if (item.isSystem ()) {
				return FALSE;
			}
			else {
				ui::Selectable::Ptr sel = ui::Selectable::get (item);
				const PoolItem & newest (sel->highestAvailableVersionObj ());

				if (newest && zypp::Edition::compare (newest.edition (), item.edition ()))
					return TRUE;
				return FALSE;
			}
		}

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

static gboolean
is_tumbleweed (void)
{
	gboolean ret = FALSE;
	static const Capability cap( "product-update()", Rel::EQ, "dup" );
	ui::Selectable::Ptr sel (ui::Selectable::get (ResKind::package, "openSUSE-release"));
	if (sel) {
		if (!sel->installedEmpty ()) {
			for_ (it, sel->installedBegin (), sel->installedEnd ()) {
				if (it->satSolvable ().provides ().matches (cap)) {
					ret = TRUE;
					break;
				}
			}
		}
	}

	return ret;
}

/**
 * Returns a set of all packages the could be updated
 * (you're able to exclude a single (normally the 'patch' repo)
 */
static void
zypp_get_package_updates (string repo, set<PoolItem> &pks)
{
	ZYpp::Ptr zypp = getZYpp ();
	zypp::Resolver_Ptr resolver = zypp->resolver ();
	ResPool pool = ResPool::instance ();

	ResObject::Kind kind = ResTraits<Package>::kind;
	ResPool::byKind_iterator it = pool.byKindBegin (kind);
	ResPool::byKind_iterator e = pool.byKindEnd (kind);

	if (is_tumbleweed ()) {
		resolver->dupSetAllowVendorChange (ZConfig::instance ().solver_dupAllowVendorChange ());
		resolver->doUpgrade ();
	} else {
		resolver->doUpdate ();
	}

	for (; it != e; ++it)
		if (it->status().isToBeInstalled()) {
			ui::Selectable::constPtr s =
				ui::Selectable::get((*it)->kind(), (*it)->name());
			if (s->hasInstalledObj())
				pks.insert(*it);
		}

	if (is_tumbleweed ()) {
		resolver->setUpgradeMode (FALSE);
	}
}

/**
 * Returns a set of all patches the could be installed.
 * An applicable ZYPP stack update will shadow all other updates (must be installed
 * first). Check for shadowed security updates.
 */
static SelfUpdate
zypp_get_patches (PkBackendJob *job, ZYpp::Ptr zypp, set<PoolItem> &patches)
{
	SelfUpdate detail = SelfUpdate::kNo;
	bool sawSecurityPatch = false;
	
	zypp->resolver ()->setIgnoreAlreadyRecommended (TRUE);
	zypp->resolver ()->resolvePool ();

	for (ResPoolProxy::const_iterator it = zypp->poolProxy ().byKindBegin<Patch>();
			it != zypp->poolProxy ().byKindEnd<Patch>(); it ++) {
		// check if the patch is needed and not set to taboo
		if((*it)->isNeeded() && !((*it)->candidateObj ().isUnwanted())) {
			Patch::constPtr patch = asKind<Patch>((*it)->candidateObj ().resolvable ());
			if (!sawSecurityPatch && patch->isCategory(Patch::CAT_SECURITY)) {
				sawSecurityPatch = true;
			}

			if (detail == SelfUpdate::kYes) {
				if (patch->restartSuggested ())
					patches.insert ((*it)->candidateObj ());
			}
			else
				patches.insert ((*it)->candidateObj ());

			// check if the patch updates libzypp or packageKit and show only these
			if (patch->restartSuggested () && detail == SelfUpdate::kNo) {
				detail = SelfUpdate::kYes;
				patches.clear ();
				patches.insert ((*it)->candidateObj ());
			}
		}

	}

	if (detail == SelfUpdate::kYes && sawSecurityPatch) {
		detail = SelfUpdate::kYesAndShaddowsSecurity;
	}
	return detail;
}

/**
  * Return the best, most friendly selection of update patches and packages that
  * we can find. Also manages SelfUpdate to prioritise critical infrastructure
  * updates.
  */
static SelfUpdate
zypp_get_updates (PkBackendJob *job, ZYpp::Ptr zypp, set<PoolItem> &candidates)
{
	typedef set<PoolItem>::iterator pi_it_t;
	SelfUpdate detail = zypp_get_patches (job, zypp, candidates);

	if (detail == SelfUpdate::kNo) {
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
	return detail;
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
		if (patch->restartSuggested ())
			*restart = PK_RESTART_ENUM_APPLICATION;
		if (patch->reloginSuggested ())
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
/* LIBXML/LIBXSLT includes - remove unnecessary in future */
#include <libxml/xmlmemory.h>
#include <libxml/debugXML.h>
#include <libxml/HTMLtree.h>
#include <libxml/xmlIO.h>
#include <libxml/DOCBparser.h>
#include <libxml/xinclude.h>
#include <libxml/catalog.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
struct reader_info {
  char *buffer;
  int   curr_old;
  int   loaded;
  int   buff_len;
} ;
struct msg_proc_helper {
  
  ResolverProblemList::iterator it;
  Resolver_Ptr resolver;
  ResolverProblemList problems;
  ProblemSolutionList *solution_list; 
  std::list<struct problem> problems2;
  char *path_to_cache;
  
  struct reader_info reader_info;

};

static std::string get_full_resolution_text(zypp::ProblemSolution& item)
{
  return item.description() + item.details();
}


// Popracować nad tym
static void add_resolution_to_zypp(struct msg_proc_helper *helper)
{
  bool apply = false;
   
  std::list<struct problem>::iterator it = helper->problems2.begin();
  
  for (; it != helper->problems2.end(); ++it) {
  
    ResolverProblemList::iterator it2 = helper->problems.begin();
    
    for (; it2 != helper->problems.end(); ++it2) {
    
      if ((*it2)->description() == (*it).kind) {
      
        break;
      }
    }
    
    if (it2 == helper->problems.end()) {
      
      continue;
    }
      
    ProblemSolutionList::const_iterator it3;
    ResolverProblem problem = **it2;
    it3 = problem.solutions().begin();
    
    for (; it3 != problem.solutions().end(); ++it3) {
    
      if (get_full_resolution_text(**it3) == (*it).selected) {
   
        apply = true;
        ProblemSolution solution = **it3;
        helper->solution_list->push_back(&solution);
      }
    }
  }
  
  if (apply)
  
    helper->resolver->applySolutions(*helper->solution_list);
}
#if 0
static void show_solutions(const char *msg_, intptr_t usr_p)
{
  char *buffer, *prev, *curr, *prev2, *prev3;
  int length, length2;
  int problem, solution;
  xmlNodePtr root, text, anchor, message, form, checkbox;
  struct msg_proc_helper *helper = (struct msg_proc_helper *) usr_p;
  
  ResolverProblemList::iterator it;
  ProblemSolutionList::const_iterator sol_it;
  

  xmlDocPtr a = bonsole_window(nullptr);
  root = xmlDocGetRootElement(a);
  
  form = xmlNewNode(NULL, BAD_CAST "form");
  xmlSetProp(form, BAD_CAST "action",BAD_CAST "app:update");
  xmlAddChild(root, form);
  
  problem = 0;
  solution = 0;
  for (it = helper->problems.begin(); it != helper->problems.end(); ++it) {
    
    text = xmlNewText(BAD_CAST (*it)->description ().c_str ());
    xmlAddChild(form, text);   
    for (sol_it = (**it).solutions().begin(); sol_it != (**it).solutions().end(); ++sol_it) {
      
      checkbox = xmlNewNode(NULL, BAD_CAST "checkbox");
      text = xmlNewText(BAD_CAST (*sol_it)->description ().c_str ());
      xmlAddChild(checkbox, text);
      
      xmlAddChild(form, checkbox);
      //text = xmlNewCDataBlock(a, BAD_CAST (*sol_it)->details ().c_str (), strlen((*sol_it)->details ().c_str ()));
      char *string= strdup((*sol_it)->details ().c_str ());
      
      char *prev = string;
      char *curr = string;
      
      while ('\0' != *curr) {
      
        if ('\n' == *curr) {
        
          *curr = '\0';
          text = xmlNewText(BAD_CAST prev);
          xmlAddChild(form, text);
          text = xmlNewNode(NULL, BAD_CAST "br");
          xmlAddChild(form, text);
          prev = curr + 1;
        }
        
        ++curr;
      }
      
      text = xmlNewText(BAD_CAST prev);
      xmlAddChild(checkbox, text);
      free(string);
      
      
      length = snprintf(NULL, 0, "%d_%d", problem, solution) + 1;
      buffer = (char*) malloc(length);
      snprintf(buffer, length, "%d_%d", problem, solution);
      
      xmlSetProp(checkbox, BAD_CAST "name", BAD_CAST buffer);
   
      
      
      //++solution;
    }
    ++problem;
  }

bonsole_window_release(nullptr);
bonsole_flush_changes(nullptr);
}

static void message_proc(const char *msg__, intptr_t usr_p)
{
  char *buffer, *prev, *curr, *prev2, *prev3, *spec;
  int length, length2;
  int problem, solution;
  xmlNodePtr root, text, anchor, message, form, checkbox;
  struct msg_proc_helper *helper = (struct msg_proc_helper *) usr_p;
  
  char *msg_ = bonsole_message_unescape_string(msg__, 0);
  
  if (0 == strncmp("update?", msg_, sizeof("update?") - 1)) {
  
    if (NULL == helper->solution_list) {
    
      helper->solution_list = new ProblemSolutionList();
    }
    bonsole_reset_document(nullptr);
    xmlDocPtr a = bonsole_window(nullptr);
    root = xmlDocGetRootElement(a);
    spec = msg_;
    buffer = spec;
    length = 0;
    while ('\0' != *buffer) {
    
      if ('&' == *buffer) {
      
        ++length;
        *buffer = '\0';
      }
      ++buffer;
    }
    ++length;
    buffer = &spec[sizeof("update?") - 1];
    
    while (0 < length) {
    
      prev = buffer;
      do {
        
        ++buffer;
      } while ('_' != *buffer);
      
      *buffer = '\0';
      ++buffer;
      do {
        ++buffer;
      } while ('=' != *buffer);
    
      
      ++buffer;
      if ('\0' != buffer[0] && 0 != strcmp(buffer, "1")) {
      
        --length;
        do {
          ++buffer;
        } while ('\0' != *buffer);
        
        continue;
      }
      
      while ('\0' != *buffer) {
        ++buffer;
      }
      
      ++buffer;
      curr = prev;
      prev = buffer;
      
      length2 = length - 1;
      while (0 < length2) {
        prev2 = buffer;
        
        
        
        do {
          
          ++buffer;
        } while ('_' != *buffer);
        
        *buffer = '\0';
        ++buffer;
        
        
        if (0 != strcmp(curr, prev2)) {
          
          --length2;
          do {
            ++buffer;
          } while ('\0' != *buffer);
          
          continue;
        }
        
        do {
          ++buffer;
        } while ('=' != *buffer);
        
        
        ++buffer;
        if ('\0' != buffer[0] && 0 != strcmp(buffer, "1")) {
          
          --length2;
          do {
            ++buffer;
          } while ('\0' != *buffer);
          
          ++buffer;
          if ('\0' != *buffer) {
          
            ++buffer;
          }
          continue;
        }
        message = xmlNewNode(NULL, BAD_CAST "message");
        text = xmlNewText(BAD_CAST "You checked two different solutions for one problem");
        xmlAddChild(message, text);   
        xmlAddChild(root, message);   
  
        bonsole_window_release(nullptr);
        show_solutions(msg_, usr_p);
        
        return;
        
      }
        do {
          ++buffer;
        } while ('\0' != *buffer);
        int problem_number, solution_number;
        
        problem_number = atoi(curr);
        while ('\0' != *curr) ++curr;
        prev3 = ++curr;
        while ('\0' != *curr && '=' != *curr) ++curr;
        *curr = '\0';
        solution_number = atoi(prev3);
        //++solution_number;
        
        ProblemSolutionList::const_iterator it2;
        ResolverProblemList::iterator it = helper->problems.begin();
        std::advance(
          it, 
          problem_number);
        ResolverProblem problem = **it;
        
        it2 = problem.solutions().begin();
        std::advance(
          it2,
          solution_number);
        ProblemSolution solution = **it2;
        
        struct problem prob;
        prob.kind = problem.description();
        //prob.solutions = 
        prob.selected = get_full_resolution_text(solution);
        
        it2 = problem.solutions().begin();
        
        for (; it2 != problem.solutions().end(); ++it2) {
          
          prob.solutions.push_back(get_full_resolution_text(**it2));
        }
        
        helper->problems2.push_back(prob);
        
        free(spec);
      
      
      --length;
    }
    
    

    bonsole_window_release(nullptr);
    bonsole_flush_changes(nullptr);
    
    bonsole_quit_loop(nullptr);
  }
  
  free(msg_);
}

#endif
static char *get_record(int fd, char *buffer)
{
  int count;
  int curr = 0;
  int buff_len = 0;
  int loaded = 0;
  bool done = false;
  
  
  
  buff_len += 512;
  buffer = (char*)realloc(buffer, buff_len);
  
  if (NULL == buffer) {
    
    return NULL;
  }
  
  while ((count = read(fd, buffer, buff_len - 1 - loaded)) > 0)  {
    
    
    curr = loaded;
    loaded += count;
    while ('\0' != buffer[curr] && curr < loaded) {
      
      ++curr;
    }
    
    if (curr < loaded && '\0' == buffer[curr]) {
    
      done = true;
      break;
    }

    buff_len += 512;
    buffer = (char*)realloc(buffer, buff_len);
    
    if (NULL == buffer) {
      
      count = 0;
      break;
    }
  };
  
  if (!done && 0 > count) {
    
    free(buffer);
    close(fd);
    return NULL;
  }
  
  buffer[loaded] = '\0';
  
  lseek(fd, - (loaded - curr) + 1, SEEK_CUR);
  
  return buffer;
}

static gboolean 
load_transaction_from_history(const char *type, const char *file, struct backend_job_private *priv_)
{
  
  char *buffer = NULL;
  int fd = open(file, O_RDONLY);
  
  if (-1 == fd) {
  
    return FALSE;
  }
  
  
  buffer = get_record(fd, buffer);
  
  if (NULL == buffer) {
    
    close(fd);
    return FALSE;
  }
  
  if (0 != strcmp("PACKAGEKIT ZYPPER USER SELECTION FILE 1.0", buffer)) {
  
    close(fd);
    return FALSE;
  }
  
  free(buffer);
  buffer = NULL;
  buffer = get_record(fd, buffer);
  
  if (NULL == buffer) {
    
    close(fd);
    return FALSE;
  }
  
  // TODO: It should be "Simulate"X, where X is a suffix - we should use strncmp
  if (0 != strcmp("Install", buffer)) {
    
    close(fd);
    return FALSE;
  }
  
  free(buffer);
  buffer = NULL;
  
  priv_->to_install.clear();
  priv_->to_remove.clear();
  
  while ((buffer = get_record(fd, buffer)) && ('\0' != buffer[0])) {
  
    if (NULL == buffer) {
      
      close(fd);
      return FALSE;
    }
    priv_->to_install.push_back(buffer);
    free(buffer);
    buffer = NULL;
  }
  
  if (NULL != buffer) {
  
    free(buffer);
    buffer = NULL;
  }
  
  while ((buffer = get_record(fd, buffer)) && ('\0' != buffer[0])) {
    
    if (NULL == buffer) {
      
      close(fd);
      return FALSE;
    }
    priv_->to_remove.push_back(buffer);
    free(buffer);
    buffer = NULL;
  }
  
  if (NULL != buffer) {
    
    free(buffer);
    buffer = NULL;
  }
  
  struct problem problem;
  
  while ((buffer = get_record(fd, buffer)) && ('\0' != buffer[0])) {
    
    if (NULL == buffer) {
      
      close(fd);
      return FALSE;
    }
    
     problem.kind = strdup(buffer); 
     
     free(buffer);
     buffer = NULL;
     
     while ((buffer = get_record(fd, buffer)) && ('\0' != buffer[0])) {
     
       
       if (NULL == buffer) {
         
         close(fd);
         return FALSE;
       }
       string a(buffer);
       
       
       problem.solutions.push_back(a);
       
       
       free(buffer);
       buffer = NULL;
    }
    
    buffer = get_record(fd, buffer);
    
    if (NULL == buffer) {
      
      close(fd);
      return FALSE;
    }
    
    problem.selected = std::string(buffer);
    
    free(buffer);
    buffer = NULL;
    
    priv_->problems.push_back(problem);
  }
  
  if (NULL != buffer) {
    
    free(buffer);
    buffer = NULL;
  }
  
  
  return TRUE;
}

static void save_package_list(int fd, std::list<std::string>::iterator curr_pkg, std::list<std::string>::iterator end_pkg_list)
{
  while (curr_pkg != end_pkg_list) {
  
    if (strlen((*curr_pkg).c_str()) >
      write(fd, (*curr_pkg).c_str(), strlen((*curr_pkg).c_str()) + 1)) {
      
      close(fd);
      return;
    }
    ++curr_pkg;
  }
  
  /* Writes null character */
  if (strlen("") >
    write(fd, "", 1)) {
    
    close(fd);
    return;
  }
}

static void save_transaction_to_cache(const char *type, const char *file, struct msg_proc_helper *helper,
                                      std::list<std::string> to_install, std::list<std::string> to_remove) {

  std::list<std::string>::iterator curr_pkg, end_pkg_list;
  int fd = open(file, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);
  
  if (-1 == fd) {
  
    return;
  }
  
  ftruncate(fd, 0);
  
  if (sizeof("PACKAGEKIT ZYPPER USER SELECTION FILE 1.0") - 1 >
    write(fd, "PACKAGEKIT ZYPPER USER SELECTION FILE 1.0", sizeof("PACKAGEKIT ZYPPER USER SELECTION FILE 1.0"))) {
  
      close(fd);
      return;
  }
    
  if (strlen(type) >
    write(fd, type, strlen(type) + 1)) {
  
    close(fd);
    return;
  }
    
  curr_pkg = to_install.begin();
  end_pkg_list = to_install.end();
  
  save_package_list(fd, curr_pkg, end_pkg_list);
  
  curr_pkg = to_remove.begin();
  end_pkg_list = to_remove.end();
  
  save_package_list(fd, curr_pkg, end_pkg_list);
  
  
  std::list<struct problem>::iterator curr_problem, end_of_problems;
  std::list<string>::iterator curr_sol, end_of_sol;
  
  
  curr_problem = helper->problems2.begin();
  end_of_problems = helper->problems2.end();
  
  while (curr_problem != end_of_problems) {
  
    curr_sol = (*curr_problem).solutions.begin();
    end_of_sol = (*curr_problem).solutions.end();
    
    if (strlen((*curr_problem).kind.c_str()) >
      write(fd, (*curr_problem).kind.c_str(), strlen((*curr_problem).kind.c_str()) + 1)) {
      
      close(fd);
      return;
    }
    
    while (curr_sol != end_of_sol) {
    
      if (strlen((*curr_sol).c_str()) >
        write(fd, (*curr_sol).c_str(), strlen((*curr_sol).c_str()) + 1)) {
        
        close(fd);
        return;
      }
      ++curr_sol;
    }
    
    if (strlen("") >
      write(fd, "", 1)) {
      
      close(fd);
      return;
    }
    
    if (strlen((*curr_problem).selected.c_str()) >
      write(fd, (*curr_problem).selected.c_str(), strlen((*curr_problem).selected.c_str()) + 1)) {
      
      close(fd);
      return;
    }
    ++curr_problem;
  }
  close(fd);
}

static char 
*get_record2(int fd, struct reader_info *info)
{
  int count = 0;
  bool done = false;
  int curr = 0;
  int curr2 = info->curr_old;
  
  
  while (info->loaded >= curr2 + 1) {
    
    
    if ('\0' == info->buffer[curr2]) {
      
      curr = info->curr_old ; 
      info->curr_old = curr2 + 1;
      return &info->buffer[curr];
    }
    
    ++curr2;
  }
  
  info->buff_len += 512;
  info->buffer = (char*)realloc(info->buffer, info->buff_len);
  
  if (NULL == info->buffer) {
    
    puts("HERE - BAD");
    return NULL;
  }
  
  while ((count = read(fd, &info->buffer[info->loaded], info->buff_len - 1 - info->loaded)) > 0 || (errno == EAGAIN && info->loaded < info->curr_old))  {
    
    puts("BAD");
    curr = info->loaded;
    info->loaded += count;
    while ('\0' != info->buffer[curr] && curr < info->loaded) {
      
      ++curr;
    }
    
    if (curr < info->loaded && '\0' == info->buffer[curr]) {
      
      done = true;
      break;
    }
    
    info->buff_len += 512;
    info->buffer = (char*)realloc(info->buffer, info->buff_len);
    
    if (NULL == info->buffer) {
      
      count = 0;
      break;
    }
  };
  
  if (!done && 0 > count && info->loaded >= info->curr_old) {
    
    perror("Error while read from pipe");
    free(info->buffer);
    close(fd);
    return NULL;
  }
  
  info->buffer[info->loaded] = '\0';
  
  curr2 = info->curr_old;
  info->curr_old = curr + 1;
  return &info->buffer[curr2];
  
}

static gboolean  
dependency_handle_selection(GIOChannel *source,
                            GIOCondition condition,
                            gpointer data)
{
  #if 0
helper->problems2.push_back(prob);
#endif
struct backend_job_private *msg_proc = (struct backend_job_private*) data;
  int fd = g_io_channel_unix_get_fd (source);
  
  //fcntl(fd, F_SETFL, O_NONBLOCK);
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  char *buffer ;
  puts("YES");
  while (buffer = get_record2(fd, &msg_proc->msg_proc_helper->reader_info)) {
  
    puts(buffer);
    puts("\n");
    if (0 == strncmp("ERR:", buffer, sizeof("ERR:") - 1)) {
    
      
    }
    else if (0 == strncmp("SELECTION:", buffer, sizeof("SELECTION:") - 1)) {
      
      puts("OKI");
      buffer = get_record2(fd, &msg_proc->msg_proc_helper->reader_info);
      
      puts(buffer);
      char *problem_str = strchr(buffer, ':');
      if (NULL == problem_str) {
        
        puts("BAD222");
        return FALSE;
      }
      
      char *solution_str = strchr(problem_str, ':');
      
      if (NULL == solution_str) {
      
        puts("BAD333");
        return FALSE;
      }
      
      int solution_number = atoi(solution_str);
      int problem_number = atoi(problem_str);
      
      
      ProblemSolutionList::const_iterator it2;
      ResolverProblemList::iterator it = msg_proc->msg_proc_helper->problems.begin();
      std::advance(
        it, 
        problem_number);
      ResolverProblem problem = **it;
      
      it2 = problem.solutions().begin();
      std::advance(
        it2,
        solution_number);
      ProblemSolution solution = **it2;
      msg_proc->msg_proc_helper->solution_list->push_back(*it2);
      puts("OKI2");
    }
    else if (0 == strncmp("STOP", buffer, sizeof("STOP") - 1)) {
      
      
    }
    else if (0 == strncmp("DONE!", buffer, sizeof("DONE!") - 1)) {
      add_resolution_to_zypp(msg_proc->msg_proc_helper);
      /* Save resolution to file */
      save_transaction_to_cache("Install", msg_proc->msg_proc_helper->path_to_cache, msg_proc->msg_proc_helper, 
                                msg_proc->to_install, msg_proc->to_remove);
      
      puts("END");
      pk_backend_job_thread_setup(msg_proc->job->helper);
      
    }
  }
  
  return TRUE;
}
static gboolean
zypp_perform_execution (PkBackendJob *job, ZYpp::Ptr zypp, PerformType type, gboolean force, PkBitfield transaction_flags)
{
        char *path_to_cache = NULL;
  
	MIL << force << " " << pk_filter_bitfield_to_string(transaction_flags) << endl;
	gboolean ret = FALSE;
	
	PkBackend *backend = PK_BACKEND(pk_backend_job_get_backend(job));
	
        int len = snprintf(NULL, 0, "/var/local/lib/PackageKit/solutions-cache-%s", job->sender) + 1;
        
        path_to_cache = (char*) malloc(len);
        
        snprintf(path_to_cache, len, "/var/local/lib/PackageKit/solutions-cache-%s", job->sender);
        
        
	try {
		if (force)
			zypp->resolver ()->setForceResolve (force);

                
                // Gather up any dependencies
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
		pk_backend_job_set_percentage(job, 0);
		zypp->resolver ()->setIgnoreAlreadyRecommended (TRUE);
		pk_backend_job_set_percentage(job, 100);
                
                
                ResPool pool = ResPool::instance ();
                test:
                if (!zypp->resolver ()->resolvePool ()) {
                struct backend_job_private *rjob = (struct backend_job_private*) pk_backend_job_get_priv_data (job);
                struct msg_proc_helper *transaction_problems;
                
                
                if (NULL == rjob) {
                  
                  rjob = new (struct backend_job_private)();
                  pk_backend_job_set_priv_data(job, rjob);
                  transaction_problems = new struct msg_proc_helper;
                  transaction_problems->path_to_cache = strdup(path_to_cache);
                  rjob->job = job;
                  
                }
                else if (rjob->msg_proc_helper){
                
                  transaction_problems = rjob->msg_proc_helper;
                }
                rjob->to_install = std::list<std::string>();
                rjob->to_remove = std::list<std::string>();
                rjob->problems = std::list<struct problem> ();
                
                ResObject::Kind kind = ResTraits<Package>::kind;
                
                // Adding packages selected by user
                      ResolverProblemList problems = zypp->resolver ()->problems ();
                      gchar * emsg = NULL, * tempmsg = NULL;
                      ResolverProblemList::iterator it = problems.begin ();
                      std::list<std::string> to_remove;
                      std::list<std::string> to_install;
                      
                      
                      bool changed = false;
                        // Adding additional packages (selected by zypper) to separated field
                      {
                        
                         // if (Test if simulation) {
                         // TODO: SL. S.L. Fill me
                         load_transaction_from_history("Install", path_to_cache, rjob);
                         
#if 0
                         struct reader_info {
                           char *buffer;
                           int   curr_old;
                           int   loaded;
                           int   buff_len;
                         };
#endif
                         transaction_problems->reader_info.buffer = NULL;
                         transaction_problems->reader_info.curr_old = 0;
                         transaction_problems->reader_info.loaded = 0;
                         transaction_problems->reader_info.buff_len = 0;
                         
                         transaction_problems->problems = problems;
                         transaction_problems->it = problems.begin();
                         transaction_problems->resolver = zypp->resolver ();
                         transaction_problems->solution_list = new ProblemSolutionList();
                         transaction_problems->problems2 = rjob->problems;//std::list<struct problem> {};//priv->problems;
                         
                         ResPool::byKind_iterator itb = pool.byKindBegin (kind);
                         ResPool::byKind_iterator ite = pool.byKindEnd (kind);
                         
                       
                         
                         std::list<std::string>::iterator it;
                         
                         
                         for (; itb != ite; ++itb) {
                           
                           if (itb->status().isToBeInstalled()) {
                             to_install.push_back(itb->satSolvable().asString());
                           }
                         }
                         
                         itb = pool.byKindBegin (kind);
                         
                         for (; itb != ite;  ++itb) {
                           
                           if (itb->status().isToBeUninstalled()) {
                             to_remove.push_back(itb->satSolvable().asString());
                           }
                         }
                         
                         if (rjob->to_remove.size() != to_remove.size()) {
                         
                           changed = true;
                        }
                         
                         if (!changed && rjob->to_remove.size() > 0) {
                           for (it = rjob->to_remove.begin(); it != rjob->to_remove.end(); ++it) {
                           
                           std::list<string>::iterator it2;
                           for (it2 = to_remove.begin(); it2 != to_remove.end(); ++it2) {
                             
                             if (*it2 == *it) {
                             
                               break;
                             }
                           }
                           
                           if (it2 == to_remove.end()) {
                             
                             changed = true;
                             break;
                           }
                         }
                         
                         if (it != rjob->to_remove.end()) {
                           
                           changed = true;
                         }
                         }
                         
                         if (rjob->to_install.size() != to_install.size()) {
                           
                           changed = true;
                         }
                         
                         if (!changed && rjob->to_install.size() > 0) {
                           
                           for (it = rjob->to_install.begin(); it != rjob->to_install.end(); ++it) {
                             
                             std::list<string>::iterator it2;
                             
                             for (it2 = to_install.begin(); it2 != to_install.end(); ++it2) {
                             
                               if (*it2 == *it) {
                                 
                                 break;
                               }
                             }
                             
                             if (it2 == to_install.end()) {
                               
                               changed = true;
                               break;
                             }
                           }
                         }
                           
                         if (!changed
                           && it != rjob->to_install.end()
                            )  {
                             
                            changed = true;
                         }
                           
                         
                           
                      }
			// Manual intervention required to resolve dependencies
			// TODO: Figure out what we need to do with PackageKit
			// to pull off interactive problem solving.
#if 0
			if (changed && NULL == rjob->msg_proc_helper) {
                        
                          if (rjob->sol_it) {
                            
                            //                             delete job->sol_it;
                          }
                          rjob->problems = std::list<struct problem> ();
                          
                          // rjob->sol_it = new ProblemSolutionList();
                          rjob->to_install = to_install;
                          rjob->to_remove = to_remove;
                          
                          
                          add_resolution_to_zypp(transaction_problems);
                          /* Save resolution to file */
                          save_transaction_to_cache("Install", path_to_cache, transaction_problems, 
                                                    rjob->to_install, rjob->to_remove);
                          
                          goto test;
                        }
#endif
                        
                        // TODO: Add support for passing request could be handled interactively
                        if (force) {
			
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
			  pk_backend_job_error_code (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "%s", emsg);
                          
                          g_free (emsg);
                          
			  goto exit;
                        }
                        
                        
                        if (changed) {
                          
                          
                          if (rjob->sol_it) {
                            
                            //                             delete job->sol_it;
                          }
                          rjob->problems = std::list<struct problem> ();
                          
                          // rjob->sol_it = new ProblemSolutionList();
                          rjob->to_install = to_install;
                          rjob->to_remove = to_remove;
                          job->done = 0;
                          if (! rjob->msg_proc_helper) {
                            rjob->msg_proc_helper = transaction_problems;
                          pid_t child_pid;
                          
                          
                          int fds[2];
                          int fds2[2];
                          
                          int input, output;
                          
                          if (pipe(fds) < 0) {
                            
                            perror("Error while pipe creating");
                          }
                          else if (pipe(fds2) < 0) {
                            
                            perror("Error while pipe creating");
                          }
                          
                          child_pid = fork();
 
                          
                          if (0 == child_pid) {
                            
                            close(fds[0]);
                            close(fds2[1]);
                            
                            int length;
                            char *comm_ch_output;
                            char *comm_ch_input;
                            
                            length = snprintf(NULL, 0, "%d", fds[1]) + 1;
                            comm_ch_output = (char*) malloc(length);
                            snprintf(comm_ch_output, length, "%d", fds[1]); 
                            
                            length = snprintf(NULL, 0, "%d", fds2[0]) + 1;
                            comm_ch_input = (char*) malloc(length);
                            snprintf(comm_ch_input, length, "%d", fds2[0]);
                            
                            printf("COMM_CH_OUTPUT: %d COMM_CH_INPUT: %d\n", fds[1], fds2[0]);
                            
                            execlp(LIBEXECDIR "/dependency-solving-helper", LIBEXECDIR "/dependency-solving-helper", "--comm-channel-input", comm_ch_input,"--comm-channel-output", comm_ch_output, NULL);
                            
                            write(STDOUT_FILENO, "ERR:\0Unable to start dependency solver\n", sizeof("Unable to start dependency solver\n") - 1);
                            exit(1);
                            
                          }
                          // TODO: SL S.L. READ DOCUMENTATION
                          else if (-1 == child_pid)
                          {
                            perror("Cannot spawn interactive dependency solver");
                            
                            close(fds[0]);
                            close(fds2[1]);
                            close(fds[1]);
                            close(fds2[0]);
                            
                            pk_backend_job_error_code (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Cannot spawn interactive dependency solver");
                          }
                          else {
                            
                            close(fds[1]);
                            close(fds2[0]);
                            GError *error;
                            GIOChannel *chann = g_io_channel_unix_new (fds[0]);
                            g_io_channel_set_encoding(chann, NULL, &error);
                            g_io_channel_set_buffered(chann, false);
                            rjob->input_id = g_io_add_watch(chann, G_IO_IN, dependency_handle_selection, (void*)rjob);
                            
                            rjob->input = fds[0];
                            rjob->output = fds2[1];
                            input = fds[0];
                            
                            write(rjob->output, job->sender, strlen(job->sender) + 1);
                            
                          }
                          }
                          {
                            ResolverProblemList::iterator it;
                            ProblemSolutionList::const_iterator sol_it;
                            const char *string;
                            
                            for (it = transaction_problems->problems.begin(); it != transaction_problems->problems.end(); ++it) {
                              
                              string = (*it)->description ().c_str ();
                              write(rjob->output, string, strlen(string)+1);
                              for (sol_it = (**it).solutions().begin(); sol_it != (**it).solutions().end(); ++sol_it) {
                                
                                
                                string = (*sol_it)->description ().c_str ();
                                write(rjob->output, string, strlen(string)+1);
                                string = (*sol_it)->details ().c_str ();
                                write(rjob->output, string, strlen(string)+1);
                                
                              }
                              write(rjob->output, "", sizeof(""));
                            }
                            
                            write(rjob->output, "", sizeof(""));
                            ret = TRUE;
                            goto exit;
                            
                        }
                        }
                        
#if 0
                        add_resolution_to_zypp(&transaction_problems);
                        
                        // Save resolution to file
                        save_transaction_to_cache("Install", path_to_cache, &transaction_problems, 
                                                  priv->to_install, priv->to_remove);
#endif

                        }
                        struct backend_job_private *rjob = (struct backend_job_private*) pk_backend_job_get_priv_data (job);
                        
                        if (NULL != rjob) {
                        
                          //cleaning up
                          
                          write(rjob->output, "", sizeof(""));
                          g_source_unref( g_main_context_find_source_by_id(g_main_context_default(), rjob->input_id));
                          close(rjob->input);
                          close(rjob->output);
                        }
                job->done = 1;
		switch (type) {
		case INSTALL:
			pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
			pk_backend_job_set_percentage(job, 0);
			_dl_progress = 0;
			break;
		case REMOVE:
			pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
			pk_backend_job_set_percentage(job, 0);
			_dl_progress = 0;
			break;
		case UPDATE:
		case UPGRADE_SYSTEM:
			pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
			pk_backend_job_set_percentage(job, 0);
			_dl_progress = 0;
			break;
		}

		if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
			ret = TRUE;

			MIL << "simulating" << endl;

			for (ResPool::const_iterator it = pool.begin (); it != pool.end (); ++it) {
				switch (type) {
				case REMOVE:
					if (!(*it)->isSystem ()) {
						it->statusReset ();
						continue;
					}
					break;
				case INSTALL:
				case UPDATE:
					// for updates we only care for updates
					if (it->status ().isToBeUninstalledDueToUpgrade ())
						continue;
					break;
				case UPGRADE_SYSTEM:
				default:
					break;
				}
				
				if (!zypp_backend_pool_item_notify (job, *it, TRUE))
					ret = FALSE;
				it->statusReset ();
			}
			goto exit;
		}


		// look for licenses to confirm

		_dl_count = 0;
		for (ResPool::const_iterator it = pool.begin (); it != pool.end (); ++it) {
			if (it->status ().isToBeInstalled ())
				_dl_count++;
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
                

		pk_backend_job_set_percentage(job, 100);
		ret = TRUE;
	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, "%s", ex.asUserString().c_str() );
	} catch (const target::rpm::RpmException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, "%s", ex.asUserString().c_str () );
	} catch (const Exception &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", ex.asUserString().c_str() );
	}

 exit:
 
        
        if (path_to_cache) free(path_to_cache);
        
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

	bool poolIsClean = sat::Pool::instance ().reposEmpty ();
	// Erase and reload all if pool is too holey (densyity [100: good | 0 bad])
	// NOTE sat::Pool::capacity() > 2 is asserted in division
	if (!poolIsClean &&
	    sat::Pool::instance ().solvablesSize () * 100 / sat::Pool::instance ().capacity () < 33)
	{
		sat::Pool::instance ().reposEraseAll ();
		poolIsClean = true;
	}

	Target_Ptr target = zypp->getTarget ();
	if (!target)
	{
		zypp->initializeTarget (pathname);	// initial target
		target = zypp->getTarget ();
	}
	else
	{
		// load rpmdb trusted keys into zypp keyring
		target->rpmDb ().exportTrustedKeysInZyppKeyRing ();
	}
	// load installed packages to pool
	target->load ();

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
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, "%s", e.asUserString().c_str() );
		return FALSE;
	}

	if (!poolIsClean)
	{
		std::vector<std::string> aliasesToRemove;

		for (const Repository &poolrepo : zypp->pool ().knownRepositories ())
		{
			if (!(poolrepo.isSystemRepo () || manager.hasRepo (poolrepo.alias ())))
				aliasesToRemove.push_back (poolrepo.alias ());
		}

		for (const std::string &aliasToRemove : aliasesToRemove)
		{
			sat::Pool::instance ().reposErase (aliasToRemove);
		}
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
		{
			if (!poolIsClean)
				sat::Pool::instance ().reposErase (repo.alias ());
			continue;
		}

		// do as zypper does
		if (!force && !repo.autorefresh())
			continue;

		// skip changeable media (DVDs and CDs).  Without doing this,
		// the disc would be required to be physically present.
		if (repo.baseUrlsBegin ()->schemeIsVolatile())
		{
			if (!poolIsClean)
				sat::Pool::instance ().reposErase (repo.alias ());
			continue;
		}

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
		g_printf("%s", repo_messages);

	pk_backend_job_set_percentage (job, 100);
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
}



/**
 * We do not pretend we're thread safe when all we do is having a huge mutex
 */
gboolean
pk_backend_supports_parallelization (PkBackend *backend)
{
        return FALSE;
}


const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("ZYpp package manager");
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return g_strdup ("Boyd Timothy <btimothy@gmail.com>, "
			 "Scott Reeves <sreeves@novell.com>, "
			 "Stefan Haas <shaas@suse.de>, "
			 "ZYpp developers <zypp-devel@opensuse.org>");
}

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	/* create private area */
	priv = new PkBackendZYppPrivate;
	priv->currentJob = 0;
	priv->zypp_mutex = PTHREAD_MUTEX_INITIALIZER;
	zypp_logging ();

	g_debug ("zypp_backend_initialize");
}

void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("zypp_backend_destroy");

	filesystem::recursive_rmdir (zypp::myTmpDir ());

	g_free (_repoName);
	delete priv;
}


static bool
zypp_is_no_solvable (const sat::Solvable &solv)
{
	return solv == sat::Solvable::noSolvable;
}

/**
  * backend_required_by_thread:
  */
static void
backend_required_by_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
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
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	pk_backend_job_set_percentage (job, 10);

	ResPool pool = zypp_build_pool (zypp, true);
	PoolStatusSaver saver;
	for (uint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);

		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						     "Package couldn't be found");
			return;
		}

		PoolItem package = PoolItem(solvable);

		// required-by only works for installed packages. It's meaningless for stuff in the repo
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
}

/**
  * pk_backend_required_by:
  */
void
pk_backend_required_by(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_job_thread_create (job, backend_required_by_thread, NULL, NULL);
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

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
				       PK_FILTER_ENUM_ARCH,
				       PK_FILTER_ENUM_NEWEST,
				       PK_FILTER_ENUM_SOURCE,
				       PK_FILTER_ENUM_APPLICATION,
				       PK_FILTER_ENUM_DOWNLOADED,
				       -1);
}

/*
 * This method is a bit of a travesty of the complexity of
 * solving dependencies. We try to give a simple answer to
 * "what packages are required for these packages" - but,
 * clearly often there is no simple answer.
 */
static void
backend_depends_on_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
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
			g_debug ("depends_on - capability '%s'", cap->asString().c_str());

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
}

void
pk_backend_depends_on (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_job_thread_create (job, backend_depends_on_thread, NULL, NULL);
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
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		MIL << package_ids[i] << endl;

		if (zypp_package_is_local(package_ids[i])) {
			pk_backend_job_details (job, package_ids[i], "", "", PK_GROUP_ENUM_UNKNOWN, "", "", (gulong)0);
			return;
		}

		sat::Solvable solv = zypp_get_package_by_id( package_ids[i] );

		if (zypp_is_no_solvable(solv)) {
			// Previously stored package_id no longer matches any solvable.
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						     "couldn't find package");
			return;
		}

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
				(pkg ? pkg->summary().c_str() : "" ),   // Package summary
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
}

void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_details_thread, NULL, NULL);
}

static void
backend_get_details_local_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	RepoManager manager;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	gchar **full_paths;
	g_variant_get (params, "(^a&s)", &full_paths);

	if (zypp == NULL){
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	for (guint i = 0; full_paths[i]; i++) {

		// check if file is really a rpm
		Pathname rpmPath (full_paths[i]);
		target::rpm::RpmHeader::constPtr rpmHeader = target::rpm::RpmHeader::readPackage (rpmPath, target::rpm::RpmHeader::NOSIGNATURE);

		if (rpmHeader == NULL) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_INTERNAL_ERROR,
				"%s is not valid rpm-File", full_paths[i]);
			return;
		}

		gchar *package_id;
		package_id = g_strjoin (";", rpmHeader->tag_name ().c_str(),
					(rpmHeader->tag_version () + "-" + rpmHeader->tag_release ()).c_str(),
					rpmHeader->tag_arch ().asString ().c_str(),
					"local",
					NULL);

		pk_backend_job_details (job,
			package_id,
			rpmHeader->tag_summary ().c_str (),
			rpmHeader->tag_license ().c_str (),
			get_enum_group (rpmHeader->tag_group ()),
			rpmHeader->tag_description ().c_str (),
			rpmHeader->tag_url ().c_str (),
			(gulong)rpmHeader->tag_size ().blocks (zypp::ByteCount::B));

		g_free (package_id);
	}
}

void
pk_backend_get_details_local (PkBackend *backend, PkBackendJob *job, gchar **full_paths)
{
	pk_backend_job_thread_create (job, backend_get_details_local_thread, NULL, NULL);
}

/**
 * backend_get_files_local_thread:
 */
static void
backend_get_files_local_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	RepoManager manager;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL)
		return;

	gchar **full_paths;
	g_variant_get (params, "(^a&s)", &full_paths);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	for (guint i = 0; full_paths[i]; i++) {

		// check if file is really a rpm
		Pathname rpmPath (full_paths[i]);
		target::rpm::RpmHeader::constPtr rpmHeader = target::rpm::RpmHeader::readPackage (rpmPath, target::rpm::RpmHeader::NOSIGNATURE);

		if (rpmHeader == NULL) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_INTERNAL_ERROR,
				"%s is not valid rpm-File", full_paths[i]);
			return;
		}

		gchar *package_id;
		package_id = g_strjoin (";", rpmHeader->tag_name ().c_str(),
					(rpmHeader->tag_version () + "-" + rpmHeader->tag_release ()).c_str(),
					rpmHeader->tag_arch ().asString ().c_str(),
					"local",
					NULL);

		std::list<std::string> filenames = rpmHeader->tag_filenames ();
		GPtrArray *array = g_ptr_array_new ();

		for (std::list<std::string>::iterator it = filenames.begin (); it != filenames.end (); it++)
			g_ptr_array_add (array, g_strdup ((*it).c_str ()));

		g_ptr_array_add(array, NULL);
		gchar **files_array = (gchar**)g_ptr_array_free (array, FALSE);

		pk_backend_job_files (job, package_id, files_array);

		g_free (package_id);
		g_strfreev (files_array);
	}
}

/**
 * pk_backend_get_files_local:
 */
void
pk_backend_get_files_local (PkBackend *backend, PkBackendJob *job, gchar **full_paths)
{
	pk_backend_job_thread_create (job, backend_get_files_local_thread, NULL, NULL);
}

static void
backend_get_distro_upgrades_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	
	if (zypp == NULL){
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
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
}

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
		return;
	}

	zypp_refresh_cache (job, zypp, force);
}

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
		return;
	}

	typedef set<PoolItem>::iterator pi_it_t;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		return;
	}

	ResPool pool = zypp_build_pool (zypp, TRUE);
	pk_backend_job_set_percentage (job, 40);

	set<PoolItem> candidates;
	SelfUpdate detail = zypp_get_updates (job, zypp, candidates);

	pk_backend_job_set_percentage (job, 80);

	pi_it_t cb = candidates.begin (), ce = candidates.end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		ResObject::constPtr res = ci->resolvable();

		// Emit the package
		PkInfoEnum infoEnum = PK_INFO_ENUM_ENHANCEMENT;
		if (detail == SelfUpdate::kYesAndShaddowsSecurity) {
			infoEnum = PK_INFO_ENUM_SECURITY;	// bsc#951592: raise priority if security patch is shadowed
		} else if (isKind<Patch>(res)) {
			Patch::constPtr patch = asKind<Patch>(res);
			if (patch->category () == "recommended") {
				infoEnum = PK_INFO_ENUM_BUGFIX;
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
}

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
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, "%s", ex.asUserString().c_str() );
	}
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
		if (!solvable) {
			// Previously stored package_id no longer matches any solvable.
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						     "couldn't find package");
			return;
		}

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

	PkBitfield transaction_flags = 0;
	gchar **package_ids;

	g_variant_get(params, "(t^a&s)",
		      &transaction_flags,
		      &package_ids);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	if (zypp == NULL){
		return;
	}

	// refresh the repos before installing packages
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	try
	{
		ResPool pool = zypp_build_pool (zypp, TRUE);
		PoolStatusSaver saver;
		pk_backend_job_set_percentage (job, 10);
		vector<PoolItem> items;
		VersionRelation relations[g_strv_length (package_ids)];
		guint to_install = 0;

		for (guint i = 0; package_ids[i]; i++) {
			MIL << package_ids[i] << endl;
			g_auto(GStrv) split = NULL;
			gint ret;
			sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
			sat::Solvable *inst_pkg = NULL;
			sat::Solvable *latest_pkg = NULL;
			vector<sat::Solvable> installed;

			if (zypp_is_no_solvable(solvable)) {
				// Previously stored package_id no longer matches any solvable.
				zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
							     "couldn't find package");
				return;
			}

			split = pk_package_id_split (package_ids[i]);
			ui::Selectable::Ptr sel (ui::Selectable::get (ResKind::package,
								      split[PK_PACKAGE_ID_NAME]));
			if (sel && !sel->installedEmpty ()) {
				for_ (it, sel->installedBegin (), sel->installedEnd ()) {
					if (it->satSolvable ().arch ().compare (Arch (split[PK_PACKAGE_ID_ARCH])) == 0) {
						installed.push_back ((it->satSolvable ()));
					}
				}
			}

			for (guint j = 0; j < installed.size (); j++) {
				inst_pkg = &installed.at (j);
				ret = inst_pkg->edition ().compare (Edition (split[PK_PACKAGE_ID_VERSION]));

				if (relations[i] == 0 && ret < 0) {
					relations[i] = NEWER_VERSION;
				} else if (relations[i] != EQUAL_VERSION && ret > 0) {
					relations[i] = OLDER_VERSION;
					if (!latest_pkg ||
					    latest_pkg->edition ().compare (inst_pkg->edition ()) < 0) {
						latest_pkg = inst_pkg;
					}
				} else if (ret == 0) {
					relations[i] = EQUAL_VERSION;
					break;
				}
			}

			if (relations[i] == EQUAL_VERSION &&
			    !pk_bitfield_contain (transaction_flags,
						  PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL)) {
				continue;
			}

			if (relations[i] == OLDER_VERSION &&
			    !pk_bitfield_contain (transaction_flags,
						  PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE)) {
				pk_backend_job_error_code (job,
							   PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
							   "higher version \"%s\" of package %s.%s is already installed",
							   latest_pkg->edition ().version ().c_str (),
							   split[PK_PACKAGE_ID_NAME],
							   split[PK_PACKAGE_ID_ARCH]);
				return;
			}

			if (relations[i] && relations[i] != EQUAL_VERSION &&
			    pk_bitfield_contain (transaction_flags,
						 PK_TRANSACTION_FLAG_ENUM_JUST_REINSTALL)) {
				pk_backend_job_error_code (job,
							   PK_ERROR_ENUM_NOT_AUTHORIZED,
							   "missing authorization to update or downgrade software");
				return;
			}

			to_install++;
			PoolItem item(solvable);
			// set status to ToBeInstalled
			item.status ().setToBeInstalled (ResStatus::USER);
			items.push_back (item);
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
			for (vector<PoolItem>::iterator it = items.begin (); it != items.end (); ++it) {
				it->statusReset ();
			}
			return;
		}

		pk_backend_job_set_percentage (job, 100);

	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}
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
}

void
pk_backend_install_signature (PkBackend *backend, PkBackendJob *job, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id)
{
	pk_backend_job_thread_create (job, backend_install_signature_thread, NULL, NULL);
}

static void
backend_remove_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	PkBitfield transaction_flags = 0;
	gboolean autoremove = false;
	gboolean allow_deps = false;
	gchar **package_ids;
	vector<PoolItem> items;

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
		return;
	}
	zypp->resolver()->setCleandepsOnRemove(autoremove);

	target = zypp->target ();

	// Load all the local system "resolvables" (packages)
	target->load ();
	pk_backend_job_set_percentage (job, 10);

	PoolStatusSaver saver;
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
			items.push_back (item);
		} else {
			item.status ().resetTransact (ResStatus::USER);
		}
	}

	pk_backend_job_set_percentage (job, 40);

	try
	{
		if (!zypp_perform_execution (job, zypp, REMOVE, TRUE, transaction_flags)) {
			//reset the status of the marked packages
			for (vector<PoolItem>::iterator it = items.begin (); it != items.end (); ++it) {
				it->statusReset();
			}
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_TRANSACTION_ERROR,
				"Couldn't remove the package");
			return;
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
}

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

		sat::Solvable installed;
		sat::Solvable newest;
		vector<sat::Solvable> pkgs;

		/* Filter the list of packages with this name to 'pkgs' */
		for (vector<sat::Solvable>::iterator it = v.begin (); it != v.end (); ++it) {

			MIL << "found " << *it << endl;

			if (zypp_filter_solvable (_filters, *it) ||
			    zypp_is_no_solvable(*it))
				continue;

			if (it->isSystem ()) {
				installed = *it;
			}

			if (zypp_is_no_solvable(newest)) {
				newest = *it;
			} else if (it->edition() > newest.edition() || Arch::compare(it->arch(), newest.arch()) > 0) {
				newest = *it;
			}
			MIL << "emit " << *it << endl;
			pkgs.push_back (*it);
		}

		/* The newest filter processes installed and available package
		 * lists separately. */
		if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NEWEST)) {
			pkgs.clear ();

			if (!zypp_is_no_solvable (installed)) {
				MIL << "emit installed " << installed << endl;
				pkgs.push_back (installed);
			}
			if (!zypp_is_no_solvable (newest)) {
				MIL << "emit newest " << newest << endl;
				pkgs.push_back (newest);
			}
		} else if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_NEWEST)) {
			if (!zypp_is_no_solvable (newest) && newest != installed) {
				pkgs.erase (find (pkgs.begin (), pkgs.end(), newest));
			}
		}

		zypp_emit_filtered_packages_in_list (job, _filters, pkgs);
	}
}

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
		return;
	}

	// refresh the repos before searching
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		return;
	}

	search = values[0];  //Fixme - support the possible multiple values (logical OR search)
	role = pk_backend_job_get_role(job);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	vector<sat::Solvable> v;

	PoolQuery q;
	q.addString( search ); // may be called multiple times (OR'ed)
	q.setCaseSensitive( false ); // [<>] We want to be case insensitive for the name and description searches...
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
		q.setCaseSensitive( true ); // [<>] But we probably want case sensitive search for the file searches.

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
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_find_packages_thread, NULL, NULL);
}

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
}

void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_search_group_thread, NULL, NULL);
}

void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_find_packages_thread, NULL, NULL);
}

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

		g_auto(GStrv) strv = NULL;
		g_autoptr(GPtrArray) pkg_files = NULL;

		pkg_files = g_ptr_array_new ();

		if (solvable.isSystem ()){
			try {
				target::rpm::RpmHeader::constPtr rpmHeader = zypp_get_rpmHeader (solvable.name (), solvable.edition ());
				list<string> files = rpmHeader->tag_filenames ();

				for (list<string>::iterator it = files.begin (); it != files.end (); ++it) {
					g_ptr_array_add (pkg_files, g_strdup (it->c_str ()));
				}

			} catch (const target::rpm::RpmException &ex) {
				zypp_backend_finished_error (job, PK_ERROR_ENUM_REPO_NOT_FOUND,
							     "Couldn't open rpm-database");
				return;
			}
		} else {
			g_ptr_array_add (pkg_files,
					 g_strdup ("Only available for installed packages"));
		}

		/* Convert to GStrv. */
		g_ptr_array_add (pkg_files, NULL);
		strv = g_strdupv ((gchar **) pkg_files->pdata);
		pk_backend_job_files (job, package_ids[i], strv);
	}
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
upgrade_system (PkBackendJob *job,
		ZYpp::Ptr zypp,
		PkBitfield transaction_flags)
{
	set<PoolItem> candidates;

	/* refresh the repos before checking for updates. */
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		return;
	}
	zypp_get_updates (job, zypp, candidates);
	if (candidates.empty ()) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_DISTRO_UPGRADE_DATA,
					   "No Distribution Upgrade Available.");

		return;
	}

	zypp->resolver ()->dupSetAllowVendorChange (ZConfig::instance ().solver_dupAllowVendorChange ());
	zypp->resolver ()->doUpgrade ();

	zypp_perform_execution (job, zypp, UPGRADE_SYSTEM, FALSE, transaction_flags);

	zypp->resolver ()->setUpgradeMode (FALSE);
}

static void
backend_update_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	PkBitfield transaction_flags = 0;
	gchar **package_ids;
	g_variant_get(params, "(t^a&s)",
		      &transaction_flags,
		      &package_ids);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		return;
	}

	ResPool pool = zypp_build_pool (zypp, TRUE);
	PkRestartEnum restart = PK_RESTART_ENUM_NONE;
	PoolStatusSaver saver;

	if (is_tumbleweed ()) {
		upgrade_system (job, zypp, transaction_flags);
		return;
	}

	for (guint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);

		if (zypp_is_no_solvable(solvable)) {
			// Previously stored package_id no longer matches any solvable.
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						     "couldn't find package");
			return;
		}

		ui::Selectable::Ptr sel( ui::Selectable::get( solvable ));

		PoolItem item(solvable);
		// patches are special - they are not installed and can't have update candidates
		if (sel->kind() != ResKind::patch) {
			MIL << "sel " << sel->kind() << " " << sel->ident() << endl;
			if (sel->installedEmpty()) {
				zypp_backend_finished_error (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Package %s is not installed", package_ids[i]);
				return;
			}
			item = sel->updateCandidateObj();
			if (!item) {
				 zypp_backend_finished_error(job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "There is no update candidate for %s", sel->installedObj().satSolvable().asString().c_str());
				return;
			}
		}

		item.status ().setToBeInstalled (ResStatus::USER);
		Patch::constPtr patch = asKind<Patch>(item.resolvable ());
		zypp_check_restart (&restart, patch);
		if (restart != PK_RESTART_ENUM_NONE){
			pk_backend_job_require_restart (job, restart, package_ids[i]);
			restart = PK_RESTART_ENUM_NONE;
		}
	}

	zypp_perform_execution (job, zypp, UPDATE, FALSE, transaction_flags);
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
				pk_backend_job_error_code (job, PK_ERROR_ENUM_NOT_SUPPORTED, "Autorefresh a repo: Enter true or false");
			}

			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "keep") == 0) {

			if (g_ascii_strcasecmp (value, "true") == 0) {
				repo.setKeepPackages (TRUE);
			} else if (g_ascii_strcasecmp (value, "false") == 0) {
				repo.setKeepPackages (FALSE);
			} else {
				pk_backend_job_error_code (job, PK_ERROR_ENUM_NOT_SUPPORTED, "Keep downloaded packages: Enter true or false");
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
				pk_backend_job_error_code (job, PK_ERROR_ENUM_NOT_SUPPORTED, "Priorities has to be between 1 (highest) and 99");
			} else {
				for (gint i = 0; i < length; i++) {
					gint tmp = g_ascii_digit_value (value[i]);

					if (tmp == -1) {
						pk_backend_job_error_code (job, PK_ERROR_ENUM_NOT_SUPPORTED, "Priorities has to be a number between 1 (highest) and 99");
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
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", ex.asString ().c_str ());
	}
}

/**
  * pk_backend_repo_set_data
  */
void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	pk_backend_job_thread_create (job, backend_repo_set_data_thread, NULL, NULL);
}

/**
 * pk_backend_what_provides_decompose: maps enums to provides
 */
static gchar **
pk_backend_what_provides_decompose (PkBackendJob *job, gchar **values)
{
	guint i;
	guint len;
	gchar **search = NULL;
	GPtrArray *array = NULL;

	/* iter on each provide string, and wrap it with the fedora prefix - unless different to openSUSE */
	len = g_strv_length (values);
	array = g_ptr_array_new_with_free_func (g_free);
	for (i=0; i<len; i++) {
		/* compatibility with previous versions of GPK */
		g_ptr_array_add (array, g_strdup (values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer1(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("plasma4(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("plasma5(%s)", values[i]));
	}
	search = pk_ptr_array_to_strv (array);
	for (i = 0; search[i] != NULL; i++)
		g_debug ("Querying provide '%s'", search[i]);
	return search;
}

static void
backend_what_provides_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	
	gchar **values;
	PkBitfield _filters;
	g_variant_get(params, "(t^a&s)",
		      &_filters,
		      &values);
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	ResPool pool = zypp_build_pool (zypp, true);

	if(g_ascii_strcasecmp("drivers_for_attached_hardware", values[0]) == 0) {
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
		gchar **search = pk_backend_what_provides_decompose (job,
								     values);
		GHashTable *installed_hash = g_hash_table_new (g_str_hash, g_str_equal);
		
		guint len = g_strv_length (search);
		for (guint i=0; i<len; i++) {
			MIL << search[i] << endl;
			Capability cap (search[i]);
			sat::WhatProvides prov (cap);
			
			for (sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); ++it) {
				if (it->isSystem ())
					g_hash_table_insert (installed_hash,
							     (const gpointer) make<ResObject>(*it)->summary().c_str (),
							     GUINT_TO_POINTER (1));
			}

			for (sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); ++it) {
				if (zypp_filter_solvable (_filters, *it))
					continue;

				/* If caller asked for uninstalled packages, filter out uninstalled instances from
				 * remote repos corresponding to locally installed packages */
				if ((_filters & pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED)) &&
				    g_hash_table_contains (installed_hash, make<ResObject>(*it)->summary().c_str ()))
					continue;

				PkInfoEnum info = it->isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
				zypp_backend_package (job, info, *it,  make<ResObject>(*it)->summary().c_str ());
			}
		}

		g_hash_table_unref (installed_hash);
	}
}

/**
  * pk_backend_what_provides
  */
void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
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
		return;
	}

	if (!zypp_refresh_cache (job, zypp, FALSE)) {
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
			const gchar *to_strv[] = { NULL, NULL };
			to_strv[0] =  target.c_str();
			pk_backend_job_files (job, package_ids[i],(gchar **) to_strv);
			pk_backend_job_package (job, PK_INFO_ENUM_DOWNLOADING, package_ids[i], item->summary ().c_str());
		}
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, ex.asUserString().c_str());
		return;
	}
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
	pk_backend_job_thread_create (job, backend_download_packages_thread, NULL, NULL);
}

void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	const gchar *locale;
	const gchar *proxy_http;
	const gchar *proxy_https;
	const gchar *proxy_ftp;
	const gchar *proxy_socks;
	const gchar *no_proxy;
	const gchar *pac;
	gchar *uri;

	locale = pk_backend_job_get_locale(job);
	if (!pk_strzero (locale)) {
		setlocale(LC_ALL, locale);
	}

	/* http_proxy */
	proxy_http = pk_backend_job_get_proxy_http (job);
	if (!pk_strzero (proxy_http)) {
		uri = pk_backend_convert_uri (proxy_http);
		g_setenv ("http_proxy", uri, TRUE);
		g_free (uri);
	}

	/* https_proxy */
	proxy_https = pk_backend_job_get_proxy_https (job);
	if (!pk_strzero (proxy_https)) {
		uri = pk_backend_convert_uri (proxy_https);
		g_setenv ("https_proxy", uri, TRUE);
		g_free (uri);
	}

	/* ftp_proxy */
	proxy_ftp = pk_backend_job_get_proxy_ftp (job);
	if (!pk_strzero (proxy_ftp)) {
		uri = pk_backend_convert_uri (proxy_ftp);
		g_setenv ("ftp_proxy", uri, TRUE);
		g_free (uri);
	}

	/* socks_proxy */
	proxy_socks = pk_backend_job_get_proxy_socks (job);
	if (!pk_strzero (proxy_socks)) {
		uri = pk_backend_convert_uri (proxy_socks);
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
		uri = pk_backend_convert_uri (pac);
		g_setenv ("pac", uri, TRUE);
		g_free (uri);
	}
}

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
