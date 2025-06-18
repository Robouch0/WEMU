#include "macros.hpp"

#include <string>
#include <ostream>
#include <iostream>
#include "firstapp.hpp"

int main(int ac, char **av) {
	WEMU::FirstApp app{};

	try {
		app.run();
	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}