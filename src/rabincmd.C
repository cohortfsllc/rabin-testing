/* 
 * Copyright (C) 2010 Bobby Martin (bobbymartin2@gmail.com)
 * Copyright (C) 2004 Hyang-Ah Kim (hakim@cs.cmu.edu)
 * Copyright (C) 2000 David Mazieres (dm@uun.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "rabinpoly.h"

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <iterator>

using namespace std;

#define INT64(n) n##LL
#define MSB64 INT64(0x8000000000000000)
#define FINGERPRINT_PT 0xbfe6b8a5bf378d83LL

typedef unsigned short BOOL;

const int FALSE = 0;
const int TRUE  = 1;

//utility functions probably belong somewhere else
//TODO handle varargs properly
#define DEBUG 0
//#define DEBUG 1

void debug(const char* msg, ...)
{
    if( DEBUG )
        {
            va_list argp;

            va_start(argp, msg);
            vfprintf(stderr, msg, argp);
            va_end(argp);
        }
}


void errorOut(const char* msg, ...)
{
    va_list argp;

    va_start(argp, msg);
    vfprintf(stderr, msg, argp);
    va_end(argp);

    exit(-1);
}


BOOL fileExists(const char* fname)
{
    return access( fname, F_OK ) != -1;
}

//end utility functions


void printChunkData(
                    const char* msgPrefix,
                    int size,
                    u_int64_t fingerprint,
                    u_int64_t hash)
{
    fprintf(stderr, "%s chunk hash: %016llx fingerprint: %016llx length: %d\n",
            msgPrefix,
            (unsigned long long) hash,
            (unsigned long long) fingerprint,
            size);
}

void printChunkContents(u_int64_t hash,
                        const unsigned char* buffer,
                        int size)
{
    fprintf(stderr,
            "Chunk with hash %016llx contains:\n",
            (unsigned long long) hash);
    for(int i = 0; i < size; ++i)
        {
            int tmp = buffer[i];
            fprintf(stderr, "%x", tmp);
        }
    fprintf(stderr, "\n");
}

u_int64_t makeBitMask(int maskSize)
{
    u_int64_t retval = 0;

    u_int64_t currBit = 1;
    for(int i = 0; i < maskSize; ++i)
        {
            retval |= currBit;
            currBit <<= 1;
        }

    return retval;
}

class ChunkBoundaryChecker
{
public:
    virtual BOOL isBoundary(u_int64_t fingerprint, int size) = 0;
};


class MaxChunkBoundaryChecker : public ChunkBoundaryChecker
{
public:
    virtual int getMaxChunkSize() = 0;
};

/*
  template <class T>
  inline T max(T a, T b)
  {
  if( a > b ) return a;
  return b;
  }
*/

class BitwiseChunkBoundaryChecker : public MaxChunkBoundaryChecker
{
private:
    const int BITS;
    u_int64_t CHUNK_BOUNDARY_MASK;
    const int MAX_SIZE;
    const int MIN_SIZE;

public:
    BitwiseChunkBoundaryChecker(int numBits)
        : BITS(numBits),
          CHUNK_BOUNDARY_MASK(makeBitMask(BITS)), 
          MAX_SIZE(4 * (1 << BITS)),
          MIN_SIZE(max(DEFAULT_WINDOW_SIZE, 1 << (BITS - 2)))
    {
        //printf("bitmask: %16llx\n", chunkBoundaryBitMask);
        //exit(0);
    }

    virtual BOOL isBoundary(u_int64_t fingerprint, int size)
    {
        return (((fingerprint & CHUNK_BOUNDARY_MASK) == 0 && size >= MIN_SIZE) ||
                (MAX_SIZE != -1 && size >= MAX_SIZE) );
    }

    int getMaxChunkSize() { return MAX_SIZE; }

};


class SpecifiedChunkBoundaryChecker : public MaxChunkBoundaryChecker
{
private:
    const int BITS;
    u_int64_t CHUNK_BOUNDARY_MASK;
    const int MAX_SIZE;
    const int MIN_SIZE;

    // must declare after CHUNK_BOUNDARY_MASK for initalization list to work
    // properly
    const u_int64_t boundaryMarker;

public:
    SpecifiedChunkBoundaryChecker(int numBits, uint minChunkSize,
                                  uint maxChunkSize,
                                  const u_int64_t boundaryM = 0)
        : BITS(numBits),
          CHUNK_BOUNDARY_MASK(makeBitMask(BITS)), 
          MAX_SIZE(maxChunkSize),
          MIN_SIZE(minChunkSize),
          boundaryMarker(boundaryM & CHUNK_BOUNDARY_MASK)
    {
        //printf("bitmask: %16llx\n", chunkBoundaryBitMask);
        //exit(0);
    }

