/* $Id$ */
#include "bbs.h"

#define NEWSDIRECT  BBSHOME "/boards/n/newspaper"
#define MOVIEDIRECT BBSHOME "/etc/NEWS"

int main(int argc, char **argv)
{
    int fd;
    fileheader_t fh, news;
    struct stat st;
    register int numfiles, n;
    FILE *fp = NULL;
    char buf[200];

    if (stat(NEWSDIRECT "/.DIR", &st) < 0)
	return 0;

    system("rm -f " MOVIEDIRECT "/*");
    system("rm -f " MOVIEDIRECT "/.DIR");

    numfiles = st.st_size / sizeof(fileheader_t);
    n = 0;
    if ((fd = open(NEWSDIRECT "/.DIR", O_RDONLY)) > 0)
    {
	lseek(fd, st.st_size - sizeof(fileheader_t), SEEK_SET);
	while (numfiles-- && n < 100)
	{
	    read(fd, &fh, sizeof(fileheader_t));
	    if (!strcmp(fh.owner, "CNA-News."))
	    {
		if (!strstr(fh.title, "¬¡°Ê¹w§i") && !strstr(fh.title, "¤¤¥¡®ð¶H§½")
		    && !strstr(fh.title, "¾ú¥v¤Wªº¤µ¤Ñ") && !strstr(fh.title, "ÀY±ø·s»D¼ÐÃD")
		    && !strstr(fh.title, "Summary") && !strstr(fh.title, "¥þ²y®ð¶H¤@Äý")
		    && !strstr(fh.title, "®Õ¥¿¤½¹q"))
		{
		    if (!(n % 10))
		    {
			if (n)
			{
			    fclose(fp);
			    append_record(MOVIEDIRECT "/.DIR", &news, sizeof(news));
			}
			strcpy(buf, MOVIEDIRECT);
			stampfile(buf, &news);
			sprintf(news.title, "¤¤¥¡ªÀ§Y®É·s»D %s", fh.date);
			strcpy(news.owner, "CNA-News.");
			if (!(fp = fopen(buf, "w")))
			    return (0);
			fprintf(fp, "[34m        ¢w¢w¢w¢w¢w¢w¢w¢w¢w[47;30m ¤¤¥¡ªÀ§Y®É·s»D[31m (%s)[m[34m¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w\n",
				fh.date);
		    }
		    fprintf(fp, "        ¢w¢w¢w¢w¢w[33m¡º[m [1;3%dm%s  [m[34m%.*s\n",
			    (n % 6 + 4) % 7 + 1, fh.title,
			    (int)(46 - strlen(fh.title)),
			    "¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w¢w");
		    n++;
		    printf("[%d]\n", n);

		}
	    }
	    lseek(fd, -(off_t) 2 * sizeof(fileheader_t), SEEK_CUR);
	}
	close(fd);
	fclose(fp);
	append_record(MOVIEDIRECT "/.DIR", &news, sizeof(news));
    }
    return 0;
}
