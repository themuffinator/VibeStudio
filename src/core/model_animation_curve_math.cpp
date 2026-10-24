#include "core/model_animation_curves.h"
#include "core/model_skeleton.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationCurves)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
double secant(const QVector<ModelPoseCurveKey> &keys, int left, int right)
{
	return (keys[right].value - keys[left].value) / double(keys[right].frame - keys[left].frame);
}
std::array<double, 4> coefficients(const QVector<ModelPoseCurveKey> &keys, int left)
{
	const auto &a = keys[left], &b = keys[left + 1];
	const double distance = b.value - a.value;
	if (a.interpolation == ModelCurveInterpolation::Step)
		return {a.value, 0, 0, 0};
	if (a.interpolation == ModelCurveInterpolation::StepNext)
		return {b.value, 0, 0, 0};
	if (a.interpolation == ModelCurveInterpolation::Linear)
		return {a.value, distance, 0, 0};
	const double span = b.frame - a.frame, m0 = modelCurveSlopes(keys, left).second * span,
				 m1 = modelCurveSlopes(keys, left + 1).first * span;
	return {a.value, m0, 3 * distance - 2 * m0 - m1, -2 * distance + m0 + m1};
}
double polynomial(const std::array<double, 4> &p, double t)
{
	return ((p[3] * t + p[2]) * t + p[1]) * t + p[0];
}
template <class F> void stationaryPoints(const std::array<double, 4> &p, const F &include)
{
	const double scale = std::max({std::abs(3 * p[3]), std::abs(2 * p[2]), std::abs(p[1])});
	if (scale == 0)
		return;
	// Normalisation avoids an absolute cutoff that would discard small but
	// real extrema, and keeps the discriminant away from overflow/underflow.
	const double a = 3 * p[3] / scale, b = 2 * p[2] / scale, c = p[1] / scale;
	if (a == 0)
	{
		if (b != 0)
			include(-c / b);
		return;
	}
	const double discriminant = std::fma(-4 * a, c, b * b);
	if (discriminant >= 0)
	{
		const double q = -.5 * (b + std::copysign(std::sqrt(discriminant), b));
		include(q / a);
		if (q != 0)
			include(c / q);
	}
}
} // namespace
QString modelCurveChannelId(int channel)
{
	return QStringList{"tx", "ty", "tz", "rx", "ry", "rz", "sx", "sy", "sz"}.value(channel);
}
int modelCurveChannelIndex(const QString &id)
{
	for (int i = 0; i < 9; ++i)
		if (modelCurveChannelId(i) == id)
			return i;
	return -1;
}
QPair<double, double> modelCurveSlopes(const QVector<ModelPoseCurveKey> &keys, int index)
{
	if (index < 0 || index >= keys.size())
		return {0, 0};
	const auto &key = keys[index];
	if (key.tangent == ModelCurveTangent::Free)
		return {key.inSlope, key.outSlope};
	if (key.tangent == ModelCurveTangent::Flat || keys.size() < 2)
		return {0, 0};
	double slope = 0;
	if (index == 0)
		slope = secant(keys, 0, 1);
	else if (index == keys.size() - 1)
		slope = secant(keys, index - 1, index);
	else
	{
		const double before = secant(keys, index - 1, index), after = secant(keys, index, index + 1);
		const double left = key.frame - keys[index - 1].frame, right = keys[index + 1].frame - key.frame;
		slope = (before * right + after * left) / (left + right);
		if (key.tangent == ModelCurveTangent::Clamped)
			slope =
				before * after <= 0 ? 0 : std::copysign(std::min(std::abs(slope), 3 * std::min(std::abs(before), std::abs(after))), slope);
	}
	return {slope, slope};
}
double evaluateModelCurve(const QVector<ModelPoseCurveKey> &keys, double frame)
{
	if (keys.isEmpty())
		return 0;
	if (frame <= keys.first().frame)
		return keys.first().value;
	if (frame >= keys.last().frame)
		return keys.last().value;
	const auto it = std::upper_bound(keys.cbegin(), keys.cend(), frame, [](double f, const ModelPoseCurveKey &k) { return f < k.frame; });
	const int left = int(it - keys.cbegin()) - 1;
	if (frame == keys[left].frame)
		return keys[left].value;
	return polynomial(coefficients(keys, left), (frame - keys[left].frame) / double(keys[left + 1].frame - keys[left].frame));
}
QPair<double, double> modelCurveRange(const QVector<ModelPoseCurveKey> &keys)
{
	if (keys.isEmpty())
		return {0, 0};
	double low = keys.first().value, high = low;
	const auto include = [&](double x)
	{
		low = std::min(low, x);
		high = std::max(high, x);
	};
	for (int i = 0; i < keys.size(); ++i)
	{
		include(keys[i].value);
		if (i + 1 == keys.size())
			break;
		const auto p = coefficients(keys, i);
		const auto root = [&](double t)
		{
			if (t > 0 && t < 1)
				include(polynomial(p, t));
		};
		stationaryPoints(p, root);
	}
	return {low, high};
}
ModelJointMatrix evaluateModelPoseCurves(const ModelPoseCurves &group, double frame)
{
	double v[9];
	for (int c = 0; c < 9; ++c)
		v[c] = evaluateModelCurve(group.channels[c], frame);
	const double radians = std::numbers::pi / 180, x = std::remainder(v[3], 360.0) * radians, y = std::remainder(v[4], 360.0) * radians,
				 z = std::remainder(v[5], 360.0) * radians;
	const double sx = std::sin(x), cx = std::cos(x), sy = std::sin(y), cy = std::cos(y), sz = std::sin(z), cz = std::cos(z);
	return {{float(cz * cy * v[6]), float((cz * sy * sx - sz * cx) * v[7]), float((cz * sy * cx + sz * sx) * v[8]), float(v[0]),
			 float(sz * cy * v[6]), float((sz * sy * sx + cz * cx) * v[7]), float((sz * sy * cx - cz * sx) * v[8]), float(v[1]),
			 float(-sy * v[6]), float(cy * sx * v[7]), float(cy * cx * v[8]), float(v[2])}};
}
bool modelCurveDifferenceBound(const QVector<ModelPoseCurveKey> &a, const QVector<ModelPoseCurveKey> &b, double first, double last,
							   double *maximum, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!maximum || a.isEmpty() || b.isEmpty() || !std::isfinite(first) || !std::isfinite(last) || first > last)
		return fail(error, Text::tr("Curve comparison requires two valid curves and an ordered finite range."));
	if (!work.check())
		return false;
	double peak = std::max(std::abs(evaluateModelCurve(a, first) - evaluateModelCurve(b, first)),
						   std::abs(evaluateModelCurve(a, last) - evaluateModelCurve(b, last)));
	const auto indexAt = [](const auto &keys, double time)
	{
		return int(std::upper_bound(keys.cbegin(), keys.cend(), time, [](double t, const auto &key) { return t < key.frame; }) -
				   keys.cbegin()) -
			   1;
	};
	int ia = indexAt(a, first), ib = indexAt(b, first);
	const auto piece = [](const auto &keys, int index, double start, double end) -> std::array<double, 4>
	{
		if (index < 0)
			return {keys.first().value, 0, 0, 0};
		if (index + 1 >= keys.size())
			return {keys.last().value, 0, 0, 0};
		const auto p = coefficients(keys, index);
		const double span = keys[index + 1].frame - keys[index].frame;
		const double x = (start - keys[index].frame) / span, s = (end - start) / span;
		return {polynomial(p, x), s * (p[1] + 2 * p[2] * x + 3 * p[3] * x * x), s * s * (p[2] + 3 * p[3] * x), s * s * s * p[3]};
	};
	for (double time = first; time < last;)
	{
		if (!work.step())
			return false;
		const double next =
			std::min({last, ia + 1 < a.size() ? double(a[ia + 1].frame) : last, ib + 1 < b.size() ? double(b[ib + 1].frame) : last});
		const auto pa = piece(a, ia, time, next), pb = piece(b, ib, time, next);
		std::array<double, 4> difference;
		for (int i = 0; i < 4; ++i)
			difference[i] = pa[i] - pb[i];
		const auto include = [&](double u)
		{
			if (u >= 0 && u <= 1)
				peak = std::max(peak, std::abs(polynomial(difference, u)));
		};
		include(0);
		include(1);
		stationaryPoints(difference, include);
		time = next;
		// Hold-next may have a distinct value exactly at a key. Check all knots,
		// as well as the one-sided limits included by the polynomial pieces.
		const double actualA = ia + 1 < a.size() && a[ia + 1].frame == time ? a[ia + 1].value : polynomial(pa, 1);
		const double actualB = ib + 1 < b.size() && b[ib + 1].frame == time ? b[ib + 1].value : polynomial(pb, 1);
		peak = std::max(peak, std::abs(actualA - actualB));
		if (ia + 1 < a.size() && a[ia + 1].frame <= time)
			++ia;
		if (ib + 1 < b.size() && b[ib + 1].frame <= time)
			++ib;
	}
	if (!work.check())
		return false;
	*maximum = peak;
	return true;
}
ModelPoseCurveKey sampleModelCurveKey(const QVector<ModelPoseCurveKey> &keys, int frame)
{
	ModelPoseCurveKey key;
	key.frame = frame;
	key.tangent = ModelCurveTangent::Free;
	key.value = evaluateModelCurve(keys, frame);
	const auto found = std::lower_bound(keys.cbegin(), keys.cend(), frame, [](const auto &k, int f) { return k.frame < f; });
	if (found != keys.cend() && found->frame == frame)
	{
		const auto slopes = modelCurveSlopes(keys, int(found - keys.cbegin()));
		key.inSlope = slopes.first;
		key.outSlope = slopes.second;
		key.interpolation = found->interpolation;
	}
	else if (found != keys.cbegin() && found != keys.cend())
	{
		const int left = int(found - keys.cbegin()) - 1;
		const auto p = coefficients(keys, left);
		const double span = keys[left + 1].frame - keys[left].frame, u = (frame - keys[left].frame) / span;
		key.inSlope = key.outSlope = (p[1] + u * (2 * p[2] + 3 * p[3] * u)) / span;
		key.interpolation = keys[left].interpolation;
	}
	return key;
}
bool sliceModelCurve(const QVector<ModelPoseCurveKey> &keys, int first, int last, int targetFirst, double factor, bool reverse,
					 int targetFrames, QVector<ModelPoseCurveKey> *result, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!result || keys.isEmpty() || first < 0 || last < first || targetFirst < 0 || !std::isfinite(factor) || factor <= 0 ||
		factor > 1000000)
		return fail(error, Text::tr("Choose a nonempty curve, an ordered range and a positive finite time scale."));
	QVector<ModelPoseCurveKey> slice{sampleModelCurveKey(keys, first)};
	for (int i = 0; i < keys.size(); ++i)
	{
		if (!work.step())
			return false;
		if (keys[i].frame > first && keys[i].frame < last)
			slice.append(sampleModelCurveKey(keys, keys[i].frame));
	}
	if (last > first)
		slice.append(sampleModelCurveKey(keys, last));
	QVector<ModelPoseCurveKey> mapped;
	for (int n = 0; n < slice.size(); ++n)
	{
		if (!work.step())
			return false;
		const int i = reverse ? int(slice.size()) - 1 - n : n;
		auto key = slice[i];
		const double time = targetFirst + double(reverse ? last - key.frame : key.frame - first) * factor;
		const double rounded = std::round(time);
		if (!std::isfinite(time) || rounded < 0 || rounded >= targetFrames || std::abs(time - rounded) > 1e-7)
			return fail(error, Text::tr("Every transferred key and boundary must land on an integer sample inside the destination clip. "
										"Change the scale or resize the clip."));
		key.frame = int(rounded);
		if (reverse)
		{
			std::swap(key.inSlope, key.outSlope);
			key.inSlope = -key.inSlope;
			key.outSlope = -key.outSlope;
			key.interpolation = i > 0 ? slice[i - 1].interpolation : ModelCurveInterpolation::Linear;
			if (key.interpolation == ModelCurveInterpolation::Step)
				key.interpolation = ModelCurveInterpolation::StepNext;
			else if (key.interpolation == ModelCurveInterpolation::StepNext)
				key.interpolation = ModelCurveInterpolation::Step;
		}
		key.inSlope /= factor;
		key.outSlope /= factor;
		if (!mapped.isEmpty() && key.frame <= mapped.last().frame)
			return fail(error, Text::tr("Time scaling would merge animation keys."));
		mapped.append(key);
	}
	if (!work.check())
		return false;
	*result = std::move(mapped);
	return true;
}
bool validateModelCurveGroup(const ModelPoseCurves &group, int frameCount, QString *error)
{
	if (group.joint.isEmpty() || frameCount < 1)
		return fail(error, Text::tr("Animation curves require a named joint and a nonempty clip."));
	qint64 count = 0;
	for (int channel = 0; channel < 9; ++channel)
	{
		const auto &keys = group.channels[channel];
		count += keys.size();
		if (keys.isEmpty() || keys.size() > frameCount || count > modelCurveMaxKeys)
			return fail(error,
						Text::tr("Each joint needs nine nonempty channels, at most one key per sample, within the curve key budget."));
		int previous = -1;
		for (const auto &key : keys)
		{
			if (key.frame <= previous || key.frame >= frameCount || int(key.interpolation) < 0 || int(key.interpolation) > 3 ||
				int(key.tangent) < 0 || int(key.tangent) > 3)
				return fail(error,
							Text::tr("Curve keys must have distinct increasing sample indices and valid interpolation and tangent modes."));
			for (double value : {key.value, key.inSlope, key.outSlope})
				if (!std::isfinite(value) || std::abs(value) > 1000000)
					return fail(error, Text::tr("Curve values and slopes must be finite and within one million units."));
			previous = key.frame;
		}
		const auto range = modelCurveRange(keys);
		if (!std::isfinite(range.first) || !std::isfinite(range.second) || range.first < -1000000 || range.second > 1000000 ||
			(channel >= 6 && range.first <= 0.000001))
			return fail(error,
						Text::tr("Channel %1 exceeds its allowed range between keys. Scale must remain positive; adjust cubic tangents.")
							.arg(modelCurveChannelId(channel)));
	}
	return true;
}
} // namespace vibestudio
