#include <stdio.h>

int cmp_int(const void *a, const void *b)
{
    return *(int*)a - *(int*)b;
}

int cmp_int_desc(const void * a, const void * b)
{
    // return cmp_int(b, a);
    return *(int*)b - *(int*)a;
}

int *
intbsearch(int key, const int *base0, int nmemb)
{
    /* 改自 /usr/src/lib/libc/stdlib/bsearch.c ,
       專給搜 int array 用的, 不透過 compar function 故較快些 */
    const   char *base = (const char *)base0;
    size_t  lim;
    int     *p;

    for (lim = nmemb; lim != 0; lim >>= 1) {
	p = (int *)(base + (lim >> 1) * 4);
	if( key == *p )
	    return p;
	if( key > *p ){/* key > p: move right */
	    base = (char *)p + 4;
	    lim--;
	}               /* else move left */
    }
    return (NULL);
}

unsigned int *
uintbsearch(const unsigned int key, const unsigned int *base0, const int nmemb)
{
    /* 改自 /usr/src/lib/libc/stdlib/bsearch.c ,
       專給搜 int array 用的, 不透過 compar function 故較快些 */
    const   char *base = (const char *)base0;
    size_t  lim;
    unsigned int     *p;

    for (lim = nmemb; lim != 0; lim >>= 1) {
        p = (unsigned int *)(base + (lim >> 1) * 4);
        if( key == *p )
            return p;
        if( key > *p ){/* key > p: move right */
            base = (char *)p + 4;
            lim--;
        }               /* else move left */
    }
    return (NULL);
}
