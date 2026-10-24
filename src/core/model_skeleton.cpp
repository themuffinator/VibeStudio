#include "core/model_skeleton.h"
#include "core/model_animation_curves.h"
#include "core/model_source_encoding.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

bool finite(float value)
{
	return std::isfinite(value);
}

bool finite(const ModelVec3& v)
{
	return finite(v.x) && finite(v.y) && finite(v.z);
}

ModelVec3 add(const ModelVec3& a, const ModelVec3& b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

ModelVec3 scaled(const ModelVec3& a, float s)
{
	return {a.x * s, a.y * s, a.z * s};
}

ModelVec3 subtract(const ModelVec3& a, const ModelVec3& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

ModelVec3 crossProduct(const ModelVec3& a, const ModelVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double lengthOf(const ModelVec3& v)
{
	return std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
}

ModelVec3 normalized(const ModelVec3& v)
{
	const double length = lengthOf(v);
	if (!(length > 1e-20)) {
		return {0.0f, 0.0f, 0.0f};
	}
	return {float(v.x / length), float(v.y / length), float(v.z / length)};
}

// The determinant of the 3x3 part, in double precision.
double determinant3(const ModelJointMatrix& a)
{
	const float* m = a.m;
	return double(m[0]) * (double(m[5]) * m[10] - double(m[6]) * m[9])
		- double(m[1]) * (double(m[4]) * m[10] - double(m[6]) * m[8])
		+ double(m[2]) * (double(m[4]) * m[9] - double(m[5]) * m[8]);
}

} // namespace

// --- Maths -----------------------------------------------------------------

ModelJointMatrix modelJointIdentity()
{
	return {};
}

ModelJointMatrix modelJointMatrix(const ModelQuat& rotation, const ModelVec3& translation, const ModelVec3& scale)
{
	const ModelQuat q = modelQuatNormalized(rotation);
	const double x = q.x, y = q.y, z = q.z, w = q.w;
	const double xx = x * x, yy = y * y, zz = z * z;
	const double xy = x * y, xz = x * z, yz = y * z;
	const double wx = w * x, wy = w * y, wz = w * z;
	ModelJointMatrix out;
	// Standard column-vector rotation matrix, each column scaled.
	out.m[0] = float((1.0 - 2.0 * (yy + zz)) * scale.x);
	out.m[1] = float((2.0 * (xy - wz)) * scale.y);
	out.m[2] = float((2.0 * (xz + wy)) * scale.z);
	out.m[3] = translation.x;
	out.m[4] = float((2.0 * (xy + wz)) * scale.x);
	out.m[5] = float((1.0 - 2.0 * (xx + zz)) * scale.y);
	out.m[6] = float((2.0 * (yz - wx)) * scale.z);
	out.m[7] = translation.y;
	out.m[8] = float((2.0 * (xz - wy)) * scale.x);
	out.m[9] = float((2.0 * (yz + wx)) * scale.y);
	out.m[10] = float((1.0 - 2.0 * (xx + yy)) * scale.z);
	out.m[11] = translation.z;
	return out;
}

ModelJointMatrix modelJointMultiply(const ModelJointMatrix& a, const ModelJointMatrix& b)
{
	ModelJointMatrix out;
	for (int row = 0; row < 3; ++row) {
		const float* ar = a.m + row * 4;
		for (int column = 0; column < 4; ++column) {
			double sum = double(ar[0]) * b.m[column] + double(ar[1]) * b.m[4 + column] + double(ar[2]) * b.m[8 + column];
			if (column == 3) {
				sum += ar[3];
			}
			out.m[row * 4 + column] = float(sum);
		}
	}
	return out;
}

ModelJointMatrix modelJointInverse(const ModelJointMatrix& a, bool* ok)
{
	const double det = determinant3(a);
	if (!std::isfinite(det) || std::abs(det) < 1e-30) {
		if (ok) { *ok = false; }
		return {};
	}
	const float* m = a.m;
	const double inv = 1.0 / det;
	double r[9];
	r[0] = (double(m[5]) * m[10] - double(m[6]) * m[9]) * inv;
	r[1] = (double(m[2]) * m[9] - double(m[1]) * m[10]) * inv;
	r[2] = (double(m[1]) * m[6] - double(m[2]) * m[5]) * inv;
	r[3] = (double(m[6]) * m[8] - double(m[4]) * m[10]) * inv;
	r[4] = (double(m[0]) * m[10] - double(m[2]) * m[8]) * inv;
	r[5] = (double(m[2]) * m[4] - double(m[0]) * m[6]) * inv;
	r[6] = (double(m[4]) * m[9] - double(m[5]) * m[8]) * inv;
	r[7] = (double(m[1]) * m[8] - double(m[0]) * m[9]) * inv;
	r[8] = (double(m[0]) * m[5] - double(m[1]) * m[4]) * inv;
	ModelJointMatrix out;
	for (int row = 0; row < 3; ++row) {
		out.m[row * 4 + 0] = float(r[row * 3 + 0]);
		out.m[row * 4 + 1] = float(r[row * 3 + 1]);
		out.m[row * 4 + 2] = float(r[row * 3 + 2]);
		out.m[row * 4 + 3] = float(-(r[row * 3 + 0] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]));
	}
	if (ok) { *ok = modelJointMatrixIsFinite(out); }
	return out;
}

ModelVec3 modelJointTransformPoint(const ModelJointMatrix& a, const ModelVec3& p)
{
	const float* m = a.m;
	return {float(double(m[0]) * p.x + double(m[1]) * p.y + double(m[2]) * p.z + m[3]),
		float(double(m[4]) * p.x + double(m[5]) * p.y + double(m[6]) * p.z + m[7]),
		float(double(m[8]) * p.x + double(m[9]) * p.y + double(m[10]) * p.z + m[11])};
}

ModelVec3 modelJointTransformVector(const ModelJointMatrix& a, const ModelVec3& v)
{
	const float* m = a.m;
	return {float(double(m[0]) * v.x + double(m[1]) * v.y + double(m[2]) * v.z),
		float(double(m[4]) * v.x + double(m[5]) * v.y + double(m[6]) * v.z),
		float(double(m[8]) * v.x + double(m[9]) * v.y + double(m[10]) * v.z)};
}

ModelVec3 modelJointTransformNormal(const ModelJointMatrix& a, const ModelVec3& n)
{
	// The inverse transpose's 3x3 part is the cofactor matrix over the
	// determinant; the determinant's sign keeps the normal's side and its
	// magnitude drops out in the normalization.
	const float* m = a.m;
	const double c00 = double(m[5]) * m[10] - double(m[6]) * m[9];
	const double c01 = double(m[6]) * m[8] - double(m[4]) * m[10];
	const double c02 = double(m[4]) * m[9] - double(m[5]) * m[8];
	const double c10 = double(m[2]) * m[9] - double(m[1]) * m[10];
	const double c11 = double(m[0]) * m[10] - double(m[2]) * m[8];
	const double c12 = double(m[1]) * m[8] - double(m[0]) * m[9];
	const double c20 = double(m[1]) * m[6] - double(m[2]) * m[5];
	const double c21 = double(m[2]) * m[4] - double(m[0]) * m[6];
	const double c22 = double(m[0]) * m[5] - double(m[1]) * m[4];
	const double det = determinant3(a);
	const double sign = det < 0 ? -1.0 : 1.0;
	const ModelVec3 out{float(sign * (c00 * n.x + c01 * n.y + c02 * n.z)), float(sign * (c10 * n.x + c11 * n.y + c12 * n.z)),
		float(sign * (c20 * n.x + c21 * n.y + c22 * n.z))};
	return normalized(out);
}

ModelVec3 modelJointTranslation(const ModelJointMatrix& a)
{
	return {a.m[3], a.m[7], a.m[11]};
}

ModelVec3 modelJointScale(const ModelJointMatrix& a)
{
	return {float(lengthOf({a.m[0], a.m[4], a.m[8]})), float(lengthOf({a.m[1], a.m[5], a.m[9]})),
		float(lengthOf({a.m[2], a.m[6], a.m[10]}))};
}

ModelQuat modelJointRotation(const ModelJointMatrix& a)
{
	ModelVec3 scale = modelJointScale(a);
	if (determinant3(a) < 0) {
		scale.x = -scale.x;
	}
	const double sx = scale.x != 0.0f ? scale.x : 1.0;
	const double sy = scale.y != 0.0f ? scale.y : 1.0;
	const double sz = scale.z != 0.0f ? scale.z : 1.0;
	const double r00 = a.m[0] / sx, r01 = a.m[1] / sy, r02 = a.m[2] / sz;
	const double r10 = a.m[4] / sx, r11 = a.m[5] / sy, r12 = a.m[6] / sz;
	const double r20 = a.m[8] / sx, r21 = a.m[9] / sy, r22 = a.m[10] / sz;
	const double trace = r00 + r11 + r22;
	double x = 0, y = 0, z = 0, w = 1;
	if (trace > 0) {
		const double s = std::sqrt(trace + 1.0) * 2.0;
		w = 0.25 * s;
		x = (r21 - r12) / s;
		y = (r02 - r20) / s;
		z = (r10 - r01) / s;
	} else if (r00 > r11 && r00 > r22) {
		const double s = std::sqrt(std::max(0.0, 1.0 + r00 - r11 - r22)) * 2.0;
		w = (r21 - r12) / s;
		x = 0.25 * s;
		y = (r01 + r10) / s;
		z = (r02 + r20) / s;
	} else if (r11 > r22) {
		const double s = std::sqrt(std::max(0.0, 1.0 + r11 - r00 - r22)) * 2.0;
		w = (r02 - r20) / s;
		x = (r01 + r10) / s;
		y = 0.25 * s;
		z = (r12 + r21) / s;
	} else {
		const double s = std::sqrt(std::max(0.0, 1.0 + r22 - r00 - r11)) * 2.0;
		w = (r10 - r01) / s;
		x = (r02 + r20) / s;
		y = (r12 + r21) / s;
		z = 0.25 * s;
	}
	ModelQuat q{float(x), float(y), float(z), float(w)};
	q = modelQuatNormalized(q);
	if (q.w < 0) {
		q = {-q.x, -q.y, -q.z, -q.w};
	}
	return q;
}

bool modelJointMatrixIsFinite(const ModelJointMatrix& a)
{
	for (float value : a.m) {
		if (!finite(value)) {
			return false;
		}
	}
	return true;
}

double modelJointMatrixDistance(const ModelJointMatrix& a, const ModelJointMatrix& b)
{
	double distance = 0;
	for (int i = 0; i < 12; ++i) {
		distance = std::max(distance, std::abs(double(a.m[i]) - double(b.m[i])));
	}
	return distance;
}

ModelQuat modelQuatNormalized(const ModelQuat& q)
{
	const double length = std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w);
	if (!(length > 1e-20) || !std::isfinite(length)) {
		return {};
	}
	return {float(q.x / length), float(q.y / length), float(q.z / length), float(q.w / length)};
}

