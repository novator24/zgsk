#include "../gskdebug.h"
#include "gskhash.h"
#include "../gskghelpers.h"
#include "../gskmacros.h"
#include <string.h>

/* --- macros which we might later expose for efficiency --- */
#define GSK_HASH_FEED(hash, data, len)			\
	G_STMT_START{					\
	  (*(hash)->feed) (hash, data, len);		\
	}G_STMT_END
#define GSK_HASH_DONE(hash)				\
	G_STMT_START{					\
	  g_assert (((hash)->flags & 1) == 0);		\
	  (hash)->hash_value = (*(hash)->done) (hash);	\
	  (hash)->flags = 1;				\
	}G_STMT_END
#define GSK_HASH_DESTROY(hash)				\
	G_STMT_START{					\
	  (*(hash)->destroy) (hash);			\
	}G_STMT_END
#define GSK_HASH_GET_SIZE(hash)				\
	  ((hash)->size)

/**
 * gsk_hash_feed:
 * @hash: the hash to feed data.
 * @data: binary data to accumulate in the hash.
 * @length: length of the binary data.
 *
 * Affect the hash incrementally;
 * hash the given binary data.
 * 
 * You may call this function on little bits of data
 * and it must have exactly the same effect
 * is if you called it once with a larger
 * slab of data.
 */
void
gsk_hash_feed       (GskHash        *hash,
		     gconstpointer   data,
		     guint           length)
{
  GSK_HASH_FEED (hash, data, length);
}

/**
 * gsk_hash_feed_str:
 * @hash: the hash to feed data.
 * @str: a NUL-terminated string to feed to the hash.
 *
 * Hash the given binary data (incrementally).
 *
 * You may mix calls to gsk_hash_feed() and gsk_hash_feed_str().
 */
void
gsk_hash_feed_str   (GskHash        *hash,
		     const char     *str)
{
  GSK_HASH_FEED (hash, str, strlen (str));
}

/**
 * gsk_hash_done:
 * @hash: the hash to finish.
 *
 * Finish processing loose data for the hash.
 * This may only be called once in
 * the lifetime of the hash.
 */
void
gsk_hash_done       (GskHash        *hash)
{
  g_return_if_fail ((hash->flags & 1) == 0);
  GSK_HASH_DONE (hash);
}

/**
 * gsk_hash_get_size:
 * @hash: the hash to query.
 *
 * Get the number of binary bytes that this
 * function maps to.
 * 
 * returns: the number of bytes of binary data in this hash.
 */
guint
gsk_hash_get_size   (GskHash        *hash)
{
  return GSK_HASH_GET_SIZE (hash);
}

/**
 * gsk_hash_get:
 * @hash: the hash to query.
 * @data_out: binary buffer to fill with the hash value.
 *
 * Get a binary hash value.  This should be of the
 * size returned by gsk_hash_get_size().
 */
void
gsk_hash_get       (GskHash        *hash,
		    guint8         *data_out)
{
  g_return_if_fail ((hash->flags & 1) == 1);
  memcpy (data_out, hash->hash_value, hash->size);
}

/**
 * gsk_hash_get_hex:
 * @hash: the hash to query.
 * @hex_out: buffer to fill with a NUL-terminated hex hash value.
 *
 * Get a hex hash value.  This should be of the
 * size returned by (gsk_hash_get_size() * 2 + 1).
 */
void
gsk_hash_get_hex   (GskHash        *hash,
		    gchar          *hex_out)
{
  static const char hex_digits[] = "0123456789abcdef";
  guint i;
  g_return_if_fail ((hash->flags & 1) == 1);
  for (i = 0; i < hash->size; i++)
    {
      guint8 h = ((guint8 *) hash->hash_value)[i];
      *hex_out++ = hex_digits[h >> 4];
      *hex_out++ = hex_digits[h & 15];
    }
  *hex_out = 0;
}

/**
 * gsk_hash_destroy:
 * @hash: the hash function.
 *
 * Free memory used by the hash object.
 */
void
gsk_hash_destroy    (GskHash        *hash)
{
  GSK_HASH_DESTROY (hash);
}

/*
 *  __  __ ____  ____
 * |  \/  |  _ \| ___|
 * | |\/| | | | |___ \
 * | |  | | |_| |___) |
 * |_|  |_|____/|____/
 * 
 */

/*	FreeBSD: src/sys/crypto/md5.h,v 1.1.2.1 2000/07/15 07:14:18 kris Exp 	*/
/*	KAME: md5.h,v 1.4 2000/03/27 04:36:22 sumikawa Exp 	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define MD5_BUFLEN	64

typedef struct {
	union {
		guint32	md5_state32[4];
		guint8	md5_state8[16];
	} md5_st;

#define md5_sta		md5_st.md5_state32[0]
#define md5_stb		md5_st.md5_state32[1]
#define md5_stc		md5_st.md5_state32[2]
#define md5_std		md5_st.md5_state32[3]
#define md5_st8		md5_st.md5_state8

	union {
		guint64	md5_count64;
		guint8	md5_count8[8];
	} md5_count;
#define md5_n	md5_count.md5_count64
#define md5_n8	md5_count.md5_count8

	guint	md5_i;
	guint8	md5_buf[MD5_BUFLEN];
} md5_ctxt;

static void md5_init (md5_ctxt *);
static void md5_loop (md5_ctxt *, guint8 *, guint);
static void md5_pad (md5_ctxt *);
static void md5_result (guint8 *, md5_ctxt *);

/* compatibility */
#define MD5_CTX		md5_ctxt
#define MD5Init(x)	md5_init((x))
#define MD5Update(x, y, z)	md5_loop((x), (y), (z))
#define MD5Final(x, y) \
do {				\
	md5_pad((y));		\
	md5_result((x), (y));	\
} while (0)

