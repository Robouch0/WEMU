#include "WemuEngineVulkan.h"

int main(int ac, char *av[]) {
	try {
		WemuEngineVulkan app;

		app.run();
	} catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}