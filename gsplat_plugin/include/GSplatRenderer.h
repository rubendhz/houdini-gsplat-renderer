#ifndef __GSPLAT_RENDERER__
#define __GSPLAT_RENDERER__


#include <SYS/SYS_Types.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_Map.h>
#include <GA/GA_Types.h>
#include <RE/RE_Geometry.h>
#include <GT/GT_GEOPrimitive.h>
#include <RE/RE_RenderContext.h>
#include "UT_GSplatVectorTypes.h"


class GSplatRenderer {
private:
    struct GSplatRegisterEntry {
        GU_Detail *gdp;
        RE_CacheVersion gversion;
        GA_Offset gvtx;
        GA_Size splatCount = 0;
        bool active = false;
        int age = -1;
        int ageSinceLastActive = -1;
        UT_Vector3HArray* splatPts = NULL;
        UT_Vector3HArray* splatColors = NULL;
        UT_FloatArray* splatAlphas = NULL;
        UT_Vector3HArray* splatScales = NULL;
        UT_Vector4HArray* splatOrients = NULL;
        MyUT_Matrix4HArray* splatShxs = NULL;
        MyUT_Matrix4HArray* splatShys = NULL;
        MyUT_Matrix4HArray* splatShzs = NULL;
    };

    // Private constructor for singleton
    GSplatRenderer();

    // Delete copy constructor and assignment operator to prevent copies
    GSplatRenderer(const GSplatRenderer&) = delete;
    GSplatRenderer& operator=(const GSplatRenderer&) = delete;

    UT_Map<std::string, std::unique_ptr<GSplatRegisterEntry>> myRenderStateRegistry;
    UT_Set<std::string> myActiveRegistries;

    RE_Geometry *myTriangleGeo;
    RE_Texture *myTexSortedIndexNormalised;
    int myGSplatSortedIndexTexDim;
    RE_Texture *myTexGsplatSh;
    int myGSplatShTexDim;
    RE_Texture *myTexGsplatColorAlphaScaleOrient;
    int myGSplatColorAlphaScaleOrientTexDim;
    std::vector<UT_Vector3F> mySplatPoints;

    int mySplatCount;
    int myAllocatedSplatCount;

    bool myIsRenderEnabled;
    bool myIsShDataPresent;
    bool myCanRender;

    // Variables to hold camera sorting state
    UT_Vector3F myPreviousCameraPos; 
    float mySortDistanceAccum = 0.0;
    bool myIsFreshGeometry;
    std::vector<float> myGsplatZDistances;
    std::vector<int> myGsplatZIndices;
    std::vector<float> myGsplatZIndices_f;
    
    unsigned int closestSqrtPowerOf2(const int n);

    bool isRenderStateRegistryCurrent();
    bool checkSignificantDelta(const UT_Vector3F& newPos, const UT_Vector3F& oldPos, const float threshold = 0.0f);
    bool argsortByDistance2(const UT_Vector3F *posSplatPointsData, const UT_Vector3F &ref_pos, const int pointCount);

    void freeTextureResources();
    void initialiseTextureResources();
    void allocateTextureResources(RE_RenderContext r);

public:
    // Public method to access the singleton instance
    static GSplatRenderer& getInstance() {
        static GSplatRenderer instance;
        return instance;
    }

    std::string registerUpdate(
        const GU_Detail *gdp,
        const RE_CacheVersion &gversion, 
        const GA_Offset &gVtxOffset,
        const GA_Size &splatCount, 
        const UT_Vector3HArray& splatPts, 
        const UT_Vector3HArray& splatColors,
        const UT_FloatArray& splatAlphas,
        const UT_Vector3HArray& splatScales,
        const UT_Vector4HArray& splatOrients,
        const MyUT_Matrix4HArray& splatShxs,
        const MyUT_Matrix4HArray& splatShys,
        const MyUT_Matrix4HArray& splatShzs); 
    
    void includeInRenderPass(std::string  gSplatId);
    
    void flushEntriesForMatchingDetail(std::string myRegistryId);

    void generateRenderGeometry(RE_RenderContext r);

    void render(RE_RenderContext r);

    void postRender();

    void setRenderingEnabled(bool isRenderEnabled);
};


#endif // __GSPLAT_RENDERER__
