#pragma once

#include <memory>
#include "Definitions.h"

struct Buffer {
    Buffer();
    // Initializes the buffer by reading the file.
    explicit Buffer(const char* fileWithPath);
    /* Accessors */
    byte*       data();
    const byte* data() const;
public:
    std::unique_ptr<byte[]> ptr;        // Storage array
    uint                    size;       // Bytes currently used
    uint                    capacity;   // Bytes available in total
};