    virtual BOOL isBoundary(u_int64_t fingerprint, int size)
    {
        return (((fingerprint & CHUNK_BOUNDARY_MASK) == boundaryMarker && size >= MIN_SIZE) ||
                (MAX_SIZE != -1 && size >= MAX_SIZE) );
    }

    int getMaxChunkSize() { return MAX_SIZE; }

};


class ChunkProcessor
{
private:
    int size;

    //protected:
public:
    virtual void internalProcessByte(unsigned char c)
    {
        ++size;
        //printf("processed byte %d\n", getSize());
    }

    virtual void internalCompleteChunk(u_int64_t hash, u_int64_t fingerprint)
    {
        size = 0;
    }

public:
    ChunkProcessor() : size(0) {}

    virtual ~ChunkProcessor() {}

    void processByte(unsigned char c) 
    {
        internalProcessByte(c);
    }

    void completeChunk(u_int64_t hash, u_int64_t fingerprint)
    {
        internalCompleteChunk(hash, fingerprint);
    }

    virtual int getSize()
    {
        return size;
    }
}; // class ChunkProcessor


string toString(u_int64_t num)
{
    ostringstream oss;
    oss << setfill('0') << setw(16) << hex << num;
    return oss.str();
}

string toDecString(long long num)
{
    ostringstream oss;
    oss << dec << num;
    return oss.str();
}


class PrintChunkProcessor : public ChunkProcessor
{
private:
protected:
    virtual void internalCompleteChunk(u_int64_t hash, u_int64_t fingerprint)
    {
        printChunkData("Found", getSize(), fingerprint, hash);
        ChunkProcessor::internalCompleteChunk(hash, fingerprint);
    }

public:
    PrintChunkProcessor()
    {
    }
};


class CreateFileChunkProcessor : public ChunkProcessor
{
private:
    string      chunkDir;
    FILE*       tmpChunkFile;

protected:
    virtual void internalProcessByte(unsigned char c)
    {
        ChunkProcessor::internalProcessByte(c);
        fputc(c, getTmpChunkFile());
    }

    virtual void internalCompleteChunk(u_int64_t hash, u_int64_t fingerprint)
    {
        ChunkProcessor::internalCompleteChunk(hash, fingerprint);

        string chunkName(chunkDir);
        chunkName += "/";
        chunkName += toString(hash);
        chunkName += ".rabin";

        fclose(tmpChunkFile);
        tmpChunkFile = NULL;

        //if chunk already exists, just delete tmp chunk file
        if( fileExists(chunkName.c_str()) )
            {
                unlink(getTmpChunkFileName().c_str());
            }
        //else rename tmp chunk to correct chunk name and close file
        else
            {
                rename(getTmpChunkFileName().c_str(), chunkName.c_str());
            }
    }

public:
    CreateFileChunkProcessor(string chunkDir)
        : tmpChunkFile(NULL),
          chunkDir(chunkDir)
    {
    }

    ~CreateFileChunkProcessor()
    {
        if( tmpChunkFile != NULL )
            {
                cerr << "Final chunk never completed!" << endl;
                fclose(tmpChunkFile);
                tmpChunkFile = NULL;
            }
    }

    string getTmpChunkFileName()
    {
        string s(chunkDir);
        s += "/tempChunk.rabin.tmp";
        return s;
    }

    FILE* getTmpChunkFile()
    {
        if( tmpChunkFile == NULL )
            {
                string s = getTmpChunkFileName();
                tmpChunkFile = fopen(s.c_str(), "w");

                if( tmpChunkFile == 0 )
                    {
                        fprintf(stderr, "Could not open %s\n", s.c_str());
                        exit(-3);
                    }
            }

        return tmpChunkFile;
    }
}; // class CreateFileChunkProcessor


class StatsChunkProcessor : public ChunkProcessor
{
private:
  string statsDir;
  string statsNotation;
  string filePrefix;
  int statsDirLevels;
  string hostName;
  string inputFileName;
  unsigned long long offset;
  unsigned long long chunkStart;
  unsigned long zeroCount;
  unsigned long long zeroBlocks;
  unsigned long zeroBlockSize;
  int chunkNumber;
  dev_t inputDevNo;
  ino_t inputInode;
  off_t expectedSize;

protected:
  virtual void internalProcessByte(unsigned char c) {
    offset++;
    if (c == 0) {
      zeroCount++;
    } else {
      zeroCount = 0;
    }
  }

