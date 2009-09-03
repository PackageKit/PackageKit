def message(typ, msg)
   $stdout.printf "message\t%s\t%s\n", typ, msg
   $stdout.flush
end

def package(package_id, status, summary)
   $stdout.printf "package\t%s\t%s\t%s\n", status, package_id, summary
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

def details(package_id, package_license, group, desc, url, bytes)
   $stdout.printf "details\t%s\t%s\t%s\t%s\t%s\t%d\n", package_id, package_license, group, desc, url, bytes
   $stdout.flush
end

def files(package_id, file_list)
   $stdout.printf "files\t%s\t%s\n", package_id, file_list
   $stdout.flush
end

def status(state)
   $stdout.printf "status\t%s\n", state
   $stdout.flush
end

def error(err, description, exit=true)
   $stdout.printf "error\t%s\t%s\n", err, description
   $stdout.flush
   if exit
      finished
      exit(1)
   end
end

def percentage(percent=nil)
   if percent==nil
      $stdout.printf "finished\n"
   else percent == 0 or percent > $percentage_old
      $stdout.printf "percentage\t%i\n", percent
      $percentage_old = percent
   end
   $stdout.flush
end

def finished
   $stdout.printf "finished\n"
   $stdout.flush
end

