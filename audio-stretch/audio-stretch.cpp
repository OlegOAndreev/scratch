#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// General notes on various pitch-shifting methods: http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/.

#define kiss_fft_scalar float
#include "kiss_fft/kiss_fftr.h"

#include "common.h"

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
    FORCE_INLINE T* row(size_t n)
    {
        return begin + n * columns;
    }

    // Returns reference to the element at the position (row, column).
    FORCE_INLINE T& operator()(size_t row, size_t column)
    {
        return begin[row * columns + column];
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
            printf("%s has pcm format, but %d bits per sample with block align %d\n", path,
                   (int)fmtHeader.bitsPerSample, (int)fmtHeader.blockAlign);
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

// "Simple stretch": take src with number of samples and channels and do linear interpolation to write to dst.
// dstRemaining is used to check for buffer overflows.
template<typename ST, typename DT>
size_t simpleStretchImpl(const ST* src, size_t srcSamples, int numChannels, double stretchBy, DT* dst,
                         size_t dstRemaining)
{
    Span2d<const ST> srcS(src, srcSamples, numChannels);
    Span2d<DT> dstS(dst, dstRemaining, numChannels);
    if (stretchBy == 1.0) {
        if (srcSamples > dstRemaining) {
            printf("Overflow dst: %d vs %d\n", (int)srcSamples, (int)dstRemaining);
            exit(1);
        }
        for (size_t pos = 0; pos < srcSamples; pos++) {
            for (int ch = 0; ch < numChannels; ch++) {
                dstS(pos, ch) = srcS(pos, ch);
            }
        }
        return srcSamples;
    } else {
        // The condition is: dstToWrite is the largest X such that (X - 1) * srcDelta + 1 < srcSamples.
        // Assume that double precision is enough to ignore the rounding errors here (srcDelta being added several
        // times compared to srcDelta being multiplied by integer).
        size_t dstToWrite = (srcSamples - 1) * stretchBy + 0.999999;
        // Positions and deltas are encoded as int+float pair.
        size_t srcIntPos = 0;
        double srcFracPos = 0.0;
        double rcp = 1.0 / stretchBy;
        size_t srcIntDelta = (size_t)rcp;
        double srcFracDelta = rcp - srcIntDelta;
        for (size_t dstPos = 0; dstPos < dstToWrite; dstPos++) {
            // Sanity check that we computed dstToWrite correctly.
            if (srcIntPos + 1 >= srcSamples) {
                printf("Overflow src: %d vs %d\n", (int)(srcIntPos + 1), (int)srcSamples);
                exit(1);
            }
            for (int ch = 0; ch < numChannels; ch++) {
                dstS(dstPos, ch) = srcS(srcIntPos, ch) * (1 - srcFracPos) + srcS(srcIntPos + 1, ch) * srcFracPos;
            }
            srcIntPos += srcIntDelta;
            srcFracPos += srcFracDelta;
            if (srcFracPos >= 1.0) {
                // srcFracDelta <= 1.0, so fracPos < 2.0
                srcIntPos++;
                srcFracPos -= 1.0;
            }
        }
        return dstToWrite;
    }
}

template<typename T>
size_t simpleStretchSoundSamples(const T* src, size_t numSamples, int numChannels, double stretchBy,
                                 T* dst, size_t dstNumSamples)
{
    return simpleStretchImpl(src, numSamples, numChannels, stretchBy, dst, dstNumSamples);
}

// Helper struct for transforming blocks of data with STFT.
struct StftCfg
{
    size_t fftSize;
    size_t offset;
    vector<kiss_fft_scalar> srcBuf;
    vector<kiss_fft_cpx> freq;
    vector<kiss_fft_cpx> newFreq;
    vector<kiss_fft_scalar> dstBuf;
    vector<kiss_fft_scalar> window;
    kiss_fftr_cfg fftCfg;
    kiss_fftr_cfg fftiCfg;
    vector<double> prevPhases;
    bool hasLastPhases;
    vector<double> newMagnitudes;
    vector<double> prevNewPhases;
    vector<double> newPhaseDiffs;

    StftCfg(size_t fftSize, size_t offset)
        : fftSize(fftSize)
        , offset(offset)
    {
        srcBuf.resize(fftSize);
        freq.resize(fftSize / 2 + 1);
        newFreq.resize(fftSize / 2 + 1);
        dstBuf.resize(fftSize);
        window.resize(fftSize);
        fftCfg = kiss_fftr_alloc(fftSize, 0, nullptr, nullptr);
        fftiCfg = kiss_fftr_alloc(fftSize, 1, nullptr, nullptr);
        prevPhases.resize(fftSize / 2 + 1);
        hasLastPhases = false;
        newMagnitudes.resize(fftSize / 2 + 1);
        prevNewPhases.resize(fftSize / 2 + 1);
        newPhaseDiffs.resize(fftSize / 2 + 1);
    }

    ~StftCfg()
    {
        kiss_fftr_free(fftCfg);
        kiss_fftr_free(fftiCfg);
    }
};

// Generates an almost hann window, modified so that the overlap-add of windows is constant 1.
void fillHannWindow(kiss_fft_scalar* window, size_t fftSize, size_t offset)
{
    if (offset % 2 != 0) {
        printf("Odd offset: %d (fft size %d)\n", (int)offset, (int)fftSize);
        exit(1);
    }
    kiss_fft_scalar norm = (kiss_fft_scalar)offset / fftSize;
    for (size_t k = 0; k < fftSize; k++) {
        window[k] = -norm * cos(2 * M_PI * k / fftSize) + norm;
    }
}

// Returns the phaseDiff normalized back to [-M_PI, M_PI] range.
kiss_fft_scalar normalizePhaseDiff(kiss_fft_scalar phaseDiff)
{
    if (phaseDiff > -M_PI) {
        return fmod(phaseDiff + M_PI, M_PI * 2) - M_PI;
    } else {
        return M_PI + fmod(phaseDiff + M_PI, M_PI * 2);
    }
}

// Multiply all the frequencies by stretchBy, correct the phases from lastPhases.
void stretchFreq(StftCfg* cfg, double stretchBy)
{
    size_t freqSize = cfg->fftSize / 2 + 1;
    kiss_fft_cpx* freq = cfg->freq.data();
    double* prevPhases = cfg->prevPhases.data();
    double* newMagnitudes = cfg->newMagnitudes.data();
    double* newPhaseDiffs = cfg->newPhaseDiffs.data();
    double* prevNewPhases = cfg->prevNewPhases.data();
    double origPhaseMult = 2 * M_PI * cfg->offset / cfg->fftSize;

    std::fill(newMagnitudes, newMagnitudes + freqSize, 0.0);
    std::fill(newPhaseDiffs, newPhaseDiffs + freqSize, 0.0);

    for (size_t k = 0; k < freqSize; k++) {
        size_t newk = k * stretchBy;
        if (newk >= freqSize) {
            break;
        }

        double magn = sqrt(freq[k].r * freq[k].r + freq[k].i * freq[k].i);
        bool largeMagn = (magn > newMagnitudes[newk]);
        newMagnitudes[newk] += magn;
        double phase = atan2(freq[k].i, freq[k].r);
        if (cfg->hasLastPhases) {
            // Original phase diff is the difference between the potential phase (phase of the frequency bin k
            // at the end of the previous block) and the actual phase for the frequency bin k at the start
            // of the new block. This phase diff is then applied to the stretched frequencies. The final formula
            // is simplified a bit from the one from smbPitchShift.cpp
            double phaseDiff = normalizePhaseDiff(phase - prevPhases[k] - origPhaseMult * k);
            double newPhaseDiff = phaseDiff * stretchBy + (k * stretchBy - newk) * M_PI / 2.0;
            // If multiple old phase bins stretch into one new phase bin (if stretchBy < 1.0), we add their
            // magnitudes but want to want to choose only one phase (summing up phases from multiple bins
            // make no sense). Therefore, we separately compute newPhaseDiffs and then add them to prevNewPhases
            // in the end when finally computing newFreq. Ideally we would choose the phase from the bin with
            // the highest magnitude here.
            if (largeMagn) {
                newPhaseDiffs[newk] = origPhaseMult * newk + newPhaseDiff;
            }
        } else {
            // prevNewPhases contains zeroes in this case, so the final phase will be equal to phase.
            newPhaseDiffs[newk] = phase;
        }
        prevPhases[k] = phase;
    }
    cfg->hasLastPhases = true;

    kiss_fft_cpx* newFreq = cfg->newFreq.data();
    for (size_t k = 0; k < freqSize; k++) {
        prevNewPhases[k] += newPhaseDiffs[k];
        newFreq[k].r = newMagnitudes[k] * cos(prevNewPhases[k]);
        newFreq[k].i = newMagnitudes[k] * sin(prevNewPhases[k]);
    }
}

// Takes cfg->srcBuf, does STFT, changes pitch by stretchBy via frequencies and does inverse STFT to cfg->dstBuf.
void doStftPitchChange(StftCfg* cfg, double stretchBy)
{
    kiss_fftr(cfg->fftCfg, cfg->srcBuf.data(), cfg->freq.data());
    stretchFreq(cfg, stretchBy);
    kiss_fftri(cfg->fftiCfg, cfg->newFreq.data(), cfg->dstBuf.data());
}

// The simples STFT pitch shifter: first change frequency (shift frequencies for each individual STFT block separately)
// and then basically do the linear interpolation like the simple stretch.
// The idea is taken from http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
template<typename T>
size_t stftStretchSoundSamples(const T* src, size_t numSamples, int numChannels, double stretchBy, size_t fftSize,
                               T* dst, size_t dstNumSamples)
{
    // Offset of next block from to the previous one must always be fftSize / (n * 2) so that sum of offset
    // hann windows is always constant.
    size_t offset = fftSize / 4;

    Span2d<const T> srcV(src, numSamples, numChannels);
    Span2d<T> dstV(dst, numSamples, numChannels);
    StftCfg cfg(fftSize, offset);
    // STFT outputs are accumulated in circular buffer in dstAccumBuf. The part of dstAccumBuf, for which all
    // overlapped blocks have been summed, will be stretched and written to dst.
    vector<kiss_fft_scalar> dstAccumBuf;
    dstAccumBuf.resize(fftSize * numChannels);
    Span2d<kiss_fft_scalar> dstAccumBufS(dstAccumBuf.data(), fftSize, numChannels);

    size_t dstWritten = 0;

    kiss_fft_scalar* window = cfg.window.data();
    fillHannWindow(window, fftSize, offset);

    // Start with negative block offset, because each element must be overlapped by all possible offsets windows.
    // Real block offset is block - fftSize, but we do not want ot mess with unsigned integers here.
    for (size_t block = offset; block < fftSize; block += offset) {
        size_t prefix = fftSize - block;
        // read != fftSize-prefix holds only when the numSamples < fftSize-offset.
        size_t read = min(fftSize - prefix, numSamples);
        for (int ch = 0; ch < numChannels; ch++) {
            // Prepare srcBuf. The first part for negative block offsets is prefilled with zeros.
            kiss_fft_scalar *srcBuf = cfg.srcBuf.data();
            for (size_t k = 0; k < prefix; k++) {
                srcBuf[k] = 0.0;
            }
            for (size_t k = prefix; k < prefix + read; k++) {
                srcBuf[k] = (kiss_fft_scalar)srcV(k - prefix, ch) * window[k];
            }
            for (size_t k = prefix + read; k < fftSize; k++) {
                srcBuf[k] = 0.0;
            }

            doStftPitchChange(&cfg, stretchBy);

            // Write dstBuf to dstAccumBuf. For blocks with negative offset we never wrap around the dstAccumBuf,
            // always start writing at index 0.
            kiss_fft_scalar *dstBuf = cfg.dstBuf.data();
            for (size_t k = 0; k < read; k++) {
                dstAccumBufS(k, ch) += dstBuf[k + prefix] / fftSize;
            }
        }
    }

    // Process the full blocks.
    for (size_t block = 0; block < numSamples; block += offset) {
        size_t read = min(fftSize, numSamples - block);
        size_t accumStart = block % fftSize;
        for (int ch = 0; ch < numChannels; ch++) {
            // Prepare srcBuf.
            kiss_fft_scalar* srcBuf = cfg.srcBuf.data();
            for (size_t k = 0; k < read; k++) {
                srcBuf[k] = (kiss_fft_scalar)srcV(block + k, ch) * window[k];
            }
            for (size_t k = read; k < fftSize; k++) {
                srcBuf[k] = 0.0;
            }

            doStftPitchChange(&cfg, stretchBy);

            kiss_fft_scalar* dstBuf = cfg.dstBuf.data();
            // Write dstBuf to dstAccumBuf. We want to write dstBuf[k] to dstAccumBufS((block + k) % fftSize).
            // The last part dstBuf[fftSize - offset, fftSize) must overwrite, not add to the dstAccumBuf. Thus,
            // we have to do two or three separate loops, the last one overwriting, not adding the data.
            if (accumStart != 0) {
                // We wrap around the dstAccumBuf, do two loops when writing.
                for (size_t k = accumStart; k < fftSize; k++) {
                    dstAccumBufS(k, ch) += dstBuf[k - accumStart] / fftSize;
                }
                for (size_t k = 0; k < accumStart - offset; k++) {
                    dstAccumBufS(k, ch) += dstBuf[k + fftSize - accumStart] / fftSize;
                }
                for (size_t k = accumStart - offset; k < accumStart; k++) {
                    dstAccumBufS(k, ch) = dstBuf[k + fftSize - accumStart] / fftSize;
                }
            } else {
                for (size_t k = 0; k < fftSize - offset; k++) {
                    dstAccumBufS(k, ch) += dstBuf[k] / fftSize;
                }
                for (size_t k = fftSize - offset; k < fftSize; k++) {
                    dstAccumBufS(k, ch) = dstBuf[k] / fftSize;
                }
            }
        }

        // We can take the dstAccumBuf[accumStart:accumStart + offset], stretch and write it to the dst.
        size_t numOutput = min(offset, numSamples - block);
        // NOTE: There is a small problem here that each block is stretched individually, so there are discontinuities
        // between blocks. These discontinuities can be solved by saving and passing the last fractional position
        // to the next simpleStretchImpl call.
#if 1
        dstWritten += simpleStretchImpl(dstAccumBufS.row(accumStart), numOutput, numChannels, stretchBy,
                                        dstV.row(dstWritten), dstNumSamples - dstWritten);
#else
        dstWritten += simpleStretchImpl(dstAccumBufS.row(accumStart), numOutput, numChannels, 1.0,
                                        dstV.row(dstWritten), dstNumSamples - dstWritten);
#endif
    }

    return dstWritten;
}

template<typename T>
size_t doStretchSound(const T* src, size_t numSamples, int numChannels, double stretchBy, StretchMethod method,
                      size_t fftSize, T* dst, size_t dstNumSamples)
{
    switch (method) {
    case StretchMethod::Simple:
        return simpleStretchSoundSamples(src, numSamples, numChannels, stretchBy, dst, dstNumSamples);
    case StretchMethod::Stft:
        return stftStretchSoundSamples(src, numSamples, numChannels, stretchBy, fftSize, dst, dstNumSamples);
    }
}

struct Magnitude {
    double magnitude;
    size_t freq;
    kiss_fft_scalar r;
    kiss_fft_scalar i;
};

template<typename T>
void printFreq(T* data, size_t numSamples, int numChannels, int rate)
{
    size_t n = 1 << ((size_t)log2(numSamples) + 1);
    printf("Analyzing %d samples (channel 0)\n", (int)n);

    kiss_fftr_cfg cfg = kiss_fftr_alloc(n, 0, nullptr, nullptr);
    vector<kiss_fft_scalar> dataCopy;
    dataCopy.resize(n);
    for (size_t k = 0; k < min(n, numSamples); k++) {
        dataCopy[k] = (kiss_fft_scalar)data[k * numChannels];
    }
    vector<kiss_fft_cpx> freq;
    freq.resize(n / 2 + 1);
    kiss_fftr(cfg, dataCopy.data(), freq.data());

    vector<Magnitude> magnitudes;
    magnitudes.resize(n / 2 + 1);
    for (size_t k = 0; k < n / 2 + 1; k++) {
        magnitudes[k].magnitude = sqrt(freq[k].r * freq[k].r + freq[k].i * freq[k].i);
        magnitudes[k].freq = k;
        magnitudes[k].r = freq[k].r;
        magnitudes[k].i = freq[k].i;
#if 0
        printf("[%d (%d-%dHz)]: %g (%g %g)\n", (int)k, (int)(k * rate / n), (int)((k + 1) * rate / n)
               magnitudes[k].magnitude, freq[k].r, freq[k].i);
#endif
    }
    std::sort(magnitudes.begin(), magnitudes.end(), [] (const Magnitude& m1, const Magnitude& m2) {
        return m1.magnitude > m2.magnitude;
    });
    for (size_t k = 0; k < 15; k++) {
        printf("top %d: %d (%d-%dHz) %g (%g %g: %g)\n", (int)k, (int)magnitudes[k].freq,
               (int)(magnitudes[k].freq * rate / n), (int)((magnitudes[k].freq + 1) * rate / n),
               magnitudes[k].magnitude, magnitudes[k].r, magnitudes[k].i,
               atan2(magnitudes[k].i, magnitudes[k].r));
    }

    kiss_fftr_free(cfg);
}

SoundData stretchSound(const SoundData& src, double stretchBy, StretchMethod method, size_t fftSize)
{
    SoundData dst;
    dst.format = src.format;
    dst.rate = src.rate;
    dst.numChannels = src.numChannels;
    // NOTE: Calculating the actual number of samples, which will be written while stretching, can be quite
    // error-prone due to floating point math errors. The easy way: overallocate the dst buffer and get
    // the number of samples actually written from the stretching function.
    dst.numSamples = src.numSamples * stretchBy * 1.1 + 1;
    dst.samples.resize(dst.getByteLength());

    size_t dstWritten;
    switch (dst.format) {
    case SampleFormat::Sint16:
        dstWritten = doStretchSound((int16_t*)src.samples.data(), src.numSamples, src.numChannels, stretchBy, method,
                                    fftSize, (int16_t*)dst.samples.data(), dst.numSamples);
        break;
    case SampleFormat::Float:
        dstWritten = doStretchSound((float*)src.samples.data(), src.numSamples, src.numChannels, stretchBy, method,
                                    fftSize, (float*)dst.samples.data(), dst.numSamples);
        break;
    }

    // Sanity check.
    if (dstWritten > dst.numSamples) {
        printf("ERROR: Number of samples written is greater than the allocated: %d vs %d\n",
               (int)dstWritten, (int)dst.numSamples);
        exit(1);
    }
    dst.numSamples = dstWritten;

#if 1
    switch (dst.format) {
    case SampleFormat::Sint16:
        printFreq((int16_t*)dst.samples.data(), dstWritten, dst.numChannels, dst.rate);
        break;
    case SampleFormat::Float:
        printFreq((float*)dst.samples.data(), dstWritten, dst.numChannels, dst.rate);
        break;
    }
#endif

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
           "\t--fft-size SIZE\t\t\tSize of the FFT to be used (only if stft method is specified, 4096 by default.\n"
           " neareset frequency.\n",
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
    double stretchBy = 1.0;
    StretchMethod method = StretchMethod::Stft;
    int fftSize = 4096;

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
        printf("Input: %dhz wave with %d rate, output: %s, stretch: %g\n", inputSineWaveHz, inputSineWaveRate,
               outputPath, stretchBy);
    }
    switch (method) {
    case StretchMethod::Simple:
        printf("Method: simple\n");
        break;
    case StretchMethod::Stft:
        printf("Method: stft, fft size: %d\n", (int)fftSize);
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

    SoundData dstData = stretchSound(srcData, stretchBy, method, fftSize);

    Wave::writeWav(outputPath, dstData);

    return 0;
}
