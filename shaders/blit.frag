#version 330 core

in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uScene;
uniform vec2 uResolution;
uniform float uAxisRepairWidth;
uniform float uPolarRepairAmount;

void main() {
    float halfRepairWidth = uAxisRepairWidth;
    float offsetFromCenter = gl_FragCoord.x - 0.5 * uResolution.x;
    if (abs(offsetFromCenter) < halfRepairWidth) {
        vec2 texel = 1.0 / uResolution;
        vec2 leftUv = vec2(0.5 - halfRepairWidth * texel.x, vUv.y);
        vec2 rightUv = vec2(0.5 + halfRepairWidth * texel.x, vUv.y);
        float blend = smoothstep(
            -halfRepairWidth,
            halfRepairWidth,
            offsetFromCenter
        );
        vec4 horizontalRepair = mix(texture(uScene, leftUv), texture(uScene, rightUv), blend);
        vec2 centeredPixels = gl_FragCoord.xy - 0.5 * uResolution;
        vec2 rotatedPixels = vec2(-centeredPixels.y, centeredPixels.x);
        vec2 rotatedUv = (rotatedPixels + 0.5 * uResolution) / uResolution;
        vec4 polarRepair = texture(uScene, clamp(rotatedUv, vec2(0.0), vec2(1.0)));
        float polarFeather = 1.0 - smoothstep(0.0, halfRepairWidth, abs(offsetFromCenter));
        fragColor = mix(horizontalRepair, polarRepair, uPolarRepairAmount * polarFeather);
    } else {
        fragColor = texture(uScene, vUv);
    }
}