  virtual void internalCompleteChunk(u_int64_t hash, u_int64_t fingerprint) {
    int chunkSize = (int) (offset - chunkStart + 1);

    if (chunkSize != 0 && zeroCount >= chunkSize) {
      zeroBlocks++;
      zeroCount = 0;
      if (zeroBlockSize == 0) {
	zeroBlockSize = chunkSize;
      }
    } else {
      string hashString = toString(hash);
      string dir = getDir(hashString, chunkSize);

      string statFileName = filePrefix + "-"
	+ toDecString(chunkNumber)+ ".stats";

      // if a notation is provided, use it to prefix the .stats file name
      if (statsNotation != "") {
	statFileName = statsNotation + "-" + statFileName;
      }

      string path = dir + "/" + statFileName;

      int fd = creat(path.c_str(), 0777);
      if (fd < 0) {
	errorOut("could not open stats file \"%s\"\n", path.c_str());
      }

      FILE* f = fdopen(fd, "w");
      if (f == NULL) {
	errorOut("could not open write file \"%s\"\n", path.c_str());
      }

      fprintf(f,
	      "file name: %s\nchunk number: %d\nstart offset: %llu\n"
	      "end offset: %llu\nsize: %llu\n",
	      inputFileName.c_str(), chunkNumber, (unsigned long) chunkStart,
	      (unsigned long) offset, (unsigned long) chunkSize);
      fclose(f);
    }

    chunkStart = offset + 1;
    chunkNumber++;
  }

    void makeDirs(string hash) {
        string dirPath = statsDir + "/";
        for (int l = 0; l < statsDirLevels; l++) {
            dirPath += hash.substr(l, 1) + "/";
            int result = mkdir(dirPath.c_str(), 0777);
            if (result != 0 && errno != EEXIST) {
                errorOut("could not make directory \"%s\"\n", dirPath.c_str());
            }
        }
    }

    string getDir(string hash, int chunkSize) {
        string dirPath = statsDir + "/";
        for (int l = 0; l < statsDirLevels; l++) {
            dirPath += hash.substr(l, 1) + "/";
        }

        dirPath += hash + ".hash";

        for (int attempt = 1; attempt <= 2; attempt++) {
            int result = mkdir(dirPath.c_str(), 0777);
            if (0 != result) {
                if (errno == EEXIST) {
                    break;
                } else {
                    if (attempt == 1) {
                        makeDirs(hash);
                    } else {
                        errorOut("could not create directory \"%s\"\n",
                                 dirPath.c_str());
                    }
                }
            } else {
                string path = dirPath + "/" + toDecString(chunkSize) + ".size";
                FILE* f = fopen(path.c_str(), "w");
                fprintf(f, "%d\n", chunkSize);
                fclose(f);
                break;
            }
        }

        return dirPath;
    }

public:
    StatsChunkProcessor(string statsDir,
                        string statsNotation,
                        int statsDirLevels,
                        string inputFileName,
                        int inputFd)
        : statsDir(statsDir), statsNotation(statsNotation),
          statsDirLevels(statsDirLevels),
          inputFileName(inputFileName),
	  chunkStart(0), offset(-1), chunkNumber(0),
	  zeroCount(0), zeroBlocks(0), zeroBlockSize(0)
    {
        {
            char nameBuf[1024];
            if (0 != gethostname(nameBuf, sizeof(nameBuf))) {
                errorOut("error: could not retrieve this host's name\n");
            }
            hostName = nameBuf;
        }

        struct stat statBuf;
        if (0 != fstat(inputFd, &statBuf)) {
            errorOut("could not stat input file\n");
        }
        inputDevNo = statBuf.st_dev;
        inputInode = statBuf.st_ino;
        expectedSize = statBuf.st_size;

        if (0 != stat(statsDir.c_str(), &statBuf)) {
            errorOut("could not examine stats directory \"%s\"\n",
                    statsDir.c_str());
        }
        if (!S_ISDIR(statBuf.st_mode)) {
            errorOut("provided stats directory \"%s\" is not a directory\n",
                     statsDir.c_str());
        }

        if (0 != access(statsDir.c_str(), R_OK | W_OK | X_OK)) {
            errorOut("do not have full access to stats directory \"%s\"\n",
                    statsDir.c_str());
        }

	filePrefix =  hostName + "-" + toDecString(inputDevNo)
	  + "-" + toDecString(inputInode);
    }

