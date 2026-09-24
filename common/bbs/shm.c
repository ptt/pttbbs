#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#ifdef __linux__
#include <sys/vfs.h>
#endif
#include "cmsys.h"
#include "cmbbs.h"
#include "common.h"
#include "var.h"

#ifdef USE_POSIX_SHM
static const bool use_posix_shm = true;
#else
static const bool use_posix_shm = false;
#endif

#ifdef USE_HUGETLB
#define HUGEPAGE_DEFAULT_ALIGN (2UL * 1024 * 1024)

/* Where hugetlbfs is mounted (Linux). */
#ifndef HUGETLBFS_PATH
#define HUGETLBFS_PATH "/dev/hugepages"
#endif

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif

static inline size_t
round_up_align(size_t sz, size_t align)
{
    if (align == 0)
        return sz;
    return ((sz + align - 1) / align) * align;
}

static void
hugetlb_error(const char *what, const char *path, size_t page_size, int err)
{
    size_t pages = page_size ? round_up_align(sizeof(SHM_t), page_size) / page_size : 0;
    fprintf(stderr, "[hugetlb %s error] name = %s, size = %zu, page = %zu, "
            "pages = %zu, errno = %d: %s\n",
            what, path, sizeof(SHM_t), page_size, pages, err, strerror(err));
}

/*
 * USE_HUGETLB never falls back to regular pages: hugetlbfs (Linux) and
 * largepage objects (FreeBSD) live in a different namespace from the
 * generic POSIX SHM, so a fallback would let creators and attachers end up
 * on different segments.
 */
#if defined(__linux__)
static SHM_t *
hugetlb_open_shm(const char *name, int *is_created)
{
    void *shmptr;
    int create = !!is_created;
    int is_new = 0;
    int fd, err;
    size_t page_size = 0, map_size;
    struct statfs sfs;
    struct stat st;
    char real_path[PATHLEN];

    snprintf(real_path, sizeof(real_path), "%s/%s", HUGETLBFS_PATH,
             name[0] == '/' ? name + 1 : name);

    if (create) {
        fd = open(real_path, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd >= 0)
            is_new = 1;
        else if (errno == EEXIST)
            fd = open(real_path, O_RDWR);
    } else {
        fd = open(real_path, O_RDWR);
    }
    if (fd < 0) {
        hugetlb_error("open", real_path, 0, errno);
        fprintf(stderr, "USE_HUGETLB needs hugetlbfs mounted on %s and "
                "writable by the BBS user.\n", HUGETLBFS_PATH);
        return NULL;
    }

    if (fstatfs(fd, &sfs) < 0) {
        hugetlb_error("fstatfs", real_path, 0, errno);
        goto fail;
    }
    if ((unsigned long)sfs.f_type != (unsigned long)HUGETLBFS_MAGIC ||
        sfs.f_bsize <= 0) {
        fprintf(stderr, "[hugetlb error] %s is not on hugetlbfs "
                "(f_type = 0x%lx). Mount hugetlbfs on %s.\n",
                real_path, (unsigned long)sfs.f_type, HUGETLBFS_PATH);
        goto fail;
    }
    page_size = (size_t)sfs.f_bsize;
    map_size = round_up_align(sizeof(SHM_t), page_size);

    if (is_new) {
        if (ftruncate(fd, (off_t)map_size) < 0) {
            hugetlb_error("ftruncate", real_path, page_size, errno);
            goto fail;
        }
    } else if (fstat(fd, &st) < 0) {
        hugetlb_error("fstat", real_path, page_size, errno);
        goto fail;
    } else if (st.st_size < (off_t)sizeof(SHM_t)) {
        fprintf(stderr, "[hugetlb error] %s is too small (%lld < %zu). "
                "Remove it and run shmctl init again.\n",
                real_path, (long long)st.st_size, sizeof(SHM_t));
        goto fail;
    }

    shmptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shmptr == MAP_FAILED) {
        hugetlb_error("mmap", real_path, page_size, errno);
        fprintf(stderr, "Check HugePages_Free in /proc/meminfo "
                "(vm.nr_hugepages).\n");
        goto fail;
    }
    close(fd);
    if (is_created)
        *is_created = is_new;
    return (SHM_t *)shmptr;

