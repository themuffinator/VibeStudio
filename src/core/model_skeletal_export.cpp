#include "core/model_skeletal_export.h"
#include "core/model_file_io.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QDir>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vibestudio {

namespace {

// Half a degree between unit normals.
constexpr double kNormalAlignment = 0.99996;

bool samePosition(const ModelVec3& a, const ModelVec3& b)
{
	return std::abs(double(a.x) - b.x) <= 1e-4 && std::abs(double(a.y) - b.y) <= 1e-4 && std::abs(double(a.z) - b.z) <= 1e-4;
}

bool sameNormal(const ModelVec3& a, const ModelVec3& b)
{
	const double la = std::sqrt(double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z);
	const double lb = std::sqrt(double(b.x) * b.x + double(b.y) * b.y + double(b.z) * b.z);
	if (!(la > 1e-12) || !(lb > 1e-12)) {
		return la <= 1e-12 && lb <= 1e-12;
	}
	return (double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z) / (la * lb) >= kNormalAlignment;
}

// Vertices of `actual` that differ from `expected`.
int differingVertices(const ModelFrameGeometry& expected, const ModelFrameGeometry& actual, int vertexCount)
{
	int differing = 0;
	for (int vertex = 0; vertex < vertexCount; ++vertex) {
		if (vertex >= actual.positions.size() || vertex >= expected.positions.size()) {
			++differing;
			continue;
		}
		bool same = samePosition(expected.positions.at(vertex), actual.positions.at(vertex));
		if (same && vertex < expected.normals.size() && vertex < actual.normals.size()) {
			same = sameNormal(expected.normals.at(vertex), actual.normals.at(vertex));
		}
		if (!same) {
			++differing;
		}
	}
	return differing;
}

} // namespace

// --- Joint rigidity --------------------------------------------------------

ModelJointMatrix modelJointRigidPart(const ModelJointMatrix& matrix)
{
	return modelJointMatrix(modelJointRotation(matrix), modelJointTranslation(matrix));
}

ModelJointMatrix modelJointStretch(const ModelJointMatrix& matrix)
{
	// The rigid part's inverse is its transpose with the translation undone;
	// only the 3x3 product is wanted, so the translation is dropped.
	const ModelJointMatrix rotation = modelJointMatrix(modelJointRotation(matrix), ModelVec3{});
	ModelJointMatrix out;
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			double sum = 0;
			for (int k = 0; k < 3; ++k) {
				// transpose(rotation)[row][k] = rotation[k][row]
				sum += double(rotation.m[k * 4 + row]) * matrix.m[k * 4 + column];
			}
			out.m[row * 4 + column] = float(sum);
		}
		out.m[row * 4 + 3] = 0.0f;
	}
	return out;
}

bool modelJointIsRigid(const ModelJointMatrix& matrix, double tolerance)
{
	return modelJointMatrixIsFinite(matrix) && modelJointMatrixDistance(modelJointStretch(matrix), modelJointIdentity()) <= tolerance;
}

// --- Skeletal clips ----------------------------------------------------------

int modelSkeletalClipIndex(const ModelSkeleton& skeleton, const QString& text)
{
	const QString wanted = text.trimmed();
	if (wanted.isEmpty()) {
		return -1;
	}
	bool number = false;
	const int index = wanted.toInt(&number);
	if (number) {
		return index >= 0 && index < skeleton.clips.size() ? index : -1;
	}
	for (int clip = 0; clip < skeleton.clips.size(); ++clip) {
		if (skeleton.clips.at(clip).name == wanted) {
			return clip;
		}
	}
	for (int clip = 0; clip < skeleton.clips.size(); ++clip) {
		if (skeleton.clips.at(clip).name.compare(wanted, Qt::CaseInsensitive) == 0) {
			return clip;
		}
	}
	return -1;
}

int modelSkeletalClipForFrame(const ModelMesh& mesh, int frame)
{
	const QVector<int>& map = mesh.skeleton.bakedClipForFrame;
	if (frame < 0 || frame >= mesh.frames.size() || map.size() != mesh.frames.size()) {
		return -1;
	}
	const int clip = map.at(frame);
	return clip >= 0 && clip < mesh.skeleton.clips.size() ? clip : -1;
}

