
#include "CAssimpSceneObject.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>



namespace ion
{
	namespace Scene
	{
		CAssimpSceneObject::SMaterial * ReadMaterial(aiMaterial * Material);
		CAssimpSceneObject::SMeshBuffer * ReadBuffer(aiNode * RootNode, aiMesh * Mesh, vector<CAssimpSceneObject::SMaterial *> const & Materials);
		CAssimpSceneObject::SMeshNode * TraverseMesh(aiScene const * Scene, aiNode * Node, vector<CAssimpSceneObject::SMeshBuffer *> const & Buffers);

		void CAssimpSceneObject::SMeshNode::CalculateAbsoluteTransformation(glm::mat4 const & ParentTransformation)
		{
			*AbsoluteTransformation = ParentTransformation * Transformation;
			RecurseOnChildren<void, glm::mat4 const &>(& CAssimpSceneObject::SMeshNode::CalculateAbsoluteTransformation, *AbsoluteTransformation);
		}

		void CAssimpSceneObject::SMeshNode::CreatePSOs(CRenderPass * RenderPass, CAssimpSceneObject * SceneObject)
		{
			for (auto & Buffer : Buffers)
			{
				SharedPointer<Graphics::IPipelineState> PipelineState = RenderPass->GetGraphicsContext()->CreatePipelineState();
				PipelineState->SetProgram(SceneObject->Shader);

				PipelineState->SetIndexBuffer(Buffer->IndexBuffer);
				PipelineState->SetVertexBuffer(0, Buffer->VertexBuffer);

				PipelineState->OfferUniform("uLocalMatrix", AbsoluteTransformation);

				if (Buffer->Material)
				{
					PipelineState->OfferUniform("uMaterial.AmbientColor", Buffer->Material->Ambient);
					PipelineState->OfferUniform("uMaterial.DiffuseColor", Buffer->Material->Diffuse);
					PipelineState->OfferUniform("uMaterial.SpecularColor", Buffer->Material->Specular);
					PipelineState->OfferUniform("uMaterial.Shininess", Buffer->Material->Shininess);

					for (uint i = 0; i < Buffer->Material->Textures.size(); ++ i)
					{
						PipelineState->SetTexture(String::Build("uMaterial.DiffuseTexture%d", i), Buffer->Material->Textures[i]);
					}
				}

				RenderPass->PreparePipelineStateForRendering(PipelineState, SceneObject);
				PipelineStates.push_back(PipelineState);
			}

			RecurseOnChildren<void, CRenderPass *, CAssimpSceneObject *>(& CAssimpSceneObject::SMeshNode::CreatePSOs, RenderPass, SceneObject);
		}

		void CAssimpSceneObject::SMeshNode::DrawPSOs(CRenderPass * RenderPass, CAssimpSceneObject * SceneObject)
		{
			for (auto & PipelineState : PipelineStates)
			{
				RenderPass->SubmitPipelineStateForRendering(PipelineState, SceneObject);
			}

			RecurseOnChildren<void, CRenderPass *, CAssimpSceneObject *>(& CAssimpSceneObject::SMeshNode::DrawPSOs, RenderPass, SceneObject);
		}

		CAssimpSceneObject::~CAssimpSceneObject()
		{
		}

		void CAssimpSceneObject::Load(CRenderPass * RenderPass)
		{
			for (auto & Material : Scene->Materials)
			{
				Material->Textures.clear();

				for (auto & Image : Material->Images)
				{
					Graphics::ITexture::EFormatComponents Format = Graphics::ITexture::EFormatComponents::R;
					switch (Image->GetChannels())
					{
					case 2:
						Format = Graphics::ITexture::EFormatComponents::RG;
						break;
					case 3:
						Format = Graphics::ITexture::EFormatComponents::RGB;
						break;
					case 4:
						Format = Graphics::ITexture::EFormatComponents::RGBA;
						break;
					}
					SharedPointer<Graphics::ITexture2D> Texture = RenderPass->GetGraphicsAPI()->CreateTexture2D(
						Image->GetSize(),
						Graphics::ITexture::EMipMaps::True,
						Format,
						Graphics::ITexture::EInternalFormatType::Fix8);
					Texture->Upload(
						Image->GetData(),
						Image->GetSize(),
						Format,
						Graphics::EScalarType::UnsignedInt8);
					Material->Textures.push_back(Texture);
				}
			}

			for (auto & Buffer : Scene->Buffers)
			{
				if (! Buffer->IndexBuffer)
				{
					vector<u32> IndexData;
					IndexData.reserve(Buffer->Triangles.size() * 3);

					std::for_each(Buffer->Triangles.begin(), Buffer->Triangles.end(), [&IndexData](STriangle const & Triangle)
					{
						for (uint i = 0; i < 3; ++ i)
						{
							IndexData.push_back(Triangle.Indices[i]);
						}
					});

					Buffer->IndexBuffer = RenderPass->GetGraphicsAPI()->CreateIndexBuffer();
					Buffer->IndexBuffer->UploadData(IndexData);
				}

				if (! Buffer->VertexBuffer)
				{
					vector<float> VertexData;
					VertexData.reserve(Buffer->Vertices.size() * 12);

					std::for_each(Buffer->Vertices.begin(), Buffer->Vertices.end(), [&VertexData](SVertex const & Vertex)
					{
						for (uint i = 0; i < 3; ++ i)
						{
							VertexData.push_back(Vertex.Position[i]);
						}
						for (uint i = 0; i < 3; ++ i)
						{
							VertexData.push_back(Vertex.Normal[i]);
						}
						for (uint i = 0; i < 2; ++ i)
						{
							VertexData.push_back(Vertex.TextureCoordinates[i]);
						}
					});

					Buffer->VertexBuffer = RenderPass->GetGraphicsAPI()->CreateVertexBuffer();
					Buffer->VertexBuffer->UploadData(VertexData);
					Graphics::SInputLayoutElement InputLayout[] =
					{
						{ "vPosition",  3, Graphics::EAttributeType::Float },
						{ "vNormal",    3, Graphics::EAttributeType::Float },
						{ "vTexCoords", 2, Graphics::EAttributeType::Float },
					};
					Buffer->VertexBuffer->SetInputLayout(InputLayout, ION_ARRAYSIZE(InputLayout));
				}
			}

			if (! Scene->Root->PipelineStates.size())
			{
				Scene->Root->CreatePSOs(RenderPass, this);
			}

			Loaded = true;
		}

