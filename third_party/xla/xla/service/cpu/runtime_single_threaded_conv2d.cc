/* Copyright 2017 The OpenXLA Authors.

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

#include "xla/service/cpu/runtime_single_threaded_conv2d.h"

#include <cstddef>
#include <cstdint>

#include "absl/base/attributes.h"
#include "xla/backends/cpu/runtime/convolution_thunk_internal.h"

ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY void
__xla_cpu_runtime_EigenSingleThreadedConv2DF16(
    const void* /*run_options_ptr*/, Eigen::half* out, Eigen::half* lhs,
    Eigen::half* rhs, int64_t input_batch, int64_t input_rows,
    int64_t input_cols, int64_t input_channels, int64_t kernel_rows,
    int64_t kernel_cols, int64_t kernel_channels, int64_t kernel_filters,
    int64_t output_rows, int64_t output_cols, int64_t row_stride,
    int64_t col_stride, int64_t padding_top, int64_t padding_bottom,
    int64_t padding_left, int64_t padding_right, int64_t lhs_row_dilation,
    int64_t lhs_col_dilation, int64_t rhs_row_dilation,
    int64_t rhs_col_dilation, int64_t feature_group_count) {
  // TODO(adambanas): This value should be read from debug options. For now, we
  // use a default value of 8 GiB.
  size_t max_workspace_size = (size_t)8 << 30;

  xla::cpu::internal::EigenConv2D(
      Eigen::DefaultDevice(), out, lhs, rhs, input_batch, input_rows,
      input_cols, input_channels, kernel_rows, kernel_cols, kernel_channels,
      kernel_filters, output_rows, output_cols, row_stride, col_stride,
      padding_top, padding_bottom, padding_left, padding_right,
      lhs_row_dilation, lhs_col_dilation, rhs_row_dilation, rhs_col_dilation,
      feature_group_count, nullptr, /*use_thunk_runtime=*/false,
      max_workspace_size);
}

ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY void
__xla_cpu_runtime_EigenSingleThreadedConv2DF32(
    const void* /*run_options_ptr*/, float* out, float* lhs, float* rhs,
    int64_t input_batch, int64_t input_rows, int64_t input_cols,
    int64_t input_channels, int64_t kernel_rows, int64_t kernel_cols,
    int64_t kernel_channels, int64_t kernel_filters, int64_t output_rows,
    int64_t output_cols, int64_t row_stride, int64_t col_stride,
    int64_t padding_top, int64_t padding_bottom, int64_t padding_left,
    int64_t padding_right, int64_t lhs_row_dilation, int64_t lhs_col_dilation,
    int64_t rhs_row_dilation, int64_t rhs_col_dilation,
    int64_t feature_group_count) {
  // TODO(adambanas): This value should be read from debug options. For now, we
  // use a default value of 8 GiB.
  size_t max_workspace_size = (size_t)8 << 30;

  xla::cpu::internal::EigenConv2D(
      Eigen::DefaultDevice(), out, lhs, rhs, input_batch, input_rows,
      input_cols, input_channels, kernel_rows, kernel_cols, kernel_channels,
      kernel_filters, output_rows, output_cols, row_stride, col_stride,
      padding_top, padding_bottom, padding_left, padding_right,
      lhs_row_dilation, lhs_col_dilation, rhs_row_dilation, rhs_col_dilation,
      feature_group_count, nullptr, /*use_thunk_runtime=*/false,
      max_workspace_size);
}
