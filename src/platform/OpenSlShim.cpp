#include "kudroid/platform/OpenSlShim.h"

#include "kudroid/platform/OpenSlAbi.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

extern "C" {
int32_t bionic_slEngineCreateAudioPlayer(void*, void**, const void*, const void*, uint32_t, const void*, const void*);
void bionic_slObjectDestroy(void*);
int32_t bionic_slAndroidSimpleBufferQueueEnqueue(void*, const void*, uint32_t);
int32_t bionic_slAndroidSimpleBufferQueueClear(void*);
int32_t bionic_slAndroidSimpleBufferQueueGetState(void*, uint32_t*);
int32_t bionic_slAndroidSimpleBufferQueueRegisterCallback(void*, void*, void*);
int32_t bionic_slPlaySetPlayState(void*, uint32_t);
int32_t bionic_slPlayGetPlayState(void*, uint32_t*);
int32_t bionic_slPlayGetPosition(void*, uint32_t*);
int32_t bionic_slVolumeSetVolumeLevel(void*, int32_t);
}

namespace {
// UUID values follow the Android OpenSL ES public ABI.
const SLInterfaceID_ engineId{0x8d97c260, 0xddd4, 0x11db, 0x958f, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID_ objectId{0x79216360, 0xddd7, 0x11db, 0xac16, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID_ playId{0xef0bd9c0, 0xddd7, 0x11db, 0xbf49, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID_ recordId{0xc5657aa0, 0xdddb, 0x11db, 0x82f7, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID_ volumeId{0x09e8ede0, 0xddde, 0x11db, 0xb4f6, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID_ queueId{0x2bc99cc0, 0xddd4, 0x11db, 0x8d99, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID_ androidQueueId{0x198e4940, 0xc5d7, 0x11df, 0xa2a6, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID_ configurationId{0x89f6a7e0, 0xbeac, 0x11df, 0x8b5c, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID_ outputMixId{0x97750f60, 0xddd7, 0x11db, 0x92b1, {0, 2, 0xa5, 0xd5, 0xc5, 0x1b}};
}

extern "C" {
const SLInterfaceID SL_IID_ENGINE = &engineId;
const SLInterfaceID SL_IID_OBJECT = &objectId;
const SLInterfaceID SL_IID_PLAY = &playId;
const SLInterfaceID SL_IID_RECORD = &recordId;
const SLInterfaceID SL_IID_VOLUME = &volumeId;
const SLInterfaceID SL_IID_BUFFERQUEUE = &queueId;
const SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE = &androidQueueId;
const SLInterfaceID SL_IID_ANDROIDCONFIGURATION = &configurationId;
const SLInterfaceID SL_IID_OUTPUTMIX = &outputMixId;
}

namespace {

enum class Kind { Engine, Player, Mix };
constexpr unsigned kQueue = 1, kVolume = 2, kConfiguration = 4;

const SLObjectItf_& objectTable();
const SLEngineItf_& engineTable();
const SLPlayItf_& playTable();
const SLVolumeItf_& volumeTable();
const SLAndroidSimpleBufferQueueItf_& queueTable();
const SLAndroidConfigurationItf_& configurationTable();

struct Object {
    const SLObjectItf_* object = &objectTable();
    const SLEngineItf_* engine = &engineTable();
    const SLPlayItf_* play = &playTable();
    const SLVolumeItf_* volume = &volumeTable();
    const SLAndroidSimpleBufferQueueItf_* queue = &queueTable();
    const SLAndroidConfigurationItf_* configuration = &configurationTable();
    Kind kind;
    std::atomic<SLuint32> state{SL_OBJECT_STATE_UNREALIZED};
    std::mutex mutex;
    void* backend = nullptr;
    unsigned interfaces = 0;
    SLuint32 queueCapacity = 0;
    SLuint32 queueInflight = 0;
    SLuint32 bytesPerFrame = 0;
    SLuint32 streamType = SL_ANDROID_STREAM_MEDIA;
    SLuint32 performance = SL_ANDROID_PERFORMANCE_NONE;
    SLmillibel level = 0;
    SLboolean mute = SL_BOOLEAN_FALSE;
    slAndroidSimpleBufferQueueCallback callback = nullptr;
    void* context = nullptr;
    std::vector<std::weak_ptr<Object>> children;
    explicit Object(Kind type) : kind(type) {}
};

std::mutex registryMutex;
std::unordered_map<const void*, std::shared_ptr<Object>> objects;

template<class Interface> std::shared_ptr<Object> find(Interface self) {
    std::lock_guard<std::mutex> lock(registryMutex);
    auto found = objects.find(self);
    return found == objects.end() ? nullptr : found->second;
}

void registerObject(const std::shared_ptr<Object>& object) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const void* handles[]{&object->object, &object->engine, &object->play,
                          &object->volume, &object->queue, &object->configuration};
    for (const void* handle : handles) {
        objects.emplace(handle, object);
    }
}

bool matches(SLInterfaceID left, SLInterfaceID right) {
    return left && right && std::memcmp(left, right, sizeof(SLInterfaceID_)) == 0;
}

unsigned playerInterface(SLInterfaceID iid) {
    if (matches(iid, SL_IID_BUFFERQUEUE) || matches(iid, SL_IID_ANDROIDSIMPLEBUFFERQUEUE)) return kQueue;
    if (matches(iid, SL_IID_VOLUME)) return kVolume;
    if (matches(iid, SL_IID_ANDROIDCONFIGURATION)) return kConfiguration;
    return 0;
}

SLresult requested(Kind kind, SLuint32 count, const SLInterfaceID* ids,
                   const SLboolean* required, unsigned& interfaces) {
    if (count && (!ids || !required)) return SL_RESULT_PARAMETER_INVALID;
    for (SLuint32 index = 0; index < count; ++index) {
        if (!ids[index]) return SL_RESULT_PARAMETER_INVALID;
        const unsigned bit = kind == Kind::Player ? playerInterface(ids[index]) : 0;
        const bool supported = bit || matches(ids[index], SL_IID_OBJECT) ||
                               (kind == Kind::Player && matches(ids[index], SL_IID_PLAY)) ||
                               (kind == Kind::Engine && matches(ids[index], SL_IID_ENGINE));
        if (!supported && required[index]) return SL_RESULT_FEATURE_UNSUPPORTED;
        interfaces |= bit;
    }
    return SL_RESULT_SUCCESS;
}

template<class... Args> SLresult unsupported(Args...) { return SL_RESULT_FEATURE_UNSUPPORTED; }

SLresult realize(SLObjectItf self, SLboolean async) {
    auto object = find(self);
    if (!object) return SL_RESULT_PARAMETER_INVALID;
    if (async) return SL_RESULT_FEATURE_UNSUPPORTED;
    SLuint32 expected = SL_OBJECT_STATE_UNREALIZED;
    return object->state.compare_exchange_strong(expected, SL_OBJECT_STATE_REALIZED)
               ? SL_RESULT_SUCCESS : SL_RESULT_PRECONDITIONS_VIOLATED;
}

SLresult getInterface(SLObjectItf self, SLInterfaceID iid, void* out) {
    if (!out || !iid) return SL_RESULT_PARAMETER_INVALID;
    *static_cast<const void**>(out) = nullptr;
    auto object = find(self);
    if (!object) return SL_RESULT_PARAMETER_INVALID;
    const bool configuration = matches(iid, SL_IID_ANDROIDCONFIGURATION);
    if (!configuration && object->state != SL_OBJECT_STATE_REALIZED) return SL_RESULT_PRECONDITIONS_VIOLATED;
    const void* handle = nullptr;
    if (matches(iid, SL_IID_OBJECT)) handle = &object->object;
    if (object->kind == Kind::Engine && matches(iid, SL_IID_ENGINE)) handle = &object->engine;
    if (object->kind == Kind::Player) {
        const unsigned bit = playerInterface(iid);
        if (matches(iid, SL_IID_PLAY)) handle = &object->play;
        else if (bit && (object->interfaces & bit)) {
            if (bit == kQueue) handle = &object->queue;
            if (bit == kVolume) handle = &object->volume;
            if (bit == kConfiguration) handle = &object->configuration;
        }
    }
    *static_cast<const void**>(out) = handle;
    return handle ? SL_RESULT_SUCCESS : SL_RESULT_FEATURE_UNSUPPORTED;
}

void destroy(SLObjectItf self) {
    auto object = find(self);
    if (!object) return;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        for (auto it = objects.begin(); it != objects.end();) {
            if (it->second == object) it = objects.erase(it);
            else ++it;
        }
    }
    std::vector<std::weak_ptr<Object>> children;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        children.swap(object->children);
    }
    for (auto& child : children) {
        if (auto current = child.lock()) destroy(&current->object);
    }
    if (object->backend) bionic_slObjectDestroy(object->backend);
}

void attach(const std::shared_ptr<Object>& engine, const std::shared_ptr<Object>& child) {
    std::lock_guard<std::mutex> lock(engine->mutex);
    engine->children.push_back(child);
    registerObject(child);
}

SLresult createPlayer(SLEngineItf self, SLObjectItf* out, SLDataSource* source, SLDataSink* sink,
                      SLuint32 count, const SLInterfaceID* ids, const SLboolean* required) {
    if (!out) return SL_RESULT_PARAMETER_INVALID;
    *out = nullptr;
    auto engine = find(self);
    if (!engine || engine->kind != Kind::Engine) return SL_RESULT_PARAMETER_INVALID;
    if (engine->state != SL_OBJECT_STATE_REALIZED) return SL_RESULT_PRECONDITIONS_VIOLATED;
    if (!source || !source->pLocator || !source->pFormat || !sink || !sink->pLocator) return SL_RESULT_PARAMETER_INVALID;
    const auto* locator = static_cast<const SLDataLocator_AndroidSimpleBufferQueue*>(source->pLocator);
    const auto* destination = static_cast<const SLDataLocator_OutputMix*>(sink->pLocator);
    if ((locator->locatorType != SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE &&
         locator->locatorType != SL_DATALOCATOR_BUFFERQUEUE) || destination->locatorType != SL_DATALOCATOR_OUTPUTMIX)
        return SL_RESULT_CONTENT_UNSUPPORTED;
    if (!locator->numBuffers) return SL_RESULT_PARAMETER_INVALID;
    auto mix = find(destination->outputMix);
    if (!mix || mix->kind != Kind::Mix || mix->state != SL_OBJECT_STATE_REALIZED) return SL_RESULT_PRECONDITIONS_VIOLATED;
    const auto* format = static_cast<const SLDataFormat_PCM*>(source->pFormat);
    if (format->formatType != SL_DATAFORMAT_PCM && format->formatType != SL_ANDROID_DATAFORMAT_PCM_EX)
        return SL_RESULT_CONTENT_UNSUPPORTED;
    if (!format->numChannels || format->numChannels > 8 || format->samplesPerSec < 8000000 ||
        format->samplesPerSec > 192000000 || format->bitsPerSample != format->containerSize ||
        (format->bitsPerSample != 8 && format->bitsPerSample != 16 && format->bitsPerSample != 32) ||
        format->endianness != SL_BYTEORDER_LITTLEENDIAN) return SL_RESULT_CONTENT_UNSUPPORTED;
    if (format->formatType == SL_ANDROID_DATAFORMAT_PCM_EX) {
        const auto representation = static_cast<const SLAndroidDataFormat_PCM_EX*>(source->pFormat)->representation;
        if (!((representation == SL_ANDROID_PCM_REPRESENTATION_FLOAT && format->bitsPerSample == 32) ||
              (representation == SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT && format->bitsPerSample != 8) ||
              (representation == SL_ANDROID_PCM_REPRESENTATION_UNSIGNED_INT && format->bitsPerSample == 8)))
            return SL_RESULT_CONTENT_UNSUPPORTED;
    }
    auto player = std::make_shared<Object>(Kind::Player);
    auto result = requested(Kind::Player, count, ids, required, player->interfaces);
    if (result != SL_RESULT_SUCCESS) return result;
    result = bionic_slEngineCreateAudioPlayer(nullptr, &player->backend, source, sink, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) return result;
    player->queueCapacity = locator->numBuffers;
    player->bytesPerFrame = format->numChannels * (format->containerSize / 8);
    attach(engine, player);
    *out = &player->object;
    return SL_RESULT_SUCCESS;
}

SLresult createMix(SLEngineItf self, SLObjectItf* out, SLuint32 count,
                   const SLInterfaceID* ids, const SLboolean* required) {
    if (!out) return SL_RESULT_PARAMETER_INVALID;
    *out = nullptr;
    auto engine = find(self);
    if (!engine || engine->kind != Kind::Engine) return SL_RESULT_PARAMETER_INVALID;
    if (engine->state != SL_OBJECT_STATE_REALIZED) return SL_RESULT_PRECONDITIONS_VIOLATED;
    auto mix = std::make_shared<Object>(Kind::Mix);
    const auto result = requested(Kind::Mix, count, ids, required, mix->interfaces);
    if (result != SL_RESULT_SUCCESS) return result;
    attach(engine, mix);
    *out = &mix->object;
    return SL_RESULT_SUCCESS;
}

void completed(void*, void* context) {
    // Look up the handle before dereferencing callback state after destruction.
    auto object = find(context);
    if (!object) return;
    slAndroidSimpleBufferQueueCallback callback;
    void* user;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        callback = object->callback;
        user = object->context;
    }
    if (callback) callback(&object->queue, user);
}

SLresult registerCallback(SLAndroidSimpleBufferQueueItf self, slAndroidSimpleBufferQueueCallback callback, void* context) {
    auto object = find(self);
    if (!object) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    object->callback = callback;
    object->context = context;
    return bionic_slAndroidSimpleBufferQueueRegisterCallback(object->backend,
               callback ? reinterpret_cast<void*>(&completed) : nullptr, const_cast<void*>(static_cast<const void*>(self)));
}

SLresult setLevel(SLVolumeItf self, SLmillibel level) {
    auto object = find(self);
    if (!object || level < SL_MILLIBEL_MIN || level > 0) return SL_RESULT_PARAMETER_INVALID;
    SLmillibel applied;
    {
        std::lock_guard<std::mutex> lock(object->mutex);
        object->level = level;
        applied = object->mute ? SL_MILLIBEL_MIN : level;
    }
    return bionic_slVolumeSetVolumeLevel(object->backend, applied);
}

SLresult configurationValue(SLAndroidConfigurationItf self, const SLchar* key, SLuint32* size, void* value, bool set) {
    auto object = find(self);
    if (!object || !key || !size || (set && !value)) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(object->mutex);
    SLuint32* property = nullptr;
    if (std::strcmp(reinterpret_cast<const char*>(key), "androidPlaybackStreamType") == 0) property = &object->streamType;
    if (std::strcmp(reinterpret_cast<const char*>(key), "androidPerformanceMode") == 0) property = &object->performance;
    if (!property) return SL_RESULT_FEATURE_UNSUPPORTED;
    if (set) {
        if (object->state != SL_OBJECT_STATE_UNREALIZED) return SL_RESULT_PRECONDITIONS_VIOLATED;
        if (*size != sizeof(SLuint32)) return SL_RESULT_PARAMETER_INVALID;
        SLuint32 input;
        std::memcpy(&input, value, sizeof(input));
        if (property == &object->streamType ? input > SL_ANDROID_STREAM_NOTIFICATION : input > SL_ANDROID_PERFORMANCE_POWER_SAVING)
            return SL_RESULT_PARAMETER_INVALID;
        *property = input;
    } else {
        const SLuint32 available = *size;
        *size = sizeof(SLuint32);
        if (value && available < sizeof(SLuint32)) return SL_RESULT_BUFFER_INSUFFICIENT;
        if (value) std::memcpy(value, property, sizeof(SLuint32));
    }
    return SL_RESULT_SUCCESS;
}

const SLObjectItf_& objectTable() {
    static const SLObjectItf_ table = [] {
        SLObjectItf_ table{};
        table.Realize = &realize;
        table.Resume = &unsupported;
        table.GetState = [](SLObjectItf self, SLuint32* state) -> SLresult {
            auto object = find(self);
            if (!object || !state) return SL_RESULT_PARAMETER_INVALID;
            *state = object->state.load();
            return SL_RESULT_SUCCESS;
        };
        table.GetInterface = &getInterface;
        table.RegisterCallback = &unsupported;
        table.AbortAsyncOperation = [](SLObjectItf) {};
        table.Destroy = &destroy;
        table.SetPriority = &unsupported;
        table.GetPriority = &unsupported;
        table.SetLossOfControlInterfaces = &unsupported;
        return table;
    }();
    return table;
}

const SLEngineItf_& engineTable() {
    static const SLEngineItf_ table = [] {
        SLEngineItf_ table{};
        table.CreateLEDDevice = &unsupported;
        table.CreateVibraDevice = &unsupported;
        table.CreateAudioPlayer = &createPlayer;
        table.CreateAudioRecorder = [](SLEngineItf, SLObjectItf* out, SLDataSource*, SLDataSink*, SLuint32,
                                        const SLInterfaceID*, const SLboolean*) -> SLresult {
            if (!out) return SL_RESULT_PARAMETER_INVALID;
            *out = nullptr;
            return SL_RESULT_FEATURE_UNSUPPORTED;
        };
        table.CreateMidiPlayer = &unsupported;
        table.CreateListener = &unsupported;
        table.Create3DGroup = &unsupported;
        table.CreateOutputMix = &createMix;
        table.CreateMetadataExtractor = &unsupported;
        table.CreateExtensionObject = &unsupported;
        table.QueryNumSupportedInterfaces = &unsupported;
        table.QuerySupportedInterfaces = &unsupported;
        table.QueryNumSupportedExtensions = [](SLEngineItf, SLuint32* count) -> SLresult {
            if (!count) return SL_RESULT_PARAMETER_INVALID;
            *count = 0;
            return SL_RESULT_SUCCESS;
        };
        table.QuerySupportedExtension = &unsupported;
        table.IsExtensionSupported = [](SLEngineItf, const SLchar* name, SLboolean* supported) -> SLresult {
            if (!name || !supported) return SL_RESULT_PARAMETER_INVALID;
            *supported = SL_BOOLEAN_FALSE;
            return SL_RESULT_SUCCESS;
        };
        return table;
    }();
    return table;
}

const SLPlayItf_& playTable() {
    static const SLPlayItf_ table = [] {
        SLPlayItf_ table{};
        table.SetPlayState = [](SLPlayItf self, SLuint32 state) -> SLresult {
            auto object = find(self);
            return object ? bionic_slPlaySetPlayState(object->backend, state) : SL_RESULT_PARAMETER_INVALID;
        };
        table.GetPlayState = [](SLPlayItf self, SLuint32* state) -> SLresult {
            auto object = find(self);
            if (!object || !state) return SL_RESULT_PARAMETER_INVALID;
            return bionic_slPlayGetPlayState(object->backend, state);
        };
        table.GetDuration = [](SLPlayItf, SLmillisecond* duration) -> SLresult {
            if (!duration) return SL_RESULT_PARAMETER_INVALID;
            *duration = SL_TIME_UNKNOWN;
            return SL_RESULT_SUCCESS;
        };
        table.GetPosition = [](SLPlayItf self, SLmillisecond* position) -> SLresult {
            auto object = find(self);
            if (!object || !position) return SL_RESULT_PARAMETER_INVALID;
            return bionic_slPlayGetPosition(object->backend, position);
        };
        table.RegisterCallback = &unsupported;
        table.SetCallbackEventsMask = [](SLPlayItf, SLuint32 mask) -> SLresult {
            return mask ? SL_RESULT_FEATURE_UNSUPPORTED : SL_RESULT_SUCCESS;
        };
        table.GetCallbackEventsMask = [](SLPlayItf, SLuint32* mask) -> SLresult {
            if (!mask) return SL_RESULT_PARAMETER_INVALID;
            *mask = 0;
            return SL_RESULT_SUCCESS;
        };
        table.SetMarkerPosition = &unsupported;
        table.ClearMarkerPosition = &unsupported;
        table.GetMarkerPosition = &unsupported;
        table.SetPositionUpdatePeriod = &unsupported;
        table.GetPositionUpdatePeriod = &unsupported;
        return table;
    }();
    return table;
}

const SLVolumeItf_& volumeTable() {
    static const SLVolumeItf_ table = [] {
        SLVolumeItf_ table{};
        table.SetVolumeLevel = &setLevel;
        table.GetVolumeLevel = [](SLVolumeItf self, SLmillibel* level) -> SLresult {
            auto object = find(self);
            if (!object || !level) return SL_RESULT_PARAMETER_INVALID;
            std::lock_guard<std::mutex> lock(object->mutex);
            *level = object->level;
            return SL_RESULT_SUCCESS;
        };
        table.GetMaxVolumeLevel = [](SLVolumeItf, SLmillibel* level) -> SLresult {
            if (!level) return SL_RESULT_PARAMETER_INVALID;
            *level = 0;
            return SL_RESULT_SUCCESS;
        };
        table.SetMute = [](SLVolumeItf self, SLboolean mute) -> SLresult {
            auto object = find(self);
            if (!object) return SL_RESULT_PARAMETER_INVALID;
            SLmillibel applied;
            {
                std::lock_guard<std::mutex> lock(object->mutex);
                object->mute = mute != SL_BOOLEAN_FALSE;
                applied = object->mute ? SL_MILLIBEL_MIN : object->level;
            }
            return bionic_slVolumeSetVolumeLevel(object->backend, applied);
        };
        table.GetMute = [](SLVolumeItf self, SLboolean* mute) -> SLresult {
            auto object = find(self);
            if (!object || !mute) return SL_RESULT_PARAMETER_INVALID;
            std::lock_guard<std::mutex> lock(object->mutex);
            *mute = object->mute;
            return SL_RESULT_SUCCESS;
        };
        table.EnableStereoPosition = &unsupported;
        table.IsEnabledStereoPosition = &unsupported;
        table.SetStereoPosition = &unsupported;
        table.GetStereoPosition = &unsupported;
        return table;
    }();
    return table;
}

const SLAndroidSimpleBufferQueueItf_& queueTable() {
    static const SLAndroidSimpleBufferQueueItf_ table = [] {
        SLAndroidSimpleBufferQueueItf_ table{};
        table.Enqueue = [](SLAndroidSimpleBufferQueueItf self, const void* data, SLuint32 size) -> SLresult {
            auto object = find(self);
            if (!object || !data || !size || size % object->bytesPerFrame) return SL_RESULT_PARAMETER_INVALID;
            {
                std::lock_guard<std::mutex> lock(object->mutex);
                SLuint32 state[2]{};
                const auto result = bionic_slAndroidSimpleBufferQueueGetState(object->backend, state);
                if (result != SL_RESULT_SUCCESS) return result;
                if (state[0] + object->queueInflight >= object->queueCapacity) return SL_RESULT_BUFFER_INSUFFICIENT;
                ++object->queueInflight;
            }
            const auto result = bionic_slAndroidSimpleBufferQueueEnqueue(object->backend, data, size);
            {
                std::lock_guard<std::mutex> lock(object->mutex);
                --object->queueInflight;
            }
            return result;
        };
        table.Clear = [](SLAndroidSimpleBufferQueueItf self) -> SLresult {
            auto object = find(self);
            return object ? bionic_slAndroidSimpleBufferQueueClear(object->backend) : SL_RESULT_PARAMETER_INVALID;
        };
        table.GetState = [](SLAndroidSimpleBufferQueueItf self, SLAndroidSimpleBufferQueueState* state) -> SLresult {
            auto object = find(self);
            if (!object || !state) return SL_RESULT_PARAMETER_INVALID;
            return bionic_slAndroidSimpleBufferQueueGetState(object->backend, &state->count);
        };
        table.RegisterCallback = &registerCallback;
        return table;
    }();
    return table;
}

const SLAndroidConfigurationItf_& configurationTable() {
    static const SLAndroidConfigurationItf_ table = [] {
        SLAndroidConfigurationItf_ table{};
        table.SetConfiguration = [](SLAndroidConfigurationItf self, const SLchar* key, const void* value, SLuint32 size) -> SLresult {
            return configurationValue(self, key, &size, const_cast<void*>(value), true);
        };
        table.GetConfiguration = [](SLAndroidConfigurationItf self, const SLchar* key, SLuint32* size, void* value) -> SLresult {
            return configurationValue(self, key, size, value, false);
        };
        table.AcquireJavaProxy = &unsupported;
        table.ReleaseJavaProxy = &unsupported;
        return table;
    }();
    return table;
}

SLresult createEngine(SLObjectItf* out, SLuint32 optionCount, const SLEngineOption* options,
                      SLuint32 count, const SLInterfaceID* ids, const SLboolean* required) {
    if (!out || (optionCount && !options)) return SL_RESULT_PARAMETER_INVALID;
    *out = nullptr;
    for (SLuint32 index = 0; index < optionCount; ++index) {
        if (options[index].feature != SL_ENGINEOPTION_THREADSAFE && options[index].feature != SL_ENGINEOPTION_LOSSOFCONTROL)
            return SL_RESULT_PARAMETER_INVALID;
    }
    auto object = std::make_shared<Object>(Kind::Engine);
    const auto result = requested(Kind::Engine, count, ids, required, object->interfaces);
    if (result != SL_RESULT_SUCCESS) return result;
    registerObject(object);
    *out = &object->object;
    return SL_RESULT_SUCCESS;
}

} // namespace

namespace kudroid {
const SymbolEntry* get_opensl_symbols(size_t* count) {
    static const SymbolEntry symbols[] = {
        {"slCreateEngine", reinterpret_cast<void*>(&createEngine)},
#define IID_ENTRY(name) {#name, const_cast<void*>(static_cast<const void*>(&name))}
        IID_ENTRY(SL_IID_ENGINE), IID_ENTRY(SL_IID_OBJECT), IID_ENTRY(SL_IID_PLAY),
        IID_ENTRY(SL_IID_RECORD), IID_ENTRY(SL_IID_VOLUME), IID_ENTRY(SL_IID_BUFFERQUEUE),
        IID_ENTRY(SL_IID_ANDROIDSIMPLEBUFFERQUEUE), IID_ENTRY(SL_IID_ANDROIDCONFIGURATION), IID_ENTRY(SL_IID_OUTPUTMIX),
#undef IID_ENTRY
    };
    if (count) *count = sizeof(symbols) / sizeof(symbols[0]);
    return symbols;
}
} // namespace kudroid
