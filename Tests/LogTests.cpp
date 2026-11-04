#include "Rice/Debug.h"
#include <thread>
#include <iostream>
int main(){
    Rice::Debug::Init();Rice::Debug::Init();Rice::Debug::ClearMessages();
    RICE_CORE_INFO("Window startup {}",42);RICE_ERROR("Game failure");spdlog::warn("Default logger");
    auto messages=Rice::Debug::GetMessages();
    if(messages.size()!=3||messages[0].text.find("Rice: Window startup 42")==std::string::npos||messages[1].level!=spdlog::level::err||messages[2].level!=spdlog::level::warn)return 1;
    Rice::Debug::ClearMessages();std::vector<std::thread> threads;
    for(int i=0;i<4;++i)threads.emplace_back([i]{for(int j=0;j<600;++j)RICE_LOG("Worker {} item {}",i,j);});
    for(auto& thread:threads)thread.join();messages=Rice::Debug::GetMessages();
    if(messages.size()!=Rice::Debug::MessageCapacity)return 2;
    for(size_t i=1;i<messages.size();++i)if(messages[i].sequence<=messages[i-1].sequence)return 3;
    RICE_CORE_WARNING("{}",std::string(20000,'x'));messages=Rice::Debug::GetMessages();if(messages.back().text.size()>16420)return 4;
    Rice::Debug::ClearMessages();if(!Rice::Debug::GetMessages().empty())return 5;
    std::cout<<"Editor log routing, concurrency and capacity passed\n";return 0;
}
