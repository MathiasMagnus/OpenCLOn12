#pragma once
#include "program.hpp"
#include "clc_compiler.h"
#include "kernel.hpp"
#include "dxcapi.h"

void SignBlob(void* pBlob, size_t size)
{
    auto& DXIL = g_Platform->GetDXIL();
    auto pfnCreateInstance = DXIL.proc_address<decltype(&DxcCreateInstance)>("DxcCreateInstance");
    ComPtr<IDxcValidator> spValidator;
    if (SUCCEEDED(pfnCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(&spValidator))))
    {
        struct Blob : IDxcBlob
        {
            void* pBlob;
            UINT Size;
            Blob(void* p, UINT s) : pBlob(p), Size(s) { }
            STDMETHOD(QueryInterface)(REFIID, void** ppv) { *ppv = this; return S_OK; }
            STDMETHOD_(ULONG, AddRef)() { return 1; }
            STDMETHOD_(ULONG, Release)() { return 0; }
            STDMETHOD_(void*, GetBufferPointer)() override { return pBlob; }
            STDMETHOD_(SIZE_T, GetBufferSize)() override { return Size; }
        } Blob = { pBlob, (UINT)size };
        ComPtr<IDxcOperationResult> spResult;
        (void)spValidator->Validate(&Blob, DxcValidatorFlags_InPlaceEdit, &spResult);
    }
}

extern CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithSource(cl_context        context_,
    cl_uint           count,
    const char** strings,
    const size_t* lengths,
    cl_int* errcode_ret) CL_API_SUFFIX__VERSION_1_0
{
    if (!context_)
    {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return nullptr;
    }
    Context& context = *static_cast<Context*>(context_);
    auto ReportError = context.GetErrorReporter(errcode_ret);

    if (count == 0)
    {
        return ReportError("count is zero.", CL_INVALID_VALUE);
    }

    try
    {
        std::string CompleteProgram;
        for (cl_uint i = 0; i < count; ++i)
        {
            if (strings[i] == nullptr)
            {
                return ReportError("strings contains a NULL entry.", CL_INVALID_VALUE);
            }
            size_t length = (lengths && lengths[i]) ? lengths[i] : strlen(strings[i]);
            CompleteProgram.append(strings[i], length);
        }

        if (errcode_ret) *errcode_ret = CL_SUCCESS;
        return new Program(context, std::move(CompleteProgram));
    }
    catch (std::bad_alloc&) { return ReportError(nullptr, CL_OUT_OF_HOST_MEMORY); }
    catch (std::exception & e) { return ReportError(e.what(), CL_OUT_OF_RESOURCES); }
    catch (_com_error&) { return ReportError(nullptr, CL_OUT_OF_RESOURCES); }
}

extern CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithBinary(cl_context                     context_,
    cl_uint                        num_devices,
    const cl_device_id* device_list,
    const size_t* lengths,
    const unsigned char** binaries,
    cl_int* binary_status,
    cl_int* errcode_ret) CL_API_SUFFIX__VERSION_1_0
{
    if (!context_)
    {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return nullptr;
    }
    Context& context = *static_cast<Context*>(context_);
    auto ReportError = context.GetErrorReporter(errcode_ret);

    if (num_devices == 0 || device_list == nullptr)
    {
        return ReportError("num_devices must not be zero and device_list must not be NULL.", CL_INVALID_VALUE);
    }
    if (num_devices != 1)
    {
        return ReportError("This platform only supports 1 device per context.", CL_INVALID_DEVICE);
    }

    Device* device = static_cast<Device*>(device_list[0]);
    if (device != &context.GetDevice())
    {
        return ReportError("Device in device_list does not belong to context.", CL_INVALID_DEVICE);
    }

    if (!lengths || !binaries || !lengths[0] || !binaries[0])
    {
        if (binary_status)
            *binary_status = CL_INVALID_VALUE;
        return ReportError("lengths, binaries, and the entries within must not be NULL.", CL_INVALID_VALUE);
    }
    
    // TODO:
    // Validation
    // Determine binary type

    try
    {
        std::unique_ptr<byte[]> BinaryCopy(new byte[lengths[0]]);
        if (errcode_ret) *errcode_ret = CL_SUCCESS;
        if (binary_status) *binary_status = CL_SUCCESS;
        return new Program(context, std::move(BinaryCopy), lengths[0], CL_PROGRAM_BINARY_TYPE_EXECUTABLE);
    }
    catch (std::bad_alloc&) { return ReportError(nullptr, CL_OUT_OF_HOST_MEMORY); }
    catch (std::exception & e) { return ReportError(e.what(), CL_OUT_OF_RESOURCES); }
    catch (_com_error&) { return ReportError(nullptr, CL_OUT_OF_RESOURCES); }
}

