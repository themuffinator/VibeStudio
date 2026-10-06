#include "core/model_pose.h"

#include <QQuaternion>

#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool finite(ModelVec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
double length(ModelVec3 v) { return std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z); }
ModelVec3 normalized(ModelVec3 v)
{
	const double magnitude = length(v);
	return {float(v.x / magnitude), float(v.y / magnitude), float(v.z / magnitude)};
}
bool validAmount(double amount) { return std::isfinite(amount) && amount >= 0 && amount <= 1; }
bool rigid(const ModelTag &tag)
{
	if (!finite(tag.origin))
	{
		return false;
	}
	// Match editable-document admission tolerance, including imported rounding.
	for (int row = 0; row < 3; ++row)
	{
		for (int other = 0; other < 3; ++other)
		{
			double dot = 0;
			for (int column = 0; column < 3; ++column)
			{
				dot += double(tag.axis[row * 3 + column]) * tag.axis[other * 3 + column];
			}
			if (!std::isfinite(dot) || std::abs(dot - (row == other ? 1 : 0)) > 0.01)
			{
				return false;
			}
		}
	}
	return true;
}
bool reflected(const ModelTag &tag)
{
	const auto *a = tag.axis;
	return double(a[0]) * (double(a[4]) * a[8] - double(a[5]) * a[7]) - double(a[1]) * (double(a[3]) * a[8] - double(a[5]) * a[6]) +
			   double(a[2]) * (double(a[3]) * a[7] - double(a[4]) * a[6]) <
		   0;
}
QQuaternion rotation(const ModelTag &tag, bool reflect)
{
	QMatrix3x3 matrix;
	// Tag rows are local basis vectors; Qt stores them as matrix columns.
	// Factor a shared local-Z reflection out before quaternion conversion.
	for (int row = 0; row < 3; ++row)
	{
		for (int column = 0; column < 3; ++column)
		{
			matrix(row, column) = tag.axis[column * 3 + row] * (reflect && column == 2 ? -1.f : 1.f);
		}
	}
	return QQuaternion::fromRotationMatrix(matrix).normalized();
}
} // namespace

bool sampleModelAnimation(int first, int count, double offset, bool interpolate, ModelAnimationSample *output)
{
	if (!output || first < 0 || count <= 0 || first > std::numeric_limits<int>::max() - count || !std::isfinite(offset) || offset < 0)
	{
		return false;
	}
	const double position = std::fmod(offset, double(count));
	const int relative = int(std::floor(position));
	*output = {first + relative, first + (relative + 1) % count, interpolate && count > 1 ? position - relative : 0};
	return true;
}

ModelVec3 interpolateModelPosition(ModelVec3 first, ModelVec3 second, double amount)
{
	if (amount == 0)
	{
		return first;
	}
	if (amount == 1)
	{
		return second;
	}
	return {float(double(first.x) * (1 - amount) + double(second.x) * amount),
			float(double(first.y) * (1 - amount) + double(second.y) * amount),
			float(double(first.z) * (1 - amount) + double(second.z) * amount)};
}

bool interpolateModelNormal(ModelVec3 first, ModelVec3 second, double amount, ModelVec3 *output)
{
	if (!output || !validAmount(amount) || !finite(first) || !finite(second) || length(first) < 1e-6 || length(second) < 1e-6)
	{
		return false;
	}
	const auto normal = interpolateModelPosition(normalized(first), normalized(second), amount);
	if (length(normal) < 1e-6)
	{
		return false;
	}
	*output = normalized(normal);
	return true;
}

bool interpolateModelTag(const ModelTag &first, const ModelTag &second, double amount, ModelTag *output)
{
	if (!output || !validAmount(amount) || first.name != second.name || !rigid(first) || !rigid(second) ||
		reflected(first) != reflected(second))
	{
		return false;
	}
	if (amount == 0 || amount == 1)
	{
		*output = amount == 0 ? first : second;
		output->frameIndex = first.frameIndex;
		return true;
	}
	const bool reflect = reflected(first);
	auto result = first;
	result.origin = interpolateModelPosition(first.origin, second.origin, amount);
	const auto matrix =
		QQuaternion::slerp(rotation(first, reflect), rotation(second, reflect), float(amount)).normalized().toRotationMatrix();
	for (int axis = 0; axis < 3; ++axis)
	{
		for (int component = 0; component < 3; ++component)
		{
			result.axis[axis * 3 + component] = matrix(component, axis) * (reflect && axis == 2 ? -1.f : 1.f);
		}
	}
	*output = result;
	return true;
}
} // namespace vibestudio