    virtual ~StatsChunkProcessor()
    {
      string zeroFileName = filePrefix + ".zeroes";
      string zeroPath = statsDir + "/" + zeroFileName;
      FILE* f = fopen(zeroPath.c_str(), "w");
      fprintf(f, "zero blocks: %llu\nzero block size: %lu\n",
	      zeroBlocks, zeroBlockSize);
      fclose(f);
    }
}; // class StatsChunkProcessor


class CompressChunkProcessor : public ChunkProcessor
{
private:
    FILE*   outfile;
    int     maxChunkSize;
    unsigned char*   buffer;
    long    chunkNum;
    map<u_int64_t, long> chunkLocations;
protected:
    virtual void internalProcessByte(unsigned char c)
    {
        if( getSize() < maxChunkSize)
            {
                buffer[getSize()] = c;
            }
        else
            {
                fprintf(stderr, "Compression buffer overflow, size = %d\n", getSize());
            }

        ChunkProcessor::internalProcessByte(c);
    }

    virtual void internalCompleteChunk(u_int64_t hash, u_int64_t fingerprint)
    {
        if( DEBUG )
            {
                if( hash == INT64(0x3030303237373500) )
                    {
                        printChunkContents(hash, buffer, getSize());
                    }
            }

        //if this is the very first chunk, just write it out
        if( chunkLocations.size() == 0 )
            {
                fwrite(buffer, 1, getSize(), outfile);
                debug("first chunk %ld of length %d with hash %016llx\n", chunkNum, getSize(), hash);
            }
        else
            {
                map<u_int64_t, long>::iterator chunkIter = chunkLocations.find(hash);

                //otherwise if it hasn't been written out, mark it as an in-place chunk & record its location for future reference
                if( chunkIter == chunkLocations.end() )
                    {
                        //if the first byte is 0xfe, it would be ambiguous if that's in-place
                        //data or a chunk loc.  So we use 0xff as an 'escape' character.
                        if( buffer[0] == 0xff || buffer[0] == 0xfe )
                            {
                                unsigned char flag = 0xff;
                                fwrite(&flag, 1, 1, outfile);
                                debug("0xff\n");
                            }

                        fwrite(buffer, 1, getSize(), outfile);
                        debug("chunk %ld of length %d with hash %016llx\n", chunkNum, getSize(), hash);
                    }
                //otherwise mark this as an already found chunk & write the location
                else
                    {
                        unsigned char flag = 0xfe;
                        fwrite(&flag, 1, 1, outfile);
                        //chunkLoc is how far back from this chunk did the copy last occur
                        long chunkLoc = chunkNum - chunkIter->second;

                        //To accomodate varying sizes of indexes without always using
                        //4 bytes, we just store 7 bits of the number in each byte.  Then the
                        //high bit is set to 0 for all leading bytes, and set to 1 in the
                        //last byte as a terminator.  So chunkNums < 128 use only one byte,
                        //chunkNums < 16384 use two bytes, etc.
                        //(Num required bytes = ceil(log2(chunkNum)/7)
                        unsigned char b;
                        while(chunkLoc >= 127)
                            {
                                b = chunkLoc & 0x7f;  //grab the low 7 bits
                                chunkLoc >>= 7;       //shift them out
                                debug("%d to file\n", b);
                                fwrite(&b, sizeof(b), 1, outfile); //write them to the file
                            }

                        b = chunkLoc | 0x80;
                        fwrite(&b, sizeof(b), 1, outfile);
                        debug("%d to file\n", b);
                        debug("reference to chunk %ld of length %d with hash %016llx\n", chunkLoc, getSize(), hash);
                    }
            }

        chunkLocations[hash] = chunkNum;
        ++chunkNum;

        ChunkProcessor::internalCompleteChunk(hash, fingerprint);
    }

public:
    CompressChunkProcessor(FILE* outfile, int maxChunkSize)
        : outfile(outfile),
          maxChunkSize(maxChunkSize),
          buffer(new unsigned char[maxChunkSize]),
          chunkNum(0)
    {
    }

    ~CompressChunkProcessor()
    {
        if( outfile != stdout )
            {
                fclose(outfile);
                outfile = NULL;
            }

        delete[] buffer;
    }
};

