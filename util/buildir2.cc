#include <unordered_set>
#include <vector>
#include <string>
extern "C" {
#include "bbs.h"
#include "getopt.h"
}

#define DEFAULT_POST_PREFIXES "MDSGH"

static bool verbose = false;
static const char *accept_prefixes = DEFAULT_POST_PREFIXES;

#define DETECT_OK (0)
#define DETECT_FAILED (-1)

int detect_header(fileheader_t *fhdr, const char *path)
{
    const char *filename = strrchr(path, '/');
    if (filename == NULL)
	filename = path;
    else
	++filename;

    struct stat st;
    if (stat(path, &st) != 0 || st.st_size == 0)
	return DETECT_FAILED;

    FILE *fp = fopen(path, "r");
    if (fp == NULL)
	return DETECT_FAILED;

    memset(fhdr, 0, sizeof(*fhdr));
    /* set file name */
    strcpy(fhdr->filename, filename);

    /* set file time */
    time4_t filetime = atoi(filename + 2);
    if(filetime > 740000000) {
	struct tm *ptime = localtime4(&filetime);
	sprintf(fhdr->date, "%2d/%02d", ptime->tm_mon + 1,
		ptime->tm_mday);
    } else
	strcpy(fhdr->date, "     ");

    /* set file mode */
    fhdr->filemode = FILE_READ;

    /* set article owner */
    char buf[512];
    fgets(buf, sizeof(buf), fp);
    if(strncmp(buf, "作者: ", 6) == 0 ||
	    strncmp(buf, "發信人: ", 8) == 0) {
	int i, j;

	for(i = 5; buf[i] != ' '; i++);
	for(; buf[i] == ' '; i++);
	for(j = i + 1; buf[j] != ' '; j++);
	j -= i;
	if(j > IDLEN + 1)
	    j = IDLEN + 1;
	strncpy(fhdr->owner, buf + i, j);
	fhdr->owner[IDLEN + 1] = '\0';
	strtok(fhdr->owner, " .@\t\n\r");
	if(strtok(NULL, " .@\t\n\r"))
	    strcat(fhdr->owner, ".");

	/* set article title */
	while(fgets(buf, sizeof(buf), fp))
	    if(strncmp(buf, "標題: ", 6) == 0 ||
		    strncmp(buf, "標  題: ", 8) == 0) {
		for( i = 5 ; buf[i] != ' ' ; ++i )
		    ;
		for( ; buf[i] == ' ' ; ++i )
		    ;
		strtok(buf + i - 1, "\n");
		strncpy(fhdr->title, buf + i, TTLEN);
		fhdr->title[TTLEN] = '\0';
		for( i = 0 ; fhdr->title[i] != 0 ; ++i )
		    if( fhdr->title[i] == '\e' ||
			    fhdr->title[i] == '\b' )
			fhdr->title[i] = ' ';
		if (verbose)
		    fprintf(stderr, "%s, owner: %s, title: %s\n",
			    path, fhdr->owner, fhdr->title);
		break;
	    }
    } else if(strncmp(buf, "⊙ 歡迎光臨", 11) == 0) {
	strcpy(fhdr->title, "會議記錄");
    } else if(strncmp(buf, "\33[1;33;46m★", 12) == 0||
	    strncmp(buf, "To", 2) == 0) {
	strcpy(fhdr->title, "熱線記錄");
    }
    if(!fhdr->title[0])
	strcpy(fhdr->title, "無標題");
    fclose(fp);
    return DETECT_OK;
}

int dirselect(const struct dirent *dir)
{
    return strchr(accept_prefixes, dir->d_name[0]) && dir->d_name[1] == '.';
}

int mysort(const struct dirent **a, const struct dirent **b)
{
    return atoi(((*a)->d_name+2))-atoi(((*b)->d_name+2));
}

