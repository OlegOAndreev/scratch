#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>
#include <vector>

#include "common.h"
#include "file-utils.h"

using std::priority_queue;
using std::string;
using std::vector;

size_t const kDefaultMaxMemory = 1024 * 1024 * 1024LL;

string getNextChunkFile(char const* dstFile, size_t numChunks)
{
	char buf[1000];
	snprintf(buf, sizeof(buf), "%s.chunk.%d", dstFile, (int)numChunks);
	return buf;
}

void processChunk(char const* dstFile, vector<string>* chunk, vector<string>* chunkFiles,
				  int* totalSortTimeMs, int* totalWriteTimeMs)
{
	uint64_t sortStartTime = getTimeCounter();
	std::sort(chunk->begin(), chunk->end());
	*totalSortTimeMs += elapsedMsec(sortStartTime);

	// Write the curChunk into next chunk file. Be sure to call FileLineWriter destructor before computing
	// the elapsed time.
	uint64_t writeStartTime = getTimeCounter();
	{
		string nextChunkFile = getNextChunkFile(dstFile, chunkFiles->size());
		chunkFiles->push_back(nextChunkFile);
		printf("Writing chunk %s\n", nextChunkFile.c_str());
		FileLineWriter chunkWriter(nextChunkFile.c_str());
		for (string const& line : *chunk) {
			chunkWriter.writeLine(line);
		}
	}
	*totalWriteTimeMs += elapsedMsec(writeStartTime);
}

// Chunks the srcFile into chunks of no more than maxMemory (expects each line to be less than maxMemory),
// sorts each chunk and writes it into a temporary file. Returns the list of chunk filenames.
vector<string> chunkAndSort(char const* srcFile, char const* dstFile, size_t maxMemory)
{
	uint64_t startTime = getTimeCounter();
	int totalSortTimeMs = 0;
	int totalWriteTimeMs = 0;
	vector<string> chunkFiles;

	FileLineReader srcReader(srcFile);
	string line;
	vector<string> curChunk;
	size_t curChunkMemory = 0;
	while (srcReader.readLine(&line)) {
		if (curChunkMemory + line.size() > maxMemory) {
			processChunk(dstFile, &curChunk, &chunkFiles, &totalSortTimeMs, &totalWriteTimeMs);
			curChunk.clear();
			curChunkMemory = 0;
		}
		curChunk.push_back(line);
		curChunkMemory += line.size();
	}
	if (!curChunk.empty()) {
		processChunk(dstFile, &curChunk, &chunkFiles, &totalSortTimeMs, &totalWriteTimeMs);
	}

	printf("Chunked and sorted %d chunks in %dms (%dms for sorting and %dms for writing)\n",
		   (int)chunkFiles.size(), elapsedMsec(startTime), totalSortTimeMs, totalWriteTimeMs);
	return chunkFiles;
}

// A simple pair: line from chunk and id of the chunk it has been read from.
struct ChunkLine {
	size_t chunkId;
	string line;

	ChunkLine(size_t chunkId, string const& line)
		: chunkId(chunkId)
		, line(line)
	{
	}

	bool operator<(ChunkLine const& other) const
	{
		return line < other.line;
	}
};

// Merges sorted chunk files into dstFile.
void mergeChunks(vector<string> const& chunkFiles, char const* dstFile)
{
	uint64_t startTime = getTimeCounter();
	{
		vector<FileLineReader> chunkReaders;
		for (string const& chunkFile : chunkFiles) {
			chunkReaders.emplace_back(chunkFile.c_str());
		}

		priority_queue<ChunkLine> chunkLines;
		string line;
		for (size_t chunkId = 0; chunkId < chunkReaders.size(); chunkId++) {
			if (chunkReaders[chunkId].readLine(&line)) {
				chunkLines.push(ChunkLine(chunkId, line));
			} else {
				printf("No read from %s\n", chunkFiles[chunkId].c_str());
			}
		}

		FileLineWriter dstWriter(dstFile);
		while (!chunkLines.empty()) {
			ChunkLine const& top = chunkLines.top();
			dstWriter.writeLine(top.line);
			size_t nextChunkId = top.chunkId;
			chunkLines.pop();
			if (chunkReaders[nextChunkId].readLine(&line)) {
				chunkLines.push(ChunkLine(nextChunkId, line));
			}
		}
	}
	printf("Merged %d chunks in %dms\n", (int)chunkFiles.size(), elapsedMsec(startTime));
}