/*	FreeBSD: src/sys/crypto/md5.c,v 1.1.2.2 2001/07/03 11:01:27 ume Exp 	*/
/*	KAME: md5.c,v 1.5 2000/11/08 06:13:08 itojun Exp 	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#ifdef HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#endif
#include <sys/time.h>

#define SHIFT(X, s) (((X) << (s)) | ((X) >> (32 - (s))))

#define F(X, Y, Z) (((X) & (Y)) | ((~X) & (Z)))
#define G(X, Y, Z) (((X) & (Z)) | ((Y) & (~Z)))
#define H(X, Y, Z) ((X) ^ (Y) ^ (Z))
#define I(X, Y, Z) ((Y) ^ ((X) | (~Z)))

#define ROUND1(a, b, c, d, k, s, i) { \
	(a) = (a) + F((b), (c), (d)) + X[(k)] + T[(i)]; \
	(a) = SHIFT((a), (s)); \
	(a) = (b) + (a); \
}

#define ROUND2(a, b, c, d, k, s, i) { \
	(a) = (a) + G((b), (c), (d)) + X[(k)] + T[(i)]; \
	(a) = SHIFT((a), (s)); \
	(a) = (b) + (a); \
}

#define ROUND3(a, b, c, d, k, s, i) { \
	(a) = (a) + H((b), (c), (d)) + X[(k)] + T[(i)]; \
	(a) = SHIFT((a), (s)); \
	(a) = (b) + (a); \
}

#define ROUND4(a, b, c, d, k, s, i) { \
	(a) = (a) + I((b), (c), (d)) + X[(k)] + T[(i)]; \
	(a) = SHIFT((a), (s)); \
	(a) = (b) + (a); \
}

#define Sa	 7
#define Sb	12
#define Sc	17
#define Sd	22

#define Se	 5
#define Sf	 9
#define Sg	14
#define Sh	20

#define Si	 4
#define Sj	11
#define Sk	16
#define Sl	23

#define Sm	 6
#define Sn	10
#define So	15
#define Sp	21

#define MD5_A0	0x67452301
#define MD5_B0	0xefcdab89
#define MD5_C0	0x98badcfe
#define MD5_D0	0x10325476

/* Integer part of 4294967296 times abs(sin(i)), where i is in radians. */
static const guint32 T[65] = {
	0,
	0xd76aa478, 	0xe8c7b756,	0x242070db,	0xc1bdceee,
	0xf57c0faf,	0x4787c62a, 	0xa8304613,	0xfd469501,
	0x698098d8,	0x8b44f7af,	0xffff5bb1,	0x895cd7be,
	0x6b901122, 	0xfd987193, 	0xa679438e,	0x49b40821,

	0xf61e2562,	0xc040b340, 	0x265e5a51, 	0xe9b6c7aa,
	0xd62f105d,	0x2441453,	0xd8a1e681,	0xe7d3fbc8,
	0x21e1cde6,	0xc33707d6, 	0xf4d50d87, 	0x455a14ed,
	0xa9e3e905,	0xfcefa3f8, 	0x676f02d9, 	0x8d2a4c8a,

	0xfffa3942,	0x8771f681, 	0x6d9d6122, 	0xfde5380c,
	0xa4beea44, 	0x4bdecfa9, 	0xf6bb4b60, 	0xbebfbc70,
	0x289b7ec6, 	0xeaa127fa, 	0xd4ef3085,	0x4881d05,
	0xd9d4d039, 	0xe6db99e5, 	0x1fa27cf8, 	0xc4ac5665,

	0xf4292244, 	0x432aff97, 	0xab9423a7, 	0xfc93a039,
	0x655b59c3, 	0x8f0ccc92, 	0xffeff47d, 	0x85845dd1,
	0x6fa87e4f, 	0xfe2ce6e0, 	0xa3014314, 	0x4e0811a1,
	0xf7537e82, 	0xbd3af235, 	0x2ad7d2bb, 	0xeb86d391,
};

static const guint8 md5_paddat[MD5_BUFLEN] = {
	0x80,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,	
};

static void md5_calc (guint8 *, md5_ctxt *);

static inline void md5_init(ctxt)
	md5_ctxt *ctxt;
{
	ctxt->md5_n = 0;
	ctxt->md5_i = 0;
	ctxt->md5_sta = MD5_A0;
	ctxt->md5_stb = MD5_B0;
	ctxt->md5_stc = MD5_C0;
	ctxt->md5_std = MD5_D0;
	bzero(ctxt->md5_buf, sizeof(ctxt->md5_buf));
}

static inline void md5_loop(ctxt, input, len)
	md5_ctxt *ctxt;
	guint8 *input;
	u_int len; /* number of bytes */
{
	u_int gap, i;

	ctxt->md5_n += len * 8; /* byte to bit */
	gap = MD5_BUFLEN - ctxt->md5_i;

	if (len >= gap) {
		bcopy((void *)input, (void *)(ctxt->md5_buf + ctxt->md5_i),
			gap);
		md5_calc(ctxt->md5_buf, ctxt);

		for (i = gap; i + MD5_BUFLEN <= len; i += MD5_BUFLEN) {
			md5_calc((guint8 *)(input + i), ctxt);
		}
		
		ctxt->md5_i = len - i;
		bcopy((void *)(input + i), (void *)ctxt->md5_buf, ctxt->md5_i);
	} else {
		bcopy((void *)input, (void *)(ctxt->md5_buf + ctxt->md5_i),
			len);
		ctxt->md5_i += len;
	}
}

static void md5_pad(ctxt)
	md5_ctxt *ctxt;
{
	u_int gap;

	/* Don't count up padding. Keep md5_n. */	
	gap = MD5_BUFLEN - ctxt->md5_i;
	if (gap > 8) {
		bcopy((void *)md5_paddat,
		      (void *)(ctxt->md5_buf + ctxt->md5_i),
		      gap - sizeof(ctxt->md5_n));
	} else {
		/* including gap == 8 */
		bcopy((void *)md5_paddat, (void *)(ctxt->md5_buf + ctxt->md5_i),
			gap);
		md5_calc(ctxt->md5_buf, ctxt);
		bcopy((void *)(md5_paddat + gap),
		      (void *)ctxt->md5_buf,
		      MD5_BUFLEN - sizeof(ctxt->md5_n));
	}

	/* 8 byte word */	
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
	bcopy(&ctxt->md5_n8[0], &ctxt->md5_buf[56], 8);
#elif (G_BYTE_ORDER == G_BIG_ENDIAN)
	ctxt->md5_buf[56] = ctxt->md5_n8[7];
	ctxt->md5_buf[57] = ctxt->md5_n8[6];
	ctxt->md5_buf[58] = ctxt->md5_n8[5];
	ctxt->md5_buf[59] = ctxt->md5_n8[4];
	ctxt->md5_buf[60] = ctxt->md5_n8[3];
	ctxt->md5_buf[61] = ctxt->md5_n8[2];
	ctxt->md5_buf[62] = ctxt->md5_n8[1];
	ctxt->md5_buf[63] = ctxt->md5_n8[0];
#else
	must be either BIG or LITTLE endian
#endif

	md5_calc(ctxt->md5_buf, ctxt);
}

static void md5_result(digest, ctxt)
	guint8 *digest;
	md5_ctxt *ctxt;
{
	/* 4 byte words */
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
	bcopy(&ctxt->md5_st8[0], digest, 16);
#endif
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
	digest[ 0] = ctxt->md5_st8[ 3]; digest[ 1] = ctxt->md5_st8[ 2];
	digest[ 2] = ctxt->md5_st8[ 1]; digest[ 3] = ctxt->md5_st8[ 0];
	digest[ 4] = ctxt->md5_st8[ 7]; digest[ 5] = ctxt->md5_st8[ 6];
	digest[ 6] = ctxt->md5_st8[ 5]; digest[ 7] = ctxt->md5_st8[ 4];
	digest[ 8] = ctxt->md5_st8[11]; digest[ 9] = ctxt->md5_st8[10];
	digest[10] = ctxt->md5_st8[ 9]; digest[11] = ctxt->md5_st8[ 8];
	digest[12] = ctxt->md5_st8[15]; digest[13] = ctxt->md5_st8[14];
	digest[14] = ctxt->md5_st8[13]; digest[15] = ctxt->md5_st8[12];
#endif
}

