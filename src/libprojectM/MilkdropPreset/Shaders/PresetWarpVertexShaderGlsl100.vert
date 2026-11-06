precision mediump float;

// The GLES 2 backend computes the warped u/v coordinates per vertex on the CPU (PerPixelWarp.hpp),
// as it keeps them for the next frame's motion vectors and has no float render target for them.
attribute vec2 vertex_position;
attribute vec2 rad_ang;
attribute vec2 warp_uv;

uniform mat4 vertex_transformation;
uniform vec2 texelOffset;
uniform float decay;

varying vec4 frag_COLOR;
varying vec4 frag_TEXCOORD0;
varying vec2 frag_TEXCOORD1;

void main() {
    gl_Position = vertex_transformation * vec4(vertex_position, 0.0, 1.0);

    // original UV coordinates
    vec2 uv_original = vertex_position * 0.5 + 0.5 + texelOffset;

    frag_COLOR = vec4(decay, decay, decay, 1.0);
    frag_TEXCOORD0.xy = warp_uv;
    frag_TEXCOORD0.zw = uv_original;
    frag_TEXCOORD1 = rad_ang;
}
