/** @file saltygme.c
 * This example demonstrates loading, running and scripting a very simple
 * NaCl module.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_point.h"
#include "ppapi/c/pp_size.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_audio.h"
#include "ppapi/c/ppb_audio_config.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_graphics_2d.h"
#include "ppapi/c/ppb_image_data.h"
#include "ppapi/c/ppb_instance.h"
#include "ppapi/c/ppb_messaging.h"
#include "ppapi/c/ppb_url_loader.h"
#include "ppapi/c/ppb_url_request_info.h"
#include "ppapi/c/ppb_url_response_info.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppp.h"
#include "ppapi/c/ppp_instance.h"
#include "ppapi/c/ppp_messaging.h"

#include "xzdec.h"
#include "plugin-api.h"

#include "loading-song.xbm"

static PP_Module module_id = 0;
static PPB_Audio *g_audio_if = NULL;
static PPB_AudioConfig *g_audioconfig_if = NULL;
static PPB_Core *g_core_if = NULL;
static PPB_Graphics2D *g_graphics2d_if = NULL;
static PPB_ImageData *g_imagedata_if = NULL;
static PPB_Instance *g_instance_if = NULL;
static PPB_Messaging *g_messaging_if = NULL;
static PPB_URLLoader *g_urlloader_if = NULL;
static PPB_URLRequestInfo *g_urlrequestinfo_if = NULL;
static PPB_URLResponseInfo* g_urlresponseinfo_if = NULL;
static PPB_Var* g_var_if = NULL;

#define FRAME_RATE 30
#define OSCOPE_WIDTH  512
#define OSCOPE_HEIGHT 256
#define MAX_RESULT_STR_LEN 100
#define BUFFER_INCREMENT (1024 * 1024 * 10)
#define CHANNELS 2
#define SAMPLES_PER_FRAME 2
#define BYTES_PER_SAMPLE 2
#define BYTES_PER_FRAME 4
#define BUFFER_SIZE (MASTER_FREQUENCY * CHANNELS)
#define BUFFER_SIZE_IN_FRAMES MASTER_FREQUENCY
#define PRE_BUFFER_FRAMES (MASTER_FREQUENCY / 2)
#define FRAME_COUNT 4096

#define CONTAINER_STRING "Game Music Files"
#define CONTAINER_STRING_SIZE 16
#define CONTAINER_MAX_TRACKS 256
#define MAX_VOICES 16

/* functions callable from JS */
static const char* const kSetTrackId = "setTrack";
static const char* const kPrevTrackId = "prevTrack";
static const char* const kNextTrackId = "nextTrack";
static const char* const kStartPlaybackId = "startPlayback";
static const char* const kStopPlaybackId = "stopPlayback";
static const char* const kDisableVizId = "disableViz";
static const char* const kEnableVizId = "enableViz";
static const char* const kToggleVoiceId = "toggleVoice";

/* properties that can be queried from JS */
static const char* const kTrackCountId = "trackCount";
static const char* const kCurrentTrackId = "currentTrack";
static const char* const kGetVoicesId = "getVoices";

static const char ContentLengthString[] = "Content-Length: ";

/* things that can go wrong */
#define FAILURE_MEMORY 1
#define FAILURE_DECOMPRESS 2
#define FAILURE_CORRUPT_FILE 3
#define FAILURE_NETWORK 4

extern pluginInfo pluginGameMusicEmu;
extern pluginInfo pluginVio2sf;
extern pluginInfo pluginAosdkDSF;
extern pluginInfo pluginAosdkPSF;
extern pluginInfo pluginAosdkPSF2;
extern pluginInfo pluginAosdkSSF;

typedef struct
{
  int failureState;  /* if this is non-zero, something went wrong and no
                      * further processing should occur */

  PP_Instance instance;
  uint64_t baseTime;  /* base millisecond clock */
  pthread_mutex_t audioMutex;
  int specialContainer;  /* indicates special handling for a .gamemusic container */
  uint32_t containerTrackOffsets[CONTAINER_MAX_TRACKS];
  uint32_t containerTrackSizes[CONTAINER_MAX_TRACKS];
  struct PP_Var nextTrackCommand;

  /* player plugin */
  pluginInfo *playerPlugin;
  unsigned char *pluginContext;

  /* network resource */
  PP_Resource songLoader;
  unsigned char *networkBuffer;
  int networkBufferSize;
  int networkBufferPtr;
  int isLoaded;      /* indicates if the file is finished loading yet */
  int contentLength;

  /* audio playback */
  unsigned char *dataBuffer;
  int dataBufferSize;
  int dataBufferPtr;
  PP_Resource audioConfig;
  PP_Resource audioHandle;
  int frameCount;
  int startPlaying;  /* indicates if the timer callback should start audio */
  int isPlaying;     /* indicates whether playback is currently occurring */
  short audioBuffer[BUFFER_SIZE];
  unsigned int audioStart;
  unsigned int audioEnd;
  int voiceMuted[MAX_VOICES];
  int secondCounter;  /* set to framerate, dec on each frame, fire on 0 */
  int frameCountForCurrentTrack;

  /* graphics */
  PP_Resource graphics2d;
  PP_Resource oscopeData;
  uint64_t msToUpdateVideo;
  int vizFrameCounter;
  uint8_t r, g, b;
  int rInc, gInc, bInc;
  int vizEnabled;
  int disableViz;  /* this is a signal to disable the viz (blank the viz first) */
  int fadeOutFrames; /* the number of loading frames to display after load */
} SaltyGmeContext;

