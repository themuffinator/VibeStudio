#include "core/audio_decode.h"

// Pinned MIT-0 dr_libs and BSD Xiph Vorbis. See docs/CREDITS.md and the
// external/audio UPSTREAM.json files. No device, filesystem or network IO.
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_FLOAT_OUTPUT
#include <dr_mp3.h>
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include <dr_flac.h>
#include <vorbis/vorbisfile.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <csetjmp>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace
{
// All Xiph allocations belong to a per-import region. The reference C library
// does not uniformly handle allocation failure. Guarded calls return here on
// exhaustion, without resuming a partially initialized decoder or skipping any
// C++ destructors. Remaining allocations are released by the region destructor.
struct XiphMemory;
struct alignas(std::max_align_t) XiphAllocation {
	XiphMemory* owner;
	XiphAllocation* next;
	XiphAllocation* previous;
	size_t size;
};
thread_local XiphMemory* activeXiphMemory = nullptr;
struct XiphMemory {
	static constexpr size_t Limit = 16 * 1024 * 1024;
	std::jmp_buf jump;
	size_t limit = Limit;
	XiphAllocation* first = nullptr;
	size_t used = 0;
	~XiphMemory()
	{
		while (first) {
			auto* next = first->next;
			std::free(first);
			first = next;
		}
	}
	bool call(void (*operation)(void*), void* context)
	{
		auto* previous = activeXiphMemory;
		activeXiphMemory = this;
		if (setjmp(jump) != 0) {
			activeXiphMemory = previous;
			return false;
		}
		operation(context); // Only C library calls and trivial callback frames.
		activeXiphMemory = previous;
		return true;
	}
};
} // namespace

extern "C" void* vibestudio_xiph_malloc(size_t size)
{
	auto* owner = activeXiphMemory;
	if (!owner) {
		return nullptr;
	}
	if (owner->used > owner->limit - sizeof(XiphAllocation) ||
	    size > owner->limit - owner->used - sizeof(XiphAllocation)) {
		std::longjmp(owner->jump, 1);
	}
	auto* block = static_cast<XiphAllocation*>(std::malloc(sizeof(XiphAllocation) + size));
	if (!block) {
		std::longjmp(owner->jump, 1);
	}
	*block = {owner, owner->first, nullptr, size};
	if (owner->first) {
		owner->first->previous = block;
	}
	owner->first = block;
	owner->used += sizeof(XiphAllocation) + size;
	return block + 1;
}
extern "C" void vibestudio_xiph_free(void* data)
{
	if (!data) {
		return;
	}
	auto* block = static_cast<XiphAllocation*>(data) - 1;
	auto* owner = block->owner;
	if (block->previous) {
		block->previous->next = block->next;
	} else {
		owner->first = block->next;
	}
	if (block->next) {
		block->next->previous = block->previous;
	}
	owner->used -= sizeof(XiphAllocation) + block->size;
	std::free(block);
}
extern "C" void* vibestudio_xiph_calloc(size_t count, size_t size)
{
	if (size && count > XiphMemory::Limit / size) {
		if (activeXiphMemory) {
			std::longjmp(activeXiphMemory->jump, 1);
		}
		return nullptr;
	}
	void* data = vibestudio_xiph_malloc(count * size);
	if (data) {
		std::memset(data, 0, count * size);
	}
	return data;
}
extern "C" void* vibestudio_xiph_realloc(void* data, size_t size)
{
	if (!data) {
		return vibestudio_xiph_malloc(size);
	}
	if (!size) {
		vibestudio_xiph_free(data);
		return nullptr;
	}
	auto* block = static_cast<XiphAllocation*>(data) - 1;
	auto* owner = block->owner;
	const auto old = block->size;
	if (size > owner->limit - (owner->used - old)) {
		std::longjmp(owner->jump, 1);
	}
	auto* replacement = static_cast<XiphAllocation*>(std::realloc(block, sizeof(XiphAllocation) + size));
	if (!replacement) {
		std::longjmp(owner->jump, 1);
	}
	if (replacement->previous) {
		replacement->previous->next = replacement;
	} else {
		owner->first = replacement;
	}
	if (replacement->next) {
		replacement->next->previous = replacement;
	}
	replacement->size = size;
	owner->used = owner->used - old + size;
	return replacement + 1;
}

