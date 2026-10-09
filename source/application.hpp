#pragma once

#include "graphics_internal.hpp"

struct GLFWwindow;

namespace application {

	bool initialize(GLFWwindow* window);
	void shutdown();

	void update(double time);
	void render(const graphics::internal::FrameData& fd);

} // namespace application