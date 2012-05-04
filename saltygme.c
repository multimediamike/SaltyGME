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

#include "gme-source/gme.h"
#include "xz-embedded/xz.h"

static PP_Module module_id = 0;
static struct PPB_Audio *g_audio_if = NULL;
static struct PPB_AudioConfig *g_audioconfig_if = NULL;
static struct PPB_Core *g_core_if = NULL;
static struct PPB_Graphics2D *g_graphics2d_if = NULL;
static struct PPB_ImageData *g_imagedata_if = NULL;
static struct PPB_Instance *g_instance_if = NULL;
static struct PPB_Messaging *g_messaging_if = NULL;
static struct PPB_URLLoader *g_urlloader_if = NULL;
static struct PPB_URLRequestInfo *g_urlrequestinfo_if = NULL;
static struct PPB_URLResponseInfo* g_urlresponseinfo_if = NULL;
static struct PPB_Var* g_var_if = NULL;

#define FRAME_RATE 30
#define OSCOPE_WIDTH  512
#define OSCOPE_HEIGHT 256
#define FREQUENCY 44100
#define MAX_RESULT_STR_LEN 100
#define BUFFER_INCREMENT (1024 * 1024)
#define CHANNELS 2
#define BUFFER_SIZE (FREQUENCY * CHANNELS)

/* functions callable from JS */
static const char* const kPrevTrackId = "prevTrack";
static const char* const kNextTrackId = "nextTrack";
static const char* const kStartPlaybackId = "startPlayback";
static const char* const kStopPlaybackId = "stopPlayback";

/* properties that can be queried from JS */
static const char* const kTrackCountId = "trackCount";
static const char* const kCurrentTrackId = "currentTrack";
static const char* const kVoiceCountId = "voiceCount";
static const char* const kVoiceNameId = "voiceName";

typedef struct
{
  PP_Instance instance;
  uint64_t baseTime;  /* base millisecond clock */
  pthread_mutex_t audioMutex;

  /* network resource */
  PP_Resource songLoader;
  unsigned char *networkBuffer;
  int networkBufferSize;
  int networkBufferPtr;

  /* audio playback */
  unsigned char *dataBuffer;
  int dataBufferSize;
  int dataBufferPtr;
  PP_Resource audioConfig;
  PP_Resource audioHandle;
  int sampleCount;
  Music_Emu *emu;
  int currentTrack;
  int trackCount;
  gme_info_t *metadata;
  int startPlaying;  /* indicates if the timer callback should start audio */
  int isPlaying;     /* indicates whether playback is currently occurring */
  PP_Bool songLoaded;
  int voiceCount;
  short audioBuffer[BUFFER_SIZE];
  unsigned int audioStart;
  unsigned int audioEnd;

  /* graphics */
  PP_Resource graphics2d;
  PP_Resource oscopeData;
  uint64_t msToUpdateVideo;
  int frameCounter;
  uint8_t r, g, b;
  int rInc, gInc, bInc;
} SaltyGmeContext;

/* for mapping { PP_Instance, SaltyGmeContext } */
typedef struct
{
  PP_Instance instance;
  SaltyGmeContext context;
} InstanceContextMap;

#define MAX_INSTANCES 5
static InstanceContextMap g_instanceContextMap[MAX_INSTANCES];
static int g_instanceContextMapSize = 0;