		void CAssimpSceneObject::Draw(CRenderPass * RenderPass)
		{
			Scene->Root->DrawPSOs(RenderPass, this);
		}

		void CAssimpSceneObject::ReadFromFile(string const & FileName)
		{
			Assimp::Importer Importer;

			unsigned int pFlags =
				aiProcess_CalcTangentSpace |
				aiProcess_GenSmoothNormals |
				aiProcess_Triangulate |
				aiProcess_JoinIdenticalVertices |
				aiProcess_SortByPType |
				aiProcess_GenNormals;

			aiScene const * const ImportedScene = Importer.ReadFile(FileName, pFlags);

			if (! ImportedScene)
			{
				std::cerr << "Failed to import mesh file '" << FileName << "': " << Importer.GetErrorString() << std::endl;
			}
			else
			{
				if (Scene)
				{
					delete Scene;
				}
				Scene = new SScene();

				for (uint i = 0; i < ImportedScene->mNumMaterials; ++ i)
				{
					Scene->Materials.push_back(ReadMaterial(ImportedScene->mMaterials[i]));
				}
				for (uint i = 0; i < ImportedScene->mNumMeshes; ++ i)
				{
					Scene->Buffers.push_back(ReadBuffer(ImportedScene->mRootNode, ImportedScene->mMeshes[i], Scene->Materials));
				}

				Scene->Root = TraverseMesh(ImportedScene, ImportedScene->mRootNode, Scene->Buffers);
				Scene->Root->CalculateAbsoluteTransformation();
			}
		}

		void CAssimpSceneObject::SetRenderCategory(int const Category)
		{
		}

		CAssimpSceneObject::SMaterial * ReadMaterial(aiMaterial * Material)
		{
			CAssimpSceneObject::SMaterial * Result = new CAssimpSceneObject::SMaterial();
			aiColor4D Color;

			Material->Get(AI_MATKEY_COLOR_DIFFUSE, Color);
			*Result->Diffuse = color4f(Color.r, Color.g, Color.b, Color.a);
			Material->Get(AI_MATKEY_COLOR_AMBIENT, Color);
			*Result->Ambient = color4f(Color.r, Color.g, Color.b, Color.a);
			Material->Get(AI_MATKEY_COLOR_SPECULAR, Color);
			*Result->Specular = color4f(Color.r, Color.g, Color.b, Color.a);

			if (Material->GetTextureCount(aiTextureType_DIFFUSE))
			{
				aiString Path;
				Material->GetTexture(aiTextureType_DIFFUSE, 0, &Path);

				CImage * Image = CImage::Load(string("Assets/Meshes/") + Path.C_Str());
				if (Image)
				{
					Result->Images.push_back(Image);
					Log::Info("Texture loaded for assimp material: %s", Path.C_Str());
				}
				else
				{
					Log::Error("Texture failure: %s", Path.C_Str());
				}
			}

			return Result;
		}

		glm::mat4 AItoGLM(aiMatrix4x4 const & ai)
		{
			return glm::mat4(
				ai.a1, ai.b1, ai.c1, ai.d1,
				ai.a2, ai.b2, ai.c2, ai.d2,
				ai.a3, ai.b3, ai.c3, ai.d3,
				ai.a4, ai.b4, ai.c4, ai.d4
				);
		}

