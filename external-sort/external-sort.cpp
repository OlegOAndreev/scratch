#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>
#include <vector>

#define COUNT_STRING_COMPARES

#include "common.h"
#include "file-utils.h"
#include "sort.h"


#if defined(COUNT_STRING_COMPARES)
size_t compareStrCount = 0;
#endif

size_t const kDefaultMaxMemory = 1024 * 1024 * 1024LL;

// Global state is bad, but passing a ton of not-really related parameters through functions is not good either.
size_t maxMemory = kDefaultMaxMemory;
bool leaveChunks = false;
bool preallocate = true;


// List of chunk files with total size of the original file.
struct ChunkFiles {
    std::vector<std::string> filenames;
    off_t totalSize;

    // Support gcc 4.6 :-(
    ChunkFiles()
        : totalSize(0)
    {
    }
};

// A simple pair: line from chunk and num of the chunk it has been read from.
struct LineWithNum {
    size_t chunkNum;
    std::string line;

    LineWithNum(size_t _chunkNum, std::string const& _line)
        : chunkNum(_chunkNum)
        , line(_line)
    {
    }

    bool operator<(LineWithNum const& other) const
    {
        // Inverted due to priority_queue being a max heap.
        return line > other.line;
    }
};

// A simple pair: line from chunk and num of the chunk it has been read from.
struct StringViewWithNum {
    size_t chunkNum;
    StringView line;

    StringViewWithNum(size_t _chunkNum, StringView _line)
        : chunkNum(_chunkNum)
        , line(_line)
    {
    }

    bool operator<(StringViewWithNum other) const
    {
        return line > other.line;
    }
};

// A collection of ChunkFileReaders, allowing to read lines from multiple chunks.
class MultiChunkReader {
public:
    // Adds new chunk reader.
    void addReader(char const* filename, size_t bufferSize)
    {
        readers.emplace_back(filename, bufferSize);
        lines.emplace_back();
        nextLine.push_back(0);
    }

    // Reads next line from the chunk and returns true if line has been read.
    bool readLineFrom(size_t chunkNum, StringView* line)
    {
        std::vector<StringView>& chunkLines = lines[chunkNum];
        size_t chunkLine = nextLine[chunkNum];
        if (chunkLine < chunkLines.size()) {
            *line = chunkLines[chunkLine];
            nextLine[chunkNum]++;
            return true;
        } else {
            ChunkFileReader& reader = readers[chunkNum];
            if (reader.readAndSplit(&chunkLines)) {
                *line = chunkLines[0];
                nextLine[chunkNum] = 1;
                return true;
            } else {
                return false;
            }
        }
    }

private:
    std::vector<ChunkFileReader> readers;
    std::vector<std::vector<StringView>> lines;
    std::vector<size_t> nextLine;
};

std::string stringFromView(StringView line)
{
    return std::string(line.begin, line.begin + line.length);
}

void stringFromView(StringView line, std::string* str)
{
    str->clear();
    str->append(line.begin, line.begin + line.length);
}

std::string getNextChunkFile(char const* dstFile, size_t numChunks)
{
    char buf[1000];
    snprintf(buf, sizeof(buf), "%s.chunk.%d", dstFile, (int)numChunks);
    return buf;
}

// Sorts chunk, writes into new chunk file (with name based on dstFile) and appends the new name to chunkFiles.
// Updates totalSortTimeMs and totalWriteTimeMs.
void sortAndWriteChunk(char const* dstFile, std::vector<std::string>* chunk, std::vector<std::string>* filenames,
                       int* totalSortTimeMs, int* totalWriteTimeMs)
{
    uint64_t sortStartTime = getTimeTicks();
    std::sort(chunk->begin(), chunk->end());
    *totalSortTimeMs += elapsedMsec(sortStartTime);

    // Write the curChunk into next chunk file. Be sure to call FileLineWriter destructor before computing
    // the elapsed time.
    uint64_t writeStartTime = getTimeTicks();
    {
        std::string nextChunkFile = getNextChunkFile(dstFile, filenames->size());
        filenames->push_back(nextChunkFile);
        off_t preallocateSize = 0;
        if (preallocate) {
            // +1 for line separator.
            for (std::string const& line : *chunk) {
                preallocateSize += line.length() + 1;
            }
        }
        printf("Writing chunk %s with preallocated len %lld\n", nextChunkFile.c_str(), (long long)preallocateSize);
        FileLineWriter chunkWriter(nextChunkFile.c_str(), preallocateSize);
        for (std::string const& line : *chunk) {
            chunkWriter.writeLine(line);
        }
    }
    *totalWriteTimeMs += elapsedMsec(writeStartTime);
}