extern CL_API_ENTRY cl_int CL_API_CALL
clRetainProgram(cl_program program) CL_API_SUFFIX__VERSION_1_0
{
    if (!program)
    {
        return CL_INVALID_PROGRAM;
    }
    static_cast<Program*>(program)->Retain();
    return CL_SUCCESS;
}

extern CL_API_ENTRY cl_int CL_API_CALL
clReleaseProgram(cl_program program) CL_API_SUFFIX__VERSION_1_0
{
    if (!program)
    {
        return CL_INVALID_PROGRAM;
    }
    static_cast<Program*>(program)->Release();
    return CL_SUCCESS;
}

extern CL_API_ENTRY cl_int CL_API_CALL
clBuildProgram(cl_program           program_,
    cl_uint              num_devices,
    const cl_device_id* device_list,
    const char* options,
    void (CL_CALLBACK* pfn_notify)(cl_program program,
        void* user_data),
    void* user_data) CL_API_SUFFIX__VERSION_1_0
{
    if (!program_)
    {
        return CL_INVALID_PROGRAM;
    }
    Program& program = *static_cast<Program*>(program_);
    Context& context = program.GetContext();
    auto ReportError = context.GetErrorReporter();

    if (num_devices == 0 || device_list == nullptr)
    {
        return ReportError("num_devices must not be zero and device_list must not be NULL.", CL_INVALID_VALUE);
    }
    if (num_devices != 1)
    {
        return ReportError("This platform only supports 1 device per context.", CL_INVALID_DEVICE);
    }

    Device* device = static_cast<Device*>(device_list[0]);
    if (device != &context.GetDevice())
    {
        return ReportError("Device in device_list does not belong to context.", CL_INVALID_DEVICE);
    }

    return program.Build(options, pfn_notify, user_data);
}

extern CL_API_ENTRY cl_int CL_API_CALL
clCompileProgram(cl_program           program_,
    cl_uint              num_devices,
    const cl_device_id* device_list,
    const char* options,
    cl_uint              num_input_headers,
    const cl_program* input_headers,
    const char** header_include_names,
    void (CL_CALLBACK* pfn_notify)(cl_program program,
        void* user_data),
    void* user_data) CL_API_SUFFIX__VERSION_1_2
{
    if (!program_)
    {
        return CL_INVALID_PROGRAM;
    }
    Program& program = *static_cast<Program*>(program_);
    Context& context = program.GetContext();
    auto ReportError = context.GetErrorReporter();

    if (num_devices == 0 || device_list == nullptr)
    {
        return ReportError("num_devices must not be zero and device_list must not be NULL.", CL_INVALID_VALUE);
    }
    if (num_devices != 1)
    {
        return ReportError("This platform only supports 1 device per context.", CL_INVALID_DEVICE);
    }

    Device* device = static_cast<Device*>(device_list[0]);
    if (device != &context.GetDevice())
    {
        return ReportError("Device in device_list does not belong to context.", CL_INVALID_DEVICE);
    }

    return program.Compile(options, num_input_headers, input_headers, header_include_names, pfn_notify, user_data);
}

