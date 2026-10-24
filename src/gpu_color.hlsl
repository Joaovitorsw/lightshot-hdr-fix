Texture2D<float4> source : register(t0);
cbuffer Conversion : register(b0) { float white; uint hdr; float2 padding; };
float4 Vertex(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2,-2) + float2(-1,1),0,1);
}
float Finite(float x) {
    uint magnitude = asuint(x) & 0x7fffffff;
    if (magnitude > 0x7f800000) return 0;
    if (magnitude == 0x7f800000) return (asuint(x) & 0x80000000) == 0 ? asfloat(0x7f7fffff) : -asfloat(0x7f7fffff);
    return x;
}
float Encode(float x) {
    x = saturate(x);
    return x <= .0031308 ? 12.92*x : 1.055*pow(x,1.0/2.4)-.055;
}
float4 Pixel(float4 position : SV_Position) : SV_Target {
    float3 raw = source.Load(int3(int2(position.xy),0)).rgb;
    raw = float3(Finite(raw.r),Finite(raw.g),Finite(raw.b));
    float3 color = raw;
    if (hdr != 0) {
        // Scale before evaluating luminance: equivalent to the CPU shoulder,
        // without overflow for FP16 infinities or very small SDR white levels.
        float peak = max(max(abs(raw.r),abs(raw.g)),abs(raw.b));
        float effectiveWhite = white;
        if (peak > 1e30) {
            raw *= 1e-20;
            peak *= 1e-20;
            effectiveWhite *= 1e-20;
        }
        if (peak > 0) {
            float3 unitColor = raw / peak;
            float unitLuminance = dot(unitColor,float3(.2126,.7152,.0722));
            float relativeWhite = effectiveWhite / peak;
            if (unitLuminance > 0 && relativeWhite < unitLuminance / .75) {
                float inverseLuminance = relativeWhite / unitLuminance;
                float mapped = 1 - .0625*inverseLuminance / (1-.5*inverseLuminance);
                color = unitColor * (mapped / unitLuminance);
            } else {
                color = raw / effectiveWhite;
            }
        }
    }
    return float4(Encode(color.r),Encode(color.g),Encode(color.b),1);
}
