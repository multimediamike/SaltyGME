#include <stdlib.h>
#include <string.h>

#include "plugin-api.h"
#include "gme-source/gme.h"

#define CONTAINER_STRING "Game Music Files"
#define CONTAINER_STRING_SIZE 16
#define CONTAINER_MAX_TRACKS 256
#define SAMPLES_PER_FRAME 2

typedef struct
{
  Music_Emu *emu;
  uint8_t *dataBuffer;
  int dataBufferSize;
  int specialContainer;
  uint32_t containerTrackOffsets[CONTAINER_MAX_TRACKS];
  uint32_t containerTrackSizes[CONTAINER_MAX_TRACKS];
  int trackCount;
  int voiceCount;
  int currentTrack;
} gmeContext;

static int GmeInitPlugin(void *context, uint8_t *data, int size)
{
  gmeContext *gmeCxt = (gmeContext*)context;
  int i, j;
  gme_err_t status = NULL;

  gmeCxt->dataBuffer = data;
  gmeCxt->dataBufferSize = size;
  gmeCxt->trackCount = 0;
  gmeCxt->voiceCount = 0;

  /* check for special container format */
  if (strncmp((char*)gmeCxt->dataBuffer, CONTAINER_STRING, CONTAINER_STRING_SIZE) == 0)
  {
    gmeCxt->specialContainer = 1;
    gmeCxt->trackCount =
      (gmeCxt->dataBuffer[16] << 24) | (gmeCxt->dataBuffer[17] << 16) |
      (gmeCxt->dataBuffer[18] <<  8) | (gmeCxt->dataBuffer[19]);
    j = 0;
    /* load the offsets */
    for (i = CONTAINER_STRING_SIZE + 4;
         i < CONTAINER_STRING_SIZE + 4 + gmeCxt->trackCount * 4;
         i += 4)
    {
      gmeCxt->containerTrackOffsets[j++] =
        (gmeCxt->dataBuffer[i  ] << 24) | (gmeCxt->dataBuffer[i+1] << 16) |
        (gmeCxt->dataBuffer[i+2] <<  8) | (gmeCxt->dataBuffer[i+3]);
    }
    /* derive the sizes */
    for (i = 0; i < gmeCxt->trackCount - 1; i++)
      gmeCxt->containerTrackSizes[i] =
        gmeCxt->containerTrackOffsets[i+1] - gmeCxt->containerTrackOffsets[i];
    /* special case for last size */
    gmeCxt->containerTrackSizes[i] =
      gmeCxt->dataBufferSize - gmeCxt->containerTrackOffsets[i];
  }
  else
    gmeCxt->specialContainer = 0;

  gmeCxt->currentTrack = 0;

  gmeCxt->emu = NULL;

  if (!gmeCxt->specialContainer)
  {
    status = gme_open_data(gmeCxt->dataBuffer, gmeCxt->dataBufferSize,
      &gmeCxt->emu, MASTER_FREQUENCY);
    if (!status)
    {
      gmeCxt->trackCount = gme_track_count(gmeCxt->emu);
      gmeCxt->voiceCount = gme_voice_count(gmeCxt->emu);
    }
  }

  /* if no error message was returned, open call successed */
  return (status == NULL);
}

static int GmeStartTrack(void *context, int trackNumber)
{
  int i;
  gmeContext *gmeCxt = (gmeContext*)context;
  gme_err_t status;

  if (trackNumber == -1)
    i = gmeCxt->currentTrack;
  else
    i = trackNumber;

  if (gmeCxt->specialContainer)
  {
    if (gmeCxt->emu)
      gme_delete(gmeCxt->emu);

    status = gme_open_data(&gmeCxt->dataBuffer[gmeCxt->containerTrackOffsets[i]],
      gmeCxt->containerTrackSizes[i], &gmeCxt->emu, MASTER_FREQUENCY);
    if (status)
      return 0;
    status = gme_start_track(gmeCxt->emu, 0);
    if (status)
      return 0;
    gmeCxt->voiceCount = gme_voice_count(gmeCxt->emu);
  }
  else
  {
    status = gme_start_track(gmeCxt->emu, i);
    gmeCxt->voiceCount = gme_voice_count(gmeCxt->emu);
    if (status)
      return 0;
  }

  return 1;
}

static int GmeGenerateStereoFrames(void *context, int16_t *samples, int frameCount)
{
  gmeContext *gmeCxt = (gmeContext*)context;
  gme_err_t status;

  status = gme_play(gmeCxt->emu, frameCount * SAMPLES_PER_FRAME, samples);

  return (status == NULL);
}

static int GmeGetTrackCount(void *context)
{
  gmeContext *gmeCxt = (gmeContext*)context;
  return gmeCxt->trackCount;
}

static int GmeGetCurrentTrack(void *context)
{
  gmeContext *gmeCxt = (gmeContext*)context;
  return gmeCxt->currentTrack;
}

static int GmeNextTrack(void *context)
{
  gmeContext *gmeCxt = (gmeContext*)context;
  gmeCxt->currentTrack = (gmeCxt->currentTrack + 1) % gmeCxt->trackCount;

  return gmeCxt->currentTrack;
}

static int GmePreviousTrack(void *context)
{
  gmeContext *gmeCxt = (gmeContext*)context;

  gmeCxt->currentTrack--;
  if (gmeCxt->currentTrack < 0)
    gmeCxt->currentTrack = gmeCxt->trackCount - 1;

  return gmeCxt->currentTrack;
}

static int GmeGetVoiceCount(void *context)
{
  gmeContext *gmeCxt = (gmeContext*)context;

  return gmeCxt->voiceCount;
}

static const char* GmeGetVoiceName(void *context, int voiceNumber)
{
  gmeContext *gmeCxt = (gmeContext*)context;

  if (voiceNumber < gmeCxt->voiceCount)
    return gme_voice_name(gmeCxt->emu, voiceNumber);
  else
    return NULL;
}

static int GmeVoicesCanBeToggled(void *context)
{
  /* yes, individual voices can be toggled */
  return 1;
}

static int GmeSetVoiceState(void *context, int voice, int enabled)
{
  gmeContext *gmeCxt = (gmeContext*)context;
  if (voice < gmeCxt->voiceCount)
    gme_mute_voice(gmeCxt->emu, voice, enabled);
  return 1;
}

pluginInfo pluginGameMusicEmu =
{
  .initPlugin =           GmeInitPlugin,
  .startTrack =           GmeStartTrack,
  .generateStereoFrames = GmeGenerateStereoFrames,
  .getTrackCount =        GmeGetTrackCount,
  .getCurrentTrack =      GmeGetCurrentTrack,
  .nextTrack =            GmeNextTrack,
  .previousTrack =        GmePreviousTrack,
  .getVoiceCount =        GmeGetVoiceCount,
  .getVoiceName =         GmeGetVoiceName,
  .voicesCanBeToggled =   GmeVoicesCanBeToggled,
  .setVoiceState =        GmeSetVoiceState,
  .contextSize =          sizeof(gmeContext)
};
