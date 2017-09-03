#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <soundio/soundio.h>

using std::pair;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

enum class AudioBackend {
    SDL,
    Soundio
};

enum class OutputMode {
    SineWave,
    DelayedMic
};

const int kNumChannels = 2;

const int kDefaultAudioSampleRate = 48000;
const int kDefaultAudioSamples = 1024;
const int kDefaultWaveHz = 250;

const Uint16 kWaveFormatTagPcm = 1;
const Uint16 kWaveFormatTagFloat = 3;

// WAVE file structure, see http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html.
struct WaveHeader {
    Uint32 riffHeader = 0x46464952; // "RIFF"
    Uint32 totalSize = 36; // Must be 36 + dataChunkSize.
    Uint32 format = 0x45564157; // "WAVE"

    Uint32 fmtChunkId = 0x20746d66; // "fmt "
    Uint32 fmtChunkSize = 16;
    Uint16 formatTag = 0; // one of kWaveFormatTag*
    Uint16 numChannels = 0;
    Uint32 sampleRate = 0;

    Uint32 byteRate = UINT32_MAX;
    Uint16 blockAlign = 0;
    Uint16 bitsPerSample = 0;

    Uint32 dataChunkId = 0x61746164; // "data"
    Uint32 dataChunkSize = 0;
};

AudioBackend audioBackend = AudioBackend::SDL;
int audioSampleRate = kDefaultAudioSampleRate;
int audioSamples = kDefaultAudioSamples;
SDL_AudioFormat audioFormat = AUDIO_S16;
bool audioDebug = false;

unique_ptr<Uint8[]> samples;
size_t samplesSize = 0;
// In bytes.
size_t outputPos = 0;
// In bytes, only for OutputMode::DelayedMic.
size_t inputPos = 0;
// File to write the recorded data to.
int outputFd = -1;
size_t outputFdDataSize = 0;

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
    samplesSize = numSamples * kNumChannels * SDL_AUDIO_BITSIZE(audioFormat) / 8;
    switch (audioFormat) {
        case AUDIO_S16:
            samples.reset(new Uint8[samplesSize]);
            prepareSineWaveImpl<Sint16>(numSamples, 0, audioSampleRate, kNumChannels, waveHz, -10000, 10000,
                                        (Sint16*)samples.get());
            break;
        case AUDIO_U16:
            samplesSize = numSamples * sizeof(Uint16) * kNumChannels;
            samples.reset(new Uint8[samplesSize]);
            prepareSineWaveImpl<Uint16>(numSamples, 0, audioSampleRate, kNumChannels, waveHz, 0, 20000,
                                        (Uint16*)samples.get());
            break;
        case AUDIO_F32:
            samplesSize = numSamples * sizeof(float) * kNumChannels;
            samples.reset(new Uint8[samplesSize]);
            prepareSineWaveImpl<float>(numSamples, 0, audioSampleRate, kNumChannels, waveHz, -0.3, 0.3,
                                       (float*)samples.get());
            break;
        default:
            printf("Format not implemented\n");
            exit(1);
    }
}

void prepareOutputDelayedMic(int delayMs)
{
    // Calculate delay in samples and make it a multiple of audioSamples.
    int delaySamples = audioSampleRate * delayMs / 1000;
    if (delaySamples % audioSamples != 0) {
        delaySamples += audioSamples - (delaySamples % audioSamples);
    }
    // Double the input delay for buffer size (the first half for initial silence).
    samplesSize = delaySamples * 2 * kNumChannels * SDL_AUDIO_BITSIZE(audioFormat) / 8;
    samples.reset(new Uint8[samplesSize]);
    memset(&samples[0], 0, samplesSize);
    inputPos = samplesSize / 2;
    printf("Delay input by %d samples (size %d, first input pos %d)\n", delaySamples, (int)samplesSize, (int)inputPos);
}

void writeWaveHeader(int fd)
{
    static_assert(SDL_BYTEORDER == SDL_LIL_ENDIAN, "Big endian is not supported by WAVE writer");
    WaveHeader header;
    // Write the default values, they will be overwritten later.
    if (write(fd, &header, sizeof(header)) != sizeof(header)) {
        printf("Header write failed\n");
        exit(1);
    }
}

