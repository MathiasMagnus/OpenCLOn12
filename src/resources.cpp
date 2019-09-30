#include "resources.hpp"
#include "formats.hpp"

constexpr cl_mem_flags ValidMemFlags =
    CL_MEM_READ_WRITE |
    CL_MEM_WRITE_ONLY |
    CL_MEM_READ_ONLY |
    CL_MEM_USE_HOST_PTR |
    CL_MEM_ALLOC_HOST_PTR |
    CL_MEM_COPY_HOST_PTR |
    CL_MEM_HOST_WRITE_ONLY |
    CL_MEM_HOST_READ_ONLY |
    CL_MEM_HOST_NO_ACCESS;

constexpr cl_mem_flags DeviceReadWriteFlagsMask =
    CL_MEM_READ_WRITE |
    CL_MEM_WRITE_ONLY |
    CL_MEM_READ_ONLY;
constexpr cl_mem_flags HostReadWriteFlagsMask =
    CL_MEM_HOST_WRITE_ONLY |
    CL_MEM_HOST_READ_ONLY |
    CL_MEM_HOST_NO_ACCESS;

void ModifyResourceArgsForMemFlags(D3D12TranslationLayer::ResourceCreationArgs& Args, cl_mem_flags flags)
{
    if ((flags & DeviceReadWriteFlagsMask) == 0)
        flags |= CL_MEM_READ_WRITE;
    if (flags & CL_MEM_ALLOC_HOST_PTR)
    {
        auto& usage = Args.m_appDesc.m_usage;
        switch (flags & (DeviceReadWriteFlagsMask | HostReadWriteFlagsMask))
        {
        case (CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY):
            usage = D3D12TranslationLayer::RESOURCE_USAGE_STAGING;
            break;
        default:
            usage = D3D12TranslationLayer::RESOURCE_USAGE_DYNAMIC;
            break;
        }
        Args.m_heapDesc.Properties.Type =
            (usage == D3D12TranslationLayer::RESOURCE_USAGE_STAGING) ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_UPLOAD;
    }
    else
    {
        if ((flags & CL_MEM_HOST_NO_ACCESS) == 0)
        {
            Args.m_appDesc.m_usage = D3D12TranslationLayer::RESOURCE_USAGE_DYNAMIC;
        }
        Args.m_heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    }
}

template <typename TErrFunc>
bool ValidateMemFlagsBase(cl_mem_flags flags, TErrFunc&& ReportError)
{
    if (flags & ValidMemFlags)
    {
        ReportError("Unknown flags specified.", CL_INVALID_VALUE);
        return false;
    }
    if (!IsZeroOrPow2(flags & DeviceReadWriteFlagsMask))
    {
        ReportError("Only one of CL_MEM_READ_WRITE, CL_MEM_WRITE_ONLY, and CL_MEM_READ_ONLY can be specified.", CL_INVALID_VALUE);
        return false;
    }
    if (!IsZeroOrPow2(flags & HostReadWriteFlagsMask))
    {
        ReportError("Only one of CL_MEM_HOST_WRITE_ONLY, CL_MEM_HOST_READ_ONLY, and CL_MEM_HOST_NO_ACCESS can be specified.", CL_INVALID_VALUE);
        return false;
    }
    if ((flags & CL_MEM_USE_HOST_PTR) && (flags & (CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR)))
    {
        ReportError("CL_MEM_USE_HOST_PTR cannot be used with either CL_MEM_ALLOC_HOST_PTR or CL_MEM_COPY_HOST_PTR.", CL_INVALID_VALUE);
        return false;
    }

    return true;
}

template <typename TErrFunc>
bool ValidateMemFlags(cl_mem_flags flags, bool bHaveHostPtr, TErrFunc&& ReportError)
{
    const bool bNeedHostPtr = (flags & (CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR));
    if (bNeedHostPtr && !bHaveHostPtr)
    {
        ReportError("When CL_MEM_USE_HOST_PTR or CL_MEM_COPY_HOST_PTR are specified, host_ptr must not be null.", CL_INVALID_HOST_PTR);
        return false;
    }
    else if (bHaveHostPtr && !bNeedHostPtr)
    {
        ReportError("When CL_MEM_USE_HOST_PTR or CL_MEM_COPY_HOST_PTR are not specified, host_ptr must be null.", CL_INVALID_HOST_PTR);
        return false;
    }

    return ValidateMemFlagsBase(flags, ReportError);
}

