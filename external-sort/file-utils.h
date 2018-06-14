#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using std::unique_ptr;
using std::vector;

namespace {

size_t const kDefaultBufferSize = 1024 * 1024;

char const kLineSeparator = '\n';

// A wrapper around FILE*, allowing to read lines in std::string. Ignores last empty line to match the behavior of FileLineWriter.
class FileLineReader
{
public:
	FileLineReader(char const* filename, size_t bufferCapacity = kDefaultBufferSize)
	{
		f = fopen(filename, "rb");
		if (f == nullptr) {
			printf("Could not open file %s\n", filename);
			exit(1);
		}
		// Disable buffering because we will be buffering ourselves.
		setvbuf(f, nullptr, _IONBF, 0);
		buf.reset(new char[bufferCapacity]);
		bufCapacity = bufferCapacity;
		bufSize = 0;
		bufRead = 0;
	}

	// We need this because of the noexcept requirement in vector.
	FileLineReader(FileLineReader&& other) noexcept
		: f(other.f)
		, buf(std::move(other.buf))
		, bufCapacity(other.bufCapacity)
		, bufSize(other.bufSize)
		, bufRead(other.bufRead)
	{
		other.f = nullptr;
	}

	~FileLineReader()
	{
		if (f != nullptr) {
			fclose(f);
		}
	}

	// Reads next line and returns true and writes to str if the EOF has not been reached, returns false otherwise.
	bool readLine(std::string* str)
	{
		str->clear();
		char* bufp = buf.get();
		// Reads until either the line separator or end of file is reached.
		while (true) {
			char* eol = (char*)memchr(bufp + bufRead, kLineSeparator, bufSize - bufRead);
			if (eol != nullptr) {
				str->append(bufp + bufRead, eol);
				bufRead = eol - bufp + 1;
				return true;
			} else {
				str->append(bufp + bufRead, bufp + bufSize);
				bufSize = fread(bufp, 1, bufCapacity, f);
				bufRead = 0;
				if (bufSize == 0) {
					// If we reached the end of file and did not read anything, this means either the file is empty
					// or the last line is empty. We treat these two cases the same.
					return !str->empty();
				}
			}
		}
	}

private:
	FILE* f;
	unique_ptr<char[]> buf;
	size_t bufCapacity;
	size_t bufSize;
	size_t bufRead;
};


// A simple wrapper around FILE*, allowing to write lines to file.
class FileLineWriter
{
public:
	FileLineWriter(char const* filename, size_t bufferSize = kDefaultBufferSize)
	{
		f = fopen(filename, "wb");
		if (f == nullptr) {
			printf("Could not open file %s\n", filename);
			exit(1);
		}

		// Increase the buffer size from default few kbs.
		setvbuf(f, nullptr, _IOFBF, bufferSize);
	}

	~FileLineWriter()
	{
		if (f != nullptr) {
			fclose(f);
		}
	}

	void writeLine(std::string const& str)
	{
		fwrite(str.data(), 1, str.size(), f);
		fputc('\n', f);
	}

private:
	FILE* f;
};

// An index into buffer, containing the line.
struct LineOffset
{
	size_t offset;
	size_t length;

	LineOffset(size_t offset, size_t length)
		: offset(offset)
		, length(length)
	{
	}
};

// A wrapper around FILE*, reading big chunks of memory, splitting them into strings and providing access directly to the chunk.
//  Ignores last empty line to match the behavior of ChunkFileWriter.
class ChunkFileReader
{
public:
	ChunkFileReader(char const* filename, size_t bufferSize = kDefaultBufferSize)
	{
		fd = open(filename, O_RDONLY);
		if (fd == -1) {
			printf("Could not open file %s\n", filename);
			exit(1);
		}
		buf.reset(new char[bufferSize]);
		bufCapacity = bufferSize;
		bufFilled = 0;
		bufRemaining = 0;
	}

	// We need this because of the noexcept requirement in vector.
	ChunkFileReader(ChunkFileReader&& other) noexcept
		: fd(other.fd)
		, buf(std::move(other.buf))
		, bufCapacity(other.bufCapacity)
		, bufFilled(other.bufFilled)
		, bufRemaining(other.bufRemaining)
	{
		other.fd = -1;
	}

	~ChunkFileReader()
	{
		if (fd != -1) {
			close(fd);
		}
	}

	// Returns the pointer to the buffer (guaranteed to be the same during ChunkFileReader life).
	char const* buffer() const
	{
		return buf.get();
	}

	// Reads new chunk and splits it into lines.
	void readAndSplit(vector<LineOffset>* lines)
	{
		char* bufp = buf.get();
		// Move the remaining part from the end of the buffer.
		if (bufRemaining < bufFilled) {
			bufFilled = bufFilled - bufRemaining;
			memmove(bufp, bufp + bufRemaining, bufFilled);
		} else {
			bufFilled = 0;
		}
		ssize_t readChars = read(fd, bufp + bufFilled, bufCapacity - bufFilled);
		if (readChars < 0) {
			printf("Could not read file\n");
			exit(1);
		}
		bufFilled += readChars;
		// We process the remainder differently, depending on whether this is the end of the file or not.
		bool eof = bufFilled != bufCapacity;

		lines->clear();
		size_t lastOffset = 0;
		while (true) {
			char* lastp = bufp + lastOffset;
			char* sep = (char*)memchr(lastp, kLineSeparator, bufFilled - lastOffset);
			if (sep == nullptr) {
				break;
			}
			lines->push_back(LineOffset(lastOffset, sep - lastp));
			lastOffset = sep - bufp + 1;
		}
		bufRemaining = lastOffset;
		// If this is the end of file and last line is not empty, add it.
		if (eof && lastOffset < bufFilled) {
			lines->push_back(LineOffset(lastOffset, bufFilled - lastOffset));
		}
	}

private:
	int fd;
	unique_ptr<char[]> buf;
	size_t bufCapacity;
	size_t bufFilled;
	// Remaining part of the line, not included in last readAndSplit output (basically location of the last separator + 1).
	size_t bufRemaining;
};


// A wrapper around FILE*, providing the direct access to the buffer (to reduce the amount of copies).
class ChunkFileWriter
{
public:
	ChunkFileWriter(char const* filename, size_t bufferSize = kDefaultBufferSize)
	{
		fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if (fd == -1) {
			printf("Could not open file %s\n", filename);
			exit(1);
		}
		buf.reset(new char[bufferSize]);
		bufCapacity = bufferSize;
		bufWritten = 0;
	}

	~ChunkFileWriter()
	{
		if (bufWritten > 0) {
			if (write(fd, buf.get(), bufWritten) < (ssize_t)bufWritten) {
				printf("Could not write to file\n");
				exit(1);
			}
		}
		close(fd);
	}

	// Returns the pointer to the buffer to write into. Length should not include the line separator
	// Line separator will be appended automatically.
	char* getLinePtr(size_t length)
	{
		if (length > bufCapacity - 1) {
			printf("Requested length larger than buffer capacity: %d vs %d", (int)length, (int)bufCapacity);
			exit(1);
		}
		if (bufWritten + length > bufCapacity - 1) {
			if (write(fd, buf.get(), bufWritten) < (ssize_t)bufWritten) {
				printf("Could not write to file\n");
				exit(1);
			}
			bufWritten = 0;
		}
		char* ret = buf.get() + bufWritten;
		bufWritten += length;
		buf[bufWritten] = '\n';
		bufWritten++;
		return ret;
	}

private:
	int fd;
	unique_ptr<char[]> buf;
	size_t bufCapacity;
	size_t bufWritten;
};

} // namespace
