#pragma once

#include "string-view.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/falloc.h>
#endif

namespace {

size_t const kDefaultBufferSize = 1024 * 1024;

char const kLineSeparator = '\n';

void preallocateForFd(int fd, off_t preallocateSize)
{
#if defined(__linux__)
    fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, preallocateSize);
#elif defined(__APPLE__)
    fstore_t store = { F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, preallocateSize, 0 };
    if (fcntl(fd, F_PREALLOCATE, &store) < 0) {
        store.fst_flags = F_ALLOCATEALL;
        fcntl(fd, F_PREALLOCATE, &store);
    }
#endif
}

// A wrapper around FILE*, allowing to read lines in std::string. Ignores last empty line to match the behavior of FileLineWriter.
class FileLineReader {
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
    std::unique_ptr<char[]> buf;
    size_t bufCapacity;
    size_t bufSize;
    size_t bufRead;
};


// A simple wrapper around FILE*, allowing to write lines to file.
class FileLineWriter {
public:
    FileLineWriter(char const* filename, off_t preallocateSize = 0, size_t bufferSize = kDefaultBufferSize)
    {
        f = fopen(filename, "wb");
        if (f == nullptr) {
            printf("Could not open file %s\n", filename);
            exit(1);
        }

        // Increase the buffer size from default few kbs.
        setvbuf(f, nullptr, _IOFBF, bufferSize);

        if (preallocateSize > 0) {
            preallocateForFd(fileno(f), preallocateSize);
        }
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

// A wrapper around FILE*, reading big chunks of memory, splitting them into strings and providing access directly to the chunk.
// Ignores last empty line to match the behavior of ChunkFileWriter.
class ChunkFileReader {
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

    // Reads new chunk and splits it into lines. Returns true if at least one line has been read.
    size_t readAndSplit(std::vector<StringView>* lines)
    {
        bool eof = fillBuf();
        splitLines(eof, lines);
        return lines->size() > 0;
    }

private:
    int fd;
    std::unique_ptr<char[]> buf;
    size_t bufCapacity;
    size_t bufFilled;
    // Remaining part of the line, not included in last readAndSplit output (basically location of the last separator + 1).
    size_t bufRemaining;

    bool fillBuf()
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
        return bufFilled != bufCapacity;
    }

    void splitLines(bool eof, std::vector<StringView>* lines)
    {
        lines->clear();
        char* bufp = buf.get();
        size_t lastOffset = 0;
        while (true) {
            char* lastp = bufp + lastOffset;
            char* sep = (char*)memchr(lastp, kLineSeparator, bufFilled - lastOffset);
            if (sep == nullptr) {
                break;
            }
            lines->push_back(StringView(lastp, sep - lastp));
            lastOffset = sep - bufp + 1;
        }
        bufRemaining = lastOffset;

        if (bufRemaining == 0 && !eof) {
            printf("Line larger than %d, not supported\n", (int)bufCapacity);
            exit(1);
        }

        // If this is the end of file and last line is not empty, add it.
        if (eof && lastOffset < bufFilled) {
            lines->push_back(StringView(bufp + lastOffset, bufFilled - lastOffset));
        }
    }
};


// A wrapper around FILE*, providing the direct access to the buffer. Supports preallocating files.
class ChunkFileWriter {
public:
    ChunkFileWriter(char const* filename, off_t preallocateSize = 0, size_t bufferSize = kDefaultBufferSize)
    {
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            printf("Could not open file %s\n", filename);
            exit(1);
        }
        if (preallocateSize > 0) {
            preallocateForFd(fd, preallocateSize);
        }
        buf = new char[bufferSize];
        bufCapacity = bufferSize;
        bufWritten = 0;
    }

    ~ChunkFileWriter()
    {
        if (bufWritten > 0) {
            if (write(fd, buf, bufWritten) < (ssize_t)bufWritten) {
                printf("Could not write to file\n");
                exit(1);
            }
        }
        close(fd);
        delete[] buf;
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
            if (write(fd, buf, bufWritten) < (ssize_t)bufWritten) {
                printf("Could not write to file\n");
                exit(1);
            }
            bufWritten = 0;
        }
        char* ret = buf + bufWritten;
        bufWritten += length;
        buf[bufWritten] = '\n';
        bufWritten++;
        return ret;
    }

    // Writes line.
    void writeLine(StringView line)
    {
        char* ptr = getLinePtr(line.length);
        memcpy(ptr, line.begin, line.length);
    }

private:
    int fd;
    char* buf;
    size_t bufCapacity;
    size_t bufWritten;
};

void deleteFiles(std::vector<std::string> const& files)
{
    for (auto const& file : files) {
        if (unlink(file.c_str()) < 0) {
            printf("Failed deleting file %s\n", file.c_str());
        }
    }
}

} // namespace