template <typename TErrFunc>
bool ValidateMemFlagsForBufferReference(cl_mem_flags flags, Resource& buffer, TErrFunc&& ReportError)
{
    if (flags & (CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR))
    {
        ReportError("Cannot set CL_MEM_USE_HOST_PTR, CL_MEM_ALLOC_HOST_PTR, or CL_MEM_COPY_HOST_PTR for sub-buffers or 1D image buffers.", CL_INVALID_VALUE);
        return false;
    }
    flags |= buffer.m_Flags & (CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR);

    if ((buffer.m_Flags & DeviceReadWriteFlagsMask) == 0)
    {
        flags |= buffer.m_Flags & DeviceReadWriteFlagsMask;
    }
    else if (((buffer.m_Flags & CL_MEM_WRITE_ONLY) && (flags & (CL_MEM_READ_ONLY | CL_MEM_READ_WRITE))) ||
        ((buffer.m_Flags & CL_MEM_READ_ONLY) && (flags & (CL_MEM_WRITE_ONLY | CL_MEM_READ_WRITE))))
    {
        ReportError("Attempting to add device read or write capabilities via sub-buffer or 1D image buffer.", CL_INVALID_VALUE);
        return false;
    }

    if ((buffer.m_Flags & HostReadWriteFlagsMask) == 0)
    {
        flags |= buffer.m_Flags & HostReadWriteFlagsMask;
    }
    else if (((buffer.m_Flags & CL_MEM_HOST_WRITE_ONLY) && (flags & CL_MEM_HOST_READ_ONLY)) ||
        ((buffer.m_Flags & CL_MEM_HOST_READ_ONLY) && (flags & CL_MEM_HOST_WRITE_ONLY)) ||
        ((buffer.m_Flags & CL_MEM_HOST_NO_ACCESS) && (flags & (CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_WRITE_ONLY))))
    {
        ReportError("Attempting to add host read or write capabilities via sub-buffer or 1D image buffer.", CL_INVALID_VALUE);
        return false;
    }
    return true;
}

/* Memory Object APIs */
extern CL_API_ENTRY cl_mem CL_API_CALL
clCreateBuffer(cl_context   context_,
    cl_mem_flags flags,
    size_t       size,
    void *       host_ptr,
    cl_int *     errcode_ret) CL_API_SUFFIX__VERSION_1_0
{
    if (!context_)
    {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return nullptr;
    }
    Context& context = *static_cast<Context*>(context_);
    auto ReportError = context.GetErrorReporter(errcode_ret);

    if (size == 0 || size > UINT_MAX)
    {
        return ReportError("Invalid buffer size.", CL_INVALID_BUFFER_SIZE);
    }

    if (!ValidateMemFlags(flags, host_ptr != nullptr, ReportError))
    {
        return nullptr;
    }

    D3D12TranslationLayer::ResourceCreationArgs Args = {};
    Args.m_appDesc.m_Subresources = 1;
    Args.m_appDesc.m_SubresourcesPerPlane = 1;
    Args.m_appDesc.m_NonOpaquePlaneCount = 1;
    Args.m_appDesc.m_MipLevels = 1;
    Args.m_appDesc.m_ArraySize = 1;
    Args.m_appDesc.m_Depth = 1;
    Args.m_appDesc.m_Width = (UINT)size;
    Args.m_appDesc.m_Height = 1;
    Args.m_appDesc.m_Format = DXGI_FORMAT_UNKNOWN;
    Args.m_appDesc.m_Samples = 1;
    Args.m_appDesc.m_Quality = 0;
    Args.m_appDesc.m_resourceDimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Args.m_appDesc.m_usage = D3D12TranslationLayer::RESOURCE_USAGE_DEFAULT;
    Args.m_appDesc.m_bindFlags = D3D12TranslationLayer::RESOURCE_BIND_UNORDERED_ACCESS | D3D12TranslationLayer::RESOURCE_BIND_SHADER_RESOURCE;
    Args.m_desc12 = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    Args.m_heapDesc = CD3DX12_HEAP_DESC(0, D3D12_HEAP_TYPE_DEFAULT);
    ModifyResourceArgsForMemFlags(Args, flags);

    try
    {
        if (errcode_ret) *errcode_ret = CL_SUCCESS;
        return Resource::CreateBuffer(context, Args, host_ptr, flags);
    }
    catch (std::bad_alloc&) { return ReportError(nullptr, CL_OUT_OF_HOST_MEMORY); }
    catch (_com_error& e)
    {
        if (e.Error() == E_INVALIDARG)
            return ReportError("Invalid buffer description.", CL_INVALID_VALUE);
        return ReportError(nullptr, CL_OUT_OF_RESOURCES);
    }
    catch (std::exception& e)
    {
        return ReportError(e.what(), CL_OUT_OF_RESOURCES);
    }
}

extern CL_API_ENTRY cl_mem CL_API_CALL
clCreateSubBuffer(cl_mem                   buffer_,
    cl_mem_flags             flags,
    cl_buffer_create_type    buffer_create_type,
    const void *             buffer_create_info,
    cl_int *                 errcode_ret) CL_API_SUFFIX__VERSION_1_1
{
    if (!buffer_)
    {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return nullptr;
    }
    Resource& buffer = *static_cast<Resource*>(buffer_);
    Context& context = buffer.m_Parent.get();
    auto ReportError = context.GetErrorReporter(errcode_ret);

    if (!ValidateMemFlagsForBufferReference(flags, buffer, ReportError))
    {
        return nullptr;
    }

    if (buffer_create_type != CL_BUFFER_CREATE_TYPE_REGION)
    {
        return ReportError("Invalid buffer create type.", CL_INVALID_VALUE);
    }
    auto& region = *reinterpret_cast<const cl_buffer_region*>(buffer_create_info);

    if (region.size == 0) return ReportError("Invalid buffer region size.", CL_INVALID_BUFFER_SIZE);
    if ((region.origin % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) != 0)
        return ReportError("Invalid buffer region origin alignment.", CL_MISALIGNED_SUB_BUFFER_OFFSET);
    if (region.origin + region.size > buffer.m_Desc.image_width) return ReportError("Origin + size for sub-buffer is out of bounds", CL_INVALID_VALUE);

    try
    {
        if (errcode_ret) *errcode_ret = CL_SUCCESS;
        return Resource::CreateSubBuffer(buffer, region, flags);
    }
    catch (std::bad_alloc&) { return ReportError(nullptr, CL_OUT_OF_HOST_MEMORY); }
    catch (std::exception& e) { return ReportError(e.what(), CL_OUT_OF_RESOURCES); }
}

