#pragma once

#include "core/model_mesh.h"

#include <QtEndian>
#include <cstring>

namespace vibestudio::tests
{
// Synthetic IDPO layout, independently assembled from the public format.
// No model, palette or texture content is taken from a game.
struct MdlFixture
{
	QByteArray bytes;
	int skinTimes = 0, frameTimes = 0, normalIndex = 0;
};
inline void mdlInteger(QByteArray &bytes, qint32 value)
{
	const auto offset = bytes.size();
	bytes.resize(offset + 4);
	qToLittleEndian(value, bytes.data() + offset);
}
inline void mdlNumber(QByteArray &bytes, float value)
{
	quint32 bits;
	std::memcpy(&bits, &value, 4);
	mdlInteger(bytes, qint32(bits));
}
inline MdlFixture groupedMdlFixture()
{
	MdlFixture result;
	auto &bytes = result.bytes;
	bytes = "IDPO";
	mdlInteger(bytes, 6);
	for (float value : {1.f, 1.f, 1.f, -4.f, -4.f, 0.f, 16.f, 1.25f, 2.5f, 3.75f})
	{
		mdlNumber(bytes, value);
	}
	for (qint32 value : {2, 8, 4, 4, 2, 2, 1, qint32(0x80000010u)})
	{
		mdlInteger(bytes, value);
	}
	mdlNumber(bytes, 2.5f);
	mdlInteger(bytes, 1);
	mdlInteger(bytes, 2);
	result.skinTimes = bytes.size();
	mdlNumber(bytes, .1f);
	mdlNumber(bytes, .25f);
	for (int member = 0; member < 3; ++member)
	{
		if (member == 2)
		{
			mdlInteger(bytes, 0);
		}
		for (int index = 0; index < 32; ++index)
		{
			const int values[]{0, 1, 16, 31, 96, 127, 224, 255};
			bytes.append(char(values[(index + member) % 8]));
		}
	}
	for (const auto &st : {QPair{0, 0}, QPair{7, 0}, QPair{7, 3}, QPair{0, 3}})
	{
		mdlInteger(bytes, 0);
		mdlInteger(bytes, st.first);
		mdlInteger(bytes, st.second);
	}
	for (qint32 value : {1, 0, 1, 2, 1, 0, 2, 3})
	{
		mdlInteger(bytes, value);
	}
	mdlInteger(bytes, 1);
	mdlInteger(bytes, 2);
	bytes += QByteArray::fromHex("0000000008080400");
	result.frameTimes = bytes.size();
	mdlNumber(bytes, .1f);
	mdlNumber(bytes, .3f);
	for (int frame = 0; frame < 3; ++frame)
	{
		if (frame == 2)
		{
			mdlInteger(bytes, 0);
		}
		auto bounds = QByteArray::fromHex("0000000008080000");
		bounds[2] = char(frame * 4);
		bounds[6] = char(frame * 4);
		bytes += bounds;
		const auto name = frame < 2 ? QByteArray("walk") + QByteArray::number(frame + 1) : QByteArray("idle1");
		bytes += name.leftJustified(16, '\0');
		for (const auto &xy : {QPair{0, 0}, QPair{8, 0}, QPair{8, 8}, QPair{0, 8}})
		{
			bytes.append(char(xy.first));
			bytes.append(char(xy.second));
			bytes.append(char(frame * 4));
			result.normalIndex = bytes.size();
			bytes.append(char(5));
		}
	}
	return result;
}
} // namespace vibestudio::tests
