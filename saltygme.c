/** @file saltygme.c
 * This example demonstrates loading, running and scripting a very simple
 * NaCl module.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_audio.h"
#include "ppapi/c/ppb_audio_config.h"
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

static PP_Module module_id = 0;
static struct PPB_Audio *g_audio_if = NULL;
static struct PPB_AudioConfig *g_audioconfig_if = NULL;
static struct PPB_Graphics2D *g_graphics2d_if = NULL;
static struct PPB_ImageData *g_imagedata_if = NULL;
static struct PPB_Instance *g_instance_if = NULL;
static struct PPB_Messaging *g_messaging_if = NULL;
static struct PPB_URLLoader *g_urlloader_if = NULL;
static struct PPB_URLRequestInfo *g_urlrequestinfo_if = NULL;
const struct PPB_URLResponseInfo* g_urlresponseinfo_if = NULL;
static struct PPB_Var* g_var_if = NULL;

#define OSCOPE_WIDTH  512
#define OSCOPE_HEIGHT 256
#define FREQUENCY 44100
#define MAX_RESULT_STR_LEN 100
#define BUFFER_INCREMENT (1024 * 1024)

typedef struct
{
  /* keeping track of the instance */
  PP_Instance instance;

  /* network resource */
  PP_Resource songLoader;

  /* audio playback */
  PP_Resource audioConfig;
  PP_Resource audioHandle;
  int sampleCount;
  Music_Emu *emu;
  int currentTrack;
  int trackCount;
  gme_info_t *metadata;
  unsigned char *networkBuffer;
  int networkBufferSize;
  int networkBufferPtr;
  int isPlaying;
  int autoplay;
  PP_Bool songLoaded;
  int voiceCount;

  /* graphics */
  PP_Resource graphics2d;
  PP_Resource oscopeData;
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

  cxt->sampleCount = 4096;
  cxt->trackCount = 1;
  cxt->networkBuffer = NULL;
  cxt->networkBufferSize = 0;
  cxt->networkBufferPtr = 0;
  cxt->isPlaying = 0;
  cxt->autoplay = 0;
  cxt->songLoaded = PP_FALSE;
  cxt->instance = instance;

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

static void GmeCallback(void* samples, uint32_t num_bytes, void* user_data)
{
  SaltyGmeContext *cxt = (SaltyGmeContext*)user_data;
  short *short_samples = (short*)(samples);

  gme_play(cxt->emu, cxt->sampleCount * 2, short_samples);
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
/* TODO(sdk_user): 2. Uncomment this when you need it.  It is commented out so
 * that the compiler doesn't complain about unused functions.
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
/* TODO(sdk_user): 3. Uncomment this when you need it.  It is commented out so
 * that the compiler doesn't complain about unused functions.
 */
static struct PP_Var AllocateVarFromCStr(const char* str) {
  if (g_var_if != NULL)
    return g_var_if->VarFromUtf8(module_id, str, strlen(str));
  return PP_MakeUndefined();
}

static void GraphicsFlushCallback(void* user_data, int32_t result)
{
}

static void ReadCallback(void* user_data, int32_t result)
{
  SaltyGmeContext *cxt = (SaltyGmeContext*)user_data;
  struct PP_CompletionCallback readCallback = { ReadCallback, cxt };
  void *temp;
  struct PP_Var var_result;
  struct PP_Size graphics_size;

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
    gme_err_t status;
    status = gme_open_data(cxt->networkBuffer, cxt->networkBufferSize, &cxt->emu, FREQUENCY);
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
{
  int i;
  uint32_t *pixels = g_imagedata_if->Map(cxt->oscopeData);
  struct PP_CompletionCallback flushCallback = { GraphicsFlushCallback, cxt };

  pixels += 128 * OSCOPE_WIDTH;
  for (i = 0; i < OSCOPE_WIDTH; i++)
    *pixels++ = 0xFFFFFFFF;
  g_graphics2d_if->ReplaceContents(cxt->graphics2d, cxt->oscopeData);
  g_graphics2d_if->Flush(cxt->graphics2d, flushCallback);
}
    if (cxt->autoplay)
    {
      g_audio_if->StartPlayback(cxt->audioHandle);
      cxt->isPlaying = 1;
    }

    /* signal the web page that the load was successful */
    var_result = AllocateVarFromCStr("songLoaded:1");
    g_messaging_if->PostMessage(cxt->instance, var_result);
  }
}

