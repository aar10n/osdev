#ifndef _UAPI_SYS_UN_H
#define _UAPI_SYS_UN_H

#include <sys/socket.h>

#define UNIX_PATH_MAX 108

struct sockaddr_un {
  sa_family_t sun_family;  // AF_UNIX
  char sun_path[UNIX_PATH_MAX];  // pathname
};

#endif // _UAPI_SYS_UN_H