bool insert_filenames(
	std::unordered_set<std::string>& index,
	const std::string& dirpath)
{
    int fd = open(dirpath.c_str(), O_RDONLY);
    if (fd < 0) {
	perror("open");
	return false;
    }

    int n = -1;
    fileheader_t fhdr;
    while ((n = read(fd, &fhdr, sizeof(fhdr))) == sizeof(fhdr)) {
	index.insert(fhdr.filename);
    }

    close(fd);
    return n == 0;
}

static void usage(const char *progname)
{
    fprintf(stderr,
	    "Usage: %s [options] -o output.DIR -d directory\n\n"
	    " -v\n"
	    "\tverbose\n"
	    " -d directory\n"
	    "\t(Required) The directory to search for posts.\n"
	    " -i index.DIR\n"
	    "\t(Can be specified zero or more times) The .DIR file to read from\n"
	    " -o output.DIR\n"
	    "\t(Required) Output DIR.\n"
	    " -t epoch\n"
	    "\tIgnore post before certain time point. (Default: 0)\n"
	    " -p prefixes\n"
	    "\tThe program will only search files for these prefixes. (Default: " DEFAULT_POST_PREFIXES ")\n",
	    progname);
}

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

int main(int argc, char **argv)
{
    std::string outdir, bdir;
    std::vector<std::string> dirs;
    time4_t ignore_before = 0;

    if (argc <= 1) {
	usage(argv[0]);
	return 1;
    }

    int opt;
    while ((opt = getopt(argc, argv, "vd:i:o:t:p:")) != -1) {
	switch (opt) {
	    case 'v':
		verbose = true;
		break;

	    case 'd':
		if (!bdir.empty())
		    die("-d can only be specified once.");
		bdir = optarg;
		break;

	    case 'i':
		dirs.push_back(optarg);
		break;

	    case 'o':
		if (!outdir.empty())
		    die("-o can only be specified once.");
		outdir = optarg;
		break;

	    case 't':
		ignore_before = atoi(optarg);
		if (std::to_string(ignore_before) != optarg)
		    die("Unable to parse option -t");
		break;

	    case 'p':
		accept_prefixes = optarg;
		break;

	    default:
		usage(argv[0]);
		return 1;
	}
    }
    if (optind < argc) {
	fprintf(stderr, "Extra arguments: %s\n", argv[optind]);
	return 1;
    }

    if (outdir.empty())
	die("-o must be specified");
    if (bdir.empty())
	die("-d must be specified");

    std::unordered_set<std::string> indexed;
    for (const auto& dir : dirs) {
	if (!insert_filenames(indexed, dir)) {
	    fprintf(stderr, "Failed to read DIR: %s\n", dir.c_str());
	    return 1;
	}
    }
    fprintf(stderr, "%d entries loaded.\n", (int)indexed.size());

    int nr_indexed = 0, nr_written = 0, nr_failed = 0, nr_skip = 0;
    int fout;
    int total;
    struct dirent **dirlist;

    if ((total = scandir(bdir.c_str(), &dirlist, dirselect, mysort)) == -1) {
	fprintf(stderr, "scandir failed!\n");
    }

    if ((fout = OpenCreate(outdir.c_str(), O_WRONLY | O_TRUNC)) == -1) {
	perror(outdir.c_str());
    }

    char path[MAXPATHLEN];
    fileheader_t fhdr;
    for (int i = 0; i < total; ++i) {
	const char *filename = dirlist[i]->d_name;
	if (indexed.find(filename) != indexed.end()) {
	    ++nr_indexed;
	    continue;
	}
	snprintf(path, sizeof(path), "%s/%s", bdir.c_str(), filename);
	time4_t posttime = atoi(filename + 2);
	if (posttime < ignore_before) {
	    ++nr_skip;
	} else if (detect_header(&fhdr, path) == DETECT_OK) {
	    write(fout, &fhdr, sizeof(fhdr));
	    ++nr_written;
	} else {
	    ++nr_failed;
	}
    }
    close(fout);

    for(int i = 0; i < total; ++i)
	free(dirlist[i]);
    free(dirlist);

    fprintf(stderr, "%d already indexed, %d written, %d failed.\n",
	    nr_indexed, nr_written, nr_failed);
    return 0;
}
