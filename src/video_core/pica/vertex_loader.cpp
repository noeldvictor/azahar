// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/logging/log.h"
#include "video_core/pica/vertex_loader.h"

namespace Pica {

VertexLoader::VertexLoader(Memory::MemorySystem& memory, const PipelineRegs& regs) {
    const auto& attribute_config = regs.vertex_attributes;
    const PAddr base_address = attribute_config.GetPhysicalBaseAddress();
    num_total_attributes = attribute_config.GetNumTotalAttributes();

    for (u32 i = 0; i < 16; i++) {
        vertex_attribute_is_default[i] = attribute_config.IsDefaultAttribute(i);
    }

    // Setup attribute data from loaders
    for (u32 loader = 0; loader < 12; ++loader) {
        const auto& loader_config = attribute_config.attribute_loaders[loader];

        u32 offset = 0;

        // TODO: What happens if a loader overwrites a previous one's data?
        for (u32 component = 0; component < loader_config.component_count; ++component) {
            if (component >= 12) {
                LOG_ERROR(HW_GPU,
                          "Overflow in the vertex attribute loader {} trying to load component {}",
                          loader, component);
                continue;
            }

            u32 attribute_index = loader_config.GetComponent(component);
            if (attribute_index < 12) {
                offset = Common::AlignUp(offset,
                                         attribute_config.GetElementSizeInBytes(attribute_index));
                const PAddr source_address =
                    base_address + static_cast<u32>(loader_config.data_offset) + offset;
                vertex_attribute_sources[attribute_index] =
                    memory.GetPhysicalPointer(source_address);
                vertex_attribute_strides[attribute_index] =
                    static_cast<u32>(loader_config.byte_count);
                vertex_attribute_loaders[attribute_index] =
                    VertexLoaderUtils::Decode(attribute_config.GetFormat(attribute_index),
                                              attribute_config.GetNumElements(attribute_index));
                if (vertex_attribute_sources[attribute_index] == nullptr) [[unlikely]] {
                    vertex_attribute_loaders[attribute_index] =
                        VertexLoaderUtils::AttributeLoader::Invalid;
                }
                offset += attribute_config.GetStride(attribute_index);
            } else if (attribute_index < 16) {
                // Attribute ids 12, 13, 14 and 15 signify 4, 8, 12 and 16-byte paddings,
                // respectively
                offset = Common::AlignUp(offset, 4);
                offset += (attribute_index - 11) * 4;
            } else {
                UNREACHABLE(); // This is truly unreachable due to the number of bits for each
                               // component
            }
        }
    }
}

VertexLoader::~VertexLoader() = default;

void VertexLoader::LoadVertex(u32 vertex, AttributeBuffer& input,
                              AttributeBuffer& input_default_attributes) const {
    for (s32 i = 0; i < num_total_attributes; ++i) {
        // Load the default attribute if we're configured to do so
        if (vertex_attribute_is_default[i]) {
            input[i] = input_default_attributes[i];
            continue;
        }

        // TODO(yuriks): In this case, no data gets loaded and the vertex
        // remains with the last value it had. This isn't currently maintained
        // as global state, however, and so won't work in Citra yet.
        const auto attribute_loader = vertex_attribute_loaders[i];
        if (attribute_loader == VertexLoaderUtils::AttributeLoader::Invalid) {
            LOG_ERROR(HW_GPU, "Vertex retension unimplemented");
            continue;
        }

        // Load per-vertex data from the loader arrays
        const void* source = vertex_attribute_sources[i] + vertex_attribute_strides[i] * vertex;
        VertexLoaderUtils::LoadAttribute(attribute_loader, source, input[i]);
    }
}

} // namespace Pica
