precision mediump float;

varying vec2 fragment_tex_coord;

uniform sampler2D texture_sampler;

void main(){
    gl_FragColor = texture2D(texture_sampler, fragment_tex_coord);
}
