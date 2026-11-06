precision mediump float;

varying vec4 fragment_color;
varying vec2 fragment_texture;

uniform sampler2D texture_sampler;

void main(){
    gl_FragColor = fragment_color * texture2D(texture_sampler, fragment_texture.st);
}
