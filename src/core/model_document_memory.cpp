#include "core/model_document_memory.h"

namespace vibestudio
{

void ModelMemoryVisitor::mesh(const ModelMesh &mesh)
{
	string(mesh.sourcePath);
	string(mesh.formatId);
	string(mesh.formatName);
	list(mesh.surfaces);
	for (const auto &surface : mesh.surfaces)
	{
		string(surface.name);
		list(surface.triangles);
		list(surface.texCoords);
		list(surface.frames);
		for (const auto &frame : surface.frames)
		{
			list(frame.positions);
			list(frame.normals);
		}
		strings(surface.skinPaths);
		strings(surface.warnings);
		set(surface.uvSeams);
		list(surface.skinning.first);
		list(surface.skinning.count);
		list(surface.skinning.influences);
		list(surface.topologyVertices);
		list(surface.editOrigins.first);
		list(surface.editOrigins.count);
		list(surface.editOrigins.sources);
		list(surface.editOrigins.flags);
	}
	list(mesh.frames);
	for (const auto &frame : mesh.frames)
	{
		string(frame.name);
	}
	list(mesh.animations);
	for (const auto &clip : mesh.animations)
	{
		string(clip.name);
	}
	list(mesh.tags);
	for (const auto &tag : mesh.tags)
	{
		string(tag.name);
	}
	list(mesh.embeddedSkins);
	for (const auto &skin : mesh.embeddedSkins)
	{
		string(skin.name);
		image(skin.image);
		list(skin.indexedFrames);
		for (const auto &pixels : skin.indexedFrames)
		{
			bytes(pixels);
		}
		list(skin.intervals);
	}
	strings(mesh.skinPaths);
	strings(mesh.detailLines);
	strings(mesh.warnings);
	string(mesh.error);
	bytes(mesh.mdl.palette);
	list(mesh.mdl.frameGroups);
	for (const auto &group : mesh.mdl.frameGroups)
	{
		list(group.intervals);
	}
	list(mesh.collisionBoxes);
	for (const auto &box : mesh.collisionBoxes)
	{
		string(box.name);
		list(box.framePoses);
	}
	const auto &skeleton = mesh.skeleton;
	list(skeleton.joints);
	for (const auto &joint : skeleton.joints)
	{
		string(joint.name);
	}
	list(skeleton.clips);
	for (const auto &clip : skeleton.clips)
	{
		string(clip.name);
		string(clip.sourcePath);
		list(clip.frames);
		for (const auto &pose : clip.frames)
		{
			list(pose);
		}
		list(clip.frameMins);
		list(clip.frameMaxs);
		list(clip.curves);
		for (const auto &group : clip.curves)
		{
			string(group.joint);
			for (const auto &channel : group.channels)
			{
				list(channel);
			}
		}
	}
	list(skeleton.tags);
	for (const auto &tag : skeleton.tags)
	{
		string(tag.name);
	}
	string(skeleton.sourceFormat);
	list(skeleton.bakedClipForFrame);
	strings(mesh.companionPaths);
}

void ModelMemoryVisitor::topology(const ModelSurfaceTopology &topology)
{
	// The triangles are usually the mesh's own array, counted once.
	list(topology.triangles);
	list(topology.edges);
	hash(topology.faceUses);
	set(topology.allEdges);
	set(topology.edgeVertices);
}

void ModelMemoryLedger::clear()
{
	m_blocks.clear();
	m_live.clear();
	m_total = 0;
	m_shared = 0;
}

void ModelMemoryLedger::add(const Visit &visit)
{
	ModelMemoryVisitor visitor(
		[this](const void *address, qint64 bytes)
		{
			auto &entry = m_blocks[address];
			if (entry.references++ == 0)
			{
				entry.bytes = bytes;
				m_total += bytes;
				if (m_live.contains(address))
				{
					m_shared += bytes;
				}
			}
		});
	visit(visitor);
}

void ModelMemoryLedger::remove(const Visit &visit)
{
	ModelMemoryVisitor visitor(
		[this](const void *address, qint64)
		{
			const auto found = m_blocks.find(address);
			if (found == m_blocks.end() || --found->references > 0)
			{
				return;
			}
			m_total -= found->bytes;
			if (m_live.contains(address))
			{
				m_shared -= found->bytes;
			}
			m_blocks.erase(found);
		});
	visit(visitor);
}

void ModelMemoryLedger::setLive(const Visit &visit)
{
	m_live.clear();
	m_shared = 0;
	ModelMemoryVisitor visitor(
		[this](const void *address, qint64)
		{
			if (m_live.contains(address))
			{
				return;
			}
			m_live.insert(address);
			const auto found = m_blocks.constFind(address);
			if (found != m_blocks.cend())
			{
				m_shared += found->bytes;
			}
		});
	visit(visitor);
}

} // namespace vibestudio
