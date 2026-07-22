#version 330 core

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uFov;
uniform float uSpin;
uniform float uDiskInner;
uniform float uDiskOuter;
uniform float uAccretionRate;
uniform float uExposure;
uniform float uMinStep;
uniform float uMaxStep;
uniform int uMaxSteps;
uniform sampler2D uDiskTexture;

const float PI = 3.141592653589793;
const float HALF_PI = 1.5707963267948966;

struct InverseMetric {
    float tt;
    float tp;
    float rr;
    float hh;
    float pp;
};

struct CovariantMetric {
    float tt;
    float tp;
    float rr;
    float hh;
    float pp;
};

struct GeodesicState {
    float r;
    float theta;
    float phi;
    float pr;
    float ptheta;
};

struct GeodesicDerivative {
    float r;
    float theta;
    float phi;
    float pr;
    float ptheta;
};

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

InverseMetric inverseMetric(float r, float theta, float a) {
    float sinTheta = max(abs(sin(theta)), 0.0005);
    float sin2 = sinTheta * sinTheta;
    float cosTheta = cos(theta);
    float sigma = r * r + a * a * cosTheta * cosTheta;
    float delta = max(r * r - 2.0 * r + a * a, 0.00002);
    float bigA = (r * r + a * a) * (r * r + a * a) - a * a * delta * sin2;

    InverseMetric g;
    g.tt = -bigA / (sigma * delta);
    g.tp = -2.0 * a * r / (sigma * delta);
    g.rr = delta / sigma;
    g.hh = 1.0 / sigma;
    g.pp = (delta - a * a * sin2) / (sigma * delta * sin2);
    return g;
}

CovariantMetric covariantMetric(float r, float theta, float a) {
    float sinTheta = max(abs(sin(theta)), 0.0005);
    float sin2 = sinTheta * sinTheta;
    float cosTheta = cos(theta);
    float sigma = r * r + a * a * cosTheta * cosTheta;
    float delta = max(r * r - 2.0 * r + a * a, 0.00002);

    CovariantMetric g;
    g.tt = -(1.0 - 2.0 * r / sigma);
    g.tp = -2.0 * a * r * sin2 / sigma;
    g.rr = sigma / delta;
    g.hh = sigma;
    g.pp = sin2 * (r * r + a * a + 2.0 * a * a * r * sin2 / sigma);
    return g;
}

float hamiltonian(
    float r,
    float theta,
    float pr,
    float ptheta,
    float pt,
    float pphi,
    float a
) {
    InverseMetric g = inverseMetric(r, theta, a);
    return 0.5 * (
        g.tt * pt * pt +
        2.0 * g.tp * pt * pphi +
        g.pp * pphi * pphi +
        g.rr * pr * pr +
        g.hh * ptheta * ptheta
    );
}

GeodesicDerivative evaluateHamiltonEquations(
    GeodesicState state,
    float pt,
    float pphi,
    float a
) {
    float epsilonR = max(0.00018, state.r * 0.00012);
    float epsilonTheta = 0.00018;
    float dHdr = (
        hamiltonian(state.r + epsilonR, state.theta, state.pr, state.ptheta, pt, pphi, a) -
        hamiltonian(state.r - epsilonR, state.theta, state.pr, state.ptheta, pt, pphi, a)
    ) / (2.0 * epsilonR);
    float dHdTheta = (
        hamiltonian(state.r, state.theta + epsilonTheta, state.pr, state.ptheta, pt, pphi, a) -
        hamiltonian(state.r, state.theta - epsilonTheta, state.pr, state.ptheta, pt, pphi, a)
    ) / (2.0 * epsilonTheta);
    InverseMetric g = inverseMetric(state.r, state.theta, a);

    GeodesicDerivative derivative;
    derivative.r = g.rr * state.pr;
    derivative.theta = g.hh * state.ptheta;
    derivative.phi = g.tp * pt + g.pp * pphi;
    derivative.pr = -dHdr;
    derivative.ptheta = -dHdTheta;
    return derivative;
}

GeodesicState stateOffset(GeodesicState state, GeodesicDerivative derivative, float scale) {
    state.r += derivative.r * scale;
    state.theta += derivative.theta * scale;
    state.phi += derivative.phi * scale;
    state.pr += derivative.pr * scale;
    state.ptheta += derivative.ptheta * scale;
    return state;
}

