#include "mod_ptt.h"

extern int numboards;
extern boardheader_t *bcache;
extern int ptt_handler(request_rec *r, void *args);

int bbs_board(request_rec *r, void *args)
{
    int i;
    r->content_type = "text/xml";
    ap_send_http_header(r);
    ap_rputs("<hr> Ptt \n", r);
return ptt_handler (r,args);

    ap_rprintf(r,"r->filename : %s <br>",r->filename);
    ap_rprintf(r,"r->request_time : %s <br>",ctime(&r->request_time));
    ap_rprintf(r,"r->method : %s <br>",r->method);
    ap_rprintf(r,"r->method_number : %d <br>",r->method_number);
    ap_rprintf(r,"r->path_info : %s <br>",r->path_info);
    ap_rprintf(r,"r->args : %s <br>",r->args);
    ap_rprintf(r,"r->unparsed_uri : %s <br>",r->unparsed_uri);
    ap_rprintf(r,"r->uri : %s <br>",r->uri);
    ap_rprintf(r,"r->handler : %s <br>",r->handler);
    ap_rprintf(r,"r->content_type : %s <br>",r->content_type);
    ap_rprintf(r, "Server built: \"%s\"\n", ap_get_server_built());

    ap_rputs("<hr> Ptt \n", r);

    ap_rputs("<XML>",r);
/*
    for(i = 0; i++ < numboards; i++)
      {
         ap_rputs("<board>",r);
         ap_rputs("</board>",r);
      }
*/
 
   return OK;
}
