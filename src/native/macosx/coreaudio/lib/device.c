/*
 * Jitsi, the OpenSource Java VoIP and Instant Messaging client.
 *
 * Distributable under LGPL license.
 * See terms of license at gnu.org.
 */
#include "device.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CFString.h>
#include <pthread.h>
#include <stdio.h>

extern void maccoreaudio_log(
        const char * error_format,
        ...);

/**
 * Functions to list, access and modifies audio devices via coreaudio.
 *
 * @author Vincent Lucas
 */
const char* transportTypeAggregate = "Aggregate";
const char* transportTypeAirPlay = "AirPlay";
const char* transportTypeAutoAggregate = "Auto aggregate";
const char* transportTypeAVB = "AVB";
const char* transportTypeBlueTooth = "Bluetooth";
const char* transportTypeBuiltIn = "Built-in";
const char* transportTypeDisplayPort = "DisplayPort";
const char* transportTypeFireWire = "FireWire";
const char* transportTypeHDMI = "HDMI";
const char* transportTypePCI = "PCI";
const char* transportTypeThunderbolt = "Thunderbolt";
const char* transportTypeUnknown = "Unknown";
const char* transportTypeUSB = "USB";
const char* transportTypeVirtual = "Virtual";

/**
 * Private definition of functions,
 */

AudioDeviceID maccoreaudio_getDevice(
        const char * deviceUID);

AudioDeviceID maccoreaudio_getDeviceForSpecificScope(
        const char * deviceUID,
        UInt32 inputOutputScope);

char* maccoreaudio_getDeviceProperty(
        const char * deviceUID,
        AudioObjectPropertySelector propertySelector);

char* maccoreaudio_getAudioDeviceProperty(
        AudioDeviceID device,
        AudioObjectPropertySelector propertySelector);

OSStatus maccoreaudio_setDeviceVolume(
        const char * deviceUID,
        Float32 volume,
        UInt32 inputOutputScope);

Float32 maccoreaudio_getDeviceVolume(
        const char * deviceUID,
        UInt32 inputOutputScope);

OSStatus maccoreaudio_getChannelsForStereo(
        const char * deviceUID,
        UInt32 * channels);

int maccoreaudio_countChannels(
        const char * deviceUID,
        AudioObjectPropertyScope inputOutputScope);

static OSStatus maccoreaudio_devicesChangedCallback(
        AudioObjectID inObjectID,
        UInt32 inNumberAddresses,
        const AudioObjectPropertyAddress inAddresses[],
        void *inClientData);

OSStatus maccoreaudio_initConverter(
        const char * deviceUID,
        const AudioStreamBasicDescription * javaFormat,
        unsigned char isJavaFormatSource,
        AudioConverterRef * converter,
        double * conversionRatio);

inline UInt32 CalculateLPCMFlags (
        UInt32 inValidBitsPerChannel,
        UInt32 inTotalBitsPerChannel,
        bool inIsFloat,
        bool inIsBigEndian,
        bool inIsNonInterleaved);

inline void FillOutASBDForLPCM(
        AudioStreamBasicDescription * outASBD,
        Float64 inSampleRate,
        UInt32 inChannelsPerFrame,
        UInt32 inValidBitsPerChannel,
        UInt32 inTotalBitsPerChannel,
        bool inIsFloat,
        bool inIsBigEndian,
        bool inIsNonInterleaved);

char* maccoreaudio_getDefaultDeviceUID(
        UInt32 inputOutputScope);

maccoreaudio_stream * maccoreaudio_startStream(
        const char * deviceUID,
        void* callbackFunction,
        void* callbackObject,
        void* callbackMethod,
        void* readWriteFunction,
        unsigned char isJavaFormatSource,
        float sampleRate,
        UInt32 nbChannels,
        UInt32 bitsPerChannel,
        unsigned char isFloat,
        unsigned char isBigEndian,
        unsigned char isNonInterleaved);

OSStatus maccoreaudio_readInputStream(
        AudioDeviceID inDevice,
        const AudioTimeStamp* inNow,
        const AudioBufferList* inInputData,
        const AudioTimeStamp* inInputTime,
        AudioBufferList* outOutputData,
        const AudioTimeStamp* inOutputTime,
        void* inClientData);

OSStatus maccoreaudio_writeOutputStream(
        AudioDeviceID inDevice,
        const AudioTimeStamp* inNow,
        const AudioBufferList* inInputData,
        const AudioTimeStamp* inInputTime,
        AudioBufferList* outOutputData,
        const AudioTimeStamp* inOutputTime,
        void* inClientData);

OSStatus maccoreaudio_getStreamVirtualFormat(
        AudioStreamID stream,
        AudioStreamBasicDescription * format);

OSStatus
maccoreaudio_getDeviceFormat(
        const char * deviceUID,
        unsigned char isOutput,
        AudioStreamBasicDescription * deviceFormat);

OSStatus
maccoreaudio_getDeviceFormatDeprecated(
        const char * deviceUID,
        unsigned char isOutput,
        AudioStreamBasicDescription * deviceFormat);

void
maccoreaudio_getDefaultFormat(
        AudioStreamBasicDescription * deviceFormat);

/**
 * Do nothing: there is no need to initializes anything to get device
 * information on MacOsX.
 *
 * @return Always returns 0 (always works).
 */
int maccoreaudio_initDevices(void)
{
    return 0;
}

/**
 * Do nothing: there is no need to frees anything once getting device
 * information is finished on MacOsX.
 */
void maccoreaudio_freeDevices(void)
{
    // Nothing to do.
}

/**
 * Returns if the audio device is an input device.
 *
 * @param deviceUID The device UID.
 *
 * @return True if the given device identifier correspond to an input device.
 * False otherwise.
 */
int maccoreaudio_isInputDevice(
        const char * deviceUID)
{
    return (maccoreaudio_countChannels(
                deviceUID,
                kAudioDevicePropertyScopeInput) > 0);
}

/**
 * Returns if the audio device is an output device.
 *
 * @param deviceUID The device UID.
 *
 * @return True if the given device identifier correspond to an output device.
 * False otherwise.
 */
int maccoreaudio_isOutputDevice(
        const char * deviceUID)
{
    return (maccoreaudio_countChannels(
                deviceUID,
                kAudioDevicePropertyScopeOutput) > 0);
}

/**
 * Returns the audio device corresponding to the UID given in parameter. Or
 * kAudioObjectUnknown if the device is nonexistant or if anything as failed.
 *
 * @param deviceUID The device UID.
 *
 * @return The audio device corresponding to the UID given in parameter. Or
 * kAudioObjectUnknown if the device is nonexistant or if anything as failed.
 */
AudioDeviceID maccoreaudio_getDevice(
        const char * deviceUID)
{
    return maccoreaudio_getDeviceForSpecificScope(
            deviceUID,
            kAudioObjectPropertyScopeGlobal);
}