namespace vibestudio
{
namespace
{
constexpr size_t DecoderMemoryLimit = 16 * 1024 * 1024;
constexpr qint64 MetadataLimit = 4 * 1024 * 1024;
constexpr int BatchFrames = 4096;

quint8 u8(const QByteArray& data, qsizetype at) { return quint8(data[at]); }
quint32 le32(const QByteArray& data, qsizetype at)
{
	// Callers validate container extents; keep the scalar read bounded as well.
	// The explicit null check also excludes Qt's one-byte empty buffer for GCC.
	if (data.isNull() || at < 0 || at > data.size() - qsizetype(sizeof(quint32))) { return 0; }
	return qFromLittleEndian<quint32>(data.constData() + at);
}

struct Decode {
	const QByteArray& bytes;
	const AudioWorkControl& control;
	const AudioDecodeBudget& budget;
	AudioClipResult result;
	bool stop()
	{
		if (!result.cancelled && control.cancelled && control.cancelled()) {
			result.cancelled = true;
		}
		return result.cancelled;
	}
	bool fail(const char* message)
	{
		result.error = QCoreApplication::translate("AudioDecode", message);
		return false;
	}
	bool format(int channels, int rate, quint64 frames)
	{
		if (channels < 1 || channels > 8 || rate < 1 || rate > 384000 ||
		    frames > quint64(budget.samples / channels)) {
			return fail(QT_TRANSLATE_NOOP("AudioDecode", "The decoded sound exceeds the editor's "
			                                             "channel, rate or sample limit."));
		}
		result.clip.channels = channels;
		result.clip.sampleRate = rate;
		if (frames) {
			result.clip.samples.reserve(qsizetype(frames) * channels);
		}
		return true;
	}
	bool append(const float* data, qint64 frames)
	{
		const qint64 count = frames * result.clip.channels;
		if (count > budget.samples - result.clip.samples.size()) {
			return fail(
			    QT_TRANSLATE_NOOP("AudioDecode", "The decoded sound exceeds the requested sample budget."));
		}
		for (qint64 i = 0; i < count; ++i) {
			if (!std::isfinite(data[i])) {
				return fail(QT_TRANSLATE_NOOP("AudioDecode", "The decoder returned a non-finite sample."));
			}
		}
		const auto old = result.clip.samples.size();
		result.clip.samples.resize(old + count);
		std::copy_n(data, count, result.clip.samples.data() + old);
		return true;
	}
};

struct alignas(std::max_align_t) Allocation {
	size_t size;
};
struct Budget {
	size_t used = 0;
	size_t limit = DecoderMemoryLimit;
	bool exhausted = false;
	static void* allocate(size_t size, void* context)
	{
		auto& self = *static_cast<Budget*>(context);
		if (self.used > self.limit - sizeof(Allocation) ||
		    size > self.limit - self.used - sizeof(Allocation)) {
			self.exhausted = true;
			return nullptr;
		}
		auto* header = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
		if (!header) {
			self.exhausted = true;
			return nullptr;
		}
		header->size = size;
		self.used += sizeof(Allocation) + size;
		return header + 1;
	}
	static void release(void* data, void* context)
	{
		if (!data) {
			return;
		}
		auto* header = static_cast<Allocation*>(data) - 1;
		static_cast<Budget*>(context)->used -= sizeof(Allocation) + header->size;
		std::free(header);
	}
	static void* resize(void* data, size_t size, void* context)
	{
		if (!data) {
			return allocate(size, context);
		}
		auto& self = *static_cast<Budget*>(context);
		auto* header = static_cast<Allocation*>(data) - 1;
		const auto old = header->size;
		if (size > self.limit - (self.used - old)) {
			self.exhausted = true;
			return nullptr;
		}
		auto* next = static_cast<Allocation*>(std::realloc(header, sizeof(Allocation) + size));
		if (!next) {
			self.exhausted = true;
			return nullptr;
		}
		next->size = size;
		self.used = self.used - old + size;
		return next + 1;
	}
};

struct Input {
	Decode& work;
	qsizetype first = 0, end = 0, cursor = 0;
	static size_t read(void* context, void* output, size_t count)
	{
		auto& self = *static_cast<Input*>(context);
		if (self.work.stop()) {
			return 0;
		}
		const auto n = std::min(count, size_t(self.end - self.first - self.cursor));
		std::memcpy(output, self.work.bytes.constData() + self.first + self.cursor, n);
		self.cursor += qsizetype(n);
		return n;
	}
	bool seek(int offset, int origin)
	{
		if (work.stop()) {
			return false;
		}
		const qint64 next = (origin == 0 ? 0 : origin == 1 ? cursor : end - first) + qint64(offset);
		if (next < 0 || next > end - first) {
			return false;
		}
		cursor = next;
		return true;
	}
};

bool decodeFlac(Decode& work)
{
	// STREAMINFO and its PCM digest follow RFC 9639, sections 8.2 and 10.1.
	const auto& bytes = work.bytes;
	if (bytes.size() < 42 || (u8(bytes, 4) & 127) != 0 || bytes.mid(5, 3) != QByteArray::fromHex("000022")) {
		return work.fail(
		    QT_TRANSLATE_NOOP("AudioDecode", "The FLAC STREAMINFO header is incomplete or invalid."));
	}
	const quint64 format = qFromBigEndian<quint64>(bytes.constData() + 18);
	const auto frames = format & 0xfffffffffull;
	const int channels = int((format >> 41) & 7) + 1;
	const int bits = int((format >> 36) & 31) + 1;
	const QByteArray digest = bytes.mid(26, 16);
	const bool hasDigest = digest != QByteArray(16, '\0');
	if (bits < 4 || !work.format(channels, int(format >> 44), frames)) {
		return false;
	}
	if (frames == 0 && !hasDigest) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
		                                   "FLAC import needs a sample count or PCM checksum to "
		                                   "verify a complete sound."));
	}
	qsizetype at = 4;
	bool last = false;
	for (int blocks = 0; !last; ++blocks) {
		if (work.stop()) {
			return false;
		}
		if (blocks >= 1024 || at + 4 > bytes.size()) {
			return work.fail(QT_TRANSLATE_NOOP(
			    "AudioDecode", "The FLAC metadata is incomplete or exceeds the block limit."));
		}
		const int type = u8(bytes, at) & 127;
		last = (u8(bytes, at) & 128) != 0;
		const qsizetype length =
		    (qsizetype(u8(bytes, at + 1)) << 16) | (u8(bytes, at + 2) << 8) | u8(bytes, at + 3);
		if (type == 127 || (blocks > 0 && type == 0) || length > bytes.size() - at - 4 ||
		    at + length + 4 > MetadataLimit) {
			return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
			                                   "The FLAC metadata is invalid, truncated or exceeds 4 MiB."));
		}
		at += 4 + length;
	}
	Budget budget;
	budget.limit = work.budget.decoderBytes;
	Input input{work, 0, bytes.size(), 0};
	drflac_allocation_callbacks allocation{&budget, Budget::allocate, Budget::resize, Budget::release};
	auto* raw = drflac_open(
	    Input::read,
	    [](void* p, int offset, drflac_seek_origin origin) -> drflac_bool32 {
		    return static_cast<Input*>(p)->seek(offset, int(origin));
	    },
	    [](void* p, drflac_int64* cursor) -> drflac_bool32 {
		    *cursor = static_cast<Input*>(p)->cursor;
		    return DRFLAC_TRUE;
	    },
	    &input, &allocation);
	std::unique_ptr<drflac, decltype(&drflac_close)> decoder(raw, drflac_close);
	if (!decoder) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
		                                   "The FLAC decoder could not open this stream within its "
		                                   "16 MiB memory budget."));
	}
	QCryptographicHash hash(QCryptographicHash::Md5);
	std::array<drflac_int32, BatchFrames * 8> pcm{};
	std::array<float, BatchFrames * 8> output{};
	QByteArray packed(BatchFrames * channels * ((bits + 7) / 8), Qt::Uninitialized);
	while (!work.stop()) {
		const auto n = drflac_read_pcm_frames_s32(decoder.get(), BatchFrames, pcm.data());
		if (n == 0) {
			break;
		}
		const int count = int(n) * channels;
		int dest = 0;
		for (int i = 0; i < count; ++i) {
			output[i] = float(double(pcm[i]) / 2147483648.0);
			const quint32 value = quint32(pcm[i] >> (32 - bits));
			for (int byte = 0; byte < (bits + 7) / 8; ++byte) {
				packed[dest++] = char((value >> (byte * 8)) & 255);
			}
		}
		if (hasDigest) {
			hash.addData(QByteArrayView(packed.constData(), dest));
		}
		if (!work.append(output.data(), qint64(n))) {
			return false;
		}
	}
	if (work.stop()) {
		return false;
	}
	if (budget.exhausted || (frames && quint64(work.result.clip.frameCount()) != frames) ||
	    (hasDigest && hash.result() != digest)) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
		                                   "FLAC sample count or checksum verification failed; the "
		                                   "sound may be truncated or corrupt."));
	}
	return true;
}

