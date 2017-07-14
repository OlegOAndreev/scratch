#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <SDL2/SDL.h>

using std::pair;
using std::unordered_map;
using std::vector;

Uint64 timeFreq;

int audioSampleRate = 48000;
int audioFrames = 1024;
SDL_AudioFormat audioFormat = AUDIO_S16;
bool audioDebug = false;
int outputWaveHz = 250;

vector<Uint8> outputData;
size_t numOutputSamples = 0;

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
            outputData.resize(numSamples * sizeof(Sint16) * 2);
            prepareSineWaveImpl<Sint16>(numSamples, 0, audioSampleRate, 2, waveHz, -10000, 10000,
                                        (Sint16*)outputData.data());
            break;
        case AUDIO_U16:
            outputData.resize(numSamples * sizeof(Uint16) * 2);
            prepareSineWaveImpl<Uint16>(numSamples, 0, audioSampleRate, 2, waveHz, 0, 20000,
                                        (Uint16*)outputData.data());
            break;
        case AUDIO_F32:
            outputData.resize(numSamples * sizeof(float) * 2);
            prepareSineWaveImpl<float>(numSamples, 0, audioSampleRate, 2, waveHz, -1.0, 1.0,
                                       (float*)outputData.data());
            break;
        default:
            printf("Sine wave not implemented for format\n");
            exit(1);
            break;
    }
}

void SDLCALL outputCallback(void* /*userdata*/, Uint8* stream, int len)
{
    static size_t lastPos = 0;
    size_t outputSize = outputData.size();
    if ((size_t)len > outputSize) {
        printf("Internal error: len > outputSize: %d > %d\n", len, (int)outputSize);
        return;
    }

    if (lastPos + len <= outputSize) {
        memcpy(stream, outputData.data() + lastPos, len);
        lastPos += len;
        if (lastPos == outputData.size()) {
            lastPos = 0;
        }
    } else {
        size_t end = outputData.size() - lastPos;
        memcpy(stream, outputData.data() + lastPos, end);
        lastPos = len - end;
        memcpy(stream + end, outputData.data(), lastPos);
    }

    static Uint64 lastOutputCounter = 0;
    if (audioDebug) {
        Uint64 newCounter = SDL_GetPerformanceCounter();
        if (lastOutputCounter > 0) {
            int deltaUs = (int)((newCounter - lastOutputCounter) * 1000 / timeFreq);
            int numSamples = len * 4 / SDL_AUDIO_BITSIZE(audioFormat);
            outputTimeHist[deltaUs]++;
            printf("Output %dms = %d samples\n", deltaUs, numSamples);
        }
        lastOutputCounter = newCounter;
    }
}

template<typename T, typename U>
void printNumberMap(const unordered_map<T, U> hist)
{
    vector<pair<T, U>> pairs(hist.begin(), hist.end());
    std::sort(pairs.begin(), pairs.end(), [](const pair<T, U>& p1, const pair<T, U>& p2) {
        return p1.first < p2.first;
    });
    for (const auto& p : pairs) {
        printf("[%g]: %g\n", (double)p.first, (double)p.second);
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--rate") == 0) {
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

    SDL_Init(SDL_INIT_EVERYTHING);
    timeFreq = SDL_GetPerformanceFrequency();

    SDL_AudioSpec desiredSpec = {};
    SDL_AudioSpec obtainedSpec;
    desiredSpec.channels = 2;
    desiredSpec.freq = audioSampleRate;
    desiredSpec.samples = audioFrames;
    desiredSpec.format = audioFormat;
    desiredSpec.callback = outputCallback;
    // Allow changes, but fail if any changes actually occur.
    SDL_AudioDeviceID outputDev = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &obtainedSpec,
                                                      SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (outputDev == 0) {
        printf("Failed opening capture device\n");
        return 1;
    }
    if (obtainedSpec.channels != desiredSpec.channels) {
        printf("Different number of channels obtained\n");
        return 1;
    }
    if (obtainedSpec.freq != desiredSpec.freq) {
        printf("Different frequency obtained\n");
        return 1;
    }
    if (obtainedSpec.format != desiredSpec.format) {
        printf("Different format obtained\n");
        return 1;
    }
    printf("Output params: %d ch, %d sample rate, %d samples, %d format\n", obtainedSpec.channels,
           obtainedSpec.freq, obtainedSpec.samples, obtainedSpec.format);

    prepareSineWave(outputWaveHz);

    SDL_PauseAudioDevice(outputDev, 0);

    SDL_CreateWindow("Audio test", 50, 50, 100, 100, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
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
    return 0;
}