/**
 * Returns the audio device corresponding to the UID given in parameter for the
 * specified scope (global, input or output). Or kAudioObjectUnknown if the
 * device is nonexistant or if anything as failed.
 *
 * @param deviceUID The device UID.
 * @param inputOutputScope The scope to tell if this is an output or an input
 * device.
 *
 * @return The audio device corresponding to the UID given in parameter. Or
 * kAudioObjectUnknown if the device is nonexistant or if anything as failed.
 */
AudioDeviceID maccoreaudio_getDeviceForSpecificScope(
        const char * deviceUID,
        UInt32 inputOutputScope)
{
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size;

    // Converts the device UID into a ref.
    CFStringRef deviceUIDRef;
    if((deviceUIDRef = CFStringCreateWithCString(
            kCFAllocatorDefault,
            deviceUID,
            kCFStringEncodingASCII)) == NULL)
    {
        maccoreaudio_log(
                "maccoreaudio_getDevice (coreaudio/device.c): \
                    \n\tCFStringCreateWithCString for device %s",
                    deviceUID);
        return kAudioObjectUnknown;
    }

    // Gets the device corresponding to the given UID.
    AudioValueTranslation translation;
    translation.mInputData = &deviceUIDRef;
    translation.mInputDataSize = sizeof(deviceUIDRef);
    translation.mOutputData = &device;
    translation.mOutputDataSize = sizeof(device);
    size = sizeof(translation);
    address.mSelector = kAudioHardwarePropertyDeviceForUID;
    address.mScope = inputOutputScope;
    address.mElement = kAudioObjectPropertyElementMaster;

    if((err = AudioObjectGetPropertyData(
            kAudioObjectSystemObject,
            &address,
            0,
            NULL,
            &size,
            &translation)) != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getDevice (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData, err: %d for device %s",
                    ((int) err),
                    deviceUID);

        // Frees the allocated device UID ref.
        CFRelease(deviceUIDRef);

        return kAudioObjectUnknown;
    }

    // Frees the allocated device UID ref.
    CFRelease(deviceUIDRef);

    return device;
}

/**
 * Returns the default input device UID.
 *
 * @return The default input device UID. NULL if an error occurs.
 */
char* maccoreaudio_getDefaultInputDeviceUID(void)
{
    return maccoreaudio_getDefaultDeviceUID(kAudioDevicePropertyScopeInput);
}

/**
 * Returns the default output device UID.
 *
 * @return The default output device UID. NULL if an error occurs.
 */
char* maccoreaudio_getDefaultOutputDeviceUID(void)
{
    return maccoreaudio_getDefaultDeviceUID(kAudioDevicePropertyScopeOutput);
}

/**
 * Returns the default device UID for input or ouput.
 *
 * @param inputOutputScope The scope to tell if this is an output or an input
 * device.
 *
 * @return The default device UID for input or ouput. NULL if an error occurs.
 */
char* maccoreaudio_getDefaultDeviceUID(
        UInt32 inputOutputScope)
{
    OSStatus err = noErr;
    AudioDeviceID device;
    UInt32 size = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress address;
    char * deviceUID = NULL;

    if(inputOutputScope == kAudioDevicePropertyScopeInput)
    {
        address.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    }
    else
    {
        address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    }
    address.mScope = inputOutputScope;
    address.mElement = kAudioObjectPropertyElementMaster;

    if((err = AudioObjectGetPropertyData(
                    kAudioObjectSystemObject,
                    &address,
                    0,
                    NULL,
                    &size,
                    &device))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getDefaultDeviceUID (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData, err: %d",
                    ((int) err));
        return NULL;
    }

    if((deviceUID = maccoreaudio_getAudioDeviceProperty(
                    device,
                    kAudioDevicePropertyDeviceUID))
            == NULL)
    {
        maccoreaudio_log(
                "maccoreaudio_getDefaultDeviceUID (coreaudio/device.c): \
                    \n\tgetAudioDeviceProperty");
        return NULL;
    }

    return deviceUID;
}

/**
 * Returns the device name for the given device. Or NULL, if not available. The
 * returned string must be freed by the caller.
 *
 * @param device The device to get the name from.
 *
 * @return The device name for the given device. Or NULL, if not available. The
 * returned string must be freed by the caller.
 */
char* maccoreaudio_getDeviceName(
        const char * deviceUID)
{
    return maccoreaudio_getDeviceProperty(deviceUID, kAudioObjectPropertyName);
}

/**
 * Returns the device model identifier for the given device. Or NULL, if not
 * available. The returned string must be freed by the caller.
 *
 * @param device The device to get the name from.
 *
 * @return The device model identifier for the given device. Or NULL, if not
 * available. The returned string must be freed by the caller.
 */
char* maccoreaudio_getDeviceModelIdentifier(
        const char * deviceUID)
{
    return
        maccoreaudio_getDeviceProperty(deviceUID, kAudioDevicePropertyModelUID);
}

/**
 * Returns the requested device property for the given device UID. Or NULL, if
 * not available. The returned string must be freed by the caller.
 *
 * @param deviceUID The device identifier to get the property from.
 * @param propertySelector The property we want to retrieve.
 *
 * @return The requested device property for the given device UID. Or NULL, if
 * not available. The returned string must be freed by the caller.
 */
char* maccoreaudio_getDeviceProperty(
        const char * deviceUID,
        AudioObjectPropertySelector propertySelector)
{
    AudioDeviceID device;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceProperty (coreaudio/device.c): \
                    \n\tgetDevice: %s",
                    deviceUID);
        return NULL;
    }

    return maccoreaudio_getAudioDeviceProperty(device, propertySelector);
}

/**
 * Returns the requested device property for the given device. Or NULL, if not
 * available. The returned string must be freed by the caller.
 *
 * @param device The device to get the name from.
 * @param propertySelector The property we want to retrieve.
 *
 * @return The requested device property for the given device. Or NULL, if not
 * available. The returned string must be freed by the caller.
 */
char* maccoreaudio_getAudioDeviceProperty(
        AudioDeviceID device,
        AudioObjectPropertySelector propertySelector)
{
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;
    UInt32 size;

    // Gets the device property
    CFStringRef deviceProperty;
    size = sizeof(deviceProperty);
    address.mSelector = propertySelector;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = kAudioObjectPropertyElementMaster;

    if((err = AudioObjectGetPropertyData(
            device,
            &address,
            0,
            NULL,
            &size,
            &deviceProperty)) != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceProperty (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData, err: %d",
                ((int) err));
        return NULL;
    }

    // Converts the device property to ASCII.
    CFIndex devicePropertyLength = CFStringGetLength(deviceProperty) + 1;
    char * deviceASCIIProperty;
    // The caller of this function must free the string.
    if((deviceASCIIProperty
                = (char *) malloc(devicePropertyLength * sizeof(char)))
            == NULL)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_getDeviceProperty (coreaudio/device.c): \
                    \n\tmalloc",
                strerror(errno));
        return NULL;
    }
    if(CFStringGetCString(
                deviceProperty,
                deviceASCIIProperty,
                devicePropertyLength,
                kCFStringEncodingASCII))
    {
        return deviceASCIIProperty;
    }
    return NULL;
}

