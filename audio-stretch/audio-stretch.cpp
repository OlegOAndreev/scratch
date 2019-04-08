#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <queue>
#include <vector>

#define kiss_fft_scalar float
#include "kiss_fft/kiss_fftr.h"

#include "common.h"


// General notes on various pitch-shifting methods:
// http://blogs.zynaptiq.com/bernsee/time-pitch-overview/
//
// Basic phase vocoder implementation: http://downloads.dspdimension.com/smbPitchShift.cpp
//
// Desription of phase gradient approach to improve the phase vocoder: Phase Vocoder Done Right,
// Zdeneˇk Pru ̊ša and Nicki Holighaus, Acoustics Research Institute, Austrian Academy
// of Sciences Vienna, Austria

enum class SampleFormat {
    Sint16,
    Float
};

enum class StretchMethod {
    Simple,
    Stft
};

struct StretchParams {
    double pitchShift = 1.0;
    double timeStretch = 1.0;
    int rate = 0;
    size_t fftSize = 2048;
    int overlap = 4;
    bool phaseGradient = false;
};

size_t getSampleSize(SampleFormat format)
{
    switch (format) {
    case SampleFormat::Sint16:
        return 2;
    case SampleFormat::Float:
        return 4;
    default:
        ENSURE(false, "Not supported format");
    }
}

struct SoundData {
    SampleFormat format = SampleFormat::Sint16;
    int rate = 0;
    int numChannels = 0;
    size_t numSamples = 0;
    std::vector<uint8_t> samples;

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

    Span2d(T* begin_, size_t rows_, size_t columns_)
        : begin(begin_)
        , rows(rows_)
        , columns(columns_)
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
            printf("%s has float format, but %d bits per sample %d\n", path,
                   (int)fmtHeader.bitsPerSample, (int)fmtHeader.blockAlign);
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
                printf("%s has less bytes than required: %d vs %d\n", path, (int)readBytes,
                       (int)header.chunkSize);
                exit(1);
            }
            break;
        }
        fseek(f, header.chunkSize, SEEK_CUR);
    }

    printf("%s: %d ch, rate %d, %d samples\n", path, data.numChannels, data.rate,
           (int)data.numSamples);
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

} // namespace Wave

template<typename ST>
void prepareSineSamples(size_t numSamples, int waveHz, int rate, ST low, ST high, ST* data)
{
    ST ampl = (high - low) / 2;
    ST base = low + ampl;
    double sampleRad = 2 * M_PI * waveHz / rate;
    for (size_t i = 0; i < numSamples; i++) {
        data[i] = base + ST(std::sin(sampleRad * i) * ampl);
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
        prepareSineSamples<int16_t>(data.numSamples, waveHz, rate, -10000, 10000,
                                    (int16_t*)data.samples.data());
        break;
    case SampleFormat::Float:
        prepareSineSamples<float>(data.numSamples, waveHz, rate, -0.3, 0.3,
                                  (float*)data.samples.data());
        break;
    }
    return data;
}

// Returns clamped values, assumes that ST can hold all values from DT.
template<typename ST, typename DT>
DT castAndClamp(ST v)
{
    static ST dstMin = (ST)std::numeric_limits<DT>::min();
    static ST dstMax = (ST)std::numeric_limits<DT>::max();
    return std::min(std::max(v, dstMin), dstMax);
}

// Struct, holding the necessary values to resample audio chunk by chunk. The main use-case for
// this struct solves is passing the last position (and, potentially, last value) from resample
// for the previous chunk to resample for the next chunk.
template<typename ST>
struct LinearResampleState {
    int numChannels;

    // Iterator position, stored for continuing resampling the next block. Positions and deltas
    // are encoded as int+float pair. If nextSrcIntPos == SIZE_MAX, lastSample must be used
    // as a first element before processing next src block.
    size_t nextSrcIntPos = 0;
    double nextSrcFloatPos = 0.0;
    // This value is used if the nextSrcIntPos == SIZE_MAX.
    std::vector<ST> lastSample;

    LinearResampleState(int numChannels_)
        : numChannels(numChannels_)
    {
        lastSample.resize(numChannels);
    }
};