// Chunks the srcFile into chunks of no more than maxMemory (expects each line to be less than maxMemory),
// sorts each chunk and writes it into a temporary file. Returns the list of chunk filenames and total length of the file.
ChunkFiles chunkAndSort(char const* srcFile, char const* dstFile)
{
    uint64_t startTime = getTimeTicks();
    int totalSortTimeMs = 0;
    int totalWriteTimeMs = 0;
    ChunkFiles ret;

    FileLineReader srcReader(srcFile);
    std::string line;
    std::vector<std::string> curChunk;
    size_t curChunkMemory = 0;
    while (srcReader.readLine(&line)) {
        if (curChunkMemory + line.size() > maxMemory) {
            sortAndWriteChunk(dstFile, &curChunk, &ret.filenames, &totalSortTimeMs, &totalWriteTimeMs);
            curChunk.clear();
            curChunkMemory = 0;
        }
        curChunk.push_back(line);
        curChunkMemory += line.size() + 1;
        ret.totalSize += line.size() + 1;
    }
    if (!curChunk.empty()) {
        sortAndWriteChunk(dstFile, &curChunk, &ret.filenames, &totalSortTimeMs, &totalWriteTimeMs);
    }

    printf("Chunked and sorted %d chunks in %dms (%dms for sorting and %dms for writing)\n",
           (int)ret.filenames.size(), elapsedMsec(startTime), totalSortTimeMs, totalWriteTimeMs);
    return ret;
}

// Merges sorted chunk files into dstFile.
void mergeChunks(ChunkFiles const& chunkFiles, char const* dstFile)
{
    uint64_t startTime = getTimeTicks();
    {
        std::vector<FileLineReader> chunkReaders;
        for (std::string const& chunkFile : chunkFiles.filenames) {
            chunkReaders.emplace_back(chunkFile.c_str());
        }

        std::priority_queue<LineWithNum> topLines;
        std::string line;
        for (size_t chunkNum = 0; chunkNum < chunkReaders.size(); chunkNum++) {
            if (chunkReaders[chunkNum].readLine(&line)) {
                topLines.push(LineWithNum(chunkNum, line));
            } else {
                printf("No lines in %s\n", chunkFiles.filenames[chunkNum].c_str());
            }
        }

        off_t preallocateSize = preallocate ? chunkFiles.totalSize : 0;
        printf("Writing dst %s with preallocated len %lld\n", dstFile, (long long)preallocateSize);
        FileLineWriter dstWriter(dstFile, preallocateSize);
        while (!topLines.empty()) {
            LineWithNum const& top = topLines.top();
            dstWriter.writeLine(top.line);
            size_t nextChunkNum = top.chunkNum;
            topLines.pop();
            if (chunkReaders[nextChunkNum].readLine(&line)) {
                topLines.push(LineWithNum(nextChunkNum, line));
            }
        }
    }
    printf("Merged %d chunks in %dms\n", (int)chunkFiles.filenames.size(), elapsedMsec(startTime));
}

// Sorts lines from srcFile into dstFile, using no more that maxMemory in the process. If leaveChunks is true,
// does not remove temporary chunk files.
void externalSort(char const* srcFile, char const* dstFile)
{
    uint64_t startTime = getTimeTicks();
    ChunkFiles chunkFiles = chunkAndSort(srcFile, dstFile);
    mergeChunks(chunkFiles, dstFile);
    if (!leaveChunks) {
        uint64_t deleteStartTime = getTimeTicks();
        deleteFiles(chunkFiles.filenames);
        printf("Deleted %d chunks in %dms\n", (int)chunkFiles.filenames.size(), elapsedMsec(deleteStartTime));
    }
    printf("Total sorting time is %dms\n", elapsedMsec(startTime));
}

