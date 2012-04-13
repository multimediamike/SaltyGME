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
static struct PPB_Messaging *g_messaging_if = NULL;
static struct PPB_URLLoader *g_urlloader_if = NULL;
static struct PPB_URLRequestInfo *g_urlrequestinfo_if = NULL;
const struct PPB_URLResponseInfo* g_urlresponseinfo_if = NULL;
static struct PPB_Var* g_var_if = NULL;

/* music playback matters */
static PP_Resource g_audioConfig;
static PP_Resource g_audioHandle;
static int g_sampleCount = 4096;
#define FREQUENCY 44100
static Music_Emu *g_emu;
static int g_currentTrack;
static int g_trackCount = 1;
static gme_info_t *g_metadata;
static unsigned char *g_networkBuffer = NULL;
#define BUFFER_INCREMENT (1024 * 1024)
static int g_networkBufferSize = 0;
static int g_networkBufferPtr = 0;
static int g_isPlaying = 0;
static int g_autoplay = 1;  /* assume autoplay unless explicitly disabled */
static PP_Bool g_songLoaded = PP_FALSE;
static const char **g_voiceNames = NULL;
int g_voiceCount;

static void GmeCallback(void* samples, uint32_t num_bytes, void* user_data)
{
  short *short_samples = (short*)(samples);

  gme_play(g_emu, g_sampleCount * 2, short_samples);
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

static void ReadComplete(void* user_data, int32_t result)
{
  PP_Resource songLoader = (PP_Resource)user_data;
  struct PP_CompletionCallback readCallback = { ReadComplete, (void*)songLoader };
  void *temp;

  printf("ReadComplete(%p, %d)\n", user_data, result);
  
  /* data has been transferred to buffer; check if more is on the way */
  g_networkBufferPtr += result;
  if (result > 0)
  {
    /* is a bigger buffer needed? */
    if (g_networkBufferPtr >= g_networkBufferSize)
    {
        g_networkBufferSize += BUFFER_INCREMENT;
        temp = realloc(g_networkBuffer, g_networkBufferSize);
        if (!temp)
            printf("Help! memory problem!\n");
        else
            g_networkBuffer = temp;
    }

    /* not all the data has arrived yet; read again */
    g_urlloader_if->ReadResponseBody(songLoader,
      &g_networkBuffer[g_networkBufferPtr],
      g_networkBufferSize - g_networkBufferPtr,
      readCallback);
  }
  else
  {
    /* song is ready, time to start playback */
    g_songLoaded = PP_TRUE;

    gme_err_t status;
#warning need to check error
    status = gme_open_data(g_networkBuffer, g_networkBufferSize, &g_emu, FREQUENCY);
    g_currentTrack = 0;
    g_trackCount = gme_track_count(g_emu);
    g_voiceCount = gme_voice_count(g_emu);
//    g_voiceNames = gme_voice_names(g_emu);
    status = gme_start_track(g_emu, g_currentTrack);
    gme_mute_voice(g_emu, 2, 1);
    status = gme_track_info(g_emu, &g_metadata, g_currentTrack);
    printf("%d tracks, %s, %s, %s, %s\n",
      gme_track_count(g_emu),
      g_metadata->system,
      g_metadata->game,
      g_metadata->song,
      g_metadata->author);

g_autoplay = 1;
    if (g_autoplay)
    {
      g_audio_if->StartPlayback(g_audioHandle);
      g_isPlaying = 1;
    }
  }
}

/* This is called when the Open() call gets header data from server */
//static const char ContentLengthString[] = "Content-Length: ";
static void OpenComplete(void* user_data, int32_t result)
{
  PP_Resource songLoader = (PP_Resource)user_data;
  struct PP_CompletionCallback readCallback = { ReadComplete, (void*)songLoader };

  printf("OpenComplete(%p, %d)\n", user_data, result);
  /* this probably needs to be more graceful, but this is a good first cut */
  if (result != 0)
    return;

  if (!g_networkBuffer)
  {
    g_networkBufferSize = BUFFER_INCREMENT;
    g_networkBuffer = (unsigned char*)malloc(g_networkBufferSize);
  }
printf("%s:%s:%d, network buffer is %d bytes large\n", __FILE__, __func__, __LINE__, g_networkBufferSize);
  g_urlloader_if->ReadResponseBody(songLoader, 
    g_networkBuffer, g_networkBufferSize, readCallback);
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
  PP_Resource songLoader;
  PP_Resource songRequest;
  struct PP_Var urlProperty;
  struct PP_Var getVar = AllocateVarFromCStr("GET");
  struct PP_CompletionCallback OpenCallback;
  int32_t ret;

printf("*** %s:%s:%d - %d arguments:\n", __FILE__, __func__, __LINE__, argc);

  for (i = 0; i < argc; i++)
  {
//    printf("argn[%d] = %s; argv[%d] = %s\n", i, argn[i], i, argv[i]);
    if (strcmp(argn[i], "songurl") == 0)
{
printf("  time to fetch %s\n", argv[i]);
      urlProperty = AllocateVarFromCStr(argv[i]);
}
  }

  /* obtained song URL; now load it */
  songLoader = g_urlloader_if->Create(instance);

  songRequest = g_urlrequestinfo_if->Create(instance);
  g_urlrequestinfo_if->SetProperty(songRequest, PP_URLREQUESTPROPERTY_URL, urlProperty);
  g_urlrequestinfo_if->SetProperty(songRequest, PP_URLREQUESTPROPERTY_METHOD, getVar);

  OpenCallback.func = OpenComplete;
  OpenCallback.user_data = (void*) songLoader;
  ret = g_urlloader_if->Open(songLoader, songRequest, OpenCallback);
if (ret != PP_OK_COMPLETIONPENDING)
  printf("%s:%s:%d, HELP! Open() call failed\n", __FILE__, __func__, __LINE__);

  /* prepare audio interface */
  g_sampleCount = g_audioconfig_if->RecommendSampleFrameCount(FREQUENCY, g_sampleCount);
  g_audioConfig = g_audioconfig_if->CreateStereo16Bit(instance, FREQUENCY, g_sampleCount);
  if (!g_audioConfig)
    return PP_FALSE;
  g_audioHandle = g_audio_if->Create(instance, g_audioConfig, GmeCallback, NULL);
  if (!g_audioHandle)
    return PP_FALSE;

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
void Messaging_HandleMessage(PP_Instance instance, struct PP_Var var_message) {
printf("*** %s:%s:%d\n", __FILE__, __func__, __LINE__);
  /* TODO(sdk_user): 1. Make this function handle the incoming message. */
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
