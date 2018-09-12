#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// General notes on various pitch-shifting methods: http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/.

#define kiss_fft_scalar float
#include "kiss_fft/kiss_fftr.h"

using std::max;
using std::min;
using std::vector;

enum class SampleFormat {
    Sint16,
    Float
};

enum class StretchMethod {
    Simple,
    Stft
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
    vector<uint8_t> samples;

    size_t getByteLength() const
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
            data.samples.resize(header.chunkSize);
            size_t readBytes = fread(data.samples.data(), 1, header.chunkSize, f);
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
    riffHeader.totalSize = 36 + data.getByteLength();
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
    header.chunkSize = data.getByteLength();
    if (fwrite(&header, 1, sizeof(header), f) != sizeof(header)) {
        printf("Cannot write %s\n", path);
        exit(1);
    }

    if (fwrite(data.samples.data(), 1, header.chunkSize, f) != header.chunkSize) {
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
    data.samples.resize(data.getByteLength());
    switch (format) {
    case SampleFormat::Sint16:
        prepareSineSamples<int16_t>(data.numSamples, waveHz, rate, -10000, 10000, (int16_t*)data.samples.data());
        break;
    case SampleFormat::Float:
        prepareSineSamples<float>(data.numSamples, waveHz, rate, -0.3, 0.3, (float*)data.samples.data());
        break;
    }
    return data;
}

// Returns the number of samples that would be generated by stretchImpl() from source buffer size n.
size_t numStretchedSamples(size_t n, float stretchBy)
{
    // Minus one due to interpolation requiring two src samples.
    return (size_t)((n - 1) * (double)stretchBy) + 1;
}

template<typename ST>
void stretchImpl(const ST* src, size_t srcN, int numChannels, float stretchBy, ST* dst, size_t dstN)
{
    // Each srcIndex += 1 corresponds to dstIndex += rcp.
    float rcp = 1.0f / stretchBy;
    // Fixed point emulation, positions and deltas are encoded as int+float pair.
    size_t intPart = (size_t)rcp;
    float fracPart = rcp - intPart;

    size_t intPos = 0;
    // Set initial pos so that the original and stretched sequences are cocentric.
    float fracPos = ((srcN - 1) - (dstN - 1) / (double)stretchBy) / 2.0f;
    if (fracPos < 0 || fracPos >= 1.0f) {
        printf("Unmatched sizes: %d vs %g\n", (int)(srcN - 1), (dstN - 1) / (double)stretchBy);
        exit(1);
    }

    for (size_t dstPos = 0; dstPos < dstN; dstPos++) {
        if (fracPos > 0.0) {
            if (intPos >= srcN - 1) {
                printf("Out of bounds access: %d vs %d\n", (int)intPos, (int)srcN);
                exit(1);
            }
            // Linear interpolation between src[intPos] and src[intPos + 1].
            for (int ch = 0; ch < numChannels; ch++) {
                dst[dstPos * numChannels + ch] = src[intPos * numChannels + ch] * (1 - fracPos)
                        + src[(intPos + 1) * numChannels + ch] * fracPos;
            }
        } else {
            if (intPos >= srcN) {
                printf("Out of bounds access: %d vs %d\n", (int)intPos, (int)srcN);
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

template<typename ST>
void simpleStretchSoundSamples(const ST* src, size_t numSamples, int numChannels, float stretchBy,
                               ST* dst, size_t dstNumSamples)
{
    stretchImpl(src, numSamples, numChannels, stretchBy, dst, dstNumSamples);
}

// Multiply all the frequencies by stretchBy.
void stretchFreq(const kiss_fft_scalar* freq, kiss_fft_scalar* newFreq, size_t freqSize, float stretchBy, bool interpolateFreq)
{
    std::fill(newFreq, newFreq + freqSize, 0.0);
    if (interpolateFreq) {
        for (size_t k = 0; k < freqSize; k++) {
            float newk = k * stretchBy;
            size_t intPos = (size_t)newk;
            float fracPos = newk - intPos;
            // Simply ignore the last newk, which is < freqSize, this is no biggy.
            if (intPos + 1 >= freqSize) {
                break;
            }
            newFreq[intPos] += freq[k] * (1 - fracPos);
            newFreq[intPos + 1] += freq[k] * fracPos;
        }
    } else {
        for (size_t k = 0; k < freqSize; k++) {
            size_t newk = k * stretchBy;
            if (newk >= freqSize) {
                break;
            }
            newFreq[newk] += freq[k];
        }
    }
}

// Multiply all the frequencies by stretchBy.
void stretchFreq(const kiss_fft_cpx* freq, kiss_fft_cpx* newFreq, size_t freqSize, float stretchBy, bool interpolateFreq)
{
    kiss_fft_cpx zero{0.0, 0.0};
    std::fill(newFreq, newFreq + freqSize, zero);
    if (interpolateFreq) {
        for (size_t k = 0; k < freqSize; k++) {
            float newk = k * stretchBy;
            size_t intPos = (size_t)newk;
            float fracPos = newk - intPos;
            // Simply ignore the last newk, which is < freqSize, this is no biggy.
            if (intPos + 1 >= freqSize) {
                break;
            }
            newFreq[intPos].r += freq[k].r * (1 - fracPos);
            newFreq[intPos].i += freq[k].i * (1 - fracPos);
            newFreq[intPos + 1].r += freq[k].i * fracPos;
            newFreq[intPos + 1].r += freq[k].i * fracPos;
        }
    } else {
        for (size_t k = 0; k < freqSize; k++) {
            size_t newk = k * stretchBy;
            if (newk >= freqSize) {
                break;
            }
            newFreq[newk].r += freq[k].r;
            newFreq[newk].i += freq[k].i;
        }
    }
}

// A simple array_view-like structure, presenting a linear array as a row-major matrix.
template <typename T>
struct Span2d {
    T* begin;
    size_t rows;
    size_t columns;

    Span2d(T* begin, size_t rows, size_t columns)
        : begin(begin)
        , rows(rows)
        , columns(columns)
    {
    }

    // Returns pointer to the row n.
    T* row(size_t n)
    {
        return begin + n * columns;
    }

    // Returns reference to the element at the position (row, column).
    T& operator()(size_t row, size_t column)
    {
        return begin[row * columns + column];
    }
};

// MDCT is useless here.
#if 0
// Implementation of MDCT transform.
//
// References:
//  * https://arxiv.org/pdf/0708.4399.pdf
//  * http://read.pudn.com/downloads151/doc/653574/mdct_e.pdf
//  * https://www.appletonaudio.com/blog/2013/derivation-of-fast-dct-4-algorithm-based-on-dft/
//  * https://www.appletonaudio.com/blog/2013/understanding-the-modified-discrete-cosine-transform-mdct/
//  * https://www.dsprelated.com/showcode/196.php
//  * https://gist.github.com/rygorous/2e27c3e2277a283a4f829b43b0d597e4
//  * https://github.com/libav/libav/blob/master/libavcodec/mdct_template.c
struct MdctConfig
{
    size_t fftSize;
    kiss_fft_cfg fftCfg;
    bool withWindow;
    vector<kiss_fft_scalar> window;
    vector<kiss_fft_cpx> twiddles;
    vector<kiss_fft_cpx> tmpSrc;
    vector<kiss_fft_cpx> tmpDst;

    // Initializes new MDCT transform, n is the number of frequencies, the source must have nFreq * 2 elements.
    // If withWindow is true, uses standard windowing function, sin(pi * (n + 0.5) / 2*N).
    MdctConfig(size_t fftSize, bool withWindow)
        : fftSize(fftSize)
        , withWindow(withWindow)
    {
        fftCfg = kiss_fft_alloc(fftSize / 2, 0, nullptr, nullptr);
        // Prepare twiddles.
        twiddles.resize(fftSize);
        float alpha = M_PI / (fftSize * 8.);
        float omega = M_PI / fftSize;
        float scale = sqrt(sqrt(2. / fftSize));
        window.resize(fftSize * 2);
        if (withWindow) {
            // The most basic window for MDCT.
            for (size_t k = 0; k < fftSize * 2; k++) {
                window[k] = sin(M_PI * (k + 0.5) / (fftSize * 2));
            }
        } else {
            for (size_t k = 0; k < fftSize * 2; k++) {
                window[k] = 1.0;
            }
        }
        for (size_t k = 0; k < fftSize / 2; k++) {
            twiddles[k].r = cos(omega * k + alpha) * scale;
            twiddles[k].i = -sin(omega * k + alpha) * scale;
        }
        tmpSrc.resize(fftSize);
        tmpDst.resize(fftSize);
    }


    // Runs mdst for src, placing the results in freq.src length must be n * 2, freq length must be n.
    void mdct(const kiss_fft_scalar* src, kiss_fft_scalar* freq)
    {
        // Prepare tmpSrc for fft by converting MDCT -> DCT4 and multiplying by twiddles.
        size_t fftSize2 = fftSize / 2;
        size_t fftSize32 = fftSize * 3 / 2;
        size_t fftSize52 = fftSize * 5 / 2;
        size_t fftSize4 = fftSize / 4;
#define SRC(idx) src[idx] * window[idx]
        for (size_t k = 0; k < fftSize4; k++) {
            kiss_fft_scalar re = -SRC(fftSize32 - 1 - k * 2) - SRC(fftSize32 + k * 2);
            kiss_fft_scalar im = SRC(fftSize2 - 1 - k * 2) - SRC(fftSize2 + k * 2);
            kiss_fft_scalar twre = twiddles[k].r;
            kiss_fft_scalar twim = twiddles[k].i;
            tmpSrc[k].r = re * twre - im * twim;
            tmpSrc[k].i = re * twim + im * twre;
        }
        for (size_t k = fftSize4; k < fftSize2; k++) {
            kiss_fft_scalar re = SRC(k * 2 - fftSize2) - SRC(fftSize32 - 1 - k * 2);
            kiss_fft_scalar im = -SRC(fftSize2 + k * 2) - SRC(fftSize52 - 1 - k * 2);
            kiss_fft_scalar twre = twiddles[k].r;
            kiss_fft_scalar twim = twiddles[k].i;
            tmpSrc[k].r = re * twre - im * twim;
            tmpSrc[k].i = re * twim + im * twre;
        }
#undef SRC
        kiss_fft(fftCfg, tmpSrc.data(), tmpDst.data());
        // Post-twiddle multiplication (nice trick to use one twiddle both for pre- and post-multiplication) and shuffling.
        for (size_t k = 0; k < fftSize2; k++) {
            kiss_fft_scalar re = tmpDst[k].r;
            kiss_fft_scalar im = tmpDst[k].i;
            kiss_fft_scalar twre = twiddles[k].r;
            kiss_fft_scalar twim = twiddles[k].i;
            freq[k * 2] = re * twre - im * twim;
            freq[fftSize - 1 - k * 2] = -re * twim - im * twre;
        }
    }

    // Runs imdct on freq, overlap-adding results to dst. This assumes that first half (n) elements in dst are already initialized
    // and will be added to, the second half will be overwritten.
    void imdct(const kiss_fft_scalar* freq, kiss_fft_scalar* dst)
    {
        // Prepare tmpSrc for fft by multiplying by twiddles.
        size_t fftSize2 = fftSize / 2;
        size_t fftSize32 = fftSize * 3 / 2;
        size_t fftSize52 = fftSize * 5 / 2;
        size_t fftSize4 = fftSize / 4;
        for (size_t k = 0; k < fftSize2; k++) {
            kiss_fft_scalar re = freq[k * 2];
            kiss_fft_scalar im = freq[fftSize - 1 - k * 2];
            kiss_fft_scalar twre = twiddles[k].r;
            kiss_fft_scalar twim = twiddles[k].i;
            tmpSrc[k].r = -re * twre + im * twim;
            tmpSrc[k].i = -re * twim - im * twre;
        }
        kiss_fft(fftCfg, tmpSrc.data(), tmpDst.data());
        // Post-twiddle and summing with existing data.
#define DST_SET(idx, value) dst[idx] = value * window[idx]
#define DST_ADD(idx, value) dst[idx] += value * window[idx]
        for (size_t k = 0; k < fftSize4; k++) {
            kiss_fft_scalar re = tmpDst[k].r;
            kiss_fft_scalar im = tmpDst[k].i;
            kiss_fft_scalar twre = twiddles[k].r;
            kiss_fft_scalar twim = twiddles[k].i;
            kiss_fft_scalar re1 = re * twre - im * twim;
            kiss_fft_scalar im1 = -re * twim - im * twre;
            DST_SET(fftSize32 - 1 - k * 2, re1);
            DST_SET(fftSize32 + k * 2, re1);
            DST_ADD(fftSize2 + k * 2, im1);
            DST_ADD(fftSize2 - 1 - k * 2, -im1);
        }
        for (size_t k = fftSize4; k < fftSize2; k++) {
            kiss_fft_scalar re = tmpDst[k].r;
            kiss_fft_scalar im = tmpDst[k].i;
            kiss_fft_scalar twre = twiddles[k].r;
            kiss_fft_scalar twim = twiddles[k].i;
            kiss_fft_scalar re1 = re * twre - im * twim;
            kiss_fft_scalar im1 = -re * twim - im * twre;
            DST_ADD(fftSize32 - 1 - k * 2, re1);
            DST_ADD(k * 2 - fftSize2, -re1);
            DST_SET(fftSize2 + k * 2, im1);
            DST_SET(fftSize52 - 1 - k * 2, im1);
        }
#undef DST_SET
#undef DST_ADD
    }
};

// MDCT version is very primitive: does mdct for each channel separately, stretches each frequency buffer via stretchFreq
// and converts back. Obvious missed optimizations:
//  * not copying the data around when ST == kiss_fft_scalar and numChannels == 1;
//  * not shifting the srcBuf+dstBuf to the left, just doing the modular arithmetic in mdct/imdct functions.
template<typename ST>
void mdctStretchSoundSamples(const ST* src, size_t numSamples, int numChannels, float stretchBy, size_t fftSize,
                             bool interpolateFreq, ST* dst)
{
    MdctConfig cfg(fftSize, true);
    // srcBuf and dstBuf store two blocks at a time as required by mdct. The second block is then moved over the first one
    // after being processed.
    vector<kiss_fft_scalar> srcBuf(fftSize * 2 * numChannels);
    vector<kiss_fft_scalar> dstBuf(fftSize * 2 * numChannels);
    vector<kiss_fft_scalar> freq(fftSize * numChannels);
    vector<kiss_fft_scalar> newFreq(fftSize * numChannels);
    // Views to numSamples x numChannels data, src and dst are stored by samples, srcBuf, dstBuf and freq are stored by channels.
    Span2d<const ST> srcS(src, numSamples, numChannels);
    Span2d<ST> dstS(dst, numSamples, numChannels);
    Span2d<kiss_fft_scalar> srcBufS(srcBuf.data(), numChannels, fftSize * 2);
    Span2d<kiss_fft_scalar> dstBufS(dstBuf.data(), numChannels, fftSize * 2);
    Span2d<kiss_fft_scalar> freqS(freq.data(), numChannels, fftSize);
    Span2d<kiss_fft_scalar> newFreqS(newFreq.data(), numChannels, fftSize);

    // Process all src and dst in mdctSize-sized blocks. The first and the last mdctSize blocks will be filled with zeros,
    // so the loop is until numSamples + mdctSize.
    for (size_t block = 0; block < numSamples + fftSize; block += fftSize) {
        // Prepare the new block in part srcBuf[ch][mdctSize..mdctSize * 2).
        if (block < numSamples) {
            size_t n = min(numSamples - block, fftSize);
            // Scatter src channels from samples to blocks.
            for (int ch = 0; ch < numChannels; ch++) {
                for (size_t i = 0; i < n; i++) {
                    srcBufS(ch, fftSize + i) = (kiss_fft_scalar)srcS(block + i, ch);
                }
                // Is run only for last not-full block.
                for (size_t i = n; i < fftSize; i++) {
                    srcBufS(ch, fftSize + i) = 0.0;
                }
            }
        } else {
            // The first and last blocks will be always zeroes. This fills the last block (the first block is initialized when
            // initializing the srcBuf).
            for (int ch = 0; ch < numChannels; ch++) {
                kiss_fft_scalar* srcBufCh = srcBuf.data() + fftSize * 2 * ch;
                for (size_t i = 0; i < fftSize; i++) {
                    srcBufCh[fftSize + i] = 0.0;
                }
            }
        }

        for (int ch = 0; ch < numChannels; ch++) {
            // Convert the srcBuf[ch] to frequency data.
            cfg.mdct(srcBufS.row(ch), freqS.row(ch));

            // Stretch the frequency.
            stretchFreq(freqS.row(ch), newFreqS.row(ch), fftSize, stretchBy, interpolateFreq);

            // Convert the modified frequency back to dstBuf[ch][0..mdctSize * 2), the required data is in dstBuf[ch][0..mdctSize).
            cfg.imdct(newFreqS.row(ch), dstBufS.row(ch));
        }

        // Copy the results for the previous block to dst (which has now been overlap-added by imdct).
        // The first block is ignored.
        if (block > 0) {
            size_t n = min(fftSize, numSamples - block + fftSize);
            // Gather channels into samples.
            for (int ch = 0; ch < numChannels; ch++) {
                for (size_t i = 0; i < n; i++) {
                    dstS(block - fftSize + i, ch) = (ST)dstBufS(ch, i);
                }
            }
        }

        // Move the current block to the left (previous block).
        for (int ch = 0; ch < numChannels; ch++) {
            kiss_fft_scalar* srcBufCh = srcBufS.row(ch);
            kiss_fft_scalar* dstBufCh = dstBufS.row(ch);
            std::copy(srcBufCh + fftSize, srcBufCh + fftSize * 2, srcBufCh);
            std::copy(dstBufCh + fftSize, dstBufCh + fftSize * 2, dstBufCh);
        }
    }
}
#endif

// The simples STFT pitch shifter: shift frequencies for each individual STFT block separately.
template<typename ST>
void stftStretchSoundSamples(const ST* src, size_t numSamples, int numChannels, float stretchBy, size_t fftSize,
                             bool interpolateFreq, ST* dst)
{
    // Offset of next block compared to the previous one. NOTE: When updating offset, update window function as well.
    size_t offset = fftSize / 4;
    Span2d<const ST> srcS(src, numSamples, numChannels);
    Span2d<ST> dstS(dst, numSamples, numChannels);
    vector<kiss_fft_scalar> srcBuf;
    srcBuf.resize(fftSize);
    vector<kiss_fft_scalar> dstBuf;
    dstBuf.resize(fftSize);
    vector<kiss_fft_cpx> freq;
    freq.resize(fftSize / 2 + 1);
    vector<kiss_fft_cpx> newFreq;
    newFreq.resize(fftSize / 2 + 1);

    kiss_fftr_cfg fftCfg = kiss_fftr_alloc(fftSize, 0, nullptr, nullptr);
    kiss_fftr_cfg fftiCfg = kiss_fftr_alloc(fftSize, 1, nullptr, nullptr);

    vector<kiss_fft_scalar> window;
    window.resize(fftSize);
    for (size_t k = 0; k < fftSize; k++) {
        // Almost hamming window, modified so that the overlap-add of windows is constant 1.
        // NOTE: When updating the offset, update this window as well.
        window[k] = -0.25 * cos(2 * M_PI * k / fftSize) + 0.25;
    }

    // Prefill the dst with zeros so that we can add to elements safely later.
    std::fill(dst, dst + numSamples * numChannels, (ST)0);

    // Run the first few blocks starting with negative indices so that the src[0] is overlapped the required amount of times.
    // We have to do only fftSize / offset - 1 iterations (if we do fftSize / offset iterations, the first will be all zeroes,
    // hardly interesting).
    for (size_t negBlock = offset; negBlock < fftSize; negBlock += offset) {
        size_t start = fftSize - negBlock;
        for (int ch = 0; ch < numChannels; ch++) {
            for (size_t k = 0; k < start; k++) {
                srcBuf[k] = 0.0;
            }
            // read != fftSize-start only when the numSamples < fftSize-offset.
            size_t read = min(fftSize - start, numSamples) + start;
            for (size_t k = start; k < read; k++) {
                srcBuf[k] = (kiss_fft_scalar)srcS(k - start, ch) * window[k];
            }
            for (size_t k = read; k < fftSize; k++) {
                srcBuf[k] = 0.0;
            }
            kiss_fftr(fftCfg, srcBuf.data(), freq.data());

            // Do processing with freq.
            stretchFreq(freq.data(), newFreq.data(), fftSize / 2 + 1, stretchBy, interpolateFreq);

            kiss_fftri(fftiCfg, newFreq.data(), dstBuf.data());
            for (size_t k = start; k < read; k++) {
                dstS(k - start, ch) += (ST)(dstBuf[k] / fftSize);
            }
        }
    }

    // Do the primary loop.
    for (size_t block = 0; block < numSamples; block += offset) {
        size_t read = min(fftSize, numSamples - block);
        for (int ch = 0; ch < numChannels; ch++) {
            for (size_t k = 0; k < read; k++) {
                srcBuf[k] = (kiss_fft_scalar)srcS(block + k, ch) * window[k];
            }
            for (size_t k = read; k < fftSize; k++) {
                srcBuf[k] = 0.0;
            }
            kiss_fftr(fftCfg, srcBuf.data(), freq.data());

            // Do processing with freq.
            stretchFreq(freq.data(), newFreq.data(), fftSize / 2 + 1, stretchBy, interpolateFreq);

            kiss_fftri(fftiCfg, newFreq.data(), dstBuf.data());
            for (size_t k = 0; k < read; k++) {
                dstS(block + k, ch) += (ST)(dstBuf[k] / fftSize);
            }
        }
    }
}

template<typename ST>
void doStretchSound(const ST* src, size_t numSamples, int numChannels, float stretchBy, StretchMethod method,
                    size_t fftSize, bool interpolateFreq, ST* dst)
{
    switch (method) {
    case StretchMethod::Simple:
        simpleStretchSoundSamples(src, numSamples, numChannels, stretchBy, dst,
                                      numStretchedSamples(numSamples, stretchBy));
        break;
    case StretchMethod::Stft:
        stftStretchSoundSamples(src, numSamples, numChannels, stretchBy, fftSize, interpolateFreq, dst);
        break;
    }
}

SoundData stretchSound(const SoundData& src, float stretchBy, StretchMethod method, size_t fftSize,
                       bool interpolateFreq)
{
    SoundData dst;
    dst.format = src.format;
    dst.rate = src.rate;
    dst.numChannels = src.numChannels;

    if (method != StretchMethod::Simple) {
        dst.numSamples = src.numSamples;
    } else {
        dst.numSamples = numStretchedSamples(src.numSamples, stretchBy);
    }
    dst.samples.resize(dst.getByteLength());

    switch (dst.format) {
    case SampleFormat::Sint16:
        doStretchSound((int16_t*)src.samples.data(), src.numSamples, src.numChannels, stretchBy, method, fftSize, interpolateFreq,
                       (int16_t*)dst.samples.data());
        break;
    case SampleFormat::Float:
        doStretchSound((float*)src.samples.data(), src.numSamples, src.numChannels, stretchBy, method, fftSize, interpolateFreq,
                       (float*)dst.samples.data());
        break;
    }

    return dst;
}

void printUsage(const char* argv0)
{
    printf("Usage: %s [options]\n"
           "Options:\n"
           "\t--input-file FILE.WAV\t\tLoad source from WAV file\n"
           "\t--input-sine HZ\t\t\tGenerate source sine wave with given frequency\n"
           "\t--input-sine-length SEC\t\tLength of sine wave in seconds, 5 by default\n"
           "\t--input-sine-rate RATE\t\tSet rate when generating source sine wave, 48000 by default\n"
           "\t--input-sine-fmt s16|f32\tSet format when generating source sine wave, float by default\n"
           "\t--output-file FILE.wav\t\tPath to resampled file, out.wav by default\n"
           "\t--stretch VALUE\t\t\tStretch by this value, 1.0 by default (no stretching)\n"
           "\t--method METHOD\t\t\tWhich method to use for stretching: simple (do not preserve time, default), stft\n"
           "\t--fft-size SIZE\t\t\tSize of the FFT to be used (only if stft method is specified, 1024 by default.\n"
           "\t--interpolate-freq\t\tIf enabled, interpolate frequency when stretching via stft method, otherwise use neareset frequency.\n",
           argv0);
}

int main(int argc, char** argv)
{
    const char* inputPath = nullptr;
    const char* outputPath = "out.wav";
    int inputSineWaveHz = 0;
    int inputSineWaveRate = 48000;
    double inputSineLength = 5.0;
    SampleFormat inputSineWaveFmt = SampleFormat::Float;
    float stretchBy = 1.0;
    StretchMethod method = StretchMethod::Stft;
    int fftSize = 1024;
    bool interpolateFreq = false;

    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--input-sine") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --input-sine\n");
                return 1;
            }

            inputSineWaveHz = atoi(argv[i + 1]);
            if (inputSineWaveHz <= 0) {
                printf("Argument for --input-sine must be positive integer\n");
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--input-sine-length") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --input-sine-length\n");
                return 1;
            }

            inputSineLength = atof(argv[i + 1]);
            if (inputSineLength <= 0) {
                printf("Argument for --input-sine-length must be positive\n");
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
                printf("Argument for --input-sine-rate must be positive integer\n");
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
        } else if (strcmp(argv[i], "--method") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --method\n");
                return 1;
            }
            if (strcmp(argv[i + 1], "simple") == 0) {
                method = StretchMethod::Simple;
            } else if (strcmp(argv[i + 1], "stft") == 0) {
                method = StretchMethod::Stft;
            } else {
                printf("Unknown method %s\n", argv[i + 1]);
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--fft-size") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --fft-size\n");
                return 1;
            }
            fftSize = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--interpolate-freq") == 0) {
            interpolateFreq = true;
            i++;
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
    switch (method) {
    case StretchMethod::Simple:
        printf("Method: simple\n");
        break;
    case StretchMethod::Stft:
        printf("Method: stft, fft size: %d, interpolate freq: %s\n", (int)fftSize, interpolateFreq ? "true" : "false");
        break;
    }

    if (stretchBy <= 0) {
        printf("Cannot stretch by %g\n", stretchBy);
        exit(1);
    }
    if (fftSize <= 0) {
        printf("Incorrect FFT size: %d\n", fftSize);
        exit(1);
    }

    SoundData srcData;
    if (inputPath) {
        srcData = Wave::loadWav(inputPath);
    } else {
        srcData = prepareSine(inputSineWaveHz, inputSineLength, inputSineWaveRate, inputSineWaveFmt);
    }

    SoundData dstData = stretchSound(srcData, stretchBy, method, fftSize, interpolateFreq);

    Wave::writeWav(outputPath, dstData);

    return 0;
}
