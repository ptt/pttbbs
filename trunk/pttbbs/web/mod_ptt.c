#include "mod_ptt.h"
extern int numboards;
extern boardheader_t *bcache;
typedef struct excfg {
    int cmode;                  /* Environment to which record applies (directory,
                                 * server, or combination).
                                 */
#define CONFIG_MODE_SERVER 1
#define CONFIG_MODE_DIRECTORY 2
#define CONFIG_MODE_COMBO 3     /* Shouldn't ever happen. */
    int local;                  /* Boolean: "Example" directive declared here? */
    int congenital;             /* Boolean: did we inherit an "Example"? */
    char *trace;                /* Pointer to trace string. */
    char *loc;                  /* Location to which this record applies. */
} excfg;

static const char *trace = NULL;
static table *static_calls_made = NULL;

static pool *ptt_pool = NULL;
static pool *ptt_subpool = NULL;

module MODULE_VAR_EXPORT ptt_module;
excfg * our_dconfig(request_rec *r)
{

    return (excfg *) ap_get_module_config(r->per_dir_config, &ptt_module);
}

static void setup_module_cells()
{
    /*
     * If we haven't already allocated our module-private pool, do so now.
     */
    if (ptt_pool == NULL) {
        ptt_pool = ap_make_sub_pool(NULL);
    };
    /*
     * Likewise for the table of routine/environment pairs we visit outside of
     * request context.
     */
    if (static_calls_made == NULL) {
        static_calls_made = ap_make_table(ptt_pool, 16);
    };
}



static int ptt_handler(request_rec *r)
{
    int i;
    excfg *dcfg;

    dcfg = our_dconfig(r);
    //resolve_utmp();
    //resolve_boards();
   // resolve_garbage();
  //  resolve_fcache();

    r->content_type = "text/html";

    ap_soft_timeout("send ptt call trace", r);
    ap_send_http_header(r);
#ifdef CHARSET_EBCDIC
    /* Server-generated response, converted */
    ap_bsetflag(r->connection->client, B_EBCDIC2ASCII, r->ebcdic.conv_out = 1);
#endif

    /*
     * If we're only supposed to send header information (HEAD request), we're
     * already there.
     */
    if (r->header_only) {
        ap_kill_timeout(r);
        return OK;
    }

    /*
     * Now send our actual output.  Since we tagged this as being
     * "text/html", we need to embed any HTML.
     */
    ap_rputs("  ptt3 <P>\n", r);

    ap_rprintf(r, "  Apache HTTP Server version: \"%s\"\n",
	    ap_get_server_version());
    ap_rprintf(r,"r->filename : %s <br>",r->filename);
    ap_rprintf(r,"r->request_time : %s <br>",ctime(&r->request_time));
    ap_rprintf(r,"r->method : %s <br>",r->method);
    ap_rprintf(r,"r->method_number : %d <br>",r->method_number);
    ap_rprintf(r,"r->path_info : %s <br>",r->path_info);
    ap_rprintf(r,"r->args : %s <br>",r->args);
    ap_rprintf(r,"r->unparsed_uri : %s <br>",r->unparsed_uri);
    ap_rprintf(r,"r->handler : %s <br>",r->handler);
    ap_rprintf(r,"r->content_type : %s <br>",r->content_type);
    ap_rprintf(r, "  Server built: \"%s\"\n", ap_get_server_built());

    for(i = 0; i++ < numboards; i++)
         ap_rprintf(r,"%s %s<br>",bcache[i].brdname,bcache[i].title);
    /*
     * We're all done, so cancel the timeout we set.  Since this is probably
     * the end of the request we *could* assume this would be done during
     * post-processing - but it's possible that another handler might be
     * called and inherit our outstanding timer.  Not good; to each its own.
     */
    ap_kill_timeout(r);
    /*
     * We did what we wanted to do, so tell the rest of the server we
     * succeeded.
     */
    return OK;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/* Now let's declare routines for each of the callback phase in order.      */
/* (That's the order in which they're listed in the callback list, *not     */
/* the order in which the server calls them!  See the command_rec           */
/* declaration near the bottom of this file.)  Note that these may be       */
/* called for situations that don't relate primarily to our function - in   */
/* other words, the fixup handler shouldn't assume that the request has     */
/* to do with "example" stuff.                                              */
/*                                                                          */
/* With the exception of the content handler, all of our routines will be   */
/* called for each request, unless an earlier handler from another module   */
/* aborted the sequence.                                                    */
/*                                                                          */
/* Handlers that are declared as "int" can return the following:            */
/*                                                                          */
/*  OK          Handler accepted the request and did its thing with it.     */
/*  DECLINED    Handler took no action.                                     */
/*  HTTP_mumble Handler looked at request and found it wanting.             */
/*                                                                          */
/* What the server does after calling a module handler depends upon the     */
/* handler's return value.  In all cases, if the handler returns            */
/* DECLINED, the server will continue to the next module with an handler    */
/* for the current phase.  However, if the handler return a non-OK,         */
/* non-DECLINED status, the server aborts the request right there.  If      */
/* the handler returns OK, the server's next action is phase-specific;      */
/* see the individual handler comments below for details.                   */
/*                                                                          */
/*--------------------------------------------------------------------------*/
/* 
 * This function is called during server initialisation.  Any information
 * that needs to be recorded must be in static cells, since there's no
 * configuration record.
 *
 * There is no return value.
 */

static void ptt_child_init(server_rec *s, pool *p)
{

    char *note;
    char *sname = s->server_hostname;

    //resolve_utmp();
    //resolve_boards();
    //resolve_garbage();
    //resolve_fcache();
    /*
     * Set up any module cells that ought to be initialised.
     */
    setup_module_cells();
    /*
     * The arbitrary text we add to our trace entry indicates for which server
     * we're being called.
     */
    sname = (sname != NULL) ? sname : "";
    note = ap_pstrcat(p, "ptt_child_init(", sname, ")", NULL);
}

static void ptt_child_exit(server_rec *s, pool *p)
{

    char *note;
    char *sname = s->server_hostname;

    /*
     * The arbitrary text we add to our trace entry indicates for which server
     * we're being called.
     */
    sname = (sname != NULL) ? sname : "";
    note = ap_pstrcat(p, "ptt_child_exit(", sname, ")", NULL);
}

static const handler_rec ptt_handlers[] =
{
    {"ptt_h", ptt_handler},
    {NULL}
};

module MODULE_VAR_EXPORT ptt_module =
{
    STANDARD_MODULE_STUFF,
    NULL,               /* module initializer */
    NULL,  /* per-directory config creator */
    NULL,   /* dir config merger */
    NULL,       /* server config creator */
    NULL,        /* server config merger */
    NULL,               /* command table */
    ptt_handlers,           /* [9] list of handlers */
    NULL,  /* [2] filename-to-URI translation */
    NULL,      /* [5] check/validate user_id */
    NULL,       /* [6] check user_id is valid *here* */
    NULL,     /* [4] check access by host address */
    NULL,       /* [7] MIME type checker/setter */
    NULL,        /* [8] fixups */
    NULL,             /* [10] logger */
#if MODULE_MAGIC_NUMBER >= 19970103
    NULL,      /* [3] header parser */
#endif
#if MODULE_MAGIC_NUMBER >= 19970719
    ptt_child_init,         /* process initializer */
#endif
#if MODULE_MAGIC_NUMBER >= 19970728
    ptt_child_exit,         /* process exit/cleanup */
#endif
#if MODULE_MAGIC_NUMBER >= 19970902
    NULL
#endif
};