class DataSource
{
public:
    virtual int getByte() = 0;
};

class RawFileDataSource : public DataSource
{
private:
    FILE* is;
public:
    RawFileDataSource(FILE* is) : is(is) {}
    ~RawFileDataSource()
    {
        fclose(is);
    }

    virtual int getByte()
    {
        return fgetc(is);
    }
};

class ExtractDataSource : public DataSource
{
private:
    FILE*  raf;
    FILE*  infile;
    FILE*  currReadFile;
    BOOL   inPlaceChunk;
    fpos_t storedLoc;
public:
    ExtractDataSource(const char* outfileName, FILE* infile)
        : raf(NULL),
          inPlaceChunk(TRUE),
          infile(infile),
          currReadFile(infile)
    {
        raf = fopen(outfileName, "wb+");

        if( raf == 0 )
            {
                printf("Could not open %s for read/write\n", outfileName);
                exit(-2);
            }
    }

    void restoreLocation()
    {
        fsetpos(raf, &storedLoc);
        currReadFile = infile;
    }

    void write(void* buffer, int size)
    {
        //if( storedLoc <- check for 0, to indicate that we don't have a storedLoc
        fwrite(buffer, 1, size, raf);
    }

    void readExistingChunk(fpos_t loc, fpos_t* currChunkBegin)
    {
        //the chunk we're about to read from storedLoc we will write here
        fgetpos(raf, currChunkBegin);

        fgetpos(raf, &storedLoc);
        fsetpos(raf, &loc);

        currReadFile = raf;

        inPlaceChunk = FALSE;
    }

    void startInPlaceChunk(fpos_t* currChunkBegin)
    {
        inPlaceChunk = TRUE;
        fgetpos(raf, currChunkBegin);
        currReadFile = infile;
    }

    void close()
    {
        fclose(raf);
        raf = NULL;
    }

    BOOL isInPlaceChunk() { return inPlaceChunk; }

    virtual int peekByte()
    {
        fpos_t tmp;
        fgetpos(currReadFile, &tmp);
        int retval = fgetc(currReadFile);
        fsetpos(currReadFile, &tmp);

        return retval;
    }

    virtual int getByte()
    {
        return fgetc(currReadFile);
    }
};

class ExtractChunkProcessor : public ChunkProcessor
{
private:
    int                maxChunkSize;
    unsigned char*     buffer;
    fpos_t             currChunkBegin;
    ExtractDataSource* eds;
    vector<fpos_t>     chunkLocations;
    long               currChunkNum;
protected:
    virtual void internalProcessByte(unsigned char c)
    {
        if( getSize() < maxChunkSize)
            {
                buffer[getSize()] = c;
            }
        else
            {
                fprintf(stderr, "Extraction buffer overflow, size = %d\n", getSize());
            }

        ChunkProcessor::internalProcessByte(c);
    }

    virtual void internalCompleteChunk(u_int64_t hash, u_int64_t fingerprint)
    {
        //store the index of this chunk
        chunkLocations.push_back(currChunkBegin);

        char const *s;
        if( eds->isInPlaceChunk() )
            {
                s="";
            }
        else
            {
                s="reference to ";

                eds->restoreLocation();
            }
        debug("%schunk %ld of length %d with hash %016llx\n", s, currChunkNum, getSize(), hash);

        //write the chunk we just processed
        eds->write(buffer, getSize());

        //see how to handle what comes next in the input file
        int tmpGet = eds->peekByte();

        //-1 indicates end of file
        if( tmpGet != -1 )
            {
                unsigned char nextByte = tmpGet;

                //this is a chunk we already processed; find it
                if( nextByte == 0xfe )
                    {
                        eds->getByte();

                        long chunkLoc = 0;

                        unsigned char b = 0;
                        int i = 0;
                        do
                            {
                                b = eds->getByte();
                                debug("%d to file\n", b);

                                //get lower 7 bits of b
                                long tmp = b & 0x7f;
                                //shift it to where it goes in chunkLoc (least significant byte is first)
                                tmp <<= (7 * i);
                                chunkLoc |= tmp;

                                ++i;
                            }
                        while( (b & 0x80) == 0 );

                        //the chunk loc is how many back to go from the last chunk
                        int chunkNum = chunkLocations.size() - chunkLoc;

                        //now I know the chunkLoc; find it in the output file & copy over the chunk
                        //eds will remember where the end of the file is for later restore
                        eds->readExistingChunk(chunkLocations[chunkNum], &currChunkBegin);

                        currChunkNum = chunkNum; //<-- just for debugging
                    }
                else
                    {
                        //0xff indicates another in-place chunk but is an escape character; eat it
                        //see CompressChunkProcessor.internalCompleteChunk
                        if( nextByte == 0xff )
                            {
                                debug("0xff\n");
                                eds->getByte();
                            }
                        //any other character indicates an in-place chunk as well

                        //store the position of the next chunk
                        eds->startInPlaceChunk(&currChunkBegin);

                        currChunkNum = chunkLocations.size();  //<-- just for debugging
                    }

                ChunkProcessor::internalCompleteChunk(hash, fingerprint);
            }
    }

public:
    ExtractChunkProcessor(int maxChunkSize, ExtractDataSource* dataSource)
        : maxChunkSize(maxChunkSize),
          buffer(new unsigned char[maxChunkSize]),
          eds(dataSource)
    {
        eds->startInPlaceChunk(&currChunkBegin);
    }