// A version of chunkAndSort, which uses ChunkFileReader/ChunkFileWriter.
ChunkFiles chunkAndSortFaster(char const* srcFile, char const* dstFile)
{
    uint64_t startTime = getTimeTicks();
    int totalSortTimeMs = 0;
    int totalWriteTimeMs = 0;
    ChunkFiles ret;

    ChunkFileReader srcReader(srcFile, maxMemory);
    std::vector<StringView> chunkLines;
    while (srcReader.readAndSplit(&chunkLines)) {
        off_t chunkLength = 0;
        // +1 for line separator.
        for (StringView line : chunkLines) {
            chunkLength += line.length + 1;
        }
        ret.totalSize += chunkLength;

        uint64_t sortStartTime = getTimeTicks();
        std::sort(chunkLines.begin(), chunkLines.end());
        totalSortTimeMs += elapsedMsec(sortStartTime);

        uint64_t writeStartTime = getTimeTicks();
        {
            std::string nextChunkFile = getNextChunkFile(dstFile, ret.filenames.size());
            ret.filenames.push_back(nextChunkFile);
            off_t preallocateSize = preallocate ? chunkLength : 0;
            printf("Writing chunk %s with preallocated len %lld\n", nextChunkFile.c_str(), (long long)preallocateSize);
            ChunkFileWriter chunkWriter(nextChunkFile.c_str(), preallocateSize);
            for (StringView line : chunkLines) {
                chunkWriter.writeLine(line);
            }
        }
        totalWriteTimeMs += elapsedMsec(writeStartTime);
    }

    printf("Chunked and sorted %d chunks in %dms (%dms for sorting and %dms for writing)\n",
           (int)ret.filenames.size(), elapsedMsec(startTime), totalSortTimeMs, totalWriteTimeMs);
    return ret;
}

// A version of mergeChunks, which uses ChunkFileReader/ChunkFileWriter.
void mergeChunksFaster(ChunkFiles const& chunkFiles, char const* dstFile)
{
    uint64_t startTime = getTimeTicks();
    {
        MultiChunkReader multiChunkReader;
        size_t numChunks = chunkFiles.filenames.size();
        size_t perChunkBufferSize = maxMemory / numChunks;
        for (std::string const& chunkFile : chunkFiles.filenames) {
            multiChunkReader.addReader(chunkFile.c_str(), perChunkBufferSize);
        }

        std::priority_queue<StringViewWithNum> topLines;
        for (size_t chunkNum = 0; chunkNum < numChunks; chunkNum++) {
            StringView line;
            if (multiChunkReader.readLineFrom(chunkNum, &line)) {
                topLines.push(StringViewWithNum(chunkNum, line));
            }
        }

        off_t preallocateSize = preallocate ? chunkFiles.totalSize : 0;
        printf("Writing dst %s with preallocated len %lld\n", dstFile, (long long)preallocateSize);
        ChunkFileWriter dstWriter(dstFile, preallocateSize);
        while (!topLines.empty()) {
            StringViewWithNum line = topLines.top();
            dstWriter.writeLine(line.line);
            topLines.pop();
            if (multiChunkReader.readLineFrom(line.chunkNum, &line.line)) {
                topLines.push(line);
            }
        }
    }
    printf("Merged %d chunks in %dms\n", (int)chunkFiles.filenames.size(), elapsedMsec(startTime));
}

// A version of externalSort which uses ChunkFileReader/ChunkFileWriter.
void externalSortFaster(char const* srcFile, char const* dstFile)
{
    uint64_t startTime = getTimeTicks();
    ChunkFiles chunkFiles = chunkAndSortFaster(srcFile, dstFile);
    mergeChunksFaster(chunkFiles, dstFile);
    if (!leaveChunks) {
        uint64_t deleteStartTime = getTimeTicks();
        deleteFiles(chunkFiles.filenames);
        printf("Deleted %d chunks in %dms\n", (int)chunkFiles.filenames.size(), elapsedMsec(deleteStartTime));
    }
    printf("Total sorting time is %dms\n", elapsedMsec(startTime));
}

void printErrorInverseStrings(std::string const& line1, std::string const& line2, int lineCount)
{
    printf("ERROR: Lines %d and %d are inverse:\n  %s\nvs\n  %s\n", lineCount, lineCount + 1,
           line1.c_str(), line2.c_str());
    exit(1);
}

// Validates that srcFile is sorted.
void validateSort(char const* srcFile)
{
    uint64_t startTime = getTimeTicks();
    {
        FileLineReader reader(srcFile);
        std::string lines[2];
        size_t prevLine = 0;
        size_t curLine = 1;
        reader.readLine(&lines[prevLine]);
        int lineCount = 1;
        while (reader.readLine(&lines[curLine])) {
            prevLine = 1 - curLine;
            if (lines[prevLine] > lines[curLine]) {
                printErrorInverseStrings(lines[prevLine], lines[curLine], lineCount);
            }
            curLine = prevLine;
            lineCount++;
        }
    }
    printf("Validated successfully in %dms\n", elapsedMsec(startTime));
}

