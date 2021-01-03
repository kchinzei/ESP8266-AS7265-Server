/*
  The MIT License (MIT)
  Copyright (c) Kiyo Chinzei (kchinzei@gmail.com)
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/
/*
  bufferedFile class

  When constructed it allocates good size of buffer in the heap and writes SPIFFS after the buffer fills or the filel closed.
  It's intended to ease the wearing of ESP's flash memory. Writing a small chunk of data will increase the number of writing,
  resulting a shorter lifetime.

  Physical block/page and erase block size of flash memory varies by models/productions/lots. But I assume usually 256 bytes and 4KB.
  (https://www.esp32.com/viewtopic.php?t=561)
  
  Make Asayake to Wake Project.
  Kiyo Chinzei
  https://github.com/kchinzei/ESP8266-AS7265-Server
*/

#ifndef _bufferedfile_h_
#define _bufferedfile_h_

#include <FS.h>

class bufferedFile {
public:
    static const size_t bufferChunkSize = 4096;
    size_t preferredSize;
    const char *fname;

    bufferedFile(size_t preferredSize = 0) {
        this->preferredSize = preferredSize;
        this->_size = 0;
        this->fname = nullptr;
        this->_current = this->_buf = nullptr;
    }

    bool open(const String filename) {
        return this->open(filename.c_str());
    }
    
    bool open(const char *filename) {
        size_t heapsize = ESP.getFreeHeap();

        // I don't have an idea how much heap should be left.
        // Simply I assume I can use up to half of it.
        if (this->preferredSize <= 0) this->preferredSize = heapsize/2;
        if (heapsize/2 >= this->preferredSize)
            this->_size = this->preferredSize;
        else
            this->_size = heapsize/2;

        if (this->_size % this->bufferChunkSize)
            this->_size = (this->_size / this->bufferChunkSize) * this->bufferChunkSize;
        if (this->_size <= 0) {
            this->_size = min(this->bufferChunkSize, heapsize/2);
        }

        if (this->_buf) free(this->_buf);
        this->_current = this->_buf = (uint8_t *) malloc(this->_size);

        this->fname = filename;
        this->_fp = SPIFFS.open(this->fname, "w");
        return !!this->_fp;
    }

    size_t write(const uint8_t *buf, size_t length) {
        size_t written = 0;
        
        if (this->_buf) {
            while (length) {
                size_t size_extra = this->_size - (this->_current - this->_buf);
                
                if (length < size_extra) {
                    memcpy(this->_current, buf, length);
                    Serial.printf("store %ld bytes; extra= %ld bytes\n", length, size_extra);
                    this->_current += length;
                    written += length;
                    length = 0;
                } else {
                    // Need buffer flush.
                    memcpy(this->_current, buf, size_extra);
                    Serial.printf("store %ld bytes; extra= %ld bytes; REACH MAX\n", length, size_extra);
                    this->_fp.write(this->_buf, this->_size);
                    Serial.printf("write %ld bytes\n", this->_size);
                    this->_current = this->_buf;
                    length -= size_extra;
                    written += size_extra;
                }
            }
        } else {
            // When it fails to malloc, it does not use buffer.
            this->_fp.write(buf, length);
            written = length;
        }
        return written;
    }

    size_t close() {
        size_t written = 0;
        
        if (this->_buf) {
            size_t size_remain = this->_current - this->_buf;
            Serial.printf("write %ld bytes remaining\n", size_remain);
            if (size_remain > 0) {
                this->_fp.write(this->_buf, size_remain);
                written += size_remain;
            }
            free(this->_buf);
        }
        this->_fp.close();
        this->fname = nullptr;
        this->_buf = nullptr;
        this->_size = 0;
        return written;
    }

    ~bufferedFile() {
        this->close();
    }
    
private:
    File _fp;
    size_t _size;
    uint8_t * _buf;
    uint8_t * _current;
};

#endif // _bufferedfile_h_