void rewriteWaveHeader(int fd)
{
    WaveHeader header;
    lseek(fd, 0, SEEK_SET);
    header.formatTag = audioFormat == AUDIO_F32 ? kWaveFormatTagFloat : kWaveFormatTagPcm;
    header.numChannels = kNumChannels;
    header.totalSize = 36 + outputFdDataSize;
    header.sampleRate = audioSampleRate;
    header.byteRate = kNumChannels * audioSampleRate * SDL_AUDIO_BITSIZE(audioFormat) / 8;
    header.blockAlign = kNumChannels * SDL_AUDIO_BITSIZE(audioFormat) / 8;
    header.dataChunkSize = outputFdDataSize;
    // Rewrite the header with actual values.
    if (write(fd, &header, sizeof(header)) != sizeof(header)) {
        printf("Header write failed\n");
        exit(1);
    }
}

template<typename T>
size_t copyFromSamples(size_t pos, size_t len, T* dest)
{
    if (pos + len <= samplesSize) {
        memcpy(dest, &samples[pos], len);
        pos += len;
        if (pos == samplesSize) {
            pos = 0;
        }
    } else {
        size_t end = samplesSize - pos;
        memcpy(dest, &samples[pos], end);
        pos = len - end;
        memcpy(dest + end, &samples[0], pos);
    }
    return pos;
}

template<size_t frameSize>
size_t copyAndDupImpl(Uint8* dst, size_t dstLen, size_t dstPos, const Uint8* src, size_t srcLen)
{
    for (size_t i = 0; i < srcLen; i += frameSize) {
        memcpy(&dst[dstPos], &src[i], frameSize);
        dstPos += frameSize;
        for (int channel = 1; channel < kNumChannels; channel++) {
            memset(&dst[dstPos], 0, frameSize);
            dstPos += frameSize;
        }
        if (dstPos >= dstLen) {
            dstPos = 0;
        }
//        for (int channel = 0; channel < kNumChannels; channel++) {
//            memcpy(&dst[dstPos], &src[i], frameSize);
//            dstPos += frameSize;
//        }
//        if (dstPos >= dstLen) {
//            dstPos = 0;
//        }
    }
    return dstPos;
}

size_t copyAndDup(Uint8* dst, size_t dstLen, size_t dstPos, const Uint8* src, size_t srcLen)
{
    switch (audioFormat) {
        case AUDIO_S16:
            return copyAndDupImpl<sizeof(Sint16)>(dst, dstLen, dstPos, src, srcLen);
        case AUDIO_F32:
            return copyAndDupImpl<sizeof(float)>(dst, dstLen, dstPos, src, srcLen);
        case AUDIO_U16:
            return copyAndDupImpl<sizeof(Uint16)>(dst, dstLen, dstPos, src, srcLen);
        default:
            printf("Wrong format\n");
            exit(1);
    }
}

void convertU16toS16(Uint8* stream, size_t len)
{
    Uint16* stream16 = (Uint16*)stream;
    for (size_t i = 0; i < len / 2; i++) {
        stream16[i] -= UINT16_MAX / 2;
    }
}

void writeWaveData(Uint8* stream, size_t len)
{
    // 48000 * 2 channels * 1 second.
    static Uint8 scratch[96000];

    if ((int)sizeof(scratch) < len / kNumChannels) {
        printf("Internal error: len > input scratch buffer size: %d > %d\n", (int)len, (int)sizeof(scratch) );
    }
    size_t writeLen = copyAndDup(scratch, sizeof(scratch), 0, stream, len);
    if (audioFormat == AUDIO_U16) {
        convertU16toS16(scratch, writeLen);
    }
    if ((size_t)write(outputFd, stream, writeLen) != writeLen) {
        printf("Data write failed\n");
        exit(1);
    }
    outputFdDataSize += writeLen;
}

// Either write to file or output.
void SDLCALL sdlOutputCallback(void* /*userdata*/, Uint8* stream, int len)
{
    if (outputFd != -1) {
        writeWaveData(stream, len);
        memset(stream, 0, len);
    } else {
        if ((size_t)len > samplesSize) {
            printf("Internal error: len > output size: %d > %d\n", len, (int)samplesSize);
            return;
        }

        outputPos = copyFromSamples(outputPos, len, stream);
    }

    static Uint64 lastCounter = 0;
    if (audioDebug) {
        Uint64 newCounter = SDL_GetPerformanceCounter();
        if (lastCounter > 0) {
            int deltaUs = (int)((newCounter - lastCounter) * 1000 / timeFreq);
            int numSamples = len * 4 / SDL_AUDIO_BITSIZE(audioFormat);
            outputTimeHist[deltaUs]++;
            printf("SDL output %dms = %d samples\n", deltaUs, numSamples);
        }
        lastCounter = newCounter;
    }
}

