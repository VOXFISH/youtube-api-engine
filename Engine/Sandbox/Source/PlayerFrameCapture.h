#pragma once
#include "../../Tools/RiceCon/Shared/PlayerMediaChannel.h"
#include <glad/glad.h>
#include <array>
namespace Rice {
// Called with the fixed stream framebuffer bound, after game GUI rendering.
// Readback uses three PBOs and zero-timeout fences; a full queue drops capture
// work rather than waiting for the GPU or Controller.
class PlayerFrameCapture {
    struct Pending { GLuint pbo=0;GLsync fence=nullptr; };
    std::array<Pending,3> pending_{};
    std::shared_ptr<RiceController::PlayerMediaChannel> channel_;
    std::size_t next_=0;
    static constexpr int Width=1920,Height=1080;
    static constexpr std::size_t Bytes=static_cast<std::size_t>(Width)*Height*4;
public:
    explicit PlayerFrameCapture(std::shared_ptr<RiceController::PlayerMediaChannel> channel):channel_(std::move(channel)){
        GLint before=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&before);
        for(auto& item:pending_){glGenBuffers(1,&item.pbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,item.pbo);glBufferData(GL_PIXEL_PACK_BUFFER,Bytes,nullptr,GL_STREAM_READ);}
        glBindBuffer(GL_PIXEL_PACK_BUFFER,before);
    }
    ~PlayerFrameCapture(){for(auto& item:pending_){if(item.fence)glDeleteSync(item.fence);if(item.pbo)glDeleteBuffers(1,&item.pbo);}}
    void Capture(){
        GLint before=0,alignment=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&before);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
        for(std::size_t offset=0;offset<pending_.size();++offset){auto& item=pending_[(next_+offset)%pending_.size()];if(item.fence){
            const auto result=glClientWaitSync(item.fence,0,0);
            if(result==GL_ALREADY_SIGNALED||result==GL_CONDITION_SATISFIED){
                glBindBuffer(GL_PIXEL_PACK_BUFFER,item.pbo);
                if(const auto* bytes=static_cast<const std::uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,Bytes,GL_MAP_READ_BIT))){
                    try{channel_->PublishVideo({bytes,Bytes},Width,Height,true);}catch(...){glUnmapBuffer(GL_PIXEL_PACK_BUFFER);glBindBuffer(GL_PIXEL_PACK_BUFFER,before);throw;}
                    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                }
                glDeleteSync(item.fence);item.fence=nullptr;
            }else if(result==GL_WAIT_FAILED)throw std::runtime_error("Player video fence failed");
        }}
        auto& target=pending_[next_];
        if(!target.fence){
            glBindBuffer(GL_PIXEL_PACK_BUFFER,target.pbo);glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,Width,Height,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            target.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();next_=(next_+1)%pending_.size();
        }
        glPixelStorei(GL_PACK_ALIGNMENT,alignment);glBindBuffer(GL_PIXEL_PACK_BUFFER,before);
    }
};
}
