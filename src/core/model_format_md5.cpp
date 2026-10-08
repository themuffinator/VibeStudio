// Doom 3-family MD5 models: Doom 3, Quake 4, Prey and Enemy Territory: Quake
// Wars. Decodes MD5Version 10 md5mesh and md5anim text behind decodeModelMesh,
// writes both back (core/model_md5.h), and reads the `model` declarations of
// Doom 3 .def files so a mesh picks up the animation names its game uses.
//
// The layouts follow the released Doom 3 GPL source (id Software, GPLv3,
// https://github.com/id-Software/DOOM-3, November 2011 release):
// - md5mesh: idRenderModelMD5::LoadModel, idRenderModelMD5::ParseJoint and
//   idMD5Mesh::ParseMesh in neo/renderer/Model_md5.cpp;
// - vertex positions: idSIMD_Generic::TransformVerts and the idJointMat layout
//   (neo/idlib/math/Simd_Generic.cpp, neo/idlib/math/JointTransform.h);
// - md5anim: idMD5Anim::LoadAnim and GetInterpolatedFrame in
//   neo/game/anim/Anim.cpp, with idSIMD_Generic::TransformJoints for the
//   parent-relative to model-space concatenation;
// - model declarations: idDeclModelDef::Parse and ParseAnim in
//   neo/game/anim/Anim_Blend.cpp, and idDeclFile::LoadAndParse in
//   neo/framework/DeclManager.cpp for the "type name { ... }" framing (def/
//   files default to entityDef, Game_local.cpp).
// This is an independent implementation; no upstream code is copied.
//
// Winding: Doom 3 derives each triangle's plane as cross(c - a, b - a)
// (idSIMD_Generic::DeriveTriPlanes, called by R_DeriveFacePlanes in
// neo/renderer/tr_trisurf.cpp), and GL_Cull (neo/renderer/tr_backend.cpp)
// culls GL_FRONT for ordinary front-sided materials, so the file stores
// clockwise front faces, like MD3. The decoder swaps b and c on the way in so
// cross(b - a, c - a) points out of the model, and the writers swap them back.
//
// Rotations: joints store the x, y and z of a unit quaternion. Doom 3 rebuilds
// w >= 0 (idCQuat::ToQuat, idVec3::ToQuat via CalcW) and applies the transpose
// of idQuat::ToMat3 (idJointMat::SetRotation), which is the conjugate rotation;
// modelQuatFromXyzNegativeW / modelQuatToXyzNegativeW in core/model_skeleton.h
// encode exactly that, so joint matrices here equal Doom 3's idJointMat.

#include "core/model_formats_p.h"
#include "core/model_md5.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QHash>
#include <QPair>
#include <QSet>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <system_error>
#include <utility>
#include <vector>

namespace vibestudio {

namespace {

using model_formats::kMaxFrames;
using model_formats::kMaxInfluencesPerVertex;
using model_formats::kMaxJoints;
using model_formats::kMaxSurfaces;
using model_formats::kMaxSurfaceTriangles;
using model_formats::kMaxSurfaceVertices;
using model_formats::kMaxTextBytes;
using model_formats::kMaxVertexSlots;

constexpr int kMd5Version = 10;
// Joint poses (frames times joints) one md5anim may hold, and all the clips of
// one mesh together. Real Doom 3 clips hold tens of thousands.
constexpr qint64 kMaxAnimJointPoses = 2LL * 1024LL * 1024LL;
constexpr qint64 kMaxTotalJointPoses = 4LL * 1024LL * 1024LL;
constexpr int kMaxFrameRate = 100000;
// How far `inherit` chains are followed between model declarations.
constexpr int kMaxDeclInheritDepth = 32;
// Anim component bits (ANIM_TX .. ANIM_QZ, neo/game/anim/Anim.h).
constexpr quint32 kAnimTx = 1;
constexpr quint32 kAnimTy = 2;
constexpr quint32 kAnimTz = 4;
constexpr quint32 kAnimQx = 8;
constexpr quint32 kAnimQy = 16;
constexpr quint32 kAnimQz = 32;
constexpr quint32 kAnimAllBits = 63;

ModelVec3 vecAdd(const ModelVec3& a, const ModelVec3& b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

ModelVec3 vecSubtract(const ModelVec3& a, const ModelVec3& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

ModelVec3 vecCross(const ModelVec3& a, const ModelVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

ModelVec3 vecMin(const ModelVec3& a, const ModelVec3& b)
{
	return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

ModelVec3 vecMax(const ModelVec3& a, const ModelVec3& b)
{
	return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

int countBits(quint32 value)
{
	int count = 0;
	for (; value != 0; value &= value - 1) {
		++count;
	}
	return count;
}

// ---------------------------------------------------------------------------
// Tokens
//
// The idLexer subset MD5 files and declarations use: whitespace is every byte
// up to the space, // and /* */ comments, quoted strings without escapes
// (LEXFL_NOSTRINGESCAPECHARS), single-character ( ) { } , punctuation, and
// everything else as words (names, paths and numbers).
// ---------------------------------------------------------------------------

struct Md5Token {
	enum class Kind { End, Word, String, Punct };
	Kind kind = Kind::End;
	const char* data = nullptr;
	qsizetype length = 0;
	int line = 1;

	[[nodiscard]] bool isEnd() const { return kind == Kind::End; }
	[[nodiscard]] bool is(const char* literal) const
	{
		if (kind == Kind::End || kind == Kind::String) {
			return false;
		}
		const size_t size = std::strlen(literal);
		return size == size_t(length) && std::memcmp(data, literal, size) == 0;
	}
	[[nodiscard]] bool isPunct(char ch) const { return kind == Kind::Punct && length == 1 && data[0] == ch; }
	[[nodiscard]] bool isValue() const { return kind == Kind::Word || kind == Kind::String; }
	[[nodiscard]] QString latin1() const { return QString::fromLatin1(data, length); }
	[[nodiscard]] QString utf8() const { return QString::fromUtf8(data, length); }
	// The token as quoted in a message, cut short.
	[[nodiscard]] QString shown() const { return QString::fromLatin1(data, std::min<qsizetype>(length, 40)); }
};

class Md5Lexer {
public:
	enum class Problem { None, OpenString, OpenComment };

	explicit Md5Lexer(const QByteArray& bytes)
		: m_data(bytes.constData())
		, m_size(bytes.size())
	{
		// A UTF-8 byte order mark is not part of the first token.
		if (m_size >= 3 && uchar(m_data[0]) == 0xEF && uchar(m_data[1]) == 0xBB && uchar(m_data[2]) == 0xBF) {
			m_pos = 3;
		}
	}

	Md5Token next()
	{
		if (m_peeked) {
			m_peeked = false;
			return m_peek;
		}
		return scan();
	}

	const Md5Token& peek()
	{
		if (!m_peeked) {
			m_peek = scan();
			m_peeked = true;
		}
		return m_peek;
	}

	// Puts back the token next() just returned.
	void unread(const Md5Token& token)
	{
		m_peek = token;
		m_peeked = true;
	}

	[[nodiscard]] Problem problem() const { return m_problem; }
	[[nodiscard]] int problemLine() const { return m_problemLine; }
	void clearProblem() { m_problem = Problem::None; }

private:
	static bool isPunct(char ch) { return ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == ','; }

	void setProblem(Problem problem, int line)
	{
		if (m_problem == Problem::None) {
			m_problem = problem;
			m_problemLine = line;
		}
	}

	Md5Token scan()
	{
		for (;;) {
			while (m_pos < m_size && uchar(m_data[m_pos]) <= ' ') {
				if (m_data[m_pos] == '\n') {
					++m_line;
				}
				++m_pos;
			}
			if (m_pos + 1 < m_size && m_data[m_pos] == '/' && m_data[m_pos + 1] == '/') {
				while (m_pos < m_size && m_data[m_pos] != '\n') {
					++m_pos;
				}
				continue;
			}
			if (m_pos + 1 < m_size && m_data[m_pos] == '/' && m_data[m_pos + 1] == '*') {
				const int startLine = m_line;
				m_pos += 2;
				bool closed = false;
				while (m_pos < m_size) {
					if (m_data[m_pos] == '*' && m_pos + 1 < m_size && m_data[m_pos + 1] == '/') {
						m_pos += 2;
						closed = true;
						break;
					}
					if (m_data[m_pos] == '\n') {
						++m_line;
					}
					++m_pos;
				}
				if (!closed) {
					setProblem(Problem::OpenComment, startLine);
				}
				continue;
			}
			break;
		}
		Md5Token token;
		token.line = m_line;
		if (m_pos >= m_size) {
			return token;
		}
		const char first = m_data[m_pos];
		if (first == '"') {
			const qsizetype start = ++m_pos;
			while (m_pos < m_size && m_data[m_pos] != '"' && m_data[m_pos] != '\n') {
				++m_pos;
			}
			token.kind = Md5Token::Kind::String;
			token.data = m_data + start;
			token.length = m_pos - start;
			if (m_pos < m_size && m_data[m_pos] == '"') {
				++m_pos;
			} else {
				// idLexer refuses a newline inside a string.
				setProblem(Problem::OpenString, token.line);
			}
			return token;
		}
		if (isPunct(first)) {
			token.kind = Md5Token::Kind::Punct;
			token.data = m_data + m_pos;
			token.length = 1;
			++m_pos;
			return token;
		}
		const qsizetype start = m_pos;
		while (m_pos < m_size) {
			const char ch = m_data[m_pos];
			if (uchar(ch) <= ' ' || ch == '"' || isPunct(ch)) {
				break;
			}
			if (ch == '/' && m_pos + 1 < m_size && (m_data[m_pos + 1] == '/' || m_data[m_pos + 1] == '*')) {
				break;
			}
			++m_pos;
		}
		token.kind = Md5Token::Kind::Word;
		token.data = m_data + start;
		token.length = m_pos - start;
		return token;
	}

	const char* m_data = nullptr;
	qsizetype m_size = 0;
	qsizetype m_pos = 0;
	int m_line = 1;
	Md5Token m_peek;
	bool m_peeked = false;
	Problem m_problem = Problem::None;
	int m_problemLine = 0;
};

// A whole number, as idLexer::ParseInt reads it (no fractions).
bool parseIntegerToken(const Md5Token& token, qint64* value)
{
	if (token.kind != Md5Token::Kind::Word || token.length <= 0) {
		return false;
	}
	const char* begin = token.data;
	const char* end = token.data + token.length;
	if (*begin == '+') {
		++begin;
	}
	if (begin == end) {
		return false;
	}
	long long parsed = 0;
	const auto result = std::from_chars(begin, end, parsed);
	if (result.ec != std::errc() || result.ptr != end) {
		return false;
	}
	*value = qint64(parsed);
	return true;
}

enum class NumberResult { Ok, NotNumber, NotFinite };

// A number in the C locale; NaN, infinities and values beyond float are refused.
NumberResult parseNumberToken(const Md5Token& token, float* value)
{
	if (token.kind != Md5Token::Kind::Word || token.length <= 0) {
		return NumberResult::NotNumber;
	}
	const char* begin = token.data;
	const char* end = token.data + token.length;
	if (*begin == '+') {
		++begin;
	}
	if (begin == end) {
		return NumberResult::NotNumber;
	}
	double parsed = 0.0;
	const auto result = std::from_chars(begin, end, parsed, std::chars_format::general);
	if (result.ec == std::errc::result_out_of_range && result.ptr == end) {
		return NumberResult::NotFinite;
	}
	if (result.ec != std::errc() || result.ptr != end) {
		return NumberResult::NotNumber;
	}
	if (!std::isfinite(parsed) || std::abs(parsed) > double(std::numeric_limits<float>::max())) {
		return NumberResult::NotFinite;
	}
	*value = float(parsed);
	return NumberResult::Ok;
}

// Reads MD5 text strictly, the way idRenderModelMD5 and idMD5Anim do: every
// keyword is expected in order and the first problem becomes the error.
class Md5Reader {
public:
	Md5Reader(const QByteArray& bytes, QString* error, const ModelWorkControl& control)
		: m_lexer(bytes)
		, m_error(error)
		, m_work(control, ModelWorkPhase::Validating, error)
	{
	}

	bool step() { return m_work.step(); }
	bool check() { return m_work.check(); }

	bool fail(const QString& message)
	{
		if (m_error && m_error->isEmpty()) {
			*m_error = message;
		}
		return false;
	}

	bool expect(const char* keyword)
	{
		const Md5Token token = m_lexer.next();
		if (!lexOk()) {
			return false;
		}
		if (token.is(keyword)) {
			return true;
		}
		if (token.isEnd()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file ends on line %1 where \"%2\" belongs.")
				.arg(QString::number(token.line), QString::fromLatin1(keyword)));
		}
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file has \"%1\" on line %2 where \"%3\" belongs.")
			.arg(token.shown(), QString::number(token.line), QString::fromLatin1(keyword)));
	}

	// True when the next token is `keyword`; it is consumed only when it is.
	bool accept(const char* keyword)
	{
		if (!m_lexer.peek().is(keyword)) {
			return false;
		}
		m_lexer.next();
		return true;
	}

	bool readInteger(qint64* value)
	{
		const Md5Token token = m_lexer.next();
		if (!lexOk()) {
			return false;
		}
		if (parseIntegerToken(token, value)) {
			return true;
		}
		if (token.isEnd()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file ends on line %1 where a whole number belongs.")
				.arg(token.line));
		}
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file has \"%1\" on line %2 where a whole number belongs.")
			.arg(token.shown(), QString::number(token.line)));
	}

