#define char_lower(c)  ((c >= 'A' && c <= 'Z') ? c|32 : c)

#if 0 /* string.h , libc */
int
strcasestr(str, tag)
  char *str, *tag;              /* tag : lower-case string */
{
  char buf[256];

  str_lower(buf, str);
  return (int) strstr(buf, tag);
}
#endif

int
bad_subject(char *subject)
{
   char *badkey[] = {"µL½X","avcd","mp3",NULL};
   int i;
   for(i=0; badkey[i]; i++)
     if(strcasestr(subject, badkey[i])) return 1;
   return 0;
}           

