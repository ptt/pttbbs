/* $Id$ */

#ifndef _CMDIET_H
#define _CMDIET_H

#ifdef __dietlibc__
#define random     glibc_random
#define srandom            glibc_srandom
#define initstate   glibc_initstate
#define setstate    glibc_setstate
long int random(void);
void srandom(unsigned int seed);
char *initstate(unsigned int seed, char *state, size_t n);
char *setstate(char *state);
#endif
 
#endif