extern CL_API_ENTRY cl_mem CL_API_CALL
clCreateImage(cl_context              context_,
    cl_mem_flags            flags,
    const cl_image_format * image_format,
    const cl_image_desc *   image_desc,
    void *                  host_ptr,
    cl_int *                errcode_ret) CL_API_SUFFIX__VERSION_1_2
{
    if (!context_)
    {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return nullptr;
    }
    Context& context = *static_cast<Context*>(context_);
    auto ReportError = context.GetErrorReporter(errcode_ret);

    if (context.GetDevice().IsMCDM())
    {
        return ReportError("Images not supported.", CL_INVALID_OPERATION);
    }

    if (!ValidateMemFlags(flags, host_ptr != nullptr, ReportError))
    {
        return nullptr;
    }

    if (!image_format)
    {
        return ReportError("Null image format.", CL_INVALID_IMAGE_FORMAT_DESCRIPTOR);
    }
    if (!image_desc)
    {
        return ReportError("Null image desc.", CL_INVALID_IMAGE_DESCRIPTOR);
    }

    auto image_desc_copy = *image_desc;
    D3D12TranslationLayer::ResourceCreationArgs Args = {};
    switch (image_desc->image_type)
    {
    case CL_MEM_OBJECT_BUFFER:
        return ReportError("image_type of CL_MEM_OBJECT_BUFFER is invalid for clCreateImage.", CL_INVALID_IMAGE_DESCRIPTOR);
    case CL_MEM_OBJECT_IMAGE1D_ARRAY:
        if (image_desc->image_array_size > D3D12_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION)
            return ReportError("Array size exceeds maximum Texture1D array dimensionality.", CL_INVALID_IMAGE_DESCRIPTOR);
        // fallthrough
    case CL_MEM_OBJECT_IMAGE1D:
    case CL_MEM_OBJECT_IMAGE1D_BUFFER:
        Args.m_appDesc.m_resourceDimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        if (image_desc->image_width > D3D12_REQ_TEXTURE1D_U_DIMENSION)
            return ReportError("Width exceeds maximum Texture1D width.", CL_INVALID_IMAGE_DESCRIPTOR);
        image_desc_copy.image_height = 0;
        image_desc_copy.image_depth = 0;
        break;
    case CL_MEM_OBJECT_IMAGE2D_ARRAY:
        if (image_desc->image_array_size > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION)
            return ReportError("Array size exceeds maximum Texture2D array dimensionality.", CL_INVALID_IMAGE_DESCRIPTOR);
        // fallthrough
    case CL_MEM_OBJECT_IMAGE2D:
        Args.m_appDesc.m_resourceDimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        if (image_desc->image_width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            return ReportError("Width exceeds maximum Texture2D width.", CL_INVALID_IMAGE_DESCRIPTOR);
        if (image_desc->image_height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            return ReportError("Height exceeds maximum Texture2D height.", CL_INVALID_IMAGE_DESCRIPTOR);
        image_desc_copy.image_depth = 0;
        break;
    case CL_MEM_OBJECT_IMAGE3D:
        Args.m_appDesc.m_resourceDimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        if (image_desc->image_width > D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION)
            return ReportError("Width exceeds maximum Texture3D width.", CL_INVALID_IMAGE_DESCRIPTOR);
        if (image_desc->image_height > D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION)
            return ReportError("Height exceeds maximum Texture3D height.", CL_INVALID_IMAGE_DESCRIPTOR);
        if (image_desc->image_depth > D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION)
            return ReportError("Depth exceeds maximum Texture3D depth.", CL_INVALID_IMAGE_DESCRIPTOR);
        break;
    default:
        return ReportError("Invalid image_type.", CL_INVALID_IMAGE_DESCRIPTOR);
    }

    Args.m_appDesc.m_NonOpaquePlaneCount = 1;
    Args.m_appDesc.m_MipLevels = 1;
    Args.m_appDesc.m_Depth = (UINT)image_desc->image_depth;
    Args.m_appDesc.m_Width = (UINT)image_desc->image_width;
    Args.m_appDesc.m_Height = (UINT)image_desc->image_height;
    Args.m_appDesc.m_Format = GetDXGIFormatForCLImageFormat(*image_format);
    Args.m_appDesc.m_Samples = 1;
    Args.m_appDesc.m_Quality = 0;
    Args.m_appDesc.m_ArraySize = (UINT16)image_desc->image_array_size;
    if (image_desc->image_type != CL_MEM_OBJECT_IMAGE1D_ARRAY &&
        image_desc->image_type != CL_MEM_OBJECT_IMAGE2D_ARRAY)
    {
        if (image_desc->image_array_size > 0)
            ReportError("image_array_size shouldn't be specified for non-array image types.", CL_SUCCESS);
        Args.m_appDesc.m_ArraySize = 1;
        image_desc_copy.image_array_size = 0;
    }
    else if (image_desc->image_array_size == 0)
    {
        return ReportError("image_array_size must be > 0 for array types.", CL_INVALID_IMAGE_DESCRIPTOR);
    }

    auto ElementByteSize = CD3D11FormatHelper::GetByteAlignment(Args.m_appDesc.m_Format);
    if (image_desc->image_row_pitch == 0)
    {
        image_desc_copy.image_row_pitch = ElementByteSize * image_desc->image_width;
    }
    else if (host_ptr == nullptr)
    {
        return ReportError("image_row_pitch must be 0 if host_ptr is null.", CL_INVALID_IMAGE_DESCRIPTOR);
    }
    else if (image_desc->image_row_pitch < ElementByteSize * image_desc->image_width ||
             image_desc->image_row_pitch % ElementByteSize != 0)
    {
        return ReportError("image_row_pitch must be >= image_width * size of element in bytes, and must be a multiple of the element size in bytes.", CL_INVALID_IMAGE_DESCRIPTOR);
    }

    switch (image_desc->image_type)
    {
    case CL_MEM_OBJECT_IMAGE1D_ARRAY:
    case CL_MEM_OBJECT_IMAGE2D_ARRAY:
    case CL_MEM_OBJECT_IMAGE3D:
        if (image_desc->image_slice_pitch == 0)
        {
            image_desc_copy.image_slice_pitch = image_desc_copy.image_row_pitch * max<size_t>(image_desc->image_height, 1);
        }
        else if (host_ptr == nullptr)
        {
            return ReportError("image_slice_pitch must be 0 if host_ptr is null.", CL_INVALID_IMAGE_DESCRIPTOR);
        }
        else if (image_desc->image_slice_pitch < image_desc_copy.image_row_pitch * max<size_t>(image_desc->image_height, 1) ||
                 image_desc->image_slice_pitch % image_desc_copy.image_row_pitch != 0)
        {
            return ReportError("image_slice_pitch must be >= image_row_pitch * height (or just image_row_pitch for buffers), and must be a multiple of the image_row_pitch.", CL_INVALID_IMAGE_DESCRIPTOR);
        }
        break;
    default:
        image_desc_copy.image_slice_pitch = 0;
    }
    image_desc = &image_desc_copy;

    Args.m_appDesc.m_Subresources = Args.m_appDesc.m_ArraySize;
    Args.m_appDesc.m_SubresourcesPerPlane = Args.m_appDesc.m_ArraySize;

    if (image_desc->num_mip_levels != 0 || image_desc->num_samples != 0)
    {
        return ReportError("num_mip_levels and num_samples must be 0.", CL_INVALID_IMAGE_DESCRIPTOR);
    }
    image_desc_copy.num_mip_levels = 0;
    image_desc_copy.num_samples = 0;

    if (Args.m_appDesc.m_Format == DXGI_FORMAT_UNKNOWN)
    {
        return ReportError("Invalid image format.", CL_IMAGE_FORMAT_NOT_SUPPORTED);
    }

    if (image_desc->image_type == CL_MEM_OBJECT_IMAGE1D_BUFFER)
    {
        if (image_desc->buffer == nullptr)
        {
            return ReportError("When image_type is CL_MEM_OBJECT_IMAGE1D_BUFFER, buffer must be valid.", CL_INVALID_IMAGE_DESCRIPTOR);
        }

        Resource& buffer = *static_cast<Resource*>(image_desc->buffer);
        if (buffer.m_Desc.image_type != CL_MEM_OBJECT_BUFFER)
        {
            return ReportError("When image_type is CL_MEM_OBJECT_IMAGE1D_BUFFER, buffer must specify a buffer.", CL_INVALID_IMAGE_DESCRIPTOR);
        }
        if (!ValidateMemFlagsForBufferReference(flags, buffer, ReportError))
        {
            return nullptr;
        }

        size_t size = CD3D11FormatHelper::GetByteAlignment(GetDXGIFormatForCLImageFormat(*image_format)) * image_desc->image_width;
        if (size > buffer.m_Desc.image_width)
        {
            return ReportError("1D image buffer size is too large.", CL_INVALID_IMAGE_DESCRIPTOR);
        }
    }
    else
    {
        if (image_desc->buffer != nullptr)
        {
            return ReportError("Only specify buffer when image_type is CL_MEM_OBJECT_IMAGE1D_BUFFER.", CL_INVALID_IMAGE_DESCRIPTOR);
        }
    }

    try
    {
        if (errcode_ret) *errcode_ret = CL_SUCCESS;
        if (image_desc->image_type == CL_MEM_OBJECT_IMAGE1D_BUFFER)
        {
            return Resource::CreateImage1DBuffer(*static_cast<Resource*>(image_desc->buffer), *image_format, *image_desc, flags);
        }
        else
        {
            Args.m_appDesc.m_usage = D3D12TranslationLayer::RESOURCE_USAGE_DEFAULT;
            Args.m_appDesc.m_bindFlags = D3D12TranslationLayer::RESOURCE_BIND_UNORDERED_ACCESS | D3D12TranslationLayer::RESOURCE_BIND_SHADER_RESOURCE;
            Args.m_heapDesc = CD3DX12_HEAP_DESC(0, D3D12_HEAP_TYPE_DEFAULT);
            ModifyResourceArgsForMemFlags(Args, flags);

            Args.m_desc12.Dimension = Args.m_appDesc.m_resourceDimension;
            Args.m_desc12.Width = Args.m_appDesc.m_Width;
            Args.m_desc12.Height = Args.m_appDesc.m_Height;
            Args.m_desc12.DepthOrArraySize = Args.m_desc12.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ?
                (UINT16)Args.m_appDesc.m_Depth : Args.m_appDesc.m_ArraySize;
            Args.m_desc12.Format = Args.m_appDesc.m_Format;
            Args.m_desc12.MipLevels = Args.m_appDesc.m_MipLevels;
            Args.m_desc12.SampleDesc = { Args.m_appDesc.m_Samples, Args.m_appDesc.m_Quality };
            Args.m_desc12.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            Args.m_desc12.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            return Resource::CreateImage(context, Args, host_ptr, *image_format, *image_desc, flags);
        }
    }
    catch (std::bad_alloc &) { return ReportError(nullptr, CL_OUT_OF_HOST_MEMORY); }
    catch (_com_error &e)
    {
        if (e.Error() == E_INVALIDARG)
            return ReportError("Invalid buffer description.", CL_INVALID_VALUE);
        return ReportError(nullptr, CL_OUT_OF_RESOURCES);
    }
    catch (std::exception &e)
    {
        return ReportError(e.what(), CL_OUT_OF_RESOURCES);
    }
}

extern CL_API_ENTRY CL_EXT_PREFIX__VERSION_1_1_DEPRECATED cl_mem CL_API_CALL
clCreateImage2D(cl_context              context,
    cl_mem_flags            flags,
    const cl_image_format * image_format,
    size_t                  image_width,
    size_t                  image_height,
    size_t                  image_row_pitch,
    void *                  host_ptr,
    cl_int *                errcode_ret) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED
{
    cl_image_desc desc = {};
    desc.image_type = CL_MEM_OBJECT_IMAGE2D;
    desc.image_width = image_width;
    desc.image_height = image_height;
    desc.image_row_pitch = image_row_pitch;
    return clCreateImage(context, flags, image_format, &desc, host_ptr, errcode_ret);
}

extern CL_API_ENTRY CL_EXT_PREFIX__VERSION_1_1_DEPRECATED cl_mem CL_API_CALL
clCreateImage3D(cl_context              context,
    cl_mem_flags            flags,
    const cl_image_format * image_format,
    size_t                  image_width,
    size_t                  image_height,
    size_t                  image_depth,
    size_t                  image_row_pitch,
    size_t                  image_slice_pitch,
    void *                  host_ptr,
    cl_int *                errcode_ret) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED
{
    cl_image_desc desc = {};
    desc.image_type = CL_MEM_OBJECT_IMAGE3D;
    desc.image_width = image_width;
    desc.image_height = image_height;
    desc.image_depth = image_depth;
    desc.image_row_pitch = image_row_pitch;
    desc.image_slice_pitch = image_slice_pitch;
    return clCreateImage(context, flags, image_format, &desc, host_ptr, errcode_ret);
}

extern CL_API_ENTRY cl_int CL_API_CALL
clRetainMemObject(cl_mem memobj) CL_API_SUFFIX__VERSION_1_0
{
    if (!memobj)
    {
        return CL_INVALID_MEM_OBJECT;
    }
    static_cast<Resource*>(memobj)->Retain();
    return CL_SUCCESS;
}

extern CL_API_ENTRY cl_int CL_API_CALL
clReleaseMemObject(cl_mem memobj) CL_API_SUFFIX__VERSION_1_0
{
    if (!memobj)
    {
        return CL_INVALID_MEM_OBJECT;
    }
    static_cast<Resource*>(memobj)->Release();
    return CL_SUCCESS;
}

extern CL_API_ENTRY cl_int CL_API_CALL
clGetSupportedImageFormats(cl_context           context_,
    cl_mem_flags         flags,
    cl_mem_object_type   image_type,
    cl_uint              num_entries,
    cl_image_format *    image_formats,
    cl_uint *            num_image_formats) CL_API_SUFFIX__VERSION_1_0
{
    if (!context_)
    {
        return CL_INVALID_CONTEXT;
    }
    Context& context = *static_cast<Context*>(context_);
    auto ReportError = context.GetErrorReporter();

    {
        cl_int validation_error;
        if (!ValidateMemFlagsBase(flags, context.GetErrorReporter(&validation_error)))
        {
            return validation_error;
        }
    }

    switch (image_type)
    {
    case CL_MEM_OBJECT_IMAGE1D:
    case CL_MEM_OBJECT_IMAGE1D_ARRAY:
    case CL_MEM_OBJECT_IMAGE1D_BUFFER:
    case CL_MEM_OBJECT_IMAGE2D:
    case CL_MEM_OBJECT_IMAGE2D_ARRAY:
    case CL_MEM_OBJECT_IMAGE3D:
        break;
    default: return ReportError("Invalid image_type.", CL_INVALID_VALUE);
    }

    if (num_entries == 0 && image_formats != nullptr)
    {
        return ReportError("num_entries must be nonzero when image_formats is not null.", CL_INVALID_VALUE);
    }

    cl_uint NumFormats = 0;
    for (UINT i = 0; i < DXGI_FORMAT_B8G8R8X8_UNORM; ++i)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT Support = { (DXGI_FORMAT)i };
        if (FAILED(context.GetDevice().GetDevice()->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))))
        {
            continue;
        }

        if ((flags & (CL_MEM_WRITE_ONLY | CL_MEM_READ_WRITE)) &&
            (Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == D3D12_FORMAT_SUPPORT2_NONE)
        {
            continue;
        }

        // OpenCL 1.2 doesn't require a single kernel to be able to read and write images, so we can bind
        // readable images as SRVs and only require sample support, rather than typed UAV load.
        if ((flags & (CL_MEM_READ_ONLY | CL_MEM_READ_WRITE)) &&
            (Support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) == D3D12_FORMAT_SUPPORT1_NONE)
        {
            continue;
        }

        cl_image_format format = GetCLImageFormatForDXGIFormat(Support.Format);
        if (format.image_channel_data_type != 0)
        {
            if (NumFormats < num_entries)
            {
                image_formats[NumFormats] = format;
            }
            ++NumFormats;
        }
    }

    if (num_image_formats)
    {
        *num_image_formats = NumFormats;
    }
    return CL_SUCCESS;
}