// A version of validateSort, which uses ChunkFileReader (and generally does almost zero allocations).
void validateSortFaster(char const* srcFile)
{
    uint64_t startTime = getTimeTicks();
    {
        ChunkFileReader reader(srcFile);
        std::vector<StringView> lines;
        int lineCount = 1;
        // Store the last chunk line to compare it to the first line of the new chunk. Valid only if lineCount > 1.
        std::string lastChunkLine;
        while (reader.readAndSplit(&lines)) {
            // Check if first line is less than the previous chunk last line.
            if (lineCount > 1) {
                if (lastChunkLine > lines.front()) {
                    printErrorInverseStrings(lastChunkLine, stringFromView(lines.front()), lineCount - 1);
                }
            }
            for (size_t i = 1; i < lines.size(); i++, lineCount++) {
                if (lines[i - 1] > lines[i]) {
                    printErrorInverseStrings(stringFromView(lines[i - 1]), stringFromView(lines[i]), lineCount);
                }
            }
            lineCount++;
            // Store last chunk line for comparing with first line of next chunk.
            stringFromView(lines.back(), &lastChunkLine);
        }
    }
    printf("Validated successfully in %dms\n", elapsedMsec(startTime));
}

// Generates random dstFile with numLines and average line length = avgLineLen. The pair (numLines, avgLineLen) is used
// as a randomization seed.
void generateFile(char const* dstFile, int numLines, int avgLineLen)
{
    uint64_t startTime = getTimeTicks();
    {
        int minLineLen = avgLineLen / 2;
        int maxLineLen = avgLineLen * 3 / 2;
        std::string line;
        line.reserve(maxLineLen);
        // Make random generate the same for the same parameters.
        uint32_t xorstate[4] = { uint32_t(numLines + avgLineLen), 0, 0, 0 };

        // Preallocate the average file size.
        off_t preallocateSize = preallocate ? numLines * avgLineLen : 0;
        FileLineWriter writer(dstFile, preallocateSize);
        for (int i = 0; i < numLines; i++) {
            int lineLen = randomRange(xorstate, minLineLen, maxLineLen);
            line.clear();
            for (int j = 0; j < lineLen; j++) {
                line.push_back(randomRange(xorstate, '0', 'z' + 1));
            }
            writer.writeLine(line);
        }
    }
    printf("Generated %d lines x %d avg len in %dms\n", numLines, avgLineLen, elapsedMsec(startTime));
}

// A copy of std::generate, which guarantees to be unrolled by 8 elements.
template<typename T, typename Gen>
void generateUnrolled(T* p, size_t n, Gen gen)
{
    for (size_t i = 0; i < n - 7; i += 8) {
        T e0 = gen();
        T e1 = gen();
        T e2 = gen();
        T e3 = gen();
        T e4 = gen();
        T e5 = gen();
        T e6 = gen();
        T e7 = gen();
        p[i] = e0;
        p[i + 1] = e1;
        p[i + 2] = e2;
        p[i + 3] = e3;
        p[i + 4] = e4;
        p[i + 5] = e5;
        p[i + 6] = e6;
        p[i + 7] = e7;
    }
    for (size_t i = n & ~7; i < n; i++) {
        p[i] = gen();
    }
}

// A version of generateFile, which uses ChunkFileWriter for writing and generateUnrolled for filling the buffer.
void generateFileFaster(char const* dstFile, int numLines, int avgLineLen)
{
    uint64_t startTime = getTimeTicks();
    {
        int minLineLen = avgLineLen / 2;
        int maxLineLen = avgLineLen * 3 / 2;
        // Make random generate the same for the same parameters.
        uint32_t xorstate[4] = { uint32_t(numLines + avgLineLen), 0, 0, 0 };

        // Preallocate the average file size.
        off_t preallocateSize = preallocate ? numLines * avgLineLen : 0;
        ChunkFileWriter writer(dstFile, preallocateSize);
        for (int i = 0; i < numLines; i++) {
            int lineLen = randomRange(xorstate, minLineLen, maxLineLen);
            char* line = writer.getLinePtr(lineLen);
            generateUnrolled(line, lineLen, [&xorstate] {
                return randomRange(xorstate, '0', 'z' + 1);
            });
        }
    }
    printf("Generated %d lines x %d avg len in %dms\n", numLines, avgLineLen, elapsedMsec(startTime));
}

