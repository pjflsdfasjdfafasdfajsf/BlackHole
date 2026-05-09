cbuffer Uniforms : register(b0, space1)
{
    float4x4 ProjectionView;
    float4x4 Model;
};

Texture2D AtlasTexture : register(t0, space2);
SamplerState AtlasSampler : register(s0, space2);

struct VsInput {
    float3 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float2 UV : TEXCOORD2;
};

struct VsOutput {
    float4 Position : SV_Position;
    float4 Color : TEXCOORD0;
    float2 UV : TEXCOORD1;
};

VsOutput VsMain(VsInput Input)
{
    VsOutput Output;
    
    Output.Position = mul(float4(Input.Position, 1.0f), mul(Model, ProjectionView));
    Output.Color = Input.Color;
    Output.UV = Input.UV;

    return Output;
}

float4 PsMain(VsOutput Input) : SV_Target0 {
    float4 TextureColor = AtlasTexture.Sample(AtlasSampler, Input.UV);

    return Input.Color * TextureColor;
}