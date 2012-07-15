#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin-api.h"
#include "vio2sf/vio2sf.h"

#define CONTAINER_STRING "PSF Song Archive"
#define CONTAINER_STRING_SIZE 16
#define INDEX_RECORD_SIZE 12

typedef struct
{
  uint8_t *dataBuffer;
  int dataBufferSize;
  int trackCount;
  int currentTrack;
  int initialized;
} twosfContext;

/* obviously not thread-sade */
static twosfContext *currentTwosfContext;

int xsf_get_lib(char *pfilename, void **ppbuffer, unsigned int *plength)
{
  unsigned int offset;
  int i;
  int found = 0;
  unsigned int nameOffset;
  char *nameRecord;
  unsigned int filePtrOffset = 0;
  unsigned int fileSize = 0;
  unsigned char *data = currentTwosfContext->dataBuffer;
  unsigned char *dataCopy = NULL;

  offset = 20;
  for (i = 0; i < currentTwosfContext->trackCount; i++)
  {
    nameOffset = 
      (data[offset +  8] << 24) |
      (data[offset +  9] << 16) |
      (data[offset + 10] <<  8) |
      (data[offset + 11] <<  0);
    nameRecord = (char*)&data[nameOffset];
    /* should be a binary search, ideally */
    if (strncasecmp(nameRecord, pfilename, strlen(pfilename)) == 0)
    {
      found = 1;
      filePtrOffset =
        (data[offset + 0] << 24) |
        (data[offset + 1] << 16) |
        (data[offset + 2] <<  8) |
        (data[offset + 3] <<  0);
      fileSize =
        (data[offset + 4] << 24) |
        (data[offset + 5] << 16) |
        (data[offset + 6] <<  8) |
        (data[offset + 7] <<  0);
      break;
    }
    offset += INDEX_RECORD_SIZE;
  }

  if (found)
  {
    dataCopy = (unsigned char*)malloc(fileSize);
    memcpy(dataCopy, &data[filePtrOffset], fileSize);
  }
  *ppbuffer = dataCopy;
  *plength = fileSize;

  return found;
}

static int TwosfInitPlugin(void *privateData, uint8_t *data, int size)
{
  twosfContext *cxt = (twosfContext*)privateData;

  cxt->dataBuffer = data;
  cxt->dataBufferSize = size;
  cxt->trackCount = 0;
  cxt->currentTrack = 0;

  /* check for special container format */
  if (cxt->dataBufferSize < CONTAINER_STRING_SIZE ||
    strncmp((char*)cxt->dataBuffer, CONTAINER_STRING, CONTAINER_STRING_SIZE) == 1)
    cxt->initialized = 0;
  else
  {
    cxt->initialized = 1;
    cxt->trackCount =
      (cxt->dataBuffer[16] << 24) | (cxt->dataBuffer[17] << 16) |
      (cxt->dataBuffer[18] <<  8) | (cxt->dataBuffer[19]);
  }

  return cxt->initialized;
}

static int TwosfStartTrack(void *privateData, int trackNumber)
{
  twosfContext *cxt = (twosfContext*)privateData;
  unsigned int offset;
  unsigned int fileIndex;
  unsigned char *filePtr;
  unsigned int fileSize;
  unsigned char *dataCopy = NULL;

  if (trackNumber == -1)
    trackNumber = cxt->currentTrack;

  offset = 20 + (trackNumber * INDEX_RECORD_SIZE);
  fileIndex =
    (cxt->dataBuffer[offset + 0] << 24) |
    (cxt->dataBuffer[offset + 1] << 16) |
    (cxt->dataBuffer[offset + 2] <<  8) |
    (cxt->dataBuffer[offset + 3] <<  0);
  filePtr = &cxt->dataBuffer[fileIndex];
  offset += 4;
  fileSize =
    (cxt->dataBuffer[offset + 0] << 24) |
    (cxt->dataBuffer[offset + 1] << 16) |
    (cxt->dataBuffer[offset + 2] <<  8) |
    (cxt->dataBuffer[offset + 3] <<  0);

  dataCopy = (unsigned char*)malloc(fileSize);
  if (dataCopy)
  {
    memcpy(dataCopy, filePtr, fileSize);
    currentTwosfContext = cxt;
    return xsf_start(dataCopy, fileSize);
  }
  else
    return 0;
}

static int TwosfGenerateStereoFrames(void *privateData, int16_t *samples, int frameCount)
{
  int status;

  status = xsf_gen(samples, frameCount / 2);

  return (status == XSF_TRUE);
}

static int TwosfGetTrackCount(void *privateData)
{
  twosfContext *cxt = (twosfContext*)privateData;
  return cxt->trackCount;
}

static int TwosfGetCurrentTrack(void *privateData)
{
  twosfContext *cxt = (twosfContext*)privateData;
  return cxt->currentTrack;
}

static int TwosfNextTrack(void *privateData)
{
  twosfContext *cxt = (twosfContext*)privateData;
  cxt->currentTrack = (cxt->currentTrack + 1) % cxt->trackCount;

  return cxt->currentTrack;
}

static int TwosfPreviousTrack(void *privateData)
{
  twosfContext *cxt = (twosfContext*)privateData;

  cxt->currentTrack--;
  if (cxt->currentTrack < 0)
    cxt->currentTrack = cxt->trackCount - 1;

  return cxt->currentTrack;
}

static int TwosfGetVoiceCount(void *privateData)
{
  /* just claim that there is one master voice */
  return 1;
}

static const char* TwosfGetVoiceName(void *privateData, int voiceNumber)
{
  return "Nintendo DS Audio";
}

static int TwosfVoicesCanBeToggled(void *privateData)
{
  /* no, individual voices can not be toggled */
  return 0;
}

static int TwosfSetVoiceState(void *privateData, int voice, int enabled)
{
  return 0;
}

pluginInfo pluginVio2sf =
{
  .initPlugin =           TwosfInitPlugin,
  .startTrack =           TwosfStartTrack,
  .generateStereoFrames = TwosfGenerateStereoFrames,
  .getTrackCount =        TwosfGetTrackCount,
  .getCurrentTrack =      TwosfGetCurrentTrack,
  .nextTrack =            TwosfNextTrack,
  .previousTrack =        TwosfPreviousTrack,
  .getVoiceCount =        TwosfGetVoiceCount,
  .getVoiceName =         TwosfGetVoiceName,
  .voicesCanBeToggled =   TwosfVoicesCanBeToggled,
  .setVoiceState =        TwosfSetVoiceState,
  .contextSize =          sizeof(twosfContext)
};