// Does the cheapest of the cheapest resamples from src to dst, writes ~ srcSamples * stretch
// samples. The resample works very poorly for small stretch values (e.g. < 0.5) as it basically
// starts ignoring every second src sample.
template<typename ST, typename DT>
size_t resampleChunk(LinearResampleState<ST>* state, const ST* src, size_t srcSamples,
                     double stretch, DT* dst, size_t dstRemaining)
{
    int numChannels = state->numChannels;
    Span2d<const ST> srcS(src, srcSamples, numChannels);
    Span2d<DT> dstS(dst, dstRemaining, numChannels);
    if (stretch == 1.0) {
        if (srcSamples > dstRemaining) {
            printf("Overflow dst: %d vs %d\n", (int)srcSamples, (int)dstRemaining);
            exit(1);
        }
        if (state->nextSrcIntPos != 0 || state->nextSrcFloatPos != 0.0) {
            printf("Stretch 1.0 should always keep lastSrcIntPos/lastSrcFloatPos equal to zero\n");
            exit(1);
        }
        for (size_t pos = 0; pos < srcSamples; pos++) {
            for (int ch = 0; ch < numChannels; ch++) {
                dstS(pos, ch) = castAndClamp<ST, DT>(srcS(pos, ch));
            }
        }
        return srcSamples;
    } else {
        double stretchRcp = 1.0 / stretch;
        size_t srcIntDelta = (size_t)stretchRcp;
        double srcFloatDelta = stretchRcp - srcIntDelta;
        size_t dstPos = 0;
        size_t srcIntPos = state->nextSrcIntPos;
        double srcFloatPos = state->nextSrcFloatPos;
        // Process src positions between the last sample from previous block and first sample
        // from current block.
        if (srcIntPos == SIZE_MAX) {
            while (true) {
                // Sanity check that dst is allocated correctly.
                if (dstPos >= dstRemaining) {
                    printf("Overflow dst: %d vs %d\n", (int)dstPos, (int)dstRemaining);
                    exit(1);
                }
                for (int ch = 0; ch < numChannels; ch++) {
                    ST d = state->lastSample[ch] * (1 - srcFloatPos) + srcS(0, ch) * srcFloatPos;
                    dstS(dstPos, ch) = castAndClamp<ST, DT>(d);
                }
                dstPos++;
                srcIntPos += srcIntDelta;
                srcFloatPos += srcFloatDelta;
                if (srcFloatPos >= 1.0) {
                    srcIntPos++;
                    srcFloatPos -= 1.0;
                }
                // Use defined overflow for size_t. NOTE: The would certainly be cleaner
                // if ssize_t has been used for srcIntPos and lastSrcIntPos.
                if (srcIntPos != SIZE_MAX) {
                    break;
                }
            }
        }
        // Main loop for processing sample from src.
        while (true) {
            if (srcIntPos >= srcSamples - 1) {
                break;
            }
            // Sanity check that dst is allocated correctly.
            if (dstPos >= dstRemaining) {
                printf("Overflow dst: %d vs %d\n", (int)dstPos, (int)dstRemaining);
                exit(1);
            }
            for (int ch = 0; ch < numChannels; ch++) {
                ST d = srcS(srcIntPos, ch) * (1 - srcFloatPos)
                        + srcS(srcIntPos + 1, ch) * srcFloatPos;
                dstS(dstPos, ch) = castAndClamp<ST, DT>(d);
            }
            dstPos++;
            srcIntPos += srcIntDelta;
            srcFloatPos += srcFloatDelta;
            if (srcFloatPos >= 1.0) {
                srcIntPos++;
                srcFloatPos -= 1.0;
            }
        }
        if (srcIntPos == srcSamples - 1) {
            // The src position is between the last sample from the current block and
            // the first sample of the next block. Store the last sample for interpolating
            // during next call to resampleChunk.
            state->nextSrcIntPos = SIZE_MAX;
            for (int ch = 0; ch < numChannels; ch++) {
                state->lastSample[ch] = srcS(srcIntPos, ch);
            }
        } else {
            state->nextSrcIntPos = srcIntPos - srcSamples;
        }
        state->nextSrcFloatPos = srcFloatPos;
        return dstPos;
    }
}

template<typename T>
size_t simpleStretchSoundSamples(const T* src, size_t numSamples, int numChannels,
                                 const StretchParams& params, T* dst, size_t dstNumSamples)
{
    double stretch;
    if (params.pitchShift != 1.0 && params.timeStretch != 1.0) {
        printf("Simple stretch method only works if you specify either time stretching"
               " or pitch shifting\n");
        exit(1);
    } else if (params.pitchShift != 1.0) {
        stretch = 1 / params.pitchShift;
    } else {
        stretch = params.timeStretch;
    }
    LinearResampleState<T> state(numChannels);
    return resampleChunk(&state, src, numSamples, stretch, dst, dstNumSamples);
}

void fillHannWindow(kiss_fft_scalar* window, size_t fftSize)
{
    for (size_t k = 0; k < fftSize; k++) {
        window[k] = -0.5 * std::cos(2 * M_PI * k / fftSize) + 0.5;
    }
}

// Helper struct for transforming blocks of data with STFT.
struct StftState {
    size_t fftSize;
    size_t offset;
    int numChannels;
    std::vector<kiss_fft_scalar> window;
    kiss_fftr_cfg fftCfg;
    kiss_fftr_cfg fftiCfg;

    // Buffers modified at each STFT step.
    std::vector<kiss_fft_scalar> srcBuf;
    std::vector<kiss_fft_cpx> freqBuf;
    std::vector<kiss_fft_cpx> dstFreqBuf;
    std::vector<kiss_fft_scalar> dstBuf;

    StftState(size_t fftSize_, size_t offset_, int numChannels_)
        : fftSize(fftSize_)
        , offset(offset_)
        , numChannels(numChannels_)
    {
        window.resize(fftSize);
        fftCfg = kiss_fftr_alloc(fftSize, 0, nullptr, nullptr);
        fftiCfg = kiss_fftr_alloc(fftSize, 1, nullptr, nullptr);

        srcBuf.resize(fftSize * numChannels);
        dstBuf.resize(fftSize * numChannels);
        freqBuf.resize(fftSize / 2 + 1);
        dstFreqBuf.resize(fftSize / 2 + 1);
    }

    ~StftState()
    {
        kiss_fftr_free(fftCfg);
        kiss_fftr_free(fftiCfg);
    }