ModelQuat modelQuatMultiply(const ModelQuat& a, const ModelQuat& b)
{
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

ModelQuat modelQuatConjugate(const ModelQuat& q)
{
	return {-q.x, -q.y, -q.z, q.w};
}

ModelVec3 modelQuatRotate(const ModelQuat& q, const ModelVec3& v)
{
	// v' = v + 2w(u x v) + 2u x (u x v), with u the vector part.
	const ModelVec3 u{q.x, q.y, q.z};
	const ModelVec3 t = scaled(crossProduct(u, v), 2.0f);
	return add(add(v, scaled(t, q.w)), crossProduct(u, t));
}

ModelQuat modelQuatSlerp(const ModelQuat& from, const ModelQuat& toIn, float t)
{
	ModelQuat to = toIn;
	double cosine = double(from.x) * to.x + double(from.y) * to.y + double(from.z) * to.z + double(from.w) * to.w;
	if (cosine < 0) {
		cosine = -cosine;
		to = {-to.x, -to.y, -to.z, -to.w};
	}
	double a = 1.0 - t, b = t;
	if (cosine < 0.9995) {
		const double angle = std::acos(std::min(1.0, cosine));
		const double sine = std::sin(angle);
		a = std::sin((1.0 - t) * angle) / sine;
		b = std::sin(t * angle) / sine;
	}
	return modelQuatNormalized({float(a * from.x + b * to.x), float(a * from.y + b * to.y), float(a * from.z + b * to.z),
		float(a * from.w + b * to.w)});
}

ModelQuat modelQuatFromXyzNegativeW(float x, float y, float z)
{
	const double remainder = 1.0 - (double(x) * x + double(y) * y + double(z) * z);
	const float w = remainder > 0 ? float(-std::sqrt(remainder)) : 0.0f;
	return modelQuatNormalized({x, y, z, w});
}

ModelVec3 modelQuatToXyzNegativeW(const ModelQuat& in)
{
	ModelQuat q = modelQuatNormalized(in);
	if (q.w > 0) {
		q = {-q.x, -q.y, -q.z, -q.w};
	}
	return {q.x, q.y, q.z};
}

ModelJointMatrix modelJointFromQuakeAngles(float pitch, float yaw, float roll, const ModelVec3& translation)
{
	// AngleVectors from q_math.c (released Quake III Arena source).
	constexpr double degrees = 3.14159265358979323846 / 180.0;
	const double sy = std::sin(yaw * degrees), cy = std::cos(yaw * degrees);
	const double sp = std::sin(pitch * degrees), cp = std::cos(pitch * degrees);
	const double sr = std::sin(roll * degrees), cr = std::cos(roll * degrees);
	const double forward[3]{cp * cy, cp * sy, -sp};
	const double right[3]{-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
	const double up[3]{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
	ModelJointMatrix out;
	for (int row = 0; row < 3; ++row) {
		out.m[row * 4 + 0] = float(forward[row]);
		out.m[row * 4 + 1] = float(-right[row]);
		out.m[row * 4 + 2] = float(up[row]);
	}
	out.m[3] = translation.x;
	out.m[7] = translation.y;
	out.m[11] = translation.z;
	return out;
}

// --- Hierarchy -------------------------------------------------------------

QVector<ModelJointMatrix> modelJointsToModelSpace(const QVector<int>& parents, const QVector<ModelJointMatrix>& local)
{
	if (parents.size() != local.size()) {
		return {};
	}
	QVector<ModelJointMatrix> out(local.size());
	for (int joint = 0; joint < local.size(); ++joint) {
		const int parent = parents.at(joint);
		if (parent >= joint) {
			return {};
		}
		out[joint] = parent < 0 ? local.at(joint) : modelJointMultiply(out.at(parent), local.at(joint));
	}
	return out;
}

QVector<ModelJointMatrix> modelJointsToLocalSpace(const QVector<int>& parents, const QVector<ModelJointMatrix>& modelSpace)
{
	if (parents.size() != modelSpace.size()) {
		return {};
	}
	QVector<ModelJointMatrix> out(modelSpace.size());
	for (int joint = 0; joint < modelSpace.size(); ++joint) {
		const int parent = parents.at(joint);
		if (parent >= joint) {
			return {};
		}
		out[joint] = parent < 0 ? modelSpace.at(joint) : modelJointMultiply(modelJointInverse(modelSpace.at(parent)), modelSpace.at(joint));
	}
	return out;
}

QVector<int> modelJointParents(const ModelSkeleton& skeleton)
{
	QVector<int> parents;
	parents.reserve(skeleton.joints.size());
	for (const ModelJoint& joint : skeleton.joints) {
		parents.append(joint.parent);
	}
	return parents;
}

int modelJointIndex(const ModelSkeleton& skeleton, const QString& name, Qt::CaseSensitivity sensitivity)
{
	for (int index = 0; index < skeleton.joints.size(); ++index) {
		if (skeleton.joints.at(index).name.compare(name, sensitivity) == 0) {
			return index;
		}
	}
	return -1;
}

QVector<ModelJointMatrix> modelSkeletonBindPose(const ModelSkeleton& skeleton)
{
	QVector<ModelJointMatrix> pose;
	pose.reserve(skeleton.joints.size());
	for (const ModelJoint& joint : skeleton.joints) {
		pose.append(joint.bind);
	}
	return pose;
}

// --- Validation ------------------------------------------------------------

bool validateModelSkeleton(const ModelMesh& mesh, QString* error, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!work.check()) return false;
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	const ModelSkeleton& skeleton = mesh.skeleton;
	QSet<QString> names;
	for (int index = 0; index < skeleton.joints.size(); ++index) {
		if (!work.step()) return false;
		const ModelJoint& joint = skeleton.joints.at(index);
		if (joint.parent < -1 || joint.parent >= index) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Joint %1 (%2) names a parent that does not come before it.").arg(index).arg(joint.name));
		}
		const QString key = joint.name.toLower();
		if (names.contains(key)) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Two joints are both named \"%1\".").arg(joint.name));
		}
		names.insert(key);
		if (!modelJointMatrixIsFinite(joint.bind)) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Joint %1 (%2) has a bind pose that is not a finite number.").arg(index).arg(joint.name));
		}
	}
	for (const ModelSkeletalClip& clip : skeleton.clips) {
		for (int frame = 0; frame < clip.frames.size(); ++frame) {
			if (!work.step()) return false;
			if (clip.frames.at(frame).size() != skeleton.joints.size()) {
				return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Frame %1 of clip \"%2\" has %3 joint pose(s) for %4 joint(s).")
					.arg(frame).arg(clip.name).arg(clip.frames.at(frame).size()).arg(skeleton.joints.size()));
			}
			for (const ModelJointMatrix& matrix : clip.frames.at(frame)) {
				if (!work.step()) return false;
				if (!modelJointMatrixIsFinite(matrix)) {
					return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Frame %1 of clip \"%2\" has a joint pose that is not a finite number.").arg(frame).arg(clip.name));
				}
			}
		}
		if ((!clip.frameMins.isEmpty() && clip.frameMins.size() != clip.frames.size())
			|| (!clip.frameMaxs.isEmpty() && clip.frameMaxs.size() != clip.frames.size())) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Clip \"%1\" has bounds for a different number of frames than it holds.").arg(clip.name));
		}
	}
	for (const ModelSkeletalTag& tag : skeleton.tags) {
		if (tag.joint < 0 || tag.joint >= skeleton.joints.size()) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Tag \"%1\" follows a joint that does not exist.").arg(tag.name));
		}
		if (!modelJointMatrixIsFinite(tag.offset)) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Tag \"%1\" has an offset that is not a finite number.").arg(tag.name));
		}
	}
	for (const ModelSurface& surface : mesh.surfaces) {
		const ModelSurfaceSkinning& skinning = surface.skinning;
		if (skinning.isEmpty()) {
			continue;
		}
		if (skeleton.joints.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Surface \"%1\" has joint influences but the model has no joints.").arg(surface.name));
		}
		if (skinning.first.size() != surface.vertexCount || skinning.count.size() != surface.vertexCount) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Surface \"%1\" has influence ranges for %2 vertices but holds %3.")
				.arg(surface.name).arg(skinning.first.size()).arg(surface.vertexCount));
		}
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			const int first = skinning.first.at(vertex);
			const int count = skinning.count.at(vertex);
			if (first < 0 || count < 0 || qint64(first) + count > skinning.influences.size()) {
				return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Vertex %1 of surface \"%2\" has an influence range outside the list.").arg(vertex).arg(surface.name));
			}
			for (int index = first; index < first + count; ++index) {
				const ModelJointInfluence& influence = skinning.influences.at(index);
				if (influence.joint < 0 || influence.joint >= skeleton.joints.size()) {
					return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Vertex %1 of surface \"%2\" follows a joint that does not exist.").arg(vertex).arg(surface.name));
				}
				if (!finite(influence.weight) || !finite(influence.offset) || !finite(influence.normalOffset)) {
					return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Vertex %1 of surface \"%2\" has an influence that is not a finite number.").arg(vertex).arg(surface.name));
				}
			}
		}
	}
	return validateModelAnimationCurves(mesh, error, control);
}

