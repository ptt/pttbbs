#define _UTIL_C_
#include <map>
#include <vector>
#include <string>
#include <algorithm>
extern "C" {
#include "bbs.h"
}

void usage(const char *progname) {
    fprintf(stderr, "Usage: "
	    "%s <output> <dir1> <dir2> ...\n\n"
	    "Merges multiple DIR files, removing deleted (check by filename field; will not check filesystem) posts.\n"
	    "\n"
	    "The files are sorted with post time derived from filename.\n"
	    "If post time happens to be the same, the order will be the same as specified in the command line.\n"
	    "Dot-DIR file is processed in the order that specified in command line.\n"
	    "Later occurences of the same filename replaces previous files.\n"
	    "\n"
	    "This program is useful when merging from backup. Here's an example invocation of this use case:\n"
	    "$ %s .DIR.merge /backup-path/.DIR .DIR && cp .DIR .DIR.bak && mv .DIR.merge .DIR\n"
	    "Note: You might want to `shmctl touchboard <brdname>` afterwards. This program will not do this for you.\n"
	    "\n",
	    progname, progname);
}

struct AnnotatedHeader {
    int order;
    fileheader_t header;

    time4_t posttime() const { return atoi(header.filename + 2); }
    std::string filename() const { return header.filename; }
    bool valid() const {
	return header.owner[0] && header.filename[0] && header.filename[0] != '.';
    }
};

struct AnnotatedHeaderSet {
    AnnotatedHeaderSet() {}
    void Update(const AnnotatedHeader &hdr) {
	fn_header_map[hdr.filename()] = hdr;
    }
    void CopyToVector(std::vector<AnnotatedHeader> &out) {
	for (std::map<std::string, AnnotatedHeader>::const_iterator it = fn_header_map.begin();
	     it != fn_header_map.end(); ++it) {
	    out.push_back(it->second);
	}
    }
  private:
    std::map<std::string, AnnotatedHeader> fn_header_map;

    AnnotatedHeaderSet(const AnnotatedHeaderSet &);
    AnnotatedHeaderSet &operator=(const AnnotatedHeaderSet &);
};

bool cmp_by_posttime_order(const AnnotatedHeader& a, const AnnotatedHeader& b)
{
    return a.posttime() != b.posttime() ? a.posttime() < b.posttime() : a.order < b.order;
}

int read_dir(AnnotatedHeaderSet &hdrset, const char *path, int offset)
{
    AnnotatedHeader hdr;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
	perror(path);
	return -1;
    }

    int count = 0;
    int n;
    while ((n = read(fd, &hdr.header, sizeof(hdr.header))) == sizeof(hdr.header)) {
	if (!hdr.valid())
	    continue;
	hdr.order = offset + count++;
	hdrset.Update(hdr);
    }

    close(fd);
    return n == 0 ? count : -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
	usage(argv[0]);
	return 1;
    }

    const char *path = argv[1];
    int offset = 0;
    AnnotatedHeaderSet hdrset;

    for (int i = 2; i < argc; ++i) {
	int nr = read_dir(hdrset, argv[i], offset);
	if (nr < 0) {
	    return 1;
	}
	offset += nr;
    }

    std::vector<AnnotatedHeader> hdrs;
    hdrset.CopyToVector(hdrs);
    std::sort(hdrs.begin(), hdrs.end(), cmp_by_posttime_order);

    int fd = open(path, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if (fd < 0) {
	perror(path);
	return 1;
    }

    std::string last;
    for (const auto& hdr : hdrs) {
	if (hdr.filename() == last)
	    continue;

	if (write(fd, &hdr.header, sizeof(hdr.header)) != sizeof(hdr.header)) {
	    perror("write");
	    return 1;
	}
    }
    close(fd);
    return 0;
}
