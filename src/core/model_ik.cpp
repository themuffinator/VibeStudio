#include "core/model_ik.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelIk)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
// Double precision construction; retained poses use the normal float format.
struct V
{
	double x = 0, y = 0, z = 0;
	V operator+(V b) const
	{
		return {x + b.x, y + b.y, z + b.z};
	}
	V operator-(V b) const
	{
		return {x - b.x, y - b.y, z - b.z};
	}
	V operator*(double n) const
	{
		return {x * n, y * n, z * n};
	}
	double dot(V b) const
	{
		return x * b.x + y * b.y + z * b.z;
	}
	V cross(V b) const
	{
		return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
	}
	double length() const
	{
		return std::sqrt(dot(*this));
	}
	V unit() const
	{
		return *this * (1 / length());
	}
	ModelVec3 vec() const
	{
		return {float(x), float(y), float(z)};
	}
};
V v(ModelVec3 p)
{
	return {p.x, p.y, p.z};
}
V origin(const ModelJointMatrix &m)
{
	return v(modelJointTranslation(m));
}
bool finite(ModelVec3 p)
{
	return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 1000000 && std::abs(p.y) <= 1000000 &&
		   std::abs(p.z) <= 1000000;
}
double tolerance(double a, double b)
{
	return std::max(0.00001, (a + b) * 0.000002);
}
int endIndex(const ModelMesh &mesh, const ModelIkOptions &o)
{
	const int end = modelJointIndex(mesh.skeleton, o.endJoint, Qt::CaseSensitive);
	if (end < 0 || o.clip < 0 || o.clip >= mesh.skeleton.clips.size() || o.first < 0 || o.last < o.first ||
		o.last >= mesh.skeleton.clips[o.clip].frames.size())
		return -1;
	const int middle = mesh.skeleton.joints[end].parent;
	if (middle < 0 || middle >= end || mesh.skeleton.joints[middle].parent < 0 || mesh.skeleton.joints[middle].parent >= middle)
		return -1;
	return end;
}
// Shortest rigid swing, with an explicit deterministic half-turn axis.
// Left-multiplication preserves the original affine basis (including scale).
ModelJointMatrix swing(V from, V to, V preferredAxis)
{
	from = from.unit();
	to = to.unit();
	const double cosine = std::clamp(from.dot(to), -1.0, 1.0);
	V axis = from.cross(to);
	const double sine = axis.length();
	if (sine < 1e-12)
	{
		if (cosine > 0)
			return modelJointIdentity();
		axis = preferredAxis - from * preferredAxis.dot(from);
		if (axis.length() < 1e-12)
		{
			const V basis = std::abs(from.x) <= std::abs(from.y) && std::abs(from.x) <= std::abs(from.z) ? V{1, 0, 0}
							: std::abs(from.y) <= std::abs(from.z)										 ? V{0, 1, 0}
																										 : V{0, 0, 1};
			axis = from.cross(basis);
		}
	}
	axis = axis.unit();
	const double q[3] = {axis.x, axis.y, axis.z};
	const double skew[3][3] = {{0, -axis.z, axis.y}, {axis.z, 0, -axis.x}, {-axis.y, axis.x, 0}};
	ModelJointMatrix result;
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			result.m[r * 4 + c] = float((r == c ? cosine : 0) + (1 - cosine) * q[r] * q[c] + sine * skew[r][c]);
	return result;
}
ModelJointMatrix moved(const ModelJointMatrix &matrix, const ModelJointMatrix &rotation, V from, V to)
{
	auto result = modelJointMultiply(rotation, matrix);
	const auto position = to + v(modelJointTransformVector(rotation, (origin(matrix) - from).vec()));
	result.m[3] = float(position.x);
	result.m[7] = float(position.y);
	result.m[11] = float(position.z);
	return result;
}
bool solve(const ModelSkeleton &skeleton, QVector<ModelJointMatrix> *pose, int end, const ModelIkOptions &o, QString *error,
		   ModelWorkProgress &work)
{
	const int middle = skeleton.joints[end].parent, root = skeleton.joints[middle].parent;
	const auto before = *pose;
	const V a = origin(before[root]), b = origin(before[middle]), c = origin(before[end]), target = v(o.target), pole = v(o.pole);
	const double upper = (b - a).length(), lower = (c - b).length(), distance = (target - a).length();
	if (!std::isfinite(upper + lower + distance) || upper <= 1e-6 || lower <= 1e-6)
		return fail(error, Text::tr("Inverse kinematics needs two nonzero bone lengths in every selected sample."));
	const double tol = tolerance(upper, lower), low = std::abs(upper - lower), high = upper + lower;
	if (!o.nearestReachable && (distance < low - tol || distance > high + tol))
		return fail(error, Text::tr("The target is outside this sample's reach (%1–%2 model units). Choose nearest reachable to clamp it.")
							   .arg(low, 0, 'g', 9)
							   .arg(high, 0, 'g', 9));
	const double reach = std::clamp(distance, low, high);
	V elbow, tip, normal;
	if (reach < 1e-12)
	{
		if ((pole - a).length() < 1e-6)
			return fail(error, Text::tr("The pole must differ from the root for a completely folded chain."));
		elbow = a + (pole - a).unit() * upper;
		tip = a;
		normal = (b - a).cross(elbow - a);
	}
	else
	{
		V axis = target - a;
		if (axis.length() < 1e-12)
			axis = (c - a).length() > 1e-12 ? c - a : b - a;
		axis = axis.unit();
		const double along = (upper * upper - lower * lower + reach * reach) / (2 * reach);
		const double height = std::sqrt(std::max(0.0, upper * upper - along * along));
		V bend = (pole - a) - axis * (pole - a).dot(axis);
		if (height > tol && bend.length() < 1e-6)
			return fail(error, Text::tr("The pole lies on the root-to-target line. Move it off the line to choose a bend direction."));
		bend = bend.length() > 1e-12 ? bend.unit() : V{};
		elbow = a + axis * along + bend * height;
		tip = a + axis * reach;
		normal = axis.cross(bend);
	}
	// Repeating a solved positional constraint must not accumulate float noise.
	if ((elbow - b).length() <= tol && (tip - c).length() <= tol)
		return true;
	const auto upperRotation = swing(b - a, elbow - a, normal), lowerRotation = swing(c - b, tip - elbow, normal);
	const auto endRotation = o.keepEndOrientation ? modelJointIdentity() : lowerRotation;
	QVector<int> anchors(skeleton.joints.size(), -1);
	for (int j = 0; j < pose->size(); ++j)
	{
		if (!work.step())
			return false;
		if (j == root || j == middle || j == end)
			anchors[j] = j;
		else if (skeleton.joints[j].parent >= 0)
			anchors[j] = anchors[skeleton.joints[j].parent];
		const int anchor = anchors[j];
		if (anchor < 0)
			continue;
		(*pose)[j] = anchor == root		? moved(before[j], upperRotation, a, a)
					 : anchor == middle ? moved(before[j], lowerRotation, b, elbow)
										: moved(before[j], endRotation, c, tip);
	}
	const auto actualMiddle = origin((*pose)[middle]), actualTip = origin((*pose)[end]);
	if (std::abs((actualMiddle - a).length() - upper) > tol || std::abs((actualTip - actualMiddle).length() - lower) > tol ||
		(actualTip - tip).length() > tol)
		return fail(
			error,
			Text::tr("The solved pose cannot preserve bone lengths at this coordinate precision. Move the model closer to its origin."));
	return true;
}
} // namespace
bool ModelIkOptions::operator==(const ModelIkOptions &o) const
{
	return clip == o.clip && first == o.first && last == o.last && endJoint == o.endJoint && target.x == o.target.x &&
		   target.y == o.target.y && target.z == o.target.z && pole.x == o.pole.x && pole.y == o.pole.y && pole.z == o.pole.z &&
		   nearestReachable == o.nearestReachable && keepEndOrientation == o.keepEndOrientation;
}
QVector<ModelIkSample> modelIkSamples(const ModelMesh &mesh, const ModelIkOptions &o)
{
	QVector<ModelIkSample> result;
	const int end = endIndex(mesh, o);
	if (end < 0)
		return result;
	const int middle = mesh.skeleton.joints[end].parent, root = mesh.skeleton.joints[middle].parent;
	for (int f = o.first; f <= o.last; ++f)
	{
		const auto &pose = mesh.skeleton.clips[o.clip].frames[f];
		if (pose.size() != mesh.skeleton.joints.size())
			return {};
		const V a = origin(pose[root]), b = origin(pose[middle]), c = origin(pose[end]);
		const double tol = tolerance((b - a).length(), (c - b).length()), residual = (c - v(o.target)).length();
		result.append({f, c.vec(), residual, tol, residual <= tol});
	}
	return result;
}
bool applyModelIk(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	const auto &o = edit.ik;
	if (edit.kind != ModelEditKind::SolveTwoBoneIk || !defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Inverse kinematics accepts only its own chain, sample and target settings."));
	if (!validateModelSkeleton(*mesh, error))
		return false;
	const int end = endIndex(*mesh, o);
	if (end < 0)
		return fail(error,
					Text::tr("Choose a retained clip, a valid inclusive sample range and an end joint with a parent and grandparent."));
	if (!finite(o.target) || !finite(o.pole))
		return fail(error, Text::tr("Target and pole coordinates must be finite and within one million model units."));
	QVector<QVector<ModelJointMatrix>> samples;
	bool changed = false;
	for (int frame = o.first; frame <= o.last; ++frame)
	{
		if (!work.check())
			return false;
		auto pose = mesh->skeleton.clips[o.clip].frames[frame];
		if (!solve(mesh->skeleton, &pose, end, o, error, work))
		{
			if (error)
				*error = Text::tr("Sample %1: %2").arg(frame).arg(*error);
			return false;
		}
		for (int j = 0; j < pose.size(); ++j)
			changed = changed || modelJointMatrixDistance(pose[j], mesh->skeleton.clips[o.clip].frames[frame][j]) != 0;
		samples.append(std::move(pose));
	}
	if (!changed)
		return work.check();
	return replaceModelSkeletalSamples(mesh, o.clip, o.first, samples, error, control);
}
} // namespace vibestudio