int normalizeModelSkinningWeights(ModelSurfaceSkinning* skinning)
{
	if (!skinning || skinning->isEmpty()) {
		return 0;
	}
	int changed = 0;
	QVector<ModelJointInfluence> influences;
	influences.reserve(skinning->influences.size());
	QVector<int> first(skinning->first.size());
	QVector<int> count(skinning->count.size());
	for (int vertex = 0; vertex < skinning->first.size(); ++vertex) {
		const int start = skinning->first.at(vertex);
		const int length = skinning->count.at(vertex);
		double sum = 0;
		int kept = 0;
		for (int index = start; index < start + length && index < skinning->influences.size(); ++index) {
			const float weight = skinning->influences.at(index).weight;
			if (weight > 0 && finite(weight)) {
				sum += weight;
				++kept;
			}
		}
		first[vertex] = influences.size();
		bool vertexChanged = kept != length;
		for (int index = start; index < start + length && index < skinning->influences.size(); ++index) {
			ModelJointInfluence influence = skinning->influences.at(index);
			if (!(influence.weight > 0) || !finite(influence.weight)) {
				continue;
			}
			const float normalized = sum > 0 ? float(influence.weight / sum) : influence.weight;
			if (std::abs(normalized - influence.weight) > 1e-6f) {
				vertexChanged = true;
			}
			influence.weight = normalized;
			influences.append(influence);
		}
		count[vertex] = influences.size() - first.at(vertex);
		if (vertexChanged) {
			++changed;
		}
	}
	skinning->first = first;
	skinning->count = count;
	skinning->influences = influences;
	return changed;
}