/* for mapping { PP_Instance, SaltyGmeContext } */
typedef struct
{
  PP_Instance instance;
  SaltyGmeContext context;
} InstanceContextMap;

#define SONG_LOAD_FAILED(error) \
  cxt->failureState = error; \
  var_result = AllocateVarFromCStr("songLoaded:0"); \
  g_messaging_if->PostMessage(cxt->instance, var_result);

#define MAX_INSTANCES 5
static InstanceContextMap g_instanceContextMap[MAX_INSTANCES];
static int g_instanceContextMapSize = 0;

static struct PP_Var AllocateVarFromCStr(const char*);
void Messaging_HandleMessage(PP_Instance, struct PP_Var);

static PP_Bool InitContext(PP_Instance instance)
{
  InstanceContextMap *mapEntry;
  SaltyGmeContext *cxt;

  if (g_instanceContextMapSize >= MAX_INSTANCES)
    return PP_FALSE;

  mapEntry = &g_instanceContextMap[g_instanceContextMapSize++];
  mapEntry->instance = instance;
  cxt = &mapEntry->context;

  cxt->failureState = 0;
  cxt->networkBuffer = NULL;
  cxt->networkBufferSize = 0;
  cxt->networkBufferPtr = 0;
  cxt->isLoaded = 0;
  cxt->isPlaying = 0;
  cxt->startPlaying = 0;
  cxt->instance = instance;
  cxt->audioStart = 0;
  cxt->audioEnd = 0;

  cxt->r = cxt->g = cxt->b = 250;
  cxt->rInc = -1;
  cxt->gInc = -2;
  cxt->bInc = -3;
  cxt->disableViz = 0;
  cxt->vizEnabled = 1;
  cxt->secondCounter = FRAME_RATE;
  cxt->nextTrackCommand = AllocateVarFromCStr(kNextTrackId);

  if (pthread_mutex_init(&cxt->audioMutex, NULL) != 0)
    return PP_FALSE;

  return PP_TRUE;
}

static SaltyGmeContext *GetContext(PP_Instance instance)
{
  int i;

  for (i = 0; i < MAX_INSTANCES; i++)
    if (g_instanceContextMap[i].instance == instance)
      return &g_instanceContextMap[i].context;

  return NULL;
}

void ResetMillisecondsCount(SaltyGmeContext *cxt)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  cxt->baseTime = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

uint64_t GetMillisecondsCount(SaltyGmeContext *cxt)
{
  struct timeval tv;
  uint64_t currentMsTime;

  gettimeofday(&tv, NULL);
  currentMsTime = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
  return currentMsTime - cxt->baseTime;
}

static void AudioCallback(void* samples, uint32_t reqLenInBytes, void* user_data)
{
  SaltyGmeContext *cxt = (SaltyGmeContext*)user_data;
  int bufferStartInBytes, bufferEndInBytes;
  int actualLen;
  unsigned char *byteSamplePtr = (unsigned char*)samples;

  pthread_mutex_lock(&cxt->audioMutex);

  /* if there is no audio ready to feed, leave */
  if (cxt->audioStart >= cxt->audioEnd)
  {
    pthread_mutex_unlock(&cxt->audioMutex);
    return;
  }

  /* guard against overruns up front */
  if ((cxt->audioEnd - cxt->audioStart) * BYTES_PER_FRAME < reqLenInBytes)
  {
    reqLenInBytes = (cxt->audioEnd - cxt->audioStart) * BYTES_PER_FRAME;
  }

  bufferStartInBytes = (cxt->audioStart % BUFFER_SIZE_IN_FRAMES) * BYTES_PER_FRAME;
  bufferEndInBytes = (cxt->audioEnd % BUFFER_SIZE_IN_FRAMES) * BYTES_PER_FRAME;

  /* decide how much to feed */
  if (bufferStartInBytes > bufferEndInBytes)
    actualLen = BUFFER_SIZE_IN_FRAMES * BYTES_PER_FRAME - bufferStartInBytes;
  else
    actualLen = bufferEndInBytes - bufferStartInBytes;
  if (actualLen > reqLenInBytes)
    actualLen = reqLenInBytes;

  /* feed it */
  memcpy(byteSamplePtr, &cxt->audioBuffer[bufferStartInBytes / BYTES_PER_SAMPLE], actualLen);
  cxt->audioStart += actualLen / BYTES_PER_FRAME;

  /* wrap-around case */
  if (actualLen < reqLenInBytes)
  {
    byteSamplePtr += actualLen;
    actualLen = reqLenInBytes - actualLen;
    bufferStartInBytes = (cxt->audioStart % BUFFER_SIZE_IN_FRAMES) * BYTES_PER_FRAME;
    /* feed it */
    memcpy(byteSamplePtr, &cxt->audioBuffer[bufferStartInBytes / BYTES_PER_SAMPLE], actualLen);
    cxt->audioStart += actualLen / BYTES_PER_FRAME;
  }

  pthread_mutex_unlock(&cxt->audioMutex);
}

