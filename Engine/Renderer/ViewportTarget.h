#pragma once
#include <cstdint>
#include <functional>
namespace Rice {
class ViewportTarget {
    uint32_t framebuffer=0,color=0,depth=0;
    int width=0,height=0;
public:
    ViewportTarget()=default;
    ViewportTarget(const ViewportTarget&)=delete;
    ~ViewportTarget();
    uint32_t Render(int w,int h,const std::function<void()>& draw);
};
}
