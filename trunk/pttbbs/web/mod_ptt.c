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
    if (ptt_pool == NULL) {
        ptt_pool = ap_make_sub_pool(NULL);
    };
    if (static_calls_made == NULL) {
        static_calls_made = ap_make_table(ptt_pool, 16);
    };
}



static int ptt_handler(request_rec *r)
{
    int i;
    excfg *dcfg;


    dcfg = our_dconfig(r);

    r->content_type = "text/html";
    ap_soft_timeout("send ptt call trace", r);
    ap_send_http_header(r);

    if (r->header_only) {
        ap_kill_timeout(r);
        return OK;
    }

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


    ap_rprintf(r, "  Server built: \"%s\"<br>", ap_get_server_built());
    ap_rprintf(r, "  numboards: \"%d\"<br>", numboards);

    for(i = 0; i < 10; i++)
          ap_rprintf(r,"%d. %s %s<br>",i,bcache[i].brdname,bcache[i].title);

    //  for(i = 0; i < 10 /*numboards*/; i++)
    //      ap_rprintf(r,"%s %s<br>",bcache[i].brdname,bcache[i].title);
    ap_kill_timeout(r);
    return OK;
}

/*  OK          Handler accepted the request and did its thing with it.     */
/*  DECLINED    Handler took no action.                                     */
/*  HTTP_mumble Handler looked at request and found it wanting.             */

static void ptt_child_init(server_rec *s, pool *p)
{

    char *note;
    char *sname = s->server_hostname;
    attach_SHM();


    setup_module_cells();
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
