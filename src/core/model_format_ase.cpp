// ASCII Scene Export (*3DSMAX_ASCIIEXPORT) static models: read the way Doom 3
// and Quake 4 load them, and written so Doom 3 and q3map2 read them back.
//
// Layout and conversion follow the released Doom 3 GPL source:
// neo/renderer/Model_ase.cpp (ASE_Parse and its ASE_Key* handlers) and
// idRenderModelStatic::ConvertASEToModelSurfaces (neo/renderer/Model.cpp),
// with the material path rules of idFileSystemLocal::OSPathToRelativePath
// (neo/framework/FileSystem.cpp) and idDeclManagerLocal::MakeNameCanonical
// (neo/framework/DeclManager.cpp). q3map2 differences come from picomodel
// (libs/picomodel/pm_ase.c) and q3map2's model.cpp in NetRadiant Custom
// (external/compilers/q3map2-nrc). This is an independent implementation; no
// upstream code is copied.
//
// Conventions:
// - Positions are used as written: 3ds Max is Z-up like the game, and Doom 3
//   does not apply NODE_TM to the (already world-space) vertex list. Normals
//   are turned by the NODE_TM rows, as ASE_KeyMESH_NORMALS does.
// - TVERT v is inverted, t = 1 - v ("our OpenGL second texture axis is
//   inverted from MAX's sense"); Doom 3 then applies the material's UVW
//   offset, tiling and angle. q3map2 inverts v the same way and ignores UVW.
// - Winding: 3ds Max lists face corners A B C counter-clockwise seen from the
//   front. Doom 3 stores them as A C B for its clockwise fronts ("we are
//   flipping the order here to change the front/back facing from 3DS to our
//   standard (clockwise facing out)"), picomodel as C B A. The studio keeps
//   counter-clockwise fronts, so A B C is used as written.
// - Only an object's first *MESH is geometry. Later meshes and *MESH_ANIMATION
//   blocks are animation samples, which Doom 3 does not draw either.
// - Materials: Doom 3 takes an object's material from *MATERIAL_REF and names
//   it from the *BITMAP of its diffuse map: the path after the first complete
//   "base" folder (Quake 4: "q4base"; a ".pk4/" component is skipped), then
//   made canonical ('\' to '/', text from the last '.' dropped). Doom 3 reads
//   the bitmap as one whitespace-delimited word and ignores *MESH_MTLID: a
//   multi/sub-object material draws the whole object with the last diffuse
//   bitmap anywhere in that material block. q3map2 splits faces by sub-material
//   (picomodel's _ase_get_submaterial_or_default), so the studio splits them
//   too and names each part with Doom 3's rule applied to that sub-material,
//   warning that Doom 3 draws it differently. q3map2 (model.cpp, AssModelMesh)
//   uses the bitmap without its extension unless *MATERIAL_NAME already looks
//   like a shader ("textures/..." or "models/..."), puts bare names in the
//   model's folder, and trims absolute paths at "/models/" or "/textures/";
//   when that differs from Doom 3's name a detail line says so.
// - Corners merge into one vertex when they share a vertex, a texture vertex
//   and (when the file has normals) a normal within Doom 3's r_slopNormal
//   (0.02). Doom 3's position and UV "slop" merges are not applied.
#include "core/model_ase.h"
#include "core/model_formats_p.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio {

namespace model_formats {

namespace {

constexpr int kMaxAseDepth = 64;
// 1 - r_slopNormal (neo/renderer/Model.cpp).
constexpr float kNormalMergeDot = 0.98f;

enum class AseKind { End, Keyword, Open, Close, String, Word };

struct AseToken {
	AseKind kind = AseKind::End;
	qsizetype start = 0;
	qsizetype length = 0;
	int line = 0;
};

// Splits the text into whitespace-separated words as ASE_GetToken does, with
// quoted strings kept whole (Doom 3 itself stops a quoted bitmap path at its
// first space; decodeAse reports that separately).
class AseLexer {
public:
	explicit AseLexer(const QByteArray& bytes)
		: m_bytes(bytes)
	{
	}

	const AseToken& peek()
	{
		if (!m_hasPeek) {
			m_peek = scan();
			m_hasPeek = true;
		}
		return m_peek;
	}

	AseToken next()
	{
		const AseToken token = peek();
		m_hasPeek = false;
		return token;
	}

	[[nodiscard]] QByteArray text(const AseToken& token) const { return m_bytes.mid(token.start, token.length); }
	[[nodiscard]] int unterminatedLine() const { return m_unterminatedLine; }

private:
	AseToken scan()
	{
		const qsizetype size = m_bytes.size();
		while (m_position < size && uchar(m_bytes.at(m_position)) <= 32) {
			if (m_bytes.at(m_position) == '\n') {
				++m_line;
			}
			++m_position;
		}
		AseToken token;
		token.line = m_line;
		if (m_position >= size) {
			return token;
		}
		if (m_bytes.at(m_position) == '"') {
			const qsizetype open = m_position + 1;
			qsizetype close = open;
			while (close < size && m_bytes.at(close) != '"' && m_bytes.at(close) != '\n' && m_bytes.at(close) != '\r') {
				++close;
			}
			if (close >= size || m_bytes.at(close) != '"') {
				m_unterminatedLine = m_line;
				m_position = size;
				return token;
			}
			token.kind = AseKind::String;
			token.start = open;
			token.length = close - open;
			m_position = close + 1;
			return token;
		}
		const qsizetype start = m_position;
		while (m_position < size && uchar(m_bytes.at(m_position)) > 32) {
			++m_position;
		}
		token.start = start;
		token.length = m_position - start;
		const char first = m_bytes.at(start);
		if (token.length == 1 && first == '{') {
			token.kind = AseKind::Open;
		} else if (token.length == 1 && first == '}') {
			token.kind = AseKind::Close;
		} else if (first == '*') {
			token.kind = AseKind::Keyword;
		} else {
			token.kind = AseKind::Word;
		}
		return token;
	}

	const QByteArray& m_bytes;
	qsizetype m_position = 0;
	int m_line = 1;
	AseToken m_peek;
	bool m_hasPeek = false;
	int m_unterminatedLine = 0;
};

struct AseUvw {
	double uOffset = 0.0;
	double vOffset = 0.0;
	double uTiling = 1.0;
	double vTiling = 1.0;
	double angle = 0.0;
	[[nodiscard]] bool isIdentity() const { return uOffset == 0.0 && vOffset == 0.0 && uTiling == 1.0 && vTiling == 1.0 && angle == 0.0; }
};

struct AseSubMaterial {
	QString name;
	QString bitmap;
	bool hasBitmap = false;
	int declaredIndex = 0;
};

struct AseMaterial {
	QString name;
	QString bitmap;
	bool hasBitmap = false;
	QVector<AseSubMaterial> subs;
	int nestedSubs = 0;
	// Doom 3's reading of the material block: the last diffuse bitmap and UVW
	// values anywhere in it, sub-materials included (ASE_ParseBracedBlock
	// hands every nested token to ASE_KeyMATERIAL).
	QString doom3Bitmap;
	bool doom3HasBitmap = false;
	AseUvw doom3Uvw;
};

struct AseFace {
	std::array<int, 3> v{};
	std::array<int, 3> tv{-1, -1, -1};
	int mtlId = 0;
};

struct AseTVert {
	double u = 0.0;
	double v = 0.0;
};

struct AseVertexNormal {
	int face = 0;
	int vertex = 0;
	ModelVec3 normal;
	int line = 0;
};

struct AseMeshData {
	bool present = false;
	qint64 numVertices = 0;
	qint64 numFaces = 0;
	qint64 numTVertices = 0;
	qint64 numTVFaces = 0;
	bool sawTVertList = false;
	QVector<ModelVec3> vertices;
	QVector<AseFace> faces;
	QVector<AseTVert> tverts;
	QVector<std::array<int, 3>> tfaces;
	bool normalsParsed = false;
	QVector<AseVertexNormal> vertexNormals;
};

struct AseObject {
	QString name;
	int line = 0;
	double rows[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
	AseMeshData mesh;
	int extraMeshes = 0;
	int materialRef = 0;
};

class AseParser {
public:
	AseParser(const QByteArray& bytes, ModelMesh* mesh, ModelWorkProgress& work)
		: m_lexer(bytes)
		, m_mesh(mesh)
		, m_work(work)
	{
	}

