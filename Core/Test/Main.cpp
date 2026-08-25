#include "Core/Decoder.h"

#include <iostream>

int main(int argc, char* argv[])
{
	const char* path = (argc > 1) ? argv[1] : "testdata/add_order_a.bin";
	Decoder decoder;
	Book* book = decoder.decodeFile(path);
	if (book == nullptr) {
		return 1;
	}
	std::cout << "core_test: decoded " << path
	          << " bid_levels=" << book->bid.size()
	          << " ask_levels=" << book->ask.size() << "\n";
	delete book;
	return 0;
}