extern CL_API_ENTRY cl_int CL_API_CALL
clGetMemObjectInfo(cl_mem           memobj,
    cl_mem_info      param_name,
    size_t           param_value_size,
    void *           param_value,
    size_t *         param_value_size_ret) CL_API_SUFFIX__VERSION_1_0
{
    if (!memobj)
    {
        return CL_INVALID_MEM_OBJECT;
    }

    Resource& resource = *static_cast<Resource*>(memobj);
    auto RetValue = [&](auto&& param)
    {
        return CopyOutParameter(param, param_value_size, param_value, param_value_size_ret);
    };
    switch (param_name)
    {
    case CL_MEM_TYPE: return RetValue(resource.m_Desc.image_type);
    case CL_MEM_FLAGS: return RetValue(resource.m_Flags);
    case CL_MEM_SIZE: return RetValue((size_t)resource.GetUnderlyingResource()->GetResourceSize()); // TODO: GetResourceAllocationInfo instead?
    case CL_MEM_HOST_PTR: return RetValue(resource.m_pHostPointer);
    case CL_MEM_MAP_COUNT: return RetValue(resource.GetMapCount());
    case CL_MEM_REFERENCE_COUNT: return RetValue(resource.GetRefCount());
    case CL_MEM_CONTEXT: return RetValue(&resource.m_Parent.get());
    case CL_MEM_ASSOCIATED_MEMOBJECT: return RetValue(resource.m_ParentBuffer.Get());
    case CL_MEM_OFFSET: return RetValue(resource.m_Offset);
    }
    return CL_INVALID_VALUE;
}