GeodesicState integrateRk4(
    GeodesicState state,
    float pt,
    float pphi,
    float a,
    float h
) {
    GeodesicDerivative k1 = evaluateHamiltonEquations(state, pt, pphi, a);
    GeodesicDerivative k2 = evaluateHamiltonEquations(stateOffset(state, k1, 0.5 * h), pt, pphi, a);
    GeodesicDerivative k3 = evaluateHamiltonEquations(stateOffset(state, k2, 0.5 * h), pt, pphi, a);
    GeodesicDerivative k4 = evaluateHamiltonEquations(stateOffset(state, k3, h), pt, pphi, a);

    state.r += h * (k1.r + 2.0 * k2.r + 2.0 * k3.r + k4.r) / 6.0;
    state.theta += h * (k1.theta + 2.0 * k2.theta + 2.0 * k3.theta + k4.theta) / 6.0;
    state.phi += h * (k1.phi + 2.0 * k2.phi + 2.0 * k3.phi + k4.phi) / 6.0;
    state.pr += h * (k1.pr + 2.0 * k2.pr + 2.0 * k3.pr + k4.pr) / 6.0;
    state.ptheta += h * (k1.ptheta + 2.0 * k2.ptheta + 2.0 * k3.ptheta + k4.ptheta) / 6.0;
    return state;
}

float boyerLindquistRadius(vec3 p, float a) {
    float rho2 = dot(p, p);
    float term = rho2 - a * a;
    return sqrt(max(0.5 * (term + sqrt(term * term + 4.0 * a * a * p.y * p.y)), 0.001));
}

bool initializePhoton(
    vec3 origin,
    vec3 direction,
    float a,
    out GeodesicState state,
    out float pt,
    out float pphi
) {
    float r = boyerLindquistRadius(origin, a);
    float theta = acos(clamp(origin.y / max(r, 0.001), -1.0, 1.0));
    float phi = atan(origin.z, origin.x);

    float sinTheta = max(sin(theta), 0.0005);
    vec3 er = vec3(sinTheta * cos(phi), cos(theta), sinTheta * sin(phi));
    vec3 etheta = vec3(cos(theta) * cos(phi), -sinTheta, cos(theta) * sin(phi));
    vec3 ephi = vec3(-sin(phi), 0.0, cos(phi));

    float dr = dot(direction, er);
    float dtheta = dot(direction, etheta) / max(r, 0.001);
    float dphi = dot(direction, ephi) / max(r * sinTheta, 0.001);

    CovariantMetric g = covariantMetric(r, theta, a);
    float A = g.tt;
    float B = 2.0 * g.tp * dphi;
    float C = g.rr * dr * dr + g.hh * dtheta * dtheta + g.pp * dphi * dphi;
    float discriminant = max(B * B - 4.0 * A * C, 0.0);
    float dt = (-B - sqrt(discriminant)) / (2.0 * A);

    float rawPt = g.tt * dt + g.tp * dphi;
    float energy = max(-rawPt, 0.00001);
    pt = rawPt / energy;
    pphi = (g.tp * dt + g.pp * dphi) / energy;

    state.r = r;
    state.theta = theta;
    state.phi = phi;
    state.pr = g.rr * dr / energy;
    state.ptheta = g.hh * dtheta / energy;
    return discriminant > 0.0;
}

vec3 temperatureColor(float temperature) {
    vec3 amber = vec3(1.00, 0.18, 0.025);
    vec3 gold = vec3(1.00, 0.58, 0.16);
    vec3 white = vec3(1.00, 0.94, 0.76);
    vec3 blueWhite = vec3(0.78, 0.90, 1.00);
    float t = clamp(temperature, 0.0, 1.0);
    if (t < 0.42) return mix(amber, gold, t / 0.42);
    if (t < 0.78) return mix(gold, white, (t - 0.42) / 0.36);
    return mix(white, blueWhite, (t - 0.78) / 0.22);
}