// Benchmarks sorting the srcFile in chunks.
void benchmarkSort(char const* srcFile, char const* sortMethod)
{
    ChunkFileReader fileReader(srcFile, 1024 * 1024 * 1024);
    std::vector<StringView> lines;
    while (fileReader.readAndSplit(&lines)) {
        uint64_t startTime = getTimeTicks();
        callSortMethod(sortMethod, lines.begin(), lines.end());
        printf("Sorted %d lines by %s sort in %dms\n", (int)lines.size(), sortMethod, elapsedMsec(startTime));
    }
}

void printUsage(char const* argv0)
{
    printf("Usage: %s operation [operation params] [options]\n\n"
           "Operations:\n"
           "  sort SRCFILE DSTFILE\t\t\t\tSorts SRCFILE into DSTFILE\n"
           "  sort-faster SRCFILE DSTFILE\t\t\tSorts SRCFILE into DSTFILE (with optimizations)\n"
           "  validate FILE\t\t\t\t\tValidates that the FILE is sorted\n"
           "  validate-faster FILE\t\t\t\tValidates that the FILE is sorted (with optimizations)\n"
           "  generate FILE NUMLINES AVGLINE\t\tGenerates an ASCII FILE with given number of lines and average line length\n"
           "  generate-faster FILE NUMLINES AVGLINE\t\tGenerates an ASCII file with given number of lines and average line length"
           " (with optimizations)\n"
           "  benchmark-sort FILE METHOD\t\t\tBenchmarks the sorting routine by sorting the file in memory\n"
           "Options:\n"
           "  --max-memory SIZE\t\tThe max amount of memory to be used for in-memory buffers\n"
           "  --leave-chunks\t\tDo not remove the chunks left after sorting\n"
           "  --no-preallocate\t\tDo not preallocate file space (can be both a win and a loss depending on FS, OS etc.)\n",
           argv0);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // Parse options. Also change argc to not include the options.
    int optionArgc = argc;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--leave-chunks") == 0) {
            leaveChunks = true;
            optionArgc = std::min(optionArgc, i);
        } else if (strcmp(argv[i], "--no-preallocate") == 0) {
            preallocate = false;
            optionArgc = std::min(optionArgc, i);
        } else if (strcmp(argv[i], "--max-memory") == 0) {
            if (i + 1 >= argc) {
                printUsage(argv[0]);
                return 1;
            }
            maxMemory = (size_t)atoll(argv[i + 1]);
            optionArgc = std::min(optionArgc, i);
            i++;
        }
    }
    argc = optionArgc;

    // Parse operations and their arguments.
    if (strcmp(argv[1], "sort") == 0) {
        if (argc != 4) {
            printUsage(argv[0]);
            return 1;
        }
        externalSort(argv[2], argv[3]);
    } else if (strcmp(argv[1], "sort-faster") == 0) {
        if (argc != 4) {
            printUsage(argv[0]);
            return 1;
        }
        externalSortFaster(argv[2], argv[3]);
    } else if (strcmp(argv[1], "validate") == 0) {
        if (argc != 3) {
            printUsage(argv[0]);
            return 1;
        }
        validateSort(argv[2]);
    } else if (strcmp(argv[1], "validate-faster") == 0) {
        if (argc != 3) {
            printUsage(argv[0]);
            return 1;
        }
        validateSortFaster(argv[2]);
    } else if (strcmp(argv[1], "generate") == 0) {
        if (argc != 5) {
            printUsage(argv[0]);
            return 1;
        }
        generateFile(argv[2], atoi(argv[3]), atoi(argv[4]));
    } else if (strcmp(argv[1], "generate-faster") == 0) {
        if (argc != 5) {
            printUsage(argv[0]);
            return 1;
        }
        generateFileFaster(argv[2], atoi(argv[3]), atoi(argv[4]));
    } else if (strcmp(argv[1], "benchmark-sort") == 0) {
        if (argc != 4) {
            printUsage(argv[0]);
            return 1;
        }
        benchmarkSort(argv[2], argv[3]);
    } else {
        printUsage(argv[0]);
        return 1;
    }

#if defined(COUNT_STRING_COMPARES)
    printf("Total comparisons: %lld\n", (long long)compareStrCount);
#endif

    return 0;
}