/* This is called when the Open() call gets header data from server */
//static const char ContentLengthString[] = "Content-Length: ";
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
//printf("%s:%s:%d, network buffer is %d bytes large\n", __FILE__, __func__, __LINE__, g_networkBufferSize);
  g_urlloader_if->ReadResponseBody(cxt->songLoader, 
    cxt->networkBuffer, cxt->networkBufferSize, readCallback);
}

/**
 * Called when the NaCl module is instantiated on the web page. The identifier
 * of the new instance will be passed in as the first argument (this value is
 * generated by the browser and is an opaque handle).  This is called for each
 * instantiation of the NaCl module, which is each time the <embed> tag for
 * this module is encountered.
 *
 * If this function reports a failure (by returning @a PP_FALSE), the NaCl
 * module will be deleted and DidDestroy will be called.
 * @param[in] instance The identifier of the new instance representing this
 *     NaCl module.
 * @param[in] argc The number of arguments contained in @a argn and @a argv.
 * @param[in] argn An array of argument names.  These argument names are
 *     supplied in the <embed> tag, for example:
 *       <embed id="nacl_module" dimensions="2">
 *     will produce two arguments, one named "id" and one named "dimensions".
 * @param[in] argv An array of argument values.  These are the values of the
 *     arguments listed in the <embed> tag.  In the above example, there will
 *     be two elements in this array, "nacl_module" and "2".  The indices of
 *     these values match the indices of the corresponding names in @a argn.
 * @return @a PP_TRUE on success.
 */
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

printf("*** %s:%s:%d: Instance = 0x%X; module_id = 0x%X; %d arguments:\n", __FILE__, __func__, __LINE__, instance, module_id, argc);

  if (!InitContext(instance))
    return PP_FALSE;
  cxt = GetContext(instance);

  for (i = 0; i < argc; i++)
  {
    if (strcmp(argn[i], "songurl") == 0)
      urlProperty = AllocateVarFromCStr(argv[i]);
    else if (strcmp(argn[i], "autoplay"))
//      if (atoi(argv[i]))
        cxt->autoplay = 1;
  }

  /* prepare audio interface */
  cxt->sampleCount = g_audioconfig_if->RecommendSampleFrameCount(FREQUENCY, cxt->sampleCount);
  cxt->audioConfig = g_audioconfig_if->CreateStereo16Bit(instance, FREQUENCY, cxt->sampleCount);
  if (!cxt->audioConfig)
    return PP_FALSE;
  cxt->audioHandle = g_audio_if->Create(instance, cxt->audioConfig, GmeCallback, cxt);
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
if (ret != PP_OK_COMPLETIONPENDING)
  printf("%s:%s:%d, HELP! Open() call failed\n", __FILE__, __func__, __LINE__);

  return PP_TRUE;
}

/**
 * Called when the NaCl module is destroyed. This will always be called,
 * even if DidCreate returned failure. This routine should deallocate any data
 * associated with the instance.
 * @param[in] instance The identifier of the instance representing this NaCl
 *     module.
 */
static void Instance_DidDestroy(PP_Instance instance) {
printf("*** %s:%s:%d\n", __FILE__, __func__, __LINE__);
}

/**
 * Called when the position, the size, or the clip rect of the element in the
 * browser that corresponds to this NaCl module has changed.
 * @param[in] instance The identifier of the instance representing this NaCl
 *     module.
 * @param[in] position The location on the page of this NaCl module. This is
 *     relative to the top left corner of the viewport, which changes as the
 *     page is scrolled.
 * @param[in] clip The visible region of the NaCl module. This is relative to
 *     the top left of the plugin's coordinate system (not the page).  If the
 *     plugin is invisible, @a clip will be (0, 0, 0, 0).
 */