static void md5_calc(b64, ctxt)
	guint8 *b64;
	md5_ctxt *ctxt;
{
	guint32 A = ctxt->md5_sta;
	guint32 B = ctxt->md5_stb;
	guint32 C = ctxt->md5_stc;
	guint32 D = ctxt->md5_std;
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
	guint32 *X = (guint32 *)b64;
#elif (G_BYTE_ORDER == G_BIG_ENDIAN)
	/* 4 byte words */
	/* what a brute force but fast! */
	guint32 X[16];
	guint8 *y = (guint8 *)X;
	y[ 0] = b64[ 3]; y[ 1] = b64[ 2]; y[ 2] = b64[ 1]; y[ 3] = b64[ 0];
	y[ 4] = b64[ 7]; y[ 5] = b64[ 6]; y[ 6] = b64[ 5]; y[ 7] = b64[ 4];
	y[ 8] = b64[11]; y[ 9] = b64[10]; y[10] = b64[ 9]; y[11] = b64[ 8];
	y[12] = b64[15]; y[13] = b64[14]; y[14] = b64[13]; y[15] = b64[12];
	y[16] = b64[19]; y[17] = b64[18]; y[18] = b64[17]; y[19] = b64[16];
	y[20] = b64[23]; y[21] = b64[22]; y[22] = b64[21]; y[23] = b64[20];
	y[24] = b64[27]; y[25] = b64[26]; y[26] = b64[25]; y[27] = b64[24];
	y[28] = b64[31]; y[29] = b64[30]; y[30] = b64[29]; y[31] = b64[28];
	y[32] = b64[35]; y[33] = b64[34]; y[34] = b64[33]; y[35] = b64[32];
	y[36] = b64[39]; y[37] = b64[38]; y[38] = b64[37]; y[39] = b64[36];
	y[40] = b64[43]; y[41] = b64[42]; y[42] = b64[41]; y[43] = b64[40];
	y[44] = b64[47]; y[45] = b64[46]; y[46] = b64[45]; y[47] = b64[44];
	y[48] = b64[51]; y[49] = b64[50]; y[50] = b64[49]; y[51] = b64[48];
	y[52] = b64[55]; y[53] = b64[54]; y[54] = b64[53]; y[55] = b64[52];
	y[56] = b64[59]; y[57] = b64[58]; y[58] = b64[57]; y[59] = b64[56];
	y[60] = b64[63]; y[61] = b64[62]; y[62] = b64[61]; y[63] = b64[60];
#else
	Must be either BIG or LITTLE endian.
#endif

	ROUND1(A, B, C, D,  0, Sa,  1); ROUND1(D, A, B, C,  1, Sb,  2);
	ROUND1(C, D, A, B,  2, Sc,  3); ROUND1(B, C, D, A,  3, Sd,  4);
	ROUND1(A, B, C, D,  4, Sa,  5); ROUND1(D, A, B, C,  5, Sb,  6);
	ROUND1(C, D, A, B,  6, Sc,  7); ROUND1(B, C, D, A,  7, Sd,  8);
	ROUND1(A, B, C, D,  8, Sa,  9); ROUND1(D, A, B, C,  9, Sb, 10);
	ROUND1(C, D, A, B, 10, Sc, 11); ROUND1(B, C, D, A, 11, Sd, 12);
	ROUND1(A, B, C, D, 12, Sa, 13); ROUND1(D, A, B, C, 13, Sb, 14);
	ROUND1(C, D, A, B, 14, Sc, 15); ROUND1(B, C, D, A, 15, Sd, 16);
	
	ROUND2(A, B, C, D,  1, Se, 17); ROUND2(D, A, B, C,  6, Sf, 18);
	ROUND2(C, D, A, B, 11, Sg, 19); ROUND2(B, C, D, A,  0, Sh, 20);
	ROUND2(A, B, C, D,  5, Se, 21); ROUND2(D, A, B, C, 10, Sf, 22);
	ROUND2(C, D, A, B, 15, Sg, 23); ROUND2(B, C, D, A,  4, Sh, 24);
	ROUND2(A, B, C, D,  9, Se, 25); ROUND2(D, A, B, C, 14, Sf, 26);
	ROUND2(C, D, A, B,  3, Sg, 27); ROUND2(B, C, D, A,  8, Sh, 28);
	ROUND2(A, B, C, D, 13, Se, 29); ROUND2(D, A, B, C,  2, Sf, 30);
	ROUND2(C, D, A, B,  7, Sg, 31); ROUND2(B, C, D, A, 12, Sh, 32);

	ROUND3(A, B, C, D,  5, Si, 33); ROUND3(D, A, B, C,  8, Sj, 34);
	ROUND3(C, D, A, B, 11, Sk, 35); ROUND3(B, C, D, A, 14, Sl, 36);
	ROUND3(A, B, C, D,  1, Si, 37); ROUND3(D, A, B, C,  4, Sj, 38);
	ROUND3(C, D, A, B,  7, Sk, 39); ROUND3(B, C, D, A, 10, Sl, 40);
	ROUND3(A, B, C, D, 13, Si, 41); ROUND3(D, A, B, C,  0, Sj, 42);
	ROUND3(C, D, A, B,  3, Sk, 43); ROUND3(B, C, D, A,  6, Sl, 44);
	ROUND3(A, B, C, D,  9, Si, 45); ROUND3(D, A, B, C, 12, Sj, 46);
	ROUND3(C, D, A, B, 15, Sk, 47); ROUND3(B, C, D, A,  2, Sl, 48);
	
	ROUND4(A, B, C, D,  0, Sm, 49); ROUND4(D, A, B, C,  7, Sn, 50);	
	ROUND4(C, D, A, B, 14, So, 51); ROUND4(B, C, D, A,  5, Sp, 52);	
	ROUND4(A, B, C, D, 12, Sm, 53); ROUND4(D, A, B, C,  3, Sn, 54);	
	ROUND4(C, D, A, B, 10, So, 55); ROUND4(B, C, D, A,  1, Sp, 56);	
	ROUND4(A, B, C, D,  8, Sm, 57); ROUND4(D, A, B, C, 15, Sn, 58);	
	ROUND4(C, D, A, B,  6, So, 59); ROUND4(B, C, D, A, 13, Sp, 60);	
	ROUND4(A, B, C, D,  4, Sm, 61); ROUND4(D, A, B, C, 11, Sn, 62);	
	ROUND4(C, D, A, B,  2, So, 63); ROUND4(B, C, D, A,  9, Sp, 64);

	ctxt->md5_sta += A;
	ctxt->md5_stb += B;
	ctxt->md5_stc += C;
	ctxt->md5_std += D;
}
#undef SHIFT
#undef F
#undef G
#undef H
#undef I
#undef ROUND1
#undef ROUND2
#undef ROUND3
#undef ROUND4
#undef Sa
#undef Sb
#undef Sc
#undef Sd
#undef Se
#undef Sf
#undef Sg
#undef Sh
#undef Si
#undef Sj
#undef Sk
#undef Sl
#undef Sm
#undef Sn
#undef So
#undef Sp
#undef MD5_A0
#undef MD5_B0
#undef MD5_C0
#undef MD5_D0


/* --- gskmd5 code --- */
typedef struct _HashMD5 HashMD5;
struct _HashMD5
{
  GskHash hash;
  guint8  md5value[16];
  MD5_CTX context;
};

