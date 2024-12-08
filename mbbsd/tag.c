#include <stdlib.h>
#include <string.h>

#include "bbs.h"

/* ----------------------------------------------------- */
/* Tag List ╪пер                                         */
/* ----------------------------------------------------- */
struct TagItem {
    // TODO use aid_t
    char filename[FNLEN];
};

static struct TagItem *TagList = NULL;	/* ascending list */
static int             TagNum = 0;	/* tag's number */

static int compare_tagitem(const void *pa, const void *pb) {
    const struct TagItem *taga = pa, *tagb = pb;
    return strcmp(taga->filename, tagb->filename);
}

int IsEmptyTagList() {
    return !TagList || TagNum <= 0;
}

void ClearTagList() {
    TagNum = 0;
}

struct TagItem *FindTaggedItem(const fileheader_t *fh) {
    if (IsEmptyTagList())
        return NULL;

    return bsearch(fh->filename, TagList, TagNum, sizeof(*TagList), compare_tagitem);
}

struct TagItem *RemoveTagItem(const fileheader_t *fh) {
    struct TagItem *tag = IsEmptyTagList() ? NULL : FindTaggedItem(fh);
    if (!tag)
        return NULL;

    TagNum--;
    memmove(tag, tag + 1, (TagNum - (tag - TagList)) * sizeof(*tag));
    return tag;
}

struct TagItem *AddTagItem(const fileheader_t *fh) {
    if (TagNum == MAXTAGS)
        return NULL;
    if (TagList == NULL)
        TagList = calloc(MAXTAGS + 1, sizeof(*TagList));
    else
        memset(TagList+TagNum, 0, sizeof(*TagList));
    // assert(!FindTaggedItem(fh));
    strlcpy(TagList[TagNum++].filename, fh->filename, sizeof(fh->filename));
    qsort(TagList, TagNum, sizeof(*TagList), compare_tagitem);
    return FindTaggedItem(fh);
}

struct TagItem *ToggleTagItem(const fileheader_t *fh) {
    struct TagItem *tag = RemoveTagItem(fh);
    if (tag)
        return tag;
    return AddTagItem(fh);
}
