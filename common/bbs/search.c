#include <string.h>
#include "cmbbs.h"
#include "pttstruct.h"
#include "modes.h"

int
match_fileheader_predicate(const fileheader_t *fh, fileheader_predicate_t *pred)
{
    const char * const keyword = pred->keyword;
    int sr_mode = pred->mode;

    // The order does not matter. Only single sr_mode at a time.
    if (sr_mode & RS_MARK)
	return fh->filemode & FILE_MARKED;
    else if (sr_mode & RS_SOLVED)
	return fh->filemode & FILE_SOLVED;
    else if (sr_mode & RS_NEWPOST)
	return strncmp(fh->title, "Re:", 3) != 0;
    else if (sr_mode & RS_AUTHOR)
	return DBCS_strcasestr(fh->owner, keyword) != NULL;
    else if (sr_mode & RS_KEYWORD)
	return DBCS_strcasestr(fh->title, keyword) != NULL;
    else if (sr_mode & RS_KEYWORD_EXCLUDE)
	return DBCS_strcasestr(fh->title, keyword) == NULL;
    else if (sr_mode & RS_TITLE)
	return strcasecmp(subject(fh->title), keyword) == 0;
    else if (sr_mode & RS_RECOMMEND)
	return pred->recommend > 0 ?
	    (fh->recommend >= pred->recommend) :
	    (fh->recommend <= pred->recommend);
    else if (sr_mode & RS_MONEY) {
	// We don't know the money if a ref is instead stored.
	// This can happen in DIR generated after select, bottom DIR, etc.
	if (fh->multi.refer.flag || fh->filemode & INVALIDMONEY_MODES ||
	    fh->multi.money > MAX_POST_MONEY) {
	    return 0;
	}
	return fh->multi.money >= pred->money;
    }
    return 0;
}