	bool parse()
	{
		while (true) {
			if (!m_work.step()) { return false; }
			const AseToken token = m_lexer.next();
			if (token.kind == AseKind::End) {
				if (m_lexer.unterminatedLine() > 0) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE text has an unterminated quoted string on line %1.").arg(m_lexer.unterminatedLine()));
				}
				return true;
			}
			if (token.kind == AseKind::Close) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE text closes a block that was never opened on line %1.").arg(token.line));
			}
			if (token.kind == AseKind::Open) {
				if (!skipBlockBody(token.line)) { return false; }
				continue;
			}
			if (token.kind != AseKind::Keyword) {
				continue;
			}
			const QByteArray keyword = m_lexer.text(token);
			if (keyword == "*3DSMAX_ASCIIEXPORT") {
				const QVector<AseToken> args = lineArgs(token.line);
				if (!args.isEmpty()) {
					version = m_lexer.text(args.constFirst()).toInt();
				}
			} else if (keyword == "*MATERIAL_LIST") {
				if (!openBlock(token)) { return false; }
				if (!parseMaterialList(1, token.line)) { return false; }
			} else if (keyword == "*GEOMOBJECT") {
				if (!openBlock(token)) { return false; }
				if (!parseGeomObject(1, token.line)) { return false; }
			} else if (keyword == "*GROUP") {
				if (!openBlock(token)) { return false; }
				if (!parseGroup(1, token.line)) { return false; }
			} else if (!skipStatement(token)) {
				return false;
			}
		}
	}

	int version = 0;
	QVector<AseMaterial> materials;
	QVector<AseObject> objects;

private:
	bool fail(const QString& message)
	{
		if (m_mesh->error.isEmpty()) {
			m_mesh->error = message;
		}
		return false;
	}

	QString tokenString(const AseToken& token) const { return QString::fromUtf8(m_lexer.text(token)); }