	bool readNumber(float* value)
	{
		const Md5Token token = m_lexer.next();
		if (!lexOk()) {
			return false;
		}
		switch (parseNumberToken(token, value)) {
		case NumberResult::Ok:
			return true;
		case NumberResult::NotFinite:
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file has a number that is not finite (\"%1\") on line %2.")
				.arg(token.shown(), QString::number(token.line)));
		case NumberResult::NotNumber:
			break;
		}
		if (token.isEnd()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file ends on line %1 where a number belongs.").arg(token.line));
		}
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file has \"%1\" on line %2 where a number belongs.")
			.arg(token.shown(), QString::number(token.line)));
	}

	// A quoted string or a bare word (idLexer::ReadToken).
	bool readName(QString* value)
	{
		const Md5Token token = m_lexer.next();
		if (!lexOk()) {
			return false;
		}
		if (token.isValue()) {
			*value = token.latin1();
			return true;
		}
		if (token.isEnd()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file ends on line %1 where a name belongs.").arg(token.line));
		}
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file has \"%1\" on line %2 where a name belongs.")
			.arg(token.shown(), QString::number(token.line)));
	}

	// "( a b c )", as idLexer::Parse1DMatrix reads it.
	bool readVector(float* values, int count)
	{
		if (!expect("(")) {
			return false;
		}
		for (int index = 0; index < count; ++index) {
			if (!readNumber(values + index)) {
				return false;
			}
		}
		return expect(")");
	}

	bool readVec3(ModelVec3* value)
	{
		float values[3] = {0.0f, 0.0f, 0.0f};
		if (!readVector(values, 3)) {
			return false;
		}
		*value = {values[0], values[1], values[2]};
		return true;
	}

private:
	bool lexOk()
	{
		switch (m_lexer.problem()) {
		case Md5Lexer::Problem::None:
			return true;
		case Md5Lexer::Problem::OpenString:
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file has a quoted string on line %1 that is not closed.")
				.arg(m_lexer.problemLine()));
		case Md5Lexer::Problem::OpenComment:
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file has a comment opened on line %1 that is not closed.")
				.arg(m_lexer.problemLine()));
		}
		return false;
	}

	Md5Lexer m_lexer;
	QString* m_error = nullptr;
	ModelWorkProgress m_work;
};

// ---------------------------------------------------------------------------
// md5mesh
// ---------------------------------------------------------------------------

struct Md5JointRecord {
	QString name;
	int parent = -1;
	ModelVec3 position;
	// The three quaternion components the file stores.
	ModelVec3 rotation;
};

struct Md5WeightRecord {
	int joint = 0;
	float bias = 0.0f;
	// The weight's position in its joint's space.
	ModelVec3 position;
};

struct Md5MeshRecord {
	QString name;
	QString shader;
	QVector<ModelTexCoord> texCoords;
	QVector<int> firstWeight;
	QVector<int> weightCount;
	// File (clockwise) order.
	QVector<ModelTriangle> triangles;
	QVector<Md5WeightRecord> weights;
	int droppedTriangles = 0;
};

struct Md5MeshFile {
	QString commandLine;
	QVector<Md5JointRecord> joints;
	QVector<Md5MeshRecord> meshes;
};

