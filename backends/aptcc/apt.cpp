
#include "apt.h"
#include <pk-backend.h>

std::string get_short_description(const pkgCache::VerIterator &ver,
                                   pkgRecords *records)
{
	if(ver.end() || ver.FileList().end() || records == NULL)
		return std::string();

// #ifndef HAVE_DDTP
// egg_debug ("~HAVE_DDTP");
// 	pkgCache::VerFileIterator vf = ver.FileList();
// 
// 	if(vf.end())
// 		return std::string();
// 	else
// 		return records->Lookup(vf).ShortDesc();
// #else
egg_debug ("HAVE_DDTP");
	pkgCache::DescIterator d = ver.TranslatedDescription();

	if(d.end())
		return std::string();

	pkgCache::DescFileIterator df = d.FileList();

	if(df.end())
		return std::string();
	else
		// apt "helpfully" cw::util::transcodes the description for us, instead of
		// providing direct access to it.  So I need to assume that the
		// description is encoded in the current locale.
		return records->Lookup(df).ShortDesc();
// #endif
}