GSK_DECLARE_POOL_ALLOCATORS(HashMD5, hash_md5, 4)

static void
gsk_hash_md5_feed (GskHash       *hash,
		   gconstpointer  data,
		   guint          len)
{
  HashMD5 *hash_md5 = (HashMD5 *) hash;
  MD5Update (&hash_md5->context, (gpointer) data, len);
}

static gpointer
gsk_hash_md5_done (GskHash        *hash)
{
  HashMD5 *hash_md5 = (HashMD5 *) hash;
  MD5Final (hash_md5->md5value, &hash_md5->context);
  return hash_md5->md5value;
}

static void
gsk_hash_md5_destroy (GskHash *hash)
{
  hash_md5_free ((HashMD5 *) hash);
}

/**
 * gsk_hash_new_md5:
 * 
 * Create a new MD5 hasher.
 *
 * returns: the newly allocated hash object.
 */
GskHash *
gsk_hash_new_md5 ()
{
  HashMD5 *hash_md5 = hash_md5_alloc ();
  GskHash *hash = (GskHash *) hash_md5;
  hash->size = 16;
  hash->feed = gsk_hash_md5_feed;
  hash->done = gsk_hash_md5_done;
  hash->destroy = gsk_hash_md5_destroy;
  hash->flags = 0;
  hash->hash_value = NULL;
  MD5Init (&hash_md5->context);
  return hash;
}

/*
 *  ____  _   _    _    _
 * / ___|| | | |  / \  / |
 * \___ \| |_| | / _ \ | |
 *  ___) |  _  |/ ___ \| |
 * |____/|_| |_/_/   \_\_|
 * 
 */
/*	FreeBSD: src/sys/crypto/sha1.h,v 1.3.2.3 2000/10/12 18:59:31 archie Exp 	*/
/*	KAME: sha1.h,v 1.5 2000/03/27 04:36:23 sumikawa Exp 	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * FIPS pub 180-1: Secure Hash Algorithm (SHA-1)
 * based on: http://csrc.nist.gov/fips/fip180-1.txt
 * implemented by Jun-ichiro itojun Itoh <itojun@itojun.org>
 */

#ifndef _NETINET6_SHA1_H_
#define _NETINET6_SHA1_H_

struct sha1_ctxt {
	union {
		guint8	b8[20];
		guint32	b32[5];
	} h;
	union {
		guint8	b8[8];
		guint64	b64[1];
	} c;
	union {
		guint8	b8[64];
		guint32	b32[16];
	} m;
	guint8	count;
};

static void sha1_init (struct sha1_ctxt *);
static void sha1_pad (struct sha1_ctxt *);
static void sha1_loop (struct sha1_ctxt *, const guint8 *, size_t);
static void sha1_result (struct sha1_ctxt *, guint8 *);

/* compatibilty with other SHA1 source codes */
typedef struct sha1_ctxt SHA1_CTX;
#define SHA1Init(x)		sha1_init((x))
#define SHA1Update(x, y, z)	sha1_loop((x), (y), (z))
#define SHA1Final(x, y)		sha1_result((y), (x))

#define	SHA1_RESULTLEN	(160/8)

#endif /*_NETINET6_SHA1_H_*/

/*	FreeBSD: src/sys/crypto/sha1.c,v 1.2.2.4 2001/07/03 11:01:27 ume Exp 	*/
/*	KAME: sha1.c,v 1.5 2000/11/08 06:13:08 itojun Exp 	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * FIPS pub 180-1: Secure Hash Algorithm (SHA-1)
 * based on: http://csrc.nist.gov/fips/fip180-1.txt
 * implemented by Jun-ichiro itojun Itoh <itojun@itojun.org>
 */

#include <sys/types.h>
#include <sys/time.h>

/* constant table */
static guint32 _K[] = { 0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6 };
#define	K(t)	_K[(t) / 20]

#define	F0(b, c, d)	(((b) & (c)) | ((~(b)) & (d)))
#define	F1(b, c, d)	(((b) ^ (c)) ^ (d))
#define	F2(b, c, d)	(((b) & (c)) | ((b) & (d)) | ((c) & (d)))
#define	F3(b, c, d)	(((b) ^ (c)) ^ (d))

#define	S(n, x)		(((x) << (n)) | ((x) >> (32 - n)))

#define	H(n)	(ctxt->h.b32[(n)])
#define	COUNT	(ctxt->count)
#define	BCOUNT	(ctxt->c.b64[0] / 8)
#define	W(n)	(ctxt->m.b32[(n)])

#define	PUTBYTE(x)	{ \
	ctxt->m.b8[(COUNT % 64)] = (x);		\
	COUNT++;				\
	COUNT %= 64;				\
	ctxt->c.b64[0] += 8;			\
	if (COUNT % 64 == 0)			\
		sha1_step(ctxt);		\
     }

#define	PUTPAD(x)	{ \
	ctxt->m.b8[(COUNT % 64)] = (x);		\
	COUNT++;				\
	COUNT %= 64;				\
	if (COUNT % 64 == 0)			\
		sha1_step(ctxt);		\
     }

static void sha1_step (struct sha1_ctxt *);