// idRenderModelMD5::LoadModel and idMD5Mesh::ParseMesh.
bool parseMd5MeshText(const QByteArray& bytes, Md5MeshFile* out, QString* error, const ModelWorkControl& control)
{
	Md5Reader reader(bytes, error, control);
	if (!reader.check()) {
		return false;
	}
	qint64 version = 0;
	if (!reader.expect("MD5Version") || !reader.readInteger(&version)) {
		return false;
	}
	if (version != kMd5Version) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MD5 version %1; only MD5Version 10 is decoded.").arg(version));
	}
	if (!reader.expect("commandline") || !reader.readName(&out->commandLine)) {
		return false;
	}
	qint64 jointCount = 0;
	if (!reader.expect("numJoints") || !reader.readInteger(&jointCount)) {
		return false;
	}
	if (jointCount < 1 || jointCount > kMaxJoints) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 mesh declares %1 joint(s); between 1 and %2 are supported.")
			.arg(jointCount).arg(kMaxJoints));
	}
	qint64 meshCount = 0;
	if (!reader.expect("numMeshes") || !reader.readInteger(&meshCount)) {
		return false;
	}
	if (meshCount < 0 || meshCount > kMaxSurfaces) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 mesh declares %1 mesh(es); up to %2 are supported.")
			.arg(meshCount).arg(kMaxSurfaces));
	}

	if (!reader.expect("joints") || !reader.expect("{")) {
		return false;
	}
	for (int index = 0; index < int(jointCount); ++index) {
		if (!reader.step()) {
			return false;
		}
		Md5JointRecord joint;
		qint64 parent = 0;
		if (!reader.readName(&joint.name) || !reader.readInteger(&parent)) {
			return false;
		}
		// Doom 3 computes each joint's parent-relative pose from its parent's,
		// so parents must come first; any negative index is a root.
		if (parent >= index) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Joint %1 (%2) names parent %3, which does not come before it.")
				.arg(QString::number(index), joint.name, QString::number(parent)));
		}
		joint.parent = parent < 0 ? -1 : int(parent);
		if (!reader.readVec3(&joint.position) || !reader.readVec3(&joint.rotation)) {
			return false;
		}
		out->joints.append(joint);
	}
	if (!reader.expect("}")) {
		return false;
	}

	// Vertices and their (per-vertex) influences across every mesh.
	qint64 totalVertices = 0;
	qint64 influenceTotal = 0;
	for (int meshIndex = 0; meshIndex < int(meshCount); ++meshIndex) {
		if (!reader.step()) {
			return false;
		}
		Md5MeshRecord record;
		if (!reader.expect("mesh") || !reader.expect("{")) {
			return false;
		}
		if (reader.accept("name") && !reader.readName(&record.name)) {
			return false;
		}
		if (!reader.expect("shader") || !reader.readName(&record.shader)) {
			return false;
		}

		qint64 vertexCount = 0;
		if (!reader.expect("numverts") || !reader.readInteger(&vertexCount)) {
			return false;
		}
		if (vertexCount < 0 || vertexCount > kMaxSurfaceVertices) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Mesh %1 declares %2 vertices; up to %3 are supported.")
				.arg(meshIndex).arg(vertexCount).arg(kMaxSurfaceVertices));
		}
		if (vertexCount > kMaxVertexSlots - totalVertices) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 mesh holds more vertices than can be decoded."));
		}
		totalVertices += vertexCount;
		for (int vertex = 0; vertex < int(vertexCount); ++vertex) {
			if (!reader.step()) {
				return false;
			}
			// The vertex number is read and ignored, as Doom 3 does.
			qint64 ignored = 0;
			float st[2] = {0.0f, 0.0f};
			qint64 first = 0;
			qint64 count = 0;
			if (!reader.expect("vert") || !reader.readInteger(&ignored) || !reader.readVector(st, 2) || !reader.readInteger(&first)
				|| !reader.readInteger(&count)) {
				return false;
			}
			if (count < 1 || count > kMaxInfluencesPerVertex) {
				return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of mesh %2 has %3 weight(s); between 1 and %4 are supported.")
					.arg(vertex).arg(meshIndex).arg(count).arg(kMaxInfluencesPerVertex));
			}
			if (first < 0 || first > qint64(std::numeric_limits<int>::max()) - count) {
				return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of mesh %2 starts at weight %3, which is out of range.")
					.arg(vertex).arg(meshIndex).arg(first));
			}
			influenceTotal += count;
			if (influenceTotal > kMaxVertexSlots) {
				return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 mesh holds more joint influences than can be decoded."));
			}
			// MD5 STs are in image space with t = 0 on the top row, as MD3's are.
			record.texCoords.append({st[0], st[1]});
			record.firstWeight.append(int(first));
			record.weightCount.append(int(count));
		}

		qint64 triangleCount = 0;
		if (!reader.expect("numtris") || !reader.readInteger(&triangleCount)) {
			return false;
		}
		if (triangleCount < 0 || triangleCount > kMaxSurfaceTriangles) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Mesh %1 declares %2 triangles; up to %3 are supported.")
				.arg(meshIndex).arg(triangleCount).arg(kMaxSurfaceTriangles));
		}
		for (int triangle = 0; triangle < int(triangleCount); ++triangle) {
			if (!reader.step()) {
				return false;
			}
			qint64 ignored = 0;
			qint64 corners[3] = {0, 0, 0};
			if (!reader.expect("tri") || !reader.readInteger(&ignored) || !reader.readInteger(corners) || !reader.readInteger(corners + 1)
				|| !reader.readInteger(corners + 2)) {
				return false;
			}
			bool inRange = true;
			for (const qint64 corner : corners) {
				inRange = inRange && corner >= 0 && corner < vertexCount;
			}
			if (!inRange) {
				++record.droppedTriangles;
				continue;
			}
			record.triangles.append({int(corners[0]), int(corners[1]), int(corners[2])});
		}

		qint64 weightCount = 0;
		if (!reader.expect("numweights") || !reader.readInteger(&weightCount)) {
			return false;
		}
		if (weightCount < 0 || weightCount > kMaxVertexSlots) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Mesh %1 declares %2 weights; up to %3 are supported.")
				.arg(meshIndex).arg(weightCount).arg(kMaxVertexSlots));
		}
		for (int weight = 0; weight < int(weightCount); ++weight) {
			if (!reader.step()) {
				return false;
			}
			qint64 ignored = 0;
			qint64 joint = 0;
			Md5WeightRecord parsed;
			if (!reader.expect("weight") || !reader.readInteger(&ignored) || !reader.readInteger(&joint)) {
				return false;
			}
			if (joint < 0 || joint >= jointCount) {
				return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Weight %1 of mesh %2 follows joint %3, which does not exist.")
					.arg(weight).arg(meshIndex).arg(joint));
			}
			parsed.joint = int(joint);
			if (!reader.readNumber(&parsed.bias) || !reader.readVec3(&parsed.position)) {
				return false;
			}
			record.weights.append(parsed);
		}
		if (!reader.expect("}")) {
			return false;
		}
		for (int vertex = 0; vertex < record.firstWeight.size(); ++vertex) {
			const qint64 first = record.firstWeight.at(vertex);
			const qint64 count = record.weightCount.at(vertex);
			if (first + count > weightCount) {
				return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of mesh %2 uses weights %3 to %4, but the mesh holds %5.")
					.arg(vertex).arg(meshIndex).arg(first).arg(first + count - 1).arg(weightCount));
			}
		}
		out->meshes.append(std::move(record));
	}
	// Doom 3 stops reading after the last mesh; anything after it is ignored.
	return true;
}

// ---------------------------------------------------------------------------
// md5anim
// ---------------------------------------------------------------------------

struct Md5AnimJointRecord {
	QString name;
	int parent = -1;
	quint32 flags = 0;
	int firstComponent = 0;
};

struct Md5AnimFile {
	QString commandLine;
	int frameRate = 0;
	int animatedComponents = 0;
	QVector<Md5AnimJointRecord> joints;
	// The baseframe concatenated into model space.
	QVector<ModelJointMatrix> baseModelSpace;
	QVector<ModelVec3> frameMins;
	QVector<ModelVec3> frameMaxs;
	// One model-space matrix per joint per frame.
	QVector<QVector<ModelJointMatrix>> frames;
	QStringList warnings;
};

// idMD5Anim::LoadAnim, with each frame built like GetInterpolatedFrame: the
// baseframe with the flagged components replaced, then concatenated to model
// space parent first.
bool parseMd5AnimText(const QByteArray& bytes, Md5AnimFile* out, QString* error, const ModelWorkControl& control)
{
	if (qint64(bytes.size()) > kMaxTextBytes) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file is larger than the %1 MiB a text model may hold.")
				.arg(kMaxTextBytes / (1024 * 1024));
		}
		return false;
	}
	Md5Reader reader(bytes, error, control);
	if (!reader.check()) {
		return false;
	}
	qint64 version = 0;
	if (!reader.expect("MD5Version") || !reader.readInteger(&version)) {
		return false;
	}
	if (version != kMd5Version) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MD5 version %1; only MD5Version 10 is decoded.").arg(version));
	}
	if (!reader.expect("commandline") || !reader.readName(&out->commandLine)) {
		return false;
	}
	qint64 frameCount = 0;
	if (!reader.expect("numFrames") || !reader.readInteger(&frameCount)) {
		return false;
	}
	if (frameCount < 1 || frameCount > kMaxFrames) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 animation declares %1 frame(s); between 1 and %2 are supported.")
			.arg(frameCount).arg(kMaxFrames));
	}
	qint64 jointCount = 0;
	if (!reader.expect("numJoints") || !reader.readInteger(&jointCount)) {
		return false;
	}
	if (jointCount < 1 || jointCount > kMaxJoints) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 animation declares %1 joint(s); between 1 and %2 are supported.")
			.arg(jointCount).arg(kMaxJoints));
	}
	qint64 frameRate = 0;
	if (!reader.expect("frameRate") || !reader.readInteger(&frameRate)) {
		return false;
	}
	if (frameRate < 0 || frameRate > kMaxFrameRate) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 animation declares frame rate %1, which is outside the supported range.")
			.arg(frameRate));
	}
	qint64 componentCount = 0;
	if (!reader.expect("numAnimatedComponents") || !reader.readInteger(&componentCount)) {
		return false;
	}
	if (componentCount < 0 || componentCount > jointCount * 6) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh",
			"The MD5 animation declares %1 animated component(s) for %2 joint(s); at most six per joint are allowed.")
			.arg(componentCount).arg(jointCount));
	}
	if (frameCount * jointCount > kMaxAnimJointPoses) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 animation holds more joint poses than can be decoded."));
	}
	out->frameRate = int(frameRate);
	out->animatedComponents = int(componentCount);

	QVector<int> parents;
	if (!reader.expect("hierarchy") || !reader.expect("{")) {
		return false;
	}
	for (int index = 0; index < int(jointCount); ++index) {
		if (!reader.step()) {
			return false;
		}
		Md5AnimJointRecord joint;
		qint64 parent = 0;
		qint64 flags = 0;
		qint64 first = 0;
		if (!reader.readName(&joint.name) || !reader.readInteger(&parent) || !reader.readInteger(&flags) || !reader.readInteger(&first)) {
			return false;
		}
		if (parent >= index) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Joint %1 (%2) names parent %3, which does not come before it.")
				.arg(QString::number(index), joint.name, QString::number(parent)));
		}
		joint.parent = parent < 0 ? -1 : int(parent);
		if (index > 0 && joint.parent < 0) {
			out->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Joint %1 (%2) is a second root; Doom 3 accepts only one root joint in an animation.")
				.arg(QString::number(index), joint.name);
		}
		if (flags < 0 || (quint64(flags) & ~quint64(kAnimAllBits)) != 0) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh",
				"Joint %1 (%2) sets animation flags %3; only the six component bits (0 to 63) are defined.")
				.arg(QString::number(index), joint.name, QString::number(flags)));
		}
		joint.flags = quint32(flags);
		// Doom 3 also range-checks the first component of joints that animate
		// nothing; those never read it, so only animated joints are held to it.
		const int bits = countBits(joint.flags);
		if (bits > 0 && (first < 0 || first + bits > componentCount)) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Joint %1 (%2) reads components %3 to %4, but each frame holds %5.")
				.arg(QString::number(index), joint.name, QString::number(first), QString::number(first + bits - 1), QString::number(componentCount)));
		}
		joint.firstComponent = bits > 0 ? int(first) : 0;
		out->joints.append(joint);
		parents.append(joint.parent);
	}
	if (!reader.expect("}")) {
		return false;
	}

	if (!reader.expect("bounds") || !reader.expect("{")) {
		return false;
	}
	for (int frame = 0; frame < int(frameCount); ++frame) {
		if (!reader.step()) {
			return false;
		}
		ModelVec3 mins;
		ModelVec3 maxs;
		if (!reader.readVec3(&mins) || !reader.readVec3(&maxs)) {
			return false;
		}
		out->frameMins.append(mins);
		out->frameMaxs.append(maxs);
	}
	if (!reader.expect("}")) {
		return false;
	}

	QVector<ModelVec3> basePositions;
	QVector<ModelVec3> baseRotations;
	QVector<ModelJointMatrix> baseLocal;
	if (!reader.expect("baseframe") || !reader.expect("{")) {
		return false;
	}
	for (int index = 0; index < int(jointCount); ++index) {
		if (!reader.step()) {
			return false;
		}
		ModelVec3 position;
		ModelVec3 rotation;
		if (!reader.readVec3(&position) || !reader.readVec3(&rotation)) {
			return false;
		}
		basePositions.append(position);
		baseRotations.append(rotation);
		baseLocal.append(modelJointMatrix(modelQuatFromXyzNegativeW(rotation.x, rotation.y, rotation.z), position));
	}
	if (!reader.expect("}")) {
		return false;
	}
	out->baseModelSpace = modelJointsToModelSpace(parents, baseLocal);
	if (out->baseModelSpace.size() != int(jointCount)) {
		return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "The MD5 animation's joint hierarchy is not in parent-first order."));
	}

	QVector<float> components(int(componentCount), 0.0f);
	for (int frame = 0; frame < int(frameCount); ++frame) {
		if (!reader.step()) {
			return false;
		}
		qint64 number = 0;
		if (!reader.expect("frame") || !reader.readInteger(&number)) {
			return false;
		}
		if (number != frame) {
			return reader.fail(QCoreApplication::translate("VibeStudioModelMesh", "Frame %1 of the MD5 animation is numbered %2.").arg(frame).arg(number));
		}
		if (!reader.expect("{")) {
			return false;
		}
		for (int component = 0; component < components.size(); ++component) {
			if (!reader.step() || !reader.readNumber(&components[component])) {
				return false;
			}
		}
		if (!reader.expect("}")) {
			return false;
		}
		QVector<ModelJointMatrix> local = baseLocal;
		for (int index = 0; index < out->joints.size(); ++index) {
			const Md5AnimJointRecord& joint = out->joints.at(index);
			if (joint.flags == 0) {
				continue;
			}
			ModelVec3 position = basePositions.at(index);
			ModelVec3 rotation = baseRotations.at(index);
			int next = joint.firstComponent;
			if (joint.flags & kAnimTx) { position.x = components.at(next++); }
			if (joint.flags & kAnimTy) { position.y = components.at(next++); }
			if (joint.flags & kAnimTz) { position.z = components.at(next++); }
			if (joint.flags & kAnimQx) { rotation.x = components.at(next++); }
			if (joint.flags & kAnimQy) { rotation.y = components.at(next++); }
			if (joint.flags & kAnimQz) { rotation.z = components.at(next++); }
			local[index] = modelJointMatrix(modelQuatFromXyzNegativeW(rotation.x, rotation.y, rotation.z), position);
		}
		out->frames.append(modelJointsToModelSpace(parents, local));
	}
	return true;
}

