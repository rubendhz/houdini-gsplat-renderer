/***************************************************************************************/
/*  Filename: GSplatShaderManger.C                                                     */
/*  Description: Basic Shader Manager for GSplat Plugin                                */
/*                                                                                     */
/*  Copyright (C) 2024 Ruben Diaz                                                      */
/*                                                                                     */
/*  License: AGPL-3.0-or-later                                                         */
/*           https://github.com/rubendhz/houdini-gsplat-renderer/blob/develop/LICENSE  */
/***************************************************************************************/


#include "GSplatShaderManager.h"
#include "GSplatShaderSource.h"

#include "GSplatLogger.h"

#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sstream>


GsplatShaderManager::GsplatShaderManager() 
{
    myCustomMainShaderPreviousHash = 0;
}

GsplatShaderManager::~GsplatShaderManager() 
{
}

const std::string GsplatShaderManager::_readFileToString(const char* filePath) {
    std::ifstream file(filePath);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

RE_Shader* GsplatShaderManager::getShader(GSplatShaderType shaderType, RE_Render* r) 
{
    bool useDefaultShader = true;
    bool hasShaderSrcOverrideChanged = false;
    
    std::string vertexShaderOverrideSource;
    std::string fragmentShaderOverrideSource;

    if (shaderType == GSPLAT_MAIN_SHADER)
    {
        const char* overrideMainShaderEnvVar = std::getenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_SET");
        
        if (overrideMainShaderEnvVar)
        {
            useDefaultShader = false;

            const char* overrideMainShaderVertexSrcFilePath = std::getenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_VERTEX_SRC_PATH");
            const char* overrideMainShaderFragmentSrcFilePath = std::getenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_FRAGMENT_SRC_PATH");

            if (overrideMainShaderVertexSrcFilePath && overrideMainShaderFragmentSrcFilePath)
            {
                //TODO: handle potential file reading errors
                vertexShaderOverrideSource = _readFileToString(overrideMainShaderVertexSrcFilePath);
                fragmentShaderOverrideSource = _readFileToString(overrideMainShaderFragmentSrcFilePath);

                // GSplatLogger::getInstance().log(
                //     GSplatLogger::LogLevel::_INFO_,
                //     "Vertex: %s",
                //     vertexShaderOverrideSource.c_str()
                // );
                // GSplatLogger::getInstance().log(
                //     GSplatLogger::LogLevel::_INFO_,
                //     "Fragment: %s",
                //     fragmentShaderOverrideSource.c_str()
                // );
                
                std::hash<std::string> hasher;
                size_t myCustomVertexShaderCurrentHash = hasher(vertexShaderOverrideSource.c_str());
                size_t myCustomFragmentShaderCurrentHash = hasher(fragmentShaderOverrideSource.c_str());
                size_t myCustomShaderCurrentHash = myCustomVertexShaderCurrentHash ^ (myCustomFragmentShaderCurrentHash + 0x9e3779b9 + (myCustomVertexShaderCurrentHash << 6) + (myCustomVertexShaderCurrentHash >> 2));

                if (myCustomShaderCurrentHash != myCustomMainShaderPreviousHash)
                {
                    myCustomMainShaderPreviousHash = myCustomShaderCurrentHash;
                    hasShaderSrcOverrideChanged = true;
                }

                
    #if !defined(WIN32)
                //setenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_SET", "2", 1); // set to 2, to indicate no need to rehash
                unsetenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_VERTEX_SRC_PATH");
                unsetenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_FRAGMENT_SRC_PATH");
    #else
                //_putenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_SET=2");
                _putenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_VERTEX_SRC_PATH=");
                _putenv("GSPLAT_DEBUG_OPENGL_MAIN_SHADER_OVERRIDE_FRAGMENT_SRC_PATH=");
    #endif        
            }
        }
        else
        {
            if (myCustomMainShaderPreviousHash != 0)
            {
                hasShaderSrcOverrideChanged = true;
            }
            myCustomMainShaderPreviousHash = 0;
        }
    }

    std::unordered_map<GSplatShaderType, RE_Shader*>::iterator it = myShaderMap.find(shaderType);
    if (it != myShaderMap.end()) 
    {
        if (!hasShaderSrcOverrideChanged)
        {
            return it->second;
        }
        else
        {
            myShaderMap.erase(shaderType);
        }
    }
    
    std::string shaderName = getNameForShaderType(shaderType);
    RE_Shader* shader = RE_Shader::create(shaderName.c_str());
    if (shader) 
    {
        bool shaderLinked = false;

        UT_String shader_error_msg;

        if (useDefaultShader)
        {         
            char* vertexShaderSource = nullptr;
            char* fragmentShaderSource = nullptr;
            getSourceForShaderType(shaderType, &vertexShaderSource, &fragmentShaderSource);
            shaderLinked = addAndLinkShader(shader, r, vertexShaderSource, fragmentShaderSource, shader_error_msg);
        }
        else
        {
            shaderLinked = addAndLinkShader(shader, r, vertexShaderOverrideSource.c_str(), fragmentShaderOverrideSource.c_str(), shader_error_msg);
        }

        if (shaderLinked) 
        {
            myShaderMap[shaderType] = shader;
            if (!useDefaultShader)
            {
                GSplatLogger::getInstance().log(
                    GSplatLogger::LogLevel::_INFO_,
                    "Custom shader linked: %s",
                    shaderName.c_str()
                );
            }
            else
            {
                GSplatLogger::getInstance().log(
                    GSplatLogger::LogLevel::_INFO_,
                    "Shader linked: %s",
                    shaderName.c_str()
                );
            }
        } 
        else 
        {
            GSplatLogger::getInstance().log(
                GSplatLogger::LogLevel::_ERROR_,
                "Failed to set up %s shader: %s",
                shaderName.c_str(),
                shader_error_msg.buffer()
            );
            delete shader;
            shader = nullptr;
        }
    } 
    else 
    {
        GSplatLogger::getInstance().log(
            GSplatLogger::LogLevel::_ERROR_,
            "Failed to create %s shader",
            shaderName.c_str()
        );
    }
    return shader;

}

bool GsplatShaderManager::addAndLinkShader(RE_Shader* shader, RE_Render* r, const char* vertexShaderSource, const char* fragmentShaderSource, UT_String& msg)
{
    shader->addShader(r, RE_SHADER_VERTEX, vertexShaderSource, "VertexShader", 0, &msg);
    shader->addShader(r, RE_SHADER_FRAGMENT, fragmentShaderSource, "FragmentShader", 0, &msg);

    bool linkSuccess = shader->linkShaders(r, &msg);
    bool validateSuccess = shader->validateShader(r, &msg);
    return linkSuccess && validateSuccess;
}

bool GsplatShaderManager::getSourceForShaderType(const GSplatShaderType shaderType, char **vertexShaderSource, char **fragmentShaderSource)
{
    switch (shaderType) 
    {
        case GSPLAT_WIRE_SHADER:
            *vertexShaderSource = (char*)GSplatWireVertexShader.c_str();
            *fragmentShaderSource = (char*)GSplatWireFragmentShader.c_str();
            break;
        case GSPLAT_MAIN_SHADER:
            *vertexShaderSource = (char*)GSplatMainVertexShader.c_str();
            *fragmentShaderSource = (char*)GSplatMainFragmentShader.c_str();
            break;
        default:
            return false;
    }
    return true;
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
