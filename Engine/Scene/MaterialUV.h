#pragma once
namespace Rice {
// Shared alpha/albedo coordinates for color and depth passes.
inline const char* MaterialUVGLSL=R"(
uniform int cubeLayout;
in vec3 localNormal;
in vec3 localPosition;
vec2 materialUV(){
    if(cubeLayout==0)return UV;
    vec3 n=localNormal;
    int face;
    if(abs(n.x)>=abs(n.y) && abs(n.x)>=abs(n.z))face=n.x>0.0?0:1;
    else if(abs(n.y)>=abs(n.z))face=n.y>0.0?2:3;
    else face=n.z>0.0?4:5;
    vec2 cell;
    vec2 grid;
    vec2 uv=UV;
    if(cubeLayout==1){
        grid=vec2(3,4);
        if(face==0)cell=vec2(1,1);
        if(face==1){cell=vec2(1,3);uv=vec2(-localPosition.z,-localPosition.y)+.5;}
        if(face==2){cell=vec2(1,0);uv=vec2(-localPosition.z,-localPosition.x)+.5;}
        if(face==3){cell=vec2(1,2);uv=vec2(-localPosition.z,localPosition.x)+.5;}
        if(face==4)cell=vec2(0,1);
        if(face==5)cell=vec2(2,1);
    }else if(cubeLayout==2){
        grid=vec2(4,3);
        if(face==0)cell=vec2(2,1);
        if(face==1)cell=vec2(0,1);
        if(face==2)cell=vec2(1,0);
        if(face==3)cell=vec2(1,2);
        if(face==4)cell=vec2(1,1);
        if(face==5)cell=vec2(3,1);
    }else{
        grid=cubeLayout==3?vec2(6,1):vec2(1,6);
        cell=cubeLayout==3?vec2(face,0):vec2(0,face);
    }
    // Source cells use top-left image coordinates. The loader flips images for GL.
    cell.y=grid.y-1.0-cell.y;
    vec2 pixels=vec2(textureSize(image,0))/grid;
    uv=clamp(uv,0.0,1.0)*(1.0-1.0/pixels)+.5/pixels;
    return (cell+uv)/grid;
}
)";
}
