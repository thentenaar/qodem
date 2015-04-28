#ifndef __Q_SYS_TIME_H__
#define __Q_SYS_TIME_H__

#include <winsock.h>    /* struct timeval */

/*
 * Use ftime() to derive gettimeofday() equivalent data.
 */
extern void gettimeofday(struct timeval * tv, struct timezone * ignored);

#endif /* __Q_SYS_TIME_H__ */
