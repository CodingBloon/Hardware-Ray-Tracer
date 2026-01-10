#include "Graphics/RayTracing/RTApp.h"

int main() {

	try {
		RayTracing::RTApp app;

		app.run();
	} catch (const std::runtime_error& e) {
		system("pause");
		return EXIT_FAILURE;
	}
}