QStringList modelSkeletalClipSummaries(const ModelSkeleton& skeleton)
{
	QStringList lines;
	for (int index = 0; index < skeleton.clips.size(); ++index) {
		const ModelSkeletalClip& clip = skeleton.clips.at(index);
		const QString rate = clip.framesPerSecond > 0 && std::isfinite(clip.framesPerSecond)
			? QCoreApplication::translate("VibeStudioModelSkeleton", "%1 fps").arg(clip.framesPerSecond, 0, 'g', 6)
			: QCoreApplication::translate("VibeStudioModelSkeleton", "no rate");
		lines << QCoreApplication::translate("VibeStudioModelSkeleton", "%1 %2 (%n frame(s), %3)", nullptr, int(clip.frames.size()))
					 .arg(index)
					 .arg(clip.name, rate);
	}
	return lines;
}

QString modelMd5ClipFileName(const ModelSkeleton& skeleton, int index)
{
	QString stem;
	if (index >= 0 && index < skeleton.clips.size()) {
		for (const QChar character : skeleton.clips.at(index).name.trimmed()) {
			const char ascii = character.toLatin1();
			const bool safe = character.unicode() < 128
				&& ((ascii >= 'A' && ascii <= 'Z') || (ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9') || ascii == '_' || ascii == '-'
					|| ascii == '.');
			stem.append(safe ? character : QLatin1Char('_'));
		}
	}
	while (stem.startsWith(QLatin1Char('.'))) {
		stem.remove(0, 1);
	}
	if (stem.isEmpty()) {
		stem = QStringLiteral("clip%1").arg(index);
	}
	return stem + QStringLiteral(".md5anim");
}

// --- Every clip as an md5anim ----------------------------------------------

bool prepareModelMd5Clips(const ModelMesh& mesh, const QString& directory, const ModelExportOptions& options, QVector<ModelMd5ClipFile>* files,
	QString* error, const ModelWorkControl& control)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (directory.trimmed().isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Choose a folder for the MD5 animations."));
	}
	if (mesh.skeleton.isEmpty() || mesh.skeleton.clips.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "An md5anim needs a skeletal clip; this model animates by frames."));
	}
	QVector<ModelMd5ClipFile> prepared;
	QSet<QString> names;
	QStringList refused;
	const QDir folder(directory);
	for (int clip = 0; clip < mesh.skeleton.clips.size(); ++clip) {
		const QString name = modelMd5ClipFileName(mesh.skeleton, clip);
		if (names.contains(name.toLower())) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton",
				"Two clips would both be written as %1. Rename one in Edit Skeletal Animation so every clip has its own file.")
							.arg(name));
		}
		names.insert(name.toLower());
		ModelExportOptions clipOptions = options;
		clipOptions.clip = clip;
		ModelMd5ClipFile file;
		file.clip = clip;
		file.path = QDir::cleanPath(folder.absoluteFilePath(name));
		QString problem;
		file.bytes = exportEditableModel(mesh, QStringLiteral("md5anim"), clipOptions, &problem, control, &file.report);
		// A strict refusal keeps its report; every other clip is still prepared
		// so the refusal can list what each one would leave out.
		const bool strictRefusal = file.bytes.isEmpty() && options.strict && !file.report.omissions.isEmpty() && file.report.target.ready();
		if (file.bytes.isEmpty() && !strictRefusal) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "Clip %1 \"%2\": %3").arg(clip).arg(mesh.skeleton.clips.at(clip).name, problem));
		}
		if (strictRefusal) {
			refused << QStringLiteral("%1 \"%2\"").arg(clip).arg(mesh.skeleton.clips.at(clip).name);
		}
		prepared.append(std::move(file));
	}
	if (files) {
		*files = std::move(prepared);
	}
	if (!refused.isEmpty()) {
		const int count = int(refused.size());
		return fail(QCoreApplication::translate("VibeStudioModelSkeleton",
			"Strict export refused, so no MD5 animation was written. Clips that would leave out data the editable source holds (%n): %1.",
			nullptr, count)
						.arg(refused.join(QStringLiteral(", "))));
	}
	return true;
}

QStringList modelMd5ClipsSharedNotes(const QVector<ModelMd5ClipFile>& files)
{
	QStringList shared;
	if (files.size() < 2) {
		return shared;
	}
	for (const QString& note : files.first().report.notes) {
		const bool everywhere = std::all_of(files.cbegin() + 1, files.cend(), [&note](const ModelMd5ClipFile& file) { return file.report.notes.contains(note); });
		if (everywhere && !shared.contains(note)) {
			shared << note;
		}
	}
	return shared;
}