void SDLCALL sdlInputCallback(void* /*userdata*/, Uint8* stream, int len)
{
    if ((size_t)len > samplesSize) {
        printf("Internal error: len > output size: %d > %d\n", len, (int)samplesSize);
        return;
    }

    inputPos = copyAndDup(samples.get(), samplesSize, inputPos, stream, len);

    static Uint64 lastCounter = 0;
    if (audioDebug) {
        Uint64 newCounter = SDL_GetPerformanceCounter();
        if (lastCounter > 0) {
            int deltaUs = (int)((newCounter - lastCounter) * 1000 / timeFreq);
            int numSamples = len * 2 / SDL_AUDIO_BITSIZE(audioFormat);
            captureTimeHist[deltaUs]++;
            printf("SDL capture %dms = %d samples\n", deltaUs, numSamples);
        }
        lastCounter = newCounter;
    }
}

template<size_t frameSize>
size_t copySoundioFrames(int framesToWrite, size_t pos, struct SoundIoChannelArea* areas)
{
    // Check if we've got the regular layout: interleaved channels with step = sizeof(T).
    size_t len = framesToWrite * frameSize * kNumChannels;
    bool standardLayout = (areas[0].step == frameSize * kNumChannels)
            && (areas[1].step == frameSize * kNumChannels)
            && (areas[1].ptr - areas[0].ptr == frameSize)
            && (len <= samplesSize);
    if (standardLayout) {
        return copyFromSamples(pos, len, areas[0].ptr);
    } else {
        for (int frame = 0; frame < framesToWrite; frame++) {
            for (int channel = 0; channel < kNumChannels; channel++) {
                memcpy(areas[channel].ptr, &samples[pos], frameSize);
                areas[channel].ptr += areas[channel].step;
                pos += frameSize;
            }
            if (pos >= samplesSize) {
                pos = 0;
            }
        }
        return pos;
    }
}

