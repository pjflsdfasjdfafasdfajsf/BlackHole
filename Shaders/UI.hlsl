cbuffer Uniforms : register(b0, space1)
{
    float4x4 ProjectionView;
    float4x4 Model;
    /// valid only when Mode == 1 (UIPanel).
    /// width and height of the panel in pixels
    float2 Size;
    /// valid only when Mode == 1 (UIPanel).
    /// corner radius in pixels
    float Radius;
    /// 0 = text
    /// 1 = panel
    float Mode;
    /// 
    float CornerFlags;
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
    float2 Size : TEXCOORD2;
    float Radius : TEXCOORD3;
    float Mode : TEXCOORD4;
    float CornerFlags : TEXCOORD5;
};

VsOutput VsMain(VsInput Input)
{
    VsOutput Output;
    
    Output.Position = mul(float4(Input.Position, 1.0f), mul(Model, ProjectionView));
    Output.Color = Input.Color;
    Output.UV = Input.UV;
    Output.Size = Size;
    Output.Radius = Radius;
    Output.Mode = Mode;
    Output.CornerFlags = CornerFlags;

    return Output;
}

float4 PsMain(VsOutput Input) : SV_Target0
{
    float4 TextureColor = AtlasTexture.Sample(AtlasSampler, Input.UV);
    float4 Result = Input.Color * TextureColor;

    if (Input.Mode > 0.5)
    {
        bool IsRight = Input.UV.x > 0.5;
        bool IsBottom = Input.UV.y > 0.5;

        int Flag = IsBottom
            ? (IsRight ? 4 : 8)
            : (IsRight ? 2 : 1);

        int Flags = (int)Input.CornerFlags;
        float R = (Flags & Flag) ? Input.Radius : 0.0;

        float2 Center = (Input.UV - 0.5) * Input.Size;
        float2 Q = abs(Center) - Input.Size * 0.5 + R;
        float Distance = length(max(Q, 0.0)) + min(max(Q.x, Q.y), 0.0) - R;
        float Alpha = 1.0 - smoothstep(-1.0, 0.0, Distance);
        Result.a *= Alpha;
    }

    return Result;
}