    kiss_fft_scalar* srcBufCh(int ch)
    {
        return srcBuf.data() + fftSize * ch;
    }

    kiss_fft_scalar* dstBufCh(int ch)
    {
        return dstBuf.data() + fftSize * ch;
    }
};

// Helper struct, containing data for simple phase vocoder.
struct SimpleVocoderState {
    size_t freqSize;
    // Phases from analysis from previous frame.
    std::vector<kiss_fft_scalar> prevAnaPhases;
    // Phases from synthesis from previous frame.
    std::vector<kiss_fft_scalar> prevSynPhases;

    // Scratch buffers.
    std::vector<kiss_fft_scalar> synMagnitudes;
    std::vector<kiss_fft_scalar> synPhaseDiffs;

    SimpleVocoderState(size_t fftSize, int numChannels)
    {
        freqSize = fftSize / 2 + 1;
        prevAnaPhases.resize(freqSize * numChannels);
        prevSynPhases.resize(freqSize * numChannels);
        synMagnitudes.resize(freqSize);
        synPhaseDiffs.resize(freqSize);
    }

    kiss_fft_scalar* prevAnaPhasesCh(int channel)
    {
        return prevAnaPhases.data() + freqSize * channel;
    }

    kiss_fft_scalar* prevSynPhasesCh(int channel)
    {
        return prevSynPhases.data() + freqSize * channel;
    }
};

// Returns the phaseDiff normalized back to [-M_PI, M_PI] range.
kiss_fft_scalar normalizePhase(kiss_fft_scalar phaseDiff)
{
    // Assumes that phaseDiff is not that far the range so that we do not use fmod, which
    // can be pretty slow.
#if 1
    if (phaseDiff < -M_PI) {
        do {
            phaseDiff += M_PI * 2;
        } while (phaseDiff < -M_PI);
    } else if (phaseDiff > M_PI) {
        do {
            phaseDiff -= M_PI * 2;
        } while (phaseDiff > M_PI);
    }
    return phaseDiff;
#else
    if (phaseDiff > -M_PI) {
        return std::fmod(phaseDiff + M_PI, M_PI * 2) - M_PI;
    } else {
        return M_PI + std::fmod(phaseDiff + M_PI, M_PI * 2);
    }
#endif
}

// Standard simple phase vocoder impl: take stft->freq and compute stft->dstFreq for given channel
// with given pitchShift.
void stretchFreqSimple(StftState* stft, SimpleVocoderState* vocoder, double pitchShift, int channel)
{
    size_t freqSize = stft->fftSize / 2 + 1;
    kiss_fft_cpx* freq = stft->freqBuf.data();
    kiss_fft_scalar* prevAnaPhases = vocoder->prevAnaPhasesCh(channel);
    kiss_fft_scalar* prevSynPhases = vocoder->prevSynPhasesCh(channel);
    kiss_fft_scalar* synPhaseDiffs = vocoder->synPhaseDiffs.data();
    kiss_fft_scalar* synMagnitudes = vocoder->synMagnitudes.data();

    size_t overlap = stft->fftSize / stft->offset;
    // Overlaps must be powers of two, so use it to optimize performance: replacing the k % overlap
    // with k & overlapMask. You can always replace k * origPhaseMult with
    // (k % overlap) * origPhaseMult.
    size_t overlapMask = overlap - 1;
    if ((overlapMask & overlap) != 0) {
        printf("Overlap must be pow-of-2\n");
        exit(1);
    }
    // origPhaseMult is used to calculate the expected phase difference for a basis for
    // frequency bin k: e^(2 * pi * k * t / freqSize) = e^(2 * pi * k / overlap)
    // when t = stft->offset, therefore the expected phase at t = stft->offset equals
    // to origPhaseMult * k. In order to constrain the phase difference to [0, 2 * pi), simply
    // replace the k with (k % overlap): origPhaseMult * (k % overlap). As noted above,
    // this is equivalent to origPhaseMulti * (k & overlapMask).
    double origPhaseMult = 2 * M_PI / overlap;

    std::fill(synMagnitudes, synMagnitudes + freqSize, 0.0);
    std::fill(synPhaseDiffs, synPhaseDiffs + freqSize, 0.0);

    for (size_t k = 0; k < freqSize; k++) {
        size_t newk = k * pitchShift;
        if (newk >= freqSize) {
            break;
        }

        kiss_fft_scalar magn = sqrt(freq[k].r * freq[k].r + freq[k].i * freq[k].i);
        bool largeMagn = (magn > synMagnitudes[newk]);
        synMagnitudes[newk] += magn;
        kiss_fft_scalar phase = std::atan2(freq[k].i, freq[k].r);
        // If multiple old phase bins stretch into one new phase bin (if pitchShift < 1.0),
        // we add their magnitudes but want to want to choose only one phase (summing up phases
        // from multiple bins make no sense). Therefore, we separately compute newPhaseDiffs
        // and then add them to prevNewPhases in the end when finally computing dstFreq. Ideally
        // we would choose the phase from the bin with the highest magnitude here.
        if (largeMagn) {
            // Original phase diff is the difference between the potential phase (phase of
            // the frequency bin k at the end of the previous block) and the actual phase
            // for the frequency bin k at the start of the new block. This phase diff is then
            // applied to the stretched frequencies. The final formula is simplified version of
            // the one from smbPitchShift.cpp Modulo overlapMask is important here, because
            // otherwise the origPhaseMult * k can be very largenn  and normalizePhase does
            // not expect it.
            kiss_fft_scalar anaPhaseDiff = normalizePhase(phase - prevAnaPhases[k]
                                                          - origPhaseMult * (k & overlapMask));
            // The following two lines are basically equivalent to
            //   synPhaseDiffs[newk] = pitchShift * (anaPhaseDiff + origPhaseMult * k).
            // The main difference is that the synPhaseDiffs is much closer to zero, so that
            // normalizePhase would take less time later.
            kiss_fft_scalar synPhaseDiff = anaPhaseDiff * pitchShift
                    + (k * pitchShift - newk) * origPhaseMult;
            synPhaseDiffs[newk] = origPhaseMult * (newk & overlapMask) + synPhaseDiff;
        }
        prevAnaPhases[k] = phase;
    }

    kiss_fft_cpx* dstFreq = stft->dstFreqBuf.data();
    for (size_t k = 0; k < freqSize; k++) {
        // Do the normalize here so that the newPhases does not become too large to limit the
        // the floating point errors stay bounded.
        prevSynPhases[k] = normalizePhase(prevSynPhases[k] + synPhaseDiffs[k]);
        dstFreq[k].r = synMagnitudes[k] * std::cos(prevSynPhases[k]);
        dstFreq[k].i = synMagnitudes[k] * std::sin(prevSynPhases[k]);
    }
}