// Doom 3 refuses an animation whose joint count, names or parents differ from
// the model's (idMD5Anim::CheckModelHierarchy); names compare exactly.
bool animMatchesSkeleton(const Md5AnimFile& anim, const QVector<ModelJoint>& joints)
{
	if (anim.joints.size() != joints.size()) {
		return false;
	}
	for (int index = 0; index < joints.size(); ++index) {
		if (anim.joints.at(index).name != joints.at(index).name || anim.joints.at(index).parent != joints.at(index).parent) {
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Paths and companions
// ---------------------------------------------------------------------------

QString normalizedGamePath(const QString& path)
{
	QString normalized = path;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	while (normalized.contains(QStringLiteral("//"))) {
		normalized.replace(QStringLiteral("//"), QStringLiteral("/"));
	}
	while (normalized.startsWith(QStringLiteral("./"))) {
		normalized.remove(0, 2);
	}
	return normalized.toLower();
}

// True when `path` (a full or virtual path) is the game-relative `relative`.
bool gamePathMatches(const QString& path, const QString& relative)
{
	const QString full = normalizedGamePath(path);
	QString tail = normalizedGamePath(relative);
	while (tail.startsWith(QLatin1Char('/'))) {
		tail.remove(0, 1);
	}
	if (full.isEmpty() || tail.isEmpty()) {
		return false;
	}
	return full == tail || full.endsWith(QLatin1Char('/') + tail);
}

// "<game folder>/def" for a model under ".../models/...", or empty.
QString gameDefinitionFolder(const QString& modelPath)
{
	QString normalized = modelPath;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const int at = normalized.toLower().lastIndexOf(QStringLiteral("/models/"));
	return at > 0 ? normalized.left(at) + QStringLiteral("/def") : QString();
}

QString uniqueClipName(const ModelSkeleton& skeleton, const QString& wanted, int except = -1)
{
	const auto taken = [&](const QString& name) {
		for (int index = 0; index < skeleton.clips.size(); ++index) {
			if (index != except && skeleton.clips.at(index).name.compare(name, Qt::CaseInsensitive) == 0) {
				return true;
			}
		}
		return false;
	};
	const QString base = wanted.isEmpty() ? QStringLiteral("anim") : wanted;
	if (!taken(base)) {
		return base;
	}
	for (int suffix = 2;; ++suffix) {
		const QString candidate = QStringLiteral("%1_%2").arg(base).arg(suffix);
		if (!taken(candidate)) {
			return candidate;
		}
	}
}

// Reads md5anims through the companion budget and appends the ones that fit
// the mesh's skeleton as clips.
class Md5ClipLoader {
public:
	Md5ClipLoader(ModelMesh* mesh, const ModelWorkControl& control, model_formats::Companions& companions)
		: m_mesh(mesh)
		, m_control(control)
		, m_companions(companions)
	{
	}

	// False only when the decode must stop (cancelled); problems with one
	// file become warnings. *added is the new clip's index, or -1.
	bool load(const QString& path, const QString& clipName, int* added)
	{
		*added = -1;
		m_attempted.append(path);
		if (m_stopped) {
			return true;
		}
		QByteArray bytes;
		QString problem;
		if (!m_companions.read(path, &bytes, &problem)) {
			if (m_companions.exhausted()) {
				stop(problem);
			} else {
				m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The animation %1 could not be read: %2").arg(path, problem);
			}
			return true;
		}
		Md5AnimFile anim;
		if (!parseMd5AnimText(bytes, &anim, &problem, m_control)) {
			if (!modelWorkCheckpoint(m_control, ModelWorkPhase::Validating, 0, 0, &m_mesh->error)) {
				return false;
			}
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The animation %1 could not be read: %2").arg(path, problem);
			return true;
		}
		ModelSkeleton& skeleton = m_mesh->skeleton;
		if (!animMatchesSkeleton(anim, skeleton.joints)) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The animation %1 does not match the mesh's joints and was left out.")
				.arg(path);
			return true;
		}
		const qint64 poses = qint64(anim.frames.size()) * qint64(skeleton.joints.size());
		if (poses > kMaxTotalJointPoses - m_poses) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The animation %1 was left out to keep the clips within %2 joint poses.")
				.arg(path).arg(kMaxTotalJointPoses);
			return true;
		}
		m_poses += poses;
		for (const QString& warning : std::as_const(anim.warnings)) {
			m_mesh->warnings << QStringLiteral("%1: %2").arg(path, warning);
		}
		ModelSkeletalClip clip;
		clip.name = uniqueClipName(skeleton, clipName);
		clip.sourcePath = path;
		clip.framesPerSecond = anim.frameRate;
		clip.frames = std::move(anim.frames);
		clip.frameMins = std::move(anim.frameMins);
		clip.frameMaxs = std::move(anim.frameMaxs);
		skeleton.clips.append(std::move(clip));
		*added = int(skeleton.clips.size()) - 1;
		return true;
	}

	bool readBytes(const QString& path, QByteArray* bytes)
	{
		if (m_stopped) {
			return false;
		}
		QString problem;
		if (m_companions.read(path, bytes, &problem)) {
			return true;
		}
		if (m_companions.exhausted()) {
			stop(problem);
		} else {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The declaration file %1 could not be read: %2").arg(path, problem);
		}
		return false;
	}

	[[nodiscard]] bool stopped() const { return m_stopped || m_companions.exhausted(); }

	[[nodiscard]] bool attempted(const QString& relative) const
	{
		for (const QString& path : m_attempted) {
			if (gamePathMatches(path, relative)) {
				return true;
			}
		}
		return false;
	}

private:
	void stop(const QString& problem)
	{
		if (!m_stopped) {
			m_mesh->warnings << problem;
			m_stopped = true;
		}
	}

	ModelMesh* m_mesh = nullptr;
	const ModelWorkControl& m_control;
	model_formats::Companions& m_companions;
	QStringList m_attempted;
	qint64 m_poses = 0;
	bool m_stopped = false;
};

struct LoadedDecl {
	Doom3ModelDecl decl;
	int file = 0;
};

QString effectiveDeclMesh(const QVector<LoadedDecl>& decls, const QHash<QString, int>& byName, int index, int depth)
{
	const Doom3ModelDecl& decl = decls.at(index).decl;
	if (!decl.meshPath.isEmpty() || decl.inherit.isEmpty() || depth >= kMaxDeclInheritDepth) {
		return decl.meshPath;
	}
	const int parent = byName.value(decl.inherit.toLower(), -1);
	return parent >= 0 && parent != index ? effectiveDeclMesh(decls, byName, parent, depth + 1) : QString();
}

// Inherited anims first, each own anim replacing an inherited one of the same
// name (idDeclModelDef::ParseAnim reuses the inherited idAnim).
QVector<QPair<QString, QString>> effectiveDeclAnims(const QVector<LoadedDecl>& decls, const QHash<QString, int>& byName, int index, int depth)
{
	const Doom3ModelDecl& decl = decls.at(index).decl;
	QVector<QPair<QString, QString>> anims;
	if (!decl.inherit.isEmpty() && depth < kMaxDeclInheritDepth) {
		const int parent = byName.value(decl.inherit.toLower(), -1);
		if (parent >= 0 && parent != index) {
			anims = effectiveDeclAnims(decls, byName, parent, depth + 1);
		}
	}
	for (const auto& anim : decl.anims) {
		bool replaced = false;
		for (auto& existing : anims) {
			if (existing.first == anim.first) {
				existing.second = anim.second;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			anims.append(anim);
		}
	}
	return anims;
}

// Looks through def/*.def for model declarations naming this mesh and applies
// their animation names. False only when the decode must stop.
bool applyModelDeclarations(const QString& path, ModelMesh* mesh, const ModelWorkControl& control, model_formats::Companions& companions,
	Md5ClipLoader& loader)
{
	if (!companions.canList() || loader.stopped()) {
		return true;
	}
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	const QStringList suffixes{QStringLiteral("def")};
	QStringList files = companions.list(QStringLiteral("def"), suffixes);
	const QString rootFolder = gameDefinitionFolder(path);
	if (!rootFolder.isEmpty()) {
		files += companions.list(rootFolder, suffixes);
	}
	QStringList unique;
	QSet<QString> seen;
	for (const QString& file : std::as_const(files)) {
		const QString key = normalizedGamePath(file);
		if (!seen.contains(key)) {
			seen.insert(key);
			unique.append(file);
		}
	}

	QVector<LoadedDecl> decls;
	QStringList readFiles;
	QVector<QStringList> fileWarnings;
	for (const QString& file : std::as_const(unique)) {
		if (!work.step()) {
			return false;
		}
		if (loader.stopped()) {
			break;
		}
		QByteArray bytes;
		if (!loader.readBytes(file, &bytes)) {
			continue;
		}
		QStringList warnings;
		const QVector<Doom3ModelDecl> parsed = parseDoom3ModelDecls(QString::fromLatin1(bytes), &warnings);
		readFiles.append(file);
		fileWarnings.append(warnings);
		for (const Doom3ModelDecl& decl : parsed) {
			decls.append({decl, int(readFiles.size()) - 1});
		}
	}
	if (!work.check()) {
		return false;
	}

	// The first declaration of a name wins, as the decl manager keeps it.
	QHash<QString, int> byName;
	for (int index = 0; index < decls.size(); ++index) {
		const QString key = decls.at(index).decl.name.toLower();
		if (!byName.contains(key)) {
			byName.insert(key, index);
		}
	}

	ModelSkeleton& skeleton = mesh->skeleton;
	QSet<int> claimed;
	QSet<int> warnedFiles;
	for (int index = 0; index < decls.size(); ++index) {
		if (!work.step()) {
			return false;
		}
		if (!gamePathMatches(path, effectiveDeclMesh(decls, byName, index, 0))) {
			continue;
		}
		const LoadedDecl& loaded = decls.at(index);
		const QString& file = readFiles.at(loaded.file);
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Named by model declaration \"%1\" in %2.").arg(loaded.decl.name, file);
		if (!warnedFiles.contains(loaded.file)) {
			warnedFiles.insert(loaded.file);
			for (const QString& warning : fileWarnings.at(loaded.file)) {
				mesh->warnings << QStringLiteral("%1: %2").arg(file, warning);
			}
		}
		const QVector<QPair<QString, QString>> anims = effectiveDeclAnims(decls, byName, index, 0);
		for (const auto& anim : anims) {
			if (!work.step()) {
				return false;
			}
			const QString& alias = anim.first;
			const QString& animPath = anim.second;
			if (alias.isEmpty() || animPath.isEmpty()) {
				continue;
			}
			int clipIndex = -1;
			for (int clip = 0; clip < skeleton.clips.size(); ++clip) {
				if (gamePathMatches(skeleton.clips.at(clip).sourcePath, animPath)) {
					clipIndex = clip;
					break;
				}
			}
			if (clipIndex >= 0) {
				// A sibling clip takes the first name a declaration gives it,
				// unless another clip already goes by that name.
				if (!claimed.contains(clipIndex)) {
					claimed.insert(clipIndex);
					if (uniqueClipName(skeleton, alias, clipIndex) == alias) {
						skeleton.clips[clipIndex].name = alias;
					}
				}
				continue;
			}
			if (loader.attempted(animPath) || loader.stopped()) {
				continue;
			}
			int added = -1;
			if (!loader.load(animPath, alias, &added)) {
				return false;
			}
			if (added >= 0) {
				claimed.insert(added);
			}
		}
	}
	return true;
}

// Area-weighted face normals (counter-clockwise faces), summed across vertices
// that share an exact position the way R_DeriveTangents merges duplicated
// vertices (neo/renderer/tr_trisurf.cpp), so UV seams do not show as creases.
QVector<ModelVec3> weldedFaceNormals(const ModelSurface& surface, const QVector<ModelVec3>& positions)
{
	const int count = int(positions.size());
	QVector<ModelVec3> sums(count, ModelVec3{0.0f, 0.0f, 0.0f});
	for (const ModelTriangle& triangle : surface.triangles) {
		if (triangle.a < 0 || triangle.b < 0 || triangle.c < 0 || triangle.a >= count || triangle.b >= count || triangle.c >= count) {
			continue;
		}
		const ModelVec3& a = positions.at(triangle.a);
		const ModelVec3 face = vecCross(vecSubtract(positions.at(triangle.b), a), vecSubtract(positions.at(triangle.c), a));
		sums[triangle.a] = vecAdd(sums.at(triangle.a), face);
		sums[triangle.b] = vecAdd(sums.at(triangle.b), face);
		sums[triangle.c] = vecAdd(sums.at(triangle.c), face);
	}
	std::vector<int> order(static_cast<size_t>(count));
	std::iota(order.begin(), order.end(), 0);
	const auto less = [&positions](int left, int right) {
		const ModelVec3& a = positions.at(left);
		const ModelVec3& b = positions.at(right);
		if (a.x != b.x) {
			return a.x < b.x;
		}
		if (a.y != b.y) {
			return a.y < b.y;
		}
		return a.z < b.z;
	};
	std::sort(order.begin(), order.end(), less);
	QVector<ModelVec3> normals(count, ModelVec3{0.0f, 0.0f, 1.0f});
	size_t start = 0;
	while (start < order.size()) {
		size_t end = start + 1;
		while (end < order.size() && !less(order[start], order[end]) && !less(order[end], order[start])) {
			++end;
		}
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		for (size_t index = start; index < end; ++index) {
			const ModelVec3& sum = sums.at(order[index]);
			x += sum.x;
			y += sum.y;
			z += sum.z;
		}
		const double length = std::sqrt(x * x + y * y + z * z);
		const ModelVec3 normal = length > 1e-20 && std::isfinite(length) ? ModelVec3{float(x / length), float(y / length), float(z / length)}
			: ModelVec3{0.0f, 0.0f, 1.0f};
		for (size_t index = start; index < end; ++index) {
			normals[order[index]] = normal;
		}
		start = end;
	}
	return normals;
}

QString surfaceNameFor(const Md5MeshRecord& record, int index)
{
	QString name = record.name.trimmed();
	if (name.isEmpty()) {
		QString shader = record.shader;
		shader.replace(QLatin1Char('\\'), QLatin1Char('/'));
		name = shader.mid(shader.lastIndexOf(QLatin1Char('/')) + 1).trimmed();
	}
	return name.isEmpty() ? QStringLiteral("mesh%1").arg(index) : name;
}

// ---------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------

// Latin-1 with the characters a quoted MD5 string cannot hold replaced.
QByteArray md5SafeText(const QString& text)
{
	QByteArray bytes = text.toLatin1();
	for (char& ch : bytes) {
		if (ch == '"') {
			ch = '\'';
		} else if (uchar(ch) < ' ') {
			ch = '_';
		}
	}
	return bytes;
}

class Md5TextWriter {
public:
	void text(const char* value) { m_out.append(value); }
	void text(const QByteArray& value) { m_out.append(value); }
	void quoted(const QString& value)
	{
		m_out.append('"');
		m_out.append(md5SafeText(value));
		m_out.append('"');
	}
	void integer(qint64 value) { m_out.append(QByteArray::number(value)); }
	// C-locale fixed notation with ten decimals, trailing zeros trimmed: idLexer
	// reads it (no exponents), and floats survive the round trip.
	void number(double value)
	{
		if (!std::isfinite(value)) {
			m_finite = false;
			m_out.append('0');
			return;
		}
		QByteArray digits = QByteArray::number(value, 'f', 10);
		if (digits.contains('.')) {
			while (digits.endsWith('0')) {
				digits.chop(1);
			}
			if (digits.endsWith('.')) {
				digits.chop(1);
			}
		}
		if (digits == "-0") {
			digits = "0";
		}
		m_out.append(digits);
	}
	void vector(const ModelVec3& value)
	{
		m_out.append("( ");
		number(value.x);
		m_out.append(' ');
		number(value.y);
		m_out.append(' ');
		number(value.z);
		m_out.append(" )");
	}
	[[nodiscard]] bool finite() const { return m_finite; }
	QByteArray take() { return std::move(m_out); }

private:
	QByteArray m_out;
	bool m_finite = true;
};

// The three components an MD5 file stores for a joint matrix's rotation.
ModelVec3 md5RotationOf(const ModelJointMatrix& matrix)
{
	return modelQuatToXyzNegativeW(modelJointRotation(matrix));
}

} // namespace