	bool number(const AseToken& token, double* value)
	{
		bool ok = false;
		const double parsed = m_lexer.text(token).toDouble(&ok);
		if (!ok || !std::isfinite(parsed) || (token.kind != AseKind::Word && token.kind != AseKind::String)) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE value \"%1\" on line %2 is not a number.").arg(tokenString(token)).arg(token.line));
		}
		*value = parsed;
		return true;
	}

	bool integer(const AseToken& token, qint64* value)
	{
		bool ok = false;
		QByteArray text = m_lexer.text(token);
		if (text.endsWith(':')) {
			text.chop(1);
		}
		const qint64 parsed = text.toLongLong(&ok);
		if (!ok || (token.kind != AseKind::Word && token.kind != AseKind::String)) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE value \"%1\" on line %2 is not a whole number.").arg(tokenString(token)).arg(token.line));
		}
		*value = parsed;
		return true;
	}

	bool argCount(const QVector<AseToken>& args, int needed, const AseToken& keyword)
	{
		if (args.size() < needed) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE %1 on line %2 is missing values.").arg(tokenString(keyword)).arg(keyword.line));
		}
		return true;
	}

	// The words after a keyword on its own line, up to any brace.
	QVector<AseToken> lineArgs(int line)
	{
		QVector<AseToken> args;
		while (true) {
			const AseToken& token = m_lexer.peek();
			if (token.kind == AseKind::End || token.kind == AseKind::Open || token.kind == AseKind::Close || token.line != line) {
				break;
			}
			args.append(m_lexer.next());
		}
		return args;
	}

	// Consumes the rest of a keyword's line and the '{' that opens its block.
	bool openBlock(const AseToken& keyword)
	{
		lineArgs(keyword.line);
		const AseToken token = m_lexer.next();
		if (token.kind != AseKind::Open) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE %1 on line %2 has no block.").arg(tokenString(keyword)).arg(keyword.line));
		}
		return true;
	}

	// After a '{' was consumed: skips to its matching '}'.
	bool skipBlockBody(int openLine)
	{
		int level = 1;
		while (level > 0) {
			if (!m_work.step()) { return false; }
			const AseToken token = m_lexer.next();
			if (token.kind == AseKind::End) {
				return unbalanced(openLine);
			}
			if (token.kind == AseKind::Open) {
				++level;
			} else if (token.kind == AseKind::Close) {
				--level;
			}
		}
		return true;
	}

	bool unbalanced(int openLine)
	{
		if (m_lexer.unterminatedLine() > 0) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE text has an unterminated quoted string on line %1.").arg(m_lexer.unterminatedLine()));
		}
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE text ends inside the block opened on line %1; its braces are unbalanced.").arg(openLine));
	}

	// A keyword this decoder does not use: its line, and its block when one follows.
	bool skipStatement(const AseToken& keyword)
	{
		lineArgs(keyword.line);
		if (m_lexer.peek().kind == AseKind::Open) {
			const AseToken open = m_lexer.next();
			return skipBlockBody(open.line);
		}
		return true;
	}

	// The body of a block whose '{' was consumed, calling `handler` for each
	// keyword; stray words and nested anonymous blocks are skipped.
	template<typename Handler>
	bool parseBody(int depth, int openLine, Handler handler)
	{
		if (depth > kMaxAseDepth) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE blocks are nested more than %1 deep.").arg(kMaxAseDepth));
		}
		while (true) {
			if (!m_work.step()) { return false; }
			const AseToken token = m_lexer.next();
			switch (token.kind) {
			case AseKind::End:
				return unbalanced(openLine);
			case AseKind::Close:
				return true;
			case AseKind::Open:
				if (!skipBlockBody(token.line)) { return false; }
				break;
			case AseKind::Keyword:
				if (!handler(token, m_lexer.text(token))) { return false; }
				break;
			case AseKind::String:
			case AseKind::Word:
				break;
			}
		}
	}

	bool parseGroup(int depth, int openLine)
	{
		return parseBody(depth, openLine, [&](const AseToken& token, const QByteArray& keyword) {
			if (keyword == "*GEOMOBJECT") {
				return openBlock(token) && parseGeomObject(depth + 1, token.line);
			}
			if (keyword == "*GROUP") {
				return openBlock(token) && parseGroup(depth + 1, token.line);
			}
			return skipStatement(token);
		});
	}

	bool parseMaterialList(int depth, int openLine)
	{
		return parseBody(depth, openLine, [&](const AseToken& token, const QByteArray& keyword) {
			if (keyword != "*MATERIAL") {
				return skipStatement(token);
			}
			if (materials.size() >= kMaxSurfaces) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE material list holds more materials than can be decoded."));
			}
			if (!openBlock(token)) { return false; }
			AseMaterial material;
			if (!parseMaterial(&material, depth + 1, token.line)) { return false; }
			materials.append(material);
			return true;
		});
	}

	bool parseMaterial(AseMaterial* material, int depth, int openLine)
	{
		return parseBody(depth, openLine, [&](const AseToken& token, const QByteArray& keyword) {
			if (keyword == "*MATERIAL_NAME") {
				const QVector<AseToken> args = lineArgs(token.line);
				if (!args.isEmpty()) {
					material->name = tokenString(args.constFirst());
				}
				return true;
			}
			if (keyword == "*MAP_DIFFUSE") {
				return openBlock(token) && parseDiffuse(&material->bitmap, &material->hasBitmap, material, depth + 1, token.line);
			}
			if (keyword == "*SUBMATERIAL") {
				const QVector<AseToken> args = lineArgs(token.line);
				AseSubMaterial sub;
				if (!args.isEmpty()) {
					qint64 index = 0;
					if (!integer(args.constFirst(), &index)) { return false; }
					sub.declaredIndex = int(std::clamp<qint64>(index, -1, kMaxSkins));
				}
				if (!openBlock(token)) { return false; }
				if (!parseSubMaterial(&sub, material, depth + 1, token.line)) { return false; }
				if (material->subs.size() >= kMaxSkins) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "An ASE material holds more sub-materials than can be decoded."));
				}
				material->subs.append(sub);
				return true;
			}
			return skipStatement(token);
		});
	}

	// `sub` is null for sub-materials nested below the first level: they are
	// not split out, but Doom 3 still sees their bitmaps.
	bool parseSubMaterial(AseSubMaterial* sub, AseMaterial* top, int depth, int openLine)
	{
		return parseBody(depth, openLine, [&](const AseToken& token, const QByteArray& keyword) {
			if (keyword == "*MATERIAL_NAME") {
				const QVector<AseToken> args = lineArgs(token.line);
				if (sub && !args.isEmpty()) {
					sub->name = tokenString(args.constFirst());
				}
				return true;
			}
			if (keyword == "*MAP_DIFFUSE") {
				if (!openBlock(token)) { return false; }
				if (sub) {
					return parseDiffuse(&sub->bitmap, &sub->hasBitmap, top, depth + 1, token.line);
				}
				return parseDiffuse(nullptr, nullptr, top, depth + 1, token.line);
			}
			if (keyword == "*SUBMATERIAL") {
				++top->nestedSubs;
				return openBlock(token) && parseSubMaterial(nullptr, top, depth + 1, token.line);
			}
			return skipStatement(token);
		});
	}

	bool parseDiffuse(QString* bitmap, bool* hasBitmap, AseMaterial* top, int depth, int openLine)
	{
		return parseBody(depth, openLine, [&](const AseToken& token, const QByteArray& keyword) {
			const QVector<AseToken> args = lineArgs(token.line);
			if (keyword == "*BITMAP") {
				const QString value = args.isEmpty() ? QString() : tokenString(args.constFirst());
				if (bitmap) {
					*bitmap = value;
					*hasBitmap = true;
				}
				top->doom3Bitmap = value;
				top->doom3HasBitmap = true;
				return true;
			}
			double* field = nullptr;
			if (keyword == "*UVW_U_OFFSET") {
				field = &top->doom3Uvw.uOffset;
			} else if (keyword == "*UVW_V_OFFSET") {
				field = &top->doom3Uvw.vOffset;
			} else if (keyword == "*UVW_U_TILING") {
				field = &top->doom3Uvw.uTiling;
			} else if (keyword == "*UVW_V_TILING") {
				field = &top->doom3Uvw.vTiling;
			} else if (keyword == "*UVW_ANGLE") {
				field = &top->doom3Uvw.angle;
			}
			if (field) {
				return argCount(args, 1, token) && number(args.constFirst(), field);
			}
			if (m_lexer.peek().kind == AseKind::Open) {
				const AseToken open = m_lexer.next();
				return skipBlockBody(open.line);
			}
			return true;
		});
	}

	bool parseGeomObject(int depth, int openLine)
	{
		if (objects.size() >= kMaxSurfaces) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE file holds more objects than can be decoded."));
		}
		AseObject object;
		object.line = openLine;
		const bool ok = parseBody(depth, openLine, [&](const AseToken& token, const QByteArray& keyword) {
			if (keyword == "*NODE_NAME") {
				const QVector<AseToken> args = lineArgs(token.line);
				if (object.name.isEmpty() && !args.isEmpty()) {
					object.name = tokenString(args.constFirst());
				}
				return true;
			}
			if (keyword == "*NODE_TM") {
				return openBlock(token) && parseBody(depth + 1, token.line, [&](const AseToken& row, const QByteArray& rowKeyword) {
					int index = -1;
					if (rowKeyword == "*TM_ROW0") {
						index = 0;
					} else if (rowKeyword == "*TM_ROW1") {
						index = 1;
					} else if (rowKeyword == "*TM_ROW2") {
						index = 2;
					}
					if (index < 0) {
						return skipStatement(row);
					}
					const QVector<AseToken> args = lineArgs(row.line);
					if (!argCount(args, 3, row)) { return false; }
					for (int column = 0; column < 3; ++column) {
						if (!number(args.at(column), &object.rows[index][column])) { return false; }
					}
					return true;
				});
			}
			if (keyword == "*MESH") {
				if (object.mesh.present) {
					++object.extraMeshes;
					return skipStatement(token);
				}
				object.mesh.present = true;
				return openBlock(token) && parseMesh(&object.mesh, depth + 1, token.line);
			}
			if (keyword == "*MESH_ANIMATION") {
				return openBlock(token) && parseBody(depth + 1, token.line, [&](const AseToken& inner, const QByteArray& innerKeyword) {
					if (innerKeyword == "*MESH") {
						++object.extraMeshes;
					}
					return skipStatement(inner);
				});
			}
			if (keyword == "*MATERIAL_REF") {
				const QVector<AseToken> args = lineArgs(token.line);
				qint64 reference = 0;
				if (!argCount(args, 1, token) || !integer(args.constFirst(), &reference)) { return false; }
				object.materialRef = int(std::clamp<qint64>(reference, -1, kMaxSurfaces + 1));
				return true;
			}
			return skipStatement(token);
		});
		if (!ok) {
			return false;
		}
		objects.append(object);
		return true;
	}

	bool count(const AseToken& token, qint64 limit, qint64* value)
	{
		const QVector<AseToken> args = lineArgs(token.line);
		if (!argCount(args, 1, token) || !integer(args.constFirst(), value)) {
			return false;
		}
		if (*value < 0 || *value > limit) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE %1 on line %2 declares %3, outside the supported range.")
				.arg(tokenString(token)).arg(token.line).arg(*value));
		}
		return true;
	}

	bool parseMesh(AseMeshData* data, int depth, int openLine)
	{
		return parseBody(depth, openLine, [&](const AseToken& token, const QByteArray& keyword) {
			if (keyword == "*MESH_NUMVERTEX") {
				return count(token, kMaxSurfaceVertices, &data->numVertices);
			}
			if (keyword == "*MESH_NUMFACES") {
				return count(token, kMaxSurfaceTriangles, &data->numFaces);
			}
			if (keyword == "*MESH_NUMTVERTEX") {
				return count(token, kMaxSurfaceVertices, &data->numTVertices);
			}
			if (keyword == "*MESH_NUMTVFACES") {
				if (!count(token, kMaxSurfaceTriangles, &data->numTVFaces)) { return false; }
				if (data->numTVFaces != data->numFaces) {
					// ASE_KeyMESH: "MESH_NUMTVFACES != MESH_NUMFACES" is an error in Doom 3.
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE mesh on line %1 declares %2 texture faces for %3 faces.")
						.arg(token.line).arg(data->numTVFaces).arg(data->numFaces));
				}
				return true;
			}
			if (keyword == "*MESH_VERTEX_LIST") {
				return openBlock(token) && parseBody(depth + 1, token.line, [&](const AseToken& entry, const QByteArray& entryKeyword) {
					if (entryKeyword != "*MESH_VERTEX") {
						return skipStatement(entry);
					}
					const QVector<AseToken> args = lineArgs(entry.line);
					if (!argCount(args, 4, entry)) { return false; }
					if (data->vertices.size() >= data->numVertices) {
						return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE vertex list on line %1 holds more vertices than MESH_NUMVERTEX declares.").arg(entry.line));
					}
					double xyz[3] = {0.0, 0.0, 0.0};
					for (int axis = 0; axis < 3; ++axis) {
						if (!number(args.at(axis + 1), &xyz[axis])) { return false; }
					}
					data->vertices.append(ModelVec3{float(xyz[0]), float(xyz[1]), float(xyz[2])});
					return true;
				});
			}
			if (keyword == "*MESH_FACE_LIST") {
				return openBlock(token) && parseBody(depth + 1, token.line, [&](const AseToken& entry, const QByteArray& entryKeyword) {
					if (entryKeyword != "*MESH_FACE") {
						return skipStatement(entry);
					}
					return parseFace(data, entry);
				});
			}
			if (keyword == "*MESH_TVERTLIST") {
				data->sawTVertList = true;
				return openBlock(token) && parseBody(depth + 1, token.line, [&](const AseToken& entry, const QByteArray& entryKeyword) {
					if (entryKeyword != "*MESH_TVERT") {
						return skipStatement(entry);
					}
					const QVector<AseToken> args = lineArgs(entry.line);
					if (!argCount(args, 3, entry)) { return false; }
					if (data->tverts.size() >= data->numTVertices) {
						return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE texture vertex list on line %1 holds more entries than MESH_NUMTVERTEX declares.").arg(entry.line));
					}
					AseTVert tvert;
					if (!number(args.at(1), &tvert.u) || !number(args.at(2), &tvert.v)) { return false; }
					data->tverts.append(tvert);
					return true;
				});
			}
			if (keyword == "*MESH_TFACELIST") {
				return openBlock(token) && parseBody(depth + 1, token.line, [&](const AseToken& entry, const QByteArray& entryKeyword) {
					if (entryKeyword != "*MESH_TFACE") {
						return skipStatement(entry);
					}
					const QVector<AseToken> args = lineArgs(entry.line);
					if (!argCount(args, 4, entry)) { return false; }
					if (data->tfaces.size() >= data->numTVFaces) {
						return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE texture face list on line %1 holds more entries than MESH_NUMTVFACES declares.").arg(entry.line));
					}
					std::array<int, 3> corners{};
					for (int corner = 0; corner < 3; ++corner) {
						qint64 value = 0;
						if (!integer(args.at(corner + 1), &value)) { return false; }
						corners[corner] = int(std::clamp<qint64>(value, -1, kMaxSurfaceVertices + 1));
					}
					data->tfaces.append(corners);
					return true;
				});
			}
			if (keyword == "*MESH_NORMALS") {
				data->normalsParsed = true;
				int currentFace = -1;
				return openBlock(token) && parseBody(depth + 1, token.line, [&](const AseToken& entry, const QByteArray& entryKeyword) {
					const bool faceNormal = entryKeyword == "*MESH_FACENORMAL";
					if (!faceNormal && entryKeyword != "*MESH_VERTEXNORMAL") {
						return skipStatement(entry);
					}
					const QVector<AseToken> args = lineArgs(entry.line);
					if (!argCount(args, 4, entry)) { return false; }
					qint64 index = 0;
					double n[3] = {0.0, 0.0, 0.0};
					if (!integer(args.constFirst(), &index)) { return false; }
					for (int axis = 0; axis < 3; ++axis) {
						if (!number(args.at(axis + 1), &n[axis])) { return false; }
					}
					if (faceNormal) {
						if (index < 0 || index >= kMaxSurfaceTriangles) {
							return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE face normal on line %1 names face %2, outside the supported range.").arg(entry.line).arg(index));
						}
						currentFace = int(index);
						return true;
					}
					if (currentFace < 0) {
						return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE vertex normal on line %1 comes before any face normal.").arg(entry.line));
					}
					if (data->vertexNormals.size() >= 3LL * kMaxSurfaceTriangles) {
						return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE mesh holds more vertex normals than can be decoded."));
					}
					AseVertexNormal record;
					record.face = currentFace;
					record.vertex = int(std::clamp<qint64>(index, -1, kMaxSurfaceVertices + 1));
					record.normal = ModelVec3{float(n[0]), float(n[1]), float(n[2])};
					record.line = entry.line;
					data->vertexNormals.append(record);
					return true;
				});
			}
			return skipStatement(token);
		});
	}

	// *MESH_FACE n: A: a B: b C: c AB: .. BC: .. CA: .. *MESH_SMOOTHING s *MESH_MTLID m
	bool parseFace(AseMeshData* data, const AseToken& entry)
	{
		const QVector<AseToken> args = lineArgs(entry.line);
		if (data->faces.size() >= data->numFaces) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE face list on line %1 holds more faces than MESH_NUMFACES declares.").arg(entry.line));
		}
		AseFace face;
		int found = 0;
		static const char* const kLabels[3] = {"A:", "B:", "C:"};
		for (int index = 0; index < args.size(); ++index) {
			const QByteArray text = m_lexer.text(args.at(index));
			if (text == "*MESH_MTLID" && index + 1 < args.size()) {
				qint64 value = 0;
				if (!integer(args.at(index + 1), &value)) { return false; }
				face.mtlId = int(std::clamp<qint64>(value, -1, kMaxSkins + 1));
				++index;
				continue;
			}
			for (int corner = 0; corner < 3; ++corner) {
				if (!text.startsWith(kLabels[corner])) {
					continue;
				}
				AseToken valueToken = args.at(index);
				if (text.size() > 2) {
					valueToken.start += 2;
					valueToken.length -= 2;
				} else if (index + 1 < args.size()) {
					valueToken = args.at(++index);
				} else {
					break;
				}
				qint64 value = 0;
				if (!integer(valueToken, &value)) { return false; }
				face.v[corner] = int(std::clamp<qint64>(value, -1, kMaxSurfaceVertices + 1));
				found |= 1 << corner;
				break;
			}
		}
		if (found != 7) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The ASE face on line %1 does not name its A, B and C corners.").arg(entry.line));
		}
		data->faces.append(face);
		return true;
	}

	AseLexer m_lexer;
	ModelMesh* m_mesh;
	ModelWorkProgress& m_work;
};

