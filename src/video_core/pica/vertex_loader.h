// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/memory.h"
#include "video_core/pica/output_vertex.h"
#include "video_core/pica/regs_pipeline.h"
#include "video_core/pica/vertex_loader_utils.h"

namespace Memory {
class MemorySystem;
}

namespace Pica {

class VertexLoader {
public:
    explicit VertexLoader(Memory::MemorySystem& memory, const PipelineRegs& regs);
    ~VertexLoader();

    void LoadVertex(u32 vertex, AttributeBuffer& input,
                    AttributeBuffer& input_default_attributes) const;

    int GetNumTotalAttributes() const {
        return num_total_attributes;
    }

private:
    std::array<const u8*, 16> vertex_attribute_sources{};
    std::array<u32, 16> vertex_attribute_strides{};
    std::array<VertexLoaderUtils::AttributeLoader, 16> vertex_attribute_loaders{};
    std::array<bool, 16> vertex_attribute_is_default;
    int num_total_attributes = 0;
};

} // namespace Pica
