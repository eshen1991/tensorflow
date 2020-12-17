/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/delegates/gpu/metal/kernels/concat.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tensorflow/lite/delegates/gpu/common/model.h"
#include "tensorflow/lite/delegates/gpu/common/operations.h"
#include "tensorflow/lite/delegates/gpu/common/shape.h"
#include "tensorflow/lite/delegates/gpu/common/types.h"
#include "tensorflow/lite/delegates/gpu/common/util.h"
#include "tensorflow/lite/delegates/gpu/metal/compute_task_descriptor.h"

namespace tflite {
namespace gpu {
namespace metal {
namespace {

bool IsAllChannelsX4(const std::vector<int>& channels) {
  for (int channel : channels) {
    if (channel % 4 != 0) {
      return false;
    }
  }
  return true;
}

std::string GetConcatZCode(const std::vector<int> channels) {
  const std::string postfix[] = {".x", ".y", ".z", ".w"};
  std::string c = R"(
    #include <metal_stdlib>
    using namespace metal;
    struct uniforms {
      int4 src_size;
      int4 dst_size;
    };

    $0
    kernel void ComputeFunction(
                                $1
                                uint2 ugid[[thread_position_in_grid]]) {
  int X = static_cast<int>(ugid.x);
  int Y = static_cast<int>(ugid.y);
  int Z = 0;
  if (X >= U.dst_size.x || Y >= U.dst_size.y) return;

  FLT4 value = FLT4(0.0f);
  const int xy_offset = Y * U.src_size.x + X;
  int linear_index = xy_offset;
)";

  if (IsAllChannelsX4(channels)) {
    // When all channels % 4 == 0 we can read/assign/write FLT4 elements easily.
    // Also it is easy to write a loop in this case, to prevent long kernel
    // generation.
    for (int i = 0; i < channels.size(); ++i) {
      const int depth = DivideRoundUp(channels[i], 4);
      const std::string src_tensor = "src_tensor" + std::to_string(i);
      c += "  for (int i = 0; i < " + std::to_string(depth) + "; ++i) {\n";
      c += "    int src_index = i * U.src_size.w + xy_offset;\n";
      c += "    value = " + src_tensor + "[src_index];\n";
      c += "    uint3 gid = uint3(ugid.x, ugid.y, uint(Z));\n";
      c += "    $2\n";
      c += "    dst_tensor[linear_index] = value;\n";
      c += "    linear_index += U.src_size.w;\n";
      c += "    Z++;\n";
      c += "  }\n";
    }
  } else {
    int out_channel = 0;
    int read_index = 0;
    int z = 0;
    for (int i = 0; i < channels.size(); ++i) {
      const int depth = DivideRoundUp(channels[i], 4);
      const std::string src_tensor = "src_tensor" + std::to_string(i);
      for (int d = 0; d < depth; ++d) {
        const int channels_in_group = std::min(4, channels[i] - d * 4);
        const std::string temp_name = "t" + std::to_string(read_index);
        const std::string src_index =
            std::to_string(d) + " * U.src_size.w + xy_offset";
        c += "  FLT4 " + temp_name + " = " + src_tensor + "[" + src_index +
             "];\n";
        for (int ch = 0; ch < channels_in_group; ++ch) {
          c += "  value" + postfix[out_channel] + " = ";
          c += temp_name + postfix[ch] + ";\n";
          out_channel++;
          if (out_channel == 4) {
            out_channel = 0;
            c += "  {\n";
            c += "    uint3 gid = uint3(ugid.x, ugid.y, uint(Z));\n";
            c += "    $2\n";
            c += "    dst_tensor[linear_index] = value;\n";
            c += "    linear_index += U.src_size.w;\n";
            c += "    Z++;\n";
            c += "  }\n";
            z++;
          }
        }
        read_index++;
      }
    }
    if (out_channel != 0) {
      c += "  {\n";
      c += "    uint3 gid = uint3(ugid.x, ugid.y, uint(Z));\n";
      c += "    $2\n";
      c += "    dst_tensor[linear_index] = value;\n";
      c += "  }\n";
    }
  }
  c += "}\n";
  return c;
}
}  // namespace

ComputeTaskDescriptor ConcatZ(const OperationDef& definition,
                              const ConcatAttributes& attr,
                              const std::vector<BHWC>& input_shapes) {
  std::vector<int> channels;
  channels.reserve(input_shapes.size());
  for (const auto& shape : input_shapes) {
    channels.push_back(shape.c);
  }
  ComputeTaskDescriptor desc(definition);
  desc.shader_source = GetConcatZCode(channels);

  for (int i = 0; i < definition.src_tensors.size(); ++i) {
    desc.AddSrcTensor("src_tensor" + std::to_string(i),
                      definition.src_tensors[i]);
  }
  desc.AddDstTensor("dst_tensor", definition.dst_tensors[0]);

  desc.uniform_buffers = {
      {"constant uniforms& U",
       [](const std::vector<BHWC>& src_shapes,
          const std::vector<BHWC>& dst_shapes) {
         std::vector<int> uniform_params{
             src_shapes[0].w,
             src_shapes[0].h,
             DivideRoundUp(src_shapes[0].c, 4),
             src_shapes[0].w * src_shapes[0].h,
             dst_shapes[0].w,
             dst_shapes[0].h,
             DivideRoundUp(dst_shapes[0].c, 4),
             dst_shapes[0].w * dst_shapes[0].h,
         };
         return GetByteBuffer(uniform_params);
       }},
  };

  desc.resize_function = [](const std::vector<BHWC>& src_shapes,
                            const std::vector<BHWC>& dst_shapes) {
    uint3 grid(dst_shapes[0].w, dst_shapes[0].h, 1);
    uint3 group_size{8u, 4u, 1u};
    uint3 groups;
    groups.x = DivideRoundUp(grid.x, group_size.x);
    groups.y = DivideRoundUp(grid.y, group_size.y);
    groups.z = DivideRoundUp(grid.z, group_size.z);
    return std::make_pair(group_size, groups);
  };

  return desc;
}

ComputeTaskDescriptor ConcatX(const OperationDef& definition,
                              const ConcatAttributes& attr,
                              const std::vector<BHWC>& input_shapes) {
  ComputeTaskDescriptor desc(definition);
  std::string code = R"(
    #include <metal_stdlib>
    using namespace metal;
    $0
    kernel void ComputeFunction(
                                $1
                                uint3 gid[[thread_position_in_grid]]) {
      if (int(gid.x) >= size.x || int(gid.y) >= size.y) {
        return;
      }
      FLT4 value;
    )";
  int output_width = 0;
  for (int buffer_index = 0; buffer_index < input_shapes.size();
       buffer_index++) {
    const auto& dims = input_shapes[buffer_index];
    output_width += dims.w;

    // Generated shader example:
    // if (gid.x < 10) value = src_tensor0[(gid.y + gid.z * 3) * 4 + gid.x - 3];
    // else
    if (buffer_index < input_shapes.size() - 1) {
      code += "if (gid.x < " + std::to_string(output_width) + ")";
    }
    code += "value = src_tensor" + std::to_string(buffer_index) +
            "[(gid.y + gid.z * " + std::to_string(dims.h) + ") * " +
            std::to_string(dims.w) + " + gid.x - " +
            std::to_string(output_width - dims.w) + "];\n";
    if (buffer_index < input_shapes.size() - 1) {
      code += "else ";
    }
  }
  code += "const int linear_index = (gid.y + gid.z * " +
          std::to_string(input_shapes[0].h) + ") * " +
          std::to_string(output_width) + " + gid.x;";
  code += R"(
      $2
      dst_tensor[linear_index] = value;
    }
  )";
  desc.shader_source = code;

