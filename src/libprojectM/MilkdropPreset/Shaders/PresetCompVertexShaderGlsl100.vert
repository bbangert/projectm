precision mediump float;

attribute vec2 vertex_position;
attribute vec4 vertex_color;
attribute vec2 vertex_texture;
attribute vec2 vertex_rad_ang;

varying vec4 frag_COLOR;
varying vec2 frag_TEXCOORD0;
varying vec2 frag_TEXCOORD1;

void main(){
    vec4 position = vec4(vertex_position, 0.0, 1.0);
    gl_Position = position;
    frag_COLOR = vertex_color;
    frag_TEXCOORD0 = vertex_texture;
    frag_TEXCOORD1 = vertex_rad_ang;
}
