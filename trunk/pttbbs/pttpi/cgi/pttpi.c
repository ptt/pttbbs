/* $Id: pttpi.c,v 1.1 2003/05/19 01:33:00 in2 Exp $ */
#include "bbs.h"
#include "pierr.h"
#include <xmlrpc.h>
#include <xmlrpc_cgi.h>

extern SHM_t *SHM;
typedef xmlrpc_int32 int32;

#define errorexit() if( env->fault_occurred ) return NULL
#define check_board_and_permission(bid)					\
    if( bid < 0 || bid > MAX_BOARD       ||				\
	!bcache[bid].brdname[0]          ||				\
	(bcache[bid].brdattr & BRD_HIDE) ||				\
	(!(bcache[bid].brdattr & BRD_GROUPBOARD) &&			\
	 (bcache[bid].brdattr & BRD_POSTMASK)) )			\
        return xmlrpc_build_value(env, "{s:i}",				\
				  "errno", PIERR_NOBRD);

#define errorreturn(returncode)						\
    return xmlrpc_build_value(env, "{s:i}",				\
                              "errno", returncode)

xmlrpc_value *
getBid(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
    /* !!! overwrite util_cache.c !!! */
    char    *brdname;
    int     bid;

    xmlrpc_parse_value(env, param_array, "(s)", &brdname);
    errorexit();

    bid = getbnum(brdname);
    return xmlrpc_build_value(env, "{s:i,s:s,s:i}",
			      "errno", (bid == -1 ? PIERR_NOBRD : PIERR_OK),
			      "brdname", brdname,
			      "bid", (int32)bid);
}

xmlrpc_value *
getBrdInfo(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
    int32 bid;

    xmlrpc_parse_value(env, param_array, "(i)", &bid);
    errorexit();
    check_board_and_permission(bid);

    return xmlrpc_build_value(env, "{s:i,s:s,s:i,s:6,s:s,s:i}",
			      "errno",   PIERR_OK,
			      "brdname", bcache[bid].brdname,
			      "bid",     (int32)bid,
			      "title",   bcache[bid].title, strlen(bcache[bid].title),
			      "BM",      bcache[bid].BM,
			      "nuser",   bcache[bid].nuser);
}

char *getpath(int bid, char *fn)
{
    static  char    fpath[MAXPATHLEN];
    if( fn == NULL )
	fn = "";
    snprintf(fpath, sizeof(fpath), "boards/%c/%s/%s",
	     bcache[bid].brdname[0], bcache[bid].brdname, fn);
    return fpath;
}

int getfilesize(int bid, char *fn, int *fd)
{
    struct  stat    sb;
    if( fd == NULL ){
	if( stat(getpath(bid, fn), &sb) < 0 )
	    return -1;
    }
    else {
	if( (*fd = open(getpath(bid, fn), O_RDONLY)) < 0 )
	    return -1;
	if( fstat(*fd, &sb) < 0 ){
	    close(*fd);
	    return -1;
	}
    }
    return sb.st_size;
}

xmlrpc_value *
getNarticle(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
    int     bid, nas;

    xmlrpc_parse_value(env, param_array, "(i)", &bid);
    errorexit();
    check_board_and_permission(bid);
    nas = getfilesize(bid, ".DIR", NULL);

    return xmlrpc_build_value(env, "{s:i,s:i}",
			      "errno",    (nas == -1 ? PIERR_INT : PIERR_OK),
			      "narticle", nas / sizeof(fileheader_t));
}

xmlrpc_value *
class_list(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
    int     bid;
    boardheader_t *bptr;
    xmlrpc_value  *ret, *t;

    xmlrpc_parse_value(env, param_array, "(i)", &bid);
    errorexit();

    if( bid != 0 )
	check_board_and_permission(bid);

    if( bid != 0 && !(bcache[bid].brdattr & BRD_GROUPBOARD) )
	errorreturn(PIERR_NOTCLASS);

    ret = xmlrpc_build_value(env, "()");
    for( bptr = bcache[bid].firstchild[0] ;
	 bptr != (boardheader_t*)~0      ;
	 bptr = bptr->next[0]              ){
	if( (bptr->brdattr & BRD_HIDE) ||
	    (bptr->level && !bptr->brdattr & BRD_POSTMASK) )
	    continue;
	t = xmlrpc_build_value(env, "{s:i,s:s,s:6,s:i,s:6,s:i,s:b}",
			       "bid",     (int32)(bptr - bcache),
			       "brdname", bptr->brdname,
			       "title",   bptr->title + 5, strlen(bptr->title) - 5,
			       "gid",     bptr->gid,
			       "BM",      bptr->BM, strlen(bptr->BM),
			       "nuser",   (int32)bptr->nuser,
			       "isclass", (xmlrpc_bool)
			                  (bptr->brdattr & BRD_GROUPBOARD) ? 1 : 0
			       );
	xmlrpc_array_append_item(env, ret, t);
    }
    return ret;
}

