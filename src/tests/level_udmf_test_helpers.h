#pragma once
#include "tests/doom_preview_test_helpers.h"
namespace vibestudio::tests::udmf {
inline QByteArray textmap() {
	QByteArray text = "// Original generated room\r\nnamespace = \"zdoom\";\r\nuser_title = \"Room \\\"A\\\"\";\r\n";
	text += "vertex { x = +0.2500; y = 0.25; user_marker = 0xDEAD; }\r\nvertex { x = 0.25; y = 256.25; }\r\nvertex { x = 256.25; y = "
			"256.25; }\r\nvertex { x = 256.25; y = 0.25; }\r\n";
	for (int i = 0; i < 4; ++i) {
		text += QStringLiteral("linedef { v1 = %1; v2 = %2; sidefront = %1; blocking = true; }\r\n").arg(i).arg((i + 1) % 4).toUtf8();
		text += "sidedef { sector = 0; texturemiddle = \"STONE\"; offsetx = 1.5; user_style = Fancy; }\r\n";
	}
	text +=
		"sector { heightfloor = 0.5; heightceiling = 128.5; texturefloor = \"STONE\"; textureceiling = \"CEIL\"; lightlevel = 192; }\r\n";
	text += "thing { x = 64.25; y = 64.25; height = 0.5; type = 1; angle = 90; skill1 = true; skill2 = true; skill3 = true; skill4 = true; "
			"skill5 = true; single = true; coop = true; dm = true; user_note = \"untouched\"; }\r\n";
	text += "extension { version = 9007199254740993; text = \"opaque \\n bytes\"; }\r\n// End of source";
	return text;
}
inline QByteArray nodes() {
	QByteArray b("XNOD");
	const auto word = [&](quint32 n) {
		auto at = b.size();
		b.resize(at + 4);
		doom::put32(b, int(at), n);
	};
	word(4);
	word(0);
	word(1);
	word(4);
	word(4);
	for (int i = 0; i < 4; ++i) {
		word(i);
		word((i + 1) % 4);
		b.append(char(i));
		b.append('\0');
		b.append('\0');
	}
	word(0);
	return b;
}
inline QByteArray fixture(const QByteArray& source = textmap()) {
	auto rest = doom::lumps(doom::fixture());
	for (auto& record : rest) {
		if (record.name == "MAP01") {
			record.name = "MAP02";
			break;
		}
	}
	QVector<doom::Lump> first{{"MAP01", {}},		 {"TEXTMAP", source}, {"PORTDATA", QByteArray("\0sidecar", 8)},
							  {"ZNODES", nodes()},	 {"ENDMAP", {}},	  {"USERDATA", "first"},
							  {"USERDATA", "second"}};
	return doom::wad(first + rest);
}
} // namespace vibestudio::tests::udmf
