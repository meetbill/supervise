/* Public domain. */

#ifndef ENV_H
#define ENV_H

//environ is a system-wide global virable
extern char **environ;

extern /*@null@*/char *env_get(const char *);

#endif
