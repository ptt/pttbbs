#include "mod_ptt.h"
extern SHM_t   *SHM;
extern int     *GLOBALVAR;

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


static int xml_header(request_rec *r)
{
    r->content_type = "text/xml";
    ap_send_http_header(r);
    ap_rputs("<?xml version=\"1.0\" encoding=\"Big5\"?> \n", r);
    ap_rprintf(r, "<!--  HTTP-Server-version=\"%s\"\n",
	    ap_get_server_version());
    ap_rprintf(r,"   r-filename=\"%s\"\n",r->filename);
    ap_rprintf(r,"   r-request_time=\"%s\"\n",ctime(&r->request_time));
    ap_rprintf(r,"   r-method=\"%s\"\n",r->method);
    ap_rprintf(r,"   r-method_number=\"%d\"\n",r->method_number);
    ap_rprintf(r,"   r-path_info=\"%s\"\n",r->path_info);
    ap_rprintf(r,"   r-args=\"%s\"\n",r->args);
    ap_rprintf(r,"   r-unparsed_uri=\"%s\"\n",r->unparsed_uri);
    ap_rprintf(r,"   r-handler=\"%s\"\n",r->handler);
    ap_rprintf(r,"   r-content_type=\"%s\"\n",r->content_type);
    ap_rprintf(r, "  Serverbuilt=\"%s\" \n", ap_get_server_built());
    ap_rprintf(r, "  numboards=\"%d\" \n", numboards);
    ap_rprintf(r, "  shm=\"%d\" \n", SHM->loaded );
    ap_rprintf(r, "  max_user=\"%d\" -->", SHM->max_user );
}
static int userlist(request_rec *r)
{
 int i,offset=0;
 userinfo_t *ptr;
 xml_header(r);
 if (r->header_only) {
        return OK;
    }
 if(r->args) offset=atoi(r->args);
 if(offset<0 || offset>SHM->UTMPnumber)offset=0;

 ap_rprintf(r,"<userlist>");
 for(i=offset;i<SHM->UTMPnumber && i<50+offset;i++)
  {
    ptr= (userinfo_t *)SHM->sorted[SHM->currsorted][0][i];
    if(!ptr || ptr->userid[0]==0 || ptr->invisible ) continue;
    ap_rprintf(r,"<user>\n");
          ap_rprintf(r," <id>%d</id>\n",i+1);
          ap_rprintf(r," <total>%d</total>\n",SHM->UTMPnumber);
          ap_rprintf(r," <uid>%d</uid>\n",ptr->uid);
          ap_rprintf(r," <userid>%s</userid>\n",ptr->userid);
          ap_rprintf(r," <username>%s</username>\n",
                           ap_escape_html(r->pool,ptr->username));
          ap_rprintf(r," <from>%s</from>\n",ptr->from);
          ap_rprintf(r," <from_alias>%d</from_alias>\n",ptr->from_alias);
          ap_rprintf(r," <mailalert>%d</mailalert>\n",ptr->mailalert);
          ap_rprintf(r," <mind>%s</mind>\n",ap_escape_html(r->pool,ptr->mind));
    ap_rprintf(r,"</user>");
  }   
 ap_rprintf(r,"</userlist>");
}
static int showboard(request_rec *r, int id)
{
    int i;
    boardheader_t *bptr=NULL;
    id=id-1;
    ap_rprintf(r,"<brdlist>");
    bptr = (boardheader_t *)bcache[id].firstchild[0];
    for(; bptr!= (boardheader_t*)~0; )
       {
        if((bcache[id].brdattr&BRD_HIDE)|| 
         bcache[id].level&& !(bcache[id].brdattr & BRD_POSTMASK )) continue;
          ap_rprintf(r,"<brd>\n");
          i=(bptr-bcache);   
          ap_rprintf(r," <bid>%d</bid>",i+1);
          ap_rprintf(r," <brdname>%s</brdname>\n",bptr->brdname);
          ap_rprintf(r," <title>%s</title>\n",ap_escape_html(r->pool,bptr->title));
          ap_rprintf(r," <nuser>%d</nuser>\n",bptr->nuser);
          ap_rprintf(r," <gid>%d</gid>\n",bptr->gid);
          ap_rprintf(r," <childcount>%d</childcount>\n",bptr->childcount);
          ap_rprintf(r," <BM>%s</BM>\n",bptr->BM);
          ap_rprintf(r," <brdattr>%d</brdattr>\n",bptr->brdattr);
          ap_rprintf(r," <total>%d</total>\n",SHM->total[i]);
          ap_rprintf(r,"</brd>\n");
          bptr=(boardheader_t*)bptr->next[0];
       }

    ap_rprintf(r,"</brdlist>");
}