static void Instance_DidChangeView(PP_Instance instance,
                                   const struct PP_Rect* position,
                                   const struct PP_Rect* clip) {
printf("*** %s:%s:%d\n", __FILE__, __func__, __LINE__);
}

/**
 * Notification that the given NaCl module has gained or lost focus.
 * Having focus means that keyboard events will be sent to the NaCl module
 * represented by @a instance. A NaCl module's default condition is that it
 * will not have focus.
 *
 * Note: clicks on NaCl modules will give focus only if you handle the
 * click event. You signal if you handled it by returning @a true from
 * HandleInputEvent. Otherwise the browser will bubble the event and give
 * focus to the element on the page that actually did end up consuming it.
 * If you're not getting focus, check to make sure you're returning true from
 * the mouse click in HandleInputEvent.
 * @param[in] instance The identifier of the instance representing this NaCl
 *     module.
 * @param[in] has_focus Indicates whether this NaCl module gained or lost
 *     event focus.
 */
static void Instance_DidChangeFocus(PP_Instance instance,
                                    PP_Bool has_focus) {
printf("*** %s:%s:%d\n", __FILE__, __func__, __LINE__);
}

/**
 * Handler that gets called after a full-frame module is instantiated based on
 * registered MIME types.  This function is not called on NaCl modules.  This
 * function is essentially a place-holder for the required function pointer in
 * the PPP_Instance structure.
 * @param[in] instance The identifier of the instance representing this NaCl
 *     module.
 * @param[in] url_loader A PP_Resource an open PPB_URLLoader instance.
 * @return PP_FALSE.
 */
static PP_Bool Instance_HandleDocumentLoad(PP_Instance instance,
                                           PP_Resource url_loader) {
  /* NaCl modules do not need to handle the document load function. */
printf("*** %s:%s:%d\n", __FILE__, __func__, __LINE__);
  return PP_FALSE;
}


/**
 * Handler for messages coming in from the browser via postMessage.  The
 * @a var_message can contain anything: a JSON string; a string that encodes
 * method names and arguments; etc.  For example, you could use JSON.stringify
 * in the browser to create a message that contains a method name and some
 * parameters, something like this:
 *   var json_message = JSON.stringify({ "myMethod" : "3.14159" });
 *   nacl_module.postMessage(json_message);
 * On receipt of this message in @a var_message, you could parse the JSON to
 * retrieve the method name, match it to a function call, and then call it with
 * the parameter.
 * @param[in] instance The instance ID.
 * @param[in] message The contents, copied by value, of the message sent from
 *     browser via postMessage.
 */
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
    cxt->currentTrack = (cxt->currentTrack + 1) % cxt->trackCount;
    gme_start_track(cxt->emu, cxt->currentTrack);
    snprintf(result_string, MAX_RESULT_STR_LEN, "currentTrack:%d", cxt->currentTrack + 1);
    var_result = AllocateVarFromCStr(result_string);
  }
  else if (strncmp(message, kStartPlaybackId, strlen(kStartPlaybackId)) == 0)
  {
    printf("starting playback\n");
  }
  else if (strncmp(message, kStopPlaybackId, strlen(kStopPlaybackId)) == 0)
  {
    printf("stopping playback\n");
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

/**
 * Entry points for the module.
 * Initialize instance interface and scriptable object class.
 * @param[in] a_module_id Module ID
 * @param[in] get_browser_interface Pointer to PPB_GetInterface
 * @return PP_OK on success, any other value on failure.
 */
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

/**
 * Returns an interface pointer for the interface of the given name, or NULL
 * if the interface is not supported.
 * @param[in] interface_name name of the interface
 * @return pointer to the interface
 */
PP_EXPORT const void* PPP_GetInterface(const char* interface_name) {
printf("*** %s:%s:%d, %s\n", __FILE__, __func__, __LINE__, interface_name);
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
printf("*** %s:%s:%d\n", __FILE__, __func__, __LINE__);
}
