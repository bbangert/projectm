precision mediump float;

attribute vec2 vertex_position;
attribute vec4 vertex_color;
attribute vec2 vertex_texture;

varying vec4 fragment_color;
varying vec2 fragment_texture;

void main(){
    gl_Position = vec4(vertex_position, 0.0, 1.0);
    fragment_color = vertex_color;
    fragment_texture = vertex_texture;
}
