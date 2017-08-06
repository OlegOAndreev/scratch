#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <SDL2/SDL.h>
#include <soundio/soundio.h>

using std::pair;
using std::unordered_map;
using std::vector;

enum class AudioBackend {
    SDL,
    Soundio
};

const int kNumChannels = 2;

AudioBackend audioBackend = AudioBackend::SDL;
int audioSampleRate = 48000;
int audioFrames = 1024;
SDL_AudioFormat audioFormat = AUDIO_S16;
bool audioDebug = false;
int outputWaveHz = 250;

vector<Uint8> samples;

Uint64 timeFreq;

unordered_map<Uint64, size_t> captureTimeHist;
unordered_map<Uint64, size_t> outputTimeHist;

template<typename ST>
void prepareSineWaveImpl(size_t numSamples, size_t startSample, int sampleRate, int channels, int waveHz,
                         ST low, ST high, ST* data)
{
    ST ampl = (high - low) / 2;
    ST base = low + ampl;
    double sampleRad = 2 * M_PI * waveHz / sampleRate;
    for (size_t i = 0; i < numSamples; i++) {
        ST value = base + ST(sin(sampleRad * (i + startSample)) * ampl);
        for (int j = 0; j < channels; j++) {
            data[i * channels + j] = value;
        }
    }
}

void prepareSineWave(int waveHz)
{
    // Fill the whole second with data.
    size_t numSamples = audioSampleRate;
    switch (audioFormat) {
        case AUDIO_S16:
            samples.resize(numSamples * sizeof(Sint16) * 2);
            prepareSineWaveImpl<Sint16>(numSamples, 0, audioSampleRate, kNumChannels, waveHz, -10000, 10000,
                                        (Sint16*)samples.data());
            break;
        case AUDIO_U16:
            samples.resize(numSamples * sizeof(Uint16) * 2);
            prepareSineWaveImpl<Uint16>(numSamples, 0, audioSampleRate, kNumChannels, waveHz, 0, 20000,
                                        (Uint16*)samples.data());
            break;
        case AUDIO_F32:
            samples.resize(numSamples * sizeof(float) * 2);
            prepareSineWaveImpl<float>(numSamples, 0, audioSampleRate, kNumChannels, waveHz, -0.3, 0.3,
                                       (float*)samples.data());
            break;
        default:
            printf("Format not implemented\n");
            exit(1);
    }
}

template<typename T>
size_t copyFromSamples(size_t pos, size_t len, T* dest)
{
    if (pos + len <= samples.size()) {
        memcpy(dest, samples.data() + pos, len);
        pos += len;
        if (pos == samples.size()) {
            pos = 0;
        }
    } else {
        size_t end = samples.size() - pos;
        memcpy(dest, samples.data() + pos, end);
        pos = len - end;
        memcpy(dest + end, samples.data(), pos);
    }
    return pos;
}

void SDLCALL sdlOutputCallback(void* /*userdata*/, Uint8* stream, int len)
{
    static size_t lastPos = 0;
    if ((size_t)len > samples.size()) {
        printf("Internal error: len > output size: %d > %d\n", len, (int)samples.size());
        return;
    }

    lastPos = copyFromSamples(lastPos, len, stream);

    static Uint64 lastOutputCounter = 0;
    if (audioDebug) {
        Uint64 newCounter = SDL_GetPerformanceCounter();
        if (lastOutputCounter > 0) {
            int deltaUs = (int)((newCounter - lastOutputCounter) * 1000 / timeFreq);
            int numSamples = len * 4 / SDL_AUDIO_BITSIZE(audioFormat);
            outputTimeHist[deltaUs]++;
            printf("SDL output %dms = %d samples\n", deltaUs, numSamples);
        }
        lastOutputCounter = newCounter;
    }
}

template<typename T>
void copySoundioFrames(int framesToWrite, size_t* lastPos, struct SoundIoChannelArea* areas)
{
    // Check if we've got the regular layout: interleaved channels with step = sizeof(T).
    size_t len = framesToWrite * sizeof(T) * kNumChannels;
    bool standardLayout = (areas[0].step == sizeof(T) * kNumChannels)
            && (areas[1].step == sizeof(T) * kNumChannels)
            && (areas[1].ptr - areas[0].ptr == sizeof(T))
            && (len <= samples.size());
    if (standardLayout) {
        *lastPos = copyFromSamples(*lastPos, len, areas[0].ptr);
    } else {
        size_t pos = *lastPos;
        for (int frame = 0; frame < framesToWrite; frame++) {
            for (int channel = 0; channel < kNumChannels; channel++) {
                memcpy(areas[channel].ptr, samples.data() + pos, sizeof(T));
                areas[channel].ptr += areas[channel].step;
                pos += sizeof(T);
                if (pos > samples.size()) {
                    pos = 0;
                }
            }
        }
        *lastPos = pos;
    }
}