// MPEG Layer III header fields and frame lengths: ISO/IEC 11172-3 / 13818-3;
// same table values as VibeStudio's existing metadata reader in
// asset_tools.cpp.
struct Mp3Info {
	int channels = 0, rate = 0, samples = 0, size = 0;
};
Mp3Info mp3Header(const QByteArray& bytes, qsizetype at)
{
	if (at + 4 > bytes.size()) {
		return {};
	}
	const auto* p = reinterpret_cast<const uchar*>(bytes.constData() + at);
	const int version = (p[1] >> 3) & 3;
	const int index = p[2] >> 4;
	const int rateIndex = (p[2] >> 2) & 3;
	if (p[0] != 255 || (p[1] & 224) != 224 || version == 1 || ((p[1] >> 1) & 3) != 1 || index == 0 ||
	    index == 15 || rateIndex == 3) {
		return {};
	}
	constexpr int bitrates[2][15] = {{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
	                                 {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
	constexpr int rates[3] = {44100, 48000, 32000};
	const int rate = rates[rateIndex] / (version == 3 ? 1 : version == 2 ? 2 : 4);
	const int samples = version == 3 ? 1152 : 576;
	return {int((p[3] >> 6) == 3 ? 1 : 2), rate, samples,
	        (samples / 8) * bitrates[version == 3 ? 0 : 1][index] * 1000 / rate + int((p[2] >> 1) & 1)};
}

bool decodeMp3(Decode& work)
{
	const auto& bytes = work.bytes;
	qsizetype first = 0, end = bytes.size();
	// ID3v2.2/3/4 bounded envelope. Tag contents are not editable audio metadata.
	if (bytes.startsWith("ID3")) {
		if (bytes.size() < 10 || u8(bytes, 3) < 2 || u8(bytes, 3) > 4) {
			return work.fail(
			    QT_TRANSLATE_NOOP("AudioDecode", "The ID3 header is incomplete or unsupported."));
		}
		qsizetype length = 0;
		for (int i = 6; i < 10; ++i) {
			if (u8(bytes, i) & 128) {
				return work.fail(QT_TRANSLATE_NOOP("AudioDecode", "The ID3 size is invalid."));
			}
			length = (length << 7) | u8(bytes, i);
		}
		first = 10 + length + ((u8(bytes, 3) == 4 && (u8(bytes, 5) & 16)) ? 10 : 0);
		if (first > bytes.size() || first > MetadataLimit) {
			return work.fail(
			    QT_TRANSLATE_NOOP("AudioDecode", "The ID3 metadata is truncated or exceeds 4 MiB."));
		}
	}
	if (end - first >= 128 && bytes.mid(end - 128, 3) == "TAG") {
		end -= 128;
	}
	if (end - first >= 32 && bytes.mid(end - 32, 8) == "APETAGEX") {
		const auto size = le32(bytes, end - 20);
		const auto flags = le32(bytes, end - 12);
		const qint64 total = qint64(size) + ((flags & 0x80000000u) ? 32 : 0);
		if (size < 32 || total > MetadataLimit || total > end - first) {
			return work.fail(
			    QT_TRANSLATE_NOOP("AudioDecode", "The trailing APE metadata is invalid or exceeds 4 MiB."));
		}
		end -= total;
	}
	const auto initial = mp3Header(bytes, first);
	if (!initial.size || !work.format(initial.channels, initial.rate, 0)) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode", "Import requires a complete MPEG Layer "
		                                                  "III stream with a supported bitrate."));
	}
	quint64 encodedFrames = 0;
	for (qsizetype at = first; at < end;) {
		if (work.stop()) {
			return false;
		}
		const auto frame = mp3Header(bytes, at);
		if (frame.size == 0 || frame.size > end - at || frame.channels != initial.channels ||
		    frame.rate != initial.rate) {
			return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
			                                   "The MP3 has an incomplete frame, unexpected data or "
			                                   "a changing channel/rate format."));
		}
		encodedFrames += frame.samples;
		if (encodedFrames > quint64(work.budget.samples / initial.channels) + 3 * 1152) {
			return work.fail(QT_TRANSLATE_NOOP("AudioDecode", "The MP3 exceeds the decoded sample limit."));
		}
		at += frame.size;
	}
	Budget budget;
	budget.limit = work.budget.decoderBytes;
	Input input{work, first, end, 0};
	drmp3_allocation_callbacks allocation{&budget, Budget::allocate, Budget::resize, Budget::release};
	drmp3 decoder{};
	if (!drmp3_init(
	        &decoder, Input::read,
	        [](void* p, int offset, drmp3_seek_origin origin) -> drmp3_bool32 {
		        return static_cast<Input*>(p)->seek(offset, int(origin));
	        },
	        [](void* p, drmp3_int64* cursor) -> drmp3_bool32 {
		        *cursor = static_cast<Input*>(p)->cursor;
		        return DRMP3_TRUE;
	        },
	        nullptr, &input, &allocation)) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
		                                   "The MP3 decoder could not open this stream within its "
		                                   "16 MiB memory budget."));
	}
	const auto close = [](drmp3* p) { drmp3_uninit(p); };
	std::unique_ptr<drmp3, decltype(close)> guard(&decoder, close);
	const auto expected = decoder.totalPCMFrameCount;
	const quint64 skipped = quint64(decoder.delayInPCMFrames) + decoder.paddingInPCMFrames;
	const quint64 headerFrames =
	    decoder.streamStartOffset == quint64(initial.size) ? quint64(initial.samples) : 0;
	if (headerFrames > encodedFrames || skipped > encodedFrames - headerFrames ||
	    (expected != DRMP3_UINT64_MAX && expected != encodedFrames - headerFrames)) {
		return work.fail(
		    QT_TRANSLATE_NOOP("AudioDecode", "The MP3 timing metadata conflicts with its audio frames."));
	}
	const auto completeFrames = encodedFrames - headerFrames - skipped;
	// The validated frame walk also bounds/reserves streams without Xing timing.
	if (!work.format(initial.channels, initial.rate, completeFrames)) {
		return false;
	}
	std::array<float, BatchFrames * 2> output{};
	while (!work.stop()) {
		const auto n = drmp3_read_pcm_frames_f32(&decoder, BatchFrames, output.data());
		if (!n) {
			break;
		}
		if (!work.append(output.data(), qint64(n))) {
			return false;
		}
	}
	if (work.stop()) {
		return false;
	}
	if (budget.exhausted || quint64(work.result.clip.frameCount()) != completeFrames) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
		                                   "The MP3 decoder could not produce the declared complete sound."));
	}
	return true;
}

