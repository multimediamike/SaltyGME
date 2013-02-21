#include <stdio.h>
#include <stdlib.h>
#include <pulse/simple.h>

#include "xzdec.h"
#include "plugin-api.h"

extern pluginInfo pluginGameMusicEmu;
extern pluginInfo pluginVio2sf;
extern pluginInfo pluginAosdkDSF;
extern pluginInfo pluginAosdkPSF;
extern pluginInfo pluginAosdkPSF2;
extern pluginInfo pluginAosdkSSF;

#define BUFFER_SIZE 2048

int main(int argc, char *argv[])
{
  pluginInfo *playerPlugin;
  FILE *f;
  unsigned char *data;
  size_t data_size;
  unsigned char *context;
  pa_simple *s;
  pa_sample_spec spec;
  int pa_error;
  short audio_buffer[BUFFER_SIZE * 2];
  int track_count;
  int requested_track;

  /* initialize the Embedded XZ library, in case it's needed */
  init_xz();

  /* process rigid command line options */
  if (argc < 4)
  {
    printf("USAGE: salty-pulse <engine number> <song file> <track number>\n");
    printf("Available engines:\n");
    printf("  1: GME\n");
    printf("  2: AOSDK/DSF\n");
    printf("  3: AOSDK/PSF\n");
    printf("  4: AOSDK/PSF2\n");
    printf("  5: AOSDK/SSF\n");
    printf("  6: Vio2sf\n");
    return 1;
  }

  /* choose player plugin */
  switch (atoi(argv[1]))
  {
    case 1:
      playerPlugin = &pluginGameMusicEmu;
      break;
    case 2:
      playerPlugin = &pluginAosdkDSF;
      break;
    case 3:
      playerPlugin = &pluginAosdkPSF;
      break;
    case 4:
      playerPlugin = &pluginAosdkPSF2;
      break;
    case 5:
      playerPlugin = &pluginAosdkSSF;
      break;
    case 6:
      playerPlugin = &pluginVio2sf;
      break;
    default:
      printf("invalid engine number: %d\n", atoi(argv[1]));
      break;
  }

  /* load song data */
  f = fopen(argv[2], "rb");
  if (!f)
  {
    perror(argv[2]);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  data_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  data = (unsigned char *)malloc(data_size);
  if (!data)
  {
    printf("no memory\n");
    return 2;
  }
  if (fread(data, data_size, 1, f) != 1)
  {
    perror(argv[2]);
    fclose(f);
    return 1;
  }
  fclose(f);

  printf("data is %d bytes large\n", data_size);

  /* initialize player */
  context = (unsigned char *)malloc(playerPlugin->contextSize);
  if (!context)
  {
    printf("no memory\n");
    return 2;
  }
  if (!playerPlugin->initPlugin(context, data, data_size))
  {
    printf("could not init player plugin\n");
    return 3;
  }
  track_count = playerPlugin->getTrackCount(context);
  requested_track = atoi(argv[3]) - 1;
  printf("initialization successful; file has %d tracks\n", track_count);
  if (requested_track < 0 || requested_track >= track_count)
  {
    printf("invalid track: %d\n", requested_track + 1);
    return 1;
  }
  playerPlugin->startTrack(context, requested_track);

  /* open PulseAudio */
  spec.channels = 2;
  spec.rate = MASTER_FREQUENCY;
  spec.format = PA_SAMPLE_S16LE;
  s = pa_simple_new(NULL, "PulseAudio Testbench", PA_STREAM_PLAYBACK, NULL,
    "Audio", &spec, NULL, NULL, NULL);
  if (!s)
  {
    printf("problem opening audio via PulseAudio\n");
    return 3;
  }

  while (1)
  {
    playerPlugin->generateStereoFrames(context, audio_buffer, BUFFER_SIZE);
    pa_simple_write(s, audio_buffer, BUFFER_SIZE * 2 * 2, &pa_error);
  }

  pa_simple_drain(s, &pa_error);
  pa_simple_free(s);

  return 0;
}
