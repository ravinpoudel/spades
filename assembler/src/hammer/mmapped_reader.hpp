//***************************************************************************
//* Copyright (c) 2011-2012 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#ifndef HAMMER_MMAPPED_READER_HPP
#define HAMMER_MMAPPED_READER_HPP

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <string>

class MMappedReader {
  int StreamFile;
  bool Unlink;
  std::string FileName;
  MMappedReader(const MMappedReader &) = delete;

 protected:
  uint8_t* MappedRegion;
  size_t FileSize, BytesRead;

 public:
  MMappedReader(const std::string &filename, bool unlink = false) {
    struct stat buf;
    FileName = filename;

    FileSize = (stat(FileName.c_str(), &buf) != 0 ? 0 : buf.st_size);    

    StreamFile = open(FileName.c_str(), O_RDONLY);
    VERIFY(StreamFile != -1);
    
    MappedRegion =
        (uint8_t*)mmap(NULL, FileSize, PROT_READ, MAP_FILE | MAP_PRIVATE,
                       StreamFile, 0);
    VERIFY((intptr_t)MappedRegion != -1L);

    BytesRead = 0;
    Unlink = unlink;
  }
  
  virtual ~MMappedReader() {
    munmap(MappedRegion, FileSize);
    close(StreamFile);

    if (Unlink)
      unlink(FileName.c_str());
  }

  void read(void* buf, size_t amount) {
    memcpy(buf, MappedRegion + BytesRead, amount);
    BytesRead += amount;
  }

  bool good() const {
    return BytesRead < FileSize;
  }

  size_t size() const { return FileSize; }
};

template<typename T>
class MMappedRecordReader : public MMappedReader {
 public:
  MMappedRecordReader(const std::string &FileName, bool unlink = true):
      MMappedReader(FileName, unlink) {
    VERIFY(FileSize % sizeof(T) == 0);
  }
  
  void read(T* el, size_t amount) {
    memcpy(el, MappedRegion + BytesRead, amount * sizeof(T));
    BytesRead += amount*sizeof(T);
  }

  size_t size() const { return FileSize / sizeof(T); }
};


#endif // HAMMER_MMAPPED_READER_HPP
