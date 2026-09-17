#version 450
layout(set = 0, binding = 0) uniform sampler2D luminance;
layout(set = 0, binding = 1) uniform sampler2D chrominance;
layout(location = 0) in vec2 texcoord;
layout(location = 0) out vec4 color;

void main()
{
    vec3 yuv = vec3(texture(luminance, texcoord).r, texture(chrominance, texcoord).rg);
    yuv -= vec3(0.062745, 0.501960, 0.501960);
    vec3 rgb = vec3(1.164383 * yuv.x + 1.596027 * yuv.z,
                    1.164383 * yuv.x - 0.391762 * yuv.y - 0.812968 * yuv.z,
                    1.164383 * yuv.x + 2.017232 * yuv.y);
    color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
