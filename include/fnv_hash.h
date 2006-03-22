#ifndef _FNV_HASH_H_
#define _FNV_HASH_H_
/*
 * Fowler / Noll / Vo Hash (FNV Hash)
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * This is an implementation of the algorithms posted above.
 * This file is placed in the public domain by Peter Wemm.
 *
 * $FreeBSD: src/sys/sys/fnv_hash.h,v 1.2 2001/03/20 02:10:18 peter Exp $
 */

typedef unsigned int Fnv32_t;
typedef unsigned long long Fnv64_t;

#define FNV1_32_INIT ((Fnv32_t) 33554467UL)
#define FNV1_64_INIT ((Fnv64_t) 0xcbf29ce484222325ULL)

#define FNV_32_PRIME ((Fnv32_t) 0x01000193UL)
#define FNV_64_PRIME ((Fnv64_t) 0x100000001b3ULL)

static __inline Fnv32_t
fnv_32_buf(const void *buf, size_t len, Fnv32_t hval)
{
	const unsigned char *s = (const unsigned char *)buf;

	while (len-- != 0) {
		hval *= FNV_32_PRIME;
		hval ^= *s++;
	}
	return hval;
}

static __inline Fnv32_t
fnv_32_str(const char *str, Fnv32_t hval)
{
	const unsigned char *s = (const unsigned char *)str;
	Fnv32_t c;

	while ((c = *s++) != 0) {
		hval *= FNV_32_PRIME;
		hval ^= c;
	}
	return hval;
}

static __inline Fnv32_t
fnv1a_32_str(const char *str, Fnv32_t hval)
{
	const unsigned char *s = (const unsigned char *)str;
	Fnv32_t c;

	while ((c = *s++) != 0) {
		hval ^= c;
		hval *= FNV_32_PRIME;
	}
	return hval;
}

static __inline Fnv32_t
fnv1a_32_strcase(const char *str, Fnv32_t hval)
{
	const unsigned char *s = (const unsigned char *)str;
	Fnv32_t c;

	while ((c = *s++) != 0) {
		hval ^= toupper(c);
		hval *= FNV_32_PRIME;
	}
	return hval;
}

static __inline Fnv64_t
fnv_64_buf(const void *buf, size_t len, Fnv64_t hval)
{
	const unsigned char *s = (const unsigned char *)buf;

	while (len-- != 0) {
		hval *= FNV_64_PRIME;
		hval ^= *s++;
	}
	return hval;
}

static __inline Fnv64_t
fnv_64_str(const char *str, Fnv64_t hval)
{
	const unsigned char *s = (const unsigned char *)str;
	Fnv64_t c;

	while ((c = *s++) != 0) {
		hval *= FNV_64_PRIME;
		hval ^= c;
	}
	return hval;
}

static __inline Fnv64_t
fnv1a_64_strcase(const char *str, Fnv64_t hval)
{
	const unsigned char *s = (const unsigned char *)str;
	Fnv64_t c;

	while ((c = *s++) != 0) {
		hval ^= toupper(c);
		hval *= FNV_64_PRIME;
	}
	return hval;
}

#define FNV1A_CHAR(c,hval) do { hval^=(unsigned char)c; hval*=FNV_32_PRIME; } while(0)
#endif
