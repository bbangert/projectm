precision mediump float;

varying vec4 fragment_color;
varying vec2 fragment_texture;

uniform sampler2D texture_sampler;

uniform int blend_mode;

void main() {
    // GLSL ES 1.00 has no switch statement. See MilkdropSpriteFragmentGlsl330.frag for the original
    // Milkdrop code equivalents of each blend mode.
    vec4 texel = texture2D(texture_sampler, fragment_texture.st);

    if (blend_mode == 0)
    {
        // Vertex diffuse color modulated with texture color, vertex alpha only.
        gl_FragColor = vec4(fragment_color.rgb * texel.rgb, fragment_color.a);
    }
    else if (blend_mode == 1 || blend_mode == 2)
    {
        // Vertex and texture colors modulated, then scaled by vertex alpha. Full opacity.
        gl_FragColor = vec4((fragment_color.rgb * texel.rgb) * fragment_color.a, 1.0);
    }
    else if (blend_mode == 3)
    {
        // Texture color only, full opacity.
        gl_FragColor = vec4(texel.rgb, 1.0);
    }
    else
    {
        // Vertex diffuse color and alpha modulated with texture color and alpha.
        gl_FragColor = fragment_color * texel;
    }
}
