
#pragma once

#include <ionScene.h>


namespace ion
{
	namespace Scene
	{

		class CAssimpSceneObject : public ISceneObject
		{

		public:

			~CAssimpSceneObject();

			virtual void Load(CRenderPass * RenderPass);
			virtual void Draw(CRenderPass * RenderPass);

			virtual void ReadFromFile(string const & FileName);


			struct SVertex
			{
				vec3f Position;
				vec3f Normal;
				vec2f TextureCoordinates;

				color4f Color;

				vec2i BoneIndices = vec2i(-1);
				vec2f BoneWeights = 0;
			};

			struct STriangle
			{
				uint Indices[3];
				vec3f Normal;
			};

			struct SMeshBone
			{
				glm::mat4 OffsetMatrix, RelativeTransform;
				string Name;
				SMeshBone * Parent = nullptr;
			};

			struct SMaterial
			{
				vector<SharedPtr<Graphics::ITexture2D>> Textures;

				SharedPtr<Graphics::CUniformValue<color3f>> Ambient = std::make_shared<Graphics::CUniformValue<color3f>>(0.05f);
				SharedPtr<Graphics::CUniformValue<color3f>> Diffuse = std::make_shared<Graphics::CUniformValue<color3f>>(0.9f);
				SharedPtr<Graphics::CUniformValue<color3f>> Specular = std::make_shared<Graphics::CUniformValue<color3f>>(1.f);
				SharedPtr<Graphics::CUniformValue<float>> Shininess = std::make_shared<Graphics::CUniformValue<float>>(1000.f);
			};

			struct SMeshBuffer
			{
				vector<SVertex> Vertices;
				vector<STriangle> Triangles;
				vector<SMeshBone> Bones;

				SMaterial * Material = nullptr;

				SharedPtr<Graphics::IVertexBuffer> VertexBuffer;
				SharedPtr<Graphics::IIndexBuffer> IndexBuffer;
			};

			struct SMeshNode : public ITreeNode<SMeshNode>
			{
				vector<SMeshBuffer *> Buffers;
				glm::mat4 Transformation;

				vector<SharedPtr<Graphics::IPipelineState>> PipelineStates;
				SharedPtr<Graphics::CUniformValue<glm::mat4>> AbsoluteTransformation = std::make_shared<Graphics::CUniformValue<glm::mat4>>();

				void CreatePSOs(CRenderPass * RenderPass, CAssimpSceneObject * SceneObject);
				void DrawPSOs(CRenderPass * RenderPass, CAssimpSceneObject * SceneObject);
				void CalculateAbsoluteTransformation(glm::mat4 const & ParentTransformation = glm::mat4(1.f));
			};

			struct SScene
			{
				vector<SMaterial *> Materials;
				vector<SMeshBuffer *> Buffers;
				SMeshNode * Root;
			};

			SharedPtr<Graphics::IShaderProgram> Shader;

		protected:

			SScene * Scene = nullptr;

		};

	}
}