QString canonicalMaterialName(const QString& name)
{
	QString canonical = name;
	canonical.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const qsizetype dot = canonical.lastIndexOf(QLatin1Char('.'));
	if (dot >= 0) {
		canonical.truncate(dot);
	}
	return canonical;
}

// The first "/name/" folder in a '/' path, as OSPathToRelativePath matches
// BASE_GAMEDIR: the name must have a separator on both sides.
qsizetype findFolder(const QString& path, const QString& name)
{
	qsizetype from = 0;
	while (true) {
		const qsizetype found = path.indexOf(name, from);
		if (found < 0) {
			return -1;
		}
		const qsizetype after = found + name.size();
		if (found > 0 && path.at(found - 1) == QLatin1Char('/') && after < path.size() && path.at(after) == QLatin1Char('/')) {
			return found;
		}
		from = found + 1;
	}
}

struct Doom3BitmapName {
	QString material;
	bool resolved = false;
	bool cutAtSpace = false;
};

// The material name Doom 3 makes of a *BITMAP path (ASE_KeyMAP_DIFFUSE,
// OSPathToRelativePath, MakeNameCanonical). Unresolved paths (no "base"
// folder) leave Doom 3 with an empty name; the studio keeps the path itself,
// without a drive or leading slashes, so the surface still names something.
Doom3BitmapName doom3MaterialFromBitmap(const QString& bitmap)
{
	Doom3BitmapName result;
	QString path = bitmap;
	for (qsizetype index = 0; index < path.size(); ++index) {
		if (path.at(index).unicode() <= 32) {
			path.truncate(index);
			result.cutAtSpace = true;
			break;
		}
	}
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	qsizetype base = findFolder(path, QStringLiteral("base"));
	if (base < 0) {
		base = findFolder(path, QStringLiteral("q4base"));
	}
	QString relative;
	if (base >= 0) {
		const qsizetype pk4 = path.indexOf(QStringLiteral(".pk4/"), base);
		const qsizetype slash = pk4 >= 0 ? pk4 + 4 : path.indexOf(QLatin1Char('/'), base);
		relative = path.mid(slash + 1);
		result.resolved = true;
	} else {
		relative = path;
		if (relative.size() >= 2 && relative.at(1) == QLatin1Char(':')) {
			relative.remove(0, 2);
		}
		while (relative.startsWith(QLatin1Char('/'))) {
			relative.remove(0, 1);
		}
	}
	result.material = canonicalMaterialName(relative);
	return result;
}

QString pathExtensionless(const QString& path)
{
	const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
	const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
	return dot > slash ? path.left(dot) : path;
}

// q3map2's shader name for an ASE material (model.cpp, AssModelMesh).
QString q3map2ShaderName(const QString& materialName, const QString& bitmap, const QString& modelDirectory)
{
	const bool looksLikeShader = materialName.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)
		|| materialName.startsWith(QStringLiteral("textures\\"), Qt::CaseInsensitive)
		|| materialName.startsWith(QStringLiteral("models/"), Qt::CaseInsensitive)
		|| materialName.startsWith(QStringLiteral("models\\"), Qt::CaseInsensitive);
	QString shader = (!bitmap.isEmpty() && !looksLikeShader) ? bitmap : materialName;
	shader.replace(QLatin1Char('\\'), QLatin1Char('/'));
	shader = pathExtensionless(shader);
	if (!shader.contains(QLatin1Char('/'))) {
		return joinPath(modelDirectory, shader);
	}
	if (shader.startsWith(QLatin1Char('/')) || (shader.size() > 1 && shader.at(1) == QLatin1Char(':')) || shader.contains(QStringLiteral(".."))) {
		qsizetype found = shader.indexOf(QStringLiteral("/models/"), 0, Qt::CaseInsensitive);
		if (found < 0) {
			found = shader.indexOf(QStringLiteral("/textures/"), 0, Qt::CaseInsensitive);
		}
		if (found >= 0) {
			return shader.mid(found + 1);
		}
		return joinPath(modelDirectory, shader.mid(shader.lastIndexOf(QLatin1Char('/')) + 1));
	}
	return shader;
}