quint32 oggCrc(const QByteArray& bytes, qsizetype at, qsizetype size)
{
	// Ogg CRC polynomial, RFC 3533 section 6. Header checksum bytes are zero.
	static const auto table = [] {
		std::array<quint32, 256> values{};
		for (quint32 i = 0; i < 256; ++i) {
			quint32 value = i << 24;
			for (int bit = 0; bit < 8; ++bit) {
				value = (value << 1) ^ ((value & 0x80000000u) ? 0x04c11db7u : 0);
			}
			values[i] = value;
		}
		return values;
	}();
	quint32 crc = 0;
	for (qsizetype i = 0; i < size; ++i) {
		crc = (crc << 8) ^ table[(crc >> 24) ^ ((i >= 22 && i < 26) ? 0 : u8(bytes, at + i))];
	}
	return crc;
}

bool decodeVorbis(Decode& work)
{
	const auto& bytes = work.bytes;
	quint32 serial = 0, sequence = 0;
	quint64 total = 0, previousGranule = 0;
	bool continuation = false, finished = false;
	qsizetype packetSize = 0;
	int packets = 0;
	QByteArray identification;
	for (qsizetype at = 0; at < bytes.size();) {
		if (work.stop()) {
			return false;
		}
		if (finished || at + 27 > bytes.size() || bytes.mid(at, 4) != "OggS" || u8(bytes, at + 4) != 0) {
			return work.fail(QT_TRANSLATE_NOOP(
			    "AudioDecode", "The Ogg container is incomplete, chained or has unexpected data."));
		}
		const int flags = u8(bytes, at + 5), segments = u8(bytes, at + 26);
		if ((flags & ~7) || bool(flags & 1) != continuation || (at == 0 ? !(flags & 2) : bool(flags & 2)) ||
		    at + 27 + segments > bytes.size()) {
			return work.fail(QT_TRANSLATE_NOOP("AudioDecode", "The Ogg page or packet sequence is invalid."));
		}
		if (at == 0) {
			serial = le32(bytes, at + 14);
		}
		if (le32(bytes, at + 14) != serial || le32(bytes, at + 18) != sequence++) {
			return work.fail(QT_TRANSLATE_NOOP(
			    "AudioDecode", "Ogg import requires one complete, sequential logical stream."));
		}
		qsizetype payload = at + 27 + segments, size = 27 + segments;
		for (int i = 0; i < segments; ++i) {
			size += u8(bytes, at + 27 + i);
		}
		if (size > bytes.size() - at || oggCrc(bytes, at, size) != le32(bytes, at + 22)) {
			return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
			                                   "The Ogg page is truncated or its checksum does not match."));
		}
		for (int i = 0; i < segments; ++i) {
			const int length = u8(bytes, at + 27 + i);
			packetSize += length;
			if (packetSize > (packets == 1 ? MetadataLimit : 1024 * 1024)) {
				return work.fail(QT_TRANSLATE_NOOP("AudioDecode", "An Ogg packet exceeds the bounded "
				                                                  "decoder header or packet limit."));
			}
			if (packets == 0) {
				identification += bytes.mid(payload, length);
			}
			payload += length;
			continuation = length == 255;
			if (!continuation) {
				++packets;
				packetSize = 0;
			}
		}
		const auto granule = qFromLittleEndian<quint64>(bytes.constData() + at + 6);
		if (granule != std::numeric_limits<quint64>::max()) {
			if (granule < previousGranule || granule > quint64(work.budget.samples)) {
				return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
				                                   "The Ogg sample positions are invalid or exceed the "
				                                   "sample limit."));
			}
			previousGranule = granule;
		}
		finished = (flags & 4) != 0;
		if (finished) {
			total = granule;
		}
		at += size;
	}
	if (!finished || continuation || packets < 4 || total == 0 || identification.size() != 30 ||
	    identification.left(7) != QByteArray("\x01vorbis", 7) || le32(identification, 7) != 0) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode", "Import supports complete Ogg Vorbis streams; this "
		                                                  "stream is incomplete or uses another codec."));
	}
	if (!work.format(u8(identification, 11), int(le32(identification, 12)), total)) {
		return false;
	}
	XiphMemory memory;
	memory.limit = work.budget.decoderBytes;
	Input input{work, 0, bytes.size(), 0};
	OggVorbis_File decoder{};
	const ov_callbacks callbacks{
	    [](void* output, size_t size, size_t count, void* context) -> size_t {
		    if (!size) {
			    return 0;
		    }
		    const auto& source = *static_cast<Input*>(context);
		    const auto available = size_t(source.end - source.first - source.cursor) / size;
		    return Input::read(context, output, std::min(available, count) * size) / size;
	    },
	    [](void* context, ogg_int64_t offset, int origin) -> int {
		    auto& source = *static_cast<Input*>(context);
		    if (offset < -AudioInputByteLimit || offset > AudioInputByteLimit || origin < 0 || origin > 2) {
			    return -1;
		    }
		    return source.seek(int(offset), origin) ? 0 : -1;
	    },
	    nullptr, [](void* context) -> long { return long(static_cast<Input*>(context)->cursor); }};
	struct Open {
		Input* input;
		OggVorbis_File* decoder;
		ov_callbacks callbacks;
		int result;
	} open{&input, &decoder, callbacks, 0};
	if (!memory.call(
	        [](void* context) {
		        auto& operation = *static_cast<Open*>(context);
		        operation.result =
		            ov_open_callbacks(operation.input, operation.decoder, nullptr, 0, operation.callbacks);
	        },
	        &open) ||
	    open.result != 0) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
		                                   "The Vorbis setup is unsupported, invalid or exceeds "
		                                   "the 16 MiB decoder memory budget."));
	}
	const auto* info = ov_info(&decoder, -1);
	const int channels = work.result.clip.channels;
	if (!info || info->channels != channels || info->rate != work.result.clip.sampleRate ||
	    ov_pcm_total(&decoder, -1) != qint64(total)) {
		return work.fail(QT_TRANSLATE_NOOP(
		    "AudioDecode", "The Vorbis decoder format disagrees with the identification header."));
	}
	// Vorbis I section 4.3.9 -> standard FLAC/WAVE speaker order (not a downmix).
	constexpr int mapping[8][8] = {{0},
	                               {0, 1},
	                               {0, 2, 1},
	                               {0, 1, 2, 3},
	                               {0, 2, 1, 3, 4},
	                               {0, 2, 1, 5, 3, 4},
	                               {0, 2, 1, 6, 5, 3, 4},
	                               {0, 2, 1, 7, 5, 6, 3, 4}};
	std::array<float, BatchFrames * 8> output{};
	struct Read {
		OggVorbis_File* decoder;
		float** samples;
		int section;
		long frames;
	} read{&decoder, nullptr, 0, 0};
	while (!work.stop()) {
		if (!memory.call(
		        [](void* context) {
			        auto& operation = *static_cast<Read*>(context);
			        operation.frames =
			            ov_read_float(operation.decoder, &operation.samples, BatchFrames, &operation.section);
		        },
		        &read) ||
		    read.frames < 0 || read.section != 0) {
			return work.fail(QT_TRANSLATE_NOOP("AudioDecode",
			                                   "Vorbis decoding failed, lost a packet or exceeded "
			                                   "its memory budget."));
		}
		const int n = int(read.frames);
		if (!n) {
			break;
		}
		for (int frame = 0; frame < n; ++frame) {
			for (int channel = 0; channel < channels; ++channel) {
				output[frame * channels + channel] = read.samples[mapping[channels - 1][channel]][frame];
			}
		}
		if (!work.append(output.data(), n)) {
			return false;
		}
	}
	if (work.stop()) {
		return false;
	}
	if (quint64(work.result.clip.frameCount()) != total) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode", "The Vorbis stream could not produce "
		                                                  "its complete declared sample range."));
	}
	if (!memory.call([](void* context) { ov_clear(static_cast<OggVorbis_File*>(context)); }, &decoder)) {
		return work.fail(QT_TRANSLATE_NOOP("AudioDecode", "Vorbis decoder cleanup failed."));
	}
	return true;
}
} // namespace

