/***************************************************************************************/
/*  Filename: GR_GSplat.h                                                              */
/*  Description: Custom Rendering class for GSplat primitives                          */
/*                                                                                     */
/*  Copyright (C) 2024 Ruben Diaz                                                      */
/*                                                                                     */
/*  License: AGPL-3.0-or-later                                                         */
/*           https://github.com/rubendhz/houdini-gsplat-renderer/blob/develop/LICENSE  */
/***************************************************************************************/


#ifndef __GR_GSPLAT__
#define __GR_GSPLAT__

#include <GUI/GUI_PrimitiveHook.h>
#include <GR/GR_Primitive.h>

#include "GEO_GSplat.h"

/// The primitive render hook which creates GR_PrimGsplat objects.
class GR_PrimGsplatHook : public GUI_PrimitiveHook
{
public:
	GR_PrimGsplatHook();
    ~GR_PrimGsplatHook() override;

    /// This is called when a new GR_Primitive is required for a Gsplat.
    /// gt_prim or geo_prim contains the GT or GEO primitive this object is being
    /// created for, depending on whether this hook is registered to capture
    /// GT or GEO primitives.
    /// info and cache_name should be passed down to the GR_Primitive
    /// constructor.
    /// processed should return GR_PROCESSED or GR_PROCESSED_NON_EXCLUSIVE if
    /// a primitive is created. Non-exclusive allows other render hooks or the
    /// native Houdini primitives to be created for the same primitive, which is
    /// useful for support hooks (drawing decorations, bounding boxes, etc). 
    GR_Primitive        *createPrimitive(const GT_PrimitiveHandle &gt_prim,
					 const GEO_Primitive *geo_prim,
					 const GR_RenderInfo *info,
					 const char *cache_name,
					 GR_PrimAcceptResult &processed
                                         ) override;
};
    
/// Primitive object that is created by GR_PrimGsplatHook whenever a
/// Gsplat primitive is found. This object can be persistent between
/// renders, though display flag changes or navigating though SOPs can cause
/// it to be deleted and recreated later.
class GR_PrimGsplat : public GR_Primitive
{
private:
	std::string myRegistryId;

public:
	GR_PrimGsplat(const GR_RenderInfo *info,
					const char *cache_name,
					const GEO_Primitive *prim);
	~GR_PrimGsplat() override;

	const char  *className() const override { return "GR_PrimGsplat"; }

	/// See if the Gsplat primitive can be consumed by this primitive. Only
	/// Gsplat from the same detail will ever be passed in. 
	GR_PrimAcceptResult acceptPrimitive(GT_PrimitiveType t,
					int geo_type,
					const GT_PrimitiveHandle &ph,
					const GEO_Primitive *prim) override;

	/// Called whenever the parent detail is changed, draw modes are changed,
	/// selection is changed, or certain volatile display options are changed
	/// (such as level of detail).
	void                update(RE_RenderContext                 r,
					const GT_PrimitiveHandle  &primh,
					const GR_UpdateParms	 &p) override;

	/// Called whenever the primitive is required to render, which may be more
	/// than one time per viewport redraw (beauty, shadow passes, wireframe-over)
	/// It also may be called outside of a viewport redraw to do picking of the
	/// geometry.
	void                render(RE_RenderContext              r,
					GR_RenderMode	       render_mode,
					GR_RenderFlags	       flags,
					GR_DrawParms	       dp) override;
	void                renderDecoration(
								RE_RenderContext r,
				GR_Decoration decor,
				const GR_DecorationParms &parms) override;
	int                 renderPick(RE_RenderContext r,
					const GR_DisplayOption *opt,
					unsigned int pick_type,
					GR_PickStyle pick_style,
					bool has_pick_map) override;

private:
	struct SHHandles {
		GA_ROHandleV3 sh[16];
		GA_ROHandleF sh_fallback[45];
		bool fallback;
		bool valid;
	};

	bool initSHHandle(const GU_Detail *gdp, SHHandles& handles, const char* name, int index);
	bool initSHHandleFallback(const GU_Detail *gdp, SHHandles& handles, const char* name, int index);
	bool initAllSHHandles(const GU_Detail *gdp, SHHandles& handles);

	int	myID;

	std::string myGsplatStrId;
	RE_Geometry *myWireframeGeo;
	GA_Size myGsplatCount;
	UT_Vector3Array mySplatPts;
	UT_Vector3HArray mySplatColors;
	UT_FloatArray mySplatAlphas; //TODO: make 16 bit like the rest?
	UT_Vector3HArray mySplatScales;
	UT_Vector4HArray mySplatOrients;
	MyUT_Matrix4HArray myShxs;
	MyUT_Matrix4HArray myShys;
	MyUT_Matrix4HArray myShzs;

	bool mySetExplicitCameraPos;
	UT_Vector3 myExplicitCameraPos;

	int myShOrder;
};


#endif // __GR_GSPLAT__