ModelVec3 crossOf(const ModelVec3& a, const ModelVec3& b, const ModelVec3& c)
{
	const double ux = double(b.x) - a.x;
	const double uy = double(b.y) - a.y;
	const double uz = double(b.z) - a.z;
	const double vx = double(c.x) - a.x;
	const double vy = double(c.y) - a.y;
	const double vz = double(c.z) - a.z;
	return ModelVec3{float((uy * vz) - (uz * vy)), float((uz * vx) - (ux * vz)), float((ux * vy) - (uy * vx))};
}

float dotF(const ModelVec3& a, const ModelVec3& b)
{
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

ModelVec3 normalised(const ModelVec3& v)
{
	const double length = std::sqrt((double(v.x) * v.x) + (double(v.y) * v.y) + (double(v.z) * v.z));
	if (!(length > 0.0) || !std::isfinite(length)) {
		return {};
	}
	return ModelVec3{float(v.x / length), float(v.y / length), float(v.z / length)};
}

void fillStaticFrame(ModelMesh* mesh)
{
	ModelFrameInfo frame;
	frame.index = 0;
	frame.name = QStringLiteral("frame0");
	bool first = true;
	float radius = 0.0f;
	for (const ModelSurface& surface : mesh->surfaces) {
		for (const ModelVec3& p : surface.frames.constFirst().positions) {
			if (first) {
				frame.mins = p;
				frame.maxs = p;
				first = false;
			}
			frame.mins.x = std::min(frame.mins.x, p.x);
			frame.mins.y = std::min(frame.mins.y, p.y);
			frame.mins.z = std::min(frame.mins.z, p.z);
			frame.maxs.x = std::max(frame.maxs.x, p.x);
			frame.maxs.y = std::max(frame.maxs.y, p.y);
			frame.maxs.z = std::max(frame.maxs.z, p.z);
			radius = std::max(radius, std::sqrt(dotF(p, p)));
		}
	}
	frame.radius = radius;
	mesh->frames.append(frame);
}

struct AseVertexKey {
	int vertex = 0;
	int tvertex = 0;
	bool operator==(const AseVertexKey& other) const { return vertex == other.vertex && tvertex == other.tvertex; }
};

size_t qHash(const AseVertexKey& key, size_t seed = 0)
{
	return qHashMulti(seed, key.vertex, key.tvertex);
}

struct AseSurfaceBuild {
	ModelSurface surface;
	QHash<AseVertexKey, int> firstVertex;
	QVector<int> nextVertex;
	QVector<ModelVec3> normalSums;
	int subIndex = -1;
};

// Builds the object's surfaces; false with mesh->error set on a bad index.
bool buildObject(const AseParser& parser, const AseObject& object, int objectIndex, const QString& modelDirectory, QSet<QString>* usedNames,
	ModelMesh* mesh, ModelWorkProgress& work, qint64* totalVertices)
{
	const AseMeshData& data = object.mesh;
	const QString objectName = object.name.isEmpty() ? QStringLiteral("object%1").arg(objectIndex) : object.name;
	if (data.vertices.size() != data.numVertices) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "ASE object %1 lists %2 of its %3 vertices.").arg(objectName).arg(data.vertices.size()).arg(data.numVertices);
		return false;
	}
	if (data.faces.size() != data.numFaces) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "ASE object %1 lists %2 of its %3 faces.").arg(objectName).arg(data.faces.size()).arg(data.numFaces);
		return false;
	}
	if (data.sawTVertList && data.tverts.size() != data.numTVertices) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "ASE object %1 lists %2 of its %3 texture vertices.").arg(objectName).arg(data.tverts.size()).arg(data.numTVertices);
		return false;
	}
	// ConvertASEToModelSurfaces: texture coordinates are used only when every
	// face has a texture face and there are texture vertices.
	const bool useUvs = data.numFaces > 0 && data.numTVFaces == data.numFaces && data.numTVertices != 0;
	if (useUvs && data.tfaces.size() != data.numTVFaces) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "ASE object %1 lists %2 of its %3 texture faces.").arg(objectName).arg(data.tfaces.size()).arg(data.numTVFaces);
		return false;
	}
	if (useUvs && data.tverts.size() != data.numTVertices) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "ASE object %1 lists %2 of its %3 texture vertices.").arg(objectName).arg(data.tverts.size()).arg(data.numTVertices);
		return false;
	}
	for (int face = 0; face < data.faces.size(); ++face) {
		if (!work.step()) { return false; }
		for (int corner = 0; corner < 3; ++corner) {
			const int vertex = data.faces.at(face).v.at(corner);
			if (vertex < 0 || vertex >= data.vertices.size()) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Face %1 of ASE object %2 refers to vertex %3, beyond its %4 vertices.")
					.arg(face).arg(objectName).arg(vertex).arg(data.vertices.size());
				return false;
			}
			if (useUvs) {
				const int tvertex = data.tfaces.at(face).at(corner);
				if (tvertex < 0 || tvertex >= data.tverts.size()) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Face %1 of ASE object %2 refers to texture vertex %3, beyond its %4 texture vertices.")
						.arg(face).arg(objectName).arg(tvertex).arg(data.tverts.size());
					return false;
				}
			}
		}
	}

	// Corner normals, turned by the NODE_TM rows (ASE_KeyMESH_NORMALS).
	QVector<std::array<ModelVec3, 3>> cornerNormals;
	QVector<std::array<bool, 3>> cornerHasNormal;
	if (data.normalsParsed) {
		cornerNormals.resize(data.faces.size());
		cornerHasNormal.fill(std::array<bool, 3>{false, false, false}, data.faces.size());
		for (const AseVertexNormal& record : data.vertexNormals) {
			if (!work.step()) { return false; }
			if (record.face >= data.faces.size()) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The ASE normal on line %1 names face %2, beyond the %3 faces of object %4.")
					.arg(record.line).arg(record.face).arg(data.faces.size()).arg(objectName);
				return false;
			}
			const AseFace& face = data.faces.at(record.face);
			int corner = 0;
			while (corner < 3 && face.v.at(corner) != record.vertex) {
				++corner;
			}
			if (corner == 3) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The ASE normal on line %1 is for vertex %2, which face %3 of object %4 does not use.")
					.arg(record.line).arg(record.vertex).arg(record.face).arg(objectName);
				return false;
			}
			const ModelVec3& n = record.normal;
			ModelVec3 turned;
			turned.x = float((n.x * object.rows[0][0]) + (n.y * object.rows[1][0]) + (n.z * object.rows[2][0]));
			turned.y = float((n.x * object.rows[0][1]) + (n.y * object.rows[1][1]) + (n.z * object.rows[2][1]));
			turned.z = float((n.x * object.rows[0][2]) + (n.y * object.rows[1][2]) + (n.z * object.rows[2][2]));
			turned = normalised(turned);
			if (dotF(turned, turned) > 0.0f) {
				cornerNormals[record.face][corner] = turned;
				cornerHasNormal[record.face][corner] = true;
			}
		}
	}

	// Material and sub-material per face. Doom 3 indexes materials in list
	// order; picomodel picks the sub-material whose index matches MESH_MTLID,
	// else sub-material 0. The studio falls back to the first listed one.
	const AseMaterial* material = nullptr;
	if (!parser.materials.isEmpty()) {
		if (object.materialRef < 0 || object.materialRef >= parser.materials.size()) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "ASE object %1 refers to material %2, beyond the %3 material(s).")
				.arg(objectName).arg(object.materialRef).arg(parser.materials.size());
			return false;
		}
		material = &parser.materials.at(object.materialRef);
	}
	const auto subFor = [&](int mtlId) -> int {
		if (!material || material->subs.isEmpty()) {
			return -1;
		}
		int zero = -1;
		for (int index = 0; index < material->subs.size(); ++index) {
			if (material->subs.at(index).declaredIndex == mtlId) {
				return index;
			}
			if (zero < 0 && material->subs.at(index).declaredIndex == 0) {
				zero = index;
			}
		}
		return zero >= 0 ? zero : 0;
	};

	const AseUvw uvw = material ? material->doom3Uvw : AseUvw{};
	const double uOffset = -uvw.uOffset;
	const double vOffset = uvw.vOffset;
	const double textureSin = std::sin(uvw.angle);
	const double textureCos = std::cos(uvw.angle);

	QVector<AseSurfaceBuild> builds;
	QHash<int, int> buildForSub;
	int degenerate = 0;
	for (int faceIndex = 0; faceIndex < data.faces.size(); ++faceIndex) {
		if (!work.step()) { return false; }
		const AseFace& face = data.faces.at(faceIndex);
		const ModelVec3& a = data.vertices.at(face.v[0]);
		const ModelVec3& b = data.vertices.at(face.v[1]);
		const ModelVec3& c = data.vertices.at(face.v[2]);
		const ModelVec3 faceCross = crossOf(a, b, c);
		const ModelVec3 faceNormal = normalised(faceCross);
		if (face.v[0] == face.v[1] || face.v[1] == face.v[2] || face.v[2] == face.v[0] || dotF(faceNormal, faceNormal) <= 0.0f) {
			++degenerate;
			continue;
		}
		const int sub = subFor(face.mtlId);
		auto found = buildForSub.constFind(sub);
		if (found == buildForSub.constEnd()) {
			if (mesh->surfaces.size() + builds.size() >= kMaxSurfaces) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The ASE file has more surfaces than can be decoded.");
				return false;
			}
			AseSurfaceBuild build;
			build.subIndex = sub;
			build.surface.frames.append(ModelFrameGeometry{});
			builds.append(build);
			found = buildForSub.insert(sub, int(builds.size()) - 1);
		}
		AseSurfaceBuild& build = builds[found.value()];
		ModelSurface& surface = build.surface;
		ModelFrameGeometry& geometry = surface.frames.first();
		std::array<int, 3> corners{};
		for (int corner = 0; corner < 3; ++corner) {
			const int vertex = face.v[corner];
			const int tvertex = useUvs ? data.tfaces.at(faceIndex).at(corner) : -1;
			ModelVec3 normal = faceNormal;
			const bool explicitNormal = data.normalsParsed && cornerHasNormal.at(faceIndex).at(corner);
			if (explicitNormal) {
				normal = cornerNormals.at(faceIndex).at(corner);
			}
			const AseVertexKey key{vertex, tvertex};
			int chosen = -1;
			int last = -1;
			const auto first = build.firstVertex.constFind(key);
			if (first != build.firstVertex.constEnd()) {
				for (int candidate = first.value(); candidate >= 0; candidate = build.nextVertex.at(candidate)) {
					if (!data.normalsParsed || dotF(geometry.normals.at(candidate), normal) > kNormalMergeDot) {
						chosen = candidate;
						break;
					}
					last = candidate;
				}
			}
			if (chosen < 0) {
				if (surface.vertexCount >= kMaxSurfaceVertices || *totalVertices >= kMaxVertexSlots) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The ASE file has more vertices than can be decoded.");
					return false;
				}
				chosen = surface.vertexCount++;
				++*totalVertices;
				geometry.positions.append(data.vertices.at(vertex));
				geometry.normals.append(normal);
				build.normalSums.append(ModelVec3{});
				ModelTexCoord uv;
				if (useUvs) {
					const AseTVert& tvert = data.tverts.at(tvertex);
					const double u = (tvert.u * uvw.uTiling) + uOffset;
					const double v = ((1.0 - tvert.v) * uvw.vTiling) + vOffset;
					uv.u = float((u * textureCos) + (v * textureSin));
					uv.v = float((u * -textureSin) + (v * textureCos));
				}
				surface.texCoords.append(uv);
				build.nextVertex.append(-1);
				if (last >= 0) {
					build.nextVertex[last] = chosen;
				} else if (first == build.firstVertex.constEnd()) {
					build.firstVertex.insert(key, chosen);
				}
			}
			if (!explicitNormal) {
				ModelVec3& sum = build.normalSums[chosen];
				sum.x += faceCross.x;
				sum.y += faceCross.y;
				sum.z += faceCross.z;
			}
			corners[corner] = chosen;
		}
		if (surface.triangles.size() >= kMaxSurfaceTriangles) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The ASE file has more triangles than can be decoded.");
			return false;
		}
		// 3ds Max's A B C is already counter-clockwise from the front.
		surface.triangles.append(ModelTriangle{corners[0], corners[1], corners[2]});
	}
	if (degenerate > 0) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 degenerate face(s) of ASE object %2 were skipped.").arg(degenerate).arg(objectName);
	}
	if (object.extraMeshes > 0) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "ASE object %1 carries %2 animation mesh sample(s); only its first mesh is decoded, as Doom 3 draws it.")
			.arg(objectName).arg(object.extraMeshes);
	}
	if (material && !material->subs.isEmpty()) {
		const Doom3BitmapName whole = doom3MaterialFromBitmap(material->doom3Bitmap);
		if (material->doom3HasBitmap && !whole.material.isEmpty()) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh",
				"ASE object %1 uses the multi-material %2: Doom 3 draws the whole object with %3, the last diffuse bitmap in that material, while q3map2 splits it by sub-material as shown here.")
				.arg(objectName, material->name, whole.material);
		} else {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh",
				"ASE object %1 uses the multi-material %2: Doom 3 draws the whole object with its default material, while q3map2 splits it by sub-material as shown here.")
				.arg(objectName, material->name);
		}
		if (material->nestedSubs > 0) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Material %1 nests %2 sub-material(s) below its first level; they are not split out.")
				.arg(material->name).arg(material->nestedSubs);
		}
	}
	if (material && !uvw.isIdentity()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Material %1 offsets, tiles or turns its UVs; Doom 3 applies this as shown, q3map2 does not.")
			.arg(material->name);
	}

	for (AseSurfaceBuild& build : builds) {
		if (!work.step()) { return false; }
		ModelSurface& surface = build.surface;
		ModelFrameGeometry& geometry = surface.frames.first();
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			const ModelVec3 summed = normalised(build.normalSums.at(vertex));
			if (dotF(summed, summed) > 0.0f) {
				geometry.normals[vertex] = summed;
			}
		}
		QString name = objectName;
		QString materialName;
		QString bitmap;
		bool hasBitmap = false;
		if (material && build.subIndex >= 0) {
			const AseSubMaterial& sub = material->subs.at(build.subIndex);
			name = QStringLiteral("%1_%2").arg(objectName).arg(sub.declaredIndex);
			materialName = sub.name;
			bitmap = sub.bitmap;
			hasBitmap = sub.hasBitmap;
		} else if (material) {
			materialName = material->name;
			bitmap = material->bitmap;
			hasBitmap = material->hasBitmap;
		}
		QString unique = name.left(120);
		for (int suffix = 2; usedNames->contains(unique); ++suffix) {
			unique = QStringLiteral("%1_%2").arg(name.left(112)).arg(suffix);
		}
		usedNames->insert(unique);
		surface.name = unique;
		surface.index = int(mesh->surfaces.size());

		QString skin;
		if (hasBitmap && !bitmap.isEmpty()) {
			const Doom3BitmapName doom3 = doom3MaterialFromBitmap(bitmap);
			skin = doom3.material;
			if (doom3.cutAtSpace) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The bitmap path %1 contains a space; Doom 3 reads it only up to the space, as %2.")
					.arg(bitmap, skin);
			}
			if (!doom3.resolved) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh",
					"The bitmap path %1 has no base folder, so Doom 3 cannot name a material from it and draws surface %2 with its default material.")
					.arg(bitmap, surface.name);
			}
		} else if (material) {
			skin = canonicalMaterialName(materialName);
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Material %1 has no diffuse bitmap; Doom 3 draws surface %2 with its default material.")
				.arg(materialName, surface.name);
		}
		if (!skin.isEmpty()) {
			surface.skinPaths.append(skin);
			if (!mesh->skinPaths.contains(skin)) {
				mesh->skinPaths.append(skin);
			}
			const QString q3map2 = q3map2ShaderName(materialName, (hasBitmap && bitmap.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0) ? bitmap : QString(), modelDirectory);
			if (q3map2 != skin) {
				mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "q3map2 reads surface %1 as shader %2.").arg(surface.name, q3map2);
			}
		}
		mesh->surfaces.append(surface);
	}
	return true;
}

} // namespace

