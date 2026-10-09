#version 450

layout(location = 0) in vec3 in_position;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 tint;
} pc;

layout(location = 0) out vec3 v_color;

void main() {
    gl_Position = pc.mvp * vec4(in_position, 1.0);

    float phi = atan(in_position.y, in_position.x);
    vec3 procedural = 0.5 + 0.5 * cos(phi + vec3(0.0, 2.0944, 4.1888));

    v_color = mix(vec3(1.0), procedural, pc.tint.a) * pc.tint.rgb;
}