precision mediump float;

// GLSL ES 1.00 has no gl_VertexID and the GLES 2 backend has no float u/v render target to read
// back, so both end points of each motion vector line are computed on the CPU and passed in as
// plain positions (texture coordinates, 0...1).
attribute vec2 vertex_position;
attribute vec4 vertex_color;

uniform mat4 vertex_transformation;

varying vec4 fragment_color;

void main() {
    // Transform positions from 0...1 to -1...1 in each direction.
    vec2 pos = vertex_position * 2.0 - 1.0;

    // Flip final Y, as we draw this top-down, which is bottom-up in OpenGL.
    pos.y = -pos.y;

    // Now we've got the usual coordinates, apply our orthogonal transformation.
    gl_Position = vertex_transformation * vec4(pos, 0.0, 1.0);
    fragment_color = vertex_color;
}