static void
sha1_step(ctxt)
	struct sha1_ctxt *ctxt;
{
	guint32	a, b, c, d, e;
	size_t t, s;
	guint32	tmp;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	struct sha1_ctxt tctxt;
	bcopy(&ctxt->m.b8[0], &tctxt.m.b8[0], 64);
	ctxt->m.b8[0] = tctxt.m.b8[3]; ctxt->m.b8[1] = tctxt.m.b8[2];
	ctxt->m.b8[2] = tctxt.m.b8[1]; ctxt->m.b8[3] = tctxt.m.b8[0];
	ctxt->m.b8[4] = tctxt.m.b8[7]; ctxt->m.b8[5] = tctxt.m.b8[6];
	ctxt->m.b8[6] = tctxt.m.b8[5]; ctxt->m.b8[7] = tctxt.m.b8[4];
	ctxt->m.b8[8] = tctxt.m.b8[11]; ctxt->m.b8[9] = tctxt.m.b8[10];
	ctxt->m.b8[10] = tctxt.m.b8[9]; ctxt->m.b8[11] = tctxt.m.b8[8];
	ctxt->m.b8[12] = tctxt.m.b8[15]; ctxt->m.b8[13] = tctxt.m.b8[14];
	ctxt->m.b8[14] = tctxt.m.b8[13]; ctxt->m.b8[15] = tctxt.m.b8[12];
	ctxt->m.b8[16] = tctxt.m.b8[19]; ctxt->m.b8[17] = tctxt.m.b8[18];
	ctxt->m.b8[18] = tctxt.m.b8[17]; ctxt->m.b8[19] = tctxt.m.b8[16];
	ctxt->m.b8[20] = tctxt.m.b8[23]; ctxt->m.b8[21] = tctxt.m.b8[22];
	ctxt->m.b8[22] = tctxt.m.b8[21]; ctxt->m.b8[23] = tctxt.m.b8[20];
	ctxt->m.b8[24] = tctxt.m.b8[27]; ctxt->m.b8[25] = tctxt.m.b8[26];
	ctxt->m.b8[26] = tctxt.m.b8[25]; ctxt->m.b8[27] = tctxt.m.b8[24];
	ctxt->m.b8[28] = tctxt.m.b8[31]; ctxt->m.b8[29] = tctxt.m.b8[30];
	ctxt->m.b8[30] = tctxt.m.b8[29]; ctxt->m.b8[31] = tctxt.m.b8[28];
	ctxt->m.b8[32] = tctxt.m.b8[35]; ctxt->m.b8[33] = tctxt.m.b8[34];
	ctxt->m.b8[34] = tctxt.m.b8[33]; ctxt->m.b8[35] = tctxt.m.b8[32];
	ctxt->m.b8[36] = tctxt.m.b8[39]; ctxt->m.b8[37] = tctxt.m.b8[38];
	ctxt->m.b8[38] = tctxt.m.b8[37]; ctxt->m.b8[39] = tctxt.m.b8[36];
	ctxt->m.b8[40] = tctxt.m.b8[43]; ctxt->m.b8[41] = tctxt.m.b8[42];
	ctxt->m.b8[42] = tctxt.m.b8[41]; ctxt->m.b8[43] = tctxt.m.b8[40];
	ctxt->m.b8[44] = tctxt.m.b8[47]; ctxt->m.b8[45] = tctxt.m.b8[46];
	ctxt->m.b8[46] = tctxt.m.b8[45]; ctxt->m.b8[47] = tctxt.m.b8[44];
	ctxt->m.b8[48] = tctxt.m.b8[51]; ctxt->m.b8[49] = tctxt.m.b8[50];
	ctxt->m.b8[50] = tctxt.m.b8[49]; ctxt->m.b8[51] = tctxt.m.b8[48];
	ctxt->m.b8[52] = tctxt.m.b8[55]; ctxt->m.b8[53] = tctxt.m.b8[54];
	ctxt->m.b8[54] = tctxt.m.b8[53]; ctxt->m.b8[55] = tctxt.m.b8[52];
	ctxt->m.b8[56] = tctxt.m.b8[59]; ctxt->m.b8[57] = tctxt.m.b8[58];
	ctxt->m.b8[58] = tctxt.m.b8[57]; ctxt->m.b8[59] = tctxt.m.b8[56];
	ctxt->m.b8[60] = tctxt.m.b8[63]; ctxt->m.b8[61] = tctxt.m.b8[62];
	ctxt->m.b8[62] = tctxt.m.b8[61]; ctxt->m.b8[63] = tctxt.m.b8[60];
#endif

	a = H(0); b = H(1); c = H(2); d = H(3); e = H(4);

	for (t = 0; t < 20; t++) {
		s = t & 0x0f;
		if (t >= 16) {
			W(s) = S(1, W((s+13) & 0x0f) ^ W((s+8) & 0x0f) ^ W((s+2) & 0x0f) ^ W(s));
		}
		tmp = S(5, a) + F0(b, c, d) + e + W(s) + K(t);
		e = d; d = c; c = S(30, b); b = a; a = tmp;
	}
	for (t = 20; t < 40; t++) {
		s = t & 0x0f;
		W(s) = S(1, W((s+13) & 0x0f) ^ W((s+8) & 0x0f) ^ W((s+2) & 0x0f) ^ W(s));
		tmp = S(5, a) + F1(b, c, d) + e + W(s) + K(t);
		e = d; d = c; c = S(30, b); b = a; a = tmp;
	}
	for (t = 40; t < 60; t++) {
		s = t & 0x0f;
		W(s) = S(1, W((s+13) & 0x0f) ^ W((s+8) & 0x0f) ^ W((s+2) & 0x0f) ^ W(s));
		tmp = S(5, a) + F2(b, c, d) + e + W(s) + K(t);
		e = d; d = c; c = S(30, b); b = a; a = tmp;
	}
	for (t = 60; t < 80; t++) {
		s = t & 0x0f;
		W(s) = S(1, W((s+13) & 0x0f) ^ W((s+8) & 0x0f) ^ W((s+2) & 0x0f) ^ W(s));
		tmp = S(5, a) + F3(b, c, d) + e + W(s) + K(t);
		e = d; d = c; c = S(30, b); b = a; a = tmp;
	}

	H(0) = H(0) + a;
	H(1) = H(1) + b;
	H(2) = H(2) + c;
	H(3) = H(3) + d;
	H(4) = H(4) + e;

	bzero(&ctxt->m.b8[0], 64);
}

/*------------------------------------------------------------*/

static void
sha1_init(ctxt)
	struct sha1_ctxt *ctxt;
{
	bzero(ctxt, sizeof(struct sha1_ctxt));
	H(0) = 0x67452301;
	H(1) = 0xefcdab89;
	H(2) = 0x98badcfe;
	H(3) = 0x10325476;
	H(4) = 0xc3d2e1f0;
}

static void
sha1_pad(ctxt)
	struct sha1_ctxt *ctxt;
{
	size_t padlen;		/*pad length in bytes*/
	size_t padstart;

	PUTPAD(0x80);

	padstart = COUNT % 64;
	padlen = 64 - padstart;
	if (padlen < 8) {
		bzero(&ctxt->m.b8[padstart], padlen);
		COUNT += padlen;
		COUNT %= 64;
		sha1_step(ctxt);
		padstart = COUNT % 64;	/* should be 0 */
		padlen = 64 - padstart;	/* should be 64 */
	}
	bzero(&ctxt->m.b8[padstart], padlen - 8);
	COUNT += (padlen - 8);
	COUNT %= 64;
#if G_BYTE_ORDER == G_BIG_ENDIAN
	PUTPAD(ctxt->c.b8[0]); PUTPAD(ctxt->c.b8[1]);
	PUTPAD(ctxt->c.b8[2]); PUTPAD(ctxt->c.b8[3]);
	PUTPAD(ctxt->c.b8[4]); PUTPAD(ctxt->c.b8[5]);
	PUTPAD(ctxt->c.b8[6]); PUTPAD(ctxt->c.b8[7]);
#else
	PUTPAD(ctxt->c.b8[7]); PUTPAD(ctxt->c.b8[6]);
	PUTPAD(ctxt->c.b8[5]); PUTPAD(ctxt->c.b8[4]);
	PUTPAD(ctxt->c.b8[3]); PUTPAD(ctxt->c.b8[2]);
	PUTPAD(ctxt->c.b8[1]); PUTPAD(ctxt->c.b8[0]);
#endif
}

static void
sha1_loop(ctxt, input, len)
	struct sha1_ctxt *ctxt;
	const guint8 *input;
	size_t len;
{
	size_t gaplen;
	size_t gapstart;
	size_t off;
	size_t copysiz;

	off = 0;

	while (off < len) {
		gapstart = COUNT % 64;
		gaplen = 64 - gapstart;

		copysiz = (gaplen < len - off) ? gaplen : len - off;
		bcopy(&input[off], &ctxt->m.b8[gapstart], copysiz);
		COUNT += copysiz;
		COUNT %= 64;
		ctxt->c.b64[0] += copysiz * 8;
		if (COUNT % 64 == 0)
			sha1_step(ctxt);
		off += copysiz;
	}
}

