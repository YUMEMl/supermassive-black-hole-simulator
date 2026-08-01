#ifdef TON618_VULKAN
layout(location = 0) out vec2 vUv;
#define TON618_VERTEX_ID gl_VertexIndex
#else
out vec2 vUv;
#define TON618_VERTEX_ID gl_VertexID
#endif

void main() {
    vec2 position = vec2(
        (TON618_VERTEX_ID == 1) ? 3.0 : -1.0,
        (TON618_VERTEX_ID == 2) ? 3.0 : -1.0
    );
    vUv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
