#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

using std::unique_ptr;

enum class SampleFormat {
    Sint16,
    Float
};

size_t getSampleSize(SampleFormat format)
{
    switch (format) {
        case SampleFormat::Sint16:
            return 2;
        case SampleFormat::Float:
            return 4;
    }
}

struct SoundData {
    SampleFormat format = SampleFormat::Sint16;
    int rate = 0;
    int numChannels = 0;
    size_t numSamples = 0;
    unique_ptr<uint8_t[]> samples;

    size_t getSamplesSize() const
    {
        return numChannels * numSamples * getSampleSize(format);
    }
};

namespace Wave {
    const uint32_t kRiffChunkId = 0x46464952; // "RIFF"
    const uint32_t kWaveFormat = 0x45564157; // "WAVE"
    struct RiffHeader {
        uint32_t chunkId = 0; // Must be kRiffChunkId
        uint32_t totalSize = 0;
        uint32_t format = 0;
    };

    const uint32_t kFmtChunkId = 0x20746d66; // "fmt "
    const uint16_t kWaveFormatTagPcm = 1;
    const uint16_t kWaveFormatTagFloat = 3;
    struct FmtHeader {
        uint32_t chunkId = 0; // Must be kFmtChunkId
        uint32_t chunkSize = 0;
        uint16_t formatTag = 0; // Must be one of kWaveFormatTag*
        uint16_t numChannels = 0;
        uint32_t sampleRate = 0;

        uint32_t byteRate = UINT32_MAX;
        uint16_t blockAlign = 0;
        uint16_t bitsPerSample = 0;
    };

    const uint32_t kDataChunkId = 0x61746164; // "data"

    struct GenericHeader {
        uint32_t chunkId = 0;
        uint32_t chunkSize = 0;
    };

    SoundData loadWav(const char* path)
    {
        SoundData data;
        FILE* f = fopen(path, "r");
        if (!f) {
            printf("Unable to open %s\n", path);
            exit(1);
        }
        RiffHeader riffHeader;
        if (fread(&riffHeader, 1, sizeof(riffHeader), f) != sizeof(riffHeader)) {
            printf("%s is truncated: no RIFF header\n", path);
            exit(1);
        }
        if (riffHeader.chunkId != kRiffChunkId) {
            printf("%s is not a RIFF file\n", path);
            exit(1);
        }
        if (riffHeader.format != kWaveFormat) {
            printf("%s is not a WAVE file\n", path);
            exit(1);
        }

        FmtHeader fmtHeader;
        if (fread(&fmtHeader, 1, sizeof(fmtHeader), f) != sizeof(fmtHeader)) {
            printf("%s is truncated: no fmt header\n", path);
            exit(1);
        }
        if (fmtHeader.chunkId != kFmtChunkId) {
            printf("%s has no fmt chunk\n", path);
            exit(1);
        }
        if (fmtHeader.formatTag == kWaveFormatTagPcm) {
            data.format = SampleFormat::Sint16;
            if (fmtHeader.bitsPerSample != 16 || fmtHeader.blockAlign != 2 * fmtHeader.numChannels) {
                printf("%s has pcm format, but %d bits per sample with block align %d\n", path, (int)fmtHeader.bitsPerSample,
                       (int)fmtHeader.blockAlign);
                exit(1);
            }
        } else if (fmtHeader.formatTag == kWaveFormatTagFloat) {
            data.format = SampleFormat::Float;
            if (fmtHeader.bitsPerSample != 32 || fmtHeader.blockAlign != 4 * fmtHeader.numChannels) {
                printf("%s has float format, but %d bits per sample %d\n", path, (int)fmtHeader.bitsPerSample,
                       (int)fmtHeader.blockAlign);
                exit(1);
            }
        } else {
            printf("%s has strange format tag: %d\n", path, (int)fmtHeader.formatTag);
            exit(1);
        }
        data.rate = fmtHeader.sampleRate;
        data.numChannels = fmtHeader.numChannels;

        // Skip chunks until data data chunk.
        while (true) {
            GenericHeader header;
            if (fread(&header, 1, sizeof(header), f) != sizeof(header)) {
                printf("%s is truncated: no data header\n", path);
                exit(1);
            }

            if (header.chunkId == kDataChunkId) {
                data.numSamples = header.chunkSize / fmtHeader.blockAlign;
                data.samples.reset(new uint8_t[header.chunkSize]);
                size_t readBytes = fread(data.samples.get(), 1, header.chunkSize, f);
                if (readBytes != header.chunkSize) {
                    printf("%s has less bytes than required: %d vs %d\n", path, (int)readBytes, (int)header.chunkSize);
                    exit(1);
                }
                break;
            }
            fseek(f, header.chunkSize, SEEK_CUR);
        }

        printf("%s: %d ch, rate %d, %d samples\n", path, data.numChannels, data.rate, (int)data.numSamples);
        return data;
    }