bool writeModelMd5Clips(const QVector<ModelMd5ClipFile>& files, bool overwrite, bool dryRun, const QString& protectedSource, QString* error,
	const ModelWorkControl& control)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	QVector<ModelWriteTarget> targets;
	for (const ModelMd5ClipFile& file : files) {
		const ModelWriteTarget target = inspectModelWriteTarget(file.path, control);
		if (!target.isValid()) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "%1: %2").arg(file.path, target.error));
		}
		if (modelPathsReferToSameFile(file.path, protectedSource)) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "%1 is the editable source. Choose another folder.").arg(file.path));
		}
		if (target.existed && !overwrite) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton",
				"%1 already exists, so no MD5 animation was written. Choose an empty folder or allow replacing existing files.")
							.arg(file.path));
		}
		if (file.bytes.size() > modelFileByteLimit) {
			return fail(QCoreApplication::translate("VibeStudioModelSkeleton", "%1 would exceed the 64 MiB output limit.").arg(file.path));
		}
		targets.append(target);
	}
	if (dryRun) {
		for (const ModelWriteTarget& target : std::as_const(targets)) {
			QString problem;
			if (!validateModelWriteTarget(target, &problem, control)) {
				return fail(problem);
			}
		}
		return true;
	}
	QStringList written;
	for (int index = 0; index < files.size(); ++index) {
		QString problem;
		if (!writeModelFile(targets.at(index), files.at(index).bytes, &problem, control)) {
			return fail(written.isEmpty() ? problem
										  : QCoreApplication::translate("VibeStudioModelSkeleton", "%1 Already written: %2.")
												.arg(problem, written.join(QStringLiteral(", "))));
		}
		written << files.at(index).path;
	}
	return true;
}

// --- Frame audit -------------------------------------------------------------

bool auditModelSkeletalFrames(const ModelMesh& mesh, ModelSkeletalFrameAudit* audit, QString* error, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	ModelSkeletalFrameAudit result;
	const ModelSkeleton& skeleton = mesh.skeleton;
	if (skeleton.isEmpty()) {
		if (audit) {
			*audit = result;
		}
		return work.check();
	}
	const QVector<int>& map = skeleton.bakedClipForFrame;
	const bool mapped = map.size() == mesh.frames.size();
	const int bindFrame = modelBindPoseFrame(mesh);
	QVector<int> sampled(skeleton.clips.size(), 0);
	int previous = -1;
	for (int frame = 0; frame < mesh.frames.size(); ++frame) {
		if (!work.step()) {
			return false;
		}
		const int clip = mapped ? map.at(frame) : (frame == bindFrame ? -1 : -2);
		if (clip == -1) {
			continue;
		}
		if (clip < 0 || clip >= skeleton.clips.size() || clip < previous || sampled.at(clip) >= skeleton.clips.at(clip).frames.size()) {
			result.unmappedFrames.append(frame);
			continue;
		}
		previous = clip;
		const QVector<ModelJointMatrix>& pose = skeleton.clips.at(clip).frames.at(sampled[clip]++);
		int differing = 0;
		for (const ModelSurface& surface : mesh.surfaces) {
			if (!work.step()) {
				return false;
			}
			if (frame >= surface.frames.size()) {
				differing += surface.vertexCount;
				continue;
			}
			const bool skinned = !surface.skinning.isEmpty() && surface.skinning.first.size() == surface.vertexCount
				&& surface.skinning.count.size() == surface.vertexCount;
			if (skinned) {
				ModelFrameGeometry expected;
				if (!skinModelSurface(surface, pose, &expected, error)) {
					return false;
				}
				differing += differingVertices(expected, surface.frames.at(frame), surface.vertexCount);
			} else if (!surface.frames.isEmpty()) {
				differing += differingVertices(surface.frames.first(), surface.frames.at(frame), surface.vertexCount);
			}
		}
		if (differing > 0) {
			result.editedFrames.append(frame);
			result.editedFrameClips.append(clip);
			result.editedVertices += differing;
		}
	}
	for (const ModelAnimation& animation : mesh.animations) {
		if (!work.step()) {
			return false;
		}
		const auto found = std::find_if(skeleton.clips.cbegin(), skeleton.clips.cend(),
			[&animation](const ModelSkeletalClip& clip) { return clip.name == animation.name; });
		const auto rate = [](double value) { return value > 0 && std::isfinite(value) ? value : 0.0; };
		if (found == skeleton.clips.cend() || found->frames.size() != animation.frameCount
			|| std::abs(rate(found->framesPerSecond) - rate(animation.framesPerSecond)) > 1e-9) {
			result.changedAnimations.append(animation.name);
		}
	}
	if (!work.check()) {
		return false;
	}
	if (audit) {
		*audit = result;
	}
	return true;
}

} // namespace vibestudio