extern CL_API_ENTRY cl_program CL_API_CALL
clLinkProgram(cl_context           context_,
    cl_uint              num_devices,
    const cl_device_id* device_list,
    const char* options,
    cl_uint              num_input_programs,
    const cl_program* input_programs,
    void (CL_CALLBACK* pfn_notify)(cl_program program,
        void* user_data),
    void* user_data,
    cl_int* errcode_ret) CL_API_SUFFIX__VERSION_1_2
{
    if (!context_)
    {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return nullptr;
    }
    Context& context = *static_cast<Context*>(context_);
    auto ReportError = context.GetErrorReporter(errcode_ret);

    if (num_devices == 0 || device_list == nullptr)
    {
        return ReportError("num_devices must not be zero and device_list must not be NULL.", CL_INVALID_VALUE);
    }
    if (num_devices != 1)
    {
        return ReportError("This platform only supports 1 device per context.", CL_INVALID_DEVICE);
    }

    Device* device = static_cast<Device*>(device_list[0]);
    if (device != &context.GetDevice())
    {
        return ReportError("Device in device_list does not belong to context.", CL_INVALID_DEVICE);
    }

    try
    {
        ref_ptr NewProgram(new Program(context), adopt_ref{});
        cl_int LinkStatus = NewProgram->Link(options, num_input_programs, input_programs, pfn_notify, user_data);
        if (!LinkStatus)
        {
            NewProgram.Release();
        }
        return NewProgram.Detach();
    }
    catch (std::bad_alloc&) { return ReportError(nullptr, CL_OUT_OF_HOST_MEMORY); }
    catch (std::exception & e) { return ReportError(e.what(), CL_OUT_OF_RESOURCES); }
    catch (_com_error&) { return ReportError(nullptr, CL_OUT_OF_RESOURCES); }
}

extern CL_API_ENTRY cl_int CL_API_CALL
clGetProgramInfo(cl_program         program_,
    cl_program_info    param_name,
    size_t             param_value_size,
    void* param_value,
    size_t* param_value_size_ret) CL_API_SUFFIX__VERSION_1_0
{
    if (!program_)
    {
        return CL_INVALID_PROGRAM;
    }

    Program& program = *static_cast<Program*>(program_);
    auto RetValue = [&](auto&& param)
    {
        return CopyOutParameter(param, param_value_size, param_value, param_value_size_ret);
    };

    switch (param_name)
    {
    case CL_PROGRAM_REFERENCE_COUNT: return RetValue(program.GetRefCount());
    case CL_PROGRAM_CONTEXT: return RetValue((cl_context)&program.GetContext());
    case CL_PROGRAM_NUM_DEVICES: return RetValue(1u);
    case CL_PROGRAM_DEVICES: return RetValue((cl_device_id)&program.GetDevice());
    case CL_PROGRAM_SOURCE: return RetValue(program.m_Source.c_str());
    case CL_PROGRAM_BINARY_SIZES: { std::lock_guard lock(program.m_Lock); return RetValue(program.m_BinarySize); }
    case CL_PROGRAM_BINARIES:
    {
        std::lock_guard lock(program.m_Lock);
        if (param_value_size && param_value_size < sizeof(void*))
        {
            return CL_INVALID_VALUE;
        }
        if (param_value_size)
        {
            memcpy(reinterpret_cast<void**>(param_value)[0], program.m_Binary.get(), program.m_BinarySize);
        }
        if (param_value_size_ret)
        {
            *param_value_size_ret = sizeof(void*);
        }
        return CL_SUCCESS;
    }
    case CL_PROGRAM_NUM_KERNELS:
    {
        std::lock_guard lock(program.m_Lock);
        if (program.m_BinaryType != CL_PROGRAM_BINARY_TYPE_EXECUTABLE)
        {
            return CL_INVALID_PROGRAM_EXECUTABLE;
        }
        return RetValue(program.m_KernelNames.size());
    }
    case CL_PROGRAM_KERNEL_NAMES:
    {
        std::lock_guard lock(program.m_Lock);
        if (program.m_BinaryType != CL_PROGRAM_BINARY_TYPE_EXECUTABLE)
        {
            return CL_INVALID_PROGRAM_EXECUTABLE;
        }
        size_t stringSize = 0;
        for (size_t i = 0; i < program.m_KernelNames.size(); ++i)
        {
            stringSize += program.m_KernelNames[i].size();
        }
        stringSize += program.m_KernelNames.size(); // 1 semicolon between each name + 1 null terminator
        if (param_value_size && param_value_size < stringSize)
        {
            return CL_INVALID_VALUE;
        }
        if (param_value_size)
        {
            char* pOut = reinterpret_cast<char*>(param_value);
            for (size_t i = 0; i < program.m_KernelNames.size(); ++i)
            {
                pOut = std::copy(program.m_KernelNames[i].begin(), program.m_KernelNames[i].end(), pOut);
                *(pOut++) = ';';
            }
            *(--pOut) = '\0';
        }
        if (param_value_size_ret)
        {
            *param_value_size_ret = stringSize;
        }
        return CL_SUCCESS;
    }
    }

    return CL_INVALID_VALUE;
}