    void writeWav(const char* path, const SoundData& data)
    {
        if (data.numChannels < 1) {
            printf("Illegal number of channels: %d\n", data.numChannels);
            exit(1);
        }
        if (data.rate < 1) {
            printf("Illegal rate: %d\n", data.rate);
            exit(1);
        }

        FILE* f = fopen(path, "w");
        if (!f) {
            printf("Unable to open %s\n", path);
            exit(1);
        }
        RiffHeader riffHeader;
        riffHeader.chunkId = kRiffChunkId;
        riffHeader.totalSize = 36 + data.getSamplesSize();
        riffHeader.format = kWaveFormat;
        if (fwrite(&riffHeader, 1, sizeof(riffHeader), f) != sizeof(riffHeader)) {
            printf("Cannot write %s\n", path);
            exit(1);
        }

        FmtHeader fmtHeader;
        fmtHeader.chunkId = kFmtChunkId;
        fmtHeader.chunkSize = 16;
        fmtHeader.sampleRate = data.rate;
        fmtHeader.numChannels = data.numChannels;
        switch (data.format) {
            case SampleFormat::Sint16:
                fmtHeader.formatTag = kWaveFormatTagPcm;
                break;
            case SampleFormat::Float:
                fmtHeader.formatTag = kWaveFormatTagFloat;
                break;
        }
        fmtHeader.byteRate = data.rate * data.numChannels * getSampleSize(data.format);
        fmtHeader.bitsPerSample = getSampleSize(data.format) * 8;
        fmtHeader.blockAlign = getSampleSize(data.format) * data.numChannels;
        if (fwrite(&fmtHeader, 1, sizeof(fmtHeader), f) != sizeof(fmtHeader)) {
            printf("Cannot write %s\n", path);
            exit(1);
        }

        GenericHeader header;
        header.chunkId = kDataChunkId;
        header.chunkSize = data.getSamplesSize();
        if (fwrite(&header, 1, sizeof(header), f) != sizeof(header)) {
            printf("Cannot write %s\n", path);
            exit(1);
        }

        if (fwrite(data.samples.get(), 1, header.chunkSize, f) != header.chunkSize) {
            printf("Cannot write %s\n", path);
            exit(1);
        }
    }
}

template<typename ST>
void prepareSineSamples(size_t numSamples, int waveHz, int rate, ST low, ST high, ST* data)
{
    ST ampl = (high - low) / 2;
    ST base = low + ampl;
    double sampleRad = 2 * M_PI * waveHz / rate;
    for (size_t i = 0; i < numSamples; i++) {
        data[i] = base + ST(sin(sampleRad * i) * ampl);
    }
}

SoundData prepareSine(int waveHz, double seconds, int rate, SampleFormat format)
{
    SoundData data;
    data.format = format;
    data.rate = rate;
    data.numChannels = 1;
    data.numSamples = (size_t)(seconds * rate);
    data.samples.reset(new uint8_t[data.getSamplesSize()]);
    switch (format) {
        case SampleFormat::Sint16:
            prepareSineSamples<int16_t>(data.numSamples, waveHz, rate, -10000, 10000, (int16_t*)data.samples.get());
            break;
        case SampleFormat::Float:
            prepareSineSamples<float>(data.numSamples, waveHz, rate, -0.1, 0.1, (float*)data.samples.get());
            break;
    }
    return data;
}

template<typename ST>
void stretchSoundSamples(const ST* src, size_t numSamples, int numChannels, double stretchBy,
                         ST* dst, size_t dstNumSamples)
{
    if (stretchBy <= 0) {
        printf("Cannot stretch by %g\n", stretchBy);
        exit(1);
    }

    // Each srcIndex += 1 corresponds to dstIndex += reciprocal.
    double reciprocal = 1.0 / stretchBy;
    // Fixed point emulation.
    size_t intPart = (size_t)reciprocal;
    double fracPart = reciprocal - intPart;

    size_t intPos = 0;
    // Invariant: fracPos and fracPart are always in range [0, 1).
    double fracPos = 0.0;

    for (size_t dstPos = 0; dstPos < dstNumSamples; dstPos++) {
        if (fracPos > 0.0) {
            if (intPos >= numSamples - 1) {
                printf("Out of bounds access: %d vs %d\n", (int)intPos, (int)numSamples);
                exit(1);
            }
            // Linear interpolation between src[intPos] and src[intPos + 1].
            for (int ch = 0; ch < numChannels; ch++) {
                dst[dstPos * numChannels + ch] = src[intPos * numChannels + ch] * (1 - fracPos)
                        + src[(intPos + 1) * numChannels + ch] * fracPos;
            }
        } else {
            if (intPos >= numSamples) {
                printf("Out of bounds access: %d vs %d\n", (int)intPos, (int)numSamples);
                exit(1);
            }
            for (int ch = 0; ch < numChannels; ch++) {
                dst[dstPos * numChannels + ch] = src[intPos * numChannels + ch];
            }
        }

        intPos += intPart;
        fracPos += fracPart;
        if (fracPos >= 1.0) {
            intPos++;
            fracPos -= 1.0;
        }
    }
}