		void FindParentName(string const & ChildName, aiNode * Node, string & OutString, glm::mat4 & OutMatrix)
		{
			for (uint i = 0; i < Node->mNumChildren; ++ i)
			{
				if (ChildName == Node->mChildren[i]->mName.C_Str())
				{
					OutString = Node->mName.C_Str();
					OutMatrix = AItoGLM(Node->mChildren[i]->mTransformation);
				}
				else
				{
					FindParentName(ChildName, Node->mChildren[i], OutString, OutMatrix);
					if (OutString.size())
						return;
				}
			}
		}

		CAssimpSceneObject::SMeshBuffer * ReadBuffer(aiNode * RootNode, aiMesh * Mesh, vector<CAssimpSceneObject::SMaterial *> const & Materials)
		{
			CAssimpSceneObject::SMeshBuffer * Buffer = new CAssimpSceneObject::SMeshBuffer();

			// Vertices
			Buffer->Vertices.reserve(Mesh->mNumVertices);
			for (uint j = 0; j < Mesh->mNumVertices; ++ j)
			{
				CAssimpSceneObject::SVertex Vertex;
				Vertex.Position = vec3f(Mesh->mVertices[j].x, Mesh->mVertices[j].y, Mesh->mVertices[j].z);
				if (Mesh->HasNormals())
					Vertex.Normal = vec3f(Mesh->mNormals[j].x, Mesh->mNormals[j].y, Mesh->mNormals[j].z);
				if (Mesh->HasTextureCoords(0))
					Vertex.TextureCoordinates = vec2f(Mesh->mTextureCoords[0][j].x, Mesh->mTextureCoords[0][j].y);
				if (Mesh->HasVertexColors(0))
					Vertex.Color = color4f(Mesh->mColors[0][j].r, Mesh->mColors[0][j].g, Mesh->mColors[0][j].b, Mesh->mColors[0][j].a);

				Buffer->Vertices.push_back(Vertex);
			}

			// Faces
			Buffer->Triangles.reserve(Mesh->mNumFaces);
			for (uint j = 0; j < Mesh->mNumFaces; ++ j)
			{
				CAssimpSceneObject::STriangle Triangle;
				for (uint k = 0; k < 3; ++ k)
					Triangle.Indices[k] = Mesh->mFaces[j].mIndices[k];

				Buffer->Triangles.push_back(Triangle);
			}

			// Bones
			for (uint j = 0; j < Mesh->mNumBones; ++ j)
			{
				CAssimpSceneObject::SMeshBone Bone;
				Bone.Name = Mesh->mBones[j]->mName.C_Str();
				Bone.OffsetMatrix = AItoGLM(Mesh->mBones[j]->mOffsetMatrix);

				for (uint k = 0; k < Mesh->mBones[j]->mNumWeights; ++ k)
				{
					if (Buffer->Vertices[Mesh->mBones[j]->mWeights[k].mVertexId].BoneIndices[0] == -1)
					{
						Buffer->Vertices[Mesh->mBones[j]->mWeights[k].mVertexId].BoneIndices[0] = j;
						Buffer->Vertices[Mesh->mBones[j]->mWeights[k].mVertexId].BoneWeights[0] = Mesh->mBones[j]->mWeights[k].mWeight;
					}
					else if (Buffer->Vertices[Mesh->mBones[j]->mWeights[k].mVertexId].BoneIndices[1] == -1)
					{
						Buffer->Vertices[Mesh->mBones[j]->mWeights[k].mVertexId].BoneIndices[1] = j;
						Buffer->Vertices[Mesh->mBones[j]->mWeights[k].mVertexId].BoneWeights[1] = Mesh->mBones[j]->mWeights[k].mWeight;
					}
					else
					{
						printf("Error! Vertex in mesh has more than two bone weights!\n");
					}
				}

				Buffer->Bones.push_back(Bone);
			}

			for (auto & Bone : Buffer->Bones)
			{
				string ParentName;
				glm::mat4 MyTransform;
				FindParentName(Bone.Name, RootNode, ParentName, MyTransform);

				for (auto & Parent : Buffer->Bones)
				{
					if (ParentName == Parent.Name)
						Bone.Parent = & Parent;
				}
				Bone.RelativeTransform = MyTransform;
			}

			// Material
			Buffer->Material = Materials[Mesh->mMaterialIndex];

			return Buffer;
		}

		CAssimpSceneObject::SMeshNode * TraverseMesh(aiScene const * Scene, aiNode * Node, vector<CAssimpSceneObject::SMeshBuffer *> const & Buffers)
		{
			CAssimpSceneObject::SMeshNode * Result = new CAssimpSceneObject::SMeshNode();

			Result->Transformation = AItoGLM(Node->mTransformation);

			for (uint i = 0; i < Node->mNumMeshes; ++ i)
				Result->Buffers.push_back(Buffers[Node->mMeshes[i]]);

			for (uint i = 0; i < Node->mNumChildren; ++ i)
				Result->AddChild(TraverseMesh(Scene, Node->mChildren[i], Buffers));

			return Result;
		}

	}
}