static void
sha1_result(ctxt, digest)
	struct sha1_ctxt *ctxt;
	guint8 *digest;
{
	sha1_pad(ctxt);
#if G_BYTE_ORDER == G_BIG_ENDIAN
	bcopy(&ctxt->h.b8[0], digest, 20);
#else
	digest[0] = ctxt->h.b8[3]; digest[1] = ctxt->h.b8[2];
	digest[2] = ctxt->h.b8[1]; digest[3] = ctxt->h.b8[0];
	digest[4] = ctxt->h.b8[7]; digest[5] = ctxt->h.b8[6];
	digest[6] = ctxt->h.b8[5]; digest[7] = ctxt->h.b8[4];
	digest[8] = ctxt->h.b8[11]; digest[9] = ctxt->h.b8[10];
	digest[10] = ctxt->h.b8[9]; digest[11] = ctxt->h.b8[8];
	digest[12] = ctxt->h.b8[15]; digest[13] = ctxt->h.b8[14];
	digest[14] = ctxt->h.b8[13]; digest[15] = ctxt->h.b8[12];
	digest[16] = ctxt->h.b8[19]; digest[17] = ctxt->h.b8[18];
	digest[18] = ctxt->h.b8[17]; digest[19] = ctxt->h.b8[16];
#endif
}

#undef	F0
#undef	F1
#undef	F2
#undef	F3
#undef	S
#undef	H
#undef	COUNT	
#undef	BCOUNT	
#undef	W
#undef	PUTBYTE
#undef	PUTPAD
/* --- gsk sha1 code --- */
typedef struct _HashSHA1 HashSHA1;
struct _HashSHA1
{
  GskHash hash;
  guint8  sha1value[16];
  SHA1_CTX context;
};

GSK_DECLARE_POOL_ALLOCATORS(HashSHA1, hash_sha1, 4);

static void
gsk_hash_sha1_feed (GskHash       *hash,
		   gconstpointer  data,
		   guint          len)
{
  HashSHA1 *hash_sha1 = (HashSHA1 *) hash;
  SHA1Update (&hash_sha1->context, (gpointer) data, len);
}

static gpointer
gsk_hash_sha1_done (GskHash        *hash)
{
  HashSHA1 *hash_sha1 = (HashSHA1 *) hash;
  SHA1Final (hash_sha1->sha1value, &hash_sha1->context);
  return hash_sha1->sha1value;
}

static void
gsk_hash_sha1_destroy (GskHash *hash)
{
  hash_sha1_free ((HashSHA1 *) hash);
}

/**
 * gsk_hash_new_sha1:
 * 
 * Create a new SHA1 hasher.
 *
 * returns: the newly allocated hash object.
 */
GskHash *
gsk_hash_new_sha1 ()
{
  HashSHA1 *hash_sha1 = hash_sha1_alloc ();
  GskHash *hash = (GskHash *) hash_sha1;
  hash->size = 20;
  hash->feed = gsk_hash_sha1_feed;
  hash->done = gsk_hash_sha1_done;
  hash->destroy = gsk_hash_sha1_destroy;
  hash->flags = 0;
  hash->hash_value = NULL;
  SHA1Init (&hash_sha1->context);
  return hash;
}

/*
 *  ____  _   _    _        ____  ____   __   
 * / ___|| | | |  / \      |___ \| ___| / /_  
 * \___ \| |_| | / _ \ _____ __) |___ \| '_ \ 
 *  ___) |  _  |/ ___ \_____/ __/ ___) | (_) |
 * |____/|_| |_/_/   \_\   |_____|____/ \___/ 
 */
typedef struct _HashSHA256 HashSHA256;
struct _HashSHA256
{
  GskHash hash;
  guint32 total[2];
  guint32 state[8];
  guint8 buffer[64];
  guint8 digest[32];    /* if done */
};

#define GET_UINT32(n,b,i)                      \
G_STMT_START{                                  \
    (n) = ((guint32) (b)[(i)    ] << 24)       \
        | ((guint32) (b)[(i) + 1] << 16)       \
        | ((guint32) (b)[(i) + 2] <<  8)       \
        | ((guint32) (b)[(i) + 3]      );      \
}G_STMT_END

#define PUT_UINT32(n,b,i)                     \
G_STMT_START{                                 \
    (b)[(i)    ] = (guint8) ((n) >> 24);      \
    (b)[(i) + 1] = (guint8) ((n) >> 16);      \
    (b)[(i) + 2] = (guint8) ((n) >>  8);      \
    (b)[(i) + 3] = (guint8) ((n)      );      \
}G_STMT_END

