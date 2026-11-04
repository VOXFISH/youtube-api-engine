#include "Rice/Benchmark.h"
#include "ViewportTarget.h"
#include <glad/glad.h>
#include <algorithm>
#include <stdexcept>
namespace Rice {
ViewportTarget::~ViewportTarget(){ RICE_BENCHMARK_FUNCTION(); if(depth)glDeleteRenderbuffers(1,&depth);if(color)glDeleteTextures(1,&color);if(framebuffer)glDeleteFramebuffers(1,&framebuffer);}
uint32_t ViewportTarget::Render(int w,int h,const std::function<void()>& draw){
    RICE_BENCHMARK_FUNCTION();
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);w=std::clamp(w,1,limit);h=std::clamp(h,1,limit);
    GLint oldFramebuffer,viewport[4],oldTexture,oldRenderbuffer;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFramebuffer);glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);glGetIntegerv(GL_RENDERBUFFER_BINDING,&oldRenderbuffer);
    bool scissor=glIsEnabled(GL_SCISSOR_TEST);glDisable(GL_SCISSOR_TEST);
    auto restore=[&]{ RICE_BENCHMARK_FUNCTION(); glBindFramebuffer(GL_FRAMEBUFFER,oldFramebuffer);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glBindTexture(GL_TEXTURE_2D,oldTexture);glBindRenderbuffer(GL_RENDERBUFFER,oldRenderbuffer);if(scissor)glEnable(GL_SCISSOR_TEST);};
    try{
        if(!framebuffer){glGenFramebuffers(1,&framebuffer);glGenTextures(1,&color);glGenRenderbuffers(1,&depth);}
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        if(w!=width||h!=height){
            glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
            glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Viewport framebuffer incomplete");width=w;height=h;
        }
        glViewport(0,0,w,h);glClearColor(.075f,.08f,.09f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);draw();restore();return color;
    }catch(...){restore();throw;}
}
}
