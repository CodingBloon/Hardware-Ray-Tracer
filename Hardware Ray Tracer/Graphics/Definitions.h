#pragma once

#include "Window.h"
#include <iostream>

#define VK_CHECK_RESULT(f, m) { if(f != VK_SUCCESS) throw std::runtime_error(m); }