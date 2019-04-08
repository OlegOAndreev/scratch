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


enum class AudioBackend {
    SDL,
    Soundio
};

enum class OutputMode {
    SineWave,
    DelayedMic
};

const int kNumChannels = 2;

const int kDefaultSampleRate = 48000;
const int kDefaultBufferSamples = 1024;
const int kDefaultWaveHz = 250;
const SDL_AudioFormat kDefaultAudioFormat = AUDIO_S16;

AudioBackend backend = AudioBackend::SDL;
int sampleRate = kDefaultSampleRate;
int bufferSamples = kDefaultBufferSamples;
SDL_AudioFormat audioFormat = kDefaultAudioFormat;
bool debug = false;

struct SamplesBuffer {
    std::unique_ptr<Uint8[]> samples;
    size_t size = 0;
    // In bytes.
    size_t outputPos = 0;
    // In bytes, only for OutputMode::DelayedMic.
    size_t inputPos = 0;

    void init(size_t newSize)
    {
        samples.reset(new Uint8[newSize]);
        size = newSize;
        inputPos = 0;
        outputPos = 0;
    }

    template<typename T>
    T* ptr()
    {
        return reinterpret_cast<T*>(samples.get());
    }

    template<typename T>
    T* outputPtr()
    {
        return reinterpret_cast<T*>(samples.get() + outputPos);
    }

    template<typename T>
    T* inputPtr()
    {
        return reinterpret_cast<T*>(samples.get() + inputPos);
    }

    void copyFrom(void* dst, size_t len)
    {
        if (outputPos + len <= size) {
            memcpy(dst, samples.get() + outputPos, len);
            outputPos += len;
            if (outputPos == size) {
                outputPos = 0;
            }
        } else {
            size_t untilEnd = size - outputPos;
            memcpy(dst, samples.get() + outputPos, untilEnd);
            outputPos = len - untilEnd;
            memcpy((Uint8*)dst + untilEnd, samples.get(), outputPos);
        }
    }

    template<size_t sampleSize>
    void copyToAndDupImpl(const void* src, size_t len)
    {
        if (kNumChannels == 1) {
            if (inputPos + len <= size) {
                memcpy(samples.get() + inputPos, src, len);
                inputPos += len;
                if (inputPos == size) {
                    inputPos = 0;
                }
            } else {
                size_t untilEnd = size - inputPos;
                memcpy(samples.get() + inputPos, src, untilEnd);
                inputPos = len - untilEnd;
                memcpy(samples.get(), (const Uint8*)src + untilEnd, inputPos);
            }
        } else {
            for (size_t i = 0; i < len; i += sampleSize) {
                memcpy(samples.get() + inputPos, (const Uint8*)src + i, sampleSize);
                inputPos += sampleSize;
                for (int channel = 1; channel < kNumChannels; channel++) {
                    memcpy(samples.get() + inputPos, (const Uint8*)src + i, sampleSize);
                    inputPos += sampleSize;
                }
                if (inputPos >= size) {
                    inputPos = 0;
                }
            }
        }
    }

    void copyToAndDup(const void* src, size_t len)
    {
        switch (audioFormat) {
            case AUDIO_S16:
                copyToAndDupImpl<2>(src, len);
                break;
            case AUDIO_F32:
                copyToAndDupImpl<4>(src, len);
                break;
            default:
                printf("Wrong format\n");
                exit(1);
        }
    }
};
SamplesBuffer samples;

struct WaveWriter {
    int fd = -1;
    size_t dataSize = 0;

    void writeHeader()
    {
        static_assert(SDL_BYTEORDER == SDL_LIL_ENDIAN,
                      "Big endian is not supported by WAVE writer");
        WaveHeader header;
        // Write the default values, they will be overwritten later.
        if (write(fd, &header, sizeof(header)) != sizeof(header)) {
            printf("Header write failed\n");
            exit(1);
        }
    }

    void writeData(void* buf, size_t len)
    {
        if ((size_t)write(fd, buf, len) != len) {
            printf("Data write failed\n");
            exit(1);
        }
        dataSize += len;
    }

