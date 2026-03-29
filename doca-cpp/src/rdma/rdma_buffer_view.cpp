#include <doca-cpp/rdma/rdma_buffer_view.hpp>

namespace doca::rdma
{

RdmaBufferView RdmaBufferView::Create(void * data, std::size_t size, uint32_t index)
{
    auto view = RdmaBufferView();
    view.data = data;
    view.size = size;
    view.index = index;
    return view;
}

void * RdmaBufferView::Data() const
{
    return this->data;
}

std::size_t RdmaBufferView::Size() const
{
    return this->size;
}

uint32_t RdmaBufferView::Index() const
{
    return this->index;
}

}  // namespace doca::rdma