// --- Skinning --------------------------------------------------------------

QVector<ModelVec3> modelFaceNormals(const ModelSurface& surface, const QVector<ModelVec3>& positions)
{
	QVector<ModelVec3> normals(positions.size(), ModelVec3{0.0f, 0.0f, 0.0f});
	for (const ModelTriangle& triangle : surface.triangles) {
		if (triangle.a < 0 || triangle.b < 0 || triangle.c < 0 || triangle.a >= positions.size() || triangle.b >= positions.size()
			|| triangle.c >= positions.size()) {
			continue;
		}
		const ModelVec3 a = positions.at(triangle.a), b = positions.at(triangle.b), c = positions.at(triangle.c);
		const ModelVec3 face = crossProduct(subtract(b, a), subtract(c, a));
		normals[triangle.a] = add(normals.at(triangle.a), face);
		normals[triangle.b] = add(normals.at(triangle.b), face);
		normals[triangle.c] = add(normals.at(triangle.c), face);
	}
	for (ModelVec3& normal : normals) {
		normal = normalized(normal);
		if (normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f) {
			normal = {0.0f, 0.0f, 1.0f};
		}
	}
	return normals;
}

bool skinModelSurface(const ModelSurface& surface, const QVector<ModelJointMatrix>& pose, ModelFrameGeometry* out, QString* error)
{
	const ModelSurfaceSkinning& skinning = surface.skinning;
	if (skinning.first.size() != surface.vertexCount || skinning.count.size() != surface.vertexCount) {
		if (error) { *error = QCoreApplication::translate("VibeStudioModelSkeleton", "Surface \"%1\" has no influence range for every vertex.").arg(surface.name); }
		return false;
	}
	out->positions.resize(surface.vertexCount);
	out->normals.resize(surface.vertexCount);
	bool missingNormals = false;
	for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
		ModelVec3 position{0.0f, 0.0f, 0.0f};
		ModelVec3 normal{0.0f, 0.0f, 0.0f};
		const int first = skinning.first.at(vertex);
		const int count = skinning.count.at(vertex);
		for (int index = first; index < first + count; ++index) {
			if (index < 0 || index >= skinning.influences.size()) {
				if (error) { *error = QCoreApplication::translate("VibeStudioModelSkeleton", "Vertex %1 of surface \"%2\" has an influence range outside the list.").arg(vertex).arg(surface.name); }
				return false;
			}
			const ModelJointInfluence& influence = skinning.influences.at(index);
			if (influence.joint < 0 || influence.joint >= pose.size()) {
				if (error) { *error = QCoreApplication::translate("VibeStudioModelSkeleton", "Vertex %1 of surface \"%2\" follows a joint the pose does not have.").arg(vertex).arg(surface.name); }
				return false;
			}
			const ModelJointMatrix& matrix = pose.at(influence.joint);
			position = add(position, scaled(modelJointTransformPoint(matrix, influence.offset), influence.weight));
			normal = add(normal, scaled(modelJointTransformVector(matrix, influence.normalOffset), influence.weight));
		}
		out->positions[vertex] = position;
		const ModelVec3 unit = normalized(normal);
		if (unit.x == 0.0f && unit.y == 0.0f && unit.z == 0.0f) {
			missingNormals = true;
		}
		out->normals[vertex] = unit;
	}
	if (missingNormals) {
		const QVector<ModelVec3> faces = modelFaceNormals(surface, out->positions);
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			const ModelVec3& normal = out->normals.at(vertex);
			if (normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f) {
				out->normals[vertex] = faces.at(vertex);
			}
		}
	}
	return true;
}

bool modelSkinningFromBindPose(const ModelSkeleton& skeleton, const QVector<ModelVec3>& positions, const QVector<ModelVec3>& normals,
	const QVector<int>& joints, const QVector<float>& weights, int perVertex, ModelSurfaceSkinning* out, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (perVertex <= 0 || joints.size() != positions.size() * perVertex || weights.size() != joints.size()
		|| (!normals.isEmpty() && normals.size() != positions.size())) {
		return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "The vertex weights do not match the vertex count."));
	}
	QVector<ModelJointMatrix> inverseBind;
	inverseBind.reserve(skeleton.joints.size());
	for (const ModelJoint& joint : skeleton.joints) {
		bool ok = true;
		inverseBind.append(modelJointInverse(joint.bind, &ok));
		if (!ok) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Joint \"%1\" has a bind pose that cannot be inverted.").arg(joint.name));
		}
	}
	ModelSurfaceSkinning skinning;
	skinning.first.reserve(positions.size());
	skinning.count.reserve(positions.size());
	for (int vertex = 0; vertex < positions.size(); ++vertex) {
		skinning.first.append(skinning.influences.size());
		for (int slot = 0; slot < perVertex; ++slot) {
			const int joint = joints.at(vertex * perVertex + slot);
			const float weight = weights.at(vertex * perVertex + slot);
			if (!(weight > 0) || !finite(weight)) {
				continue;
			}
			if (joint < 0 || joint >= skeleton.joints.size()) {
				return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Vertex %1 follows joint %2, which does not exist.").arg(vertex).arg(joint));
			}
			ModelJointInfluence influence;
			influence.joint = joint;
			influence.weight = weight;
			influence.offset = modelJointTransformPoint(inverseBind.at(joint), positions.at(vertex));
			if (!normals.isEmpty()) {
				// Skinning blends transformed direction vectors, then normalizes.
				// Keep the inverse-bind magnitude so bind scale cancels exactly.
				influence.normalOffset = modelJointTransformVector(inverseBind.at(joint), normals.at(vertex));
			}
			skinning.influences.append(influence);
		}
		skinning.count.append(skinning.influences.size() - skinning.first.last());
	}
	normalizeModelSkinningWeights(&skinning);
	*out = skinning;
	return true;
}