    void rewriteHeader()
    {
        WaveHeader header;
        header.formatTag = audioFormat == AUDIO_F32 ? kWaveFormatTagFloat : kWaveFormatTagPcm;
        header.numChannels = kNumChannels;
        header.totalSize = 36 + dataSize;
        header.sampleRate = sampleRate;
        header.byteRate = kNumChannels * sampleRate * SDL_AUDIO_BITSIZE(audioFormat) / 8;
        header.blockAlign = kNumChannels * SDL_AUDIO_BITSIZE(audioFormat) / 8;
        header.bitsPerSample = SDL_AUDIO_BITSIZE(audioFormat);
        header.dataChunkSize = dataSize;
        // Rewrite the header with actual values.
        lseek(fd, 0, SEEK_SET);
        if (write(fd, &header, sizeof(header)) != sizeof(header)) {
            printf("Header write failed\n");
            exit(1);
        }
    }

    // WAVE file structure, see http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html.
    static const Uint32 kRiffHeader = 0x46464952; // "RIFF"
    static const Uint32 kWaveFormat = 0x45564157; // "WAVE"
    static const Uint32 kFmtChunkId = 0x20746d66; // "fmt "
    static const Uint32 kDataChunkId = 0x61746164; // "data"
    static const Uint16 kWaveFormatTagPcm = 1;
    static const Uint16 kWaveFormatTagFloat = 3;
    struct WaveHeader {
        Uint32 riffHeader = kRiffHeader;
        Uint32 totalSize = 36; // Must be 36 + dataChunkSize.
        Uint32 format = kWaveFormat;

        Uint32 fmtChunkId = kFmtChunkId;
        Uint32 fmtChunkSize = 16;
        Uint16 formatTag = 0; // one of kWaveFormatTag*
        Uint16 numChannels = 0;
        Uint32 sampleRate = 0;

        Uint32 byteRate = UINT32_MAX;
        Uint16 blockAlign = 0;
        Uint16 bitsPerSample = 0;

        Uint32 dataChunkId = kDataChunkId;
        Uint32 dataChunkSize = 0;
    };
};
WaveWriter waveWriter;

// High-frequency timer rate.
Uint64 timeFreq;

std::unordered_map<Uint64, size_t> inputTimeHist;
std::unordered_map<Uint64, size_t> outputTimeHist;

template<typename ST>
void prepareSineWaveImpl(size_t num, size_t startSample, int waveHz, ST low, ST high, ST* data)
{
    ST ampl = (high - low) / 2;
    ST base = low + ampl;
    double sampleRad = 2 * M_PI * waveHz / sampleRate;
    for (size_t i = 0; i < num; i++) {
        ST value = base + ST(sin(sampleRad * (i + startSample)) * ampl);
        for (int j = 0; j < kNumChannels; j++) {
            data[i * kNumChannels + j] = value;
        }
    }
}

void prepareSineWave(int waveHz)
{
    // Fill the whole second with data.
    samples.init(sampleRate * kNumChannels * SDL_AUDIO_BITSIZE(audioFormat) / 8);
    switch (audioFormat) {
        case AUDIO_S16:
            prepareSineWaveImpl<Sint16>(sampleRate, 0, waveHz, -10000, 10000,
                                        samples.ptr<Sint16>());
            break;
        case AUDIO_F32:
            prepareSineWaveImpl<float>(sampleRate, 0, waveHz, -0.3, 0.3, samples.ptr<float>());
            break;
        default:
            printf("Format not implemented\n");
            exit(1);
    }
}

void prepareOutputDelayedMic(int delayMs)
{
    // Calculate delay in samples and make it a multiple of audioSamples.
    int delaySamples = sampleRate * delayMs / 1000;
    if (delaySamples % bufferSamples != 0) {
        delaySamples += bufferSamples - (delaySamples % bufferSamples);
    }
    // Double the input delay for buffer size (the first half for initial silence).
    samples.init(delaySamples * 2 * kNumChannels * SDL_AUDIO_BITSIZE(audioFormat) / 8);
    memset(samples.ptr<Uint8>(), 0, samples.size);
    samples.inputPos = samples.size / 2;
    printf("Delay input by %d samples (size %d, first input pos %d)\n", delaySamples,
           (int)samples.size, (int)samples.inputPos);
}

