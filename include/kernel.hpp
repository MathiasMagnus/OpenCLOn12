// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once

#include "program.hpp"
#include "resources.hpp"

class Sampler;
class Kernel : public CLChildBase<Kernel, Program, cl_kernel>
{
private:
    clc_dxil_object const* m_pDxil;
    D3D12TranslationLayer::SShaderDecls m_ShaderDecls;
    D3D12TranslationLayer::RootSignature m_RootSig;

    std::vector<D3D12TranslationLayer::UAV*> m_UAVs;
    std::vector<D3D12TranslationLayer::SRV*> m_SRVs;
    std::vector<D3D12TranslationLayer::Sampler*> m_Samplers;
    std::vector<byte> m_KernelArgsCbData;
    std::vector<struct clc_runtime_arg_info> m_ArgMetadataToCompiler;

    std::vector<::ref_ptr<Sampler>> m_ConstSamplers;
    std::vector<::ref_ptr<Resource>> m_InlineConsts;

    friend class ExecuteKernel;
    friend extern CL_API_ENTRY cl_int CL_API_CALL clGetKernelInfo(cl_kernel, cl_kernel_info, size_t, void*, size_t*);
    friend extern CL_API_ENTRY cl_int CL_API_CALL clGetKernelArgInfo(cl_kernel, cl_uint, cl_kernel_arg_info, size_t, void*, size_t*);

public:
    Kernel(Program& Parent, clc_dxil_object const* pDxil);
    ~Kernel();

    cl_int SetArg(cl_uint arg_index, size_t arg_size, const void* arg_value);

    uint16_t const* GetRequiredLocalDims() const;
    uint16_t const* GetLocalDimsHint() const;
};
