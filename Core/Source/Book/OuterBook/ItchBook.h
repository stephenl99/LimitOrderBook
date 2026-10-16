#pragma once

#include <utility>

#include "ItchSecurityDirectory.h"
#include "OuterBook.h"

class ItchBook : public Book {
public:
    ItchSecurityDirectory itch_directory;

    void absorb(ItchBook&& other)
    {
        Book::absorb(std::move(other));
        itch_directory.absorb(std::move(other.itch_directory));
    }
};