// Helper struct, containing data for phase vocoder with phase gradient method.
struct PhaseGradientVocoderState {
    struct HeapElem {
        // Frequency bin.
        size_t freqIdx;
        // Magnitude for the frequency.
        kiss_fft_scalar magn;
        // True if the element is from the previous STFT frame, false if from the current.
        bool prevFrame;

        bool operator<(HeapElem const& other) const {
            return magn < other.magn;
        }
    };

    size_t freqSize;
    std::vector<kiss_fft_scalar> prevAnaMagnitudes;
    std::vector<kiss_fft_scalar> prevAnaPhases;
    std::vector<kiss_fft_scalar> prevSynPhases;

    // Scratch buffers.
    std::vector<kiss_fft_scalar> anaMagnitudes;
    std::vector<kiss_fft_scalar> anaPhases;
    std::vector<kiss_fft_scalar> synMagnitudes;
    std::vector<kiss_fft_scalar> synPhases;
    std::priority_queue<HeapElem> maxHeap;
    // If true, synPhases is assigned.
    std::vector<uint8_t> phaseAssigned;

    PhaseGradientVocoderState(size_t fftSize, int numChannels)
    {
        freqSize = fftSize / 2 + 1;
        prevAnaMagnitudes.resize(freqSize * numChannels);
        prevAnaPhases.resize(freqSize * numChannels);
        prevSynPhases.resize(freqSize * numChannels);

        anaMagnitudes.resize(freqSize);
        anaPhases.resize(freqSize);
        synMagnitudes.resize(freqSize);
        synPhases.resize(freqSize);
        phaseAssigned.resize(freqSize);
    }

    kiss_fft_scalar* prevAnaMagnitudesCh(int channel)
    {
        return prevAnaMagnitudes.data() + freqSize * channel;
    }

    kiss_fft_scalar* prevAnaPhasesCh(int channel)
    {
        return prevAnaPhases.data() + freqSize * channel;
    }

    kiss_fft_scalar* prevSynPhasesCh(int channel)
    {
        return prevSynPhases.data() + freqSize * channel;
    }
};