void writeWaveData(size_t len)
{
    // Trivial method: just write 48000 * 2 channels * 1 second.
    static Uint8 scratch[96000];
    if (len > sizeof(scratch)) {
        printf("Internal error: len > input scratch buffer size: %d > %d\n", (int)len,
               (int)sizeof(scratch) );
    }
    samples.copyFrom(scratch, len);
    waveWriter.writeData(scratch, len);
}

// Either write to file or output.
void SDLCALL sdlOutputCallback(void* /*userdata*/, Uint8* stream, int len)
{
    if (waveWriter.fd != -1) {
        writeWaveData(len);
        memset(stream, 0, len);
    } else {
        if ((size_t)len > samples.size) {
            printf("Internal error: len > output size: %d > %d\n", len, (int)samples.size);
            return;
        }

        samples.copyFrom(stream, len);
    }

    static Uint64 lastCounter = 0;
    if (debug) {
        Uint64 newCounter = SDL_GetPerformanceCounter();
        if (lastCounter > 0) {
            int deltaUs = (int)((newCounter - lastCounter) * 1000 / timeFreq);
            int lenSamples = len * 8 / (SDL_AUDIO_BITSIZE(audioFormat) * kNumChannels);
            outputTimeHist[deltaUs]++;
            printf("SDL output %dms = %d samples\n", deltaUs, lenSamples);
        }
        lastCounter = newCounter;
    }
}

void SDLCALL sdlInputCallback(void* /*userdata*/, Uint8* stream, int len)
{
    if ((size_t)len > samples.size) {
        printf("Internal error: len > output size: %d > %d\n", len, (int)samples.size);
        return;
    }

    samples.copyToAndDup(stream, len);

    static Uint64 lastCounter = 0;
    if (debug) {
        Uint64 newCounter = SDL_GetPerformanceCounter();
        if (lastCounter > 0) {
            int deltaUs = (int)((newCounter - lastCounter) * 1000 / timeFreq);
            int lenSamples = len * 8 / SDL_AUDIO_BITSIZE(audioFormat);
            inputTimeHist[deltaUs]++;
            printf("SDL input %dms = %d samples\n", deltaUs, lenSamples);
        }
        lastCounter = newCounter;
    }
}

template<size_t frameSize>
void copyToSoundioFrames(int framesToWrite, struct SoundIoChannelArea* areas)
{
    // Check if we've got the regular layout: interleaved channels with step = sizeof(T).
    size_t len = framesToWrite * frameSize * kNumChannels;
    bool standardLayout = (areas[0].step == frameSize * kNumChannels)
            && (areas[1].step == frameSize * kNumChannels)
            && (areas[1].ptr - areas[0].ptr == frameSize)
            && (len <= samples.size);
    if (waveWriter.fd != -1) {
        writeWaveData(len);
        if (standardLayout) {
            memset(areas[0].ptr, 0, len);
        } else {
            for (int frame = 0; frame < framesToWrite; frame++) {
                for (int channel = 0; channel < kNumChannels; channel++) {
                    memset(areas[channel].ptr, 0, frameSize);
                    areas[channel].ptr += areas[channel].step;
                    samples.outputPos += frameSize;
                }
                if (samples.outputPos >= samples.size) {
                    samples.outputPos = 0;
                }
            }
        }
    } else {
        if (standardLayout) {
            samples.copyFrom(areas[0].ptr, len);
        } else {
            for (int frame = 0; frame < framesToWrite; frame++) {
                for (int channel = 0; channel < kNumChannels; channel++) {
                    memcpy(areas[channel].ptr, samples.outputPtr<Uint8>(), frameSize);
                    areas[channel].ptr += areas[channel].step;
                    samples.outputPos += frameSize;
                }
                if (samples.outputPos >= samples.size) {
                    samples.outputPos = 0;
                }
            }
        }
    }
}