void soundioWriteCallback(struct SoundIoOutStream* outstream, int /*frameCountMin*/, int frameCountMax)
{
    static size_t lastPos = 0;

    int framesLeft = frameCountMax;
    while (framesLeft > 0) {
        struct SoundIoChannelArea* areas;
        int framesToWrite = framesLeft;
        int err;
        if ((err = soundio_outstream_begin_write(outstream, &areas, &framesToWrite))) {
            printf("Unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }
        if (framesToWrite == 0) {
            break;
        }

        switch (audioFormat) {
            case AUDIO_S16:
                copySoundioFrames<Sint16>(framesToWrite, &lastPos, areas);
                break;
            case AUDIO_U16:
                copySoundioFrames<Uint16>(framesToWrite, &lastPos, areas);
                break;
            case AUDIO_F32:
                copySoundioFrames<float>(framesToWrite, &lastPos, areas);
                break;
            default:
                printf("Format not implemented\n");
                exit(1);
        }

        if ((err = soundio_outstream_end_write(outstream))) {
            if (err == SoundIoErrorUnderflow) {
                return;
            }
            printf("Unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        framesLeft -= framesToWrite;

        static Uint64 lastOutputCounter = 0;
        if (audioDebug) {
            Uint64 newCounter = SDL_GetPerformanceCounter();
            if (lastOutputCounter > 0) {
                int deltaUs = (int)((newCounter - lastOutputCounter) * 1000 / timeFreq);
                outputTimeHist[deltaUs]++;
                printf("Soundio output %dms = %d samples\n", deltaUs, framesToWrite);
            }
            lastOutputCounter = newCounter;
        }
    }
}

void soundioUnderflowCallback(struct SoundIoOutStream* /*outstream*/) {
    static int count = 0;
    count++;
    printf("Underflow %d\n", count);
}

template<typename T, typename U>
void printNumberMap(const unordered_map<T, U>& hist)
{
    vector<pair<T, U>> pairs(hist.begin(), hist.end());
    std::sort(pairs.begin(), pairs.end(), [](const pair<T, U>& p1, const pair<T, U>& p2) {
        return p1.first < p2.first;
    });
    for (const auto& p : pairs) {
        printf("[%g]: %g\n", (double)p.first, (double)p.second);
    }
}

bool initSDL()
{
    SDL_AudioSpec desiredSpec = {};
    SDL_AudioSpec obtainedSpec;
    desiredSpec.channels = kNumChannels;
    desiredSpec.freq = audioSampleRate;
    desiredSpec.samples = audioFrames;
    desiredSpec.format = audioFormat;
    desiredSpec.callback = sdlOutputCallback;
    // Allow changes, but fail if any changes actually occur.
    SDL_AudioDeviceID outputDev = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &obtainedSpec,
                                                      SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (outputDev == 0) {
        printf("Failed opening capture device\n");
        return false;
    }
    if (obtainedSpec.channels != desiredSpec.channels) {
        printf("Different number of channels obtained\n");
        return false;
    }
    if (obtainedSpec.freq != desiredSpec.freq) {
        printf("Different frequency obtained\n");
        return false;
    }
    if (obtainedSpec.format != desiredSpec.format) {
        printf("Different format obtained\n");
        return 1;
    }
    printf("SDL output params: %d ch, %d sample rate, %d samples, %d format\n", obtainedSpec.channels,
           obtainedSpec.freq, obtainedSpec.samples, obtainedSpec.format);
    SDL_PauseAudioDevice(outputDev, 0);
    return true;
}

SoundIo* soundio = nullptr;
struct SoundIoOutStream* soundioOutstream = nullptr;
struct SoundIoDevice* soundioDevice = nullptr;

bool initSoundIO()
{
    soundio = soundio_create();
    int soundioErr = soundio_connect(soundio);
    if (soundioErr != 0) {
        printf("Unable to connect to soundio backend: %s\n", soundio_strerror(soundioErr));
        return false;
    }
    printf("Soundio backend: %s\n", soundio_backend_name(soundio->current_backend));
    soundio_flush_events(soundio);

    int selectedDeviceIndex = soundio_default_output_device_index(soundio);

    if (selectedDeviceIndex < 0) {
        printf("Output device not found\n");
        return false;
    }

    soundioDevice = soundio_get_output_device(soundio, selectedDeviceIndex);
    fprintf(stderr, "Output device: %s\n", soundioDevice->name);

    if (soundioDevice->probe_error) {
        printf("Cannot probe device: %s\n", soundio_strerror(soundioDevice->probe_error));
        return false;
    }

    if (soundioDevice->current_layout.channel_count != 2) {
        printf("No suitable number of channels available, default: %d\n",
               soundioDevice->current_layout.channel_count);
        return false;
    }

    soundioOutstream = soundio_outstream_create(soundioDevice);
    soundioOutstream->write_callback = soundioWriteCallback;
    soundioOutstream->underflow_callback = soundioUnderflowCallback;
    soundioOutstream->software_latency = ((float)audioFrames) / audioSampleRate;
    soundioOutstream->sample_rate = audioSampleRate;
    switch (audioFormat) {
        case AUDIO_S16:
            soundioOutstream->format = SoundIoFormatS16NE;
            break;
        case AUDIO_U16:
            soundioOutstream->format = SoundIoFormatU16NE;
            break;
        case AUDIO_F32:
            soundioOutstream->format = SoundIoFormatFloat32NE;
            break;
        default:
            printf("Format not implemented\n");
            exit(1);
    }
    if (!soundio_device_supports_format(soundioDevice, soundioOutstream->format)) {
        printf("No suitable device format available\n");
        return false;
    }

    soundioErr = soundio_outstream_open(soundioOutstream);
    if (soundioErr != 0) {
        fprintf(stderr, "Unable to open device: %s", soundio_strerror(soundioErr));
        return false;
    }
    if (soundioOutstream->layout_error != 0) {
        printf("Unable to set channel layout: %s\n", soundio_strerror(soundioOutstream->layout_error));
        return false;
    }
    soundioErr = soundio_outstream_start(soundioOutstream);
    if (soundioErr != 0) {
        printf("Unable to start device: %s\n", soundio_strerror(soundioErr));
        return false;
    }

    printf("Soundio software latency: %f\n", soundioOutstream->software_latency);

    return true;
}

void cleanupSoundIO()
{
    soundio_outstream_destroy(soundioOutstream);
    soundio_device_unref(soundioDevice);
    soundio_destroy(soundio);
}

int main(int argc, char** argv)
{
    SDL_Init(SDL_INIT_EVERYTHING);
    timeFreq = SDL_GetPerformanceFrequency();

    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--backend") == 0) {
            // Input/output sample rate, e.g. 44100, 48000 etc.
            if (i == argc - 1) {
                printf("Missing argument --rate\n");
                return 1;
            }
            if (strcasecmp(argv[i + 1], "sdl") == 0) {
                audioBackend = AudioBackend::SDL;
            } else if (strcasecmp(argv[i + 1], "soundio") == 0) {
                audioBackend = AudioBackend::Soundio;
            } else {
                printf("Unknown backend %s\n", argv[i + 1]);
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--rate") == 0) {
            // Input/output sample rate, e.g. 44100, 48000 etc.
            if (i == argc - 1) {
                printf("Missing argument --rate\n");
                return 1;
            }
            audioSampleRate = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--frames") == 0) {
            // Number of frames in the buffers: 512, 1024, 2048 etc.
            if (i == argc - 1) {
                printf("Missing argument for --frames\n");
                return 1;
            }
            audioFrames = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--wave-hz") == 0) {
            // HZ of sine wave.
            if (i == argc - 1) {
                printf("Missing argument for --wave-hz\n");
                return 1;
            }
            outputWaveHz = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--format") == 0) {
            // Format: s16, u16 or f32.
            if (i == argc - 1) {
                printf("Missing argument for --format\n");
                return 1;
            }
            if (strcasecmp(argv[i + 1], "s16") == 0) {
                audioFormat = AUDIO_S16;
            } else if (strcasecmp(argv[i + 1], "u16") == 0) {
                audioFormat = AUDIO_U16;
            } else if (strcasecmp(argv[i + 1], "f32") == 0) {
                audioFormat = AUDIO_F32;
            } else {
                printf("Unknown format %s\n", argv[i + 1]);
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--debug") == 0) {
            audioDebug = true;
            i++;
        } else {
            printf("Unknown argument %s\n", argv[i]);
            return 1;
        }
    }

    prepareSineWave(outputWaveHz);

    switch (audioBackend) {
        case AudioBackend::SDL:
            if (!initSDL()) {
                return 1;
            }
            break;
        case AudioBackend::Soundio:
            if (!initSoundIO()) {
                return 1;
            }
            break;
    }

    SDL_CreateWindow("Audio test", 50, 50, 500, 500, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_QUIT) {
            printf("Quit requested\n");
            break;
        }
    }

    if (audioDebug) {
        printf("Capture time histogram:\n");
        printNumberMap(captureTimeHist);
        printf("Output time histogram:\n");
        printNumberMap(outputTimeHist);
    }

    SDL_Quit();
    switch (audioBackend) {
        case AudioBackend::SDL:
            break;
        case AudioBackend::Soundio:
            cleanupSoundIO();
            break;
    }
    return 0;
}