/**
 * Returns a mutable C string contained in the @a var or NULL if @a var is not
 * string.  This makes a copy of the string in the @a var and adds a NULL
 * terminator.  Note that VarToUtf8() does not guarantee the NULL terminator on
 * the returned string.  See the comments for VarToUtf8() in ppapi/c/ppb_var.h
 * for more info.  The caller is responsible for freeing the returned memory.
 * @param[in] var PP_Var containing string.
 * @return a mutable C string representation of @a var.
 * @note The caller is responsible for freeing the returned string.
 */
static char* AllocateCStrFromVar(struct PP_Var var) {
  uint32_t len = 0;
  if (g_var_if != NULL) {
    const char* var_c_str = g_var_if->VarToUtf8(var, &len);
    if (len > 0) {
      char* c_str = (char*)malloc(len + 1);
      memcpy(c_str, var_c_str, len);
      c_str[len] = '\0';
      return c_str;
    }
  }
  return NULL;
}

/**
 * Creates a new string PP_Var from C string. The resulting object will be a
 * refcounted string object. It will be AddRef()ed for the caller. When the
 * caller is done with it, it should be Release()d.
 * @param[in] str C string to be converted to PP_Var
 * @return PP_Var containing string.
 */
static struct PP_Var AllocateVarFromCStr(const char* str) {
  if (g_var_if != NULL)
    return g_var_if->VarFromUtf8(str, strlen(str));
  return PP_MakeUndefined();
}

static void GraphicsFlushCallback(void* user_data, int32_t result)
{
}

/* returns the track number suitable for the UI, i.e., offset from 1 */
static int GetCurrentUITrack(SaltyGmeContext *cxt)
{
  return cxt->playerPlugin->getCurrentTrack(cxt->pluginContext) + 1;
}

static void StartTrack(SaltyGmeContext *cxt, int trackNumber)
{
  int i;
  int voiceCount;

  cxt->playerPlugin->startTrack(cxt->pluginContext, trackNumber);
  cxt->frameCountForCurrentTrack = 0;

  /* mute states propagate across tracks */
  voiceCount = cxt->playerPlugin->getVoiceCount(cxt->pluginContext);
  for (i = 0; i < MAX_VOICES; i++)
    if (i < voiceCount)
      cxt->playerPlugin->setVoiceState(cxt->pluginContext, i,
        cxt->voiceMuted[i]);
}

static void DrawLoadingFrame(uint32_t *pixels, uint32_t blackPixel,
  uint32_t loadingPixel, uint32_t whitePixel, int percentComplete)
{
  uint32_t pixelPtr;
  int x, y;
  int i;
  unsigned char mask;
  unsigned char currentByte = 0;
  int progressLimit;

  if (percentComplete < 0)
    percentComplete = 0;
  else if (percentComplete > 100)
    percentComplete = 100;

  progressLimit = (OSCOPE_WIDTH - 1) * percentComplete / 100;

  mask = 0;
  i = 0;
  for (y = 0; y < OSCOPE_HEIGHT; y++)
  {
    pixelPtr = y * OSCOPE_WIDTH;
    for (x = 0; x < OSCOPE_WIDTH; x++)
    {
      if (!mask)
      {
        currentByte = loading_song_bits[i++];
        mask = 0x01;
      }

      if (currentByte & mask)
      {
        if (x >= progressLimit)
          pixels[pixelPtr++] = blackPixel;
        else
          pixels[pixelPtr++] = loadingPixel;
      }
      else
        pixels[pixelPtr++] = whitePixel;

      mask <<= 1;
    }
  }
}

/* plots all black pixels in a frame */
static void ClearFrame(uint32_t *pixels)
{
  int i;
  for (i = 0; i < OSCOPE_WIDTH * OSCOPE_HEIGHT; i++)
    pixels[i] = 0xFF000000;
}

