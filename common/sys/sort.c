
int cmp_int(const void *a, const void *b)
{
    return *(int*)a - *(int*)b;
}

int cmp_int_desc(const void * a, const void * b)
{
    return cmp_int(b, a);
}
