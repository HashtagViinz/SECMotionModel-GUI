#include "scene/FrameData.hpp"

#include <cmath>

namespace secgui {

double Vec3::norm() const {
	return std::sqrt(x * x + y * y + z * z);
}

} // namespace secgui
