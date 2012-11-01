#ifndef PLUGIN_API_H
#define PLUGIN_API_H

#include <inttypes.h>

#define MASTER_FREQUENCY 44100

typedef int (*InitPluginFunc)(void *context, uint8_t *data, int size);

/* start, play, and stop */
typedef int (*StartTrackFunc)(void *context, int trackNumber);
typedef int (*GenerateStereoFramesFunc)(void *context, int16_t *samples, int frameCount);

/* track management */
typedef int (*GetTrackCountFunc)(void *context);
typedef int (*GetCurrentTrackFunc)(void *context);
typedef int (*NextTrackFunc)(void *context);
typedef int (*PreviousTrackFunc)(void *context);

/* voice functions */
typedef int (*GetVoiceCountFunc)(void *context);
typedef const char* (*GetVoiceNameFunc)(void *context, int voiceNumber);
typedef int (*VoicesCanBeToggledFunc)(void *context);
typedef int (*SetVoiceStateFunc)(void *context, int voice, int enabled);

typedef struct
{
  InitPluginFunc           initPlugin;

  StartTrackFunc           startTrack;
  GenerateStereoFramesFunc generateStereoFrames;

  GetTrackCountFunc        getTrackCount;
  GetCurrentTrackFunc      getCurrentTrack;
  NextTrackFunc            nextTrack;
  PreviousTrackFunc        previousTrack;

  GetVoiceCountFunc        getVoiceCount;
  GetVoiceNameFunc         getVoiceName;
  VoicesCanBeToggledFunc   voicesCanBeToggled;
  SetVoiceStateFunc        setVoiceState;

  size_t                   contextSize;
} pluginInfo;

#endif  // PLUGIN_API_H

