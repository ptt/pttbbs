#define _UTIL_C_
#include <vector>
#include <string>
#include <algorithm>
extern "C" {
#include "bbs.h"
}

void usage() {
    fprintf(stderr, "Usage: "
	    "merge_dir <output> <dir1> <dir2> ...\n\n"
	    "\tMerges multiple DIR files, removing deleted posts.\n");
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

bool cmp_by_posttime_order(const AnnotatedHeader& a, const AnnotatedHeader& b)
{
    return a.posttime() != b.posttime() ? a.posttime() < b.posttime() : a.order < b.order;
}

int read_dir(std::vector<AnnotatedHeader>& hdrs, const char *path, int offset)
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
	hdrs.push_back(hdr);
    }

    close(fd);
    return n == 0 ? count : -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
	usage();
	return 1;
    }

    const char *path = argv[1];
    int offset = 0;
    std::vector<AnnotatedHeader> hdrs;

    for (int i = 2; i < argc; ++i) {
	int nr = read_dir(hdrs, argv[i], offset);
	if (nr < 0) {
	    return 1;
	}
	offset += nr;
    }

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