// ---------------------------------------------------------------------------
// Decoders
// ---------------------------------------------------------------------------

namespace model_formats {

void decodeMd5Mesh(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) {
		return;
	}
	if (qint64(bytes.size()) > kMaxTextBytes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD5 file is larger than the %1 MiB a text model may hold.")
			.arg(kMaxTextBytes / (1024 * 1024));
		return;
	}
	Md5MeshFile file;
	if (!parseMd5MeshText(bytes, &file, &mesh->error, control)) {
		return;
	}
	mesh->version = kMd5Version;

	ModelSkeleton& skeleton = mesh->skeleton;
	skeleton.sourceFormat = QStringLiteral("md5");
	for (const Md5JointRecord& record : std::as_const(file.joints)) {
		ModelJoint joint;
		joint.name = record.name;
		joint.parent = record.parent;
		// Joints are stored in model space (LoadModel converts them to
		// parent-relative poses afterwards).
		joint.bind = modelJointMatrix(modelQuatFromXyzNegativeW(record.rotation.x, record.rotation.y, record.rotation.z), record.position);
		skeleton.joints.append(joint);
	}
	const QVector<ModelJointMatrix> bindPose = modelSkeletonBindPose(skeleton);
	QVector<ModelJointMatrix> inverseBind;
	inverseBind.reserve(bindPose.size());
	for (int index = 0; index < bindPose.size(); ++index) {
		bool ok = true;
		inverseBind.append(modelJointInverse(bindPose.at(index), &ok));
		if (!ok) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Joint %1 (%2) has a bind pose that cannot be inverted.")
				.arg(QString::number(index), skeleton.joints.at(index).name);
			return;
		}
	}

	QSet<QString> skinNames;
	for (int index = 0; index < file.meshes.size(); ++index) {
		if (!work.step()) {
			return;
		}
		const Md5MeshRecord& record = file.meshes.at(index);
		ModelSurface surface;
		surface.index = index;
		surface.name = surfaceNameFor(record, index);
		surface.vertexCount = int(record.texCoords.size());
		surface.texCoords = record.texCoords;
		if (!record.shader.isEmpty()) {
			surface.skinPaths.append(record.shader);
			if (!skinNames.contains(record.shader)) {
				skinNames.insert(record.shader);
				mesh->skinPaths.append(record.shader);
			}
		}
		surface.triangles.reserve(record.triangles.size());
		for (const ModelTriangle& triangle : record.triangles) {
			// Clockwise in the file; see the winding note at the top.
			surface.triangles.append({triangle.a, triangle.c, triangle.b});
		}
		if (record.droppedTriangles > 0) {
			surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.")
				.arg(record.droppedTriangles);
		}

		// One influence range per vertex (weights shared between vertices are
		// copied), so each influence can carry its vertex's normal.
		ModelSurfaceSkinning& skinning = surface.skinning;
		skinning.first.reserve(surface.vertexCount);
		skinning.count.reserve(surface.vertexCount);
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			if (!work.step()) {
				return;
			}
			skinning.first.append(int(skinning.influences.size()));
			const int first = record.firstWeight.at(vertex);
			const int count = record.weightCount.at(vertex);
			for (int weight = first; weight < first + count; ++weight) {
				const Md5WeightRecord& source = record.weights.at(weight);
				ModelJointInfluence influence;
				influence.joint = source.joint;
				// Doom 3 uses the biases as written, without normalizing.
				influence.weight = source.bias;
				influence.offset = source.position;
				skinning.influences.append(influence);
			}
			skinning.count.append(count);
		}

		ModelFrameGeometry bind;
		QString problem;
		if (!skinModelSurface(surface, bindPose, &bind, &problem)) {
			mesh->error = problem;
			return;
		}
		for (const ModelVec3& position : std::as_const(bind.positions)) {
			if (!isFinite(position)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Mesh %1 places a vertex beyond the range of finite numbers.").arg(index);
				return;
			}
		}
		bind.normals = weldedFaceNormals(surface, bind.positions);
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			if (!work.step()) {
				return;
			}
			const int first = skinning.first.at(vertex);
			for (int influence = first; influence < first + skinning.count.at(vertex); ++influence) {
				ModelJointInfluence& target = skinning.influences[influence];
				target.normalOffset = modelJointTransformNormal(inverseBind.at(target.joint), bind.normals.at(vertex));
			}
		}
		surface.frames.append(std::move(bind));
		mesh->surfaces.append(std::move(surface));
	}

	if (!file.commandLine.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Command line: %1").arg(file.commandLine);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Joints: %1").arg(skeleton.joints.size());
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Meshes: %1").arg(file.meshes.size());

	// Animations beside the mesh, then those its .def declarations name.
	Md5ClipLoader loader(mesh, control, companions);
	if (companions.canRead() && companions.canList()) {
		QStringList anims = companions.list(pathDirectory(path), {QStringLiteral("md5anim")});
		std::sort(anims.begin(), anims.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
		for (const QString& animPath : std::as_const(anims)) {
			if (!work.step()) {
				return;
			}
			if (loader.stopped()) {
				break;
			}
			int added = -1;
			if (!loader.load(animPath, pathStem(animPath), &added)) {
				return;
			}
		}
	}
	const int detailInsert = mesh->detailLines.size();
	if (companions.canRead() && !applyModelDeclarations(path, mesh, control, companions, loader)) {
		return;
	}
	mesh->detailLines.insert(detailInsert, QCoreApplication::translate("VibeStudioModelMesh", "Animations read: %1").arg(skeleton.clips.size()));

	ModelSkeletonBakeOptions options;
	options.includeBindPose = true;
	QString problem;
	if (!bakeModelSkeleton(mesh, options, &problem, control)) {
		mesh->error = problem.isEmpty() ? QCoreApplication::translate("VibeStudioModelMesh", "The MD5 skeleton could not be baked into frames.") : problem;
		return;
	}
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
}

