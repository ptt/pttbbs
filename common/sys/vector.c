/* $Id$ */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "cmsys.h"

#define chartoupper(c)  ((c >= 'a' && c <= 'z') ? c+'A'-'a' : c)

void
Vector_init(struct Vector *self, const int size)
{
    assert(size != 0);
    self->size = size;
    self->length = 0;
    self->capacity = 0;
    self->base = NULL;
    self->constant = false;
}

void
Vector_init_const(struct Vector *self, char * base, const int length, const int size)
{
    assert(size != 0);
    self->size = size;
    self->length = length;
    self->capacity = 0;
    self->base = base;
    self->constant = true;
}

void
Vector_delete(struct Vector *self)
{
    self->length = 0;
    self->capacity = 0;
    if (self->base && !self->constant)
	free(self->base);
    self->base = NULL;
    self->constant = false;
}

void
Vector_clear(struct Vector *self, const int size)
{
    Vector_delete(self);
    Vector_init(self, size);
}

int
Vector_length(const struct Vector *self)
{
    return self->length;
}

void
Vector_resize(struct Vector *self, const int length)
{
    int capacity = length * self->size;

    assert(!self->constant);

#define MIN_CAPACITY 4096
    if (capacity == 0) {
	if (self->base)
	    free(self->base);
	self->base = NULL;
	self->capacity = 0;
    } else {
	int old_capacity = self->capacity;
	assert(capacity > 0);
	if (self->capacity == 0)
	    self->capacity = MIN_CAPACITY;
	//if (self->capacity > capacity && self->capacity > MIN_CAPACITY)
	//    self->capacity /= 2;
	while (self->capacity < capacity)
	    self->capacity *= 2;

	if (old_capacity != self->capacity || self->base == NULL) {
	    char *tmp = (char *)realloc(self->base, self->capacity);
	    assert(tmp);
	    self->base = tmp;
	}
    }
}

void
Vector_add(struct Vector *self, const char *name)
{
    assert(!self->constant);
    Vector_resize(self, self->length+1);
    strlcpy(self->base + self->size * self->length, name, self->size);
    self->length++;
}

const char*
Vector_get(const struct Vector *self, const int idx)
{
    assert(0 <= idx && idx < self->length);
    return self->base + self->size * idx;
}

int
Vector_MaxLen(const struct Vector *list, const int offset, const int count)
{
    int i, j;
    int maxlen = 0;

    for(i=offset, j=count; i<list->length && j > 0; i++, j--) {
	int len = strlen(list->base + list->size * i);
	if (len > maxlen)
	    maxlen = len;
    }
    assert(maxlen <= list->size);
    return maxlen;
}

int
Vector_match(const struct Vector *src, struct Vector *dst, const int key, const int pos)
{
    int uckey, lckey;
    int i;

    Vector_clear(dst, src->size);

    uckey = chartoupper(key);
    if (key >= 'A' && key <= 'Z')
	lckey = key | 0x20;
    else
	lckey = key;

    for (i=0; i<src->length; i++) {
	int ch = src->base[src->size * i + pos];
	if (ch == lckey || ch == uckey)
	    Vector_add(dst, src->base + src->size * i);
    }

    return dst->length;
}

void
Vector_sublist(const struct Vector *src, struct Vector *dst, const char *tag)
{
    int i;
    int len;
    Vector_clear(dst, src->size);

    len = strlen(tag);
    for (i=0; i<src->length; i++)
	if (len==0 || strncasecmp(src->base + src->size * i, tag, len)==0)
	    Vector_add(dst, src->base + src->size * i);
}

int
Vector_remove(struct Vector *self, const char *name)
{
    int i;
    assert(!self->constant);
    for (i=0; i<self->length; i++)
	if (strcasecmp(self->base + self->size * i, name) == 0) {
	    strlcpy(self->base + self->size * i,
		    self->base + self->size * (self->length-1), self->size);

	    self->length--;
	    Vector_resize(self, self->length);
	    return 1;
	}
    return 0;
}

int
Vector_search(const struct Vector *self, const char *name)
{
    int i;
    for (i=0; i<self->length; i++)
	if (strcasecmp(self->base + self->size * i, name) == 0)
	    return i;
    return -1;
}

