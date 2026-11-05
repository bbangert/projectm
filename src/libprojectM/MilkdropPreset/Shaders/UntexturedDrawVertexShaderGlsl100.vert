precision mediump float;

attribute vec2 vertex_position;
attribute vec4 vertex_color;

uniform mat4 vertex_transformation;
uniform float vertex_point_size;

varying vec4 fragment_color;

void main(){
    gl_Position = vertex_transformation * vec4(vertex_position, 0.0, 1.0);
    gl_PointSize = vertex_point_size;
    fragment_color = vertex_color;
}
