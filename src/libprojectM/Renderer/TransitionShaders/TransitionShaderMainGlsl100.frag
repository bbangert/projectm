
void main() {
    vec4 _user_out_color = vec4(1.0, 1.0, 1.0, 1.0);

    mainImage(_user_out_color, gl_FragCoord.xy);

    gl_FragColor = vec4(_user_out_color.xyz, 1.0);
}
