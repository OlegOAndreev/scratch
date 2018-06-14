#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>
#include <vector>

#include "common.h"
#include "file-utils.h"

using std::min;
using std::priority_queue;
using std::string;
using std::vector;

size_t const kDefaultMaxMemory = 1024 * 1024 * 1024LL;

// Comparer for two ChunkOffsets.
bool compareLines(char const* buffer, LineOffset line1, LineOffset line2)
{
	int ret = memcmp(buffer + line1.offset, buffer + line2.offset, min(line1.length, line2.length));
	if (ret < 0) {
		return true;
	} else if (ret > 0) {
		return false;
	} else {
		return line1.length < line2.length;
	}
}

// Comparer for LineOffset and string.
bool compareLines(char const* buffer, string const& line1, LineOffset line2)
{
	int ret = memcmp(line1.c_str(), buffer + line2.offset, min(line1.length(), line2.length));
	if (ret < 0) {
		return true;
	} else if (ret > 0) {
		return false;
	} else {
		return line1.length() < line2.length;
	}
}

string stringFromChunkLine(char const* buffer, LineOffset line)
{
	return string(buffer + line.offset, buffer + line.offset + line.length);
}

void stringFromChunkLine(char const* buffer, LineOffset line, string* str)
{
	str->clear();
	str->append(buffer + line.offset, buffer + line.offset + line.length);
}

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
struct LineWithId {
	size_t chunkId;
	string line;

	LineWithId(size_t chunkId, string const& line)
		: chunkId(chunkId)
		, line(line)
	{
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

		auto lineWithIdComp = [] (LineWithId const& line1, LineWithId const& line2) {
			return line1.line > line2.line;
		};
		priority_queue<LineWithId, vector<LineWithId>, decltype(lineWithIdComp)> chunkLines(lineWithIdComp);
		string line;
		for (size_t chunkId = 0; chunkId < chunkReaders.size(); chunkId++) {
			if (chunkReaders[chunkId].readLine(&line)) {
				chunkLines.push(LineWithId(chunkId, line));
			} else {
				printf("No read from %s\n", chunkFiles[chunkId].c_str());
			}
		}

		FileLineWriter dstWriter(dstFile);
		while (!chunkLines.empty()) {
			LineWithId const& top = chunkLines.top();
			dstWriter.writeLine(top.line);
			size_t nextChunkId = top.chunkId;
			chunkLines.pop();
			if (chunkReaders[nextChunkId].readLine(&line)) {
				chunkLines.push(LineWithId(nextChunkId, line));
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

void printErrorInverseStrings(string const& line1, string const& line2, int lineCount)
{
	printf("ERROR: Lines %d and %d are inverse:\n  %s\nvs\n  %s\n", lineCount, lineCount + 1, line1.c_str(), line2.c_str());
	exit(1);
}

void validateSort(char const* srcFile)
{
	uint64_t startTime = getTimeCounter();
	{
		FileLineReader reader(srcFile);
		string lines[2];
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

void validateSortFaster(char const* srcFile)
{
	uint64_t startTime = getTimeCounter();
	{
		ChunkFileReader reader(srcFile);
		char const* buf = reader.buffer();
		vector<LineOffset> lines;
		int lineCount = 1;
		// Store the last chunk line to compare it to the first line of the new chunk. Valid only if lineCount > 1.
		string lastChunkLine;
		while (true) {
			reader.readAndSplit(&lines);
			if (lines.empty()) {
				break;
			}
			// Check if first line is less than the previous chunk last line.
			if (lineCount > 1) {
				if (!compareLines(buf, lastChunkLine, lines.front())) {
					printErrorInverseStrings(lastChunkLine, stringFromChunkLine(buf, lines.front()), lineCount - 1);
				}
			}
			for (size_t i = 1; i < lines.size(); i++, lineCount++) {
				if (!compareLines(buf, lines[i - 1], lines[i])) {
					printErrorInverseStrings(stringFromChunkLine(buf, lines[i - 1]), stringFromChunkLine(buf, lines[i]), lineCount);
				}
			}
			lineCount++;
			// Store last chunk line for comparing with first line of next chunk.
			stringFromChunkLine(buf, lines.back(), &lastChunkLine);
		}
	}
	printf("Validated successfully in %dms\n", elapsedMsec(startTime));
}

void generateFile(char const* dstFile, int numLines, int avgLineLen)
{
	uint64_t startTime = getTimeCounter();
	{
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

void generateFileFaster(char const* dstFile, int numLines, int avgLineLen)
{
	uint64_t startTime = getTimeCounter();
	{
		int minLineLen = avgLineLen / 2;
		int maxLineLen = avgLineLen * 3 / 2;
		// Make random generate the same for the same parameters.
		uint32_t xorstate[4] = { uint32_t(numLines + avgLineLen), 0, 0, 0 };

		// Preallocate the average file size.
		ChunkFileWriter writer(dstFile, numLines * avgLineLen, kDefaultBufferSize);
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
		   "  sort srcFile dstFile [maxMemory]\t\tSorts srcFile into dst using at most maxMemory chunks\n"
		   "  validate file\t\t\t\t\tValidates that the file is sorted\n"
		   "  validate-faster file\t\t\t\tValidates that the file is sorted\n"
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
	} else {
		printUsage(argv[0]);
		return 1;
	}
	return 0;
}