/**
 * Sets the input volume for a given device.
 *
 * @param device The device which volume must be changed.
 * @param volume The new volume of the device. This is a scalar value between
 * 0.0 and 1.0
 *
 * @return noErr if everything works well. Another value if an error has
 * occured.  
 */
OSStatus maccoreaudio_setInputDeviceVolume(
        const char * deviceUID,
        Float32 volume)
{
    return maccoreaudio_setDeviceVolume(
            deviceUID,
            volume,
            kAudioDevicePropertyScopeInput);
}

/**
 * Sets the output volume for a given device.
 *
 * @param device The device which volume must be changed.
 * @param volume The new volume of the device. This is a scalar value between
 * 0.0 and 1.0
 *
 * @return noErr if everything works well. Another value if an error has
 * occured.  
 */
OSStatus maccoreaudio_setOutputDeviceVolume(
        const char * deviceUID,
        Float32 volume)
{
    return maccoreaudio_setDeviceVolume(
            deviceUID,
            volume,
            kAudioDevicePropertyScopeOutput);
}

/**
 * Sets the input or output volume for a given device. This is an internal
 * (private) function and must only be called by setInputDeviceVolume or
 * setOutputDeviceVolume.
 *
 * @param device The device which volume must be changed.
 * @param volume The new volume of the device. This is a scalar value between
 * 0.0 and 1.0
 * @param inputOutputScope The scope to tell if this is an output or an input
 * device.
 *
 * @return noErr if everything works well. Another value if an error has
 * occured.  
 */
OSStatus maccoreaudio_setDeviceVolume(
        const char * deviceUID,
        Float32 volume,
        UInt32 inputOutputScope)
{
    AudioDeviceID device;
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;
    UInt32 size;
    UInt32 channels[2];

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_setDeviceVolume (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDevice (unknown device for UID: %s)",
                    deviceUID);
        return -1;
    }

    // get the input device stereo channels
    if((maccoreaudio_getChannelsForStereo(deviceUID, channels)) != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_setDeviceVolume (coreaudio/device.c): \
                    \n\tmaccoreaudio_getChannelsForStereo, err: %d \
                    for device %s",
                    ((int) err),
                    deviceUID);
        return err;
    }

    // Sets the volume
    size = sizeof(volume);
    address.mSelector = kAudioDevicePropertyVolumeScalar;
    address.mScope = inputOutputScope;
    int i;
    int elementsLength = 3;
    UInt32 elements[] =
    {
        // The global volume.
        kAudioObjectPropertyElementMaster,
        // The left channel.
        channels[0],
        // The right channel.
        channels[1]
    };

    // Applies the volume to the different elements of the device.
    for(i = 0; i < elementsLength; ++i)
    {
        address.mElement = elements[i];
        // Checks if this device volume can be set. If yes, then do so.
        if(AudioObjectHasProperty(device, &address))
        {
            if((err = AudioObjectSetPropertyData(
                    device,
                    &address,
                    0,
                    NULL,
                    size,
                    &volume)) != noErr)
            {
                maccoreaudio_log(
                        "maccoreaudio_setDeviceVolume (coreaudio/device.c): \
                            \n\tAudioObjectSetPropertyData, err: %d \
                            for device %s",
                            ((int) err),
                            deviceUID);
                return err;
            }
        }
    }

    return err;
}

/**
 * Gets the input volume for a given device.
 *
 * @param deviceUID The device UID to get volume from.
 *
 * @return The device volume as a scalar value between 0.0 and 1.0. Returns -1.0
 * if an error occurs.
 */
Float32 maccoreaudio_getInputDeviceVolume(
        const char * deviceUID)
{
    return maccoreaudio_getDeviceVolume(
            deviceUID,
            kAudioDevicePropertyScopeInput);
}

/**
 * Gets the output volume for a given device.
 *
 * @param deviceUID The device UID to get volume from.
 *
 * @return The device volume as a scalar value between 0.0 and 1.0. Returns -1.0
 * if an error occurs.
 */
Float32 maccoreaudio_getOutputDeviceVolume(
        const char * deviceUID)
{
    return maccoreaudio_getDeviceVolume(
            deviceUID,
            kAudioDevicePropertyScopeOutput);
}

/**
 * Gets the input or output volume for a given device. This is an internal
 * (private) function and must only be called by getInputDeviceVolume or
 * getOutputDeviceVolume.
 *
 * @param deviceUID The device UID to get volume from.
 * @param inputOutputScope The scope to tell if this is an output or an input
 * device.
 *
 * @return The device volume as a scalar value between 0.0 and 1.0. Returns -1.0
 * if an error occurs.
 */
Float32 maccoreaudio_getDeviceVolume(
        const char * deviceUID,
        UInt32 inputOutputScope)
{
    AudioDeviceID device;
    Float32 volume = -1.0;
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;
    UInt32 size;
    UInt32 channels[2];

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceVolume (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDevice: %s",
                    deviceUID);
        return -1.0;
    }

    // get the input device stereo channels
    if((maccoreaudio_getChannelsForStereo(deviceUID, channels)) != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceVolume (coreaudio/device.c): \
                    \n\tmaccoreaudio_getChannelsForStereo, err: %d \
                    for device %s",
                    ((int) err),
                    deviceUID);
        return -1.0;
    }

    // Sets the volume
    size = sizeof(volume);
    address.mSelector = kAudioDevicePropertyVolumeScalar;
    address.mScope = inputOutputScope;
    int i;
    int elementsLength = 3;
    UInt32 elements[] =
    {
        // The global volume.
        kAudioObjectPropertyElementMaster,
        // The left channel.
        channels[0],
        // The right channel.
        channels[1]
    };

    // Applies the volume to the different elements of the device.
    for(i = 0; i < elementsLength; ++i)
    {
        address.mElement = elements[i];
        // Checks if this device volume can be set. If yes, then do so.
        if(AudioObjectHasProperty(device, &address))
        {
            if((err = AudioObjectGetPropertyData(
                    device,
                    &address,
                    0,
                    NULL,
                    &size,
                    &volume)) != noErr)
            {
                maccoreaudio_log(
                        "maccoreaudio_getDeviceVolume (coreaudio/device.c): \
                            \n\tAudioObjectSetPropertyData, err: %d \
                            for device %s",
                            ((int) err),
                            deviceUID);
                return -1.0;
            }
        }
    }

    return volume;
}

/**
 * Sets the channels for stereo of a given device.
 *
 * @param deviceUID The device UID to get the channels from.
 * @param channels The channels to be filled in with the correct values. This
 * must be a 2 item length array.
 *
 * @return An OSStatus set to noErr if everything works well. Any other vlaue
 * otherwise.
 */
