#ifndef EXTERNS_H
#define EXTERNS_H

#ifndef ARG
#ifdef __STDC__
#define ARG(x) x
#else
#define ARG(x) ()
#endif
#endif

char           *fileglue ARG((char *,...));
char           *ascii_date ARG(());
char          **split ARG((char *, char *));

#endif
