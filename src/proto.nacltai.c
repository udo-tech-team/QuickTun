/* Copyright 2010 Ivo Smits <Ivo@UCIS.nl>. All rights reserved.
   Redistribution and use in source and binary forms, with or without modification, are
   permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.

   THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
   FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
   ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   The views and conclusions contained in the software and documentation are those of the
   authors and should not be interpreted as representing official policies, either expressed
   or implied, of Ivo Smits.*/

#include "common.c"
#include "crypto_box_curve25519xsalsa20poly1305.h"
#include "crypto_scalarmult_curve25519.h"
#include <sys/types.h>
#include <sys/time.h>

#define uint64 unsigned long long //typedef unsigned long long uint64;

struct tai {
  uint64 x;
};
struct taia {
  struct tai sec;
  unsigned long nano; /* 0...999999999 */
  unsigned long atto; /* 0...999999999 */
};

struct qt_proto_data_nacltai {
	unsigned char cenonce[crypto_box_curve25519xsalsa20poly1305_NONCEBYTES], cdnonce[crypto_box_curve25519xsalsa20poly1305_NONCEBYTES];
	unsigned char cbefore[crypto_box_curve25519xsalsa20poly1305_BEFORENMBYTES];
	struct taia cdtaip, cdtaie;
};

#define noncelength 16
#define nonceoffset (crypto_box_curve25519xsalsa20poly1305_NONCEBYTES - noncelength)
/*static unsigned char cbefore[crypto_box_curve25519xsalsa20poly1305_BEFORENMBYTES];
static unsigned char buffer1[MAX_PACKET_LEN+crypto_box_curve25519xsalsa20poly1305_ZEROBYTES], buffer2[MAX_PACKET_LEN+crypto_box_curve25519xsalsa20poly1305_ZEROBYTES];
static const unsigned char* buffer1offset = buffer1 + crypto_box_curve25519xsalsa20poly1305_ZEROBYTES;
static const unsigned char* buffer2offset = buffer2 + crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES - noncelength;*/
static const int overhead                 = crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES + noncelength;

void tai_pack(char *s, struct tai *t) {
  uint64 x;
  x = t->x;
  s[7] = x & 255; x >>= 8;
  s[6] = x & 255; x >>= 8;
  s[5] = x & 255; x >>= 8;
  s[4] = x & 255; x >>= 8;
  s[3] = x & 255; x >>= 8;
  s[2] = x & 255; x >>= 8;
  s[1] = x & 255; x >>= 8;
  s[0] = x;
}
void tai_unpack(char *s, struct tai *t) {
  uint64 x;
  x = (unsigned char) s[0];
  x <<= 8; x += (unsigned char) s[1];
  x <<= 8; x += (unsigned char) s[2];
  x <<= 8; x += (unsigned char) s[3];
  x <<= 8; x += (unsigned char) s[4];
  x <<= 8; x += (unsigned char) s[5];
  x <<= 8; x += (unsigned char) s[6];
  x <<= 8; x += (unsigned char) s[7];
  t->x = x;
}
void taia_pack(char *s, struct taia *t) {
  unsigned long x;
  tai_pack(s,&t->sec);
  s += 8;
  x = t->atto;
  s[7] = x & 255; x >>= 8;
  s[6] = x & 255; x >>= 8;
  s[5] = x & 255; x >>= 8;
  s[4] = x;
  x = t->nano;
  s[3] = x & 255; x >>= 8;
  s[2] = x & 255; x >>= 8;
  s[1] = x & 255; x >>= 8;
  s[0] = x;
} 
void taia_unpack(char *s, struct taia *t) {
  unsigned long x;
  tai_unpack(s,&t->sec);
  s += 8;
  x = (unsigned char) s[4];
  x <<= 8; x += (unsigned char) s[5];
  x <<= 8; x += (unsigned char) s[6];
  x <<= 8; x += (unsigned char) s[7];
  t->atto = x;
  x = (unsigned char) s[0];
  x <<= 8; x += (unsigned char) s[1];
  x <<= 8; x += (unsigned char) s[2];
  x <<= 8; x += (unsigned char) s[3];
  t->nano = x;
}

void taia_now(struct taia *t) {
  struct timeval now;
  gettimeofday(&now,(struct timezone *) 0);
  t->sec.x = 4611686018427387914ULL + (uint64) now.tv_sec;
  t->nano = 1000 * now.tv_usec + 500;
  t->atto++;
}

static int encode(struct qtsession* sess, char* raw, char* enc, int len) {
	if (debug) fprintf(stderr, "Encoding packet of %d bytes from %p to %p\n", len, raw, enc);
	struct qt_proto_data_nacltai* d = (struct qt_proto_data_nacltai*)sess->protocol_data;
	memset(raw, 0, crypto_box_curve25519xsalsa20poly1305_ZEROBYTES);
	taia_now(&d->cdtaie);
	taia_pack(d->cenonce + nonceoffset, &(d->cdtaie));
	if (crypto_box_curve25519xsalsa20poly1305_afternm(enc, raw, len + crypto_box_curve25519xsalsa20poly1305_ZEROBYTES, d->cenonce, d->cbefore)) return errorexit("Encryption failed");
	memcpy((void*)(enc + crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES - noncelength), d->cenonce + nonceoffset, noncelength);
	len += overhead;
	if (debug) fprintf(stderr, "Encoded packet of %d bytes from %p to %p\n", len, raw, enc);
	return len;
}

