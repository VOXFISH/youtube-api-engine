#include "Streaming/StreamReadiness.h"
#include <iostream>
#include <stdexcept>
using namespace Rice::Streaming;
int main(){
    try {
        const auto first=StreamReadiness::BeginInstance();
        if(StreamReadiness::IsReady())throw std::runtime_error("Instance automatically became ready");
        if(!ReadyForStream(first) || !StreamReadiness::IsReady())throw std::runtime_error("Explicit readiness signal failed");
        const auto next=StreamReadiness::BeginInstance();
        if(ReadyForStream(first) || StreamReadiness::IsReady())throw std::runtime_error("Late old callback marked new instance ready");
        if(!ReadyForStream(next))throw std::runtime_error("Current token rejected");
        StreamReadiness::Retire();
        if(ReadyForStream(next) || ReadyForStream() || StreamReadiness::IsReady())throw std::runtime_error("Retired game became ready");
        std::cout<<"Explicit stream readiness tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