// --- Baking ----------------------------------------------------------------

QString modelBakedFrameName(const QString& clipName, int frame)
{
	const QString number = QString::number(frame);
	QString stem;
	for (const QChar character : clipName) {
		if (character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('-')) {
			stem.append(character.toLatin1() ? QChar(character.toLatin1()) : QLatin1Char('_'));
		} else if (!stem.endsWith(QLatin1Char('_'))) {
			stem.append(QLatin1Char('_'));
		}
	}
	if (stem.isEmpty()) {
		stem = QStringLiteral("frame");
	}
	// Frame names hold 15 characters and a terminator; the number survives.
	const int room = std::max(1, 15 - int(number.size()));
	return stem.left(room) + number;
}

bool bakeModelSkeleton(ModelMesh* mesh, const ModelSkeletonBakeOptions& options, QString* error, const ModelWorkControl& control)
{
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!mesh || mesh->skeleton.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "The model has no skeleton to bake."));
	}
	QString problem;
	if (!validateModelSkeleton(*mesh, &problem, control)) {
		return fail(problem);
	}
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!work.check()) {
		return false;
	}
	ModelSkeleton& skeleton = mesh->skeleton;

	struct PlannedFrame {
		int clip = -1;
		int frame = 0;
	};
	QVector<PlannedFrame> plan;
	const int limit = std::max(1, options.maxFrames);
	if (options.includeBindPose || skeleton.clips.isEmpty()) {
		plan.append({-1, 0});
	}
	qint64 totalVertices = 0;
	for (const ModelSurface& surface : mesh->surfaces) {
		totalVertices += surface.vertexCount;
	}
	const auto fits = [&](qint64 frames) {
		return frames <= limit && (totalVertices == 0 || frames * totalVertices <= std::max<qint64>(1, options.maxVertexSlots));
	};
	if (!fits(plan.size())) {
		return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "The model has too many vertices to hold even one baked frame."));
	}
	QVector<ModelAnimation> animations;
	QStringList skipped;
	for (int clipIndex = 0; clipIndex < skeleton.clips.size(); ++clipIndex) {
		const ModelSkeletalClip& clip = skeleton.clips.at(clipIndex);
		if (options.byIndex ? !options.clipIndexes.contains(clipIndex)
							: !options.clips.isEmpty() && !options.clips.contains(clip.name, Qt::CaseInsensitive)) {
			continue;
		}
		if (clip.frames.isEmpty()) {
			continue;
		}
		if (!fits(qint64(plan.size()) + clip.frames.size())) {
			skipped.append(clip.name);
			continue;
		}
		ModelAnimation animation;
		animation.name = clip.name;
		animation.firstFrame = plan.size();
		animation.frameCount = clip.frames.size();
		animation.framesPerSecond = clip.framesPerSecond > 0 && std::isfinite(clip.framesPerSecond) ? clip.framesPerSecond : 0.0;
		animations.append(animation);
		for (int frame = 0; frame < clip.frames.size(); ++frame) {
			plan.append({clipIndex, frame});
		}
	}

	const QVector<ModelJointMatrix> bindPose = modelSkeletonBindPose(skeleton);
	QVector<ModelSurface> surfaces = mesh->surfaces;
	for (ModelSurface& surface : surfaces) {
		if (!work.step()) { return false; }
		ModelFrameGeometry still;
		if (surface.skinning.isEmpty()) {
			if (surface.frames.isEmpty()) {
				return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Surface \"%1\" has neither joint influences nor geometry.").arg(surface.name));
			}
			still = surface.frames.first();
		}
		surface.frames.clear();
		surface.frames.reserve(plan.size());
		for (const PlannedFrame& planned : plan) {
			if (!work.step()) { return false; }
			if (surface.skinning.isEmpty()) {
				surface.frames.append(still);
				continue;
			}
			const QVector<ModelJointMatrix>& pose = planned.clip < 0 ? bindPose : skeleton.clips.at(planned.clip).frames.at(planned.frame);
			ModelFrameGeometry geometry;
			if (!skinModelSurface(surface, pose, &geometry, &problem)) {
				return fail(problem);
			}
			surface.frames.append(geometry);
		}
	}

	QVector<ModelFrameInfo> frames;
	frames.reserve(plan.size());
	QVector<ModelTag> tags;
	QVector<int> clipForFrame;
	clipForFrame.reserve(plan.size());
	for (int index = 0; index < plan.size(); ++index) {
		if (!work.step()) { return false; }
		const PlannedFrame& planned = plan.at(index);
		ModelFrameInfo info;
		info.index = index;
		info.name = planned.clip < 0 ? QStringLiteral("bindpose") : modelBakedFrameName(skeleton.clips.at(planned.clip).name, planned.frame);
		bool haveBounds = false;
		for (const ModelSurface& surface : surfaces) {
			for (const ModelVec3& position : surface.frames.at(index).positions) {
				if (!finite(position)) {
					continue;
				}
				if (!haveBounds) {
					info.mins = info.maxs = position;
					haveBounds = true;
					continue;
				}
				info.mins = {std::min(info.mins.x, position.x), std::min(info.mins.y, position.y), std::min(info.mins.z, position.z)};
				info.maxs = {std::max(info.maxs.x, position.x), std::max(info.maxs.y, position.y), std::max(info.maxs.z, position.z)};
			}
		}
		if (planned.clip >= 0) {
			const ModelSkeletalClip& clip = skeleton.clips.at(planned.clip);
			if (!haveBounds && planned.frame < clip.frameMins.size() && planned.frame < clip.frameMaxs.size()) {
				info.mins = clip.frameMins.at(planned.frame);
				info.maxs = clip.frameMaxs.at(planned.frame);
			}
		}
		const ModelVec3 centre{(info.mins.x + info.maxs.x) * 0.5f, (info.mins.y + info.maxs.y) * 0.5f, (info.mins.z + info.maxs.z) * 0.5f};
		info.radius = float(lengthOf(subtract(info.maxs, centre)));
		frames.append(info);
		clipForFrame.append(planned.clip);

		const QVector<ModelJointMatrix>& pose = planned.clip < 0 ? bindPose : skeleton.clips.at(planned.clip).frames.at(planned.frame);
		for (const ModelSkeletalTag& skeletalTag : skeleton.tags) {
			const ModelJointMatrix world = modelJointMultiply(pose.at(skeletalTag.joint), skeletalTag.offset);
			ModelTag tag;
			tag.name = skeletalTag.name;
			tag.frameIndex = index;
			tag.origin = modelJointTranslation(world);
			// ModelTag::axis is row-major with each row one axis (MD3's
			// axis[3][3]), so the rows are the matrix columns, normalized.
			for (int axis = 0; axis < 3; ++axis) {
				const ModelVec3 column = normalized({world.m[axis], world.m[4 + axis], world.m[8 + axis]});
				tag.axis[axis * 3 + 0] = column.x;
				tag.axis[axis * 3 + 1] = column.y;
				tag.axis[axis * 3 + 2] = column.z;
			}
			tags.append(tag);
		}
	}

	mesh->surfaces = surfaces;
	mesh->frames = frames;
	mesh->animations = animations;
	mesh->tags = tags;
	mesh->frameCount = frames.size();
	skeleton.bakedClipForFrame = clipForFrame;
	if (!skipped.isEmpty()) {
		// The skeleton keeps every clip, so this is a viewing limit, not lost
		// data: a detail line, which editable import does not refuse.
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelSkeleton", "%1 clip(s) were left out to keep the baked frames within %2 frames and %3 vertex positions: %4")
			.arg(skipped.size()).arg(limit).arg(options.maxVertexSlots).arg(skipped.join(QStringLiteral(", ")));
	}
	return true;
}