fail:
    err = errno;
    close(fd);
    if (is_new)
        unlink(real_path);
    errno = err;
    return NULL;
}
#elif defined(__FreeBSD__)
#ifndef SHM_LARGEPAGE_ALLOC_DEFAULT
#error "USE_HUGETLB with USE_POSIX_SHM needs largepage SHM (FreeBSD 13+)."
#endif
static SHM_t *
hugetlb_open_shm(const char *name, int *is_created)
{
    void *shmptr;
    int create = !!is_created;
    int is_new = 0;
    int fd, err;
    int flags = MAP_SHARED;
    size_t page_size = HUGEPAGE_DEFAULT_ALIGN;
    size_t map_size = round_up_align(sizeof(SHM_t), page_size);
    struct stat st;

    if (create) {
        fd = shm_create_largepage(name, O_CREAT | O_EXCL | O_RDWR,
                                  1 /* 2MB superpage */,
                                  SHM_LARGEPAGE_ALLOC_DEFAULT, 0666);
        if (fd >= 0)
            is_new = 1;
        else if (errno == EEXIST)
            fd = shm_open(name, O_RDWR, 0);
    } else {
        fd = shm_open(name, O_RDWR, 0);
    }
    if (fd < 0) {
        hugetlb_error("shm_open", name, page_size, errno);
        return NULL;
    }

    if (is_new) {
        if (ftruncate(fd, (off_t)map_size) < 0) {
            hugetlb_error("ftruncate", name, page_size, errno);
            goto fail;
        }
    } else if (fstat(fd, &st) < 0) {
        hugetlb_error("fstat", name, page_size, errno);
        goto fail;
    } else if (st.st_size < (off_t)sizeof(SHM_t)) {
        fprintf(stderr, "[hugetlb error] %s is too small (%lld < %zu). "
                "Remove it and run shmctl init again.\n",
                name, (long long)st.st_size, sizeof(SHM_t));
        goto fail;
    }

#ifdef MAP_ALIGNED_SUPER
    flags |= MAP_ALIGNED_SUPER;
#endif
    shmptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, flags, fd, 0);
    if (shmptr == MAP_FAILED) {
        hugetlb_error("mmap", name, page_size, errno);
        goto fail;
    }
    close(fd);
    if (is_created)
        *is_created = is_new;
    return (SHM_t *)shmptr;

fail:
    err = errno;
    close(fd);
    if (is_new)
        shm_unlink(name);
    errno = err;
    return NULL;
}
#else
#error "USE_HUGETLB with USE_POSIX_SHM is only supported on Linux and FreeBSD."
#endif
#endif /* USE_HUGETLB */

static SHM_t *
posix_open_shm(const char *path, int *is_created)
{
#ifdef USE_HUGETLB
    return hugetlb_open_shm(path, is_created);
#else
    void *shmptr = NULL;
    int is_new = 0;
    int fd = -1;
    int create = !!is_created;
    size_t map_size = sizeof(SHM_t);

    if (create) {
        fd = shm_open(path, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd >= 0) {
            is_new = 1;
        } else if (errno == EEXIST) {
            fd = shm_open(path, O_RDWR, 0666);
        }
    } else {
        fd = shm_open(path, O_RDWR, 0666);
    }
    if (fd < 0) {
        fprintf(stderr, "[shm_open error] name = %s, errno = %d: %s\n",
                path, errno, strerror(errno));
        return NULL;
    }

    if (create && is_new) {
        if (ftruncate(fd, (off_t)map_size) < 0) {
            fprintf(stderr, "[ftruncate error] name = %s, size = %zu, errno = %d: %s\n",
                    path, map_size, errno, strerror(errno));
            close(fd);
            return NULL;
        }
    }

    if (is_created) {
        *is_created = is_new;
    }

    shmptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (shmptr == MAP_FAILED) {
        fprintf(stderr, "[mmap error] name = %s, size = %zu, errno = %d: %s\n",
                path, map_size, errno, strerror(errno));
        return NULL;
    }
    return (SHM_t *)shmptr;
#endif
}