extern CL_API_ENTRY cl_int CL_API_CALL
clGetProgramBuildInfo(cl_program            program_,
    cl_device_id          device,
    cl_program_build_info param_name,
    size_t                param_value_size,
    void* param_value,
    size_t* param_value_size_ret) CL_API_SUFFIX__VERSION_1_0
{
    if (!program_)
    {
        return CL_INVALID_PROGRAM;
    }

    Program& program = *static_cast<Program*>(program_);
    auto RetValue = [&](auto&& param)
    {
        return CopyOutParameter(param, param_value_size, param_value, param_value_size_ret);
    };

    if (device != &program.GetDevice())
    {
        return program.GetContext().GetErrorReporter()("Invalid device.", CL_INVALID_DEVICE);
    }

    std::lock_guard lock(program.m_Lock);
    switch (param_name)
    {
    case CL_PROGRAM_BUILD_STATUS: return RetValue(program.m_BuildStatus);
    case CL_PROGRAM_BUILD_OPTIONS: return RetValue(program.m_LastBuildOptions.c_str());
    case CL_PROGRAM_BUILD_LOG: return RetValue(program.m_BuildLog.c_str());
    case CL_PROGRAM_BINARY_TYPE: return RetValue(program.m_BinaryType);
    }

    return CL_INVALID_VALUE;
}

Program::Program(Context& Parent, std::string Source)
    : CLChildBase(Parent)
    , m_Source(std::move(Source))
{
}

Program::Program(Context& Parent, std::unique_ptr<byte[]> Binary, size_t BinarySize, cl_program_binary_type Type)
    : CLChildBase(Parent)
    , m_Binary(Binary.release(), &operator delete[])
    , m_BinarySize(BinarySize)
    , m_BinaryType(Type)
{
}

Program::Program(Context& Parent)
    : CLChildBase(Parent)
{
}

cl_int Program::Build(const char* options, Callback pfn_notify, void* user_data)
{
    auto ReportError = GetContext().GetErrorReporter();

    // Parse options
    BuildArgs Args = {};
    cl_int ret = ParseOptions(options, Args.Common, true, true);
    if (ret != CL_SUCCESS)
    {
        return ReportError("Invalid options.", CL_INVALID_BUILD_OPTIONS);
    }

    {
        // Ensure that we can build
        std::lock_guard Lock(m_Lock);
        if (m_BuildStatus != CL_BUILD_NONE)
        {
            return ReportError("Cannot build program: program already has been built.", CL_INVALID_OPERATION);
        }
        if (m_BinaryType == CL_PROGRAM_BINARY_TYPE_EXECUTABLE)
        {
            return ReportError("Cannot build program: program contains executable.", CL_INVALID_OPERATION);
        }

        // Update build status to indicate build is starting so nobody else can start a build
        m_BuildStatus = CL_BUILD_IN_PROGRESS;
        m_LastBuildOptions = options ? options : "";
    }

    if (pfn_notify)
    {
        Args.Common.pfn_notify = pfn_notify;
        Args.Common.CallbackUserData = user_data;
        GetDevice().QueueProgramOp([this, Args]()
            {
                this->BuildImpl(Args);
            });
        return CL_SUCCESS;
    }
    else
    {
        BuildImpl(Args);

        std::lock_guard Lock(m_Lock);
        if (m_BuildStatus != CL_BUILD_SUCCESS)
        {
            return CL_BUILD_PROGRAM_FAILURE;
        }
        return CL_SUCCESS;
    }
}