void decodeMd5Anim(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) {
		return;
	}
	Md5AnimFile anim;
	if (!parseMd5AnimText(bytes, &anim, &mesh->error, control)) {
		return;
	}
	mesh->version = kMd5Version;
	ModelSkeleton& skeleton = mesh->skeleton;
	skeleton.sourceFormat = QStringLiteral("md5");
	for (int index = 0; index < anim.joints.size(); ++index) {
		const Md5AnimJointRecord& record = anim.joints.at(index);
		ModelJoint joint;
		joint.name = record.name;
		joint.parent = record.parent;
		joint.bind = anim.baseModelSpace.at(index);
		joint.flags = record.flags;
		skeleton.joints.append(joint);
	}
	mesh->warnings << anim.warnings;
	if (!anim.commandLine.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Command line: %1").arg(anim.commandLine);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Joints: %1").arg(anim.joints.size());
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Frames: %1").arg(anim.frames.size());
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Frame rate: %1").arg(anim.frameRate);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Animated components: %1").arg(anim.animatedComponents);

	ModelSkeletalClip clip;
	clip.name = pathStem(path).trimmed();
	if (clip.name.isEmpty()) {
		clip.name = QStringLiteral("anim");
	}
	clip.framesPerSecond = anim.frameRate;
	clip.frames = std::move(anim.frames);
	clip.frameMins = std::move(anim.frameMins);
	clip.frameMaxs = std::move(anim.frameMaxs);
	skeleton.clips.append(std::move(clip));

	ModelSkeletonBakeOptions options;
	options.includeBindPose = false;
	QString problem;
	if (!bakeModelSkeleton(mesh, options, &problem, control)) {
		mesh->error = problem.isEmpty() ? QCoreApplication::translate("VibeStudioModelMesh", "The MD5 skeleton could not be baked into frames.") : problem;
		return;
	}
	// Animation only: no surfaces, so geometryAvailable stays false.
}

} // namespace model_formats

// ---------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------

