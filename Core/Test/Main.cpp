#include "Core/Decoder.h"

#include <iostream>

int main(int argc, char* argv[])
{
	const char* path = argc > 1 ? argv[1] : "testdata/add_order_a.bin";
	Decoder decoder;
	std::unique_ptr<Book> books = decoder.decode_file(path);
	if (books == nullptr) {
		return 1;
	}
	const auto book = books->find(1);
	if (!book.has_value()) {
		std::cout << "core_test: no book for locate 1\n";
		return 1;
	}
	std::cout << "core_test: decoded " << path << "\n"
	          << " locate=" << (*book)->stock_locate() << "\n"
	          << " bid_levels=" << (*book)->bid_levels.size() << "\n"
	          << " ask_levels=" << (*book)->ask_levels.size() << "\n";
	if (!(*book)->bid_levels.empty()) {
		const Level& level = (*book)->bid_levels.begin()->second;
		if (!level.orders.empty()) {
			std::cout << level.orders.front().order_reference_number() << "\n";
		}
	}
	return 0;
}