SoundData stretchSound(const SoundData& src, double stretchBy)
{
    SoundData dst;
    dst.format = src.format;
    dst.rate = src.rate;
    dst.numChannels = src.numChannels;
    // Minus one due to interpolation requiring two src samples.
    dst.numSamples = (size_t)(src.numSamples * stretchBy) - 1;
    dst.samples.reset(new uint8_t[dst.getSamplesSize()]);

    switch (dst.format) {
        case SampleFormat::Sint16:
            stretchSoundSamples<int16_t>((int16_t*)src.samples.get(), src.numSamples, src.numChannels, stretchBy,
                                         (int16_t*)dst.samples.get(), dst.numSamples);
            break;
        case SampleFormat::Float:
            stretchSoundSamples<float>((float*)src.samples.get(), src.numSamples, src.numChannels, stretchBy,
                                       (float*)dst.samples.get(), dst.numSamples);
            break;
    }

    return dst;
}

void printUsage(const char* argv0)
{
    printf("Usage: %s [options]\n"
           "Options:\n"
           "\t--input-file FILE.WAV\t\t\tLoad source from WAV file\n"
           "\t--input-sine HZ\t\t\tGenerate source sine wave with given frequency\n"
           "\t--input-sine-rate RATE\t\t\tSet rate when generating source sine wave, 48000 by default\n"
           "\t--input-sine-fmt s16|f32\t\t\tSet format when generating source sine wave, float by default\n"
           "\t--output-file FILE.wav\t\tPath to resampled file, out.wav by default\n"
           "\t--stretch VALUE\t\tStretch by this value, 1.0 by default (no stretching)\n",
           argv0);
}

int main(int argc, char** argv)
{
    const char* inputPath = nullptr;
    const char* outputPath = "out.wav";
    int inputSineWaveHz = 0;
    int inputSineWaveRate = 48000;
    SampleFormat inputSineWaveFmt = SampleFormat::Sint16;
    float stretchBy = 1.0;

    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--input-sine") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --input-sine\n");
                return 1;
            }

            inputSineWaveHz = atoi(argv[i + 1]);
            if (inputSineWaveHz <= 0) {
                printf("Argument for --input-sine must be positive integer");
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--input-sine-rate") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --input-sine-rate\n");
                return 1;
            }

            inputSineWaveRate = atoi(argv[i + 1]);
            if (inputSineWaveRate <= 0) {
                printf("Argument for --input-sine-rate must be positive integer");
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--input-sine-fmt") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --input-sine-fmt\n");
                return 1;
            }

            if (strcmp(argv[i + 1], "s16") == 0) {
                inputSineWaveFmt = SampleFormat::Sint16;
            } else if (strcmp(argv[i + 1], "f32") == 0) {
                inputSineWaveFmt = SampleFormat::Float;
            } else {
                printf("Unknown format %s\n", argv[i + 1]);
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--input-file") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --input-file\n");
                return 1;
            }
            inputPath = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--output-file") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --output-file\n");
                return 1;
            }
            outputPath = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--stretch") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --stretch\n");
                return 1;
            }
            stretchBy = atof(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            printf("Unknown argument: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inputPath) {
        printf("Input: %s, output: %s, stretch: %g\n", inputPath, outputPath, stretchBy);
    } else {
        printf("Input: %dhz wave with %d rate, output: %s, stretch: %g\n", inputSineWaveHz, inputSineWaveRate, outputPath,
               stretchBy);
    }

    SoundData srcData;
    if (inputPath) {
        srcData = Wave::loadWav(inputPath);
    } else {
        srcData = prepareSine(inputSineWaveHz, 5.0, inputSineWaveRate, inputSineWaveFmt);
    }

    SoundData dstData = stretchSound(srcData, stretchBy);

    Wave::writeWav(outputPath, dstData);

    return 0;
}
