#version 330 core

layout(location = 0) in vec2 aPos;

uniform vec2 uViewport;
uniform vec4 uRect; // x, y, w, h in pixels (top-left origin)

out vec2 vLocal; // position within the rect, in pixels

void main() {
    // aPos is a unit quad in [0,1]
    vLocal = aPos * uRect.zw;
    vec2 pixel = uRect.xy + aPos * uRect.zw;
    vec2 ndc = vec2(
        (pixel.x / uViewport.x) * 2.0 - 1.0,
        1.0 - (pixel.y / uViewport.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