static void
posix_shm_error(void)
{
    fprintf(stderr,
            "Shared Memory [name: %s, version: %d] ERROR!!\n"
            "System is not initialized. Run initbbs first.\n",
            SHM_NAME, SHM_VERSION);
}

static SHM_t *
sysv_open_shm(int shmkey, int *is_created)
{
    void *shmptr = NULL;
    int shmid = -1;
    int is_new = 0;
    int create = !!is_created;

    int flags = 0;

#ifdef USE_HUGETLB
    flags |= SHM_HUGETLB;
#endif

    if (create) {
        flags |= 0600 | IPC_CREAT;
        shmid = shmget(shmkey, sizeof(SHM_t), flags | IPC_EXCL);
        if (shmid >= 0) {
            is_new = 1;
        } else if (errno == EEXIST) {
            shmid = shmget(shmkey, sizeof(SHM_t), flags);
        }
    } else {
        shmid = shmget(shmkey, sizeof(SHM_t), flags);
    }

    if (shmid < 0) {
        fprintf(stderr, "[shmget error] key = 0x%x (%d), size = %zu, errno = %d: %s\n",
                shmkey, shmkey, sizeof(SHM_t), errno, strerror(errno));
        return NULL;
    }

    if (is_created) {
        *is_created = is_new;
    }

    shmptr = shmat(shmid, NULL, 0);
    if (shmptr == (void *)-1) {
        fprintf(stderr, "[shmat error] key = 0x%x (%d), shmid = %d, errno = %d: %s\n",
                shmkey, shmkey, shmid, errno, strerror(errno));
        return NULL;
    }
    return (SHM_t *)shmptr;
}

static void
sysv_shm_error(void)
{
    fprintf(stderr,
            "Shared Memory [key: 0x%x (%d), version: %d] ERROR!!\n"
            "System is not initialized. Run initbbs first.\n",
            SHM_KEY, SHM_KEY, SHM_VERSION);
}

static void
shm_error(void)
{
    if (use_posix_shm) {
        posix_shm_error();
        return;
    }
    sysv_shm_error();
}

SHM_t *
create_shm(int *is_created)
{
    if (use_posix_shm)
        return posix_open_shm(SHM_NAME, is_created);
    return sysv_open_shm(SHM_KEY, is_created);
}

SHM_t *
attach_shm(void)
{
    return create_shm(NULL);
}

SHM_t *
attach_check_SHM(void)
{
    SHM = attach_shm();
    if (!SHM) {
        return NULL;
    }

    if (SHM->version != SHM_VERSION || SHM->size != sizeof(SHM_t) || !SHM->loaded) {
        SHM = NULL;
        return NULL;
    }

    if (SHM->Btouchtime == 0) {
        SHM->Btouchtime = 1;
    }
    bcache = SHM->bcache;

    if (SHM->Ptouchtime == 0) {
        SHM->Ptouchtime = 1;
    }

    if (SHM->Ftouchtime == 0) {
        SHM->Ftouchtime = 1;
    }

    return SHM;
}

void
attach_SHM(void)
{
    if (!attach_check_SHM()) {
        shm_error();
        exit(1);
    }
}


/* Return the index of uentp (in uinfo). */
int
get_utmp_id(const userinfo_t *uentp)
{
    if (!uentp)
        return -1;
    return (int)(uentp - &SHM->uinfo[0]);
}
