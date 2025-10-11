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

#include "dir_reader.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**
 * Minimal DirectoryReader implementation
 * 
 * Just wraps opendir/readdir for tech de-risking PoC
 */

struct dir_reader {
    DIR *dir;
};

struct dir_reader *dir_reader_create_classic(void) {
    return calloc(1, sizeof(struct dir_reader));
}

void dir_reader_destroy(struct dir_reader *reader) {
    if (reader) {
        free(reader);
    }
}

int dir_reader_open(struct dir_reader *reader, const char *path) {
    if (!reader || !path)
        return -EINVAL;
    
    reader->dir = opendir(path);
    if (!reader->dir)
        return -errno;
    
    return 0;
}

int dir_reader_read(struct dir_reader *reader,
                    struct dir_entry *entries, int max_entries) {
    if (!reader || !reader->dir || !entries || max_entries <= 0)
        return -EINVAL;
    
    int count = 0;
    while (count < max_entries) {
        errno = 0;
        struct dirent *ent = readdir(reader->dir);
        if (!ent) {
            if (errno != 0)
                return -errno;  // Error
            break;  // EOF
        }
        
        entries[count].ino = ent->d_ino;
        entries[count].type = ent->d_type;
        // Use snprintf for safe string copying with guaranteed null-termination
        (void)snprintf(entries[count].name, sizeof(entries[count].name), "%s", ent->d_name);
        count++;
    }
    
    return count;
}

void dir_reader_close(struct dir_reader *reader) {
    if (reader && reader->dir) {
        closedir(reader->dir);
        reader->dir = NULL;
    }
}