QByteArray exportModelMd5Mesh(const ModelMesh& mesh, const ModelMd5ExportOptions& options, QString* error, const ModelWorkControl& control)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return QByteArray();
	};
	QString progressError;
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, &progressError);
	if (!work.check()) {
		return fail(progressError);
	}
	if (!mesh.geometryAvailable || mesh.surfaces.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model has no geometry to write as an MD5 mesh."));
	}
	const bool skeletal = !mesh.skeleton.isEmpty();
	QVector<ModelJoint> joints;
	if (skeletal) {
		QString problem;
		if (!validateModelSkeleton(mesh, &problem)) {
			return fail(problem);
		}
		joints = mesh.skeleton.joints;
	} else {
		// A static model follows one joint at the origin.
		ModelJoint origin;
		origin.name = QStringLiteral("origin");
		origin.parent = -1;
		joints.append(origin);
	}
	if (joints.size() > kMaxJoints) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model has %1 joints; MD5 files written here hold up to %2.")
			.arg(joints.size()).arg(kMaxJoints));
	}
	bool invertible = true;
	const ModelJointMatrix rootInverse = modelJointInverse(joints.first().bind, &invertible);
	if (!invertible) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "Joint %1 (%2) has a bind pose that cannot be inverted.")
			.arg(QStringLiteral("0"), joints.first().name));
	}

	struct OutWeight {
		int joint = 0;
		float bias = 0.0f;
		ModelVec3 offset;
	};
	struct OutMesh {
		const ModelSurface* surface = nullptr;
		QString shader;
		QVector<int> first;
		QVector<int> count;
		QVector<OutWeight> weights;
	};
	QVector<OutMesh> meshes;
	for (const ModelSurface& surface : mesh.surfaces) {
		if (!work.step()) {
			return fail(progressError);
		}
		if (surface.vertexCount <= 0) {
			continue;
		}
		const bool skinned = skeletal && !surface.skinning.isEmpty();
		const ModelFrameGeometry* still = surface.frames.isEmpty() ? nullptr : &surface.frames.first();
		if (!skinned && (!still || still->positions.size() < surface.vertexCount)) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "Surface \"%1\" has no geometry to write.").arg(surface.name));
		}
		for (const ModelTriangle& triangle : surface.triangles) {
			if (triangle.a < 0 || triangle.b < 0 || triangle.c < 0 || triangle.a >= surface.vertexCount || triangle.b >= surface.vertexCount
				|| triangle.c >= surface.vertexCount) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "Surface \"%1\" has a triangle that names a vertex it does not hold.")
					.arg(surface.name));
			}
		}
		OutMesh out;
		out.surface = &surface;
		out.shader = !surface.skinPaths.isEmpty() && !surface.skinPaths.first().isEmpty() ? surface.skinPaths.first() : surface.name;
		out.first.reserve(surface.vertexCount);
		out.count.reserve(surface.vertexCount);
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			if (!work.step()) {
				return fail(progressError);
			}
			out.first.append(int(out.weights.size()));
			const int count = skinned ? surface.skinning.count.at(vertex) : 0;
			if (count > kMaxInfluencesPerVertex) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of surface \"%2\" has %3 joint influences; MD5 files written here hold up to %4.")
					.arg(QString::number(vertex), surface.name, QString::number(count), QString::number(kMaxInfluencesPerVertex)));
			}
			if (count > 0) {
				const int first = surface.skinning.first.at(vertex);
				for (int index = first; index < first + count; ++index) {
					const ModelJointInfluence& influence = surface.skinning.influences.at(index);
					out.weights.append({influence.joint, influence.weight, influence.offset});
				}
				out.count.append(count);
				continue;
			}
			// Unskinned geometry rides rigidly on the first joint.
			ModelVec3 position;
			if (still && vertex < still->positions.size()) {
				position = still->positions.at(vertex);
			}
			out.weights.append({0, 1.0f, modelJointTransformPoint(rootInverse, position)});
			out.count.append(1);
		}
		if (out.weights.size() > kMaxVertexSlots) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "Surface \"%1\" has more joint influences than an MD5 mesh can hold.")
				.arg(surface.name));
		}
		meshes.append(std::move(out));
	}
	if (meshes.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model has no vertices to write as an MD5 mesh."));
	}

	// The layout idRenderModelMD5::LoadModel reads, in the spacing Doom 3's
	// exporter writes.
	Md5TextWriter writer;
	writer.text("MD5Version 10\ncommandline ");
	writer.quoted(options.commandLine);
	writer.text("\n\nnumJoints ");
	writer.integer(joints.size());
	writer.text("\nnumMeshes ");
	writer.integer(meshes.size());
	writer.text("\n\njoints {\n");
	for (const ModelJoint& joint : std::as_const(joints)) {
		if (!work.step()) {
			return fail(progressError);
		}
		writer.text("\t");
		writer.quoted(joint.name);
		writer.text("\t");
		writer.integer(joint.parent);
		writer.text(" ");
		writer.vector(modelJointTranslation(joint.bind));
		writer.text(" ");
		writer.vector(md5RotationOf(joint.bind));
		writer.text("\t\t// ");
		if (joint.parent >= 0) {
			writer.text(md5SafeText(joints.at(joint.parent).name));
		}
		writer.text("\n");
	}
	writer.text("}\n");
	for (const OutMesh& out : std::as_const(meshes)) {
		const ModelSurface& surface = *out.surface;
		writer.text("\nmesh {\n\t// meshes: ");
		writer.text(md5SafeText(surface.name));
		writer.text("\n\tshader ");
		writer.quoted(out.shader);
		writer.text("\n\n\tnumverts ");
		writer.integer(surface.vertexCount);
		writer.text("\n");
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			if (!work.step()) {
				return fail(progressError);
			}
			const ModelTexCoord st = vertex < surface.texCoords.size() ? surface.texCoords.at(vertex) : ModelTexCoord{};
			writer.text("\tvert ");
			writer.integer(vertex);
			writer.text(" ( ");
			writer.number(st.u);
			writer.text(" ");
			writer.number(st.v);
			writer.text(" ) ");
			writer.integer(out.first.at(vertex));
			writer.text(" ");
			writer.integer(out.count.at(vertex));
			writer.text("\n");
		}
		writer.text("\n\tnumtris ");
		writer.integer(surface.triangles.size());
		writer.text("\n");
		for (int index = 0; index < surface.triangles.size(); ++index) {
			if (!work.step()) {
				return fail(progressError);
			}
			const ModelTriangle& triangle = surface.triangles.at(index);
			// Back to the file's clockwise order.
			writer.text("\ttri ");
			writer.integer(index);
			writer.text(" ");
			writer.integer(triangle.a);
			writer.text(" ");
			writer.integer(triangle.c);
			writer.text(" ");
			writer.integer(triangle.b);
			writer.text("\n");
		}
		writer.text("\n\tnumweights ");
		writer.integer(out.weights.size());
		writer.text("\n");
		for (int index = 0; index < out.weights.size(); ++index) {
			if (!work.step()) {
				return fail(progressError);
			}
			const OutWeight& weight = out.weights.at(index);
			writer.text("\tweight ");
			writer.integer(index);
			writer.text(" ");
			writer.integer(weight.joint);
			writer.text(" ");
			writer.number(weight.bias);
			writer.text(" ");
			writer.vector(weight.offset);
			writer.text("\n");
		}
		writer.text("}\n");
	}
	if (!writer.finite()) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model holds a number that is not finite, so it cannot be written as MD5."));
	}
	if (!work.check()) {
		return fail(progressError);
	}
	return writer.take();
}

QByteArray exportModelMd5Anim(const ModelMesh& mesh, int clipIndex, const ModelMd5ExportOptions& options, QString* error, const ModelWorkControl& control)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return QByteArray();
	};
	QString progressError;
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, &progressError);
	if (!work.check()) {
		return fail(progressError);
	}
	const ModelSkeleton& skeleton = mesh.skeleton;
	if (skeleton.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model has no skeleton to write as an MD5 animation."));
	}
	if (clipIndex < 0 || clipIndex >= skeleton.clips.size()) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model has no animation clip %1.").arg(clipIndex));
	}
	QString problem;
	if (!validateModelSkeleton(mesh, &problem)) {
		return fail(problem);
	}
	const ModelSkeletalClip& clip = skeleton.clips.at(clipIndex);
	if (clip.frames.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "Clip \"%1\" has no frames to write.").arg(clip.name));
	}
	if (clip.frames.size() > kMaxFrames || skeleton.joints.size() > kMaxJoints
		|| qint64(clip.frames.size()) * skeleton.joints.size() > kMaxAnimJointPoses) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "Clip \"%1\" holds more frames or joints than an MD5 animation written here can hold.")
			.arg(clip.name));
	}
	const QVector<int> parents = modelJointParents(skeleton);
	const int jointCount = int(skeleton.joints.size());

	QVector<QVector<ModelJointMatrix>> locals;
	locals.reserve(clip.frames.size());
	for (const QVector<ModelJointMatrix>& frame : clip.frames) {
		if (!work.step()) {
			return fail(progressError);
		}
		QVector<ModelJointMatrix> local = modelJointsToLocalSpace(parents, frame);
		if (local.size() != jointCount) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "Clip \"%1\" does not fit the joint hierarchy.").arg(clip.name));
		}
		locals.append(std::move(local));
	}

	// Bounds from the clip, else from the skinned geometry (unskinned surfaces
	// ride on the first joint, as the md5mesh writer binds them), else from
	// the joint origins.
	bool invertible = true;
	const ModelJointMatrix rootInverse = modelJointInverse(skeleton.joints.first().bind, &invertible);
	QVector<ModelVec3> mins;
	QVector<ModelVec3> maxs;
	for (int frame = 0; frame < clip.frames.size(); ++frame) {
		if (!work.step()) {
			return fail(progressError);
		}
		if (frame < clip.frameMins.size() && frame < clip.frameMaxs.size()) {
			mins.append(clip.frameMins.at(frame));
			maxs.append(clip.frameMaxs.at(frame));
			continue;
		}
		const QVector<ModelJointMatrix>& pose = clip.frames.at(frame);
		bool haveBounds = false;
		ModelVec3 low;
		ModelVec3 high;
		const auto include = [&](const ModelVec3& point) {
			if (!model_formats::isFinite(point)) {
				return;
			}
			low = haveBounds ? vecMin(low, point) : point;
			high = haveBounds ? vecMax(high, point) : point;
			haveBounds = true;
		};
		for (const ModelSurface& surface : mesh.surfaces) {
			if (!surface.skinning.isEmpty()) {
				ModelFrameGeometry geometry;
				if (!skinModelSurface(surface, pose, &geometry, &problem)) {
					return fail(problem);
				}
				for (const ModelVec3& point : std::as_const(geometry.positions)) {
					include(point);
				}
			} else if (invertible && !surface.frames.isEmpty()) {
				const ModelJointMatrix carry = modelJointMultiply(pose.first(), rootInverse);
				for (const ModelVec3& point : surface.frames.first().positions) {
					include(modelJointTransformPoint(carry, point));
				}
			}
		}
		if (!haveBounds) {
			for (const ModelJointMatrix& matrix : pose) {
				include(modelJointTranslation(matrix));
			}
		}
		mins.append(low);
		maxs.append(high);
	}

	int frameRate = options.defaultFrameRate > 0 ? options.defaultFrameRate : 24;
	if (std::isfinite(clip.framesPerSecond) && clip.framesPerSecond >= 1.0 && clip.framesPerSecond <= double(kMaxFrameRate)) {
		frameRate = int(std::lround(clip.framesPerSecond));
	}
	frameRate = std::min(frameRate, kMaxFrameRate);

	// The layout idMD5Anim::LoadAnim reads. Every joint animates all six
	// components (flags 63, components 6i .. 6i + 5), so no value depends on
	// the baseframe and the clip round-trips exactly.
	Md5TextWriter writer;
	writer.text("MD5Version 10\ncommandline ");
	writer.quoted(options.commandLine);
	writer.text("\n\nnumFrames ");
	writer.integer(clip.frames.size());
	writer.text("\nnumJoints ");
	writer.integer(jointCount);
	writer.text("\nframeRate ");
	writer.integer(frameRate);
	writer.text("\nnumAnimatedComponents ");
	writer.integer(qint64(jointCount) * 6);
	writer.text("\n\nhierarchy {\n");
	for (int index = 0; index < jointCount; ++index) {
		const ModelJoint& joint = skeleton.joints.at(index);
		writer.text("\t");
		writer.quoted(joint.name);
		writer.text("\t");
		writer.integer(joint.parent);
		writer.text(" ");
		writer.integer(kAnimAllBits);
		writer.text(" ");
		writer.integer(qint64(index) * 6);
		writer.text("\t// ");
		if (joint.parent >= 0) {
			writer.text(md5SafeText(skeleton.joints.at(joint.parent).name));
			writer.text(" ");
		}
		writer.text("( Tx Ty Tz Qx Qy Qz )\n");
	}
	writer.text("}\n\nbounds {\n");
	for (int frame = 0; frame < mins.size(); ++frame) {
		writer.text("\t");
		writer.vector(mins.at(frame));
		writer.text(" ");
		writer.vector(maxs.at(frame));
		writer.text("\n");
	}
	writer.text("}\n\nbaseframe {\n");
	for (const ModelJointMatrix& matrix : locals.first()) {
		writer.text("\t");
		writer.vector(modelJointTranslation(matrix));
		writer.text(" ");
		writer.vector(md5RotationOf(matrix));
		writer.text("\n");
	}
	writer.text("}\n");
	for (int frame = 0; frame < locals.size(); ++frame) {
		if (!work.step()) {
			return fail(progressError);
		}
		writer.text("\nframe ");
		writer.integer(frame);
		writer.text(" {\n");
		for (const ModelJointMatrix& matrix : locals.at(frame)) {
			if (!work.step()) {
				return fail(progressError);
			}
			const ModelVec3 position = modelJointTranslation(matrix);
			const ModelVec3 rotation = md5RotationOf(matrix);
			writer.text("\t ");
			writer.number(position.x);
			writer.text(" ");
			writer.number(position.y);
			writer.text(" ");
			writer.number(position.z);
			writer.text(" ");
			writer.number(rotation.x);
			writer.text(" ");
			writer.number(rotation.y);
			writer.text(" ");
			writer.number(rotation.z);
			writer.text("\n");
		}
		writer.text("}\n");
	}
	if (!writer.finite()) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model holds a number that is not finite, so it cannot be written as MD5."));
	}
	if (!work.check()) {
		return fail(progressError);
	}
	return writer.take();
}

