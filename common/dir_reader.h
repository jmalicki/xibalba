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