extern CL_API_ENTRY cl_int CL_API_CALL
clGetImageInfo(cl_mem           image,
    cl_image_info    param_name,
    size_t           param_value_size,
    void *           param_value,
    size_t *         param_value_size_ret) CL_API_SUFFIX__VERSION_1_0
{
    if (!image)
    {
        return CL_INVALID_MEM_OBJECT;
    }

    Resource& resource = *static_cast<Resource*>(image);
    if (resource.m_Desc.image_type == CL_MEM_OBJECT_BUFFER)
    {
        return resource.m_Parent->GetErrorReporter()("clGetImageInfo cannot be called on a buffer.", CL_INVALID_MEM_OBJECT);
    }
    auto RetValue = [&](auto&& param)
    {
        return CopyOutParameter(param, param_value_size, param_value, param_value_size_ret);
    };

    switch (param_name)
    {
    case CL_IMAGE_FORMAT: return RetValue(resource.m_Format);
    case CL_IMAGE_ELEMENT_SIZE: return RetValue((size_t)CD3D11FormatHelper::GetByteAlignment(GetDXGIFormatForCLImageFormat(resource.m_Format)));
    case CL_IMAGE_ROW_PITCH: return RetValue(resource.m_Desc.image_row_pitch);
    case CL_IMAGE_SLICE_PITCH: return RetValue(resource.m_Desc.image_slice_pitch);
    case CL_IMAGE_WIDTH: return RetValue(resource.m_Desc.image_width);
    case CL_IMAGE_DEPTH: return RetValue(resource.m_Desc.image_depth);
    case CL_IMAGE_ARRAY_SIZE: return RetValue(resource.m_Desc.image_array_size);
    case CL_IMAGE_BUFFER: return RetValue(resource.m_Desc.buffer);
    case CL_IMAGE_NUM_MIP_LEVELS: return RetValue(resource.m_Desc.num_mip_levels);
    case CL_IMAGE_NUM_SAMPLES: return RetValue(resource.m_Desc.num_samples);
    }
    return CL_INVALID_VALUE;
}

