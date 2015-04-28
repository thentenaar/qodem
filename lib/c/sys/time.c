#include <sys/time.h>

#ifdef __BORLANDC__

#include <sys/timeb.h>

/*
 * Use ftime() to derive gettimeofday() equivalent data.
 */
void gettimeofday(struct timeval * tv, struct timezone * ignored) {
        struct timeb localtime;
        if (tv == NULL) {
                return;
        }
        ftime(&localtime);
        tv->tv_sec  = localtime.time + localtime.timezone;
        tv->tv_usec = localtime.millitm * 1000;
} /* ---------------------------------------------------------------------- */

#endif /* __BORLANDC__ */
