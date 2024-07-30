#ifndef _TUTIL_H_
#define _TUTIL_H_

/*
 * Format : 2024-07-23 10:38:24
 * @param buf - Buffer length must be larger or equal then 20.
 */
void tstamp_sec(char *buf, size_t buflen);

/*
 * Format : 2024-070-23 10:38:24 234
 * @param buf - Buffer length must be larger or equal then 24.
 */
void tstamp_msec(char *buf, size_t buflen);

#endif // _TUTIL_H_
