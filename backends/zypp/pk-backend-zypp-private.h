
#ifndef PK_BACKEND_ZYPP_PRIVATE_H
#define PK_BACKEND_ZYPP_PRIVATE_H

#include <map>
#include <string>
#include <vector>
#include <pthread.h>

#include "zypp-events.h"

typedef struct {

  std::vector<std::string> signatures;
  EventDirector eventDirector;
  PkBackendJob *currentJob;
  
  pthread_mutex_t zypp_mutex;

} PkBackendZYppPrivate;

#endif
