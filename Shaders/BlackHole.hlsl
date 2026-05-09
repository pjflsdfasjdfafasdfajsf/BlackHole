cbuffer Uniforms : register(b0, space1)
{
    float Time;
};

struct VsOutput
{
    float4 Position : SV_Position;
    float Time : TEXCOORD0;
};

VsOutput VsMain(uint VertexID : SV_VertexID)
{
    VsOutput Output;
    
    float X = (float)((VertexID << 1) & 2) * 2.0 - 1.0;
    float Y = (float)(VertexID & 2) * 2.0 - 1.0;
    
    Output.Position = float4(X, Y, 0.0, 1.0);
    Output.Time = Time;

    return Output;
}

float4 PsMain(VsOutput Input) : SV_Target0
{
    float2 Resolution = float2(1280.0, 720.0);
    float2 UV = (Input.Position.xy / Resolution) * 2.0 - 1.0;
    UV.x *= Resolution.x / Resolution.y;

    float3 RayOrigin = float3(0.0, -2.0, -8.0); 
    float3 LookAt = float3(0.0, 0.0, 0.0);
    
    float3 Forward = normalize(LookAt - RayOrigin);
    float3 Right = normalize(cross(float3(0.0, 1.0, 0.0), Forward));
    float3 Up = cross(Forward, Right);
    
    float3 RayDirection = normalize(UV.x * Right + UV.y * Up + 1.0 * Forward);
    float EventHorizon = 1.0;
    
    float3 Position = RayOrigin;
    float3 Velocity = RayDirection;
    
    float3 DiskColor = float3(0.0, 0.0, 0.0);
    float DiskAlpha = 0.0;
    
    for (int I = 0; I < 300; I++)
    {
        float3 PreviousPosition = Position;
        float Radius = length(Position);
        
        if (Radius < EventHorizon) 
        {
            break;
        }
        
        if (Radius > 20.0) 
        {
            break;
        }
        
        float3 Gravity = -normalize(Position) * (1.5 / (Radius * Radius * Radius));
        float StepSize = max(0.015, (Radius - EventHorizon) * 0.1);
        
        Velocity = normalize(Velocity + Gravity * StepSize);
        Position += Velocity * StepSize;
        
        if (PreviousPosition.y * Position.y < 0.0)
        {
            float Fraction = abs(PreviousPosition.y) / (abs(PreviousPosition.y) + abs(Position.y));
            float3 HitPosition = lerp(PreviousPosition, Position, Fraction);
            float HitRadius = length(HitPosition.xz);

            if (HitRadius > 1.6 && HitRadius < 6.0)
            {
                float NormalizedRadius = (HitRadius - 1.6) / 4.4;
                float3 BaseColor = lerp(float3(2.5, 1.8, 1.2), float3(1.0, 0.2, 0.01), NormalizedRadius);
                
                float Angle = atan2(HitPosition.z, HitPosition.x);
                
                float OrbitalSpeed = 2.5 / pow(HitRadius, 1.5); 
                float Spin = Angle - Input.Time * OrbitalSpeed;
                
                float Tracks = sin(HitRadius * 35.0) * 0.5 + 0.5;
                Tracks *= sin(HitRadius * 12.0) * 0.5 + 0.5;
                
                float Clumps = sin(Spin * 7.0 + HitRadius * 50.0) * 0.5 + 0.5;
                Clumps *= sin(Spin * 14.0 - HitRadius * 30.0) * 0.5 + 0.5;
                
                float Density = smoothstep(1.6, 2.0, HitRadius) * smoothstep(6.0, 4.5, HitRadius);
                
                Density *= (0.25 + 0.75 * Tracks * (0.3 + 0.7 * Clumps));
                
                float3 DiskVelocity = normalize(float3(-HitPosition.z, 0.0, HitPosition.x));
                float Doppler = 1.0 + dot(Velocity, DiskVelocity) * 0.7;
                
                BaseColor *= Doppler * Doppler; 
                
                float Alpha = Density * 0.95;
                DiskColor += BaseColor * Density * 3.0 * (1.0 - DiskAlpha);
                DiskAlpha += Alpha * (1.0 - DiskAlpha);
            }
        }
    }
    
    return float4(DiskColor, 1.0);
}