cl_int Program::Compile(const char* options, cl_uint num_input_headers, const cl_program* input_headers, const char** header_include_names, Callback pfn_notify, void* user_data)
{
    auto ReportError = GetContext().GetErrorReporter();

    // Parse options
    CompileArgs Args = {};
    cl_int ret = ParseOptions(options, Args.Common, true, false);
    if (ret != CL_SUCCESS)
    {
        return ReportError("Invalid options.", CL_INVALID_COMPILER_OPTIONS);
    }

    for (cl_uint i = 0; i < num_input_headers; ++i)
    {
        if (!input_headers[i] || !header_include_names[i] || header_include_names[i][0] == '\0')
        {
            return ReportError("Invalid header or header name.", CL_INVALID_VALUE);
        }
        Program& header = *static_cast<Program*>(input_headers[i]);
        if (header.m_Source.empty())
        {
            return ReportError("Header provided has no source.", CL_INVALID_VALUE);
        }
        Args.Headers[header_include_names[i]] = &header;
    }

    {
        // Ensure that we can compile
        std::lock_guard Lock(m_Lock);
        if (m_BuildStatus != CL_BUILD_NONE)
        {
            return ReportError("Cannot compile program: program already has been built.", CL_INVALID_OPERATION);
        }
        if (m_BinaryType != CL_PROGRAM_BINARY_TYPE_NONE)
        {
            return ReportError("Program already contains a binary.", CL_INVALID_OPERATION);
        }

        // Update build status to indicate compile is starting so nobody else can start a build
        m_BuildStatus = CL_BUILD_IN_PROGRESS;
        m_LastBuildOptions = options ? options : "";
    }

    if (pfn_notify)
    {
        Args.Common.pfn_notify = pfn_notify;
        Args.Common.CallbackUserData = user_data;
        GetDevice().QueueProgramOp([this, Args]()
            {
                this->CompileImpl(Args);
            });
        return CL_SUCCESS;
    }
    else
    {
        CompileImpl(Args);

        std::lock_guard Lock(m_Lock);
        if (m_BuildStatus != CL_BUILD_SUCCESS)
        {
            return CL_COMPILE_PROGRAM_FAILURE;
        }
        return CL_SUCCESS;
    }
}

cl_int Program::Link(const char* options, cl_uint num_input_programs, const cl_program* input_programs, Callback pfn_notify, void* user_data)
{
    auto ReportError = GetContext().GetErrorReporter();

    // Parse options
    LinkArgs Args = {};
    cl_int ret = ParseOptions(options, Args.Common, false, true);
    if (ret != CL_SUCCESS)
    {
        return ReportError("Invalid options.", CL_INVALID_LINKER_OPTIONS);
    }

    for (cl_uint i = 0; i < num_input_programs; ++i)
    {
        if (!input_programs[i])
        {
            return ReportError("Invalid header or header name.", CL_INVALID_VALUE);
        }
        Program& lib = *static_cast<Program*>(input_programs[i]);
        std::lock_guard Lock(lib.m_Lock);
        if (lib.m_BuildStatus == CL_BUILD_IN_PROGRESS)
        {
            return ReportError("Invalid input program: program is currently being built.", CL_INVALID_OPERATION);
        }
        if (lib.m_BuildStatus == CL_BUILD_ERROR)
        {
            return ReportError("Invalid input program: program failed to be built.", CL_INVALID_OPERATION);
        }
        if (lib.m_BinaryType == CL_PROGRAM_BINARY_TYPE_NONE ||
            lib.m_BinaryType == CL_PROGRAM_BINARY_TYPE_EXECUTABLE)
        {
            return ReportError("Invalid input program: program does not contain library or compiled object.", CL_INVALID_OPERATION);
        }
        Args.LinkPrograms.emplace_back(&lib);
    }

    // Note: Don't need to take our own lock, since no other thread can have access to this object
    // Update build status to indicate compile is starting so nobody else can start a build
    m_BuildStatus = CL_BUILD_IN_PROGRESS;
    m_LastBuildOptions = options ? options : "";

    if (pfn_notify)
    {
        Args.Common.pfn_notify = pfn_notify;
        Args.Common.CallbackUserData = user_data;
        GetDevice().QueueProgramOp([this, Args]()
            {
                this->LinkImpl(Args);
            });
        return CL_SUCCESS;
    }
    else
    {
        LinkImpl(Args);

        std::lock_guard Lock(m_Lock);
        if (m_BuildStatus != CL_BUILD_SUCCESS)
        {
            return CL_LINK_PROGRAM_FAILURE;
        }
        return CL_SUCCESS;
    }
}

