// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "convolution_components.hpp"

#include <gsl/gsl_assert>
#include <gsl/span_ext>
#include <ngraph/validation_util.hpp>

namespace CUDAPlugin::Convolution::Details {

constexpr int CONV_1D_DIMS_NUMBER = NON_SPATIAL_DIMS_NUMBER + 1;

template <typename TConvNode>
ConvolutionParams::ConvolutionParams(const TConvNode& node)
    : element_type_{ node.get_input_element_type(ConvArgIndices::input) }
    , input_shape_{ node.get_input_shape(ConvArgIndices::input) }
    , filter_shape_{ node.get_input_shape(ConvArgIndices::filter) }
    , output_shape_{ node.get_output_shape(ConvArgIndices::output) }
    , strides_{ node.get_strides() }
    , dilations_{ node.get_dilations() }
    , padding_before_{ node.get_pads_begin() }
    , padding_after_{ node.get_pads_end() }
{
    Expects(input_shape_.size() > NON_SPATIAL_DIMS_NUMBER);
    Expects(element_type_ == node.get_input_element_type(ConvArgIndices::filter));
    Expects(element_type_ == node.get_output_element_type(ConvArgIndices::output));

    InferPadding(node);

    if (input_shape_.size() == CONV_1D_DIMS_NUMBER)
        ConvertConv1DToConv2D();

    const size_t dims_number = NumberOfDims();
    Ensures(input_shape_.size() == dims_number);
    Ensures(filter_shape_.size() == dims_number);
    Ensures(output_shape_.size() == dims_number);

    const size_t spatial_dims_number = NumberOfSpatialDims();
    // Convolution dimension according to op spec is 1D, 2D or 3D.
    // 1D is already turned into 2D at this point.
    Ensures((spatial_dims_number == 2) || (spatial_dims_number == 3));
    Ensures(strides_.size() == spatial_dims_number);
    Ensures(dilations_.size() == spatial_dims_number);
    Ensures(padding_before_.size() == spatial_dims_number);
    Ensures(padding_after_.size() == spatial_dims_number);
}
template ConvolutionParams::ConvolutionParams(const ngraph::op::v1::Convolution& node);
template ConvolutionParams::ConvolutionParams(const nodes::FusedConv2D& node);

template <typename TConvNode>
void ConvolutionParams::InferPadding(const TConvNode& node) {
    const ngraph::op::PadType pad_type = node.get_auto_pad();
    switch (pad_type) {
        case ngraph::op::PadType::EXPLICIT:
            break;
        case ngraph::op::PadType::SAME_LOWER:
        case ngraph::op::PadType::SAME_UPPER:
        {
            const ngraph::Shape filter_spatial_shape {filter_shape_.begin() + NON_SPATIAL_DIMS_NUMBER,
                                                      filter_shape_.end()};
            padding_before_.clear();
            padding_after_.clear();
            ngraph::infer_auto_padding(input_shape_, filter_spatial_shape, strides_, dilations_,
                                       pad_type, padding_before_, padding_after_);
        } break;
        case ngraph::op::PadType::VALID:
        {
            size_t spatial_dims_number = NumberOfSpatialDims();
            padding_before_ = ngraph::CoordinateDiff(spatial_dims_number, 0);
            padding_after_ = ngraph::CoordinateDiff(spatial_dims_number, 0);
        } break;
        default:
            Expects(false);
    }
}

void ConvolutionParams::ConvertConv1DToConv2D() {
    if (input_shape_.size() != CONV_1D_DIMS_NUMBER)
        return;

    Expects(input_shape_.size() == CONV_1D_DIMS_NUMBER);
    input_shape_.insert(input_shape_.begin() + NON_SPATIAL_DIMS_NUMBER, 1);
    Expects(filter_shape_.size() == CONV_1D_DIMS_NUMBER);
    filter_shape_.insert(filter_shape_.begin() + NON_SPATIAL_DIMS_NUMBER, 1);
    Expects(output_shape_.size() == CONV_1D_DIMS_NUMBER);
    output_shape_.insert(output_shape_.begin() + NON_SPATIAL_DIMS_NUMBER, 1);
    strides_.insert(strides_.begin(), 1);
    dilations_.insert(dilations_.begin(), 1);
    padding_before_.insert(padding_before_.begin(), 0);
    padding_after_.insert(padding_after_.begin(), 0);
}

template <typename TConvNode>
ConvolutionBackwardDataParams::ConvolutionBackwardDataParams(
    const TConvNode& node)
    : element_type_{ node.get_input_element_type(ConvBackArgIndices::doutput) }
    , doutput_shape_{ node.get_input_shape(ConvBackArgIndices::doutput) }
    , filter_shape_{ node.get_input_shape(ConvBackArgIndices::filter) }
    , dinput_shape_{ static_cast<const ngraph::Node&>(node).get_output_shape(0) }
    , strides_{ node.get_strides() }
    , dilations_{ node.get_dilations() }
    , pads_begin_{ node.get_pads_begin() }
    , pads_end_{ node.get_pads_end() }
    , auto_pad_{ node.get_auto_pad() }
    , output_padding_{ node.get_output_padding() } {
    Expects(doutput_shape_.size() > NON_SPATIAL_DIMS_NUMBER);
    Expects(element_type_ == node.get_input_element_type(ConvBackArgIndices::filter));
    Expects(element_type_ == node.get_output_element_type(ConvBackArgIndices::dinput));

    if (doutput_shape_.size() == CONV_1D_DIMS_NUMBER) {
        ConvertConv1DToConv2D();
    }

    const size_t dims_number = NumberOfDims();
    Ensures(doutput_shape_.size() == dims_number);
    Ensures(filter_shape_.size() == dims_number);
    Ensures(dinput_shape_.size() == dims_number);

    const size_t spatial_dims_number = NumberOfSpatialDims();
    // Convolution dimension according to op spec is 1D, 2D or 3D.
    // 1D is already turned into 2D at this point.
    Ensures((spatial_dims_number == 2) || (spatial_dims_number == 3));
    Ensures(strides_.size() == spatial_dims_number);
    Ensures(dilations_.size() == spatial_dims_number);
    Ensures(pads_begin_.size() == spatial_dims_number);
    Ensures(pads_end_.size() == spatial_dims_number);
}
template ConvolutionBackwardDataParams::ConvolutionBackwardDataParams(const ngraph::op::v1::ConvolutionBackpropData& node);
template ConvolutionBackwardDataParams::ConvolutionBackwardDataParams(const nodes::FusedConvBackpropData2D& node);

void ConvolutionBackwardDataParams::ConvertConv1DToConv2D() {
    if (doutput_shape_.size() != CONV_1D_DIMS_NUMBER)
        return;

    Expects(doutput_shape_.size() == CONV_1D_DIMS_NUMBER);
    doutput_shape_.insert(doutput_shape_.begin() + NON_SPATIAL_DIMS_NUMBER, 1);
    Expects(filter_shape_.size() == CONV_1D_DIMS_NUMBER);
    filter_shape_.insert(filter_shape_.begin() + NON_SPATIAL_DIMS_NUMBER, 1);
    Expects(dinput_shape_.size() == CONV_1D_DIMS_NUMBER);
    dinput_shape_.insert(dinput_shape_.begin() + NON_SPATIAL_DIMS_NUMBER, 1);
    strides_.insert(strides_.begin(), 1);
    dilations_.insert(dilations_.begin(), 1);
    pads_begin_.insert(pads_begin_.begin(), 0);
    pads_end_.insert(pads_end_.begin(), 0);
}

FusedConvolutionParams::FusedConvolutionParams(
    const CUDAPlugin::nodes::FusedConv2D& node)
    : conv_{ node }
    , bias_shape_{ node.get_input_shape(FusedConvolutionIndices::bias) }
    , activation_{ node.get_activation() } {
    Expects(conv_.NumberOfSpatialDims() == 2);
    Expects(conv_.element_type_ == node.get_input_element_type(FusedConvolutionIndices::bias));
    if (node.inputs().size() == 4) {
        add_shape_ = node.get_input_shape(FusedConvolutionIndices::add);
    }
}

FusedConvolutionBackwardDataParams::FusedConvolutionBackwardDataParams(
    const CUDAPlugin::nodes::FusedConvBackpropData2D& node)
    : conv_{ node } {
    Expects(conv_.NumberOfSpatialDims() == 2 || conv_.NumberOfSpatialDims() == 3);
    if (node.inputs().size() == 4) {
        add_shape_ = node.get_input_shape(FusedConvolutionBackwardDataIndices<4>::add);
        Expects(conv_.element_type_ == node.get_input_element_type(FusedConvolutionBackwardDataIndices<4>::add));
    } else {
        add_shape_ = node.get_input_shape(FusedConvolutionBackwardDataIndices<3>::add);
        Expects(conv_.element_type_ == node.get_input_element_type(FusedConvolutionBackwardDataIndices<3>::add));
    }
}

} // namespace CUDAPlugin
