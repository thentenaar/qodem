/*++

Copyright (c) 2000, Microsoft Corporation

Module Name:
    wspiapi.h

Abstract:
    The file contains protocol independent API functions.

Revision History:
    Wed Jul 12 10:50:31 2000, Created

--*/

#ifndef _WSPIAPI_H_
#define _WSPIAPI_H_

#if defined(__BORLANDC__) || defined(_MSC_VER)

typedef int socklen_t;

typedef struct addrinfo {
  int             ai_flags;
  int             ai_family;
  int             ai_socktype;
  int             ai_protocol;
  size_t          ai_addrlen;
  char            *ai_canonname;
  struct sockaddr  *ai_addr;
  struct addrinfo  *ai_next;
} ADDRINFOA, *PADDRINFOA;

/* getnameinfo constants */
#define NI_MAXHOST	1025
#define NI_MAXSERV	32

#define NI_NOFQDN       0x01
#define NI_NUMERICHOST	0x02
#define NI_NAMEREQD	0x04
#define NI_NUMERICSERV	0x08
#define NI_DGRAM	0x10

/* getaddrinfo constants */
#define AI_PASSIVE	1
#define AI_CANONNAME	2
#define AI_NUMERICHOST	4

/* getaddrinfo error codes */
#define EAI_AGAIN	WSATRY_AGAIN
#define EAI_BADFLAGS	WSAEINVAL
#define EAI_FAIL	WSANO_RECOVERY
#define EAI_FAMILY	WSAEAFNOSUPPORT
#define EAI_MEMORY	WSA_NOT_ENOUGH_MEMORY
#define EAI_NODATA	WSANO_DATA
#define EAI_NONAME	WSAHOST_NOT_FOUND
#define EAI_SERVICE	WSATYPE_NOT_FOUND
#define EAI_SOCKTYPE	WSAESOCKTNOSUPPORT

char* WSAAPI gai_strerrorA(int);
WCHAR* WSAAPI gai_strerrorW(int);
#ifdef UNICODE
#define gai_strerror   gai_strerrorW
#else
#define gai_strerror   gai_strerrorA
#endif  /* UNICODE */

#endif

#define WspiapiMalloc(tSize)    calloc(1, (tSize))
#define WspiapiFree(p)          free(p)
#define WspiapiSwap(a, b, c)    { (c) = (a); (a) = (b); (b) = (c); }
#define getaddrinfo             WspiapiGetAddrInfo
#define getnameinfo             WspiapiGetNameInfo
#define freeaddrinfo            WspiapiFreeAddrInfo

typedef int (WINAPI *WSPIAPI_PGETADDRINFO) (
    IN  const char                      *nodename,
    IN  const char                      *servname,
    IN  const struct addrinfo           *hints,
    OUT struct addrinfo                 **res);

typedef int (WINAPI *WSPIAPI_PGETNAMEINFO) (
    IN  const struct sockaddr           *sa,
    IN  socklen_t                       salen,
    OUT char                            *host,
    IN  size_t                          hostlen,
    OUT char                            *serv,
    IN  size_t                          servlen,
    IN  int                             flags);

typedef void (WINAPI *WSPIAPI_PFREEADDRINFO) (
    IN  struct addrinfo                 *ai);



#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////////////////////////////////
// v4 only versions of getaddrinfo and friends.
// NOTE: gai_strerror is inlined in ws2tcpip.h
////////////////////////////////////////////////////////////
char *
WINAPI
WspiapiStrdup (
        IN  const char *                    pszString);


BOOL
WINAPI
WspiapiParseV4Address (
    IN  const char *                    pszAddress,
    OUT PDWORD                          pdwAddress);

struct addrinfo *
WINAPI
WspiapiNewAddrInfo (
    IN  int                             iSocketType,
    IN  int                             iProtocol,
    IN  WORD                            wPort,
    IN  DWORD                           dwAddress);

int
WINAPI
WspiapiQueryDNS(
    IN  const char                      *pszNodeName,
    IN  int                             iSocketType,
    IN  int                             iProtocol,
    IN  WORD                            wPort,
    OUT char                            *pszAlias,
    OUT struct addrinfo                 **pptResult);


int
WINAPI
WspiapiLookupNode(
    IN  const char                      *pszNodeName,
    IN  int                             iSocketType,
    IN  int                             iProtocol,
    IN  WORD                            wPort,
    IN  BOOL                            bAI_CANONNAME,
    OUT struct addrinfo                 **pptResult);

int
WINAPI
WspiapiClone (
    IN  WORD                            wPort,
    IN  struct addrinfo                 *ptResult);


void
WINAPI
WspiapiLegacyFreeAddrInfo (
    IN  struct addrinfo                 *ptHead);


int
WINAPI
WspiapiLegacyGetAddrInfo(
    IN const char                       *pszNodeName,
    IN const char                       *pszServiceName,
    IN const struct addrinfo            *ptHints,
    OUT struct addrinfo                 **pptResult);

int
WINAPI
WspiapiLegacyGetNameInfo(
    IN  const struct sockaddr           *ptSocketAddress,
    IN  socklen_t                       tSocketLength,
    OUT char                            *pszNodeName,
    IN  size_t                          tNodeLength,
    OUT char                            *pszServiceName,
    IN  size_t                          tServiceLength,
    IN  int                             iFlags);


typedef struct
{
    char const          *pszName;
    FARPROC             pfAddress;
} WSPIAPI_FUNCTION;

#define WSPIAPI_FUNCTION_ARRAY                                  \
{                                                               \
    "getaddrinfo",      (FARPROC) WspiapiLegacyGetAddrInfo,     \
    "getnameinfo",      (FARPROC) WspiapiLegacyGetNameInfo,     \
    "freeaddrinfo",     (FARPROC) WspiapiLegacyFreeAddrInfo,    \
}


FARPROC
WINAPI
WspiapiLoad(
    IN  WORD                            wFunction);

int
WINAPI
WspiapiGetAddrInfo(
    IN const char                       *nodename,
    IN const char                       *servname,
    IN const struct addrinfo            *hints,
    OUT struct addrinfo                 **res);


int
WINAPI
WspiapiGetNameInfo (
    IN  const struct sockaddr           *sa,
    IN  socklen_t                       salen,
    OUT char                            *host,
    IN  size_t                          hostlen,
    OUT char                            *serv,
    IN  size_t                          servlen,
    IN  int                             flags);


void
WINAPI
WspiapiFreeAddrInfo (
    IN  struct addrinfo                 *ai);

#ifdef  __cplusplus
}
#endif

#endif // _WSPIAPI_H_
