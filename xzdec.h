#ifndef _XZDEC_H_
#define _XZDEC_H_

int xz_decompress(unsigned char *encoded, int encoded_size,
  unsigned char **decoded, int *decoded_size);

#endif  // _XZDEC_H_
