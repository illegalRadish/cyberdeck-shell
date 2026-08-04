#version 330 core

layout(location = 0) in vec2 aPos;

uniform vec2 uViewport;
uniform vec4 uRect;  // x, y, w, h (pixels, top-left origin)

out vec2 vLocal;     // 0..1 across the quad
out vec2 vFrag;      // pixel coords (top-left origin)

void main() {
    vLocal = aPos;
    vec2 pixel = uRect.xy + aPos * uRect.zw;
    vFrag = pixel;
    // convert to NDC (top-left origin)
    vec2 ndc;
    ndc.x = (pixel.x / uViewport.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (pixel.y / uViewport.y) * 2.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
