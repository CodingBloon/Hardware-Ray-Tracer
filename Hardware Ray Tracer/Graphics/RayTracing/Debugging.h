#pragma once

#include <iostream>

#define DEBUG(message) std::cout << message << std::endl
#define BUILD(name, step, stepCount, message) std::cout << "[INFO] " << name << ": " << step << " of " << stepCount << " completed! " << message << std::endl