static PP_Bool InitContext(PP_Instance instance)
{
  InstanceContextMap *mapEntry;
  SaltyGmeContext *cxt;

  if (g_instanceContextMapSize >= MAX_INSTANCES)
    return PP_FALSE;

  mapEntry = &g_instanceContextMap[g_instanceContextMapSize++];
  mapEntry->instance = instance;
  cxt = &mapEntry->context;

  cxt->trackCount = 1;
  cxt->networkBuffer = NULL;
  cxt->networkBufferSize = 0;
  cxt->networkBufferPtr = 0;
  cxt->isPlaying = 0;
  cxt->startPlaying = 0;
  cxt->songLoaded = PP_FALSE;
  cxt->instance = instance;
  cxt->audioStart = 0;
  cxt->audioEnd = 0;

  cxt->r = cxt->g = cxt->b = 250;
  cxt->rInc = -2;
  cxt->gInc = -3;
  cxt->bInc = -4;

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

  pthread_mutex_lock(&cxt->audioMutex);

  /* if there is no audio ready to feed, leave */
  if (cxt->audioStart >= cxt->audioEnd)
  {
    pthread_mutex_unlock(&cxt->audioMutex);
    return;
  }

  bufferStartInBytes = (cxt->audioStart % BUFFER_SIZE) * 2;
  bufferEndInBytes = (cxt->audioEnd % BUFFER_SIZE ) * 2;

  /* decide how much to feed */
  if (bufferStartInBytes > bufferEndInBytes)
    actualLen = BUFFER_SIZE * 2 - bufferStartInBytes;
  else
    actualLen = bufferEndInBytes - bufferStartInBytes;
  if (actualLen > reqLenInBytes)
    actualLen = reqLenInBytes;

  /* feed it */
  memcpy(samples, &cxt->audioBuffer[bufferStartInBytes / 2], actualLen);
  cxt->audioStart += actualLen / 2;

  /* wrap-around case */
  if (actualLen < reqLenInBytes)
  {
    samples += actualLen;
    actualLen = reqLenInBytes - actualLen;
    bufferStartInBytes = (cxt->audioStart % BUFFER_SIZE) * 2;
    /* feed it */
    memcpy(samples, &cxt->audioBuffer[bufferStartInBytes / 2], actualLen);
    cxt->audioStart += actualLen / 2;
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
    return g_var_if->VarFromUtf8(module_id, str, strlen(str));
  return PP_MakeUndefined();
}

static void GraphicsFlushCallback(void* user_data, int32_t result)
{
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
  int samplesToGenerate;

  if (cxt->isPlaying || cxt->startPlaying)
  {
    /* check if it's time to generate more audio */
    pthread_mutex_lock(&cxt->audioMutex);
    if ((cxt->audioEnd - cxt->audioStart) < (cxt->sampleCount / 2))
    {
      if (((cxt->audioEnd + cxt->sampleCount) % BUFFER_SIZE) < (cxt->audioStart % BUFFER_SIZE))
      {
        /* this case handles wraparound in the audio buffer */

        /* before the wraparound */
        samplesToGenerate = BUFFER_SIZE - (cxt->audioEnd % BUFFER_SIZE);
        gme_play(cxt->emu, samplesToGenerate,
          &cxt->audioBuffer[cxt->audioEnd % BUFFER_SIZE]);
        cxt->audioEnd += samplesToGenerate;

        /* after the wraparound */
        samplesToGenerate = cxt->sampleCount - samplesToGenerate;
        gme_play(cxt->emu, samplesToGenerate,
          &cxt->audioBuffer[cxt->audioEnd % BUFFER_SIZE]);
        cxt->audioEnd += samplesToGenerate;
      }
      else
      {
        /* simple, non-wraparound case */
        gme_play(cxt->emu, cxt->sampleCount, &cxt->audioBuffer[cxt->audioEnd % BUFFER_SIZE]);
        cxt->audioEnd += cxt->sampleCount;
      }
    }
    pthread_mutex_unlock(&cxt->audioMutex);

    /* if playback is signaled, start playback and clear the signal */
    if (cxt->startPlaying)
    {
      ResetMillisecondsCount(cxt);
      cxt->frameCounter = 0;
      cxt->msToUpdateVideo = 0;  /* update video at relative MS tick 0 */
      g_audio_if->StartPlayback(cxt->audioHandle);
      cxt->startPlaying = 0;
      cxt->isPlaying = 1;
    }

    /* check if it's time to update the visualization */
    if (GetMillisecondsCount(cxt) >= cxt->msToUpdateVideo)
    {
      pixels = g_imagedata_if->Map(cxt->oscopeData);
      memset(pixels, 0, OSCOPE_WIDTH * OSCOPE_HEIGHT * sizeof(unsigned int));
      vizBuffer = &cxt->audioBuffer[(cxt->frameCounter % FRAME_RATE) * BUFFER_SIZE / FRAME_RATE];
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

      /* white bar in the middle */
      pixels = g_imagedata_if->Map(cxt->oscopeData);
      pixels += OSCOPE_WIDTH * OSCOPE_HEIGHT / 2;
      for (i = 0; i < OSCOPE_WIDTH; i++)
        *pixels++ = 0xFFFFFFFF;

      topLeft.x = 0;
      topLeft.y = 0;
      g_graphics2d_if->PaintImageData(cxt->graphics2d, cxt->oscopeData, &topLeft, NULL);
      g_graphics2d_if->Flush(cxt->graphics2d, flushCallback);

      cxt->msToUpdateVideo = ++cxt->frameCounter * 1000 / FRAME_RATE;
    }
  }

  g_core_if->CallOnMainThread(5, timerCallback, 0);
}

static void ReadCallback(void* user_data, int32_t result)
{
  SaltyGmeContext *cxt = (SaltyGmeContext*)user_data;
  struct PP_CompletionCallback readCallback = { ReadCallback, cxt };
  struct PP_CompletionCallback timerCallback = { TimerCallback, cxt };
  void *temp;
  struct PP_Var var_result;
  struct PP_Size graphics_size;
  enum xz_ret ret;
  struct xz_dec *xz;
  struct xz_buf buf;
  gme_err_t status;

  printf("ReadCallback(%p, %d)\n", user_data, result);
  
  /* data has been transferred to buffer; check if more is on the way */
  cxt->networkBufferPtr += result;
  if (result > 0)
  {
    /* is a bigger buffer needed? */
    if (cxt->networkBufferPtr >= cxt->networkBufferSize)
    {
        cxt->networkBufferSize += BUFFER_INCREMENT;
        temp = realloc(cxt->networkBuffer, cxt->networkBufferSize);
        if (!temp)
            printf("Help! memory problem!\n");
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
      buf.in = cxt->networkBuffer;
      buf.in_pos = 0;
      buf.in_size = cxt->networkBufferPtr;

      cxt->dataBufferSize = BUFFER_INCREMENT;
      cxt->dataBuffer = (unsigned char*)malloc(cxt->dataBufferSize);
      if (!cxt->dataBuffer)
      {
        var_result = AllocateVarFromCStr("songLoaded:0");
        return;
      }

      buf.out = cxt->dataBuffer;
      buf.out_pos = 0;
      buf.out_size = cxt->dataBufferSize;

      xz_crc32_init();
      xz = xz_dec_init(XZ_DYNALLOC, (uint32_t)-1);
      if (!xz)
      {
        var_result = AllocateVarFromCStr("songLoaded:0");
        return;
      }

      ret = xz_dec_run(xz, &buf);
      cxt->dataBufferPtr = buf.out_pos;
      free(cxt->networkBuffer);
    }
    else
    {
      cxt->dataBuffer = cxt->networkBuffer;
      cxt->dataBufferPtr = cxt->networkBufferPtr;
      cxt->networkBuffer = NULL;
    }

    status = gme_open_data(cxt->dataBuffer, cxt->dataBufferPtr, &cxt->emu, FREQUENCY);
    if (status)
    {
      /* signal the web page that the load failed */
      var_result = AllocateVarFromCStr("songLoaded:0");
      return;
    }

    /* song is ready, time to start playback */
    cxt->songLoaded = PP_TRUE;

    cxt->currentTrack = 0;
    cxt->trackCount = gme_track_count(cxt->emu);
    cxt->voiceCount = gme_voice_count(cxt->emu);
    status = gme_start_track(cxt->emu, cxt->currentTrack);
    status = gme_track_info(cxt->emu, &cxt->metadata, cxt->currentTrack);

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
    g_core_if->CallOnMainThread(0, timerCallback, 0);

    /* signal the web page that the load was successful */
    var_result = AllocateVarFromCStr("songLoaded:1");
    g_messaging_if->PostMessage(cxt->instance, var_result);
  }
}

/* This is called when the Open() call gets header data from server */
static void OpenComplete(void* user_data, int32_t result)
{
  SaltyGmeContext *cxt = (SaltyGmeContext*)user_data;
  struct PP_CompletionCallback readCallback = { ReadCallback, cxt };

  /* this probably needs to be more graceful, but this is a good first cut */
  if (result != 0)
    return;

  if (!cxt->networkBuffer)
  {
    cxt->networkBufferSize = BUFFER_INCREMENT;
    cxt->networkBuffer = (unsigned char*)malloc(cxt->networkBufferSize);
  }
  g_urlloader_if->ReadResponseBody(cxt->songLoader, 
    cxt->networkBuffer, cxt->networkBufferSize, readCallback);
}

static PP_Bool Instance_DidCreate(PP_Instance instance,
                                  uint32_t argc,
                                  const char* argn[],
                                  const char* argv[]) {
  int i;
  PP_Resource songRequest;
  struct PP_Var urlProperty;
  struct PP_Var getVar = AllocateVarFromCStr("GET");
  struct PP_CompletionCallback OpenCallback;
  int32_t ret;
  SaltyGmeContext *cxt;

  if (!InitContext(instance))
    return PP_FALSE;
  cxt = GetContext(instance);

  for (i = 0; i < argc; i++)
  {
    if (strcmp(argn[i], "songurl") == 0)
      urlProperty = AllocateVarFromCStr(argv[i]);
    else if (strcmp(argn[i], "autoplay"))
      cxt->startPlaying = 1;
  }

  /* prepare audio interface */
  cxt->sampleCount = g_audioconfig_if->RecommendSampleFrameCount(FREQUENCY, 4096);
  cxt->audioConfig = g_audioconfig_if->CreateStereo16Bit(instance, FREQUENCY, cxt->sampleCount);
  cxt->sampleCount *= 2;

  if (!cxt->audioConfig)
    return PP_FALSE;
  cxt->audioHandle = g_audio_if->Create(instance, cxt->audioConfig, AudioCallback, cxt);
  if (!cxt->audioHandle)
    return PP_FALSE;

  /* obtained song URL; now load it */
  cxt->songLoader = g_urlloader_if->Create(instance);

  songRequest = g_urlrequestinfo_if->Create(instance);
  g_urlrequestinfo_if->SetProperty(songRequest, PP_URLREQUESTPROPERTY_URL, urlProperty);
  g_urlrequestinfo_if->SetProperty(songRequest, PP_URLREQUESTPROPERTY_METHOD, getVar);

  OpenCallback.func = OpenComplete;
  OpenCallback.user_data = cxt;
  ret = g_urlloader_if->Open(cxt->songLoader, songRequest, OpenCallback);

  return PP_TRUE;
}

static void Instance_DidDestroy(PP_Instance instance) {
  SaltyGmeContext *cxt;

  cxt = GetContext(instance);

  pthread_mutex_destroy(&cxt->audioMutex);
}

static void Instance_DidChangeView(PP_Instance instance,
                                   const struct PP_Rect* position,
                                   const struct PP_Rect* clip) {
}

static void Instance_DidChangeFocus(PP_Instance instance,
                                    PP_Bool has_focus) {
}

static PP_Bool Instance_HandleDocumentLoad(PP_Instance instance,
                                           PP_Resource url_loader) {
  /* NaCl modules do not need to handle the document load function. */
  return PP_FALSE;
}

void Messaging_HandleMessage(PP_Instance instance, struct PP_Var var_message)
{
  char* message;
  struct PP_Var var_result;
  char result_string[MAX_RESULT_STR_LEN];
  SaltyGmeContext *cxt;

  if (var_message.type != PP_VARTYPE_STRING) {
    /* Only handle string messages */
    return;
  }

  cxt = GetContext(instance);

  var_result = PP_MakeUndefined();
  message = AllocateCStrFromVar(var_message);
  if (strncmp(message, kNextTrackId, strlen(kNextTrackId)) == 0)
  {
    printf("next track\n");
    if (gme_track_count(cxt->emu) < 1)
      return;
    cxt->currentTrack = (cxt->currentTrack + 1) % cxt->trackCount;
    gme_start_track(cxt->emu, cxt->currentTrack);
    snprintf(result_string, MAX_RESULT_STR_LEN, "currentTrack:%d", cxt->currentTrack + 1);
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kPrevTrackId, strlen(kPrevTrackId)) == 0)
  {
    printf("previous track\n");
    if (gme_track_count(cxt->emu) < 1)
      return;
    cxt->currentTrack = (cxt->currentTrack - 1);
    if (cxt->currentTrack < 0)
      cxt->currentTrack = cxt->trackCount - 1;
    gme_start_track(cxt->emu, cxt->currentTrack);
    snprintf(result_string, MAX_RESULT_STR_LEN, "currentTrack:%d", cxt->currentTrack + 1);
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kStartPlaybackId, strlen(kStartPlaybackId)) == 0)
  {
    printf("starting playback\n");
    cxt->startPlaying = 1;
  }
  else if (strncmp(message, kStopPlaybackId, strlen(kStopPlaybackId)) == 0)
  {
    printf("stopping playback\n");
    g_audio_if->StopPlayback(cxt->audioHandle);
    cxt->isPlaying = 0;
  }
  else if (strncmp(message, kTrackCountId, strlen(kTrackCountId)) == 0)
  {
    printf("getting track count\n");
    snprintf(result_string, MAX_RESULT_STR_LEN, "trackCount:%d", cxt->trackCount);
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kCurrentTrackId, strlen(kCurrentTrackId)) == 0)
  {
    printf("getting current track number\n");
    snprintf(result_string, MAX_RESULT_STR_LEN, "currentTrack:%d", cxt->currentTrack + 1);
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kVoiceCountId, strlen(kVoiceCountId)) == 0)
  {
    printf("get voice count (%d)\n", cxt->voiceCount);
    snprintf(result_string, MAX_RESULT_STR_LEN, "voiceCount:%d", cxt->voiceCount);
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kVoiceNameId, strlen(kVoiceNameId)) == 0)
  {
    printf("getting voice name\n");
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
  g_var_if = (struct PPB_Var*)(get_browser_interface(PPB_VAR_INTERFACE));
printf("*** %s:%s:%d\n", __FILE__, __func__, __LINE__);

  /* load all the modules that will be required */
  g_audio_if =
      (struct PPB_Audio*)(get_browser_interface(PPB_AUDIO_INTERFACE));
  g_audioconfig_if =
      (struct PPB_AudioConfig*)(get_browser_interface(PPB_AUDIO_CONFIG_INTERFACE));
  g_core_if =
      (struct PPB_Core*)(get_browser_interface(PPB_CORE_INTERFACE));
  g_graphics2d_if =
      (struct PPB_Graphics2D*)(get_browser_interface(PPB_GRAPHICS_2D_INTERFACE));
  g_imagedata_if =
      (struct PPB_ImageData*)(get_browser_interface(PPB_IMAGEDATA_INTERFACE));
  g_instance_if =
      (struct PPB_Instance*)(get_browser_interface(PPB_INSTANCE_INTERFACE));
  g_messaging_if =
      (struct PPB_Messaging*)(get_browser_interface(PPB_MESSAGING_INTERFACE));
  g_urlloader_if =
      (struct PPB_URLLoader*)(get_browser_interface(PPB_URLLOADER_INTERFACE));
  g_urlrequestinfo_if =
      (struct PPB_URLRequestInfo*)(get_browser_interface(PPB_URLREQUESTINFO_INTERFACE));
  g_urlresponseinfo_if =
      (struct PPB_URLResponseInfo*)(get_browser_interface(PPB_URLRESPONSEINFO_INTERFACE));
  return PP_OK;
}

PP_EXPORT const void* PPP_GetInterface(const char* interface_name) {
  if (strcmp(interface_name, PPP_INSTANCE_INTERFACE) == 0) {
    static struct PPP_Instance instance_interface = {
      &Instance_DidCreate,
      &Instance_DidDestroy,
      &Instance_DidChangeView,
      &Instance_DidChangeFocus,
      &Instance_HandleDocumentLoad
    };
    return &instance_interface;
  } else if (strcmp(interface_name, PPP_MESSAGING_INTERFACE) == 0) {
    static struct PPP_Messaging g_messaging_if = {
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