// Phase vocoder with phase gradient impl: take stft->freq and compute stft->dstFreq for given
// channel with given pitchShift.
void stretchFreqPhaseGradient(StftState* stft, PhaseGradientVocoderState* vocoder,
                              double pitchShift, int channel)
{
    // Ignore frequencies with magnitude < (max magnitude * kMaxMagnitudeTolerance).
    static const float kMinMagnitudeTolerance = 1e-3;

    size_t freqSize = stft->fftSize / 2 + 1;
    kiss_fft_cpx* freq = stft->freqBuf.data();
    kiss_fft_scalar* prevAnaMagnitudes = vocoder->prevAnaMagnitudesCh(channel);
    kiss_fft_scalar* prevAnaPhases = vocoder->prevAnaPhasesCh(channel);
    kiss_fft_scalar* prevSynPhases = vocoder->prevSynPhasesCh(channel);
    kiss_fft_scalar* anaMagnitudes = vocoder->anaMagnitudes.data();
    kiss_fft_scalar* anaPhases = vocoder->anaPhases.data();
    kiss_fft_scalar* synMagnitudes = vocoder->synMagnitudes.data();
    kiss_fft_scalar* synPhases = vocoder->synPhases.data();
    std::priority_queue<PhaseGradientVocoderState::HeapElem>& maxHeap = vocoder->maxHeap;
    uint8_t* phaseAssigned = vocoder->phaseAssigned.data();

    size_t overlap = stft->fftSize / stft->offset;
    // Overlaps must be powers of two, so use it to optimize performance: replacing
    // the k % overlap with k & overlapMask. You can always replace k * origPhaseMult
    // with (k % overlap) * origPhaseMult.
    size_t overlapMask = overlap - 1;
    if ((overlapMask & overlap) != 0) {
        printf("Overlap must be pow-of-2\n");
        exit(1);
    }
    double origPhaseMult = 2 * M_PI / overlap;

    kiss_fft_scalar maxMagn = 0.0;
    for (size_t k = 0; k < freqSize; k++) {
        kiss_fft_scalar magn = sqrt(freq[k].r * freq[k].r + freq[k].i * freq[k].i);
        anaMagnitudes[k] = magn;
        maxMagn = std::max(std::max(maxMagn, anaMagnitudes[k]), prevAnaMagnitudes[k]);
    }
    kiss_fft_scalar minMagn = maxMagn * kMinMagnitudeTolerance;

    // std::priroity_queue has no clear()...
    while (!maxHeap.empty()) {
        maxHeap.pop();
    }

    std::fill(synMagnitudes, synMagnitudes + freqSize, 0.0);
    std::fill(synPhases, synPhases + freqSize, 0.0);
    // Number of zeroes in phaseAssigned array.
    int numUnassigned = 0;
    for (size_t k = 0; k < freqSize; k++) {
        size_t newk = k * pitchShift;
        if (newk >= freqSize) {
            break;
        }

        kiss_fft_scalar magn = anaMagnitudes[k];
        // If pitchShift < 1.0, several analysis frequency bins may correspond to one synthesis bin,
        // therefore, add, not replace.
        synMagnitudes[newk] += magn;

        // Optimization: do not compute phase for frequencies below the minMagn threshold.
        if (magn > minMagn) {
            anaPhases[k] = std::atan2(freq[k].i, freq[k].r);
            phaseAssigned[k] = false;
            numUnassigned++;
            maxHeap.push({k, prevAnaMagnitudes[k], true});
        } else {
            // The original paper assigns random values to frequencies below the min magnitude,
            // but we simply leave the phase to be 0.0 (see std::fill above for synPhases).
            anaPhases[k] = 0.0;
            phaseAssigned[k] = true;
        }
    }

    while (numUnassigned > 0) {
        if (maxHeap.empty()) {
            printf("INTERNAL ERROR: no more elements remaining in the heap, %d still unassigned\n",
                   numUnassigned);
            break;
        }
        PhaseGradientVocoderState::HeapElem topElem = maxHeap.top();
        maxHeap.pop();
        size_t k = topElem.freqIdx;
        if (topElem.prevFrame) {
            if (!phaseAssigned[k]) {
                phaseAssigned[k] = true;
                numUnassigned--;
                maxHeap.push({k, anaMagnitudes[k], false});

                size_t newk = k * pitchShift;
                if (newk < freqSize) {
                    // Original phase diff is the difference between the potential phase (phase
                    // of the frequency bin k at the end of the previous block) and the actual
                    // phase for the frequency bin k at the start of the new block. This phase
                    // diff is then applied to the stretched frequencies. The final formula
                    // is simplified a bit from the one from smbPitchShift.cpp
                    // Modulo overlapMask is important here, because otherwise the origPhaseMult * k
                    // can be very large and normalizePhase does not expect it.
                    kiss_fft_scalar anaPhaseDiff = normalizePhase(
                                anaPhases[k] - prevAnaPhases[k]
                                - origPhaseMult * (k & overlapMask));
                    // The following two lines are basically equivalent to
                    //   synPhaseDiffs[newk] = pitchShift * (anaPhaseDiff + origPhaseMult * k).
                    // The main difference is that the synPhaseDiffs is much closer to zero, so that
                    // normalizePhase would take less time later.
                    kiss_fft_scalar synPhaseDiff = anaPhaseDiff * pitchShift
                            + (k * pitchShift - newk) * origPhaseMult;
                    synPhases[newk] = prevSynPhases[newk] + synPhaseDiff
                            + origPhaseMult * (newk & overlapMask);
                }
            }
        } else {
            if (k > 0 && !phaseAssigned[k - 1]) {
                phaseAssigned[k - 1] = true;
                numUnassigned--;
                maxHeap.push({k - 1, anaMagnitudes[k - 1], false});

                size_t newk1 = (k - 1) * pitchShift;
                size_t newk = k * pitchShift;
                if (newk < freqSize && newk1 != newk) {
                    synPhases[newk1] = synPhases[newk] - anaPhases[k] + anaPhases[k - 1];
                }
            }
            if (k < freqSize - 1 && !phaseAssigned[k + 1]) {
                phaseAssigned[k + 1] = true;
                numUnassigned--;
                maxHeap.push({k + 1, anaMagnitudes[k + 1], false});

                size_t newk1 = (k + 1) * pitchShift;
                size_t newk = k * pitchShift;
                if (newk1 < freqSize && newk1 != newk) {
                    synPhases[newk1] = synPhases[newk] - anaPhases[k] + anaPhases[k + 1];
                }
            }
        }
    }

    kiss_fft_cpx* dstFreq = stft->dstFreqBuf.data();
    for (size_t k = 0; k < freqSize; k++) {
        // Do the normalize here so that the prevNewPhases does not become too large to limit the
        // floating point errors.
        prevSynPhases[k] = normalizePhase(synPhases[k]);
        dstFreq[k].r = synMagnitudes[k] * std::cos(prevSynPhases[k]);
        dstFreq[k].i = synMagnitudes[k] * std::sin(prevSynPhases[k]);
    }

    std::copy(anaMagnitudes, anaMagnitudes + freqSize, prevAnaMagnitudes);
    std::copy(anaPhases, anaPhases + freqSize, prevAnaPhases);
}