cl_int Program::ParseOptions(const char* optionsStr, CommonOptions& optionsStruct, bool SupportCompilerOptions, bool SupportLinkerOptions)
{
    char InDefineOrInclude = '\0';
    while (optionsStr && *optionsStr)
    {
        switch (*optionsStr)
        {
        case ' ':
            ++optionsStr;
            break;
        case '-':
        case '/':
        {
            if (InDefineOrInclude)
            {
                return CL_INVALID_BUILD_OPTIONS;
            }
            ++optionsStr;
            const char* base = optionsStr;
            const char* term = strchr(optionsStr, ' ');
            std::string_view word = term ? std::string_view(base, term - base) : std::string_view(base);
            optionsStr = term;
            if (SupportCompilerOptions)
            {
                if (word == "D" || word == "I")
                {
                    InDefineOrInclude = word[0];
                    break;
                }
                if (word[0] == 'D' || word[0] == 'I')
                {
                    optionsStr = base + 1;
                    InDefineOrInclude = word[0];
                    break;
                }
                // Simply validate, we're ignoring these for now
                if (word == "cl-single-precision-constant" ||
                    word == "cl-fp32-correctly-rounded-divide-sqrt" ||
                    word == "cl-opt-disable" ||
                    word == "cl-mad-enable" ||
                    word == "w" ||
                    word == "Werror" ||
                    word.find("cl-std=") == 0 ||
                    word == "cl-kernel-arg-info")
                {
                    break;
                }
            }
            if (SupportLinkerOptions)
            {
                if (word == "create-library")
                {
                    optionsStruct.CreateLibrary = true;
                    break;
                }
                if (word == "enable-link-options")
                {
                    if (!optionsStruct.CreateLibrary)
                    {
                        return CL_INVALID_BUILD_OPTIONS;
                    }
                    break;
                }
            }
            if (word == "cl-denorms-are-zero" ||
                word == "cl-no-signed-zeros" ||
                word == "cl-unsafe-math-optimizations" ||
                word == "cl-finite-math-only" ||
                word == "cl-fast-relaxed-math")
            {
                break;
            }
            return CL_INVALID_BUILD_OPTIONS;
        }
        default:
        {
            if (!InDefineOrInclude)
            {
                return CL_INVALID_BUILD_OPTIONS;
            }
            const char* base = optionsStr;
            const char* term = strchr(optionsStr, ' ');
            std::string_view word = term ? std::string_view(base, term - base) : std::string_view(base);
            const char* equals = strchr(optionsStr, '=');
            optionsStr = term;
            if (InDefineOrInclude == 'D')
            {
                std::string_view lhs = equals ? std::string_view(base, equals - base) : word;
                std::string_view rhs = equals ? std::string_view(equals + 1, term - (equals + 1)) : "1";
                optionsStruct.Defines[std::string(lhs)] = rhs;
            }
            break;
        }
        }
    }
    return CL_SUCCESS;
}

