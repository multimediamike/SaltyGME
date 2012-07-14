#include <stdlib.h>
#include "xz.h"

#define BUFFER_INCREMENT (1024 * 1024 * 10)

void init_xz()
{
  xz_crc32_init();
}

int xz_decompress(unsigned char *encoded, int encoded_size,
  unsigned char **decoded, int *decoded_size)
{
  enum xz_ret ret;
  struct xz_dec *xz;
  struct xz_buf buf;
  unsigned char *dec_buffer;
  unsigned char *temp;
  int dec_size;

  *decoded = NULL;
  *decoded_size = 0;

  buf.in = encoded;
  buf.in_pos = 0;
  buf.in_size = encoded_size;

  dec_size = BUFFER_INCREMENT;
  dec_buffer = (unsigned char *)malloc(dec_size);
  if (!dec_buffer)
  {
    return 0;
  }

  buf.out = dec_buffer;
  buf.out_pos = 0;
  buf.out_size = dec_size;

  xz = xz_dec_init(XZ_DYNALLOC, (uint32_t)-1);
  if (!xz)
  {
    free(dec_buffer);
    return 0;
  }

  do
  {
    ret = xz_dec_run(xz, &buf);
    if (ret == XZ_OK)
    {
      /* things are okay, but more buffer space is needed */
      dec_size += BUFFER_INCREMENT;
      temp = realloc(dec_buffer, dec_size);
      if (!temp)
      {
        free(dec_buffer);
        xz_dec_end(xz);
        return 0;
      }
      else
        dec_buffer = temp;
      buf.out_size = dec_size;
      buf.out = dec_buffer;
    }
    else
    {
      /* any other status is an exit condition (either error or stream end) */
      break;
    }
  } while (ret != XZ_STREAM_END);

  if (ret == XZ_STREAM_END)
  {
    /* resize the final buffer */
    dec_size = buf.out_pos;
    temp = realloc(dec_buffer, dec_size);
    if (!temp)
    {
      free(dec_buffer);
      xz_dec_end(xz);
      return 0;
    }
    *decoded = temp;
    *decoded_size = dec_size;
    xz_dec_end(xz);
    return 1;
  }
  else
  {
    xz_dec_end(xz);
    free(dec_buffer);
    return 0;
  }
}
