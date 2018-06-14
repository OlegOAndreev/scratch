#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using std::unique_ptr;

namespace {

size_t const kDefaultBufferSize = 1024 * 1024;

char const kLineSeparator = '\n';

// A wrapper around FILE*, allowing to read lines in std::string. Ignores last empty line to match the behavior of FileWriter.
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

	// we need this because of the noexcept requirement in vector.
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


// A wrapper around FILE*, providing access
class ChunkFileReader
{

};

// A wrapper around FILE*, providing the direct access to the buffer (to reduce the amount of copies).
class ChunkFileWriter
{
public:
	ChunkFileWriter(char const* filename, size_t bufferSize = kDefaultBufferSize)
	{
		f = fopen(filename, "wb");
		if (f == nullptr) {
			printf("Could not open file %s\n", filename);
			exit(1);
		}
		// We do our own buffering.
		setvbuf(f, nullptr, _IONBF, 0);
		buf.reset(new char[bufferSize]);
		bufCapacity = bufferSize;
		bufWritten = 0;
	}

	~ChunkFileWriter()
	{
		if (bufWritten > 0) {
			fwrite(buf.get(), 1, bufWritten, f);
		}
		fclose(f);
	}

	// Returns the pointer to the buffer to write into. Length should not include the line separator
	// Line separator will be appended automatically.
	char* getLinePtr(size_t length)
	{
		if (bufWritten + length > bufCapacity - 1) {
			fwrite(buf.get(), 1, bufWritten, f);
			bufWritten = 0;
		}
		char* ret = buf.get() + bufWritten;
		bufWritten += length;
		buf[bufWritten] = '\n';
		bufWritten++;
		return ret;
	}

private:
	FILE* f;
	unique_ptr<char[]> buf;
	size_t bufCapacity;
	size_t bufWritten;
};

} // namespace