QStringList modelSkeletonSummaryLines(const ModelSkeleton& skeleton)
{
	QStringList lines;
	QVector<int> depth(skeleton.joints.size(), 0);
	for (int index = 0; index < skeleton.joints.size(); ++index) {
		const ModelJoint& joint = skeleton.joints.at(index);
		depth[index] = joint.parent >= 0 && joint.parent < index ? depth.at(joint.parent) + 1 : 0;
		lines << QStringLiteral("%1%2").arg(QString(depth.at(index) * 2, QLatin1Char(' ')), joint.name);
	}
	return lines;
}

// --- Editing ---------------------------------------------------------------

namespace {

double distanceBetween(const ModelVec3& a, const ModelVec3& b)
{
	return lengthOf(subtract(a, b));
}

} // namespace

int modelBindPoseFrame(const ModelMesh& mesh)
{
	const QVector<int>& map = mesh.skeleton.bakedClipForFrame;
	for (int index = 0; index < map.size() && index < mesh.frames.size(); ++index) {
		if (map.at(index) == -1) {
			return index;
		}
	}
	const int clipIndex = map.value(0, -3);
	if (clipIndex >= 0 && clipIndex < mesh.skeleton.clips.size() && !mesh.skeleton.clips.at(clipIndex).frames.isEmpty()) {
		const QVector<ModelJointMatrix>& first = mesh.skeleton.clips.at(clipIndex).frames.first();
		for (int joint = 0; joint < mesh.skeleton.joints.size() && joint < first.size(); ++joint) {
			if (modelJointMatrixDistance(mesh.skeleton.joints.at(joint).bind, first.at(joint)) > 1e-4) {
				return -1;
			}
		}
		return 0;
	}
	return -1;
}

int rebindModelSkinning(ModelMesh* mesh, int frame, double tolerance)
{
	if (!mesh || mesh->skeleton.isEmpty() || frame < 0) {
		return 0;
	}
	const QVector<ModelJointMatrix> bind = modelSkeletonBindPose(mesh->skeleton);
	const QVector<ModelJointMatrix> inverse = modelSkeletonInverseBindPose(mesh->skeleton);
	int rebound = 0;
	for (ModelSurface& surface : mesh->surfaces) {
		ModelSurfaceSkinning& skinning = surface.skinning;
		if (skinning.isEmpty() || skinning.first.size() != surface.vertexCount || frame >= surface.frames.size()) {
			continue;
		}
		ModelFrameGeometry skinned;
		if (!skinModelSurface(surface, bind, &skinned)) {
			continue;
		}
		const ModelFrameGeometry& target = surface.frames.at(frame);
		for (int vertex = 0; vertex < surface.vertexCount && vertex < target.positions.size(); ++vertex) {
			const ModelVec3 normal = vertex < target.normals.size() ? target.normals.at(vertex) : skinned.normals.value(vertex);
			const bool samePosition = distanceBetween(skinned.positions.at(vertex), target.positions.at(vertex)) <= tolerance;
			const double alignment = double(normal.x) * skinned.normals.at(vertex).x + double(normal.y) * skinned.normals.at(vertex).y
				+ double(normal.z) * skinned.normals.at(vertex).z;
			if (samePosition && alignment > 0.9999) {
				continue;
			}
			rebindModelSkinVertex(&skinning, vertex, inverse, target.positions.at(vertex), normal);
			++rebound;
		}
	}
	return rebound;
}

void rebindModelSkinVertex(ModelSurfaceSkinning* skinning, int vertex, const QVector<ModelJointMatrix>& inverseBind,
	const ModelVec3& position, const ModelVec3& normal)
{
	const int first = skinning->first.at(vertex);
	const int count = skinning->count.at(vertex);
	double sum = 0;
	for (int index = first; index < first + count; ++index) {
		sum += skinning->influences.at(index).weight;
	}
	// The weighted sum of one shared point is that point times the weights'
	// sum, so dividing by it reproduces the position even when the file's
	// weights do not add up to one.
	const float scale = sum > 1e-6 ? float(1.0 / sum) : 1.0f;
	for (int index = first; index < first + count; ++index) {
		ModelJointInfluence& influence = skinning->influences[index];
		if (influence.joint < 0 || influence.joint >= inverseBind.size()) {
			continue;
		}
		influence.offset = modelJointTransformPoint(inverseBind.at(influence.joint), scaled(position, scale));
		influence.normalOffset = modelJointTransformVector(inverseBind.at(influence.joint), normal);
	}
}

QVector<ModelJointMatrix> modelSkeletonInverseBindPose(const ModelSkeleton& skeleton)
{
	QVector<ModelJointMatrix> inverse;
	inverse.reserve(skeleton.joints.size());
	for (const ModelJoint& joint : skeleton.joints) {
		inverse.append(modelJointInverse(joint.bind));
	}
	return inverse;
}

bool rebakeModelSkeleton(ModelMesh* mesh, QString* error, const ModelWorkControl& control, int maxFrames, qint64 maxVertexSlots,
	const QVector<int>* clips)
{
	if (!mesh || mesh->skeleton.isEmpty()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioModelSkeleton", "The model has no skeleton to bake."); }
		return false;
	}
	const int bindFrame = modelBindPoseFrame(*mesh);
	if (bindFrame >= 0) {
		rebindModelSkinning(mesh, bindFrame);
	}
	ModelSkeletonBakeOptions options;
	options.includeBindPose = mesh->skeleton.bakedClipForFrame.contains(-1) || mesh->skeleton.clips.isEmpty();
	options.maxFrames = std::max(1, maxFrames);
	options.maxVertexSlots = std::max<qint64>(1, maxVertexSlots);
	if (clips) {
		options.byIndex = true;
		options.clipIndexes = *clips;
		// With no chosen poses the bind pose is the one editable frame.
		const bool posed = std::any_of(clips->cbegin(), clips->cend(), [mesh](int clip) {
			return clip >= 0 && clip < mesh->skeleton.clips.size() && !mesh->skeleton.clips.at(clip).frames.isEmpty();
		});
		options.includeBindPose = options.includeBindPose || !posed;
	}
	ModelMesh candidate = *mesh;
	if (!bakeModelSkeleton(&candidate, options, error, control)) {
		return false;
	}
	*mesh = candidate;
	return true;
}

