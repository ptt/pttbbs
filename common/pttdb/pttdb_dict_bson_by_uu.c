#include "cmpttdb/pttdb_dict_bson_by_uu.h"
#include "cmpttdb/pttdb_dict_bson_by_uu_private.h"

Err
init_dict_bson_by_uu(DictBsonByUU *dict_bson_by_uu, int n_dict)
{
    dict_bson_by_uu->n_dict = n_dict;
    dict_bson_by_uu->dicts = malloc(sizeof(_DictBsonByUU *) * n_dict);
    if(!dict_bson_by_uu->dicts) {
        dict_bson_by_uu->n_dict = 0;
        return S_ERR;
    }
    bzero(dict_bson_by_uu->dicts, sizeof(_DictBsonByUU *) * n_dict);

    return S_OK;
}

Err 
add_to_dict_bson_by_uu(UUID uuid, bson_t *b, DictBsonByUU *dict_bson_by_uu)
{    
    if(!dict_bson_by_uu->n_dict) return S_ERR;

    int n_dict = dict_bson_by_uu->n_dict;
    int idx = uuid[0] % n_dict;
    _DictBsonByUU *p_dict = dict_bson_by_uu->dicts[idx];

    if(!p_dict) {
        dict_bson_by_uu->dicts[idx] = malloc(sizeof(_DictBsonByUU));
        if(!dict_bson_by_uu->dicts[idx]) return S_ERR;

        p_dict = dict_bson_by_uu->dicts[idx];
        p_dict->next = NULL;
        memcpy(p_dict->uuid, uuid, UUIDLEN);
        p_dict->b = b;
    }
    else {
        while(p_dict->next) p_dict = p_dict->next;

        p_dict->next = malloc(sizeof(_DictBsonByUU));        
        if(!p_dict->next) return S_ERR;

        p_dict = p_dict->next;
        p_dict->next = NULL;
        memcpy(p_dict->uuid, uuid, UUIDLEN);
        p_dict->b = b;
    }

    return S_OK;
}

Err
get_bson_from_dict_bson_by_uu(DictBsonByUU *dict_bson_by_uu, UUID uuid, bson_t **b)
{
    if(!dict_bson_by_uu->n_dict) return S_ERR;

    int idx = uuid[0] % dict_bson_by_uu->n_dict;
    _DictBsonByUU *p_dict = dict_bson_by_uu->dicts[idx];

    if(!p_dict) {
        return S_ERR_NOT_EXISTS;
    }

    for(; p_dict; p_dict = p_dict->next) {
        if(!strncmp((char *)uuid, (char *)p_dict->uuid, UUIDLEN)) {
            *b = p_dict->b;
            return S_OK;
        }
    }

    return S_ERR_NOT_EXISTS;

}

Err
bsons_to_dict_bson_by_uu(bson_t **b, int n_b, char *key, DictBsonByUU *dict_bson_by_uu)
{
    Err error_code = S_OK;

    int n_dict = n_b < MAX_N_DICT_BSON_BY_UU ? n_b : MAX_N_DICT_BSON_BY_UU;

    init_dict_bson_by_uu(dict_bson_by_uu, n_dict);

    bson_t **p_b = b;
    UUID uuid = {};
    int len = 0;
    for(int i = 0; i < n_b; i++, p_b++) {
        error_code = bson_get_value_bin(*p_b, key, UUIDLEN, (char *)uuid, &len);
        if(error_code) break;

        error_code = add_to_dict_bson_by_uu(uuid, *p_b, dict_bson_by_uu);
        if(error_code) break;
    }

    return error_code;
}

Err
safe_destroy_dict_bson_by_uu(DictBsonByUU *dict_bson_by_uu)
{
    Err error_code;
    int n_dict = dict_bson_by_uu->n_dict;
    _DictBsonByUU **p_dict = dict_bson_by_uu->dicts;
    for(int i = 0; i < n_dict; i++, p_dict++) {
        error_code = _safe_destroy_dict_bson_by_uu_core(*p_dict);
    }
    safe_free((void **)&dict_bson_by_uu->dicts);
    dict_bson_by_uu->n_dict = 0;

    return S_OK;
}


Err
_safe_destroy_dict_bson_by_uu_core(_DictBsonByUU *dict_bson_by_uu)
{
    if(!dict_bson_by_uu) return S_OK;

    _DictBsonByUU *pre_p = NULL;
    _DictBsonByUU *current_p = NULL;
    for(current_p = dict_bson_by_uu->next, pre_p = dict_bson_by_uu; current_p != NULL; pre_p = current_p, current_p = current_p->next) {
        free(pre_p);        
    }
    free(pre_p);
    return S_OK;
}

Err
display_dict_bson_by_uu(DictBsonByUU *dict_bson_by_uu, char *prompt)
{
    int n_dict = dict_bson_by_uu->n_dict;
    int j = 0;
    _DictBsonByUU *p_dict = NULL;
    char *str = NULL;
    for(int i = 0; i < n_dict; i++) {
        for(p_dict = dict_bson_by_uu->dicts[i], j = 0; p_dict; p_dict = p_dict->next, j++) {
            str = bson_as_canonical_extended_json(p_dict->b, NULL);
            fprintf(stderr, "%s (%d/%d): %d: bson: %s\n", prompt, i, n_dict, j, str);
            bson_free(str);
        }
    }

    return S_OK;
}