  for (int i = 0; i < input_shapes.size(); ++i) {
    desc.AddSrcTensor("src_tensor" + std::to_string(i),
                      definition.src_tensors[i]);
  }
  desc.AddDstTensor("dst_tensor", definition.dst_tensors[0]);

  desc.uniform_buffers = {
      {"constant int3& size",
       [](const std::vector<BHWC>& src_shapes,
          const std::vector<BHWC>& dst_shapes) {
         std::vector<int> uniform_params{dst_shapes[0].w, dst_shapes[0].h,
                                         DivideRoundUp(dst_shapes[0].c, 4),
                                         /*padding=*/0};
         return GetByteBuffer(uniform_params);
       }},
  };

  desc.resize_function = [](const std::vector<BHWC>& src_shapes,
                            const std::vector<BHWC>& dst_shapes) {
    const uint3 groups_size{8, 4, 1};
    int groups_x = DivideRoundUp(dst_shapes[0].w, groups_size.x);
    int groups_y = DivideRoundUp(dst_shapes[0].h, groups_size.y);
    int groups_z = DivideRoundUp(dst_shapes[0].c, 4);
    return std::make_pair(groups_size, uint3{groups_x, groups_y, groups_z});
  };

  return desc;
}

ComputeTaskDescriptor ConcatY(const OperationDef& definition,
                              const ConcatAttributes& attr,
                              const std::vector<BHWC>& input_shapes) {
  ComputeTaskDescriptor desc(definition);
  std::string code = R"(
    #include <metal_stdlib>
    using namespace metal;
    $0
    kernel void ComputeFunction(
                                $1
                                uint3 gid[[thread_position_in_grid]]) {
      if (int(gid.x) >= size.x || int(gid.y) >= size.y) {
        return;
      }
      FLT4 value;
  )";
  int output_height = 0;
  for (int buffer_index = 0; buffer_index < input_shapes.size();
       buffer_index++) {
    const auto& dims = input_shapes[buffer_index];
    output_height += dims.h;

    // Generated shader example:
    // if (gid.y < 10) value = src_tensor0[(gid.y - 3 + gid.z * 5) * 4 + gid.x];
    // else
    if (buffer_index < input_shapes.size() - 1) {
      code += "if (gid.y < " + std::to_string(output_height) + ")";
    }
    code += "value = src_tensor" + std::to_string(buffer_index) + "[(gid.y - " +
            std::to_string(output_height - dims.h) + " + gid.z * " +
            std::to_string(dims.h) + ") * " + std::to_string(dims.w) +
            " + gid.x];\n";
    if (buffer_index < input_shapes.size() - 1) {
      code += "else ";
    }
  }
  const auto& dims = input_shapes[0];
  code += "const int linear_index = (gid.y + gid.z * " +
          std::to_string(output_height) + ") * " + std::to_string(dims.w) +
          " + gid.x;";
  code += R"(
      $2
      dst_tensor[linear_index] = value;
    }
  )";
  desc.shader_source = code;

  for (int i = 0; i < input_shapes.size(); ++i) {
    desc.AddSrcTensor("src_tensor" + std::to_string(i),
                      definition.src_tensors[i]);
  }
  desc.AddDstTensor("dst_tensor", definition.dst_tensors[0]);

  desc.uniform_buffers = {
      {"constant int3& size",
       [](const std::vector<BHWC>& src_shapes,
          const std::vector<BHWC>& dst_shapes) {
         std::vector<int> uniform_params{dst_shapes[0].w, dst_shapes[0].h,
                                         DivideRoundUp(dst_shapes[0].c, 4),
                                         /*padding=*/0};
         return GetByteBuffer(uniform_params);
       }},
  };

  desc.resize_function = [](const std::vector<BHWC>& src_shapes,
                            const std::vector<BHWC>& dst_shapes) {
    const uint3 groups_size{8, 4, 1};
    int groups_x = DivideRoundUp(dst_shapes[0].w, groups_size.x);
    int groups_y = DivideRoundUp(dst_shapes[0].h, groups_size.y);
    int groups_z = DivideRoundUp(dst_shapes[0].c, 4);
    return std::make_pair(groups_size, uint3{groups_x, groups_y, groups_z});
  };

  return desc;
}

ComputeTaskDescriptor Concat(const OperationDef& definition,
                             const ConcatAttributes& attr,
                             const std::vector<BHWC>& input_shapes) {
  if (attr.axis == Axis::CHANNELS) {
    return ConcatZ(definition, attr, input_shapes);
  } else if (attr.axis == Axis::WIDTH) {
    return ConcatX(definition, attr, input_shapes);
  } else {
    return ConcatY(definition, attr, input_shapes);
  }
}

}  // namespace metal
}  // namespace gpu
}  // namespace tflite