xmlrpc_value *
article_list(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
#define MAXGETARTICLES 20
    xmlrpc_value *ret, *t;
    fileheader_t art[MAXGETARTICLES];
    int32   bid, from, nArticles, fd, nGets, i;

    xmlrpc_parse_value(env, param_array, "(ii)", &bid, &from);
    errorexit();
    check_board_and_permission(bid);

    if( (nArticles = getfilesize(bid, ".DIR", &fd)) < 0 )
	errorreturn(PIERR_INT);
    nArticles /= sizeof(fileheader_t);
    
    if( from < 0 )
	from += nArticles;
    if( from < 0 || from > nArticles ){
	close(fd);
	errorreturn(PIERR_NOMORE);
    }

    nGets = (((from + MAXGETARTICLES) < nArticles) ?
	     MAXGETARTICLES : (nArticles - from));
    if( (nGets = read(fd, art, nGets * sizeof(fileheader_t))) < 0 ){
	close(fd);
	errorreturn(PIERR_INT);
    }
    close(fd);
    nGets /= sizeof(fileheader_t);

    ret = xmlrpc_build_value(env, "()");
    for( i = 0 ; i < nGets ; ++i ){
	t = xmlrpc_build_value(env, "{s:i,s:s,s:i,s:s,s:s,s:6}",
			       "articleid", (int32)(from + i),
			       "filename",  art[i].filename,
			       "recommend", (int32)art[i].recommend,
			       "owner",     art[i].owner,
			       "date",      art[i].date,
			       "title",     art[i].title, strlen(art[i].title)
			       );
	xmlrpc_array_append_item(env, ret, t);
    }
    return ret;
}

xmlrpc_value *
_article_readfn(xmlrpc_env *env, int bid, char *fn)
{
    char    *content;
    int     fd, size;
    xmlrpc_value *ret;
    if( (size = getfilesize(bid, fn, &fd)) < 0 )
	errorreturn(PIERR_INT);

    content = (char *)malloc(sizeof(char) * size);
    read(fd, content, size);
    close(fd);
    ret = xmlrpc_build_value(env, "{s:6}", "content", content, size);
    free(content);

    return ret;
}

xmlrpc_value *
article_readfn(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
    int     bid;
    char    *fn;

    xmlrpc_parse_value(env, param_array, "(is)", &bid, &fn);
    errorexit();

    if( fn == NULL || fn[0] != 'M' || fn[1] != '.' )
	errorreturn(PIERR_NOBRD);
    check_board_and_permission(bid);

    return _article_readfn(env, bid, fn);
}

int main(int argc, char **argv)
{
    attach_SHM();
    chdir(BBSHOME);
    xmlrpc_cgi_init(XMLRPC_CGI_NO_FLAGS);
    xmlrpc_cgi_add_method_w_doc("board.getBid", &getBid, NULL, "?",
				"get bid from brdname");
    xmlrpc_cgi_add_method_w_doc("board.getBrdInfo", &getBrdInfo, NULL, "?",
				"get board information");
    xmlrpc_cgi_add_method_w_doc("board.getNarticle", &getNarticle, NULL, "?",
				"get # articles in the board");

    xmlrpc_cgi_add_method_w_doc("class.list", &class_list, NULL, "?",
				"list (C)lass");

    xmlrpc_cgi_add_method_w_doc("article.list", &article_list, NULL, "?",
				"article list");
    xmlrpc_cgi_add_method_w_doc("article.readfn", &article_readfn, NULL, "?",
				"read the article by filename");

    xmlrpc_cgi_process_call();
    xmlrpc_cgi_cleanup();
    return 0;
}