Resource* Resource::CreateBuffer(Context& Parent, D3D12TranslationLayer::ResourceCreationArgs& Args, void* pHostPointer, cl_mem_flags flags)
{
    UnderlyingResourcePtr Underlying(
        UnderlyingResource::CreateResource(
            &Parent.GetDevice().ImmCtx(),
            Args,
            D3D12TranslationLayer::ResourceAllocationContext::FreeThread));
    if (pHostPointer)
    {
        D3D11_SUBRESOURCE_DATA Data = { pHostPointer };
        auto Lock = Parent.GetDevice().GetTaskPoolLock();
        Parent.GetDevice().ImmCtx().UpdateSubresources(
            Underlying.get(),
            Underlying->GetFullSubresourceSubset(),
            &Data,
            nullptr,
            D3D12TranslationLayer::ImmediateContext::UpdateSubresourcesScenario::InitialData);
    }
    return new Resource(Parent, std::move(Underlying), pHostPointer, Args.m_appDesc.m_Width, flags);
}

Resource* Resource::CreateSubBuffer(Resource& ParentBuffer, const cl_buffer_region& region, cl_mem_flags flags)
{
    cl_image_format image_format = {};
    return new Resource(ParentBuffer, region.origin, region.size, image_format, CL_MEM_OBJECT_BUFFER, flags);
}

