#include "GBufferRS.hlsl"

cbuffer RootConsts : register(b0) {
    uint matId;
}

struct InputPS {
    float4 position : SV_Position;
    float3 normal   : Normal1;
    float2 uvCoord  : TexCoord1;
};

struct OutputPS {
    float2 normal   : SV_Target0;
    float2 uvCoord  : SV_Target1;
    float4 uvGrad   : SV_Target2;
    uint   matId    : SV_Target3;
};

[RootSignature(RootSig)]
OutputPS main(const InputPS input) {
    OutputPS output;
    // Interpolate and normalize the normal.
    output.normal  = input.normal.xy * rsqrt(dot(input.normal, input.normal));
    // Interpolate and wrap the UV coordinates.
    output.uvCoord = frac(input.uvCoord);
    // Compute per-pixel UV derivatives.
    output.uvGrad  = float4(ddx_fine(input.uvCoord), ddy_fine(input.uvCoord));
    output.matId   = matId;
    return output;
}