#version 330 core

layout(location = 0) in vec2 aPos;

uniform vec2 uViewport;
uniform vec4 uRect; // x, y, w, h in pixels (top-left origin)
uniform vec4 uUV;   // u0, v0, u1, v1

out vec2 vUV;

void main() {
    vec2 pixel = uRect.xy + aPos * uRect.zw;
    vec2 ndc = vec2(
        (pixel.x / uViewport.x) * 2.0 - 1.0,
        1.0 - (pixel.y / uViewport.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = mix(uUV.xy, uUV.zw, aPos);
}