OSStatus maccoreaudio_getChannelsForStereo(
        const char * deviceUID,
        UInt32 * channels)
{
    AudioDeviceID device;
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;
    UInt32 size;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_getChannelsForStereo (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDevice: %s",
                    deviceUID);
        return -1;
    }

    // get the input device stereo channels
    address.mSelector = kAudioDevicePropertyPreferredChannelsForStereo;
    address.mScope = kAudioDevicePropertyScopeInput;
    address.mElement = kAudioObjectPropertyElementWildcard;
    size = sizeof(channels);
    if((err = AudioObjectGetPropertyData(
            device,
            &address,
            0,
            NULL,
            &size,
            channels)) != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getChannelsForStereo (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData, err: %d for device %s",
                    ((int) err),
                    deviceUID);
        return err;
    }

    return err;
}

/**
 * Returns the number of channels avaialable for input device.
 *
 * @param deviceUID The device UID to get the channels from.
 *
 * @return The number of channels avaialable for a given input device.
 * -1 if an error occurs.
 */
int maccoreaudio_countInputChannels(
        const char * deviceUID)
{
    return maccoreaudio_countChannels(
                deviceUID,
                kAudioDevicePropertyScopeInput);
}

/**
 * Returns the number of channels avaialable for output device.
 *
 * @param deviceUID The device UID to get the channels from.
 *
 * @return The number of channels avaialable for a given output device.
 * -1 if an error occurs.
 */
int maccoreaudio_countOutputChannels(
        const char * deviceUID)
{
    return maccoreaudio_countChannels(
                deviceUID,
                kAudioDevicePropertyScopeOutput);
}

/**
 * Returns the number of channels avaialable for a given input / output device.
 *
 * @param deviceUID The device UID to get the channels from.
 * @param inputOutputScope The scope to tell if this is an output or an input
 * device.
 *
 * @return The number of channels avaialable for a given input / output device.
 * -1 if an error occurs.
 */
int maccoreaudio_countChannels(
        const char * deviceUID,
        AudioObjectPropertyScope inputOutputScope)
{
    AudioDeviceID device;
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;
    UInt32 size;
    AudioBufferList *audioBufferList = NULL;
    int nbChannels = 0;
    int i;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_countChannels (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDevice: %s",
                    deviceUID);
        return -1;
    }

    // Gets the size of the streams for this device.
    address.mSelector = kAudioDevicePropertyStreamConfiguration;
    address.mScope = inputOutputScope;
    address.mElement = kAudioObjectPropertyElementWildcard; // 0
    if((err = AudioObjectGetPropertyDataSize(device, &address, 0, NULL, &size))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_countChannels (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyDataSize, err: %d \
                    for device %s",
                    ((int) err),
                    deviceUID);
        return -1;
    }

    // Gets the number of channels ofr each stream.
    if((audioBufferList = (AudioBufferList *) malloc(size)) == NULL)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_countChannels (coreaudio/device.c): \
                    \n\tmalloc",
                strerror(errno));
        return -1;
    }
    if((err = AudioObjectGetPropertyData(
                    device,
                    &address,
                    0,
                    NULL,
                    &size,
                    audioBufferList))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_countChannels (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData, err: %d for device %s",
                    ((int) err),
                    deviceUID);
        return -1;
    }
    for(i = 0; i < audioBufferList->mNumberBuffers; ++i)
    {
        nbChannels += audioBufferList->mBuffers[i].mNumberChannels;
    }
    free(audioBufferList);

    return nbChannels;
}

/**
 * Returns the nominal sample rate for the given device.
 *
 * @param deviceUID The device UID to get the channels from.
 *
 * @return The nominal sample rate for the given device. -1.0 if an error
 * occurs.
 */
Float64 maccoreaudio_getNominalSampleRate(
        const char * deviceUID)
{
    AudioDeviceID device;
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;
    UInt32 size;
    Float64 rate = -1.0;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_getNominalSampleRate (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDevice: %s",
                    deviceUID);
        return -1.0;
    }

    // Gets the sample rate.
    size = sizeof(Float64);
    address.mSelector = kAudioDevicePropertyNominalSampleRate;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = kAudioObjectPropertyElementMaster;

    if((err = AudioObjectGetPropertyData(
                    device,
                    &address,
                    0,
                    NULL,
                    &size,
                    &rate))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getNominalSampleRate (coreaudio/device.c): \
                    \n\tAudioObjactGetPropertyData, err: %d for device %s",
                    (int) err,
                    deviceUID);
        return -1.0;
    }

    return rate;
}

/**
 * Gets the minimal and maximal nominal sample rate for the given device.
 *
 * @param deviceUID The device UID to get the channels from.
 * @param minRate The minimal rate available for this device.
 * @param maxRate The maximal rate available for this device.
 *
 * @return noErr if everything is alright. -1.0 if an error occurs.
 */
OSStatus maccoreaudio_getAvailableNominalSampleRates(
        const char * deviceUID,
        Float64 * minRate,
        Float64 * maxRate)
{
    AudioDeviceID device;
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;
    UInt32 size;
    AudioValueRange minMaxRate;
    minMaxRate.mMinimum = -1.0;
    minMaxRate.mMaximum = -1.0;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_getAvailableNominalSampleRates \
                    (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDevice: %s",
                    deviceUID);
        return -1.0;
    }

    // Gets the available sample ratea.
    size = sizeof(AudioValueRange);
    address.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = kAudioObjectPropertyElementMaster;

    if((err = AudioObjectGetPropertyData(
                    device,
                    &address,
                    0,
                    NULL,
                    &size,
                    &minMaxRate))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getAvailableNominalSampleRates \
                    (coreaudio/device.c): \
                    \n\tAudioObjactGetPropertyData, err: %d for device %s",
                    (int) err,
                    deviceUID);
        return -1.0;
    }

    (*minRate) = minMaxRate.mMinimum;
    (*maxRate) = minMaxRate.mMaximum;

    return noErr;
}

/**
 * Lists the audio devices available and stores their UIDs in the provided
 * parameter.
 *
 * @param deviceUIDList A pointer which will be filled in with a list of device
 * UID strings. The caller is responsible to free this list and all the items.
 *
 * @return -1 in case of error. Otherwise, returns the number of devices stored
 * in the deviceUIDList.
 */