vec3 acesFilm(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 starLayer(vec2 spherical, vec2 gridSize, float threshold, float intensity) {
    vec2 grid = spherical * gridSize;
    vec2 cell = floor(grid);
    vec2 local = fract(grid) - 0.5;
    float seed = hash12(cell);
    vec2 offset = vec2(hash12(cell + 17.31), hash12(cell + 53.73)) - 0.5;
    offset *= 0.62;
    float distanceToStar = length(local - offset);
    float radius = mix(0.045, 0.13, pow(seed, 10.0));
    float core = 1.0 - smoothstep(radius, radius + 0.055, distanceToStar);
    float halo = 1.0 - smoothstep(radius, radius * 3.4 + 0.08, distanceToStar);
    float visible = step(threshold, seed);
    vec3 starColor = mix(
        vec3(0.52, 0.68, 1.0),
        vec3(1.0, 0.78, 0.50),
        hash12(cell + 9.17)
    );
    return starColor * visible * intensity * (core + halo * 0.16) * (0.65 + 1.8 * seed);
}

vec3 starField(float theta, float phi) {
    vec2 spherical = vec2(phi / (2.0 * PI), theta / PI);
    vec3 stars = starLayer(spherical, vec2(920.0, 460.0), 0.9962, 1.30);
    stars += starLayer(spherical + vec2(0.173, 0.071), vec2(410.0, 205.0), 0.9983, 2.15);
    float bandShape = max(0.0, 1.0 - abs(cos(theta) + 0.12 * sin(phi * 3.0)));
    float galacticBand = pow(bandShape, 10.0);
    float fineDust = 0.72 + 0.28 * hash12(floor(spherical * vec2(260.0, 130.0)));
    vec3 dust = mix(vec3(0.010, 0.017, 0.034), vec3(0.035, 0.048, 0.074), galacticBand);
    return stars + dust * galacticBand * fineDust;
}

vec4 sampleDisk(float r, float phi, float pt, float pphi, float a) {
    float radial = clamp((r - uDiskInner) / max(uDiskOuter - uDiskInner, 0.001), 0.0, 1.0);
    float textureAngle = phi / (2.0 * PI) - uTime * 0.006 / max(pow(r, 1.5), 1.0);
    vec2 turbulentUv = vec2(
        fract(textureAngle + radial * 2.37 + sin(r * 1.71) * 0.11),
        fract(radial * 5.0 + sin(textureAngle * 2.0 * PI) * 0.18)
    );
    vec3 textureDetail = texture(uDiskTexture, turbulentUv).rgb;
    float textureLuma = dot(textureDetail, vec3(0.299, 0.587, 0.114));
    float filament = mix(0.52, 1.38, smoothstep(0.08, 0.92, textureLuma));
    float fineRing = 0.88 + 0.12 * sin(
        r * 8.3 + sin(textureAngle * 2.0 * PI) * 2.2 + textureLuma * 4.0
    );
    filament *= fineRing;

    // Thin-disk temperature law: T is proportional to r^(-3/4).
    float radialTemperature = pow(max(uDiskInner / r, 0.0001), 0.75);
    float temperature = radialTemperature * pow(max(uAccretionRate, 0.001), 0.25);
    float innerFade = smoothstep(uDiskInner, uDiskInner * 1.08, r);
    float outerFade = 1.0 - smoothstep(uDiskOuter * 0.78, uDiskOuter, r);

    // Keplerian equatorial four-velocity and invariant frequency shift.
    CovariantMetric g = covariantMetric(r, HALF_PI, a);
    float omega = 1.0 / (pow(r, 1.5) + a);
    float ut = inversesqrt(max(-(g.tt + 2.0 * g.tp * omega + g.pp * omega * omega), 0.0001));
    float emittedEnergy = max(-ut * (pt + omega * pphi), 0.02);
    float redshift = clamp((-pt) / emittedEnergy, 0.16, 4.2);
    float invariantIntensity = pow(redshift, 3.0);

    vec3 color = temperatureColor(temperature);
    color *= mix(vec3(1.22, 0.43, 0.18), vec3(0.80, 0.98, 1.20), smoothstep(0.72, 1.55, redshift));
    float emissivity = pow(radialTemperature, 2.2) * uAccretionRate * filament * innerFade * outerFade;
    float alpha = clamp(0.18 + emissivity * 0.64, 0.0, 0.86);
    return vec4(color * emissivity * invariantIntensity * 1.16, alpha);
}

vec3 tracePixel(vec2 pixel) {
    float focal = 1.0 / tan(radians(uFov) * 0.5);
    vec3 rayDirection = normalize(
        uCameraForward * focal + uCameraRight * pixel.x + uCameraUp * pixel.y
    );

    GeodesicState state;
    float pt;
    float pphi;
    if (!initializePhoton(uCameraPosition, rayDirection, uSpin, state, pt, pphi)) {
        return vec3(0.0);
    }

    float horizon = 1.0 + sqrt(max(1.0 - uSpin * uSpin, 0.00001));
    float cameraRadius = state.r;
    vec3 accumulated = vec3(0.0);
    float opacity = 0.0;
    int diskCrossings = 0;
    bool captured = false;
    bool escaped = false;

    for (int stepIndex = 0; stepIndex < 256; ++stepIndex) {
        if (stepIndex >= uMaxSteps) break;
        if (state.r <= horizon * 1.0015) {
            captured = true;
            break;
        }
        // Once an inward ray has turned around and returned to the observer's
        // radius, its asymptotic sky direction is known. A small tolerance
        // avoids spending extra steps just to travel beyond the camera.
        if (state.r > cameraRadius * 0.995 && state.pr > 0.0 && stepIndex > 8) {
            escaped = true;
            break;
        }

        float previousTheta = state.theta;
        float previousRadius = state.r;
        float proximity = max(state.r - horizon, 0.0);
        float h = mix(uMinStep, uMaxStep, smoothstep(0.15, 11.0, proximity));
        // Kerr curvature is weak far outside the disk, so larger affine steps
        // are safe there. This keeps zoomed-out camera paths within the fixed
        // GPU loop budget without reducing near-horizon accuracy.
        float weakFieldStart = max(uDiskOuter * 1.25, 20.0);
        float weakFieldBoost = mix(1.0, 4.5, smoothstep(weakFieldStart, weakFieldStart + 28.0, state.r));
        h *= weakFieldBoost;
        state = integrateRk4(state, pt, pphi, uSpin, h);

        if (state.theta < 0.001) {
            state.theta = 0.001;
            state.ptheta = abs(state.ptheta);
        } else if (state.theta > PI - 0.001) {
            state.theta = PI - 0.001;
            state.ptheta = -abs(state.ptheta);
        }

        // Re-project p_r onto H=0 to control numerical drift of the null constraint.
        if ((stepIndex & 15) == 15 && state.r > horizon * 1.03) {
            InverseMetric projectionMetric = inverseMetric(state.r, state.theta, uSpin);
            float rest =
                projectionMetric.tt * pt * pt +
                2.0 * projectionMetric.tp * pt * pphi +
                projectionMetric.pp * pphi * pphi +
                projectionMetric.hh * state.ptheta * state.ptheta;
            float radialSquare = max(-rest / max(projectionMetric.rr, 0.00001), 0.0);
            state.pr = (state.pr < 0.0 ? -1.0 : 1.0) * sqrt(radialSquare);
        }

        if ((previousTheta - HALF_PI) * (state.theta - HALF_PI) <= 0.0 && opacity < 0.985 && diskCrossings < 3) {
            float thetaDelta = state.theta - previousTheta;
            float safeThetaDelta = abs(thetaDelta) < 0.00001
                ? (thetaDelta < 0.0 ? -0.00001 : 0.00001)
                : thetaDelta;
            float crossing = clamp((HALF_PI - previousTheta) / safeThetaDelta, 0.0, 1.0);
            float diskRadius = mix(previousRadius, state.r, crossing);
            if (diskRadius > uDiskInner && diskRadius < uDiskOuter) {
                vec4 disk = sampleDisk(diskRadius, state.phi, pt, pphi, uSpin);
                accumulated += (1.0 - opacity) * disk.rgb;
                opacity += (1.0 - opacity) * disk.a;
                diskCrossings += 1;
            }
        }
    }

    // If the loop budget ends while a photon is already moving outward in the
    // weak-field region, classify it as escaped rather than painting it black.
    float weakFieldEscapeRadius = max(uDiskOuter * 1.35, 22.0);
    if (!captured && !escaped && state.pr > 0.0 && state.r > weakFieldEscapeRadius) {
        escaped = true;
    }

    vec3 background = escaped ? starField(state.theta, state.phi) : vec3(0.0);
    if (captured) background = vec3(0.0);
    vec3 color = background * (1.0 - opacity) + accumulated;
    color *= uExposure;
    color = acesFilm(color);
    color = pow(max(color, 0.0), vec3(1.0 / 2.2));
    float vignette = 1.0 - 0.12 * dot(pixel, pixel);
    return color * max(vignette, 0.45);
}

vec3 traceStablePixel(vec2 pixel) {
    // Boyer-Lindquist coordinates are singular on the rotation axis. Rays with
    // almost zero axial angular momentum can therefore accumulate visible
    // numerical noise in a very narrow screen-space band. Reconstruct only
    // that band from stable neighbouring geodesics.
    float seamWidth = 20.0 / uResolution.y;
    vec3 color;
    if (abs(pixel.x) < seamWidth) {
        float neighbor = 48.0 / uResolution.y;
        vec3 leftColor = tracePixel(vec2(-neighbor, pixel.y));
        vec3 rightColor = tracePixel(vec2(neighbor, pixel.y));
        float blend = clamp(0.5 + 0.5 * pixel.x / seamWidth, 0.0, 1.0);
        color = mix(leftColor, rightColor, blend);
    } else {
        color = tracePixel(pixel);
    }
    return color;
}

void main() {
    vec2 pixel = (2.0 * gl_FragCoord.xy - uResolution) / uResolution.y;
    vec2 sampleOffset = vec2(0.62, -0.38) / uResolution.y;
    vec3 color = 0.5 * (
        traceStablePixel(pixel - sampleOffset) +
        traceStablePixel(pixel + sampleOffset)
    );
    fragColor = vec4(color, 1.0);
}