static int showpost(request_rec *r,int bid,int id, int num)
{
   int i;
   num=256;
   id=1;
   char path[512];
   fileheader_t headers[256];
   memset(headers,0, sizeof(fileheader_t)*256);
   sprintf(path,BBSHOME"/boards/%c/%s/.DIR",
            bcache[bid-1].brdname[0],bcache[bid-1].brdname); 
   get_records(path, headers, sizeof(fileheader_t)*256, id,num);

   ap_rprintf(r,"<postlist>");

   for(i=0;i<256;i++)
      {
          ap_rprintf(r,"<post>\n");
          ap_rprintf(r," <id>%d</id>",i+1);
          ap_rprintf(r," <filename>%s</filename>\n",headers[i].filename);
          ap_rprintf(r," <owner>%s</owner>\n",headers[i].owner);
          ap_rprintf(r," <date>%s</date>\n",headers[i].date);
          ap_rprintf(r," <title>%s</title>\n", 
                         ap_escape_html(r->pool,headers[i].title));
          ap_rprintf(r," <money>%d</money>\n",headers[i].money);
          ap_rprintf(r," <filemode>%c</filemode>\n",headers[i].filemode);
          ap_rprintf(r," <recommend>%d</recommend>\n",headers[i].recommend);
          ap_rprintf(r,"</post>\n");
      } 
   ap_rprintf(r,"</postlist>");
}
static int showmenujs(request_rec *r)
{
    int i;
    boardheader_t *bptr;
    r->content_type = "text/text";
    ap_send_http_header(r);
    ap_rputs("d=new dTree('d');\n",r);
    ap_rputs("d.add(0,-1,'Class','');\n",r);
    for(i=1;i<=numboards;i++)
     {
      bptr=&bcache[i-1];
      if(!isalpha(bptr->brdname[0]))continue;

      ap_rprintf(r,"d.add(%d,%d,\"%s %s..\",'/boards?%s');\n",
            i,bptr->gid-1,
            bptr->gid==1?"":bptr->brdname,
             ap_escape_quotes(r->pool,
            ap_escape_html(r->pool,bptr->title+7)),i);
     }
   ap_rputs("d.draw()\n",r);
   return OK;
}

static int showxml(request_rec *r)
{
    int bid=1;
    xml_header(r);
    if (r->header_only) {
        return OK;
    }
    if(r->args) bid=atoi(r->args);
    if(bid<1 || bid>numboards)bid=1;

    if(
        !(bcache[bid-1].brdattr&BRD_HIDE)&& 
         !(bcache[bid-1].level&&!(bcache[bid-1].brdattr & BRD_POSTMASK)))
        if( bid==1||bcache[bid-1].brdattr&BRD_GROUPBOARD)
         showboard(r,bid);
        else
         showpost(r,bid,0,0);
   return OK;
}
static int ptt_handler(request_rec *r)
{
    excfg *dcfg;

    dcfg = our_dconfig(r);

    ap_soft_timeout("send ptt call trace", r);

    if(!strncmp(r->unparsed_uri,"/menu",5))
         showmenujs(r);
    else if(!strncmp(r->unparsed_uri,"/userlist",9))
         userlist(r);
    else 
        showxml(r);

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
