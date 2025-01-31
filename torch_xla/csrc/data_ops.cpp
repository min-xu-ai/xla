#include "torch_xla/csrc/data_ops.h"

#include <algorithm>
#include <functional>
#include <numeric>

#include "absl/strings/str_join.h"
#include "tensorflow/compiler/xla/util.h"
#include "tensorflow/compiler/xla/xla_client/debug_macros.h"
#include "tensorflow/compiler/xla/xla_client/util.h"
#include "tensorflow/core/lib/gtl/array_slice.h"
#include "torch_xla/csrc/helpers.h"
#include "torch_xla/csrc/tensor_util.h"

namespace torch_xla {

std::vector<xla::int64> GetCompleteShape(
    tensorflow::gtl::ArraySlice<const xla::int64> output_sizes,
    tensorflow::gtl::ArraySlice<const xla::int64> input_sizes) {
  c10::optional<size_t> incomplete_dim;
  xla::int64 incomplete_element_count = 1;
  for (size_t dim = 0; dim < output_sizes.size(); ++dim) {
    xla::int64 dim_size = output_sizes[dim];
    if (dim_size < 0) {
      XLA_CHECK(!incomplete_dim)
          << "More than one incomplete dimension found: " << *incomplete_dim
          << " and " << dim;
      incomplete_dim = dim;
    } else {
      incomplete_element_count *= dim_size;
    }
  }
  xla::int64 total_element_count = xla::util::Multiply<xla::int64>(input_sizes);
  if (!incomplete_dim) {
    XLA_CHECK_EQ(total_element_count,
                 xla::util::Multiply<xla::int64>(output_sizes))
        << "[" << absl::StrJoin(output_sizes, ", ") << "] vs. ["
        << absl::StrJoin(input_sizes, ", ") << "]";
    return xla::util::ToVector<xla::int64>(output_sizes);
  }
  XLA_CHECK_EQ(total_element_count % incomplete_element_count, 0)
      << "[" << absl::StrJoin(output_sizes, ", ") << "] vs. ["
      << absl::StrJoin(input_sizes, ", ") << "]";
  std::vector<xla::int64> complete_output_sizes =
      xla::util::ToVector<xla::int64>(output_sizes);
  complete_output_sizes[*incomplete_dim] =
      total_element_count / incomplete_element_count;
  return complete_output_sizes;
}

xla::XlaOp BuildView(
    const xla::XlaOp& input,
    tensorflow::gtl::ArraySlice<const xla::int64> output_sizes) {
  const auto complete_output_sizes =
      GetCompleteShape(output_sizes, XlaHelpers::SizesOfXlaOp(input));
  return xla::Reshape(input, complete_output_sizes);
}

xla::XlaOp SqueezeTrivialDimension(const xla::XlaOp& input, size_t dim) {
  auto input_sizes = XlaHelpers::SizesOfXlaOp(input);
  XLA_CHECK_LT(dim, input_sizes.size());
  if (input_sizes[dim] != 1) {
    return input;
  }
  input_sizes.erase(input_sizes.begin() + dim);
  return xla::Reshape(input, input_sizes);
}

xla::XlaOp SqueezeAllTrivialDimensions(const xla::XlaOp& input) {
  auto input_sizes = XlaHelpers::SizesOfXlaOp(input);
  // Squeeze the trivial (of size 1) dimensions.
  std::vector<xla::int64> non_singleton_dimensions;
  std::copy_if(input_sizes.begin(), input_sizes.end(),
               std::back_inserter(non_singleton_dimensions),
               [](const size_t dim_size) { return dim_size != 1; });
  return xla::Reshape(input, non_singleton_dimensions);
}

xla::XlaOp BuildExpand(
    const xla::XlaOp& input,
    tensorflow::gtl::ArraySlice<const xla::int64> output_sizes) {
  auto input_sizes = XlaHelpers::SizesOfXlaOp(input);
  // Adjust the rank of the input to match the rank of the output.
  XLA_CHECK_LE(input_sizes.size(), output_sizes.size());
  input_sizes.insert(input_sizes.begin(),
                     output_sizes.size() - input_sizes.size(), 1);
  xla::XlaOp implicit_reshape = xla::Reshape(input, input_sizes);
  return xla::BroadcastInDim(implicit_reshape, output_sizes,
                             xla::util::Iota<xla::int64>(output_sizes.size()));
}

std::vector<xla::int64> BuildUnsqueezeDimensions(
    tensorflow::gtl::ArraySlice<const xla::int64> dimensions, size_t dim) {
  XLA_CHECK_LE(dim, dimensions.size());
  auto unsqueeze_dimensions = xla::util::ToVector<xla::int64>(dimensions);
  unsqueeze_dimensions.insert(unsqueeze_dimensions.begin() + dim, 1);
  return unsqueeze_dimensions;
}

xla::XlaOp BuildUnsqueeze(const xla::XlaOp& input, size_t dim) {
  auto dimensions =
      BuildUnsqueezeDimensions(XlaHelpers::SizesOfXlaOp(input), dim);
  return xla::Reshape(input, dimensions);
}

xla::XlaOp BuildStack(tensorflow::gtl::ArraySlice<const xla::XlaOp> inputs,
                      xla::int64 dim) {
  // Reshape inputs along the dim axis.
  XLA_CHECK_GT(inputs.size(), 0);
  std::vector<xla::XlaOp> reshaped_inputs;
  for (size_t i = 0; i < inputs.size(); ++i) {
    auto input_size = XlaHelpers::SizesOfXlaOp(inputs[i]);
    input_size.insert(input_size.begin() + dim, 1);
    reshaped_inputs.push_back(xla::Reshape(inputs[i], input_size));
  }
  return xla::ConcatInDim(inputs[0].builder(), reshaped_inputs, dim);
}

xla::XlaOp BuildCat(tensorflow::gtl::ArraySlice<const xla::XlaOp> inputs,
                    xla::int64 dim) {
  XLA_CHECK_GT(inputs.size(), 0);
  return xla::ConcatInDim(inputs[0].builder(), inputs, dim);
}

xla::XlaOp BuildRepeat(const xla::XlaOp& input,
                       tensorflow::gtl::ArraySlice<const xla::int64> repeats) {
  const auto input_sizes = XlaHelpers::SizesOfXlaOp(input);
  XLA_CHECK_GE(repeats.size(), input_sizes.size())
      << "Number of dimensions of repeat dims can not be smaller than number "
         "of dimensions of tensor";
  size_t broadcast_dims = repeats.size() - input_sizes.size();
  xla::XlaOp repeated = input;
  for (size_t dim = 0; dim < input_sizes.size(); ++dim) {
    std::vector<xla::XlaOp> repeated_inputs(repeats[broadcast_dims + dim],
                                            repeated);
    repeated = xla::ConcatInDim(input.builder(), repeated_inputs, dim);
  }
  if (repeats.size() > input_sizes.size()) {
    std::vector<xla::int64> remaining_repeats(repeats.begin(),
                                              repeats.begin() + broadcast_dims);
    repeated = xla::Broadcast(repeated, remaining_repeats);
  }
  return repeated;
}

size_t ComputeSplitCount(
    xla::int64 dim_size,
    tensorflow::gtl::ArraySlice<const xla::int64> split_sizes) {
  size_t count = 0;
  for (auto size : split_sizes) {
    if (size > dim_size) {
      break;
    }
    dim_size -= size;
    ++count;
  }
  return count;
}

std::vector<xla::XlaOp> BuildSplit(
    const xla::XlaOp& input,
    tensorflow::gtl::ArraySlice<const xla::int64> split_sizes, xla::int64 dim) {
  const auto input_sizes = XlaHelpers::SizesOfXlaOp(input);
  xla::int64 dim_size = input_sizes.at(dim);
  xla::int64 index = 0;
  std::vector<xla::XlaOp> splits;
  for (auto size : split_sizes) {
    if (index + size > dim_size) {
      break;
    }
    splits.emplace_back(SliceInDim(input, index, index + size, 1, dim));
    index += size;
  }
  return splits;
}

xla::XlaOp BuildUpdateSlice(
    const xla::XlaOp& input, const xla::XlaOp& source,
    tensorflow::gtl::ArraySlice<const xla::int64> base_indices) {
  xla::Shape input_shape = XlaHelpers::ShapeOfXlaOp(input);
  xla::XlaOp reshaped_source =
      XlaHelpers::ReshapeToRank(source, input_shape.rank());
  std::vector<xla::XlaOp> start_indices;
  for (auto index : base_indices) {
    start_indices.push_back(
        XlaHelpers::ScalarValue<xla::int64>(index, input.builder()));
  }
  return xla::DynamicUpdateSlice(input, reshaped_source, start_indices);
}

xla::XlaOp BuildSlice(
    const xla::XlaOp& input,
    tensorflow::gtl::ArraySlice<const xla::int64> base_indices,
    tensorflow::gtl::ArraySlice<const xla::int64> sizes) {
  XLA_CHECK_EQ(base_indices.size(), sizes.size());
  std::vector<xla::int64> limit_indices(base_indices.begin(),
                                        base_indices.end());
  std::transform(limit_indices.begin(), limit_indices.end(), sizes.begin(),
                 limit_indices.begin(), std::plus<xla::int64>());
  std::vector<xla::int64> strides(base_indices.size(), 1);
  return xla::Slice(input, base_indices, limit_indices, strides);
}

xla::XlaOp BuildResize(const xla::XlaOp& input,
                       tensorflow::gtl::ArraySlice<const xla::int64> size) {
  xla::Shape input_shape = XlaHelpers::ShapeOfXlaOp(input);
  xla::int64 num_elements = xla::ShapeUtil::ElementsIn(input_shape);
  xla::XlaOp r1_input = xla::Reshape(input, {num_elements});
  xla::int64 new_num_elements = xla::util::Multiply<xla::int64>(size);
  xla::XlaOp resized_input = input;
  if (num_elements > new_num_elements) {
    resized_input = xla::SliceInDim(r1_input, 0, new_num_elements, 1, 0);
  } else if (new_num_elements > num_elements) {
    xla::XlaOp zero =
        XlaHelpers::ScalarValue(0, input_shape.element_type(), input.builder());
    xla::PaddingConfig padding_config;
    auto* dims = padding_config.add_dimensions();
    dims->set_edge_padding_low(0);
    dims->set_interior_padding(0);
    dims->set_edge_padding_high(new_num_elements - num_elements);
    resized_input = xla::Pad(r1_input, zero, padding_config);
  }
  return xla::Reshape(resized_input, size);
}

xla::XlaOp BuildUnselect(const xla::XlaOp& target, const xla::XlaOp& source,
                         xla::int64 dim, xla::int64 start, xla::int64 end,
                         xla::int64 stride) {
  xla::Shape target_shape = XlaHelpers::ShapeOfXlaOp(target);
  xla::Shape source_shape = XlaHelpers::ShapeOfXlaOp(source);
  if (target_shape.dimensions(dim) == source_shape.dimensions(dim)) {
    // Shortcut for unselects which are fully covering selects.
    XLA_CHECK_EQ(start, 0);
    XLA_CHECK_EQ(stride, 1);
    XLA_CHECK_EQ(end, target_shape.dimensions(dim));
    return source;
  }

  xla::PrimitiveType pred_type =
      GetDevicePrimitiveType(xla::PrimitiveType::PRED, /*device=*/nullptr);
  xla::XlaOp source_true = XlaHelpers::ScalarBroadcast(
      1, pred_type, source_shape.dimensions(), source.builder());
  xla::XlaOp pred_zero =
      XlaHelpers::ScalarValue(0, pred_type, target.builder());
  xla::XlaOp zero =
      XlaHelpers::ScalarValue(0, target_shape.element_type(), target.builder());
  xla::PaddingConfig padding_config;
  for (xla::int64 i = 0; i < target_shape.rank(); ++i) {
    auto* dims = padding_config.add_dimensions();
    if (i == dim) {
      dims->set_edge_padding_low(start);
      dims->set_interior_padding(stride - 1);

      xla::int64 size = start + source_shape.dimensions(i) +
                        (source_shape.dimensions(i) - 1) * (stride - 1);
      dims->set_edge_padding_high(target_shape.dimensions(i) - size);
    } else {
      XLA_CHECK_EQ(target_shape.dimensions(i), source_shape.dimensions(i))
          << target_shape << " vs. " << source_shape;
      dims->set_edge_padding_low(0);
      dims->set_interior_padding(0);
      dims->set_edge_padding_high(0);
    }
  }
  xla::XlaOp padded_source = xla::Pad(source, zero, padding_config);
  xla::XlaOp mask = xla::Pad(source_true, pred_zero, padding_config);
  return xla::Select(mask, padded_source, target);
}

}  // namespace torch_xla
