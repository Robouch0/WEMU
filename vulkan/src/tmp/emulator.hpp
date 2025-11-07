#pragma once

#include "window.hpp"

// std
#include <memory>
#include <vector>

namespace WEMU {
class Emulator {
	public:
		static constexpr int WIDTH = 800;
		static constexpr int HEIGHT = 600;

  		void run();

 	private:
  		void loadGameObjects();

  		Window lveWindow{WIDTH, HEIGHT, "Vulkan Tutorial"};
};
}  // namespace lve