Resource* Resource::CreateImage(Context& Parent, D3D12TranslationLayer::ResourceCreationArgs& Args, void* pHostPointer, const cl_image_format& image_format, const cl_image_desc& image_desc, cl_mem_flags flags)
{
    UnderlyingResourcePtr Underlying(
        UnderlyingResource::CreateResource(
            &Parent.GetDevice().ImmCtx(),
            Args,
            D3D12TranslationLayer::ResourceAllocationContext::FreeThread));
    if (pHostPointer)
    {
        std::vector<D3D11_SUBRESOURCE_DATA> InitialData;
        D3D11_SUBRESOURCE_DATA SingleSubresourceInitialData;
        auto pData = &SingleSubresourceInitialData;
        assert(Args.m_appDesc.m_MipLevels == 1);
        if (Args.m_appDesc.m_SubresourcesPerPlane > 1)
        {
            InitialData.resize(Args.m_appDesc.m_SubresourcesPerPlane);
            pData = InitialData.data();
        }
        char* pSubresourceData = reinterpret_cast<char*>(pHostPointer);
        for (UINT i = 0; i < Args.m_appDesc.m_SubresourcesPerPlane; ++i)
        {
            pData[i].pSysMem = pSubresourceData;
            pData[i].SysMemPitch = (UINT)image_desc.image_row_pitch;
            pData[i].SysMemSlicePitch = (UINT)image_desc.image_slice_pitch;
            pSubresourceData += image_desc.image_slice_pitch;
        }
        auto Lock = Parent.GetDevice().GetTaskPoolLock();
        Parent.GetDevice().ImmCtx().UpdateSubresources(
            Underlying.get(),
            Underlying->GetFullSubresourceSubset(),
            pData,
            nullptr,
            D3D12TranslationLayer::ImmediateContext::UpdateSubresourcesScenario::InitialData);
    }
    return new Resource(Parent, std::move(Underlying), nullptr, image_format, image_desc, flags);
}

Resource* Resource::CreateImage1DBuffer(Resource& ParentBuffer, const cl_image_format& image_format, const cl_image_desc& image_desc, cl_mem_flags flags)
{
    return new Resource(ParentBuffer, 0, image_desc.image_width, image_format, image_desc.image_type, flags);
}

Resource::Resource(Context& Parent, UnderlyingResourcePtr Underlying, void* pHostPointer, size_t size, cl_mem_flags flags)
    : CLChildBase(Parent)
    , m_Flags(flags)
    , m_pHostPointer(pHostPointer)
    , m_Underlying(std::move(Underlying))
    , m_Desc(GetBufferDesc(size, CL_MEM_OBJECT_BUFFER))
{
}

Resource::Resource(Resource& ParentBuffer, size_t offset, size_t size, const cl_image_format& image_format, cl_mem_object_type type, cl_mem_flags flags)
    : CLChildBase(ParentBuffer.m_Parent.get())
    , m_Underlying(ParentBuffer.m_Underlying.get())
    , m_pHostPointer(ParentBuffer.m_pHostPointer && type == CL_MEM_OBJECT_BUFFER ? reinterpret_cast<char*>(ParentBuffer.m_pHostPointer) + offset : nullptr)
    , m_Flags(flags)
    , m_ParentBuffer(&ParentBuffer)
    , m_Format(image_format)
    , m_Offset(offset)
    , m_Desc(GetBufferDesc(size, type))
{
    if (type == CL_MEM_OBJECT_IMAGE1D_BUFFER)
    {
        DXGI_FORMAT DXGIFormat = GetDXGIFormatForCLImageFormat(image_format);
        UINT FormatByteSize = CD3D11FormatHelper::GetByteAlignment(DXGIFormat);
        assert(m_Offset == 0);

        if (flags & (CL_MEM_WRITE_ONLY | CL_MEM_READ_WRITE))
        {
            D3D12TranslationLayer::D3D12_UNORDERED_ACCESS_VIEW_DESC_WRAPPER UAVDescWrapper = {};
            auto &UAVDesc = UAVDescWrapper.m_Desc12;
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            UAVDesc.Format = DXGIFormat;
            UAVDesc.Buffer.CounterOffsetInBytes = 0;
            UAVDesc.Buffer.StructureByteStride = 0;
            UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            UAVDesc.Buffer.FirstElement = 0; // m_Offset / FormatByteSize;
            UAVDesc.Buffer.NumElements = (UINT)(size / FormatByteSize);

            UAVDescWrapper.m_D3D11UAVFlags = 0;

            m_UAV.emplace(&m_Parent->GetDevice().ImmCtx(), UAVDescWrapper, *m_Underlying);
        }
        if (flags & (CL_MEM_READ_ONLY | CL_MEM_READ_WRITE))
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
            SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            SRVDesc.Format = DXGIFormat;
            SRVDesc.Buffer.StructureByteStride = 0;
            SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            SRVDesc.Buffer.FirstElement = 0; // m_Offset / FormatByteSize;
            SRVDesc.Buffer.NumElements = (UINT)(size / FormatByteSize);

            m_SRV.emplace(&m_Parent->GetDevice().ImmCtx(), SRVDesc, *m_Underlying);
        }
    }
}