static void TimerCallback(void* user_data, int32_t result)
{
  SaltyGmeContext *cxt = (SaltyGmeContext*)user_data;
  struct PP_CompletionCallback timerCallback = { TimerCallback, cxt };
  struct PP_CompletionCallback flushCallback = { GraphicsFlushCallback, cxt };
  int i;
  uint32_t *pixels;
  uint32_t pixel;
  struct PP_Point topLeft;
  short *vizBuffer;
  int framesToGenerate;
  int framesPreWrap;
  int framesPostWrap;
  unsigned char progressShade;
  unsigned char textShade;
  struct PP_Var var_result;
  char result_string[MAX_RESULT_STR_LEN];
  int bufferFrames;

  if (!cxt->isLoaded && GetMillisecondsCount(cxt) >= (1000 / FRAME_RATE))
  {
    ResetMillisecondsCount(cxt);
    DrawLoadingFrame(g_imagedata_if->Map(cxt->oscopeData),
      0xFF000000, 0xFF808080, 0xFFFFFFFF,
      cxt->networkBufferPtr * 100 / cxt->contentLength);
    topLeft.x = 0;
    topLeft.y = 0;
    g_graphics2d_if->PaintImageData(cxt->graphics2d, cxt->oscopeData, &topLeft, NULL);
    g_graphics2d_if->Flush(cxt->graphics2d, flushCallback);
  }
  else if (cxt->isPlaying || cxt->startPlaying)
  {
    if (cxt->startPlaying)
    {
      ResetMillisecondsCount(cxt);
      cxt->vizFrameCounter = 0;
      cxt->msToUpdateVideo = 0;  /* update video at relative MS tick 0 */
      cxt->isPlaying = 1;

      pthread_mutex_lock(&cxt->audioMutex);
      cxt->audioStart = 0;
      cxt->audioEnd = 0;
      pthread_mutex_unlock(&cxt->audioMutex);

      bufferFrames = PRE_BUFFER_FRAMES;
    }
    else
      bufferFrames = cxt->frameCount;

    /* check if it's time to generate more audio */
    pthread_mutex_lock(&cxt->audioMutex);
    if ((cxt->audioEnd - cxt->audioStart) < bufferFrames)
    {
      framesToGenerate = bufferFrames - (cxt->audioEnd - cxt->audioStart);
      if ((cxt->audioEnd % BUFFER_SIZE_IN_FRAMES) + framesToGenerate > BUFFER_SIZE_IN_FRAMES)
      {
        /* this case handles wraparound in the audio buffer */
        framesPreWrap = BUFFER_SIZE_IN_FRAMES - (cxt->audioEnd % BUFFER_SIZE_IN_FRAMES);
        framesPostWrap = framesToGenerate - framesPreWrap;

        /* before the wraparound */
        cxt->playerPlugin->generateStereoFrames(cxt->pluginContext,
          &cxt->audioBuffer[(cxt->audioEnd % BUFFER_SIZE_IN_FRAMES) * SAMPLES_PER_FRAME],
          framesPreWrap);

        /* after the wraparound */
        cxt->playerPlugin->generateStereoFrames(cxt->pluginContext,
          &cxt->audioBuffer[0],
          framesPostWrap);
      }
      else
      {
        /* simple, non-wraparound case */
        cxt->playerPlugin->generateStereoFrames(cxt->pluginContext,
          &cxt->audioBuffer[(cxt->audioEnd % BUFFER_SIZE_IN_FRAMES) * SAMPLES_PER_FRAME],
          framesToGenerate);
      }
      cxt->audioEnd = cxt->audioStart + bufferFrames;
      cxt->frameCountForCurrentTrack += framesToGenerate;
    }
    pthread_mutex_unlock(&cxt->audioMutex);

    /* if playback is signaled, start playback and clear the signal */
    if (cxt->startPlaying)
    {
      g_audio_if->StartPlayback(cxt->audioHandle);
      cxt->startPlaying = 0;
    }

    /* check if it's time to update the visualization */
    if (GetMillisecondsCount(cxt) >= cxt->msToUpdateVideo)
    {
      pixels = g_imagedata_if->Map(cxt->oscopeData);

      /* display fade-out effect */
      if (cxt->fadeOutFrames)
      {
        progressShade = cxt->fadeOutFrames * 4;
        textShade = cxt->fadeOutFrames * 8;
        DrawLoadingFrame(pixels,
          0xFF000000,
          0xFF000000 | (progressShade << 16) | (progressShade << 8) | progressShade,
          0xFF000000 | (textShade << 16) | (textShade << 8) | textShade,
          100);
        cxt->fadeOutFrames--;
      }

      /* If the signal to disable viz was received, draw one black frame
       * before disabling */
      if (cxt->disableViz)
        ClearFrame(pixels);

      if (cxt->vizEnabled)
      {
        /* the fade-out effect takes care of clearing the frame when active */
        if (!cxt->fadeOutFrames)
          ClearFrame(pixels);
        vizBuffer = &cxt->audioBuffer[(cxt->vizFrameCounter % FRAME_RATE) * BUFFER_SIZE / FRAME_RATE];
        cxt->r += cxt->rInc;
        if (cxt->r < 64 || cxt->r > 250)
          cxt->rInc *= -1;
        cxt->g += cxt->gInc;
        if (cxt->g < 192 || cxt->g > 250)
          cxt->gInc *= -1;
        cxt->b += cxt->bInc;
        if (cxt->b < 128 || cxt->b > 250)
          cxt->bInc *= -1;
        pixel = 0xFF000000 | (cxt->r << 16) | (cxt->g << 8) | cxt->b;
        for (i = 0; i < OSCOPE_WIDTH * CHANNELS; i++)
        {
          if (i & 1)  /* right channel data */
            pixels[OSCOPE_WIDTH * ((192 - (vizBuffer[i] / 512)) - 1) + (i >> 1)] = pixel;
          else        /* left channel data */
            pixels[OSCOPE_WIDTH * (64 - (vizBuffer[i] / 512)) + (i >> 1)] = pixel;
        }
      }

      if (cxt->vizEnabled || cxt->disableViz)
      {
        /* white bar in the middle */
        pixels = g_imagedata_if->Map(cxt->oscopeData);
        pixels += OSCOPE_WIDTH * OSCOPE_HEIGHT / 2;
        for (i = 0; i < OSCOPE_WIDTH; i++)
          *pixels++ = 0xFFFFFFFF;

        topLeft.x = 0;
        topLeft.y = 0;
        g_graphics2d_if->PaintImageData(cxt->graphics2d, cxt->oscopeData, &topLeft, NULL);
        g_graphics2d_if->Flush(cxt->graphics2d, flushCallback);

        cxt->disableViz = 0;  /* clear the signal */
      }

      cxt->msToUpdateVideo = ++cxt->vizFrameCounter * 1000 / FRAME_RATE;
      cxt->secondCounter--;
    }
  }

  /* send a new time every 1/2 second */
  if (!cxt->secondCounter)
  {
    cxt->secondCounter = FRAME_RATE / 2;
    snprintf(result_string, MAX_RESULT_STR_LEN, "time:%d",
      cxt->frameCountForCurrentTrack / MASTER_FREQUENCY);
    var_result = AllocateVarFromCStr(result_string);
    g_messaging_if->PostMessage(cxt->instance, var_result);
  }

  g_core_if->CallOnMainThread(5, timerCallback, 0);
}

