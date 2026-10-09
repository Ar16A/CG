#version 450

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_normal;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 n = normalize(v_normal);
    vec3 light_dir = normalize(vec3(0.4, 0.7, 0.6));

    float diffuse = max(dot(n, light_dir), 0.0);
    float light = 0.25 + 0.75 * diffuse;

    out_color = vec4(v_color * light, 1.0);
}