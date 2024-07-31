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
        RE_CacheVersion cacheVersion;
        GA_Size splatCount = 0;
        bool active = false;
        int age = -1;
        UT_Vector3HArray* splatPts = NULL;
        UT_Vector3HArray* splatColors = NULL;
        UT_FloatArray* splatAlphas = NULL;
        UT_Vector3HArray* splatScales = NULL;
        UT_Vector4HArray* splatOrients = NULL;
        MyUT_Matrix4HArray* splatShxs = NULL;
        MyUT_Matrix4HArray* splatShys = NULL;
        MyUT_Matrix4HArray* splatShzs = NULL;
    };

    UT_Map<GT_PrimitiveHandle, std::unique_ptr<GSplatRegisterEntry>> myRenderStateRegistry;

    UT_Map<GT_PrimitiveHandle, RE_CacheVersion> myCurrentCacheVersions;

    // Private constructor for singleton
    GSplatRenderer();
    
    // Delete copy constructor and assignment operator to prevent copies
    GSplatRenderer(const GSplatRenderer&) = delete;
    GSplatRenderer& operator=(const GSplatRenderer&) = delete;

    RE_Geometry *myTriangleGeo;
    RE_Texture *myTexSortedIndexNormalised;
    int myGSplatSortedIndexTexDim;
    RE_Texture *myTexGsplatSh;
    int myGSplatShTexDim;
    RE_Texture *myTexGsplatColorAlphaScaleOrient;
    int myGSplatColorAlphaScaleOrientTexDim;

    std::vector<UT_Vector3F> mySplatPoints;

    // Global or static variables to maintain state across calls
    //State for sorting
    std::vector<float> distances;
    UT_Vector3F myPreviousCameraPos; 
    float sort_distance_accum = 0.0;
    bool firstRun;
    std::vector<float> normalizedValues;
    std::vector<int> zIndices;
    std::vector<float> zDistances;
    std::vector<float> zIndices_f;

    bool render_enabled;
    bool myShDataPresent;
    bool can_render;

    bool isRenderStateRegistryCurrent();

    unsigned int closestSqrtPowerOf2(const int n);

    bool isSignificantMovement2(const UT_Vector3F& newPos, const UT_Vector3F& oldPos, const float threshold = 0.0f);

    bool argsortByDistance2(const UT_Vector3F *posSplatPointsData, const UT_Vector3F &ref_pos, const int pointCount);

public:
    // Public method to access the singleton instance
    static GSplatRenderer& getInstance() {
        static GSplatRenderer instance;
        return instance;
    }

    void update(
        const GT_PrimitiveHandle primHandle, 
        const RE_CacheVersion cacheVersion, 
        const GA_Size splatCount, 
        const UT_Vector3HArray& splatPts, 
        const UT_Vector3HArray& splatColors,
        const UT_FloatArray& splatAlphas,
        const UT_Vector3HArray& splatScales,
        const UT_Vector4HArray& splatOrients,
        const MyUT_Matrix4HArray& splatShxs,
        const MyUT_Matrix4HArray& splatShys,
        const MyUT_Matrix4HArray& splatShzs);

    void includeInRenderPass(GT_PrimitiveHandle primHandle);
    void deregisterPrimitive(GT_PrimitiveHandle primHandle);

    void generateRenderGeometry(RE_RenderContext r);

    //void render(RE_Render *r);
    void render(RE_RenderContext r);

    void postRender();

    void purgeUnused();

    void pprint();

    void setRenderingEnabled(bool render_enabled);
};


#endif // __GSPLAT_RENDERER__
