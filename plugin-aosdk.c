#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin-api.h"
#include "aosdk/ao.h"
#include "aosdk/eng_protos.h"

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
} aosdkContext;

typedef int32(*aosdk_start_func)(uint8*, uint32);
typedef int32(*aosdk_gen_func)(int16*, uint32);

/* obviously not thread-sade */
static aosdkContext *currentAosdkContext;

int ao_get_lib(char *pfilename, uint8 **ppbuffer, uint64 *plength)
{
  unsigned int offset;
  int i;
  int found = 0;
  unsigned int nameOffset;
  char *nameRecord;
  unsigned int filePtrOffset = 0;
  unsigned int fileSize = 0;
  unsigned char *data = currentAosdkContext->dataBuffer;
  unsigned char *dataCopy = NULL;

  offset = 20;
  for (i = 0; i < currentAosdkContext->trackCount; i++)
  {
    nameOffset = 
      (data[offset +  8] << 24) |
      (data[offset +  9] << 16) |
      (data[offset + 10] <<  8) |
      (data[offset + 11] <<  0);
    nameRecord = (char*)&data[nameOffset];
    /* simulate case-insensitive filesystem */
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

#if 0
/* redirect stubs to interface the Z80 core to the QSF engine */
uint8 memory_read(uint16 addr)
{
        return qsf_memory_read(addr);
}

uint8 memory_readop(uint16 addr)
{
        return memory_read(addr);
}

uint8 memory_readport(uint16 addr)
{
        return qsf_memory_readport(addr);
}

void memory_write(uint16 addr, uint8 byte)
{
        qsf_memory_write(addr, byte);
}

void memory_writeport(uint16 addr, uint8 byte)
{
        qsf_memory_writeport(addr, byte);
}
#endif

static int AosdkInitPlugin(void *privateData, uint8_t *data, int size)
{
  aosdkContext *cxt = (aosdkContext*)privateData;

  cxt->dataBuffer = data;
  cxt->dataBufferSize = size;
  cxt->trackCount = 0;
  cxt->currentTrack = 0;

printf("%s:%s:%d\n", __FILE__, __func__, __LINE__);
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

static int AosdkStartTrack(void *privateData, int trackNumber,
  aosdk_start_func startFunc)
{
  aosdkContext *cxt = (aosdkContext*)privateData;
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
    currentAosdkContext = cxt;
    if (startFunc(dataCopy, fileSize) != AO_SUCCESS)
      return 0;
    else
      return 1;
  }
  else
    return 0;
}

static int AosdkStartTrackDSF(void *privateData, int trackNumber)
{
  return AosdkStartTrack(privateData, trackNumber, dsf_start);
}

static int AosdkStartTrackPSF(void *privateData, int trackNumber)
{
  return AosdkStartTrack(privateData, trackNumber, psf_start);
}

static int AosdkStartTrackPSF2(void *privateData, int trackNumber)
{
  return AosdkStartTrack(privateData, trackNumber, psf2_start);
}

static int AosdkStartTrackSSF(void *privateData, int trackNumber)
{
  return AosdkStartTrack(privateData, trackNumber, ssf_start);
}

static int AosdkGenerateStereoFrames(void *privateData, int16_t *samples,
  int frameCount, aosdk_gen_func genFunc)
{
  int status;

  status = genFunc(samples, frameCount);

  return (status == AO_SUCCESS);
}

static int AosdkGenerateStereoFramesDSF(void *privateData, int16_t *samples,
  int frameCount)
{
  return AosdkGenerateStereoFrames(privateData, samples, frameCount, dsf_gen);
}

static int AosdkGenerateStereoFramesPSF(void *privateData, int16_t *samples,
  int frameCount)
{
  return AosdkGenerateStereoFrames(privateData, samples, frameCount, psf_gen);
}

static int AosdkGenerateStereoFramesPSF2(void *privateData, int16_t *samples,
  int frameCount)
{
  return AosdkGenerateStereoFrames(privateData, samples, frameCount, psf2_gen);
}

static int AosdkGenerateStereoFramesSSF(void *privateData, int16_t *samples,
  int frameCount)
{
  return AosdkGenerateStereoFrames(privateData, samples, frameCount, ssf_gen);
}

static int AosdkGetTrackCount(void *privateData)
{
  aosdkContext *cxt = (aosdkContext*)privateData;
  return cxt->trackCount;
}

static int AosdkGetCurrentTrack(void *privateData)
{
  aosdkContext *cxt = (aosdkContext*)privateData;
  return cxt->currentTrack;
}

static int AosdkNextTrack(void *privateData)
{
  aosdkContext *cxt = (aosdkContext*)privateData;
  cxt->currentTrack = (cxt->currentTrack + 1) % cxt->trackCount;

  return cxt->currentTrack;
}

static int AosdkPreviousTrack(void *privateData)
{
  aosdkContext *cxt = (aosdkContext*)privateData;

  cxt->currentTrack--;
  if (cxt->currentTrack < 0)
    cxt->currentTrack = cxt->trackCount - 1;

  return cxt->currentTrack;
}

static int AosdkGetVoiceCount(void *privateData)
{
  /* just claim that there is one master voice */
  return 1;
}

static const char* AosdkGetVoiceName(void *privateData, int voiceNumber)
{
  return "AOSDK Engine";
}

static int AosdkVoicesCanBeToggled(void *privateData)
{
  /* no, individual voices can not be toggled */
  return 0;
}

static int AosdkSetVoiceState(void *privateData, int voice, int enabled)
{
  return 0;
}

pluginInfo pluginAosdkDSF =
{
  .initPlugin =           AosdkInitPlugin,
  .startTrack =           AosdkStartTrackDSF,
  .generateStereoFrames = AosdkGenerateStereoFramesDSF,
  .getTrackCount =        AosdkGetTrackCount,
  .getCurrentTrack =      AosdkGetCurrentTrack,
  .nextTrack =            AosdkNextTrack,
  .previousTrack =        AosdkPreviousTrack,
  .getVoiceCount =        AosdkGetVoiceCount,
  .getVoiceName =         AosdkGetVoiceName,
  .voicesCanBeToggled =   AosdkVoicesCanBeToggled,
  .setVoiceState =        AosdkSetVoiceState,
  .contextSize =          sizeof(aosdkContext)
};

pluginInfo pluginAosdkPSF =
{
  .initPlugin =           AosdkInitPlugin,
  .startTrack =           AosdkStartTrackPSF,
  .generateStereoFrames = AosdkGenerateStereoFramesPSF,
  .getTrackCount =        AosdkGetTrackCount,
  .getCurrentTrack =      AosdkGetCurrentTrack,
  .nextTrack =            AosdkNextTrack,
  .previousTrack =        AosdkPreviousTrack,
  .getVoiceCount =        AosdkGetVoiceCount,
  .getVoiceName =         AosdkGetVoiceName,
  .voicesCanBeToggled =   AosdkVoicesCanBeToggled,
  .setVoiceState =        AosdkSetVoiceState,
  .contextSize =          sizeof(aosdkContext)
};

pluginInfo pluginAosdkPSF2 =
{
  .initPlugin =           AosdkInitPlugin,
  .startTrack =           AosdkStartTrackPSF2,
  .generateStereoFrames = AosdkGenerateStereoFramesPSF2,
  .getTrackCount =        AosdkGetTrackCount,
  .getCurrentTrack =      AosdkGetCurrentTrack,
  .nextTrack =            AosdkNextTrack,
  .previousTrack =        AosdkPreviousTrack,
  .getVoiceCount =        AosdkGetVoiceCount,
  .getVoiceName =         AosdkGetVoiceName,
  .voicesCanBeToggled =   AosdkVoicesCanBeToggled,
  .setVoiceState =        AosdkSetVoiceState,
  .contextSize =          sizeof(aosdkContext)
};

pluginInfo pluginAosdkSSF =
{
  .initPlugin =           AosdkInitPlugin,
  .startTrack =           AosdkStartTrackSSF,
  .generateStereoFrames = AosdkGenerateStereoFramesSSF,
  .getTrackCount =        AosdkGetTrackCount,
  .getCurrentTrack =      AosdkGetCurrentTrack,
  .nextTrack =            AosdkNextTrack,
  .previousTrack =        AosdkPreviousTrack,
  .getVoiceCount =        AosdkGetVoiceCount,
  .getVoiceName =         AosdkGetVoiceName,
  .voicesCanBeToggled =   AosdkVoicesCanBeToggled,
  .setVoiceState =        AosdkSetVoiceState,
  .contextSize =          sizeof(aosdkContext)
};
