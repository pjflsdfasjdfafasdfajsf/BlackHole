struct VsOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
};

VsOutput VsMain(uint VertexID : SV_VertexID)
{
	float2 Positions[3] =
	{
		float2(0.0, 0.5),
		float2(-0.5, -0.5),
		float2(0.5, -0.5)
	};

	float4 Colors[3] =
	{
		float4(1.0, 0.0, 0.0, 1.0),
		float4(0.0, 1.0, 0.0, 1.0),
		float4(0.0, 0.0, 1.0, 1.0),
	};

	VsOutput Output;
	Output.Position = float4(Positions[VertexID], 0.0, 1.0);
	Output.Color = Colors[VertexID];

	return Output;
}

float4 PsMain(VsOutput Input) : SV_Target0
{
	return Input.Color;
}