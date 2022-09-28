// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "ppl/nn/engines/cuda/optimizer/ops/onnx/topk_op.h"

#include "ppl/nn/common/logger.h"
#include "ppl/nn/engines/cuda/kernels/onnx/topk_kernel.h"
#include "ppl/nn/oputils/onnx/reshape_topk.h"

using namespace std;
using namespace ppl::common;
using namespace ppl::nn::onnx;

#ifdef PPLNN_ENABLE_PMX_MODEL
#include "ppl/nn/models/pmx/utils.h"
#include "ppl/nn/models/pmx/oputils/onnx/topk.h"
#endif

namespace ppl { namespace nn { namespace cuda {

RetCode TopKOp::Init(const OptKernelOptions& options) {
    auto status = GenericLoadParam<TopKParam>(options, &param_);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "load param failed: " << GetRetCodeStr(status);
        return status;
    }

    return RC_SUCCESS;
}

TopKOp::TopKOp(const ir::Node* node) : CudaOptKernel(node) {
    infer_type_func_ = [](InputOutputInfo* info, std::vector<CudaTensorQuant>* quant, datatype_t type) -> RetCode {
        auto shape = info->GetOutput<TensorImpl>(1)->GetShape();
        shape->SetDataType(ppl::common::DATATYPE_INT32);
        if (info->GetInputCount() == 2) {
            shape = info->GetInput<TensorImpl>(1)->GetShape();
            shape->SetDataType(ppl::common::DATATYPE_INT64);
        } else {
            return ppl::common::RC_UNSUPPORTED;
        }
        return RC_SUCCESS;
    };

    infer_dims_func_ = [this](InputOutputInfo* info) -> RetCode {
        if (info->GetInputCount() != 2 || info->GetOutputCount() != 2) {
            return RC_INVALID_VALUE;
        }
        if (!info->GetInput<TensorImpl>(1)->GetBufferPtr()) {
            return RC_NOT_FOUND;
        }

        int64_t k;
        auto status = info->GetInput<TensorImpl>(1)->CopyToHost(&k);
        if (status != RC_SUCCESS) {
            LOG(ERROR) << "Copy value k failed: " << GetRetCodeStr(status);
            return status;
        }
        return onnx::ReshapeTopK(info, &param_, k);
    };

    infer_unsafe_dims_func_ = [](InputOutputInfo* info, std::set<uint32_t>* illegal_inputs) -> RetCode {
        auto& in_shape0 = *info->GetInput<TensorImpl>(0)->GetShape();
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            auto& out_shape = *info->GetOutput<TensorImpl>(i)->GetShape();
            out_shape.Reshape(in_shape0.GetDims(), in_shape0.GetRealDimCount());
            out_shape.SetDim(0, 1000);
        }
        return ppl::common::RC_SUCCESS;
    };
}

RetCode TopKOp::Finalize(const OptKernelOptions& options) {
    auto status = SetCommonParam(options);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "load common param failed: " << GetRetCodeStr(status);
        return status;
    }

    return RC_SUCCESS;
}

KernelImpl* TopKOp::CreateKernelImpl() const {
    return CreateKernelImplWithParam<TopKKernel>(&param_);
}

#ifdef PPLNN_ENABLE_PMX_MODEL
    ppl::common::RetCode TopKOp::SerializeData(const pmx::SerializationContext&, utils::DataStream* ds) const {
        flatbuffers::FlatBufferBuilder builder;
        auto fb_param = pmx::onnx::SerializeTopKParam(param_, &builder);
        auto fb_op_param = pmx::onnx::CreateOpParam(builder, pmx::onnx::OpParamType_TopKParam, fb_param.Union());
        pmx::onnx::FinishOpParamBuffer(builder, fb_op_param);
        return ds->Write(builder.GetBufferPointer(), builder.GetSize());
    }
    ppl::common::RetCode TopKOp::DeserializeData(const pmx::DeserializationContext&, const void* base, uint64_t size) {
        auto fb_op_param = pmx::onnx::GetOpParam(base);
        auto fb_argmax_param = fb_op_param->value_as_TopKParam();
        pmx::onnx::DeserializeTopKParam(*fb_argmax_param, &param_);
        return ppl::common::RC_SUCCESS;
    }
#endif

}}} // namespace ppl::nn::cuda