static void ReadCallback(void* user_data, int32_t result)
{
  SaltyGmeContext *cxt = (SaltyGmeContext*)user_data;
  struct PP_CompletionCallback readCallback = { ReadCallback, cxt };
  void *temp;
  struct PP_Var var_result;
  int i;

  if (cxt->failureState)
    return;

  /* data has been transferred to buffer; check if more is on the way */
  if (result > 0)
  {
    cxt->networkBufferPtr += result;
    /* is a bigger buffer needed? */
    if (cxt->networkBufferPtr >= cxt->networkBufferSize)
    {
      cxt->networkBufferSize += BUFFER_INCREMENT;
      temp = realloc(cxt->networkBuffer, cxt->networkBufferSize);
      if (!temp)
      {
        SONG_LOAD_FAILED(FAILURE_MEMORY);
        return;
      }
      else
        cxt->networkBuffer = temp;
    }

    /* not all the data has arrived yet; read again */
    g_urlloader_if->ReadResponseBody(cxt->songLoader,
      &cxt->networkBuffer[cxt->networkBufferPtr],
      cxt->networkBufferSize - cxt->networkBufferPtr,
      readCallback);
  }
  else
  {
    /* transfer the network buffer to the data buffer, decompressing
     * as necessary */
    if ((cxt->networkBuffer[0] == 0xFD) &&
        (cxt->networkBuffer[1] == '7') &&
        (cxt->networkBuffer[2] == 'z') &&
        (cxt->networkBuffer[3] == 'X') &&
        (cxt->networkBuffer[4] == 'Z'))
    {
      if (!xz_decompress(cxt->networkBuffer, cxt->networkBufferPtr,
        &cxt->dataBuffer, &cxt->dataBufferPtr))
      {
        SONG_LOAD_FAILED(FAILURE_DECOMPRESS);
        return;
      }
      
      free(cxt->networkBuffer);
    }
    else
    {
      cxt->dataBuffer = cxt->networkBuffer;
      cxt->dataBufferPtr = cxt->networkBufferPtr;
      cxt->networkBuffer = NULL;
    }

    cxt->pluginContext = (unsigned char *)malloc(cxt->playerPlugin->contextSize);
    if (!cxt->pluginContext)
    {
      SONG_LOAD_FAILED(FAILURE_MEMORY);
      return;
    }

    if (!cxt->playerPlugin->initPlugin(cxt->pluginContext, cxt->dataBuffer,
      cxt->dataBufferPtr))
    {
      /* signal the web page that the load failed */
      SONG_LOAD_FAILED(FAILURE_CORRUPT_FILE);
      return;
    }

    /* initial voice states */
    for (i = 0; i < MAX_VOICES; i++)
      cxt->voiceMuted[i] = 0;

    /* signal the web page that the load was successful */
    var_result = AllocateVarFromCStr("songLoaded:1");
    g_messaging_if->PostMessage(cxt->instance, var_result);

    cxt->isLoaded = 1;
    cxt->fadeOutFrames = 31;
  }
}