template<size_t frameSize>
void copyFromSoundioFramesAndDup(int framesToRead, struct SoundIoChannelArea* areas)
{
    // Ignore all but the first channel.
    for (int frame = 0; frame < framesToRead; frame++) {
        for (int channel = 0; channel < kNumChannels; channel++) {
            memcpy(samples.inputPtr<Uint8>(), areas[0].ptr, frameSize);
            samples.inputPos += frameSize;
        }
        areas[0].ptr += areas[0].step;
        if (samples.inputPos >= samples.size) {
            samples.inputPos = 0;
        }
    }
}

void soundioWriteCallback(struct SoundIoOutStream* outstream, int /*frameCountMin*/,
                          int frameCountMax)
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
                copyToSoundioFrames<sizeof(Sint16)>(framesToWrite, areas);
                break;
            case AUDIO_F32:
                copyToSoundioFrames<sizeof(float)>(framesToWrite, areas);
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
        if (debug) {
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

void soundioUnderflowCallback(struct SoundIoOutStream* /*outstream*/)
{
    static int count = 0;
    count++;
    printf("Soundio underflow %d\n", count);
}

void soundioReadCallback(struct SoundIoInStream* instream, int /*frameCountMin*/, int frameCountMax)
{
    int framesLeft = frameCountMax;
    while (framesLeft > 0) {
        struct SoundIoChannelArea* areas;
        int framesToRead = framesLeft;
        int err;
        if ((err = soundio_instream_begin_read(instream, &areas, &framesToRead))) {
            printf("Soundio unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }
        if (framesToRead == 0) {
            break;
        }

        if (areas) {
            switch (audioFormat) {
                case AUDIO_S16:
                    copyFromSoundioFramesAndDup<sizeof(Sint16)>(framesToRead, areas);
                    break;
                case AUDIO_F32:
                    copyFromSoundioFramesAndDup<sizeof(float)>(framesToRead, areas);
                    break;
                default:
                    printf("Format not implemented\n");
                    exit(1);
            }
        }

        if ((err = soundio_instream_end_read(instream))) {
            printf("Unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        framesLeft -= framesToRead;

        static Uint64 lastOutputCounter = 0;
        if (debug) {
            Uint64 newCounter = SDL_GetPerformanceCounter();
            if (lastOutputCounter > 0) {
                int deltaUs = (int)((newCounter - lastOutputCounter) * 1000 / timeFreq);
                inputTimeHist[deltaUs]++;
                printf("Soundio input %dms = %d samples\n", deltaUs, framesToRead);
            }
            lastOutputCounter = newCounter;
        }
    }
}

void soundioOverflowCallback(struct SoundIoInStream* /*instream*/)
{
    static int count = 0;
    count++;
    printf("Soundio overflow %d\n", count);
}


template<typename T, typename U>
void printNumberMap(const std::unordered_map<T, U>& hist)
{
    std::vector<std::pair<T, U>> pairs(hist.begin(), hist.end());
    std::sort(pairs.begin(), pairs.end(), [](const std::pair<T, U>& p1, const std::pair<T, U>& p2) {
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
    desiredSpec.freq = sampleRate;
    desiredSpec.samples = bufferSamples;
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
    printf("SDL output params: %d ch, %d sample rate, %d samples, %d format\n",
           obtainedSpec.channels, obtainedSpec.freq, obtainedSpec.samples, obtainedSpec.format);
    if (outputMode != OutputMode::SineWave) {
        desiredSpec.channels = 1;
        desiredSpec.callback = sdlInputCallback;
        SDL_AudioDeviceID inputDev = SDL_OpenAudioDevice(nullptr, 1, &desiredSpec, &obtainedSpec,
                                                         SDL_AUDIO_ALLOW_ANY_CHANGE);
        if (inputDev == 0) {
            printf("Failed opening input device\n");
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
        printf("SDL input params: %d ch, %d sample rate, %d samples, %d format\n",
               obtainedSpec.channels, obtainedSpec.freq, obtainedSpec.samples, obtainedSpec.format);
        SDL_PauseAudioDevice(inputDev, 0);
    }
    SDL_PauseAudioDevice(outputDev, 0);
    return true;
}

SoundIo* soundio = nullptr;
struct SoundIoOutStream* soundioOutStream = nullptr;
struct SoundIoDevice* soundioOutDevice = nullptr;
struct SoundIoInStream* soundioInStream = nullptr;
struct SoundIoDevice* soundioInDevice = nullptr;

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

    if (soundioOutDevice->current_layout.channel_count != kNumChannels) {
        printf("No suitable number of channels available, default: %d\n",
               soundioOutDevice->current_layout.channel_count);
        return false;
    }

    soundioOutStream = soundio_outstream_create(soundioOutDevice);
    soundioOutStream->write_callback = soundioWriteCallback;
    soundioOutStream->underflow_callback = soundioUnderflowCallback;
    soundioOutStream->software_latency = ((float)bufferSamples) / sampleRate;
    soundioOutStream->sample_rate = sampleRate;
    switch (audioFormat) {
        case AUDIO_S16:
            soundioOutStream->format = SoundIoFormatS16NE;
            break;
        case AUDIO_F32:
            soundioOutStream->format = SoundIoFormatFloat32NE;
            break;
        default:
            printf("Format not implemented\n");
            exit(1);
    }
    if (!soundio_device_supports_format(soundioOutDevice, soundioOutStream->format)) {
        printf("No suitable device format available\n");
        return false;
    }

    soundioErr = soundio_outstream_open(soundioOutStream);
    if (soundioErr != 0) {
        fprintf(stderr, "Unable to open device: %s", soundio_strerror(soundioErr));
        return false;
    }
    if (soundioOutStream->layout_error != 0) {
        printf("Unable to set channel layout: %s\n",
               soundio_strerror(soundioOutStream->layout_error));
        return false;
    }

    soundioErr = soundio_outstream_start(soundioOutStream);
    if (soundioErr != 0) {
        printf("Unable to start device: %s\n", soundio_strerror(soundioErr));
        return false;
    }

    printf("Soundio software latency: %f\n", soundioOutStream->software_latency);

    if (outputMode != OutputMode::SineWave) {
        selectedDeviceIndex = soundio_default_input_device_index(soundio);

        if (selectedDeviceIndex < 0) {
            printf("Input device not found\n");
            return false;
        }

        soundioInDevice = soundio_get_input_device(soundio, selectedDeviceIndex);
        fprintf(stderr, "Input device: %s\n", soundioOutDevice->name);

        if (soundioInDevice->probe_error) {
            printf("Cannot probe device: %s\n", soundio_strerror(soundioInDevice->probe_error));
            return false;
        }

        soundioInStream = soundio_instream_create(soundioInDevice);
        soundioInStream->read_callback = soundioReadCallback;
        soundioInStream->overflow_callback = soundioOverflowCallback;
        soundioInStream->software_latency = ((float)bufferSamples) / sampleRate;
        soundioInStream->sample_rate = sampleRate;
        switch (audioFormat) {
            case AUDIO_S16:
                soundioInStream->format = SoundIoFormatS16NE;
                break;
            case AUDIO_F32:
                soundioInStream->format = SoundIoFormatFloat32NE;
                break;
            default:
                printf("Format not implemented\n");
                exit(1);
        }
        if (!soundio_device_supports_format(soundioInDevice, soundioInStream->format)) {
            printf("No suitable device format available\n");
            return false;
        }

        soundioErr = soundio_instream_open(soundioInStream);
        if (soundioErr != 0) {
            fprintf(stderr, "Unable to open device: %s", soundio_strerror(soundioErr));
            return false;
        }
        if (soundioInStream->layout_error != 0) {
            printf("Unable to set channel layout: %s\n",
                   soundio_strerror(soundioInStream->layout_error));
            return false;
        }

        soundioErr = soundio_instream_start(soundioInStream);
        if (soundioErr != 0) {
            printf("Unable to start device: %s\n", soundio_strerror(soundioErr));
            return false;
        }

        printf("Soundio software latency: %f\n", soundioInStream->software_latency);
    }

    return true;
}

void cleanupSoundIO()
{
    soundio_outstream_destroy(soundioOutStream);
    if (soundioInStream) {
        soundio_instream_destroy(soundioInStream);
    }
    soundio_device_unref(soundioOutDevice);
    if (soundioInDevice) {
        soundio_device_unref(soundioInDevice);
    }
    soundio_destroy(soundio);
}

int main(int argc, char** argv)
{
    OutputMode outputMode = OutputMode::SineWave;
    int outputSineWaveHz = kDefaultWaveHz;
    int outputMicDelayMs = 0;

    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--sine-wave") == 0) {
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
            waveWriter.fd = open(argv[i + 1], O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
            if (waveWriter.fd < 0) {
                printf("Unable to open file %s: %s", argv[i + 1], strerror(errno));
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--backend") == 0) {
            if (i == argc - 1) {
                printf("Missing argument --rate\n");
                return 1;
            }
            if (strcasecmp(argv[i + 1], "sdl") == 0) {
                backend = AudioBackend::SDL;
            } else if (strcasecmp(argv[i + 1], "soundio") == 0) {
                backend = AudioBackend::Soundio;
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
            sampleRate = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--samples") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --samples\n");
                return 1;
            }
            bufferSamples = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--format") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --format\n");
                return 1;
            }
            if (strcasecmp(argv[i + 1], "s16") == 0) {
                audioFormat = AUDIO_S16;
            } else if (strcasecmp(argv[i + 1], "f32") == 0) {
                audioFormat = AUDIO_F32;
            } else {
                printf("Unknown format %s\n", argv[i + 1]);
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options] operation\n"
                   "Operation:\n"
                   "\t--sine-wave HZ\t\t\tOutput sine wave with given HZ, %d by default\n"
                   "\t--delay-mic MS\t\t\tCapture microphone and output with given delay\n"
                   "Options:\n"
                   "\t--file FILE.WAV\t\t\tWrite output to file in WAV format instead"
                   " of playing it\n"
                   "\t--backend (sdl|soundio)\t\tAudio backend, SDL by default.\n"
                   "\t--rate SAMPLE_RATE\t\tInput/output sample rate, %d by default\n"
                   "\t--samples SAMPLES\t\tNumber of frames in the buffers, %d by default\n"
                   "\t--format (s16|f32)\t\tOutput/input format, s16 by default\n"
                   "\t--debug\t\t\t\tOutput additional debug\n",
                    argv[0],
                    kDefaultWaveHz,
                    kDefaultSampleRate,
                    kDefaultBufferSamples);
            return 0;
        } else {
            printf("Unknown argument %s\n", argv[i]);
            return 1;
        }
    }
    printf("Channels: %d, rate: %d, buffer samples: %d, backend: %s, format: %s\n",
           kNumChannels, sampleRate, bufferSamples,
           backend == AudioBackend::SDL ? "sdl" : "soundio",
           audioFormat == AUDIO_F32 ? "f32" : "s16");

    SDL_Init(SDL_INIT_EVERYTHING);
    timeFreq = SDL_GetPerformanceFrequency();
    printf("Timer frequency: %lld\n", (long long) timeFreq);

    switch (outputMode) {
        case OutputMode::SineWave:
            printf("Outputting wave %dHZ\n", outputSineWaveHz);
            prepareSineWave(outputSineWaveHz);
            break;
        case OutputMode::DelayedMic:
            printf("Outputting delayed mic by %dms\n", outputMicDelayMs);
            prepareOutputDelayedMic(outputMicDelayMs);
            break;
    }

    if (waveWriter.fd != -1) {
        waveWriter.writeHeader();
    }

    switch (backend) {
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

    if (debug) {
        printf("Input time histogram:\n");
        printNumberMap(inputTimeHist);
        printf("Output time histogram:\n");
        printNumberMap(outputTimeHist);
    }

    SDL_Quit();
    switch (backend) {
        case AudioBackend::SDL:
            break;
        case AudioBackend::Soundio:
            cleanupSoundIO();
            break;
    }

    if (waveWriter.fd != -1) {
        waveWriter.rewriteHeader();
        close(waveWriter.fd);
    }

    return 0;
}
