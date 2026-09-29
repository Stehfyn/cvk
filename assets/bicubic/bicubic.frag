#version 450
layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(push_constant) uniform Constants
{
    vec2 texelSize;
    float sharpness;
    float padding;
    vec2 sampleScale;
};
layout(location = 0) in vec2 texcoord;
layout(location = 0) out vec4 color;

float cubic(float x)
{
    float a = -0.5;
    float absX = abs(x);
    float absX2 = absX * absX;
    float absX3 = absX2 * absX;
    if (absX <= 1.0)
    {
        return (a + 2.0) * absX3 - (a + 3.0) * absX2 + 1.0;
    }
    else if (absX < 2.0)
    {
        return a * absX3 - 5.0 * a * absX2 + 8.0 * a * absX - 4.0 * a;
    }

    return 0.0;
}

void main()
{
    vec3 bicubicColor = vec3(0.0);
    float totalWeight = 0.0;
    vec2 coord = texcoord * sampleScale;
    vec2 pixel = coord / texelSize - 0.5;
    vec2 basePixel = floor(pixel);
    vec2 fraction = pixel - basePixel;
    for (int j = -1; j <= 2; ++j)
    {
        for (int i = -1; i <= 2; ++i)
        {
            float weight = cubic(float(i) - fraction.x) * cubic(float(j) - fraction.y);
            bicubicColor += texture(inputImage, (basePixel + vec2(i, j) + 0.5) * texelSize).rgb * weight;
            totalWeight += weight;
        }
    }
    bicubicColor /= totalWeight;
    vec3 originalColor = texture(inputImage, coord).rgb;
    color = vec4(mix(bicubicColor, originalColor, sharpness), 1.0);
}