/* Given a URL loader, start downloading. */
static void StartDownload(SaltyGmeContext *cxt)
{
  struct PP_CompletionCallback readCallback = { ReadCallback, cxt };
  struct PP_Var var_result;
  struct PP_Var httpHeadersVar;
  PP_Resource songResponse;
  char *httpHeadersStr;

  /* get the size of the download as reported by Content-Length: */
  songResponse = g_urlloader_if->GetResponseInfo(cxt->songLoader);
  httpHeadersVar = g_urlresponseinfo_if->GetProperty(songResponse,
    PP_URLRESPONSEPROPERTY_HEADERS);
  httpHeadersStr = AllocateCStrFromVar(httpHeadersVar);
  cxt->contentLength = atoi(strstr(httpHeadersStr, ContentLengthString) +
    strlen(ContentLengthString));

  if (!cxt->networkBuffer)
  {
    cxt->networkBufferSize = BUFFER_INCREMENT;
    cxt->networkBuffer = (unsigned char*)malloc(cxt->networkBufferSize);
    if (!cxt->networkBuffer)
    {
      SONG_LOAD_FAILED(FAILURE_MEMORY);
      return;
    }
  }
  g_urlloader_if->ReadResponseBody(cxt->songLoader,
    cxt->networkBuffer, cxt->networkBufferSize, readCallback);
}

static void InitGraphics(SaltyGmeContext *cxt)
{
  struct PP_Size graphics_size;
  struct PP_CompletionCallback timerCallback = { TimerCallback, cxt };

  /* initialize the graphics */
  graphics_size.width = OSCOPE_WIDTH;
  graphics_size.height = OSCOPE_HEIGHT;
  cxt->graphics2d = g_graphics2d_if->Create(cxt->instance, &graphics_size, PP_TRUE);
  g_instance_if->BindGraphics(cxt->instance, cxt->graphics2d);

  cxt->oscopeData = g_imagedata_if->Create(
    cxt->instance,
    g_imagedata_if->GetNativeImageDataFormat(),
    &graphics_size,
    PP_TRUE
  );

  /* start the timer immediately */
  ResetMillisecondsCount(cxt);
  g_core_if->CallOnMainThread(0, timerCallback, 0);
}

/* This is called when the Open() call gets header data from server */
static void OpenComplete(void* user_data, int32_t result)
{
  SaltyGmeContext *cxt = (SaltyGmeContext*)user_data;
  struct PP_Var var_result;

  /* this probably needs to be more graceful, but this is a good first cut */
  if (result != 0)
  {
    SONG_LOAD_FAILED(FAILURE_NETWORK);
    return;
  }

  InitGraphics(cxt);

  StartDownload(cxt);
}

static PP_Bool Instance_DidCreate(PP_Instance instance,
                                  uint32_t argc,
                                  const char* argn[],
                                  const char* argv[]) {
  int i;
  PP_Resource songRequest;
  struct PP_Var urlProperty;
  int urlPropertySeen = 0;
  struct PP_Var getVar = AllocateVarFromCStr("GET");
  struct PP_CompletionCallback OpenCallback;
  int32_t ret;
  SaltyGmeContext *cxt;

  if (!InitContext(instance))
    return PP_FALSE;
  cxt = GetContext(instance);

  cxt->playerPlugin = NULL;
  for (i = 0; i < argc; i++)
  {
    if (strcmp(argn[i], "src") == 0)
    {
      urlProperty = AllocateVarFromCStr(argv[i]);
      urlPropertySeen = 1;
    }
    else if (strcmp(argn[i], "system") == 0)
    {
      /* select the audio plugin */
      if (strcmp(argv[i], "Sega Dreamcast") == 0)
        cxt->playerPlugin = &pluginAosdkDSF;
      else if (strcmp(argv[i], "Sony PlayStation") == 0)
        cxt->playerPlugin = &pluginAosdkPSF;
      else if (strcmp(argv[i], "Sony PlayStation 2") == 0)
        cxt->playerPlugin = &pluginAosdkPSF2;
      else if (strcmp(argv[i], "Sega Saturn") == 0)
        cxt->playerPlugin = &pluginAosdkSSF;
      else if (strcmp(argv[i], "Nintendo DS") == 0)
        cxt->playerPlugin = &pluginVio2sf;
      else
        cxt->playerPlugin = &pluginGameMusicEmu;
    }
  }

  /* if no valid system was passed in, don't try to proceed */
  if (!cxt->playerPlugin)
    return PP_FALSE;

  /* prepare audio interface */
  cxt->frameCount = g_audioconfig_if->RecommendSampleFrameCount(instance, MASTER_FREQUENCY, FRAME_COUNT);
  cxt->audioConfig = g_audioconfig_if->CreateStereo16Bit(instance, MASTER_FREQUENCY, cxt->frameCount);

  if (!cxt->audioConfig)
    return PP_FALSE;
  cxt->audioHandle = g_audio_if->Create(instance, cxt->audioConfig, AudioCallback, cxt);
  if (!cxt->audioHandle)
    return PP_FALSE;

  /* if song URL was obtained, start loading */
  if (urlPropertySeen)
  {
    cxt->songLoader = g_urlloader_if->Create(instance);

    songRequest = g_urlrequestinfo_if->Create(instance);
    g_urlrequestinfo_if->SetProperty(songRequest, PP_URLREQUESTPROPERTY_URL, urlProperty);
    g_urlrequestinfo_if->SetProperty(songRequest, PP_URLREQUESTPROPERTY_METHOD, getVar);

    OpenCallback.func = OpenComplete;
    OpenCallback.user_data = cxt;
    ret = g_urlloader_if->Open(cxt->songLoader, songRequest, OpenCallback);
  }

  return PP_TRUE;
}