Resource::Resource(Context& Parent, UnderlyingResourcePtr Underlying, void* pHostPointer, const cl_image_format& image_format, const cl_image_desc& image_desc, cl_mem_flags flags)
    : CLChildBase(Parent)
    , m_Underlying(std::move(Underlying))
    , m_pHostPointer(pHostPointer)
    , m_Format(image_format)
    , m_Desc(image_desc)
    , m_Flags(flags)
{
    DXGI_FORMAT DXGIFormat = GetDXGIFormatForCLImageFormat(image_format);
    if (flags & (CL_MEM_WRITE_ONLY | CL_MEM_READ_WRITE))
    {
        D3D12TranslationLayer::D3D12_UNORDERED_ACCESS_VIEW_DESC_WRAPPER UAVDescWrapper = {};
        auto &UAVDesc = UAVDescWrapper.m_Desc12;
        UAVDesc.Format = DXGIFormat;
        switch (image_desc.image_type)
        {
        case CL_MEM_OBJECT_IMAGE1D:
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            UAVDesc.Texture1D.MipSlice = 0;
            break;
        case CL_MEM_OBJECT_IMAGE1D_ARRAY:
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            UAVDesc.Texture1DArray.FirstArraySlice = 0;
            UAVDesc.Texture1DArray.ArraySize = (UINT)image_desc.image_array_size;
            UAVDesc.Texture1DArray.MipSlice = 0;
            break;
        case CL_MEM_OBJECT_IMAGE2D:
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            UAVDesc.Texture2D.MipSlice = 0;
            UAVDesc.Texture2D.PlaneSlice = 0;
            break;
        case CL_MEM_OBJECT_IMAGE2D_ARRAY:
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            UAVDesc.Texture2DArray.FirstArraySlice = 0;
            UAVDesc.Texture2DArray.ArraySize = (UINT)image_desc.image_array_size;
            UAVDesc.Texture2DArray.MipSlice = 0;
            UAVDesc.Texture2DArray.PlaneSlice = 0;
            break;
        case CL_MEM_OBJECT_IMAGE3D:
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            UAVDesc.Texture3D.FirstWSlice = 0;
            UAVDesc.Texture3D.WSize = (UINT)image_desc.image_depth;
            UAVDesc.Texture3D.MipSlice = 0;
            break;
        default: assert(false);
        }

        m_UAV.emplace(&m_Parent->GetDevice().ImmCtx(), UAVDescWrapper, *m_Underlying);
    }

    if (flags & (CL_MEM_READ_ONLY | CL_MEM_READ_WRITE))
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGIFormat;
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        switch (image_desc.image_type)
        {
        case CL_MEM_OBJECT_IMAGE1D:
            SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            SRVDesc.Texture1D.MipLevels = 0;
            SRVDesc.Texture1D.MostDetailedMip = 0;
            SRVDesc.Texture1D.ResourceMinLODClamp = 0;
            break;
        case CL_MEM_OBJECT_IMAGE1D_ARRAY:
            SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            SRVDesc.Texture1DArray.FirstArraySlice = 0;
            SRVDesc.Texture1DArray.ArraySize = (UINT)image_desc.image_array_size;
            SRVDesc.Texture1DArray.MipLevels = 0;
            SRVDesc.Texture1DArray.MostDetailedMip = 0;
            SRVDesc.Texture1DArray.ResourceMinLODClamp = 0;
            break;
        case CL_MEM_OBJECT_IMAGE2D:
            SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            SRVDesc.Texture2D.MipLevels = 0;
            SRVDesc.Texture2D.MostDetailedMip = 0;
            SRVDesc.Texture2D.PlaneSlice = 0;
            SRVDesc.Texture2D.ResourceMinLODClamp = 0;
            break;
        case CL_MEM_OBJECT_IMAGE2D_ARRAY:
            SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            SRVDesc.Texture2DArray.FirstArraySlice = 0;
            SRVDesc.Texture2DArray.ArraySize = (UINT)image_desc.image_array_size;
            SRVDesc.Texture2DArray.MipLevels = 0;
            SRVDesc.Texture2DArray.MostDetailedMip = 0;
            SRVDesc.Texture2DArray.PlaneSlice = 0;
            SRVDesc.Texture2DArray.ResourceMinLODClamp = 0;
            break;
        case CL_MEM_OBJECT_IMAGE3D:
            SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            SRVDesc.Texture3D.MipLevels = 0;
            SRVDesc.Texture3D.MostDetailedMip = 0;
            SRVDesc.Texture3D.ResourceMinLODClamp = 0;
            break;
        default: assert(false);
        }

        m_SRV.emplace(&m_Parent->GetDevice().ImmCtx(), SRVDesc, *m_Underlying);
    }
}

cl_image_desc Resource::GetBufferDesc(size_t size, cl_mem_object_type type)
{
    cl_image_desc desc = {};
    desc.image_width = size;
    desc.image_type = type;
    return desc;
}