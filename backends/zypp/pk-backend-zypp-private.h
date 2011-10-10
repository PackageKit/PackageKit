
#ifndef PK_BACKEND_ZYPP_PRIVATE_H
#define PK_BACKEND_ZYPP_PRIVATE_H

#include <map>
#include <string>
#include <vector>

class EventDirector;

typedef struct {

   /**
    * A map to keep track of the EventDirector objects for
    * each zypp backend that is created.
    */
    std::map<PkBackend *, EventDirector *> eventDirectors;
    std::map<PkBackend *, std::vector<std::string> *> signatures;

    EventDirector *eventDirector;

} PkBackendZYppPrivate;

#endif