    ~ExtractChunkProcessor()
    {
        eds->close();

        delete[] buffer;
    }

    ExtractDataSource* getDataSource() { return eds; }
};

void processChunks(DataSource* ds,
                   ChunkBoundaryChecker& chunkBoundaryChecker,
                   ChunkProcessor& chunkProcessor)
{
    const u_int64_t POLY = FINGERPRINT_PT;
    window rw(POLY);
    rabinpoly rp(POLY);

    int next;
    u_int64_t val = 0;
    //add a leading 1 to avoid the issue with rabin codes & leading 0s
    u_int64_t hash = 1;
    rw.slide8(1);

    while((next = ds->getByte()) != -1)
        {
            chunkProcessor.processByte(next);

            hash = rp.append8(hash, (char)next);
            val = rw.slide8((char)next);

            if( chunkBoundaryChecker.isBoundary(val, chunkProcessor.getSize()) )
                {
                    chunkProcessor.completeChunk(hash, val);
                    hash = 1;
                    rw.reset();
                    rw.slide8(1);
                }
        }

    chunkProcessor.completeChunk(hash, val);
}

int requireInt(char* str)
{
    char* str_orig = str;


    //skip whitespace
    while(str[0] == ' ' || str[0] == '\t') ++str;

    //validate
    int i = 0;
    while(str[i] != 0)
        {
            int digit = str[i] - '0';
            if( digit < 0 || digit > 9 )
                {
                    fprintf(stderr, "%s is not a number\n", str_orig);
                    exit(-1);
                }

            ++i;
        }


    return atoi(str);
}



long long requireLongLong(char* str)
{
    char *endptr;
    long long result = strtoll(str, &endptr, 0);

    if (errno != 0) {
        fprintf(stderr, "%s is not a number\n", str);
        exit(-1);
    }

    if (*str == '\0' || *endptr != '\0') {
        fprintf(stderr, "%s is not a number\n", str);
        exit(-1);
    }

    return result;
}


class Options
{
public:
    string chunkDir;
    string statsDir;
    int    statsDirLevels;
    string statsNotation;
    string outFilename;
    string inFilename;
    BOOL   compress;
    BOOL   extract;
    BOOL   print;
    BOOL   reconstruct;
    int    bits;
    uint   maxChunkSize;
    uint   minChunkSize;
    BOOL   minMaxWarnings;
    u_int64_t boundaryMarker;

    Options(int argc, char** argv)
        : compress      (FALSE),
          extract       (FALSE),
          print         (FALSE),
          reconstruct   (FALSE),
          bits          (13),
          maxChunkSize  (64 * 1024),
          minChunkSize  (2 * 1024),
          minMaxWarnings(true),
          statsDirLevels(0),
          boundaryMarker(0)
    {
        setOptionsFromArguments(argc, argv);
        validateOptionCombination();
    }