void decodeAse(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (bytes.size() > kMaxTextBytes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The ASE file is larger than the %1 MiB text limit.").arg(kMaxTextBytes / (1024 * 1024));
		return;
	}
	AseParser parser(bytes, mesh, work);
	if (!parser.parse()) {
		if (mesh->error.isEmpty()) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The ASE file could not be read.");
		}
		return;
	}
	mesh->version = parser.version;
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "ASE objects: %1").arg(parser.objects.size());
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "ASE materials: %1").arg(parser.materials.size());
	if (parser.materials.isEmpty()) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The ASE file has no materials; Doom 3 draws it with its default material.");
	}
	const QString modelDirectory = pathDirectory(path);
	QSet<QString> usedNames;
	qint64 totalVertices = 0;
	int emptyObjects = 0;
	for (int index = 0; index < parser.objects.size(); ++index) {
		if (!work.step()) { return; }
		const AseObject& object = parser.objects.at(index);
		if (!object.mesh.present || object.mesh.numFaces == 0) {
			++emptyObjects;
			continue;
		}
		if (!buildObject(parser, object, index, modelDirectory, &usedNames, mesh, work, &totalVertices)) {
			return;
		}
	}
	if (emptyObjects > 0) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "%1 ASE object(s) have no faces.").arg(emptyObjects);
	}
	if (mesh->surfaces.isEmpty()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The ASE file holds no faces that can be drawn.");
		return;
	}
	fillStaticFrame(mesh);
	mesh->skinCount = int(mesh->skinPaths.size());
	mesh->geometryAvailable = true;
}

} // namespace model_formats

