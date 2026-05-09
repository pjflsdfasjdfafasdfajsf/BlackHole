Texture2D SceneTexture : register(t0, space2);
SamplerState Sampler0  : register(s0, space2);

Texture2D BloomTexture : register(t1, space2);
SamplerState Sampler1  : register(s1, space2);

struct VsOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

VsOutput VsMain(uint VertexID : SV_VertexID)
{
    VsOutput Output;
    float X = (float)((VertexID << 1) & 2) * 2.0 - 1.0;
    float Y = (float)(VertexID & 2) * 2.0 - 1.0;
    
    Output.Position = float4(X, Y, 0.0, 1.0);
    Output.UV = float2(X * 0.5 + 0.5, 1.0 - (Y * 0.5 + 0.5));
    
    return Output;
}

float4 PsMain(VsOutput Input) : SV_Target0
{
    float3 SceneColor = SceneTexture.Sample(Sampler0, Input.UV).rgb;
    float3 BloomColor = BloomTexture.Sample(Sampler1, Input.UV).rgb;
    
    float3 Color = SceneColor + BloomColor * 0.8;
    
    Color = Color / (1.0 + Color); 
    Color = pow(Color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    
    return float4(Color, 1.0);
}