// Takes stft->srcBuf, does STFT, changes pitch by pitchShift via frequencies and does inverse
// STFT to stft->dstBuf.
void doStftPitchChange(StftState* stft, SimpleVocoderState* simpleVocoder,
                       PhaseGradientVocoderState* gradientVocoder,
                       double pitchShift, bool phaseGradient)
{
    for (int channel = 0; channel < stft->numChannels; channel++) {
        kiss_fftr(stft->fftCfg, stft->srcBuf.data() + stft->fftSize * channel,
                  stft->freqBuf.data());
        if (phaseGradient) {
            stretchFreqPhaseGradient(stft, gradientVocoder, pitchShift, channel);
        } else {
            stretchFreqSimple(stft, simpleVocoder, pitchShift, channel);
        }
        kiss_fftri(stft->fftiCfg, stft->dstFreqBuf.data(), stft->dstBuf.data()
                   + stft->fftSize * channel);
    }
}

// The simples STFT pitch shifter: first change frequency (shift frequencies for each individual
// STFT block separately) and then basically do the linear interpolation like the simple stretch.
// The algorithm is described in http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
template<typename T>
size_t stftStretchSoundSamples(const T* src, size_t numSamples, int numChannels,
                               const StretchParams& params, T* dst, size_t dstNumSamples)
{
    // See comments when defining the window below for explanation.
    if (params.overlap < 4) {
        printf("STFT requires overlap >= 4\n");
        exit(1);
    }

    // Offset of next block from to the previous one must always be fftSize / (n * 2) so that
    // sum of offset hann windows is always constant.
    size_t fftSize = params.fftSize;
    size_t offset = fftSize / params.overlap;
    // If we are time stretching, we have to compensate with pitch shift.
    double finalPitchShift = params.pitchShift * params.timeStretch;

    Span2d<const T> srcV(src, numSamples, numChannels);
    Span2d<T> dstV(dst, numSamples, numChannels);
    StftState stft(fftSize, offset, numChannels);
    SimpleVocoderState simpleVocoder(fftSize, numChannels);
    PhaseGradientVocoderState phaseGradientVocoder(fftSize, numChannels);
    // STFT outputs are accumulated in circular buffer in dstAccumBuf. The part of dstAccumBuf,
    // for which all overlapped blocks have been summed, will be stretched and written to dst.
    std::vector<kiss_fft_scalar> dstAccumBuf;
    dstAccumBuf.resize(fftSize * numChannels);
    Span2d<kiss_fft_scalar> dstAccumBufV(dstAccumBuf.data(), fftSize, numChannels);

    size_t dstWritten = 0;

    // One window will be used both for analysis and synthesis (i.e. forward and inverse
    // transforms). This idea is taken from smbPitchShift.cpp and is based on the fact that
    // for overlaps >= 4 the sum of square hann window is constant.
    // The explanation why having windows both for forward and inverse transform is more optimal
    // (instead of just forward transform) is given here: https://gauss256.github.io/blog/cola.html:
    // > As mentioned above, it is most common to choose a = 1. The reason is that in the case
    // > where we did modify the STFT, there may not be a time-domain signal whose STFT matches
    // > our modified version. Choosing a = 1 gives the Griffin-Lim
    // > [http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.306.7858&rep=rep1&type=pdf]
    // > optimal estimate (optimal in a least-squares sense) for a time-domain signal from
    // > a modified STFT. SciPy implements a = 1 for the signal reconstruction in the istft routine.
    kiss_fft_scalar* window = stft.window.data();
    fillHannWindow(window, fftSize);

    LinearResampleState<kiss_fft_scalar> resampleState(numChannels);
    // Start with negative block offset, because each element must be overlapped by all possible
    // offsets windows. Real block offset is block - fftSize, but we do not want ot mess with
    // unsigned integers here.
    for (size_t block = offset; block < fftSize; block += offset) {
        size_t prefix = fftSize - block;
        // read != fftSize-prefix holds only when the numSamples < fftSize-offset.
        size_t read = std::min(fftSize - prefix, numSamples);
        for (int channel = 0; channel < numChannels; channel++) {
            // Prepare srcBuf. The first part for negative block offsets is prefilled with zeros.
            kiss_fft_scalar *srcBuf = stft.srcBufCh(channel);
            for (size_t k = 0; k < prefix; k++) {
                srcBuf[k] = 0.0;
            }
            for (size_t k = prefix; k < prefix + read; k++) {
                srcBuf[k] = (kiss_fft_scalar)srcV(k - prefix, channel) * window[k];
            }
            for (size_t k = prefix + read; k < fftSize; k++) {
                srcBuf[k] = 0.0;
            }
        }

        doStftPitchChange(&stft, &simpleVocoder, &phaseGradientVocoder, finalPitchShift,
                          params.phaseGradient);

        // Write dstBuf to dstAccumBuf. For blocks with negative offset we never wrap around
        // the dstAccumBuf, always start writing at index 0.
        for (int channel = 0; channel < numChannels; channel++) {
            kiss_fft_scalar *dstBuf = stft.dstBuf.data();
            for (size_t k = 0; k < read; k++) {
                dstAccumBufV(k, channel) += dstBuf[k + prefix] / fftSize;
            }
        }
    }

    // Process the full blocks.
    for (size_t block = 0; block < numSamples; block += offset) {
        size_t read = std::min(fftSize, numSamples - block);
        size_t accumStart = block % fftSize;
        for (int channel = 0; channel < numChannels; channel++) {
            // Prepare srcBuf.
            kiss_fft_scalar* srcBuf = stft.srcBufCh(channel);
            for (size_t k = 0; k < read; k++) {
                srcBuf[k] = (kiss_fft_scalar)srcV(block + k, channel) * window[k];
            }
            for (size_t k = read; k < fftSize; k++) {
                srcBuf[k] = 0.0;
            }
        }

        doStftPitchChange(&stft, &simpleVocoder, &phaseGradientVocoder, finalPitchShift,
                          params.phaseGradient);

        for (int channel = 0; channel < numChannels; channel++) {
            kiss_fft_scalar* dstBuf = stft.dstBufCh(channel);
            // Write dstBuf to dstAccumBuf. We want to write dstBuf[k]
            // to dstAccumBufS((block + k) % fftSize). The last part dstBuf[fftSize - offset,
            // fftSize) must overwrite, not add to the dstAccumBuf. Thus, we have to do two
            // or three separate loops, the last one overwriting, not adding the data.
#define DST_BUF(k) (dstBuf[k] * window[k] * 4) / (fftSize * params.overlap)
            if (accumStart != 0) {
                // We wrap around the dstAccumBuf, do two loops when writing.
                for (size_t k = accumStart; k < fftSize; k++) {
                    dstAccumBufV(k, channel) += DST_BUF(k - accumStart);
                }
                for (size_t k = 0; k < accumStart - offset; k++) {
                    dstAccumBufV(k, channel) += DST_BUF(k + fftSize - accumStart);
                }
                for (size_t k = accumStart - offset; k < accumStart; k++) {
                    dstAccumBufV(k, channel) = DST_BUF(k + fftSize - accumStart);
                }
            } else {
                for (size_t k = 0; k < fftSize - offset; k++) {
                    dstAccumBufV(k, channel) += DST_BUF(k);
                }
                for (size_t k = fftSize - offset; k < fftSize; k++) {
                    dstAccumBufV(k, channel) = DST_BUF(k);
                }
            }
#undef DST_BUF
        }

        // We can take the dstAccumBuf[accumStart:accumStart + offset], stretch and write it
        // to the dst.
        size_t numOutput = std::min(offset, numSamples - block);
        dstWritten += resampleChunk(&resampleState, dstAccumBufV.row(accumStart), numOutput,
                                    params.timeStretch, dstV.row(dstWritten),
                                    dstNumSamples - dstWritten);
    }

    return dstWritten;
}