int maccoreaudio_getDeviceUIDList(
        char *** deviceUIDList)
{
    OSStatus err = noErr;
    UInt32 propsize;
    int nbDevices = -1;

    AudioObjectPropertyAddress address =
    {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };
    if((err = AudioObjectGetPropertyDataSize(
                    kAudioObjectSystemObject,
                    &address,
                    0,
                    NULL,
                    &propsize))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceUIDList (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyDataSize, err: %d",
                    ((int) err));
        return -1;
    }

    nbDevices = propsize / sizeof(AudioDeviceID);    
    AudioDeviceID *devices = NULL;
    if((devices = (AudioDeviceID*) malloc(nbDevices * sizeof(AudioDeviceID)))
            == NULL)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_getDeviceUIDList (coreaudio/device.c): \
                    \n\tmalloc",
                strerror(errno));
        return -1;
    }

    if((err = AudioObjectGetPropertyData(
                    kAudioObjectSystemObject,
                    &address,
                    0,
                    NULL,
                    &propsize,
                    devices))
            != noErr)
    {
        free(devices);
        maccoreaudio_log(
                "maccoreaudio_getDeviceUIDList (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData, err: %d",
                    ((int) err));
        return -1;
    }

    if(((*deviceUIDList) = (char**) malloc(nbDevices * sizeof(char*)))
            == NULL)
    {
        free(devices);
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_getDeviceUIDList (coreaudio/device.c): \
                    \n\tmalloc",
                strerror(errno));
        return -1;
    }

    int i;
    for(i = 0; i < nbDevices; ++i)
    {
        if(((*deviceUIDList)[i] = maccoreaudio_getAudioDeviceProperty(
                        devices[i],
                        kAudioDevicePropertyDeviceUID))
                == NULL)
        {
            int j;
            for(j = 0; j < i; ++j)
            {
                free((*deviceUIDList)[j]);
            }
            free(*deviceUIDList);
            free(devices);
            maccoreaudio_log(
                    "maccoreaudio_getDeviceUIDList (coreaudio/device.c): \
                    \n\tmaccoreaudio_getAudioDeviceProperty");
            return -1;
        }
    }

    free(devices);

    return nbDevices;
}
 
/**
 * Registers the listener for new plugged-in/out devices.
 */
void maccoreaudio_initializeHotplug(
        void* callbackFunction)
{
    AudioObjectPropertyAddress address =
    {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };

    if(AudioObjectAddPropertyListener(
                kAudioObjectSystemObject,
                &address,
                maccoreaudio_devicesChangedCallback,
                callbackFunction)
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_initializeHotplug (coreaudio/device.c): \
                    \n\tAudioObjectAddPropertyListener");
    }
}

/**
 * Unregisters the listener for new plugged-in/out devices.
 */
void maccoreaudio_uninitializeHotplug()
{
    AudioObjectPropertyAddress address =
    {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };

    if(AudioObjectRemovePropertyListener(
                kAudioObjectSystemObject,
                &address,
                maccoreaudio_devicesChangedCallback,
                NULL)
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_uninitializeHotplug (coreaudio/device.c): \
                    \n\tAudioObjectRemovePropertyListener");
    }
}

/**
 * The callback function called when a device is plugged-in/out.
 *
 * @param inObjectID The AudioObject whose properties have changed.
 * @param inNumberAddresses The number of elements in the inAddresses array.
 * @param inAddresses An array of AudioObjectPropertyAddresses indicating which
 * properties changed.
 * @param inClientData A pointer to client data established when the listener
 * proc was registered with the AudioObject.
 *
 * @return The return value is currently unused and should always be 0.
 */
static OSStatus maccoreaudio_devicesChangedCallback(
        AudioObjectID inObjectID,
        UInt32 inNumberAddresses,
        const AudioObjectPropertyAddress inAddresses[],
        void *inClientData)
{
    void (*callbackFunction) (void) = inClientData;
    callbackFunction();

    return noErr;
}

/**
 * Returns a string identifier of the device transport type.
 *
 * @param deviceUID The device UID to get the transport type from.
 *
 * @return The string identifier of the device transport type. Or NULL if
 * failed.
 */
const char* maccoreaudio_getTransportType(
        const char * deviceUID)
{
    AudioDeviceID device;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_getTransportType (coreaudio/device.c): \
                    \n\tgetDevice: %s",
                    deviceUID);
        return NULL;
    }
    // target device transport type property
    AudioObjectPropertyAddress address;
    address.mSelector = kAudioDevicePropertyTransportType;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = kAudioObjectPropertyElementMaster;

    OSStatus err;
    unsigned int transportType = 0;
    UInt32 size = sizeof(transportType);
    if((err = AudioObjectGetPropertyData(
                    device,
                    &address,
                    0,
                    NULL,
                    &size,
                    &transportType))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getTransportType (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData: err: 0x%x for device %s",
                    (int) err,
                    deviceUID);
        return NULL;
    }

    switch(transportType)
    {
        case kAudioDeviceTransportTypeAggregate:
            return transportTypeAggregate;
            break;
        case kAudioDeviceTransportTypeAirPlay:
            return transportTypeAirPlay;
            break;
        case kAudioDeviceTransportTypeAutoAggregate:
            return transportTypeAutoAggregate;
            break;
        case kAudioDeviceTransportTypeAVB:
            return transportTypeAVB;
            break;
        case kAudioDeviceTransportTypeBluetooth:
            return transportTypeBlueTooth;
            break;
        case kAudioDeviceTransportTypeBuiltIn:
            return transportTypeBuiltIn;
            break;
        case kAudioDeviceTransportTypeDisplayPort:
            return transportTypeDisplayPort;
            break;
        case kAudioDeviceTransportTypeFireWire:
            return transportTypeFireWire;
            break;
        case kAudioDeviceTransportTypeHDMI:
            return transportTypeHDMI;
            break;
        case kAudioDeviceTransportTypePCI:
            return transportTypePCI;
            break;
        case kAudioDeviceTransportTypeThunderbolt:
            return transportTypeThunderbolt;
            break;
        case kAudioDeviceTransportTypeUnknown:
            return transportTypeUnknown;
            break;
        case kAudioDeviceTransportTypeUSB:
            return transportTypeUSB;
            break;
        case kAudioDeviceTransportTypeVirtual:
            return transportTypeVirtual;
            break;
        default:
            maccoreaudio_log(
                    "maccoreaudio_getTransportType (coreaudio/device.c): \
                        \n\tNo transport type found for device %s",
                        deviceUID);
            return NULL;
            break;
    }
}

maccoreaudio_stream * maccoreaudio_startInputStream(
        const char * deviceUID,
        void* callbackFunction,
        void* callbackObject,
        void* callbackMethod,
        float sampleRate,
        UInt32 nbChannels,
        UInt32 bitsPerChannel,
        unsigned char isFloat,
        unsigned char isBigEndian,
        unsigned char isNonInterleaved)
{
    return maccoreaudio_startStream(
            deviceUID,
            callbackFunction,
            callbackObject,
            callbackMethod,
            maccoreaudio_readInputStream,
            false,
            sampleRate,
            nbChannels,
            bitsPerChannel,
            isFloat,
            isBigEndian,
            isNonInterleaved);
}

maccoreaudio_stream * maccoreaudio_startOutputStream(
        const char * deviceUID,
        void* callbackFunction,
        void* callbackObject,
        void* callbackMethod,
        float sampleRate,
        UInt32 nbChannels,
        UInt32 bitsPerChannel,
        unsigned char isFloat,
        unsigned char isBigEndian,
        unsigned char isNonInterleaved)
{
    return maccoreaudio_startStream(
            deviceUID,
            callbackFunction,
            callbackObject,
            callbackMethod,
            maccoreaudio_writeOutputStream,
            true,
            sampleRate,
            nbChannels,
            bitsPerChannel,
            isFloat,
            isBigEndian,
            isNonInterleaved);
}

