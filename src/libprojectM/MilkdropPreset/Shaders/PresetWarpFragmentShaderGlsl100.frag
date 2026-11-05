precision mediump float;

varying vec4 frag_COLOR;
varying vec4 frag_TEXCOORD0;
varying vec2 frag_TEXCOORD1;

uniform sampler2D texture_sampler;

void main() {
    // Main image. The GLES 2 backend has a single color output, so the motion vector u/v coords
    // are not written here.
    gl_FragColor = frag_COLOR * texture2D(texture_sampler, frag_TEXCOORD0.xy);
}