    void usage()
    {
        fprintf(stderr, "Flags:\n");
        fprintf(stderr, "-b <log2 of the average chunk length desired, default is 12>\n");
        fprintf(stderr, "-M <maximum chunk size in bytes>\n");
        fprintf(stderr, "-m <minimum chunk size in bytes>\n");
        fprintf(stderr, "-B <boundary marker\n");
        fprintf(stderr, "-f <fixed chunk size in bytes>\n");
        fprintf(stderr, "-d <directory in which to put/retrieve chunks>\n");
        fprintf(stderr, "-s <directory in which to put/retrieve chunk statistics>\n");
        fprintf(stderr, "-n <notation to prefix stats chunk files with, possibly to indicate host>\n");
        fprintf(stderr, "-l <number of subdirectory levels for"
                " statistics directory>\n");
        fprintf(stderr, "-p \"print chunk data for the file\" (to standard out)\n");
        fprintf(stderr, "-c \"rabin enCode/Compress file\" (to standard out or outfile)\n");
        fprintf(stderr, "-x \"rabin eXtract/decompress file\" (to standard out or outfile)\n");
        fprintf(stderr, "-r \"reconstruct file from chunk dir and printed chunk data\" (to standard out or outfile)\n");
        fprintf(stderr, "-o <file in which to put output>\n");

        fprintf(stderr, "\nFlags c, x and r are mutually exclusive.  Flag p is incompatible\n");
        fprintf(stderr, "with r.  p is incompatible with x and c unless -o is also given.\n");
    }

    void unsupported(string s)
    {
        fprintf(stderr, "%s not yet supported\n", s.c_str());
        exit(-1);
    }

    void setOptionsFromArguments(int argc, char** argv)
    {
        int c;
        extern char *optarg;
        extern int optind, optopt;

        while ((c = getopt(argc, argv, ":cxprd:o:b:M:m:f:s:l:n:B:")) != -1) {
            switch(c) {
            case 'c':
                compress = TRUE;
                break;
            case 'x':
                extract = TRUE;
                break;
            case 'p':
                print = TRUE;
                break;
            case 'r':
                unsupported("-r TODO");
                reconstruct = TRUE;
                break;
            case 'd':
                chunkDir = optarg;
                break;
            case 'o':
                outFilename = optarg;
                break;
            case 'b':
                bits = requireInt(optarg);
                break;
            case 'M':
                maxChunkSize = requireInt(optarg);
                break;
            case 'm':
                minChunkSize = requireInt(optarg);
                break;
            case 'B':
                boundaryMarker = (u_int64_t) requireLongLong(optarg);
                break;
            case 'f':
                maxChunkSize = requireInt(optarg);
                minChunkSize = requireInt(optarg);
                bits = 32;
                minMaxWarnings = false;
                break;
            case 's':
                statsDir = optarg;
                break;
            case 'l':
                statsDirLevels = requireInt(optarg);
                break;
            case 'n':
                statsNotation = optarg;
                break;
            case ':':       /* -d or -o without operand */
                fprintf(stderr,
                        "Option -%c requires an operand\n", optopt);
                exit(-1);
                break;
            case '?':
                usage();
                exit(-1);
            }
        }

        if( optind != argc - 1 )
            {
                fprintf(stderr, "Expected exactly one input file specified.\n");
                exit(-1);
            }

        inFilename = argv[optind];
    }

    void validateOptionCombination()
    {
        //-d is compatible with any flag

        if( print )
            {
                //print is incompatible with reconstruct
                if( reconstruct )
                    {
                        fprintf(stderr, "-p (print) flag makes no sense with -r (reconstruct) flag\n");
                        exit(-1);
                    }

                //print is incompatible with extract and compress unless outFilename
                //is also given.
                if( (extract || compress) && outFilename == "" )
                    {
                        fprintf(stderr, "-p (print) flag combined with -c (compress) or -x (extract) requires\n-o (output file name) where compress/extract output goes.\n");
                        exit(-1);
                    }
            }

        if( compress )
            {
                //compress and extract are mutually exclusive
                if( extract )
                    {
                        fprintf(stderr, "-c (compress) flag cannot be combined with -x (extract)\n");
                        exit(-1);
                    }

                //compress and reconstruct are mutually exclusive
                if( reconstruct )
                    {
                        fprintf(stderr, "-c (compress) flag cannot be combined with -r (reconstruct)\n");
                        exit(-1);
                    }
            }

        if( extract )
            {
                if( outFilename == "" )
                    {
                        fprintf(stderr, "-x (extract) flag requires -o (can't use stdout since we need to be able to retrieve chunks from earlier in the output file)\n");
                        exit(-1);
                    }
            }

        if( reconstruct && (chunkDir == "") )
            {
                errorOut("-r (reconstruct) requires -d (chunk directory)"
                         " to be specified\n");
            }

        if (maxChunkSize != 0 && minChunkSize != 0) {
            if (minChunkSize > maxChunkSize) {
                errorOut("max chunk size (-M) should probably be at least"
                         " 8 times as large as min chunk size (-m)\n");
            }

            const uint probableChunkSize = 1 << bits;
            if (minMaxWarnings && 2 * probableChunkSize > maxChunkSize) {
                fprintf(stderr,
                        "WARNING: max chunk size (-M) should be at least"
                        " 2 * 2**bits\n");
            } else if (minMaxWarnings &&
                       probableChunkSize <= 2 * minChunkSize) {
                fprintf(stderr,
                        "WARNING:min chunk size (-m) should be no more"
                        " than 0.5 * 2**bits\n");
            }
        } else if (maxChunkSize != 0) {
            errorOut("if you specify -m (min chunk size) you must specify"
                     " -M (max chunk size) as well\n");
        } else if (minChunkSize != 0) {
            errorOut("if you specify -M (max chunk size) you must specify"
                     " -m (min chunk size) as well\n");
        }
    }
};

