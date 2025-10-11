/*
 * Copyright (c) 2025 Joseph Malicki
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef DIR_READER_H
#define DIR_READER_H

#include <stdint.h>

/**
 * Minimal DirectoryReader abstraction for tech de-risking
 * 
 * Just classic readdir() implementation - no io_uring yet
 * Goal: Provide interface for chaos testing
 */

struct dir_entry {
    uint64_t ino;
    uint8_t type;
    char name[256];
};

struct dir_reader;

// Create/destroy
struct dir_reader *dir_reader_create_classic(void);
void dir_reader_destroy(struct dir_reader *reader);

// Operations
int dir_reader_open(struct dir_reader *reader, const char *path);
int dir_reader_read(struct dir_reader *reader, 
                    struct dir_entry *entries, int max_entries);
void dir_reader_close(struct dir_reader *reader);

#endif /* DIR_READER_H */

