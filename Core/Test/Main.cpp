#include "Core/Decoder.h"

#include <iostream>

#include "Core/Entrypoint.h"

int main(int argc, char* argv[])
{
	const char* path = argc > 1 ? argv[1] : "testdata/add_order_a.bin";
	Entrypoint::enter(path);
	return 0;
}