static inline void
sha256_process_64 (HashSHA256 *ctx,
                   const guint8 data[64])
{
  guint32 temp1, temp2, W[64];
  guint32 A, B, C, D, E, F, G, H;

  GET_UINT32 (W[0],  data,  0);
  GET_UINT32 (W[1],  data,  4);
  GET_UINT32 (W[2],  data,  8);
  GET_UINT32 (W[3],  data, 12);
  GET_UINT32 (W[4],  data, 16);
  GET_UINT32 (W[5],  data, 20);
  GET_UINT32 (W[6],  data, 24);
  GET_UINT32 (W[7],  data, 28);
  GET_UINT32 (W[8],  data, 32);
  GET_UINT32 (W[9],  data, 36);
  GET_UINT32 (W[10], data, 40);
  GET_UINT32 (W[11], data, 44);
  GET_UINT32 (W[12], data, 48);
  GET_UINT32 (W[13], data, 52);
  GET_UINT32 (W[14], data, 56);
  GET_UINT32 (W[15], data, 60);

#define  SHR(x,n) ((x & 0xFFFFFFFF) >> n)
#define ROTR(x,n) (SHR(x,n) | (x << (32 - n)))

#define S0(x) (ROTR(x, 7) ^ ROTR(x,18) ^  SHR(x, 3))
#define S1(x) (ROTR(x,17) ^ ROTR(x,19) ^  SHR(x,10))

#define S2(x) (ROTR(x, 2) ^ ROTR(x,13) ^ ROTR(x,22))
#define S3(x) (ROTR(x, 6) ^ ROTR(x,11) ^ ROTR(x,25))

#define F0(x,y,z) ((x & y) | (z & (x | y)))
#define F1(x,y,z) (z ^ (x & (y ^ z)))

#define R(t)                                    \
(                                               \
  W[t] = S1(W[t -  2]) + W[t -  7] +          \
         S0(W[t - 15]) + W[t - 16]            \
)

#define P(a,b,c,d,e,f,g,h,x,K)                  \
G_STMT_START{                                   \
  temp1 = h + S3(e) + F1(e,f,g) + K + x;      \
  temp2 = S2(a) + F0(a,b,c);                  \
  d += temp1; h = temp1 + temp2;              \
}G_STMT_END

  A = ctx->state[0];
  B = ctx->state[1];
  C = ctx->state[2];
  D = ctx->state[3];
  E = ctx->state[4];
  F = ctx->state[5];
  G = ctx->state[6];
  H = ctx->state[7];

  P (A, B, C, D, E, F, G, H, W[ 0], 0x428A2F98);
  P (H, A, B, C, D, E, F, G, W[ 1], 0x71374491);
  P (G, H, A, B, C, D, E, F, W[ 2], 0xB5C0FBCF);
  P (F, G, H, A, B, C, D, E, W[ 3], 0xE9B5DBA5);
  P (E, F, G, H, A, B, C, D, W[ 4], 0x3956C25B);
  P (D, E, F, G, H, A, B, C, W[ 5], 0x59F111F1);
  P (C, D, E, F, G, H, A, B, W[ 6], 0x923F82A4);
  P (B, C, D, E, F, G, H, A, W[ 7], 0xAB1C5ED5);
  P (A, B, C, D, E, F, G, H, W[ 8], 0xD807AA98);
  P (H, A, B, C, D, E, F, G, W[ 9], 0x12835B01);
  P (G, H, A, B, C, D, E, F, W[10], 0x243185BE);
  P (F, G, H, A, B, C, D, E, W[11], 0x550C7DC3);
  P (E, F, G, H, A, B, C, D, W[12], 0x72BE5D74);
  P (D, E, F, G, H, A, B, C, W[13], 0x80DEB1FE);
  P (C, D, E, F, G, H, A, B, W[14], 0x9BDC06A7);
  P (B, C, D, E, F, G, H, A, W[15], 0xC19BF174);
  P (A, B, C, D, E, F, G, H, R(16), 0xE49B69C1);
  P (H, A, B, C, D, E, F, G, R(17), 0xEFBE4786);
  P (G, H, A, B, C, D, E, F, R(18), 0x0FC19DC6);
  P (F, G, H, A, B, C, D, E, R(19), 0x240CA1CC);
  P (E, F, G, H, A, B, C, D, R(20), 0x2DE92C6F);
  P (D, E, F, G, H, A, B, C, R(21), 0x4A7484AA);
  P (C, D, E, F, G, H, A, B, R(22), 0x5CB0A9DC);
  P (B, C, D, E, F, G, H, A, R(23), 0x76F988DA);
  P (A, B, C, D, E, F, G, H, R(24), 0x983E5152);
  P (H, A, B, C, D, E, F, G, R(25), 0xA831C66D);
  P (G, H, A, B, C, D, E, F, R(26), 0xB00327C8);
  P (F, G, H, A, B, C, D, E, R(27), 0xBF597FC7);
  P (E, F, G, H, A, B, C, D, R(28), 0xC6E00BF3);
  P (D, E, F, G, H, A, B, C, R(29), 0xD5A79147);
  P (C, D, E, F, G, H, A, B, R(30), 0x06CA6351);
  P (B, C, D, E, F, G, H, A, R(31), 0x14292967);
  P (A, B, C, D, E, F, G, H, R(32), 0x27B70A85);
  P (H, A, B, C, D, E, F, G, R(33), 0x2E1B2138);
  P (G, H, A, B, C, D, E, F, R(34), 0x4D2C6DFC);
  P (F, G, H, A, B, C, D, E, R(35), 0x53380D13);
  P (E, F, G, H, A, B, C, D, R(36), 0x650A7354);
  P (D, E, F, G, H, A, B, C, R(37), 0x766A0ABB);
  P (C, D, E, F, G, H, A, B, R(38), 0x81C2C92E);
  P (B, C, D, E, F, G, H, A, R(39), 0x92722C85);
  P (A, B, C, D, E, F, G, H, R(40), 0xA2BFE8A1);
  P (H, A, B, C, D, E, F, G, R(41), 0xA81A664B);
  P (G, H, A, B, C, D, E, F, R(42), 0xC24B8B70);
  P (F, G, H, A, B, C, D, E, R(43), 0xC76C51A3);
  P (E, F, G, H, A, B, C, D, R(44), 0xD192E819);
  P (D, E, F, G, H, A, B, C, R(45), 0xD6990624);
  P (C, D, E, F, G, H, A, B, R(46), 0xF40E3585);
  P (B, C, D, E, F, G, H, A, R(47), 0x106AA070);
  P (A, B, C, D, E, F, G, H, R(48), 0x19A4C116);
  P (H, A, B, C, D, E, F, G, R(49), 0x1E376C08);
  P (G, H, A, B, C, D, E, F, R(50), 0x2748774C);
  P (F, G, H, A, B, C, D, E, R(51), 0x34B0BCB5);
  P (E, F, G, H, A, B, C, D, R(52), 0x391C0CB3);
  P (D, E, F, G, H, A, B, C, R(53), 0x4ED8AA4A);
  P (C, D, E, F, G, H, A, B, R(54), 0x5B9CCA4F);
  P (B, C, D, E, F, G, H, A, R(55), 0x682E6FF3);
  P (A, B, C, D, E, F, G, H, R(56), 0x748F82EE);
  P (H, A, B, C, D, E, F, G, R(57), 0x78A5636F);
  P (G, H, A, B, C, D, E, F, R(58), 0x84C87814);
  P (F, G, H, A, B, C, D, E, R(59), 0x8CC70208);
  P (E, F, G, H, A, B, C, D, R(60), 0x90BEFFFA);
  P (D, E, F, G, H, A, B, C, R(61), 0xA4506CEB);
  P (C, D, E, F, G, H, A, B, R(62), 0xBEF9A3F7);
  P (B, C, D, E, F, G, H, A, R(63), 0xC67178F2);

#undef  SHR
#undef ROTR
#undef S0
#undef S1
#undef S2
#undef S3
#undef F0
#undef F1
#undef R
#undef P
  ctx->state[0] += A;
  ctx->state[1] += B;
  ctx->state[2] += C;
  ctx->state[3] += D;
  ctx->state[4] += E;
  ctx->state[5] += F;
  ctx->state[6] += G;
  ctx->state[7] += H;
}

static void
sha256_feed (GskHash *hash, gconstpointer buffer, guint length)
{
  HashSHA256 *ctx = (HashSHA256 *) hash;
  guint32 left, fill;
  const guint8 *input = buffer;

  if (length == 0)
    return;

  left = ctx->total[0] & 0x3F;
  fill = 64 - left;

  ctx->total[0] += length;
  ctx->total[0] &= 0xFFFFFFFF;

  if (ctx->total[0] < length)
      ctx->total[1]++;

  if (left > 0 && length >= fill)
    {
      memcpy ((ctx->buffer + left), input, fill);
      sha256_process_64 (ctx, ctx->buffer);
      length -= fill;
      input  += fill;
      left = 0;
    }

  while (length >= 64)
    {
      sha256_process_64 (ctx, input);
      length -= 64;
      input  += 64;
    }

  if (length)
    memcpy (ctx->buffer + left, input, length);
}

