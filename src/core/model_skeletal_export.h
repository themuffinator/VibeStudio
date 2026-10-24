#pragma once

// What the skeletal exports (MD5 mesh and animation, IQM) can carry of an
// editable skeletal model, and how callers name the clip they want.
//
// MD5 joints hold a rotation and a translation only. A joint matrix with
// scale, shear or a reflection splits into its rigid part and the stretch
// left over (matrix = rigid * stretch). The md5mesh writer moves each joint's
// bind stretch into its weight offsets, which reproduces the bind pose
// exactly; an md5anim can then follow only poses that keep each influencing
// joint's bind stretch, and other poses are refused.
//
// The frame audit finds baked frames that the skeletal exports cannot write:
// clip samples edited apart from the skeleton, frames added or rearranged by
// frame edits, and frame animations renamed or retimed apart from their clip.

#include "core/model_document.h"
#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// --- Joint rigidity --------------------------------------------------------

// The largest entry difference at which two joint stretches still count as
// equal, so a pose keeps its bind stretch.
inline constexpr double kModelJointStretchTolerance = 1e-4;
// The largest entry difference from the identity at which a stretch counts as
// no stretch at all, so the writers leave the joint untouched.
inline constexpr double kModelJointRigidTolerance = 1e-5;

// The rotation (as modelJointRotation extracts it) and the translation.
[[nodiscard]] ModelJointMatrix modelJointRigidPart(const ModelJointMatrix& matrix);
// The 3x3 part left once the rigid part is removed, with no translation:
// modelJointMultiply(modelJointRigidPart(m), modelJointStretch(m)) == m.
[[nodiscard]] ModelJointMatrix modelJointStretch(const ModelJointMatrix& matrix);
// True when the stretch is the identity within `tolerance`: an orthonormal
// 3x3 part with a positive determinant.
[[nodiscard]] bool modelJointIsRigid(const ModelJointMatrix& matrix, double tolerance = kModelJointRigidTolerance);

// --- Skeletal clips ----------------------------------------------------------

// The clip `text` names: a zero-based index, else a clip name (exact first,
// then ignoring case). -1 when it names none.
[[nodiscard]] int modelSkeletalClipIndex(const ModelSkeleton& skeleton, const QString& text);
// The clip baked frame `frame` is a sample of; -1 for the bind pose, a frame
// added or rearranged by frame edits, or a frame that does not exist.
[[nodiscard]] int modelSkeletalClipForFrame(const ModelMesh& mesh, int frame);
// "index name (N frames, R fps)" per clip, for refusals and listings.
[[nodiscard]] QStringList modelSkeletalClipSummaries(const ModelSkeleton& skeleton);
// The file name clip `index` is written to: its name with every character
// other than ASCII letters, digits, '_', '-' and '.' replaced by '_', else
// "clip<index>", then ".md5anim".
[[nodiscard]] QString modelMd5ClipFileName(const ModelSkeleton& skeleton, int index);

// --- Every clip as an md5anim ----------------------------------------------

struct ModelMd5ClipFile {
	int clip = -1;
	// The destination: the directory joined with modelMd5ClipFileName.
	QString path;
	QByteArray bytes;
	ModelExportReport report;
};

// Every skeletal clip as md5anim bytes through exportEditableModel with
// `options` (its clip and frame are ignored). Prepares nothing when a clip is
// refused or two clips would share a file name, ignoring case. A strict
// refusal is the exception: every clip is still prepared, `files` holds each
// clip's report (refused clips without bytes), and the call fails naming the
// refused clips.
bool prepareModelMd5Clips(const ModelMesh& mesh, const QString& directory, const ModelExportOptions& options, QVector<ModelMd5ClipFile>* files,
	QString* error = nullptr, const ModelWorkControl& control = {});
// The notes every file of a batch of two or more shares, such as frames that
// belong to no clip, in the first file's order, so a summary lists them once.
[[nodiscard]] QStringList modelMd5ClipsSharedNotes(const QVector<ModelMd5ClipFile>& files);
// Reviews every destination first and writes nothing when one exists without
// `overwrite`, is a link or folder, or is `protectedSource`; a dry run stops
// after the review. A write that fails part way says which files were written.
bool writeModelMd5Clips(const QVector<ModelMd5ClipFile>& files, bool overwrite, bool dryRun, const QString& protectedSource,
	QString* error = nullptr, const ModelWorkControl& control = {});

// --- Frame audit -------------------------------------------------------------

struct ModelSkeletalFrameAudit {
	// Clip samples whose geometry differs from the skeleton's pose, with the
	// clip each belongs to and how many vertices differ in total.
	QVector<int> editedFrames;
	QVector<int> editedFrameClips;
	int editedVertices = 0;
	// Frames that are neither the bind pose nor a clip sample.
	QVector<int> unmappedFrames;
	// Frame animations whose name, length or rate no longer match a clip.
	QStringList changedAnimations;
	[[nodiscard]] bool isEmpty() const { return editedFrames.isEmpty() && unmappedFrames.isEmpty() && changedAnimations.isEmpty(); }
};

// Compares every baked frame with what the skeleton poses there (skinned
// surfaces) or with the first frame (surfaces without influences), within
// 1e-4 units and about half a degree of normal. False only on cancellation or
// a skeleton that cannot be posed.
bool auditModelSkeletalFrames(const ModelMesh& mesh, ModelSkeletalFrameAudit* audit, QString* error = nullptr,
	const ModelWorkControl& control = {});

} // namespace vibestudio
