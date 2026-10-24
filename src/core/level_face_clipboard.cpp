#include "core/level_face_clipboard.h"
#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <cmath>

namespace vibestudio {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelFaceClipboard) };
constexpr qsizetype textLimit = 4194304;
constexpr int faceLimit = 16384;
bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }

// The lexer accepts only complete tokens and comments; gaps and unterminated
// comments/strings cannot silently turn malformed data into a partial paste.
bool tokens(const QString& text, QStringList* result, const std::function<bool()>& cancelled)
{
	static const QRegularExpression pattern(QStringLiteral(R"(\G(?:\s+|//[^\r\n]*|/\*[\s\S]*?\*/|"[^"\r\n]*"|[()[\]]|[^\s()[\]{}"]+))"));
	for (qsizetype at = 0; at < text.size();) {
		if (cancelled && cancelled()) { return false; }
		const auto match = pattern.match(text, at);
		if (!match.hasMatch() || match.capturedStart() != at || match.capturedLength() == 0) { return false; }
		const auto token = match.captured(); at += token.size();
		if (token.startsWith(QStringLiteral("/*"))) {
			if (!token.endsWith(QStringLiteral("*/"))) { return false; }
			continue;
		}
		if (token.front().isSpace() || token.startsWith(QStringLiteral("//"))) { continue; }
		if (result->size() >= faceLimit * 80) { return false; }
		result->append(token);
	}
	return true;
}
struct Reader {
	const QStringList& values;
	qsizetype at = 0;
	QString peek() const { return at < values.size() ? values[at] : QString(); }
	bool take(const char* value) { if (peek() != QLatin1String(value)) { return false; } ++at; return true; }
	bool number(double* result, double limit = 1e9) {
		if (at >= values.size()) { return false; }
		bool ok = false; const double value = values[at].toDouble(&ok);
		if (!ok || !std::isfinite(value) || std::abs(value) > limit) { return false; }
		*result = value; ++at; return true;
	}
	bool group(QVector<double>* result, const char* open = "(", const char* close = ")") {
		if (!take(open)) { return false; }
		while (peek() != QLatin1String(close)) {
			double value;
			if (result->size() >= 4 || !number(&value)) { return false; }
			result->append(value);
		}
		return take(close);
	}
	bool material(QString* result) {
		if (at >= values.size()) { return false; }
		*result = values[at++];
		if (result->startsWith('"')) { *result = result->mid(1, result->size() - 2); }
		return !result->isEmpty();
	}
};
LevelMapVec3 vector(const QVector<double>& p) { return {p[0], p[1], p[2], true}; }
bool faceClipboard(const LevelMapBrushFace& face, LevelSurfaceClipboard* clipboard, QString* error)
{
	const auto coordinate = [](double value) { return std::isfinite(value) && std::abs(value) <= 1e9; };
	const auto point = [&](LevelMapVec3 p) { return p.valid && coordinate(p.x) && coordinate(p.y) && coordinate(p.z); };
	if ((!face.explicitTextureMatrix && (face.scaleX == 0 || face.scaleY == 0))
		|| (face.explicitPlane ? !point(face.planeNormal) || !coordinate(face.planeDistance) : !point(face.p0) || !point(face.p1) || !point(face.p2))) {
		return fail(error, Text::tr("Face clipboard records need bounded, finite geometry and non-zero texture scales."));
	}
	LevelMapDocument source; source.format = LevelMapFormat::QuakeMap;
	LevelMapBrush brush; brush.id = 0; brush.faces << face; source.brushes << brush;
	return copyLevelSurface(source, {0, 0}, clipboard, error);
}
QString number(double n) { return QString::number(n == 0 ? 0 : n, 'g', 17); }
QString point(LevelMapVec3 p) { return QStringLiteral("( %1 %2 %3 )").arg(number(p.x), number(p.y), number(p.z)); }
} // namespace

bool looksLikeLevelFaceText(const QString& text)
{
	// Classification only. The worker performs complete, cancellable parsing.
	qsizetype at = 0;
	while (at < text.size()) {
		if (text[at].isSpace() || text[at] == QChar(0xfeff)) { ++at; continue; }
		if (QStringView(text).mid(at).startsWith(QLatin1String("//"))) {
			const auto end = text.indexOf('\n', at + 2); if (end < 0) { return false; } at = end + 1; continue;
		}
		if (QStringView(text).mid(at).startsWith(QLatin1String("/*"))) {
			const auto end = text.indexOf(QStringLiteral("*/"), at + 2); if (end < 0) { return false; } at = end + 2; continue;
		}
		return text[at] == QLatin1Char('(');
	}
	return false;
}

bool parseLevelFaceText(const QString& text, QVector<LevelMapBrushFace>* output, QString* error, const std::function<bool()>& cancelled)
{
	if (error) { error->clear(); }
	const auto bad = [&] { return fail(error, cancelled && cancelled() ? Text::tr("Face clipboard parsing cancelled.")
		: Text::tr("Use complete .map face records with finite geometry, non-zero texture scales and optional contents/surface/value integers.")); };
	if (!output || text.isEmpty() || text.size() > textLimit || text.contains(QChar::Null)) { return bad(); }
	for (qsizetype i = 0; i < text.size(); ++i) {
		if ((i % 4096 == 0) && cancelled && cancelled()) { return bad(); }
		if (text[i].isHighSurrogate()) {
			if (++i >= text.size() || !text[i].isLowSurrogate()) { return bad(); }
		} else if (text[i].isLowSurrogate()) { return bad(); }
	}
	QStringList words;
	const auto input = text.startsWith(QChar(0xfeff)) ? text.mid(1) : text;
	if (!tokens(input, &words, cancelled)) { return bad(); }
	Reader read{words}; QVector<LevelMapBrushFace> faces;
	while (read.at < words.size()) {
		if ((cancelled && cancelled()) || faces.size() >= faceLimit) { return bad(); }
		LevelMapBrushFace face; QVector<double> first;
		if (!read.group(&first) || (first.size() != 3 && first.size() != 4)) { return bad(); }
		face.explicitPlane = first.size() == 4;
		if (face.explicitPlane) { face.planeNormal = vector(first); face.planeDistance = first[3]; }
		else {
			QVector<double> second, third;
			if (!read.group(&second) || second.size() != 3 || !read.group(&third) || third.size() != 3) { return bad(); }
			face.p0 = vector(first); face.p1 = vector(second); face.p2 = vector(third);
		}
		face.explicitTextureMatrix = read.peek() == QLatin1String("(");
		if (face.explicitTextureMatrix) {
			QVector<double> u, v;
			if (!read.take("(") || !read.group(&u) || u.size() != 3 || !read.group(&v) || v.size() != 3 || !read.take(")")
				|| !read.material(&face.textureName)) { return bad(); }
			for (int i = 0; i < 3; ++i) { face.textureMatrix[i] = u[i]; face.textureMatrix[i + 3] = v[i]; }
		} else {
			if (face.explicitPlane || !read.material(&face.textureName)) { return bad(); }
			face.explicitTextureAxes = read.peek() == QLatin1String("[");
			if (face.explicitTextureAxes) {
				QVector<double> u, v;
				if (!read.group(&u, "[", "]") || u.size() != 4 || !read.group(&v, "[", "]") || v.size() != 4) { return bad(); }
				face.uAxis = vector(u); face.vAxis = vector(v); face.uOffset = face.shiftX = u[3]; face.vOffset = face.shiftY = v[3];
			} else if (!read.number(&face.shiftX) || !read.number(&face.shiftY)) { return bad(); }
			if (!read.number(&face.rotation) || !read.number(&face.scaleX) || !read.number(&face.scaleY)
				|| face.scaleX == 0 || face.scaleY == 0) { return bad(); }
		}
		if (!read.peek().isEmpty() && read.peek() != QLatin1String("(")) {
			double content, surface, value;
			if (!read.number(&content, 4294967295.0) || !read.number(&surface, 4294967295.0) || !read.number(&value, 2147483648.0)
				|| content != std::trunc(content) || surface != std::trunc(surface) || value != std::trunc(value)
				|| content < -2147483648.0 || surface < -2147483648.0 || value < -2147483648.0 || value > 2147483647.0) { return bad(); }
			face.contentFlags = qint64(content); face.surfaceFlags = qint64(surface); face.surfaceValue = qint64(value); face.flagsWritten = true;
		}
		LevelSurfaceClipboard checked;
		if (!faceClipboard(face, &checked, error)) { return false; }
		faces << face;
	}
	if (faces.isEmpty() || (cancelled && cancelled())) { return bad(); }
	*output = std::move(faces); return true;
}

bool parseLevelFaceClipboard(const QString& text, LevelSurfaceClipboard* output, QString* error, const std::function<bool()>& cancelled)
{
	QVector<LevelMapBrushFace> faces; LevelSurfaceClipboard result;
	if (!output || !parseLevelFaceText(text, &faces, error, cancelled) || !faceClipboard(faces.last(), &result, error)) { return false; }
	*output = result; return true;
}

QString levelFaceSelectionText(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces, QString* error)
{
	if (error) { error->clear(); }
	const auto bad = [&] { fail(error, Text::tr("Select between 1 and 16,384 valid brush faces to copy their attributes.")); return QString(); };
	if ((document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) || faces.isEmpty() || faces.size() > faceLimit) { return bad(); }
	QHash<int, const LevelMapBrush*> brushes; for (const auto& brush : document.brushes) { brushes.insert(brush.id, &brush); }
	QString result;
	for (const auto& ref : faces) {
		const auto* brush = brushes.value(ref.brushId);
		if (!brush || ref.faceIndex < 0 || ref.faceIndex >= brush->faces.size()) { return bad(); }
		const auto& f = brush->faces[ref.faceIndex]; LevelSurfaceClipboard checked;
		if (!faceClipboard(f, &checked, error)) { return {}; }
		QString line = f.explicitPlane ? QStringLiteral("( %1 %2 %3 %4 )").arg(number(f.planeNormal.x), number(f.planeNormal.y), number(f.planeNormal.z), number(f.planeDistance))
			: point(f.p0) + ' ' + point(f.p1) + ' ' + point(f.p2);
		if (f.explicitTextureMatrix) {
			const auto& m = f.textureMatrix;
			line += QStringLiteral(" ( ( %1 %2 %3 ) ( %4 %5 %6 ) ) %7").arg(number(m[0]), number(m[1]), number(m[2]), number(m[3]), number(m[4]), number(m[5]), f.textureName);
		} else {
			line += ' ' + f.textureName;
			line += f.explicitTextureAxes ? QStringLiteral(" [ %1 %2 %3 %4 ] [ %5 %6 %7 %8 ]").arg(number(f.uAxis.x), number(f.uAxis.y), number(f.uAxis.z), number(f.uOffset), number(f.vAxis.x), number(f.vAxis.y), number(f.vAxis.z), number(f.vOffset))
				: QStringLiteral(" %1 %2").arg(number(f.shiftX), number(f.shiftY));
			line += QStringLiteral(" %1 %2 %3").arg(number(f.rotation), number(f.scaleX), number(f.scaleY));
		}
		if (f.flagsWritten || f.contentFlags || f.surfaceFlags || f.surfaceValue) { line += QStringLiteral(" %1 %2 %3").arg(f.contentFlags).arg(f.surfaceFlags).arg(f.surfaceValue); }
		result += line + '\n'; if (result.size() > textLimit) { return bad(); }
	}
	return result;
}
} // namespace vibestudio
