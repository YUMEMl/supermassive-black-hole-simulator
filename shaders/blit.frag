#ifdef TON618_VULKAN
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform BlitParameters {
    vec4 resolutionRepair;
} blit;

#define uResolution blit.resolutionRepair.xy
#define uAxisRepairWidth blit.resolutionRepair.z
#define uPolarRepairAmount blit.resolutionRepair.w
#else
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uScene;
uniform vec2 uResolution;
uniform float uAxisRepairWidth;
uniform float uPolarRepairAmount;
#endif

void main() {
    float halfRepairWidth = uAxisRepairWidth;
    float offsetFromCenter = gl_FragCoord.x - 0.5 * uResolution.x;
    if (abs(offsetFromCenter) < halfRepairWidth) {
        vec2 texel = 1.0 / uResolution;
        vec2 leftUv = vec2(0.5 - halfRepairWidth * texel.x, vUv.y);
        vec2 rightUv = vec2(0.5 + halfRepairWidth * texel.x, vUv.y);
        float gradientDistance = 4.0;
        vec2 leftOuterUv = vec2(
            0.5 - (halfRepairWidth + gradientDistance) * texel.x,
            vUv.y
        );
        vec2 rightOuterUv = vec2(
            0.5 + (halfRepairWidth + gradientDistance) * texel.x,
            vUv.y
        );
        vec4 leftColor = texture(uScene, leftUv);
        vec4 rightColor = texture(uScene, rightUv);
        vec4 leftSlope = (leftColor - texture(uScene, leftOuterUv)) *
            (2.0 * halfRepairWidth / gradientDistance);
        vec4 rightSlope = (texture(uScene, rightOuterUv) - rightColor) *
            (2.0 * halfRepairWidth / gradientDistance);
        float blend = clamp(
            (offsetFromCenter + halfRepairWidth) / (2.0 * halfRepairWidth),
            0.0,
            1.0
        );
        float blend2 = blend * blend;
        float blend3 = blend2 * blend;
        vec4 horizontalRepair =
            (2.0 * blend3 - 3.0 * blend2 + 1.0) * leftColor +
            (blend3 - 2.0 * blend2 + blend) * leftSlope +
            (-2.0 * blend3 + 3.0 * blend2) * rightColor +
            (blend3 - blend2) * rightSlope;
#ifdef TON618_VULKAN
        vec2 fragmentCoordinate = vec2(gl_FragCoord.x, uResolution.y - gl_FragCoord.y);
#else
        vec2 fragmentCoordinate = gl_FragCoord.xy;
#endif
        vec2 centeredPixels = fragmentCoordinate - 0.5 * uResolution;
        vec2 rotatedPixels = vec2(-centeredPixels.y, centeredPixels.x);
        vec2 rotatedUv = (rotatedPixels + 0.5 * uResolution) / uResolution;
#ifdef TON618_VULKAN
        rotatedUv.y = 1.0 - rotatedUv.y;
#endif
        vec4 polarRepair = texture(uScene, clamp(rotatedUv, vec2(0.0), vec2(1.0)));
        float polarFeather = 1.0 - smoothstep(0.0, halfRepairWidth, abs(offsetFromCenter));
        fragColor = mix(horizontalRepair, polarRepair, uPolarRepairAmount * polarFeather);
    } else {
        fragColor = texture(uScene, vUv);
    }
}