namespace {

// The shortest decimal text that reads back as exactly `value`.
QString floatText(float value)
{
	for (int precision = 6; precision <= 9; ++precision) {
		const QString text = QString::number(double(value), 'g', precision);
		if (float(text.toDouble()) == value) {
			return text;
		}
	}
	return QString::number(double(value), 'g', 9);
}

// The text of a TVERT v whose decode (1 - v, in double) is exactly `v`.
QString tvertVText(float v)
{
	const double target = 1.0 - double(v);
	for (int precision = 6; precision <= 17; ++precision) {
		const QString text = QString::number(target, 'g', precision);
		if (float(1.0 - text.toDouble()) == v) {
			return text;
		}
	}
	return QString::number(target, 'g', 17);
}

QString quoted(QString text)
{
	text.replace(QLatin1Char('"'), QLatin1Char('_'));
	text.replace(QLatin1Char('\n'), QLatin1Char(' '));
	text.replace(QLatin1Char('\r'), QLatin1Char(' '));
	return QLatin1Char('"') + text + QLatin1Char('"');
}

bool finiteVec(const ModelVec3& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

QByteArray exportModelAse(const ModelMesh& mesh, int frameIndex, QString* error, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	if (!work.check()) { return {}; }
	const auto failWith = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		return QByteArray();
	};
	int frameCount = int(mesh.frames.size());
	if (frameCount == 0) {
		for (const ModelSurface& surface : mesh.surfaces) {
			frameCount = std::max(frameCount, int(surface.frames.size()));
		}
	}
	if (frameIndex < 0 || frameIndex >= frameCount) {
		return failWith(QCoreApplication::translate("VibeStudioModelMesh", "The model has no frame %1 to export.").arg(frameIndex));
	}
	QVector<const ModelSurface*> surfaces;
	for (const ModelSurface& surface : mesh.surfaces) {
		if (surface.triangles.isEmpty() || surface.vertexCount <= 0 || frameIndex >= surface.frames.size()
			|| surface.frames.at(frameIndex).positions.size() != surface.vertexCount || surface.texCoords.size() != surface.vertexCount) {
			continue;
		}
		surfaces.append(&surface);
	}
	if (surfaces.isEmpty()) {
		return failWith(QCoreApplication::translate("VibeStudioModelMesh", "Frame %1 has no geometry to export as ASE.").arg(frameIndex));
	}
	if (surfaces.size() > model_formats::kMaxSurfaces) {
		return failWith(QCoreApplication::translate("VibeStudioModelMesh", "The model has more surfaces than an ASE export supports."));
	}

	QStringList nodeNames;
	QStringList materialNames;
	QSet<QString> usedNames;
	for (int index = 0; index < surfaces.size(); ++index) {
		const ModelSurface& surface = *surfaces.at(index);
		QString name = surface.name.isEmpty() ? QStringLiteral("surface%1").arg(index) : surface.name;
		QString unique = name;
		for (int suffix = 2; usedNames.contains(unique); ++suffix) {
			unique = QStringLiteral("%1_%2").arg(name).arg(suffix);
		}
		usedNames.insert(unique);
		nodeNames.append(unique);
		QString material;
		for (const QString& skin : surface.skinPaths) {
			if (!skin.trimmed().isEmpty()) {
				material = skin.trimmed();
				break;
			}
		}
		if (material.isEmpty()) {
			material = unique;
		}
		material.replace(QLatin1Char('\\'), QLatin1Char('/'));
		materialNames.append(material);
	}

	QString out;
	out.reserve(4096);
	const QLatin1Char newline('\n');
	out += QStringLiteral("*3DSMAX_ASCIIEXPORT\t200") + newline;
	out += QStringLiteral("*COMMENT \"VibeStudio ASCII scene export\"") + newline;
	out += QStringLiteral("*SCENE {") + newline;
	out += QStringLiteral("\t*SCENE_FIRSTFRAME 0") + newline;
	out += QStringLiteral("\t*SCENE_LASTFRAME 0") + newline;
	out += QStringLiteral("\t*SCENE_FRAMESPEED 30") + newline;
	out += QStringLiteral("\t*SCENE_TICKSPERFRAME 160") + newline;
	out += QStringLiteral("}") + newline;

	// Doom 3 names the material from the bitmap path after a "base" folder,
	// so the bitmap is written as /base/<skin>, with a .tga extension when the
	// skin has none (Doom 3 drops the text from the last '.'). *MATERIAL_NAME
	// holds the skin itself, which q3map2 uses when it looks like a shader
	// path ("models/..." or "textures/...").
	out += QStringLiteral("*MATERIAL_LIST {") + newline;
	out += QStringLiteral("\t*MATERIAL_COUNT %1").arg(surfaces.size()) + newline;
	for (int index = 0; index < surfaces.size(); ++index) {
		if (!work.step()) { return {}; }
		const QString& material = materialNames.at(index);
		const QString fileName = material.mid(material.lastIndexOf(QLatin1Char('/')) + 1);
		const QString bitmap = QStringLiteral("/base/") + material + (fileName.contains(QLatin1Char('.')) ? QString() : QStringLiteral(".tga"));
		out += QStringLiteral("\t*MATERIAL %1 {").arg(index) + newline;
		out += QStringLiteral("\t\t*MATERIAL_NAME ") + quoted(material) + newline;
		out += QStringLiteral("\t\t*MATERIAL_CLASS \"Standard\"") + newline;
		out += QStringLiteral("\t\t*MATERIAL_AMBIENT 0.5880\t0.5880\t0.5880") + newline;
		out += QStringLiteral("\t\t*MATERIAL_DIFFUSE 0.5880\t0.5880\t0.5880") + newline;
		out += QStringLiteral("\t\t*MATERIAL_SPECULAR 0.9000\t0.9000\t0.9000") + newline;
		out += QStringLiteral("\t\t*MATERIAL_SHINE 0.1000") + newline;
		out += QStringLiteral("\t\t*MATERIAL_SHINESTRENGTH 0.0000") + newline;
		out += QStringLiteral("\t\t*MATERIAL_TRANSPARENCY 0.0000") + newline;
		out += QStringLiteral("\t\t*MAP_DIFFUSE {") + newline;
		out += QStringLiteral("\t\t\t*MAP_NAME \"Map #%1\"").arg(index + 1) + newline;
		out += QStringLiteral("\t\t\t*MAP_CLASS \"Bitmap\"") + newline;
		out += QStringLiteral("\t\t\t*MAP_SUBNO 1") + newline;
		out += QStringLiteral("\t\t\t*MAP_AMOUNT 1.0000") + newline;
		out += QStringLiteral("\t\t\t*BITMAP ") + quoted(bitmap) + newline;
		out += QStringLiteral("\t\t\t*MAP_TYPE Screen") + newline;
		out += QStringLiteral("\t\t\t*UVW_U_OFFSET 0.0000") + newline;
		out += QStringLiteral("\t\t\t*UVW_V_OFFSET 0.0000") + newline;
		out += QStringLiteral("\t\t\t*UVW_U_TILING 1.0000") + newline;
		out += QStringLiteral("\t\t\t*UVW_V_TILING 1.0000") + newline;
		out += QStringLiteral("\t\t\t*UVW_ANGLE 0.0000") + newline;
		out += QStringLiteral("\t\t\t*BITMAP_FILTER Pyramidal") + newline;
		out += QStringLiteral("\t\t}") + newline;
		out += QStringLiteral("\t}") + newline;
	}
	out += QStringLiteral("}") + newline;

	for (int index = 0; index < surfaces.size(); ++index) {
		if (!work.step()) { return {}; }
		const ModelSurface& surface = *surfaces.at(index);
		const ModelFrameGeometry& geometry = surface.frames.at(frameIndex);
		const QString node = quoted(nodeNames.at(index));
		out += QStringLiteral("*GEOMOBJECT {") + newline;
		out += QStringLiteral("\t*NODE_NAME ") + node + newline;
		// Doom 3 turns normals by these rows and starts from a zero matrix,
		// so the identity must be written.
		out += QStringLiteral("\t*NODE_TM {") + newline;
		out += QStringLiteral("\t\t*NODE_NAME ") + node + newline;
		out += QStringLiteral("\t\t*INHERIT_POS 0 0 0") + newline;
		out += QStringLiteral("\t\t*INHERIT_ROT 0 0 0") + newline;
		out += QStringLiteral("\t\t*INHERIT_SCL 0 0 0") + newline;
		out += QStringLiteral("\t\t*TM_ROW0 1.0000\t0.0000\t0.0000") + newline;
		out += QStringLiteral("\t\t*TM_ROW1 0.0000\t1.0000\t0.0000") + newline;
		out += QStringLiteral("\t\t*TM_ROW2 0.0000\t0.0000\t1.0000") + newline;
		out += QStringLiteral("\t\t*TM_ROW3 0.0000\t0.0000\t0.0000") + newline;
		out += QStringLiteral("\t}") + newline;
		out += QStringLiteral("\t*MESH {") + newline;
		out += QStringLiteral("\t\t*TIMEVALUE 0") + newline;
		out += QStringLiteral("\t\t*MESH_NUMVERTEX %1").arg(surface.vertexCount) + newline;
		out += QStringLiteral("\t\t*MESH_NUMFACES %1").arg(surface.triangles.size()) + newline;
		out += QStringLiteral("\t\t*MESH_VERTEX_LIST {") + newline;
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			if (!work.step()) { return {}; }
			const ModelVec3& p = geometry.positions.at(vertex);
			if (!finiteVec(p)) {
				return failWith(QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 has a position that is not finite.").arg(surface.name));
			}
			out += QStringLiteral("\t\t\t*MESH_VERTEX %1\t%2\t%3\t%4").arg(vertex).arg(floatText(p.x), floatText(p.y), floatText(p.z)) + newline;
		}
		out += QStringLiteral("\t\t}") + newline;
		out += QStringLiteral("\t\t*MESH_FACE_LIST {") + newline;
		for (int face = 0; face < surface.triangles.size(); ++face) {
			if (!work.step()) { return {}; }
			const ModelTriangle& t = surface.triangles.at(face);
			if (t.a < 0 || t.b < 0 || t.c < 0 || t.a >= surface.vertexCount || t.b >= surface.vertexCount || t.c >= surface.vertexCount) {
				return failWith(QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 has a triangle index outside its vertices.").arg(surface.name));
			}
			// The studio's counter-clockwise a b c is 3ds Max's A B C.
			out += QStringLiteral("\t\t\t*MESH_FACE %1:\tA: %2\tB: %3\tC: %4\tAB: 1\tBC: 1\tCA: 1\t*MESH_SMOOTHING 1\t*MESH_MTLID 0")
				.arg(face).arg(t.a).arg(t.b).arg(t.c) + newline;
		}
		out += QStringLiteral("\t\t}") + newline;
		out += QStringLiteral("\t\t*MESH_NUMTVERTEX %1").arg(surface.vertexCount) + newline;
		out += QStringLiteral("\t\t*MESH_TVERTLIST {") + newline;
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex) {
			if (!work.step()) { return {}; }
			const ModelTexCoord& uv = surface.texCoords.at(vertex);
			if (!std::isfinite(uv.u) || !std::isfinite(uv.v)) {
				return failWith(QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 has a texture coordinate that is not finite.").arg(surface.name));
			}
			out += QStringLiteral("\t\t\t*MESH_TVERT %1\t%2\t%3\t0").arg(vertex).arg(floatText(uv.u), tvertVText(uv.v)) + newline;
		}
		out += QStringLiteral("\t\t}") + newline;
		out += QStringLiteral("\t\t*MESH_NUMTVFACES %1").arg(surface.triangles.size()) + newline;
		out += QStringLiteral("\t\t*MESH_TFACELIST {") + newline;
		for (int face = 0; face < surface.triangles.size(); ++face) {
			if (!work.step()) { return {}; }
			const ModelTriangle& t = surface.triangles.at(face);
			out += QStringLiteral("\t\t\t*MESH_TFACE %1\t%2\t%3\t%4").arg(face).arg(t.a).arg(t.b).arg(t.c) + newline;
		}
		out += QStringLiteral("\t\t}") + newline;
		out += QStringLiteral("\t\t*MESH_NUMCVERTEX 0") + newline;
		out += QStringLiteral("\t\t*MESH_NORMALS {") + newline;
		for (int face = 0; face < surface.triangles.size(); ++face) {
			if (!work.step()) { return {}; }
			const ModelTriangle& t = surface.triangles.at(face);
			const int corners[3] = {t.a, t.b, t.c};
			ModelVec3 faceNormal = model_formats::normalised(model_formats::crossOf(geometry.positions.at(t.a), geometry.positions.at(t.b), geometry.positions.at(t.c)));
			if (model_formats::dotF(faceNormal, faceNormal) <= 0.0f) {
				faceNormal = ModelVec3{0.0f, 0.0f, 1.0f};
			}
			out += QStringLiteral("\t\t\t*MESH_FACENORMAL %1\t%2\t%3\t%4").arg(face).arg(floatText(faceNormal.x), floatText(faceNormal.y), floatText(faceNormal.z)) + newline;
			for (int corner : corners) {
				ModelVec3 normal = corner < geometry.normals.size() ? model_formats::normalised(geometry.normals.at(corner)) : ModelVec3{};
				if (!finiteVec(normal) || model_formats::dotF(normal, normal) <= 0.0f) {
					normal = faceNormal;
				}
				out += QStringLiteral("\t\t\t\t*MESH_VERTEXNORMAL %1\t%2\t%3\t%4").arg(corner).arg(floatText(normal.x), floatText(normal.y), floatText(normal.z)) + newline;
			}
		}
		out += QStringLiteral("\t\t}") + newline;
		out += QStringLiteral("\t}") + newline;
		out += QStringLiteral("\t*PROP_MOTIONBLUR 0") + newline;
		out += QStringLiteral("\t*PROP_CASTSHADOW 1") + newline;
		out += QStringLiteral("\t*PROP_RECVSHADOW 1") + newline;
		out += QStringLiteral("\t*MATERIAL_REF %1").arg(index) + newline;
		out += QStringLiteral("}") + newline;
	}
	if (!work.check()) { return {}; }
	return out.toUtf8();
}

} // namespace vibestudio
