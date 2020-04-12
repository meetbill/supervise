/* Public domain. */

#ifndef TAIA_H
#define TAIA_H

#include "tai.h"

struct taia {
  struct tai sec; //struct tai {uint_64 x;};
  // 1 sec = 10e3 mS = 10e6 uS = 10e9 nS = 10e12 pS = 10e18 aS
  unsigned long nano; /* 0...999999999 10e-9*/
  unsigned long atto; /* 0...999999999 10e-18*/
} ;

extern void taia_tai(const struct taia *,struct tai *);

extern void taia_now(struct taia *);

extern double taia_approx(const struct taia *);
extern double taia_frac(const struct taia *);

extern void taia_add(struct taia *,const struct taia *,const struct taia *);
extern void taia_addsec(struct taia *,const struct taia *,int);
extern void taia_sub(struct taia *,const struct taia *,const struct taia *);
extern void taia_half(struct taia *,const struct taia *);
extern int taia_less(const struct taia *,const struct taia *);

#define TAIA_PACK 16
extern void taia_pack(char *,const struct taia *);
extern void taia_unpack(const char *,struct taia *);

#define TAIA_FMTFRAC 19
extern unsigned int taia_fmtfrac(char *,const struct taia *);

extern void taia_uint(struct taia *,unsigned int);

#endif