void Program::BuildImpl(BuildArgs const& Args)
{
    // No need for lock - build status was set to building, so binary type cannot change out from under us
    if (m_BinaryType != CL_PROGRAM_BINARY_TYPE_NONE)
    {
        // TODO:
        // Right now all binaries are program binaries, so if we were created with one, just update our type and we're done.
        m_BinaryType = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
    }
    else
    {
        auto& Compiler = g_Platform->GetCompiler();
        auto compile = Compiler.proc_address<decltype(&clc_compile_from_source)>("clc_compile_from_source");
        auto free = Compiler.proc_address<decltype(&free_blob)>("free_blob");

        std::vector<clc_define> defines;
        for (auto& def : Args.Common.Defines)
        {
            defines.push_back({ def.first.c_str(), def.second.c_str() });
        }
        void* blob = nullptr;
        size_t blobSize = 0;
        int result = compile(m_Source.c_str(), "source.cl", defines.data(), defines.size(), nullptr, 0, nullptr, nullptr, nullptr, &blob, &blobSize);
        if (blob) SignBlob(blob, blobSize);

        std::lock_guard Lock(m_Lock);
        if (result == 0)
        {
            m_Binary = decltype(m_Binary)(blob, free);
            m_BinarySize = blobSize;
            // TODO: Library if linker option set
            m_BinaryType = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
            m_BuildStatus = CL_BUILD_SUCCESS;
        }
        else
        {
            m_BuildStatus = CL_BUILD_ERROR;
        }
    }
    if (Args.Common.pfn_notify)
    {
        Args.Common.pfn_notify(this, Args.Common.CallbackUserData);
    }
}

void Program::CompileImpl(CompileArgs const& Args)
{
    auto& Compiler = g_Platform->GetCompiler();
    auto compile = Compiler.proc_address<decltype(&clc_compile_from_source)>("clc_compile_from_source");
    auto free = Compiler.proc_address<decltype(&free_blob)>("free_blob");

    std::vector<clc_define> defines;
    for (auto& def : Args.Common.Defines)
    {
        defines.push_back({ def.first.c_str(), def.second.c_str() });
    }
    std::vector<clc_header> headers;
    for (auto& header : Args.Headers)
    {
        headers.push_back({ header.first.c_str(), header.second->m_Source.c_str() });
    }
    void* blob = nullptr;
    size_t blobSize = 0;
    int result = compile(m_Source.c_str(), "source.cl", defines.data(), defines.size(), headers.data(), headers.size(), nullptr, nullptr, nullptr, &blob, &blobSize);
    if (blob) SignBlob(blob, blobSize);

    {
        std::lock_guard Lock(m_Lock);
        if (result == 0)
        {
            m_Binary = decltype(m_Binary)(blob, free);
            m_BinarySize = blobSize;
            m_BinaryType = CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT;
            m_BuildStatus = CL_BUILD_SUCCESS;
        }
        else
        {
            m_BuildStatus = CL_BUILD_ERROR;
        }
    }
    if (Args.Common.pfn_notify)
    {
        Args.Common.pfn_notify(this, Args.Common.CallbackUserData);
    }
}

void Program::LinkImpl(LinkArgs const& Args)
{
    std::lock_guard Lock(m_Lock);
    // TODO: Actually link
    if (Args.LinkPrograms.size() > 1)
    {
        m_BuildStatus = CL_BUILD_ERROR;
    }
    else
    {
        m_Binary = decltype(m_Binary)(new byte[Args.LinkPrograms[0]->m_BinarySize], &operator delete[]);
        m_BinarySize = Args.LinkPrograms[0]->m_BinarySize;
        memcpy(m_Binary.get(), Args.LinkPrograms[0]->m_Binary.get(), m_BinarySize);
        m_BinaryType = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
        m_BuildStatus = CL_BUILD_SUCCESS;
    }
    if (Args.Common.pfn_notify)
    {
        Args.Common.pfn_notify(this, Args.Common.CallbackUserData);
    }
}