/**
 * The the IO processing of a device.
 *
 * @param deviceUID The device UID to get the data from / to.
 * @param callbackFunction A function called 
 * @param readWriteFunction A function pointer called by the IO when data are
 * available for read / write.
 */
maccoreaudio_stream * maccoreaudio_startStream(
        const char * deviceUID,
        void* callbackFunction,
        void* callbackObject,
        void* callbackMethod,
        void* readWriteFunction,
        unsigned char isJavaFormatSource,
        float sampleRate,
        UInt32 nbChannels,
        UInt32 bitsPerChannel,
        unsigned char isFloat,
        unsigned char isBigEndian,
        unsigned char isNonInterleaved)
{
    AudioDeviceID device;
    OSStatus err = noErr;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_startStream (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDevice: %s",
                    deviceUID);
        return NULL;
    }

    // Init the stream structure.
    maccoreaudio_stream * stream;
    if((stream = (maccoreaudio_stream*) malloc(sizeof(maccoreaudio_stream)))
            == NULL)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_startStream (coreaudio/device.c): \
                    \n\tmalloc",
                strerror(errno));
        return NULL;
    }
    stream->ioProcId = NULL;
    stream->callbackFunction = callbackFunction;
    stream->callbackObject = callbackObject;
    stream->callbackMethod = callbackMethod;

    if(pthread_mutex_init(&stream->mutex, NULL) != 0)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_startStream (coreaudio/device.c): \
                    \n\tpthread_mutex_init",
                strerror(errno));
        free(stream);
        return NULL;
    }

    AudioStreamBasicDescription javaFormat;
    FillOutASBDForLPCM(
            &javaFormat,
            sampleRate,
            nbChannels,
            bitsPerChannel,
            bitsPerChannel,
            isFloat,
            isBigEndian,
            isNonInterleaved); 
    if((err = maccoreaudio_initConverter(
                deviceUID,
                &javaFormat,
                isJavaFormatSource,
                &stream->converter,
                &stream->conversionRatio))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_startStream (coreaudio/device.c): \
                    \n\tmaccoreaudio_initConverter: 0x%x for device %s",
                    (int) err,
                    deviceUID);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        return NULL;
    }

    //  register the IOProc
    if((err = AudioDeviceCreateIOProcID(
            device,
            readWriteFunction,
            stream,
            &stream->ioProcId)) != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_startStream (coreaudio/device.c): \
                    \n\tAudioDeviceIOProcID: 0x%x for device %s",
                    (int) err,
                    deviceUID);
        AudioConverterDispose(stream->converter);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        return NULL;
    }

    //  start IO
    if((err = AudioDeviceStart(device, stream->ioProcId)) != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_startStream (coreaudio/device.c): \
                    \n\tAudioDeviceStart: 0x%x for device %s",
                    (int) err,
                    deviceUID);
        AudioDeviceDestroyIOProcID(device, stream->ioProcId);
        AudioConverterDispose(stream->converter);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        return NULL;
    }

    return stream;
}

/**
 * Stops the stream for the given device.
 *
 * @param deviceUID The device identifier.
 * @param stream The stream to stop.
 */
void maccoreaudio_stopStream(
        const char * deviceUID,
        maccoreaudio_stream * stream)
{
    AudioDeviceID device;
    OSStatus err = noErr;

    if(pthread_mutex_lock(&stream->mutex) != 0)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_stopStream (coreaudio/device.c): \
                    \n\tpthread_mutex_lock",
                strerror(errno));
    }

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_stopStream (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDevice: %s",
                    deviceUID);
    }
    else
    {
        //  stop IO
        if((err = AudioDeviceStop(device, stream->ioProcId)) != noErr)
        {
            maccoreaudio_log(
                    "maccoreaudio_stopStream (coreaudio/device.c): \
                        \n\tAudioDeviceStop: 0x%x for device %s",
                        (int) err,
                        deviceUID);
        }
        //  unregister the IOProc
        if((err = AudioDeviceDestroyIOProcID(device, stream->ioProcId))
                != noErr)
        {
            maccoreaudio_log(
                    "maccoreaudio_stopStream (coreaudio/device.c): \
                        \n\tAudioDeviceDestroyIOProcID: 0x%x for device %s",
                        (int) err,
                        deviceUID);
        }
    }

    if((err = AudioConverterDispose(stream->converter)) != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_stopStream (coreaudio/device.c): \
                \n\tAudioConverterDispose: 0x%x for device %s",
                (int) err,
                deviceUID);
    }

    stream->ioProcId = 0;
    if(pthread_mutex_unlock(&stream->mutex) != 0)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_stopStream (coreaudio/device.c): \
                    \n\tpthread_mutex_unlock",
                strerror(errno));
    }
    if(pthread_mutex_destroy(&stream->mutex) != 0)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_stopStream (coreaudio/device.c): \
                    \n\tpthread_mutex_destroy",
                strerror(errno));
    }

    free(stream);
}

OSStatus maccoreaudio_readInputStream(
        AudioDeviceID inDevice,
        const AudioTimeStamp* inNow,
        const AudioBufferList* inInputData,
        const AudioTimeStamp* inInputTime,
        AudioBufferList* outOutputData,
        const AudioTimeStamp* inOutputTime,
        void* inClientData)
{
    OSStatus err = noErr;
    int error = 0;
    maccoreaudio_stream * stream = (maccoreaudio_stream*) inClientData;
    void (*callbackFunction) (char*, int, void*, void*)
        = stream->callbackFunction;
    UInt32 tmpLength
        = inInputData->mBuffers[0].mDataByteSize * stream->conversionRatio;
    char tmpBuffer[tmpLength];
    int i;

    if((error = pthread_mutex_trylock(&stream->mutex)) == 0)
    {
        if(stream->ioProcId != 0)
        {
            for(i = 0; i < inInputData->mNumberBuffers; ++i)
            {
                if(inInputData->mBuffers[i].mData != NULL
                        && inInputData->mBuffers[i].mDataByteSize > 0)
                {
                    if((err = AudioConverterConvertBuffer(
                                    stream->converter,
                                    inInputData->mBuffers[i].mDataByteSize,
                                    inInputData->mBuffers[i].mData,
                                    &tmpLength,
                                    tmpBuffer))
                            != noErr)
                    {
                        maccoreaudio_log(
                        "maccoreaudio_readInputStream (coreaudio/device.c): \
                                \n\tAudioConverterConvertBuffer: 0x%x",
                                (int) err);
                        pthread_mutex_unlock(&stream->mutex);
                        return err;
                    }

                    callbackFunction(
                            tmpBuffer,
                            tmpLength,
                            stream->callbackObject,
                            stream->callbackMethod);
                }
            }
        }
        if(pthread_mutex_unlock(&stream->mutex) != 0)
        {
            maccoreaudio_log(
                    "%s: %s\n",
                    "maccoreaudio_readInputStream (coreaudio/device.c): \
                        \n\tpthread_mutex_unlock",
                    strerror(errno));
        }
    }
    // If the error equals EBUSY, this means that the mutex is already locked by
    // the stop function.
    else if(error != EBUSY)
    {
        maccoreaudio_log(
                "%s: %s\n",
                "maccoreaudio_readInputStream (coreaudio/device.c): \
                    \n\tpthread_mutex_lock",
                strerror(errno));
    }

    return noErr;
}

