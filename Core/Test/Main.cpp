#include "Core/Decoder.h"

#include <iostream>

int main(int argc, char* argv[])
{
	const char* path = argc > 1 ? argv[1] : "testdata/add_order_a.bin";
	Decoder decoder;
	unique_ptr<Book> book = decoder.decode_file(path);
	if (book == nullptr) {
		return 1;
	}
	std::cout << "core_test: decoded " << path << "\n"
	          << " bid_levels=" << book->bid.size() << "\n"
	          << " ask_levels=" << book->ask.size() << "\n";
	std::cout << book->bid.top()->order_reference_number() << endl;
	return 0;
}