static guint8 sha256_padding[64] =
{
 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static gpointer
sha256_done (GskHash *hash)
{
  HashSHA256 *ctx = (HashSHA256 *) hash;
  guint32 last, padn;
  guint32 high, low;
  guint8 msglen[8];

  high = (ctx->total[0] >> 29)
       | (ctx->total[1] <<  3);
  low  = (ctx->total[0] <<  3);

  PUT_UINT32 (high, msglen, 0);
  PUT_UINT32 (low,  msglen, 4);

  last = ctx->total[0] & 0x3F;
  padn = (last < 56) ? (56 - last) : (120 - last);

  sha256_feed (hash, sha256_padding, padn);
  sha256_feed (hash, msglen, 8 );

  PUT_UINT32 (ctx->state[0], ctx->digest,  0);
  PUT_UINT32 (ctx->state[1], ctx->digest,  4);
  PUT_UINT32 (ctx->state[2], ctx->digest,  8);
  PUT_UINT32 (ctx->state[3], ctx->digest, 12);
  PUT_UINT32 (ctx->state[4], ctx->digest, 16);
  PUT_UINT32 (ctx->state[5], ctx->digest, 20);
  PUT_UINT32 (ctx->state[6], ctx->digest, 24);
  PUT_UINT32 (ctx->state[7], ctx->digest, 28);

  return ctx->digest;
}
#undef PUT_UINT32
#undef GET_UINT32

GskHash *
gsk_hash_new_sha256 (void)
{
  HashSHA256 *rv = g_new (HashSHA256, 1);
  GskHash *h;
  rv->total[0] = 0;
  rv->total[1] = 0;

  rv->state[0] = 0x6A09E667;
  rv->state[1] = 0xBB67AE85;
  rv->state[2] = 0x3C6EF372;
  rv->state[3] = 0xA54FF53A;
  rv->state[4] = 0x510E527F;
  rv->state[5] = 0x9B05688C;
  rv->state[6] = 0x1F83D9AB;
  rv->state[7] = 0x5BE0CD19;

  h = (GskHash *) rv;
  h->size = 32;
  h->feed = sha256_feed;
  h->done = sha256_done;
  h->destroy = (void (*)(GskHash*)) g_free;
  h->flags = 0;
  return h;
}

/*
 *   ____ ____   ____ _________
 *  / ___|  _ \ / ___|___ /___ \
 * | |   | |_) | |     |_ \ __) |
 * | |___|  _ <| |___ ___) / __/
 *  \____|_| \_\\____|____/_____|
 * 
 */
/*
 *  COPYRIGHT (C) 1986 Gary S. Brown.  You may use this program, or
 *  code or tables extracted from it, as desired without restriction.
 *
 *  First, the polynomial itself and its table of feedback terms.  The
 *  polynomial is
 *  X^32+X^26+X^23+X^22+X^16+X^12+X^11+X^10+X^8+X^7+X^5+X^4+X^2+X^1+X^0
 *
 *  Note that we take it "backwards" and put the highest-order term in
 *  the lowest-order bit.  The X^32 term is "implied"; the LSB is the
 *  X^31 term, etc.  The X^0 term (usually shown as "+1") results in
 *  the MSB being 1
 *
 *  Note that the usual hardware shift register implementation, which
 *  is what we're using (we're merely optimizing it by doing eight-bit
 *  chunks at a time) shifts bits into the lowest-order term.  In our
 *  implementation, that means shifting towards the right.  Why do we
 *  do it this way?  Because the calculated CRC must be transmitted in
 *  order from highest-order term to lowest-order term.  UARTs transmit
 *  characters in order from LSB to MSB.  By storing the CRC this way
 *  we hand it to the UART in the order low-byte to high-byte; the UART
 *  sends each low-bit to hight-bit; and the result is transmission bit
 *  by bit from highest- to lowest-order term without requiring any bit
 *  shuffling on our part.  Reception works similarly
 *
 *  The feedback terms table consists of 256, 32-bit entries.  Notes
 *
 *      The table can be generated at runtime if desired; code to do so
 *      is shown later.  It might not be obvious, but the feedback
 *      terms simply represent the results of eight shift/xor opera
 *      tions for all combinations of data and CRC register values
 *
 *      The values must be right-shifted by eight bits by the "updcrc
 *      logic; the shift must be unsigned (bring in zeroes).  On some
 *      hardware you could probably optimize the shift in assembler by
 *      using byte-swap instructions
 *      polynomial $edb88320
 */

/* $Id: gskhash.c,v 1.1.2.3 2005/08/23 23:54:44 davebenson Exp $ */

static const guint32 crc32_table[256] = {
	0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
	0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
	0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
	0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
	0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
	0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
	0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
	0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
	0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
	0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
	0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
	0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
	0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
	0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
	0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
	0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
	0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
	0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
	0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
	0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
	0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
	0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
	0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
	0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
	0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
	0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
	0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
	0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
	0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
	0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
	0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
	0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
	0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
	0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
	0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
	0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
	0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
	0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
	0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
	0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
	0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
	0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
	0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
	0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
	0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
	0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
	0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
	0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
	0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
	0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
	0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
	0x2d02ef8dL
};
static inline guint32 
crc32(guint32 val, const void *ss, int len)
{
	const unsigned char *s = ss;
        while (--len >= 0)
                val = crc32_table[(val ^ *s++) & 0xff] ^ (val >> 8);
        return val;
}

/* --- gsk crc32 interface --- */
typedef struct _HashCRC32 HashCRC32;
struct _HashCRC32
{
  GskHash base;
  guint32 cur_value;
};

GSK_DECLARE_POOL_ALLOCATORS (HashCRC32, hash_crc32, 4)

static void
gsk_hash_crc32_feed (GskHash       *hash,
		     gconstpointer  data,
		     guint          len)
{
  HashCRC32 *hash_crc32 = (HashCRC32 *) hash;
  hash_crc32->cur_value = crc32 (hash_crc32->cur_value, data, len);
}

static gpointer
gsk_hash_crc32_done (GskHash        *hash)
{
  HashCRC32 *hash_crc32 = (HashCRC32 *) hash;
  return &hash_crc32->cur_value;
}

static gpointer
gsk_hash_crc32_done_swap (GskHash        *hash)
{
  HashCRC32 *hash_crc32 = (HashCRC32 *) hash;
  hash_crc32->cur_value = GUINT32_SWAP_LE_BE (hash_crc32->cur_value);
  return &hash_crc32->cur_value;
}

static void
gsk_hash_crc32_destroy (GskHash *hash)
{
  hash_crc32_free ((HashCRC32 *) hash);
}

/**
 * gsk_hash_new_crc32:
 * @big_endian: whether to compute a big-endian crc32 hash.
 * (As opposed to a little endian hash).
 *
 * Typically called as gsk_hash_new_crc32(G_BYTE_ORDER == G_BIG_ENDIAN).
 *
 * returns: the newly allocated hash object.
 */
GskHash *
gsk_hash_new_crc32 (gboolean big_endian)
{
  HashCRC32 *hash_crc32 = hash_crc32_alloc ();
  GskHash *hash = (GskHash *) hash_crc32;
  hash->size = 4;
  hash->feed = gsk_hash_crc32_feed;
  if (big_endian == (G_BYTE_ORDER == G_BIG_ENDIAN))
    hash->done = gsk_hash_crc32_done;
  else
    hash->done = gsk_hash_crc32_done_swap;
  hash->destroy = gsk_hash_crc32_destroy;
  hash->flags = 0;
  hash->hash_value = NULL;
  hash_crc32->cur_value = 0;
  return hash;
}