// ---------------------------------------------------------------------------
// Doom 3 model declarations
// ---------------------------------------------------------------------------

QVector<Doom3ModelDecl> parseDoom3ModelDecls(const QString& text, QStringList* warnings)
{
	QVector<Doom3ModelDecl> decls;
	const QByteArray bytes = text.toUtf8();
	Md5Lexer lexer(bytes);
	const auto warn = [warnings](const QString& message) {
		if (warnings) {
			warnings->append(message);
		}
	};
	const auto nextToken = [&]() {
		const Md5Token token = lexer.next();
		switch (lexer.problem()) {
		case Md5Lexer::Problem::None:
			break;
		case Md5Lexer::Problem::OpenString:
			warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: a quoted string is not closed before the end of the line.")
				.arg(lexer.problemLine()));
			break;
		case Md5Lexer::Problem::OpenComment:
			warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: a comment is not closed.").arg(lexer.problemLine()));
			break;
		}
		lexer.clearProblem();
		return token;
	};
	const auto keyIs = [](const Md5Token& token, const char* keyword) {
		return token.kind == Md5Token::Kind::Word && token.utf8().compare(QLatin1String(keyword), Qt::CaseInsensitive) == 0;
	};
	// Skips to the brace closing one already read on `openLine`; false when
	// the text ends first.
	const auto skipBlock = [&](int openLine) {
		int depth = 1;
		for (;;) {
			const Md5Token token = nextToken();
			if (token.isEnd()) {
				warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: the block opened here is not closed.").arg(openLine));
				return false;
			}
			if (token.isPunct('{')) {
				++depth;
			} else if (token.isPunct('}') && --depth == 0) {
				return true;
			}
		}
	};
	// Skips a parenthesised group whose "(" was just read; a brace ends it
	// early so a missing ")" cannot swallow the rest of the block.
	const auto skipGroup = [&](int openLine) {
		for (;;) {
			const Md5Token token = nextToken();
			if (token.isPunct(')')) {
				return true;
			}
			if (token.isEnd() || token.isPunct('{') || token.isPunct('}')) {
				warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: the parenthesis opened here is not closed.").arg(openLine));
				if (token.isEnd()) {
					return false;
				}
				lexer.unread(token);
				return true;
			}
		}
	};

	// idDeclModelDef::Parse: the keys of one model block. False when the text
	// ends inside it (the declaration is then left out, as Doom 3 drops it).
	const auto parseModel = [&](Doom3ModelDecl decl, int openLine) {
		const auto readValue = [&](const Md5Token& key, QString* value) {
			const Md5Token token = nextToken();
			if (!token.isValue()) {
				warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: \"%2\" in model \"%3\" has no value.")
					.arg(QString::number(key.line), key.utf8(), decl.name));
				if (!token.isEnd()) {
					lexer.unread(token);
				}
				return false;
			}
			*value = token.utf8();
			return true;
		};
		for (;;) {
			const Md5Token key = nextToken();
			if (key.isEnd()) {
				warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: the block opened here is not closed.").arg(openLine));
				return false;
			}
			if (key.isPunct('}')) {
				decls.append(decl);
				return true;
			}
			if (key.isPunct('{')) {
				if (!skipBlock(key.line)) {
					return false;
				}
				continue;
			}
			if (key.isPunct('(')) {
				if (!skipGroup(key.line)) {
					return false;
				}
				continue;
			}
			if (!key.isValue()) {
				continue;
			}
			if (keyIs(key, "inherit")) {
				readValue(key, &decl.inherit);
			} else if (keyIs(key, "mesh")) {
				readValue(key, &decl.meshPath);
			} else if (keyIs(key, "skin")) {
				readValue(key, &decl.skin);
			} else if (keyIs(key, "offset")) {
				float values[3] = {0.0f, 0.0f, 0.0f};
				const Md5Token open = nextToken();
				bool ok = open.isPunct('(');
				if (!ok && (open.isEnd() || open.kind == Md5Token::Kind::Punct)) {
					lexer.unread(open);
				}
				for (int index = 0; ok && index < 3; ++index) {
					const Md5Token number = nextToken();
					ok = parseNumberToken(number, values + index) == NumberResult::Ok;
					if (!ok && (number.isEnd() || number.kind == Md5Token::Kind::Punct)) {
						lexer.unread(number);
					}
				}
				if (ok) {
					const Md5Token close = nextToken();
					ok = close.isPunct(')');
					if (!ok) {
						lexer.unread(close);
					}
				}
				if (ok) {
					decl.offset = {values[0], values[1], values[2]};
				} else {
					warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: the offset of model \"%2\" should be three numbers in parentheses.")
						.arg(QString::number(key.line), decl.name));
				}
			} else if (keyIs(key, "anim")) {
				QString alias;
				QString animPath;
				if (!readValue(key, &alias) || !readValue(key, &animPath)) {
					continue;
				}
				// Synced alternatives ("a.md5anim, b.md5anim") follow the first.
				while (lexer.peek().isPunct(',')) {
					nextToken();
					QString alternative;
					if (!readValue(key, &alternative)) {
						break;
					}
				}
				// Frame commands and anim flags are not needed here.
				if (lexer.peek().isPunct('{')) {
					const Md5Token open = nextToken();
					if (!skipBlock(open.line)) {
						return false;
					}
				}
				decl.anims.append({alias, animPath});
			} else if (keyIs(key, "remove")) {
				QString ignored;
				readValue(key, &ignored);
			} else if (keyIs(key, "channel")) {
				QString ignored;
				if (readValue(key, &ignored) && lexer.peek().isPunct('(')) {
					const Md5Token open = nextToken();
					if (!skipGroup(open.line)) {
						return false;
					}
				}
			}
			// Any other key is ignored.
		}
	};

	// idDeclFile::LoadAndParse: "type name { ... }", or "name { ... }" with the
	// folder's default type (entityDef for def/).
	for (;;) {
		const Md5Token type = nextToken();
		if (type.isEnd()) {
			break;
		}
		if (type.isPunct('{')) {
			warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: a block has no declaration name.").arg(type.line));
			if (!skipBlock(type.line)) {
				break;
			}
			continue;
		}
		if (type.isPunct('}')) {
			warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: a closing brace has no opening brace.").arg(type.line));
			continue;
		}
		if (!type.isValue()) {
			continue;
		}
		const Md5Token name = nextToken();
		if (name.isEnd()) {
			warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: \"%2\" ends the file without a declaration body.")
				.arg(QString::number(type.line), type.utf8()));
			break;
		}
		if (name.isPunct('{')) {
			if (!skipBlock(name.line)) {
				break;
			}
			continue;
		}
		if (!name.isValue()) {
			warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: \"%2\" is not followed by a declaration name.")
				.arg(QString::number(type.line), type.utf8()));
			lexer.unread(name);
			continue;
		}
		const Md5Token open = nextToken();
		if (!open.isPunct('{')) {
			warn(QCoreApplication::translate("VibeStudioModelMesh", "Line %1: the declaration \"%2\" has no body.")
				.arg(QString::number(type.line), name.utf8()));
			if (open.isEnd()) {
				break;
			}
			// Read on from the unexpected token as the next declaration.
			lexer.unread(open);
			continue;
		}
		if (type.utf8().compare(QLatin1String("model"), Qt::CaseInsensitive) == 0) {
			Doom3ModelDecl decl;
			decl.name = name.utf8();
			decl.line = type.line;
			if (!parseModel(decl, open.line)) {
				break;
			}
		} else if (!skipBlock(open.line)) {
			break;
		}
	}
	return decls;
}

} // namespace vibestudio
