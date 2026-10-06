#include "core/model_document.h"
#include "core/model_obj.h"
#include "tests/model_mdl_test_helpers.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QtEndian>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
int integer(const QByteArray &data, int at) { return qFromLittleEndian<qint32>(data.constData() + at); }
float number(const QByteArray &data, int at)
{
	const quint32 bits = qFromLittleEndian<quint32>(data.constData() + at);
	float result;
	std::memcpy(&result, &bits, sizeof(result));
	return result;
}
double area(const std::array<ModelVec3, 3> &p)
{
	return (double(p[1].x) - p[0].x) * (double(p[2].y) - p[0].y) - (double(p[1].y) - p[0].y) * (double(p[2].x) - p[0].x);
}
// Independent first-triangle readers of the public layouts already credited
// in docs/CREDITS.md. Do not use the production decoder to audit writer order.
bool clockwise(const QByteArray &data, const QString &format)
{
	std::array<ModelVec3, 3> points;
	if (format == "md3")
	{
		const int surface = integer(data, 100);
		const int triangles = surface + integer(data, surface + 88), positions = surface + integer(data, surface + 100);
		for (int c = 0; c < 3; ++c)
		{
			const int vertex = integer(data, triangles + c * 4);
			points[c] = {qFromLittleEndian<qint16>(data.constData() + positions + vertex * 8) / 64.f,
						 qFromLittleEndian<qint16>(data.constData() + positions + vertex * 8 + 2) / 64.f, 0};
		}
	}
	else
	{
		int triangles, positions, stride, scale, translate;
		if (format == "mdl")
		{
			int at = 84;
			for (int skin = 0; skin < integer(data, 48); ++skin)
			{
				const int grouped = integer(data, at);
				at += 4;
				const int members = grouped ? integer(data, at) : 1;
				if (grouped)
				{
					at += 4 + members * 4;
				}
				at += members * integer(data, 52) * integer(data, 56);
			}
			triangles = at + integer(data, 60) * 12;
			const int frame = triangles + integer(data, 64) * 16;
			if (integer(data, frame) != 0)
			{
				return false;
			}
			triangles += 4;
			positions = frame + 4 + 24;
			stride = 4;
			scale = 8;
			translate = 20;
		}
		else
		{
			triangles = integer(data, 52);
			const int frame = integer(data, 56);
			positions = frame + 40;
			stride = 2;
			scale = frame;
			translate = frame + 12;
			const int commands = integer(data, 60);
			if (integer(data, commands) != 3)
			{
				return false;
			}
			for (int c = 0; c < 3; ++c)
			{
				if (integer(data, commands + 12 + c * 12) != qFromLittleEndian<quint16>(data.constData() + triangles + c * 2))
				{
					return false;
				}
			}
		}
		for (int c = 0; c < 3; ++c)
		{
			const int vertex =
				stride == 4 ? integer(data, triangles + c * 4) : qFromLittleEndian<quint16>(data.constData() + triangles + c * 2);
			points[c] = {quint8(data[positions + vertex * 4]) * number(data, scale) + number(data, translate),
						 quint8(data[positions + vertex * 4 + 1]) * number(data, scale + 4) + number(data, translate + 4), 0};
		}
	}
	return area(points) < -10;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	auto source = decodeModelObj("panel.obj", "v 0 0 0\nv 16 0 0\nv 0 16 0\nvt 0 0\nvt .5 0\nvt 0 .5\nf 1/1 2/2 3/3\n");
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateFrame;
	duplicate.frame = 0;
	duplicate.text = "lift";
	bool ok = expect(applyModelEdit(&source, duplicate), "prepare an animated counter-clockwise source");
	if (!ok)
	{
		return EXIT_FAILURE;
	}
	for (auto &p : source.surfaces[0].frames[1].positions)
	{
		p.z += 8;
	}
	updateEditableModelMetadata(&source);
	for (const auto &format : {QString("mdl"), QString("md2"), QString("md3")})
	{
		auto mesh = source;
		if (format == "mdl")
		{
			const auto reference = decodeModelMesh("skin.mdl", tests::groupedMdlFixture().bytes);
			mesh.mdl = reference.mdl;
			mesh.mdl.frameGroups = {{0, {}}, {1, {}}};
			mesh.embeddedSkins = reference.embeddedSkins;
			updateEditableModelMetadata(&mesh);
		}
		QString error;
		const auto before = QJsonDocument(editableModelJson(mesh)).toJson();
		const auto bytes = exportEditableModel(mesh, format, 0, &error);
		if (!expect(!bytes.isEmpty(), "native export succeeds"))
		{
			std::cerr << format.toStdString() << ": " << error.toStdString() << '\n';
			return EXIT_FAILURE;
		}
		ok &= expect(clockwise(bytes, format), "native triangle and MD2 GL stream face outward using clockwise order");
		ok &= expect(before == QJsonDocument(editableModelJson(mesh)).toJson(), "winding conversion leaves source and undo data unchanged");
		auto loaded = decodeModelMesh("panel." + format, bytes);
		ok &= expect(loaded.isValid() && loaded.surfaces.size() == 1 && loaded.frames.size() == 2, "native import retains both poses");
		if (!ok)
		{
			return EXIT_FAILURE;
		}
		for (const auto &frame : loaded.surfaces[0].frames)
		{
			const auto t = loaded.surfaces[0].triangles.first();
			ok &= expect(area({frame.positions[t.a], frame.positions[t.b], frame.positions[t.c]}) > 10 && frame.normals[t.a].z > .99f,
						 "decoded front face and stored normal agree in every pose");
		}
		ModelEdit normals;
		normals.kind = ModelEditKind::RecalculateNormals;
		ok &= expect(applyModelEdit(&loaded, normals) && loaded.surfaces[0].frames[0].normals[0].z > .99f,
					 "recomputing imported normals keeps the outward direction");
		const auto obj = decodeModelObj("roundtrip.obj", exportModelFrameObj(loaded, 1).toUtf8());
		if (!expect(obj.isValid() && obj.surfaces.size() == 1 && !obj.surfaces[0].triangles.isEmpty(), "OBJ handoff decodes"))
		{
			return EXIT_FAILURE;
		}
		const auto triangle = obj.surfaces[0].triangles.first();
		const auto &frame = obj.surfaces[0].frames.first();
		ok &= expect(area({frame.positions[triangle.a], frame.positions[triangle.b], frame.positions[triangle.c]}) > 10,
					 "native-to-OBJ handoff preserves front faces");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
