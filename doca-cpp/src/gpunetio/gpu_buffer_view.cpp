#include <doca-cpp/gpunetio/gpu_buffer_view.hpp>

namespace doca::gpunetio
{

GpuBufferView GpuBufferView::Create(void * data, std::size_t size, uint32_t index)
{
    auto view = GpuBufferView();
    view.data = data;
    view.size = size;
    view.index = index;
    return view;
}

void * GpuBufferView::Data() const
{
    return this->data;
}

std::size_t GpuBufferView::Size() const
{
    return this->size;
}

uint32_t GpuBufferView::Index() const
{
    return this->index;
}

}  // namespace doca::gpunetio