void externalSort(char const* srcFile, char const* dstFile, size_t maxMemory)
{
	uint64_t startTime = getTimeCounter();
	vector<string> chunkFiles = chunkAndSort(srcFile, dstFile, maxMemory);
	mergeChunks(chunkFiles, dstFile);
	printf("Total sorting time is %dms\n", elapsedMsec(startTime));
}

void validateSort(char const* srcFile)
{
	uint64_t startTime = getTimeCounter();
	FileLineReader reader(srcFile);
	string lines[2];
	size_t prevLine = 0;
	size_t curLine = 1;
	reader.readLine(&lines[prevLine]);
	int lineCount = 1;
	while (reader.readLine(&lines[curLine])) {
		prevLine = 1 - curLine;
		if (lines[prevLine] > lines[curLine]) {
			printf("ERROR: Lines %d and %d are inverse:\n  %s\nvs\n  %s\n",
				   lineCount, lineCount + 1, lines[prevLine].c_str(), lines[curLine].c_str());
			exit(1);
		}
		curLine = prevLine;
        lineCount++;
    }
	printf("Validated successfully in %dms\n", elapsedMsec(startTime));
}

void generateFile(char const* dstFile, int numLines, int avgLineLen)
{
    uint64_t startTime = getTimeCounter();

    int minLineLen = avgLineLen / 2;
    int maxLineLen = avgLineLen * 3 / 2;
    string line;
    line.reserve(maxLineLen);
    // Make random generate the same for the same parameters.
    uint32_t xorstate[4] = { uint32_t(numLines + avgLineLen), 0, 0, 0 };

	FileLineWriter writer(dstFile);
    for (int i = 0; i < numLines; i++) {
        int lineLen = randomRange(xorstate, minLineLen, maxLineLen);
        line.clear();
		for (int j = 0; j < lineLen; j++) {
			line.push_back(randomRange(xorstate, '0', 'z' + 1));
		}
		writer.writeLine(line);
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
		p[i + 5] = e4;
		p[i + 6] = e5;
		p[i + 7] = e6;
		p[i + 8] = e7;
	}
	for (size_t i = n & ~7; i < n; i++) {
		p[i] = gen();
	}
}

void generateFileFaster(char const* dstFile, int numLines, int avgLineLen)
{
	uint64_t startTime = getTimeCounter();
	{
		int minLineLen = avgLineLen / 2;
		int maxLineLen = avgLineLen * 3 / 2;
		// Make random generate the same for the same parameters.
		uint32_t xorstate[4] = { uint32_t(numLines + avgLineLen), 0, 0, 0 };

		ChunkFileWriter writer(dstFile);
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

void printUsage(const char* argv0)
{
    printf("Usage: %s operation [options]\n"
           "Operations:\n"
		   "  sort srcFile dstFile [maxMemory]\t\t\t\tSorts srcFile into dst using at most maxMemory chunks\n"
           "  validate file\t\t\t\t\tValidates that the file is sorted\n"
		   "  generate dstFile numLines avgLine\t\tGenerates an ASCII file with given number of lines and average line length\n"
		   "  generate-faster dstFile numLines avgLine\tGenerates an ASCII file with given number of lines and average line length\n",
           argv0);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "sort") == 0) {
		if (argc != 4 && argc != 5) {
            printUsage(argv[0]);
            return 1;
        }
		size_t maxMemory = (argc > 4) ? (size_t)atoll(argv[4]) : kDefaultMaxMemory;
		externalSort(argv[2], argv[3], maxMemory);
    } else if (strcmp(argv[1], "validate") == 0) {
        if (argc != 3) {
            printUsage(argv[0]);
            return 1;
        }
        validateSort(argv[2]);
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
	} else {
        printUsage(argv[0]);
        return 1;
    }
    return 0;
}