void soundioWriteCallback(struct SoundIoOutStream* outstream, int /*frameCountMin*/, int frameCountMax)
{
    int framesLeft = frameCountMax;
    while (framesLeft > 0) {
        struct SoundIoChannelArea* areas;
        int framesToWrite = framesLeft;
        int err;
        if ((err = soundio_outstream_begin_write(outstream, &areas, &framesToWrite))) {
            printf("Soundio unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }
        if (framesToWrite == 0) {
            break;
        }

        switch (audioFormat) {
            case AUDIO_S16:
                outputPos = copySoundioFrames<sizeof(Sint16)>(framesToWrite, outputPos, areas);
                break;
            case AUDIO_U16:
                outputPos = copySoundioFrames<sizeof(Uint16)>(framesToWrite, outputPos, areas);
                break;
            case AUDIO_F32:
                outputPos = copySoundioFrames<sizeof(float)>(framesToWrite, outputPos, areas);
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
    printf("Soundio underflow %d\n", count);
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

bool initSDL(OutputMode outputMode)
{
    SDL_AudioSpec desiredSpec;
    SDL_AudioSpec obtainedSpec;
    memset(&desiredSpec, 0, sizeof(desiredSpec));
    desiredSpec.channels = kNumChannels;
    desiredSpec.freq = audioSampleRate;
    desiredSpec.samples = audioSamples;
    desiredSpec.format = audioFormat;
    desiredSpec.callback = sdlOutputCallback;
    // Allow changes, but fail if any changes actually occur.
    SDL_AudioDeviceID outputDev = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &obtainedSpec,
                                                      SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (outputDev == 0) {
        printf("Failed opening output device\n");
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
    if (outputMode != OutputMode::SineWave) {
        desiredSpec.channels = 1;
        desiredSpec.callback = sdlInputCallback;
        SDL_AudioDeviceID inputDev = SDL_OpenAudioDevice(nullptr, 1, &desiredSpec, &obtainedSpec,
                                                         SDL_AUDIO_ALLOW_ANY_CHANGE);
        if (inputDev == 0) {
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
        printf("SDL input params: %d ch, %d sample rate, %d samples, %d format\n", obtainedSpec.channels,
               obtainedSpec.freq, obtainedSpec.samples, obtainedSpec.format);
        SDL_PauseAudioDevice(inputDev, 0);
    }
    SDL_PauseAudioDevice(outputDev, 0);
    return true;
}

SoundIo* soundio = nullptr;
struct SoundIoOutStream* soundioOutstream = nullptr;
struct SoundIoDevice* soundioOutDevice = nullptr;
struct SoundIoInStream* soundioInstream = nullptr;
//struct SoundIoDevice* soundioInDevice = nullptr;

bool initSoundIO(OutputMode outputMode)
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

    soundioOutDevice = soundio_get_output_device(soundio, selectedDeviceIndex);
    fprintf(stderr, "Output device: %s\n", soundioOutDevice->name);

    if (soundioOutDevice->probe_error) {
        printf("Cannot probe device: %s\n", soundio_strerror(soundioOutDevice->probe_error));
        return false;
    }

    if (soundioOutDevice->current_layout.channel_count != 2) {
        printf("No suitable number of channels available, default: %d\n",
               soundioOutDevice->current_layout.channel_count);
        return false;
    }

    soundioOutstream = soundio_outstream_create(soundioOutDevice);
    soundioOutstream->write_callback = soundioWriteCallback;
    soundioOutstream->underflow_callback = soundioUnderflowCallback;
    soundioOutstream->software_latency = ((float)audioSamples) / audioSampleRate;
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
    if (!soundio_device_supports_format(soundioOutDevice, soundioOutstream->format)) {
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
    soundio_device_unref(soundioOutDevice);
    soundio_destroy(soundio);
}

int main(int argc, char** argv)
{
    OutputMode outputMode = OutputMode::SineWave;
    int outputSineWaveHz = kDefaultWaveHz;
    int outputMicDelayMs = 0;

    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--backend") == 0) {
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
            if (i == argc - 1) {
                printf("Missing argument --rate\n");
                return 1;
            }
            audioSampleRate = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--samples") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --samples\n");
                return 1;
            }
            audioSamples = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--format") == 0) {
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
        } else if (strcmp(argv[i], "--sine-wave") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --wave-hz\n");
                return 1;
            }
            outputMode = OutputMode::SineWave;
            outputSineWaveHz = atoi(argv[i + 1]);
            if (outputSineWaveHz <= 0) {
                printf("Argument for --wave-hz must be positive integer");
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--delay-mic") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --delay-mic\n");
                return 1;
            }
            outputMode = OutputMode::DelayedMic;
            outputMicDelayMs = atoi(argv[i + 1]);
            if (outputMicDelayMs <= 0) {
                printf("Argument for --delay-mic must be positive integer");
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--file") == 0) {
            // The amount of ms, by which the input will be delayed.
            if (i == argc - 1) {
                printf("Missing argument for --file\n");
                return 1;
            }
            outputFd = open(argv[i + 1], O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
            if (outputFd < 0) {
                printf("Unable to open file %s: %s", argv[i + 1], strerror(errno));
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--debug") == 0) {
            audioDebug = true;
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options] operation\n"
                   "Operation:\n"
                   "\t--sine-wave HZ\t\t\tOutput sine wave with given HZ, %d by default\n"
                   "\t--delay-mic MS\t\tCapture microphone and output with given delay\n"
                   "Options:\n"
                   "\t--file FILE.WAV\t\tWrite output to file in WAV format instead of playing it\n"
                   "\t--backend (sdl|soundio)\t\tAudio backend, SDL by default.\n"
                   "\t--rate SAMPLE_RATE\t\tInput/output sample rate, %d by default\n"
                   "\t--samples SAMPLES\t\tNumber of frames in the buffers, %d by default\n"
                   "\t--format (s16|u16|f32)\t\tOutput/capture format, s16 by default\n"
                   "\t--debug\t\t\t\tOutput additional debug\n",
                    argv[0],
                    kDefaultAudioSampleRate,
                    kDefaultAudioSamples,
                    kDefaultWaveHz);
            return 0;
        } else {
            printf("Unknown argument %s\n", argv[i]);
            return 1;
        }
    }

    SDL_Init(SDL_INIT_EVERYTHING);
    timeFreq = SDL_GetPerformanceFrequency();

    switch (outputMode) {
        case OutputMode::SineWave:
            prepareSineWave(outputSineWaveHz);
            break;
        case OutputMode::DelayedMic:
            prepareOutputDelayedMic(outputMicDelayMs);
            break;
    }

    if (outputFd != -1) {
        writeWaveHeader(outputFd);
    }

    switch (audioBackend) {
        case AudioBackend::SDL:
            if (!initSDL(outputMode)) {
                return 1;
            }
            break;
        case AudioBackend::Soundio:
            if (!initSoundIO(outputMode)) {
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

    if (outputFd != -1) {
        rewriteWaveHeader(outputFd);
        close(outputFd);
    }

    return 0;
}