OSStatus maccoreaudio_writeOutputStream(
        AudioDeviceID inDevice,
        const AudioTimeStamp* inNow,
        const AudioBufferList* inInputData,
        const AudioTimeStamp* inInputTime,
        AudioBufferList* outOutputData,
        const AudioTimeStamp* inOutputTime,
        void* inClientData)
{
    OSStatus err = noErr;
    int error = 0;

    maccoreaudio_stream * stream = (maccoreaudio_stream*) inClientData;
    void (*callbackFunction) (char*, int, void*, void*)
        = stream->callbackFunction;

    if(outOutputData->mNumberBuffers == 0)
    {
        return err;
    }

    int tmpLength
        = outOutputData->mBuffers[0].mDataByteSize * stream->conversionRatio;
    char tmpBuffer[tmpLength];

    if((error = pthread_mutex_trylock(&stream->mutex)) == 0)
    {
        if(stream->ioProcId != 0)
        {
            callbackFunction(
                    tmpBuffer,
                    tmpLength,
                    stream->callbackObject,
                    stream->callbackMethod);

            if((err = AudioConverterConvertBuffer(
                            stream->converter,
                            tmpLength,
                            tmpBuffer,
                            &outOutputData->mBuffers[0].mDataByteSize,
                            outOutputData->mBuffers[0].mData))
                    != noErr)
            {
                maccoreaudio_log(
                        "maccoreaudio_writeOutputStream (coreaudio/device.c): \
                        \n\tAudioConverterConvertBuffer: 0x%x", (int) err);
                memset(
                        outOutputData->mBuffers[0].mData,
                        0,
                        outOutputData->mBuffers[0].mDataByteSize);
                pthread_mutex_unlock(&stream->mutex);
                return err;
            }
        }
        if(pthread_mutex_unlock(&stream->mutex) != 0)
        {
            maccoreaudio_log(
                    "%s: %s\n",
                    "maccoreaudio_writeOutputStream (coreaudio/device.c): \
                        \n\tpthread_mutex_unlock",
                    strerror(errno));
        }
    }
    else
    {
        outOutputData->mBuffers[0].mDataByteSize = 0;

        // If the error equals EBUSY, this means that the mutex is already
        // locked by the stop function.
        if(error != EBUSY)
        {
            maccoreaudio_log(
                    "%s: %s\n",
                    "maccoreaudio_writeOutputStream (coreaudio/device.c): \
                        \n\tpthread_mutex_lock",
                    strerror(errno));
        }
    }

    // Copies the same data into the other buffers.
    int i;
    UInt32 length;
    for(i = 1; i < outOutputData->mNumberBuffers; ++i)
    {
        // Copies available data.
        length = outOutputData->mBuffers[i].mDataByteSize;
        if(length > outOutputData->mBuffers[0].mDataByteSize)
        {
            length = outOutputData->mBuffers[0].mDataByteSize;
        }
        memcpy(
                outOutputData->mBuffers[i].mData,
                outOutputData->mBuffers[0].mData,
                length);

        // Resets the resting buffer.
        if(outOutputData->mBuffers[i].mDataByteSize
                > outOutputData->mBuffers[0].mDataByteSize)
        {
            memset(
                    outOutputData->mBuffers[i].mData
                        + outOutputData->mBuffers[0].mDataByteSize,
                    0,
                    outOutputData->mBuffers[i].mDataByteSize
                        - outOutputData->mBuffers[0].mDataByteSize);
        }
    }

    return noErr;
}

/**
 * Returns the stream virtual format for a given stream.
 *
 * @param stream The stream to get the format.
 * @param format The variable to write the forat into.
 *
 * @return noErr if everything works fine. Any other value if failed.
 */
OSStatus maccoreaudio_getStreamVirtualFormat(
        AudioStreamID stream,
        AudioStreamBasicDescription * format)
{
    // Gets the audio format of the stream.
    OSStatus err = noErr;
    UInt32 size = sizeof(AudioStreamBasicDescription);
    AudioObjectPropertyAddress address;
    address.mSelector = kAudioStreamPropertyVirtualFormat;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = kAudioObjectPropertyElementMaster;
    if((err = AudioObjectGetPropertyData(
                    stream,
                    &address,
                    0,
                    NULL,
                    &size,
                    format))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getStreamVirtualFormat (coreaudio/device.c): \
                \n\tAudioObjectGetPropertyData, err: 0x%x",
                ((int) err));
        return err;
    }

    return err;
}

/**
 * Initializes a new audio converter to work between the given device and the
 * format description.
 *
 * @param deviceUID The device identifier.
 * @param javaFormat The format needed by the upper layer Java aplication.
 * @param isJavaFormatSource True if the Java format is the source of this
 * converter and the device the ouput. False otherwise.
 * @param converter A pointer to the converter used to store the new created
 * converter.
 *
 * @return noErr if everything works correctly. Any other vlue otherwise.
 */
OSStatus maccoreaudio_initConverter(
        const char * deviceUID,
        const AudioStreamBasicDescription * javaFormat,
        unsigned char isJavaFormatSource,
        AudioConverterRef * converter,
        double * conversionRatio)
{
    AudioStreamBasicDescription deviceFormat;
    OSStatus err = noErr;
    if((err = maccoreaudio_getDeviceFormat(
                    deviceUID,
                    isJavaFormatSource,
                    &deviceFormat))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_initConverter (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDeviceFormat for device: %s",
                    deviceUID);

        if((err = maccoreaudio_getDeviceFormatDeprecated(
                        deviceUID,
                        isJavaFormatSource,
                        &deviceFormat)) != noErr)
        {
            maccoreaudio_log(
                    "maccoreaudio_initConverter (coreaudio/device.c): \
                    \n\tmaccoreaudio_getDeviceFormatDeprecated \
                    for device: %s",
                    deviceUID);

            // Everything has failed to retrieve the device format, then try
            // with the default one.
            maccoreaudio_getDefaultFormat(&deviceFormat);
        }
    }

    const AudioStreamBasicDescription *inFormat = javaFormat;
    const AudioStreamBasicDescription *outFormat = &deviceFormat;
    if(!isJavaFormatSource)
    {
        inFormat = &deviceFormat;
        outFormat = javaFormat;
    }

    if((err = AudioConverterNew(inFormat, outFormat, converter))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_initConverter (coreaudio/device.c): \
                    \n\tAudioConverterNew, err: 0x%x",
                    ((int) err));
        return err;
    }

    *conversionRatio =
        ((double) javaFormat->mBytesPerFrame * javaFormat->mSampleRate)
            / ((double) deviceFormat.mBytesPerFrame * deviceFormat.mSampleRate);

    return err;
}