template<typename T>
size_t doStretchSound(const T* src, size_t numSamples, int numChannels, StretchMethod method,
                      const StretchParams& params, T* dst, size_t dstNumSamples)
{
    switch (method) {
    case StretchMethod::Simple:
        return simpleStretchSoundSamples(src, numSamples, numChannels, params, dst, dstNumSamples);
    case StretchMethod::Stft:
        return stftStretchSoundSamples(src, numSamples, numChannels, params, dst, dstNumSamples);
    default:
        ENSURE(false, "Not supported format");
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
    std::vector<kiss_fft_scalar> dataCopy;
    dataCopy.resize(n);
    for (size_t k = 0; k < std::min(n, numSamples); k++) {
        dataCopy[k] = (kiss_fft_scalar)data[k * numChannels];
    }
    std::vector<kiss_fft_cpx> freq;
    freq.resize(n / 2 + 1);
    kiss_fftr(cfg, dataCopy.data(), freq.data());

    std::vector<Magnitude> magnitudes;
    magnitudes.resize(n / 2 + 1);
    for (size_t k = 0; k < n / 2 + 1; k++) {
        magnitudes[k].magnitude = sqrt(freq[k].r * freq[k].r + freq[k].i * freq[k].i);
        magnitudes[k].freq = k;
        magnitudes[k].r = freq[k].r;
        magnitudes[k].i = freq[k].i;
#if 0
        printf("[%d (%d-%dHz)]: %g (%g %g)\n", (int)k, (int)(k * rate / n),
               (int)((k + 1) * rate / n), magnitudes[k].magnitude, freq[k].r, freq[k].i);
#endif
    }
    std::sort(magnitudes.begin(), magnitudes.end(), [] (const Magnitude& m1, const Magnitude& m2) {
        return m1.magnitude > m2.magnitude;
    });
    for (size_t k = 0; k < 15; k++) {
        printf("top %d: %d (%d-%dHz) %g (%g %g: %g)\n", (int)k, (int)magnitudes[k].freq,
               (int)(magnitudes[k].freq * rate / n), (int)((magnitudes[k].freq + 1) * rate / n),
               magnitudes[k].magnitude, magnitudes[k].r, magnitudes[k].i,
               std::atan2(magnitudes[k].i, magnitudes[k].r));
    }

    kiss_fftr_free(cfg);
}

SoundData stretchSound(const SoundData& src, StretchMethod method, const StretchParams& params)
{
    SoundData dst;
    dst.format = src.format;
    dst.rate = src.rate;
    dst.numChannels = src.numChannels;
    // NOTE: Calculating the actual number of samples, which will be written while stretching,
    // can be quite error-prone due to floating point math errors. The easy way: overallocate
    // the dst buffer and get the number of samples actually written from the stretching function.
    dst.numSamples = src.numSamples * params.timeStretch * 1.1 + 1;
    dst.samples.resize(dst.getByteLength());

    size_t dstWritten;
    switch (dst.format) {
    case SampleFormat::Sint16:
        dstWritten = doStretchSound((int16_t*)src.samples.data(), src.numSamples, src.numChannels,
                                    method, params, (int16_t*)dst.samples.data(), dst.numSamples);
        break;
    case SampleFormat::Float:
        dstWritten = doStretchSound((float*)src.samples.data(), src.numSamples, src.numChannels,
                                    method, params, (float*)dst.samples.data(), dst.numSamples);
        break;
    default:
        ENSURE(false, "Not supported format");
    }

    // Sanity check.
    if (dstWritten > dst.numSamples) {
        printf("ERROR: Number of samples written is greater than the allocated: %d vs %d\n",
               (int)dstWritten, (int)dst.numSamples);
        exit(1);
    }
    dst.numSamples = dstWritten;

#if 0
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
    StretchParams params;
    printf("Usage: %s [options]\n"
           "Options:\n"
           "\t--input-file FILE.WAV\t\tLoad source from WAV file\n"
           "\t--input-sine HZ\t\t\tGenerate source sine wave with given frequency, 400 by default\n"
           "\t--input-sine-length SEC\t\tLength of sine wave in seconds, 5 by default\n"
           "\t--input-sine-rate RATE\t\tSet rate when generating source sine wave, 48000"
           " by default\n"
           "\t--input-sine-fmt s16|f32\tSet format when generating source sine wave, float"
           " by default\n"
           "\t--output-file FILE.wav\t\tPath to resampled file, out.wav by default\n"
           "\t--time-stretch VALUE\t\tStretch time by this value, 1.0 by default (no stretching)\n"
           "\t--pitch-shift VALUE\t\tShift pitch by this value, 1.0 by default (no change)\n"
           "\t--method METHOD\t\t\tWhich method to use for stretching: simple (do not"
           " preserve time), stft (default)\n"
           "\t--fft-size SIZE\t\t\tSize of the FFT to be used (not applicable if simple method"
           " is used, %d by default.\n"
           "\t--overlap N\t\t\tNumber of FFT frames overlapping each sample (not applicable if"
           " simple method is used, %d by default.\n"
           "\t--phase-gradient\t\tUse phase gradient method as described in Phase Vocoder Done"
           " Right by Zdenek Prusa and Nicki Holighaus\n",
           argv0,
           (int)params.fftSize,
           params.overlap);
}

int main(int argc, char** argv)
{
    const char* inputPath = nullptr;
    const char* outputPath = "out.wav";
    int inputSineWaveHz = 400;
    int inputSineWaveRate = 48000;
    double inputSineLength = 5.0;
    SampleFormat inputSineWaveFmt = SampleFormat::Float;
    StretchMethod method = StretchMethod::Stft;
    StretchParams params;

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
        } else if (strcmp(argv[i], "--time-stretch") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --time-stretch\n");
                return 1;
            }
            params.timeStretch = atof(argv[i + 1]);
            if (params.timeStretch <= 0.0) {
                printf("Time cannot stretched by %g\n", params.timeStretch);
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--pitch-shift") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --pitch-shift\n");
                return 1;
            }
            params.pitchShift = atof(argv[i + 1]);
            if (params.pitchShift <= 0.0) {
                printf("Pitch cannot be changed by %g\n", params.pitchShift);
                return 1;
            }
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
            if (params.fftSize <= 0) {
                printf("Incorrect FFT size: %d\n", (int)params.fftSize);
                return 1;
            }
            params.fftSize = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--overlap") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --overlap\n");
                return 1;
            }
            if ((params.overlap & (params.overlap - 1)) != 0) {
                printf("Incorrect overlap size (must be pow-of-2): %d\n", params.overlap);
                return 1;
            }
            params.overlap = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "--phase-gradient") == 0) {
            params.phaseGradient = true;
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
        printf("Input: %s", inputPath);
    } else {
        printf("Input: %dhz wave with %d rate", inputSineWaveHz, inputSineWaveRate);
    }
    printf(", output: %s, time stretch: %g, pitch change: %g\n", outputPath, params.timeStretch,
           params.pitchShift);
    switch (method) {
    case StretchMethod::Simple:
        printf("Method: simple\n");
        break;
    case StretchMethod::Stft:
        printf("Method: stft, fft size: %d, overlap: %d\n", (int)params.fftSize, params.overlap);
        break;
    }

    SoundData srcData;
    if (inputPath) {
        srcData = Wave::loadWav(inputPath);
    } else {
        srcData = prepareSine(inputSineWaveHz, inputSineLength, inputSineWaveRate,
                              inputSineWaveFmt);
    }

    params.rate = srcData.rate;
    SoundData dstData = stretchSound(srcData, method, params);

    Wave::writeWav(outputPath, dstData);

    return 0;
}