static void Instance_DidDestroy(PP_Instance instance) {
  SaltyGmeContext *cxt;

  cxt = GetContext(instance);

  pthread_mutex_destroy(&cxt->audioMutex);
}

static void Instance_DidChangeView(PP_Instance instance,
                                   PP_Resource view) {
}

static void Instance_DidChangeFocus(PP_Instance instance,
                                    PP_Bool has_focus) {
}

static PP_Bool Instance_HandleDocumentLoad(PP_Instance instance,
                                           PP_Resource urlLoader)
{
  SaltyGmeContext *cxt = GetContext(instance);

  InitGraphics(cxt);

  cxt->songLoader = urlLoader;
  StartDownload(cxt);

  return PP_TRUE;
}

void Messaging_HandleMessage(PP_Instance instance, struct PP_Var var_message)
{
  char* message;
  struct PP_Var var_result;
  char result_string[MAX_RESULT_STR_LEN];
  SaltyGmeContext *cxt;
  int str_len;
  int i;
  int voice;

  if (var_message.type != PP_VARTYPE_STRING) {
    /* Only handle string messages */
    return;
  }

  cxt = GetContext(instance);
  if (cxt->failureState)
    return;

  var_result = PP_MakeUndefined();
  message = AllocateCStrFromVar(var_message);
  if ((strncmp(message, kNextTrackId, strlen(kNextTrackId)) == 0) ||
      (strncmp(message, kPrevTrackId, strlen(kPrevTrackId)) == 0))
  {
    g_audio_if->StopPlayback(cxt->audioHandle);
    cxt->isPlaying = 0;
    if (strncmp(message, kNextTrackId, strlen(kNextTrackId)) == 0)
      cxt->playerPlugin->nextTrack(cxt->pluginContext);
    else
      cxt->playerPlugin->previousTrack(cxt->pluginContext);
    StartTrack(cxt, -1);
    cxt->startPlaying = 1;
    snprintf(result_string, MAX_RESULT_STR_LEN, "currentTrack:%d", GetCurrentUITrack(cxt));
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kSetTrackId, strlen(kSetTrackId)) == 0)
  {
    /* check that string length allows for a ':track_num' after the command
     * and that there is a ':' character */
    str_len = strlen(kSetTrackId);
    if ((strlen(message) >= str_len + 2) &&
        (message[str_len]) == ':')
    {
      g_audio_if->StopPlayback(cxt->audioHandle);
      cxt->isPlaying = 0;
      StartTrack(cxt, atoi(&message[str_len + 1]) - 1);
      cxt->startPlaying = 1;
      snprintf(result_string, MAX_RESULT_STR_LEN, "currentTrack:%d", GetCurrentUITrack(cxt));
      var_result = AllocateVarFromCStr(result_string);
    }
  }
  else if (strncmp(message, kStartPlaybackId, strlen(kStartPlaybackId)) == 0)
  {
    cxt->startPlaying = 1;
  }
  else if (strncmp(message, kStopPlaybackId, strlen(kStopPlaybackId)) == 0)
  {
    g_audio_if->StopPlayback(cxt->audioHandle);
    cxt->isPlaying = 0;
  }
  else if (strncmp(message, kTrackCountId, strlen(kTrackCountId)) == 0)
  {
    snprintf(result_string, MAX_RESULT_STR_LEN, "trackCount:%d",
      cxt->playerPlugin->getTrackCount(cxt->pluginContext));
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kCurrentTrackId, strlen(kCurrentTrackId)) == 0)
  {
    snprintf(result_string, MAX_RESULT_STR_LEN, "currentTrack:%d", GetCurrentUITrack(cxt));
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kGetVoicesId, strlen(kGetVoicesId)) == 0)
  {
    snprintf(result_string, MAX_RESULT_STR_LEN, "voiceCount:%d",
      cxt->playerPlugin->getVoiceCount(cxt->pluginContext));
    var_result = AllocateVarFromCStr(result_string);
    g_messaging_if->PostMessage(cxt->instance, var_result);
    for (i = 0; i < cxt->playerPlugin->getVoiceCount(cxt->pluginContext); i++)
    {
      snprintf(result_string, MAX_RESULT_STR_LEN, "voiceName:%d,%s",
        i + 1,
        cxt->playerPlugin->getVoiceName(cxt->pluginContext, i));
      var_result = AllocateVarFromCStr(result_string);
      g_messaging_if->PostMessage(cxt->instance, var_result);
    }
    var_result = PP_MakeUndefined();
  }
  else if (strncmp(message, kDisableVizId, strlen(kDisableVizId)) == 0)
  {
    cxt->vizEnabled = 0;
    cxt->disableViz = 1;
  }
  else if (strncmp(message, kEnableVizId, strlen(kEnableVizId)) == 0)
  {
    cxt->vizEnabled = 1;
  }
  else if (strncmp(message, kToggleVoiceId, strlen(kToggleVoiceId)) == 0)
  {
    /* check that string length allows for a ':track_num' after the command
     * and that there is a ':' character */
    str_len = strlen(kToggleVoiceId);
    if ((strlen(message) >= str_len + 2) &&
        (message[str_len]) == ':')
    {
      voice = atoi(&message[str_len + 1]) - 1;
      if (voice >= 0 &&
        voice < cxt->playerPlugin->getVoiceCount(cxt->pluginContext))
      {
        cxt->voiceMuted[voice] ^= 1;
        cxt->playerPlugin->setVoiceState(cxt->pluginContext, voice,
          cxt->voiceMuted[voice]);
      }
    }
  }
  else
  {
    printf("Unhandled message: %s\n", message);
  }
  free(message);

  g_messaging_if->PostMessage(cxt->instance, var_result);
}