namespace {

QJsonArray matrixJson(const ModelJointMatrix& matrix)
{
	QJsonArray array;
	for (float value : matrix.m) {
		array.append(modelSourceNumber(value));
	}
	return array;
}

bool readMatrix(const QJsonValue& value, ModelJointMatrix* out)
{
	const QJsonArray array = value.toArray();
	if (array.size() != 12) {
		return false;
	}
	for (int index = 0; index < 12; ++index) {
		if (!array.at(index).isDouble() || !std::isfinite(array.at(index).toDouble())) {
			return false;
		}
		out->m[index] = float(array.at(index).toDouble());
	}
	return true;
}

QJsonArray vectorsJson(const QVector<ModelVec3>& vectors)
{
	QJsonArray array;
	for (const ModelVec3& v : vectors) {
		array.append(QJsonArray{modelSourceNumber(v.x), modelSourceNumber(v.y), modelSourceNumber(v.z)});
	}
	return array;
}

bool readVectors(const QJsonValue& value, int count, QVector<ModelVec3>* out)
{
	const QJsonArray array = value.toArray();
	if (array.size() != count) {
		return false;
	}
	out->clear();
	for (const QJsonValue& entry : array) {
		const QJsonArray v = entry.toArray();
		if (v.size() != 3 || !v.at(0).isDouble() || !v.at(1).isDouble() || !v.at(2).isDouble()) {
			return false;
		}
		out->append({float(v.at(0).toDouble()), float(v.at(1).toDouble()), float(v.at(2).toDouble())});
	}
	return true;
}

} // namespace

QJsonObject modelSkeletonJson(const ModelMesh& mesh, bool packed, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Serializing);
	const ModelSkeleton& skeleton = mesh.skeleton;
	QJsonObject object;
	object.insert(QStringLiteral("sourceFormat"), skeleton.sourceFormat);
	QJsonArray joints;
	for (const ModelJoint& joint : skeleton.joints) {
		joints.append(QJsonObject{{QStringLiteral("name"), joint.name}, {QStringLiteral("parent"), joint.parent},
			{QStringLiteral("bind"), matrixJson(joint.bind)}, {QStringLiteral("flags"), double(joint.flags)}});
	}
	object.insert(QStringLiteral("joints"), joints);
	QJsonArray tags;
	for (const ModelSkeletalTag& tag : skeleton.tags) {
		tags.append(QJsonObject{{QStringLiteral("name"), tag.name}, {QStringLiteral("joint"), tag.joint},
			{QStringLiteral("offset"), matrixJson(tag.offset)}});
	}
	object.insert(QStringLiteral("tags"), tags);
	QJsonArray clips;
	for (const ModelSkeletalClip& clip : skeleton.clips) {
		// Packed sources hold a clip's frames as one string of matrices.
		QJsonArray frames;
		for (const QVector<ModelJointMatrix>& pose : packed ? QVector<QVector<ModelJointMatrix>>{} : clip.frames) {
			if (!work.step()) {
				return {};
			}
			QJsonArray values;
			for (const ModelJointMatrix& matrix : pose) {
				for (float value : matrix.m) {
					values.append(modelSourceNumber(value));
				}
			}
			frames.append(values);
		}
		QJsonObject entry{{QStringLiteral("name"), clip.name}, {QStringLiteral("source"), clip.sourcePath},
			{QStringLiteral("framesPerSecond"), clip.framesPerSecond}, {QStringLiteral("loops"), clip.loops},
			{QStringLiteral("frames"), packed ? packModelSourceMatrices(clip.frames) : QJsonValue(frames)}};
		if (!work.check()) {
			return {};
		}
		if (!clip.frameMins.isEmpty()) {
			entry.insert(QStringLiteral("mins"), packed ? packModelSourceVectors(clip.frameMins) : QJsonValue(vectorsJson(clip.frameMins)));
			entry.insert(QStringLiteral("maxs"), packed ? packModelSourceVectors(clip.frameMaxs) : QJsonValue(vectorsJson(clip.frameMaxs)));
		}
		if (!clip.curves.isEmpty()) entry.insert(QStringLiteral("curves"), modelClipCurvesJson(clip.curves));
		clips.append(entry);
	}
	object.insert(QStringLiteral("clips"), clips);
	QJsonArray map;
	for (int clip : skeleton.bakedClipForFrame) {
		map.append(clip);
	}
	object.insert(QStringLiteral("bakedClipForFrame"), map);
	QJsonArray skinning;
	for (int index = 0; index < mesh.surfaces.size(); ++index) {
		const ModelSurfaceSkinning& surface = mesh.surfaces.at(index).skinning;
		if (surface.isEmpty()) {
			continue;
		}
		if (packed) {
			skinning.append(QJsonObject{{QStringLiteral("surface"), index}, {QStringLiteral("count"), packModelSourceIntegers(surface.count)},
				{QStringLiteral("influences"), packModelSourceInfluences(surface.influences)}});
			continue;
		}
		QJsonArray counts, influences;
		for (int count : surface.count) {
			counts.append(count);
		}
		for (const ModelJointInfluence& influence : surface.influences) {
			influences.append(influence.joint);
			for (float value : {influence.weight, influence.offset.x, influence.offset.y, influence.offset.z, influence.normalOffset.x,
					 influence.normalOffset.y, influence.normalOffset.z}) {
				influences.append(modelSourceNumber(value));
			}
		}
		skinning.append(QJsonObject{{QStringLiteral("surface"), index}, {QStringLiteral("count"), counts}, {QStringLiteral("influences"), influences}});
	}
	object.insert(QStringLiteral("skinning"), skinning);
	return object;
}

