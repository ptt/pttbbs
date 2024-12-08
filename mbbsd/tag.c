#include <stdlib.h>
#include <string.h>

#include "bbs.h"

/* ----------------------------------------------------- */
/* Tag List ╪пер                                         */
/* ----------------------------------------------------- */
static TagItem         *TagList = NULL;	/* ascending list */
static int             TagNum = 0;	/* tag's number */

static int compare_tagitem(const void *pa, const void *pb) {
    TagItem *taga = (TagItem*) pa,
            *tagb = (TagItem*) pb;
    return strcmp(taga->filename, tagb->filename);
}

int IsEmptyTagList() {
    return !TagList || TagNum <= 0;
}

void ClearTagList() {
    TagNum = 0;
}

TagItem *FindTaggedItem(const fileheader_t *fh) {
    if (IsEmptyTagList())
        return NULL;

    return (TagItem*)bsearch(
            fh->filename, TagList, TagNum, sizeof(TagItem), compare_tagitem);
}

TagItem *RemoveTagItem(const fileheader_t *fh) {
    TagItem *tag = IsEmptyTagList() ? NULL : FindTaggedItem(fh);
    if (!tag)
        return tag;

    TagNum--;
    memmove(tag, tag + 1, (TagNum - (tag - TagList)) * sizeof(TagItem));
    return tag;
}

TagItem *AddTagItem(const fileheader_t *fh) {
    if (TagNum == MAXTAGS)
        return NULL;
    if (TagList == NULL) {
        const size_t sz = sizeof(TagItem) * (MAXTAGS + 1);
        TagList = (TagItem*) malloc(sz);
        memset(TagList, 0, sz);
    } else {
        memset(TagList+TagNum, 0, sizeof(TagItem));
    }
    // assert(!FindTaggedItem(fh));
    strlcpy(TagList[TagNum++].filename, fh->filename, sizeof(fh->filename));
    qsort(TagList, TagNum, sizeof(TagItem), compare_tagitem);
    return FindTaggedItem(fh);
}

TagItem *ToggleTagItem(const fileheader_t *fh) {
    TagItem *tag = RemoveTagItem(fh);
    if (tag)
        return tag;
    return AddTagItem(fh);
}