PP_EXPORT int32_t PPP_InitializeModule(PP_Module a_module_id,
                                       PPB_GetInterface get_browser_interface) {
  module_id = a_module_id;
  g_var_if = (PPB_Var*)(get_browser_interface(PPB_VAR_INTERFACE));

  /* load all the modules that will be required */
  g_audio_if =
      (PPB_Audio*)(get_browser_interface(PPB_AUDIO_INTERFACE));
  g_audioconfig_if =
      (PPB_AudioConfig*)(get_browser_interface(PPB_AUDIO_CONFIG_INTERFACE));
  g_core_if =
      (PPB_Core*)(get_browser_interface(PPB_CORE_INTERFACE));
  g_graphics2d_if =
      (PPB_Graphics2D*)(get_browser_interface(PPB_GRAPHICS_2D_INTERFACE));
  g_imagedata_if =
      (PPB_ImageData*)(get_browser_interface(PPB_IMAGEDATA_INTERFACE));
  g_instance_if =
      (PPB_Instance*)(get_browser_interface(PPB_INSTANCE_INTERFACE));
  g_messaging_if =
      (PPB_Messaging*)(get_browser_interface(PPB_MESSAGING_INTERFACE));
  g_urlloader_if =
      (PPB_URLLoader*)(get_browser_interface(PPB_URLLOADER_INTERFACE));
  g_urlrequestinfo_if =
      (PPB_URLRequestInfo*)(get_browser_interface(PPB_URLREQUESTINFO_INTERFACE));
  g_urlresponseinfo_if =
      (PPB_URLResponseInfo*)(get_browser_interface(PPB_URLRESPONSEINFO_INTERFACE));
  return PP_OK;
}

PP_EXPORT const void* PPP_GetInterface(const char* interface_name) {
  if (strcmp(interface_name, PPP_INSTANCE_INTERFACE) == 0) {
    static PPP_Instance instance_interface = {
      &Instance_DidCreate,
      &Instance_DidDestroy,
      &Instance_DidChangeView,
      &Instance_DidChangeFocus,
      &Instance_HandleDocumentLoad
    };
    return &instance_interface;
  } else if (strcmp(interface_name, PPP_MESSAGING_INTERFACE) == 0) {
    static PPP_Messaging g_messaging_if = {
      &Messaging_HandleMessage
    };
    return &g_messaging_if;
  }
  return NULL;
}

/**
 * Called before the plugin module is unloaded.
 */
PP_EXPORT void PPP_ShutdownModule() {
}