class OptionsChunkProcessor : public ChunkProcessor
{
private:
    vector<ChunkProcessor*> processors;
    DataSource*             dataSource;

protected:
    virtual void internalProcessByte(unsigned char c)
    {
        ChunkProcessor::internalProcessByte(c);
        for(vector<ChunkProcessor*>::iterator procIter = processors.begin();
            procIter != processors.end();
            ++procIter)
            {
                (*procIter)->internalProcessByte(c);
            }
    }

    virtual void internalCompleteChunk(u_int64_t hash, u_int64_t fingerprint)
    {
        ChunkProcessor::internalCompleteChunk(hash, fingerprint);
        for(vector<ChunkProcessor*>::iterator procIter = processors.begin();
            procIter != processors.end();
            ++procIter)
            {
                (*procIter)->internalCompleteChunk(hash, fingerprint);
            }
    }

public:
    OptionsChunkProcessor(Options opts, int maxChunkSize)
        : dataSource(NULL)
    {
        FILE* is = fopen(opts.inFilename.c_str(), "r");

        if( is == 0 )
            {
                printf("Could not open %s\n", opts.inFilename.c_str());
                exit(-2);
            }

        //build list of processors
        if( opts.print ) processors.push_back(new PrintChunkProcessor());

        if( opts.chunkDir != "" )
            {
                processors.push_back(new CreateFileChunkProcessor(opts.chunkDir));
            }

        if( opts.statsDir != "" )
            {
                processors.push_back(new StatsChunkProcessor(opts.statsDir,
                                                             opts.statsNotation,
                                                             opts.statsDirLevels,
                                                             opts.inFilename,
                                                             fileno(is)));
            }

        if( opts.compress )
            {
                FILE* out = stdout;
                if( opts.outFilename != "" )
                    {
                        out = fopen(opts.outFilename.c_str(), "w");

                        if( out == NULL )
                            {
                                fprintf(stderr, "Couldn't open %s\n", opts.outFilename.c_str());
                                exit(-2);
                            }
                    }

                processors.push_back(new CompressChunkProcessor(out, maxChunkSize));
            }

        if( opts.extract )
            {
                dataSource = new ExtractDataSource(opts.outFilename.c_str(), is);
                ExtractChunkProcessor* extractProc = new ExtractChunkProcessor(maxChunkSize, (ExtractDataSource*)dataSource);
                processors.push_back(extractProc);
            }

        if( dataSource == NULL )
            {
                dataSource = new RawFileDataSource(is);
            }
    } // OptionsChunkProcessor

    ~OptionsChunkProcessor()
    {
        for(vector<ChunkProcessor*>::iterator procIter = processors.begin();
            procIter != processors.end();
            ++procIter)
            {
                delete *procIter;
            }

        delete dataSource;
    } // ~OptionsChunkProcessor

    DataSource* getDataSource()
    {
        return dataSource;
    }
}; // class OptionsChunkProcessor


int main(int argc, char **argv)
{
    Options opts(argc, argv);

    MaxChunkBoundaryChecker *cbc;
    if (opts.minChunkSize != 0 && opts.maxChunkSize != 0) {
        cbc =
            new SpecifiedChunkBoundaryChecker(opts.bits,
                                              opts.minChunkSize,
                                              opts.maxChunkSize,
                                              opts.boundaryMarker);
    } else {
        cbc = new BitwiseChunkBoundaryChecker(opts.bits);
    }

    OptionsChunkProcessor cp(opts, cbc->getMaxChunkSize());
    processChunks(cp.getDataSource(), *cbc, cp);

    delete cbc;
}
