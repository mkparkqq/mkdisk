#include <sys/time.h>
#include <time.h>
#include <stdio.h>

/*
 * Format : 2024-07-23 10:38:24
 * @param buf - Buffer length must be larger or equal then 20.
 */
void 
tstamp_sec(char *buf, size_t buflen)
{
	time_t t;
	struct tm *tmp;

	time(&t);
	tmp = localtime(&t);
	strftime(buf, buflen, "%F %T", tmp);
}

/*
 * Format : 2024-070-23 10:38:24:234
 * @param buf - Buffer length must be larger or equal then 25.
 */
void 
tstamp_msec(char *buf, int buflen)
{

	struct timeval tv;
	struct tm *tmp;
	char buf1[20];

	gettimeofday(&tv, NULL);
	tmp = localtime(&tv.tv_sec);
	strftime(buf1, buflen, "%F %T", tmp);
	snprintf(buf, buflen, "%s:%ld", buf1, tv.tv_usec);
}

