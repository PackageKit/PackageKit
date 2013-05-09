def message(typ, msg)
   $stdout.printf "message\t%s\t%s\n", typ, msg
   $stdout.flush
end

def package(package_id, status, summary)
   $stdout.printf "package\t%s\t%s\t%s\n", status, package_id, summary
   $stdout.flush
end

def media_change_required(mtype, id, text)
   $stdout.printf "media-change-required\t%s\t%s\t%s\n", mtype, id, text
   $stdout.flush
end

def distro_upgrade(repoid, dtype, name, summary)
   $stdout.printf "distro-upgrade\t%s\t%s\t%s\n", dtype, name, summary
   $stdout.flush
end

def repo_detail(repoid, name, state)
   $stdout.printf "repo-detail\t%s\t%s\t%s\n", repoid, name, state
   $stdout.flush
end

def update_detail(package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text, changelog, state, issued, updated)
   $stdout.printf "updatedetail\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text, changelog, state, issued, updated
   $stdout.flush
end

def require_restart(restart_type, details)
   $stdout.printf "requirerestart\t%s\t%s\n", restart_type, details
   $stdout.flush
end

def details(package_id, package_license, group, desc, url, bytes)
   $stdout.printf "details\t%s\t%s\t%s\t%s\t%s\t%d\n", package_id, package_license, group, desc, url, bytes
   $stdout.flush
end

def files(package_id, file_list)
   $stdout.printf "files\t%s\t%s\n", package_id, file_list
   $stdout.flush
end

def category(parent_id, cat_id, name, summary, icon)
   $stdout.printf "category\t%s\t%s\t%s\t%s\t%s\n", parent_id, cat_id, name, summary, icon
   $stdout.flush
end

def status(state)
   $stdout.printf "status\t%s\n", state
   $stdout.flush
end

def data(data)
   $stdout.printf "data\t%s\n", data
   $stdout.flush
end

def allow_cancel(allow)
   $stdout.printf "allow-cancel\t%s\n", allow
   $stdout.flush
end

def repo_signature_required(package_id, repo_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, sig_type)
   $stdout.printf "repo-signature-required\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", package_id, repo_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, sig_type
   $stdout.flush
end

def eula_required(eula_id, package_id, vendor_name, license_agreement)
   $stdout.printf "eula-required\t%s\t%s\t%s\t%s\n", eula_id, package_id, vendor_name, license_agreement
   $stdout.flush
end

def error_description(err, description)
   $stdout.printf "error\t%s\t%s\n", err, description
   $stdout.flush
end

def percentage(percent=nil)
   if percent==nil
      $stdout.printf "no-percentage-updates\n"
   else percent == 0 or percent > $percentage_old
      $stdout.printf "percentage\t%i\n", percent
      $percentage_old = percent
   end
   $stdout.flush
end

def item_progress(package_id, status, percent=nil)
   $stdout.printf "item-progress\t%s\t%s\t%i\n", package_id, status, percent
   $stdout.flush
end

def speed(bps=0)
   $stdout.printf "speed\t%i\n", bps
   $stdout.flush
end

def finished
   $stdout.printf "finished\n"
   $stdout.flush
end