QString compressedAudioFormat(const QByteArray& bytes)
{
	if (bytes.startsWith("fLaC")) {
		return QStringLiteral("flac");
	}
	if (bytes.startsWith("ID3") ||
	    (bytes.size() >= 2 && u8(bytes, 0) == 255 && (u8(bytes, 1) & 224) == 224)) {
		return QStringLiteral("mp3");
	}
	if (bytes.startsWith("OggS")) {
		const qsizetype start = bytes.size() >= 27 ? 27 + u8(bytes, 26) : bytes.size();
		return bytes.mid(start, 7) == QByteArray("\x01vorbis", 7) ? QStringLiteral("vorbis")
		                                                          : QStringLiteral("ogg");
	}
	return {};
}

AudioClipResult decodeCompressedAudio(const QByteArray& bytes, const AudioWorkControl& control,
                                      const AudioDecodeBudget& budget)
{
	Decode work{bytes, control, budget, {}};
	try {
		if (work.stop()) {
			return work.result;
		}
		if (budget.samples < 1 || budget.samples > AudioSampleLimit || budget.decoderBytes < 64 ||
		    budget.decoderBytes > DecoderMemoryLimit) {
			work.fail(QT_TRANSLATE_NOOP("AudioDecode", "The requested decode budget must stay "
			                                           "within the editor's standard limits."));
		} else if (bytes.size() > AudioInputByteLimit) {
			work.fail(QT_TRANSLATE_NOOP("AudioDecode", "The audio input exceeds the 128 MiB editing limit."));
		} else {
			const auto format = compressedAudioFormat(bytes);
			bool decoded = false;
			if (format == QLatin1String("flac")) {
				decoded = decodeFlac(work);
			} else if (format == QLatin1String("mp3")) {
				decoded = decodeMp3(work);
			} else if (format == QLatin1String("vorbis") || format == QLatin1String("ogg")) {
				decoded = decodeVorbis(work);
			} else {
				work.fail(QT_TRANSLATE_NOOP("AudioDecode", "Import supports PCM/float WAV, digital Doom DMX, "
				                                           "MP3, native FLAC and Ogg Vorbis."));
			}
			if (decoded && !work.stop()) {
				work.result.error = validateAudioClip(work.result.clip);
			} else if (work.result.error.isEmpty() && !work.result.cancelled) {
				work.fail(QT_TRANSLATE_NOOP("AudioDecode",
				                            "The compressed audio stream is invalid or unsupported."));
			}
		}
	} catch (const std::bad_alloc&) {
		work.fail(QT_TRANSLATE_NOOP("AudioDecode", "There is not enough memory to decode this sound."));
	}
	if (!work.result.error.isEmpty() || work.result.cancelled) {
		work.result.clip = {};
	} else {
		work.result.warnings << QCoreApplication::translate(
		    "AudioDecode", "Imported decoded samples. Compressed container tags, artwork and "
		                   "embedded marker conventions are not copied into the audio project.");
	}
	return work.result;
}
} // namespace vibestudio
