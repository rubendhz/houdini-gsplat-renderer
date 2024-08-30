/***************************************************************************************/
/*  Filename: DM_GSplatHook.C                                                          */
/*  Description: Custom Render hook class for GSplat rendering                         */
/*                                                                                     */
/*  Copyright (C) 2024 Ruben Diaz                                                      */
/*                                                                                     */
/*  License: AGPL-3.0-or-later                                                         */
/*           https://github.com/rubendhz/houdini-gsplat-renderer/blob/develop/LICENSE  */
/***************************************************************************************/


#ifndef __DM_GSPLAT_HOOK__
#define __DM_GSPLAT_HOOK__

#include <SOP/SOP_Node.h>

#include <DM/DM_RenderTable.h>
#include <DM/DM_SceneHook.h>
#include <RE/RE_Geometry.h>

#include "GSplatRenderer.h"

#include <iostream>

class MyCustomSceneRenderHook : public DM_SceneRenderHook {
public:
    MyCustomSceneRenderHook(DM_VPortAgent &vport, DM_ViewportType view_mask)
        : DM_SceneRenderHook(vport, view_mask) {}

    virtual bool render(RE_RenderContext r, const DM_SceneHookData &hook_data) override {

        GSplatRenderer::getInstance().generateRenderGeometry(r);

        GSplatRenderer::getInstance().render(r);

        GSplatRenderer::getInstance().postRender();

        return true;
    }
};

class MyCustomSceneHook : public DM_SceneHook 
{
public:
    MyCustomSceneHook(const char* hook_name, int priority)
        : DM_SceneHook(hook_name, priority, DM_HOOK_ALL_VIEWS) 
        {

        }

    virtual DM_SceneRenderHook* newSceneRender(DM_VPortAgent& vport,
                                               DM_SceneHookType type,
                                               DM_SceneHookPolicy policy) override 
    {
        return new MyCustomSceneRenderHook(vport, DM_VIEWPORT_ALL);
    }

    virtual void retireSceneRender(DM_VPortAgent& vport,
                                   DM_SceneRenderHook* hook) override 
    {
        delete hook;
    }
};


void newRenderHook(DM_RenderTable* table) 
{
    // Create and register the scene hook that will create render hooks as needed
    table->registerSceneHook(new MyCustomSceneHook("GSplat_RenderSceneHook", INT_MAX),
                             DM_HOOK_BEAUTY, DM_HOOK_AFTER_NATIVE);

    // TODO: should it be DM_HOOK_BEAUTY_TRANSPARENT instead of DM_HOOK_BEAUTY?
}


#endif // __DM_GSPLAT_HOOK__