static int decode(struct qtsession* sess, char* enc, char* raw, int len) {
	if (debug) fprintf(stderr, "Decoding packet of %d bytes from %p to %p\n", len, enc, raw);
	struct qt_proto_data_nacltai* d = (struct qt_proto_data_nacltai*)sess->protocol_data;
	struct taia cdtaic;
	int i;
	if (len < overhead) {
		fprintf(stderr, "Short packet received: %d\n", len);
		return -1;
	}
	len -= overhead;
	taia_unpack((char*)(enc + crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES - noncelength), &cdtaic);
	if (cdtaic.sec.x <= d->cdtaip.sec.x && cdtaic.nano <= d->cdtaip.nano && cdtaic.atto <= d->cdtaip.atto) {
		fprintf(stderr, "Timestamp going back, ignoring packet\n");
		return -1;
	}
	memcpy(d->cdnonce + nonceoffset, enc + crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES - noncelength, noncelength);
	memset(enc, 0, crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES);
	if (i = crypto_box_curve25519xsalsa20poly1305_open_afternm(raw, enc, len + crypto_box_curve25519xsalsa20poly1305_ZEROBYTES, d->cdnonce, d->cbefore)) {
		fprintf(stderr, "Decryption failed len=%d result=%d\n", len, i);
		return -1;
	}
	d->cdtaip = cdtaic;
	if (debug) fprintf(stderr, "Decoded packet of %d bytes from %p to %p\n", len, enc, raw);
	return len;
}

static int init(struct qtsession* sess) {
	struct qt_proto_data_nacltai* d = (struct qt_proto_data_nacltai*)sess->protocol_data;
	char* envval;
	printf("Initializing cryptography...\n");
	unsigned char cownpublickey[crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES], cpublickey[crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES], csecretkey[crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES];
	if (!(envval = getconf("PUBLIC_KEY"))) return errorexit("Missing PUBLIC_KEY");
	if (strlen(envval) != 2*crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES) return errorexit("PUBLIC_KEY length");
	hex2bin(cpublickey, envval, crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES);
	if (envval = getconf("PRIVATE_KEY")) {
		if (strlen(envval) != 2*crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES) return errorexit("PRIVATE_KEY length");
		hex2bin(csecretkey, envval, crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES);
	} else if (envval = getconf("PRIVATE_KEY_FILE")) {
		FILE* pkfile = fopen(envval, "rb");
		if (!pkfile) return errorexitp("Could not open PRIVATE_KEY_FILE");
		char pktextbuf[crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES * 2];
		size_t pktextsize = fread(pktextbuf, 1, sizeof(pktextbuf), pkfile);
		if (pktextsize == crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES) {
			memcpy(csecretkey, pktextbuf, crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES);
		} else if (pktextsize = 2 * crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES) {
			hex2bin(csecretkey, pktextbuf, crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES);
		} else {
			return errorexit("PRIVATE_KEY length");
		}
		fclose(pkfile);
	} else {
		return errorexit("Missing PRIVATE_KEY");
	}
	crypto_box_curve25519xsalsa20poly1305_beforenm(d->cbefore, cpublickey, csecretkey);

	memset(d->cenonce, 0, crypto_box_curve25519xsalsa20poly1305_NONCEBYTES);
	memset(d->cdnonce, 0, crypto_box_curve25519xsalsa20poly1305_NONCEBYTES);

	crypto_scalarmult_curve25519_base(cownpublickey, csecretkey);

	if (envval = getconf("TIME_WINDOW")) {
		taia_now(&d->cdtaip);
		d->cdtaip.sec.x -= atol(envval);
	} else {
		fprintf(stderr, "Warning: TIME_WINDOW not set, risking an initial replay attack\n");
	}
	if (envval = getconf("ROLE")) {
		d->cenonce[nonceoffset-1] = atoi(envval) ? 1 : 0;
	} else {
		d->cenonce[nonceoffset-1] = memcmp(cownpublickey, cpublickey, crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES) > 0 ? 1 : 0;
	}
	d->cdnonce[nonceoffset-1] = d->cenonce[nonceoffset-1] ? 0 : 1;
	return 0;
}

struct qtproto qtproto_nacltai = {
	1,
	MAX_PACKET_LEN + crypto_box_curve25519xsalsa20poly1305_ZEROBYTES,
	MAX_PACKET_LEN + crypto_box_curve25519xsalsa20poly1305_ZEROBYTES,
	crypto_box_curve25519xsalsa20poly1305_ZEROBYTES,
	crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES - noncelength,
	encode,
	decode,
	init,
	sizeof(struct qt_proto_data_nacltai),
};

#ifndef COMBINED_BINARY
int main(int argc, char** argv) {
	print_header();
	if (qtprocessargs(argc, argv) < 0) return -1;
	return qtrun(&qtproto_nacltai);
}
#endif
