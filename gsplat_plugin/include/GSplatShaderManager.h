#ifndef __GSPLAT_SHADERMANAGER__
#define __GSPLAT_SHADERMANAGER__


#include <unordered_map>
#include <string>
#include <UT/UT_Singleton.h>
#include <UT/UT_Lock.h>
#include <RE/RE_Render.h>
#include <RE/RE_Shader.h>
#include <RE/RE_Types.h>


class GsplatShaderManager;
// Typedef for the singleton template instantiation
typedef UT_SingletonWithLock<GsplatShaderManager, true, UT_Lock> GsplatShaderManagerSingleton;

class GsplatShaderManager {

public:
    enum GSplatShaderType {
        GSPLAT_WIRE_SHADER,
        GSPLAT_MAIN_SHADER,
    };
    
    GsplatShaderManager();
    ~GsplatShaderManager();
    
    // Singleton instance access
    GsplatShaderManager(const GsplatShaderManager&) = delete;
    GsplatShaderManager& operator=(const GsplatShaderManager&) = delete;
    static GsplatShaderManager& getInstance()
    {
        static GsplatShaderManagerSingleton singleton;
        return singleton.get();
    }

    RE_Shader* getShader(GSplatShaderType shaderType, RE_Render* r);
    void unloadAllShaders();

private:
    std::unordered_map<GSplatShaderType, RE_Shader*> myShaderMap;

    bool setupShader(RE_Shader* shader, const GSplatShaderType shaderType, RE_Render* r);
    std::string getNameForShaderType(GSplatShaderType shaderType);
};


#endif // __GSPLAT_SHADERMANAGER__
