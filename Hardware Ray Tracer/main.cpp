#include <iostream>
#include "App.h"

int main() {
	Core::App app{};

	throw std::runtime_error("Something went wrong!");

	app.run();
}