bool modelSkeletonFromJson(const QJsonObject& object, ModelMesh* mesh, QString* error, bool packedArrays)
{
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	const QString malformed = QCoreApplication::translate("VibeStudioModelSkeleton", "The mesh source's skeleton is malformed.");
	if (!mesh) {
		return fail(malformed);
	}
	ModelMesh result = *mesh;
	ModelSkeleton skeleton;
	skeleton.sourceFormat = object.value(QStringLiteral("sourceFormat")).toString();
	const QJsonArray joints = object.value(QStringLiteral("joints")).toArray();
	if (joints.isEmpty() || joints.size() > 4096) {
		return fail(malformed);
	}
	for (const QJsonValue& value : joints) {
		const QJsonObject entry = value.toObject();
		ModelJoint joint;
		joint.name = entry.value(QStringLiteral("name")).toString();
		joint.parent = entry.value(QStringLiteral("parent")).toInt(-2);
		joint.flags = quint32(entry.value(QStringLiteral("flags")).toDouble());
		if (!entry.value(QStringLiteral("name")).isString() || !entry.value(QStringLiteral("parent")).isDouble()
			|| !readMatrix(entry.value(QStringLiteral("bind")), &joint.bind)) {
			return fail(malformed);
		}
		skeleton.joints.append(joint);
	}
	for (const QJsonValue& value : object.value(QStringLiteral("tags")).toArray()) {
		const QJsonObject entry = value.toObject();
		ModelSkeletalTag tag;
		tag.name = entry.value(QStringLiteral("name")).toString();
		tag.joint = entry.value(QStringLiteral("joint")).toInt(-1);
		if (!readMatrix(entry.value(QStringLiteral("offset")), &tag.offset)) {
			return fail(malformed);
		}
		skeleton.tags.append(tag);
	}
	qint64 matrices = 0;
	for (const QJsonValue& value : object.value(QStringLiteral("clips")).toArray()) {
		const QJsonObject entry = value.toObject();
		ModelSkeletalClip clip;
		clip.name = entry.value(QStringLiteral("name")).toString();
		clip.sourcePath = entry.value(QStringLiteral("source")).toString();
		clip.framesPerSecond = entry.value(QStringLiteral("framesPerSecond")).toDouble();
		clip.loops = entry.value(QStringLiteral("loops")).toBool(true);
		const QJsonValue framesValue = entry.value(QStringLiteral("frames"));
		if (packedArrays && framesValue.isString()) {
			if (!unpackModelSourceMatrices(framesValue, int(skeleton.joints.size()), 16LL * 1024 * 1024 - matrices, &clip.frames)) {
				return fail(malformed);
			}
			matrices += qint64(clip.frames.size()) * skeleton.joints.size();
		} else if (!framesValue.isArray()) {
			return fail(malformed);
		}
		const QJsonArray frames = framesValue.toArray();
		for (const QJsonValue& frameValue : frames) {
			const QJsonArray values = frameValue.toArray();
			if (values.size() != skeleton.joints.size() * 12) {
				return fail(malformed);
			}
			matrices += skeleton.joints.size();
			if (matrices > 16LL * 1024 * 1024) {
				return fail(malformed);
			}
			QVector<ModelJointMatrix> pose(skeleton.joints.size());
			for (int index = 0; index < values.size(); ++index) {
				if (!values.at(index).isDouble()) {
					return fail(malformed);
				}
				pose[index / 12].m[index % 12] = float(values.at(index).toDouble());
			}
			clip.frames.append(pose);
		}
		const auto bounds = [&](const QJsonValue& value, QVector<ModelVec3>* out) {
			if (packedArrays && value.isString()) {
				return unpackModelSourceVectors(value, clip.frames.size(), out) && out->size() == clip.frames.size();
			}
			return readVectors(value, int(clip.frames.size()), out);
		};
		if (entry.contains(QStringLiteral("mins"))
			&& (!bounds(entry.value(QStringLiteral("mins")), &clip.frameMins) || !bounds(entry.value(QStringLiteral("maxs")), &clip.frameMaxs))) {
			return fail(malformed);
		}
		if (entry.contains(QStringLiteral("curves")) && !modelClipCurvesFromJson(entry.value(QStringLiteral("curves")), int(clip.frames.size()), &clip.curves, error)) return false;
		skeleton.clips.append(clip);
	}
	for (const QJsonValue& value : object.value(QStringLiteral("bakedClipForFrame")).toArray()) {
		skeleton.bakedClipForFrame.append(value.toInt(-2));
	}
	if (!skeleton.bakedClipForFrame.isEmpty() && skeleton.bakedClipForFrame.size() != result.frames.size()) {
		return fail(malformed);
	}
	for (ModelSurface& surface : result.surfaces) {
		surface.skinning = {};
	}
	for (const QJsonValue& value : object.value(QStringLiteral("skinning")).toArray()) {
		const QJsonObject entry = value.toObject();
		const int index = entry.value(QStringLiteral("surface")).toInt(-1);
		if (index < 0 || index >= result.surfaces.size()) {
			return fail(malformed);
		}
		ModelSurface& surface = result.surfaces[index];
		const QJsonValue countValue = entry.value(QStringLiteral("count")), influenceValue = entry.value(QStringLiteral("influences"));
		if (packedArrays && countValue.isString() && influenceValue.isString()) {
			ModelSurfaceSkinning skinning;
			qint64 total = 0;
			if (!unpackModelSourceIntegers(countValue, surface.vertexCount, &skinning.count) || skinning.count.size() != surface.vertexCount
				|| !unpackModelSourceInfluences(influenceValue, qint64(surface.vertexCount) * 64, &skinning.influences)) {
				return fail(malformed);
			}
			for (int n : skinning.count) {
				if (n < 0 || n > 64) {
					return fail(malformed);
				}
				skinning.first.append(int(total));
				total += n;
			}
			if (total != skinning.influences.size()) {
				return fail(malformed);
			}
			surface.skinning = skinning;
			continue;
		}
		const QJsonArray counts = countValue.toArray();
		const QJsonArray influences = influenceValue.toArray();
		if (counts.size() != surface.vertexCount || influences.size() % 8 != 0) {
			return fail(malformed);
		}
		ModelSurfaceSkinning skinning;
		int total = 0;
		for (const QJsonValue& count : counts) {
			const int n = count.toInt(-1);
			if (n < 0 || n > 64) {
				return fail(malformed);
			}
			skinning.first.append(total);
			skinning.count.append(n);
			total += n;
		}
		if (total * 8 != influences.size()) {
			return fail(malformed);
		}
		for (int at = 0; at < influences.size(); at += 8) {
			ModelJointInfluence influence;
			influence.joint = influences.at(at).toInt(-1);
			influence.weight = float(influences.at(at + 1).toDouble());
			influence.offset = {float(influences.at(at + 2).toDouble()), float(influences.at(at + 3).toDouble()), float(influences.at(at + 4).toDouble())};
			influence.normalOffset = {float(influences.at(at + 5).toDouble()), float(influences.at(at + 6).toDouble()),
				float(influences.at(at + 7).toDouble())};
			skinning.influences.append(influence);
		}
		surface.skinning = skinning;
	}
	result.skeleton = skeleton;
	QString problem;
	if (!validateModelSkeleton(result, &problem)) {
		return fail(problem);
	}
	*mesh = result;
	return true;
}

} // namespace vibestudio
