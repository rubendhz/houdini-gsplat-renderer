#include "GSplatShaderManager.h"
#include "GSplatShaderSource.h"

#include <iostream>


GsplatShaderManager::GsplatShaderManager() 
{
}

GsplatShaderManager::~GsplatShaderManager() 
{
}

// Shader Getter Implementation
RE_Shader* GsplatShaderManager::getShader(GSplatShaderType shaderType, RE_Render* r) 
{
    std::unordered_map<GSplatShaderType, RE_Shader*>::iterator it = myShaderMap.find(shaderType);
    if (it != myShaderMap.end()) 
    {
        return it->second;
    }
    
    std::string shaderName = getNameForShaderType(shaderType);
    RE_Shader* shader = RE_Shader::create(shaderName.c_str());
    if (shader) 
    {
        if (setupShader(shader, shaderType, r)) 
        {
            myShaderMap[shaderType] = shader;
        } 
        else 
        {
            std::cerr << "Failed to set up shader: " << shaderName << std::endl;
            delete shader;
            shader = nullptr;
        }
    } 
    else 
    {
        std::cerr << "Failed to create shader: " << shaderName << std::endl;
    }
    return shader;
}

bool GsplatShaderManager::setupShader(RE_Shader* shader, const GSplatShaderType shaderType, RE_Render* r) 
{
    const char* vertexShaderSource = nullptr;
    const char* fragmentShaderSource = nullptr;

    switch (shaderType) 
    {
        case GSPLAT_MAIN_SHADER:
            vertexShaderSource = GSplatMainVertexShader.c_str();
            fragmentShaderSource = GSplatMainFragmentShader.c_str();
            break;
        case GSPLAT_WIRE_SHADER: 
        default:
            vertexShaderSource = GSplatWireVertexShader.c_str();
            fragmentShaderSource = GSplatWireFragmentShader.c_str();
            break;
    }

    shader->addShader(r, RE_SHADER_VERTEX, vertexShaderSource, "VertexShader", 0);
    shader->addShader(r, RE_SHADER_FRAGMENT, fragmentShaderSource, "FragmentShader", 0);
    return shader->linkShaders(r);
}

void GsplatShaderManager::unloadAllShaders() {
    for (auto& pair : myShaderMap) 
    {
        delete pair.second;
    }
    myShaderMap.clear();
}

std::string GsplatShaderManager::getNameForShaderType(GSplatShaderType shaderType) {
    switch (shaderType) 
    {
        case GSPLAT_WIRE_SHADER: return "GsplatWireShader";
        case GSPLAT_MAIN_SHADER: return "GsplatMainShader";
        default: return "UnknownShader";
    }
}