/**
 * Computes the value for the audio stream basic description mFormatFlags
 * field for linear PCM data. This function does not support specifying sample
 * formats that are either unsigned integer or low-aligned.
 *
 * @param inValidBitsPerChannel The number of valid bits in each sample.
 * @param inTotalBitsPerChannel The total number of bits in each sample.
 * @param inIsFloat Use true if the samples are represented with floating point
             numbers.
 * @param inIsBigEndian Use true if the samples are big endian.
 * @param inIsNonInterleaved Use true if the samples are noninterleaved.
 *
 * @return A UInt32 value containing the calculated format flags.
 */
inline UInt32 CalculateLPCMFlags (
        UInt32 inValidBitsPerChannel,
        UInt32 inTotalBitsPerChannel,
        bool inIsFloat,
        bool inIsBigEndian,
        bool inIsNonInterleaved)
{
    return
        (inIsFloat ? kAudioFormatFlagIsFloat : kAudioFormatFlagIsSignedInteger)
        | (inIsBigEndian ? ((UInt32)kAudioFormatFlagIsBigEndian) : 0)
        | ((!inIsFloat && (inValidBitsPerChannel == inTotalBitsPerChannel)) ?
                kAudioFormatFlagIsPacked : kAudioFormatFlagIsAlignedHigh)
        | (inIsNonInterleaved ? ((UInt32)kAudioFormatFlagIsNonInterleaved) : 0);
}

/**
 * Fills AudioStreamBasicDescription information.
 *
 * @param outASBD On output, a filled-out AudioStreamBasicDescription structure.
 * @param inSampleRate The number of sample frames per second of the data in the
 * stream.
 * @param inChannelsPerFrame The number of channels in each frame of data.
 * @param inValidBitsPerChannel The number of valid bits in each sample.
 * @param inTotalBitsPerChannel The total number of bits in each sample.
 * @param inIsFloat Use true if the samples are represented as floating-point
 * numbers.
 * @param inIsBigEndian Use true if the samples are big endian.
 * @param inIsNonInterleaved Use true if the samples are noninterleaved.
 */
inline void FillOutASBDForLPCM(
        AudioStreamBasicDescription * outASBD,
        Float64 inSampleRate,
        UInt32 inChannelsPerFrame,
        UInt32 inValidBitsPerChannel,
        UInt32 inTotalBitsPerChannel,
        bool inIsFloat,
        bool inIsBigEndian,
        bool inIsNonInterleaved)
{
    outASBD->mSampleRate = inSampleRate;
    outASBD->mFormatID = kAudioFormatLinearPCM;
    outASBD->mFormatFlags = CalculateLPCMFlags(
            inValidBitsPerChannel,
            inTotalBitsPerChannel,
            inIsFloat,
            inIsBigEndian,
            inIsNonInterleaved);
    outASBD->mBytesPerPacket =
        (inIsNonInterleaved ? 1 : inChannelsPerFrame) *
        (inTotalBitsPerChannel/8);
    outASBD->mFramesPerPacket = 1;
    outASBD->mBytesPerFrame =
        (inIsNonInterleaved ? 1 : inChannelsPerFrame) *
        (inTotalBitsPerChannel/8);
    outASBD->mChannelsPerFrame = inChannelsPerFrame;
    outASBD->mBitsPerChannel = inValidBitsPerChannel;
}

/**
 * Returns the device format.
 *
 * @param deviceUID The device identifier.
 * @param isOutput True if the device is an output device.
 * @param deviceFormat The structure to fill in with the device format.
 *
 * @return noErr if everything works fine. Any other value for an error.
 */
OSStatus
maccoreaudio_getDeviceFormat(
        const char * deviceUID,
        unsigned char isOutput,
        AudioStreamBasicDescription * deviceFormat)
{
    AudioDeviceID device;
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceFormat (coreaudio/device.c): \
                    \n\tgetDevice: %s",
                    deviceUID);
        return kAudioObjectUnknown;
    }

    AudioStreamID audioStreamIds[1];
    UInt32 size = sizeof(AudioStreamID *);
    address.mSelector = kAudioDevicePropertyStreams;
    if(isOutput)
    {
        address.mScope = kAudioDevicePropertyScopeOutput;
    }
    else
    {
        address.mScope = kAudioDevicePropertyScopeInput;
    }
    address.mElement = kAudioObjectPropertyElementMaster;
    if((err = AudioObjectGetPropertyData(
                    device,
                    &address,
                    0,
                    NULL,
                    &size,
                    &audioStreamIds))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceFormat (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData, err: 0x%x for device %s",
                    ((int) err),
                    deviceUID);
        return err;
    }

    if((err = maccoreaudio_getStreamVirtualFormat(
                    audioStreamIds[0],
                    deviceFormat))
                != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceFormat (coreaudio/device.c): \
                    \n\tmaccoreaudio_getStreamVirtualFormat, err: 0x%x\
                    for device %s",
                    ((int) err),
                    deviceUID);
        return err;
    }

    return err;
}

/**
 * Returns the device format using deprecated property.
 *
 * @param deviceUID The device identifier.
 * @param isOutput True if the device is an output device.
 * @param deviceFormat The structure to fill in with the device format.
 *
 * @return noErr if everything works fine. Any other value for an error.
 */
OSStatus
maccoreaudio_getDeviceFormatDeprecated(
        const char * deviceUID,
        unsigned char isOutput,
        AudioStreamBasicDescription * deviceFormat)
{
    AudioDeviceID device;
    OSStatus err = noErr;
    AudioObjectPropertyAddress address;

    // Gets the correspoding device
    if((device = maccoreaudio_getDevice(deviceUID)) == kAudioObjectUnknown)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceFormatDeprecated (coreaudio/device.c): \
                    \n\tgetDevice: %s",
                    deviceUID);
        return kAudioObjectUnknown;
    }

    UInt32 size = sizeof(AudioStreamBasicDescription);
    // This property ought to some day be deprecated.
    address.mSelector = kAudioDevicePropertyStreamFormat;
    if(isOutput)
    {
        address.mScope = kAudioDevicePropertyScopeOutput;
    }
    else
    {
        address.mScope = kAudioDevicePropertyScopeInput;
    }
    address.mElement = kAudioObjectPropertyElementMaster;

    if((err = AudioObjectGetPropertyData(
                    device,
                    &address,
                    0,
                    NULL,
                    &size,
                    deviceFormat))
            != noErr)
    {
        maccoreaudio_log(
                "maccoreaudio_getDeviceFormatDeprecated (coreaudio/device.c): \
                    \n\tAudioObjectGetPropertyData err: 0x%x for device %s",
                    ((int) err),
                    deviceUID);
    }

    return err;
}

/**
 * Returns the default format.
 *
 * @param deviceFormat The structure to fill in with the default format.
 */
void
maccoreaudio_getDefaultFormat(
        AudioStreamBasicDescription * deviceFormat)
{
    FillOutASBDForLPCM(
            deviceFormat,
            44100.0,
            2,
            8 * sizeof(AudioUnitSampleType),    // 32
            8 * sizeof(AudioUnitSampleType),    // 32
            true,
            false,
            false);
}
